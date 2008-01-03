/*
 * salut-roomlist-channel.c - Source for SalutRoomlistChannel
 * Copyright (C) 2007 Collabora Ltd.
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

#include "salut-roomlist-channel.h"

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "salut-connection.h"
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/svc-channel.h>

#define DEBUG_FLAG DEBUG_ROOMLIST
#include "debug.h"

#define SALUT_TP_TYPE_ROOM_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), \
      G_TYPE_INVALID))

static void channel_iface_init (gpointer, gpointer);
static void roomlist_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (SalutRoomlistChannel, salut_roomlist_channel,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_ROOM_LIST,
      roomlist_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL)
    );

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
typedef struct _SalutRoomlistChannelPrivate SalutRoomlistChannelPrivate;

struct _SalutRoomlistChannelPrivate
{
  SalutConnection *connection;
  gchar *object_path;

  GPtrArray *rooms;

  gboolean closed;
  gboolean dispose_has_run;
};

#define SALUT_ROOMLIST_CHANNEL_GET_PRIVATE(obj) \
    ((SalutRoomlistChannelPrivate *) ((SalutRoomlistChannel *)obj->priv))

static void
salut_roomlist_channel_init (SalutRoomlistChannel *self)
{
  SalutRoomlistChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_ROOMLIST_CHANNEL, SalutRoomlistChannelPrivate);

  self->priv = priv;

  priv->rooms = g_ptr_array_new ();
}

static GObject *
salut_roomlist_channel_constructor (GType type,
                                    guint n_props,
                                    GObjectConstructParam *props)
{
  GObject *obj;
  SalutRoomlistChannelPrivate *priv;
  DBusGConnection *bus;

  obj = G_OBJECT_CLASS (salut_roomlist_channel_parent_class)->constructor (
      type, n_props, props);
  priv = SALUT_ROOMLIST_CHANNEL_GET_PRIVATE (SALUT_ROOMLIST_CHANNEL (obj));

  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  return obj;
}

static void
salut_roomlist_channel_get_property (GObject *object,
                                     guint property_id,
                                     GValue *value,
                                     GParamSpec *pspec)
{
  SalutRoomlistChannel *chan = SALUT_ROOMLIST_CHANNEL (object);
  SalutRoomlistChannelPrivate *priv =
    SALUT_ROOMLIST_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_ROOM_LIST);
      break;
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, TP_HANDLE_TYPE_NONE);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, 0);
      break;
    case PROP_CONNECTION:
      g_value_set_object (value, priv->connection);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
salut_roomlist_channel_set_property (GObject *object,
                                     guint property_id,
                                     const GValue *value,
                                     GParamSpec *pspec)
{
  SalutRoomlistChannel *chan = SALUT_ROOMLIST_CHANNEL (object);
  SalutRoomlistChannelPrivate *priv =
    SALUT_ROOMLIST_CHANNEL_GET_PRIVATE (chan);
  const gchar *value_str;

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_CHANNEL_TYPE:
      /* this property is writable in the interface (in
       * telepathy-glib > 0.7.0), but not actually
       * meaningfully changeable on this channel, so we do nothing */
      value_str = g_value_get_string (value);
      g_assert (value_str == NULL || !tp_strdiff (value_str,
            TP_IFACE_CHANNEL_TYPE_ROOM_LIST));
      break;
    case PROP_HANDLE:
      /* this property is writable in the interface, but not actually
       * meaningfully changable on this channel, so we do nothing */
      g_assert (g_value_get_uint (value) == 0);
      break;
    case PROP_HANDLE_TYPE:
      /* this property is writable in the interface, but not actually
       * meaningfully changable on this channel, so we do nothing */
      g_assert (g_value_get_uint (value) == TP_HANDLE_TYPE_NONE);
      break;
    case PROP_CONNECTION:
      priv->connection = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void salut_roomlist_channel_dispose (GObject *object);
static void salut_roomlist_channel_finalize (GObject *object);

static void
salut_roomlist_channel_class_init (
    SalutRoomlistChannelClass *salut_roomlist_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_roomlist_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_roomlist_channel_class,
      sizeof (SalutRoomlistChannelPrivate));

  object_class->constructor = salut_roomlist_channel_constructor;

  object_class->get_property = salut_roomlist_channel_get_property;
  object_class->set_property = salut_roomlist_channel_set_property;

  object_class->dispose = salut_roomlist_channel_dispose;
  object_class->finalize = salut_roomlist_channel_finalize;

  g_object_class_override_property (object_class, PROP_OBJECT_PATH,
      "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
      "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
      "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE,
      "handle");

  param_spec = g_param_spec_object ("connection", "SalutConnection object",
                                    "Salut connection object that owns this "
                                    "room list channel object.",
                                    SALUT_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);
}

