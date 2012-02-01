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

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include <string.h>

#define DEBUG_FLAG DEBUG_MUC
#include "debug.h"

/* Maximum time to wait for others joining the group  */
#define CONNECTED_TIMEOUT 60 * 1000

#include "muc-channel.h"

#include <wocky/wocky-namespaces.h>

#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/util.h>

#include <gibber/gibber-muc-connection.h>
#include <gibber/gibber-transport.h>

#include "connection.h"
#include "contact-manager.h"
#include "self.h"
#include "muc-manager.h"
#include "util.h"

#include "text-helper.h"
#include "tube-stream.h"

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
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_MUC_CONNECTION = 1,
  PROP_NAME,
  LAST_PROPERTY
};

/* private structure */
typedef struct _SalutMucChannelPrivate SalutMucChannelPrivate;

struct _SalutMucChannelPrivate
{
  gboolean dispose_has_run;
  SalutSelf *self;
  GibberMucConnection *muc_connection;
  gchar *muc_name;
  gboolean connected;
  guint timeout;
  /* (gchar *) -> (SalutContact *) */
  GHashTable *senders;
  SalutMucManager *muc_manager;
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

  if (tp_base_channel_is_requested (TP_BASE_CHANNEL (self)))
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

  g_object_get (base_conn, "self", &(priv->self), NULL);
  g_object_unref (priv->self);
  g_assert (priv->self != NULL);

  g_assert (priv->muc_connection != NULL);

  priv->connected = FALSE;
  g_signal_connect (priv->muc_connection, "connected",
      G_CALLBACK (muc_connection_connected_cb), obj);

  g_object_get (base_conn,
      "muc-manager", &(priv->muc_manager),
      NULL);
  g_assert (priv->muc_manager != NULL);

  /* no need to keep a ref on the muc manager as it keeps a ref on us */
  g_object_unref (priv->muc_manager);

  tp_group_mixin_init (obj, G_STRUCT_OFFSET(SalutMucChannel, group),
      contact_repo, base_conn->self_handle);

  tp_group_mixin_change_flags (obj,
      TP_CHANNEL_GROUP_FLAG_PROPERTIES |
      TP_CHANNEL_GROUP_FLAG_CAN_ADD |
      TP_CHANNEL_GROUP_FLAG_MESSAGE_ADD,
      0);
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
  TpIntSet *empty, *removed;

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

  g_object_get (G_OBJECT (conn), "contact-manager",
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
  TpIntSet *empty, *remote_pending;

  if (handle == base_connection->self_handle)
    {
      /* adding yourself, let's join the muc */
      TpIntSet *empty_;
      TpIntSet *add;
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
          g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
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
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
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
  TpIntSet *empty, *changes;
  guint i;
  SalutContactManager *contact_mgr;

  empty = tp_intset_new ();
  changes = tp_intset_new ();

  g_object_get (G_OBJECT (base_conn), "contact-manager",
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
  SalutMucChannelPrivate *priv = self->priv;
  TpBaseChannel *base_chan = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_connection = tp_base_channel_get_connection (
      base_chan);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      base_connection, TP_HANDLE_TYPE_CONTACT);
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

#ifdef ENABLE_OLPC
  if (salut_connection_olpc_observe_muc_stanza (self->connection,
          tp_base_channel_get_target_handle (base_chan),
          from_handle, stanza))
    return;
#endif

  tubes_node = wocky_node_get_child_ns (node, "tubes",
      WOCKY_TELEPATHY_NS_TUBES);
  if (tubes_node != NULL)
    {
      SalutTubesChannel *tubes_chan;
      GPtrArray *tubes;
      guint i;
      GHashTable *channels;
      gboolean created;

      tubes_chan = salut_muc_manager_ensure_tubes_channel (priv->muc_manager,
          tp_base_channel_get_target_handle (base_chan), from_handle, &created);
      g_assert (tubes_chan != NULL);

      channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
        NULL, NULL);

      if (created)
        {
          g_hash_table_insert (channels, tubes_chan, NULL);
        }

      tubes = salut_tubes_channel_muc_message_received (tubes_chan, sender,
          stanza);

      for (i = 0; i < tubes->len; i++)
        {
          SalutTubeIface *tube;

          tube = g_ptr_array_index (tubes, i);
          g_hash_table_insert (channels, tube, NULL);
        }

      if (g_hash_table_size (channels) > 0)
        {
          tp_channel_manager_emit_new_channels (priv->muc_manager, channels);
        }

      g_object_unref (tubes_chan);
      g_ptr_array_unref (tubes);
      g_hash_table_unref (channels);
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
      GError error = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
          "can't join the muc" };
      g_signal_emit (self, signals[JOIN_ERROR], 0, &error);
    }

  tp_base_channel_destroyed (TP_BASE_CHANNEL (self));
}

static void
salut_muc_channel_close (TpBaseChannel *base)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (base);

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

