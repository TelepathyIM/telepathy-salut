/*
 * muc-manager.c - Source for SalutMucManager
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

#include "config.h"
#include "muc-manager.h"

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

#include "muc-channel.h"
#include "contact-manager.h"
#include "roomlist-channel.h"
#include "roomlist-manager.h"
#include "discovery-client.h"
#include "tube-stream.h"
#include "tube-dbus.h"

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#define DEBUG_FLAG DEBUG_MUC
#include "debug.h"

static gboolean
invite_stanza_callback (WockyPorter *porter,
    WockyStanza *stanza, gpointer user_data);


static void salut_muc_manager_iface_init (gpointer g_iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE(SalutMucManager, salut_muc_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      salut_muc_manager_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER, NULL))

/* properties */
enum {
  PROP_CONNECTION = 1,
  LAST_PROP
};

/* private structure */
typedef struct _SalutMucManagerPrivate SalutMucManagerPrivate;

struct _SalutMucManagerPrivate
{
  SalutConnection *connection;
  gulong status_changed_id;

  guint invite_handler_id;

  /* GUINT_TO_POINTER (room_handle) => (SalutMucChannel *) */
  GHashTable *text_channels;

  /* tube ID => owned SalutTubeIface */
  GHashTable *tubes;

  /* borrowed TpExportableChannel => owned GSList of gpointer  */
  GHashTable *queued_requests;

  /* borrowed SalutMucChannel => owned GSList of borrowed SalutTubeIface */
  GHashTable *text_needed_for_tube;

  gboolean dispose_has_run;
};

#define SALUT_MUC_MANAGER_GET_PRIVATE(obj) \
  ((SalutMucManagerPrivate *) ((SalutMucManager *) obj)->priv)

#define TUBE_TEXT_QUARK (g_quark_from_static_string (\
          "salut-muc-manager-tube-text-channel"))

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

  priv->queued_requests = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) g_slist_free);
  priv->text_needed_for_tube = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) g_slist_free);
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
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
cancel_queued_requests (gpointer k,
    gpointer v,
    gpointer d)
{
  SalutMucManager *self = SALUT_MUC_MANAGER (d);
  GSList *requests_satisfied = v;
  GSList *iter;

  for (iter = requests_satisfied; iter != NULL; iter = iter->next)
    {
      tp_channel_manager_emit_request_failed (self,
          iter->data, TP_ERROR, TP_ERROR_DISCONNECTED,
          "Unable to complete this channel request, we're disconnecting!");
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

  if (priv->queued_requests != NULL)
    g_hash_table_foreach (priv->queued_requests,
        cancel_queued_requests, self);

  tp_clear_pointer (&priv->queued_requests, g_hash_table_unref);
  tp_clear_pointer (&priv->text_needed_for_tube, g_hash_table_unref);

  tp_clear_pointer (&priv->text_channels, g_hash_table_unref);
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
  WockyPorter *porter;

  obj = G_OBJECT_CLASS (salut_muc_manager_parent_class)->
    constructor (type, n_props, props);

  priv = SALUT_MUC_MANAGER_GET_PRIVATE (obj);

  porter = priv->connection->porter;
  priv->invite_handler_id = wocky_porter_register_handler_from_anyone (
      porter, WOCKY_STANZA_TYPE_MESSAGE, WOCKY_STANZA_SUB_TYPE_NONE,
      WOCKY_PORTER_HANDLER_PRIORITY_NORMAL + 1, /* so we get called before the IM manager */
      invite_stanza_callback, obj,
      '(', "invite",
        ':', WOCKY_TELEPATHY_NS_CLIQUE,
      ')', NULL);

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
}

void
salut_muc_manager_dispose (GObject *object)
{
  SalutMucManager *self = SALUT_MUC_MANAGER (object);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->connection->porter != NULL)
    {
      wocky_porter_unregister_handler (priv->connection->porter,
          priv->invite_handler_id);
      priv->invite_handler_id = 0;
    }

  salut_muc_manager_close_all (self);
  g_assert (priv->text_channels == NULL);
  g_assert (priv->queued_requests == NULL);
  g_assert (priv->text_needed_for_tube == NULL);

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_muc_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_muc_manager_parent_class)->dispose (object);
}

