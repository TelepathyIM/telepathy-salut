/*
 * salut-contact-channel.c - Source for SalutContactChannel
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

#include "salut-contact-channel.h"
#include "salut-contact-channel-signals-marshal.h"

#include "salut-contact-channel-glue.h"

#include "handle-types.h"
#include "tp-channel-iface.h"
#include "salut-connection.h"
#include "telepathy-helpers.h"
#include "telepathy-interfaces.h"
#include "telepathy-errors.h"

G_DEFINE_TYPE_WITH_CODE(SalutContactChannel, salut_contact_channel, 
  G_TYPE_OBJECT, G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL));

/* signal enum */
enum
{
    CLOSED,
    GROUP_FLAGS_CHANGED,
    MEMBERS_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_CONNECTION,
  LAST_PROPERTY
};

/* private structure */
typedef struct _SalutContactChannelPrivate SalutContactChannelPrivate;

struct _SalutContactChannelPrivate
{
  SalutConnection *conn;
  gchar *object_path;
  Handle handle;
  gboolean dispose_has_run;
};

#define SALUT_CONTACT_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_CONTACT_CHANNEL, SalutContactChannelPrivate))

static void
salut_contact_channel_init (SalutContactChannel *obj)
{
  SalutContactChannelPrivate *priv = SALUT_CONTACT_CHANNEL_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  priv->object_path = NULL;
}

static GObject *
salut_contact_channel_constructor (GType type, guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj;
  DBusGConnection *bus;
  gboolean valid;
  SalutContactChannelPrivate *priv;

  /* Parent constructor chain */
  obj = G_OBJECT_CLASS(salut_contact_channel_parent_class)->
        constructor(type, n_props, props);

  priv = SALUT_CONTACT_CHANNEL_GET_PRIVATE (SALUT_CONTACT_CHANNEL (obj));

  /* Connect to the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object(bus, priv->object_path, obj);

  /* Ref our handle */
  valid = handle_ref(priv->conn->handle_repo, TP_HANDLE_TYPE_LIST, priv->handle);
  g_assert(valid);

  /* Impossible to add/remove/rescind on any of our lists */
  group_mixin_init(obj, G_STRUCT_OFFSET(SalutContactChannel, group),
                     priv->conn->handle_repo, priv->conn->self_handle);
  group_mixin_change_flags(obj, 0, 0);
  return obj;
}

static void salut_contact_channel_dispose (GObject *object);
static void salut_contact_channel_finalize (GObject *object);

static void
salut_contact_channel_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  SalutContactChannel *chan = SALUT_CONTACT_CHANNEL (object);
  SalutContactChannelPrivate *priv = SALUT_CONTACT_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST);
      break;
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, TP_HANDLE_TYPE_LIST);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, priv->handle);
      break;
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


static void
salut_contact_channel_set_property (GObject     *object,
                                    guint        property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  SalutContactChannel *chan = SALUT_CONTACT_CHANNEL (object);
  SalutContactChannelPrivate *priv = SALUT_CONTACT_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_HANDLE:
      priv->handle = g_value_get_uint (value);
      break;
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
salut_contact_channel_class_init (SalutContactChannelClass *salut_contact_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_contact_channel_class);
  GParamSpec *param_spec; 

  g_type_class_add_private (salut_contact_channel_class, sizeof (SalutContactChannelPrivate));

  object_class->constructor = salut_contact_channel_constructor;

  object_class->get_property = salut_contact_channel_get_property;
  object_class->set_property = salut_contact_channel_set_property;

  object_class->dispose = salut_contact_channel_dispose;
  object_class->finalize = salut_contact_channel_finalize;
  
  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "Roster channel object.",
                                    SALUT_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  g_object_class_override_property (object_class, PROP_OBJECT_PATH, "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE, "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE, "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (salut_contact_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_contact_channel_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
  group_mixin_class_init(object_class, 
    G_STRUCT_OFFSET(SalutContactChannelClass, group_class),
    NULL, NULL);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (salut_contact_channel_class), &dbus_glib_salut_contact_channel_object_info);
}

void
salut_contact_channel_dispose (GObject *object)
{
  SalutContactChannel *self = SALUT_CONTACT_CHANNEL (object);
  SalutContactChannelPrivate *priv = SALUT_CONTACT_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_signal_emit(self, signals[CLOSED], 0);

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_contact_channel_parent_class)->dispose)
    G_OBJECT_CLASS (salut_contact_channel_parent_class)->dispose (object);
}

void
salut_contact_channel_finalize (GObject *object)
{
  SalutContactChannel *self = SALUT_CONTACT_CHANNEL (object);
  SalutContactChannelPrivate *priv = SALUT_CONTACT_CHANNEL_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  g_free(priv->object_path);
  handle_unref(priv->conn->handle_repo, TP_HANDLE_TYPE_LIST, priv->handle);
  group_mixin_finalize(object);

  G_OBJECT_CLASS (salut_contact_channel_parent_class)->finalize (object);
}



