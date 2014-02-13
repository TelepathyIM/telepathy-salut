/*
 * tube-stream.c - Source for SalutTubeStream
 * Copyright (C) 2007-2008 Collabora Ltd.
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
#include "tube-stream.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winbase.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#endif

#include <glib/gstdio.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include <wocky/wocky.h>

#include <gibber/gibber-bytestream-direct.h>
#include <gibber/gibber-bytestream-iface.h>
#include <gibber/gibber-bytestream-oob.h>
#include <gibber/gibber-fd-transport.h>
#include <gibber/gibber-listener.h>
#include <gibber/gibber-tcp-transport.h>
#include <gibber/gibber-transport.h>
#include <gibber/gibber-unix-transport.h>

#define DEBUG_FLAG DEBUG_TUBES

#include "debug.h"
#include "connection.h"
#include "muc-tube-stream.h"
#include "tube-iface.h"
#include "si-bytestream-manager.h"
#include "contact-manager.h"
#include "util.h"

static void tube_iface_init (gpointer g_iface, gpointer iface_data);
static void streamtube_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (SalutTubeStream, salut_tube_stream,
    TP_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE (SALUT_TYPE_TUBE_IFACE, tube_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_STREAM_TUBE1,
      streamtube_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_TUBE1,
      NULL));

static const gchar * const salut_tube_stream_channel_allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    TP_PROP_CHANNEL_TYPE_STREAM_TUBE1_SERVICE,
    NULL
};

/* Linux glibc bits/socket.h suggests that struct sockaddr_storage is
 * not guaranteed to be big enough for AF_UNIX addresses */
typedef union
{
#ifdef GIBBER_TYPE_UNIX_TRANSPORT
  /* we'd call this unix, but gcc predefines that. Thanks, gcc */
  struct sockaddr_un un;
#endif
  struct sockaddr_in ipv4;
  struct sockaddr_in6 ipv6;
} SockAddr;

/* signals */
enum
{
  OPENED,
  NEW_CONNECTION,
  CLOSED,
  OFFERED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_SELF_HANDLE = 1,
  PROP_ID,
  PROP_TYPE,
  PROP_SERVICE,
  PROP_PARAMETERS,
  PROP_STATE,
  PROP_OFFERED,
  PROP_ADDRESS_TYPE,
  PROP_ADDRESS,
  PROP_ACCESS_CONTROL,
  PROP_ACCESS_CONTROL_PARAM,
  PROP_PORT,
  PROP_IQ_REQ,
  PROP_SUPPORTED_SOCKET_TYPES,
  LAST_PROPERTY
};

typedef struct _SalutTubeStreamPrivate SalutTubeStreamPrivate;
struct _SalutTubeStreamPrivate
{
  TpHandle self_handle;
  guint64 id;
  guint port;
  WockyStanza *iq_req;

  /* Bytestreams for MUC tubes (using stream initiation) or 1-1 tubes (using
   * direct TCP connections). One tube can have several bytestreams. The
   * mapping between the tube bytestream and the transport to the local
   * application is stored in the transport_to_bytestream and
   * bytestream_to_transport fields. This is used both on initiator-side and
   * on recipient-side. */

  /* (GibberBytestreamIface *) -> (GibberTransport *)
   *
   * The (b->t) is inserted as soon as they are created. On initiator side,
   * we receive an incoming bytestream, create a transport and insert (b->t).
   * On recipient side, we receive an incoming transport, create a bytestream
   * and insert (b->t).
   */
  GHashTable *bytestream_to_transport;

  /* (GibberTransport *) -> (GibberBytestreamIface *)
   *
   * The (t->b) is inserted when the bytestream is open.
   */
  GHashTable *transport_to_bytestream;

  /* (GibberTransport *) -> guint */
  GHashTable *transport_to_id;
  guint last_connection_id;

  gchar *service;
  GHashTable *parameters;
  TpTubeChannelState state;
  /* whether the tube is already offered at construct-time (with the
   * Channel.Type.Tubes interface) */
  gboolean offered;

  TpSocketAddressType address_type;
  GValue *address;
  TpSocketAccessControl access_control;
  GValue *access_control_param;

  /* listen for connections from local applications */
  GibberListener *local_listener;

  /* listen for connections from the remote CM */
  GibberListener *contact_listener;

  gboolean offer_needed;

  gboolean dispose_has_run;
};

#define SALUT_TUBE_STREAM_GET_PRIVATE(obj) \
    ((SalutTubeStreamPrivate *) ((SalutTubeStream *) obj)->priv)

static void data_received_cb (GibberBytestreamIface *ibb, TpHandle sender,
    GString *data, gpointer user_data);

static void salut_tube_stream_add_bytestream (SalutTubeIface *tube,
    GibberBytestreamIface *bytestream);

static void
generate_ascii_string (guint len,
                       gchar *buf)
{
  const gchar *chars =
    "0123456789"
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "_-";
  guint i;

  for (i = 0; i < len; i++)
    buf[i] = chars[g_random_int_range (0, 64)];
}

static void
transport_handler (GibberTransport *transport,
                   GibberBuffer *data,
                   gpointer user_data)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (user_data);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  GibberBytestreamIface *bytestream;

  bytestream = g_hash_table_lookup (priv->transport_to_bytestream, transport);
  if (bytestream == NULL)
    {
      DEBUG ("no open bytestream associated with this transport");
      return;
    }

  DEBUG ("read %" G_GSIZE_FORMAT " bytes from socket", data->length);

  gibber_bytestream_iface_send (bytestream, data->length,
      (const gchar *) data->data);
}

static void
fire_connection_closed (SalutTubeStream *self,
    GibberTransport *transport,
    const gchar *error,
    const gchar *debug_msg)
{
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  guint connection_id;

  connection_id = GPOINTER_TO_UINT (g_hash_table_lookup (priv->transport_to_id,
        transport));
  if (connection_id == 0)
    {
      DEBUG ("ConnectionClosed has already been fired for this connection");
      return;
    }

  /* remove the ID so we are sure we won't fire ConnectionClosed twice for the
   * same connection. */
  g_hash_table_remove (priv->transport_to_id, transport);

  tp_svc_channel_type_stream_tube1_emit_connection_closed (self,
      connection_id, error, debug_msg);
}

static void
transport_disconnected_cb (GibberTransport *transport,
                           SalutTubeStream *self)
{
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  GibberBytestreamIface *bytestream;

  fire_connection_closed (self, transport, TP_ERROR_STR_CANCELLED,
      "local socket has been disconnected");

  bytestream = g_hash_table_lookup (priv->transport_to_bytestream, transport);
  if (bytestream == NULL)
    return;

  DEBUG ("transport disconnected. close the extra bytestream");

  gibber_bytestream_iface_close (bytestream, NULL);
}

static void
remove_transport (SalutTubeStream *self,
                  GibberBytestreamIface *bytestream,
                  GibberTransport *transport)
{
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);

  DEBUG ("disconnect and remove transport");
  g_signal_handlers_disconnect_matched (transport, G_SIGNAL_MATCH_DATA,
      0, 0, NULL, NULL, self);

  gibber_transport_disconnect (transport);

  fire_connection_closed (self, transport, TP_ERROR_STR_CONNECTION_LOST,
      "bytestream has been broken");

  /* the transport may not be in transport_to_bytestream if the bytestream was
   * not fully open */
  g_hash_table_remove (priv->transport_to_bytestream, transport);

  g_hash_table_remove (priv->bytestream_to_transport, bytestream);
  g_hash_table_remove (priv->transport_to_id, transport);
}

static void
transport_buffer_empty_cb (GibberTransport *transport,
                           SalutTubeStream *self)
{
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  GibberBytestreamIface *bytestream;
  GibberBytestreamState state;

  bytestream = g_hash_table_lookup (priv->transport_to_bytestream, transport);
  g_assert (bytestream != NULL);
  g_object_get (bytestream, "state", &state, NULL);

  if (state == GIBBER_BYTESTREAM_STATE_CLOSED)
    {
      DEBUG ("buffer is now empty. Transport can be removed");
      remove_transport (self, bytestream, transport);
      return;
    }

  /* Buffer is empty so we can unblock the buffer if it was blocked */
  DEBUG ("tube buffer is empty. Unblock the bytestream");
  gibber_bytestream_iface_block_reading (bytestream, FALSE);
}

