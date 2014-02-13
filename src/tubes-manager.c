/*
 * tubes-manager.c - Source for SalutTubesManager
 * Copyright (C) 2006-2008 Collabora Ltd.
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
#include "tubes-manager.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <wocky/wocky.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#define DEBUG_FLAG DEBUG_TUBES

#include <salut/caps-channel-manager.h>

#include "debug.h"
#include "extensions/extensions.h"
#include "connection.h"
#include "capabilities.h"
#include "muc-manager.h"
#include "muc-channel.h"
#include "self.h"
#include "util.h"
#include "tube-iface.h"
#include "tube-dbus.h"
#include "tube-stream.h"


static void salut_tubes_manager_iface_init (gpointer g_iface,
    gpointer iface_data);
static void gabble_caps_channel_manager_iface_init (
    GabbleCapsChannelManagerIface *);

static SalutTubeIface * create_new_tube (SalutTubesManager *self,
    SalutTubeType type,
    TpHandle handle,
    const gchar *service,
    GHashTable *parameters,
    guint64 tube_id,
    guint portnum,
    WockyStanza *iq_req);

G_DEFINE_TYPE_WITH_CODE (SalutTubesManager,
    salut_tubes_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
        salut_tubes_manager_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER,
      gabble_caps_channel_manager_iface_init))

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_CONTACT_MANAGER,
  LAST_PROPERTY
};

typedef struct _SalutTubesManagerPrivate \
          SalutTubesManagerPrivate;
struct _SalutTubesManagerPrivate
{
  SalutConnection *conn;
  gulong status_changed_id;
  guint iq_tube_handler_id;
  SalutContactManager *contact_manager;

  /* guint tube ID => (owned) (SalutTubeIface *) */
  GHashTable *tubes;

  gboolean dispose_has_run;
};

#define SALUT_TUBES_MANAGER_GET_PRIVATE(obj) \
    ((SalutTubesManagerPrivate *) obj->priv)

static void
salut_tubes_manager_init (SalutTubesManager *self)
{
  SalutTubesManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_TUBES_MANAGER, SalutTubesManagerPrivate);

  self->priv = priv;

  priv->tubes = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) g_object_unref);

  priv->conn = NULL;
  priv->dispose_has_run = FALSE;
}

/* similar to the same function in tubes-channel.c but extract
 * information from a 1-1 <iq> message */
