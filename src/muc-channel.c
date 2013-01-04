/*
 * muc-channel.c - Source for SalutMucChannel
 * Copyright (C) 2006,2010 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
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
#include "muc-channel.h"

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef G_OS_UNIX
#include <arpa/inet.h>
#endif

#include <string.h>

#define DEBUG_FLAG DEBUG_MUC
#include "debug.h"

/* Maximum time to wait for others joining the group  */
#define CONNECTED_TIMEOUT 60 * 1000

#include <wocky/wocky.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include <gibber/gibber-bytestream-muc.h>
#include <gibber/gibber-muc-connection.h>
#include <gibber/gibber-transport.h>

#include "connection.h"
#include "contact-manager.h"
#include "self.h"
#include "muc-manager.h"
#include "util.h"

#include "text-helper.h"
#include "tube-stream.h"
#include "tube-dbus.h"

G_DEFINE_TYPE_WITH_CODE(SalutMucChannel, salut_muc_channel, TP_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
        tp_group_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_TEXT,
      tp_message_mixin_text_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_MESSAGES,
      tp_message_mixin_messages_iface_init);
)

static const char *salut_muc_channel_interfaces[] = {
  TP_IFACE_CHANNEL_INTERFACE_GROUP,
  TP_IFACE_CHANNEL_INTERFACE_MESSAGES,
  NULL
};

/* signal enum */
enum
{
    READY,
    JOIN_ERROR,
    NEW_TUBE,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_MUC_CONNECTION = 1,
  PROP_NAME,
  PROP_CREATOR,
  LAST_PROPERTY
};

struct _SalutMucChannelPrivate
{
  gboolean dispose_has_run;
  SalutSelf *self;
  GibberMucConnection *muc_connection;
  gchar *muc_name;
  gboolean connected;
  gboolean creator;
  guint timeout;
  /* (gchar *) -> (SalutContact *) */
  GHashTable *senders;

  gboolean autoclose;

  GHashTable *tubes;
};

/* Callback functions */
static gboolean salut_muc_channel_send_stanza (SalutMucChannel *self,
                                               guint type,
                                               const gchar *token,
                                               const gchar *text,
                                               WockyStanza *stanza,
                                               GError **error);
static void salut_muc_channel_received_stanza (GibberMucConnection *conn,
                                               const gchar *sender,
                                               WockyStanza *stanza,
                                               gpointer user_data);
static gboolean
salut_muc_channel_connect (SalutMucChannel *channel, GError **error);
static void salut_muc_channel_disconnected (GibberTransport *transport,
                                            gpointer user_data);
static void salut_muc_channel_send (GObject *channel,
                                    TpMessage *message,
                                    TpMessageSendingFlags flags);
static void salut_muc_channel_close (TpBaseChannel *base);

static void update_tube_info (SalutMucChannel *self);
static SalutTubeIface * create_new_tube (SalutMucChannel *self,
    TpTubeType type,
    TpHandle initiator,
    const gchar *service,
    GHashTable *parameters,
    guint64 tube_id,
    guint portnum,
    WockyStanza *iq_req,
    gboolean requested);

static void
salut_muc_channel_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  SalutMucChannel *chan = SALUT_MUC_CHANNEL (object);
  SalutMucChannelPrivate *priv = chan->priv;

  switch (property_id) {
    case PROP_NAME:
      g_value_set_string (value, priv->muc_name);
      break;
    case PROP_MUC_CONNECTION:
      g_value_set_object (value, priv->muc_connection);
      break;
    case PROP_CREATOR:
      g_value_set_boolean (value, priv->creator);
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
  SalutMucChannelPrivate *priv = chan->priv;

  switch (property_id) {
    case PROP_NAME:
      g_free (priv->muc_name);
      priv->muc_name = g_value_dup_string (value);
      break;
    case PROP_MUC_CONNECTION:
      priv->muc_connection = g_value_get_object (value);
      break;
    case PROP_CREATOR:
      priv->creator = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
salut_muc_channel_add_self_to_members (SalutMucChannel *self)
{
  SalutMucChannelPrivate *priv = self->priv;
  TpBaseConnection *base_conn = tp_base_channel_get_connection (
      TP_BASE_CHANNEL (self));
  TpIntset *empty;
  TpIntset *add;

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
  tp_intset_add (add, base_conn->self_handle);

  tp_group_mixin_change_members (G_OBJECT (self),
      "", add, empty, empty, empty, base_conn->self_handle,
      TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

  tp_intset_destroy (empty);
  tp_intset_destroy (add);
}

static gboolean
connected_timeout_cb (gpointer user_data)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (user_data);
  SalutMucChannelPrivate *priv = self->priv;

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
  SalutMucChannelPrivate *priv = self->priv;

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

#define NUM_SUPPORTED_MESSAGE_TYPES 3

static void
salut_muc_channel_constructed (GObject *obj)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (obj);
  SalutMucChannelPrivate *priv = self->priv;
  TpBaseConnection *base_conn = tp_base_channel_get_connection (
      TP_BASE_CHANNEL (self));
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      base_conn, TP_HANDLE_TYPE_CONTACT);
  TpChannelTextMessageType types[NUM_SUPPORTED_MESSAGE_TYPES] = {
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE,
  };
  const gchar * supported_content_types[] = {
      "text/plain",
      NULL
  };

  /* Parent constructed chain */
  void (*chain_up) (GObject *) =
    ((GObjectClass *) salut_muc_channel_parent_class)->constructed;

  if (chain_up != NULL)
    chain_up (obj);

  /* Message mixin initialisation */
  tp_message_mixin_init (obj, G_STRUCT_OFFSET (SalutMucChannel, message_mixin),
      base_conn);
  tp_message_mixin_implement_sending (obj, salut_muc_channel_send,
      NUM_SUPPORTED_MESSAGE_TYPES, types, 0,
      TP_DELIVERY_REPORTING_SUPPORT_FLAG_RECEIVE_FAILURES |
      TP_DELIVERY_REPORTING_SUPPORT_FLAG_RECEIVE_SUCCESSES,
      supported_content_types);

  g_object_get (base_conn,
      "self", &(priv->self),
      NULL);
  g_object_unref (priv->self);
  g_assert (priv->self != NULL);

  g_assert (priv->muc_connection != NULL);

  priv->connected = FALSE;
  g_signal_connect (priv->muc_connection, "connected",
      G_CALLBACK (muc_connection_connected_cb), obj);

  tp_group_mixin_init (obj, G_STRUCT_OFFSET(SalutMucChannel, group),
      contact_repo, base_conn->self_handle);

  tp_group_mixin_change_flags (obj,
      TP_CHANNEL_GROUP_FLAG_PROPERTIES |
      TP_CHANNEL_GROUP_FLAG_CAN_ADD |
      TP_CHANNEL_GROUP_FLAG_MESSAGE_ADD,
      0);

  priv->tubes = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) g_object_unref);
}

