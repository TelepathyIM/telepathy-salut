/*
 * salut-tubes-channel.c - Source for SalutTubesChannel
 * Copyright (C) 2007 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the tubesplied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "salut-tubes-channel.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>

#include <dbus/dbus-glib.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/svc-channel.h>
#include <telepathy-glib/svc-generic.h>

#include <gibber/gibber-muc-connection.h>
#include <gibber/gibber-bytestream-muc.h>
#include <gibber/gibber-xmpp-stanza.h>
#include <gibber/gibber-namespaces.h>
#include <gibber/gibber-xmpp-error.h>
#include <gibber/gibber-iq-helper.h>

#define DEBUG_FLAG DEBUG_TUBES
#include "debug.h"
#include "salut-util.h"
#include "salut-connection.h"
#include "salut-contact.h"
#include "salut-muc-channel.h"
#include "salut-xmpp-connection-manager.h"
#include "salut-direct-bytestream-manager.h"
#include "tube-iface.h"
#include "tube-dbus.h"
#include "tube-stream.h"

#define SALUT_CHANNEL_TUBE_TYPE \
    (dbus_g_type_get_struct ("GValueArray", \
        G_TYPE_UINT, \
        G_TYPE_UINT, \
        G_TYPE_UINT, \
        G_TYPE_STRING, \
        dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), \
        G_TYPE_UINT, \
        G_TYPE_INVALID))

#define DBUS_NAME_PAIR_TYPE \
    (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID))

static void channel_iface_init (gpointer g_iface, gpointer iface_data);
static void tubes_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (SalutTubesChannel, salut_tubes_channel, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
      tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_TUBES, tubes_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
        tp_external_group_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL);
);

static void xmpp_connection_manager_new_connection_cb (
    SalutXmppConnectionManager *mgr, GibberXmppConnection *conn,
    SalutContact *contact, gpointer user_data);

static void xmpp_connection_manager_connection_closed_cb (
    SalutXmppConnectionManager *mgr, GibberXmppConnection *conn,
    SalutContact *contact, gpointer user_data);

static void xmpp_connection_manager_connection_closing_cb (
    SalutXmppConnectionManager *mgr, GibberXmppConnection *conn,
    SalutContact *contact, gpointer user_data);

static void send_channel_iq_tubes (SalutTubesChannel *self);

/* Channel state */
typedef enum
{
  CHANNEL_NOT_CONNECTED = 0,
  CHANNEL_CONNECTING,
  CHANNEL_CONNECTED,
  CHANNEL_CLOSING,
} ChannelState;

/* properties */
static const char *salut_tubes_channel_interfaces[] = {
  TP_IFACE_CHANNEL_INTERFACE_GROUP,
  /* If more interfaces are added, either keep Group as the first, or change
   * the implementations of salut_tubes_channel_get_interfaces () and
   * salut_tubes_channel_get_property () too */
  NULL
};

enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_CONNECTION,
  PROP_MUC,
  PROP_INTERFACES,
  PROP_TARGET_ID,
  PROP_REQUESTED,
  PROP_INITIATOR_ID,
  PROP_INITIATOR_HANDLE,
  PROP_CHANNEL_DESTROYED,
  PROP_CHANNEL_PROPERTIES,

  /* only for 1-1 tubes */
  PROP_CONTACT,
  PROP_XMPP_CONNECTION_MANAGER,

  LAST_PROPERTY
};

/* private structure */
typedef struct _SalutTubesChannelPrivate SalutTubesChannelPrivate;

struct _SalutTubesChannelPrivate
{
  SalutConnection *conn;
  gchar *object_path;
  TpHandle handle;
  TpHandleType handle_type;
  TpHandle self_handle;
  TpHandle initiator;
  /* Used for MUC tubes channel only */
  GibberMucConnection *muc_connection;

  /* Used for 1-1 tubes channel */
  SalutContact *contact;
  GibberXmppConnection *xmpp_connection;
  SalutXmppConnectionManager *xmpp_connection_manager;
  ChannelState state;
  GibberIqHelper *iq_helper;

  /* guint tube_id -> SalutTubeDBus tube */
  GHashTable *tubes;

  gboolean closed;
  gboolean dispose_has_run;
};

#define SALUT_TUBES_CHANNEL_GET_PRIVATE(obj) \
  ((SalutTubesChannelPrivate *) ((SalutTubesChannel *)obj)->priv)

static gboolean update_tubes_info (SalutTubesChannel *self);
static void muc_connection_lost_senders_cb (GibberMucConnection *conn,
    GArray *senders, gpointer user_data);
static void muc_connection_new_senders_cb (GibberMucConnection *conn,
    GArray *senders, gpointer user_data);
static gboolean extract_tube_information (SalutTubesChannel *self,
    GibberXmppNode *tube_node, TpTubeType *type, TpHandle *initiator_handle,
    const gchar **service, GHashTable **parameters, guint *tube_id);
static SalutTubeIface * create_new_tube (SalutTubesChannel *self,
    TpTubeType type, TpHandle initiator, const gchar *service,
    GHashTable *parameters, guint tube_id, guint portnum,
    GibberIqHelperRequestStanza *iq_req);

static void
salut_tubes_channel_init (SalutTubesChannel *self)
{
  SalutTubesChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_TUBES_CHANNEL, SalutTubesChannelPrivate);

  self->priv = priv;

  priv->tubes = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) g_object_unref);

  priv->contact = NULL;
  priv->xmpp_connection = NULL;
  priv->state = CHANNEL_NOT_CONNECTED;
  priv->xmpp_connection_manager = NULL;

  priv->dispose_has_run = FALSE;
  priv->closed = FALSE;
}

static GObject *
salut_tubes_channel_constructor (GType type,
                                 guint n_props,
                                 GObjectConstructParam *props)
{
  GObject *obj;
  SalutTubesChannel *self;
  SalutTubesChannelPrivate *priv;
  DBusGConnection *bus;
  TpHandleRepoIface *handle_repo;

  obj = G_OBJECT_CLASS (salut_tubes_channel_parent_class)->
        constructor (type, n_props, props);

  self = SALUT_TUBES_CHANNEL (obj);
  priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  g_assert (priv->conn != NULL);
  handle_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, priv->handle_type);

  tp_handle_ref (handle_repo, priv->handle);

  switch (priv->handle_type)
    {
    case TP_HANDLE_TYPE_CONTACT:
      g_assert (self->muc == NULL);
      g_assert (priv->xmpp_connection_manager != NULL);
      priv->self_handle = ((TpBaseConnection *)
          (priv->conn))->self_handle;
      g_signal_connect (priv->xmpp_connection_manager, "new-connection",
          G_CALLBACK (xmpp_connection_manager_new_connection_cb), obj);
      break;

    case TP_HANDLE_TYPE_ROOM:
      g_assert (self->muc != NULL);
      g_assert (priv->xmpp_connection_manager == NULL);
      priv->self_handle = self->muc->group.self_handle;
      tp_external_group_mixin_init (obj, (GObject *) self->muc);
      g_object_get (self->muc,
          "muc-connection", &(priv->muc_connection),
          NULL);
      g_assert (priv->muc_connection != NULL);

      g_signal_connect (priv->muc_connection, "new-senders",
          G_CALLBACK (muc_connection_new_senders_cb), self);
      g_signal_connect (priv->muc_connection, "lost-senders",
          G_CALLBACK (muc_connection_lost_senders_cb), self);

      break;
    default:
      g_assert_not_reached ();
    }

  /* Connect to the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  DEBUG ("Registering at '%s'", priv->object_path);

  return obj;
}

static void
salut_tubes_channel_get_property (GObject *object,
                                  guint property_id,
                                  GValue *value,
                                  GParamSpec *pspec)
{
  SalutTubesChannel *chan = SALUT_TUBES_CHANNEL (object);
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (chan);

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_value_set_string (value, priv->object_path);
        break;
      case PROP_CHANNEL_TYPE:
        g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_TUBES);
        break;
      case PROP_HANDLE_TYPE:
        g_value_set_uint (value, priv->handle_type);
        break;
      case PROP_HANDLE:
        g_value_set_uint (value, priv->handle);
        break;
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_MUC:
        g_value_set_object (value, chan->muc);
        break;
      case PROP_CONTACT:
        g_value_set_object (value, priv->contact);
        break;
      case PROP_XMPP_CONNECTION_MANAGER:
        g_value_set_object (value, priv->xmpp_connection_manager);
      case PROP_INTERFACES:
        if (chan->muc)
          g_value_set_static_boxed (value, salut_tubes_channel_interfaces);
        else
          g_value_set_static_boxed (value, salut_tubes_channel_interfaces + 1);
        break;
      case PROP_TARGET_ID:
        {
           TpHandleRepoIface *repo = tp_base_connection_get_handles (
             (TpBaseConnection *) priv->conn, priv->handle_type);

           g_value_set_string (value, tp_handle_inspect (repo, priv->handle));
        }
        break;
      case PROP_INITIATOR_HANDLE:
        g_assert (priv->initiator != 0);
        g_value_set_uint (value, priv->initiator);
        break;
      case PROP_INITIATOR_ID:
        {
          TpHandleRepoIface *repo = tp_base_connection_get_handles (
              (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

          g_assert (priv->initiator != 0);
          g_value_set_string (value, tp_handle_inspect (repo, priv->handle));
        }
        break;
      case PROP_REQUESTED:
        g_value_set_boolean (value, (priv->initiator != priv->self_handle));
        break;
      case PROP_CHANNEL_DESTROYED:
        g_value_set_boolean (value, priv->closed);
        break;
      case PROP_CHANNEL_PROPERTIES:
        g_value_take_boxed (value,
            tp_dbus_properties_mixin_make_properties_hash (object,
                TP_IFACE_CHANNEL, "TargetHandle",
                TP_IFACE_CHANNEL, "TargetHandleType",
                TP_IFACE_CHANNEL, "ChannelType",
                TP_IFACE_CHANNEL, "TargetID",
                TP_IFACE_CHANNEL, "InitiatorHandle",
                TP_IFACE_CHANNEL, "InitiatorID",
                TP_IFACE_CHANNEL, "Requested",
                NULL));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_tubes_channel_set_property (GObject *object,
                                  guint property_id,
                                  const GValue *value,
                                  GParamSpec *pspec)
{
  SalutTubesChannel *chan = SALUT_TUBES_CHANNEL (object);
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (chan);
  const gchar *value_str;

  switch (property_id)
    {
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
              TP_IFACE_CHANNEL_TYPE_TUBES));
        break;
      case PROP_HANDLE_TYPE:
        priv->handle_type = g_value_get_uint (value);
        break;
      case PROP_HANDLE:
        priv->handle = g_value_get_uint (value);
        break;
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      case PROP_MUC:
        chan->muc = g_value_get_object (value);
        break;
      case PROP_CONTACT:
        priv->contact = g_value_get_object (value);
        /* contact is set only for 1-1 tubes */
        if (priv->contact != NULL)
          g_object_ref (priv->contact);
        break;
      case PROP_XMPP_CONNECTION_MANAGER:
        priv->xmpp_connection_manager = g_value_get_object (value);
        /* xmpp_connection_manager is set only for 1-1 tubes */
        if (priv->xmpp_connection_manager != NULL)
          g_object_ref (priv->xmpp_connection_manager);
        break;
      case PROP_INITIATOR_HANDLE:
        priv->initiator = g_value_get_uint (value);
        g_assert (priv->initiator != 0);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
