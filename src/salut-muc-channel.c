/*
 * salut-muc-channel.c - Source for SalutMucChannel
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
#include <arpa/inet.h>

#include <string.h>

#define DEBUG_FLAG DEBUG_MUC
#include "debug.h"

/* Maximum time to wait for others joining the group  */
#define CONNECTED_TIMEOUT 60 * 1000

#include "salut-muc-channel.h"

#include <gibber/gibber-namespaces.h>
#include <gibber/gibber-transport.h>

#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/util.h>

#include <gibber/gibber-muc-connection.h>

#include "salut-connection.h"
#include "salut-self.h"
#include "salut-xmpp-connection-manager.h"
#include "salut-muc-manager.h"

#include "text-helper.h"

static void channel_iface_init (gpointer g_iface, gpointer iface_data);
static void text_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE(SalutMucChannel, salut_muc_channel, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
      tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);
    G_IMPLEMENT_INTERFACE(TP_TYPE_CHANNEL_IFACE, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
        tp_group_mixin_iface_init);
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CHANNEL_TYPE_TEXT, text_iface_init);
)

static const char *salut_muc_channel_interfaces[] = {
  TP_IFACE_CHANNEL_INTERFACE_GROUP,
  NULL
};

/* signal enum */
enum
{
    READY,
    JOIN_ERROR,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_MUC_CONNECTION,
  PROP_CONNECTION,
  PROP_NAME,
  PROP_CREATOR,
  PROP_XMPP_CONNECTION_MANAGER,
  PROP_INTERFACES,
  PROP_TARGET_ID,
  PROP_REQUESTED,
  PROP_INITIATOR_HANDLE,
  PROP_INITIATOR_ID,
  PROP_CHANNEL_PROPERTIES,
  PROP_CHANNEL_DESTROYED,
  LAST_PROPERTY
};

/* private structure */
typedef struct _SalutMucChannelPrivate SalutMucChannelPrivate;

struct _SalutMucChannelPrivate
{
  gboolean dispose_has_run;
  gchar *object_path;
  TpHandle handle;
  SalutSelf *self;
  SalutXmppConnectionManager *xmpp_connection_manager;
  GibberMucConnection *muc_connection;
  gchar *muc_name;
  gboolean connected;
  gboolean creator;
  guint timeout;
  /* (gchar *) -> (SalutContact *) */
  GHashTable *senders;
  SalutMucManager *muc_manager;
  TpHandle initiator;
};

#define SALUT_MUC_CHANNEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_MUC_CHANNEL, SalutMucChannelPrivate))

/* Callback functions */
static gboolean salut_muc_channel_send_stanza (SalutMucChannel *self,
                                               guint type,
                                               const gchar *text,
                                               GibberXmppStanza *stanza,
                                               GError **error);
static void salut_muc_channel_received_stanza (GibberMucConnection *conn,
                                               const gchar *sender,
                                               GibberXmppStanza *stanza,
                                               gpointer user_data);
static gboolean
salut_muc_channel_connect (SalutMucChannel *channel, GError **error);
static void salut_muc_channel_disconnected (GibberTransport *transport,
                                            gpointer user_data);