/* Channel Manager interface */

static void
salut_muc_manager_foreach_channel (TpChannelManager *iface,
                                   TpExportableChannelFunc foreach,
                                   gpointer user_data)
{
  SalutMucManager *fac = SALUT_MUC_MANAGER (iface);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (fac);
  GHashTableIter iter;
  gpointer value;

  g_hash_table_iter_init (&iter, priv->text_channels);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      TpExportableChannel *chan = TP_EXPORTABLE_CHANNEL (value);

      /* do the text channel */
      foreach (chan, user_data);

      /* now its tube channels */
      salut_muc_channel_foreach (SALUT_MUC_CHANNEL (chan),
          foreach, user_data);
    }
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

static void
salut_muc_manager_type_foreach_channel_class (GType type,
    TpChannelManagerTypeChannelClassFunc func,
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

  /* im.telepathy1.Channel.Type.Text */
  g_value_set_static_string (channel_type_value, TP_IFACE_CHANNEL_TYPE_TEXT);
  func (type, table, muc_channel_allowed_properties,
      user_data);

  /* im.telepathy1.Channel.Type.StreamTube */
  g_value_set_static_string (channel_type_value,
      TP_IFACE_CHANNEL_TYPE_STREAM_TUBE1);
  func (type, table, salut_tube_stream_channel_get_allowed_properties (),
      user_data);

  /* Muc Channel.Type.DBusTube */
  g_value_set_static_string (channel_type_value,
      TP_IFACE_CHANNEL_TYPE_DBUS_TUBE1);
  func (type, table, salut_tube_dbus_channel_get_allowed_properties (),
      user_data);

  g_hash_table_unref (table);
}

static void
associate_channel_to_data (GHashTable *table,
    gpointer channel,
    gpointer data)
{
  GSList *list;

  if (data == NULL)
    return;

  /* yes it might be more 'efficient' to use prepend, then reverse the
   * list before use but that's just annoying. I doubt there'll ever
   * be more than one item in the list anyway. */

  /* get the old list */
  list = g_hash_table_lookup (table, channel);

  /* add the data to it */
  list = g_slist_append (list, data);

  /* steal it so it doesn't get freed */
  g_hash_table_steal (table, channel);

  /* throw it back in */
  g_hash_table_insert (table, channel, list);
}

static void
muc_channel_closed_cb (SalutMucChannel *chan,
                       gpointer user_data)
{
  SalutMucManager *self = SALUT_MUC_MANAGER (user_data);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  TpBaseChannel *base = TP_BASE_CHANNEL (chan);
  TpHandle handle;

  /* channel is actually reappearing, announce it */
  if (tp_base_channel_is_respawning (base))
    {
      tp_channel_manager_emit_new_channel (self,
          TP_EXPORTABLE_CHANNEL (chan), NULL);
      return;
    }

  if (tp_base_channel_is_registered (base))
    {
      tp_channel_manager_emit_channel_closed_for_object (self,
          TP_EXPORTABLE_CHANNEL (chan));
    }

  if (tp_base_channel_is_destroyed (base)
      && priv->text_channels)
    {
      g_object_get (chan, "handle", &handle, NULL);
      DEBUG ("Removing channel with handle %u", handle);

      g_hash_table_remove (priv->text_channels, GUINT_TO_POINTER (handle));
    }
}

static void
muc_channel_tube_closed_cb (SalutTubeIface *tube,
    SalutMucManager *mgr)
{
  SalutMucChannel *channel;

  tp_channel_manager_emit_channel_closed_for_object (mgr,
      TP_EXPORTABLE_CHANNEL (tube));

  channel = g_object_get_qdata (G_OBJECT (tube), TUBE_TEXT_QUARK);
  g_assert (channel != NULL);

  if (salut_muc_channel_can_be_closed (channel)
      && salut_muc_channel_get_autoclose (channel))
    {
      tp_base_channel_close (TP_BASE_CHANNEL (channel));
    }
}