initialise_connection (SalutTubesChannel *self, GibberXmppConnection *conn)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  g_assert (conn != NULL);
  priv->xmpp_connection = conn;
  g_object_ref (priv->xmpp_connection);

  g_assert (
     (priv->xmpp_connection->stream_flags &
       ~(GIBBER_XMPP_CONNECTION_STREAM_FULLY_OPEN
         |GIBBER_XMPP_CONNECTION_CLOSE_SENT)) == 0);

  g_signal_handlers_disconnect_matched (priv->xmpp_connection_manager,
      G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
  g_signal_connect (priv->xmpp_connection_manager, "connection-closed",
      G_CALLBACK (xmpp_connection_manager_connection_closed_cb), self);
  g_signal_connect (priv->xmpp_connection_manager, "connection-closing",
      G_CALLBACK (xmpp_connection_manager_connection_closing_cb), self);

  if (priv->xmpp_connection->stream_flags
        & GIBBER_XMPP_CONNECTION_CLOSE_SENT) {
    priv->state = CHANNEL_CLOSING;
    DEBUG ("priv->state = CHANNEL_CLOSING");
  } else {
    priv->state = CHANNEL_CONNECTED;
    DEBUG ("priv->state = CHANNEL_CONNECTED");
    send_channel_iq_tubes (self);
  }
}

static void
xmpp_connection_manager_new_connection_cb (SalutXmppConnectionManager *mgr,
                                           GibberXmppConnection *conn,
                                           SalutContact *contact,
                                           gpointer user_data)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (user_data);
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  if (contact != priv->contact)
    /* This new connection is not for this channel */
    return;

  DEBUG ("pending connection fully open");

  initialise_connection (self, conn);
}

static void
connection_disconnected (SalutTubesChannel *self)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  if (priv->xmpp_connection != NULL)
    {
      DEBUG ("connection closed.");

      g_object_unref (priv->xmpp_connection);
      priv->xmpp_connection = NULL;
    }

  g_signal_handlers_disconnect_matched (priv->xmpp_connection_manager,
      G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);

  priv->state = CHANNEL_NOT_CONNECTED;

  g_signal_connect (priv->xmpp_connection_manager, "new-connection",
      G_CALLBACK (xmpp_connection_manager_new_connection_cb), self);

  /* If some tubes in remote-pending state, reopen the xmpp connection?
  if (g_queue_get_length (priv->out_queue) > 0) {
    setup_connection (self);
  }
  */
}

static void
xmpp_connection_manager_connection_closed_cb (SalutXmppConnectionManager *mgr,
                                              GibberXmppConnection *conn,
                                              SalutContact *contact,
                                              gpointer user_data)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (user_data);
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  if (priv->contact != contact)
    return;

  g_assert (priv->xmpp_connection == conn);
  connection_disconnected (self);
}

static void
xmpp_connection_manager_connection_closing_cb (SalutXmppConnectionManager *mgr,
                                               GibberXmppConnection *conn,
                                               SalutContact *contact,
                                               gpointer user_data)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (user_data);
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  if (priv->contact != contact)
    return;

  DEBUG ("connection closing");
  g_assert (priv->xmpp_connection == conn);
  priv->state = CHANNEL_CLOSING;
}

static void
setup_connection (SalutTubesChannel *self)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  SalutXmppConnectionManagerRequestConnectionResult result;
  GibberXmppConnection *conn = NULL;

  DEBUG ("called. state=%d", priv->state);

  if (priv->state == CHANNEL_CONNECTING)
    return;

  g_assert (priv->xmpp_connection == NULL);

  result = salut_xmpp_connection_manager_request_connection (
      priv->xmpp_connection_manager, priv->contact, &conn, NULL);

  if (result == SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_DONE)
    {
      DEBUG ("connection done.");
      initialise_connection (self, conn);
    }
  else if (result ==
      SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_PENDING)
    {
      DEBUG ("Requested connection pending");
      priv->state = CHANNEL_CONNECTING;
      return;
    }
  else
    {
      priv->state = CHANNEL_NOT_CONNECTED;
      return;
    }
}

static void
d_bus_names_changed_added (SalutTubesChannel *self,
                           guint tube_id,
                           TpHandle contact,
                           const gchar *new_name)
{
  GPtrArray *added = g_ptr_array_sized_new (1);
  GArray *removed = g_array_new (FALSE, FALSE, sizeof (guint));
  GValue tmp = {0,};
  guint i;

  g_value_init (&tmp, DBUS_NAME_PAIR_TYPE);
  g_value_take_boxed (&tmp,
      dbus_g_type_specialized_construct (DBUS_NAME_PAIR_TYPE));
  dbus_g_type_struct_set (&tmp,
      0, contact,
      1, new_name,
      G_MAXUINT);
  g_ptr_array_add (added, g_value_get_boxed (&tmp));

  tp_svc_channel_type_tubes_emit_d_bus_names_changed (self,
      tube_id, added, removed);

  for (i = 0; i < added->len; i++)
    g_boxed_free (DBUS_NAME_PAIR_TYPE, added->pdata[i]);
  g_ptr_array_free (added, TRUE);
  g_array_free (removed, TRUE);
}

static void
d_bus_names_changed_removed (SalutTubesChannel *self,
                             guint tube_id,
                             TpHandle contact)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  GPtrArray *added = g_ptr_array_new ();
  GArray *removed = g_array_new (FALSE, FALSE, sizeof (guint));

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    return;

  g_array_append_val (removed, contact);

  tp_svc_channel_type_tubes_emit_d_bus_names_changed (self,
      tube_id, added, removed);

  g_ptr_array_free (added, TRUE);
  g_array_free (removed, TRUE);
}

static void
add_name_in_dbus_names (SalutTubesChannel *self,
                        guint tube_id,
                        TpHandle handle,
                        const gchar *dbus_name)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  SalutTubeDBus *tube;

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    return;

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));
  if (tube == NULL)
    return;

  if (salut_tube_dbus_add_name (tube, handle, dbus_name))
    {
      /* Emit the DBusNamesChanged signal */
      d_bus_names_changed_added (self, tube_id, handle, dbus_name);
    }
}