static void
add_transport (SalutTubeStream *self,
               GibberTransport *transport,
               GibberBytestreamIface *bytestream)
{
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);

  gibber_transport_set_handler (transport, transport_handler, self);

  g_hash_table_insert (priv->transport_to_bytestream,
      g_object_ref (transport), g_object_ref (bytestream));

  g_signal_connect (transport, "disconnected",
      G_CALLBACK (transport_disconnected_cb), self);
  g_signal_connect (transport, "buffer-empty",
      G_CALLBACK (transport_buffer_empty_cb), self);

  /* We can transfer transport's data; unblock it. */
  gibber_transport_block_receiving (transport, FALSE);
}

static void
bytestream_write_blocked_cb (GibberBytestreamIface *bytestream,
                             gboolean blocked,
                             SalutTubeStream *self)
{
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  GibberTransport *transport;

  transport = g_hash_table_lookup (priv->bytestream_to_transport,
      bytestream);
  g_assert (transport != NULL);

  if (blocked)
    {
      DEBUG ("bytestream blocked, stop to read data from the tube socket");
    }
  else
    {
      DEBUG ("bytestream unblocked, restart to read data from the tube socket");
    }

  gibber_transport_block_receiving (transport, blocked);
}

static void
extra_bytestream_state_changed_cb (GibberBytestreamIface *bytestream,
                                   GibberBytestreamState state,
                                   gpointer user_data)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (user_data);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);

  if (state == GIBBER_BYTESTREAM_STATE_OPEN)
    {
      GibberTransport *transport;

      DEBUG ("extra bytestream open");

      g_signal_connect (bytestream, "data-received",
          G_CALLBACK (data_received_cb), self);
      g_signal_connect (bytestream, "write-blocked",
          G_CALLBACK (bytestream_write_blocked_cb), self);

      transport = g_hash_table_lookup (priv->bytestream_to_transport,
            bytestream);
      g_assert (transport != NULL);

      add_transport (self, transport, bytestream);
    }
  else if (state == GIBBER_BYTESTREAM_STATE_CLOSED)
    {
      GibberTransport *transport;

      DEBUG ("extra bytestream closed");
      transport = g_hash_table_lookup (priv->bytestream_to_transport,
          bytestream);
      if (transport != NULL)
        {
          if (gibber_transport_buffer_is_empty (transport))
            {
              DEBUG ("Buffer is empty, we can remove the transport");
              remove_transport (self, bytestream, transport);
            }
          else
            {
              DEBUG ("Wait buffer is empty before disconnect the transport");
            }
        }
    }
}

struct _extra_bytestream_negotiate_cb_data
{
  SalutTubeStream *self;
  /* transport from the local application */
  GibberTransport *transport;
};

static void
extra_bytestream_negotiate_cb (GibberBytestreamIface *bytestream,
                               gpointer user_data)
{
  struct _extra_bytestream_negotiate_cb_data *data =
    (struct _extra_bytestream_negotiate_cb_data *) user_data;
  SalutTubeStream *self = data->self;
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);

  if (bytestream == NULL)
    {
      DEBUG ("initiator refused new bytestream");

      fire_connection_closed (self, data->transport,
          TP_ERROR_STR_CONNECTION_REFUSED, "connection has been refused");

      g_object_unref (data->transport);
      g_slice_free (struct _extra_bytestream_negotiate_cb_data, data);
      return;
    }

  DEBUG ("extra bytestream accepted");

  g_hash_table_insert (priv->bytestream_to_transport, g_object_ref (bytestream),
      data->transport);

  g_signal_connect (bytestream, "state-changed",
      G_CALLBACK (extra_bytestream_state_changed_cb), self);

  g_slice_free (struct _extra_bytestream_negotiate_cb_data, data);
}

/* XXX we should move that in some kind of bytestream factory */
static gchar *
generate_stream_id (SalutTubeStream *self)
{
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseChannelClass *cls = TP_BASE_CHANNEL_GET_CLASS (base);
  gchar *stream_id;

  if (cls->target_entity_type == TP_ENTITY_TYPE_CONTACT)
    {
      stream_id = g_strdup_printf ("%lu-%u", (unsigned long) time (NULL),
          g_random_int ());
    }
  else
    {
      /* GibberMucConnection's stream-id is a guint8 */
      stream_id = g_strdup_printf ("%u", g_random_int_range (1, G_MAXUINT8));
    }

  return stream_id;
}

static gboolean
start_stream_initiation (SalutTubeStream *self,
                         GibberTransport *transport,
                         GError **error)
{
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseChannelClass *cls = TP_BASE_CHANNEL_GET_CLASS (base);
  TpHandle initiator = tp_base_channel_get_initiator (base);
  WockyNode *node, *si_node;
  WockyStanza *msg;
  WockyNode *msg_node;
  TpHandleRepoIface *contact_repo, *room_repo;
  const gchar *jid;
  gchar *stream_id, *id_str;
  gboolean result;
  struct _extra_bytestream_negotiate_cb_data *data;
  SalutContact *contact;
  SalutContactManager *contact_mgr;
  SalutSiBytestreamManager *si_bytestream_mgr;
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  SalutConnection *conn = SALUT_CONNECTION (base_conn);

  contact_repo = tp_base_connection_get_handles (base_conn,
      TP_ENTITY_TYPE_CONTACT);
  room_repo = tp_base_connection_get_handles (base_conn,
      TP_ENTITY_TYPE_ROOM);

  jid = tp_handle_inspect (contact_repo, initiator);

  stream_id = generate_stream_id (self);

  msg = salut_si_bytestream_manager_make_stream_init_iq (conn->name, jid,
      stream_id, WOCKY_TELEPATHY_NS_TUBES);
  msg_node = wocky_stanza_get_top_node (msg);

  si_node = wocky_node_get_child_ns (msg_node, "si", WOCKY_XMPP_NS_SI);
  g_assert (si_node != NULL);

  id_str = g_strdup_printf ("%" G_GUINT64_FORMAT, priv->id);

  g_assert (cls->target_entity_type == TP_ENTITY_TYPE_ROOM);

  /* FIXME: this needs standardizing */
  node = wocky_node_add_child_ns (si_node, "muc-stream",
      WOCKY_TELEPATHY_NS_TUBES);
  wocky_node_set_attribute (node, "muc", tp_handle_inspect (
          room_repo, tp_base_channel_get_target_handle (base)));

  wocky_node_set_attribute (node, "tube", id_str);

  data = g_slice_new (struct _extra_bytestream_negotiate_cb_data);
  data->self = self;
  data->transport = g_object_ref (transport);

  g_object_get (conn,
      "si-bytestream-manager", &si_bytestream_mgr,
      "contact-manager", &contact_mgr,
      NULL);
  g_assert (si_bytestream_mgr != NULL);
  g_assert (contact_mgr != NULL);

  contact = salut_contact_manager_get_contact (contact_mgr, initiator);
  if (contact == NULL)
    {
      result = FALSE;
      g_set_error (error, TP_ERROR, TP_ERROR_NETWORK_ERROR,
          "can't find contact with handle %d", initiator);
      g_object_unref (transport);
      g_slice_free (struct _extra_bytestream_negotiate_cb_data, data);
    }
  else
    {
      wocky_stanza_set_to_contact (msg, WOCKY_CONTACT (contact));

      result = salut_si_bytestream_manager_negotiate_stream (
        si_bytestream_mgr,
        contact,
        msg,
        stream_id,
        extra_bytestream_negotiate_cb,
        data,
        error);

      /* FIXME: data and one ref on data->transport are leaked if the tube is
       * closed before we got the SI reply. */

      g_object_unref (contact);
    }

  if (!result)
    {
      g_object_unref (data->transport);
      g_slice_free (struct _extra_bytestream_negotiate_cb_data, data);
    }

  g_object_unref (si_bytestream_mgr);
  g_object_unref (contact_mgr);
  g_object_unref (msg);
  g_free (stream_id);
  g_free (id_str);

  return result;
}

