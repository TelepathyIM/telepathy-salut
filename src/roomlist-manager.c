/*
 * roomlist-manager.c - Source for SalutRoomlistManager
 * Copyright (C) 2008 Collabora Ltd.
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
#include "roomlist-manager.h"

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef G_OS_UNIX
#include <arpa/inet.h>
#endif

#include <wocky/wocky.h>

#include <gibber/gibber-muc-connection.h>

#include <salut/caps-channel-manager.h>

#include "roomlist-channel.h"
#include "contact-manager.h"
#include "muc-manager.h"
#include "roomlist-channel.h"
#include "discovery-client.h"

#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/base-channel.h>

#define DEBUG_FLAG DEBUG_MUC
#include "debug.h"

static void salut_roomlist_manager_iface_init (gpointer g_iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE(SalutRoomlistManager, salut_roomlist_manager,
                        G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
                                        salut_roomlist_manager_iface_init);
                        G_IMPLEMENT_INTERFACE (
                          GABBLE_TYPE_CAPS_CHANNEL_MANAGER, NULL))

/* properties */
enum {
  PROP_CONNECTION = 1,
  LAST_PROP
};

/* private structure */
typedef struct _SalutRoomlistManagerPrivate SalutRoomlistManagerPrivate;

struct _SalutRoomlistManagerPrivate
{
  SalutConnection *connection;
  gulong status_changed_id;

  GSList *roomlist_channels;

  /* Map from channels to the request-tokens of requests that they will
   * satisfy when they're ready.
   * Borrowed TpExportableChannel => GSList of gpointer */
  GHashTable *queued_requests;

  gboolean dispose_has_run;
};

#define SALUT_ROOMLIST_MANAGER_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_ROOMLIST_MANAGER, \
                                SalutRoomlistManagerPrivate))

static void
salut_roomlist_manager_init (SalutRoomlistManager *obj)
{
}

static void
salut_roomlist_manager_get_property (GObject *object,
    guint property_id, GValue *value, GParamSpec *pspec)
{
  SalutRoomlistManager *self = SALUT_ROOMLIST_MANAGER (object);
  SalutRoomlistManagerPrivate *priv = SALUT_ROOMLIST_MANAGER_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->connection);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_roomlist_manager_set_property (GObject *object,
                                guint property_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
  SalutRoomlistManager *self = SALUT_ROOMLIST_MANAGER (object);
  SalutRoomlistManagerPrivate *priv = SALUT_ROOMLIST_MANAGER_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->connection = g_value_get_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_roomlist_manager_close_all (SalutRoomlistManager *self)
{
  SalutRoomlistManagerPrivate *priv = SALUT_ROOMLIST_MANAGER_GET_PRIVATE (self);

  DEBUG ("closing channels");

  if (priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (priv->connection, priv->status_changed_id);
      priv->status_changed_id = 0;
    }

  if (priv->roomlist_channels != NULL)
    {
      GSList *l = priv->roomlist_channels;
      priv->roomlist_channels = NULL;
      g_slist_foreach (l, (GFunc) g_object_unref, NULL);
      g_slist_free (l);
    }
}

static void
connection_status_changed_cb (SalutConnection *conn,
                              guint status,
                              guint reason,
                              SalutRoomlistManager *self)
{
  switch (status)
    {
    case TP_CONNECTION_STATUS_DISCONNECTED:
      salut_roomlist_manager_close_all (self);
      break;
    }
}

static GObject *
salut_roomlist_manager_constructor (GType type,
                               guint n_props,
                               GObjectConstructParam *props)
{
  GObject *obj;
  SalutRoomlistManagerPrivate *priv;

  obj = G_OBJECT_CLASS (salut_roomlist_manager_parent_class)->
    constructor (type, n_props, props);

  priv = SALUT_ROOMLIST_MANAGER_GET_PRIVATE (obj);

  priv->status_changed_id = g_signal_connect (priv->connection,
      "status-changed", (GCallback) connection_status_changed_cb, obj);

  return obj;
}

static void salut_roomlist_manager_dispose (GObject *object);
static void salut_roomlist_manager_finalize (GObject *object);

static void
salut_roomlist_manager_class_init (
    SalutRoomlistManagerClass *salut_roomlist_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_roomlist_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_roomlist_manager_class,
                              sizeof (SalutRoomlistManagerPrivate));

  object_class->get_property = salut_roomlist_manager_get_property;
  object_class->set_property = salut_roomlist_manager_set_property;

  object_class->constructor = salut_roomlist_manager_constructor;
  object_class->dispose = salut_roomlist_manager_dispose;
  object_class->finalize = salut_roomlist_manager_finalize;

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "The Salut Connection associated with this roomlist manager",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION,
      param_spec);
}

