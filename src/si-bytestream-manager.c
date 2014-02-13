/*
 * si-bytestream-manager.c - Source for SalutSiBytestreamManager
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

#include "config.h"
#include "si-bytestream-manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gibber/gibber-bytestream-oob.h>

#include <wocky/wocky.h>

#include "im-manager.h"
#include "muc-manager.h"
#include "tubes-manager.h"

#define DEBUG_FLAG DEBUG_SI_BYTESTREAM_MGR
#include "debug.h"

G_DEFINE_TYPE (SalutSiBytestreamManager, salut_si_bytestream_manager,
    G_TYPE_OBJECT)

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_HOST_NAME_FQDN,
  LAST_PROPERTY
};

/* private structure */
typedef struct _SalutSiBytestreamManagerPrivate SalutSiBytestreamManagerPrivate;

struct _SalutSiBytestreamManagerPrivate
{
  SalutConnection *connection;
  SalutImManager *im_manager;
  SalutMucManager *muc_manager;
  gchar *host_name_fqdn;

  guint si_request_id;

  gboolean dispose_has_run;
};

#define SALUT_SI_BYTESTREAM_MANAGER_GET_PRIVATE(obj) \
    ((SalutSiBytestreamManagerPrivate *) ((SalutSiBytestreamManager *) obj)->priv)

static void
salut_si_bytestream_manager_init (SalutSiBytestreamManager *self)
{
  SalutSiBytestreamManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_SI_BYTESTREAM_MANAGER, SalutSiBytestreamManagerPrivate);

  self->priv = priv;

  priv->dispose_has_run = FALSE;
}