static void
rooms_free (SalutRoomlistChannel *self)
{
  SalutRoomlistChannelPrivate *priv =
    SALUT_ROOMLIST_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *base_connection = (TpBaseConnection *) (priv->connection);
  TpHandleRepoIface *room_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_ROOM);
  guint i;

  g_assert (priv->rooms != NULL);

  for (i = 0; i < priv->rooms->len; i++)
    {
      GValue room = {0,};
      gpointer boxed;
      TpHandle handle;

      boxed = g_ptr_array_index (priv->rooms, i);
      g_value_init (&room, SALUT_TP_TYPE_ROOM_STRUCT);
      g_value_set_static_boxed (&room, boxed);
      dbus_g_type_struct_get (&room,
          0, &handle,
          G_MAXUINT);

      g_boxed_free (SALUT_TP_TYPE_ROOM_STRUCT, boxed);
      tp_handle_unref (room_repo, handle);
    }

  g_ptr_array_free (priv->rooms, TRUE);
  priv->rooms = NULL;
}

static void
salut_roomlist_channel_dispose (GObject *object)
{
  SalutRoomlistChannel *self = SALUT_ROOMLIST_CHANNEL (object);
  SalutRoomlistChannelPrivate *priv =
    SALUT_ROOMLIST_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (!priv->closed)
    {
      tp_svc_channel_emit_closed ((TpSvcChannel *)object);
      priv->closed = TRUE;
    }

  if (G_OBJECT_CLASS (salut_roomlist_channel_parent_class)->dispose)
    G_OBJECT_CLASS (salut_roomlist_channel_parent_class)->dispose (object);
}

static void
salut_roomlist_channel_finalize (GObject *object)
{
  SalutRoomlistChannel *self = SALUT_ROOMLIST_CHANNEL (object);
  SalutRoomlistChannelPrivate *priv =
    SALUT_ROOMLIST_CHANNEL_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  g_free (priv->object_path);

  if (priv->rooms != NULL)
    rooms_free (self);

  G_OBJECT_CLASS (salut_roomlist_channel_parent_class)->finalize (object);
}

SalutRoomlistChannel *
salut_roomlist_channel_new (SalutConnection *conn,
                            const gchar *object_path)
{
  g_return_val_if_fail (SALUT_IS_CONNECTION (conn), NULL);
  g_return_val_if_fail (object_path != NULL, NULL);

  return SALUT_ROOMLIST_CHANNEL (
      g_object_new (SALUT_TYPE_ROOMLIST_CHANNEL,
                    "connection", conn,
                    "object-path", object_path,
                    NULL));
}

void
salut_roomlist_channel_add_room (SalutRoomlistChannel *self,
                                 const gchar *room_name)
{
  SalutRoomlistChannelPrivate *priv =
    SALUT_ROOMLIST_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *base_connection = (TpBaseConnection *) (priv->connection);
  TpHandleRepoIface *room_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_ROOM);
  GValue room = {0,};
  TpHandle handle;
  GHashTable *keys;

  handle = tp_handle_ensure (room_repo, room_name, NULL, NULL);
  if (handle == 0)
    return;

  keys = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);

  g_value_init (&room, SALUT_TP_TYPE_ROOM_STRUCT);
  g_value_take_boxed (&room,
      dbus_g_type_specialized_construct (SALUT_TP_TYPE_ROOM_STRUCT));
  dbus_g_type_struct_set (&room,
      0, handle,
      1, "org.freedesktop.Telepathy.Channel.Type.Text",
      2, keys,
      G_MAXUINT);
  g_ptr_array_add (priv->rooms, g_value_get_boxed (&room));
  g_hash_table_destroy (keys);

  DEBUG ("add room %s", room_name);
}

void
salut_roomlist_channel_remove_room (SalutRoomlistChannel *self,
                                    const gchar *room_name)
{
  SalutRoomlistChannelPrivate *priv =
    SALUT_ROOMLIST_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *base_connection = (TpBaseConnection *) (priv->connection);
  TpHandleRepoIface *room_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_ROOM);
  TpHandle handle;
  guint i;

  handle = tp_handle_lookup (room_repo, room_name, NULL, NULL);
  if (handle == 0)
    return;

  for (i = 0; i < priv->rooms->len; i++)
    {
      GValue room = {0,};
      gpointer boxed;
      TpHandle h;

      boxed = g_ptr_array_index (priv->rooms, i);
      g_value_init (&room, SALUT_TP_TYPE_ROOM_STRUCT);
      g_value_set_static_boxed (&room, boxed);
      dbus_g_type_struct_get (&room,
          0, &h,
          G_MAXUINT);

      if (handle == h)
        {
          g_boxed_free (SALUT_TP_TYPE_ROOM_STRUCT, boxed);
          g_ptr_array_remove_index_fast (priv->rooms, i);
          tp_handle_unref (room_repo, handle);
          DEBUG ("remove %s", room_name);
          break;
        }
    }
}

