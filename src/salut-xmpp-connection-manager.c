/*
 * salut-xmpp-connection-manager.c - Source for SalutXmppConnectionManager
 * Copyright (C) 2007 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the tubesplied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "salut-xmpp-connection-manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "salut-tubes-channel.h"
#include "salut-contact.h"

#include <gibber/gibber-xmpp-connection-listener.h>

#define DEBUG_FLAG DEBUG_CONNECTION
#include "debug.h"

#include "signals-marshal.h"

G_DEFINE_TYPE (SalutXmppConnectionManager, salut_xmpp_connection_manager, \
    G_TYPE_OBJECT)

/* signals */
enum
{
  NEW_CONNECTION,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _SalutXmppConnectionManagerPrivate \
          SalutXmppConnectionManagerPrivate;

struct _SalutXmppConnectionManagerPrivate
{
  GibberXmppConnectionListener *listener;

  gboolean dispose_has_run;
};

#define SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE(obj) \
    ((SalutXmppConnectionManagerPrivate *) obj->priv)

static void
salut_xmpp_connection_manager_init (SalutXmppConnectionManager *self)
{
  SalutXmppConnectionManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_XMPP_CONNECTION_MANAGER, SalutXmppConnectionManagerPrivate);

  self->priv = priv;

  priv->listener = NULL;
  priv->dispose_has_run = FALSE;
}

static void
new_connection_cb (GibberXmppConnectionListener *listener,
                   GibberXmppConnection *connection,
                   struct sockaddr_storage *addr,
                   guint size,
                   gpointer user_data)
{
  SalutXmppConnectionManager *self = SALUT_XMPP_CONNECTION_MANAGER (user_data);
  g_signal_emit (self, signals[NEW_CONNECTION], 0, connection, addr, size);
}

static GObject *
salut_xmpp_connection_manager_constructor (GType type,
                                           guint n_props,
                                           GObjectConstructParam *props)
{
  GObject *obj;
  SalutXmppConnectionManager *self;
  SalutXmppConnectionManagerPrivate *priv;

  obj = G_OBJECT_CLASS (salut_xmpp_connection_manager_parent_class)->
    constructor (type, n_props, props);

  self = SALUT_XMPP_CONNECTION_MANAGER (obj);
  priv = SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);

  priv->listener = gibber_xmpp_connection_listener_new ();
  g_signal_connect (priv->listener, "new-connection",
      G_CALLBACK (new_connection_cb), self);

  return obj;
}

void
salut_xmpp_connection_manager_dispose (GObject *object)
{
  SalutXmppConnectionManager *self = SALUT_XMPP_CONNECTION_MANAGER (object);
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  if (priv->listener != NULL)
    {
      g_object_unref (priv->listener);
      priv->listener = NULL;
    }

  priv->dispose_has_run = TRUE;

  if (G_OBJECT_CLASS (salut_xmpp_connection_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_xmpp_connection_manager_parent_class)->dispose (
        object);
}

static void
salut_xmpp_connection_manager_get_property (GObject *object,
                                            guint property_id,
                                            GValue *value,
                                            GParamSpec *pspec)
{
  switch (property_id)
    {
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_xmpp_connection_manager_set_property (GObject *object,
                                            guint property_id,
                                            const GValue *value,
                                            GParamSpec *pspec)
{
  switch (property_id)
    {
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_xmpp_connection_manager_class_init (
    SalutXmppConnectionManagerClass *salut_xmpp_connection_manager_class)
{
  GObjectClass *object_class =
    G_OBJECT_CLASS (salut_xmpp_connection_manager_class);

  g_type_class_add_private (salut_xmpp_connection_manager_class,
      sizeof (SalutXmppConnectionManagerPrivate));

  object_class->constructor = salut_xmpp_connection_manager_constructor;

  object_class->dispose = salut_xmpp_connection_manager_dispose;

  object_class->get_property = salut_xmpp_connection_manager_get_property;
  object_class->set_property = salut_xmpp_connection_manager_set_property;

  signals[NEW_CONNECTION] =
    g_signal_new (
        "new-connection",
        G_OBJECT_CLASS_TYPE (salut_xmpp_connection_manager_class),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
        0,
        NULL, NULL,
        salut_signals_marshal_VOID__OBJECT_POINTER_UINT,
        G_TYPE_NONE, 3, GIBBER_TYPE_XMPP_CONNECTION, G_TYPE_POINTER,
        G_TYPE_UINT);
}

SalutXmppConnectionManager *
salut_xmpp_connection_manager_new (void)
{
  return g_object_new (
      SALUT_TYPE_XMPP_CONNECTION_MANAGER,
      NULL);
}

int
salut_xmpp_connection_manager_listen (SalutXmppConnectionManager *self)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
  int port;

  for (port = 5298; port < 5400; port++)
    {
      GError *error = NULL;
      if (gibber_xmpp_connection_listener_listen (priv->listener, port,
            &error))
        break;

      if (error->code != GIBBER_XMPP_CONNECTION_LISTENER_ERROR_ADDR_IN_USE)
        {
          g_error_free (error);
          return -1;
        }

      g_error_free (error);
      error = NULL;
    }

  if (port >= 5400)
    return -1;

  return port;
}