/* start a new stream in a tube from the recipient side */
static gboolean
start_stream_direct (SalutTubeStream *self,
                     GibberTransport *transport,
                     GError **error)
{
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseChannelClass *cls = TP_BASE_CHANNEL_GET_CLASS (base);
  TpHandle initiator = tp_base_channel_get_initiator (base);
  SalutContact *contact;
  SalutContactManager *contact_mgr;
  GibberBytestreamIface *bytestream;

  g_assert (cls->target_entity_type == TP_ENTITY_TYPE_CONTACT);

  g_object_get (tp_base_channel_get_connection (base),
      "contact-manager", &contact_mgr,
      NULL);
  g_assert (contact_mgr != NULL);

  contact = salut_contact_manager_get_contact (contact_mgr, initiator);
  if (contact == NULL)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_NETWORK_ERROR,
          "can't find contact with handle %d", initiator);

      g_object_unref (contact_mgr);

      return FALSE;
    }

  bytestream = g_object_new (GIBBER_TYPE_BYTESTREAM_DIRECT,
      "addresses", salut_contact_get_addresses (contact),
      "state", GIBBER_BYTESTREAM_STATE_LOCAL_PENDING,
      "peer-id", contact->name,
      "port", priv->port,
      NULL);

  g_assert (bytestream != NULL);

  g_hash_table_insert (priv->bytestream_to_transport,
      bytestream, g_object_ref (transport));

  g_signal_connect (bytestream, "state-changed",
      G_CALLBACK (extra_bytestream_state_changed_cb), self);

  /* Let's start the initiation of the stream */
  if (!gibber_bytestream_iface_initiate (bytestream))
    {
      /* Initiation failed. */
      gibber_bytestream_iface_close (bytestream, NULL);

      g_object_unref (contact);
      g_object_unref (contact_mgr);

      return FALSE;
    }

  g_object_unref (contact);
  g_object_unref (contact_mgr);

  return TRUE;
}

static guint
generate_connection_id (SalutTubeStream *self,
    GibberTransport *transport)
{
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);

  priv->last_connection_id++;

  g_hash_table_insert (priv->transport_to_id, transport,
      GUINT_TO_POINTER (priv->last_connection_id));

  return priv->last_connection_id;
}

static void
fire_new_local_connection (SalutTubeStream *self,
    GibberTransport *transport)
{
  guint connection_id;

  connection_id = generate_connection_id (self, transport);

  tp_svc_channel_type_stream_tube1_emit_new_local_connection (self,
      connection_id);
}

/* callback for listening connections from the local application */
static void
local_new_connection_cb (GibberListener *listener,
                         GibberTransport *transport,
                         struct sockaddr_storage *addr,
                         guint size,
                         gpointer user_data)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (user_data);
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseChannelClass *cls = TP_BASE_CHANNEL_GET_CLASS (base);

  /* Block the transport while there is no open bytestream to transfer
   * its data. */
  gibber_transport_block_receiving (transport, TRUE);

  /* Streams in MUC tubes are established with stream initiation (XEP-0095).
   * We use SalutSiBytestreamManager.
   *
   * Streams in P2P tubes are established directly with a TCP connection. We
   * use SalutDirectBytestreamManager.
   */
  if (cls->target_entity_type == TP_ENTITY_TYPE_CONTACT)
    {
      if (!start_stream_direct (self, transport, NULL))
        {
          DEBUG ("closing new client connection");
          return;
        }
    }
  else
    {
      if (!start_stream_initiation (self, transport, NULL))
        {
          DEBUG ("closing new client connection");
          return;
        }
    }

  fire_new_local_connection (self, transport);
}

static void
fire_new_remote_connection (SalutTubeStream *self,
    GibberTransport *transport,
    TpHandle contact,
    const gchar *contact_id)
{
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  GValue access_control_param = {0,};
  guint connection_id;

  g_assert (priv->access_control == TP_SOCKET_ACCESS_CONTROL_LOCALHOST);

  /* set a dummy value */
  g_value_init (&access_control_param, G_TYPE_INT);
  g_value_set_int (&access_control_param, 0);

  connection_id = GPOINTER_TO_UINT (g_hash_table_lookup (priv->transport_to_id,
        transport));
  g_assert (connection_id != 0);

  tp_svc_channel_type_stream_tube1_emit_new_remote_connection (self,
      contact, contact_id, &access_control_param, connection_id);
  g_value_unset (&access_control_param);
}

static GibberTransport *
new_connection_to_socket (SalutTubeStream *self,
                          GibberBytestreamIface *bytestream)
{
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  GibberTransport *transport;

  g_assert (tp_base_channel_is_requested (base));

#ifdef GIBBER_TYPE_UNIX_TRANSPORT
  if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_UNIX)
    {
      GArray *array;
      array = g_value_get_boxed (priv->address);
      DEBUG ("Will try to connect to socket: %s", (const gchar *) array->data);

      transport = GIBBER_TRANSPORT (gibber_unix_transport_new ());
      gibber_unix_transport_connect (GIBBER_UNIX_TRANSPORT (transport),
          array->data, NULL);
    }
  else
#endif
  if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_IPV4 ||
      priv->address_type == TP_SOCKET_ADDRESS_TYPE_IPV6)
    {
      gchar *ip;
      gchar *port_str;
      guint port;

      dbus_g_type_struct_get (priv->address,
          0, &ip,
          1, &port,
          G_MAXUINT);
      port_str = g_strdup_printf ("%d", port);

      transport = GIBBER_TRANSPORT (gibber_tcp_transport_new ());
      gibber_tcp_transport_connect (GIBBER_TCP_TRANSPORT (transport), ip,
          port_str);

      g_free (ip);
      g_free (port_str);
    }
  else
    {
      g_assert_not_reached ();
    }

  /* Block the transport while there is no open bytestream to transfer
   * its data. */
  gibber_transport_block_receiving (transport, TRUE);

  generate_connection_id (self, transport);

  g_hash_table_insert (priv->bytestream_to_transport, g_object_ref (bytestream),
      g_object_ref (transport));

  g_signal_connect (bytestream, "state-changed",
      G_CALLBACK (extra_bytestream_state_changed_cb), self);

  g_object_unref (transport);
  return transport;
}

static gboolean
tube_stream_open (SalutTubeStream *self,
                  GError **error)
{
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  TpBaseChannel *base = TP_BASE_CHANNEL (self);

  DEBUG ("called");

  if (tp_base_channel_is_requested (base))
    /* Nothing to do if we are the initiator of this tube.
     * We'll connect to the socket each time request a new bytestream. */
    return TRUE;

  /* We didn't create this tube so it doesn't have
   * a socket associated with it. Let's create one */
  g_assert (priv->address == NULL);
  g_assert (priv->local_listener == NULL);
  priv->local_listener = gibber_listener_new ();

  g_signal_connect (priv->local_listener, "new-connection",
      G_CALLBACK (local_new_connection_cb), self);

  if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_UNIX)
    {
      GArray *array;
      gchar suffix[8];
      gchar *path;
      int ret;

      generate_ascii_string (8, suffix);
      path = g_strdup_printf ("/tmp/stream-%.8s", suffix);

      DEBUG ("create socket: %s", path);

      array = g_array_sized_new (TRUE, FALSE, sizeof (gchar), strlen (path));
      g_array_insert_vals (array, 0, path, strlen (path));

      priv->address = tp_g_value_slice_new (DBUS_TYPE_G_UCHAR_ARRAY);
      g_value_set_boxed (priv->address, array);

      g_array_unref (array);

      ret = gibber_listener_listen_socket (priv->local_listener, path, FALSE,
          error);
      if (ret != TRUE)
        {
          g_assert (error != NULL && *error != NULL);
          DEBUG ("Error listening on socket %s: %s", path, (*error)->message);
          g_free (path);
          return FALSE;
        }

      if (priv->access_control == TP_SOCKET_ACCESS_CONTROL_LOCALHOST)
        {
          /* Everyone can use the socket */
          chmod (path, 0777);
        }

      g_free (path);
    }
  else if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_IPV4)
    {
      int ret;

      ret = gibber_listener_listen_tcp_loopback_af (priv->local_listener, 0,
          GIBBER_AF_IPV4, error);
      if (!ret)
        {
          g_assert (error != NULL && *error != NULL);
          DEBUG ("Error listening on socket: %s", (*error)->message);
          return FALSE;
        }

      priv->address = tp_g_value_slice_new (TP_STRUCT_TYPE_SOCKET_ADDRESS_IPV4);
      g_value_take_boxed (priv->address,
          dbus_g_type_specialized_construct (
            TP_STRUCT_TYPE_SOCKET_ADDRESS_IPV4));

      dbus_g_type_struct_set (priv->address,
          0, "127.0.0.1",
          1, gibber_listener_get_port (priv->local_listener),
          G_MAXUINT);
    }
  else if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_IPV6)
    {
      int ret;

      ret = gibber_listener_listen_tcp_loopback_af (priv->local_listener, 0,
          GIBBER_AF_IPV6, error);
      if (!ret)
        {
          g_assert (error != NULL && *error != NULL);
          DEBUG ("Error listening on socket: %s", (*error)->message);
          return FALSE;
        }

      priv->address = tp_g_value_slice_new (TP_STRUCT_TYPE_SOCKET_ADDRESS_IPV6);
      g_value_take_boxed (priv->address,
          dbus_g_type_specialized_construct (
            TP_STRUCT_TYPE_SOCKET_ADDRESS_IPV6));

      dbus_g_type_struct_set (priv->address,
          0, "::1",
          1, gibber_listener_get_port (priv->local_listener),
          G_MAXUINT);
    }
  else
    {
      g_assert_not_reached ();
    }

  return TRUE;
}

