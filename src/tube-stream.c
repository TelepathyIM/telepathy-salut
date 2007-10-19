/*
 * tube-stream.c - Source for SalutTubeStream
 * Copyright (C) 2007 Ltd.
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

#include "tube-stream.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <gibber/gibber-xmpp-stanza.h>
#include <gibber/gibber-namespaces.h>
#include <gibber/gibber-bytestream-iface.h>

#define DEBUG_FLAG DEBUG_TUBES

#include "debug.h"
#include "signals-marshal.h"
#include "extensions/extensions.h"
#include "salut-connection.h"
#include "namespaces.h"
#include "tube-iface.h"
#include "salut-bytestream-manager.h"
#include "salut-contact-manager.h"

static void
tube_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (SalutTubeStream, salut_tube_stream, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (SALUT_TYPE_TUBE_IFACE, tube_iface_init));

#define SOCKET_ADDRESS_IPV4_TYPE \
    dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_UINT, \
        G_TYPE_INVALID)

#define SOCKET_ADDRESS_IPV6_TYPE \
    dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_UINT, \
        G_TYPE_INVALID)

/* Linux glibc bits/socket.h suggests that struct sockaddr_storage is
 * not guaranteed to be big enough for AF_UNIX addresses */
typedef union
{
  /* we'd call this unix, but gcc predefines that. Thanks, gcc */
  struct sockaddr_un un;
  struct sockaddr_in ipv4;
  struct sockaddr_in6 ipv6;
} SockAddr;

/* signals */
enum
{
  OPENED,
  NEW_CONNECTION,
  CLOSED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_HANDLE,
  PROP_HANDLE_TYPE,
  PROP_SELF_HANDLE,
  PROP_ID,
  PROP_TYPE,
  PROP_INITIATOR,
  PROP_SERVICE,
  PROP_PARAMETERS,
  PROP_STATE,
  PROP_ADDRESS_TYPE,
  PROP_ADDRESS,
  PROP_ACCESS_CONTROL,
  PROP_ACCESS_CONTROL_PARAM,
  LAST_PROPERTY
};

typedef struct _SalutTubeStreamPrivate SalutTubeStreamPrivate;
struct _SalutTubeStreamPrivate
{
  SalutConnection *conn;
  TpHandle handle;
  TpHandleType handle_type;
  TpHandle self_handle;
  guint id;
  GHashTable *fd_to_bytestreams;
  GHashTable *bytestream_to_io_channel;
  GHashTable *io_channel_to_watcher_source_id;
  TpHandle initiator;
  gchar *service;
  GHashTable *parameters;
  TpTubeState state;

  TpSocketAddressType address_type;
  GValue *address;
  TpSocketAccessControl access_control;
  GValue *access_control_param;
  GIOChannel *listen_io_channel;
  guint listen_io_channel_source_id;
  gboolean closed;

  gboolean dispose_has_run;
};

#define SALUT_TUBE_STREAM_GET_PRIVATE(obj) \
    ((SalutTubeStreamPrivate *) obj->priv)

static void data_received_cb (GibberBytestreamIface *ibb, TpHandle sender,
    GString *data, gpointer user_data);

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

gboolean
data_to_read_on_socket_cb (GIOChannel *source,
                           GIOCondition condition,
                           gpointer data)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (data);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  GibberBytestreamIface *bytestream;
  int fd;
  gchar buffer[4096];
  gsize num_read;
  GIOStatus status;
  GError *error = NULL;
  gboolean result = TRUE;

  if (! (condition & G_IO_IN))
    return TRUE;

  fd = g_io_channel_unix_get_fd (source);

  bytestream = g_hash_table_lookup (priv->fd_to_bytestreams,
      GINT_TO_POINTER (fd));
  if (bytestream == NULL)
    {
      DEBUG ("no bytestream associated with this socket");

      g_hash_table_remove (priv->io_channel_to_watcher_source_id, source);
      return FALSE;
    }

  memset (&buffer, 0, sizeof (buffer));

  status = g_io_channel_read_chars (source, buffer, 4096, &num_read, &error);
  if (status == G_IO_STATUS_NORMAL)
    {
      DEBUG ("read %" G_GSIZE_FORMAT " bytes from socket", num_read);

      gibber_bytestream_iface_send (bytestream, num_read, buffer);
      result = TRUE;
    }
  else if (status == G_IO_STATUS_EOF)
    {
      DEBUG ("error reading from socket: EOF");

      gibber_bytestream_iface_close (bytestream, NULL);
      result = FALSE;
    }
  else if (status == G_IO_STATUS_AGAIN)
    {
      DEBUG ("error reading from socket: resource temporarily unavailable");

      result = TRUE;
    }
  else
    {
      DEBUG ("error reading from socket: %s", error ? error->message : "");

      gibber_bytestream_iface_close (bytestream, NULL);
      result = FALSE;
    }

  if (error != NULL)
    g_error_free (error);

  return TRUE;
}