static gboolean
extract_tube_information (TpHandleRepoIface *contact_repo,
                          WockyStanza *stanza,
                          gboolean *close_out,
                          SalutTubeType *type,
                          TpHandle *initiator_handle,
                          const gchar **service,
                          GHashTable **parameters,
                          guint64 *tube_id,
                          guint *portnum,
                          GError **error)
{
  WockyNode *iq;
  WockyNode *tube_node, *close_node, *node;
  gboolean _close;

  iq = wocky_stanza_get_top_node (stanza);

  if (initiator_handle != NULL)
    {
      const gchar *from;
      from = wocky_node_get_attribute (iq, "from");
      if (from == NULL)
        {
          g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
              "got a message without a from field");
          return FALSE;
        }
      *initiator_handle = tp_handle_ensure (contact_repo, from, NULL,
          NULL);

      if (*initiator_handle == 0)
        {
          g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
              "invalid initiator ID %s", from);
          return FALSE;
        }
    }

  tube_node = wocky_node_get_child_ns (iq, "tube",
      WOCKY_TELEPATHY_NS_TUBES);
  close_node = wocky_node_get_child_ns (iq, "close",
      WOCKY_TELEPATHY_NS_TUBES);

  if (tube_node == NULL && close_node == NULL)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "The <iq> does not have a <tube> nor a <close>");
      return FALSE;
    }
  if (tube_node != NULL && close_node != NULL)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "The <iq> has both a <tube> nor a <close>");
      return FALSE;
    }
  if (tube_node != NULL)
    {
      node = tube_node;
    }
  else
    {
      node = close_node;
    }

  _close = (close_node != NULL);
  if (close_out != NULL)
    {
      *close_out = _close;
    }

  if (tube_id != NULL)
    {
      const gchar *str;
      guint64 tmp;

      str = wocky_node_get_attribute (node, "id");
      if (str == NULL)
        {
          g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
              "no tube id in tube request");
          return FALSE;
        }

      tmp = g_ascii_strtoull (str, NULL, 10);
      if (tmp == 0 || tmp > G_MAXUINT32)
        {
          g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
              "tube id is non-numeric or out of range: %s", str);
          return FALSE;
        }
      *tube_id = tmp;
    }

  /* next fields are not in the close stanza */
  if (_close)
    return TRUE;

  if (type != NULL)
    {
      const gchar *tube_type;

      tube_type = wocky_node_get_attribute (tube_node, "type");
      if (!tp_strdiff (tube_type, "stream"))
        *type = SALUT_TUBE_TYPE_STREAM;
      else if (!tp_strdiff (tube_type, "dbus"))
        *type = SALUT_TUBE_TYPE_DBUS;
      else
        {
          g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
              "The <iq><tube> does not have a correct type: '%s'", tube_type);
          return FALSE;
        }
    }

  if (service != NULL)
    {
      *service = wocky_node_get_attribute (tube_node, "service");
    }

  if (parameters != NULL)
    {
      WockyNode *parameters_node;

      parameters_node = wocky_node_get_child (tube_node, "parameters");
      *parameters = salut_wocky_node_extract_properties (parameters_node,
          "parameter");
    }

  if (portnum != NULL)
    {
      WockyNode *transport_node;
      const gchar *str;
      gchar *endptr;
      long int tmp;

      transport_node = wocky_node_get_child (tube_node, "transport");
      if (transport_node == NULL)
        {
          g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
              "no transport to connect to in the tube request");
          return FALSE;
        }

      str = wocky_node_get_attribute (transport_node, "port");
      if (str == NULL)
        {
          g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
              "no port to connect to in the tube request");
          return FALSE;
        }

      tmp = strtol (str, &endptr, 10);
      if (!endptr || *endptr)
        {
          g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
              "port is not numeric: %s", str);
          return FALSE;
        }
      *portnum = (int) tmp;
    }

  return TRUE;
}

static gboolean
iq_tube_request_cb (WockyPorter *porter,
                    WockyStanza *stanza,
                    gpointer user_data)
{
  SalutTubesManager *self = SALUT_TUBES_MANAGER (user_data);
  SalutTubesManagerPrivate *priv = SALUT_TUBES_MANAGER_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_ENTITY_TYPE_CONTACT);

  /* tube informations */
  const gchar *service;
  SalutTubeType tube_type;
  TpHandle initiator_handle;
  GHashTable *parameters;
  guint64 tube_id;
  guint portnum = 0;
  gboolean close_;
  GError *error = NULL;

  SalutTubeIface *chan;

  /* after this point, the message is for us, so in all cases we either handle
   * it or send an error reply */

  if (!extract_tube_information (contact_repo, stanza, &close_, &tube_type,
          &initiator_handle, &service, &parameters, &tube_id, &portnum,
          &error))
    {
      WockyStanza *reply;
      GError err = { WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
                     error->message };

      reply = wocky_stanza_build_iq_error (stanza, NULL);
      wocky_stanza_error_to_node (&err, wocky_stanza_get_top_node (reply));

      wocky_porter_send (priv->conn->porter, reply);

      g_error_free (error);
      g_object_unref (reply);
      return TRUE;
    }

  DEBUG ("received a tube request, tube id %" G_GUINT64_FORMAT, tube_id);

  chan = g_hash_table_lookup (priv->tubes,
      GUINT_TO_POINTER (tube_id));

  if (close_)
  {
    if (chan != NULL)
      {
        DEBUG ("received a tube close message");
        salut_tube_iface_close (chan, TRUE);
      }
  }
  else
  {
    if (chan == NULL)
      {
        /* create new tube here */
        chan = create_new_tube (self, tube_type,
            initiator_handle, service, parameters, tube_id, portnum, stanza);
      }

    /* announce tube channel */
    tp_channel_manager_emit_new_channel (TP_CHANNEL_MANAGER (self),
        TP_EXPORTABLE_CHANNEL (chan), NULL);

    g_hash_table_unref (parameters);
  }

  return TRUE;
}

