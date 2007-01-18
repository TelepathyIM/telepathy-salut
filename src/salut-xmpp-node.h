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
  gchar *content;

  /* Private */
  GQuark ns;
  GSList *attributes;
  GSList *children;
};

typedef gboolean (*salut_xmpp_node_each_attr_func)(const gchar *key, 
                                                   const gchar *value,
                                                   const gchar *ns,
                                                   gpointer user_data);

typedef gboolean (*salut_xmpp_node_each_child_func)(SalutXmppNode *node,
                                                    gpointer user_data);

void salut_xmpp_node_each_attribute(SalutXmppNode *node,
                                    salut_xmpp_node_each_attr_func func,
                                    gpointer user_data);
void salut_xmpp_node_each_child(SalutXmppNode *node,
                                salut_xmpp_node_each_child_func func,
                                gpointer user_data);

const gchar *salut_xmpp_node_get_attribute(SalutXmppNode *node, 
                                           const gchar *key);
const gchar *salut_xmpp_node_get_attribute_ns(SalutXmppNode *node, 
                                              const gchar *key,
                                              const gchar *ns);

void  salut_xmpp_node_set_attribute(SalutXmppNode *node, 
                                    const gchar *key,
                                    const gchar *value);

void  salut_xmpp_node_set_attribute_ns(SalutXmppNode *node, 
                                       const gchar *key,
                                       const gchar *value,
                                       const gchar *ns);

/* Set attribute with the given size for the value */
void salut_xmpp_node_set_attribute_n(SalutXmppNode *node, 
                                      const gchar *key, 
                                      const gchar *value,
                                      gsize value_size);
void salut_xmpp_node_set_attribute_n_ns(SalutXmppNode *node, 
                                         const gchar *key, 
                                         const gchar *value,
                                         gsize value_size,
                                         const gchar *ns);

/* Getting children */
SalutXmppNode *salut_xmpp_node_get_child(SalutXmppNode *node,
                                         const gchar *name);
SalutXmppNode *salut_xmpp_node_get_child_ns(SalutXmppNode *node,
                                            const gchar *name,
                                            const gchar *ns);

/* Creating child nodes */
SalutXmppNode *salut_xmpp_node_add_child(SalutXmppNode *node, 
                                         const gchar *name);
SalutXmppNode *salut_xmpp_node_add_child_ns(SalutXmppNode *node, 
                                            const gchar *name,
                                            const gchar *ns);

SalutXmppNode *salut_xmpp_node_add_child_with_content(SalutXmppNode *node, 
                                                       const gchar *name,
                                                       const char *content);
SalutXmppNode *salut_xmpp_node_add_child_with_content_ns(SalutXmppNode *node, 
                                                       const gchar *name,
                                                       const gchar *content,
                                                       const gchar *ns);

/* Setting/Getting namespaces */
void salut_xmpp_node_set_ns(SalutXmppNode *node, const gchar *ns);
const gchar *salut_xmpp_node_get_ns(SalutXmppNode *node);


/* Setting or adding content */
void salut_xmpp_node_set_content(SalutXmppNode *node, const gchar *content);
void salut_xmpp_node_append_content(SalutXmppNode *node, const gchar *content);
void salut_xmpp_node_append_content_n(SalutXmppNode *node, 
                                      const gchar *content,
                                       gsize size);

/* Create a new standalone node, usually only used by the stanza object */
SalutXmppNode *salut_xmpp_node_new(const char *name);

/* Frees the node and all it's children! */
void salut_xmpp_node_free(SalutXmppNode *node);

G_END_DECLS

#endif /* #ifndef __SALUT_XMPP_NODE_H__*/