static void
salut_muc_channel_init (SalutMucChannel *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, SALUT_TYPE_MUC_CHANNEL,
      SalutMucChannelPrivate);

  /* allocate any data required by the object here */
  self->priv->muc_name = NULL;
  self->priv->timeout = 0;
  self->priv->senders = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) g_object_unref);
}

static void salut_muc_channel_dispose (GObject *object);
static void salut_muc_channel_finalize (GObject *object);

static void
invitation_append_parameter (gpointer key, gpointer value, gpointer data)
{
  WockyNode *node = (WockyNode *) data;
  wocky_node_add_child_with_content (node, (gchar *) key,
      (gchar *) value);
}

static WockyStanza *
create_invitation (SalutMucChannel *self,
    SalutContact *contact,
    const gchar *message)
{
  SalutMucChannelPrivate *priv = self->priv;
  TpBaseChannel *base_chan = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_connection = tp_base_channel_get_connection (
      base_chan);
  SalutConnection *conn = SALUT_CONNECTION (base_connection);
  TpHandleRepoIface *room_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_ROOM);
  WockyStanza *msg;
  WockyNode *invite_node;
  const gchar *target_id = tp_handle_inspect (room_repo,
      tp_base_channel_get_target_handle (base_chan));

  msg = wocky_stanza_build_to_contact (WOCKY_STANZA_TYPE_MESSAGE,
      WOCKY_STANZA_SUB_TYPE_NORMAL,
      conn->name, WOCKY_CONTACT (contact),
      '(', "body",
        '$', "You got a Clique chatroom invitation",
      ')',
      '(', "invite",
        '*', &invite_node,
        ':', WOCKY_TELEPATHY_NS_CLIQUE,
        '(', "roomname",
          '$', target_id,
        ')',
      ')',
      NULL);

  if (message != NULL && *message != '\0')
    {
      wocky_node_add_child_with_content (invite_node, "reason", message);
    }

  g_hash_table_foreach (
      (GHashTable *) gibber_muc_connection_get_parameters (
          priv->muc_connection),
      invitation_append_parameter, invite_node);

#ifdef ENABLE_OLPC
  salut_self_olpc_augment_invitation (priv->self,
      tp_base_channel_get_target_handle (base_chan), contact->handle,
      invite_node);
#endif

  return msg;
}

gboolean
salut_muc_channel_publish_service (SalutMucChannel *self)
{
  SalutMucChannelPrivate *priv = self->priv;

  return SALUT_MUC_CHANNEL_GET_CLASS (self)->publish_service (self,
      priv->muc_connection, priv->muc_name);
}

typedef struct
{
  SalutMucChannel *self;
  SalutContact *contact;
} SendInviteData;

static void
send_invite_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  WockyPorter *porter = WOCKY_PORTER (source_object);
  SendInviteData *data = user_data;
  TpBaseConnection *base_connection = tp_base_channel_get_connection (
      TP_BASE_CHANNEL (data->self));
  GError *error = NULL;
  TpHandle handle;
  TpIntset *empty, *removed;

  if (wocky_porter_send_finish (porter, result, &error))
    /* success */
    goto out;

  /* failure */
  DEBUG ("Failed to send stanza: %s", error->message);
  g_clear_error (&error);

  handle = data->contact->handle;

  empty = tp_intset_new ();
  removed = tp_intset_new ();
  tp_intset_add (removed, handle);

  tp_group_mixin_change_members (G_OBJECT (data->self), "", empty, removed, empty,
      empty, base_connection->self_handle,
      TP_CHANNEL_GROUP_CHANGE_REASON_ERROR);

  tp_intset_destroy (empty);
  tp_intset_destroy (removed);

out:
  g_object_unref (data->contact);
  g_slice_free (SendInviteData, data);
}

