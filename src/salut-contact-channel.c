/*
 * salut-contact-channel.c - Source for SalutContactChannel
 * Copyright (C) 2005-2008 Collabora Ltd.
 * Copyright (C) 2005-2008 Nokia Corporation
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

#include "salut-contact-channel.h"

#include <stdio.h>
#include <stdlib.h>

#include <dbus/dbus-glib.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/util.h>

#include "extensions/extensions.h"

#include "salut-connection.h"

static void
channel_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE(SalutContactChannel, salut_contact_channel,
  G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
      tp_dbus_properties_mixin_iface_init);
  G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL);
  G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
  G_IMPLEMENT_INTERFACE (SALUT_TYPE_SVC_CHANNEL_FUTURE, NULL);
  G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);
  G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
      tp_group_mixin_iface_init);
  G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_CONTACT_LIST, NULL);
)

static const gchar *salut_contact_channel_interfaces[] = {
    TP_IFACE_CHANNEL_INTERFACE_GROUP,
    SALUT_IFACE_CHANNEL_FUTURE,
    NULL
};

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_CONNECTION,
  PROP_INTERFACES,
  PROP_TARGET_ID,
  PROP_CHANNEL_PROPERTIES,
  PROP_CHANNEL_DESTROYED,
  PROP_INITIATOR_HANDLE,
  PROP_INITIATOR_ID,
  PROP_REQUESTED,
  LAST_PROPERTY
};

/* private structure */
typedef struct _SalutContactChannelPrivate SalutContactChannelPrivate;

struct _SalutContactChannelPrivate
{
  SalutConnection *conn;
  gchar *object_path;
  TpHandle handle;
  gboolean requested;
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
  SalutContactChannelPrivate *priv;
  TpHandleRepoIface *handle_repo;
  TpHandleRepoIface *contact_repo;
  TpBaseConnection *base_conn;

  /* Parent constructor chain */
  obj = G_OBJECT_CLASS(salut_contact_channel_parent_class)->
        constructor (type, n_props, props);

  priv = SALUT_CONTACT_CHANNEL_GET_PRIVATE (SALUT_CONTACT_CHANNEL (obj));

  /* Connect to the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  /* Ref our handle */
  base_conn = TP_BASE_CONNECTION(priv->conn);

