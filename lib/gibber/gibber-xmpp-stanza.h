/*
 * gibber-xmpp-stanza.h - Header for GibberXmppStanza
 * Copyright (C) 2006 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd@luon.net>
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

#ifndef __GIBBER_XMPP_STANZA_H__
#define __GIBBER_XMPP_STANZA_H__

#include <glib-object.h>
#include "gibber-xmpp-node.h"
#include "wocky-stanza.h"

G_BEGIN_DECLS

typedef WockyStanza GibberXmppStanza;
typedef WockyStanzaClass GibberXmppStanzaClass;

#define GIBBER_TYPE_XMPP_STANZA WOCKY_TYPE_STANZA
#define GIBBER_XMPP_STANZA(obj) (WOCKY_STANZA (obj))
#define GIBBER_XMPP_STANZA_CLASS(klass) (WOCKY_STANZA_CLASS (klass))
#define GIBBER_IS_XMPP_STANZA(obj) (WOCKY_IS_STANZA (obj))
#define GIBBER_IS_XMPP_STANZA_CLASS(klass) (WOCKY_IS_STANZA_CLASS (klass))
#define GIBBER_XMPP_STANZA_GET_CLASS(obj) (WOCKY_STANZA_GET_CLASS (obj))

#define GIBBER_STANZA_TYPE_NONE            WOCKY_STANZA_TYPE_NONE
#define GIBBER_STANZA_TYPE_MESSAGE         WOCKY_STANZA_TYPE_MESSAGE
#define GIBBER_STANZA_TYPE_PRESENCE        WOCKY_STANZA_TYPE_PRESENCE
#define GIBBER_STANZA_TYPE_IQ              WOCKY_STANZA_TYPE_IQ
#define GIBBER_STANZA_TYPE_STREAM          WOCKY_STANZA_TYPE_STREAM
#define GIBBER_STANZA_TYPE_STREAM_FEATURES WOCKY_STANZA_TYPE_STREAM_FEATURES
#define GIBBER_STANZA_TYPE_AUTH            WOCKY_STANZA_TYPE_AUTH
#define GIBBER_STANZA_TYPE_CHALLENGE       WOCKY_STANZA_TYPE_CHALLENGE
#define GIBBER_STANZA_TYPE_RESPONSE        WOCKY_STANZA_TYPE_RESPONSE
#define GIBBER_STANZA_TYPE_SUCCESS         WOCKY_STANZA_TYPE_SUCCESS
#define GIBBER_STANZA_TYPE_FAILURE         WOCKY_STANZA_TYPE_FAILURE
#define GIBBER_STANZA_TYPE_STREAM_ERROR    WOCKY_STANZA_TYPE_STREAM_ERROR
#define GIBBER_STANZA_TYPE_UNKNOWN         WOCKY_STANZA_TYPE_UNKNOWN
#define NUM_GIBBER_STANZA_TYPE             NUM_WOCKY_STANZA_TYPE
typedef WockyStanzaType GibberStanzaType;

#define GIBBER_STANZA_SUB_TYPE_NONE         WOCKY_STANZA_SUB_TYPE_NONE
#define GIBBER_STANZA_SUB_TYPE_AVAILABLE    WOCKY_STANZA_SUB_TYPE_AVAILABLE
#define GIBBER_STANZA_SUB_TYPE_NORMAL       WOCKY_STANZA_SUB_TYPE_NORMAL
#define GIBBER_STANZA_SUB_TYPE_CHAT         WOCKY_STANZA_SUB_TYPE_CHAT
#define GIBBER_STANZA_SUB_TYPE_GROUPCHAT    WOCKY_STANZA_SUB_TYPE_GROUPCHAT
#define GIBBER_STANZA_SUB_TYPE_HEADLINE     WOCKY_STANZA_SUB_TYPE_HEADLINE
#define GIBBER_STANZA_SUB_TYPE_UNAVAILABLE  WOCKY_STANZA_SUB_TYPE_UNAVAILABLE
#define GIBBER_STANZA_SUB_TYPE_PROBE        WOCKY_STANZA_SUB_TYPE_PROBE
#define GIBBER_STANZA_SUB_TYPE_SUBSCRIBE    WOCKY_STANZA_SUB_TYPE_SUBSCRIBE
#define GIBBER_STANZA_SUB_TYPE_UNSUBSCRIBE  WOCKY_STANZA_SUB_TYPE_UNSUBSCRIBE
#define GIBBER_STANZA_SUB_TYPE_SUBSCRIBED   WOCKY_STANZA_SUB_TYPE_SUBSCRIBED
#define GIBBER_STANZA_SUB_TYPE_UNSUBSCRIBED WOCKY_STANZA_SUB_TYPE_UNSUBSCRIBED
#define GIBBER_STANZA_SUB_TYPE_GET          WOCKY_STANZA_SUB_TYPE_GET
#define GIBBER_STANZA_SUB_TYPE_SET          WOCKY_STANZA_SUB_TYPE_SET
#define GIBBER_STANZA_SUB_TYPE_RESULT       WOCKY_STANZA_SUB_TYPE_RESULT
#define GIBBER_STANZA_SUB_TYPE_ERROR        WOCKY_STANZA_SUB_TYPE_ERROR
#define GIBBER_STANZA_SUB_TYPE_UNKNOWN      WOCKY_STANZA_SUB_TYPE_UNKNOWN
#define NUM_GIBBER_STANZA_SUB_TYPE          NUM_WOCKY_STANZA_SUB_TYPE
typedef WockyStanzaSubType GibberStanzaSubType;

#define GIBBER_NODE WOCKY_NODE_START
#define GIBBER_NODE_TEXT WOCKY_NODE_TEXT
#define GIBBER_NODE_END WOCKY_NODE_END
#define GIBBER_NODE_ATTRIBUTE WOCKY_NODE_ATTRIBUTE
#define GIBBER_NODE_XMLNS WOCKY_NODE_XMLNS
#define GIBBER_NODE_ASSIGN_TO WOCKY_NODE_ASSIGN_TO
#define GIBBER_STANZA_END NULL

#define gibber_xmpp_stanza_new_ns wocky_stanza_new
#define gibber_xmpp_stanza_build wocky_stanza_build
#define gibber_xmpp_stanza_get_type_info wocky_stanza_get_type_info

G_END_DECLS

#endif /* #ifndef __GIBBER_XMPP_STANZA_H__*/