static void
salut_muc_channel_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  SalutMucChannel *chan = SALUT_MUC_CHANNEL (object);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (chan);
  TpBaseConnection *base_conn = (TpBaseConnection *) chan->connection;

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_NAME:
      g_value_set_string (value, priv->muc_name);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_TEXT);
      break;
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, TP_HANDLE_TYPE_ROOM);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, priv->handle);
      break;
    case PROP_CONNECTION:
      g_value_set_object (value, chan->connection);
      break;
    case PROP_MUC_CONNECTION:
      g_value_set_object (value, priv->muc_connection);
      break;
    case PROP_CREATOR:
      g_value_set_boolean (value, priv->creator);
      break;
    case PROP_XMPP_CONNECTION_MANAGER:
      g_value_set_object (value, priv->xmpp_connection_manager);
      break;
    case PROP_INTERFACES:
      g_value_set_static_boxed (value, salut_muc_channel_interfaces);
      break;
    case PROP_TARGET_ID:
      {
        TpHandleRepoIface *repo = tp_base_connection_get_handles (base_conn,
            TP_HANDLE_TYPE_ROOM);

        g_value_set_string (value, tp_handle_inspect (repo, priv->handle));
      }
      break;
    case PROP_INITIATOR_HANDLE:
      g_value_set_uint (value, priv->initiator);
      break;
    case PROP_INITIATOR_ID:
      {
        TpHandleRepoIface *repo = tp_base_connection_get_handles (base_conn,
            TP_HANDLE_TYPE_CONTACT);

        g_value_set_string (value, tp_handle_inspect (repo, priv->initiator));
      }
      break;
    case PROP_REQUESTED:
      g_value_set_boolean (value,
          (priv->initiator == tp_base_connection_get_self_handle (base_conn)));
      break;
      case PROP_CHANNEL_DESTROYED:
        /* TODO: this should be FALSE if there are still pending messages, so
         *       the channel manager can respawn the channel.
         */
        g_value_set_boolean (value, TRUE);
        break;
      case PROP_CHANNEL_PROPERTIES:
        g_value_set_boxed (value,
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
salut_muc_channel_set_property (GObject     *object,
                                guint        property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  SalutMucChannel *chan = SALUT_MUC_CHANNEL (object);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (chan);
  const gchar *tmp;

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_NAME:
      g_free (priv->muc_name);
      priv->muc_name = g_value_dup_string (value);
      break;
    case PROP_HANDLE:
      priv->handle = g_value_get_uint (value);
      break;
    case PROP_CONNECTION:
      chan->connection = g_value_get_object (value);
      break;
    case PROP_MUC_CONNECTION:
      priv->muc_connection = g_value_get_object (value);
      break;
    case PROP_HANDLE_TYPE:
      g_assert (g_value_get_uint (value) == 0
               || g_value_get_uint (value) == TP_HANDLE_TYPE_ROOM);
      break;
    case PROP_CHANNEL_TYPE:
      tmp = g_value_get_string (value);
      g_assert (tmp == NULL
          || !tp_strdiff (g_value_get_string (value),
              TP_IFACE_CHANNEL_TYPE_TEXT));
      break;
    case PROP_CREATOR:
      priv->creator = g_value_get_boolean (value);
      break;
    case PROP_INITIATOR_HANDLE:
      priv->initiator = g_value_get_uint (value);
      break;
    case PROP_XMPP_CONNECTION_MANAGER:
      priv->xmpp_connection_manager = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
salut_muc_channel_add_self_to_members (SalutMucChannel *self)
{
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION (self->connection);
  TpIntSet *empty;
  TpIntSet *add;

  priv->connected = TRUE;
  g_signal_emit (self, signals[READY], 0);

  if (priv->timeout != 0)
    {
      g_source_remove (priv->timeout);
      priv->timeout = 0;
    }

  /* Now we are connected, move yourself to members */
  empty = tp_intset_new ();
  add = tp_intset_new ();
  tp_intset_add (add, base_connection->self_handle);

  tp_group_mixin_change_members (G_OBJECT (self),
      "", add, empty, empty, empty, base_connection->self_handle,
      TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

  tp_intset_destroy (empty);
  tp_intset_destroy (add);
}

static gboolean
connected_timeout_cb (gpointer user_data)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (user_data);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);

  DEBUG ("Didn't receive muc senders. Timeout expired, "
      "adding myself as member anyway");
  salut_muc_channel_add_self_to_members (self);
  priv->timeout = 0;

  return FALSE;
}

static void
muc_connection_connected_cb (GibberMucConnection *connection,
                             SalutMucChannel *self)
{
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);

  if (priv->creator)
    {
      DEBUG ("I created this muc. Adding myself as member now");
      salut_muc_channel_add_self_to_members (self);
    }
  else
    {
      DEBUG ("I didn't create this muc. Waiting for senders before adding "
          "myself as member");
      priv->timeout = g_timeout_add (CONNECTED_TIMEOUT, connected_timeout_cb,
          self);
    }

  salut_muc_channel_publish_service  (self);
}