static gboolean
streaminit_parse_request (WockyStanza *stanza,
                          const gchar **profile,
                          const gchar **from,
                          const gchar **stream_id,
                          const gchar **stream_init_id,
                          const gchar **mime_type,
                          GSList **stream_methods)
{
  WockyNode *iq, *si, *feature, *x;
  GSList *x_children, *field_children;

  iq = wocky_stanza_get_top_node (stanza);

  if (stream_init_id != NULL)
    *stream_init_id = wocky_node_get_attribute (iq, "id");

  *from = wocky_node_get_attribute (iq, "from");
  if (*from == NULL)
    {
      DEBUG ("got a message without a from field");
      return FALSE;
    }

  /* Parse <si> */
  si = wocky_node_get_child_ns (iq, "si", WOCKY_XMPP_NS_SI);
  if (si == NULL)
    return FALSE;

  *stream_id = wocky_node_get_attribute (si, "id");
  if (*stream_id == NULL)
    {
      DEBUG ("got a SI request without a stream id field");
      return FALSE;
    }

  *mime_type = wocky_node_get_attribute (si, "mime-type");
  /* if no mime_type is defined, XEP-0095 says to assume
   * "application/octet-stream" */

  *profile = wocky_node_get_attribute (si, "profile");
  if (*profile == NULL)
    {
      DEBUG ("got a SI request without a profile field");
      return FALSE;
    }

  /* Parse <feature> */
  feature = wocky_node_get_child_ns (si, "feature",
      WOCKY_XMPP_NS_FEATURENEG);
  if (feature == NULL)
    {
      DEBUG ("got a SI request without a feature field");
      return FALSE;
    }

  x = wocky_node_get_child_ns (feature, "x", WOCKY_XMPP_NS_DATA);
  if (x == NULL)
    {
      DEBUG ("got a SI request without a X data field");
      return FALSE;
    }

  for (x_children = x->children; x_children;
      x_children = g_slist_next (x_children))
    {
      WockyNode *field = x_children->data;

      if (tp_strdiff (wocky_node_get_attribute (field, "var"),
            "stream-method"))
        /* some future field, ignore it */
        continue;

      if (tp_strdiff (wocky_node_get_attribute (field, "type"),
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
          WockyNode *stream_method, *value;
          const gchar *stream_method_str;

          stream_method = (WockyNode *) field_children->data;

          value = wocky_node_get_child (stream_method, "value");
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
                          WockyContact *contact)
{
  if (state == GIBBER_BYTESTREAM_STATE_CLOSED)
    {
      WockyMetaPorter *porter;

      DEBUG ("bytestream closed, release the connection");
      g_object_get (bytestream, "porter", &porter, NULL);

      wocky_meta_porter_unhold (porter, contact);

      g_object_unref (porter);
      g_object_unref (bytestream);
    }
}

static GibberBytestreamIface *
choose_bytestream_method (SalutSiBytestreamManager *self,
                          GSList *stream_methods,
                          WockyPorter *porter,
                          SalutContact *contact,
                          const gchar *stream_id,
                          WockyStanza *stream_init_iq)
{
  SalutSiBytestreamManagerPrivate *priv =
    SALUT_SI_BYTESTREAM_MANAGER_GET_PRIVATE (self);
  GSList *l;

  /* We create the stream according the stream method chosen.
   * User has to accept it */

  /* check OOB */
  for (l = stream_methods; l != NULL; l = l->next)
    {
      if (!tp_strdiff (l->data, WOCKY_XMPP_NS_IQ_OOB))
        {
          DEBUG ("choose OOB in methods list");
          return g_object_new (GIBBER_TYPE_BYTESTREAM_OOB,
              "porter", porter,
              "stream-id", stream_id,
              "state", GIBBER_BYTESTREAM_STATE_LOCAL_PENDING,
              "self-id", priv->connection->name,
              "peer-id", contact->name,
              "contact", contact,
              "stream-init-iq", stream_init_iq,
              NULL);
        }
    }

  return NULL;
}

static gboolean
si_request_cb (WockyPorter *porter,
               WockyStanza *stanza,
               gpointer user_data)
{
  SalutSiBytestreamManager *self = SALUT_SI_BYTESTREAM_MANAGER (user_data);
  SalutSiBytestreamManagerPrivate *priv =
    SALUT_SI_BYTESTREAM_MANAGER_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->connection, TP_ENTITY_TYPE_CONTACT);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->connection, TP_ENTITY_TYPE_ROOM);
  TpHandle peer_handle;
  GibberBytestreamIface *bytestream = NULL;
  WockyNode *top_node = wocky_stanza_get_top_node (stanza);
  WockyNode *si, *node;
  const gchar *profile, *from, *stream_id, *mime_type;
  GSList *stream_methods = NULL;
  WockyContact *contact = wocky_stanza_get_from_contact (stanza);

   /* after this point, the message is for us, so in all cases we either handle
   * it or send an error reply */

  if (!streaminit_parse_request (stanza, &profile, &from, &stream_id,
        NULL, &mime_type, &stream_methods))
    {
      GError err = { WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
                      "failed to parse SI request" };
      WockyStanza *reply;

      reply = wocky_stanza_build_iq_error (stanza, NULL);
      wocky_stanza_error_to_node (&err, wocky_stanza_get_top_node (reply));

      wocky_porter_send (porter, reply);

      g_object_unref (reply);
      return TRUE;
    }

  si = wocky_node_get_child_ns (top_node, "si", WOCKY_XMPP_NS_SI);
  g_assert (si != NULL);

  DEBUG ("received a SI request");

  peer_handle = tp_handle_lookup (contact_repo, from, NULL, NULL);
  if (peer_handle == 0)
    {
      goto out;
    }

  /* check stream method */
  bytestream = choose_bytestream_method (self, stream_methods, porter,
      SALUT_CONTACT (contact), stream_id, stanza);

  if (bytestream == NULL)
    {
      GError err = { WOCKY_XMPP_ERROR, WOCKY_SI_ERROR_NO_VALID_STREAMS,
                      NULL };
      WockyStanza *reply;

      DEBUG ("SI request doesn't contain any supported stream method.");

      reply = wocky_stanza_build_iq_error (stanza, NULL);
      wocky_stanza_error_to_node (&err, wocky_stanza_get_top_node (reply));

      wocky_porter_send (porter, reply);

      g_object_unref (reply);
      goto out;
    }

  /* Now that we have a bytestream, it's responsible for declining the IQ
   * if needed. */

  /* As bytestreams are not porter aware, they can't take/release
   * the connection so we do it for them.
   * We'll release it when the bytestream will be closed */
  wocky_meta_porter_hold (WOCKY_META_PORTER (porter), contact);

  g_signal_connect (bytestream, "state-changed",
     G_CALLBACK (bytestream_state_changed), contact);

  /* We inform the right manager we received a SI request */
  if (tp_strdiff (profile, WOCKY_TELEPATHY_NS_TUBES))
    {
      GError e = { WOCKY_SI_ERROR, WOCKY_SI_ERROR_BAD_PROFILE, "" };
      DEBUG ("SI profile unsupported: %s", profile);

      gibber_bytestream_iface_close (bytestream, &e);
      goto out;
    }

  /* A Tubes SI request can only be a muc tube extra bytestream offer.
   * We don't use SI for 1-1 tubes
   */

  if ((node = wocky_node_get_child_ns (si, "muc-stream",
          WOCKY_TELEPATHY_NS_TUBES)))
    {
      const gchar *muc;
      TpHandle room_handle;
      SalutMucManager *muc_mgr;

      muc = wocky_node_get_attribute (node, "muc");
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
  else
    {
      GError e = { WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "Invalid tube SI request: expected <tube>, <stream> or "
          "<muc-stream>" };

      DEBUG ("Invalid tube SI request");
      gibber_bytestream_iface_close (bytestream, &e);
      goto out;
    }

out:
  g_slist_free (stream_methods);
  return TRUE;
}

static void
salut_si_bytestream_manager_dispose (GObject *object)
{
  SalutSiBytestreamManager *self = SALUT_SI_BYTESTREAM_MANAGER (object);
  SalutSiBytestreamManagerPrivate *priv = SALUT_SI_BYTESTREAM_MANAGER_GET_PRIVATE
      (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->connection->porter != NULL)
    {
      wocky_porter_unregister_handler (priv->connection->porter,
          priv->si_request_id);
      priv->si_request_id = 0;
    }

  g_object_unref (priv->im_manager);
  g_object_unref (priv->muc_manager);

  if (G_OBJECT_CLASS (salut_si_bytestream_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_si_bytestream_manager_parent_class)->dispose (object);
}

static void
salut_si_bytestream_manager_finalize (GObject *object)
{
  SalutSiBytestreamManager *self = SALUT_SI_BYTESTREAM_MANAGER (object);
  SalutSiBytestreamManagerPrivate *priv = SALUT_SI_BYTESTREAM_MANAGER_GET_PRIVATE (
      self);

  g_free (priv->host_name_fqdn);

  if (G_OBJECT_CLASS (salut_si_bytestream_manager_parent_class)->finalize)
    G_OBJECT_CLASS (salut_si_bytestream_manager_parent_class)->finalize
        (object);
}

static void
salut_si_bytestream_manager_get_property (GObject *object,
                                          guint property_id,
                                          GValue *value,
                                          GParamSpec *pspec)
{
  SalutSiBytestreamManager *self = SALUT_SI_BYTESTREAM_MANAGER (object);
  SalutSiBytestreamManagerPrivate *priv = SALUT_SI_BYTESTREAM_MANAGER_GET_PRIVATE (
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
salut_si_bytestream_manager_set_property (GObject *object,
                                          guint property_id,
                                          const GValue *value,
                                          GParamSpec *pspec)
{
  SalutSiBytestreamManager *self = SALUT_SI_BYTESTREAM_MANAGER (object);
  SalutSiBytestreamManagerPrivate *priv = SALUT_SI_BYTESTREAM_MANAGER_GET_PRIVATE (
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
salut_si_bytestream_manager_constructor (GType type,
                                      guint n_props,
                                      GObjectConstructParam *props)
{
  GObject *obj;
  SalutSiBytestreamManager *self;
  SalutSiBytestreamManagerPrivate *priv;

  obj = G_OBJECT_CLASS (salut_si_bytestream_manager_parent_class)->
           constructor (type, n_props, props);

  self = SALUT_SI_BYTESTREAM_MANAGER (obj);
  priv = SALUT_SI_BYTESTREAM_MANAGER_GET_PRIVATE (self);

  g_assert (priv->connection != NULL);
  g_object_get (priv->connection,
      "im-manager", &(priv->im_manager),
      "muc-manager", &(priv->muc_manager),
      NULL);
  g_assert (priv->im_manager != NULL);
  g_assert (priv->muc_manager != NULL);
  g_assert (priv->host_name_fqdn != NULL);

  priv->si_request_id = wocky_porter_register_handler_from_anyone (
      priv->connection->porter, WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_SET, WOCKY_PORTER_HANDLER_PRIORITY_NORMAL,
      si_request_cb, self,
      '(', "si",
        ':', WOCKY_XMPP_NS_SI,
      ')', NULL);

  return obj;
}

static void
salut_si_bytestream_manager_class_init (
    SalutSiBytestreamManagerClass *salut_si_bytestream_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS
      (salut_si_bytestream_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_si_bytestream_manager_class,
      sizeof (SalutSiBytestreamManagerPrivate));

  object_class->constructor = salut_si_bytestream_manager_constructor;
  object_class->dispose = salut_si_bytestream_manager_dispose;
  object_class->finalize = salut_si_bytestream_manager_finalize;

  object_class->get_property = salut_si_bytestream_manager_get_property;
  object_class->set_property = salut_si_bytestream_manager_set_property;

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "Salut Connection that owns the connection for this bytestream channel",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string (
      "host-name-fqdn",
      "host name FQDN",
      "The FQDN host name that will be used by OOB bytestreams",
      NULL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_HOST_NAME_FQDN,
      param_spec);
}

SalutSiBytestreamManager *
salut_si_bytestream_manager_new (SalutConnection *conn,
                              const gchar *host_name_fqdn)
{
  g_return_val_if_fail (SALUT_IS_CONNECTION (conn), NULL);

  return g_object_new (
      SALUT_TYPE_SI_BYTESTREAM_MANAGER,
      "connection", conn,
      "host-name-fqdn", host_name_fqdn,
      NULL);
}

/**
 * salut_si_bytestream_manager_make_stream_init_iq
 *
 * @from: your contact
 * @to: the contact to who you want to offer the stream
 * @stream_id: the stream ID of the new stream
 * @profile: the profile associated with the stream
 *
 * Create a SI request IQ as described in XEP-0095.
 *
 */
WockyStanza *
salut_si_bytestream_manager_make_stream_init_iq (const gchar *from,
                                                 const gchar *to,
                                                 const gchar *stream_id,
                                                 const gchar *profile)
{
  return wocky_stanza_build (
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
      from, to,
      '(', "si",
        ':', WOCKY_XMPP_NS_SI,
        '@', "id", stream_id,
        '@', "profile", profile,
        '@', "mime-type", "application/octet-stream",
        '(', "feature",
          ':', WOCKY_XMPP_NS_FEATURENEG,
          '(', "x",
            ':', WOCKY_XMPP_NS_DATA,
            '@', "type", "form",
            '(', "field",
              '@', "var", "stream-method",
              '@', "type", "list-single",

              '(', "option",
                '(', "value",
                  '$', WOCKY_XMPP_NS_IQ_OOB,
                ')',
              ')',

            ')',
          ')',
        ')',
      ')', NULL);
}

struct streaminit_reply_cb_data
{
  SalutSiBytestreamManager *self;
  gchar *stream_id;
  SalutSiBytestreamManagerNegotiateReplyFunc func;
  gpointer user_data;
  SalutContact *contact;
  WockyStanza *stanza;
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

  if (data->contact != NULL)
    g_object_unref (data->contact);

  if (data->stanza != NULL)
    g_object_unref (data->stanza);

  g_slice_free (struct streaminit_reply_cb_data, data);
}

static gboolean
check_bytestream_oob_peer_addr (GibberBytestreamOOB *bytestream,
                                struct sockaddr *addr,
                                socklen_t addrlen,
                                gpointer user_data)
{
  SalutSiBytestreamManager *self = SALUT_SI_BYTESTREAM_MANAGER (user_data);
  SalutSiBytestreamManagerPrivate *priv = SALUT_SI_BYTESTREAM_MANAGER_GET_PRIVATE (
      self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->connection, TP_ENTITY_TYPE_CONTACT);
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

  result = salut_contact_has_address (contact, addr, addrlen);
  g_object_unref (contact);

  return result;
}

static void
si_request_sent_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  WockyPorter *porter = WOCKY_PORTER (source_object);
  struct streaminit_reply_cb_data *data =
    (struct streaminit_reply_cb_data *) user_data;
  SalutSiBytestreamManagerPrivate *priv =
    SALUT_SI_BYTESTREAM_MANAGER_GET_PRIVATE (data->self);
  WockyStanza *stanza;
  GError *error = NULL;

  WockyStanzaSubType sub_type;
  WockyNode *si, *feature, *x;
  GibberBytestreamIface *bytestream = NULL;
  const gchar *from, *stream_method;
  GSList *x_children;
  WockyNode *node;

  stanza = wocky_porter_send_iq_finish (porter, result, &error);

  if (result == NULL)
    {
      DEBUG ("sending SI request failed: %s", error->message);
      g_clear_error (&error);
      goto END;
    }

  DEBUG ("received SI request response");

  node = wocky_stanza_get_top_node (stanza);

  wocky_stanza_get_type_info (stanza, NULL, &sub_type);
  if (sub_type != WOCKY_STANZA_SUB_TYPE_RESULT)
    {
      DEBUG ("stream %s declined", data->stream_id);
      goto END;
    }

  /* stream accepted */
  from = wocky_node_get_attribute (node, "from");
  if (from == NULL)
    {
      DEBUG ("got a message without a from field");
      goto END;
    }

  si = wocky_node_get_child_ns (node, "si",
      WOCKY_XMPP_NS_SI);
  if (si == NULL)
    {
      DEBUG ("got a SI reply without a si field");
      goto END;
    }

  feature = wocky_node_get_child_ns (si, "feature",
      WOCKY_XMPP_NS_FEATURENEG);
  if (feature == NULL)
    {
      DEBUG ("got a SI reply without a feature field");
      goto END;
    }

  x = wocky_node_get_child_ns (feature, "x", WOCKY_XMPP_NS_DATA);
  if (x == NULL)
    {
      DEBUG ("got a SI reply without a x field");
      goto END;
    }

  for (x_children = x->children; x_children;
      x_children = g_slist_next (x_children))
    {
      WockyNode *value, *field = x_children->data;

      if (tp_strdiff (wocky_node_get_attribute (field, "var"),
            "stream-method"))
        /* some future field, ignore it */
        continue;

      value = wocky_node_get_child (field, "value");
      if (value == NULL)
        {
          DEBUG ("SI reply's stream-method field "
              "doesn't contain stream-method value");
          goto END;
        }

      stream_method = value->content;

      if (!tp_strdiff (stream_method, WOCKY_XMPP_NS_IQ_OOB))
      {
        /* Remote user have accepted the stream */
        DEBUG ("remote user chose a OOB bytestream");
        bytestream = g_object_new (GIBBER_TYPE_BYTESTREAM_OOB,
              "porter", porter,
              "stream-id", data->stream_id,
              "state", GIBBER_BYTESTREAM_STATE_INITIATING,
              "self-id", priv->connection->name,
              "peer-id", from,
              "contact", wocky_stanza_get_from_contact (stanza),
              "stream-init-iq", NULL,
              "host", priv->host_name_fqdn,
              NULL);
        gibber_bytestream_oob_set_check_addr_func (
            GIBBER_BYTESTREAM_OOB (bytestream), check_bytestream_oob_peer_addr,
            data->self);
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

  /* As bytestreams are not porter aware, they can't take/release
   * the connection so we do it for them.
   * We'll release it when the bytestream will be closed */
  wocky_meta_porter_hold (WOCKY_META_PORTER (porter),
      WOCKY_CONTACT (data->contact));

  g_signal_connect (bytestream, "state-changed",
     G_CALLBACK (bytestream_state_changed), data->contact);

  /* Let's start the initiation of the stream */
  if (!gibber_bytestream_iface_initiate (bytestream))
    {
      /* Initiation failed. */
      gibber_bytestream_iface_close (bytestream, NULL);
      bytestream = NULL;
    }

END:
  /* user callback */
  data->func (bytestream, data->user_data);

  streaminit_reply_cb_data_free (data);

  if (stanza != NULL)
    g_object_unref (stanza);
}

/*
 * salut_si_bytestream_manager_negotiate_stream:
 *
 * @contact: the contact to who send the SI request
 * @stanza: the SI negotiation IQ (created using
 * salut_si_bytestream_manager_make_stream_init_iq)
 * @stream_id: the stream identifier
 * @func: the callback to call when we receive the answser of the request
 * @user_data: user data to pass to the callback
 * @error: pointer in which to return a GError in case of failure.
 *
 * Send a Stream Initiation (XEP-0095) request.
 */
gboolean
salut_si_bytestream_manager_negotiate_stream (SalutSiBytestreamManager *self,
                                              SalutContact *contact,
                                              WockyStanza *stanza,
                                              const gchar *stream_id,
                                              SalutSiBytestreamManagerNegotiateReplyFunc func,
                                              gpointer user_data,
                                              GError **error)
{
  SalutSiBytestreamManagerPrivate *priv;
  struct streaminit_reply_cb_data *data;

  g_assert (SALUT_IS_SI_BYTESTREAM_MANAGER (self));
  g_assert (stream_id != NULL);
  g_assert (func != NULL);

  priv = SALUT_SI_BYTESTREAM_MANAGER_GET_PRIVATE (self);

  data = streaminit_reply_cb_data_new ();
  data->self = self;
  data->stream_id = g_strdup (stream_id);
  data->func = func;
  data->user_data = user_data;
  data->contact = g_object_ref (contact);
  data->stanza = g_object_ref (stanza);

  DEBUG ("send an SI request to %s", contact->name);

  wocky_porter_send_iq_async (priv->connection->porter,
      stanza, NULL, si_request_sent_cb, data);

  return TRUE;
}
