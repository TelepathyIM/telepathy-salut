/*
 * salut-im-channel.c - Source for SalutImChannel
 * Copyright (C) 2005-2008,2010 Collabora Ltd.
 *   @author: Sjoerd Simons <sjoerd@luon.net>
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

#include "salut-im-channel.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include <dbus/dbus-glib.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-generic.h>

#include <gibber/gibber-linklocal-transport.h>
#include <gibber/gibber-namespaces.h>
#include <gibber/gibber-xmpp-connection.h>
#include <gibber/gibber-xmpp-stanza.h>

#define DEBUG_FLAG DEBUG_IM
#include "debug.h"
#include "salut-connection.h"
#include "salut-contact.h"
#include "salut-xmpp-connection-manager.h"
#include "salut-signals-marshal.h"
#include "salut-util.h"
#include "text-helper.h"

static void channel_iface_init (gpointer g_iface, gpointer iface_data);

static void xmpp_connection_manager_new_connection_cb (
    SalutXmppConnectionManager *mgr, GibberXmppConnection *conn,
    SalutContact *contact, gpointer user_data);

static gboolean _setup_connection (SalutImChannel *self, GError **error);

G_DEFINE_TYPE_WITH_CODE (SalutImChannel, salut_im_channel, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
      tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_TEXT,
      tp_message_mixin_text_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_MESSAGES,
      tp_message_mixin_messages_iface_init);
);

static const gchar *salut_im_channel_interfaces[] = {
    TP_IFACE_CHANNEL_INTERFACE_MESSAGES,
    NULL
};

static gboolean message_stanza_filter (SalutXmppConnectionManager *mgr,
    GibberXmppConnection *conn, GibberXmppStanza *stanza,
    SalutContact *contact, gpointer user_data);

static void message_stanza_callback (SalutXmppConnectionManager *mgr,
    GibberXmppConnection *conn, GibberXmppStanza *stanza,
    SalutContact *contact, gpointer user_data);

/* Channel state */
typedef enum
{
  CHANNEL_NOT_CONNECTED = 0,
  CHANNEL_CONNECTING,
  CHANNEL_CONNECTED,
  CHANNEL_CLOSING,
} ChannelState;

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_CONTACT,
  PROP_CONNECTION,
  PROP_XMPP_CONNECTION_MANAGER,
  PROP_INTERFACES,
  PROP_TARGET_ID,
  PROP_INITIATOR_HANDLE,
  PROP_INITIATOR_ID,
  PROP_REQUESTED,
  PROP_CHANNEL_PROPERTIES,
  PROP_CHANNEL_DESTROYED,
  LAST_PROPERTY
};

/* private structure */
typedef struct _SalutImChannelPrivate SalutImChannelPrivate;

struct _SalutImChannelPrivate
{
  gboolean dispose_has_run;
  gchar *object_path;
  TpHandle handle;
  TpHandle initiator;
  SalutContact *contact;
  SalutConnection *connection;
  GibberXmppConnection *xmpp_connection;
  SalutXmppConnectionManager *xmpp_connection_manager;
  /* Outcoming and incoming message queues */
  GQueue *out_queue;
  ChannelState state;
  gboolean closed;
};

#define SALUT_IM_CHANNEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE \
    ((o), SALUT_TYPE_IM_CHANNEL, SalutImChannelPrivate))

typedef struct _SalutImChannelMessage SalutImChannelMessage;

struct _SalutImChannelMessage {
  guint time;
  guint type;
  gchar *text;
  gchar *token;
  GibberXmppStanza *stanza;
};

static void
salut_im_channel_do_close (SalutImChannel *self)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  ChannelState oldstate = priv->state;

  if (priv->closed)
    return;
  priv->closed = TRUE;

  priv->state = CHANNEL_NOT_CONNECTED;

  switch (oldstate)
    {
      case CHANNEL_NOT_CONNECTED:
        break;
      case CHANNEL_CONNECTING:
        break;
      case CHANNEL_CLOSING:
        break;
      case CHANNEL_CONNECTED:
        g_assert (priv->xmpp_connection != NULL);
        break;
    }

  DEBUG ("Emitting closed signal for %s", priv->object_path);
  tp_svc_channel_emit_closed (self);
}