static void
muc_channel_new_tube_cb (SalutMucChannel *channel,
    SalutTubeIface *tube,
    SalutMucManager *mgr)
{
  tp_channel_manager_emit_new_channel (mgr,
      TP_EXPORTABLE_CHANNEL (tube), NULL);

  g_signal_connect (tube, "closed",
      G_CALLBACK (muc_channel_tube_closed_cb), mgr);

  g_object_set_qdata (G_OBJECT (tube), TUBE_TEXT_QUARK,
      channel);
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

static void
muc_channel_ready_cb (SalutMucChannel *chan,
    SalutMucManager *mgr)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (mgr);
  GSList *tube_channels;
  GSList *l;

  /* announce the text channel finally, but only if it is on the bus */
  if (tp_base_channel_is_registered (TP_BASE_CHANNEL (chan)))
    {
      GSList *satisfied = g_hash_table_lookup (priv->queued_requests, chan);

      tp_channel_manager_emit_new_channel (mgr,
          TP_EXPORTABLE_CHANNEL (chan), satisfied);
    }
  g_hash_table_remove (priv->queued_requests, chan);

  /* announce tube channels now */
  tube_channels = g_hash_table_lookup (priv->text_needed_for_tube, chan);

  for (l = tube_channels; l != NULL; l = l->next)
    {
      SalutTubeIface *tube = SALUT_TUBE_IFACE (l->data);
      GSList *requests_satisfied;

      requests_satisfied = g_hash_table_lookup (priv->queued_requests, tube);

      tp_channel_manager_emit_new_channel (mgr,
          TP_EXPORTABLE_CHANNEL (tube), requests_satisfied);

      g_hash_table_remove (priv->queued_requests, tube);
    }

  g_hash_table_remove (priv->text_needed_for_tube, chan);
}

static void
muc_channel_join_error_cb (SalutMucChannel *chan,
    GError *error,
    SalutMucManager *mgr)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (mgr);
  GSList *requests_satisfied;
  GSList *tube_channels;
  GSList *l;

#define FAIL_REQUESTS(requests) \
  { \
    GSList *_l; \
    for (_l = requests; _l != NULL; _l = _l->next) \
      { \
        tp_channel_manager_emit_request_failed (mgr, _l->data, \
            error->domain, error->code, error->message); \
      } \
  }

  /* first fail the text channel itself */
  requests_satisfied = g_hash_table_lookup (priv->queued_requests, chan);
  FAIL_REQUESTS(requests_satisfied);
  g_hash_table_remove (priv->queued_requests, chan);

  /* now fail all tube channel requests */
  tube_channels = g_hash_table_lookup (priv->text_needed_for_tube, chan);

  for (l = tube_channels; l != NULL; l = l->next)
    {
      TpExportableChannel *tube = TP_EXPORTABLE_CHANNEL (l->data);

      requests_satisfied = g_hash_table_lookup (priv->queued_requests, tube);
      FAIL_REQUESTS (requests_satisfied);
      g_hash_table_remove (priv->queued_requests, tube);
    }

  g_hash_table_remove (priv->text_needed_for_tube, chan);
}

