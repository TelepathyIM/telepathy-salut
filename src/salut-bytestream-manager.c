/*
 * salut-bytestream-manager.c - Source for SalutBytestreamManager
 * Copyright (C) 2007 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "salut-bytestream-manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gibber/gibber-bytestream-ibb.h>
#include <gibber/gibber-bytestream-oob.h>
#include <gibber/gibber-xmpp-stanza.h>
#include <gibber/gibber-namespaces.h>
#include <gibber/gibber-xmpp-error.h>
#include <gibber/gibber-iq-helper.h>

#include "salut-im-manager.h"
#include "salut-muc-manager.h"
#include "salut-tubes-manager.h"

#define DEBUG_FLAG DEBUG_BYTESTREAM_MGR
#include "debug.h"

G_DEFINE_TYPE (SalutBytestreamManager, salut_bytestream_manager, G_TYPE_OBJECT)

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_HOST_NAME_FQDN,
  LAST_PROPERTY
};

/* private structure */
typedef struct _SalutBytestreamManagerPrivate SalutBytestreamManagerPrivate;

struct _SalutBytestreamManagerPrivate
{
  SalutConnection *connection;
  SalutImManager *im_manager;
  SalutMucManager *muc_manager;
  SalutXmppConnectionManager *xmpp_connection_manager;
  gchar *host_name_fqdn;

  gboolean dispose_has_run;
};

#define SALUT_BYTESTREAM_MANAGER_GET_PRIVATE(obj) \
    ((SalutBytestreamManagerPrivate *) obj->priv)

static void
salut_bytestream_manager_init (SalutBytestreamManager *self)
{
  SalutBytestreamManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_BYTESTREAM_MANAGER, SalutBytestreamManagerPrivate);

  self->priv = priv;

  priv->dispose_has_run = FALSE;
}

/* Filter for SI request (XEP-0095) */
static gboolean
si_request_filter (SalutXmppConnectionManager *xcm,
                   GibberXmppConnection *conn,
                   GibberXmppStanza *stanza,
                   SalutContact *contact,
                   gpointer user_data)
{
  GibberStanzaType type;
  GibberStanzaSubType sub_type;

  gibber_xmpp_stanza_get_type_info (stanza, &type, &sub_type);
  if (type != GIBBER_STANZA_TYPE_IQ)
    return FALSE;

  if (sub_type != GIBBER_STANZA_SUB_TYPE_SET)
    return FALSE;

  return (gibber_xmpp_node_get_child_ns (stanza->node, "si",
        GIBBER_XMPP_NS_SI) != NULL);
}

static gboolean
streaminit_parse_request (GibberXmppStanza *stanza,
                          const gchar **profile,
                          const gchar **from,
                          const gchar **stream_id,
                          const gchar **stream_init_id,
                          const gchar **mime_type,
                          GSList **stream_methods)
{
  GibberXmppNode *iq, *si, *feature, *x;
  GSList *x_children, *field_children;

  iq = stanza->node;

  *stream_init_id = gibber_xmpp_node_get_attribute (iq, "id");

  *from = gibber_xmpp_node_get_attribute (stanza->node, "from");
  if (*from == NULL)
    {
      DEBUG ("got a message without a from field");
      return FALSE;
    }

  /* Parse <si> */
  si = gibber_xmpp_node_get_child_ns (iq, "si", GIBBER_XMPP_NS_SI);
  if (si == NULL)
    return FALSE;

  *stream_id = gibber_xmpp_node_get_attribute (si, "id");
  if (*stream_id == NULL)
    {
      DEBUG ("got a SI request without a stream id field");
      return FALSE;
    }

  *mime_type = gibber_xmpp_node_get_attribute (si, "mime-type");
  /* if no mime_type is defined, XEP-0095 says to assume
   * "application/octet-stream" */

  *profile = gibber_xmpp_node_get_attribute (si, "profile");
  if (*profile == NULL)
    {
      DEBUG ("got a SI request without a profile field");
      return FALSE;
    }

  /* Parse <feature> */
  feature = gibber_xmpp_node_get_child_ns (si, "feature",
      GIBBER_XMPP_NS_FEATURENEG);
  if (feature == NULL)
    {
      DEBUG ("got a SI request without a feature field");
      return FALSE;
    }

  x = gibber_xmpp_node_get_child_ns (feature, "x", GIBBER_XMPP_NS_DATA);
  if (x == NULL)
    {
      DEBUG ("got a SI request without a X data field");
      return FALSE;
    }

  for (x_children = x->children; x_children;
      x_children = g_slist_next (x_children))
    {
      GibberXmppNode *field = x_children->data;

      if (tp_strdiff (gibber_xmpp_node_get_attribute (field, "var"),
            "stream-method"))
        /* some future field, ignore it */
        continue;

      if (tp_strdiff (gibber_xmpp_node_get_attribute (field, "type"),
            "list-single"))
        {
          DEBUG ( "SI request's stream-method field was "
              "not of type list-single");
          return FALSE;
        }

      /* Get the stream methods offered */
      *stream_methods = NULL;
      for (field_children = field->children; field_children;
          field_children = g_slist_next (field_children))
        {
          GibberXmppNode *stream_method, *value;
          const gchar *stream_method_str;

          stream_method = (GibberXmppNode *) field_children->data;

          value = gibber_xmpp_node_get_child (stream_method, "value");
          if (value == NULL)
            continue;

          stream_method_str = value->content;
          if (!tp_strdiff (stream_method_str, ""))
            continue;

          DEBUG ("Got stream-method %s", stream_method_str);

          /* Append to the stream_methods list */
          *stream_methods = g_slist_append (*stream_methods,
              (gchar *) stream_method_str);
        }

      /* no need to parse the rest of the fields, we've found the one we
       * wanted */
      break;
    }

  if (*stream_methods == NULL)
    {
      DEBUG ("got a SI request without stream method proposed");
      return FALSE;
    }

  return TRUE;
}