static SalutImChannelMessage *
salut_im_channel_message_new (guint type,
                              const gchar *text,
                              const gchar *token,
                              GibberXmppStanza *stanza)
{
  SalutImChannelMessage *msg;
  msg = g_new0 (SalutImChannelMessage, 1);
  msg->type = type;
  msg->text = g_strdup (text);
  msg->time = time (NULL);
  msg->token = g_strdup (token);
  msg->stanza = stanza;
  if (stanza != NULL)
    g_object_ref (G_OBJECT (stanza));

  return msg;
}

static void
salut_im_channel_message_free (SalutImChannelMessage *message)
{
  g_free (message->text);
  g_free (message->token);
  if (message->stanza)
    g_object_unref (message->stanza);

  g_free (message);
}

static gboolean
_send_message (SalutImChannel *self, guint type, const gchar *text,
    const gchar *token, GibberXmppStanza *stanza, GError **error);

static void
salut_im_channel_init (SalutImChannel *obj)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (obj);
  /* allocate any data required by the object here */
  priv->object_path = NULL;
  priv->contact = NULL;
  priv->xmpp_connection = NULL;
  priv->out_queue = g_queue_new ();
  priv->state = CHANNEL_NOT_CONNECTED;
  priv->xmpp_connection_manager = NULL;
}


static void
salut_im_channel_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  SalutImChannel *chan = SALUT_IM_CHANNEL (object);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (chan);
  TpBaseConnection *base_conn = (TpBaseConnection *) priv->connection;

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_value_set_string (value, priv->object_path);
        break;
      case PROP_CHANNEL_TYPE:
        g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_TEXT);
        break;
      case PROP_HANDLE_TYPE:
        g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
        break;
      case PROP_HANDLE:
        g_value_set_uint (value, priv->handle);
        break;
      case PROP_CONTACT:
        g_value_set_object (value, priv->contact);
        break;
      case PROP_CONNECTION:
        g_value_set_object (value, priv->connection);
        break;
      case PROP_XMPP_CONNECTION_MANAGER:
        g_value_set_object (value, priv->xmpp_connection_manager);
        break;
      case PROP_INTERFACES:
        g_value_set_static_boxed (value, salut_im_channel_interfaces);
        break;
      case PROP_TARGET_ID:
        {
           TpHandleRepoIface *repo = tp_base_connection_get_handles (base_conn,
             TP_HANDLE_TYPE_CONTACT);

           g_value_set_string (value, tp_handle_inspect (repo, priv->handle));
        }
        break;
      case PROP_INITIATOR_HANDLE:
        g_value_set_uint (value, priv->initiator);
        break;
      case PROP_INITIATOR_ID:
          {
            TpHandleRepoIface *repo = tp_base_connection_get_handles (
                base_conn, TP_HANDLE_TYPE_CONTACT);

            g_assert (priv->initiator != 0);
            g_value_set_string (value,
                tp_handle_inspect (repo, priv->initiator));
          }
        break;
      case PROP_REQUESTED:
        g_value_set_boolean (value, (priv->initiator == base_conn->self_handle));
        break;
      case PROP_CHANNEL_DESTROYED:
        /* TODO: this should be FALSE if there are still pending messages, so
         *       the channel manager can respawn the channel.
         */
        g_value_set_boolean (value, TRUE);
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
                TP_IFACE_CHANNEL, "Interfaces",
                NULL));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_im_channel_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  SalutImChannel *chan = SALUT_IM_CHANNEL (object);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (chan);
  const gchar *tmp;

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_free (priv->object_path);
        priv->object_path = g_value_dup_string (value);
        break;
      case PROP_HANDLE:
        priv->handle = g_value_get_uint (value);
        break;
      case PROP_INITIATOR_HANDLE:
        priv->initiator = g_value_get_uint (value);
        g_assert (priv->initiator != 0);
        break;
      case PROP_CONTACT:
        priv->contact = g_value_get_object (value);
        g_object_ref (priv->contact);
        break;
      case PROP_CONNECTION:
        priv->connection = g_value_get_object (value);
        break;
      case PROP_HANDLE_TYPE:
        g_assert (g_value_get_uint (value) == 0 ||
            g_value_get_uint (value) == TP_HANDLE_TYPE_CONTACT);
        break;
      case PROP_CHANNEL_TYPE:
        tmp = g_value_get_string (value);
        g_assert (tmp == NULL || !tp_strdiff (g_value_get_string (value),
              TP_IFACE_CHANNEL_TYPE_TEXT));
        break;
      case PROP_XMPP_CONNECTION_MANAGER:
        priv->xmpp_connection_manager = g_value_get_object (value);
        g_object_ref (priv->xmpp_connection_manager);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