static void
extra_bytestream_state_changed_cb (GibberBytestreamIface *bytestream,
                                   GibberBytestreamState state,
                                   gpointer user_data)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (user_data);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  GIOChannel *channel;

  channel = g_hash_table_lookup (priv->bytestream_to_io_channel,
      bytestream);
  if (channel == NULL)
    {
      DEBUG ("no IO channel associated with the bytestream");
      return;
    }

  if (state == GIBBER_BYTESTREAM_STATE_OPEN)
    {
      guint source_id;
      DEBUG ("extra bytestream open");

      g_signal_connect (bytestream, "data-received",
          G_CALLBACK (data_received_cb), self);

      source_id = g_io_add_watch (channel, G_IO_IN, data_to_read_on_socket_cb,
          self);
      g_hash_table_insert (priv->io_channel_to_watcher_source_id,
          g_io_channel_ref (channel), GUINT_TO_POINTER (source_id));
    }
  else if (state == GIBBER_BYTESTREAM_STATE_CLOSED)
    {
      int fd;

      DEBUG ("extra bytestream closed");

      fd = g_io_channel_unix_get_fd (channel);

      g_hash_table_remove (priv->fd_to_bytestreams, GINT_TO_POINTER (fd));
      g_hash_table_remove (priv->bytestream_to_io_channel, bytestream);
      g_hash_table_remove (priv->io_channel_to_watcher_source_id, channel);
    }
}

struct _extra_bytestream_negotiate_cb_data
{
  SalutTubeStream *self;
  gint fd;
};

static void
extra_bytestream_negotiate_cb (GibberBytestreamIface *bytestream,
                               const gchar *stream_id,
                               GibberXmppStanza *msg,
                               gpointer user_data)
{
  struct _extra_bytestream_negotiate_cb_data *data =
    (struct _extra_bytestream_negotiate_cb_data *) user_data;
  SalutTubeStream *self = data->self;
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  GIOChannel *channel;

  if (bytestream == NULL)
    {
      DEBUG ("initiator refused new bytestream");

      close (data->fd);
      return;
    }

  DEBUG ("extra bytestream accepted");

  channel = g_io_channel_unix_new (data->fd);
  g_io_channel_set_encoding (channel, NULL, NULL);
  g_io_channel_set_buffered (channel, FALSE);
  g_io_channel_set_close_on_unref (channel, TRUE);

  g_hash_table_insert (priv->fd_to_bytestreams, GINT_TO_POINTER (data->fd),
      g_object_ref (bytestream));
  g_hash_table_insert (priv->bytestream_to_io_channel,
      g_object_ref (bytestream), channel);

  g_signal_connect (bytestream, "state-changed",
                G_CALLBACK (extra_bytestream_state_changed_cb), self);

  g_slice_free (struct _extra_bytestream_negotiate_cb_data, data);
}

/* XXX we should move that in some kind of bytestream factory */
static gchar *
generate_stream_id (SalutTubeStream *self)
{
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  gchar *stream_id;

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
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
                         gint fd,
                         GError **error)
{
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  GibberXmppNode *node, *si_node;
  GibberXmppStanza *msg;
  TpHandleRepoIface *contact_repo;
  const gchar *jid;
  gchar *stream_id, *id_str;
  gboolean result;
  struct _extra_bytestream_negotiate_cb_data *data;
  SalutContact *contact;
  SalutContactManager *contact_mgr;
  SalutBytestreamManager *bytestream_mgr;

  contact_repo = tp_base_connection_get_handles (
     (TpBaseConnection*) priv->conn, TP_HANDLE_TYPE_CONTACT);

  jid = tp_handle_inspect (contact_repo, priv->initiator);

  stream_id = generate_stream_id (self);

  msg = salut_bytestream_manager_make_stream_init_iq (priv->conn->name, jid,
      stream_id, GIBBER_TELEPATHY_NS_TUBES);

  si_node = gibber_xmpp_node_get_child_ns (msg->node, "si", GIBBER_XMPP_NS_SI);
  g_assert (si_node != NULL);

  id_str = g_strdup_printf ("%u", priv->id);

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      node = gibber_xmpp_node_add_child (si_node, "stream");
    }
  else
    {
      TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
          (TpBaseConnection*) priv->conn, TP_HANDLE_TYPE_ROOM);

      /* FIXME: this needs standardizing */
      node = gibber_xmpp_node_add_child (si_node, "muc-stream");
      gibber_xmpp_node_set_attribute (node, "muc", tp_handle_inspect (
            room_repo, priv->handle));
    }

  gibber_xmpp_node_set_ns (node, GIBBER_TELEPATHY_NS_TUBES);
  gibber_xmpp_node_set_attribute (node, "tube", id_str);

  data = g_slice_new (struct _extra_bytestream_negotiate_cb_data);
  data->self = self;
  data->fd = fd;

  g_object_get (priv->conn,
      "bytestream-manager", &bytestream_mgr,
      "contact-manager", &contact_mgr,
      NULL);
  g_assert (bytestream_mgr != NULL);
  g_assert (contact_mgr != NULL);

  contact = salut_contact_manager_get_contact (contact_mgr, priv->initiator);
  if (contact == NULL)
    {
      result = FALSE;
      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
          "can't find contact with handle %d", priv->initiator);
    }
  else
    {
      result = salut_bytestream_manager_negotiate_stream (
        bytestream_mgr,
        contact,
        msg,
        stream_id,
        extra_bytestream_negotiate_cb,
        data,
        error);

      g_object_unref (contact);
    }

  g_object_unref (bytestream_mgr);
  g_object_unref (contact_mgr);
  g_object_unref (msg);
  g_free (stream_id);
  g_free (id_str);

  return result;
}

