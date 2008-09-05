/*
 * salut-muc-manager.c - Source for SalutMucManager
 * Copyright (C) 2006 Collabora Ltd.
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
#include <string.h>
#include <arpa/inet.h>

#include "salut-muc-manager.h"

#include <gibber/gibber-muc-connection.h>
#include <gibber/gibber-namespaces.h>
#include <gibber/gibber-xmpp-error.h>

#include "salut-muc-channel.h"
#include "salut-contact-manager.h"
#include "salut-tubes-channel.h"
#include "salut-roomlist-channel.h"
#include "salut-xmpp-connection-manager.h"
#include "salut-discovery-client.h"

#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG DEBUG_MUC
#include "debug.h"

static gboolean
invite_stanza_filter (SalutXmppConnectionManager *mgr,
    GibberXmppConnection *conn, GibberXmppStanza *stanza,
    SalutContact *contact, gpointer user_data);

static void
invite_stanza_callback (SalutXmppConnectionManager *mgr,
    GibberXmppConnection *conn, GibberXmppStanza *stanza,
    SalutContact *contact, gpointer user_data);


static void salut_muc_manager_factory_iface_init (gpointer g_iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE(SalutMucManager, salut_muc_manager,
                        G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE,
                                        salut_muc_manager_factory_iface_init));

/* properties */
enum {
  PROP_CONNECTION = 1,
  PROP_XCM,
  LAST_PROP
};

/* private structure */
typedef struct _SalutMucManagerPrivate SalutMucManagerPrivate;

struct _SalutMucManagerPrivate
{
  SalutConnection *connection;
  SalutXmppConnectionManager *xmpp_connection_manager;

  /* GUINT_TO_POINTER (room_handle) => (SalutMucChannel *) */
  GHashTable *text_channels;
#ifdef ENABLE_DBUS_TUBES
   /* GUINT_TO_POINTER(room_handle) => (SalutTubesChannel *) */
  GHashTable *tubes_channels;
#endif
  GSList *roomlist_channels;

  gboolean dispose_has_run;
};

#define SALUT_MUC_MANAGER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_MUC_MANAGER, SalutMucManagerPrivate))

static void
salut_muc_manager_init (SalutMucManager *obj)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (obj);
  priv->connection = NULL;

  /* allocate any data required by the object here */
  priv->text_channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                               NULL, g_object_unref);

#ifdef ENABLE_DBUS_TUBES
  priv->tubes_channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);
#endif

  priv->roomlist_channels = NULL;
}

static void
salut_muc_manager_get_property (GObject *object,
    guint property_id, GValue *value, GParamSpec *pspec)
{
  SalutMucManager *self = SALUT_MUC_MANAGER (object);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->connection);
        break;
      case PROP_XCM:
        g_value_set_object (value, priv->xmpp_connection_manager);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_muc_manager_set_property (GObject *object,
                                guint property_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
  SalutMucManager *self = SALUT_MUC_MANAGER (object);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->connection = g_value_get_object (value);
        break;
      case PROP_XCM:
        priv->xmpp_connection_manager = g_value_get_object (value);
        g_object_ref (priv->xmpp_connection_manager);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
salut_muc_manager_constructor (GType type,
                               guint n_props,
                               GObjectConstructParam *props)
{
  GObject *obj;
  SalutMucManagerPrivate *priv;

  obj = G_OBJECT_CLASS (salut_muc_manager_parent_class)->
    constructor (type, n_props, props);

  priv = SALUT_MUC_MANAGER_GET_PRIVATE (obj);

  salut_xmpp_connection_manager_add_stanza_filter (
      priv->xmpp_connection_manager, NULL,
      invite_stanza_filter, invite_stanza_callback, obj);

  return obj;
}

static void salut_muc_manager_dispose (GObject *object);
static void salut_muc_manager_finalize (GObject *object);

static void
salut_muc_manager_class_init (SalutMucManagerClass *salut_muc_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_muc_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_muc_manager_class,
                              sizeof (SalutMucManagerPrivate));

  object_class->get_property = salut_muc_manager_get_property;
  object_class->set_property = salut_muc_manager_set_property;

  object_class->constructor = salut_muc_manager_constructor;
  object_class->dispose = salut_muc_manager_dispose;
  object_class->finalize = salut_muc_manager_finalize;

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "The Salut Connection associated with this muc manager",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION,
      param_spec);

  param_spec = g_param_spec_object (
      "xmpp-connection-manager",
      "SalutXmppConnectionManager object",
      "The Salut XMPP Connection Manager associated with this muc "
      "manager",
      SALUT_TYPE_XMPP_CONNECTION_MANAGER,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_XCM,
      param_spec);
}