static void
salut_tube_stream_init (SalutTubeStream *self)
{
  SalutTubeStreamPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_TUBE_STREAM, SalutTubeStreamPrivate);

  self->priv = priv;

  priv->transport_to_bytestream = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, (GDestroyNotify) g_object_unref,
      (GDestroyNotify) g_object_unref);

  priv->bytestream_to_transport = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, (GDestroyNotify) g_object_unref,
      (GDestroyNotify) g_object_unref);

  priv->transport_to_id = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, NULL);
  priv->last_connection_id = 0;

  priv->address_type = TP_SOCKET_ADDRESS_TYPE_UNIX;
  priv->address = NULL;
  priv->access_control = TP_SOCKET_ACCESS_CONTROL_LOCALHOST;
  priv->access_control_param = NULL;
  priv->offer_needed = FALSE;

  priv->dispose_has_run = FALSE;
}

static gboolean
close_each_extra_bytestream (gpointer key,
                             gpointer value,
                             gpointer user_data)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (user_data);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  GibberTransport *transport = (GibberTransport *) value;
  GibberBytestreamIface *bytestream = (GibberBytestreamIface *) key;

  /* We are iterating over priv->transport_to_bytestream so we can't modify it.
   * Disconnect signals so extra_bytestream_state_changed_cb won't be
   * called */
  g_signal_handlers_disconnect_matched (bytestream, G_SIGNAL_MATCH_DATA,
      0, 0, NULL, NULL, self);

  g_signal_handlers_disconnect_matched (transport, G_SIGNAL_MATCH_DATA,
      0, 0, NULL, NULL, self);

  gibber_bytestream_iface_close (bytestream, NULL);
  gibber_transport_disconnect (transport);
  fire_connection_closed (self, transport, TP_ERROR_STR_CANCELLED,
      "tube is closing");

  g_hash_table_remove (priv->transport_to_bytestream, transport);

  return TRUE;
}

static void
salut_tube_stream_dispose (GObject *object)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (object);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  TpBaseChannel *base = TP_BASE_CHANNEL (self);

  if (priv->dispose_has_run)
    return;

  salut_tube_iface_close (SALUT_TUBE_IFACE (self), FALSE);

  if (tp_base_channel_is_requested (base) &&
      priv->address_type == TP_SOCKET_ADDRESS_TYPE_UNIX &&
      priv->address != NULL)
    {
      /* We created a new UNIX socket. Let's delete it */
      GArray *array;
      GString *path;

      array = g_value_get_boxed (priv->address);
      path = g_string_new_len (array->data, array->len);

      if (g_unlink (path->str) != 0)
        {
          DEBUG ("unlink of %s failed: %s", path->str, g_strerror (errno));
        }

      g_string_free (path, TRUE);
    }

  if (priv->transport_to_bytestream != NULL)
    {
      g_hash_table_unref (priv->transport_to_bytestream);
      priv->transport_to_bytestream = NULL;
    }

  if (priv->bytestream_to_transport != NULL)
    {
      g_hash_table_unref (priv->bytestream_to_transport);
      priv->bytestream_to_transport = NULL;
    }

  if (priv->transport_to_id != NULL)
    {
      g_hash_table_unref (priv->transport_to_id);
      priv->transport_to_id = NULL;
    }

  if (priv->local_listener != NULL)
    {
      g_object_unref (priv->local_listener);
      priv->local_listener = NULL;
    }

  if (priv->contact_listener != NULL)
    {
      g_object_unref (priv->contact_listener);
      priv->contact_listener = NULL;
    }

  priv->dispose_has_run = TRUE;

  if (G_OBJECT_CLASS (salut_tube_stream_parent_class)->dispose)
    G_OBJECT_CLASS (salut_tube_stream_parent_class)->dispose (object);
}

static void
salut_tube_stream_finalize (GObject *object)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (object);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);

  g_free (priv->service);
  if (priv->parameters != NULL)
    {
      g_hash_table_unref (priv->parameters);
      priv->parameters = NULL;
    }

  if (priv->address != NULL)
    {
      tp_g_value_slice_free (priv->address);
      priv->address = NULL;
    }

  if (priv->access_control_param != NULL)
    {
      tp_g_value_slice_free (priv->access_control_param);
      priv->access_control_param = NULL;
    }

  G_OBJECT_CLASS (salut_tube_stream_parent_class)->finalize (object);
}

