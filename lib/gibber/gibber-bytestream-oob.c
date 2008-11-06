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
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

#include <glib.h>

#include "gibber-bytestream-iface.h"
#include "gibber-xmpp-connection.h"
#include "gibber-xmpp-stanza.h"
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

#include "signals-marshal.h"

static void
bytestream_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GibberBytestreamOOB, gibber_bytestream_oob,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GIBBER_TYPE_BYTESTREAM_IFACE,
      bytestream_iface_init));

/* signals */
enum
{
  DATA_RECEIVED,
  STATE_CHANGED,
  WRITE_BLOCKED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_XMPP_CONNECTION = 1,
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
  GibberXmppConnection *xmpp_connection;
  gchar *self_id;
  gchar *peer_id;
  gchar *stream_id;
  gchar *stream_init_id;
  /* ID of the OOB opening stanza. We'll reply to
   * it when we the bytestream is closed */
  gchar *stream_open_id;
  GibberBytestreamState state;
  gchar *host;

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
    ((GibberBytestreamOOBPrivate *) (GibberBytestreamOOB *)obj->priv)

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

static GibberXmppStanza *
make_iq_oob_sucess_response (const gchar *from,
                             const gchar *to,
                             const gchar *id)
{
  return gibber_xmpp_stanza_build (
      GIBBER_STANZA_TYPE_IQ, GIBBER_STANZA_SUB_TYPE_RESULT,
      from, to,
      GIBBER_NODE_ATTRIBUTE, "id", id,
      GIBBER_STANZA_END);
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

  g_signal_emit (G_OBJECT (self), signals[DATA_RECEIVED], 0, priv->peer_id,
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
  g_signal_emit (self, signals[WRITE_BLOCKED], 0, blocked);
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

  priv->transport = transport;
  gibber_transport_set_handler (transport, transport_handler, self);

  g_signal_connect (transport, "connected",
      G_CALLBACK (transport_connected_cb), self);
  g_signal_connect (transport, "disconnected",
      G_CALLBACK (transport_disconnected_cb), self);
  g_signal_connect (priv->transport, "buffer-empty",
      G_CALLBACK (transport_buffer_empty_cb), self);
}

static void
connect_to_url (GibberBytestreamOOB *self,
                const gchar *url)
{
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);
  GibberLLTransport *ll_transport;
  gchar **tokens;
  struct sockaddr_storage addr;
  socklen_t len;
  const gchar *host, *port;
  gint portnum = 0;

  if (!g_str_has_prefix (url, "x-tcp://"))
    {
      GError e = { GIBBER_XMPP_ERROR, XMPP_ERROR_ITEM_NOT_FOUND,
          "URL is not a TCP URL" };

      DEBUG ("URL is not a TCP URL: %s. Close the bytestream", url);
      gibber_bytestream_iface_close (GIBBER_BYTESTREAM_IFACE (self), &e);
      return;
    }

  /* TODO: if we want to support IPv6 literals, we have to remove
   * [] around the address */

  url += strlen ("x-tcp://");
  tokens = g_strsplit (url, ":", 2);
  host = tokens[0];
  port = tokens[1];

  /* FIXME, this is very specific to salut and won't work with a normal xmpp
   * client */
  if (!gibber_transport_get_sockaddr (
      GIBBER_TRANSPORT (priv->xmpp_connection->transport),
      &addr, &len))
    {
      /* I'm too lazy to create more specific errors for this  as it should
       * never happen while using salut anyway.. */
      GError e = { GIBBER_XMPP_ERROR, XMPP_ERROR_ITEM_NOT_FOUND,
          "Unsable get socket address for the control connection" };
      DEBUG ("Could not get socket address for the control connection" );
      gibber_bytestream_iface_close (GIBBER_BYTESTREAM_IFACE (self), &e);
      goto out;
    }

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

  ((struct sockaddr_in *) &addr)->sin_port = g_htons ((guint16) portnum);

  ll_transport = gibber_ll_transport_new ();
  set_transport (self, GIBBER_TRANSPORT (ll_transport));
  gibber_ll_transport_open_sockaddr (ll_transport, &addr, NULL);

out:
  g_strfreev (tokens);
}

static gboolean
parse_oob_init_iq (GibberBytestreamOOB *self,
                   GibberXmppStanza *stanza)
{
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);
  GibberXmppNode *query_node, *url_node;
  GibberStanzaType type;
  GibberStanzaSubType sub_type;
  const gchar *stream_id, *url;