static void
salut_tubes_manager_close_all (SalutTubesManager *self)
{
  SalutTubesManagerPrivate *priv = SALUT_TUBES_MANAGER_GET_PRIVATE (self);

  DEBUG ("closing 1-1 tubes tubes channels");

  if (priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (priv->conn,
          priv->status_changed_id);
      priv->status_changed_id = 0;
    }

  tp_clear_pointer (&priv->tubes, g_hash_table_unref);
}

static void
connection_status_changed_cb (SalutConnection *conn,
                              guint status,
                              guint reason,
                              SalutTubesManager *self)
{
  switch (status)
    {
    case TP_CONNECTION_STATUS_DISCONNECTED:
      salut_tubes_manager_close_all (self);
      break;
    }
}

static GObject *
salut_tubes_manager_constructor (GType type,
                                  guint n_props,
                                  GObjectConstructParam *props)
{
  GObject *obj;
  SalutTubesManager *self;
  SalutTubesManagerPrivate *priv;

  obj = G_OBJECT_CLASS (salut_tubes_manager_parent_class)->
           constructor (type, n_props, props);

  self = SALUT_TUBES_MANAGER (obj);
  priv = SALUT_TUBES_MANAGER_GET_PRIVATE (self);

  priv->iq_tube_handler_id = wocky_porter_register_handler_from_anyone (
      priv->conn->porter, WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_SET, WOCKY_PORTER_HANDLER_PRIORITY_NORMAL,
      iq_tube_request_cb, self, NULL);

  priv->status_changed_id = g_signal_connect (priv->conn,
      "status-changed", (GCallback) connection_status_changed_cb, obj);

  return obj;
}

static void
salut_tubes_manager_dispose (GObject *object)
{
  SalutTubesManager *fac = SALUT_TUBES_MANAGER (object);
  SalutTubesManagerPrivate *priv =
    SALUT_TUBES_MANAGER_GET_PRIVATE (fac);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  salut_tubes_manager_close_all (fac);

  wocky_porter_unregister_handler (priv->conn->porter,
      priv->iq_tube_handler_id);
  priv->iq_tube_handler_id = 0;

  if (priv->contact_manager != NULL)
    {
      g_object_unref (priv->contact_manager);
      priv->contact_manager = NULL;
    }

  if (G_OBJECT_CLASS (salut_tubes_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_tubes_manager_parent_class)->dispose (
        object);
}

static void
salut_tubes_manager_get_property (GObject *object,
                                  guint property_id,
                                  GValue *value,
                                  GParamSpec *pspec)
{
  SalutTubesManager *fac = SALUT_TUBES_MANAGER (object);
  SalutTubesManagerPrivate *priv =
    SALUT_TUBES_MANAGER_GET_PRIVATE (fac);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_CONTACT_MANAGER:
        g_value_set_object (value, priv->contact_manager);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_tubes_manager_set_property (GObject *object,
                                  guint property_id,
                                  const GValue *value,
                                  GParamSpec *pspec)
{
  SalutTubesManager *fac = SALUT_TUBES_MANAGER (object);
  SalutTubesManagerPrivate *priv =
    SALUT_TUBES_MANAGER_GET_PRIVATE (fac);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      case PROP_CONTACT_MANAGER:
        priv->contact_manager = g_value_dup_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_tubes_manager_class_init (
    SalutTubesManagerClass *salut_tubes_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (
      salut_tubes_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_tubes_manager_class,
      sizeof (SalutTubesManagerPrivate));

  object_class->constructor = salut_tubes_manager_constructor;
  object_class->dispose = salut_tubes_manager_dispose;

  object_class->get_property = salut_tubes_manager_get_property;
  object_class->set_property = salut_tubes_manager_set_property;

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "Salut connection object that owns this Tubes channel factory object.",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_object (
      "contact-manager",
      "SalutContactManager object",
      "Salut Contact Manager associated with the Salut Connection of this "
      "manager",
      SALUT_TYPE_CONTACT_MANAGER,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONTACT_MANAGER,
      param_spec);
}


static void
salut_tubes_manager_foreach_channel (TpChannelManager *manager,
                                     TpExportableChannelFunc foreach,
                                     gpointer user_data)
{
  SalutTubesManager *fac = SALUT_TUBES_MANAGER (manager);
  SalutTubesManagerPrivate *priv =
    SALUT_TUBES_MANAGER_GET_PRIVATE (fac);
  GHashTableIter iter;
  gpointer value;

  g_hash_table_iter_init (&iter, priv->tubes);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      foreach (TP_EXPORTABLE_CHANNEL (value), user_data);
    }
}

static const gchar * const tubes_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetEntityType",
    NULL
};

static const gchar * const stream_tube_channel_allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    TP_PROP_CHANNEL_TYPE_STREAM_TUBE1_SERVICE,
    NULL
};

