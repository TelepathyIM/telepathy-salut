/*
 * salut-connection.c - Source for SalutConnection
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

#include "salut-connection.h"
#include "salut-connection-signals-marshal.h"

#include "salut-connection-glue.h"

G_DEFINE_TYPE(SalutConnection, salut_connection, G_TYPE_OBJECT)

/* signal enum */
enum
{
    ALIASES_CHANGED,
    NEW_CHANNEL,
    PRESENCE_UPDATE,
    STATUS_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _SalutConnectionPrivate SalutConnectionPrivate;

struct _SalutConnectionPrivate
{
  gboolean dispose_has_run;
};

#define SALUT_CONNECTION_GET_PRIVATE(obj) \
    ((SalutConnectionPrivate *)obj->priv)

static void
salut_connection_init (SalutConnection *self)
{
  SalutConnectionPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_CONNECTION, SalutConnectionPrivate);

  self->priv = priv;

  /* allocate any data required by the object here */
}

static void salut_connection_dispose (GObject *object);
static void salut_connection_finalize (GObject *object);

static void
salut_connection_class_init (SalutConnectionClass *salut_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_connection_class);

  g_type_class_add_private (salut_connection_class, sizeof (SalutConnectionPrivate));

  object_class->dispose = salut_connection_dispose;
  object_class->finalize = salut_connection_finalize;

  signals[ALIASES_CHANGED] =
    g_signal_new ("aliases-changed",
                  G_OBJECT_CLASS_TYPE (salut_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID)))));

  signals[NEW_CHANNEL] =
    g_signal_new ("new-channel",
                  G_OBJECT_CLASS_TYPE (salut_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_connection_marshal_VOID__STRING_STRING_UINT_UINT_BOOLEAN,
                  G_TYPE_NONE, 5, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_BOOLEAN);

  signals[PRESENCE_UPDATE] =
    g_signal_new ("presence-update",
                  G_OBJECT_CLASS_TYPE (salut_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_map ("GHashTable", G_TYPE_UINT, (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)))), G_TYPE_INVALID)))));

  signals[STATUS_CHANGED] =
    g_signal_new ("status-changed",
                  G_OBJECT_CLASS_TYPE (salut_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_connection_marshal_VOID__UINT_UINT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (salut_connection_class), &dbus_glib_salut_connection_object_info);
}

void
salut_connection_dispose (GObject *object)
{
  SalutConnection *self = SALUT_CONNECTION (object);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_connection_parent_class)->dispose)
    G_OBJECT_CLASS (salut_connection_parent_class)->dispose (object);
}

void
salut_connection_finalize (GObject *object)
{
  SalutConnection *self = SALUT_CONNECTION (object);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (salut_connection_parent_class)->finalize (object);
}