  handle_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_LIST);
  contact_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);

  tp_handle_ref (handle_repo, priv->handle);

  /* Impossible to add/remove/rescind on any of our lists */
  tp_group_mixin_init (obj,
      G_STRUCT_OFFSET (SalutContactChannel, group),
      contact_repo, base_conn->self_handle);

  tp_group_mixin_change_flags (obj, 0, 0);
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
      case PROP_INTERFACES:
        g_value_set_static_boxed (value, salut_contact_channel_interfaces);
        break;
    case PROP_TARGET_ID:
      {
         TpHandleRepoIface *repo = tp_base_connection_get_handles (
           (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_LIST);

         g_value_set_string (value, tp_handle_inspect (repo, priv->handle));
      }
      break;
    case PROP_CHANNEL_PROPERTIES:
      g_value_take_boxed (value,
          tp_dbus_properties_mixin_make_properties_hash (object,
              TP_IFACE_CHANNEL, "TargetHandle",
              TP_IFACE_CHANNEL, "TargetHandleType",
              TP_IFACE_CHANNEL, "ChannelType",
              TP_IFACE_CHANNEL, "TargetID",
              SALUT_IFACE_CHANNEL_FUTURE, "InitiatorHandle",
              SALUT_IFACE_CHANNEL_FUTURE, "InitiatorID",
              SALUT_IFACE_CHANNEL_FUTURE, "Requested",
              NULL));
      break;
    case PROP_CHANNEL_DESTROYED:
      g_value_set_boolean (value, TRUE);
      break;
    case PROP_INITIATOR_HANDLE:
      g_value_set_uint (value, 0);
      break;
    case PROP_INITIATOR_ID:
      g_value_set_static_string (value, "");
      break;
    case PROP_REQUESTED:
      g_value_set_boolean (value, priv->requested);
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
  const gchar *tmp;

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
    case PROP_HANDLE_TYPE:
      g_assert (g_value_get_uint (value) == 0
               || g_value_get_uint (value) == TP_HANDLE_TYPE_LIST);
      break;
    case PROP_CHANNEL_TYPE:
      tmp = g_value_get_string (value);
      g_assert (tmp == NULL
               || !tp_strdiff (g_value_get_string (value),
                       TP_IFACE_CHANNEL_TYPE_CONTACT_LIST));
      break;
    case PROP_REQUESTED:
      priv->requested = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
salut_contact_channel_class_init (SalutContactChannelClass *salut_contact_channel_class)
{
  static TpDBusPropertiesMixinPropImpl channel_props[] = {
      { "TargetHandleType", "handle-type", NULL },
      { "TargetHandle", "handle", NULL },
      { "TargetID", "target-id", NULL },
      { "ChannelType", "channel-type", NULL },
      { "Interfaces", "interfaces", NULL },
      { NULL }
  };
  static TpDBusPropertiesMixinPropImpl future_props[] = {
      { "Requested", "requested", NULL },
      { "InitiatorHandle", "initiator-handle", NULL },
      { "InitiatorID", "initiator-id", NULL },
      { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { TP_IFACE_CHANNEL,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        channel_props,
      },
      { SALUT_IFACE_CHANNEL_FUTURE,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        future_props,
      },
      { NULL }
  };
  GObjectClass *object_class = G_OBJECT_CLASS (salut_contact_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_contact_channel_class, sizeof (SalutContactChannelPrivate));

  object_class->constructor = salut_contact_channel_constructor;

  object_class->get_property = salut_contact_channel_get_property;
  object_class->set_property = salut_contact_channel_set_property;

  object_class->dispose = salut_contact_channel_dispose;
  object_class->finalize = salut_contact_channel_finalize;

  param_spec = g_param_spec_object ("connection", "SalutConnection object",
                                    "Salut connection object that owns this "
                                    "Roster channel object.",
                                    SALUT_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
      "Additional Channel.Interface.* interfaces",
      G_TYPE_STRV,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NAME);
  g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

  g_object_class_override_property (object_class, PROP_OBJECT_PATH,
      "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
      "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
      "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");

  g_object_class_override_property (object_class, PROP_CHANNEL_PROPERTIES,
      "channel-properties");
  g_object_class_override_property (object_class, PROP_CHANNEL_DESTROYED,
      "channel-destroyed");

  param_spec = g_param_spec_string ("target-id", "Target JID",
      "The string obtained by inspecting this channel's handle",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_TARGET_ID, param_spec);

  param_spec = g_param_spec_boolean ("requested", "Requested?",
      "True if this channel was requested by the local user",
      FALSE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NAME);
  g_object_class_install_property (object_class, PROP_REQUESTED, param_spec);

  param_spec = g_param_spec_uint ("initiator-handle", "Initiator's handle",
      "Always 0 on contact list channels",
      0, G_MAXUINT32, 0,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NAME);
  g_object_class_install_property (object_class, PROP_INITIATOR_HANDLE,
      param_spec);

  param_spec = g_param_spec_string ("initiator-id", "Initiator JID",
      "Always the empty string on contact list channels",
      NULL,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NAME);
  g_object_class_install_property (object_class, PROP_INITIATOR_ID,
      param_spec);

  salut_contact_channel_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (SalutContactChannelClass, dbus_props_class));

  tp_group_mixin_class_init (object_class,
      G_STRUCT_OFFSET (SalutContactChannelClass, group_class),
      NULL, NULL);
  tp_group_mixin_init_dbus_properties (object_class);
}

void
salut_contact_channel_dispose (GObject *object)
{
  SalutContactChannel *self = SALUT_CONTACT_CHANNEL (object);
  SalutContactChannelPrivate *priv = SALUT_CONTACT_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *base_conn = TP_BASE_CONNECTION(priv->conn);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (base_conn,
        TP_HANDLE_TYPE_LIST);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  tp_svc_channel_emit_closed (TP_SVC_CHANNEL (object));

  tp_handle_unref (handle_repo, priv->handle);


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
  g_free (priv->object_path);

  tp_group_mixin_finalize (object);

  G_OBJECT_CLASS (salut_contact_channel_parent_class)->finalize (object);
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
static void
salut_contact_channel_get_channel_type (TpSvcChannel *iface,
    DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      TP_IFACE_CHANNEL_TYPE_CONTACT_LIST);
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
static void
salut_contact_channel_get_handle (TpSvcChannel *iface,
                                  DBusGMethodInvocation *context)
{
  SalutContactChannel *self = SALUT_CONTACT_CHANNEL(iface);
  SalutContactChannelPrivate *priv = SALUT_CONTACT_CHANNEL_GET_PRIVATE(self);

  tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_LIST,
                                         priv->handle);
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
static void
salut_contact_channel_get_interfaces (TpSvcChannel *iface,
                                      DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_interfaces (context,
      salut_contact_channel_interfaces);
}


static void
channel_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, salut_contact_channel_##x)
  IMPLEMENT (get_channel_type);
  IMPLEMENT (get_handle);
  IMPLEMENT (get_interfaces);
#undef IMPLEMENT
}