gboolean
salut_muc_channel_send_invitation (SalutMucChannel *self,
                                   TpHandle handle,
                                   const gchar *message,
                                   GError **error)
{
  WockyStanza *stanza;
  SalutContactManager *contact_manager = NULL;
  SalutContact *contact;
  TpBaseConnection *base_conn = tp_base_channel_get_connection (
      TP_BASE_CHANNEL (self));
  SalutConnection *conn = SALUT_CONNECTION (base_conn);
  WockyPorter *porter = conn->porter;
  SendInviteData *data;

  g_object_get (G_OBJECT (conn),
      "contact-manager", &contact_manager,
      NULL);
  g_assert (contact_manager != NULL);

  contact = salut_contact_manager_get_contact (contact_manager, handle);
  g_object_unref (contact_manager);

  if (contact == NULL)
    {
      *error = g_error_new (TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "Couldn't contact the contact");
      return FALSE;
    }

  DEBUG ("request XMPP connection with contact %s", contact->name);

  stanza = create_invitation (self, contact, message);

  data = g_slice_new0 (SendInviteData);
  data->self = self;
  data->contact = contact; /* steal the ref */

  wocky_porter_send_async (porter, stanza, NULL, send_invite_cb, data);

  g_object_unref (stanza);
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
  SalutMucChannel *self = SALUT_MUC_CHANNEL (iface);
  TpBaseConnection *base_connection = tp_base_channel_get_connection (
      TP_BASE_CHANNEL (self));
  TpIntset *empty, *remote_pending;

  if (handle == base_connection->self_handle)
    {
      /* adding yourself, let's join the muc */
      TpIntset *empty_;
      TpIntset *add;
      gboolean ret = TRUE;

      if (tp_handle_set_is_member (self->group.remote_pending,
          base_connection->self_handle))
        {
          /* Already in remote pending, no need to redo */
          return TRUE;
        }

      empty_ = tp_intset_new ();
      add = tp_intset_new ();
      tp_intset_add (add, handle);
      /* Add to members */

      if (salut_muc_channel_connect (self, NULL))
        {
          /* We are considered as remote-pending while the muc connection
           * is not connected */
          tp_group_mixin_change_members (G_OBJECT (self),
              message, empty_, empty_, empty_, add,
              base_connection->self_handle,
              TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
        }
      else
        {
          g_set_error (error, TP_ERROR, TP_ERROR_NETWORK_ERROR,
              "Failed to connect to the group");
          ret = FALSE;
        }

      tp_intset_destroy (empty_);
      tp_intset_destroy (add);
      return ret;
    }

  /* Adding a contact, let's invite him */

  DEBUG ("Trying to add handle %u to %s", handle,
      tp_base_channel_get_object_path (TP_BASE_CHANNEL (self)));

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
salut_muc_channel_leave (SalutMucChannel *self,
    TpChannelGroupChangeReason reason,
    const gchar *message)
{
  SalutMucChannelPrivate *priv = self->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (self);

  if (tp_base_channel_is_destroyed (base))
    return;

  if (priv->connected)
    {
      /* FIXME: send a part-message based on reason and message first,
       * once we've defined how */

      /* priv->closed will be set in salut_muc_channel_disconnected */
      gibber_muc_connection_disconnect (priv->muc_connection);
    }
  else
    {
      tp_base_channel_destroyed (base);
    }
}

static gboolean
salut_muc_channel_remove_member_with_reason (GObject *object,
    TpHandle handle,
    const gchar *message,
    guint reason,
    GError **error)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (object);
  TpBaseConnection *base_connection = tp_base_channel_get_connection (
      TP_BASE_CHANNEL (self));

  if (handle != base_connection->self_handle)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
          "Contacts cannot be kicked from Clique chatrooms");
      return FALSE;
    }

  salut_muc_channel_leave (self, reason, message);
  return TRUE;
}

static void
salut_muc_channel_fill_immutable_properties (TpBaseChannel *chan,
    GHashTable *properties)
{
  TpBaseChannelClass *cls = TP_BASE_CHANNEL_CLASS (
      salut_muc_channel_parent_class);

  cls->fill_immutable_properties (chan, properties);

  tp_dbus_properties_mixin_fill_properties_hash (
      G_OBJECT (chan), properties,
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "MessagePartSupportFlags",
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "DeliveryReportingSupport",
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "SupportedContentTypes",
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "MessageTypes",
      NULL);
}