static GObject *
salut_muc_channel_constructor (GType type, guint n_props,
    GObjectConstructParam *props)
{
  GObject *obj;
  DBusGConnection *bus;
  SalutMucChannel *self;
  SalutMucChannelPrivate *priv;
  TpBaseConnection *base_conn;
  TpHandleRepoIface *handle_repo;
  TpHandleRepoIface *contact_repo;

  /* Parent constructor chain */
  obj = G_OBJECT_CLASS (salut_muc_channel_parent_class)->
        constructor (type, n_props, props);

  self = SALUT_MUC_CHANNEL (obj);
  priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);

  /* Ref our handle */
  base_conn = TP_BASE_CONNECTION (self->connection);

  handle_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_ROOM);

  tp_handle_ref (handle_repo, priv->handle);

  /* Text mixin initialisation */
  contact_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);
  tp_text_mixin_init (obj, G_STRUCT_OFFSET (SalutMucChannel, text),
      contact_repo);

  tp_text_mixin_set_message_types (obj,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL, TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE, G_MAXUINT);

  g_object_get (self->connection, "self", &(priv->self), NULL);
  g_object_unref (priv->self);
  g_assert (priv->self != NULL);

  g_assert (priv->muc_connection != NULL);

  priv->connected = FALSE;
  g_signal_connect (priv->muc_connection, "connected",
      G_CALLBACK (muc_connection_connected_cb), obj);

  g_object_get (self->connection,
      "muc-manager", &(priv->muc_manager),
      NULL);
  g_assert (priv->muc_manager != NULL);

  /* no need to keep a ref on the muc manager as it keeps a ref on us */
  g_object_unref (priv->muc_manager);

  /* Connect to the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  tp_group_mixin_init (obj, G_STRUCT_OFFSET(SalutMucChannel, group),
      contact_repo, base_conn->self_handle);

  tp_group_mixin_change_flags (obj,
       TP_CHANNEL_GROUP_FLAG_CAN_ADD|TP_CHANNEL_GROUP_FLAG_MESSAGE_ADD, 0);

  return obj;
}

static void
salut_muc_channel_init (SalutMucChannel *self)
{
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);
  /* allocate any data required by the object here */
  priv->object_path = NULL;
  self->connection = NULL;
  priv->xmpp_connection_manager = NULL;
  priv->muc_name = NULL;
  priv->timeout = 0;
  priv->senders = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) g_object_unref);
}

static void salut_muc_channel_dispose (GObject *object);
static void salut_muc_channel_finalize (GObject *object);

static void
invitation_append_parameter (gpointer key, gpointer value, gpointer data)
{
  GibberXmppNode *node = (GibberXmppNode *) data;
  gibber_xmpp_node_add_child_with_content (node, (gchar *) key,
      (gchar *) value);
}

static GibberXmppStanza *
create_invitation (SalutMucChannel *self, TpHandle handle,
    const gchar *message)
{
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION(self->connection);
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_ROOM);
  GibberXmppStanza *msg;
  GibberXmppNode *invite_node;

  const gchar *name = tp_handle_inspect (contact_repo, handle);

  msg = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_MESSAGE,
      GIBBER_STANZA_SUB_TYPE_NORMAL,
      self->connection->name, name,
      GIBBER_NODE, "body",
        GIBBER_NODE_TEXT, "You got a Clique chatroom invitation",
      GIBBER_NODE_END,
      GIBBER_NODE, "invite",
        GIBBER_NODE_ASSIGN_TO, &invite_node,
        GIBBER_NODE_XMLNS, GIBBER_TELEPATHY_NS_CLIQUE,
        GIBBER_NODE, "roomname",
          GIBBER_NODE_TEXT, tp_handle_inspect (room_repo, priv->handle),
        GIBBER_NODE_END,
      GIBBER_NODE_END,
      GIBBER_STANZA_END);

  if (message != NULL && *message != '\0')
    {
      gibber_xmpp_node_add_child_with_content (invite_node, "reason", message);
    }

  g_hash_table_foreach (
      (GHashTable *) gibber_muc_connection_get_parameters (
          priv->muc_connection),
      invitation_append_parameter, invite_node);

#ifdef ENABLE_OLPC
  salut_self_olpc_augment_invitation (priv->self, priv->handle, handle,
      invite_node);
#endif

  return msg;
}

gboolean
salut_muc_channel_publish_service (SalutMucChannel *self)
{
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);

  return SALUT_MUC_CHANNEL_GET_CLASS (self)->publish_service (self,
      priv->muc_connection, priv->muc_name);
}