static void
salut_tube_stream_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (object);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_SELF_HANDLE:
        g_value_set_uint (value, priv->self_handle);
        break;
      case PROP_ID:
        g_value_set_uint64 (value, priv->id);
        break;
      case PROP_TYPE:
        g_value_set_uint (value, SALUT_TUBE_TYPE_STREAM);
        break;
      case PROP_SERVICE:
        g_value_set_string (value, priv->service);
        break;
      case PROP_PARAMETERS:
        g_value_set_boxed (value, priv->parameters);
        break;
      case PROP_STATE:
        g_value_set_uint (value, priv->state);
        break;
      case PROP_OFFERED:
        g_value_set_boolean (value, priv->offered);
        break;
      case PROP_ADDRESS_TYPE:
        g_value_set_uint (value, priv->address_type);
        break;
      case PROP_ADDRESS:
        g_value_set_pointer (value, priv->address);
        break;
      case PROP_ACCESS_CONTROL:
        g_value_set_uint (value, priv->access_control);
        break;
      case PROP_ACCESS_CONTROL_PARAM:
        g_value_set_pointer (value, priv->access_control_param);
        break;
      case PROP_PORT:
        g_value_set_uint (value, priv->port);
        break;
      case PROP_IQ_REQ:
        g_value_set_pointer (value, priv->iq_req);
        break;
      case PROP_SUPPORTED_SOCKET_TYPES:
        g_value_take_boxed (value,
            salut_tube_stream_get_supported_socket_types ());
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_tube_stream_set_property (GObject *object,
                                 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (object);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_SELF_HANDLE:
        priv->self_handle = g_value_get_uint (value);
        break;
      case PROP_ID:
        priv->id = g_value_get_uint64 (value);
        break;
      case PROP_SERVICE:
        g_free (priv->service);
        priv->service = g_value_dup_string (value);
        break;
      case PROP_PARAMETERS:
        if (priv->parameters != NULL)
          g_hash_table_unref (priv->parameters);
        priv->parameters = g_value_dup_boxed (value);
        break;
      case PROP_OFFERED:
        priv->offered = g_value_get_boolean (value);
        break;
      case PROP_ADDRESS_TYPE:
        g_assert (g_value_get_uint (value) == TP_SOCKET_ADDRESS_TYPE_UNIX ||
            g_value_get_uint (value) == TP_SOCKET_ADDRESS_TYPE_IPV4 ||
            g_value_get_uint (value) == TP_SOCKET_ADDRESS_TYPE_IPV6);
        priv->address_type = g_value_get_uint (value);
        break;
      case PROP_ADDRESS:
        if (priv->address == NULL)
          {
            priv->address = tp_g_value_slice_dup (g_value_get_pointer (value));
          }
        break;
      case PROP_ACCESS_CONTROL:
        /* For now, only "localhost" control is implemented */
        g_assert (g_value_get_uint (value) ==
            TP_SOCKET_ACCESS_CONTROL_LOCALHOST);
        priv->access_control = g_value_get_uint (value);
        break;
      case PROP_ACCESS_CONTROL_PARAM:
        if (priv->access_control_param == NULL)
          {
            priv->access_control_param = tp_g_value_slice_dup (
                g_value_get_pointer (value));
          }
        break;
      case PROP_PORT:
        priv->port = g_value_get_uint (value);
        break;
      case PROP_IQ_REQ:
        priv->iq_req = g_value_get_pointer (value);
        if (priv->iq_req != NULL)
          g_object_ref (priv->iq_req);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
salut_tube_stream_constructor (GType type,
                                guint n_props,
                                GObjectConstructParam *props)
{
  GObject *obj;
  SalutTubeStreamPrivate *priv;
  TpBaseChannel *base;

  obj = G_OBJECT_CLASS (salut_tube_stream_parent_class)->
           constructor (type, n_props, props);

  priv = SALUT_TUBE_STREAM_GET_PRIVATE (SALUT_TUBE_STREAM (obj));

  base = TP_BASE_CHANNEL (obj);

  if (tp_base_channel_get_initiator (base) == priv->self_handle)
    {
      /* We initiated this tube */
      priv->state = TP_TUBE_CHANNEL_STATE_NOT_OFFERED;
      /* FIXME: we should probably remove this offer_needed */
      priv->offer_needed = TRUE;
    }
  else
    {
      priv->state = TP_TUBE_CHANNEL_STATE_LOCAL_PENDING;
    }

  DEBUG ("Registering at '%s'", tp_base_channel_get_object_path (base));

  return obj;
}

static void
salut_tube_stream_fill_immutable_properties (TpBaseChannel *chan,
    GHashTable *properties)
{
  TpBaseChannelClass *cls = TP_BASE_CHANNEL_CLASS (
      salut_tube_stream_parent_class);

  cls->fill_immutable_properties (chan, properties);

  tp_dbus_properties_mixin_fill_properties_hash (
      G_OBJECT (chan), properties,
      TP_IFACE_CHANNEL_TYPE_STREAM_TUBE1, "Service",
      TP_IFACE_CHANNEL_TYPE_STREAM_TUBE1, "SupportedSocketTypes",
      NULL);

  if (!tp_base_channel_is_requested (chan))
    {
      tp_dbus_properties_mixin_fill_properties_hash (
          G_OBJECT (chan), properties,
          TP_IFACE_CHANNEL_INTERFACE_TUBE1, "Parameters",
          NULL);
    }
}

static gchar *
salut_tube_stream_get_object_path_suffix (TpBaseChannel *base)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (base);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);

  return g_strdup_printf ("StreamTubeChannel/%u/%" G_GUINT64_FORMAT,
      tp_base_channel_get_target_handle (base),
      priv->id);
}

static void
salut_tube_stream_close_dbus (TpBaseChannel *base)
{
  salut_tube_iface_close ((SalutTubeIface *) base, FALSE);
}

static GPtrArray *
salut_tube_stream_get_interfaces (TpBaseChannel *chan)
{
  GPtrArray *interfaces = TP_BASE_CHANNEL_CLASS (salut_tube_stream_parent_class)
    ->get_interfaces (chan);

  g_ptr_array_add (interfaces, TP_IFACE_CHANNEL_INTERFACE_TUBE1);
  return interfaces;
}