#define NUM_SUPPORTED_MESSAGE_TYPES 3

static void
_salut_im_channel_send (GObject *channel,
                        TpMessage *message,
                        TpMessageSendingFlags flags);
static GObject *
salut_im_channel_constructor (GType type,
                              guint n_props,
                              GObjectConstructParam *props)
{
  GObject *obj;
  TpDBusDaemon *bus;
  SalutImChannelPrivate *priv;
  TpBaseConnection *base_conn;
  TpHandleRepoIface *contact_repo;

  TpChannelTextMessageType types[NUM_SUPPORTED_MESSAGE_TYPES] = {
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE,
  };

  const gchar * supported_content_types[] = {
      "text/plain",
      NULL
  };

  /* Parent constructor chain */
  obj = G_OBJECT_CLASS (salut_im_channel_parent_class)->
        constructor (type, n_props, props);

  priv = SALUT_IM_CHANNEL_GET_PRIVATE (SALUT_IM_CHANNEL (obj));

  /* Ref our handle and initiator handle */
  base_conn = TP_BASE_CONNECTION (priv->connection);

  contact_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);

  tp_handle_ref (contact_repo, priv->handle);

  g_assert (priv->initiator != 0);
  tp_handle_ref (contact_repo, priv->initiator);

  /* Initialize message mixin */
  tp_message_mixin_init (obj, G_STRUCT_OFFSET (SalutImChannel, message_mixin),
      base_conn);

  tp_message_mixin_implement_sending (obj, _salut_im_channel_send,
      NUM_SUPPORTED_MESSAGE_TYPES, types, 0,
      TP_DELIVERY_REPORTING_SUPPORT_FLAG_RECEIVE_FAILURES,
      supported_content_types);

  /* Connect to the bus */
  bus = tp_base_connection_get_dbus_daemon (base_conn);
  tp_dbus_daemon_register_object (bus, priv->object_path, obj);

  g_signal_connect (priv->xmpp_connection_manager, "new-connection",
      G_CALLBACK (xmpp_connection_manager_new_connection_cb), obj);

  return obj;
}

static void salut_im_channel_dispose (GObject *object);
static void salut_im_channel_finalize (GObject *object);