struct
pending_connection_for_invite_data
{
  SalutMucChannel *self;
  SalutContact *contact;
  GibberXmppStanza *invite;
};

static struct pending_connection_for_invite_data *
pending_connection_for_invite_data_new (void)
{
  return g_slice_new (struct pending_connection_for_invite_data);
}

static void
pending_connection_for_invite_data_free (
    struct pending_connection_for_invite_data *data)
{
  g_object_unref (data->invite);
  g_slice_free (struct pending_connection_for_invite_data, data);
}

static void
xmpp_connection_manager_new_connection_cb (SalutXmppConnectionManager *mgr,
                                           GibberXmppConnection *connection,
                                           SalutContact *contact,
                                           gpointer user_data)
{
  struct pending_connection_for_invite_data *data =
    (struct pending_connection_for_invite_data *) user_data;

  if (data->contact != contact)
    /* Not the connection we are waiting for */
    return;

  DEBUG ("got awaited connection with %s. Send invite", contact->name);

  gibber_xmpp_connection_send (connection, data->invite, NULL);

  g_signal_handlers_disconnect_matched (mgr, G_SIGNAL_MATCH_DATA, 0, 0, NULL,
      NULL, data);

  pending_connection_for_invite_data_free (data);
}

static void
xmpp_connection_manager_connection_failed_cb (SalutXmppConnectionManager *mgr,
                                              GibberXmppConnection *connection,
                                              SalutContact *contact,
                                              GQuark domain,
                                              gint code,
                                              gchar *message,
                                              gpointer user_data)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (user_data);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION (self->connection);
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_CONTACT);
  struct pending_connection_for_invite_data *data =
    (struct pending_connection_for_invite_data *) user_data;
  TpHandle handle;
  TpIntSet *empty, *removed;

  if (data->contact != contact)
    /* Not the connection we are waiting for */
    return;

  DEBUG ("awaited connection with %s failed: %s. Can't send invite",
      contact->name, message);


  g_signal_handlers_disconnect_matched (mgr, G_SIGNAL_MATCH_DATA, 0, 0, NULL,
      NULL, data);

  pending_connection_for_invite_data_free (data);

  handle = tp_handle_lookup (contact_repo, contact->name, NULL, NULL);
  if (handle == 0)
    return;

  /* Can't invite the contact, remove it from remote pending */
  empty = tp_intset_new ();
  removed = tp_intset_new ();
  tp_intset_add (removed, handle);
  tp_group_mixin_change_members (G_OBJECT(self), "", empty, removed, empty,
      empty, base_connection->self_handle,
      TP_CHANNEL_GROUP_CHANGE_REASON_ERROR);
}

gboolean
salut_muc_channel_send_invitation (SalutMucChannel *self,
                                   TpHandle handle,
                                   const gchar *message,
                                   GError **error)
{
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);
  GibberXmppStanza *stanza;
  SalutContactManager *contact_manager = NULL;
  SalutContact *contact;
  SalutXmppConnectionManagerRequestConnectionResult request_result;
  gboolean result;
  struct pending_connection_for_invite_data *data;
  GibberXmppConnection *connection = NULL;

  g_object_get (G_OBJECT (self->connection), "contact-manager",
      &contact_manager, NULL);
  g_assert (contact_manager != NULL);

  contact = salut_contact_manager_get_contact (contact_manager, handle);
  g_object_unref (contact_manager);

  if (contact == NULL)
    {
      *error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Couldn't contact the contact");
      return FALSE;
    }

  DEBUG ("request XMPP connection with contact %s", contact->name);
  request_result = salut_xmpp_connection_manager_request_connection (
      priv->xmpp_connection_manager, contact, &connection, error);

  if (request_result ==
      SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_FAILURE)
    {
      DEBUG ("request connection failed");
      return FALSE;
    }

  stanza = create_invitation (self, handle, message);

  if (request_result ==
      SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_DONE)
    {
      DEBUG ("got an existing connection. Send the invite");
      result = gibber_xmpp_connection_send (connection, stanza, error);
      g_object_unref (stanza);
      return TRUE;
    }

  DEBUG ("requested connection pending. We have to wait to send the invite");
  data = pending_connection_for_invite_data_new ();
  data->self = self;
  data->contact = contact;
  data->invite = stanza;

  g_signal_connect (priv->xmpp_connection_manager, "new-connection",
      G_CALLBACK (xmpp_connection_manager_new_connection_cb), data);
  g_signal_connect (priv->xmpp_connection_manager, "connection-failed",
      G_CALLBACK (xmpp_connection_manager_connection_failed_cb), data);

  return TRUE;
}