static SalutMucChannel *
salut_muc_manager_new_muc_channel (SalutMucManager *mgr,
                                   TpHandle handle,
                                   GibberMucConnection *connection,
                                   TpHandle initiator,
                                   gboolean new_connection,
                                   gboolean requested,
                                   gboolean initially_register)
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
  path = g_strdup_printf ("%s/MucChannel/%u",
      tp_base_connection_get_object_path (base_connection),
      handle);

  chan = SALUT_MUC_MANAGER_GET_CLASS (mgr)->create_muc_channel (mgr,
      priv->connection, path, connection, handle, name, initiator,
      new_connection, requested);
  g_free (path);

  if (initially_register)
    tp_base_channel_register ((TpBaseChannel *) chan);

  g_signal_connect (chan, "closed", G_CALLBACK (muc_channel_closed_cb), mgr);
  g_signal_connect (chan, "new-tube", G_CALLBACK (muc_channel_new_tube_cb), mgr);

  g_hash_table_insert (priv->text_channels, GUINT_TO_POINTER (handle), chan);

  if (salut_muc_channel_is_ready (chan))
    muc_channel_ready_cb (chan, mgr);
  else
    g_signal_connect (chan, "ready", G_CALLBACK (muc_channel_ready_cb), mgr);

  g_signal_connect (chan, "join-error", G_CALLBACK (muc_channel_join_error_cb), mgr);

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
    g_hash_table_unref (params);

  if (connection == NULL)
    {
      DEBUG ("get connection failed: %s", connection_error->message);
      if (error != NULL)
        *error = g_error_new_literal (TP_ERROR, TP_ERROR_NETWORK_ERROR,
            connection_error->message);
      g_error_free (connection_error);
      return NULL;
    }

  /* We requested the channel, so invite ourselves to it */
  if (!gibber_muc_connection_connect (connection, &connection_error))
    {
      DEBUG ("Connect failed: %s", connection_error->message);
      if (error != NULL)
        *error = g_error_new_literal (TP_ERROR, TP_ERROR_NETWORK_ERROR,
            connection_error->message);
      g_error_free (connection_error);
      g_object_unref (connection);
      return NULL;
    }
  DEBUG ("Connect succeeded");

  requested = (request_token != NULL);

  text_chan = salut_muc_manager_new_muc_channel (mgr, handle,
      connection, tp_base_connection_get_self_handle (base_connection),
      (params == NULL), requested, announce);
  r = salut_muc_channel_invited (text_chan,
        tp_base_connection_get_self_handle (base_connection), NULL, NULL);
  /* Inviting ourselves to a connected channel should always
   * succeed */
  g_assert (r);

  /* only signal the creation of the text channel if we're asked for
   * it to happen. note that in the case of announce=FALSE,
   * muc_channel_ready_cb will still get fired, but because it isn't
   * registered on the bus, it will not be signalled then either. */
  if (announce)
    {
      if (salut_muc_channel_is_ready (text_chan))
        {
          if (request_token != NULL)
            tokens = g_slist_prepend (tokens, request_token);

          tp_channel_manager_emit_new_channel (mgr,
              TP_EXPORTABLE_CHANNEL (text_chan), tokens);
        }
      else
        {
          associate_channel_to_data (priv->queued_requests,
              text_chan, request_token);
        }
    }

  g_slist_free (tokens);

  return text_chan;
}

static gboolean
handle_tube_channel_request (SalutMucManager *self,
                             gpointer request_token,
                             GHashTable *request_properties,
                             gboolean require_new,
                             TpHandle handle,
                             GError **error)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  SalutMucChannel *text_chan;
  SalutTubeIface *new_channel;

  text_chan = g_hash_table_lookup (priv->text_channels,
      GUINT_TO_POINTER (handle));

  if (text_chan == NULL)
    {
      DEBUG ("have to create the text channel before the tube one");
      text_chan = salut_muc_manager_request_new_muc_channel (self,
          handle, NULL, FALSE, error);

      if (text_chan == NULL)
        return FALSE;
    }

  new_channel = salut_muc_channel_tube_request (text_chan,
      request_properties);
  g_assert (new_channel != NULL);

  g_signal_connect (new_channel, "closed",
      G_CALLBACK (muc_channel_tube_closed_cb), self);

  g_object_set_qdata (G_OBJECT (new_channel), TUBE_TEXT_QUARK,
      text_chan);

  if (salut_muc_channel_is_ready (text_chan))
    {
      GSList *request_tokens = g_slist_append (NULL, request_token);

      tp_channel_manager_emit_new_channel (self,
          TP_EXPORTABLE_CHANNEL (new_channel), request_tokens);

      g_slist_free (request_tokens);
    }
  else
    {
      associate_channel_to_data (priv->text_needed_for_tube,
          text_chan, new_channel);

      /* we need to do this so when the muc channel is ready, the tube
       * can be announced and satisfy this request */
      associate_channel_to_data (priv->queued_requests,
          new_channel, request_token);
    }

  return TRUE;
}