/* Temporarily disabled since the implementation is incomplete. */
#if 0
static const gchar * const dbus_tube_channel_allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    TP_PROP_CHANNEL_TYPE_DBUS_TUBE1_SERVICE_NAME,
    NULL
};
#endif

static void
salut_tubes_manager_type_foreach_channel_class (GType type,
    TpChannelManagerTypeChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table;
  GValue *value;

  /* 1-1 Channel.Type.StreamTube */
  table = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE1);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType",
      value);

  value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (value, TP_ENTITY_TYPE_CONTACT);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetEntityType",
      value);

  func (type, table, salut_tube_stream_channel_get_allowed_properties (),
      user_data);

  g_hash_table_unref (table);

  /* 1-1 Channel.Type.DBusTube */
  /* Temporarily disabled since the implementation is incomplete. */
#if 0
  table = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType",
      value);

  value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (value, TP_ENTITY_TYPE_CONTACT);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetEntityType",
      value);

  func (type, table, dbus_tube_channel_allowed_properties, user_data);

  g_hash_table_unref (table);
#endif
}

static SalutTubeIface *
salut_tubes_manager_lookup (SalutTubesManager *self,
    const gchar *type,
    TpHandle handle,
    const gchar *service)
{
  SalutTubesManagerPrivate *priv =
    SALUT_TUBES_MANAGER_GET_PRIVATE (self);
  GHashTableIter iter;
  gpointer value;

  g_hash_table_iter_init (&iter, priv->tubes);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      SalutTubeIface *tube = value;
      gboolean match = FALSE;

      gchar *channel_type, *channel_service;
      TpHandle channel_handle;

      g_object_get (tube,
          "channel-type", &channel_type,
          "handle", &channel_handle,
          "service", &channel_service,
          NULL);

      if (!tp_strdiff (type, channel_type)
          && handle == channel_handle
          && !tp_strdiff (service, channel_service))
        match = TRUE;

      g_free (channel_type);
      g_free (channel_service);

      if (match)
        return tube;
    }

  return NULL;
}

static void
channel_closed_cb (SalutTubeIface *tube,
    SalutTubesManager *self)
{
  SalutTubesManagerPrivate *priv =
    SALUT_TUBES_MANAGER_GET_PRIVATE (self);
  guint id;

  g_object_get (tube,
      "id", &id,
      NULL);

  tp_channel_manager_emit_channel_closed_for_object (TP_CHANNEL_MANAGER (self),
      TP_EXPORTABLE_CHANNEL (tube));

  if (priv->tubes != NULL)
    g_hash_table_remove (priv->tubes, GUINT_TO_POINTER (id));
}

static guint64
generate_tube_id (SalutTubesManager *self)
{
  SalutTubesManagerPrivate *priv =
    SALUT_TUBES_MANAGER_GET_PRIVATE (self);
  guint64 out;

  /* probably totally overkill */
  do
    {
      out = g_random_int_range (1, G_MAXINT32);
    }
  while (g_hash_table_lookup (priv->tubes,
          GUINT_TO_POINTER (out)) != NULL);

  return out;
}