/* FIXME: This is an ugly workaround. See fd.o #15092
 * We shouldn't export this function */
gboolean
salut_muc_channel_add_member (GObject *iface,
                              TpHandle handle,
                              const gchar *message,
                              GError **error)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL(iface);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION (self->connection);
  TpIntSet *empty, *remote_pending;

  if (handle == base_connection->self_handle)
    {
      /* adding yourself, let's join the muc */
      TpIntSet *empty;
      TpIntSet *add;
      gboolean ret = TRUE;

      if (tp_handle_set_is_member (self->group.remote_pending,
          base_connection->self_handle))
        {
          /* Already in remote pending, no need to redo */
          return TRUE;
        }

      empty = tp_intset_new ();
      add = tp_intset_new ();
      tp_intset_add (add, handle);
      /* Add to members */

      if (salut_muc_channel_connect (self, NULL))
        {
          /* We are considered as remote-pending while the muc connection
           * is not connected */
          tp_group_mixin_change_members (G_OBJECT (self),
              message, empty, empty, empty, add, base_connection->self_handle,
              TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
        }
      else
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
              "Failed to connect to the group");
          ret = FALSE;
        }

      tp_intset_destroy (empty);
      tp_intset_destroy (add);
      return ret;
    }

  /* Adding a contact, let's invite him */

  DEBUG ("Trying to add handle %u to %s", handle ,priv->object_path);

  if (!salut_muc_channel_send_invitation (self, handle, message, error))
    return FALSE;

  /* Set the contact as remote pending */
  empty = tp_intset_new ();
  remote_pending = tp_intset_new ();
  tp_intset_add (remote_pending, handle);
  tp_group_mixin_change_members (G_OBJECT(self), "", empty, empty, empty,
      remote_pending, base_connection->self_handle,
      TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);
  tp_intset_destroy (empty);
  tp_intset_destroy (remote_pending);

  return TRUE;
}

static void
salut_muc_channel_class_init (SalutMucChannelClass *salut_muc_channel_class) {
  GObjectClass *object_class = G_OBJECT_CLASS (salut_muc_channel_class);
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

  g_type_class_add_private (salut_muc_channel_class,
      sizeof (SalutMucChannelPrivate));

  object_class->dispose = salut_muc_channel_dispose;
  object_class->finalize = salut_muc_channel_finalize;

  object_class->constructor = salut_muc_channel_constructor;
  object_class->get_property = salut_muc_channel_get_property;
  object_class->set_property = salut_muc_channel_set_property;

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
      "The contact which invited us to the MUC, or ourselves if we joined of "
      "our own accord",
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

  param_spec = g_param_spec_string ("name",
                                    "Name of the muc group",
                                    "The name of the muc group",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class,
                                   PROP_NAME, param_spec);

  param_spec = g_param_spec_object ("muc-connection",
                                    "The GibberMucConnection",
                                    "muc connection  object",
                                    GIBBER_TYPE_MUC_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class,
                                   PROP_MUC_CONNECTION, param_spec);
  param_spec = g_param_spec_object ("connection",
                                    "SalutConnection object",
                                    "Salut Connection that owns the"
                                    "connection for this IM channel",
                                    SALUT_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class,
                                   PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_object (
      "xmpp-connection-manager",
      "SalutXmppConnectionManager object",
      "Salut XMPP Connection manager used for this MUC channel",
      SALUT_TYPE_XMPP_CONNECTION_MANAGER,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_XMPP_CONNECTION_MANAGER,
      param_spec);

  param_spec = g_param_spec_boolean (
      "creator",
      "creator",
      "Whether or not we created this muc",
      FALSE,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class,
      PROP_CREATOR, param_spec);

  param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
      "Additional Channel.Interface.* interfaces",
      G_TYPE_STRV,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NAME);
  g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);


  signals[READY] = g_signal_new (
        "ready",
        G_OBJECT_CLASS_TYPE (salut_muc_channel_class),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL,
        g_cclosure_marshal_VOID__VOID,
        G_TYPE_NONE, 0);

  signals[JOIN_ERROR] = g_signal_new (
        "join-error",
        G_OBJECT_CLASS_TYPE (salut_muc_channel_class),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL,
        g_cclosure_marshal_VOID__POINTER,
        G_TYPE_NONE, 1, G_TYPE_POINTER);

  salut_muc_channel_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (SalutMucChannelClass, dbus_props_class));

  tp_text_mixin_class_init (object_class,
      G_STRUCT_OFFSET(SalutMucChannelClass, text_class));

  tp_group_mixin_class_init (object_class,
      G_STRUCT_OFFSET(SalutMucChannelClass, group_class),
      salut_muc_channel_add_member, NULL);
  tp_group_mixin_init_dbus_properties (object_class);
}