static void
salut_im_channel_class_init (SalutImChannelClass *salut_im_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_im_channel_class);
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

  g_type_class_add_private (salut_im_channel_class,
      sizeof (SalutImChannelPrivate));

  object_class->dispose = salut_im_channel_dispose;
  object_class->finalize = salut_im_channel_finalize;

  object_class->constructor = salut_im_channel_constructor;
  object_class->get_property = salut_im_channel_get_property;
  object_class->set_property = salut_im_channel_set_property;

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
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TARGET_ID, param_spec);

  param_spec = g_param_spec_boolean ("requested", "Requested?",
      "True if this channel was requested by the local user",
      FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REQUESTED, param_spec);

  param_spec = g_param_spec_uint ("initiator-handle", "Initiator's handle",
      "The contact who initiated the channel",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIATOR_HANDLE,
      param_spec);

  param_spec = g_param_spec_string ("initiator-id", "Initiator JID",
      "The string obtained by inspecting the initiator-handle",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIATOR_ID,
      param_spec);

  param_spec = g_param_spec_object (
      "contact",
      "SalutContact object",
      "Salut Contact to which this channel is dedicated",
      SALUT_TYPE_CONTACT,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONTACT, param_spec);

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "Salut Connection that owns the connection for this IM channel",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_object (
      "xmpp-connection-manager",
      "SalutXmppConnectionManager object",
      "Salut XMPP Connection manager used for this IM channel",
      SALUT_TYPE_XMPP_CONNECTION_MANAGER,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_XMPP_CONNECTION_MANAGER,
      param_spec);

  param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
      "Additional Channel.Interface.* interfaces",
      G_TYPE_STRV,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

  salut_im_channel_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (SalutImChannelClass, dbus_props_class));

  tp_message_mixin_init_dbus_properties (object_class);
}

void
salut_im_channel_dispose (GObject *object)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (object);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->connection);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (base_conn,
        TP_HANDLE_TYPE_CONTACT);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_signal_handlers_disconnect_matched (priv->xmpp_connection_manager,
      G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);

  tp_handle_unref (handle_repo, priv->handle);

  if (priv->initiator != 0)
    tp_handle_unref (handle_repo, priv->initiator);

  salut_im_channel_do_close (self);

  if (priv->xmpp_connection)
    {
      salut_xmpp_connection_manager_remove_stanza_filter (
          priv->xmpp_connection_manager, priv->xmpp_connection,
          message_stanza_filter, message_stanza_callback, self);

      g_object_unref (priv->xmpp_connection);
      priv->xmpp_connection = NULL;
    }

  g_object_unref (priv->contact);
  priv->contact = NULL;

  if (priv->xmpp_connection_manager != NULL)
    {
      g_object_unref (priv->xmpp_connection_manager);
      priv->xmpp_connection_manager = NULL;
    }

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_im_channel_parent_class)->dispose)
    G_OBJECT_CLASS (salut_im_channel_parent_class)->dispose (object);
}

static void
salut_im_channel_finalize (GObject *object)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (object);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  g_free (priv->object_path);

  g_queue_foreach (priv->out_queue, (GFunc) salut_im_channel_message_free,
      NULL);
  g_queue_free (priv->out_queue);

  tp_message_mixin_finalize (object);

  G_OBJECT_CLASS (salut_im_channel_parent_class)->finalize (object);
}

static void
_sendout_message (SalutImChannel *self,
                  guint timestamp,
                  guint type,
                  const gchar *text,
                  const gchar *token,
                  GibberXmppStanza *stanza)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  if (gibber_xmpp_connection_send (priv->xmpp_connection, stanza, NULL))
    {
      salut_xmpp_connection_manager_reset_connection_timer (
          priv->xmpp_connection_manager, priv->xmpp_connection);
    }
  else
    {
      text_helper_report_delivery_error (TP_SVC_CHANNEL (self),
          TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN, timestamp, type, text, token);
    }
}

static void
_flush_queue (SalutImChannel *self)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  SalutImChannelMessage *msg;
  /*Connected!, flusch the queue ! */
  while ((msg = g_queue_pop_head (priv->out_queue)) != NULL)
    {
      if (msg->text == NULL)
        {
          if (!gibber_xmpp_connection_send (priv->xmpp_connection,
                msg->stanza, NULL))
            g_warning ("Sending message failed");
        }
      else
        {
          _sendout_message (self, msg->time, msg->type, msg->text,
              msg->token, msg->stanza);
        }

      salut_im_channel_message_free (msg);
    }
}