static SalutTubeIface *
create_new_tube (SalutTubesManager *self,
    SalutTubeType type,
    TpHandle handle,
    const gchar *service,
    GHashTable *parameters,
    guint64 tube_id,
    guint portnum,
    WockyStanza *iq_req)
{
  SalutTubesManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_TUBES_MANAGER, SalutTubesManagerPrivate);
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->conn);
  TpHandle self_handle = tp_base_connection_get_self_handle (base_conn);
  SalutTubeIface *tube;

  if (type == SALUT_TUBE_TYPE_STREAM)
    {
      tube = SALUT_TUBE_IFACE (salut_tube_stream_new (priv->conn,
              handle, TP_ENTITY_TYPE_CONTACT,
              self_handle, self_handle, FALSE, service,
              parameters, tube_id, portnum, iq_req, TRUE));
    }
  else if (type == SALUT_TUBE_TYPE_DBUS)
    {
      tube = SALUT_TUBE_IFACE (salut_tube_dbus_new (priv->conn,
              handle, TP_ENTITY_TYPE_CONTACT, self_handle, NULL,
              self_handle, service, parameters, tube_id, TRUE));
    }
  else
    {
      g_return_val_if_reached (NULL);
    }

  tp_base_channel_register ((TpBaseChannel *) tube);

  g_signal_connect (tube, "closed",
      G_CALLBACK (channel_closed_cb), self);

  g_hash_table_insert (priv->tubes, GUINT_TO_POINTER (tube_id),
      tube);

  return tube;
}

/* Returns: (transfer none): new tube channel. the channel manager
 * holds the ref to this channel, so don't unref it! */
static SalutTubeIface *
new_channel_from_request (SalutTubesManager *self,
    GHashTable *request)
{
  SalutTubeIface *tube;

  SalutTubeType type;
  const gchar *ctype, *service;
  TpHandle handle;
  guint64 tube_id;
  GHashTable *parameters;

  ctype = tp_asv_get_string (request, TP_PROP_CHANNEL_CHANNEL_TYPE);
  handle = tp_asv_get_uint32 (request, TP_PROP_CHANNEL_TARGET_HANDLE, NULL);

  tube_id = generate_tube_id (self);

  /* requested tubes have an empty parameters dict */
  parameters = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) tp_g_value_slice_free);


  if (!tp_strdiff (ctype, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE1))
    {
      service = tp_asv_get_string (request,
          TP_PROP_CHANNEL_TYPE_STREAM_TUBE1_SERVICE);

      type = SALUT_TUBE_TYPE_STREAM;
    }
  else if (!tp_strdiff (ctype, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE1))
    {
      service = tp_asv_get_string (request,
          TP_PROP_CHANNEL_TYPE_DBUS_TUBE1_SERVICE_NAME);

      type = SALUT_TUBE_TYPE_DBUS;
    }
  else
    {
      g_return_val_if_reached (NULL);
    }

  tube = create_new_tube (self, type, handle, service,
      parameters, tube_id, 0, NULL);

  g_hash_table_unref (parameters);

  return tube;
}

static gboolean
salut_tubes_manager_requestotron (SalutTubesManager *self,
    TpChannelManagerRequest *request,
    GHashTable *request_properties,
    gboolean require_new)
{
  SalutTubesManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_TUBES_MANAGER, SalutTubesManagerPrivate);
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->conn);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      base_conn, TP_ENTITY_TYPE_CONTACT);
  TpHandle handle;
  GError *error = NULL;
  const gchar *channel_type;
  const gchar *service = NULL;
  SalutTubeIface *new_channel;
  GSList *tokens = NULL;

  if (tp_asv_get_uint32 (request_properties,
        TP_IFACE_CHANNEL ".TargetEntityType", NULL) != TP_ENTITY_TYPE_CONTACT)
    return FALSE;

  channel_type = tp_asv_get_string (request_properties,
            TP_IFACE_CHANNEL ".ChannelType");

  if (
  /* Temporarily disabled since the implementation is incomplete. */
  /*  tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE1) && */
      tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE1))
    return FALSE;

  if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE1))
    {
      if (tp_channel_manager_asv_has_unknown_properties (request_properties,
              tubes_channel_fixed_properties,
              salut_tube_stream_channel_get_allowed_properties (),
              &error))
        goto error;

      /* "Service" is a mandatory, not-fixed property */
      service = tp_asv_get_string (request_properties,
                TP_PROP_CHANNEL_TYPE_STREAM_TUBE1_SERVICE);
      if (service == NULL)
        {
          g_set_error (&error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
              "StreamTube requests must include '%s'",
              TP_PROP_CHANNEL_TYPE_STREAM_TUBE1_SERVICE);
          goto error;
        }
    }