static void
salut_muc_channel_class_init (SalutMucChannelClass *salut_muc_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_muc_channel_class);
  TpBaseChannelClass *base_class = TP_BASE_CHANNEL_CLASS (
      salut_muc_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_muc_channel_class,
      sizeof (SalutMucChannelPrivate));

  object_class->dispose = salut_muc_channel_dispose;
  object_class->finalize = salut_muc_channel_finalize;
  object_class->constructed = salut_muc_channel_constructed;
  object_class->get_property = salut_muc_channel_get_property;
  object_class->set_property = salut_muc_channel_set_property;

  base_class->channel_type = TP_IFACE_CHANNEL_TYPE_TEXT;
  base_class->interfaces = salut_muc_channel_interfaces;
  base_class->target_handle_type = TP_HANDLE_TYPE_ROOM;
  base_class->close = salut_muc_channel_close;
  base_class->fill_immutable_properties =
    salut_muc_channel_fill_immutable_properties;

  param_spec = g_param_spec_string ("name",
                                    "Name of the muc group",
                                    "The name of the muc group",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class,
                                   PROP_NAME, param_spec);

  param_spec = g_param_spec_object ("muc-connection",
                                    "The GibberMucConnection",
                                    "muc connection  object",
                                    GIBBER_TYPE_MUC_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class,
                                   PROP_MUC_CONNECTION, param_spec);

  param_spec = g_param_spec_boolean (
      "creator",
      "creator",
      "Whether or not we created this muc",
      FALSE,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class,
      PROP_CREATOR, param_spec);

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

  signals[NEW_TUBE] = g_signal_new (
      "new-tube",
      G_OBJECT_CLASS_TYPE (salut_muc_channel_class),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT,
      /* this should be SALUT_TYPE_TUBE_IFACE but GObject
       * wants a value type, not an interface. */
      G_TYPE_NONE, 1, TP_TYPE_BASE_CHANNEL);

  tp_group_mixin_class_init (object_class,
      G_STRUCT_OFFSET(SalutMucChannelClass, group_class),
      salut_muc_channel_add_member, NULL);
  tp_group_mixin_init_dbus_properties (object_class);

  tp_group_mixin_class_allow_self_removal (object_class);
  tp_group_mixin_class_set_remove_with_reason_func (object_class,
      salut_muc_channel_remove_member_with_reason);

  tp_message_mixin_init_dbus_properties (object_class);
}

void
salut_muc_channel_dispose (GObject *object)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (object);
  SalutMucChannelPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  priv->connected = FALSE;

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
      g_hash_table_unref (priv->senders);
      priv->senders = NULL;
    }

  tp_clear_pointer (&priv->tubes, g_hash_table_unref);

  /* release any references held by the object here */
  if (G_OBJECT_CLASS (salut_muc_channel_parent_class)->dispose)
    G_OBJECT_CLASS (salut_muc_channel_parent_class)->dispose (object);
}

void
salut_muc_channel_finalize (GObject *object)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (object);
  SalutMucChannelPrivate *priv = self->priv;

  /* free any data held directly by the object here */
  g_free (priv->muc_name);

  tp_group_mixin_finalize (object);
  tp_message_mixin_finalize (object);

  if (G_OBJECT_CLASS (salut_muc_channel_parent_class)->finalize)
    G_OBJECT_CLASS (salut_muc_channel_parent_class)->finalize (object);
}

gboolean
salut_muc_channel_invited (SalutMucChannel *self, TpHandle inviter,
                          const gchar *stanza, GError **error)
{
  TpBaseConnection *base_connection = tp_base_channel_get_connection (
      TP_BASE_CHANNEL (self));
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_CONTACT);
  gboolean ret = TRUE;

  /* Got invited to this muc channel */
  DEBUG ("Got an invitation to %s from %s",
      tp_handle_inspect (contact_repo,
          tp_base_channel_get_target_handle (TP_BASE_CHANNEL (self))),
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
      g_array_unref (members);
    }
  else
    {
      TpIntset *empty = tp_intset_new ();
      TpIntset *local_pending = tp_intset_new ();

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
                              const gchar *token,
                              const gchar *text,
                              WockyStanza *stanza,
                              GError **error)
{
  SalutMucChannelPrivate *priv = self->priv;

  if (!gibber_muc_connection_send (priv->muc_connection, stanza, error)) {
    text_helper_report_delivery_error (TP_SVC_CHANNEL (self),
       TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN, time (NULL), type, token, text);
    return FALSE;
  }

  return TRUE;
}

static void
salut_muc_channel_add_members (SalutMucChannel *self,
                               GArray *members)
{
  SalutMucChannelPrivate *priv = self->priv;
  TpBaseConnection *base_conn = tp_base_channel_get_connection (
      TP_BASE_CHANNEL (self));
  TpIntset *empty, *changes;
  guint i;
  SalutContactManager *contact_mgr;

  empty = tp_intset_new ();
  changes = tp_intset_new ();

  g_object_get (G_OBJECT (base_conn),
      "contact-manager", &contact_mgr,
      NULL);
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
  SalutMucChannelPrivate *priv = self->priv;
  TpBaseChannel *base_chan = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_connection = tp_base_channel_get_connection (
      base_chan);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      base_connection, TP_HANDLE_TYPE_CONTACT);
  TpIntset *empty, *changes;
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

