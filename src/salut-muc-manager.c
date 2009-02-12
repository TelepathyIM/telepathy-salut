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
#include "salut-roomlist-manager.h"
#include "salut-xmpp-connection-manager.h"
#include "salut-discovery-client.h"
#include "tube-stream.h"

#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>

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


static void salut_muc_manager_iface_init (gpointer g_iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE(SalutMucManager, salut_muc_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      salut_muc_manager_iface_init));

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
  gulong status_changed_id;
  SalutXmppConnectionManager *xmpp_connection_manager;

  /* GUINT_TO_POINTER (room_handle) => (SalutMucChannel *) */
  GHashTable *text_channels;
   /* GUINT_TO_POINTER(room_handle) => (SalutTubesChannel *) */
  GHashTable *tubes_channels;

  gboolean dispose_has_run;
};

#define SALUT_MUC_MANAGER_GET_PRIVATE(obj) \
  ((SalutMucManagerPrivate *) ((SalutMucManager *) obj)->priv)

static void
salut_muc_manager_init (SalutMucManager *obj)
{
  SalutMucManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (obj,
      SALUT_TYPE_MUC_MANAGER, SalutMucManagerPrivate);

  obj->priv = priv;

  priv->connection = NULL;

  /* allocate any data required by the object here */
  priv->text_channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                               NULL, g_object_unref);
  priv->tubes_channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);
}

static void
salut_muc_manager_get_property (GObject *object,
                                guint property_id,
                                GValue *value,
                                GParamSpec *pspec)
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

static void
salut_muc_manager_close_all (SalutMucManager *self)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);

  DEBUG ("closing channels");

  if (priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (priv->connection, priv->status_changed_id);
      priv->status_changed_id = 0;
    }

  if (priv->text_channels)
    {
      GHashTable *tmp = priv->text_channels;
      priv->text_channels = NULL;
      g_hash_table_destroy (tmp);
    }

  if (priv->tubes_channels != NULL)
    {
      GHashTable *tmp = priv->tubes_channels;
      priv->tubes_channels = NULL;
      g_hash_table_destroy (tmp);
    }
}

static void
connection_status_changed_cb (SalutConnection *conn,
                              guint status,
                              guint reason,
                              SalutMucManager *self)
{
  switch (status)
    {
    case TP_CONNECTION_STATUS_DISCONNECTED:
      salut_muc_manager_close_all (self);
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

  priv->status_changed_id = g_signal_connect (priv->connection,
      "status-changed", (GCallback) connection_status_changed_cb, obj);

  return obj;
}

static void salut_muc_manager_dispose (GObject *object);

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

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "The Salut Connection associated with this muc manager",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION,
      param_spec);

  param_spec = g_param_spec_object (
      "xmpp-connection-manager",
      "SalutXmppConnectionManager object",
      "The Salut XMPP Connection Manager associated with this muc "
      "manager",
      SALUT_TYPE_XMPP_CONNECTION_MANAGER,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
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

  salut_muc_manager_close_all (self);
  g_assert (priv->text_channels == NULL);
  g_assert (priv->tubes_channels == NULL);

  if (priv->xmpp_connection_manager != NULL)
    {
      g_object_unref (priv->xmpp_connection_manager);
      priv->xmpp_connection_manager = NULL;
    }

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_muc_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_muc_manager_parent_class)->dispose (object);
}

/* Channel Manager interface */

struct _ForeachData
{
  TpExportableChannelFunc foreach;
  gpointer user_data;
};

static void
_foreach_slave (gpointer key,
                gpointer value,
                gpointer user_data)
{
  struct _ForeachData *data = (struct _ForeachData *) user_data;
  TpExportableChannel *channel = TP_EXPORTABLE_CHANNEL (value);

  data->foreach (channel, data->user_data);
}