static void
add_yourself_in_dbus_names (SalutTubesChannel *self,
                            guint tube_id)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  SalutTubeDBus *tube;
  gchar *dbus_name;

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    return;

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));
  if (tube == NULL)
    return;

  g_object_get (tube,
      "dbus-name", &dbus_name,
      NULL);

  add_name_in_dbus_names (self, tube_id, priv->self_handle, dbus_name);

  g_free (dbus_name);
}

/**
 * salut_tubes_channel_get_available_tube_types
 *
 * Implements D-Bus method GetAvailableTubeTypes
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_get_available_tube_types (TpSvcChannelTypeTubes *iface,
                                              DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  GArray *ret;
  TpTubeType type;

  g_assert (SALUT_IS_TUBES_CHANNEL (self));

  ret = g_array_sized_new (FALSE, FALSE, sizeof (TpTubeType), 1);
  type = TP_TUBE_TYPE_DBUS;
  g_array_append_val (ret, type);
  type = TP_TUBE_TYPE_STREAM;
  g_array_append_val (ret, type);

  tp_svc_channel_type_tubes_return_from_get_available_tube_types (context,
      ret);

  g_array_free (ret, TRUE);
}

struct _add_in_old_dbus_tubes_data
{
  GHashTable *old_dbus_tubes;
  TpHandle contact;
};

static void
add_in_old_dbus_tubes (gpointer key,
                       gpointer value,
                       gpointer user_data)
{
  guint tube_id = GPOINTER_TO_UINT (key);
  SalutTubeIface *tube = SALUT_TUBE_IFACE (value);
  struct _add_in_old_dbus_tubes_data *data =
    (struct _add_in_old_dbus_tubes_data *) user_data;
  TpTubeType type;

  g_object_get (tube, "type", &type, NULL);

  if (type != TP_TUBE_TYPE_DBUS)
    return;

  if (salut_tube_dbus_handle_in_names (SALUT_TUBE_DBUS (tube),
        data->contact))
    {
      /* contact was in this tube */
      g_hash_table_insert (data->old_dbus_tubes, GUINT_TO_POINTER (tube_id),
          tube);
    }
}

struct
emit_d_bus_names_changed_foreach_data
{
  SalutTubesChannel *self;
  TpHandle contact;
};

static void
emit_d_bus_names_changed_foreach (gpointer key,
                                  gpointer value,
                                  gpointer user_data)
{
  guint tube_id = GPOINTER_TO_UINT (key);
  SalutTubeDBus *tube = SALUT_TUBE_DBUS (value);
  struct emit_d_bus_names_changed_foreach_data *data =
    (struct emit_d_bus_names_changed_foreach_data *) user_data;
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (
      data->self);

  if (salut_tube_dbus_remove_name (tube, data->contact))
    {
      /* Emit the DBusNamesChanged signal */
      d_bus_names_changed_removed (data->self, tube_id, data->contact);
    }

  /* Remove the contact as sender in the muc bytestream */
  if (priv->handle_type == TP_HANDLE_TYPE_ROOM)
    {
      GibberBytestreamIface *bytestream;

      g_object_get (tube, "bytestream", &bytestream, NULL);
      g_assert (bytestream != NULL);

      if (GIBBER_IS_BYTESTREAM_MUC (bytestream))
        {
          TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
              (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
          const gchar *sender;

          sender = tp_handle_inspect (contact_repo, data->contact);
          if (sender != NULL)
            gibber_bytestream_muc_remove_sender (
                GIBBER_BYTESTREAM_MUC (bytestream), sender);
        }

      g_object_unref (bytestream);
    }
}

/* MUC message */
void
salut_tubes_channel_muc_message_received (SalutTubesChannel *self,
                                          const gchar *sender,
                                          GibberXmppStanza *stanza)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle contact;
  GibberXmppNode *tubes_node;
  GSList *l;
  GHashTable *old_dbus_tubes;
  struct _add_in_old_dbus_tubes_data add_data;
  struct emit_d_bus_names_changed_foreach_data emit_data;
  GibberStanzaType type;
  GibberStanzaSubType sub_type;

  contact = tp_handle_lookup (contact_repo, sender, NULL, NULL);
  g_assert (contact != 0);

  if (contact == priv->self_handle)
    /* We don't need to inspect our own tubes */
    return;

  gibber_xmpp_stanza_get_type_info (stanza, &type, &sub_type);
  if (type != GIBBER_STANZA_TYPE_MESSAGE
      || sub_type != GIBBER_STANZA_SUB_TYPE_GROUPCHAT)
    return;

  tubes_node = gibber_xmpp_node_get_child_ns (stanza->node, "tubes",
      GIBBER_TELEPATHY_NS_TUBES);
  g_assert (tubes_node != NULL);

  /* Fill old_dbus_tubes with D-BUS tubes previoulsy announced by
   * the contact */
  old_dbus_tubes = g_hash_table_new (g_direct_hash, g_direct_equal);
  add_data.old_dbus_tubes = old_dbus_tubes;
  add_data.contact = contact;
  g_hash_table_foreach (priv->tubes, add_in_old_dbus_tubes, &add_data);

  for (l = tubes_node->children; l != NULL; l = l->next)
    {
      GibberXmppNode *tube_node = (GibberXmppNode *) l->data;
      const gchar *stream_id;
      SalutTubeIface *tube;
      guint tube_id;
      TpTubeType type;

      stream_id = gibber_xmpp_node_get_attribute (tube_node, "stream-id");

      extract_tube_information (self, tube_node, NULL,
          NULL, NULL, NULL, &tube_id);
      tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));

      if (tube == NULL)
        {
          /* We don't know yet this tube */
          const gchar *service;
          TpTubeType type;
          TpHandle initiator_handle;
          GHashTable *parameters;
          guint tube_id;

          if (extract_tube_information (self, tube_node, &type,
                &initiator_handle, &service, &parameters, &tube_id))
            {
              switch (type)
                {
                  case TP_TUBE_TYPE_DBUS:
                    {
                      if (initiator_handle == 0)
                        {
                          DEBUG ("D-Bus tube initiator missing");
                          /* skip to the next child of <tubes> */
                          continue;
                        }
                    }
                    break;
                  case TP_TUBE_TYPE_STREAM:
                    {
                      if (initiator_handle != 0)
                        /* ignore it */
                        tp_handle_unref (contact_repo, initiator_handle);

                      initiator_handle = contact;
                      tp_handle_ref (contact_repo, initiator_handle);
                    }
                    break;
                  default:
                    {
                      g_assert_not_reached ();
                    }
                }

              tube = create_new_tube (self, type, initiator_handle,
                  service, parameters, tube_id, 0, NULL);

              /* the tube has reffed its initiator, no need to keep a ref */
              tp_handle_unref (contact_repo, initiator_handle);
            }
        }
      else
        {
          /* The contact is in the tube.
           * Remove it from old_dbus_tubes if needed */
          g_hash_table_remove (old_dbus_tubes, GUINT_TO_POINTER (tube_id));
        }

      if (tube == NULL)
        continue;

      g_object_get (tube, "type", &type, NULL);

      if (type == TP_TUBE_TYPE_DBUS)
        {
          /* Update mapping of handle -> D-Bus name. */
          if (!salut_tube_dbus_handle_in_names (SALUT_TUBE_DBUS (tube),
                contact))
            {
              /* Contact just joined the tube */
              const gchar *new_name;

              new_name = gibber_xmpp_node_get_attribute (tube_node,
                  "dbus-name");

              if (!new_name)
                {
                  DEBUG ("Contact %u isn't announcing their D-Bus name",
                         contact);
                  continue;
                }

              add_name_in_dbus_names (self, tube_id, contact, new_name);

              /* associate the contact with his stream id */
              if (priv->handle_type == TP_HANDLE_TYPE_ROOM)
                {
                  GibberBytestreamIface *bytestream;

                  g_object_get (tube, "bytestream", &bytestream, NULL);
                  g_assert (bytestream != NULL);

                  if (GIBBER_IS_BYTESTREAM_MUC (bytestream))
                    {
                      guint16 tmp = (guint16) atoi (stream_id);

                      gibber_bytestream_muc_add_sender (
                          GIBBER_BYTESTREAM_MUC (bytestream), sender, tmp);
                    }

                  g_object_unref (bytestream);
                }
            }
        }
    }

  /* Tubes remaining in old_dbus_tubes was left by the contact */
  emit_data.contact = contact;
  emit_data.self = self;
  g_hash_table_foreach (old_dbus_tubes, emit_d_bus_names_changed_foreach,
      &emit_data);

  g_hash_table_destroy (old_dbus_tubes);
}

