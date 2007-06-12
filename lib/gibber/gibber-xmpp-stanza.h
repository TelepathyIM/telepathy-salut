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

G_BEGIN_DECLS

typedef struct _GibberXmppStanza GibberXmppStanza;
typedef struct _GibberXmppStanzaClass GibberXmppStanzaClass;

struct _GibberXmppStanzaClass {
    GObjectClass parent_class;
};

struct _GibberXmppStanza {
    GObject parent;
    GibberXmppNode *node;
};

GType gibber_xmpp_stanza_get_type(void);

/* TYPE MACROS */
#define GIBBER_TYPE_XMPP_STANZA \
  (gibber_xmpp_stanza_get_type())
#define GIBBER_XMPP_STANZA(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_XMPP_STANZA, GibberXmppStanza))
#define GIBBER_XMPP_STANZA_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_XMPP_STANZA, GibberXmppStanzaClass))
#define GIBBER_IS_XMPP_STANZA(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_XMPP_STANZA))
#define GIBBER_IS_XMPP_STANZA_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_XMPP_STANZA))
#define GIBBER_XMPP_STANZA_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_XMPP_STANZA, GibberXmppStanzaClass))

typedef enum
{
  GIBBER_STANZA_TYPE_NONE,
  GIBBER_STANZA_TYPE_MESSAGE,
  GIBBER_STANZA_TYPE_PRESENCE,
  GIBBER_STANZA_TYPE_IQ,
  GIBBER_STANZA_TYPE_STREAM,
  GIBBER_STANZA_TYPE_STREAM_FEATURES,
  GIBBER_STANZA_TYPE_AUTH,
  GIBBER_STANZA_TYPE_CHALLENGE,
  GIBBER_STANZA_TYPE_RESPONSE,
  GIBBER_STANZA_TYPE_SUCCESS,
  GIBBER_STANZA_TYPE_FAILURE,
  GIBBER_STANZA_TYPE_STREAM_ERROR,
  LAST_GIBBER_STANZA_TYPE
} GibberStanzaType;

typedef enum
{
  GIBBER_STANZA_SUB_TYPE_NOT_SET,
  GIBBER_STANZA_SUB_TYPE_AVAILABLE,
  GIBBER_STANZA_SUB_TYPE_NORMAL,
  GIBBER_STANZA_SUB_TYPE_CHAT,
  GIBBER_STANZA_SUB_TYPE_GROUPCHAT,
  GIBBER_STANZA_SUB_TYPE_HEADLINE,
  GIBBER_STANZA_SUB_TYPE_UNAVAILABLE,
  GIBBER_STANZA_SUB_TYPE_PROBE,
  GIBBER_STANZA_SUB_TYPE_SUBSCRIBE,
  GIBBER_STANZA_SUB_TYPE_UNSUBSCRIBE,
  GIBBER_STANZA_SUB_TYPE_SUBSCRIBED,
  GIBBER_STANZA_SUB_TYPE_UNSUBSCRIBED,
  GIBBER_STANZA_SUB_TYPE_GET,
  GIBBER_STANZA_SUB_TYPE_SET,
  GIBBER_STANZA_SUB_TYPE_RESULT,
  GIBBER_STANZA_SUB_TYPE_ERROR,
  LAST_GIBBER_STANZA_SUB_TYPE
} GibberStanzaSubType;

enum
{
  GIBBER_NODE,
  GIBBER_NODE_TEXT,
  GIBBER_NODE_END,
  GIBBER_NODE_ATTRIBUTE,
  GIBBER_NODE_XMLNS,
  GIBBER_STANZA_END
};

GibberXmppStanza *
gibber_xmpp_stanza_new(const gchar *name);

GibberXmppStanza *
gibber_xmpp_stanza_build (GibberStanzaType type, GibberStanzaSubType sub_type,
    const gchar *from, const gchar *to, guint spec, ...);

G_END_DECLS

#endif /* #ifndef __GIBBER_XMPP_STANZA_H__*/