static void
_error_flush_queue (SalutImChannel *self) {
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  SalutImChannelMessage *msg;
  /*Connection failed!, flusch the queue ! */
  while ((msg = g_queue_pop_head (priv->out_queue)) != NULL)
    {
      DEBUG ("Sending out SendError for msg: %s", msg->text);
      if (msg->text != NULL)
        text_helper_report_delivery_error (TP_SVC_CHANNEL (self),
            TP_CHANNEL_TEXT_SEND_ERROR_OFFLINE,
            msg->time, msg->type, msg->text, msg->token);

      salut_im_channel_message_free (msg);
    }
}

void
salut_im_channel_received_stanza (SalutImChannel *self,
                                  GibberXmppStanza *stanza)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *base_conn = (TpBaseConnection *) priv->connection;
  const gchar *from;
  TpChannelTextMessageType msgtype;
  const gchar *body;
  const gchar *body_offset;

  if (!text_helper_parse_incoming_message (stanza, &from, &msgtype,
        &body, &body_offset))
    {
      DEBUG ("Stanza not a text message, ignoring");
      return;
    }

  if (body == NULL)
    {
      /* No body ? Ignore */
      DEBUG ("Text message without a body");
      return;
    }

  /* FIXME validate the from */
  tp_message_mixin_take_received (G_OBJECT (self),
      text_helper_create_received_message (base_conn, priv->handle,
          time (NULL), msgtype, body_offset));
}

static gboolean
message_stanza_filter (SalutXmppConnectionManager *mgr,
                       GibberXmppConnection *conn,
                       GibberXmppStanza *stanza,
                       SalutContact *contact,
                       gpointer user_data)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (user_data);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  if (priv->contact != contact)
    return FALSE;

  return salut_im_channel_is_text_message (stanza);
}

static void
message_stanza_callback (SalutXmppConnectionManager *mgr,
                         GibberXmppConnection *conn,
                         GibberXmppStanza *stanza,
                         SalutContact *contact,
                         gpointer user_data)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (user_data);

  salut_im_channel_received_stanza (self, stanza);
}

static void
connection_disconnected (SalutImChannel *self)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  if (priv->xmpp_connection != NULL)
    {
      DEBUG ("connection closed. Remove filters");

      salut_xmpp_connection_manager_remove_stanza_filter (
          priv->xmpp_connection_manager, priv->xmpp_connection,
          message_stanza_filter, message_stanza_callback, self);

      g_object_unref (priv->xmpp_connection);
      priv->xmpp_connection = NULL;
    }

  g_signal_handlers_disconnect_matched (priv->xmpp_connection_manager,
      G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);

  priv->state = CHANNEL_NOT_CONNECTED;

  g_signal_connect (priv->xmpp_connection_manager, "new-connection",
      G_CALLBACK (xmpp_connection_manager_new_connection_cb), self);

  if (g_queue_get_length (priv->out_queue) > 0)
    {
      _setup_connection (self, NULL);
    }
}

static void
xmpp_connection_manager_connection_closed_cb (SalutXmppConnectionManager *mgr,
                                              GibberXmppConnection *conn,
                                              SalutContact *contact,
                                              gpointer user_data)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (user_data);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  if (priv->contact != contact)
    return;

  g_assert (priv->xmpp_connection == conn);
  connection_disconnected (self);
}

static void
xmpp_connection_manager_connection_failed_cb (SalutXmppConnectionManager *mgr,
                                              GibberXmppConnection *conn,
                                              SalutContact *contact,
                                              GQuark domain,
                                              gint code,
                                              gchar *message,
                                              gpointer user_data)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (user_data);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  if (contact != priv->contact)
    return;

  DEBUG ("connection failed, flush messages queue");
  g_assert (priv->xmpp_connection == NULL || priv->xmpp_connection == conn);
  _error_flush_queue (self);
  connection_disconnected (self);
}