/* 1-1 message */
void
salut_tubes_channel_message_received (SalutTubesChannel *self,
                                      const gchar *service,
                                      TpTubeType tube_type,
                                      TpHandle initiator_handle,
                                      GHashTable *parameters,
                                      guint tube_id,
                                      guint portnum,
                                      GibberIqHelperRequestStanza *iq_req)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  SalutTubeIface *tube;

  /* do we already know this tube? */
  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));
  if (tube == NULL)
    {
      tube = create_new_tube (self, tube_type, initiator_handle, service,
        parameters, tube_id, portnum, iq_req);
    }
}

void
salut_tubes_channel_message_close_received (SalutTubesChannel *self,
                                            TpHandle initiator_handle,
                                            guint tube_id)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  SalutTubeIface *tube;

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));

  if (tube)
    {
      DEBUG ("received a tube close message");
      salut_tube_iface_close (tube, TRUE);
    }
  else
    {
      DEBUG ("received a tube close message on a non existent tube");
    }
}

static void
muc_connection_new_senders_cb (GibberMucConnection *conn,
                               GArray *senders,
                               gpointer user_data)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (user_data);

  update_tubes_info (self);
}

static void
muc_connection_lost_senders_cb (GibberMucConnection *conn,
                                GArray *senders,
                                gpointer user_data)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (user_data);
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  guint i;

  for (i = 0; i < senders->len; i++)
    {
      gchar *sender;
      TpHandle contact;
      GHashTable *old_dbus_tubes;
      struct _add_in_old_dbus_tubes_data add_data;
      struct emit_d_bus_names_changed_foreach_data emit_data;

      sender = g_array_index (senders, gchar *, i);

      contact = tp_handle_lookup (contact_repo, sender, NULL, NULL);
      if (contact == 0)
        {
          DEBUG ("unknown sender: %s", sender);
          return;
        }

      old_dbus_tubes = g_hash_table_new (g_direct_hash, g_direct_equal);
      add_data.old_dbus_tubes = old_dbus_tubes;
      add_data.contact = contact;
      g_hash_table_foreach (priv->tubes, add_in_old_dbus_tubes, &add_data);

      /* contact left the muc so he left all its tubes */
      emit_data.contact = contact;
      emit_data.self = self;
      g_hash_table_foreach (old_dbus_tubes, emit_d_bus_names_changed_foreach,
          &emit_data);

      g_hash_table_destroy (old_dbus_tubes);
    }
}

static void
copy_tube_in_ptr_array (gpointer key,
                        gpointer value,
                        gpointer user_data)
{
  SalutTubeIface *tube = (SalutTubeIface *) value;
  guint tube_id = GPOINTER_TO_UINT(key);
  TpHandle initiator;
  gchar *service;
  GHashTable *parameters;
  TpTubeState state;
  TpTubeType type;
  GPtrArray *array = (GPtrArray *) user_data;
  GValue entry = {0,};

  g_object_get (tube,
                "type", &type,
                "initiator", &initiator,
                "service", &service,
                "parameters", &parameters,
                "state", &state,
                NULL);

  g_value_init (&entry, SALUT_CHANNEL_TUBE_TYPE);
  g_value_take_boxed (&entry,
          dbus_g_type_specialized_construct (SALUT_CHANNEL_TUBE_TYPE));
  dbus_g_type_struct_set (&entry,
          0, tube_id,
          1, initiator,
          2, type,
          3, service,
          4, parameters,
          5, state,
          G_MAXUINT);

  g_ptr_array_add (array, g_value_get_boxed (&entry));
  g_free (service);
  g_hash_table_unref (parameters);
}

static GPtrArray *
make_tubes_ptr_array (SalutTubesChannel *self,
                      GHashTable *tubes)
{
  GPtrArray *ret;

  ret = g_ptr_array_sized_new (g_hash_table_size (tubes));

  g_hash_table_foreach (tubes, copy_tube_in_ptr_array, ret);

  return ret;
}

/**
 * salut_tubes_channel_list_tubes
 *
 * Implements D-Bus method ListTubes
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_list_tubes (TpSvcChannelTypeTubes *iface,
                                 DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  SalutTubesChannelPrivate *priv;
  GPtrArray *ret;
  guint i;

  g_assert (SALUT_IS_TUBES_CHANNEL (self));

  priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  ret = make_tubes_ptr_array (self, priv->tubes);
  tp_svc_channel_type_tubes_return_from_list_tubes (context, ret);

  for (i = 0; i < ret->len; i++)
    g_boxed_free (SALUT_CHANNEL_TUBE_TYPE, ret->pdata[i]);

  g_ptr_array_free (ret, TRUE);
}

static void
tube_closed_cb (SalutTubeIface *tube,
                gpointer user_data)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (user_data);
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  guint tube_id;

  DEBUG ("Called. closed=%d", priv->closed);
  if (priv->closed)
    return;

  g_object_get (tube, "id", &tube_id, NULL);
  if (!g_hash_table_remove (priv->tubes, GUINT_TO_POINTER (tube_id)))
    {
      DEBUG ("Can't find tube having this id: %d", tube_id);
    }

  DEBUG ("tube %d removed", tube_id);

  /* Emit the DBusNamesChanged signal */
  d_bus_names_changed_removed (self, tube_id, priv->self_handle);

  update_tubes_info (self);

  tp_svc_channel_type_tubes_emit_tube_closed (self, tube_id);
}

static void
tube_opened_cb (SalutTubeIface *tube,
                gpointer user_data)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (user_data);
  guint tube_id;

  g_object_get (tube, "id", &tube_id, NULL);

  tp_svc_channel_type_tubes_emit_tube_state_changed (self, tube_id,
      TP_TUBE_STATE_OPEN);
}

static SalutTubeIface *
create_new_tube (SalutTubesChannel *self,
                 TpTubeType type,
                 TpHandle initiator,
                 const gchar *service,
                 GHashTable *parameters,
                 guint tube_id,
                 guint portnum,
                 GibberIqHelperRequestStanza *iq_req)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  SalutTubeIface *tube;
  TpTubeState state;
  GibberMucConnection *muc_connection = NULL;

  if (self->muc != NULL)
    g_object_get (self->muc, "muc-connection", &muc_connection, NULL);

  switch (type)
    {
    case TP_TUBE_TYPE_DBUS:
      tube = SALUT_TUBE_IFACE (salut_tube_dbus_new (priv->conn,
          priv->handle, priv->handle_type, priv->self_handle, muc_connection,
          initiator, service, parameters, tube_id));
      break;
    case TP_TUBE_TYPE_STREAM:
      tube = SALUT_TUBE_IFACE (salut_tube_stream_new (priv->conn,
          priv->xmpp_connection_manager, priv->handle, priv->handle_type,
          priv->self_handle, initiator, service, parameters, tube_id,
          portnum, iq_req));
      break;
    default:
      g_assert_not_reached ();
    }

  DEBUG ("create tube %u", tube_id);
  g_hash_table_insert (priv->tubes, GUINT_TO_POINTER (tube_id), tube);
  update_tubes_info (self);

  g_object_get (tube, "state", &state, NULL);

  tp_svc_channel_type_tubes_emit_new_tube (self,
      tube_id,
      initiator,
      type,
      service,
      parameters,
      state);

  if (type == TP_TUBE_TYPE_DBUS &&
      state != TP_TUBE_STATE_LOCAL_PENDING)
    {
      add_yourself_in_dbus_names (self, tube_id);
    }

  g_signal_connect (tube, "opened", G_CALLBACK (tube_opened_cb), self);
  g_signal_connect (tube, "closed", G_CALLBACK (tube_closed_cb), self);

  if (muc_connection != NULL)
    g_object_unref (muc_connection);

  return tube;
}

/* tube_node is a MUC <message> */
static gboolean
extract_tube_information (SalutTubesChannel *self,
                          GibberXmppNode *tube_node,
                          TpTubeType *type,
                          TpHandle *initiator_handle,
                          const gchar **service,
                          GHashTable **parameters,
                          guint *tube_id)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  if (type != NULL)
    {
      const gchar *_type;

      _type = gibber_xmpp_node_get_attribute (tube_node, "type");


      if (!tp_strdiff (_type, "stream"))
        {
          *type = TP_TUBE_TYPE_STREAM;
        }
      else if (!tp_strdiff (_type, "dbus"))
        {
          *type = TP_TUBE_TYPE_DBUS;
        }
      else
        {
          DEBUG ("Unknown tube type: %s", _type);
          return FALSE;
        }
    }

  if (initiator_handle != NULL)
    {
      const gchar *initiator;

      initiator = gibber_xmpp_node_get_attribute (tube_node, "initiator");

      if (initiator != NULL)
        {
          *initiator_handle = tp_handle_ensure (contact_repo, initiator, NULL,
              NULL);

          if (*initiator_handle == 0)
            {
              DEBUG ("invalid initiator ID %s", initiator);
              return FALSE;
            }
        }
      else
        {
          *initiator_handle = 0;
        }
    }

  if (service != NULL)
    {
      *service = gibber_xmpp_node_get_attribute (tube_node, "service");
    }

  if (parameters != NULL)
    {
      GibberXmppNode *node;

      node = gibber_xmpp_node_get_child (tube_node, "parameters");
      *parameters = salut_gibber_xmpp_node_extract_properties (node,
          "parameter");
    }

  if (tube_id != NULL)
    {
      const gchar *str;
      gchar *endptr;
      long int tmp;

      str = gibber_xmpp_node_get_attribute (tube_node, "id");
      if (str == NULL)
        {
          DEBUG ("no tube id in SI request");
          return FALSE;
        }

      tmp = strtol (str, &endptr, 10);
      if (!endptr || *endptr)
        {
          DEBUG ("tube id is not numeric: %s", str);
          return FALSE;
        }
      *tube_id = (int) tmp;
    }

  return TRUE;
}