static void
salut_muc_manager_foreach_channel (TpChannelManager *iface,
                                   TpExportableChannelFunc foreach,
                                   gpointer user_data)
{
  SalutMucManager *fac = SALUT_MUC_MANAGER (iface);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (fac);
  struct _ForeachData data;

  data.user_data = user_data;
  data.foreach = foreach;

  g_hash_table_foreach (priv->text_channels, _foreach_slave, &data);
  g_hash_table_foreach (priv->tubes_channels, _foreach_slave, &data);
}

static const gchar * const muc_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};

static const gchar * const * muc_tubes_channel_fixed_properties =
    muc_channel_fixed_properties;

static const gchar * const muc_channel_allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    NULL
};

static const gchar * const * muc_tubes_channel_allowed_properties =
    muc_channel_allowed_properties;


static void
salut_muc_manager_foreach_channel_class (TpChannelManager *manager,
                                         TpChannelManagerChannelClassFunc func,
                                         gpointer user_data)
{
  GHashTable *table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);
  GValue *channel_type_value, *handle_type_value;

  channel_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  /* no string value yet - we'll change it for each channel class */
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType",
      channel_type_value);

  handle_type_value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (handle_type_value, TP_HANDLE_TYPE_ROOM);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetHandleType",
      handle_type_value);

  /* org.freedesktop.Telepathy.Channel.Type.Text */
  g_value_set_static_string (channel_type_value, TP_IFACE_CHANNEL_TYPE_TEXT);
  func (manager, table, muc_channel_allowed_properties,
      user_data);

  /* org.freedesktop.Telepathy.Channel.Type.Tubes */
  g_value_set_static_string (channel_type_value, TP_IFACE_CHANNEL_TYPE_TUBES);
  func (manager, table, muc_tubes_channel_allowed_properties,
      user_data);

  /* org.freedesktop.Telepathy.Channel.Type.StreamTube */
  g_value_set_static_string (channel_type_value,
      SALUT_IFACE_CHANNEL_TYPE_STREAM_TUBE);
  func (manager, table, salut_stream_tube_channel_allowed_properties,
      user_data);

  g_hash_table_destroy (table);
}


static void
muc_channel_closed_cb (SalutMucChannel *chan,
                       gpointer user_data)
{
  SalutMucManager *self = SALUT_MUC_MANAGER (user_data);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  TpHandle handle;

  tp_channel_manager_emit_channel_closed_for_object (self,
      TP_EXPORTABLE_CHANNEL (chan));

  if (priv->text_channels)
    {
      g_object_get (chan, "handle", &handle, NULL);
      DEBUG ("Removing channel with handle %u", handle);

      if (priv->tubes_channels != NULL)
        {
          SalutTubesChannel *tubes;

          tubes = g_hash_table_lookup (priv->tubes_channels,
              GUINT_TO_POINTER (handle));
          if (tubes != NULL)
            salut_tubes_channel_close (tubes);
        }

      g_hash_table_remove (priv->text_channels, GUINT_TO_POINTER (handle));
    }
}

/**
 * tubes_channel_closed_cb:
 *
 * Signal callback for when a tubes channel is closed. Removes the references
 * that MucManager holds to them.
 */
static void
tubes_channel_closed_cb (SalutTubesChannel *chan,
                         gpointer user_data)
{
  SalutMucManager *fac = SALUT_MUC_MANAGER (user_data);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (fac);
  TpHandle room_handle;

  tp_channel_manager_emit_channel_closed_for_object (fac,
      TP_EXPORTABLE_CHANNEL (chan));

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
                                   TpHandle initiator,
                                   gboolean new_connection,
                                   gboolean requested)
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
      priv->connection, path, connection, handle, name, initiator,
      new_connection, priv->xmpp_connection_manager, requested);
  g_free (path);

  g_signal_connect (chan, "closed", G_CALLBACK (muc_channel_closed_cb), mgr);

  g_hash_table_insert (priv->text_channels, GUINT_TO_POINTER (handle), chan);

  return chan;
}

/**
 * new_tubes_channel:
 *
 * Creates the SalutTubesChannel object with the given parameters.
 */