void
salut_muc_manager_dispose (GObject *object)
{
  SalutMucManager *self = SALUT_MUC_MANAGER (object);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  salut_xmpp_connection_manager_remove_stanza_filter (
      priv->xmpp_connection_manager, NULL,
      invite_stanza_filter, invite_stanza_callback, self);

  tp_channel_factory_iface_close_all (TP_CHANNEL_FACTORY_IFACE (object));
  g_assert (priv->text_channels == NULL);
#ifdef ENABLE_DBUS_TUBES
  g_assert (priv->tubes_channels == NULL);
#endif

  if (priv->xmpp_connection_manager != NULL)
    {
      g_object_unref (priv->xmpp_connection_manager);
      priv->xmpp_connection_manager = NULL;
    }

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_muc_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_muc_manager_parent_class)->dispose (object);
}

void
salut_muc_manager_finalize (GObject *object)
{
  /*SalutMucManager *self = SALUT_MUC_MANAGER (object);*/
  /*SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);*/

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (salut_muc_manager_parent_class)->finalize (object);
}

static void
closed_channel_foreach (TpHandle handle,
                        SalutMucChannel *channel,
                        SalutMucManager *self)
{
  salut_muc_channel_emit_closed (channel);
}

/* Channel Factory interface */

static void
salut_muc_manager_factory_iface_close_all (TpChannelFactoryIface *iface) {
  SalutMucManager *mgr = SALUT_MUC_MANAGER (iface);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (mgr);

  if (priv->text_channels)
    {
      GHashTable *tmp = priv->text_channels;
      priv->text_channels = NULL;
      g_hash_table_foreach (tmp, (GHFunc) closed_channel_foreach, mgr);
      g_hash_table_destroy (tmp);
  }

#ifdef ENABLE_DBUS_TUBES
  if (priv->tubes_channels != NULL)
    {
      GHashTable *tmp = priv->tubes_channels;
      priv->tubes_channels = NULL;
      g_hash_table_destroy (tmp);
    }
#endif

  if (priv->roomlist_channels != NULL)
    {
      GSList *l = priv->roomlist_channels;
      priv->roomlist_channels = NULL;
      g_slist_foreach (l, (GFunc) g_object_unref, NULL);
      g_slist_free (l);
    }
}

static void
salut_muc_manager_factory_iface_connecting (TpChannelFactoryIface *iface)
{
}

static void
salut_muc_manager_factory_iface_connected (TpChannelFactoryIface *iface)
{
}

static void
salut_muc_manager_factory_iface_disconnected (TpChannelFactoryIface *iface)
{
  /* FIMXE close all channels ? */
}

struct foreach_data {
  TpChannelFunc func;
  gpointer data;
};

static void
salut_muc_manager_iface_foreach_one (gpointer key,
                                     gpointer value,
                                     gpointer data)
{
  TpChannelIface *chan = TP_CHANNEL_IFACE (value);
  struct foreach_data *f = (struct foreach_data *) data;

  f->func (chan, f->data);
}

static void
salut_muc_manager_iface_foreach_one_list (TpChannelIface *chan,
                                          gpointer data)
{
  struct foreach_data *f = (struct foreach_data *) data;

  f->func (chan, f->data);
}

