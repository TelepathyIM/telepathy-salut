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

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <gibber/gibber-namespaces.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/message-mixin.h>
#include <wocky/wocky-namespaces.h>

#define DEBUG_FLAG DEBUG_IM
#include "debug.h"

#include "text-helper.h"

#include "salut-util.h"

static void
add_text (GibberXmppStanza *stanza, const gchar *text)
{
  WockyNode *node = wocky_stanza_get_top_node (stanza);
  GibberXmppNode *htmlnode;

  gibber_xmpp_node_add_child_with_content (node, "body", text);

  /* Add plain xhtml-im node */
  htmlnode = gibber_xmpp_node_add_child_ns (node, "html",
      GIBBER_XMPP_NS_XHTML_IM);
  gibber_xmpp_node_add_child_with_content_ns (htmlnode, "body", text,
      GIBBER_W3C_NS_XHTML);
}

static GibberXmppStanza *
create_message_stanza (const gchar *from,
  const gchar *to, TpChannelTextMessageType type, const gchar *text,
  GError **error)
{
  GibberXmppStanza *stanza;
  WockyNode *node;

  if (type > TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE)
    {
      DEBUG ("invalid message type %u", type);

      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "invalid message type: %u", type);

      return NULL;
    }
  stanza = gibber_xmpp_stanza_new_ns ("message", WOCKY_XMPP_NS_JABBER_CLIENT);
  node = wocky_stanza_get_top_node (stanza);

  gibber_xmpp_node_set_attribute (node, "from", from);
  gibber_xmpp_node_set_attribute (node, "to", to);

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

  return stanza;
}

GibberXmppStanza *
text_helper_create_message (const gchar *from,
                            const gchar *to,
                            TpChannelTextMessageType type,
                            const gchar *text,
                            GError **error)
{
  GibberXmppStanza *stanza;
  WockyNode *node;

  stanza = create_message_stanza (from, to, type, text, error);
  node = wocky_stanza_get_top_node (stanza);

  if (stanza == NULL)
    return NULL;

  switch (type)
    {
      case TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL:
      case TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION:
        gibber_xmpp_node_set_attribute (node, "type", "chat");
        break;
      case TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE:
        gibber_xmpp_node_set_attribute (node, "type", "normal");
        break;
      default:
        g_assert_not_reached ();
        break;
    }

  return stanza;
}

GibberXmppStanza *
text_helper_create_message_groupchat (const gchar *from,
                                      const gchar *to,
                                      TpChannelTextMessageType type,
                                      const gchar *text,
                                      GError **error)
{
  GibberXmppStanza *stanza;
  WockyNode *node;

  stanza = create_message_stanza (from, to, type, text, error);
  if (stanza == NULL)
    return NULL;

  node = wocky_stanza_get_top_node (stanza);

  switch (type)
    {
      case TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL:
      case TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION:
        gibber_xmpp_node_set_attribute (node, "type", "groupchat");
        break;
      case TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE:
        gibber_xmpp_node_set_attribute (node, "type", "normal");
        break;
      default:
        g_assert_not_reached ();
        break;
    }

  return stanza;
}

gboolean
text_helper_parse_incoming_message (GibberXmppStanza *stanza,
                        const gchar **from,
                        TpChannelTextMessageType *msgtype,
                        const gchar **body,
                        const gchar **body_offset)
{
  const gchar *type;
  GibberXmppNode *node;
  GibberXmppNode *event;
  WockyNode *top_node = wocky_stanza_get_top_node (stanza);

  *from = gibber_xmpp_node_get_attribute (top_node, "from");
  type = gibber_xmpp_node_get_attribute (top_node, "type");
  /* Work around iChats strange way of doing typing notification */
  event = gibber_xmpp_node_get_child_ns (top_node, "x",
    GIBBER_XMPP_NS_EVENT);

  if (event != NULL)
    {
      /* If the event has a composing and an id child, this is a typing
       * notification and it should be dropped */
      if (gibber_xmpp_node_get_child (event, "composing") != NULL &&
          gibber_xmpp_node_get_child (event, "id") != NULL)
        {
          return FALSE;
        }
    }
  /*
   * Parse body if it exists.
   */
  node = gibber_xmpp_node_get_child (top_node, "body");

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
  const GHashTable *part;
  guint msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
  const gchar *msgtext;
  gchar *msgtoken;
  gboolean valid = TRUE;

  if (tp_message_count_parts (message) != 2)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Invalid number of message parts, expected 2, got %d",
          tp_message_count_parts (message));
      return FALSE;
    }

  part = tp_message_peek (message, 0);

  if (tp_asv_lookup (part, "message-type"))
    msgtype = tp_asv_get_uint32 (part, "message-type", &valid);

  if (!valid || msgtype > TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Invalid message type");
      return FALSE;
    }

  part = tp_message_peek (message, 1);
  msgtext = tp_asv_get_string (part, "content");

  if (msgtext == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
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
  guint handle_type;
  TpMessage *message;
  TpMessage *delivery_echo;

  g_object_get (self,
      "connection", &base_conn,
      "handle", &handle,
      "handle-type", &handle_type,
      NULL);

  delivery_echo = tp_message_new (base_conn, 2, 2);
  tp_message_set_handle (delivery_echo, 0, "message-sender",
      TP_HANDLE_TYPE_CONTACT, base_conn->self_handle);
  tp_message_set_uint32 (delivery_echo, 0, "message-type", type);
  tp_message_set_uint64 (delivery_echo, 0, "message-sent", (guint64)timestamp);
  tp_message_set_string (delivery_echo, 1, "content-type", "text/plain");
  tp_message_set_string (delivery_echo, 1, "content", text);

  message = tp_message_new (base_conn, 1, 1);
  tp_message_set_handle (message, 0, "message-sender", handle_type, handle);
  tp_message_set_uint32 (message, 0, "message-type",
      TP_CHANNEL_TEXT_MESSAGE_TYPE_DELIVERY_REPORT);
  tp_message_set_uint32 (message, 0, "delivery-status",
      TP_DELIVERY_STATUS_TEMPORARILY_FAILED);
  tp_message_set_uint32 (message, 0, "delivery-error",
      TP_CHANNEL_TEXT_SEND_ERROR_OFFLINE);
  tp_message_set_string (message, 0, "delivery-token", token);
  tp_message_take_message (message, 0, "delivery-echo", delivery_echo);

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
  TpMessage *message = tp_message_new (base_conn, 2, 2);

  tp_message_set_uint32 (message, 0, "message-type", type);
  tp_message_set_handle (message, 0, "message-sender",
      TP_HANDLE_TYPE_CONTACT, sender_handle);
  tp_message_set_uint32 (message, 0, "message-received", timestamp);

  tp_message_set_string (message, 1, "content-type", "text/plain");
  tp_message_set_string (message, 1, "content", text);

  return message;
}