static void
salut_tube_stream_class_init (SalutTubeStreamClass *salut_tube_stream_class)
{
  static TpDBusPropertiesMixinPropImpl stream_tube_props[] = {
      { "Service", "service", NULL },
      { "SupportedSocketTypes", "supported-socket-types", NULL },
      { NULL }
  };
  static TpDBusPropertiesMixinPropImpl tube_iface_props[] = {
      { "Parameters", "parameters", NULL },
      { "State", "state", NULL },
      { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { TP_IFACE_CHANNEL_TYPE_STREAM_TUBE1,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        stream_tube_props,
      },
      { TP_IFACE_CHANNEL_INTERFACE_TUBE1,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        tube_iface_props,
      },
      { NULL }
  };

  GObjectClass *object_class = G_OBJECT_CLASS (salut_tube_stream_class);
  TpBaseChannelClass *base_class = TP_BASE_CHANNEL_CLASS (salut_tube_stream_class);
  GParamSpec *param_spec;

  object_class->get_property = salut_tube_stream_get_property;
  object_class->set_property = salut_tube_stream_set_property;
  object_class->constructor = salut_tube_stream_constructor;

  base_class->channel_type = TP_IFACE_CHANNEL_TYPE_STREAM_TUBE1;
  base_class->get_interfaces = salut_tube_stream_get_interfaces;
  base_class->target_entity_type = TP_ENTITY_TYPE_CONTACT;
  base_class->close = salut_tube_stream_close_dbus;
  base_class->fill_immutable_properties =
    salut_tube_stream_fill_immutable_properties;
  base_class->get_object_path_suffix =
    salut_tube_stream_get_object_path_suffix;

  g_type_class_add_private (salut_tube_stream_class,
      sizeof (SalutTubeStreamPrivate));

  object_class->dispose = salut_tube_stream_dispose;
  object_class->finalize = salut_tube_stream_finalize;

  g_object_class_override_property (object_class, PROP_SELF_HANDLE,
    "self-handle");
  g_object_class_override_property (object_class, PROP_ID,
    "id");
  g_object_class_override_property (object_class, PROP_TYPE,
    "type");
  g_object_class_override_property (object_class, PROP_SERVICE,
    "service");
  g_object_class_override_property (object_class, PROP_PARAMETERS,
    "parameters");
  g_object_class_override_property (object_class, PROP_STATE,
    "state");

  param_spec = g_param_spec_uint (
      "address-type",
      "address type",
      "a TpSocketAddressType representing the type of the listening"
      "address of the local service",
      0, TP_NUM_SOCKET_ADDRESS_TYPES - 1,
      TP_SOCKET_ADDRESS_TYPE_UNIX,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ADDRESS_TYPE,
      param_spec);

  param_spec = g_param_spec_pointer (
      "address",
      "address",
      "The listening address of the local service, as indicated by the "
      "address-type",
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ADDRESS, param_spec);

  param_spec = g_param_spec_uint (
      "access-control",
      "access control",
      "a TpSocketAccessControl representing the access control "
      "the local service applies to the local socket",
      0, TP_NUM_SOCKET_ACCESS_CONTROLS - 1,
      TP_SOCKET_ACCESS_CONTROL_LOCALHOST,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ACCESS_CONTROL,
      param_spec);

  param_spec = g_param_spec_pointer (
      "access-control-param",
      "access control param",
      "A parameter for the access control type, to be interpreted as specified"
      "in the documentation for the Socket_Access_Control enum.",
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ACCESS_CONTROL_PARAM,
      param_spec);

  param_spec = g_param_spec_uint (
      "port",
      "port on the initiator's CM",
      "New stream in this tube will connect to the initiator's CM on this port"
      " in case of 1-1 tube",
      0,
      G_MAXUINT32,
      0,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PORT, param_spec);

  param_spec = g_param_spec_pointer (
      "iq-req",
      "A reference on the request stanza",
      "A reference on the request stanza used to reply to "
      "the iq request later",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_IQ_REQ, param_spec);

  param_spec = g_param_spec_boolean (
      "offered",
      "Whether the application asked to offer the tube",
      "Whether the application asked to offer the tube",
      FALSE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_OFFERED, param_spec);

  param_spec = g_param_spec_boxed (
      "supported-socket-types",
      "Supported socket types",
      "GHashTable containing supported socket types.",
      dbus_g_type_get_map ("GHashTable", G_TYPE_UINT, DBUS_TYPE_G_UINT_ARRAY),
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SUPPORTED_SOCKET_TYPES,
      param_spec);

  signals[OPENED] =
    g_signal_new ("tube-opened",
                  G_OBJECT_CLASS_TYPE (salut_tube_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  signals[NEW_CONNECTION] =
    g_signal_new ("tube-new-connection",
                  G_OBJECT_CLASS_TYPE (salut_tube_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[CLOSED] =
    g_signal_new ("tube-closed",
                  G_OBJECT_CLASS_TYPE (salut_tube_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  signals[OFFERED] =
    g_signal_new ("tube-offered",
                  G_OBJECT_CLASS_TYPE (salut_tube_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  salut_tube_stream_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (SalutTubeStreamClass, dbus_props_class));
}

static void
data_received_cb (GibberBytestreamIface *bytestream,
                  TpHandle sender,
                  GString *data,
                  gpointer user_data)
{
  SalutTubeStream *tube = SALUT_TUBE_STREAM (user_data);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (tube);
  GibberTransport *transport;
  GError *error = NULL;

  DEBUG ("received %" G_GSIZE_FORMAT " bytes from bytestream", data->len);

  transport = g_hash_table_lookup (priv->bytestream_to_transport, bytestream);
  g_assert (transport != NULL);

  /* If something goes wrong when trying to write the data on the transport,
   * it could be disconnected, causing its removal from the hash tables.
   * When removed, the transport would be destroyed as the hash tables keep a
   * ref on it and so we'll call _buffer_is_empty on a destroyed transport.
   * We avoid that by reffing the transport between the 2 calls so we keep it
   * artificially alive if needed. */
  g_object_ref (transport);
  if (!gibber_transport_send (transport, (const guint8 *) data->str, data->len,
      &error))
  {
    DEBUG ("sending failed: %s", error->message);
    g_error_free (error);
    g_object_unref (transport);
    return;
  }

  if (!gibber_transport_buffer_is_empty (transport))
    {
      /* We don't want to send more data while the buffer isn't empty */
      DEBUG ("tube buffer isn't empty. Block the bytestream");
      gibber_bytestream_iface_block_reading (bytestream, TRUE);
    }
  g_object_unref (transport);
}

SalutTubeStream *
salut_tube_stream_new (SalutConnection *conn,
                       TpHandle handle,
                       TpEntityType handle_type,
                       TpHandle self_handle,
                       TpHandle initiator,
                       gboolean offered,
                       const gchar *service,
                       GHashTable *parameters,
                       guint64 id,
                       guint portnum,
                       WockyStanza *iq_req,
                       gboolean requested)
{
  SalutTubeStream *obj;
  GType gtype = SALUT_TYPE_TUBE_STREAM;

  if (handle_type == TP_ENTITY_TYPE_ROOM)
    gtype = SALUT_TYPE_MUC_TUBE_STREAM;

  obj = g_object_new (gtype,
      "connection", conn,
      "handle", handle,
      "self-handle", self_handle,
      "initiator-handle", initiator,
      "offered", offered,
      "service", service,
      "parameters", parameters,
      "id", id,
      "port", portnum,
      "iq-req", iq_req,
      "requested", requested,
      NULL);

  return obj;
}

/**
 * salut_tube_stream_accept
 *
 * Implements salut_tube_iface_accept on SalutTubeIface
 */
static gboolean
salut_tube_stream_accept (SalutTubeIface *tube,
                          GError **error)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (tube);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseChannelClass *cls = TP_BASE_CHANNEL_GET_CLASS (base);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  SalutConnection *conn = SALUT_CONNECTION (base_conn);
  WockyStanza *reply;

  if (priv->state != TP_TUBE_CHANNEL_STATE_LOCAL_PENDING)
    return TRUE;

  if (!tube_stream_open (self, error))
    {
      salut_tube_iface_close (SALUT_TUBE_IFACE (self), FALSE);
      return FALSE;
    }

  if (cls->target_entity_type == TP_ENTITY_TYPE_CONTACT)
    {
      reply = wocky_stanza_build_iq_result (priv->iq_req, NULL);
      wocky_porter_send (conn->porter, reply);

      g_object_unref (priv->iq_req);
      priv->iq_req = NULL;
      g_object_unref (reply);
    }

  priv->state = TP_TUBE_CHANNEL_STATE_OPEN;
  g_signal_emit (G_OBJECT (self), signals[OPENED], 0);

  tp_svc_channel_interface_tube1_emit_tube_channel_state_changed (
      self, TP_TUBE_CHANNEL_STATE_OPEN);

  return TRUE;
}

/**
 * salut_tube_stream_accepted
 *
 * Implements salut_tube_iface_accepted on SalutTubeIface
 */
static void
salut_tube_stream_accepted (SalutTubeIface *tube)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (tube);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);

  if (priv->state == TP_TUBE_CHANNEL_STATE_OPEN)
    return;

  priv->state = TP_TUBE_CHANNEL_STATE_OPEN;
  g_signal_emit (G_OBJECT (self), signals[OPENED], 0);

  tp_svc_channel_interface_tube1_emit_tube_channel_state_changed (
      self, TP_TUBE_CHANNEL_STATE_OPEN);
}

/**
 * salut_tube_stream_offer_needed
 *
 * Implements salut_tube_iface_offer_needed on SalutTubeIface
 */
static gboolean
salut_tube_stream_offer_needed (SalutTubeIface *tube)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (tube);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  gboolean ret = priv->offer_needed;

  priv->offer_needed = FALSE;

  return ret;
}

/* callback for listening connections from the contact's CM */
static void
contact_new_connection_cb (GibberListener *listener,
                          GibberTransport *transport,
                          struct sockaddr_storage *addr,
                          guint size,
                          gpointer user_data)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (user_data);
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseChannelClass *cls = TP_BASE_CHANNEL_GET_CLASS (base);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  SalutConnection *conn = SALUT_CONNECTION (base_conn);
  GibberBytestreamIface *bytestream;
  SalutContactManager *contact_mgr;
  SalutContact *contact;

  g_assert (cls->target_entity_type == TP_ENTITY_TYPE_CONTACT);

  g_object_get (conn,
      "contact-manager", &contact_mgr,
      NULL);
  g_assert (contact_mgr != NULL);

  contact = salut_contact_manager_get_contact (contact_mgr,
      tp_base_channel_get_target_handle (base));
  if (contact == NULL)
    {
      DEBUG ("can't find contact with handle %d",
          tp_base_channel_get_target_handle (base));
      g_object_unref (contact_mgr);
      return;
    }

  bytestream = g_object_new (GIBBER_TYPE_BYTESTREAM_DIRECT,
      "state", GIBBER_BYTESTREAM_STATE_LOCAL_PENDING,
      "self-id", conn->name,
      "peer-id", contact->name,
      NULL);

  g_assert (bytestream != NULL);

  salut_tube_stream_add_bytestream (SALUT_TUBE_IFACE (self), bytestream);
  gibber_bytestream_direct_accept_socket (bytestream, transport);

  g_object_unref (bytestream);
  g_object_unref (contact);
  g_object_unref (contact_mgr);
}

/**
 * salut_tube_stream_listem
 *
 * Implements salut_tube_iface_listen on SalutTubeIface
 */
static int
salut_tube_stream_listen (SalutTubeIface *tube)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (tube);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  int ret;

  g_assert (priv->contact_listener == NULL);
  priv->contact_listener = gibber_listener_new ();

  g_signal_connect (priv->contact_listener, "new-connection",
      G_CALLBACK (contact_new_connection_cb), self);

  ret = gibber_listener_listen_tcp (priv->contact_listener, 0, NULL);
  if (ret == TRUE)
    return gibber_listener_get_port (priv->contact_listener);
  return -1;
}

static void
iq_close_reply_cb (GObject *source_object,
                   GAsyncResult *result,
                   gpointer user_data)
{
  WockyPorter *porter = WOCKY_PORTER (source_object);
  GError *error = NULL;
  WockyStanza *stanza;

  stanza = wocky_porter_send_iq_finish (porter, result, &error);

  if (stanza == NULL)
    {
      DEBUG ("Failed to close IQ: %s", error->message);
      g_clear_error (&error);
      return;
    }

  g_object_unref (stanza);
}

/**
 * salut_tube_stream_close
 *
 * Implements salut_tube_iface_close on SalutTubeIface
 */
static void
salut_tube_stream_close (SalutTubeIface *tube, gboolean closed_remotely)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (tube);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseChannelClass *cls = TP_BASE_CHANNEL_GET_CLASS (base);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  SalutConnection *conn = SALUT_CONNECTION (base_conn);

  if (tp_base_channel_is_destroyed (base))
    return;

  g_hash_table_foreach_remove (priv->bytestream_to_transport,
      close_each_extra_bytestream, self);

  /* do not send the close stanza if the tube was closed due to the remote
   * contact */
  if (!closed_remotely && cls->target_entity_type == TP_ENTITY_TYPE_CONTACT)
    {
      WockyStanza *stanza;
      const gchar *jid_from;
      TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
          base_conn, TP_ENTITY_TYPE_CONTACT);
      gchar *tube_id_str;
      SalutContactManager *contact_mgr;
      SalutContact *contact;

      jid_from = tp_handle_inspect (contact_repo, priv->self_handle);
      tube_id_str = g_strdup_printf ("%" G_GUINT64_FORMAT, priv->id);

      g_object_get (conn, "contact-manager", &contact_mgr, NULL);
      g_assert (contact_mgr != NULL);

      contact = salut_contact_manager_get_contact (contact_mgr,
          tp_base_channel_get_target_handle (base));

      stanza = wocky_stanza_build_to_contact (WOCKY_STANZA_TYPE_IQ,
          WOCKY_STANZA_SUB_TYPE_SET,
          jid_from, WOCKY_CONTACT (contact),
          '(', "close",
            ':', WOCKY_TELEPATHY_NS_TUBES,
            '@', "id", tube_id_str,
          ')', NULL);

      wocky_porter_send_iq_async (conn->porter, stanza,
          NULL, iq_close_reply_cb, tube);

      g_free (tube_id_str);

      g_object_unref (stanza);
      g_object_unref (contact);
      g_object_unref (contact_mgr);
    }

  if (cls->target_entity_type == TP_ENTITY_TYPE_CONTACT)
    {
      if (priv->contact_listener != NULL)
        {
          g_object_unref (priv->contact_listener);
          priv->contact_listener = NULL;
        }
    }

  /* Take a ref to ourselves as when we emit tube-closed
   * SalutTubesChannel will drop our last ref but we still need to
   * declare ourselves as destroyed. this is rubbish, but will
   * disappear when we finally remove the Tubes channel type. */
  g_object_ref (self);

  g_signal_emit (G_OBJECT (self), signals[CLOSED], 0);

  tp_base_channel_destroyed (base);

  g_object_unref (self);
}