/**
 * salut_connection_add_status
 *
 * Implements D-Bus method AddStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_add_status (SalutConnection *self,
                             const gchar *status,
                             GHashTable *parms,
                             GError **error)
{
  return TRUE;
}


/**
 * salut_connection_clear_status
 *
 * Implements D-Bus method ClearStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_clear_status (SalutConnection *self,
                               GError **error)
{
  return TRUE;
}


/**
 * salut_connection_connect
 *
 * Implements D-Bus method Connect
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_connect (SalutConnection *self,
                          GError **error)
{
  return TRUE;
}


/**
 * salut_connection_disconnect
 *
 * Implements D-Bus method Disconnect
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_disconnect (SalutConnection *self,
                             GError **error)
{
  return TRUE;
}


/**
 * salut_connection_get_alias_flags
 *
 * Implements D-Bus method GetAliasFlags
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_get_alias_flags (SalutConnection *self,
                                  guint *ret,
                                  GError **error)
{
  return TRUE;
}


/**
 * salut_connection_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_get_interfaces (SalutConnection *self,
                                 gchar ***ret,
                                 GError **error)
{
  return TRUE;
}


/**
 * salut_connection_get_protocol
 *
 * Implements D-Bus method GetProtocol
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_get_protocol (SalutConnection *self,
                               gchar **ret,
                               GError **error)
{
  return TRUE;
}


/**
 * salut_connection_get_self_handle
 *
 * Implements D-Bus method GetSelfHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_get_self_handle (SalutConnection *self,
                                  guint *ret,
                                  GError **error)
{
  return TRUE;
}


/**
 * salut_connection_get_status
 *
 * Implements D-Bus method GetStatus
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_get_status (SalutConnection *self,
                             guint *ret,
                             GError **error)
{
  return TRUE;
}


/**
 * salut_connection_get_statuses
 *
 * Implements D-Bus method GetStatuses
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_get_statuses (SalutConnection *self,
                               GHashTable **ret,
                               GError **error)
{
  return TRUE;
}


/**
 * salut_connection_hold_handles
 *
 * Implements D-Bus method HoldHandles
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
void
salut_connection_hold_handles (SalutConnection *self,
                               guint handle_type,
                               const GArray *handles,
                               DBusGMethodInvocation *context)
{
  return;
}


/**
 * salut_connection_inspect_handles
 *
 * Implements D-Bus method InspectHandles
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
void
salut_connection_inspect_handles (SalutConnection *self,
                                  guint handle_type,
                                  const GArray *handles,
                                  DBusGMethodInvocation *context)
{
  return;
}


/**
 * salut_connection_list_channels
 *
 * Implements D-Bus method ListChannels
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_list_channels (SalutConnection *self,
                                GPtrArray **ret,
                                GError **error)
{
  return TRUE;
}


/**
 * salut_connection_release_handles
 *
 * Implements D-Bus method ReleaseHandles
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
void
salut_connection_release_handles (SalutConnection *self,
                                  guint handle_type,
                                  const GArray *handles,
                                  DBusGMethodInvocation *context)
{
  return;
}


/**
 * salut_connection_remove_status
 *
 * Implements D-Bus method RemoveStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_remove_status (SalutConnection *self,
                                const gchar *status,
                                GError **error)
{
  return TRUE;
}


/**
 * salut_connection_request_aliases
 *
 * Implements D-Bus method RequestAliases
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_request_aliases (SalutConnection *self,
                                  const GArray *contacts,
                                  gchar ***ret,
                                  GError **error)
{
  return TRUE;
}


/**
 * salut_connection_request_channel
 *
 * Implements D-Bus method RequestChannel
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
void
salut_connection_request_channel (SalutConnection *self,
                                  const gchar *type,
                                  guint handle_type,
                                  guint handle,
                                  gboolean suppress_handler,
                                  DBusGMethodInvocation *context)
{
  return;
}


/**
 * salut_connection_request_handles
 *
 * Implements D-Bus method RequestHandles
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
void
salut_connection_request_handles (SalutConnection *self,
                                  guint handle_type,
                                  const gchar **names,
                                  DBusGMethodInvocation *context)
{
  return;
}


/**
 * salut_connection_request_presence
 *
 * Implements D-Bus method RequestPresence
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_request_presence (SalutConnection *self,
                                   const GArray *contacts,
                                   GError **error)
{
  return TRUE;
}


/**
 * salut_connection_set_aliases
 *
 * Implements D-Bus method SetAliases
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_set_aliases (SalutConnection *self,
                              GHashTable *aliases,
                              GError **error)
{
  return TRUE;
}


/**
 * salut_connection_set_last_activity_time
 *
 * Implements D-Bus method SetLastActivityTime
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_set_last_activity_time (SalutConnection *self,
                                         guint time,
                                         GError **error)
{
  return TRUE;
}


/**
 * salut_connection_set_status
 *
 * Implements D-Bus method SetStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_set_status (SalutConnection *self,
                             GHashTable *statuses,
                             GError **error)
{
  return TRUE;
}

