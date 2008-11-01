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

  if (priv->listeners != NULL)
    {
      /*
      g_io_channel_unref (priv->listener);
      priv->listener = NULL;
      g_source_remove (priv->io_watch_in);
      */
    }

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

gboolean
gibber_listener_listen_tcp (GibberListener *listener, int port,
    GError **error) {
  return gibber_listener_listen_tcp_af (listener, port, AF_UNSPEC, error);
}

gboolean
gibber_listener_listen_tcp_af (GibberListener *listener, int port,
  int adress_family, GError **error) {

  return unimplemented (error);
}

gboolean
gibber_listener_listen_tcp_loopback_af (GibberListener *listener,
  int port, int address_family, GError **error)
{
  return unimplemented (error);
}

gboolean
gibber_listener_listen_socket (GibberListener *listener,
  gchar *path, gboolean abstract, GError **error)
{
  return unimplemented (error);
}