static void
salut_muc_manager_factory_iface_foreach (TpChannelFactoryIface *iface,
                                         TpChannelFunc func, gpointer data) {
  SalutMucManager *mgr = SALUT_MUC_MANAGER(iface);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE(mgr);
  struct foreach_data f;
  f.func = func;
  f.data = data;

  g_hash_table_foreach (priv->text_channels,
      salut_muc_manager_iface_foreach_one, &f);
#ifdef ENABLE_DBUS_TUBES
  g_hash_table_foreach (priv->tubes_channels,
      salut_muc_manager_iface_foreach_one, &f);
#endif

  g_slist_foreach (priv->roomlist_channels,
      (GFunc) salut_muc_manager_iface_foreach_one_list, &f);
}

static void
muc_channel_closed_cb (SalutMucChannel *chan,
                       gpointer user_data)
{
  SalutMucManager *self = SALUT_MUC_MANAGER (user_data);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  TpHandle handle;

  if (priv->text_channels)
    {
      g_object_get (chan, "handle", &handle, NULL);
      DEBUG ("Removing channel with handle %u", handle);

#ifdef ENABLE_DBUS_TUBES
      if (priv->tubes_channels != NULL)
        {
          SalutTubesChannel *tubes;

          tubes = g_hash_table_lookup (priv->tubes_channels,
              GUINT_TO_POINTER (handle));
          if (tubes != NULL)
            salut_tubes_channel_close (tubes);
        }
#endif

      g_hash_table_remove (priv->text_channels, GUINT_TO_POINTER (handle));
    }
}

#ifdef ENABLE_DBUS_TUBES
/**
 * tubes_channel_closed_cb:
 *
 * Signal callback for when a tubes channel is closed. Removes the references
 * that MucManager holds to them.
 */
static void
tubes_channel_closed_cb (SalutTubesChannel *chan, gpointer user_data)
{
  SalutMucManager *fac = SALUT_MUC_MANAGER (user_data);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (fac);
  TpHandle room_handle;

  if (priv->tubes_channels != NULL)
    {
      g_object_get (chan, "handle", &room_handle, NULL);

      DEBUG ("removing MUC tubes channel with handle %u", room_handle);

      g_hash_table_remove (priv->tubes_channels,
          GUINT_TO_POINTER (room_handle));

      /* The channel will probably reopen soon due to an incoming tube message,
       * but closing the corresponding text channel would be too astonishing */
    }
}
#endif


static GibberMucConnection *
_get_connection (SalutMucManager *mgr,
                 const gchar *protocol,
                 GHashTable *parameters,
                 GError **error)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (mgr);

  return gibber_muc_connection_new (priv->connection->name,
      protocol, parameters, error);
}


static SalutMucChannel *
salut_muc_manager_new_muc_channel (SalutMucManager *mgr,
                                   TpHandle handle,
                                   GibberMucConnection *connection,
                                   gboolean new_connection)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE(mgr);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION(priv->connection);
  TpHandleRepoIface *room_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_ROOM);
  SalutMucChannel *chan;
  const gchar *name;
  gchar *path = NULL;

  g_assert (g_hash_table_lookup (priv->text_channels,
        GUINT_TO_POINTER (handle)) == NULL);
  DEBUG ("Requested channel for handle: %u", handle);

  /* FIXME The name of the muc and the handle might need to be different at
   * some point.. E.g. if two rooms are called the same */
  name = tp_handle_inspect (room_repo, handle);
  path = g_strdup_printf ("%s/MucChannel/%u", base_connection->object_path,
      handle);

  chan = SALUT_MUC_MANAGER_GET_CLASS (mgr)->create_muc_channel (mgr,
      priv->connection, path, connection, handle, name, new_connection,
      priv->xmpp_connection_manager);
  g_free (path);

  g_signal_connect (chan, "closed", G_CALLBACK (muc_channel_closed_cb), mgr);
  tp_channel_factory_iface_emit_new_channel (mgr, TP_CHANNEL_IFACE (chan),
      NULL);

  g_hash_table_insert (priv->text_channels, GUINT_TO_POINTER (handle), chan);

  return chan;
}

#ifdef ENABLE_DBUS_TUBES
/**
 * new_tubes_channel:
 *
 * Creates the SalutTubesChannel object with the given parameters.
 */