gboolean
listen_cb (GIOChannel *source,
           GIOCondition condition,
           gpointer data)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (data);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  int fd, listen_fd;
  SockAddr addr;
  socklen_t addrlen;
  int flags;

  listen_fd = g_io_channel_unix_get_fd (source);

  if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_UNIX)
    {
      addrlen = sizeof (addr.un);
    }
  else if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_IPV4)
    {
      addrlen = sizeof (addr.ipv4);
    }
  else if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_IPV6)
    {
      addrlen = sizeof (addr.ipv6);
    }
  else
    {
      g_assert_not_reached ();
    }

  fd = accept (listen_fd, (struct sockaddr *) &addr, &addrlen);
  if (fd == -1)
    {
      DEBUG ("Error accepting socket: %s", g_strerror (errno));
      return TRUE;
    }

  DEBUG ("connection from client");

  /* Set socket non blocking */
  flags = fcntl (fd, F_GETFL, 0);
  if (fcntl (fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
      DEBUG ("Can't set socket non blocking: %s", g_strerror (errno));
      close (fd);
      return TRUE;
    }

  DEBUG ("request new bytestream");

  if (!start_stream_initiation (self, fd, NULL))
    {
      DEBUG ("closing new client connection");
      close (fd);
    }

  return TRUE;
}

static gboolean
new_connection_to_socket (SalutTubeStream *self,
                          GibberBytestreamIface *bytestream)
{
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  int fd;
  GIOChannel *channel;
  SockAddr addr;
  socklen_t len;

  g_assert (priv->initiator == priv->self_handle);

  memset (&addr, 0, sizeof (addr));

  if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_UNIX)
    {
      GArray *array;
      array = g_value_get_boxed (priv->address);

      fd = socket (PF_UNIX, SOCK_STREAM, 0);
      if (fd == -1)
        {
          DEBUG ("Error creating socket: %s", g_strerror (errno));
          return FALSE;
        }

      addr.un.sun_family = PF_UNIX;
      strncpy (addr.un.sun_path, array->data, sizeof (addr.un.sun_path) - 1);
      addr.un.sun_path[sizeof (addr.un.sun_path)] = '\0';
      len = sizeof (addr.un);

      DEBUG ("Will try to connect to socket: %s", (const gchar *) array->data);
    }
  else if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_IPV4 ||
      priv->address_type == TP_SOCKET_ADDRESS_TYPE_IPV6)
    {
      gchar *ip;
      guint port;
      struct addrinfo req, *result = NULL;
      int ret;

      if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_IPV4)
        fd = socket (PF_INET, SOCK_STREAM, 0);
      else
        fd = socket (PF_INET6, SOCK_STREAM, 0);

      if (fd == -1)
        {
          DEBUG ("Error creating socket: %s", g_strerror (errno));
          return FALSE;
        }

      dbus_g_type_struct_get (priv->address,
          0, &ip,
          1, &port,
          G_MAXUINT);

      memset (&req, 0, sizeof (req));
      req.ai_flags = AI_NUMERICHOST;
      req.ai_socktype = SOCK_STREAM;
      req.ai_protocol = IPPROTO_TCP;

      if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_IPV4)
        req.ai_family = AF_INET;
      else
        req.ai_family = AF_INET6;

      ret = getaddrinfo (ip, NULL, &req, &result);
      if (ret != 0)
        {
          DEBUG ("getaddrinfo failed: %s",  gai_strerror (ret));
          g_free (ip);
          return FALSE;
        }

      DEBUG ("Will try to connect to %s:%u", ip, port);

      if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_IPV4)
        {
          memcpy (&addr, result->ai_addr, sizeof (addr.ipv4));
          addr.ipv4.sin_port = ntohs (port);
          len = sizeof (addr.ipv4);
        }
      else
        {
          memcpy (&addr, result->ai_addr, sizeof (addr.ipv6));
          addr.ipv6.sin6_port = ntohs (port);
          len = sizeof (addr.ipv6);
        }

      g_free (ip);
      freeaddrinfo (result);
    }
  else
    {
      g_assert_not_reached ();
    }

  if (connect (fd, (struct sockaddr *) &addr, len) == -1)
    {
      DEBUG ("Error connecting socket: %s", g_strerror (errno));
      return FALSE;
    }
  DEBUG ("Connected to socket");

  channel = g_io_channel_unix_new (fd);
  g_io_channel_set_encoding (channel, NULL, NULL);
  g_io_channel_set_buffered (channel, FALSE);
  g_io_channel_set_close_on_unref (channel, TRUE);

  g_hash_table_insert (priv->fd_to_bytestreams, GINT_TO_POINTER (fd),
      g_object_ref (bytestream));
  g_hash_table_insert (priv->bytestream_to_io_channel,
      g_object_ref (bytestream), channel);

  g_signal_connect (bytestream, "state-changed",
      G_CALLBACK (extra_bytestream_state_changed_cb), self);

  return TRUE;
}

