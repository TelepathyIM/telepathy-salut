/*
 * roomlist-channel.c - Source for SalutRoomlistChannel
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

#include "config.h"
#include "roomlist-channel.h"

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "connection.h"

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#define DEBUG_FLAG DEBUG_ROOMLIST
#include "debug.h"

static void roomlist_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (SalutRoomlistChannel, salut_roomlist_channel,
    TP_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_ROOM_LIST1,
      roomlist_iface_init);
    );

/* properties */
enum
{
  PROP_CONFERENCE_SERVER = 1,
  LAST_PROPERTY
};

/* private structure */
struct _SalutRoomlistChannelPrivate
{
  GPtrArray *rooms;

  gboolean dispose_has_run;
};

static void
salut_roomlist_channel_init (SalutRoomlistChannel *self)
{
  GDBusObjectSkeleton *skel = G_DBUS_OBJECT_SKELETON (self);
  GDBusInterfaceSkeleton *iface;

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_ROOMLIST_CHANNEL, SalutRoomlistChannelPrivate);

  self->priv->rooms = g_ptr_array_new ();

  iface = tp_svc_interface_skeleton_new (skel,
      TP_TYPE_SVC_CHANNEL_TYPE_ROOM_LIST1);
  g_dbus_object_skeleton_add_interface (skel, iface);
  g_object_unref (iface);
}

static void
salut_roomlist_channel_get_property (GObject *object,
                                     guint property_id,
                                     GValue *value,
                                     GParamSpec *pspec)
{
  switch (property_id) {
    case PROP_CONFERENCE_SERVER:
      /* Salut does not use a server, so this string is always empty */
      g_value_set_string (value, "");
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void salut_roomlist_channel_dispose (GObject *object);
static void salut_roomlist_channel_finalize (GObject *object);

static void
salut_roomlist_channel_fill_immutable_properties (TpBaseChannel *chan,
    GHashTable *properties)
{
  TpBaseChannelClass *cls = TP_BASE_CHANNEL_CLASS (
      salut_roomlist_channel_parent_class);

  cls->fill_immutable_properties (chan, properties);

  tp_dbus_properties_mixin_fill_properties_hash (
      G_OBJECT (chan), properties,
      TP_IFACE_CHANNEL_TYPE_ROOM_LIST1, "Server",
      NULL);
}

static void
salut_roomlist_channel_class_init (
    SalutRoomlistChannelClass *salut_roomlist_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_roomlist_channel_class);
  TpBaseChannelClass *base_class = TP_BASE_CHANNEL_CLASS (salut_roomlist_channel_class);
  GParamSpec *param_spec;
  static TpDBusPropertiesMixinPropImpl roomlist_props[] = {
      { "Server", "conference-server", NULL },
      { NULL }
  };

  g_type_class_add_private (salut_roomlist_channel_class,
      sizeof (SalutRoomlistChannelPrivate));

  object_class->get_property = salut_roomlist_channel_get_property;
  object_class->dispose = salut_roomlist_channel_dispose;
  object_class->finalize = salut_roomlist_channel_finalize;

  base_class->channel_type = TP_IFACE_CHANNEL_TYPE_ROOM_LIST1;
  base_class->target_entity_type = TP_ENTITY_TYPE_NONE;
  base_class->fill_immutable_properties =
    salut_roomlist_channel_fill_immutable_properties;
  base_class->close = tp_base_channel_destroyed;

  param_spec = g_param_spec_string ("conference-server",
      "Name of conference server to use",
      "Name of conference server to use, which is an empty string for Salut",
      "",
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONFERENCE_SERVER,
      param_spec);

  tp_dbus_properties_mixin_implement_interface (object_class,
      TP_IFACE_QUARK_CHANNEL_TYPE_ROOM_LIST1,
      tp_dbus_properties_mixin_getter_gobject_properties, NULL,
      roomlist_props);
}

static void
rooms_free (SalutRoomlistChannel *self)
{
  SalutRoomlistChannelPrivate *priv = self->priv;
  guint i;

  g_assert (priv->rooms != NULL);

  for (i = 0; i < priv->rooms->len; i++)
    {
      gpointer boxed;

      boxed = g_ptr_array_index (priv->rooms, i);
      g_boxed_free (TP_STRUCT_TYPE_ROOM_INFO, boxed);
    }

  g_ptr_array_unref (priv->rooms);
  priv->rooms = NULL;
}

static void
salut_roomlist_channel_dispose (GObject *object)
{
  SalutRoomlistChannel *self = SALUT_ROOMLIST_CHANNEL (object);
  SalutRoomlistChannelPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (G_OBJECT_CLASS (salut_roomlist_channel_parent_class)->dispose)
    G_OBJECT_CLASS (salut_roomlist_channel_parent_class)->dispose (object);
}

static void
salut_roomlist_channel_finalize (GObject *object)
{
  SalutRoomlistChannel *self = SALUT_ROOMLIST_CHANNEL (object);
  SalutRoomlistChannelPrivate *priv = self->priv;

  /* free any data held directly by the object here */

  if (priv->rooms != NULL)
    rooms_free (self);

  if (G_OBJECT_CLASS (salut_roomlist_channel_parent_class)->finalize)
    G_OBJECT_CLASS (salut_roomlist_channel_parent_class)->finalize (object);
}

SalutRoomlistChannel *
salut_roomlist_channel_new (SalutConnection *conn,
                            const gchar *object_path)
{
  TpHandle initiator;

  g_return_val_if_fail (SALUT_IS_CONNECTION (conn), NULL);
  g_return_val_if_fail (object_path != NULL, NULL);

  initiator = tp_base_connection_get_self_handle ((TpBaseConnection *) conn);

  return SALUT_ROOMLIST_CHANNEL (
      g_object_new (SALUT_TYPE_ROOMLIST_CHANNEL,
          "connection", conn,
          "object-path", object_path,
          "initiator-handle", initiator,
          "requested", TRUE,
          NULL));
}

void
salut_roomlist_channel_add_room (SalutRoomlistChannel *self,
                                 const gchar *room_name)
{
  SalutRoomlistChannelPrivate *priv = self->priv;
  TpBaseConnection *base_connection = tp_base_channel_get_connection (
      TP_BASE_CHANNEL (self));
  TpHandleRepoIface *room_repo =
      tp_base_connection_get_handles (base_connection, TP_ENTITY_TYPE_ROOM);
  GValue room = {0,};
  TpHandle handle;
  GHashTable *keys;
  GValue handle_name = {0,};

  handle = tp_handle_ensure (room_repo, room_name, NULL, NULL);
  if (handle == 0)
    return;

  keys = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);

  /* handle-name */
  g_value_init (&handle_name, G_TYPE_STRING);
  g_value_take_string (&handle_name, (gchar *) room_name);
  g_hash_table_insert (keys, "handle-name", &handle_name);

  g_value_init (&room, TP_STRUCT_TYPE_ROOM_INFO);
  g_value_take_boxed (&room,
      dbus_g_type_specialized_construct (TP_STRUCT_TYPE_ROOM_INFO));
  dbus_g_type_struct_set (&room,
      0, handle,
      1, "im.telepathy.v1.Channel.Type.Text",
      2, keys,
      G_MAXUINT);
  g_ptr_array_add (priv->rooms, g_value_get_boxed (&room));
  g_hash_table_unref (keys);

  DEBUG ("add room %s", room_name);
}