static SalutTubesChannel *
new_tubes_channel (SalutMucManager *self,
                   TpHandle room,
                   SalutMucChannel *muc)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *conn = (TpBaseConnection *) priv->connection;
  SalutTubesChannel *chan;
  char *object_path;

  g_assert (g_hash_table_lookup (priv->tubes_channels,
        GUINT_TO_POINTER (room)) == NULL);

  object_path = g_strdup_printf ("%s/MucTubesChannel%u",
      conn->object_path, room);

  DEBUG ("creating new tubes chan, object path %s", object_path);

  chan = g_object_new (SALUT_TYPE_TUBES_CHANNEL,
      "connection", priv->connection,
      "object-path", object_path,
      "handle", room,
      "handle-type", TP_HANDLE_TYPE_ROOM,
      "muc", muc,
      NULL);

  g_signal_connect (chan, "closed", (GCallback) tubes_channel_closed_cb, self);
  tp_channel_factory_iface_emit_new_channel (self,
      TP_CHANNEL_IFACE (chan), NULL);

  g_hash_table_insert (priv->tubes_channels, GUINT_TO_POINTER (room), chan);

  g_free (object_path);

  return chan;
}
#endif

static SalutMucChannel *
salut_muc_manager_request_new_muc_channel (SalutMucManager *mgr,
                                           TpHandle handle,
                                           GError **error)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (mgr);
  TpBaseConnection *base_connection = (TpBaseConnection *) (priv->connection);
  TpHandleRepoIface *room_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_ROOM);
  GibberMucConnection *connection;
  SalutMucChannel *text_chan;
  GError *connection_error = NULL;
  const gchar *room_name;
  GHashTable *params = NULL;
  gchar *address;
  guint16 p;
  gboolean r;

  room_name = tp_handle_inspect (room_repo, handle);

  if (SALUT_MUC_MANAGER_GET_CLASS (mgr)->find_muc_address (mgr, room_name,
        &address, &p))
    {
      /* This MUC already exists on the network, so we reuse its
       * address */
      gchar *port = g_strdup_printf ("%u", p);

      params = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
          g_free);
      g_hash_table_insert (params, "address", address);
      g_hash_table_insert (params, "port", port);
      DEBUG ("found %s port %s for room %s", address, port,
          room_name);
    }
  else
    {
      DEBUG ("Didn't find address for room %s, let's generate one", room_name);
    }

  connection = _get_connection (mgr, NULL, params, &connection_error);

  if (params != NULL)
    g_hash_table_destroy (params);

  if (connection == NULL)
    {
      DEBUG ("get connection failed: %s", connection_error->message);
      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
          connection_error->message);
      g_error_free (connection_error);
      return NULL;
    }

  /* We requested the channel, so invite ourselves to it */
  if (!gibber_muc_connection_connect (connection, &connection_error))
    {
      DEBUG ("Connect failed: %s", connection_error->message);
      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
          connection_error->message);
      g_error_free (connection_error);
      g_object_unref (connection);
      return NULL;
    }
  DEBUG ("Connect succeeded");

  text_chan = salut_muc_manager_new_muc_channel (mgr, handle,
      connection, params == NULL);
  r = salut_muc_channel_invited (text_chan,
        base_connection->self_handle, NULL, NULL);
  /* Inviting ourselves to a connected channel should always
   * succeed */
  g_assert (r);

  return text_chan;
}

static void
roomlist_channel_closed_cb (SalutRoomlistChannel *chan,
                            gpointer data)
{
  SalutMucManager *self = SALUT_MUC_MANAGER (data);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);

  if (priv->roomlist_channels != NULL)
    {
      g_object_unref (chan);
      priv->roomlist_channels = g_slist_remove (priv->roomlist_channels, chan);
    }
}

