/*
 * gibber-bytestream-oob.c - Source for GibberBytestreamOOB
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

#include "gibber-bytestream-oob.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib.h>

#include <wocky/wocky-stanza.h>
#include <wocky/wocky-porter.h>
#include <wocky/wocky-meta-porter.h>

#include "gibber-sockets.h"
#include "gibber-bytestream-iface.h"
#include "gibber-xmpp-connection.h"
#include "gibber-namespaces.h"
#include "gibber-linklocal-transport.h"
#include "gibber-xmpp-error.h"
#include "gibber-iq-helper.h"
#include "gibber-util.h"
#include "gibber-transport.h"
#include "gibber-fd-transport.h"
#include "gibber-listener.h"

#define DEBUG_FLAG DEBUG_BYTESTREAM
#include "gibber-debug.h"

#include "gibber-signals-marshal.h"

static void
bytestream_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GibberBytestreamOOB, gibber_bytestream_oob,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GIBBER_TYPE_BYTESTREAM_IFACE,
      bytestream_iface_init));

/* properties */
enum
{
  PROP_PORTER = 1,
  PROP_CONTACT,
  PROP_SELF_ID,
  PROP_PEER_ID,
  PROP_STREAM_ID,
  PROP_STREAM_INIT_ID,
  PROP_STATE,
  PROP_HOST,
  PROP_PROTOCOL,
  LAST_PROPERTY
};

typedef struct _GibberBytestreamIBBPrivate GibberBytestreamOOBPrivate;
struct _GibberBytestreamIBBPrivate
{
  WockyPorter *porter;
  WockyContact *contact;
  gchar *self_id;
  gchar *peer_id;
  gchar *stream_id;
  gchar *stream_init_id;
  /* ID of the OOB opening stanza. We'll reply to
   * it when we the bytestream is closed */
  gchar *stream_open_id;
  guint stanza_received_id;
  GibberBytestreamState state;
  gchar *host;
  gchar *url;

  /* Are we the recipient of this bytestream?
   * If not we are the sender */
  gboolean recipient;
  GibberTransport *transport;
  gboolean write_blocked;
  gboolean read_blocked;
  GibberListener *listener;

  GibberBytestreamOOBCheckAddrFunc check_addr_func;
  gpointer check_addr_func_data;

  gboolean dispose_has_run;
};

#define GIBBER_BYTESTREAM_OOB_GET_PRIVATE(obj) \
    ((GibberBytestreamOOBPrivate *) (GibberBytestreamOOB *) obj->priv)

static void gibber_bytestream_oob_do_close (GibberBytestreamOOB *self,
    GError *error, gboolean can_wait);
static void bytestream_closed (GibberBytestreamOOB *self);

static void
gibber_bytestream_oob_init (GibberBytestreamOOB *self)
{
  GibberBytestreamOOBPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GIBBER_TYPE_BYTESTREAM_OOB, GibberBytestreamOOBPrivate);

  self->priv = priv;
  priv->dispose_has_run = FALSE;
}

static WockyStanza *
make_iq_oob_sucess_response (const gchar *from,
                             const gchar *to,
                             const gchar *id)
{
  return wocky_stanza_build (
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_RESULT,
      from, to,
      WOCKY_NODE_ATTRIBUTE, "id", id,
      NULL);
}

static void
transport_handler (GibberTransport *transport,
                   GibberBuffer *data,
                   gpointer user_data)
{
  GibberBytestreamOOB *self = GIBBER_BYTESTREAM_OOB (user_data);
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);
  GString *buffer;

  buffer = g_string_new_len ((const gchar *) data->data, data->length);

  g_signal_emit_by_name (G_OBJECT (self), "data-received", priv->peer_id,
      buffer);

  g_string_free (buffer, TRUE);
}