void
salut_roomlist_channel_remove_room (SalutRoomlistChannel *self,
                                    const gchar *room_name)
{
  SalutRoomlistChannelPrivate *priv = self->priv;
  TpBaseConnection *base_connection = tp_base_channel_get_connection (
      TP_BASE_CHANNEL (self));
  TpHandleRepoIface *room_repo =
      tp_base_connection_get_handles (base_connection, TP_ENTITY_TYPE_ROOM);
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
      g_value_init (&room, TP_STRUCT_TYPE_ROOM_INFO);
      g_value_set_static_boxed (&room, boxed);
      dbus_g_type_struct_get (&room,
          0, &h,
          G_MAXUINT);

      if (handle == h)
        {
          g_boxed_free (TP_STRUCT_TYPE_ROOM_INFO, boxed);
          g_ptr_array_remove_index_fast (priv->rooms, i);
          DEBUG ("remove %s", room_name);
          break;
        }
    }
}

/**
 * salut_roomlist_channel_get_listing_rooms
 *
 * Implements D-Bus method GetListingRooms
 * on interface im.telepathy.v1.Channel.Type.RoomList
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_roomlist_channel_get_listing_rooms (TpSvcChannelTypeRoomList1 *iface,
                                          GDBusMethodInvocation *context)
{
  SalutRoomlistChannel *self = SALUT_ROOMLIST_CHANNEL (iface);

  g_assert (SALUT_IS_ROOMLIST_CHANNEL (self));

  tp_svc_channel_type_room_list1_return_from_get_listing_rooms (
      context, FALSE);
}


/**
 * salut_roomlist_channel_list_rooms
 *
 * Implements D-Bus method ListRooms
 * on interface im.telepathy.v1.Channel.Type.RoomList
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_roomlist_channel_list_rooms (TpSvcChannelTypeRoomList1 *iface,
                                   GDBusMethodInvocation *context)
{
  SalutRoomlistChannel *self = SALUT_ROOMLIST_CHANNEL (iface);
  SalutRoomlistChannelPrivate *priv = self->priv;

  tp_svc_channel_type_room_list1_emit_listing_rooms (iface, TRUE);
  tp_svc_channel_type_room_list1_emit_got_rooms (iface, priv->rooms);
  tp_svc_channel_type_room_list1_emit_listing_rooms (iface, FALSE);

  tp_svc_channel_type_room_list1_return_from_list_rooms (context);
}

/**
 * salut_roomlist_channel_stop_listing
 *
 * Implements D-Bus method StopListing
 * on interface im.telepathy.v1.Channel.Type.RoomList
 */
static void
salut_roomlist_channel_stop_listing (TpSvcChannelTypeRoomList1 *iface,
                                     GDBusMethodInvocation *context)
{
  tp_svc_channel_type_room_list1_return_from_stop_listing (context);
}

static void
roomlist_iface_init (gpointer g_iface,
                     gpointer iface_data)
{
  TpSvcChannelTypeRoomList1Class *klass = g_iface;

#define IMPLEMENT(x) tp_svc_channel_type_room_list1_implement_##x (\
    klass, salut_roomlist_channel_##x)
  IMPLEMENT(get_listing_rooms);
  IMPLEMENT(list_rooms);
  IMPLEMENT(stop_listing);
#undef IMPLEMENT
}
