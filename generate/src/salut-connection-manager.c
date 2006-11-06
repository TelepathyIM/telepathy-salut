/*
 * salut-connection-manager.c - Source for SalutConnectionManager
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>

#include "salut-connection-manager.h"
#include "salut-connection-manager-signals-marshal.h"

#include "salut-connection-manager-glue.h"

G_DEFINE_TYPE(SalutConnectionManager, salut_connection_manager, G_TYPE_OBJECT)

/* signal enum */
enum
{
    NEW_CONNECTION,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _SalutConnectionManagerPrivate SalutConnectionManagerPrivate;

struct _SalutConnectionManagerPrivate
{
  gboolean dispose_has_run;
};

#define SALUT_CONNECTION_MANAGER_GET_PRIVATE(obj) \
    ((SalutConnectionManagerPrivate *)obj->priv)

static void
salut_connection_manager_init (SalutConnectionManager *self)
{
  SalutConnectionManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_CONNECTION_MANAGER, SalutConnectionManagerPrivate);

  self->priv = priv;

  /* allocate any data required by the object here */
}

static void salut_connection_manager_dispose (GObject *object);
static void salut_connection_manager_finalize (GObject *object);

static void
salut_connection_manager_class_init (SalutConnectionManagerClass *salut_connection_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_connection_manager_class);

  g_type_class_add_private (salut_connection_manager_class, sizeof (SalutConnectionManagerPrivate));

  object_class->dispose = salut_connection_manager_dispose;
  object_class->finalize = salut_connection_manager_finalize;

  signals[NEW_CONNECTION] =
    g_signal_new ("new-connection",
                  G_OBJECT_CLASS_TYPE (salut_connection_manager_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_connection_manager_marshal_VOID__STRING_STRING_STRING,
                  G_TYPE_NONE, 3, G_TYPE_STRING, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_STRING);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (salut_connection_manager_class), &dbus_glib_salut_connection_manager_object_info);
}

void
salut_connection_manager_dispose (GObject *object)
{
  SalutConnectionManager *self = SALUT_CONNECTION_MANAGER (object);
  SalutConnectionManagerPrivate *priv = SALUT_CONNECTION_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_connection_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_connection_manager_parent_class)->dispose (object);
}

void
salut_connection_manager_finalize (GObject *object)
{
  SalutConnectionManager *self = SALUT_CONNECTION_MANAGER (object);
  SalutConnectionManagerPrivate *priv = SALUT_CONNECTION_MANAGER_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (salut_connection_manager_parent_class)->finalize (object);
}



/**
 * salut_connection_manager_get_parameters
 *
 * Implements D-Bus method GetParameters
 * on interface org.freedesktop.Telepathy.ConnectionManager
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_manager_get_parameters (SalutConnectionManager *self,
                                         const gchar *proto,
                                         GPtrArray **ret,
                                         GError **error)
{
  return TRUE;
}


/**
 * salut_connection_manager_list_protocols
 *
 * Implements D-Bus method ListProtocols
 * on interface org.freedesktop.Telepathy.ConnectionManager
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_manager_list_protocols (SalutConnectionManager *self,
                                         gchar ***ret,
                                         GError **error)
{
  return TRUE;
}


/**
 * salut_connection_manager_request_connection
 *
 * Implements D-Bus method RequestConnection
 * on interface org.freedesktop.Telepathy.ConnectionManager
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_manager_request_connection (SalutConnectionManager *self,
                                             const gchar *proto,
                                             GHashTable *parameters,
                                             gchar **ret,
                                             gchar **ret1,
                                             GError **error)
{
  return TRUE;
}