static SalutRoomlistChannel *
make_roomlist_channel (SalutMucManager *self,
                       gpointer request)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *conn = (TpBaseConnection *) priv->connection;
  SalutRoomlistChannel *roomlist_channel;
  gchar *object_path;
  static guint cpt = 0;
  GSList *rooms, *l;

  object_path = g_strdup_printf ("%s/RoomlistChannel%u",
      conn->object_path, cpt++);

  roomlist_channel = salut_roomlist_channel_new (priv->connection,
      object_path);

  rooms = SALUT_MUC_MANAGER_GET_CLASS (self)->get_rooms (self);
  for (l = rooms; l != NULL; l = g_slist_next (l))
    {
      const gchar *room_name = l->data;

      salut_roomlist_channel_add_room (roomlist_channel, room_name);
    }

  priv->roomlist_channels = g_slist_prepend (priv->roomlist_channels,
      roomlist_channel);

  g_signal_connect (roomlist_channel, "closed",
      (GCallback) roomlist_channel_closed_cb, self);

  tp_channel_factory_iface_emit_new_channel (self,
      (TpChannelIface *) roomlist_channel, request);

  g_free (object_path);
  return roomlist_channel;
}

static SalutTubesChannel *
create_tubes_channel (SalutMucManager *self,
                      TpHandle handle,
                      GError **error)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  SalutMucChannel *text_chan;
  SalutTubesChannel *tubes_chan;

  text_chan = g_hash_table_lookup (priv->text_channels,
      GUINT_TO_POINTER (handle));

  if (text_chan == NULL)
    {
      DEBUG ("have to create the text channel before the tubes one");
      text_chan = salut_muc_manager_request_new_muc_channel (self,
          handle, error);
      if (text_chan == NULL)
        return NULL;
    }

  tubes_chan = new_tubes_channel (self, handle, text_chan);
  g_assert (tubes_chan != NULL);

  return tubes_chan;
}

static TpChannelFactoryRequestStatus
salut_muc_manager_factory_iface_request (TpChannelFactoryIface *iface,
                                         const gchar *chan_type,
                                         TpHandleType handle_type,
                                         guint handle,
                                         gpointer request,
                                         TpChannelIface **ret,
                                         GError **error)
{
  SalutMucManager *mgr = SALUT_MUC_MANAGER (iface);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (mgr);
  TpBaseConnection *base_connection = (TpBaseConnection *) (priv->connection);
  TpHandleRepoIface *room_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_ROOM);
  SalutMucChannel *text_chan;
  TpChannelFactoryRequestStatus status;

  DEBUG ("MUC request: ctype=%s htype=%u handle=%u", chan_type, handle_type,
      handle);

  if (!tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_ROOM_LIST))
    {
      SalutRoomlistChannel *roomlist_channel;

      roomlist_channel = make_roomlist_channel (mgr, request);
      *ret = TP_CHANNEL_IFACE (roomlist_channel);
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED;
    }

  if (handle_type != TP_HANDLE_TYPE_ROOM)
    {
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;
    }

  /* Most be a valid room handle */
  if (!tp_handle_is_valid (room_repo, handle, NULL))
    {
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;
    }

  if (!tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_TEXT))
    {
      text_chan = g_hash_table_lookup (priv->text_channels,
          GUINT_TO_POINTER (handle));

      if (text_chan != NULL)
        {
          status = TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING;
        }
      else
        {
          text_chan = salut_muc_manager_request_new_muc_channel (mgr,
              handle, error);
          if (text_chan == NULL)
            return TP_CHANNEL_FACTORY_REQUEST_STATUS_ERROR;

          status = TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED;
        }

      g_assert (text_chan != NULL);
      *ret = TP_CHANNEL_IFACE (text_chan);
    }
#ifdef ENABLE_DBUS_TUBES
  else if (!tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_TUBES))
    {
      SalutTubesChannel *tubes_chan;

      tubes_chan = g_hash_table_lookup (priv->tubes_channels,
          GUINT_TO_POINTER (handle));

      if (tubes_chan != NULL)
        {
          status = TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING;
          *ret = TP_CHANNEL_IFACE (tubes_chan);
        }
      else
        {
          tubes_chan = create_tubes_channel (mgr, handle, error);
          if (tubes_chan == NULL)
            return TP_CHANNEL_FACTORY_REQUEST_STATUS_ERROR;

          *ret = TP_CHANNEL_IFACE (tubes_chan);

          status = TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED;
        }
    }
