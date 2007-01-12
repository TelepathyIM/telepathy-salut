/*
 * salut-xmpp-node.h - Header for Salut xmpp nodes
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

#ifndef __SALUT_XMPP_NODE_H__
#define __SALUT_XMPP_NODE_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct _SalutXmppNode SalutXmppNode;

struct _SalutXmppNode {
  gchar *name;
  gchar *value;

  GSList *attributes;
  GSList *children;
};


const gchar *salut_xmpp_node_get_attribute(SalutXmppNode *node, 
                                           const gchar *key);

void  salut_xmpp_node_set_attribute(SalutXmppNode *node, 
                                    const gchar *key,
                                    const gchar *value);

SalutXmppNode *salut_xmpp_node_get_child(SalutXmppNode *node,
                                         const gchar *name);

SalutXmppNode *salut_xmpp_node_add_child(SalutXmppNode *node,
                                         const gchar *name,
                                         const gchar *value);

/* Node creator and destructor, should only be called by the owner, e.g.
 * salut-xmpp-stanza */
SalutXmppNode *salut_xmpp_node_new(const char *name, const gchar *value);
/* Frees the node and all it's children! */
void salut_xmpp_node_free(SalutXmppNode *node);

G_END_DECLS

#endif /* #ifndef __SALUT_XMPP_NODE_H__*/
