/*
 * gibber-xmpp-connection-listener.c - Source for GibberXmppConnectionListener
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

#include "gibber-xmpp-connection-listener.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>

#include <glib.h>

#include "gibber-xmpp-connection.h"
#include "gibber-linklocal-transport.h"
#include "gibber-util.h"

#define DEBUG_FLAG DEBUG_NET
#include "gibber-debug.h"

#include "signals-marshal.h"

G_DEFINE_TYPE (GibberXmppConnectionListener, gibber_xmpp_connection_listener, \
    G_TYPE_OBJECT);

/* signals */
enum
{
  NEW_CONNECTION,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

typedef struct _GibberXmppConnectionListenerPrivate \
          GibberXmppConnectionListenerPrivate;
struct _GibberXmppConnectionListenerPrivate
{
  GIOChannel *listener;
  guint io_watch_in;
  int port;

  gboolean dispose_has_run;
};

#define GIBBER_XMPP_CONNECTION_LISTENER_GET_PRIVATE(obj) \
    ((GibberXmppConnectionListenerPrivate *) obj->priv)

GQuark
gibber_xmpp_connection_listener_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string (
        "gibber_xmpp_connection_listener_error");

  return quark;
}

static void
gibber_xmpp_connection_listener_init (GibberXmppConnectionListener *self)
{
  GibberXmppConnectionListenerPrivate *priv =
    G_TYPE_INSTANCE_GET_PRIVATE (self, GIBBER_TYPE_XMPP_CONNECTION_LISTENER,
        GibberXmppConnectionListenerPrivate);

  self->priv = priv;

  priv->listener = NULL;

  priv->dispose_has_run = FALSE;
}

static void
gibber_xmpp_connection_listener_dispose (GObject *object)
{
  GibberXmppConnectionListener *self =
    GIBBER_XMPP_CONNECTION_LISTENER (object);
  GibberXmppConnectionListenerPrivate *priv =
    GIBBER_XMPP_CONNECTION_LISTENER_GET_PRIVATE (self);

  if (priv->listener != NULL)
    {
      g_io_channel_unref (priv->listener);
      priv->listener = NULL;
      g_source_remove (priv->io_watch_in);
    }

  G_OBJECT_CLASS (gibber_xmpp_connection_listener_parent_class)->dispose (
      object);
}

static void
gibber_xmpp_connection_listener_class_init (
    GibberXmppConnectionListenerClass *gibber_xmpp_connection_listener_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (
      gibber_xmpp_connection_listener_class);

  g_type_class_add_private (gibber_xmpp_connection_listener_class,
      sizeof (GibberXmppConnectionListenerPrivate));

  object_class->dispose = gibber_xmpp_connection_listener_dispose;

  signals[NEW_CONNECTION] =
    g_signal_new (
        "new-connection",
        G_OBJECT_CLASS_TYPE (gibber_xmpp_connection_listener_class),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
        0,
        NULL, NULL,
        _gibber_signals_marshal_VOID__OBJECT_POINTER_UINT,
        G_TYPE_NONE, 3, GIBBER_TYPE_XMPP_CONNECTION, G_TYPE_POINTER,
        G_TYPE_UINT);
}

GibberXmppConnectionListener *
gibber_xmpp_connection_listener_new (void)
{
  return g_object_new (GIBBER_TYPE_XMPP_CONNECTION_LISTENER,
      NULL);
}