#endif
  else
    {
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;
    }

  return status;
}

static void salut_muc_manager_factory_iface_init (gpointer g_iface,
                                                  gpointer iface_data) {
   TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *)g_iface;

   klass->close_all = salut_muc_manager_factory_iface_close_all;
   klass->connecting = salut_muc_manager_factory_iface_connecting;
   klass->connected = salut_muc_manager_factory_iface_connected;
   klass->disconnected = salut_muc_manager_factory_iface_disconnected;
   klass->foreach = salut_muc_manager_factory_iface_foreach;
   klass->request = salut_muc_manager_factory_iface_request;
}

static gboolean
invite_stanza_filter (SalutXmppConnectionManager *mgr,
                      GibberXmppConnection *conn,
                      GibberXmppStanza *stanza,
                      SalutContact *contact,
                      gpointer user_data)
{
  GibberStanzaType type;

  gibber_xmpp_stanza_get_type_info (stanza, &type, NULL);
  if (type != GIBBER_STANZA_TYPE_MESSAGE)
    return FALSE;

  return (gibber_xmpp_node_get_child_ns (stanza->node, "invite",
        GIBBER_TELEPATHY_NS_CLIQUE) != NULL);
}

static void
invite_stanza_callback (SalutXmppConnectionManager *mgr,
                        GibberXmppConnection *conn,
                        GibberXmppStanza *stanza,
                        SalutContact *contact,
                        gpointer user_data)
{
  SalutMucManager *self = SALUT_MUC_MANAGER (user_data);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION (priv->connection);
  TpHandleRepoIface *room_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_ROOM);
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_CONTACT);
  GibberXmppNode *invite, *room_node, *reason_node;
  SalutMucChannel *chan;
  const gchar *room = NULL;
  const gchar *reason = NULL;
  const gchar **params;
  TpHandle room_handle;
  TpHandle inviter_handle;
  const gchar **p;
  GHashTable *params_hash;
  GibberMucConnection *connection = NULL;

  invite = gibber_xmpp_node_get_child_ns (stanza->node, "invite",
      GIBBER_TELEPATHY_NS_CLIQUE);
  g_assert (invite != NULL);

  DEBUG("Got an invitation");

  room_node = gibber_xmpp_node_get_child (invite, "roomname");
  if (room_node == NULL)
    {
      DEBUG ("Invalid invitation, discarding");
      return;
    }
  room = room_node->content;

  reason_node = gibber_xmpp_node_get_child (invite, "reason");
  if (reason_node != NULL)
    reason = reason_node->content;

  if (reason == NULL)
    reason = "";

  params = gibber_muc_connection_get_required_parameters (
      GIBBER_TELEPATHY_NS_CLIQUE);
  if (params == NULL)
    {
      DEBUG ("Invalid invitation, (unknown protocol) discarding");
      return;
    }

  params_hash = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
  for (p = params ; *p != NULL; p++)
    {
      GibberXmppNode *param;

      param = gibber_xmpp_node_get_child (invite, *p);
      if (param == NULL)
        {
          DEBUG("Invalid invitation, (missing parameter) discarding");
          goto discard;
        }

      g_hash_table_insert (params_hash, (gchar *) *p,
          g_strdup (param->content));
    }

  /* FIXME proper serialisation of handle name */
  /* Create the group if it doesn't exist and myself to local_pending */
  room_handle = tp_handle_ensure (room_repo, room, NULL, NULL);

  /* FIXME handle properly */
  g_assert (room_handle != 0);

  chan = g_hash_table_lookup (priv->text_channels,
      GINT_TO_POINTER (room_handle));

  if (chan == NULL)
    {
      connection = _get_connection (self, GIBBER_TELEPATHY_NS_CLIQUE,
          params_hash, NULL);
      if (connection == NULL)
        {
          DEBUG ("Invalid invitation, (wrong protocol parameters) discarding");
          goto discard;
        }

      if (connection == NULL)
        {
          tp_handle_unref (room_repo, room_handle);
          /* FIXME some kinda error to the user maybe ? Ignore for now */
          goto discard;
        }
      /* Need to create a new one */
      chan = salut_muc_manager_new_muc_channel (self, room_handle,
          connection, FALSE);
    }

  /* FIXME handle properly */
  g_assert (chan != NULL);

  inviter_handle = tp_handle_ensure (contact_repo, contact->name, NULL, NULL);