static void
bytestream_state_changed (GibberBytestreamIface *bytestream,
                          GibberBytestreamState state,
                          SalutXmppConnectionManager *mgr)
{
  if (state == GIBBER_BYTESTREAM_STATE_CLOSED)
    {
      GibberXmppConnection *connection;

      DEBUG ("bytestream closed, release the connection");
      g_object_get (bytestream, "xmpp-connection", &connection, NULL);

      salut_xmpp_connection_manager_release_connection (mgr, connection);

      g_object_unref (connection);
      g_object_unref (bytestream);
    }
}

GibberBytestreamIface *
choose_bytestream_method (SalutBytestreamManager *self,
                          GSList *stream_methods,
                          GibberXmppConnection *connection,
                          SalutContact *contact,
                          const gchar *stream_id,
                          const gchar *stream_init_id)
{
  SalutBytestreamManagerPrivate *priv =
    SALUT_BYTESTREAM_MANAGER_GET_PRIVATE (self);
  GSList *l;

  /* We create the stream according the stream method chosen.
   * User has to accept it */

  /* check OOB */
  for (l = stream_methods; l != NULL; l = l->next)
    {
      if (!tp_strdiff (l->data, GIBBER_XMPP_NS_OOB))
        {
          DEBUG ("choose OOB in methods list");
          return g_object_new (GIBBER_TYPE_BYTESTREAM_OOB,
              "xmpp-connection", connection,
              "stream-id", stream_id,
              "state", GIBBER_BYTESTREAM_STATE_LOCAL_PENDING,
              "self-id", priv->connection->name,
              "peer-id", contact->name,
              "stream-init-id", stream_init_id,
              NULL);
        }
    }

  /* check IBB */
  for (l = stream_methods; l != NULL; l = l->next)
    {
      if (!tp_strdiff (l->data, GIBBER_XMPP_NS_IBB))
        {
          DEBUG ("choose IBB in methods list");
          return g_object_new (GIBBER_TYPE_BYTESTREAM_IBB,
              "xmpp-connection", connection,
              "stream-id", stream_id,
              "state", GIBBER_BYTESTREAM_STATE_LOCAL_PENDING,
              "self-id", priv->connection->name,
              "peer-id", contact->name,
              "stream-init-id", stream_init_id,
              NULL);
        }
    }

  return NULL;
}