static void
copy_parameter (gpointer key,
                gpointer value,
                gpointer user_data)
{
  const gchar *prop = key;
  GValue *gvalue = value;
  GHashTable *parameters = user_data;
  GValue *gvalue_copied;

  gvalue_copied = g_slice_new0 (GValue);
  g_value_init (gvalue_copied, G_VALUE_TYPE (gvalue));
  g_value_copy (gvalue, gvalue_copied);

  g_hash_table_insert (parameters, g_strdup (prop), gvalue_copied);
}

static void
publish_tube_in_node (SalutTubesChannel *self,
                      GibberXmppNode *node,
                      SalutTubeIface *tube)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  GibberXmppNode *parameters_node;
  GHashTable *parameters;
  TpTubeType type;
  gchar *service, *id_str;
  guint tube_id;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
    (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle initiator_handle;

  g_object_get (G_OBJECT (tube),
      "type", &type,
      "initiator", &initiator_handle,
      "service", &service,
      "parameters", &parameters,
      "id", &tube_id,
      NULL);

  id_str = g_strdup_printf ("%u", tube_id);

  gibber_xmpp_node_set_attribute (node, "service", service);
  gibber_xmpp_node_set_attribute (node, "id", id_str);

  g_free (id_str);

  switch (type)
    {
      case TP_TUBE_TYPE_DBUS:
        {
          gchar *name, *stream_id;

          g_object_get (G_OBJECT (tube),
              "dbus-name", &name,
              "stream-id", &stream_id,
              NULL);

          gibber_xmpp_node_set_attribute (node, "type", "dbus");
          gibber_xmpp_node_set_attribute (node, "stream-id", stream_id);
          gibber_xmpp_node_set_attribute (node, "initiator",
              tp_handle_inspect (contact_repo, initiator_handle));

          if (name != NULL)
            gibber_xmpp_node_set_attribute (node, "dbus-name", name);

          g_free (name);
          g_free (stream_id);

        }
        break;
      case TP_TUBE_TYPE_STREAM:
        gibber_xmpp_node_set_attribute (node, "type", "stream");
        break;
      default:
        g_assert_not_reached ();
    }

  parameters_node = gibber_xmpp_node_add_child (node, "parameters");
  salut_gibber_xmpp_node_add_children_from_properties (parameters_node,
      parameters, "parameter");

  g_free (service);
  g_hash_table_unref (parameters);
}

struct _i_hate_g_hash_table_foreach
{
  SalutTubesChannel *self;
  GibberXmppNode *tubes_node;
};

static void
publish_tubes_in_node (gpointer key,
                       gpointer value,
                       gpointer user_data)
{
  SalutTubeIface *tube = (SalutTubeIface *) value;
  struct _i_hate_g_hash_table_foreach *data =
    (struct _i_hate_g_hash_table_foreach *) user_data;
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (
      data->self);
  TpTubeState state;
  GibberXmppNode *tube_node;
  TpTubeType type;
  TpHandle initiator;

  if (tube == NULL)
    return;

  g_object_get (tube,
      "state", &state,
      "type", &type,
      "initiator", &initiator,
      NULL);

  if (state != TP_TUBE_STATE_OPEN)
    return;

  if (type == TP_TUBE_TYPE_STREAM && initiator != priv->self_handle)
    /* We only announce stream tubes we initiated */
    return;

  tube_node = gibber_xmpp_node_add_child (data->tubes_node, "tube");
  publish_tube_in_node (data->self, tube_node, tube);
}

static gboolean
update_tubes_info (SalutTubesChannel *self)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *conn = (TpBaseConnection*) priv->conn;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      conn, TP_HANDLE_TYPE_ROOM);
  GibberXmppStanza *msg;
  GibberXmppNode *node;
  const gchar *jid;
  struct _i_hate_g_hash_table_foreach data;
  GError *error = NULL;

  if (priv->handle_type != TP_HANDLE_TYPE_ROOM)
    return FALSE;

  /* build the message */
  jid = tp_handle_inspect (room_repo, priv->handle);

  msg = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_MESSAGE,
      GIBBER_STANZA_SUB_TYPE_GROUPCHAT,
      priv->conn->name, jid,
      GIBBER_NODE, "tubes",
        GIBBER_NODE_XMLNS, GIBBER_TELEPATHY_NS_TUBES,
      GIBBER_NODE_END, GIBBER_STANZA_END);

  node = gibber_xmpp_node_get_child_ns (msg->node, "tubes",
      GIBBER_TELEPATHY_NS_TUBES);

  data.self = self;
  data.tubes_node = node;

  g_hash_table_foreach (priv->tubes, publish_tubes_in_node, &data);

  /* Send it */
  if (!gibber_muc_connection_send (priv->muc_connection, msg, &error))
    {
      g_warning ("%s: sending tubes info failed: %s", G_STRFUNC,
          error->message);
      g_error_free (error);
      g_object_unref (msg);
      return FALSE;
    }

  g_object_unref (msg);
  return TRUE;
}

static gint
generate_tube_id (void)
{
  return g_random_int_range (0, G_MAXINT);
}