void
salut_roomlist_manager_dispose (GObject *object)
{
  SalutRoomlistManager *self = SALUT_ROOMLIST_MANAGER (object);
  SalutRoomlistManagerPrivate *priv = SALUT_ROOMLIST_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  salut_roomlist_manager_close_all (self);

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_roomlist_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_roomlist_manager_parent_class)->dispose (object);
}

void
salut_roomlist_manager_finalize (GObject *object)
{
  /*SalutRoomlistManager *self = SALUT_ROOMLIST_MANAGER (object);*/
  /*SalutRoomlistManagerPrivate *priv = SALUT_ROOMLIST_MANAGER_GET_PRIVATE (self);*/

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (salut_roomlist_manager_parent_class)->finalize (object);
}

/* Channel Factory interface */

struct _ForeachData
{
  TpExportableChannelFunc foreach;
  gpointer user_data;
};


static void
salut_roomlist_manager_foreach_one_list (TpChannelManager *chan,
                                         gpointer user_data)
{
  struct _ForeachData *data = (struct _ForeachData *) user_data;
  TpExportableChannel *channel = TP_EXPORTABLE_CHANNEL (chan);

  data->foreach (channel, data->user_data);
}


static void
salut_roomlist_manager_foreach_channel (TpChannelManager *iface,
                                   TpExportableChannelFunc foreach,
                                   gpointer user_data)
{
  SalutRoomlistManager *fac = SALUT_ROOMLIST_MANAGER (iface);
  SalutRoomlistManagerPrivate *priv = SALUT_ROOMLIST_MANAGER_GET_PRIVATE (fac);
  struct _ForeachData data;

  data.user_data = user_data;
  data.foreach = foreach;

  g_slist_foreach (priv->roomlist_channels,
      (GFunc) salut_roomlist_manager_foreach_one_list, &data);
}

static const gchar * const roomlist_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};

static const gchar * const roomlist_channel_allowed_properties[] = {
    NULL
};


static void
salut_roomlist_manager_type_foreach_channel_class (GType type,
    TpChannelManagerTypeChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);
  GValue *channel_type_value, *handle_type_value;

  channel_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (channel_type_value,
      TP_IFACE_CHANNEL_TYPE_ROOM_LIST);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType",
      channel_type_value);

  handle_type_value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (handle_type_value, TP_HANDLE_TYPE_NONE);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetHandleType",
      handle_type_value);

  func (type, table, roomlist_channel_allowed_properties,
      user_data);

  g_hash_table_unref (table);
}


static void
roomlist_channel_closed_cb (SalutRoomlistChannel *channel,
                            gpointer user_data)
{
  SalutRoomlistManager *self = SALUT_ROOMLIST_MANAGER (user_data);
  SalutRoomlistManagerPrivate *priv =
    SALUT_ROOMLIST_MANAGER_GET_PRIVATE (self);

  tp_channel_manager_emit_channel_closed_for_object (self,
      TP_EXPORTABLE_CHANNEL (channel));

  if (priv->roomlist_channels != NULL)
    {
      priv->roomlist_channels = g_slist_remove (priv->roomlist_channels,
          channel);
      g_object_unref (channel);
    }
}


static SalutRoomlistChannel *
make_roomlist_channel (SalutRoomlistManager *self)
{
  SalutRoomlistManagerPrivate *priv =
    SALUT_ROOMLIST_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *conn = TP_BASE_CONNECTION (priv->connection);
  SalutRoomlistChannel *roomlist_channel;
  gchar *object_path;
  static guint cpt = 0;
  GSList *rooms, *l;

  /* FIXME: this is not optimal as all the Connection will share the same cpt
   * and we could have problem if we overflow the guint. */
  object_path = g_strdup_printf ("%s/RoomlistChannel%u",
      conn->object_path, cpt++);

  roomlist_channel = salut_roomlist_channel_new (priv->connection,
      object_path);

  tp_base_channel_register (TP_BASE_CHANNEL (roomlist_channel));

  rooms = SALUT_ROOMLIST_MANAGER_GET_CLASS (self)->get_rooms (self);
  for (l = rooms; l != NULL; l = g_slist_next (l))
    {
      const gchar *room_name = l->data;

      salut_roomlist_channel_add_room (roomlist_channel, room_name);
    }

  priv->roomlist_channels = g_slist_prepend (priv->roomlist_channels,
      g_object_ref (roomlist_channel));

  g_signal_connect (roomlist_channel, "closed",
      (GCallback) roomlist_channel_closed_cb, self);

  g_free (object_path);
  return roomlist_channel;
}