static int
try_listening_on_port (GibberXmppConnectionListener *self,
                       int port,
                       GError **error)
{
  int fd = -1, ret, yes = 1;
  struct addrinfo req, *ans = NULL;
  #define BACKLOG 5

  memset (&req, 0, sizeof (req));
  req.ai_flags = AI_PASSIVE;
  req.ai_family = AF_UNSPEC;
  req.ai_socktype = SOCK_STREAM;
  req.ai_protocol = IPPROTO_TCP;

  ret = getaddrinfo (NULL, "0", &req, &ans);
  if (ret != 0)
    {
      DEBUG ("getaddrinfo failed: %s", gai_strerror (ret));
      g_set_error (error, GIBBER_XMPP_CONNECTION_LISTENER_ERROR,
          GIBBER_XMPP_CONNECTION_LISTENER_ERROR_FAILED,
          "%s", gai_strerror (ret));
      goto error;
    }

  ((struct sockaddr_in *) ans->ai_addr)->sin_port = ntohs (port);

  fd = socket (ans->ai_family, ans->ai_socktype, ans->ai_protocol);
  if (fd == -1)
    {
      DEBUG ("socket failed: %s", g_strerror (errno));
      g_set_error (error, GIBBER_XMPP_CONNECTION_LISTENER_ERROR,
          GIBBER_XMPP_CONNECTION_LISTENER_ERROR_FAILED,
          "%s", g_strerror (errno));
      goto error;
    }

  ret = setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (int));
  if (ret == -1)
    {
      DEBUG ("setsockopt failed: %s", g_strerror (errno));
      g_set_error (error, GIBBER_XMPP_CONNECTION_LISTENER_ERROR,
          GIBBER_XMPP_CONNECTION_LISTENER_ERROR_FAILED,
          "%s", g_strerror (errno));
      goto error;
    }

  ret = bind (fd, ans->ai_addr, ans->ai_addrlen);
  if (ret  < 0)
    {
      DEBUG ("bind failed: %s", g_strerror (errno));
      g_set_error (error, GIBBER_XMPP_CONNECTION_LISTENER_ERROR,
          errno == EADDRINUSE ?
          GIBBER_XMPP_CONNECTION_LISTENER_ERROR_ADDR_IN_USE :
          GIBBER_XMPP_CONNECTION_LISTENER_ERROR_FAILED,
          "%s", g_strerror (errno));
      goto error;
    }

  ret = listen (fd, BACKLOG);
  if (ret == -1)
    {
      DEBUG ("listen failed: %s", g_strerror (errno));
      g_set_error (error, GIBBER_XMPP_CONNECTION_LISTENER_ERROR,
          errno == EADDRINUSE ?
          GIBBER_XMPP_CONNECTION_LISTENER_ERROR_ADDR_IN_USE :
          GIBBER_XMPP_CONNECTION_LISTENER_ERROR_FAILED,
          "%s", g_strerror (errno));
      goto error;
    }

  freeaddrinfo (ans);
  return fd;

error:
  if (fd > 0)
    close (fd);

  if (ans != NULL)
    freeaddrinfo (ans);
  return -1;
}

static gboolean
listener_io_in_cb (GIOChannel *source,
                   GIOCondition condition,
                   gpointer user_data)
{
  GibberXmppConnectionListener *self =
    GIBBER_XMPP_CONNECTION_LISTENER (user_data);
  int fd, nfd;
  int ret;
  char host[NI_MAXHOST];
  char port[NI_MAXSERV];
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof (struct sockaddr_storage);
  GibberLLTransport *transport;
  GibberXmppConnection *connection;

  fd = g_io_channel_unix_get_fd (source);
  nfd = accept (fd, (struct sockaddr *) &addr, &addrlen);
  gibber_normalize_address (&addr);

  transport = gibber_ll_transport_new ();
  gibber_ll_transport_open_fd (transport, nfd);

  ret = getnameinfo ((struct sockaddr *) &addr, addrlen,
      host, NI_MAXHOST, port, NI_MAXSERV,
      NI_NUMERICHOST | NI_NUMERICSERV);

  if (ret == 0)
    DEBUG("New connection from %s port %s", host, port);
  else
    DEBUG("New connection..");

  connection = gibber_xmpp_connection_new (GIBBER_TRANSPORT (transport));
  /* Unref the transport, the xmpp connection own it now */
  g_object_unref (transport);

  g_signal_emit (self, signals[NEW_CONNECTION], 0, connection, &addr, addrlen);

  g_object_unref (connection);
  return TRUE;
}

gboolean
gibber_xmpp_connection_listener_listen (GibberXmppConnectionListener *self,
                                        int port,
                                        GError **error)
{
  GibberXmppConnectionListenerPrivate *priv =
    GIBBER_XMPP_CONNECTION_LISTENER_GET_PRIVATE (self);
  int fd;

  if (priv->listener != NULL)
    {
      g_set_error (error, GIBBER_XMPP_CONNECTION_LISTENER_ERROR,
          GIBBER_XMPP_CONNECTION_LISTENER_ERROR_ALREADY_LISTENING,
          "already listening to port %d", port);
      return FALSE;
    }

  DEBUG ("Trying to listen on port %d\n", port);
  fd = try_listening_on_port (self, port, error);
  if (fd < 0)
    return FALSE;

  DEBUG ("Listening on port %d", port);
  priv->port = port;
  priv->listener = g_io_channel_unix_new (fd);
  g_io_channel_set_close_on_unref (priv->listener, TRUE);
  priv->io_watch_in = g_io_add_watch (priv->listener, G_IO_IN,
      listener_io_in_cb, self);

  return TRUE;
}
