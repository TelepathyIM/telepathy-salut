/*
 * gibber-bytestream-direct.c - Source for GibberBytestreamDirect
 * Copyright (C) 2008 Collabora Ltd.
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

#include "gibber-bytestream-direct.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <glib.h>

#include "gibber-xmpp-connection.h"
#include "gibber-linklocal-transport.h"
#include "gibber-util.h"
#include "gibber-xmpp-error.h"

#define DEBUG_FLAG DEBUG_BYTESTREAM
#include "gibber-debug.h"

#include "signals-marshal.h"

static void
bytestream_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GibberBytestreamDirect, gibber_bytestream_direct,
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
  PROP_ADDRESSES = 1,
  PROP_SELF_ID,
  PROP_PEER_ID,
  PROP_STREAM_ID,
  PROP_STATE,

  /* relevent only on recipient side to connect to the initiator */
  PROP_PORT,

  PROP_PROTOCOL,
  LAST_PROPERTY
};

typedef struct _GibberBytestreamDirectPrivate GibberBytestreamDirectPrivate;
struct _GibberBytestreamDirectPrivate
{
  /* A list of struct sockaddr_storage to try to connect to, if
   * this GibberBytestreamDirect object is on the initiator side.
   * GibberBytestreamDirect own theses structures and must free them. */
  GArray *addresses;

  gchar *self_id;
  gchar *peer_id;
  gchar *stream_id;
  GibberBytestreamState state;

  guint portnum;

  /* Are we the recipient of this bytestream?
   * If not we are the sender */
  gboolean recipient;
  GibberTransport *transport;
  gboolean write_blocked;
  gboolean read_blocked;

  GibberBytestreamDirectCheckAddrFunc check_addr_func;
  gpointer check_addr_func_data;

  gboolean dispose_has_run;
};

#define GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE(obj) \
    ((GibberBytestreamDirectPrivate *) obj->priv)

static void
gibber_bytestream_direct_init (GibberBytestreamDirect *self)
{
  GibberBytestreamDirectPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GIBBER_TYPE_BYTESTREAM_DIRECT, GibberBytestreamDirectPrivate);

  self->priv = priv;
}

static void
gibber_bytestream_direct_dispose (GObject *object)
{
  GibberBytestreamDirect *self = GIBBER_BYTESTREAM_DIRECT (object);
  GibberBytestreamDirectPrivate *priv = GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE
      (self);

  if (priv->dispose_has_run)
    return;
  priv->dispose_has_run = TRUE;

  if (priv->state != GIBBER_BYTESTREAM_STATE_CLOSED)
    {
      gibber_bytestream_iface_close (GIBBER_BYTESTREAM_IFACE (self), NULL);
    }

  G_OBJECT_CLASS (gibber_bytestream_direct_parent_class)->dispose (object);
}

static void
gibber_bytestream_direct_finalize (GObject *object)
{
  GibberBytestreamDirect *self = GIBBER_BYTESTREAM_DIRECT (object);
  GibberBytestreamDirectPrivate *priv = GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE
      (self);

  g_free (priv->stream_id);
  g_free (priv->self_id);
  g_free (priv->peer_id);

  if (priv->addresses != NULL)
    g_array_free (priv->addresses, TRUE);

  G_OBJECT_CLASS (gibber_bytestream_direct_parent_class)->finalize (object);
}