/* tube_node is a MUC <message> */
static gboolean
extract_tube_information (SalutMucChannel *self,
                          WockyNode *tube_node,
                          TpTubeType *type,
                          TpHandle *initiator_handle,
                          const gchar **service,
                          GHashTable **parameters,
                          guint *tube_id)
{
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      base_conn, TP_HANDLE_TYPE_CONTACT);

  if (type != NULL)
    {
      const gchar *_type;

      _type = wocky_node_get_attribute (tube_node, "type");


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

      initiator = wocky_node_get_attribute (tube_node, "initiator");

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
      *service = wocky_node_get_attribute (tube_node, "service");
    }

  if (parameters != NULL)
    {
      WockyNode *node;

      node = wocky_node_get_child (tube_node, "parameters");
      *parameters = salut_wocky_node_extract_properties (node,
          "parameter");
    }

  if (tube_id != NULL)
    {
      const gchar *str;
      gchar *endptr;
      long int tmp;

      str = wocky_node_get_attribute (tube_node, "id");
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
muc_channel_handle_tubes (SalutMucChannel *self,
    TpHandle contact,
    WockyStanza *stanza)
{
  SalutMucChannelPrivate *priv = self->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles (base_conn,
          TP_HANDLE_TYPE_CONTACT);
  const gchar *sender;
  WockyStanzaType stanza_type;
  WockyStanzaSubType sub_type;
  GHashTable *old_dbus_tubes;
  GHashTableIter iter;
  gpointer key, value;
  GSList *l;
  WockyNode *tubes_node;

  if (contact == TP_GROUP_MIXIN (self)->self_handle)
    /* we don't need to inspect our own tubes */
    return;

  sender = tp_handle_inspect (contact_repo, contact);

  wocky_stanza_get_type_info (stanza, &stanza_type, &sub_type);
  if (stanza_type != WOCKY_STANZA_TYPE_MESSAGE
      || sub_type != WOCKY_STANZA_SUB_TYPE_GROUPCHAT)
    return;

  tubes_node = wocky_node_get_child_ns (
      wocky_stanza_get_top_node (stanza), "tubes",
      WOCKY_TELEPATHY_NS_TUBES);
  g_assert (tubes_node != NULL);

  /* fill old_dbus_tubes with D-Bus tubes previously announced by the
   * contact */
  old_dbus_tubes = g_hash_table_new (NULL, NULL);
  g_hash_table_iter_init (&iter, priv->tubes);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      guint tube_id = GPOINTER_TO_UINT (key);
      SalutTubeIface *tube = value;
      TpTubeType type;

      g_object_get (tube,
          "type", &type,
          NULL);

      if (type != TP_TUBE_TYPE_DBUS)
        return;

      if (salut_tube_dbus_handle_in_names (SALUT_TUBE_DBUS (tube),
              contact))
        {
          g_hash_table_insert (old_dbus_tubes, GUINT_TO_POINTER (tube_id), tube);
        }
    }

  for (l = tubes_node->children; l != NULL; l = l->next)
    {
      WockyNode *tube_node = (WockyNode *) l->data;
      const gchar *stream_id;
      SalutTubeIface *tube;
      guint tube_id;
      TpTubeType type;
      GibberBytestreamIface *bytestream;

      stream_id = wocky_node_get_attribute (tube_node, "stream-id");

      if (!extract_tube_information (self, tube_node, NULL,
              NULL, NULL, NULL, &tube_id))
        continue;

      tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));

      if (tube == NULL)
        {
          /* a new tube */
          const gchar *service;
          TpHandle initiator_handle;
          GHashTable *parameters;
          guint id;

          if (extract_tube_information (self, tube_node, &type,
                  &initiator_handle, &service, &parameters, &id))
            {
              switch (type)
                {
                case TP_TUBE_TYPE_DBUS:
                  {
                    if (initiator_handle == 0)
                      {
                        DEBUG ("D-Bus tube initiator missing");
                        continue;
                      }
                  }
                  break;
                case TP_TUBE_TYPE_STREAM:
                  initiator_handle = contact;
                  break;
                default:
                  g_assert_not_reached ();
                }

              tube = create_new_tube (self, type, initiator_handle, service, parameters,
                  id, 0, NULL, FALSE);

              g_signal_emit (self, signals[NEW_TUBE], 0, tube);

              g_hash_table_unref (parameters);
            }
        }
      else
        {
          /* the contact is in the tube.
           * remove it from old_dbus_tubes if needed. */
          g_hash_table_remove (old_dbus_tubes, GUINT_TO_POINTER (tube_id));
        }

      if (tube == NULL)
        continue;

      g_object_get (tube,
          "type", &type,
          NULL);

      if (type == TP_TUBE_TYPE_DBUS
          && !salut_tube_dbus_handle_in_names (SALUT_TUBE_DBUS (tube),
              contact))
        {
          /* contact just joined the tube */
          const gchar *new_name;

          new_name = wocky_node_get_attribute (tube_node, "dbus-name");

          if (new_name == NULL)
            {
              DEBUG ("Contact %u isn't announcing his or her D-Bus name", contact);
              continue;
            }

          salut_tube_dbus_add_name (SALUT_TUBE_DBUS (tube), contact, new_name);

          g_object_get (tube,
              "bytestream", &bytestream,
              NULL);
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

  g_hash_table_iter_init (&iter, old_dbus_tubes);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      SalutTubeDBus *tube = SALUT_TUBE_DBUS (value);
      GibberBytestreamIface *bytestream;

      salut_tube_dbus_remove_name (tube, contact);

      g_object_get (tube,
          "bytestream", &bytestream,
          NULL);
      g_assert (bytestream != NULL);

      if (GIBBER_IS_BYTESTREAM_MUC (bytestream) && sender != NULL)
        {
          gibber_bytestream_muc_remove_sender (
              GIBBER_BYTESTREAM_MUC (bytestream), sender);
        }

      g_object_unref (bytestream);
    }
}