static void
transport_connected_cb (GibberTransport *transport,
                        GibberBytestreamOOB *self)
{
  DEBUG ("transport connected. Bytestream is now open");
  g_object_set (self, "state", GIBBER_BYTESTREAM_STATE_OPEN,
      NULL);
}

static void
transport_disconnected_cb (GibberTransport *transport,
                           GibberBytestreamOOB *self)
{
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);
  if (priv->state == GIBBER_BYTESTREAM_STATE_CLOSED)
    return;

  DEBUG ("transport disconnected. close the bytestream");

  if (priv->state == GIBBER_BYTESTREAM_STATE_ACCEPTED)
    {
      /* Connection to host failed */
      GError e = { GIBBER_XMPP_ERROR, XMPP_ERROR_ITEM_NOT_FOUND,
          "connection failed" };

      gibber_bytestream_iface_close (GIBBER_BYTESTREAM_IFACE (self), &e);
    }
  else
    {
      gibber_bytestream_iface_close (GIBBER_BYTESTREAM_IFACE (self), NULL);
    }
}

static void
change_write_blocked_state (GibberBytestreamOOB *self,
                            gboolean blocked)
{
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);

  if (priv->write_blocked == blocked)
    return;

  priv->write_blocked = blocked;
  g_signal_emit_by_name (self, "write-blocked", blocked);
}

static void
transport_buffer_empty_cb (GibberTransport *transport,
                           GibberBytestreamOOB *self)
{
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);

  if (priv->state == GIBBER_BYTESTREAM_STATE_CLOSING)
    {
      DEBUG ("buffer is now empty. Bytestream can be closed");
      bytestream_closed (self);
    }
  else if (priv->write_blocked)
    {
      DEBUG ("buffer is empty, unblock write to the bytestream");
      change_write_blocked_state (self, FALSE);
    }
}

static void
set_transport (GibberBytestreamOOB *self,
               GibberTransport *transport)
{
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);

  g_assert (priv->transport == NULL);

  priv->transport = g_object_ref (transport);
  gibber_transport_set_handler (transport, transport_handler, self);

  /* The transport will already be connected if it is created from
   * GibberListener. In this case, set the bytestream to open, otherwise
   * it will be done in transport_connected_cb. */
  if (gibber_transport_get_state (transport) == GIBBER_TRANSPORT_CONNECTED)
    {
      g_object_set (self, "state", GIBBER_BYTESTREAM_STATE_OPEN,
          NULL);
    }

  g_signal_connect (transport, "connected",
      G_CALLBACK (transport_connected_cb), self);
  g_signal_connect (transport, "disconnected",
      G_CALLBACK (transport_disconnected_cb), self);
  g_signal_connect (transport, "buffer-empty",
      G_CALLBACK (transport_buffer_empty_cb), self);
}