void
salut_muc_channel_dispose (GObject *object)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (object);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_signal_handlers_disconnect_matched (priv->muc_connection,
      G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);

  if (priv->muc_connection != NULL)
    {
      g_object_unref (priv->muc_connection);
      priv->muc_connection = NULL;
    }

  if (priv->timeout != 0)
    {
      g_source_remove (priv->timeout);
      priv->timeout = 0;
    }

  if (priv->senders != NULL)
    {
      g_hash_table_destroy (priv->senders);
      priv->senders = NULL;
    }

  /* release any references held by the object here */
  if (G_OBJECT_CLASS (salut_muc_channel_parent_class)->dispose)
    G_OBJECT_CLASS (salut_muc_channel_parent_class)->dispose (object);
}

void
salut_muc_channel_finalize (GObject *object)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (object);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  g_free (priv->object_path);
  g_free (priv->muc_name);

  tp_text_mixin_finalize (object);
  tp_group_mixin_finalize (object);

  G_OBJECT_CLASS (salut_muc_channel_parent_class)->finalize (object);
}

gboolean
salut_muc_channel_invited (SalutMucChannel *self, TpHandle inviter,
                          const gchar *stanza, GError **error)
{
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION (self->connection);
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_ROOM);
  gboolean ret = TRUE;

  /* Got invited to this muc channel */
  DEBUG ("Got an invitation to %s from %s",
      tp_handle_inspect (room_repo, priv->handle),
        tp_handle_inspect (contact_repo, inviter));

  /* If we are already a member, no further actions are needed */
  if (tp_handle_set_is_member (self->group.members,
      base_connection->self_handle)) {
    return TRUE;
  }

  if (inviter == base_connection->self_handle)
    {
      /* Invited ourselves, go straight to members */
      gboolean r;
      GArray *members = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle), 1);
      g_array_append_val (members, base_connection->self_handle);
      r = tp_group_mixin_add_members (G_OBJECT (self), members, "", error);
      g_assert (r);
      g_array_free (members, TRUE);
    }
  else
    {
      TpIntSet *empty = tp_intset_new ();
      TpIntSet *local_pending = tp_intset_new ();

      g_assert (stanza != NULL);

      tp_intset_add (local_pending, base_connection->self_handle);
      tp_group_mixin_change_members (G_OBJECT(self), stanza,
                                     empty, empty,
                                     local_pending, empty,
                                     inviter,
                                     TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);
      tp_intset_destroy (local_pending);
      tp_intset_destroy (empty);
    }

  return ret;
}

/* Private functions */
static gboolean
salut_muc_channel_send_stanza (SalutMucChannel *self, guint type,
                              const gchar *text,
                              GibberXmppStanza *stanza,
                              GError **error)
{
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);

  if (!gibber_muc_connection_send (priv->muc_connection, stanza, error)) {
    tp_svc_channel_type_text_emit_send_error (self,
       TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN, time (NULL), type, text);
    return FALSE;
  }

  tp_svc_channel_type_text_emit_sent (self, time (NULL), type, text);
  return TRUE;
}