static void
salut_muc_channel_received_stanza (GibberMucConnection *conn,
                                   const gchar *sender,
                                   WockyStanza *stanza,
                                   gpointer user_data)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (user_data);
  SalutMucChannelPrivate *priv = self->priv;
  TpBaseChannel *base_chan = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_connection = tp_base_channel_get_connection (
      base_chan);
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_CONTACT);

  const gchar *from, *to, *body, *body_offset;
  TpChannelTextMessageType msgtype;
  TpHandle from_handle;
  WockyNode *node = wocky_stanza_get_top_node (stanza);
  WockyNode *tubes_node;

  to = wocky_node_get_attribute (node, "to");

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

  /* are we actually hidden? */
  if (!tp_base_channel_is_registered (base_chan))
    {
      DEBUG ("making MUC channel reappear!");
      tp_base_channel_reopened_with_requested (base_chan, FALSE, from_handle);
    }

  /* let's not autoclose now */
  priv->autoclose = FALSE;

#ifdef ENABLE_OLPC
  if (salut_connection_olpc_observe_muc_stanza (
          SALUT_CONNECTION (base_connection),
          tp_base_channel_get_target_handle (base_chan),
          from_handle, stanza))
    return;
#endif

  tubes_node = wocky_node_get_child_ns (node, "tubes",
      WOCKY_TELEPATHY_NS_TUBES);
  if (tubes_node != NULL)
    {
      muc_channel_handle_tubes (self, from_handle, stanza);
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
  tp_message_mixin_take_received (G_OBJECT (self),
      text_helper_create_received_message (base_connection, from_handle,
          time (NULL), msgtype, body_offset));
}

static void
salut_muc_channel_new_senders (GibberMucConnection *connection,
                               GArray *senders,
                               gpointer user_data)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (user_data);
  TpBaseConnection *base_connection = tp_base_channel_get_connection (
      TP_BASE_CHANNEL (self));

  salut_muc_channel_add_members (self, senders);
  if (!tp_handle_set_is_member (self->group.members,
      base_connection->self_handle))
    {
      DEBUG ("Got new senders. Adding myself as member");
      salut_muc_channel_add_self_to_members (self);
    }

  update_tube_info (self);
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
  SalutMucChannelPrivate *priv = channel->priv;

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
  SalutMucChannelPrivate *priv = self->priv;

  if (priv->timeout != 0)
    {
      g_source_remove (priv->timeout);
      priv->timeout = 0;
    }

  if (!priv->connected)
    {
      /* FIXME the disconnected signal should give us an error */
      GError error = { TP_ERROR, TP_ERROR_NETWORK_ERROR,
          "can't join the muc" };
      g_signal_emit (self, signals[JOIN_ERROR], 0, &error);
    }

  tp_base_channel_destroyed (TP_BASE_CHANNEL (self));
}

gboolean
salut_muc_channel_can_be_closed (SalutMucChannel *self)
{
  if (self->priv->tubes == NULL)
    return TRUE;

  return (g_hash_table_size (self->priv->tubes) == 0);
}

gboolean
salut_muc_channel_get_autoclose (SalutMucChannel *self)
{
  return self->priv->autoclose;
}

void
salut_muc_channel_set_autoclose (SalutMucChannel *self,
    gboolean autoclose)
{
  self->priv->autoclose = autoclose;
}

static void
salut_muc_channel_close (TpBaseChannel *base)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (base);

  /* if we have some tubes around then don't close yet and just
   * disappear from the bus, faking having closed, otherwise
   * cheerio! */
  if (!salut_muc_channel_can_be_closed (self))
    {
      self->priv->autoclose = TRUE;
      tp_base_channel_disappear (base);
      return;
    }

  salut_muc_channel_leave (self, TP_CHANNEL_GROUP_CHANGE_REASON_NONE, "");

  tp_base_channel_destroyed (base);
}

static void
salut_muc_channel_send (GObject *channel,
                        TpMessage *message,
                        TpMessageSendingFlags flags)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (channel);
  SalutMucChannelPrivate *priv = self->priv;
  TpBaseConnection *base_conn = tp_base_channel_get_connection (
      TP_BASE_CHANNEL (self));
  SalutConnection *conn = SALUT_CONNECTION (base_conn);
  GError *error = NULL;
  WockyStanza *stanza = NULL;
  guint type;
  gchar *text = NULL;
  gchar *token = NULL;

  if (!text_helper_validate_tp_message (message, &type, &token, &text, &error))
    goto error;

  stanza = text_helper_create_message_groupchat (conn->name,
          priv->muc_name, type, text, &error);

  if (stanza == NULL)
    goto error;

  if (!salut_muc_channel_send_stanza (self, type, token,
      text, stanza, &error))
    goto error;

  tp_message_mixin_sent (channel, message, 0, token, NULL);
  g_free (token);
  g_object_unref (G_OBJECT (stanza));
  return;

error:
  if (stanza != NULL)
    g_object_unref (G_OBJECT (stanza));
  tp_message_mixin_sent (channel, message, 0, NULL, error);
  g_error_free (error);
  g_free (text);
  g_free (token);
  return;
}