static void
connect_to_url (GibberBytestreamOOB *self)
{
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);
  GibberLLTransport *ll_transport;
  GSocketConnection *conn;
  GSocketAddress *socket_address = NULL;
  GInetAddress *address;
  gchar **tokens;
  union {
    struct sockaddr_storage storage;
    struct sockaddr_in in;
  } addr;
  const gchar *port;
  gint portnum = 0;
  const gchar *url;
  GError *error = NULL;

  /* TODO: if we want to support IPv6 literals, we have to remove
   * [] around the address */

  url = priv->url + strlen ("x-tcp://");
  tokens = g_strsplit (url, ":", 2);
  port = tokens[1];

  if (port != NULL)
    portnum = atoi (port);

  if (portnum <= 0 || portnum > G_MAXUINT16)
   {
      /* I'm too lazy to create more specific errors for this  as it should
       * never happen while using salut anyway.. */
      GError e = { GIBBER_XMPP_ERROR, XMPP_ERROR_NOT_ACCEPTABLE,
          "Invalid port number" };
      DEBUG ("Invalid port number: %s", port);
      gibber_bytestream_iface_close (GIBBER_BYTESTREAM_IFACE (self), &e);
      goto out;
   }

  conn = wocky_meta_porter_borrow_connection (WOCKY_META_PORTER (priv->porter),
      priv->contact);

  if (conn != NULL)
    socket_address = g_socket_connection_get_remote_address (conn, NULL);

  if (conn == NULL || socket_address == NULL)
    {
      /* I'm too lazy to create more specific errors for this  as it should
       * never happen while using salut anyway.. */
      GError e = { GIBBER_XMPP_ERROR, XMPP_ERROR_ITEM_NOT_FOUND,
          "Unsable get socket address for the control connection" };
      DEBUG ("Could not get socket address for the control connection" );
      gibber_bytestream_iface_close (GIBBER_BYTESTREAM_IFACE (self), &e);
      goto out;
    }

  if (!g_socket_address_to_native (G_SOCKET_ADDRESS (address), &(addr.storage),
          sizeof (addr.storage), &error))
    {
      GError e = { GIBBER_XMPP_ERROR, XMPP_ERROR_ITEM_NOT_FOUND,
          "Failed to turn socket address into bytes" };
      DEBUG ("Failed to get native socket address: %s", error->message);
      gibber_bytestream_iface_close (GIBBER_BYTESTREAM_IFACE (self), &e);
      g_clear_error (&error);
      goto out;
    }

  g_object_unref (socket_address);

  addr.in.sin_port = g_htons ((guint16) portnum);

  ll_transport = gibber_ll_transport_new ();
  set_transport (self, GIBBER_TRANSPORT (ll_transport));
  gibber_ll_transport_open_sockaddr (ll_transport, &addr.storage, NULL);
  g_object_unref (ll_transport);

out:
  g_strfreev (tokens);
}

static void
opened_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  WockyMetaPorter *porter = WOCKY_META_PORTER (source_object);
  GibberBytestreamOOB *self = user_data;
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);
  GError *error = NULL;

  if (!wocky_meta_porter_open_finish (porter, result, &error))
    {
      DEBUG ("failed to open connection to contact");
      g_clear_error (&error);
    }
  else
    {
      connect_to_url (self);
    }

  g_free (priv->url);

  wocky_meta_porter_unref (porter, priv->contact);
}

static gboolean
parse_oob_init_iq (GibberBytestreamOOB *self,
                   WockyStanza *stanza)
{
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);
  WockyNode *query_node, *url_node;
  WockyStanzaType type;
  WockyStanzaSubType sub_type;
  const gchar *stream_id, *url;
  WockyNode *node = wocky_stanza_get_top_node (stanza);

  wocky_stanza_get_type_info (stanza, &type, &sub_type);

  if (type != WOCKY_STANZA_TYPE_IQ ||
      sub_type != WOCKY_STANZA_SUB_TYPE_SET)
    return FALSE;

  query_node = wocky_node_get_child_ns (node, "query",
      GIBBER_XMPP_NS_IQ_OOB);
  if (query_node == NULL)
    return FALSE;

  stream_id = wocky_node_get_attribute (query_node, "sid");
  if (stream_id == NULL || strcmp (stream_id, priv->stream_id) != 0)
    return FALSE;

  url_node = wocky_node_get_child (query_node, "url");
  if (url_node == NULL)
    return FALSE;
  url = url_node->content;

  priv->recipient = TRUE;
  priv->stream_open_id = g_strdup (wocky_node_get_attribute (
        node, "id"));

  if (!g_str_has_prefix (url, "x-tcp://"))
    {
      GError e = { GIBBER_XMPP_ERROR, XMPP_ERROR_ITEM_NOT_FOUND,
                   "URL is not a TCP URL" };

      DEBUG ("URL is not a TCP URL: %s. Close the bytestream", priv->url);
      gibber_bytestream_iface_close (GIBBER_BYTESTREAM_IFACE (self), &e);
      return TRUE;
    }

  priv->url = g_strdup (url);

  wocky_meta_porter_open_async (WOCKY_META_PORTER (priv->porter),
      priv->contact, NULL, opened_cb, self);

  return TRUE;
}

