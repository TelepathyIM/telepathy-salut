/*
 * text-helper.c - Source for TextHelper
 * Copyright (C) 2006,2010 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *   @author Ole Andre Vadla Ravnaas <ole.andre.ravnaas@collabora.co.uk>
 *   @author Robert McQueen <robert.mcqueen@collabora.co.uk>
 *   @author Senko Rasic <senko@senko.net>
 *   @author Sjoerd Simons <sjoerd@luon.net>
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

#define _GNU_SOURCE /* Needed for strptime (_XOPEN_SOURCE can also be used). */

#include "config.h"
#include "text-helper.h"

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <telepathy-glib/telepathy-glib.h>

#define DEBUG_FLAG DEBUG_IM
#include "debug.h"

#include "util.h"

static void
add_text (WockyStanza *stanza, const gchar *text)
{
  WockyNode *node = wocky_stanza_get_top_node (stanza);
  WockyNode *htmlnode;

  wocky_node_add_child_with_content (node, "body", text);

  /* Add plain xhtml-im node */
  htmlnode = wocky_node_add_child_ns (node, "html",
      WOCKY_XMPP_NS_XHTML_IM);
  wocky_node_add_child_with_content_ns (htmlnode, "body", text,
      WOCKY_W3C_NS_XHTML);
}

static WockyStanza *
create_message_stanza (const gchar *from, const gchar *to,
    SalutContact *contact, TpChannelTextMessageType type, const gchar *text,
    GError **error)
{
  WockyStanza *stanza;
  WockyNode *node;

  if (type > TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE)
    {
      DEBUG ("invalid message type %u", type);

      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "invalid message type: %u", type);

      return NULL;
    }
  stanza = wocky_stanza_new ("message", WOCKY_XMPP_NS_JABBER_CLIENT);
  node = wocky_stanza_get_top_node (stanza);

  wocky_node_set_attribute (node, "from", from);
  wocky_node_set_attribute (node, "to", to);

  if (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION)
    {
      gchar *tmp;
      tmp = g_strconcat ("/me ", text, NULL);
      add_text (stanza, tmp);
      g_free (tmp);
    }
  else
    {
      add_text (stanza, text);
    }

  if (contact != NULL)
    wocky_stanza_set_to_contact (stanza, WOCKY_CONTACT (contact));

  return stanza;
}

WockyStanza *
text_helper_create_message (const gchar *from,
                            SalutContact *to,
                            TpChannelTextMessageType type,
                            const gchar *text,
                            GError **error)
{
  WockyStanza *stanza;
  WockyNode *node;

  stanza = create_message_stanza (from, to->name, to, type, text, error);
  node = wocky_stanza_get_top_node (stanza);

  if (stanza == NULL)
    return NULL;

  switch (type)
    {
      case TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL:
      case TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION:
        wocky_node_set_attribute (node, "type", "chat");
        break;
      case TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE:
        wocky_node_set_attribute (node, "type", "normal");
        break;
      default:
        g_assert_not_reached ();
        break;
    }

  return stanza;
}

WockyStanza *
text_helper_create_message_groupchat (const gchar *from,
                                      const gchar *to,
                                      TpChannelTextMessageType type,
                                      const gchar *text,
                                      GError **error)
{
  WockyStanza *stanza;
  WockyNode *node;

  stanza = create_message_stanza (from, to, NULL, type, text, error);
  if (stanza == NULL)
    return NULL;

  node = wocky_stanza_get_top_node (stanza);

  switch (type)
    {
      case TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL:
      case TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION:
        wocky_node_set_attribute (node, "type", "groupchat");
        break;
      case TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE:
        wocky_node_set_attribute (node, "type", "normal");
        break;
      default:
        g_assert_not_reached ();
        break;
    }

  return stanza;
}

gboolean
text_helper_parse_incoming_message (WockyStanza *stanza,
                        const gchar **from,
                        TpChannelTextMessageType *msgtype,
                        const gchar **body,
                        const gchar **body_offset)
{
  const gchar *type;
  WockyNode *node;
  WockyNode *event;
  WockyNode *top_node = wocky_stanza_get_top_node (stanza);

  *from = wocky_node_get_attribute (top_node, "from");
  type = wocky_node_get_attribute (top_node, "type");
  /* Work around iChats strange way of doing typing notification */
  event = wocky_node_get_child_ns (top_node, "x",
    WOCKY_XMPP_NS_EVENT);

  if (event != NULL)
    {
      /* If the event has a composing and an id child, this is a typing
       * notification and it should be dropped */
      if (wocky_node_get_child (event, "composing") != NULL &&
          wocky_node_get_child (event, "id") != NULL)
        {
          return FALSE;
        }
    }
  /*
   * Parse body if it exists.
   */
  node = wocky_node_get_child (top_node, "body");

  if (node)
    {
      *body = node->content;
    }
  else
    {
      *body = NULL;
    }


  /* Messages starting with /me are ACTION messages, and the /me should be
   * removed. type="chat" messages are NORMAL.  everything else is
   * something that doesn't necessarily expect a reply or ongoing
   * conversation ("normal") or has been auto-sent, so we make it NOTICE in
   * all other cases. */

  *msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE;
  *body_offset = *body;

  if (*body)
    {
      if (0 == strncmp (*body, "/me ", 4))
        {
          *msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION;
          *body_offset = *body + 4;
        }
      else if (type != NULL && (0 == strcmp (type, "chat") ||
                                0 == strcmp (type, "groupchat")))
        {
          *msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
          *body_offset = *body;
        }
    }

  return TRUE;
}

