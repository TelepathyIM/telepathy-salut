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

#include "salut-connection.h"
#include "salut-contact-channel.h"

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>

static void 
channel_iface_init(gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE(SalutContactChannel, salut_contact_channel, 
  G_TYPE_OBJECT, 
  G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL);
  G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
  G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
      tp_group_mixin_iface_init);
  G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_CONTACT_LIST, NULL);
)

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
  TpHandle handle;
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
        constructor(type, n_props, props);

  priv = SALUT_CONTACT_CHANNEL_GET_PRIVATE (SALUT_CONTACT_CHANNEL (obj));

  /* Connect to the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object(bus, priv->object_path, obj);

  /* Ref our handle */
  base_conn = TP_BASE_CONNECTION(priv->conn);

  handle_repo = tp_base_connection_get_handles(base_conn, TP_HANDLE_TYPE_LIST);
  contact_repo = tp_base_connection_get_handles(base_conn, 
      TP_HANDLE_TYPE_CONTACT);

  tp_handle_ref(handle_repo, priv->handle);

  /* Impossible to add/remove/rescind on any of our lists */
  tp_group_mixin_init(TP_SVC_CHANNEL_INTERFACE_GROUP(obj),
      G_STRUCT_OFFSET(SalutContactChannel, group),
      contact_repo, base_conn->self_handle);

  tp_group_mixin_change_flags(TP_SVC_CHANNEL_INTERFACE_GROUP(obj), 0, 0);
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
      g_assert(g_value_get_uint(value) == 0 
               || g_value_get_uint(value) == TP_HANDLE_TYPE_LIST);
      break;
    case PROP_CHANNEL_TYPE:
      tmp = g_value_get_string(value);
      g_assert(tmp == NULL 
               || !tp_strdiff(g_value_get_string(value),
                       TP_IFACE_CHANNEL_TYPE_CONTACT_LIST));
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

  tp_group_mixin_class_init((TpSvcChannelInterfaceGroupClass *)object_class, 
    G_STRUCT_OFFSET(SalutContactChannelClass, group_class),
    NULL, NULL);
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

  tp_svc_channel_emit_closed(TP_SVC_CHANNEL(object));

  tp_handle_unref(handle_repo, priv->handle);


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

  tp_group_mixin_finalize(TP_SVC_CHANNEL_INTERFACE_GROUP(object));

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
  tp_svc_channel_return_from_get_channel_type(context, 
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
                                       DBusGMethodInvocation *context) {
  const char *interfaces[] = { TP_IFACE_CHANNEL_INTERFACE_GROUP, NULL };
  
  tp_svc_channel_return_from_get_interfaces (context, interfaces);
}


static void
channel_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, salut_contact_channel_##x)
  IMPLEMENT(get_channel_type);
  IMPLEMENT(get_handle);
  IMPLEMENT(get_interfaces);
#undef IMPLEMENT
}