static void
salut_muc_channel_add_members (SalutMucChannel *self,
                               GArray *members)
{
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);
  TpIntSet *empty, *changes;
  guint i;
  SalutContactManager *contact_mgr;

  empty = tp_intset_new ();
  changes = tp_intset_new ();

  g_object_get (G_OBJECT (self->connection), "contact-manager",
      &contact_mgr, NULL);
  g_assert (contact_mgr != NULL);

  for (i = 0; i < members->len; i++)
    {
      gchar *sender = g_array_index (members, gchar *, i);
      SalutContact *contact;

      contact = salut_contact_manager_ensure_contact (contact_mgr, sender);

      g_hash_table_insert (priv->senders, sender, contact);
      tp_intset_add (changes, contact->handle);
    }

  tp_group_mixin_change_members (G_OBJECT(self),
                                 "",
                                 changes,
                                 empty,
                                 empty, empty,
                                 0,
                                 TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
  tp_intset_destroy (changes);
  tp_intset_destroy (empty);
  g_object_unref (contact_mgr);
}

static void
salut_muc_channel_remove_members (SalutMucChannel *self,
                                  GArray *members)
{
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *base_connection =
      (TpBaseConnection *) (self->connection);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles
      (base_connection, TP_HANDLE_TYPE_CONTACT);
  TpIntSet *empty, *changes;
  guint i;

  empty = tp_intset_new ();
  changes = tp_intset_new ();

  for (i = 0; i < members->len; i++)
    {
      TpHandle handle;
      gchar *sender = g_array_index (members, gchar *, i);

      handle = tp_handle_lookup (contact_repo, sender, NULL, NULL);
      if (handle == 0)
        {
          DEBUG ("Lost sender (%s), but unknown contact", sender);
          continue;
        }

      g_hash_table_remove (priv->senders, sender);

      tp_intset_add (changes, handle);
    }

  tp_group_mixin_change_members (G_OBJECT(self),
                                 "",
                                 empty,
                                 changes,
                                 empty, empty,
                                 0,
                                 TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
  tp_intset_destroy (changes);
  tp_intset_destroy (empty);
}

static void
salut_muc_channel_received_stanza (GibberMucConnection *conn,
                                   const gchar *sender,
                                   GibberXmppStanza *stanza,
                                   gpointer user_data)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (user_data);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION (self->connection);
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_CONTACT);

  const gchar *from, *to, *body, *body_offset;
  TpChannelTextMessageType msgtype;
  TpHandle from_handle;
  GibberXmppNode *tubes_node;

  to = gibber_xmpp_node_get_attribute (stanza->node, "to");
  if (strcmp (to, priv->muc_name)) {
    DEBUG("Stanza to another muc group, discarding");
    return;
  }

  from_handle = tp_handle_lookup (contact_repo, sender, NULL, NULL);
  if (from_handle == 0)
    {
      /* FIXME, unknown contact.. Need some way to handle this safely,
       * just adding the contact is somewhat scary */
      DEBUG("Got stanza from unknown contact, discarding");
      return;
    }

#ifdef ENABLE_OLPC
  if (salut_connection_olpc_observe_muc_stanza (self->connection, priv->handle,
        from_handle, stanza))
    return;
#endif

  tubes_node = gibber_xmpp_node_get_child_ns (stanza->node, "tubes",
      GIBBER_TELEPATHY_NS_TUBES);
  if (tubes_node != NULL)
    {
      SalutTubesChannel *tubes_chan;

      tubes_chan = salut_muc_manager_ensure_tubes_channel (priv->muc_manager,
          priv->handle, from_handle);
      g_assert (tubes_chan != NULL);

      tubes_message_received (tubes_chan, sender, stanza);

      g_object_unref (tubes_chan);
    }

  if (!text_helper_parse_incoming_message (stanza, &from, &msgtype,
      &body, &body_offset))
    {
        DEBUG("Couldn't parse stanza");
        return;
    }

  if (body == NULL)
    {
      DEBUG("Message with an empty body");
      return;
    }

  /* FIXME validate the from and the to */
  tp_text_mixin_receive (G_OBJECT (self), msgtype, from_handle,
      time (NULL), body_offset);
}

static void
salut_muc_channel_new_senders (GibberMucConnection *connection,
                               GArray *senders,
                               gpointer user_data)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (user_data);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION (self->connection);

  salut_muc_channel_add_members (self, senders);
  if (!tp_handle_set_is_member (self->group.members,
      base_connection->self_handle))
    {
      DEBUG ("Got new senders. Adding myself as member");
      salut_muc_channel_add_self_to_members (self);
    }
}

