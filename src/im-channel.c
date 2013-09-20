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

#include "config.h"
#include "im-channel.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#ifdef G_OS_UNIX
#include <sys/socket.h>
#include <netdb.h>
#endif

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include <gibber/gibber-linklocal-transport.h>

#define DEBUG_FLAG DEBUG_IM
#include "debug.h"
#include "connection.h"
#include "contact.h"
#include "util.h"
#include "text-helper.h"

G_DEFINE_TYPE_WITH_CODE (SalutImChannel, salut_im_channel, TP_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_TEXT,
      tp_message_mixin_iface_init)
);

/* properties */
enum
{
  PROP_CONTACT = 1,
  LAST_PROPERTY
};

/* private structure */
struct _SalutImChannelPrivate
{
  gboolean dispose_has_run;
  SalutContact *contact;
  guint message_handler_id;
};

static void
salut_im_channel_close (TpBaseChannel *base)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (base);
  SalutImChannelPrivate *priv = self->priv;
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  SalutConnection *conn = SALUT_CONNECTION (base_conn);
  WockyPorter *porter = conn->porter;

  wocky_porter_unregister_handler (porter,
      priv->message_handler_id);
  priv->message_handler_id = 0;

  wocky_meta_porter_unhold (WOCKY_META_PORTER (porter),
      WOCKY_CONTACT (priv->contact));

  tp_base_channel_destroyed (base);
}

static void
salut_im_channel_init (SalutImChannel *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (
      self, SALUT_TYPE_IM_CHANNEL, SalutImChannelPrivate);
}


static void
salut_im_channel_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (object);
  SalutImChannelPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_CONTACT:
        g_value_set_object (value, priv->contact);
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
  SalutImChannel *self = SALUT_IM_CHANNEL (object);
  SalutImChannelPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_CONTACT:
        priv->contact = g_value_dup_object (value);
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

static void
salut_im_channel_constructed (GObject *obj)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (obj);
  SalutImChannelPrivate *priv = self->priv;
  TpBaseConnection *base_conn = tp_base_channel_get_connection (
      TP_BASE_CHANNEL (obj));
  WockyPorter *porter = SALUT_CONNECTION (base_conn)->porter;
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

  /* Parent constructed chain */
  void (*chain_up) (GObject *) =
    ((GObjectClass *) salut_im_channel_parent_class)->constructed;

  if (chain_up != NULL)
    chain_up (obj);

  /* Initialize message mixin */
  tp_message_mixin_init (obj, G_STRUCT_OFFSET (SalutImChannel, message_mixin),
      base_conn);

  tp_message_mixin_implement_sending (obj, _salut_im_channel_send,
      NUM_SUPPORTED_MESSAGE_TYPES, types, 0,
      TP_DELIVERY_REPORTING_SUPPORT_FLAG_RECEIVE_FAILURES,
      supported_content_types);

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
}

static void salut_im_channel_dispose (GObject *object);
static void salut_im_channel_finalize (GObject *object);

static void
salut_im_channel_fill_immutable_properties (TpBaseChannel *chan,
    GHashTable *properties)
{
  TpBaseChannelClass *cls = TP_BASE_CHANNEL_CLASS (
      salut_im_channel_parent_class);

  cls->fill_immutable_properties (chan, properties);

  tp_dbus_properties_mixin_fill_properties_hash (
      G_OBJECT (chan), properties,
      TP_IFACE_CHANNEL_TYPE_TEXT, "MessagePartSupportFlags",
      TP_IFACE_CHANNEL_TYPE_TEXT, "DeliveryReportingSupport",
      TP_IFACE_CHANNEL_TYPE_TEXT, "SupportedContentTypes",
      TP_IFACE_CHANNEL_TYPE_TEXT, "MessageTypes",
      NULL);
}

static gchar *
salut_im_channel_get_object_path_suffix (TpBaseChannel *chan)
{
  return g_strdup_printf ("ImChannel%u",
      tp_base_channel_get_target_handle (chan));
}

static void
salut_im_channel_class_init (SalutImChannelClass *salut_im_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_im_channel_class);
  TpBaseChannelClass *base_class = TP_BASE_CHANNEL_CLASS (salut_im_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_im_channel_class,
      sizeof (SalutImChannelPrivate));

  object_class->dispose = salut_im_channel_dispose;
  object_class->finalize = salut_im_channel_finalize;
  object_class->constructed = salut_im_channel_constructed;
  object_class->get_property = salut_im_channel_get_property;
  object_class->set_property = salut_im_channel_set_property;

  base_class->channel_type = TP_IFACE_CHANNEL_TYPE_TEXT;
  base_class->target_handle_type = TP_HANDLE_TYPE_CONTACT;
  base_class->close = salut_im_channel_close;
  base_class->fill_immutable_properties =
    salut_im_channel_fill_immutable_properties;
  base_class->get_object_path_suffix =
    salut_im_channel_get_object_path_suffix;

  param_spec = g_param_spec_object (
      "contact",
      "SalutContact object",
      "Salut Contact to which this channel is dedicated",
      SALUT_TYPE_CONTACT,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONTACT, param_spec);

  tp_message_mixin_init_dbus_properties (object_class);
}

void
salut_im_channel_dispose (GObject *object)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (object);
  SalutImChannelPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  g_object_unref (priv->contact);
  priv->contact = NULL;

  if (G_OBJECT_CLASS (salut_im_channel_parent_class)->dispose)
    G_OBJECT_CLASS (salut_im_channel_parent_class)->dispose (object);
}

static void
salut_im_channel_finalize (GObject *object)
{
  tp_message_mixin_finalize (object);

  if (G_OBJECT_CLASS (salut_im_channel_parent_class)->finalize)
    G_OBJECT_CLASS (salut_im_channel_parent_class)->finalize (object);
}

void
salut_im_channel_received_stanza (SalutImChannel *self,
                                  WockyStanza *stanza)
{
  TpBaseChannel *base_chan = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (
      base_chan);
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
      text_helper_create_received_message (base_conn,
          tp_base_channel_get_target_handle (base_chan),
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
  TpBaseConnection *base_conn = tp_base_channel_get_connection (
      TP_BASE_CHANNEL (self));
  SalutConnection *conn = SALUT_CONNECTION (base_conn);
  SendMessageData *data;

  data = g_slice_new0 (SendMessageData);
  data->self = self;
  data->timestamp = timestamp;
  data->type = type;
  data->text = g_strdup (text);
  data->token = g_strdup (token);

  wocky_porter_send_async (conn->porter,
      stanza, NULL, sent_message_cb, data);

  return TRUE;
}

static void
_salut_im_channel_send (GObject *channel,
    TpMessage *message,
    TpMessageSendingFlags flags)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (channel);
  SalutImChannelPrivate *priv = self->priv;
  TpBaseConnection *base_conn = tp_base_channel_get_connection (
      TP_BASE_CHANNEL (self));
  SalutConnection *conn = SALUT_CONNECTION (base_conn);
  GError *error = NULL;
  WockyStanza *stanza = NULL;
  guint type;
  gchar *text;
  gchar *token;

  if (!text_helper_validate_tp_message (message, &type, &token, &text, &error))
    goto error;

  stanza = text_helper_create_message (conn->name,
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

  if (error != NULL && error->domain != TP_ERROR)
    {
      GError *e = NULL;
      g_set_error_literal (&e, TP_ERROR,
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