/* Temporarily disabled since the implementation is incomplete. */
#if 0
  else if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE1))
    {
      GError *err = NULL;

      if (tp_channel_manager_asv_has_unknown_properties (request_properties,
              tubes_channel_fixed_properties,
              dbus_tube_channel_allowed_properties,
              &error))
        goto error;

      /* "ServiceName" is a mandatory, not-fixed property */
      service = tp_asv_get_string (request_properties,
                TP_IFACE_CHANNEL_TYPE_DBUS_TUBE1_SERVICE_NAME);
      if (service == NULL)
        {
          g_set_error (&error, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
              "Request missed a mandatory property '%s'",
              TP_IFACE_CHANNEL_TYPE_DBUS_TUBE1_SERVICE_NAME);
          goto error;
        }

      if (!tp_dbus_check_valid_bus_name (service, TP_DBUS_NAME_TYPE_WELL_KNOWN,
            &err))
        {
          g_set_error (&error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
              "Invalid ServiceName: %s", err->message);
          g_error_free (err);
          goto error;
        }
    }
#endif

  handle = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandle", NULL);

  if (!tp_handle_is_valid (contact_repo, handle, &error))
    goto error;

  /* Don't support opening a channel to our self handle */
  if (handle == tp_base_connection_get_self_handle (base_conn))
    {
      g_set_error (&error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "Can't open a channel to your self handle");
      goto error;
    }

  new_channel = salut_tubes_manager_lookup (self, channel_type,
      handle, service);

  if (new_channel == NULL)
    {
      new_channel = new_channel_from_request (self,
          request_properties);
      g_assert (new_channel != NULL);

      if (request != NULL)
        tokens = g_slist_prepend (NULL, request);

      tp_channel_manager_emit_new_channel (TP_CHANNEL_MANAGER (self),
          TP_EXPORTABLE_CHANNEL (new_channel), tokens);

      g_slist_free (tokens);
    }
  else
    {
      if (require_new)
        {
          g_set_error (&error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
              "A channel to #%u (service: %s) is already open",
              handle, service);
          goto error;
        }

      tp_channel_manager_emit_request_already_satisfied (
          TP_CHANNEL_MANAGER (self), request,
          TP_EXPORTABLE_CHANNEL (new_channel));
    }

  return TRUE;

error:
  tp_channel_manager_emit_request_failed (TP_CHANNEL_MANAGER (self), request,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}

static gboolean
salut_tubes_manager_create_channel (TpChannelManager *manager,
    TpChannelManagerRequest *request,
    GHashTable *request_properties)
{
  SalutTubesManager *self = SALUT_TUBES_MANAGER (manager);

  return salut_tubes_manager_requestotron (self, request,
      request_properties, TRUE);
}

SalutTubesManager *
salut_tubes_manager_new (
    SalutConnection *conn,
    SalutContactManager *contact_manager)
{
  g_return_val_if_fail (SALUT_IS_CONNECTION (conn), NULL);
  g_return_val_if_fail (SALUT_IS_CONTACT_MANAGER (contact_manager), NULL);

  return g_object_new (
      SALUT_TYPE_TUBES_MANAGER,
      "connection", conn,
      "contact-manager", contact_manager,
      NULL);
}

static void
salut_tubes_manager_iface_init (gpointer g_iface,
                                 gpointer iface_data)
{
  TpChannelManagerIface *iface = (TpChannelManagerIface *) g_iface;

  iface->foreach_channel = salut_tubes_manager_foreach_channel;
  iface->type_foreach_channel_class =
    salut_tubes_manager_type_foreach_channel_class;
  iface->create_channel = salut_tubes_manager_create_channel;
}