static void
xmpp_connection_manager_connection_closing_cb (SalutXmppConnectionManager *mgr,
                                               GibberXmppConnection *conn,
                                               SalutContact *contact,
                                               gpointer user_data)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (user_data);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  if (priv->contact != contact)
    return;

  DEBUG ("connection closing");
  g_assert (priv->xmpp_connection == conn);
  priv->state = CHANNEL_CLOSING;
}

static void
_initialise_connection (SalutImChannel *self)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  g_assert (priv->xmpp_connection != NULL);
  g_assert (
     (priv->xmpp_connection->stream_flags &
       ~(GIBBER_XMPP_CONNECTION_STREAM_FULLY_OPEN
         |GIBBER_XMPP_CONNECTION_CLOSE_SENT)) == 0);


  g_signal_handlers_disconnect_matched (priv->xmpp_connection_manager,
      G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
  g_signal_connect (priv->xmpp_connection_manager, "connection-failed",
      G_CALLBACK (xmpp_connection_manager_connection_failed_cb), self);
  g_signal_connect (priv->xmpp_connection_manager, "connection-closed",
      G_CALLBACK (xmpp_connection_manager_connection_closed_cb), self);
  g_signal_connect (priv->xmpp_connection_manager, "connection-closing",
      G_CALLBACK (xmpp_connection_manager_connection_closing_cb), self);

  salut_xmpp_connection_manager_add_stanza_filter (
      priv->xmpp_connection_manager, priv->xmpp_connection,
      message_stanza_filter, message_stanza_callback, self);

  if (priv->xmpp_connection->stream_flags
        & GIBBER_XMPP_CONNECTION_CLOSE_SENT) {
    priv->state = CHANNEL_CLOSING;
  } else {
    priv->state = CHANNEL_CONNECTED;
    _flush_queue (self);
  }
}

static void
xmpp_connection_manager_new_connection_cb (SalutXmppConnectionManager *mgr,
                                           GibberXmppConnection *conn,
                                           SalutContact *contact,
                                           gpointer user_data)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (user_data);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  if (contact != priv->contact)
    /* This new connection is not for this channel */
    return;

  DEBUG ("pending connection fully open");

  priv->xmpp_connection = conn;
  g_object_ref (priv->xmpp_connection);
  _initialise_connection (self);
}

static gboolean
_setup_connection (SalutImChannel *self,
    GError **error)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  SalutXmppConnectionManagerRequestConnectionResult result;
  GibberXmppConnection *conn = NULL;

  if (priv->state == CHANNEL_CONNECTING)
    return TRUE;

  g_assert (priv->xmpp_connection == NULL);

  result = salut_xmpp_connection_manager_request_connection (
      priv->xmpp_connection_manager, priv->contact, &conn, error);

  if (result == SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_DONE)
    {
      priv->xmpp_connection = conn;
      g_object_ref (priv->xmpp_connection);
      _initialise_connection (self);
    }
  else if (result ==
      SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_PENDING)
    {
      DEBUG ("Requested connection pending");
      priv->state = CHANNEL_CONNECTING;
      g_signal_connect (priv->xmpp_connection_manager, "connection-failed",
          G_CALLBACK (xmpp_connection_manager_connection_failed_cb), self);
    }
  else
    {
      priv->state = CHANNEL_NOT_CONNECTED;
      _error_flush_queue (self);
      return FALSE;
    }

  return TRUE;
}

static gboolean
_send_message (SalutImChannel *self,
               guint type,
               const gchar *text,
               const gchar *token,
               GibberXmppStanza *stanza,
               GError **error)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  SalutImChannelMessage *msg;

  if (priv->state == CHANNEL_NOT_CONNECTED
      && !_setup_connection (self, error))
    return FALSE;

  switch (priv->state)
    {
      case CHANNEL_NOT_CONNECTED:
      case CHANNEL_CONNECTING:
      case CHANNEL_CLOSING:
        msg = salut_im_channel_message_new (type, text, token, stanza);
        g_queue_push_tail (priv->out_queue, msg);
        break;
      case CHANNEL_CONNECTED:
        /* Connected and the queue is empty, so push it out directly */
        _sendout_message (self, time (NULL), type, text, token, stanza);
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  return TRUE;
}