static gboolean
parse_oob_iq_result (GibberBytestreamOOB *self,
                     WockyStanza *stanza)
{
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);
  WockyStanzaType type;
  WockyStanzaSubType sub_type;
  const gchar *id;
  WockyNode *node = wocky_stanza_get_top_node (stanza);

  if (priv->recipient)
    /* Only the sender have to wait for the IQ reply */
    return FALSE;

  wocky_stanza_get_type_info (stanza, &type, &sub_type);

  if (type != WOCKY_STANZA_TYPE_IQ ||
      sub_type != WOCKY_STANZA_SUB_TYPE_RESULT)
    return FALSE;

  /* FIXME: we should check if it's the right sender */
  id = wocky_node_get_attribute (node, "id");

  if (id == NULL || strcmp (id, priv->stream_open_id) != 0)
    return FALSE;

  DEBUG ("received OOB close stanza - ignoring");

  return TRUE;
}

static gboolean
received_stanza_cb (WockyPorter *porter,
                    WockyStanza *stanza,
                    gpointer user_data)
{
  GibberBytestreamOOB *self = (GibberBytestreamOOB *) user_data;
  WockyNode *node = wocky_stanza_get_top_node (stanza);
  const gchar *from;

  /* discard invalid stanza */
  from = wocky_node_get_attribute (node, "from");
  if (from == NULL)
    {
      DEBUG ("got a message without a from field");
      return FALSE;
    }

  if (parse_oob_init_iq (self, stanza))
    return TRUE;

  if (parse_oob_iq_result (self, stanza))
    return TRUE;

  return FALSE;
}

static void
gibber_bytestream_oob_dispose (GObject *object)
{
  GibberBytestreamOOB *self = GIBBER_BYTESTREAM_OOB (object);
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->state != GIBBER_BYTESTREAM_STATE_CLOSED)
    {
      if (priv->state == GIBBER_BYTESTREAM_STATE_CLOSING)
        {
          bytestream_closed (self);
        }
      else
        {
          gibber_bytestream_oob_do_close (self, NULL, FALSE);
        }
    }

  if (priv->listener != NULL)
    {
      g_object_unref (priv->listener);
      priv->listener = NULL;
    }

  if (priv->porter != NULL)
    {
      wocky_porter_unregister_handler (priv->porter, priv->stanza_received_id);
      priv->stanza_received_id = 0;
      g_object_unref (priv->porter);
      priv->porter = NULL;
    }

  if (priv->contact != NULL)
    {
      g_object_unref (priv->contact);
      priv->contact = NULL;
    }

  G_OBJECT_CLASS (gibber_bytestream_oob_parent_class)->dispose (object);
}

static void
gibber_bytestream_oob_finalize (GObject *object)
{
  GibberBytestreamOOB *self = GIBBER_BYTESTREAM_OOB (object);
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);

  g_free (priv->stream_id);
  g_free (priv->stream_init_id);
  g_free (priv->stream_open_id);
  g_free (priv->host);
  g_free (priv->self_id);
  g_free (priv->peer_id);

  G_OBJECT_CLASS (gibber_bytestream_oob_parent_class)->finalize (object);
}

static void
gibber_bytestream_oob_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
  GibberBytestreamOOB *self = GIBBER_BYTESTREAM_OOB (object);
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_PORTER:
        g_value_set_object (value, priv->porter);
        break;
      case PROP_CONTACT:
        g_value_set_object (value, priv->contact);
        break;
      case PROP_SELF_ID:
        g_value_set_string (value, priv->self_id);
        break;
      case PROP_PEER_ID:
        g_value_set_string (value, priv->peer_id);
        break;
      case PROP_STREAM_ID:
        g_value_set_string (value, priv->stream_id);
        break;
      case PROP_STREAM_INIT_ID:
        g_value_set_string (value, priv->stream_init_id);
        break;
      case PROP_STATE:
        g_value_set_uint (value, priv->state);
        break;
      case PROP_HOST:
        g_value_set_string (value, priv->host);
        break;
      case PROP_PROTOCOL:
        g_value_set_string (value, GIBBER_XMPP_NS_IQ_OOB);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