static SalutTubesChannel *
new_tubes_channel (SalutMucManager *self,
                   TpHandle room,
                   SalutMucChannel *muc,
                   TpHandle initiator,
                   gboolean requested)
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
      "initiator-handle", initiator,
      "requested", requested,
      NULL);

  g_signal_connect (chan, "closed", (GCallback) tubes_channel_closed_cb, self);

  g_hash_table_insert (priv->tubes_channels, GUINT_TO_POINTER (room), chan);

  g_free (object_path);

  return chan;
}

static SalutMucChannel *
salut_muc_manager_request_new_muc_channel (SalutMucManager *mgr,
                                           TpHandle handle,
                                           gpointer request_token,
                                           gboolean announce,
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
  GSList *tokens = NULL;
  SalutRoomlistManager *roomlist_manager;
  gboolean requested;

  g_object_get (priv->connection, "roomlist-manager", &roomlist_manager, NULL);

  room_name = tp_handle_inspect (room_repo, handle);

  if (SALUT_ROOMLIST_MANAGER_GET_CLASS (roomlist_manager)->find_muc_address
      (roomlist_manager, room_name, &address, &p))
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
  g_object_unref (roomlist_manager);

  connection = _get_connection (mgr, NULL, params, &connection_error);

  if (params != NULL)
    g_hash_table_destroy (params);

  if (connection == NULL)
    {
      DEBUG ("get connection failed: %s", connection_error->message);
      if (error != NULL)
        *error = g_error_new_literal (TP_ERRORS, TP_ERROR_NETWORK_ERROR,
            connection_error->message);
      g_error_free (connection_error);
      return NULL;
    }

  /* We requested the channel, so invite ourselves to it */
  if (!gibber_muc_connection_connect (connection, &connection_error))
    {
      DEBUG ("Connect failed: %s", connection_error->message);
      if (error != NULL)
        *error = g_error_new_literal (TP_ERRORS, TP_ERROR_NETWORK_ERROR,
            connection_error->message);
      g_error_free (connection_error);
      g_object_unref (connection);
      return NULL;
    }
  DEBUG ("Connect succeeded");

  requested = (request_token != NULL);

  text_chan = salut_muc_manager_new_muc_channel (mgr, handle,
      connection, base_connection->self_handle, params == NULL,
      requested);
  r = salut_muc_channel_invited (text_chan,
        base_connection->self_handle, NULL, NULL);
  /* Inviting ourselves to a connected channel should always
   * succeed */
  g_assert (r);

  if (request_token != NULL)
    tokens = g_slist_prepend (tokens, request_token);

  if (announce)
    {
      tp_channel_manager_emit_new_channel (mgr,
          TP_EXPORTABLE_CHANNEL (text_chan), tokens);
    }

  g_slist_free (tokens);

  return text_chan;
}

static SalutTubesChannel *
create_tubes_channel (SalutMucManager *self,
                      TpHandle handle,
                      TpHandle initiator,
                      gpointer request_token,
                      gboolean announce,
                      gboolean *text_created,
                      gboolean requested,
                      GError **error)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  SalutMucChannel *text_chan;
  SalutTubesChannel *tubes_chan;
  gboolean txt_created = FALSE;

  text_chan = g_hash_table_lookup (priv->text_channels,
      GUINT_TO_POINTER (handle));

  if (text_chan == NULL)
    {
      DEBUG ("have to create the text channel before the tubes one");
      text_chan = salut_muc_manager_request_new_muc_channel (self,
          handle, NULL, FALSE, error);

      if (text_chan == NULL)
        return NULL;

      txt_created = TRUE;
    }

  tubes_chan = new_tubes_channel (self, handle, text_chan, initiator,
      requested);
  g_assert (tubes_chan != NULL);

  if (announce)
    {
      GHashTable *channels;
      GSList *tokens = NULL;

      if (request_token != NULL)
        tokens = g_slist_prepend (tokens, request_token);

      /* announce channels */
      channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
          NULL, NULL);

      if (txt_created)
        {
          g_hash_table_insert (channels, text_chan, NULL);
        }

      g_hash_table_insert (channels, tubes_chan, tokens);
      tp_channel_manager_emit_new_channels (self, channels);

      g_hash_table_destroy (channels);
      g_slist_free (tokens);
    }


  if (text_created != NULL)
    *text_created = txt_created;

  return tubes_chan;
}

