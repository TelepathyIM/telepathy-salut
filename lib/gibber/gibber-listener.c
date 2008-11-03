/*
 * gibber-listener.c - Source for GibberListener
 * Copyright (C) 2007,2008 Collabora Ltd.
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

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>

#include <glib.h>

#include "gibber-listener.h"
#include "gibber-fd-transport.h"
#include "gibber-util.h"

#define DEBUG_FLAG DEBUG_NET
#include "gibber-debug.h"

#include "signals-marshal.h"

G_DEFINE_TYPE (GibberListener, gibber_listener, \
    G_TYPE_OBJECT);

/* signals */
enum
{
  NEW_CONNECTION,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

typedef struct {
  GIOChannel *listener;
  guint io_watch_in;
} Listener;

typedef struct _GibberListenerPrivate GibberListenerPrivate;

struct _GibberListenerPrivate
{
  GSList *listeners;

  gboolean dispose_has_run;
};

#define GIBBER_LISTENER_GET_PRIVATE(obj) \
    ((GibberListenerPrivate *) obj->priv)

GQuark
gibber_listener_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string (
        "gibber_listener_error");

  return quark;
}

static void
gibber_listener_init (GibberListener *self)
{
  GibberListenerPrivate *priv =
    G_TYPE_INSTANCE_GET_PRIVATE (self, GIBBER_TYPE_LISTENER,
        GibberListenerPrivate);

  self->priv = priv;

  priv->dispose_has_run = FALSE;
}

static void
gibber_listener_dispose (GObject *object)
{
  GibberListener *self =
    GIBBER_LISTENER (object);
  GibberListenerPrivate *priv =
    GIBBER_LISTENER_GET_PRIVATE (self);
  GSList *t;

  for (t = priv->listeners ; t != NULL ; t = g_slist_delete_link (t, t))
    {
      Listener *l = (Listener *)t->data;

      g_io_channel_unref (l->listener);
      g_source_remove (l->io_watch_in);
      g_slice_free (Listener, l);
    }

  priv->listeners = NULL;

  G_OBJECT_CLASS (gibber_listener_parent_class)->dispose (
      object);
}

static void
gibber_listener_class_init (
    GibberListenerClass *gibber_listener_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (
      gibber_listener_class);

  g_type_class_add_private (gibber_listener_class,
      sizeof (GibberListenerPrivate));

  object_class->dispose = gibber_listener_dispose;

  signals[NEW_CONNECTION] =
    g_signal_new (
        "new-connection",
        G_OBJECT_CLASS_TYPE (gibber_listener_class),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
        0,
        NULL, NULL,
        _gibber_signals_marshal_VOID__OBJECT_POINTER_UINT,
        G_TYPE_NONE, 3, GIBBER_TYPE_TRANSPORT, G_TYPE_POINTER, G_TYPE_UINT);
}

GibberListener *
gibber_listener_new (void)
{
  return g_object_new (GIBBER_TYPE_LISTENER,
      NULL);
}

static gboolean
unimplemented (GError **error)
{
  g_set_error (error, GIBBER_LISTENER_ERROR, GIBBER_LISTENER_ERROR_FAILED,
    "Unimplemented");

  return FALSE;
}


static gboolean
listener_io_in_cb (GIOChannel *source,
                   GIOCondition condition,
                   gpointer user_data)
{
  GibberListener *self = GIBBER_LISTENER (user_data);
  GibberFdTransport *transport;
  int fd, nfd;
  int ret;
  char host[NI_MAXHOST];
  char port[NI_MAXSERV];
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof (struct sockaddr_storage);

  fd = g_io_channel_unix_get_fd (source);
  nfd = accept (fd, (struct sockaddr *) &addr, &addrlen);
  gibber_normalize_address (&addr);

  transport = g_object_new (GIBBER_TYPE_FD_TRANSPORT, NULL);
  gibber_fd_transport_set_fd (transport, nfd);

  ret = getnameinfo ((struct sockaddr *) &addr, addrlen,
      host, NI_MAXHOST, port, NI_MAXSERV,
      NI_NUMERICHOST | NI_NUMERICSERV);

  if (ret == 0)
    DEBUG("New connection from %s port %s", host, port);
  else
    DEBUG("New connection..");

  g_signal_emit (self, signals[NEW_CONNECTION], 0, transport, &addr, addrlen);

  g_object_unref (transport);
  return TRUE;
}

static gboolean
add_listener (GibberListener *self, int family, int type, int protocol,
  struct sockaddr *address, socklen_t addrlen, GError **error)
{
  #define BACKLOG 5
  int fd = -1, ret, yes = 1;
  Listener *l;
  GibberListenerPrivate *priv = self->priv;
  char name [NI_MAXHOST], portname[NI_MAXSERV];
  struct sockaddr_storage baddress;
  socklen_t baddrlen;

  fd = socket (family, type, protocol);
  if (fd == -1)
    {
      DEBUG ("socket failed: %s", g_strerror (errno));
      g_set_error (error, GIBBER_LISTENER_ERROR,
          errno == EAFNOSUPPORT ? GIBBER_LISTENER_ERROR_FAMILY_NOT_SUPPORTED :
            GIBBER_LISTENER_ERROR_FAILED,
          "%s", g_strerror (errno));
      goto error;
    }

  ret = setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (int));
  if (ret == -1)
    {
      DEBUG ("setsockopt failed: %s", g_strerror (errno));
      g_set_error (error, GIBBER_LISTENER_ERROR,
          GIBBER_LISTENER_ERROR_FAILED,
          "%s", g_strerror (errno));
      goto error;
    }