/**
 * salut_tubes_channel_offer_d_bus_tube
 *
 * Implements D-Bus method OfferDBusTube
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_offer_d_bus_tube (TpSvcChannelTypeTubes *iface,
                                      const gchar *service,
                                      GHashTable *parameters,
                                      DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  SalutTubesChannelPrivate *priv;
  TpBaseConnection *base;
  guint tube_id;
  SalutTubeIface *tube;
  GHashTable *parameters_copied;

  g_assert (SALUT_IS_TUBES_CHANNEL (self));

  priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  base = (TpBaseConnection*) priv->conn;

  if (priv->handle_type == TP_HANDLE_TYPE_ROOM
    && !tp_handle_set_is_member (TP_GROUP_MIXIN (self->muc)->members,
        priv->self_handle))
    {
      GError error = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
         "Tube channel isn't connected" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  parameters_copied = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) tp_g_value_slice_free);
  g_hash_table_foreach (parameters, copy_parameter, parameters_copied);

  tube_id = generate_tube_id ();

  tube = create_new_tube (self, TP_TUBE_TYPE_DBUS, priv->self_handle,
      service, parameters_copied, tube_id, 0, NULL);

  tp_svc_channel_type_tubes_return_from_offer_d_bus_tube (context, tube_id);
}

/**
 * salut_tubes_channel_accept_d_bus_tube
 *
 * Implements D-Bus method AcceptDBusTube
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_accept_d_bus_tube (TpSvcChannelTypeTubes *iface,
                                       guint id,
                                       DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  SalutTubesChannelPrivate *priv;
  SalutTubeIface *tube;
  TpTubeState state;
  TpTubeType type;
  gchar *addr;

  g_assert (SALUT_IS_TUBES_CHANNEL (self));

  priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));
  if (tube == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &error);

      return;
    }

  g_object_get (tube,
      "type", &type,
      "state", &state,
      NULL);

  if (type != TP_TUBE_TYPE_DBUS)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not a D-Bus tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  if (state != TP_TUBE_STATE_LOCAL_PENDING)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not in the local pending state" };

      dbus_g_method_return_error (context, &error);

      return;
    }

  salut_tube_iface_accept (tube, NULL);

  update_tubes_info (self);

  g_object_get (tube,
      "dbus-address", &addr,
      NULL);

  add_yourself_in_dbus_names (self, id);

  tp_svc_channel_type_tubes_return_from_accept_d_bus_tube (context, addr);
  g_free (addr);
}

/**
 * salut_tubes_channel_close_tube
 *
 * Implements D-Bus method CloseTube
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_close_tube (TpSvcChannelTypeTubes *iface,
                                 guint id,
                                 DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  SalutTubesChannelPrivate *priv;
  SalutTubeIface *tube;

  g_assert (SALUT_IS_TUBES_CHANNEL (self));

  priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));
  if (tube == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  salut_tube_iface_close (tube, FALSE);

  tp_svc_channel_type_tubes_return_from_close_tube (context);
}

/**
 * salut_tubes_channel_get_d_bus_tube_address
 *
 * Implements D-Bus method GetDBusTubeAddress
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_get_d_bus_tube_address (TpSvcChannelTypeTubes *iface,
                                            guint id,
                                            DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  SalutTubesChannelPrivate *priv;
  SalutTubeIface *tube;
  gchar *addr;
  TpTubeType type;
  TpTubeState state;

  g_assert (SALUT_IS_TUBES_CHANNEL (self));

  priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));

  if (tube == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube,
      "type", &type,
      "state", &state,
      NULL);

  if (type != TP_TUBE_TYPE_DBUS)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not a D-Bus tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  if (state != TP_TUBE_STATE_OPEN)
    {
      GError error = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Tube is not open" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube, "dbus-address", &addr, NULL);
  tp_svc_channel_type_tubes_return_from_get_d_bus_tube_address (context,
      addr);
  g_free (addr);
}

static void
get_d_bus_names_foreach (gpointer key,
                         gpointer value,
                         gpointer user_data)
{
  GPtrArray *ret = user_data;
  GValue tmp = {0,};

  g_value_init (&tmp, DBUS_NAME_PAIR_TYPE);
  g_value_take_boxed (&tmp,
      dbus_g_type_specialized_construct (DBUS_NAME_PAIR_TYPE));
  dbus_g_type_struct_set (&tmp,
      0, key,
      1, value,
      G_MAXUINT);
  g_ptr_array_add (ret, g_value_get_boxed (&tmp));
}

/**
 * salut_tubes_channel_get_d_bus_names
 *
 * Implements D-Bus method GetDBusNames
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_get_d_bus_names (TpSvcChannelTypeTubes *iface,
                                      guint id,
                                      DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  SalutTubeIface *tube;
  GHashTable *names;
  GPtrArray *ret;
  TpTubeType type;
  TpTubeState state;
  guint i;

  g_assert (SALUT_IS_TUBES_CHANNEL (self));

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));

  if (tube == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube,
      "type", &type,
      "state", &state,
      NULL);

  if (type != TP_TUBE_TYPE_DBUS)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not a D-Bus tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  if (state != TP_TUBE_STATE_OPEN)
    {
      GError error = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Tube is not open" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube, "dbus-names", &names, NULL);
  g_assert (names);

  ret = g_ptr_array_sized_new (g_hash_table_size (names));
  g_hash_table_foreach (names, get_d_bus_names_foreach, ret);

  tp_svc_channel_type_tubes_return_from_get_d_bus_names (context, ret);

  for (i = 0; i < ret->len; i++)
    g_boxed_free (DBUS_NAME_PAIR_TYPE, ret->pdata[i]);
  g_hash_table_unref (names);
  g_ptr_array_free (ret, TRUE);
}

static void
stream_tube_new_connection_cb (SalutTubeIface *tube,
                               guint contact,
                               gpointer user_data)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (user_data);
  guint tube_id;
  TpTubeType type;

  g_object_get (tube,
      "id", &tube_id,
      "type", &type,
      NULL);

  g_assert (type == TP_TUBE_TYPE_STREAM);

  tp_svc_channel_type_tubes_emit_stream_tube_new_connection (self,
      tube_id, contact);
}

static void
iq_reply_cb (GibberIqHelper *helper,
             GibberXmppStanza *sent_stanza,
             GibberXmppStanza *reply_stanza,
             GObject *object,
             gpointer user_data)
{
  SalutTubeIface *tube = (SalutTubeIface *) user_data;
  GibberStanzaSubType sub_type;

  gibber_xmpp_stanza_get_type_info (reply_stanza, NULL, &sub_type);
  if (sub_type != GIBBER_STANZA_SUB_TYPE_RESULT)
    {
      DEBUG ("tube offer declined declined");
      salut_tube_iface_close (tube, TRUE);
      return;
    }

  g_object_set (tube,
      "state", TP_TUBE_STATE_OPEN,
      NULL);

  DEBUG ("tube offered successfully");
}

static void
new_connection_cb (GibberBytestreamIface *bytestream, gpointer user_data)
{
  SalutTubeIface *tube = user_data;

  salut_tube_iface_add_bytestream (tube, bytestream);
}

static void
send_channel_iq_tube (gpointer key,
                       gpointer value,
                       gpointer user_data)
{
  SalutTubesChannel *self = (SalutTubesChannel *) user_data;
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  SalutTubeIface *tube = (SalutTubeIface *) value;
  guint tube_id = GPOINTER_TO_UINT (key);
  TpHandle initiator;
  gchar *service;
  GHashTable *parameters;
  TpTubeState state;
  TpTubeType type;

  g_object_get (tube,
                "type", &type,
                "initiator", &initiator,
                "service", &service,
                "parameters", &parameters,
                "state", &state,
                NULL);

  DEBUG ("called for tube id %d", tube_id);
  if (salut_tube_iface_offer_needed (tube))
    {
      GError *error = NULL;
      GibberXmppNode *parameters_node;
      const char *tube_type_str;
      GibberXmppStanza *stanza;
      const gchar *jid_from, *jid_to;
      TpHandleRepoIface *contact_repo;
      gchar *tube_id_str;
      int port;
      gchar *port_str;

      DEBUG ("Tube in remote pending state");

      /* listen for future connections from the remote CM before sending the
       * iq */
      port = salut_tube_iface_listen (tube);
      g_assert (port > 0);

      contact_repo = tp_base_connection_get_handles (
         (TpBaseConnection*) priv->conn, TP_HANDLE_TYPE_CONTACT);

      jid_from = tp_handle_inspect (contact_repo, priv->self_handle);
      jid_to = tp_handle_inspect (contact_repo, priv->handle);

      switch (type)
        {
          case TP_TUBE_TYPE_DBUS:
            tube_type_str = "dbus";
            break;

          case TP_TUBE_TYPE_STREAM:
            tube_type_str = "stream";
            break;
          default:
            g_assert_not_reached ();
        }

      port_str = g_strdup_printf ("%d", port);
      tube_id_str = g_strdup_printf ("%d", tube_id);

      stanza = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_IQ,
          GIBBER_STANZA_SUB_TYPE_SET,
          jid_from, jid_to,
          GIBBER_NODE, "tube",
            GIBBER_NODE_XMLNS, GIBBER_TELEPATHY_NS_TUBES,
            GIBBER_NODE_ATTRIBUTE, "type", tube_type_str,
            GIBBER_NODE_ATTRIBUTE, "service", service,
            GIBBER_NODE_ATTRIBUTE, "id", tube_id_str,
            GIBBER_NODE, "transport",
              GIBBER_NODE_ATTRIBUTE, "port", port_str,
            GIBBER_NODE_END,
          GIBBER_NODE_END,
          GIBBER_STANZA_END);

      parameters_node = gibber_xmpp_node_add_child (
          gibber_xmpp_node_get_child (stanza->node, "tube"), "parameters");
      salut_gibber_xmpp_node_add_children_from_properties (parameters_node,
          parameters, "parameter");

      if (priv->iq_helper == NULL)
        {
          priv->iq_helper = gibber_iq_helper_new (priv->xmpp_connection);
          g_assert (priv->iq_helper);
        }

      if (!gibber_iq_helper_send_with_reply (priv->iq_helper, stanza,
          iq_reply_cb, G_OBJECT(self), tube, &error))
        {
          DEBUG ("ERROR: '%s'", error->message);
          g_error_free (error);
        }

      g_object_unref (stanza);
      g_free (tube_id_str);
      g_free (port_str);
    }

  g_free (service);
  g_hash_table_unref (parameters);
}

static void
send_channel_iq_tubes (SalutTubesChannel *self)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  DEBUG ("called. state=%d", priv->state);

  if (priv->state != CHANNEL_CONNECTED)
    {
      /* TODO: do not connect if nothing to send... */
      setup_connection (self);
      return;
    }

  g_hash_table_foreach (priv->tubes, send_channel_iq_tube, self);
}