static void
publish_tube_in_node (SalutMucChannel *self,
    WockyNode *node,
    SalutTubeIface *tube)
{
  TpBaseChannel *base = TP_BASE_CHANNEL (tube);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      base_conn, TP_HANDLE_TYPE_CONTACT);
  WockyNode *parameters_node;
  GHashTable *parameters;
  TpTubeType type;
  gchar *service, *id_str;
  guint64 tube_id;
  TpHandle initiator_handle;

  g_object_get (tube,
      "type", &type,
      "initiator-handle", &initiator_handle,
      "service", &service,
      "parameters", &parameters,
      "id", &tube_id,
      NULL);

  id_str = g_strdup_printf ("%" G_GUINT64_FORMAT, tube_id);

  wocky_node_set_attribute (node, "service", service);
  wocky_node_set_attribute (node, "id", id_str);

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

          wocky_node_set_attribute (node, "type", "dbus");
          wocky_node_set_attribute (node, "stream-id", stream_id);
          wocky_node_set_attribute (node, "initiator",
              tp_handle_inspect (contact_repo, initiator_handle));

          if (name != NULL)
            wocky_node_set_attribute (node, "dbus-name", name);

          g_free (name);
          g_free (stream_id);

        }
        break;
      case TP_TUBE_TYPE_STREAM:
        wocky_node_set_attribute (node, "type", "stream");
        break;
      default:
        g_assert_not_reached ();
    }

  parameters_node = wocky_node_add_child (node, "parameters");
  salut_wocky_node_add_children_from_properties (parameters_node,
      parameters, "parameter");

  g_free (service);
  g_hash_table_unref (parameters);
}

static void
update_tube_info (SalutMucChannel *self)
{
  SalutMucChannelPrivate *priv = self->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  SalutConnection *conn = SALUT_CONNECTION (base_conn);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      base_conn, TP_HANDLE_TYPE_ROOM);
  GHashTableIter iter;
  gpointer value;
  WockyStanza *msg;
  WockyNode *msg_node;
  WockyNode *node;
  const gchar *jid;
  GError *error = NULL;

  if (priv->tubes == NULL)
    return;

  /* build the message */
  jid = tp_handle_inspect (room_repo,
      tp_base_channel_get_target_handle (base));

  msg = wocky_stanza_build (WOCKY_STANZA_TYPE_MESSAGE,
      WOCKY_STANZA_SUB_TYPE_GROUPCHAT,
      conn->name, jid,
      WOCKY_NODE_START, "tubes",
        WOCKY_NODE_XMLNS, WOCKY_TELEPATHY_NS_TUBES,
      WOCKY_NODE_END, NULL);
  msg_node = wocky_stanza_get_top_node (msg);

  node = wocky_node_get_child_ns (msg_node, "tubes",
      WOCKY_TELEPATHY_NS_TUBES);

  g_hash_table_iter_init (&iter, priv->tubes);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      TpTubeChannelState state;
      TpTubeType type;
      TpHandle initiator;
      WockyNode *tube_node;

      g_object_get (value,
          "state", &state,
          "type", &type,
          "initiator-handle", &initiator,
          NULL);

      if (state != TP_TUBE_CHANNEL_STATE_OPEN)
        continue;

      if (type == TP_TUBE_TYPE_STREAM
          && initiator != TP_GROUP_MIXIN (self)->self_handle)
        /* We only announce stream tubes we initiated */
        return;

      tube_node = wocky_node_add_child (node, "tube");
      publish_tube_in_node (self, tube_node, value);
    }

  /* Send it */
  if (!gibber_muc_connection_send (priv->muc_connection, msg, &error))
    {
      g_warning ("%s: sending tubes info failed: %s", G_STRFUNC,
          error->message);
      g_error_free (error);
    }

  g_object_unref (msg);
}

static void
tube_opened_cb (SalutTubeIface *tube,
    SalutMucChannel *self)
{
  if (SALUT_IS_TUBE_DBUS (tube))
    {
      gchar *dbus_name;

      g_object_get (tube,
          "dbus-name", &dbus_name,
          NULL);

      salut_tube_dbus_add_name (SALUT_TUBE_DBUS (tube),
          TP_GROUP_MIXIN (self)->self_handle, dbus_name);

      g_free (dbus_name);
    }

  update_tube_info (self);
}

static void
tube_offered_cb (SalutTubeIface *tube,
    SalutMucChannel *self)
{
  update_tube_info (self);
}

static void
tube_closed_cb (SalutTubeIface *tube,
    SalutMucChannel *self)
{
  SalutMucChannelPrivate *priv = self->priv;
  guint64 id;

  g_object_get (tube,
      "id", &id,
      NULL);

  update_tube_info (self);

  if (priv->tubes != NULL)
    g_hash_table_remove (priv->tubes, GUINT_TO_POINTER (id));
}