#ifdef ENABLE_OLPC
  salut_connection_olpc_observe_invitation (priv->connection, room_handle,
      inviter_handle, invite);
#endif

  salut_muc_channel_invited (chan, inviter_handle, reason, NULL);
  tp_handle_unref (contact_repo, inviter_handle);

discard:
  if (params_hash != NULL)
    g_hash_table_destroy (params_hash);
}

/* public functions */

gboolean
salut_muc_manager_start (SalutMucManager *self,
                         GError **error)
{
  return SALUT_MUC_MANAGER_GET_CLASS (self)->start (self, error);
}

SalutMucChannel *
salut_muc_manager_get_text_channel (SalutMucManager *self,
                                    TpHandle handle)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  SalutMucChannel *muc;

  muc = g_hash_table_lookup (priv->text_channels, GUINT_TO_POINTER (handle));
  if (muc == NULL)
    return NULL;

  g_object_ref (muc);
  return muc;
}

void
salut_muc_manager_handle_si_stream_request (SalutMucManager *self,
                                            GibberBytestreamIface *bytestream,
                                            TpHandle room_handle,
                                            const gchar *stream_id,
                                            GibberXmppStanza *msg)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
     (TpBaseConnection*) priv->connection, TP_HANDLE_TYPE_ROOM);
  SalutTubesChannel *chan = NULL;

  g_return_if_fail (tp_handle_is_valid (room_repo, room_handle, NULL));

#ifdef ENABLE_DBUS_TUBES
  chan = g_hash_table_lookup (priv->tubes_channels,
      GUINT_TO_POINTER (room_handle));
#endif
  if (chan == NULL)
    {
      GError e = { GIBBER_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "No tubes channel available for this MUC" };

      DEBUG ("tubes channel doesn't exist for muc %d", room_handle);
      gibber_bytestream_iface_close (bytestream, &e);
      return;
    }

  salut_tubes_channel_bytestream_offered (chan, bytestream, msg);
}

SalutTubesChannel *
salut_muc_manager_ensure_tubes_channel (SalutMucManager *self,
                                        TpHandle handle)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  SalutTubesChannel *tubes_chan;

  tubes_chan = g_hash_table_lookup (priv->tubes_channels,
      GUINT_TO_POINTER (handle));
  if (tubes_chan != NULL)
    {
      g_object_ref (tubes_chan);
      return tubes_chan;
    }

  tubes_chan = create_tubes_channel (self, handle, NULL);
  g_assert (tubes_chan != NULL);
  g_object_ref (tubes_chan);

  return tubes_chan;
}

static void
add_room_foreach (SalutRoomlistChannel *roomlist_channel,
                  const gchar *room)
{
  salut_roomlist_channel_add_room (roomlist_channel, room);
}

void
salut_muc_manager_room_discovered (SalutMucManager *self,
                                  const gchar *room)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);

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
salut_muc_manager_room_removed (SalutMucManager *self,
                                const gchar *room)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION (priv->connection);
  TpHandleRepoIface *room_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_ROOM);
  TpHandle handle;
  SalutMucChannel *muc;

  g_slist_foreach (priv->roomlist_channels, (GFunc) remove_room_foreach,
      (gchar *) room);

    /* Do we have to re-announce this room ? */
  handle = tp_handle_lookup (room_repo, room, NULL, NULL);
  if (handle == 0)
    return;

  muc = g_hash_table_lookup (priv->text_channels, GUINT_TO_POINTER (handle));
  if (muc == NULL)
    return;

  DEBUG ("We know this room %s. Try to re-announce it", room);
  salut_muc_channel_publish_service (muc);
}