make_porter_connections (GibberBytestreamOOB *self)
{
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);
  static gboolean done = FALSE;
  gchar *jid;

  if (done)
    return;

  jid = wocky_contact_dup_jid (priv->contact);

  priv->stanza_received_id = wocky_porter_register_handler_from (priv->porter,
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_TYPE_NONE, jid,
      WOCKY_PORTER_HANDLER_PRIORITY_NORMAL, received_stanza_cb, self, NULL);

  g_free (jid);

  done = TRUE;
}

static void
gibber_bytestream_oob_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  GibberBytestreamOOB *self = GIBBER_BYTESTREAM_OOB (object);
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_PORTER:
        priv->porter = g_value_dup_object (value);

        if (priv->porter != NULL && priv->contact != NULL)
          make_porter_connections (self);
        break;
      case PROP_CONTACT:
        priv->contact = g_value_dup_object (value);

        if (priv->porter != NULL && priv->contact != NULL)
          make_porter_connections (self);
        break;
      case PROP_SELF_ID:
        g_free (priv->self_id);
        priv->self_id = g_value_dup_string (value);
        break;
      case PROP_PEER_ID:
        g_free (priv->peer_id);
        priv->peer_id = g_value_dup_string (value);
        break;
      case PROP_STREAM_ID:
        g_free (priv->stream_id);
        priv->stream_id = g_value_dup_string (value);
        break;
      case PROP_STREAM_INIT_ID:
        g_free (priv->stream_init_id);
        priv->stream_init_id = g_value_dup_string (value);
        break;
      case PROP_STATE:
        if (priv->state != g_value_get_uint (value))
            {
              priv->state = g_value_get_uint (value);
              g_signal_emit_by_name (object, "state-changed", priv->state);
            }
        break;
      case PROP_HOST:
        g_free (priv->host);
        priv->host = g_value_dup_string (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
gibber_bytestream_oob_constructor (GType type,
                                   guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj;
  GibberBytestreamOOBPrivate *priv;

  obj = G_OBJECT_CLASS (gibber_bytestream_oob_parent_class)->
           constructor (type, n_props, props);

  priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (GIBBER_BYTESTREAM_OOB (obj));

  g_assert (priv->self_id != NULL);
  g_assert (priv->peer_id != NULL);
  g_assert (priv->stream_id != NULL);
  g_assert (priv->porter != NULL);
  g_assert (priv->contact != NULL);

  return obj;
}

static void
gibber_bytestream_oob_class_init (
    GibberBytestreamOOBClass *gibber_bytestream_oob_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_bytestream_oob_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gibber_bytestream_oob_class,
      sizeof (GibberBytestreamOOBPrivate));

  object_class->dispose = gibber_bytestream_oob_dispose;
  object_class->finalize = gibber_bytestream_oob_finalize;

  object_class->get_property = gibber_bytestream_oob_get_property;
  object_class->set_property = gibber_bytestream_oob_set_property;
  object_class->constructor = gibber_bytestream_oob_constructor;

  g_object_class_override_property (object_class, PROP_SELF_ID,
      "self-id");
  g_object_class_override_property (object_class, PROP_PEER_ID,
      "peer-id");
  g_object_class_override_property (object_class, PROP_STREAM_ID,
      "stream-id");
  g_object_class_override_property (object_class, PROP_STATE,
      "state");
  g_object_class_override_property (object_class, PROP_PROTOCOL,
      "protocol");

  param_spec = g_param_spec_object (
      "porter",
      "WockyPorter object",
      "Wocky porter object used for communication by this "
      "bytestream if it's a private one",
      WOCKY_TYPE_PORTER,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PORTER,
      param_spec);

  param_spec = g_param_spec_object (
      "contact",
      "WockyContact object",
      "Contact object used for communication by this "
      "bytestream if it's a private one",
      WOCKY_TYPE_CONTACT,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONTACT,
      param_spec);

  param_spec = g_param_spec_string (
      "stream-init-id",
      "stream init ID",
      "the iq ID of the SI request, if any",
      "",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STREAM_INIT_ID,
      param_spec);

  param_spec = g_param_spec_string (
      "host",
      "host",
      "The hostname to use in the OOB URL. Literal are not allowed",
      "",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_HOST,
      param_spec);
}