void
salut_im_channel_add_connection (SalutImChannel *chan,
                                 GibberXmppConnection *conn)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (chan);
  /* FIXME if we already have a connection, we throw this one out..
   * Which can be not quite what the other side expects.. And strange things
   * can happen when two * sides try to initiate at the same time */

  g_assert (priv->xmpp_connection == NULL);
  DEBUG ("New connection for: %s", priv->contact->name);

  priv->xmpp_connection = conn;
  g_object_ref (priv->xmpp_connection);
  _initialise_connection (chan);
}

gboolean
salut_im_channel_is_text_message (GibberXmppStanza *stanza)
{
  GibberStanzaType type;

  gibber_xmpp_stanza_get_type_info (stanza, &type, NULL);
  if (type != GIBBER_STANZA_TYPE_MESSAGE)
    return FALSE;

  if (wocky_node_get_child_ns (wocky_stanza_get_top_node (stanza), "invite",
        GIBBER_TELEPATHY_NS_CLIQUE) != NULL)
    /* discard Clique MUC invite */
    return FALSE;

  return TRUE;
}

/**
 * salut_im_channel_close
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_im_channel_close (TpSvcChannel *iface,
                        DBusGMethodInvocation *context)
{
  salut_im_channel_do_close (SALUT_IM_CHANNEL (iface));
  tp_svc_channel_return_from_close (context);
}


/**
 * salut_im_channel_get_channel_type
 *
 * Implements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_im_channel_get_channel_type (TpSvcChannel *iface,
                                   DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      TP_IFACE_CHANNEL_TYPE_TEXT);
}


/**
 * salut_im_channel_get_handle
 *
 * Implements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_im_channel_get_handle (TpSvcChannel *iface,
                             DBusGMethodInvocation *context)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (iface);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_CONTACT,
      priv->handle);
}


/**
 * salut_im_channel_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_im_channel_get_interfaces (TpSvcChannel *iface,
                                 DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_interfaces (context,
      salut_im_channel_interfaces);
}

static void
channel_iface_init (gpointer g_iface,
                    gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *) g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, salut_im_channel_##x)
  IMPLEMENT(close);
  IMPLEMENT(get_channel_type);
  IMPLEMENT(get_handle);
  IMPLEMENT(get_interfaces);
#undef IMPLEMENT
}

static void
_salut_im_channel_send (GObject *channel,
                        TpMessage *message,
                        TpMessageSendingFlags flags)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (channel);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  GError *error = NULL;
  GibberXmppStanza *stanza = NULL;
  guint type;
  gchar *text;
  gchar *token;

  if (!text_helper_validate_tp_message (message, &type, &token, &text, &error))
    goto error;

  stanza = text_helper_create_message (priv->connection->name,
    priv->contact->name, type, text, &error);

  if (stanza == NULL)
    goto error;

  if (!_send_message (self, type, text, token, stanza, &error))
    goto error;

  tp_message_mixin_sent (channel, message, 0, token, NULL);
  g_free (token);
  g_object_unref (G_OBJECT (stanza));
  return;

error:
  if (stanza != NULL)
    g_object_unref (G_OBJECT (stanza));

  if (error->domain != TP_ERRORS)
    {
      GError *e = NULL;
      g_set_error_literal (&e, TP_ERRORS,
        TP_ERROR_NETWORK_ERROR,
        error->message);
      g_error_free (error);
      error = e;
    }

  tp_message_mixin_sent (channel, message, 0, NULL, error);
  g_error_free (error);
  g_free (text);
  g_free (token);
  return;
}