static gboolean
handle_stream_tube_channel_request (SalutMucManager *self,
    gpointer request_token,
    GHashTable *request_properties,
    gboolean require_new,
    TpHandle handle,
    GError **error)
{
  const gchar *service;

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          muc_tubes_channel_fixed_properties,
          salut_tube_stream_channel_get_allowed_properties (),
          error))
    return FALSE;

  /* "Service" is a mandatory, not-fixed property */
  service = tp_asv_get_string (request_properties,
            TP_PROP_CHANNEL_TYPE_STREAM_TUBE1_SERVICE);
  if (service == NULL)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
          "Request does not contain the mandatory property '%s'",
          TP_PROP_CHANNEL_TYPE_STREAM_TUBE1_SERVICE);
      return FALSE;
    }

  return handle_tube_channel_request (self, request_token, request_properties,
      require_new, handle, error);
}

static gboolean
handle_dbus_tube_channel_request (SalutMucManager *self,
    gpointer request_token,
    GHashTable *request_properties,
    gboolean require_new,
    TpHandle handle,
    GError **error)
{
  const gchar *service;

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          muc_tubes_channel_fixed_properties,
          salut_tube_dbus_channel_get_allowed_properties (),
          error))
    return FALSE;

  /* "ServiceName" is a mandatory, not-fixed property */
  service = tp_asv_get_string (request_properties,
      TP_PROP_CHANNEL_TYPE_DBUS_TUBE1_SERVICE_NAME);
  if (service == NULL)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
          "Request does not contain the mandatory property '%s'",
          TP_PROP_CHANNEL_TYPE_DBUS_TUBE1_SERVICE_NAME);
      return FALSE;
    }

  return handle_tube_channel_request (self, request_token, request_properties,
      require_new, handle, error);
}

static gboolean
salut_muc_manager_request (SalutMucManager *self,
                           gpointer request_token,
                           GHashTable *request_properties,
                           gboolean require_new)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  GError *error = NULL;
  TpHandle handle;
  const gchar *channel_type;
  SalutMucChannel *text_chan;

  if (tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandleType", NULL) != TP_HANDLE_TYPE_ROOM)
    return FALSE;

  channel_type = tp_asv_get_string (request_properties,
      TP_IFACE_CHANNEL ".ChannelType");

  if (tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_TEXT) &&
      tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE1) &&
      tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE1))
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
          if (require_new
              && tp_base_channel_is_registered (TP_BASE_CHANNEL (text_chan)))
            {
              g_set_error (&error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
                  "That channel has already been created (or requested)");
              goto error;
            }
          else
            {
              if (tp_base_channel_is_registered (TP_BASE_CHANNEL (text_chan)))
                {
                  tp_channel_manager_emit_request_already_satisfied (self,
                      request_token, TP_EXPORTABLE_CHANNEL (text_chan));
                }
              else
                {
                  tp_base_channel_register (TP_BASE_CHANNEL (text_chan));

                  if (salut_muc_channel_is_ready (text_chan))
                    {
                      GSList *tokens = g_slist_append (NULL, request_token);
                      tp_channel_manager_emit_new_channel (self,
                          TP_EXPORTABLE_CHANNEL (text_chan), tokens);
                      g_slist_free (tokens);
                    }
                  else
                    {
                      associate_channel_to_data (priv->queued_requests,
                          text_chan, request_token);
                    }
                }
            }
        }
      else
        {
          text_chan = salut_muc_manager_request_new_muc_channel (self,
              handle, request_token, TRUE, NULL);
        }

      return TRUE;
    }
  else if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE1))
    {
      if (handle_stream_tube_channel_request (self, request_token,
          request_properties, require_new, handle, &error))
        return TRUE;
    }
  else if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE1))
    {
      if (handle_dbus_tube_channel_request (self, request_token,
          request_properties, require_new, handle, &error))
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
  iface->type_foreach_channel_class =
    salut_muc_manager_type_foreach_channel_class;
  iface->request_channel = salut_muc_manager_request_channel;
  iface->create_channel = salut_muc_manager_create_channel;
  iface->ensure_channel = salut_muc_manager_ensure_channel;
}