static gboolean
tube_stream_open (SalutTubeStream *self,
                  GError **error)
{
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  int fd;

  DEBUG ("called");

  if (priv->initiator == priv->self_handle)
    /* Nothing to do if we are the initiator of this tube.
     * We'll connect to the socket each time request a new bytestream. */
    return TRUE;

  /* We didn't create this tube so it doesn't have
   * a socket associated with it. Let's create one */
  g_assert (priv->address == NULL);

  if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_UNIX)
    {
      GArray *array;
      struct sockaddr_un addr;
      gchar suffix[8];

      fd = socket (PF_UNIX, SOCK_STREAM, 0);
      if (fd == -1)
        {
          DEBUG ("Error creating socket: %s", g_strerror (errno));
          g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
              "Error creating socket: %s", g_strerror (errno));
          return FALSE;
        }

      memset (&addr, 0, sizeof (addr));
      addr.sun_family = PF_UNIX;

      generate_ascii_string (8, suffix);
      snprintf (addr.sun_path, sizeof (addr.sun_path) - 1,
        "/tmp/stream-salut-%.8s", suffix);

      DEBUG ("create socket: %s", addr.sun_path);

      array = g_array_sized_new (TRUE, FALSE, sizeof (gchar), strlen (
            addr.sun_path));
      g_array_insert_vals (array, 0, addr.sun_path, strlen (addr.sun_path));

      priv->address = tp_g_value_slice_new (DBUS_TYPE_G_UCHAR_ARRAY);
      g_value_set_boxed (priv->address, array);

      g_array_free (array, TRUE);

      if (bind (fd, (struct sockaddr *) &addr, sizeof (addr)) == -1)
        {
          DEBUG ("Error binding socket: %s", g_strerror (errno));
          g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
              "Error binding socket: %s", g_strerror (errno));
          return FALSE;
        }
    }
  else if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_IPV4)
    {
      struct sockaddr_in addr;
      socklen_t len;

      addr.sin_family = AF_INET;
      addr.sin_port = 0;         /* == ntohs (0) */
      addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

      len = sizeof (addr);

      fd = socket (PF_INET, SOCK_STREAM, 0);
      if (fd == -1)
        {
          DEBUG ("Error creating socket: %s", g_strerror (errno));
          g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
              "Error creating socket: %s", g_strerror (errno));
          return FALSE;
        }

      if (bind (fd, (struct sockaddr *) &addr, len) == -1)
        {
          DEBUG ("Error binding socket: %s", g_strerror (errno));
          g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
              "Error binding socket: %s", g_strerror (errno));
          return FALSE;
        }

      if (getsockname (fd, (struct sockaddr *) &addr, &len) == -1)
        {
          DEBUG ("getsockname failed: %s", g_strerror (errno));
          g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
              "getsockname failed: %s", g_strerror (errno));
          return FALSE;
        }

      DEBUG ("create socket %s:%u", "127.0.0.1", ntohs (addr.sin_port));

      priv->address = tp_g_value_slice_new (SOCKET_ADDRESS_IPV4_TYPE);
      g_value_take_boxed (priv->address,
          dbus_g_type_specialized_construct (SOCKET_ADDRESS_IPV4_TYPE));

      dbus_g_type_struct_set (priv->address,
          0, "127.0.0.1",
          1, ntohs (addr.sin_port),
          G_MAXUINT);
    }
  else if (priv->address_type == TP_SOCKET_ADDRESS_TYPE_IPV6)
    {
      struct sockaddr_in6 addr;
      socklen_t len;
      struct in6_addr loopback_addr = IN6ADDR_LOOPBACK_INIT;

      addr.sin6_family = AF_INET6;
      addr.sin6_port = 0;         /* == ntohs (0) */
      addr.sin6_addr = loopback_addr;

      len = sizeof (addr);

      fd = socket (PF_INET6, SOCK_STREAM, 0);
      if (fd == -1)
        {
          DEBUG ("Error creating socket: %s", g_strerror (errno));
          g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
              "Error creating socket: %s", g_strerror (errno));
          return FALSE;
        }

      if (bind (fd, (struct sockaddr *) &addr, len) == -1)
        {
          DEBUG ("Error binding socket: %s", g_strerror (errno));
          g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
              "Error binding socket: %s", g_strerror (errno));
          return FALSE;
        }

      if (getsockname (fd, (struct sockaddr *) &addr, &len) == -1)
        {
          DEBUG ("getsockname failed: %s", g_strerror (errno));
          g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
              "getsockname failed: %s", g_strerror (errno));
          return FALSE;
        }

      DEBUG ("create socket %s:%u", "::1", ntohs (addr.sin6_port));

      priv->address = tp_g_value_slice_new (SOCKET_ADDRESS_IPV6_TYPE);
      g_value_take_boxed (priv->address,
          dbus_g_type_specialized_construct (SOCKET_ADDRESS_IPV6_TYPE));

      dbus_g_type_struct_set (priv->address,
          0, "::1",
          1, ntohs (addr.sin6_port),
          G_MAXUINT);
    }
  else
    {
      g_assert_not_reached ();
    }

  if (listen (fd, 5) == -1)
    {
      DEBUG ("Error listening socket: %s", g_strerror (errno));
      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
          "Error listening socket: %s", g_strerror (errno));
      return FALSE;
    }

  priv->listen_io_channel = g_io_channel_unix_new (fd);
  g_io_channel_set_encoding (priv->listen_io_channel, NULL, NULL);
  g_io_channel_set_buffered (priv->listen_io_channel, FALSE);
  g_io_channel_set_close_on_unref (priv->listen_io_channel, TRUE);

  priv->listen_io_channel_source_id = g_io_add_watch (priv->listen_io_channel,
      G_IO_IN, listen_cb, self);

  return TRUE;
}