  gibber_xmpp_stanza_get_type_info (stanza, &type, &sub_type);

  if (type != GIBBER_STANZA_TYPE_IQ ||
      sub_type != GIBBER_STANZA_SUB_TYPE_SET)
    return FALSE;

  query_node = gibber_xmpp_node_get_child_ns (stanza->node, "query",
      GIBBER_XMPP_NS_OOB);
  if (query_node == NULL)
    return FALSE;

  stream_id = gibber_xmpp_node_get_attribute (query_node, "sid");
  if (stream_id == NULL || strcmp (stream_id, priv->stream_id) != 0)
    return FALSE;

  url_node = gibber_xmpp_node_get_child (query_node, "url");
  if (url_node == NULL)
    return FALSE;
  url = url_node->content;

  priv->recipient = TRUE;
  priv->stream_open_id = g_strdup (gibber_xmpp_node_get_attribute (
        stanza->node, "id"));

  connect_to_url (self, url);

  return TRUE;
}

static gboolean
parse_oob_iq_result (GibberBytestreamOOB *self,
                     GibberXmppStanza *stanza)
{
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);
  GibberStanzaType type;
  GibberStanzaSubType sub_type;
  const gchar *id;

  if (priv->recipient)
    /* Only the sender have to wait for the IQ reply */
    return FALSE;

  gibber_xmpp_stanza_get_type_info (stanza, &type, &sub_type);

  if (type != GIBBER_STANZA_TYPE_IQ ||
      sub_type != GIBBER_STANZA_SUB_TYPE_RESULT)
    return FALSE;

  /* FIXME: we should check if it's the right sender */
  id = gibber_xmpp_node_get_attribute (stanza->node, "id");

  if (id == NULL || strcmp (id, priv->stream_open_id) != 0)
    return FALSE;

  DEBUG ("received OOB close stanza - ignoring");

  return TRUE;
}

static void
xmpp_connection_received_stanza_cb (GibberXmppConnection *conn,
                                    GibberXmppStanza *stanza,
                                    gpointer user_data)
{
  GibberBytestreamOOB *self = (GibberBytestreamOOB *) user_data;
  const gchar *from;

  /* discard invalid stanza */
  from = gibber_xmpp_node_get_attribute (stanza->node, "from");
  if (from == NULL)
    {
      DEBUG ("got a message without a from field");
      return;
    }

  if (parse_oob_init_iq (self, stanza))
    return;

  if (parse_oob_iq_result (self, stanza))
    return;
}

static void
xmpp_connection_stream_closed_cb (GibberXmppConnection *connection,
                                  gpointer userdata)
{
  GibberBytestreamOOB *self = GIBBER_BYTESTREAM_OOB (userdata);
  DEBUG ("XMPP connection: stream closed. Close the OOB bytestream");
  g_object_set (self, "state", GIBBER_BYTESTREAM_STATE_CLOSING, NULL);
  bytestream_closed (self);
}

static void
xmpp_connection_transport_disconnected_cb (GibberLLTransport *transport,
                                           gpointer userdata)
{
  GibberBytestreamOOB *self = GIBBER_BYTESTREAM_OOB (userdata);
  DEBUG ("XMPP connection transport closed. Close the OOB bytestream");
  g_object_set (self, "state", GIBBER_BYTESTREAM_STATE_CLOSING, NULL);
  bytestream_closed (self);
}