static gboolean
salut_muc_manager_request (SalutMucManager *self,
                           gpointer request_token,
                           GHashTable *request_properties,
                           gboolean require_new)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *base_conn = (TpBaseConnection *) priv->connection;
  GError *error = NULL;
  TpHandle handle;
  const gchar *channel_type;
  SalutMucChannel *text_chan;
  SalutTubesChannel *tubes_chan;

  if (tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandleType", NULL) != TP_HANDLE_TYPE_ROOM)
    return FALSE;

  channel_type = tp_asv_get_string (request_properties,
      TP_IFACE_CHANNEL ".ChannelType");

  if (tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_TEXT) &&
      tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_TUBES) &&
      tp_strdiff (channel_type, SALUT_IFACE_CHANNEL_TYPE_STREAM_TUBE))
    return FALSE;

  /* validity already checked by TpBaseConnection */
  handle = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandle", NULL);
  g_assert (handle != 0);

  if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_TEXT))
    {
      if (tp_channel_manager_asv_has_unknown_properties (request_properties,
              muc_channel_fixed_properties, muc_channel_allowed_properties,
              &error))
        goto error;

      text_chan = g_hash_table_lookup (priv->text_channels,
          GINT_TO_POINTER (handle));

      if (text_chan != NULL)
        {
          if (require_new)
            {
              g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                  "That channel has already been created (or requested)");
              goto error;
            }
          else
            {
              tp_channel_manager_emit_request_already_satisfied (self,
                  request_token, TP_EXPORTABLE_CHANNEL (text_chan));
            }
        }
      else
        {
          text_chan = salut_muc_manager_request_new_muc_channel (self,
              handle, request_token, TRUE, NULL);
        }

      return TRUE;
    }
  else if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_TUBES))
    {
      if (tp_channel_manager_asv_has_unknown_properties (request_properties,
              muc_tubes_channel_fixed_properties,
              muc_tubes_channel_allowed_properties,
              &error))
        goto error;

      tubes_chan = g_hash_table_lookup (priv->tubes_channels,
          GUINT_TO_POINTER (handle));

      if (tubes_chan != NULL)
        {
          if (require_new)
            {
              g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                  "That channel has already been created (or requested)");
              goto error;
            }
          else
            {
              tp_channel_manager_emit_request_already_satisfied (self,
                  request_token, TP_EXPORTABLE_CHANNEL (tubes_chan));
            }
        }
      else
        {
          tubes_chan = create_tubes_channel (self, handle,
              base_conn->self_handle, request_token, TRUE, NULL, TRUE, &error);
          if (tubes_chan == NULL)
            goto error;
        }

      return TRUE;
    }
  else if (!tp_strdiff (channel_type, SALUT_IFACE_CHANNEL_TYPE_STREAM_TUBE))
    {
      const gchar *service;
      SalutTubeIface *new_channel;
      GHashTable *channels;
      GSList *request_tokens;
      gboolean announce_text = FALSE, announce_tubes = FALSE;

      if (tp_channel_manager_asv_has_unknown_properties (request_properties,
              muc_tubes_channel_fixed_properties,
              salut_stream_tube_channel_allowed_properties,
              &error))
        goto error;

      /* "Service" is a mandatory, not-fixed property */
      service = tp_asv_get_string (request_properties,
                SALUT_IFACE_CHANNEL_TYPE_STREAM_TUBE ".Service");
      if (service == NULL)
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
              "Request does not contain the mandatory property '%s'",
              SALUT_IFACE_CHANNEL_TYPE_STREAM_TUBE ".Service");
          goto error;
        }

      tubes_chan = g_hash_table_lookup (priv->tubes_channels,
          GUINT_TO_POINTER (handle));
      if (tubes_chan == NULL)
        {
          tubes_chan = create_tubes_channel (self, handle,
              base_conn->self_handle, NULL, FALSE, &announce_text,
              FALSE, &error);
          if (tubes_chan == NULL)
            goto error;
          announce_tubes = TRUE;
        }

      g_assert (tubes_chan != NULL);
      new_channel = salut_tubes_channel_tube_request (tubes_chan, channel_type,
          service);
      g_assert (new_channel != NULL);

      /* announce channels */
      channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
          NULL, NULL);

      if (announce_text)
        {
          text_chan = g_hash_table_lookup (priv->text_channels,
              GINT_TO_POINTER (handle));
          g_assert (text_chan != NULL);
          g_hash_table_insert (channels, text_chan, NULL);
        }

      if (announce_tubes)
        {
          g_hash_table_insert (channels, tubes_chan, NULL);
        }

      request_tokens = g_slist_prepend (NULL, request_token);
      g_hash_table_insert (channels, new_channel, request_tokens);
      tp_channel_manager_emit_new_channels (self, channels);

      g_hash_table_destroy (channels);
      g_slist_free (request_tokens);
      return TRUE;
    }
  else
    {
      return FALSE;
    }

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}

