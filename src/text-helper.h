/*
 * text-helper.h - Header for TextHelper
 * Copyright (C) 2006 Collabora Ltd.
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

#ifndef __TEXT_HELPER_H__
#define __TEXT_HELPER_H__

#include <telepathy-glib/enums.h>
#include <gibber/gibber-xmpp-stanza.h>


/* Utility functions for the helper user */
gboolean 
text_helper_parse_incoming_message (GibberXmppStanza *stanza, 
    const gchar **from, TpChannelTextMessageType *msgtype, 
    const gchar **body, const gchar **body_offset);

GibberXmppStanza * 
text_helper_create_message (const gchar *from,
    const gchar *to, TpChannelTextMessageType type, 
    const gchar *text, GError **error);

GibberXmppStanza * 
text_helper_create_message_groupchat (const gchar *from, const gchar *to,
    TpChannelTextMessageType type, const gchar *text, GError **error);

G_END_DECLS

#endif /* #ifndef __TEXT_HELPER_H__ */