static void
salut_muc_channel_lost_senders (GibberMucConnection *connection,
    GArray *senders, gpointer user_data)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (user_data);

  salut_muc_channel_remove_members (self, senders);
}

static gboolean
salut_muc_channel_connect (SalutMucChannel *channel,
                           GError **error)
{
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (channel);

  g_signal_connect (priv->muc_connection, "received-stanza",
      G_CALLBACK (salut_muc_channel_received_stanza), channel);

  g_signal_connect (priv->muc_connection, "disconnected",
      G_CALLBACK (salut_muc_channel_disconnected), channel);

  g_signal_connect (priv->muc_connection, "new-senders",
      G_CALLBACK (salut_muc_channel_new_senders), channel);

  g_signal_connect (priv->muc_connection, "lost-senders",
      G_CALLBACK (salut_muc_channel_lost_senders), channel);

  return gibber_muc_connection_connect (priv->muc_connection, error);
}

static void
salut_muc_channel_disconnected (GibberTransport *transport, gpointer user_data)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (user_data);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);

  if (priv->timeout != 0)
    {
      g_source_remove (priv->timeout);
      priv->timeout = 0;
    }

  if (!priv->connected)
    {
      /* FIXME the disconnected signal should give us an error */
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
          "can't join the muc" };
      g_signal_emit (self, signals[JOIN_ERROR], 0, &error);
    }

  tp_svc_channel_emit_closed (self);
}

void
salut_muc_channel_emit_closed (SalutMucChannel *self)
{
  tp_svc_channel_emit_closed (self);
}

/* channel interfaces */
/**
 * salut_muc_channel_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_muc_channel_get_interfaces (TpSvcChannel *iface,
    DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_interfaces (context,
    salut_muc_channel_interfaces);
}


/**
 * salut_muc_channel_get_handle
 *
 * Implements D-Bus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_muc_channel_get_handle (TpSvcChannel *iface,
    DBusGMethodInvocation *context)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (iface);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);

  tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_ROOM,
                                         priv->handle);
}
/**
 * salut_muc_channel_get_channel_type
 *
 * Implements D-Bus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_muc_channel_get_channel_type (TpSvcChannel *iface,
    DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      TP_IFACE_CHANNEL_TYPE_TEXT);
}

/**
 * salut_muc_channel_close
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_muc_channel_close (TpSvcChannel *iface, DBusGMethodInvocation *context)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (iface);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);

  gibber_muc_connection_disconnect (priv->muc_connection);

  tp_svc_channel_return_from_close (context);
}

static void
channel_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, salut_muc_channel_##x)
  IMPLEMENT (close);
  IMPLEMENT (get_channel_type);
  IMPLEMENT (get_handle);
  IMPLEMENT (get_interfaces);
#undef IMPLEMENT
}


/**
 * salut_muc_channel_send
 *
 * Implements D-Bus method Send
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_muc_channel_send (TpSvcChannelTypeText *channel,
                       guint type, const gchar * text,
                       DBusGMethodInvocation *context) {
  SalutMucChannel *self = SALUT_MUC_CHANNEL(channel);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(self);
  GError *error = NULL;
  GibberXmppStanza *stanza;

  if (type > TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE)
    {
      GError ierror = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
        "Invalid message type" };

      dbus_g_method_return_error (context, &ierror);

      return;
    }

  stanza = text_helper_create_message_groupchat (self->connection->name,
          priv->muc_name, type, text, &error);

  if (stanza == NULL) {
    dbus_g_method_return_error (context, error);
    g_error_free (error);
    return;
  }

  if (!salut_muc_channel_send_stanza (self, type, text, stanza, &error)) {
    g_object_unref (G_OBJECT (stanza));
    dbus_g_method_return_error (context, error);
    g_error_free (error);
  }

  g_object_unref (G_OBJECT (stanza));
  tp_svc_channel_type_text_return_from_send (context);
}

static void
text_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelTypeTextClass *klass = (TpSvcChannelTypeTextClass *)g_iface;

  tp_text_mixin_iface_init (g_iface, iface_data);
#define IMPLEMENT(x) tp_svc_channel_type_text_implement_##x (\
    klass, salut_muc_channel_##x)
  IMPLEMENT (send);
#undef IMPLEMENT
}