/*
 * gibber_bytestream_oob_send
 *
 * Implements gibber_bytestream_iface_send on GibberBytestreamIface
 */
static gboolean
gibber_bytestream_oob_send (GibberBytestreamIface *bytestream,
                            guint len,
                            const gchar *str)
{
  GibberBytestreamOOB *self = GIBBER_BYTESTREAM_OOB (bytestream);
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);
  GError *error = NULL;

  if (priv->state != GIBBER_BYTESTREAM_STATE_OPEN)
    {
      DEBUG ("can't send data through a not open bytestream (state: %d)",
          priv->state);
      return FALSE;
    }

  if (priv->write_blocked)
    {
      DEBUG ("sending data while the bytestream was blocked");
    }

  DEBUG ("send %u bytes through bytestream", len);
  if (!gibber_transport_send (priv->transport, (const guint8 *) str, len,
        &error))
    {
      DEBUG ("sending failed: %s", error->message);
      g_error_free (error);

      gibber_bytestream_iface_close (GIBBER_BYTESTREAM_IFACE (self), NULL);
      return FALSE;
    }

  if (!gibber_transport_buffer_is_empty (priv->transport))
    {
      /* We >don't want to send more data while the buffer isn't empty */
      DEBUG ("buffer isn't empty. Block write to the bytestream");
      change_write_blocked_state (self, TRUE);
    }

  return TRUE;
}

static WockyStanza *
create_si_accept_iq (GibberBytestreamOOB *self)
{
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);

  return wocky_stanza_build_to_contact (
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_RESULT,
      priv->self_id, priv->contact,
      '@', "id", priv->stream_init_id,
      '(', "si",
        ':', GIBBER_XMPP_NS_SI,
        '(', "feature",
          ':', GIBBER_XMPP_NS_FEATURENEG,
          '(', "x",
            ':', GIBBER_XMPP_NS_DATA,
            '@', "type", "submit",
            '(', "field",
              '@', "var", "stream-method",
              '(', "value",
                '$', GIBBER_XMPP_NS_IQ_OOB,
              ')',
            ')',
          ')',
        ')',
      ')', NULL);
}

/*
 * gibber_bytestream_oob_accept
 *
 * Implements gibber_bytestream_iface_accept on GibberBytestreamIface
 */
static void
gibber_bytestream_oob_accept (GibberBytestreamIface *bytestream,
                              GibberBytestreamAugmentSiAcceptReply func,
                              gpointer user_data)
{
  GibberBytestreamOOB *self = GIBBER_BYTESTREAM_OOB (bytestream);
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);
  WockyStanza *stanza;
  WockyNode *node;
  WockyNode *si;

  if (priv->state != GIBBER_BYTESTREAM_STATE_LOCAL_PENDING)
    {
      /* The stream was previoulsy or automatically accepted */
      DEBUG ("stream was already accepted");
      return;
    }

  stanza = create_si_accept_iq (self);
  node = wocky_stanza_get_top_node (stanza);
  si = wocky_node_get_child_ns (node, "si", GIBBER_XMPP_NS_SI);
  g_assert (si != NULL);

  if (func != NULL)
    {
      /* let the caller add his profile specific data */
      func (si, user_data);
    }

  wocky_porter_send (priv->porter, stanza);

  DEBUG ("stream is now accepted");
  g_object_set (self, "state", GIBBER_BYTESTREAM_STATE_ACCEPTED, NULL);
  g_object_unref (stanza);
}