static void
si_request_cb (SalutXmppConnectionManager *xcm,
               GibberXmppConnection *connection,
               GibberXmppStanza *stanza,
               SalutContact *contact,
               gpointer user_data)
{
  SalutBytestreamManager *self = SALUT_BYTESTREAM_MANAGER (user_data);
  SalutBytestreamManagerPrivate *priv =
    SALUT_BYTESTREAM_MANAGER_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->connection, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->connection, TP_HANDLE_TYPE_ROOM);
  TpHandle peer_handle;
  GibberBytestreamIface *bytestream = NULL;
  GibberXmppNode *si, *node;
  const gchar *profile, *from, *stream_id, *stream_init_id, *mime_type;
  GSList *stream_methods = NULL;

   /* after this point, the message is for us, so in all cases we either handle
   * it or send an error reply */

  if (!streaminit_parse_request (stanza, &profile, &from, &stream_id,
        &stream_init_id, &mime_type, &stream_methods))
    {
      GibberXmppStanza *reply;

      reply = gibber_iq_helper_new_error_reply (stanza, XMPP_ERROR_BAD_REQUEST,
          "failed to parse SI request");
      gibber_xmpp_connection_send (connection, reply, NULL);

      g_object_unref (reply);
      return;
    }

  si = gibber_xmpp_node_get_child_ns (stanza->node, "si", GIBBER_XMPP_NS_SI);
  g_assert (si != NULL);

  DEBUG ("received a SI request");

  peer_handle = tp_handle_lookup (contact_repo, from, NULL, NULL);
  if (peer_handle == 0)
    {
      goto out;
    }

  /* check stream method */
  bytestream = choose_bytestream_method (self, stream_methods, connection,
      contact, stream_id, stream_init_id);

  if (bytestream == NULL)
    {
      GibberXmppStanza *reply;

      DEBUG ("SI request doesn't contain any supported stream method.");
      reply = gibber_iq_helper_new_error_reply (stanza,
          XMPP_ERROR_SI_NO_VALID_STREAMS, NULL);

      gibber_xmpp_connection_send (connection, reply, NULL);

      g_object_unref (reply);
      goto out;
    }

  /* Now that we have a bytestream, it's responsible for declining the IQ
   * if needed. */

  /* As bytestreams are not XCM aware, they can't take/release
   * the connection so we do it for them.
   * We'll release it when the bytestream will be closed */
  salut_xmpp_connection_manager_take_connection (priv->xmpp_connection_manager,
      connection);

  g_signal_connect (bytestream, "state-changed",
     G_CALLBACK (bytestream_state_changed), priv->xmpp_connection_manager);

  /* We inform the right manager we received a SI request */
  if (tp_strdiff (profile, GIBBER_TELEPATHY_NS_TUBES))
    {
      GError e = { GIBBER_XMPP_ERROR, XMPP_ERROR_SI_BAD_PROFILE, "" };
      DEBUG ("SI profile unsupported: %s", profile);

      gibber_bytestream_iface_close (bytestream, &e);
      goto out;
    }

  /* A Tubes SI request can be:
   *  - a 1-1 new tube offer
   *  - a 1-1 tube extra bytestream offer
   *  - a muc tube extra bytestream offer
   */

  if ((node = gibber_xmpp_node_get_child_ns (si, "muc-stream",
          GIBBER_TELEPATHY_NS_TUBES)))
    {
      const gchar *muc;
      TpHandle room_handle;
      SalutMucManager *muc_mgr;

      muc = gibber_xmpp_node_get_attribute (node, "muc");
      if (muc == NULL)
        {
          DEBUG ("muc-stream SI doesn't contain muc attribute");
          gibber_bytestream_iface_close (bytestream, NULL);
          goto out;
        }

      room_handle = tp_handle_lookup (room_repo, muc, NULL, NULL);
      if (room_handle == 0)
        {
          DEBUG ("Unknown room: %s\n", muc);
          gibber_bytestream_iface_close (bytestream, NULL);
          goto out;
        }

      g_object_get (priv->connection, "muc-manager", &muc_mgr, NULL);
      g_assert (muc_mgr != NULL);

      salut_muc_manager_handle_si_stream_request (muc_mgr,
          bytestream, room_handle, stream_id, stanza);
      g_object_unref (muc_mgr);
    }
  else if ((node = gibber_xmpp_node_get_child_ns (si, "stream",
          GIBBER_TELEPATHY_NS_TUBES)))
    {
      SalutTubesManager *tubes_mgr;

      g_object_get (priv->connection, "tubes-manager", &tubes_mgr, NULL);
      g_assert (tubes_mgr != NULL);

      /* The SI request is an extra bytestream for a 1-1 tube */
      salut_tubes_manager_handle_si_stream_request (
          tubes_mgr, bytestream, peer_handle, stream_id, stanza);

      g_object_unref (tubes_mgr);
    }
  else
    {
      GError e = { GIBBER_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "Invalid tube SI request: expected <tube>, <stream> or "
          "<muc-stream>" };

      DEBUG ("Invalid tube SI request");
      gibber_bytestream_iface_close (bytestream, &e);
      goto out;
    }