/************************* D-Bus Method definitions **************************/

/**
 * salut_roomlist_channel_close
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_roomlist_channel_close (TpSvcChannel *iface,
                              DBusGMethodInvocation *context)
{
  SalutRoomlistChannel *self = SALUT_ROOMLIST_CHANNEL (iface);
  SalutRoomlistChannelPrivate *priv =
    SALUT_ROOMLIST_CHANNEL_GET_PRIVATE (self);
  g_assert (SALUT_IS_ROOMLIST_CHANNEL (self));

  DEBUG ("called on %p", self);

  tp_svc_channel_emit_closed (iface);
  priv->closed = TRUE;

  tp_svc_channel_return_from_close (context);
}


/**
 * salut_roomlist_channel_get_channel_type
 *
 * Implements D-Bus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_roomlist_channel_get_channel_type (TpSvcChannel *iface,
                                         DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      TP_IFACE_CHANNEL_TYPE_ROOM_LIST);
}


/**
 * salut_roomlist_channel_get_handle
 *
 * Implements D-Bus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_roomlist_channel_get_handle (TpSvcChannel *self,
                                   DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_handle (context, 0, 0);
}


/**
 * salut_roomlist_channel_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_roomlist_channel_get_interfaces (TpSvcChannel *iface,
                                       DBusGMethodInvocation *context)
{
  const char *interfaces[] = { NULL };

  tp_svc_channel_return_from_get_interfaces (context, interfaces);
}


/**
 * salut_roomlist_channel_get_listing_rooms
 *
 * Implements D-Bus method GetListingRooms
 * on interface org.freedesktop.Telepathy.Channel.Type.RoomList
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_roomlist_channel_get_listing_rooms (TpSvcChannelTypeRoomList *iface,
                                          DBusGMethodInvocation *context)
{
  SalutRoomlistChannel *self = SALUT_ROOMLIST_CHANNEL (iface);
  SalutRoomlistChannelPrivate *priv;

  g_assert (SALUT_IS_ROOMLIST_CHANNEL (self));

  priv = SALUT_ROOMLIST_CHANNEL_GET_PRIVATE (self);
  tp_svc_channel_type_room_list_return_from_get_listing_rooms (
      context, FALSE);
}


/**
 * salut_roomlist_channel_list_rooms
 *
 * Implements D-Bus method ListRooms
 * on interface org.freedesktop.Telepathy.Channel.Type.RoomList
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_roomlist_channel_list_rooms (TpSvcChannelTypeRoomList *iface,
                                   DBusGMethodInvocation *context)
{
  SalutRoomlistChannel *self = SALUT_ROOMLIST_CHANNEL (iface);
  SalutRoomlistChannelPrivate *priv;

  g_assert (SALUT_IS_ROOMLIST_CHANNEL (self));

  priv = SALUT_ROOMLIST_CHANNEL_GET_PRIVATE (self);

  tp_svc_channel_type_room_list_emit_listing_rooms (iface, TRUE);
  tp_svc_channel_type_room_list_emit_got_rooms (iface, priv->rooms);
  tp_svc_channel_type_room_list_emit_listing_rooms (iface, FALSE);

  tp_svc_channel_type_room_list_return_from_list_rooms (context);
}

/**
 * salut_roomlist_channel_stop_listing
 *
 * Implements D-Bus method StopListing
 * on interface org.freedesktop.Telepathy.Channel.Type.RoomList
 */
static void
salut_roomlist_channel_stop_listing (TpSvcChannelTypeRoomList *iface,
                                     DBusGMethodInvocation *context)
{
  tp_svc_channel_type_room_list_return_from_stop_listing (context);
}

static void
channel_iface_init (gpointer g_iface,
                    gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, salut_roomlist_channel_##x)
  IMPLEMENT(close);
  IMPLEMENT(get_channel_type);
  IMPLEMENT(get_handle);
  IMPLEMENT(get_interfaces);
#undef IMPLEMENT
}

static void
roomlist_iface_init (gpointer g_iface,
                     gpointer iface_data)
{
  TpSvcChannelTypeRoomListClass *klass =
    (TpSvcChannelTypeRoomListClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_type_room_list_implement_##x (\
    klass, salut_roomlist_channel_##x)
  IMPLEMENT(get_listing_rooms);
  IMPLEMENT(list_rooms);
  IMPLEMENT(stop_listing);
#undef IMPLEMENT
}