static void
bytestream_closed (GibberBytestreamOOB *self)
{
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);

  if (priv->recipient)
    {
      WockyStanza *stanza;

      /* As described in the XEP, we send result IQ when we have
       * finished to use the OOB */
      stanza = make_iq_oob_sucess_response (priv->self_id,
          priv->peer_id, priv->stream_open_id);
      wocky_stanza_set_contact (stanza, priv->contact);

      DEBUG ("send OOB close stanza");

      wocky_porter_send (priv->porter, stanza);
      g_object_unref (stanza);
    }
  else
    {
      /* We are the sender. Don't have to send anything */
    }

  if (priv->transport != NULL)
    {
      g_signal_handlers_disconnect_matched (priv->transport,
          G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
      gibber_transport_disconnect (priv->transport);
      g_object_unref (priv->transport);
      priv->transport = NULL;
    }

  g_object_set (self, "state", GIBBER_BYTESTREAM_STATE_CLOSED, NULL);
}

static void
gibber_bytestream_oob_decline (GibberBytestreamOOB *self,
                               GError *error)
 {
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);
  WockyStanza *stanza;
  WockyNode *node;

  g_return_if_fail (priv->state == GIBBER_BYTESTREAM_STATE_LOCAL_PENDING);

  stanza = wocky_stanza_build_to_contact (
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_ERROR,
      priv->self_id, priv->contact,
      '@', "id", priv->stream_init_id,
      NULL);
  node = wocky_stanza_get_top_node (stanza);

  if (error != NULL && error->domain == GIBBER_XMPP_ERROR)
    {
      gibber_xmpp_error_to_node (error->code, node, error->message);
    }
  else
    {
      gibber_xmpp_error_to_node (XMPP_ERROR_FORBIDDEN, node,
          "Offer Declined");
    }

  wocky_porter_send (priv->porter, stanza);

  g_object_unref (stanza);
}

static void
gibber_bytestream_oob_do_close (GibberBytestreamOOB *self,
                                GError *error,
                                gboolean can_wait)
{
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);

  if (priv->state == GIBBER_BYTESTREAM_STATE_CLOSED)
     /* bytestream already closed, do nothing */
     return;

  if (priv->state == GIBBER_BYTESTREAM_STATE_LOCAL_PENDING)
    {
      /* Stream was created using SI so we decline the request */
      gibber_bytestream_oob_decline (self, error);
    }

  g_object_set (self, "state", GIBBER_BYTESTREAM_STATE_CLOSING, NULL);
  if (can_wait && priv->transport != NULL &&
      !gibber_transport_buffer_is_empty (priv->transport))
    {
      DEBUG ("Wait transport buffer is empty before close the bytestream");
    }
  else
    {
      DEBUG ("Transport buffer is empty, we can close the bytestream");
      bytestream_closed (self);
    }
}

/*
 * gibber_bytestream_oob_close
 *
 * Implements gibber_bytestream_iface_close on GibberBytestreamIface
 */
static void
gibber_bytestream_oob_close (GibberBytestreamIface *bytestream,
                             GError *error)
{
  gibber_bytestream_oob_do_close (GIBBER_BYTESTREAM_OOB (bytestream), error,
      TRUE);
}

static WockyStanza *
make_oob_init_iq (const gchar *from,
                  const gchar *to,
                  const gchar *stream_id,
                  const gchar *url)
{
  return wocky_stanza_build (
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
      from, to,
      '(', "query",
        ':', GIBBER_XMPP_NS_IQ_OOB,
        '@', "sid", stream_id,
        '(', "url",
          '$', url,
        ')',
      ')', NULL);
}