static void
add_service_to_array (const gchar *service,
                      GPtrArray *arr,
                      SalutTubeType type)
{
  GValue monster = {0, };
  GHashTable *fixed_properties;
  GValue *channel_type_value;
  GValue *target_entity_type_value;
  gchar *tube_allowed_properties[] =
      {
        TP_IFACE_CHANNEL ".TargetHandle",
        NULL
      };

  g_assert (type == SALUT_TUBE_TYPE_STREAM || type == SALUT_TUBE_TYPE_DBUS);

  g_value_init (&monster, TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS);
  g_value_take_boxed (&monster,
      dbus_g_type_specialized_construct (
        TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS));

  fixed_properties = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  channel_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  if (type == SALUT_TUBE_TYPE_STREAM)
    g_value_set_static_string (channel_type_value,
        TP_IFACE_CHANNEL_TYPE_STREAM_TUBE1);
  else
    g_value_set_static_string (channel_type_value,
        TP_IFACE_CHANNEL_TYPE_DBUS_TUBE1);
  g_hash_table_insert (fixed_properties, TP_IFACE_CHANNEL ".ChannelType",
      channel_type_value);

  target_entity_type_value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (target_entity_type_value, TP_ENTITY_TYPE_CONTACT);
  g_hash_table_insert (fixed_properties,
      TP_IFACE_CHANNEL ".TargetEntityType", target_entity_type_value);

  target_entity_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_string (target_entity_type_value, service);
  if (type == SALUT_TUBE_TYPE_STREAM)
    g_hash_table_insert (fixed_properties,
        TP_PROP_CHANNEL_TYPE_STREAM_TUBE1_SERVICE,
        target_entity_type_value);
  else
    g_hash_table_insert (fixed_properties,
        TP_PROP_CHANNEL_TYPE_DBUS_TUBE1_SERVICE_NAME,
        target_entity_type_value);

  dbus_g_type_struct_set (&monster,
      0, fixed_properties,
      1, tube_allowed_properties,
      G_MAXUINT);

  g_hash_table_unref (fixed_properties);

  g_ptr_array_add (arr, g_value_get_boxed (&monster));
}

static void
add_generic_tube_caps (GPtrArray *arr)
{
  GValue monster1 = {0,};
  GHashTable *fixed_properties;
  GValue *channel_type_value;
  GValue *target_entity_type_value;

  /* StreamTube */
  g_value_init (&monster1, TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS);
  g_value_take_boxed (&monster1,
      dbus_g_type_specialized_construct (
        TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS));

  fixed_properties = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);

  channel_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (channel_type_value,
      TP_IFACE_CHANNEL_TYPE_STREAM_TUBE1);

  g_hash_table_insert (fixed_properties, TP_IFACE_CHANNEL ".ChannelType",
      channel_type_value);

  target_entity_type_value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (target_entity_type_value, TP_ENTITY_TYPE_CONTACT);
  g_hash_table_insert (fixed_properties,
      TP_IFACE_CHANNEL ".TargetEntityType", target_entity_type_value);

  dbus_g_type_struct_set (&monster1,
      0, fixed_properties,
      1, stream_tube_channel_allowed_properties,
      G_MAXUINT);

  g_hash_table_unref (fixed_properties);
  g_ptr_array_add (arr, g_value_get_boxed (&monster1));

  /* FIXME: enable once D-Bus tube new API are implemented */
#if 0
  /* DBusTube */
  g_value_init (&monster2, TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS);
  g_value_take_boxed (&monster2,
      dbus_g_type_specialized_construct (
        TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS));

  fixed_properties = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);

  channel_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (channel_type_value,
      SALUT_IFACE_CHANNEL_TYPE_DBUS_TUBE);

  g_hash_table_insert (fixed_properties, TP_IFACE_CHANNEL ".ChannelType",
      channel_type_value);

  target_entity_type_value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (target_entity_type_value, TP_ENTITY_TYPE_CONTACT);
  g_hash_table_insert (fixed_properties,
      TP_IFACE_CHANNEL ".TargetEntityType", target_entity_type_value);

  dbus_g_type_struct_set (&monster2,
      0, fixed_properties,
      1, gabble_tube_dbus_channel_get_allowed_properties (),
      G_MAXUINT);

  g_hash_table_unref (fixed_properties);
  g_ptr_array_add (arr, g_value_get_boxed (&monster2));
#endif
}