static gboolean
invite_stanza_callback (WockyPorter *porter,
    WockyStanza *stanza,
    gpointer user_data)
{
  SalutMucManager *self = SALUT_MUC_MANAGER (user_data);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION (priv->connection);
  TpHandleRepoIface *room_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_ROOM);
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_CONTACT);
  WockyNode *invite, *room_node, *reason_node;
  SalutMucChannel *chan;
  const gchar *room = NULL;
  const gchar *reason = NULL;
  const gchar **params;
  TpHandle room_handle;
  TpHandle inviter_handle;
  const gchar **p;
  GHashTable *params_hash;
  GibberMucConnection *connection = NULL;
  SalutContact *contact = SALUT_CONTACT (wocky_stanza_get_from_contact (stanza));

  invite = wocky_node_get_child_ns (wocky_stanza_get_top_node (stanza),
        "invite", WOCKY_TELEPATHY_NS_CLIQUE);
  g_assert (invite != NULL);

  DEBUG("Got an invitation");

  room_node = wocky_node_get_child (invite, "roomname");
  if (room_node == NULL)
    {
      DEBUG ("Invalid invitation, discarding");
      return TRUE;
    }
  room = room_node->content;

  reason_node = wocky_node_get_child (invite, "reason");
  if (reason_node != NULL)
    reason = reason_node->content;

  if (reason == NULL)
    reason = "";

  params = gibber_muc_connection_get_required_parameters (
      WOCKY_TELEPATHY_NS_CLIQUE);
  if (params == NULL)
    {
      DEBUG ("Invalid invitation, (unknown protocol) discarding");
      return TRUE;
    }

  params_hash = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
  for (p = params ; *p != NULL; p++)
    {
      WockyNode *param;

      param = wocky_node_get_child (invite, *p);
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
      connection = _get_connection (self, WOCKY_TELEPATHY_NS_CLIQUE,
          params_hash, NULL);
      if (connection == NULL)
        {
          DEBUG ("Invalid invitation, (wrong protocol parameters) discarding");
          goto discard;
        }

      if (connection == NULL)
        {
          /* FIXME some kinda error to the user maybe ? Ignore for now */
          goto discard;
        }
      /* Need to create a new one */
      chan = salut_muc_manager_new_muc_channel (self, room_handle,
          connection, inviter_handle, FALSE, FALSE, TRUE);

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

  return TRUE;

discard:
  if (params_hash != NULL)
    g_hash_table_unref (params_hash);
  return TRUE;
}

/* public functions */

SalutMucChannel *
salut_muc_manager_get_text_channel (SalutMucManager *self,
                                    TpHandle handle)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  SalutMucChannel *muc;

  if (priv->text_channels == NULL)
    return NULL;

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
                                            WockyStanza *msg)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
     (TpBaseConnection *) priv->connection, TP_HANDLE_TYPE_ROOM);
  SalutMucChannel *chan = NULL;

  g_return_if_fail (tp_handle_is_valid (room_repo, room_handle, NULL));

  chan = g_hash_table_lookup (priv->text_channels,
      GUINT_TO_POINTER (room_handle));
  if (chan == NULL)
    {
      GError e = { WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "No channel available for this MUC" };

      DEBUG ("MUC channel doesn't exist for muc %d", room_handle);
      gibber_bytestream_iface_close (bytestream, &e);
      return;
    }

  salut_muc_channel_bytestream_offered (chan, bytestream, msg);
}
