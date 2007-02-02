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

GibberXmppStanza *
gibber_xmpp_stanza_new(gchar *name);

G_END_DECLS

#endif /* #ifndef __GIBBER_XMPP_STANZA_H__*/