static guint
generate_tube_id (SalutMucChannel *self)
{
  SalutMucChannelPrivate *priv = self->priv;
  guint out;

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
create_new_tube (SalutMucChannel *self,
    TpTubeType type,
    TpHandle initiator,
    const gchar *service,
    GHashTable *parameters,
    guint64 tube_id,
    guint portnum,
    WockyStanza *iq_req,
    gboolean requested)
{
  SalutMucChannelPrivate *priv = self->priv;
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  SalutConnection *conn = SALUT_CONNECTION (base_conn);
  TpHandle self_handle = TP_GROUP_MIXIN (self)->self_handle;
  TpHandle handle = tp_base_channel_get_target_handle (base);
  SalutTubeIface *tube;

  switch (type)
    {
    case TP_TUBE_TYPE_DBUS:
      tube = SALUT_TUBE_IFACE (salut_tube_dbus_new (conn,
              handle, TP_HANDLE_TYPE_ROOM, self_handle, priv->muc_connection,
              initiator, service, parameters, tube_id, requested));
      break;
    case TP_TUBE_TYPE_STREAM:
      tube = SALUT_TUBE_IFACE (salut_tube_stream_new (conn,
              handle, TP_HANDLE_TYPE_ROOM,
              self_handle, initiator, FALSE, service,
              parameters, tube_id, portnum, iq_req, requested));
      break;
    default:
      g_return_val_if_reached (NULL);
    }

  tp_base_channel_register ((TpBaseChannel *) tube);

  DEBUG ("create tube %" G_GUINT64_FORMAT, tube_id);
  g_hash_table_insert (priv->tubes, GUINT_TO_POINTER (tube_id), tube);

  g_signal_connect (tube, "tube-opened", G_CALLBACK (tube_opened_cb), self);
  g_signal_connect (tube, "tube-offered", G_CALLBACK (tube_offered_cb), self);
  g_signal_connect (tube, "closed", G_CALLBACK (tube_closed_cb), self);

  return tube;
}

SalutTubeIface *
salut_muc_channel_tube_request (SalutMucChannel *self,
    GHashTable *request_properties)
{
  SalutTubeIface *tube;
  const gchar *channel_type;
  const gchar *service;
  GHashTable *parameters = NULL;
  guint64 tube_id;
  TpTubeType type;

  tube_id = generate_tube_id (self);

  channel_type = tp_asv_get_string (request_properties,
      TP_PROP_CHANNEL_CHANNEL_TYPE);

  if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE))
    {
      type = TP_TUBE_TYPE_STREAM;
      service = tp_asv_get_string (request_properties,
          TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE);

    }
  else if (! tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE))
    {
      type = TP_TUBE_TYPE_DBUS;
      service = tp_asv_get_string (request_properties,
          TP_PROP_CHANNEL_TYPE_DBUS_TUBE_SERVICE_NAME);
    }
  else
    /* This assertion is safe: this function's caller only calls it in one of
     * the above cases.
     * FIXME: but it would be better to pass an enum member or something maybe.
     */
    g_assert_not_reached ();

  /* requested tubes have an empty parameters dict */
  parameters = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) tp_g_value_slice_free);

  /* if the service property is missing, the requestotron rejects the request
   */
  g_assert (service != NULL);

  DEBUG ("Request a tube channel with type='%s' and service='%s'",
      channel_type, service);

  tube = create_new_tube (self, type, TP_GROUP_MIXIN (self)->self_handle,
      service, parameters, tube_id, 0, NULL, TRUE);
  g_hash_table_unref (parameters);

  return tube;
}

void
salut_muc_channel_foreach (SalutMucChannel *self,
    TpExportableChannelFunc func,
    gpointer user_data)
{
  SalutMucChannelPrivate *priv = self->priv;
  GHashTableIter iter;
  gpointer value;

  g_hash_table_iter_init (&iter, priv->tubes);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      func (TP_EXPORTABLE_CHANNEL (value), user_data);
    }
}

void
salut_muc_channel_bytestream_offered (SalutMucChannel *self,
    GibberBytestreamIface *bytestream,
    WockyStanza *msg)
{
  SalutMucChannelPrivate *priv = self->priv;
  WockyNode *node = wocky_stanza_get_top_node (msg);
  const gchar *stream_id, *tmp;
  WockyNode *si_node, *stream_node;
  guint64 tube_id;
  SalutTubeIface *tube;
  WockyStanzaType type;
  WockyStanzaSubType sub_type;

  /* Caller is expected to have checked that we have a stream or muc-stream
   * node with a stream ID and the TUBES profile
   */
  wocky_stanza_get_type_info (msg, &type, &sub_type);
  g_return_if_fail (type == WOCKY_STANZA_TYPE_IQ);
  g_return_if_fail (sub_type == WOCKY_STANZA_SUB_TYPE_SET);

  si_node = wocky_node_get_child_ns (node, "si",
      WOCKY_XMPP_NS_SI);
  g_return_if_fail (si_node != NULL);

  stream_node = wocky_node_get_child_ns (si_node,
      "muc-stream", WOCKY_TELEPATHY_NS_TUBES);
  g_return_if_fail (stream_node != NULL);

  stream_id = wocky_node_get_attribute (si_node, "id");
  g_return_if_fail (stream_id != NULL);

  tmp = wocky_node_get_attribute (stream_node, "tube");
  if (tmp == NULL)
    {
      GError e = { WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "<muc-stream> has no tube attribute" };

      DEBUG ("%s", e.message);
      gibber_bytestream_iface_close (bytestream, &e);
      return;
    }
  tube_id = g_ascii_strtoull (tmp, NULL, 10);
  if (tube_id == 0 || tube_id > G_MAXUINT32)
    {
      GError e = { WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "<muc-stream> tube attribute is non-numeric or out of range" };

      DEBUG ("tube id is non-numeric or out of range: %s", tmp);
      gibber_bytestream_iface_close (bytestream, &e);
      return;
    }

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));
  if (tube == NULL)
    {
      GError e = { WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "<muc-stream> tube attribute points to a nonexistent "
          "tube" };

      DEBUG ("tube %" G_GUINT64_FORMAT " doesn't exist", tube_id);
      gibber_bytestream_iface_close (bytestream, &e);
      return;
    }

  DEBUG ("received new bytestream request for existing tube: %" G_GUINT64_FORMAT,
      tube_id);

  salut_tube_iface_add_bytestream (tube, bytestream);
}

gboolean
salut_muc_channel_is_ready (SalutMucChannel *self)
{
  return self->priv->connected;
}