out:
  g_slist_free (stream_methods);
  return;
}

void
salut_bytestream_manager_dispose (GObject *object)
{
  SalutBytestreamManager *self = SALUT_BYTESTREAM_MANAGER (object);
  SalutBytestreamManagerPrivate *priv = SALUT_BYTESTREAM_MANAGER_GET_PRIVATE (
      self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_object_unref (priv->im_manager);
  g_object_unref (priv->muc_manager);
  g_object_unref (priv->xmpp_connection_manager);

  if (G_OBJECT_CLASS (salut_bytestream_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_bytestream_manager_parent_class)->dispose (object);
}

void
salut_bytestream_manager_finalize (GObject *object)
{
  SalutBytestreamManager *self = SALUT_BYTESTREAM_MANAGER (object);
  SalutBytestreamManagerPrivate *priv = SALUT_BYTESTREAM_MANAGER_GET_PRIVATE (
      self);

  g_free (priv->host_name_fqdn);

  if (G_OBJECT_CLASS (salut_bytestream_manager_parent_class)->finalize)
    G_OBJECT_CLASS (salut_bytestream_manager_parent_class)->finalize (object);
}

static void
salut_bytestream_manager_get_property (GObject *object,
                                       guint property_id,
                                       GValue *value,
                                       GParamSpec *pspec)
{
  SalutBytestreamManager *self = SALUT_BYTESTREAM_MANAGER (object);
  SalutBytestreamManagerPrivate *priv = SALUT_BYTESTREAM_MANAGER_GET_PRIVATE (
      self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->connection);
        break;
      case PROP_HOST_NAME_FQDN:
        g_value_set_string (value, priv->host_name_fqdn);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_bytestream_manager_set_property (GObject *object,
                                       guint property_id,
                                       const GValue *value,
                                       GParamSpec *pspec)
{
  SalutBytestreamManager *self = SALUT_BYTESTREAM_MANAGER (object);
  SalutBytestreamManagerPrivate *priv = SALUT_BYTESTREAM_MANAGER_GET_PRIVATE (
      self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->connection = g_value_get_object (value);
        break;
      case PROP_HOST_NAME_FQDN:
        g_free (priv->host_name_fqdn);
        priv->host_name_fqdn = g_value_dup_string (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
salut_bytestream_manager_constructor (GType type,
                                      guint n_props,
                                      GObjectConstructParam *props)
{
  GObject *obj;
  SalutBytestreamManager *self;
  SalutBytestreamManagerPrivate *priv;

  obj = G_OBJECT_CLASS (salut_bytestream_manager_parent_class)->
           constructor (type, n_props, props);

  self = SALUT_BYTESTREAM_MANAGER (obj);
  priv = SALUT_BYTESTREAM_MANAGER_GET_PRIVATE (self);

  g_assert (priv->connection != NULL);
  g_object_get (priv->connection,
      "im-manager", &(priv->im_manager),
      "muc-manager", &(priv->muc_manager),
      "xmpp-connection-manager", &(priv->xmpp_connection_manager),
      NULL);
  g_assert (priv->im_manager != NULL);
  g_assert (priv->muc_manager != NULL);
  g_assert (priv->xmpp_connection_manager != NULL);
  g_assert (priv->host_name_fqdn != NULL);

  salut_xmpp_connection_manager_add_stanza_filter (
      priv->xmpp_connection_manager, NULL, si_request_filter,
      si_request_cb, self);

  return obj;
}

static void
salut_bytestream_manager_class_init (
    SalutBytestreamManagerClass *salut_bytestream_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_bytestream_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_bytestream_manager_class,
      sizeof (SalutBytestreamManagerPrivate));

  object_class->constructor = salut_bytestream_manager_constructor;
  object_class->dispose = salut_bytestream_manager_dispose;
  object_class->finalize = salut_bytestream_manager_finalize;

  object_class->get_property = salut_bytestream_manager_get_property;
  object_class->set_property = salut_bytestream_manager_set_property;

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "Salut Connection that owns the connection for this bytestream channel",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string (
      "host-name-fqdn",
      "host name FQDN",
      "The FQDN host name that will be used by OOB bytestreams",
      NULL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HOST_NAME_FQDN,
      param_spec);
}

SalutBytestreamManager *
salut_bytestream_manager_new (SalutConnection *conn,
                              const gchar *host_name_fqdn)
{
  g_return_val_if_fail (SALUT_IS_CONNECTION (conn), NULL);

  return g_object_new (
      SALUT_TYPE_BYTESTREAM_MANAGER,
      "connection", conn,
      "host-name-fqdn", host_name_fqdn,
      NULL);
}

/**
 * salut_bytestream_manager_make_stream_init_iq
 *
 * @from: your contact
 * @to: the contact to who you want to offer the stream
 * @stream_id: the stream ID of the new stream
 * @profile: the profile associated with the stream
 *
 * Create a SI request IQ as described in XEP-0095.
 *
 */
GibberXmppStanza *
salut_bytestream_manager_make_stream_init_iq (const gchar *from,
                                              const gchar *to,
                                              const gchar *stream_id,
                                              const gchar *profile)
{
  return gibber_xmpp_stanza_build (
      GIBBER_STANZA_TYPE_IQ, GIBBER_STANZA_SUB_TYPE_SET,
      from, to,
      GIBBER_NODE, "si",
        GIBBER_NODE_XMLNS, GIBBER_XMPP_NS_SI,
        GIBBER_NODE_ATTRIBUTE, "id", stream_id,
        GIBBER_NODE_ATTRIBUTE, "profile", profile,
        GIBBER_NODE_ATTRIBUTE, "mime-type", "application/octet-stream",
        GIBBER_NODE, "feature",
          GIBBER_NODE_XMLNS, GIBBER_XMPP_NS_FEATURENEG,
          GIBBER_NODE, "x",
            GIBBER_NODE_XMLNS, GIBBER_XMPP_NS_DATA,
            GIBBER_NODE_ATTRIBUTE, "type", "form",
            GIBBER_NODE, "field",
              GIBBER_NODE_ATTRIBUTE, "var", "stream-method",
              GIBBER_NODE_ATTRIBUTE, "type", "list-single",

              GIBBER_NODE, "option",
                GIBBER_NODE, "value",
                  GIBBER_NODE_TEXT, GIBBER_XMPP_NS_OOB,
                GIBBER_NODE_END,
              GIBBER_NODE_END,

              GIBBER_NODE, "option",
                GIBBER_NODE, "value",
                  GIBBER_NODE_TEXT, GIBBER_XMPP_NS_IBB,
                GIBBER_NODE_END,
              GIBBER_NODE_END,

            GIBBER_NODE_END,
          GIBBER_NODE_END,
        GIBBER_NODE_END,
      GIBBER_NODE_END, GIBBER_STANZA_END);
}

struct streaminit_reply_cb_data
{
  SalutBytestreamManager *self;
  gchar *stream_id;
  SalutBytestreamManagerNegotiateReplyFunc func;
  gpointer user_data;
  gchar *iq_id;
  SalutContact *contact;
  GibberXmppStanza *stanza;
};

static struct streaminit_reply_cb_data *
streaminit_reply_cb_data_new (void)
{
  return g_slice_new0 (struct streaminit_reply_cb_data);
}

static void
streaminit_reply_cb_data_free (struct streaminit_reply_cb_data *data)
{
  g_free (data->stream_id);
  g_free (data->iq_id);

  if (data->contact != NULL)
    g_object_unref (data->contact);

  if (data->stanza != NULL)
    g_object_unref (data->stanza);

  g_slice_free (struct streaminit_reply_cb_data, data);
}

static gboolean
si_request_reply_filter (SalutXmppConnectionManager *manager,
                         GibberXmppConnection *connection,
                         GibberXmppStanza *stanza,
                         SalutContact *contact,
                         gpointer user_data)
{
  struct streaminit_reply_cb_data *data =
    (struct streaminit_reply_cb_data *) user_data;
  GibberStanzaType type;
  GibberStanzaSubType sub_type;
  const gchar *iq_id;

  gibber_xmpp_stanza_get_type_info (stanza, &type, &sub_type);
  if (type != GIBBER_STANZA_TYPE_IQ)
    return FALSE;

  if (sub_type != GIBBER_STANZA_SUB_TYPE_RESULT &&
      sub_type != GIBBER_STANZA_SUB_TYPE_ERROR)
    return FALSE;

  iq_id = gibber_xmpp_node_get_attribute (stanza->node, "id");
  return (!tp_strdiff (iq_id, data->iq_id));
}

static gboolean
check_bytestream_oob_peer_addr (GibberBytestreamOOB *bytestream,
                                struct sockaddr_storage *addr,
                                socklen_t addrlen,
                                gpointer user_data)
{
  SalutBytestreamManager *self = SALUT_BYTESTREAM_MANAGER (user_data);
  SalutBytestreamManagerPrivate *priv = SALUT_BYTESTREAM_MANAGER_GET_PRIVATE (
      self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->connection, TP_HANDLE_TYPE_CONTACT);
  TpHandle handle;
  SalutContactManager *contact_mgr;
  SalutContact *contact;
  gchar *peer;
  gboolean result;

  g_object_get (bytestream, "peer-id", &peer, NULL);
  g_assert (peer != NULL);

  handle = tp_handle_lookup (contact_repo, peer, NULL, NULL);
  g_assert (handle != 0);
  g_free (peer);

  g_object_get (priv->connection, "contact-manager", &contact_mgr, NULL);
  g_assert (contact_mgr != NULL);

  contact = salut_contact_manager_get_contact (contact_mgr, handle);
  g_object_unref (contact_mgr);
  if (contact == NULL)
    return FALSE;

  result = salut_contact_has_address (contact, addr);
  g_object_unref (contact);

  return result;
}

static void
si_request_reply_cb (SalutXmppConnectionManager *manager,
                     GibberXmppConnection *connection,
                     GibberXmppStanza *stanza,
                     SalutContact *contact,
                     gpointer user_data)
{
  struct streaminit_reply_cb_data *data =
    (struct streaminit_reply_cb_data *) user_data;
  SalutBytestreamManagerPrivate *priv =
    SALUT_BYTESTREAM_MANAGER_GET_PRIVATE (data->self);
  GibberStanzaSubType sub_type;
  GibberXmppNode *si, *feature, *x;
  GibberBytestreamIface *bytestream = NULL;
  const gchar *from, *stream_method, *stream_init_id;
  GSList *x_children;

  salut_xmpp_connection_manager_remove_stanza_filter (
      manager, connection, si_request_reply_filter, si_request_reply_cb, data);

  DEBUG ("received SI request response");

  gibber_xmpp_stanza_get_type_info (stanza, NULL, &sub_type);
  if (sub_type != GIBBER_STANZA_SUB_TYPE_RESULT)
    {
      DEBUG ("stream %s declined", data->stream_id);
      goto END;
    }

  /* stream accepted */
  stream_init_id = gibber_xmpp_node_get_attribute (stanza->node, "id");

  from = gibber_xmpp_node_get_attribute (stanza->node, "from");
  if (from == NULL)
    {
      DEBUG ("got a message without a from field");
      goto END;
    }

  si = gibber_xmpp_node_get_child_ns (stanza->node, "si",
      GIBBER_XMPP_NS_SI);
  if (si == NULL)
    {
      DEBUG ("got a SI reply without a si field");
      goto END;
    }

  feature = gibber_xmpp_node_get_child_ns (si, "feature",
      GIBBER_XMPP_NS_FEATURENEG);
  if (feature == NULL)
    {
      DEBUG ("got a SI reply without a feature field");
      goto END;
    }

  x = gibber_xmpp_node_get_child_ns (feature, "x", GIBBER_XMPP_NS_DATA);
  if (x == NULL)
    {
      DEBUG ("got a SI reply without a x field");
      goto END;
    }

  for (x_children = x->children; x_children;
      x_children = g_slist_next (x_children))
    {
      GibberXmppNode *value, *field = x_children->data;

      if (tp_strdiff (gibber_xmpp_node_get_attribute (field, "var"),
            "stream-method"))
        /* some future field, ignore it */
        continue;

      value = gibber_xmpp_node_get_child (field, "value");
      if (value == NULL)
        {
          DEBUG ("SI reply's stream-method field "
              "doesn't contain stream-method value");
          goto END;
        }

      stream_method = value->content;

      if (!tp_strdiff (stream_method, GIBBER_XMPP_NS_OOB))
      {
        /* Remote user have accepted the stream */
        DEBUG ("remote user chose a OOB bytestream");
        bytestream = g_object_new (GIBBER_TYPE_BYTESTREAM_OOB,
              "xmpp-connection", connection,
              "stream-id", data->stream_id,
              "state", GIBBER_BYTESTREAM_STATE_INITIATING,
              "self-id", priv->connection->name,
              "peer-id", from,
              "stream-init-id", NULL,
              "host", priv->host_name_fqdn,
              NULL);
        gibber_bytestream_oob_set_check_addr_func (
            GIBBER_BYTESTREAM_OOB (bytestream), check_bytestream_oob_peer_addr,
            data->self);
      }
    else if (!tp_strdiff (stream_method, GIBBER_XMPP_NS_IBB))
      {
        /* Remote user have accepted the stream */
        DEBUG ("remote user chose a IBB bytestream");
        bytestream = g_object_new (GIBBER_TYPE_BYTESTREAM_IBB,
              "xmpp-connection", connection,
              "stream-id", data->stream_id,
              "state", GIBBER_BYTESTREAM_STATE_INITIATING,
              "self-id", priv->connection->name,
              "peer-id", from,
              "stream-init-id", NULL,
              NULL);
      }
    else
      {
        DEBUG ("Remote user chose an unsupported stream method");
        goto END;
      }

      /* no need to parse the rest of the fields, we've found the one we
       * wanted */
      break;

    }

  if (bytestream == NULL)
    goto END;

  DEBUG ("stream %s accepted. Start to initiate it", data->stream_id);

  /* As bytestreams are not XCM aware, they can't take/release
   * the connection so we do it for them.
   * We'll release it when the bytestream will be closed */
  salut_xmpp_connection_manager_take_connection (priv->xmpp_connection_manager,
      connection);

  g_signal_connect (bytestream, "state-changed",
     G_CALLBACK (bytestream_state_changed), priv->xmpp_connection_manager);

  /* Let's start the initiation of the stream */
  if (!gibber_bytestream_iface_initiate (bytestream))
    {
      /* Initiation failed. */
      gibber_bytestream_iface_close (bytestream, NULL);
      bytestream = NULL;
    }

END:
  /* user callback */
  data->func (bytestream, data->stream_id, stanza,
      data->user_data);

  streaminit_reply_cb_data_free (data);
}

static gboolean
send_si_request (SalutBytestreamManager *self,
                 GibberXmppConnection *connection,
                 struct streaminit_reply_cb_data *data,
                 GError **error)
{
  SalutBytestreamManagerPrivate *priv =
    SALUT_BYTESTREAM_MANAGER_GET_PRIVATE (self);
  const gchar *iq_id;

  iq_id = gibber_xmpp_node_get_attribute (data->stanza->node, "id");
  if (iq_id != NULL)
    {
      data->iq_id = g_strdup (iq_id);
    }
  else
    {
      data->iq_id = gibber_xmpp_connection_new_id (connection);
      gibber_xmpp_node_set_attribute (data->stanza->node, "id", data->iq_id);
    }

  /* Register a filter to catch the response of the SI request */
  salut_xmpp_connection_manager_add_stanza_filter (
      priv->xmpp_connection_manager, connection,
      si_request_reply_filter, si_request_reply_cb, data);

  /* FIXME: set a timer ?
   * Or we could listen for the XMPP connection closed signal and so use
   * XCM's timer as we didn't ref the connection yet */
  if (!gibber_xmpp_connection_send (connection, data->stanza, error))
    {
      salut_xmpp_connection_manager_remove_stanza_filter (
          priv->xmpp_connection_manager, connection,
          si_request_reply_filter, si_request_reply_cb, data);

      streaminit_reply_cb_data_free (data);
      return FALSE;
    }

  return TRUE;
}

static void
xmpp_connection_manager_new_connection_cb (SalutXmppConnectionManager *mgr,
                                           GibberXmppConnection *connection,
                                           SalutContact *contact,
                                           gpointer user_data)
{
  struct streaminit_reply_cb_data *data =
    (struct streaminit_reply_cb_data *) user_data;

  if (data->contact != contact)
    /* Not the connection we are waiting for */
    return;

  DEBUG ("got connection with %s. Send SI request", contact->name);
  send_si_request (data->self, connection, data, NULL);

  g_signal_handlers_disconnect_matched (mgr, G_SIGNAL_MATCH_DATA, 0, 0, NULL,
      NULL, data);
}

static void
xmpp_connection_manager_connection_failed_cb (SalutXmppConnectionManager *mgr,
                                              GibberXmppConnection *connection,
                                              SalutContact *contact,
                                              GQuark domain,
                                              gint code,
                                              gchar *message,
                                              gpointer user_data)
{
  struct streaminit_reply_cb_data *data =
    (struct streaminit_reply_cb_data *) user_data;

  if (data->contact != contact)
    /* Not the connection we are waiting for */
    return;

  DEBUG ("connection with %s failed: %s. Can't send SI request", contact->name,
      message);

 /* Call the user callback without bytestream to inform him the SI request
  * failed */
  data->func (NULL, data->stream_id, NULL,
      data->user_data);

  g_signal_handlers_disconnect_matched (mgr, G_SIGNAL_MATCH_DATA, 0, 0, NULL,
      NULL, data);
  streaminit_reply_cb_data_free (data);
}

/*
 * salut_bytestream_manager_negotiate_stream:
 *
 * @contact: the contact to who send the SI request
 * @stanza: the SI negotiation IQ (created using
 * salut_bytestream_manager_make_stream_init_iq)
 * @stream_id: the stream identifier
 * @func: the callback to call when we receive the answser of the request
 * @user_data: user data to pass to the callback
 * @error: pointer in which to return a GError in case of failure.
 *
 * Send a Stream Initiation (XEP-0095) request.
 */
gboolean
salut_bytestream_manager_negotiate_stream (SalutBytestreamManager *self,
                                           SalutContact *contact,
                                           GibberXmppStanza *stanza,
                                           const gchar *stream_id,
                                           SalutBytestreamManagerNegotiateReplyFunc func,
                                           gpointer user_data,
                                           GError **error)
{
  SalutBytestreamManagerPrivate *priv;
  struct streaminit_reply_cb_data *data;
  GibberXmppConnection *connection = NULL;
  SalutXmppConnectionManagerRequestConnectionResult request_result;

  g_assert (SALUT_IS_BYTESTREAM_MANAGER (self));
  g_assert (stream_id != NULL);
  g_assert (func != NULL);

  priv = SALUT_BYTESTREAM_MANAGER_GET_PRIVATE (self);

  data = streaminit_reply_cb_data_new ();
  data->self = self;
  data->stream_id = g_strdup (stream_id);
  data->func = func;
  data->user_data = user_data;
  data->contact = g_object_ref (contact);
  data->stanza = g_object_ref (stanza);

  DEBUG ("request XMPP connection with %s to send the SI request",
      contact->name);

  /* We need a XMPP connection to send the SI request */
  request_result = salut_xmpp_connection_manager_request_connection (
      priv->xmpp_connection_manager, contact, &connection, error);

  if (request_result ==
      SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_DONE)
    {
      DEBUG ("got the connection with %s, send the SI request",
          data->contact->name);

      return send_si_request (self, connection, data, error);
    }
  else if (request_result ==
      SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_PENDING)
    {
      DEBUG ("connection with %s pending. Wait before send the SI request",
          contact->name);

      g_signal_connect (priv->xmpp_connection_manager, "new-connection",
          G_CALLBACK (xmpp_connection_manager_new_connection_cb), data);
      g_signal_connect (priv->xmpp_connection_manager, "connection-failed",
          G_CALLBACK (xmpp_connection_manager_connection_failed_cb), data);

      return TRUE;
    }
  else
    {
      DEBUG ("can't request connection with %s", contact->name);
      streaminit_reply_cb_data_free (data);
      return FALSE;
    }
}