static void
augment_si_accept_iq (WockyNode *si,
                      gpointer user_data)
{
  wocky_node_add_child_ns (si, "tube", WOCKY_TELEPATHY_NS_TUBES);
}

/**
 * salut_tube_stream_add_bytestream
 *
 * Implements salut_tube_iface_add_bytestream on SalutTubeIface
 */
static void
salut_tube_stream_add_bytestream (SalutTubeIface *tube,
                                  GibberBytestreamIface *bytestream)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (tube);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  GibberTransport *transport;

  if (!tp_base_channel_is_requested (base))
    {
      DEBUG ("I'm not the initiator of this tube, can't accept "
          "an extra bytestream");

      gibber_bytestream_iface_close (bytestream, NULL);
      return;
    }

  /* New bytestream, let's connect to the socket */
  transport = new_connection_to_socket (self, bytestream);
  if (transport != NULL)
    {
      TpHandle contact;
      gchar *peer_id;
      TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
          base_conn, TP_ENTITY_TYPE_CONTACT);

      if (priv->state == TP_TUBE_CHANNEL_STATE_REMOTE_PENDING)
        {
          DEBUG ("Received first connection. Tube is now open");
          priv->state = TP_TUBE_CHANNEL_STATE_OPEN;
          g_signal_emit (G_OBJECT (self), signals[OPENED], 0);
        }

      DEBUG ("accept the extra bytestream");

      gibber_bytestream_iface_accept (bytestream, augment_si_accept_iq, self);

      g_object_get (bytestream, "peer-id", &peer_id, NULL);
      contact = tp_handle_ensure (contact_repo, peer_id, NULL, NULL);

      g_signal_emit (G_OBJECT (self), signals[NEW_CONNECTION], 0, contact);

      fire_new_remote_connection (self, transport, contact, peer_id);

      g_free (peer_id);
    }
  else
    {
      gibber_bytestream_iface_close (bytestream, NULL);
    }
}

#ifdef GIBBER_TYPE_UNIX_TRANSPORT
static gboolean
check_unix_params (TpSocketAddressType address_type,
                   const GValue *address,
                   TpSocketAccessControl access_control,
                   const GValue *access_control_param,
                   GError **error)
{
  GArray *array;
  GString *socket_path;
  struct stat stat_buff;
  guint i;
  struct sockaddr_un dummy;

  g_assert (address_type == TP_SOCKET_ADDRESS_TYPE_UNIX);

  /* Check address type */
  if (G_VALUE_TYPE (address) != DBUS_TYPE_G_UCHAR_ARRAY)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Unix socket address is supposed to be ay");
      return FALSE;
    }

  array = g_value_get_boxed (address);

  if (array->len > sizeof (dummy.sun_path) - 1)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Unix socket path is too long (max length allowed: %"
          G_GSIZE_FORMAT ")",
          sizeof (dummy.sun_path) - 1);
      return FALSE;
    }

  for (i = 0; i < array->len; i++)
    {
      if (g_array_index (array, gchar , i) == '\0')
        {
          g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
              "Unix socket path can't contain zero bytes");
          return FALSE;
        }
    }

  socket_path = g_string_new_len (array->data, array->len);

  if (g_stat (socket_path->str, &stat_buff) == -1)
  {
    DEBUG ("Error calling stat on socket: %s", g_strerror (errno));

    g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT, "%s: %s",
        socket_path->str, g_strerror (errno));
    g_string_free (socket_path, TRUE);
    return FALSE;
  }

  if (!S_ISSOCK (stat_buff.st_mode))
  {
    DEBUG ("%s is not a socket", socket_path->str);

    g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
        "%s is not a socket", socket_path->str);
    g_string_free (socket_path, TRUE);
    return FALSE;
  }

  g_string_free (socket_path, TRUE);

  if (access_control != TP_SOCKET_ACCESS_CONTROL_LOCALHOST)
  {
    g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
        "Unix sockets only support localhost control access");
    return FALSE;
  }

  return TRUE;
}
#endif

static gboolean
check_ip_params (TpSocketAddressType address_type,
                 const GValue *address,
                 TpSocketAccessControl access_control,
                 const GValue *access_control_param,
                 GError **error)
{
  gchar *ip;
  guint port;
  struct addrinfo req, *result = NULL;
  int ret;

  /* Check address type */
  if (address_type == TP_SOCKET_ADDRESS_TYPE_IPV4)
    {
      if (G_VALUE_TYPE (address) != TP_STRUCT_TYPE_SOCKET_ADDRESS_IPV4)
        {
          g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
              "IPv4 socket address is supposed to be sq");
          return FALSE;
        }
    }
  else if (address_type == TP_SOCKET_ADDRESS_TYPE_IPV6)
    {
      if (G_VALUE_TYPE (address) != TP_STRUCT_TYPE_SOCKET_ADDRESS_IPV6)
        {
          g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
              "IPv6 socket address is supposed to be sq");
          return FALSE;
        }
    }
  else
    {
      g_assert_not_reached ();
    }

  dbus_g_type_struct_get (address,
      0, &ip,
      1, &port,
      G_MAXUINT);

  memset (&req, 0, sizeof (req));
  req.ai_flags = AI_NUMERICHOST;
  req.ai_socktype = SOCK_STREAM;
  req.ai_protocol = IPPROTO_TCP;

  if (address_type == TP_SOCKET_ADDRESS_TYPE_IPV4)
    req.ai_family = AF_INET;
  else
    req.ai_family = AF_INET6;

  ret = getaddrinfo (ip, NULL, &req, &result);
  if (ret != 0)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Invalid address: %s", gai_strerror (ret));
      g_free (ip);
      return FALSE;
    }

  g_free (ip);
  freeaddrinfo (result);

  if (access_control != TP_SOCKET_ACCESS_CONTROL_LOCALHOST)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "%s sockets only support localhost control access",
          (address_type == TP_SOCKET_ADDRESS_TYPE_IPV4 ? "IPv4" : "IPv6"));
      return FALSE;
    }

  return TRUE;
}