/**
 * salut_contact_channel_add_members
 *
 * Implements DBus method AddMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean salut_contact_channel_add_members (SalutContactChannel *self, 
                                            const GArray * contacts, 
                                            const gchar * message, 
                                            GError **error)
{
  return group_mixin_add_members (G_OBJECT (self), contacts, message, 
                                         error);
}


/**
 * salut_contact_channel_close
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean salut_contact_channel_close (SalutContactChannel *obj, GError **error)
{
  *error = g_error_new (TELEPATHY_ERRORS, NotImplemented,
                        "you may not close contact list channels");
  return FALSE;
}


/**
 * salut_contact_channel_get_all_members
 *
 * Implements DBus method GetAllMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean salut_contact_channel_get_all_members (SalutContactChannel *self, 
                                                GArray ** ret, 
                                                GArray ** ret1, 
                                                GArray ** ret2, 
                                                GError **error)
{
  return group_mixin_get_all_members (G_OBJECT (self), ret, ret1, ret2, error);
}


/**
 * salut_contact_channel_get_channel_type
 *
 * Implements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean salut_contact_channel_get_channel_type (SalutContactChannel *obj, gchar ** ret, GError **error)
{
  *ret = g_strdup (TP_IFACE_CHANNEL_TYPE_CONTACT_LIST);
  return TRUE;
}


/**
 * salut_contact_channel_get_group_flags
 *
 * Implements DBus method GetGroupFlags
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean salut_contact_channel_get_group_flags (SalutContactChannel *self, 
                                                guint* ret, GError **error)
{
  return group_mixin_get_group_flags (G_OBJECT (self), ret, error);
}


/**
 * salut_contact_channel_get_handle
 *
 * Implements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean salut_contact_channel_get_handle (SalutContactChannel *self, 
                                           guint* ret, 
                                           guint* ret1, GError **error)
{
  SalutContactChannelPrivate *priv = SALUT_CONTACT_CHANNEL_GET_PRIVATE(self);
  *ret = TP_HANDLE_TYPE_LIST;
  *ret1 = priv->handle;
  return TRUE;
}


/**
 * salut_contact_channel_get_handle_owners
 *
 * Implements DBus method GetHandleOwners
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean salut_contact_channel_get_handle_owners (SalutContactChannel *self, 
                                                  const GArray * handles, 
                                                  GArray ** ret, 
                                                  GError **error)
{
  return group_mixin_get_handle_owners (G_OBJECT (self), handles, ret,
                                               error);
  return TRUE;
}


/**
 * salut_contact_channel_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean salut_contact_channel_get_interfaces (SalutContactChannel *obj, 
                                               gchar *** ret, 
                                               GError **error)
{
  const char *interfaces[] = { TP_IFACE_CHANNEL_INTERFACE_GROUP, NULL };
  
  *ret = g_strdupv ((gchar **) interfaces);
  return TRUE;
}


/**
 * salut_contact_channel_get_local_pending_members
 *
 * Implements DBus method GetLocalPendingMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean 
salut_contact_channel_get_local_pending_members (SalutContactChannel *self, 
                                                 GArray ** ret, GError **error)
{
  return group_mixin_get_local_pending_members (G_OBJECT (self), ret, 
                                                       error);
}


/**
 * salut_contact_channel_get_members
 *
 * Implements DBus method GetMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean salut_contact_channel_get_members (SalutContactChannel *self, 
                                            GArray ** ret, GError **error)
{
  return group_mixin_get_members (G_OBJECT (self), ret, error);
}


/**
 * salut_contact_channel_get_remote_pending_members
 *
 * Implements DBus method GetRemotePendingMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean 
salut_contact_channel_get_remote_pending_members (SalutContactChannel *self, 
                                                  GArray ** ret, GError **error)
{
  return group_mixin_get_remote_pending_members (G_OBJECT (self), ret, error);
}


/**
 * salut_contact_channel_get_self_handle
 *
 * Implements DBus method GetSelfHandle
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean salut_contact_channel_get_self_handle (SalutContactChannel *self, 
                                                guint* ret, GError **error)
{
  return group_mixin_get_self_handle (G_OBJECT (self), ret, error);
}


/**
 * salut_contact_channel_remove_members
 *
 * Implements DBus method RemoveMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean 
salut_contact_channel_remove_members (SalutContactChannel *self, 
                                      const GArray * contacts, 
                                      const gchar * message, GError **error)
{
   return group_mixin_remove_members(G_OBJECT (self), contacts, message, error);
}