/**
 * salut_tubes_channel_offer_stream_tube
 *
 * Implements D-Bus method OfferStreamTube
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_offer_stream_tube (TpSvcChannelTypeTubes *iface,
                                        const gchar *service,
                                        GHashTable *parameters,
                                        guint address_type,
                                        const GValue *address,
                                        guint access_control,
                                        const GValue *access_control_param,
                                        DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  SalutTubesChannelPrivate *priv;
  TpBaseConnection *base;
  guint tube_id;
  SalutTubeIface *tube;
  GHashTable *parameters_copied;
  GError *error = NULL;

  priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  base = (TpBaseConnection*) priv->conn;

  if (priv->handle_type == TP_HANDLE_TYPE_ROOM
    && !tp_handle_set_is_member (TP_GROUP_MIXIN (self->muc)->members,
        priv->self_handle))
    {
      GError error = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
         "Tube channel isn't connected" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  if (!salut_tube_stream_check_params (address_type, address,
        access_control, access_control_param, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  parameters_copied = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) tp_g_value_slice_free);
  g_hash_table_foreach (parameters, copy_parameter, parameters_copied);

  tube_id = generate_tube_id ();

  tube = create_new_tube (self, TP_TUBE_TYPE_STREAM, priv->self_handle,
      service, parameters_copied, tube_id, 0, NULL);

  g_object_set (tube,
      "address-type", address_type,
      "address", address,
      "access-control", access_control,
      "access-control-param", access_control_param,
      NULL);

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      send_channel_iq_tubes (self);
    }

  g_signal_connect (tube, "new-connection",
      G_CALLBACK (stream_tube_new_connection_cb), self);

  tp_svc_channel_type_tubes_return_from_offer_stream_tube (context,
      tube_id);
}

/**
 * salut_tubes_channel_accept_stream_tube
 *
 * Implements D-Bus method AcceptStreamTube
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_accept_stream_tube (TpSvcChannelTypeTubes *iface,
                                        guint id,
                                        guint address_type,
                                        guint access_control,
                                        const GValue *access_control_param,
                                        DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  SalutTubesChannelPrivate *priv;
  SalutTubeIface *tube;
  TpTubeState state;
  TpTubeType type;
  GValue *address;
  GError *error = NULL;

  g_assert (SALUT_IS_TUBES_CHANNEL (self));

  priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));
  if (tube == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  if (address_type != TP_SOCKET_ADDRESS_TYPE_UNIX &&
      address_type != TP_SOCKET_ADDRESS_TYPE_IPV4 &&
      address_type != TP_SOCKET_ADDRESS_TYPE_IPV6)
    {
      GError *error = NULL;

      error = g_error_new (TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "Address type %d not implemented", address_type);

      dbus_g_method_return_error (context, error);

      g_error_free (error);
      return;
    }

  if (access_control != TP_SOCKET_ACCESS_CONTROL_LOCALHOST)
    {
      GError *error = NULL;

      error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Unix sockets only support localhost control access");

      dbus_g_method_return_error (context, error);

      g_error_free (error);
      return;
    }

  g_object_get (tube,
      "type", &type,
      "state", &state,
      NULL);

  if (type != TP_TUBE_TYPE_STREAM)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not a stream tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  if (state != TP_TUBE_STATE_LOCAL_PENDING)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not in the local pending state" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_set (tube,
      "address-type", address_type,
      "access-control", access_control,
      "access-control-param", access_control_param,
      NULL);

  if (!salut_tube_iface_accept (tube, &error))
    {
      dbus_g_method_return_error (context, error);
      return;
    }

  update_tubes_info (self);

  g_object_get (tube, "address", &address, NULL);

  tp_svc_channel_type_tubes_return_from_accept_stream_tube (context,
      address);
}

/**
 * salut_tubes_channel_get_stream_tube_socket_address
 *
 * Implements D-Bus method GetStreamTubeSocketAddress
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_get_stream_tube_socket_address (TpSvcChannelTypeTubes *iface,
                                                    guint id,
                                                    DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  SalutTubesChannelPrivate *priv  = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  SalutTubeIface *tube;
  TpTubeType type;
  TpTubeState state;
  GValue *address;
  TpSocketAddressType address_type;

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));
  if (tube == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube,
      "type", &type,
      "state", &state,
      NULL);

  if (type != TP_TUBE_TYPE_STREAM)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not a Stream tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  if (state != TP_TUBE_STATE_OPEN)
    {
      GError error = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Tube is not open" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube,
      "address", &address,
      "address-type", &address_type,
      NULL);

  tp_svc_channel_type_tubes_return_from_get_stream_tube_socket_address (
      context, address_type, address);
}

/**
 * salut_tubes_channel_get_available_stream_tube_types
 *
 * Implements D-Bus method GetAvailableStreamTubeTypes
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_get_available_stream_tube_types (
    TpSvcChannelTypeTubes *iface,
    DBusGMethodInvocation *context)
{
  GHashTable *ret;
  GArray *unix_tab, *ipv4_tab, *ipv6_tab;
  TpSocketAccessControl access;

  ret = g_hash_table_new (g_direct_hash, g_direct_equal);

  /* Socket_Address_Type_Unix */
  unix_tab = g_array_sized_new (FALSE, FALSE, sizeof (TpSocketAccessControl),
      1);
  access = TP_SOCKET_ACCESS_CONTROL_LOCALHOST;
  g_array_append_val (unix_tab, access);
  g_hash_table_insert (ret, GUINT_TO_POINTER (TP_SOCKET_ADDRESS_TYPE_UNIX),
      unix_tab);

  /* Socket_Address_Type_IPv4 */
  ipv4_tab = g_array_sized_new (FALSE, FALSE, sizeof (TpSocketAccessControl),
      1);
  access = TP_SOCKET_ACCESS_CONTROL_LOCALHOST;
  g_array_append_val (ipv4_tab, access);
  g_hash_table_insert (ret, GUINT_TO_POINTER (TP_SOCKET_ADDRESS_TYPE_IPV4),
      ipv4_tab);

  /* Socket_Address_Type_IPv6 */
  ipv6_tab = g_array_sized_new (FALSE, FALSE, sizeof (TpSocketAccessControl),
      1);
  access = TP_SOCKET_ACCESS_CONTROL_LOCALHOST;
  g_array_append_val (ipv6_tab, access);
  g_hash_table_insert (ret, GUINT_TO_POINTER (TP_SOCKET_ADDRESS_TYPE_IPV6),
      ipv6_tab);

  tp_svc_channel_type_tubes_return_from_get_available_stream_tube_types (
      context, ret);

  g_array_free (unix_tab, TRUE);
  g_array_free (ipv4_tab, TRUE);
  g_array_free (ipv6_tab, TRUE);
  g_hash_table_destroy (ret);
}

static void salut_tubes_channel_dispose (GObject *object);
static void salut_tubes_channel_finalize (GObject *object);

static void
salut_tubes_channel_class_init (
    SalutTubesChannelClass *salut_tubes_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_tubes_channel_class);
  GParamSpec *param_spec;
  static TpDBusPropertiesMixinPropImpl channel_props[] = {
      { "TargetHandleType", "handle-type", NULL },
      { "TargetHandle", "handle", NULL },
      { "TargetID", "target-id", NULL },
      { "ChannelType", "channel-type", NULL },
      { "Interfaces", "interfaces", NULL },
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
      { NULL }
  };

  g_type_class_add_private (salut_tubes_channel_class,
      sizeof (SalutTubesChannelPrivate));

  object_class->constructor = salut_tubes_channel_constructor;

  object_class->dispose = salut_tubes_channel_dispose;
  object_class->finalize = salut_tubes_channel_finalize;

  object_class->get_property = salut_tubes_channel_get_property;
  object_class->set_property = salut_tubes_channel_set_property;

  g_object_class_override_property (object_class, PROP_OBJECT_PATH,
      "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
      "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
      "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");

  g_object_class_override_property (object_class, PROP_CHANNEL_DESTROYED,
      "channel-destroyed");
  g_object_class_override_property (object_class, PROP_CHANNEL_PROPERTIES,
      "channel-properties");

  param_spec = g_param_spec_string ("target-id", "Target JID",
      "The string obtained by inspecting this channel's handle",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_TARGET_ID, param_spec);

  param_spec = g_param_spec_boolean ("requested", "Requested?",
      "True if this channel was requested by the local user",
      FALSE,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NAME);
  g_object_class_install_property (object_class, PROP_REQUESTED, param_spec);

  param_spec = g_param_spec_uint ("initiator-handle", "Initiator's handle",
      "The contact which caused the Tubes channel to appear",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NAME);
  g_object_class_install_property (object_class, PROP_INITIATOR_HANDLE,
      param_spec);

  param_spec = g_param_spec_string ("initiator-id", "Initiator JID",
      "The string obtained by inspecting this channel's initiator-handle",
      NULL,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NAME);
  g_object_class_install_property (object_class, PROP_INITIATOR_ID,
      param_spec);

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "Salut Connection that owns the connection for this tubes channel",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_object (
      "muc",
      "SalutMucChannel object",
      "Salut text MUC channel corresponding to this Tubes channel object, "
      "if the handle type is ROOM.",
      SALUT_TYPE_MUC_CHANNEL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_MUC, param_spec);

  param_spec = g_param_spec_object (
      "contact",
      "SalutContact object",
      "Salut Contact to which this channel is dedicated in case of 1-1 tube",
      SALUT_TYPE_CONTACT,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONTACT, param_spec);

  param_spec = g_param_spec_object (
      "xmpp-connection-manager",
      "SalutXmppConnectionManager object",
      "Salut XMPP Connection manager used for this tube channel in case of "
          "1-1 tube",
      SALUT_TYPE_XMPP_CONNECTION_MANAGER,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_XMPP_CONNECTION_MANAGER,
      param_spec);
  param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
      "Additional Channel.Interface.* interfaces",
      G_TYPE_STRV,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NAME);
  g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

  salut_tubes_channel_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (SalutTubesChannelClass, dbus_props_class));

  tp_external_group_mixin_init_dbus_properties (object_class);
}

