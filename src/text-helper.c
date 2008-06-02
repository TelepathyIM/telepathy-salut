/*
 * text-helper.c - Source for TextHelper
 * Copyright (C) 2006 Collabora Ltd.
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

#include <telepathy-glib/errors.h>

#define DEBUG_FLAG DEBUG_IM
#include "debug.h"

#include "text-helper.h"

static void
add_text(GibberXmppStanza *stanza, const gchar *text) {
  GibberXmppNode *htmlnode;

  gibber_xmpp_node_add_child_with_content(stanza->node, "body", text);

  /* Add plain xhtml-im node */
  htmlnode = gibber_xmpp_node_add_child_ns(stanza->node, "html",
                 GIBBER_XMPP_NS_XHTML_IM);
  gibber_xmpp_node_add_child_with_content_ns(htmlnode,
      "body", text, GIBBER_W3C_NS_XHTML);
}

GibberXmppStanza *
create_message_stanza (const gchar *from,
                       const gchar *to,
                       TpChannelTextMessageType type,
                       const gchar *text,
                       GError **error)
{
  GibberXmppStanza *stanza;

  if (type > TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE)
    {
      DEBUG ("invalid message type %u", type);

      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "invalid message type: %u", type);

      return NULL;
    }
  stanza = gibber_xmpp_stanza_new ("message");

  gibber_xmpp_node_set_attribute (stanza->node, "from", from);
  gibber_xmpp_node_set_attribute (stanza->node, "to", to);

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

  stanza = create_message_stanza (from, to, type, text, error);
  if (stanza == NULL)
    return NULL;

  switch (type)
    {
      case TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL:
      case TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION:
        gibber_xmpp_node_set_attribute (stanza->node, "type", "chat");
        break;
      case TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE:
        gibber_xmpp_node_set_attribute (stanza->node, "type", "normal");
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

  stanza = create_message_stanza (from, to, type, text, error);
  if (stanza == NULL)
    return NULL;

  switch (type)
    {
      case TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL:
      case TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION:
        gibber_xmpp_node_set_attribute (stanza->node, "type", "groupchat");
        break;
      case TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE:
        gibber_xmpp_node_set_attribute (stanza->node, "type", "normal");
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

  *from = gibber_xmpp_node_get_attribute (stanza->node, "from");
  type = gibber_xmpp_node_get_attribute (stanza->node, "type");
  /*
   * Parse body if it exists.
   */
  node = gibber_xmpp_node_get_child (stanza->node, "body");

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