static void
new_connection_cb (GibberListener *listener,
                   GibberTransport *transport,
                   struct sockaddr *addr,
                   guint size,
                   gpointer user_data)
{
  GibberBytestreamOOB *self = GIBBER_BYTESTREAM_OOB (user_data);
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);

  if (priv->check_addr_func != NULL && !priv->check_addr_func (self, addr,
        size, priv->check_addr_func_data))
    {
      DEBUG ("connection refused by the bytestream user");
      return;
    }

  DEBUG("New connection..");

  set_transport (self, transport);
}

/*
 * gibber_bytestream_oob_initiate
 *
 * Implements gibber_bytestream_iface_initiate on GibberBytestreamIface
 */
static gboolean
gibber_bytestream_oob_initiate (GibberBytestreamIface *bytestream)
{
  GibberBytestreamOOB *self = GIBBER_BYTESTREAM_OOB (bytestream);
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);
  WockyStanza *stanza;
  WockyNode *node;
  const gchar *id;
  int port;
  gchar *url;

  if (priv->state != GIBBER_BYTESTREAM_STATE_INITIATING)
    {
      DEBUG ("bytestream is not is the initiating state (state %d)",
          priv->state);
      return FALSE;
    }
  g_assert (priv->host != NULL);

  priv->recipient = FALSE;

  g_assert (priv->listener == NULL);
  priv->listener = gibber_listener_new ();

  g_signal_connect (priv->listener, "new-connection",
      G_CALLBACK (new_connection_cb), self);

  if (!gibber_listener_listen_tcp (priv->listener, 0, NULL))
    {
      DEBUG ("can't listen for incoming connection");
      return FALSE;
    }
  port = gibber_listener_get_port (priv->listener);

  url = g_strdup_printf ("x-tcp://%s:%d", priv->host, port);

  stanza = make_oob_init_iq (priv->self_id, priv->peer_id,
      priv->stream_id, url);
  g_free (url);
  wocky_stanza_set_contact (stanza, priv->contact);
  node = wocky_stanza_get_top_node (stanza);

  id = wocky_node_get_attribute (node, "id");
  if (id == NULL)
    {
      /* let the porter generate the IQ id for us */
      wocky_porter_send_iq_async (priv->porter, stanza,
          NULL, NULL, NULL);

      priv->stream_open_id = g_strdup (
          wocky_node_get_attribute (node, "id"));
    }
  else
    {
      /* save the stream open ID */
      priv->stream_open_id = g_strdup (id);

      wocky_porter_send (priv->porter, stanza);
    }

  g_object_unref (stanza);

  return TRUE;
}

void
gibber_bytestream_oob_set_check_addr_func (
    GibberBytestreamOOB *self,
    GibberBytestreamOOBCheckAddrFunc func,
    gpointer user_data)
{
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);

  priv->check_addr_func = func;
  priv->check_addr_func_data = user_data;
}

static void
gibber_bytestream_oob_block_reading (GibberBytestreamIface *bytestream,
                                     gboolean block)
{
  GibberBytestreamOOB *self = GIBBER_BYTESTREAM_OOB (bytestream);
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);

  if (priv->read_blocked == block)
    return;

  priv->read_blocked = block;

  DEBUG ("%s the transport bytestream", block ? "block": "unblock");
  gibber_transport_block_receiving (priv->transport, block);
}

static void
bytestream_iface_init (gpointer g_iface,
                       gpointer iface_data)
{
  GibberBytestreamIfaceClass *klass = (GibberBytestreamIfaceClass *) g_iface;

  klass->initiate = gibber_bytestream_oob_initiate;
  klass->send = gibber_bytestream_oob_send;
  klass->close = gibber_bytestream_oob_close;
  klass->accept = gibber_bytestream_oob_accept;
  klass->block_reading = gibber_bytestream_oob_block_reading;
}