gboolean
salut_tube_stream_check_params (TpSocketAddressType address_type,
                                const GValue *address,
                                TpSocketAccessControl access_control,
                                const GValue *access_control_param,
                                GError **error)
{
  switch (address_type)
    {
#ifdef GIBBER_TYPE_UNIX_TRANSPORT
      case TP_SOCKET_ADDRESS_TYPE_UNIX:
        return check_unix_params (address_type, address, access_control,
            access_control_param, error);
#endif
      case TP_SOCKET_ADDRESS_TYPE_IPV4:
      case TP_SOCKET_ADDRESS_TYPE_IPV6:
        return check_ip_params (address_type, address, access_control,
            access_control_param, error);

      default:
        g_set_error (error, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
            "Address type %d not implemented", address_type);
        return FALSE;
    }
}

/**
 * salut_tube_stream_offer_async
 *
 * Implements D-Bus method Offer
 * on im.telepathy.v1.Channel.Type.StreamTube
 */
static void
salut_tube_stream_offer_async (TpSvcChannelTypeStreamTube1 *iface,
    guint address_type,
    const GValue *address,
    guint access_control,
    GHashTable *parameters,
    DBusGMethodInvocation *context)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (iface);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  GError *error = NULL;

  if (priv->state != TP_TUBE_CHANNEL_STATE_NOT_OFFERED)
    {
      g_set_error (&error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not in the not offered state");
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  if (!salut_tube_stream_check_params (address_type, address,
        access_control, NULL, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  g_assert (address_type == TP_SOCKET_ADDRESS_TYPE_UNIX ||
      address_type == TP_SOCKET_ADDRESS_TYPE_IPV4 ||
      address_type == TP_SOCKET_ADDRESS_TYPE_IPV6);
  g_assert (priv->address == NULL);
  priv->address_type = address_type;
  priv->address = tp_g_value_slice_dup (address);
  g_assert (priv->access_control == TP_SOCKET_ACCESS_CONTROL_LOCALHOST);
  priv->access_control = access_control;
  g_assert (priv->access_control_param == NULL);

  g_object_set (self, "parameters", parameters, NULL);

  if (!salut_tube_stream_offer (self, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  tp_svc_channel_type_stream_tube1_return_from_offer (context);
}

/**
 * salut_tube_stream_accept_async
 *
 * Implements D-Bus method Accept
 * on im.telepathy.v1.Channel.Type.StreamTube
 */
static void
salut_tube_stream_accept_async (TpSvcChannelTypeStreamTube1 *iface,
    guint address_type,
    guint access_control,
    const GValue *access_control_param,
    DBusGMethodInvocation *context)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (iface);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  GError *error = NULL;

  if (priv->state != TP_TUBE_CHANNEL_STATE_LOCAL_PENDING)
    {
      GError e = { TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not in the local pending state" };

      dbus_g_method_return_error (context, &e);
      return;
    }

  if (address_type != TP_SOCKET_ADDRESS_TYPE_UNIX &&
      address_type != TP_SOCKET_ADDRESS_TYPE_IPV4 &&
      address_type != TP_SOCKET_ADDRESS_TYPE_IPV6)
    {
      error = g_error_new (TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
          "Address type %d not implemented", address_type);

      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  if (access_control != TP_SOCKET_ACCESS_CONTROL_LOCALHOST)
    {
      GError e = { TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Unix sockets only support localhost control access" };

      dbus_g_method_return_error (context, &e);
      return;
    }

  g_object_set (self,
      "address-type", address_type,
      "access-control", access_control,
      "access-control-param", access_control_param,
      NULL);

  if (!salut_tube_stream_accept (SALUT_TUBE_IFACE (self), &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

#if 0
  /* TODO: add a property "muc" and set it at initialization */
  if (cls->target_entity_type == TP_ENTITY_TYPE_ROOM)
    salut_muc_channel_send_presence (self->muc, NULL);
#endif

  tp_svc_channel_type_stream_tube1_return_from_accept (context,
      priv->address);
}

static void
destroy_socket_control_list (gpointer data)
{
  GArray *tab = data;
  g_array_unref (tab);
}

GHashTable *
salut_tube_stream_get_supported_socket_types (void)
{
  GHashTable *ret;
  GArray *unix_tab, *ipv4_tab, *ipv6_tab;
  TpSocketAccessControl access_control;

  ret = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      destroy_socket_control_list);

  /* Socket_Address_Type_Unix */
  unix_tab = g_array_sized_new (FALSE, FALSE, sizeof (TpSocketAccessControl),
      1);
  access_control = TP_SOCKET_ACCESS_CONTROL_LOCALHOST;
  g_array_append_val (unix_tab, access_control);
  g_hash_table_insert (ret, GUINT_TO_POINTER (TP_SOCKET_ADDRESS_TYPE_UNIX),
      unix_tab);

  /* Socket_Address_Type_IPv4 */
  ipv4_tab = g_array_sized_new (FALSE, FALSE, sizeof (TpSocketAccessControl),
      1);
  access_control = TP_SOCKET_ACCESS_CONTROL_LOCALHOST;
  g_array_append_val (ipv4_tab, access_control);
  g_hash_table_insert (ret, GUINT_TO_POINTER (TP_SOCKET_ADDRESS_TYPE_IPV4),
      ipv4_tab);

  /* Socket_Address_Type_IPv6 */
  ipv6_tab = g_array_sized_new (FALSE, FALSE, sizeof (TpSocketAccessControl),
      1);
  access_control = TP_SOCKET_ACCESS_CONTROL_LOCALHOST;
  g_array_append_val (ipv6_tab, access_control);
  g_hash_table_insert (ret, GUINT_TO_POINTER (TP_SOCKET_ADDRESS_TYPE_IPV6),
      ipv6_tab);

  return ret;
}

gboolean
salut_tube_stream_offer (SalutTubeStream *self,
                         GError **error)
{
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseChannelClass *cls = TP_BASE_CHANNEL_GET_CLASS (base);

  g_assert (priv->state == TP_TUBE_CHANNEL_STATE_NOT_OFFERED);

  if (cls->target_entity_type == TP_ENTITY_TYPE_CONTACT)
    {
      priv->state = TP_TUBE_CHANNEL_STATE_REMOTE_PENDING;

      tp_svc_channel_interface_tube1_emit_tube_channel_state_changed (
          self, TP_TUBE_CHANNEL_STATE_REMOTE_PENDING);
    }
  else
    {
      /* muc tube is open as soon it's offered */
      priv->state = TP_TUBE_CHANNEL_STATE_OPEN;
      tp_svc_channel_interface_tube1_emit_tube_channel_state_changed (
          self, TP_TUBE_CHANNEL_STATE_OPEN);
      g_signal_emit (G_OBJECT (self), signals[OPENED], 0);
    }

  g_signal_emit (G_OBJECT (self), signals[OFFERED], 0);
  return TRUE;
}

const gchar * const *
salut_tube_stream_channel_get_allowed_properties (void)
{
  return salut_tube_stream_channel_allowed_properties;
}

static void
tube_iface_init (gpointer g_iface,
                 gpointer iface_data)
{
  SalutTubeIfaceClass *klass = (SalutTubeIfaceClass *) g_iface;

  klass->accept = salut_tube_stream_accept;
  klass->accepted = salut_tube_stream_accepted;
  klass->offer_needed = salut_tube_stream_offer_needed;
  klass->listen = salut_tube_stream_listen;
  klass->close = salut_tube_stream_close;
  klass->add_bytestream = salut_tube_stream_add_bytestream;
}

static void
streamtube_iface_init (gpointer g_iface,
                       gpointer iface_data)
{
  TpSvcChannelTypeStreamTube1Class *klass = g_iface;

#define IMPLEMENT(x, suffix) tp_svc_channel_type_stream_tube1_implement_##x (\
    klass, salut_tube_stream_##x##suffix)
  IMPLEMENT(offer,_async);
  IMPLEMENT(accept,_async);
#undef IMPLEMENT
}