void
salut_tubes_channel_dispose (GObject *object)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (object);
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, priv->handle_type);

  if (priv->dispose_has_run)
    return;

  if (priv->muc_connection != NULL)
    {
      g_signal_handlers_disconnect_matched (priv->muc_connection,
          G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);

      g_object_unref (priv->muc_connection);
      priv->muc_connection = NULL;
    }

  if (priv->iq_helper != NULL)
    {
      g_object_unref (priv->iq_helper);
      priv->iq_helper = NULL;
    }

  if (priv->xmpp_connection != NULL)
    {
      g_object_unref (priv->xmpp_connection);
      priv->xmpp_connection = NULL;
    }

  if (priv->xmpp_connection_manager != NULL)
    {
      g_signal_handlers_disconnect_matched (priv->xmpp_connection_manager,
          G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);

      g_object_unref (priv->xmpp_connection_manager);
      priv->xmpp_connection_manager = NULL;
    }

  if (priv->contact != NULL)
    {
      g_object_unref (priv->contact);
      priv->contact = NULL;
    }

  priv->dispose_has_run = TRUE;

  tp_handle_unref (handle_repo, priv->handle);

  if (self->muc != NULL)
      tp_external_group_mixin_finalize (object);

  salut_tubes_channel_close (self);

  if (G_OBJECT_CLASS (salut_tubes_channel_parent_class)->dispose)
    G_OBJECT_CLASS (salut_tubes_channel_parent_class)->dispose (object);
}

static void
salut_tubes_channel_finalize (GObject *object)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (object);
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  g_free (priv->object_path);

  G_OBJECT_CLASS (salut_tubes_channel_parent_class)->finalize (object);
}

static void
emit_tube_closed_signal (gpointer key,
                         gpointer value,
                         gpointer user_data)
{
  guint id = GPOINTER_TO_UINT (key);
  SalutTubesChannel *self = (SalutTubesChannel *) user_data;

  tp_svc_channel_type_tubes_emit_tube_closed (self, id);
}

void
salut_tubes_channel_close (SalutTubesChannel *self)
{
  SalutTubesChannelPrivate *priv;

  g_assert (SALUT_IS_TUBES_CHANNEL (self));

  DEBUG ("called on %p", self);

  priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  if (priv->closed)
    {
      return;
    }

  priv->closed = TRUE;

  g_hash_table_foreach (priv->tubes, emit_tube_closed_signal, self);
  g_hash_table_destroy (priv->tubes);

  priv->tubes = NULL;

  tp_svc_channel_emit_closed (self);
}

/**
 * salut_tubes_channel_close_async:
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_tubes_channel_close_async (TpSvcChannel *iface,
                                 DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);

  g_assert (SALUT_IS_TUBES_CHANNEL (self));

  salut_tubes_channel_close (self);
  tp_svc_channel_return_from_close (context);
}

/**
 * salut_tubes_channel_get_channel_type
 *
 * Tubesplements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_tubes_channel_get_channel_type (TpSvcChannel *iface,
                                      DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      TP_IFACE_CHANNEL_TYPE_TUBES);
}


/**
 * salut_tubes_channel_get_handle
 *
 * Tubesplements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_tubes_channel_get_handle (TpSvcChannel *iface,
                                DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  SalutTubesChannelPrivate *priv;

  g_assert (SALUT_IS_TUBES_CHANNEL (self));
  priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  tp_svc_channel_return_from_get_handle (context, priv->handle_type,
      priv->handle);
}


/**
 * salut_tubes_channel_get_interfaces
 *
 * Tubesplements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_tubes_channel_get_interfaces (TpSvcChannel *iface,
                                    DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);

  if (self->muc)
    {
      tp_svc_channel_return_from_get_interfaces (context,
        salut_tubes_channel_interfaces);
    }
  else
    {
      /* only show the NULL */
      tp_svc_channel_return_from_get_interfaces (context,
        salut_tubes_channel_interfaces + 1);
    }
}

/* Called when we receive a SI request,
 * via salut_muc_manager_handle_si_stream_request
 */
void
salut_tubes_channel_bytestream_offered (SalutTubesChannel *self,
                                        GibberBytestreamIface *bytestream,
                                        GibberXmppStanza *msg)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  const gchar *stream_id, *tmp;
  gchar *endptr;
  GibberXmppNode *si_node, *stream_node;
  guint tube_id;
  unsigned long tube_id_tmp;
  SalutTubeIface *tube;
  GibberStanzaType type;
  GibberStanzaSubType sub_type;

  /* Caller is expected to have checked that we have a stream or muc-stream
   * node with a stream ID and the TUBES profile
   */
  gibber_xmpp_stanza_get_type_info (msg, &type, &sub_type);
  g_return_if_fail (type == GIBBER_STANZA_TYPE_IQ);
  g_return_if_fail (sub_type == GIBBER_STANZA_SUB_TYPE_SET);

  si_node = gibber_xmpp_node_get_child_ns (msg->node, "si",
      GIBBER_XMPP_NS_SI);
  g_return_if_fail (si_node != NULL);

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    stream_node = gibber_xmpp_node_get_child_ns (si_node,
        "stream", GIBBER_TELEPATHY_NS_TUBES);
  else
    stream_node = gibber_xmpp_node_get_child_ns (si_node,
        "muc-stream", GIBBER_TELEPATHY_NS_TUBES);
  g_return_if_fail (stream_node != NULL);

  stream_id = gibber_xmpp_node_get_attribute (si_node, "id");
  g_return_if_fail (stream_id != NULL);

  tmp = gibber_xmpp_node_get_attribute (stream_node, "tube");
  if (tmp == NULL)
    {
      GError e = { GIBBER_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "<stream> or <muc-stream> has no tube attribute" };

      DEBUG ("%s", e.message);
      gibber_bytestream_iface_close (bytestream, &e);
      return;
    }
  tube_id_tmp = strtoul (tmp, &endptr, 10);
  if (!endptr || *endptr || tube_id_tmp > G_MAXUINT32)
    {
      GError e = { GIBBER_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "<stream> or <muc-stream> tube attribute not numeric or > 2**32" };

      DEBUG ("tube id is not numeric or > 2**32: %s", tmp);
      gibber_bytestream_iface_close (bytestream, &e);
      return;
    }
  tube_id = (guint) tube_id_tmp;

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));
  if (tube == NULL)
    {
      GError e = { GIBBER_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "<stream> or <muc-stream> tube attribute points to a nonexistent "
          "tube" };

      DEBUG ("tube %u doesn't exist", tube_id);
      gibber_bytestream_iface_close (bytestream, &e);
      return;
    }

  DEBUG ("received new bytestream request for existing tube: %u", tube_id);

  salut_tube_iface_add_bytestream (tube, bytestream);
}

static void
tubes_iface_init (gpointer g_iface,
                  gpointer iface_data)
{
  TpSvcChannelTypeTubesClass *klass = (TpSvcChannelTypeTubesClass *) g_iface;

#define IMPLEMENT(x) tp_svc_channel_type_tubes_implement_##x (\
    klass, salut_tubes_channel_##x)
  IMPLEMENT(get_available_tube_types);
  IMPLEMENT(list_tubes);
  IMPLEMENT(close_tube);
  IMPLEMENT(offer_d_bus_tube);
  IMPLEMENT(accept_d_bus_tube);
  IMPLEMENT(get_d_bus_tube_address);
  IMPLEMENT(get_d_bus_names);
  IMPLEMENT(offer_stream_tube);
  IMPLEMENT(accept_stream_tube);
  IMPLEMENT(get_stream_tube_socket_address);
  IMPLEMENT(get_available_stream_tube_types);
#undef IMPLEMENT
}

static void
channel_iface_init (gpointer g_iface,
                    gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *) g_iface;

#define IMPLEMENT(x, suffix) tp_svc_channel_implement_##x (\
    klass, salut_tubes_channel_##x##suffix)
  IMPLEMENT(close,_async);
  IMPLEMENT(get_channel_type,);
  IMPLEMENT(get_handle,);
  IMPLEMENT(get_interfaces,);
#undef IMPLEMENT
}