static void
remove_watcher_source_id (gpointer data)
{
  guint source_id = GPOINTER_TO_UINT (data);
  GSource *source;

  source = g_main_context_find_source_by_id (NULL, source_id);
  if (source != NULL)
    {
      g_source_destroy (source);
    }
}

static void
salut_tube_stream_init (SalutTubeStream *self)
{
  SalutTubeStreamPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_TUBE_STREAM, SalutTubeStreamPrivate);

  self->priv = priv;

  priv->fd_to_bytestreams = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) g_object_unref);

  priv->bytestream_to_io_channel = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, (GDestroyNotify) g_object_unref,
      (GDestroyNotify) g_io_channel_unref);

  priv->io_channel_to_watcher_source_id = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, (GDestroyNotify) g_io_channel_unref,
      (GDestroyNotify) remove_watcher_source_id);

  priv->listen_io_channel = NULL;
  priv->listen_io_channel_source_id = 0;
  priv->address_type = TP_SOCKET_ADDRESS_TYPE_UNIX;
  priv->address = NULL;
  priv->access_control = TP_SOCKET_ACCESS_CONTROL_LOCALHOST;
  priv->access_control_param = NULL;
  priv->closed = FALSE;

  priv->dispose_has_run = FALSE;
}

static gboolean
close_each_extra_bytestream (gpointer key,
                             gpointer value,
                             gpointer user_data)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (user_data);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  GibberBytestreamIface *bytestream = (GibberBytestreamIface *) value;
  GIOChannel *channel;

  /* We are iterating over priv->fd_to_bytestreams so we can't modify it.
   * Disconnect signals so extra_bytestream_state_changed_cb won't be
   * called */
  g_signal_handlers_disconnect_matched (bytestream, G_SIGNAL_MATCH_DATA,
      0, 0, NULL, NULL, self);

  gibber_bytestream_iface_close (bytestream, NULL);

  channel = g_hash_table_lookup (priv->bytestream_to_io_channel, bytestream);
  g_assert (channel != NULL);

  g_hash_table_remove (priv->bytestream_to_io_channel, bytestream);
  g_hash_table_remove (priv->io_channel_to_watcher_source_id, channel);

  return TRUE;
}

