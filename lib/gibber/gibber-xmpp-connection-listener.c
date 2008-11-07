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
#include "gibber-listener.h"

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
  GibberListener *listener;

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
      g_object_unref (priv->listener);
      priv->listener = NULL;
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

static gboolean
new_connection_cb (GibberListener *listener,
                   GibberTransport *transport,
                   struct sockaddr *address,
                   socklen_t addrlen,
                   gpointer user_data)
{
  GibberXmppConnectionListener *self =
    GIBBER_XMPP_CONNECTION_LISTENER (user_data);
  GibberXmppConnection *connection;
  connection = gibber_xmpp_connection_new (transport);

  g_signal_emit (self, signals[NEW_CONNECTION], 0, connection,
    address, addrlen);

  g_object_unref (connection);
  return TRUE;
}

/**
 * port: the port, or 0 to choose a random port
 */
gboolean
gibber_xmpp_connection_listener_listen (GibberXmppConnectionListener *self,
                                        int port,
                                        GError **error)
{
  GibberXmppConnectionListenerPrivate *priv =
    GIBBER_XMPP_CONNECTION_LISTENER_GET_PRIVATE (self);
  int ret;

  if (priv->listener == NULL)
    {
      priv->listener = gibber_listener_new ();
      g_signal_connect (priv->listener, "new-connection",
        G_CALLBACK (new_connection_cb), self);
    }

  ret = gibber_listener_listen_tcp (priv->listener, port, error);

  if (ret == TRUE)
    priv->port = gibber_listener_get_port (priv->listener);

  return ret;
}

int
gibber_xmpp_connection_listener_get_port (
    GibberXmppConnectionListener *self)
{
  GibberXmppConnectionListenerPrivate *priv =
    GIBBER_XMPP_CONNECTION_LISTENER_GET_PRIVATE (self);

  return priv->port;
}
