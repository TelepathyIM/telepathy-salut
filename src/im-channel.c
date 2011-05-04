/*
 * im-channel.c - Source for SalutImChannel
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

#include "im-channel.h"

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
#include <wocky/wocky-namespaces.h>
#include <wocky/wocky-stanza.h>
#include <wocky/wocky-meta-porter.h>

#define DEBUG_FLAG DEBUG_IM
#include "debug.h"
#include "connection.h"
#include "contact.h"
#include "signals-marshal.h"
#include "util.h"
#include "text-helper.h"

static void channel_iface_init (gpointer g_iface, gpointer iface_data);

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

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_CONTACT,
  PROP_CONNECTION,
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
  guint message_handler_id;
  gboolean closed;
};

#define SALUT_IM_CHANNEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE \
    ((o), SALUT_TYPE_IM_CHANNEL, SalutImChannelPrivate))

static void
salut_im_channel_do_close (SalutImChannel *self)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  WockyPorter *porter = priv->connection->porter;

  if (priv->closed)
    return;
  priv->closed = TRUE;

  wocky_porter_unregister_handler (porter,
      priv->message_handler_id);
  priv->message_handler_id = 0;

  wocky_meta_porter_unhold (WOCKY_META_PORTER (porter),
      WOCKY_CONTACT (priv->contact));

  DEBUG ("Emitting closed signal for %s", priv->object_path);
  tp_svc_channel_emit_closed (self);
}

static void
salut_im_channel_init (SalutImChannel *obj)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (obj);
  /* allocate any data required by the object here */
  priv->object_path = NULL;
  priv->contact = NULL;
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
                TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "MessagePartSupportFlags",
                TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "DeliveryReportingSupport",
                TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "SupportedContentTypes",
                TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "MessageTypes",
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
        priv->contact = g_value_dup_object (value);
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

static gboolean new_message_cb (WockyPorter *porter,
    WockyStanza *stanza, gpointer user_data);

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
  WockyPorter *porter;
  gchar *jid;

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
  porter = priv->connection->porter;

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

  /* Connect to further messages */
  jid = wocky_contact_dup_jid (WOCKY_CONTACT (priv->contact));

  priv->message_handler_id = wocky_porter_register_handler_from (
      porter, WOCKY_STANZA_TYPE_MESSAGE, WOCKY_STANZA_SUB_TYPE_NONE,
      jid, WOCKY_PORTER_HANDLER_PRIORITY_NORMAL,
      new_message_cb, obj, NULL);

  g_free (jid);

  /* ensure the connection doesn't close */
  wocky_meta_porter_hold (WOCKY_META_PORTER (porter),
      WOCKY_CONTACT (priv->contact));

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

  tp_handle_unref (handle_repo, priv->handle);

  if (priv->initiator != 0)
    tp_handle_unref (handle_repo, priv->initiator);

  salut_im_channel_do_close (self);

  g_object_unref (priv->contact);
  priv->contact = NULL;

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

  tp_message_mixin_finalize (object);

  G_OBJECT_CLASS (salut_im_channel_parent_class)->finalize (object);
}

void
salut_im_channel_received_stanza (SalutImChannel *self,
                                  WockyStanza *stanza)
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
new_message_cb (WockyPorter *porter,
    WockyStanza *stanza,
    gpointer user_data)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (user_data);

  if (wocky_node_get_child_ns (wocky_stanza_get_top_node (stanza), "invite",
        WOCKY_TELEPATHY_NS_CLIQUE) != NULL)
    /* we don't handle Clique MUC invites here, so pass it on to the
     * next handler */
    return FALSE;

  salut_im_channel_received_stanza (self, stanza);

  return TRUE;
}

typedef struct
{
  SalutImChannel *self;
  guint timestamp;
  TpChannelTextMessageType type;
  gchar *text;
  gchar *token;
} SendMessageData;

static void
sent_message_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  WockyPorter *porter = WOCKY_PORTER (source_object);
  GError *error = NULL;
  SendMessageData *data = user_data;

  if (!wocky_porter_send_finish (porter, result, &error))
    {
      DEBUG ("Failed to send message: %s", error->message);
      g_clear_error (&error);

      text_helper_report_delivery_error (TP_SVC_CHANNEL (data->self),
          TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN, data->timestamp,
          data->type, data->text, data->token);
    }

  /* successfully sent message */

  g_free (data->text);
  g_free (data->token);
  g_slice_free (SendMessageData, data);
}

static gboolean
_send_message (SalutImChannel *self,
    WockyStanza *stanza,
    guint timestamp,
    TpChannelTextMessageType type,
    const gchar *text,
    const gchar *token)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  SendMessageData *data;

  data = g_slice_new0 (SendMessageData);
  data->self = self;
  data->timestamp = timestamp;
  data->type = type;
  data->text = g_strdup (text);
  data->token = g_strdup (token);

  wocky_porter_send_async (priv->connection->porter,
      stanza, NULL, sent_message_cb, data);

  return TRUE;
}

/**
 * salut_im_channel_close
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
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
  WockyStanza *stanza = NULL;
  guint type;
  gchar *text;
  gchar *token;

  if (!text_helper_validate_tp_message (message, &type, &token, &text, &error))
    goto error;

  stanza = text_helper_create_message (priv->connection->name,
    priv->contact, type, text, &error);

  if (stanza == NULL)
    goto error;

  if (!_send_message (self, stanza, time (NULL), type, text, token))
    goto error;

  tp_message_mixin_sent (channel, message, 0, token, NULL);
  g_free (token);
  g_object_unref (G_OBJECT (stanza));
  return;

error:
  if (stanza != NULL)
    g_object_unref (G_OBJECT (stanza));

  if (error != NULL && error->domain != TP_ERRORS)
    {
      GError *e = NULL;
      g_set_error_literal (&e, TP_ERRORS,
        TP_ERROR_NETWORK_ERROR,
        error->message);
      g_error_free (error);
      error = e;
    }

  if (error != NULL)
    {
      tp_message_mixin_sent (channel, message, 0, NULL, error);
      g_error_free (error);
    }
  g_free (text);
  g_free (token);
  return;
}