static gboolean
salut_roomlist_manager_request (TpChannelManager *manager,
                                gpointer request_token,
                                GHashTable *request_properties,
                                gboolean require_new)
{
  SalutRoomlistManager *self = SALUT_ROOMLIST_MANAGER (manager);
  SalutRoomlistManagerPrivate *priv =
    SALUT_ROOMLIST_MANAGER_GET_PRIVATE (self);
  SalutRoomlistChannel *roomlist_channel = NULL;
  GError *error = NULL;
  GSList *request_tokens;

  if (tp_strdiff (tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL ".ChannelType"),
        TP_IFACE_CHANNEL_TYPE_ROOM_LIST))
    return FALSE;

  if (tp_asv_get_uint32 (request_properties,
       TP_IFACE_CHANNEL ".TargetHandleType", NULL) != 0)
    {
      g_set_error (&error, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
          "RoomList channels can't have a target handle");
      goto error;
    }

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          roomlist_channel_fixed_properties,
          roomlist_channel_allowed_properties,
          &error))
    goto error;

  if (!require_new && priv->roomlist_channels != NULL)
    {
      /* reuse the first channel */
      roomlist_channel = priv->roomlist_channels->data;

      tp_channel_manager_emit_request_already_satisfied (self,
          request_token, TP_EXPORTABLE_CHANNEL (roomlist_channel));
      return TRUE;
    }

  roomlist_channel = make_roomlist_channel (self);

  g_signal_connect (roomlist_channel, "closed",
      (GCallback) roomlist_channel_closed_cb, self);
  priv->roomlist_channels = g_slist_prepend (priv->roomlist_channels,
      roomlist_channel);

  request_tokens = g_slist_prepend (NULL, request_token);
  tp_channel_manager_emit_new_channel (self,
      TP_EXPORTABLE_CHANNEL (roomlist_channel), request_tokens);
  g_slist_free (request_tokens);

  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}


static gboolean
salut_roomlist_manager_create_channel (TpChannelManager *manager,
                                   gpointer request_token,
                                   GHashTable *request_properties)
{
  return salut_roomlist_manager_request (manager, request_token,
      request_properties, TRUE);
}


static gboolean
salut_roomlist_manager_request_channel (TpChannelManager *manager,
                                    gpointer request_token,
                                    GHashTable *request_properties)
{
  return salut_roomlist_manager_request (manager, request_token,
      request_properties, FALSE);
}


static gboolean
salut_roomlist_manager_ensure_channel (TpChannelManager *manager,
                                    gpointer request_token,
                                    GHashTable *request_properties)
{
  return salut_roomlist_manager_request (manager, request_token,
      request_properties, FALSE);
}


static void salut_roomlist_manager_iface_init (gpointer g_iface,
                                          gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = salut_roomlist_manager_foreach_channel;
  iface->type_foreach_channel_class =
    salut_roomlist_manager_type_foreach_channel_class;
  iface->request_channel = salut_roomlist_manager_request_channel;
  iface->create_channel = salut_roomlist_manager_create_channel;
  iface->ensure_channel = salut_roomlist_manager_ensure_channel;
}

/* public functions */

gboolean
salut_roomlist_manager_start (SalutRoomlistManager *self,
                              GError **error)
{
  return SALUT_ROOMLIST_MANAGER_GET_CLASS (self)->start (self, error);
}

static void
add_room_foreach (SalutRoomlistChannel *roomlist_channel,
                  const gchar *room)
{
  salut_roomlist_channel_add_room (roomlist_channel, room);
}

void
salut_roomlist_manager_room_discovered (SalutRoomlistManager *self,
                                  const gchar *room)
{
  SalutRoomlistManagerPrivate *priv =
    SALUT_ROOMLIST_MANAGER_GET_PRIVATE (self);

  g_slist_foreach (priv->roomlist_channels, (GFunc) add_room_foreach,
      (gchar *) room);
}

static void
remove_room_foreach (SalutRoomlistChannel *roomlist_channel,
                     const gchar *room)
{
  salut_roomlist_channel_remove_room (roomlist_channel, room);
}

void
salut_roomlist_manager_room_removed (SalutRoomlistManager *self,
                                     const gchar *room)
{
  SalutRoomlistManagerPrivate *priv = SALUT_ROOMLIST_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION (priv->connection);
  TpHandleRepoIface *room_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_ROOM);
  TpHandle handle;
  SalutMucChannel *muc;
  SalutMucManager *muc_manager;

  g_slist_foreach (priv->roomlist_channels, (GFunc) remove_room_foreach,
      (gchar *) room);

  /* Do we have to re-announce this room ? */
  handle = tp_handle_lookup (room_repo, room, NULL, NULL);
  if (handle == 0)
    return;

  g_object_get (priv->connection, "muc-manager", &muc_manager, NULL);
  g_assert (muc_manager != NULL);

  muc = salut_muc_manager_get_text_channel (muc_manager, handle);
  if (muc == NULL)
    return;

  DEBUG ("We know this room %s. Try to re-announce it", room);
  salut_muc_channel_publish_service (muc);
  g_object_unref (muc);
}