#ifdef IPV6_V6ONLY
  if (family == AF_INET6)
    {
      ret = setsockopt (fd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof (int));
      if (ret == -1)
        {
          DEBUG ("setsockopt failed: %s", g_strerror (errno));
          g_set_error (error, GIBBER_LISTENER_ERROR,
             GIBBER_LISTENER_ERROR_FAILED,
             "%s", g_strerror (errno));
          goto error;
        }
    }
#endif

  ret = bind (fd, address, addrlen);
  if (ret  < 0)
    {
      DEBUG ("bind failed: %s", g_strerror (errno));
      g_set_error (error, GIBBER_LISTENER_ERROR,
          errno == EADDRINUSE ?
          GIBBER_LISTENER_ERROR_ADDRESS_IN_USE :
          GIBBER_LISTENER_ERROR_FAILED,
          "%s", g_strerror (errno));
      goto error;
    }

  ret = listen (fd, BACKLOG);
  if (ret == -1)
    {
      DEBUG ("listen failed: %s", g_strerror (errno));
      g_set_error (error, GIBBER_LISTENER_ERROR,
          errno == EADDRINUSE ?
          GIBBER_LISTENER_ERROR_ADDRESS_IN_USE :
          GIBBER_LISTENER_ERROR_FAILED,
          "%s", g_strerror (errno));
      goto error;
    }

  getsockname (fd, (struct sockaddr *)&baddress, &baddrlen);
  getnameinfo ((struct sockaddr *)&baddress, baddrlen, name, sizeof (name),
      portname, sizeof (portname), NI_NUMERICHOST | NI_NUMERICSERV);

  DEBUG ( "Listening on %s port %s...", name, portname);

  l = g_slice_new(Listener);

  l->listener = g_io_channel_unix_new (fd);
  g_io_channel_set_close_on_unref (l->listener, TRUE);
  l->io_watch_in = g_io_add_watch (l->listener, G_IO_IN,
      listener_io_in_cb, self);

  priv->listeners = g_slist_append (priv->listeners, l);

  return TRUE;

error:
  if (fd > 0)
    close (fd);
  return FALSE;
}

static gboolean
listen_tcp_af (GibberListener *listener, int port,
  GibberAddressFamily family, gboolean loopback, GError **error)
{
  struct addrinfo req, *ans = NULL, *a;
  GibberListenerPrivate *priv = listener->priv;
  int ret;
  gchar sport[6];

  memset (&req, 0, sizeof (req));
  if (!loopback)
    req.ai_flags = AI_PASSIVE;

  switch (family)
    {
      case GIBBER_AF_IPV4:
        req.ai_family = AF_INET;
        break;
      case GIBBER_AF_IPV6:
        req.ai_family = AF_INET6;
        break;
      case GIBBER_AF_ANY:
        req.ai_family = AF_UNSPEC;
        break;
    }
  req.ai_socktype = SOCK_STREAM;
  req.ai_protocol = IPPROTO_TCP;

  g_snprintf (sport, 6, "%d", port);

  ret = getaddrinfo (NULL, sport, &req, &ans);
  if (ret != 0)
    {
      DEBUG ("getaddrinfo failed: %s", gai_strerror (ret));
      g_set_error (error, GIBBER_LISTENER_ERROR,
          GIBBER_LISTENER_ERROR_FAILED,
          "%s", gai_strerror (ret));
      goto error;
    }

  for (a = ans ; a != NULL ; a = a->ai_next)
    {
      gboolean ret;
      GError *terror = NULL;

      ret = add_listener (listener, a->ai_family, a->ai_socktype,
        a->ai_protocol, a->ai_addr, a->ai_addrlen, &terror);

      if (ret == FALSE)
        {
          gboolean fatal = !g_error_matches (terror, GIBBER_LISTENER_ERROR,
              GIBBER_LISTENER_ERROR_FAMILY_NOT_SUPPORTED);

          /* let error always point to the last error */
          g_clear_error (error);
          g_propagate_error (error, terror);

          if (fatal)
              goto error;
        }
    }


  /* If all listeners failed, report the last error */
  if (priv->listeners == NULL)
    goto error;

  /* There was an error at some point, but it was not fatal. ignore it */
  g_clear_error (error);

  freeaddrinfo (ans);

  return TRUE;

error:
  if (ans != NULL)
    freeaddrinfo (ans);

  return FALSE;
}

gboolean
gibber_listener_listen_tcp (GibberListener *listener, int port, GError **error)
{
  return gibber_listener_listen_tcp_af (listener, port, GIBBER_AF_ANY, error);
}

gboolean
gibber_listener_listen_tcp_af (GibberListener *listener, int port,
  GibberAddressFamily family, GError **error)
{
  return listen_tcp_af (listener, port, family, FALSE, error);
}

gboolean
gibber_listener_listen_tcp_loopback (GibberListener *listener,
  int port, GError **error)
{
  return gibber_listener_listen_tcp_loopback_af (listener, port,
    GIBBER_AF_ANY, error);
}

gboolean
gibber_listener_listen_tcp_loopback_af (GibberListener *listener,
  int port, GibberAddressFamily family, GError **error)
{
  return listen_tcp_af (listener, port, family, TRUE, error);
}

gboolean
gibber_listener_listen_socket (GibberListener *listener,
  gchar *path, gboolean abstract, GError **error)
{
  return unimplemented (error);
}