#define STREAM_CAP_PREFIX (WOCKY_TELEPATHY_NS_TUBES "/stream#")
#define DBUS_CAP_PREFIX (WOCKY_TELEPATHY_NS_TUBES "/dbus#")

typedef struct {
    gboolean supports_tubes;
    GPtrArray *arr;
    TpHandle handle;
} GetContactCapsClosure;

static void
get_contact_caps_foreach (gpointer data,
    gpointer user_data)
{
  const gchar *ns = data;
  GetContactCapsClosure *closure = user_data;

  if (!g_str_has_prefix (ns, WOCKY_TELEPATHY_NS_TUBES))
    return;

  closure->supports_tubes = TRUE;

  if (g_str_has_prefix (ns, STREAM_CAP_PREFIX))
    add_service_to_array (ns + strlen (STREAM_CAP_PREFIX), closure->arr,
        SALUT_TUBE_TYPE_STREAM);
  else if (g_str_has_prefix (ns, DBUS_CAP_PREFIX))
    add_service_to_array (ns + strlen (DBUS_CAP_PREFIX), closure->arr,
        SALUT_TUBE_TYPE_DBUS);
}

static void
salut_tubes_manager_get_contact_caps_from_set (
    GabbleCapsChannelManager *iface,
    TpHandle handle,
    const GabbleCapabilitySet *caps,
    GPtrArray *arr)
{
  SalutTubesManager *self = SALUT_TUBES_MANAGER (iface);
  SalutTubesManagerPrivate *priv = SALUT_TUBES_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *base = TP_BASE_CONNECTION (priv->conn);
  GetContactCapsClosure closure = { FALSE, arr, handle };

  /* Always claim that we support tubes. */
  closure.supports_tubes = (handle == tp_base_connection_get_self_handle (base));

  gabble_capability_set_foreach (caps, get_contact_caps_foreach, &closure);

  if (closure.supports_tubes)
    add_generic_tube_caps (arr);
}

/* stolen directly from Gabble... */
static void
gabble_private_tubes_factory_add_cap (GabbleCapsChannelManager *manager,
    const gchar *client_name,
    GHashTable *cap,
    GabbleCapabilitySet *cap_set)
{
  const gchar *channel_type, *service;
  gchar *ns = NULL;

  channel_type = tp_asv_get_string (cap,
            TP_IFACE_CHANNEL ".ChannelType");

  /* this channel is not for this factory */
  if (tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE1) &&
      tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE1))
    return;

  if (tp_asv_get_uint32 (cap,
        TP_IFACE_CHANNEL ".TargetEntityType", NULL) != TP_ENTITY_TYPE_CONTACT)
    return;

  if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE1))
    {
      service = tp_asv_get_string (cap,
          TP_PROP_CHANNEL_TYPE_STREAM_TUBE1_SERVICE);

      if (service != NULL)
        ns = g_strconcat (STREAM_CAP_PREFIX, service, NULL);
    }
  else if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE1))
    {
      service = tp_asv_get_string (cap,
          TP_PROP_CHANNEL_TYPE_DBUS_TUBE1_SERVICE_NAME);

      if (service != NULL)
        ns = g_strconcat (DBUS_CAP_PREFIX, service, NULL);
    }

  if (ns != NULL)
    {
      DEBUG ("%s: adding capability %s", client_name, ns);
      gabble_capability_set_add (cap_set, ns);
      g_free (ns);
    }
}

static void
salut_tubes_manager_represent_client (
    GabbleCapsChannelManager *iface,
    const gchar *client_name,
    const GPtrArray *filters,
    const gchar * const *cap_tokens G_GNUC_UNUSED,
    GabbleCapabilitySet *cap_set,
    GPtrArray *data_forms)
{
  guint i;

  for (i = 0; i < filters->len; i++)
    {
      gabble_private_tubes_factory_add_cap (iface, client_name,
          g_ptr_array_index (filters, i), cap_set);
    }
}

static void
gabble_caps_channel_manager_iface_init (GabbleCapsChannelManagerIface *iface)
{
  iface->get_contact_caps = salut_tubes_manager_get_contact_caps_from_set;
  iface->represent_client = salut_tubes_manager_represent_client;
}