static gboolean
salut_muc_manager_create_channel (TpChannelManager *manager,
                                  gpointer request_token,
                                  GHashTable *request_properties)
{
  SalutMucManager *self = SALUT_MUC_MANAGER (manager);

  return salut_muc_manager_request (self, request_token, request_properties,
      TRUE);
}


static gboolean
salut_muc_manager_request_channel (TpChannelManager *manager,
                                   gpointer request_token,
                                   GHashTable *request_properties)
{
  SalutMucManager *self = SALUT_MUC_MANAGER (manager);

  return salut_muc_manager_request (self, request_token, request_properties,
      FALSE);
}


static gboolean
salut_muc_manager_ensure_channel (TpChannelManager *manager,
                                  gpointer request_token,
                                  GHashTable *request_properties)
{
  SalutMucManager *self = SALUT_MUC_MANAGER (manager);

  return salut_muc_manager_request (self, request_token, request_properties,
      FALSE);
}


static void salut_muc_manager_iface_init (gpointer g_iface,
                                          gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = salut_muc_manager_foreach_channel;
  iface->foreach_channel_class = salut_muc_manager_foreach_channel_class;
  iface->request_channel = salut_muc_manager_request_channel;
  iface->create_channel = salut_muc_manager_create_channel;
  iface->ensure_channel = salut_muc_manager_ensure_channel;
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

  inviter_handle = tp_handle_ensure (contact_repo, contact->name, NULL, NULL);

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
          connection, inviter_handle, FALSE, FALSE);

      tp_channel_manager_emit_new_channel (self, TP_EXPORTABLE_CHANNEL (chan),
          NULL);
    }

  /* FIXME handle properly */
  g_assert (chan != NULL);

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
     (TpBaseConnection *) priv->connection, TP_HANDLE_TYPE_ROOM);
  SalutTubesChannel *chan = NULL;

  g_return_if_fail (tp_handle_is_valid (room_repo, room_handle, NULL));

  chan = g_hash_table_lookup (priv->tubes_channels,
      GUINT_TO_POINTER (room_handle));
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

/* Caller is reponsible of announcing the channel if created */
SalutTubesChannel *
salut_muc_manager_ensure_tubes_channel (SalutMucManager *self,
                                        TpHandle handle,
                                        TpHandle actor,
                                        gboolean *created)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  SalutTubesChannel *tubes_chan;

  tubes_chan = g_hash_table_lookup (priv->tubes_channels,
      GUINT_TO_POINTER (handle));
  if (tubes_chan != NULL)
    {
      g_object_ref (tubes_chan);
      *created = FALSE;
      return tubes_chan;
    }


  tubes_chan = create_tubes_channel (self, handle, actor, NULL, FALSE, NULL,
      FALSE, NULL);
  g_assert (tubes_chan != NULL);
  g_object_ref (tubes_chan);

  *created = TRUE;
  return tubes_chan;
}