static void
xmpp_connection_parse_error_cb (GibberXmppConnection *connection,
                                gpointer userdata)
{
  GibberBytestreamOOB *self = GIBBER_BYTESTREAM_OOB (userdata);
  DEBUG ("XMPP connection: parse error. Close the OOB bytestream");
  g_object_set (self, "state", GIBBER_BYTESTREAM_STATE_CLOSING, NULL);
  bytestream_closed (self);
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
    }

  if (priv->xmpp_connection != NULL)
    {
      g_signal_handlers_disconnect_matched (priv->xmpp_connection->transport,
          G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
      g_signal_handlers_disconnect_matched (priv->xmpp_connection,
          G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
      g_object_unref (priv->xmpp_connection);
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
      case PROP_XMPP_CONNECTION:
        g_value_set_object (value, priv->xmpp_connection);
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
        g_value_set_string (value, GIBBER_XMPP_NS_OOB);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
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
      case PROP_XMPP_CONNECTION:
        priv->xmpp_connection = g_value_get_object (value);
        g_signal_connect (priv->xmpp_connection, "received-stanza",
            G_CALLBACK (xmpp_connection_received_stanza_cb), self);
        g_signal_connect (priv->xmpp_connection, "stream-closed",
            G_CALLBACK (xmpp_connection_stream_closed_cb), self);
        g_signal_connect (priv->xmpp_connection->transport, "disconnected",
           G_CALLBACK (xmpp_connection_transport_disconnected_cb), self);
        g_signal_connect (priv->xmpp_connection, "parse-error",
           G_CALLBACK (xmpp_connection_parse_error_cb), self);
        g_object_ref (priv->xmpp_connection);
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
              g_signal_emit (object, signals[STATE_CHANGED], 0, priv->state);
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
  g_assert (priv->xmpp_connection != NULL);

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
      "xmpp-connection",
      "GibberXmppConnection object",
      "Gibber XMPP connection object used for communication by this "
      "bytestream if it's a private one",
      GIBBER_TYPE_XMPP_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_XMPP_CONNECTION,
      param_spec);

  param_spec = g_param_spec_string (
      "stream-init-id",
      "stream init ID",
      "the iq ID of the SI request, if any",
      "",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STREAM_INIT_ID,
      param_spec);

  param_spec = g_param_spec_string (
      "host",
      "host",
      "The hostname to use in the OOB URL. Literal are not allowed",
      "",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HOST,
      param_spec);

  signals[DATA_RECEIVED] =
    g_signal_new ("data-received",
                  G_OBJECT_CLASS_TYPE (gibber_bytestream_oob_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  _gibber_signals_marshal_VOID__STRING_POINTER,
                  G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_POINTER);

  signals[STATE_CHANGED] =
    g_signal_new ("state-changed",
                  G_OBJECT_CLASS_TYPE (gibber_bytestream_oob_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[WRITE_BLOCKED] =
    g_signal_new ("write-blocked",
                  G_OBJECT_CLASS_TYPE (gibber_bytestream_oob_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOOLEAN,
                  G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
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
      DEBUG ("can't send data for now, bytestream is blocked");
      return FALSE;
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

static GibberXmppStanza *
create_si_accept_iq (GibberBytestreamOOB *self)
{
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);

  return gibber_xmpp_stanza_build (
      GIBBER_STANZA_TYPE_IQ, GIBBER_STANZA_SUB_TYPE_RESULT,
      priv->self_id, priv->peer_id,
      GIBBER_NODE_ATTRIBUTE, "id", priv->stream_init_id,
      GIBBER_NODE, "si",
        GIBBER_NODE_XMLNS, GIBBER_XMPP_NS_SI,
        GIBBER_NODE, "feature",
          GIBBER_NODE_XMLNS, GIBBER_XMPP_NS_FEATURENEG,
          GIBBER_NODE, "x",
            GIBBER_NODE_XMLNS, GIBBER_XMPP_NS_DATA,
            GIBBER_NODE_ATTRIBUTE, "type", "submit",
            GIBBER_NODE, "field",
              GIBBER_NODE_ATTRIBUTE, "var", "stream-method",
              GIBBER_NODE, "value",
                GIBBER_NODE_TEXT, GIBBER_XMPP_NS_OOB,
              GIBBER_NODE_END,
            GIBBER_NODE_END,
          GIBBER_NODE_END,
        GIBBER_NODE_END,
      GIBBER_NODE_END, GIBBER_STANZA_END);
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
  GibberXmppStanza *stanza;
  GibberXmppNode *si;

  if (priv->state != GIBBER_BYTESTREAM_STATE_LOCAL_PENDING)
    {
      /* The stream was previoulsy or automatically accepted */
      DEBUG ("stream was already accepted");
      return;
    }

  stanza = create_si_accept_iq (self);
  si = gibber_xmpp_node_get_child_ns (stanza->node, "si", GIBBER_XMPP_NS_SI);
  g_assert (si != NULL);

  if (func != NULL)
    {
      /* let the caller add his profile specific data */
      func (si, user_data);
    }

  gibber_xmpp_connection_send (priv->xmpp_connection, stanza, NULL);

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
      /* We are the recipient and so have to send the reply
       * to the OOB opening IQ */
      if (priv->xmpp_connection->stream_flags ==
          GIBBER_XMPP_CONNECTION_STREAM_FULLY_OPEN)
        {
          GibberXmppStanza *stanza;

          /* As described in the XEP, we send result IQ when we have
           * finished to use the OOB */
          stanza = make_iq_oob_sucess_response (priv->self_id,
              priv->peer_id, priv->stream_open_id);

          DEBUG ("send OOB close stanza");

          gibber_xmpp_connection_send (priv->xmpp_connection, stanza, NULL);
          g_object_unref (stanza);
        }
      else
        {
          DEBUG ("XMPP connection is closed. Don't send OOB close stanza");
        }
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
  GibberXmppStanza *stanza;

  g_return_if_fail (priv->state == GIBBER_BYTESTREAM_STATE_LOCAL_PENDING);

  stanza = gibber_xmpp_stanza_build (
      GIBBER_STANZA_TYPE_IQ, GIBBER_STANZA_SUB_TYPE_ERROR,
      priv->self_id, priv->peer_id,
      GIBBER_NODE_ATTRIBUTE, "id", priv->stream_init_id,
      GIBBER_STANZA_END);

  if (error != NULL && error->domain == GIBBER_XMPP_ERROR)
    {
      gibber_xmpp_error_to_node (error->code, stanza->node, error->message);
    }
  else
    {
      gibber_xmpp_error_to_node (XMPP_ERROR_FORBIDDEN, stanza->node,
          "Offer Declined");
    }

  gibber_xmpp_connection_send (priv->xmpp_connection, stanza, NULL);

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

static GibberXmppStanza *
make_oob_init_iq (const gchar *from,
                  const gchar *to,
                  const gchar *stream_id,
                  const gchar *url)
{
  return gibber_xmpp_stanza_build (
      GIBBER_STANZA_TYPE_IQ, GIBBER_STANZA_SUB_TYPE_SET,
      from, to,
      GIBBER_NODE, "query",
        GIBBER_NODE_XMLNS, GIBBER_XMPP_NS_OOB,
        GIBBER_NODE_ATTRIBUTE, "sid", stream_id,
        GIBBER_NODE, "url",
          GIBBER_NODE_TEXT, url,
        GIBBER_NODE_END,
      GIBBER_NODE_END, GIBBER_STANZA_END);
}

static void
new_connection_cb (GibberListener *listener,
                   GibberTransport *transport,
                   struct sockaddr_storage *addr,
                   guint size,
                   gpointer user_data)
{
  GibberBytestreamOOB *self = GIBBER_BYTESTREAM_OOB (user_data);
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);
  socklen_t addrlen = sizeof (struct sockaddr_storage);

  if (priv->check_addr_func != NULL && !priv->check_addr_func (self, addr,
        addrlen, priv->check_addr_func_data))
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
gboolean
gibber_bytestream_oob_initiate (GibberBytestreamIface *bytestream)
{
  GibberBytestreamOOB *self = GIBBER_BYTESTREAM_OOB (bytestream);
  GibberBytestreamOOBPrivate *priv = GIBBER_BYTESTREAM_OOB_GET_PRIVATE (self);
  GibberXmppStanza *stanza;
  GError *error = NULL;
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

  g_signal_connect (gibber_listener_new, "new-connection",
      G_CALLBACK (new_connection_cb), self);

  port = gibber_listener_listen_tcp (priv->listener, 0, NULL);

  if (port <= 0)
    {
      DEBUG ("can't listen for incoming connection");
      return FALSE;
    }

  url = g_strdup_printf ("x-tcp://%s:%d", priv->host, port);

  stanza = make_oob_init_iq (priv->self_id, priv->peer_id,
      priv->stream_id, url);
  g_free (url);

  id = gibber_xmpp_node_get_attribute (stanza->node, "id");
  if (id == NULL)
    {
      priv->stream_open_id = gibber_xmpp_connection_new_id (
          priv->xmpp_connection);
      gibber_xmpp_node_set_attribute (stanza->node, "id",
          priv->stream_open_id);
    }
  else
    {
      priv->stream_open_id = g_strdup (id);
    }

  if (!gibber_xmpp_connection_send (priv->xmpp_connection, stanza, &error))
    {
      DEBUG ("can't send OOB init stanza: %s", error->message);
      return FALSE;
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

void
gibber_bytestream_oob_block_read (GibberBytestreamOOB *self,
                                  gboolean block)
{
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
}