gboolean
text_helper_validate_tp_message (TpMessage *message,
                                 guint *type,
                                 gchar **token,
                                 gchar **text,
                                 GError **error)
{
  GVariant *part;
  guint msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
  const gchar *msgtext;
  gchar *msgtoken;
  gboolean valid = TRUE;

  if (tp_message_count_parts (message) != 2)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Invalid number of message parts, expected 2, got %d",
          tp_message_count_parts (message));
      return FALSE;
    }

  part = tp_message_dup_part (message, 0);

  if (tp_vardict_has_key (part, "message-type"))
    msgtype = tp_vardict_get_uint32 (part, "message-type", &valid);
  g_variant_unref (part);

  if (!valid || msgtype > TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Invalid message type");
      return FALSE;
    }

  part = tp_message_dup_part (message, 1);
  g_variant_lookup (part, "content", "&s", &msgtext);

  if (msgtext == NULL)
    {
      g_variant_unref (part);
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Empty message content");
      return FALSE;
    }

  msgtoken = salut_generate_id ();
  tp_message_set_string (message, 0, "message-token", msgtoken);

  if (text != NULL)
    *text = g_strdup (msgtext);

  if (type != NULL)
    *type = msgtype;

  if (token != NULL)
    *token = msgtoken;
  else
    g_free (msgtoken);

  g_variant_unref (part);
  return TRUE;
}

void
text_helper_report_delivery_error (TpSvcChannel *self,
                                   guint error_type,
                                   guint timestamp,
                                   guint type,
                                   const gchar *text,
                                   const gchar *token)
{
  TpBaseConnection *base_conn;
  TpHandle handle;
  guint entity_type;
  TpMessage *message;
  TpMessage *delivery_echo;

  g_object_get (self,
      "connection", &base_conn,
      "handle", &handle,
      "entity-type", &entity_type,
      NULL);

  delivery_echo = tp_cm_message_new (base_conn, 2);
  tp_cm_message_set_sender (delivery_echo,
      tp_base_connection_get_self_handle (base_conn));
  tp_message_set_uint32 (delivery_echo, 0, "message-type", type);
  tp_message_set_int64 (delivery_echo, 0, "message-sent", (gint64)timestamp);
  tp_message_set_string (delivery_echo, 1, "content-type", "text/plain");
  tp_message_set_string (delivery_echo, 1, "content", text);

  message = tp_cm_message_new (base_conn, 1);

  if (entity_type == TP_ENTITY_TYPE_CONTACT)
    tp_cm_message_set_sender (message, handle);

  tp_message_set_uint32 (message, 0, "message-type",
      TP_CHANNEL_TEXT_MESSAGE_TYPE_DELIVERY_REPORT);
  tp_message_set_uint32 (message, 0, "delivery-status",
      TP_DELIVERY_STATUS_TEMPORARILY_FAILED);
  tp_message_set_uint32 (message, 0, "delivery-error",
      TP_CHANNEL_TEXT_SEND_ERROR_OFFLINE);
  tp_message_set_string (message, 0, "delivery-token", token);
  tp_cm_message_take_message (message, 0, "delivery-echo", delivery_echo);

  g_object_unref (base_conn);

  tp_message_mixin_take_received (G_OBJECT (self), message);
}

TpMessage *
text_helper_create_received_message (TpBaseConnection *base_conn,
                                     guint sender_handle,
                                     guint timestamp,
                                     guint type,
                                     const gchar *text)
{
  TpMessage *message = tp_cm_message_new (base_conn, 2);

  tp_message_set_uint32 (message, 0, "message-type", type);
  tp_cm_message_set_sender (message, sender_handle);
  tp_message_set_int64 (message, 0, "message-received", timestamp);

  tp_message_set_string (message, 1, "content-type", "text/plain");
  tp_message_set_string (message, 1, "content", text);

  return message;
}