static void
gibber_bytestream_direct_get_property (GObject *object,
                                       guint property_id,
                                       GValue *value,
                                       GParamSpec *pspec)
{
  GibberBytestreamDirect *self = GIBBER_BYTESTREAM_DIRECT (object);
  GibberBytestreamDirectPrivate *priv = GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE
      (self);

  switch (property_id)
    {
      case PROP_ADDRESSES:
        g_value_set_pointer (value, priv->addresses);
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
      case PROP_STATE:
        g_value_set_uint (value, priv->state);
        break;
      case PROP_PORT:
        g_value_set_uint (value, priv->portnum);
        break;
      case PROP_PROTOCOL:
        /* this property is not used because direct bytestream are not
         * negociated on XMPP using SI */
        g_value_set_string (value, (const gchar *)"");
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gibber_bytestream_direct_set_property (GObject *object,
                                       guint property_id,
                                       const GValue *value,
                                       GParamSpec *pspec)
{
  GibberBytestreamDirect *self = GIBBER_BYTESTREAM_DIRECT (object);
  GibberBytestreamDirectPrivate *priv = GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE
      (self);

  switch (property_id)
    {
      case PROP_ADDRESSES:
        priv->addresses = g_value_get_pointer (value);
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
      case PROP_STATE:
        if (priv->state != g_value_get_uint (value))
            {
              priv->state = g_value_get_uint (value);
              g_signal_emit (object, signals[STATE_CHANGED], 0, priv->state);
            }
        break;
      case PROP_PORT:
        priv->portnum = g_value_get_uint (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
gibber_bytestream_direct_constructor (GType type,
                                      guint n_props,
                                      GObjectConstructParam *props)
{
  GObject *obj;
  GibberBytestreamDirectPrivate *priv;

  obj = G_OBJECT_CLASS (gibber_bytestream_direct_parent_class)->
           constructor (type, n_props, props);

  priv = GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE (GIBBER_BYTESTREAM_DIRECT (obj));

  g_assert (priv->self_id != NULL);
  g_assert (priv->peer_id != NULL);

  return obj;
}

static void
gibber_bytestream_direct_class_init (
    GibberBytestreamDirectClass *gibber_bytestream_direct_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_bytestream_direct_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gibber_bytestream_direct_class,
      sizeof (GibberBytestreamDirectPrivate));

  object_class->dispose = gibber_bytestream_direct_dispose;
  object_class->finalize = gibber_bytestream_direct_finalize;

  object_class->get_property = gibber_bytestream_direct_get_property;
  object_class->set_property = gibber_bytestream_direct_set_property;
  object_class->constructor = gibber_bytestream_direct_constructor;

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

  param_spec = g_param_spec_pointer (
      "addresses",
      "Array of addresses",
      "GArray of struct sockaddr_storage used to find the IP address to "
      "connect in this bytestream if it's a private one",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_ADDRESSES,
      param_spec);

  param_spec = g_param_spec_uint (
      "port",
      "port",
      "port for the recipient to connect on the initiator",
      0,
      G_MAXUINT32,
      0,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PORT,
      param_spec);

  signals[DATA_RECEIVED] =
    g_signal_new ("data-received",
        G_OBJECT_CLASS_TYPE (gibber_bytestream_direct_class),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
        0,
        NULL, NULL,
        _gibber_signals_marshal_VOID__STRING_POINTER,
        G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_POINTER);

  signals[STATE_CHANGED] =
    g_signal_new ("state-changed",
        G_OBJECT_CLASS_TYPE (gibber_bytestream_direct_class),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
        0,
        NULL, NULL,
        g_cclosure_marshal_VOID__UINT,
        G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[WRITE_BLOCKED] =
    g_signal_new ("write-blocked",
        G_OBJECT_CLASS_TYPE (gibber_bytestream_direct_class),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
        0,
        NULL, NULL,
        g_cclosure_marshal_VOID__BOOLEAN,
        G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

void
gibber_bytestream_direct_set_check_addr_func (
    GibberBytestreamDirect *self,
    GibberBytestreamDirectCheckAddrFunc func,
    gpointer user_data)
{
  GibberBytestreamDirectPrivate *priv =
      GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE (self);

  priv->check_addr_func = func;
  priv->check_addr_func_data = user_data;
}

static void
transport_handler (GibberTransport *transport,
                   GibberBuffer *data,
                   gpointer user_data)
{
  GibberBytestreamDirect *self = GIBBER_BYTESTREAM_DIRECT (user_data);
  GString *buffer;

  DEBUG ("Called. GibberBytestreamDirect emit DATA_RECEIVED.");

  buffer = g_string_new_len ((const gchar *) data->data, data->length);

  g_signal_emit (G_OBJECT (self), signals[DATA_RECEIVED], 0, NULL, buffer);

  g_string_free (buffer, TRUE);
}

static void
transport_connected_cb (GibberTransport *transport,
                        GibberBytestreamDirect *self)
{
  DEBUG ("transport connected. Bytestream is now open");
  g_object_set (self, "state", GIBBER_BYTESTREAM_STATE_OPEN,
      NULL);
}

static void
transport_disconnected_cb (GibberTransport *transport,
                           GibberBytestreamDirect *self)
{
  GibberBytestreamDirectPrivate *priv =
      GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE (self);

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
change_write_blocked_state (GibberBytestreamDirect *self,
                            gboolean blocked)
{
  GibberBytestreamDirectPrivate *priv =
      GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE (self);

  if (priv->write_blocked == blocked)
    return;

  priv->write_blocked = blocked;
  g_signal_emit (self, signals[WRITE_BLOCKED], 0, blocked);
}

static void
bytestream_closed (GibberBytestreamDirect *self)
{
  GibberBytestreamDirectPrivate *priv =
      GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE (self);

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
transport_buffer_empty_cb (GibberTransport *transport,
                           GibberBytestreamDirect *self)
{
  GibberBytestreamDirectPrivate *priv = GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE
      (self);

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
set_transport (GibberBytestreamDirect *self,
               GibberTransport *transport)
{
  GibberBytestreamDirectPrivate *priv = GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE
      (self);

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

gboolean
gibber_bytestream_direct_accept_socket (GibberBytestreamIface *bytestream,
                                        int listen_fd)
{
  GibberBytestreamDirect *self = GIBBER_BYTESTREAM_DIRECT (bytestream);
  GibberBytestreamDirectPrivate *priv;
  GibberLLTransport *ll_transport;
  struct sockaddr_storage addr;
  int fd, ret;
  char host[NI_MAXHOST];
  char port[NI_MAXSERV];
  socklen_t addrlen = sizeof (struct sockaddr_storage);


  priv = GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE (self);

  if (priv->state != GIBBER_BYTESTREAM_STATE_LOCAL_PENDING)
    {
      DEBUG ("bytestream is not is the initiating state (state %d)",
          priv->state);
      return FALSE;
    }

  fd = accept (listen_fd, (struct sockaddr *) &addr, &addrlen);
  gibber_normalize_address (&addr);

  ret = getnameinfo ((struct sockaddr *) &addr, addrlen,
      host, NI_MAXHOST, port, NI_MAXSERV,
      NI_NUMERICHOST | NI_NUMERICSERV);

  if (priv->check_addr_func != NULL && !priv->check_addr_func (self, &addr,
        addrlen, priv->check_addr_func_data))
    {
      DEBUG ("connection from %s refused by the bytestream user", host);
      return FALSE;
    }

  if (ret == 0)
    DEBUG("New connection from %s port %s", host, port);
  else
    DEBUG("New connection..");

  ll_transport = gibber_ll_transport_new ();
  set_transport (self, GIBBER_TRANSPORT (ll_transport));
  gibber_ll_transport_open_fd (ll_transport, fd);

  return TRUE;
}

void
gibber_bytestream_direct_block_read (GibberBytestreamDirect *self,
                                     gboolean block)
{
  GibberBytestreamDirectPrivate *priv =
      GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE (self);

  if (priv->read_blocked == block)
    return;

  priv->read_blocked = block;

  DEBUG ("%s the transport bytestream", block ? "block": "unblock");
  gibber_transport_block_receiving (priv->transport, block);
}

/*
 * gibber_bytestream_direct_send
 *
 * Implements gibber_bytestream_iface_send on GibberBytestreamIface
 */
static gboolean
gibber_bytestream_direct_send (GibberBytestreamIface *bytestream,
                               guint len,
                               const gchar *str)
{
  GibberBytestreamDirect *self = GIBBER_BYTESTREAM_DIRECT (bytestream);
  GibberBytestreamDirectPrivate *priv =
      GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE (self);
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


/*
 * gibber_bytestream_direct_accept
 *
 * Implements gibber_bytestream_iface_accept on GibberBytestreamIface
 */
static void
gibber_bytestream_direct_accept (GibberBytestreamIface *bytestream,
                                 GibberBytestreamAugmentSiAcceptReply func,
                                 gpointer user_data)
{
  /* nothing to do: GibberBytestreamDirect don't use Stream Initiation, so it
   * does not have to send any SI iq stanza to accept the bytestream */
}

/*
 * gibber_bytestream_direct_close
 *
 * Implements gibber_bytestream_iface_close on GibberBytestreamIface
 */
static void
gibber_bytestream_direct_close (GibberBytestreamIface *bytestream,
                             GError *error)
{
  GibberBytestreamDirect *self = GIBBER_BYTESTREAM_DIRECT (bytestream);
  GibberBytestreamDirectPrivate *priv =
      GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE (self);

  if (priv->state == GIBBER_BYTESTREAM_STATE_CLOSED)
     /* bytestream already closed, do nothing */
     return;

  g_object_set (self, "state", GIBBER_BYTESTREAM_STATE_CLOSING, NULL);
  if (priv->transport != NULL &&
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
 * gibber_bytestream_direct_initiate
 * connect to the remote end
 *
 * Implements gibber_bytestream_iface_initiate on GibberBytestreamIface
 */
static gboolean
gibber_bytestream_direct_initiate (GibberBytestreamIface *bytestream)
{
  GibberBytestreamDirect *self = GIBBER_BYTESTREAM_DIRECT (bytestream);
  GibberLLTransport *ll_transport;
  /* never cast addr but type-punning to avoid strict-aliasing issues
   * (see -fstrict-aliasing in man gcc) */
  union {
    struct sockaddr_storage storage;
    struct sockaddr_in6 in6;
  } addr;
  GibberBytestreamDirectPrivate *priv =
      GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE (self);

  DEBUG ("Called.");

  g_assert (priv->addresses != NULL);
  if (priv->addresses->len < 1)
    {
      GError e = { GIBBER_XMPP_ERROR, XMPP_ERROR_ITEM_NOT_FOUND,
          "Unsable get socket address for this contact" };
      DEBUG ("Could not get socket address for this contact" );
      gibber_bytestream_iface_close (GIBBER_BYTESTREAM_IFACE (self), &e);
      return FALSE;
    }

  /* Use the first address for now */
  addr.storage = g_array_index (priv->addresses, struct sockaddr_storage, 0);

  addr.in6.sin6_port = g_htons ((guint16) priv->portnum);

  ll_transport = gibber_ll_transport_new ();
  set_transport (self, GIBBER_TRANSPORT (ll_transport));
  gibber_ll_transport_open_sockaddr (ll_transport,
      &addr.storage, NULL);

  return TRUE;
}

static void
bytestream_iface_init (gpointer g_iface,
                       gpointer iface_data)
{
  GibberBytestreamIfaceClass *klass = (GibberBytestreamIfaceClass *) g_iface;

  klass->initiate = gibber_bytestream_direct_initiate;
  klass->send = gibber_bytestream_direct_send;
  klass->close = gibber_bytestream_direct_close;
  klass->accept = gibber_bytestream_direct_accept;
}