static void
salut_tube_stream_dispose (GObject *object)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (object);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  if (priv->dispose_has_run)
    return;

  salut_tube_iface_close (SALUT_TUBE_IFACE (self));

  if (priv->initiator != priv->self_handle &&
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

  if (priv->fd_to_bytestreams != NULL)
    {
      g_hash_table_destroy (priv->fd_to_bytestreams);
      priv->fd_to_bytestreams = NULL;
    }

  if (priv->bytestream_to_io_channel != NULL)
    {
      g_hash_table_destroy (priv->bytestream_to_io_channel);
      priv->bytestream_to_io_channel = NULL;
    }

  if (priv->io_channel_to_watcher_source_id != NULL)
    {
      g_hash_table_destroy (priv->io_channel_to_watcher_source_id);
      priv->io_channel_to_watcher_source_id = NULL;
    }

  tp_handle_unref (contact_repo, priv->initiator);

  if (priv->listen_io_channel_source_id != 0)
    {
      g_source_destroy (g_main_context_find_source_by_id (NULL,
            priv->listen_io_channel_source_id));
      priv->listen_io_channel_source_id = 0;
    }

  if (priv->listen_io_channel)
    {
      g_io_channel_unref (priv->listen_io_channel);
      priv->listen_io_channel = NULL;
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
  g_hash_table_destroy (priv->parameters);

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
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_HANDLE:
        g_value_set_uint (value, priv->handle);
        break;
      case PROP_HANDLE_TYPE:
        g_value_set_uint (value, priv->handle_type);
        break;
      case PROP_SELF_HANDLE:
        g_value_set_uint (value, priv->self_handle);
        break;
      case PROP_ID:
        g_value_set_uint (value, priv->id);
        break;
      case PROP_TYPE:
        g_value_set_uint (value, TP_TUBE_TYPE_STREAM);
        break;
      case PROP_INITIATOR:
        g_value_set_uint (value, priv->initiator);
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
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      case PROP_HANDLE:
        priv->handle = g_value_get_uint (value);
        break;
      case PROP_HANDLE_TYPE:
        priv->handle_type = g_value_get_uint (value);
        break;
      case PROP_SELF_HANDLE:
        priv->self_handle = g_value_get_uint (value);
        break;
      case PROP_ID:
        priv->id = g_value_get_uint (value);
        break;
      case PROP_INITIATOR:
        priv->initiator = g_value_get_uint (value);
        break;
      case PROP_SERVICE:
        g_free (priv->service);
        priv->service = g_value_dup_string (value);
        break;
      case PROP_PARAMETERS:
        priv->parameters = g_value_get_boxed (value);
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
  TpHandleRepoIface *contact_repo;

  obj = G_OBJECT_CLASS (salut_tube_stream_parent_class)->
           constructor (type, n_props, props);

  priv = SALUT_TUBE_STREAM_GET_PRIVATE (SALUT_TUBE_STREAM (obj));

  /* Ref the initiator handle */
  g_assert (priv->conn != NULL);
  g_assert (priv->initiator != 0);
  contact_repo = tp_base_connection_get_handles
      ((TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  tp_handle_ref (contact_repo, priv->initiator);

  /* Set initial state of the tube */
  if (priv->initiator == priv->self_handle)
    {
      /* We initiated this tube */
      if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
        {
          /* Private tube */
          priv->state = TP_TUBE_STATE_REMOTE_PENDING;
        }
      else
        {
          /* Muc tube */
          priv->state = TP_TUBE_STATE_OPEN;
        }
    }
  else
    {
      priv->state = TP_TUBE_STATE_LOCAL_PENDING;
    }

  return obj;
}

static void
salut_tube_stream_class_init (SalutTubeStreamClass *salut_tube_stream_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_tube_stream_class);
  GParamSpec *param_spec;

  object_class->get_property = salut_tube_stream_get_property;
  object_class->set_property = salut_tube_stream_set_property;
  object_class->constructor = salut_tube_stream_constructor;

  g_type_class_add_private (salut_tube_stream_class,
      sizeof (SalutTubeStreamPrivate));

  object_class->dispose = salut_tube_stream_dispose;
  object_class->finalize = salut_tube_stream_finalize;

  g_object_class_override_property (object_class, PROP_CONNECTION,
    "connection");
  g_object_class_override_property (object_class, PROP_HANDLE,
    "handle");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
    "handle-type");
  g_object_class_override_property (object_class, PROP_SELF_HANDLE,
    "self-handle");
  g_object_class_override_property (object_class, PROP_ID,
    "id");
  g_object_class_override_property (object_class, PROP_TYPE,
    "type");
  g_object_class_override_property (object_class, PROP_INITIATOR,
    "initiator");
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
      0, NUM_TP_SOCKET_ADDRESS_TYPES - 1,
      TP_SOCKET_ADDRESS_TYPE_UNIX,
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_ADDRESS_TYPE,
      param_spec);

  param_spec = g_param_spec_pointer (
      "address",
      "address",
      "The listening address of the local service, as indicated by the "
      "address-type",
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_ADDRESS, param_spec);

  param_spec = g_param_spec_uint (
      "access-control",
      "access control",
      "a TpSocketAccessControl representing the access control "
      "the local service applies to the local socket",
      0, NUM_TP_SOCKET_ACCESS_CONTROLS - 1,
      TP_SOCKET_ACCESS_CONTROL_LOCALHOST,
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_ACCESS_CONTROL,
      param_spec);

  param_spec = g_param_spec_pointer (
      "access-control-param",
      "access control param",
      "A parameter for the access control type, to be interpreted as specified"
      "in the documentation for the Socket_Access_Control enum.",
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_ACCESS_CONTROL_PARAM,
      param_spec);

  signals[OPENED] =
    g_signal_new ("opened",
                  G_OBJECT_CLASS_TYPE (salut_tube_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_signals_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[NEW_CONNECTION] =
    g_signal_new ("new-connection",
                  G_OBJECT_CLASS_TYPE (salut_tube_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_signals_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (salut_tube_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_signals_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
data_received_cb (GibberBytestreamIface *bytestream,
                  TpHandle sender,
                  GString *data,
                  gpointer user_data)
{
  SalutTubeStream *tube = SALUT_TUBE_STREAM (user_data);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (tube);
  gsize written;
  GIOChannel *channel;
  GIOStatus status;
  GError *error = NULL;

  DEBUG ("received %" G_GSIZE_FORMAT " bytes from bytestream", data->len);

  channel = g_hash_table_lookup (priv->bytestream_to_io_channel, bytestream);
  if (channel == NULL)
    {
      DEBUG ("no IO channel associated with the bytestream");
      return;
    }

  status = g_io_channel_write_chars (channel, data->str, data->len,
      &written, &error);
  if (status == G_IO_STATUS_NORMAL)
    {
      DEBUG ("%" G_GSIZE_FORMAT " bytes written to the socket", written);
    }
  else
    {
      DEBUG ("error writing to socket: %s",
          error ? error->message : "");
    }

  if (error != NULL)
    g_error_free (error);
}

SalutTubeStream *
salut_tube_stream_new (SalutConnection *conn,
                        TpHandle handle,
                        TpHandleType handle_type,
                        TpHandle self_handle,
                        TpHandle initiator,
                        const gchar *service,
                        GHashTable *parameters,
                        guint id)
{
  return g_object_new (SALUT_TYPE_TUBE_STREAM,
      "connection", conn,
      "handle", handle,
      "handle-type", handle_type,
      "self-handle", self_handle,
      "initiator", initiator,
      "service", service,
      "parameters", parameters,
      "id", id,
      NULL);
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

  if (priv->state != TP_TUBE_STATE_LOCAL_PENDING)
    return TRUE;

  if (!tube_stream_open (self, error))
    {
      salut_tube_iface_close (SALUT_TUBE_IFACE (self));
      return FALSE;
    }

  priv->state = TP_TUBE_STATE_OPEN;
  g_signal_emit (G_OBJECT (self), signals[OPENED], 0);
  return TRUE;
}

/**
 * salut_tube_stream_close
 *
 * Implements salut_tube_iface_close on SalutTubeIface
 */
static void
salut_tube_stream_close (SalutTubeIface *tube)
{
  SalutTubeStream *self = SALUT_TUBE_STREAM (tube);
  SalutTubeStreamPrivate *priv = SALUT_TUBE_STREAM_GET_PRIVATE (self);

  if (priv->closed)
    return;
  priv->closed = TRUE;

  g_hash_table_foreach_remove (priv->fd_to_bytestreams,
      close_each_extra_bytestream, self);

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      /* TODO: implement 1-1 tube */
#if 0
      LmMessage *msg;
      const gchar *jid;
      TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
          (TpBaseConnection*) priv->conn, TP_HANDLE_TYPE_CONTACT);
      gchar *id_str;

      jid = tp_handle_inspect (contact_repo, priv->handle);
      id_str = g_strdup_printf ("%u", priv->id);

      /* Send the close message */
      msg = lm_message_build (jid, LM_MESSAGE_TYPE_MESSAGE,
          '(', "close", "",
            '@', "xmlns", NS_TUBES,
            '@', "tube", id_str,
          ')',
          '(', "amp", "",
            '@', "xmlns", NS_AMP,
            '(', "rule", "",
              '@', "condition", "deliver-at",
              '@', "value", "stored",
              '@', "action", "error",
            ')',
            '(', "rule", "",
              '@', "condition", "match-resource",
              '@', "value", "exact",
              '@', "action", "error",
            ')',
          ')',
          NULL);
      g_free (id_str);

      _salut_connection_send (priv->conn, msg, NULL);

      lm_message_unref (msg);
#endif
    }

  g_signal_emit (G_OBJECT (self), signals[CLOSED], 0);
}

static void
augment_si_accept_iq (GibberXmppNode *si,
                      gpointer user_data)
{
  gibber_xmpp_node_add_child_ns (si, "tube", GIBBER_TELEPATHY_NS_TUBES);
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

  if (priv->initiator != priv->self_handle)
    {
      DEBUG ("I'm not the initiator of this tube, can't accept "
          "an extra bytestream");

      gibber_bytestream_iface_close (bytestream, NULL);
      return;
    }

  /* New bytestream, let's connect to the socket */
  if (new_connection_to_socket (self, bytestream))
    {
      TpHandle contact;
      gchar *peer_id;
      TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
          (TpBaseConnection*) priv->conn, TP_HANDLE_TYPE_CONTACT);

      if (priv->state == TP_TUBE_STATE_REMOTE_PENDING)
        {
          DEBUG ("Received first connection. Tube is now open");
          priv->state = TP_TUBE_STATE_OPEN;
          g_signal_emit (G_OBJECT (self), signals[OPENED], 0);
        }

      DEBUG ("accept the extra bytestream");

      gibber_bytestream_iface_accept (bytestream, augment_si_accept_iq, self);

      g_object_get (bytestream, "peer-id", &peer_id, NULL);
      contact = tp_handle_ensure (contact_repo, peer_id, NULL, NULL);

      g_signal_emit (G_OBJECT (self), signals[NEW_CONNECTION], 0, contact);

      tp_handle_unref (contact_repo, contact);
      g_free (peer_id);
    }
  else
    {
      gibber_bytestream_iface_close (bytestream, NULL);
    }
}

static gboolean
check_unix_params (TpSocketAddressType address_type,
                   const GValue *address,
                   TpSocketAccessControl access_control,
                   const GValue *access_control_param,
                   GError **error)
{
  GArray *array;
  GString *socket;
  struct stat stat_buff;
  guint i;
  struct sockaddr_un dummy;

  g_assert (address_type == TP_SOCKET_ADDRESS_TYPE_UNIX);

  /* Check address type */
  if (G_VALUE_TYPE (address) != DBUS_TYPE_G_UCHAR_ARRAY)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Unix socket address is supposed to be ay");
      return FALSE;
    }

  array = g_value_get_boxed (address);

  if (array->len > sizeof (dummy.sun_path) - 1)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Unix socket path is too long (max length allowed: %"
          G_GSIZE_FORMAT ")",
          sizeof (dummy.sun_path) - 1);
      return FALSE;
    }

  for (i = 0; i < array->len; i++)
    {
      if (g_array_index (array, gchar , i) == '\0')
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "Unix socket path can't contain zero bytes");
          return FALSE;
        }
    }

  socket = g_string_new_len (array->data, array->len);

  if (g_stat (socket->str, &stat_buff) == -1)
  {
    DEBUG ("Error calling stat on socket: %s", g_strerror (errno));

    g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "%s: %s",
        socket->str, g_strerror (errno));
    g_string_free (socket, TRUE);
    return FALSE;
  }

  if (!S_ISSOCK (stat_buff.st_mode))
  {
    DEBUG ("%s is not a socket", socket->str);

    g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
        "%s is not a socket", socket->str);
    g_string_free (socket, TRUE);
    return FALSE;
  }

  g_string_free (socket, TRUE);

  if (access_control != TP_SOCKET_ACCESS_CONTROL_LOCALHOST)
  {
    g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
        "Unix sockets only support localhost control access");
    return FALSE;
  }

  return TRUE;
}

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
      if (G_VALUE_TYPE (address) != SOCKET_ADDRESS_IPV4_TYPE)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "IPv4 socket address is supposed to be sq");
          return FALSE;
        }
    }
  else if (address_type == TP_SOCKET_ADDRESS_TYPE_IPV6)
    {
      if (G_VALUE_TYPE (address) != SOCKET_ADDRESS_IPV6_TYPE)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
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
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Invalid address: %s", gai_strerror (ret));
      g_free (ip);
      return FALSE;
    }

  g_free (ip);
  freeaddrinfo (result);

  if (access_control != TP_SOCKET_ACCESS_CONTROL_LOCALHOST)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
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
      case TP_SOCKET_ADDRESS_TYPE_UNIX:
        return check_unix_params (address_type, address, access_control,
            access_control_param, error);

      case TP_SOCKET_ADDRESS_TYPE_IPV4:
      case TP_SOCKET_ADDRESS_TYPE_IPV6:
        return check_ip_params (address_type, address, access_control,
            access_control_param, error);

      default:
        g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
            "Address type %d not implemented", address_type);
        return FALSE;
    }
}

static void
tube_iface_init (gpointer g_iface,
                 gpointer iface_data)
{
  SalutTubeIfaceClass *klass = (SalutTubeIfaceClass *) g_iface;

  klass->accept = salut_tube_stream_accept;
  klass->close = salut_tube_stream_close;
  klass->add_bytestream = salut_tube_stream_add_bytestream;
}
