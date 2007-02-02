/*
 * gibber-xmpp-node.h - Header for Gibber xmpp nodes
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

#ifndef __GIBBER_XMPP_NODE_H__
#define __GIBBER_XMPP_NODE_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct _GibberXmppNode GibberXmppNode;

struct _GibberXmppNode {
  gchar *name;
  gchar *content;

  /* Private */
  gchar *language;
  GQuark ns;
  GSList *attributes;
  GSList *children;
};

typedef gboolean (*gibber_xmpp_node_each_attr_func)(const gchar *key, 
                                                   const gchar *value,
                                                   const gchar *ns,
                                                   gpointer user_data);

typedef gboolean (*gibber_xmpp_node_each_child_func)(GibberXmppNode *node,
                                                    gpointer user_data);

void gibber_xmpp_node_each_attribute(GibberXmppNode *node,
                                    gibber_xmpp_node_each_attr_func func,
                                    gpointer user_data);
void gibber_xmpp_node_each_child(GibberXmppNode *node,
                                gibber_xmpp_node_each_child_func func,
                                gpointer user_data);

const gchar *gibber_xmpp_node_get_attribute(GibberXmppNode *node, 
                                           const gchar *key);
const gchar *gibber_xmpp_node_get_attribute_ns(GibberXmppNode *node, 
                                              const gchar *key,
                                              const gchar *ns);

void  gibber_xmpp_node_set_attribute(GibberXmppNode *node, 
                                    const gchar *key,
                                    const gchar *value);

void  gibber_xmpp_node_set_attribute_ns(GibberXmppNode *node, 
                                       const gchar *key,
                                       const gchar *value,
                                       const gchar *ns);

/* Set attribute with the given size for the value */
void gibber_xmpp_node_set_attribute_n(GibberXmppNode *node, 
                                      const gchar *key, 
                                      const gchar *value,
                                      gsize value_size);
void gibber_xmpp_node_set_attribute_n_ns(GibberXmppNode *node, 
                                         const gchar *key, 
                                         const gchar *value,
                                         gsize value_size,
                                         const gchar *ns);

/* Getting children */
GibberXmppNode *gibber_xmpp_node_get_child(GibberXmppNode *node,
                                         const gchar *name);
GibberXmppNode *gibber_xmpp_node_get_child_ns(GibberXmppNode *node,
                                            const gchar *name,
                                            const gchar *ns);

/* Creating child nodes */
GibberXmppNode *gibber_xmpp_node_add_child(GibberXmppNode *node, 
                                         const gchar *name);
GibberXmppNode *gibber_xmpp_node_add_child_ns(GibberXmppNode *node, 
                                            const gchar *name,
                                            const gchar *ns);

GibberXmppNode *gibber_xmpp_node_add_child_with_content(GibberXmppNode *node, 
                                                       const gchar *name,
                                                       const char *content);
GibberXmppNode *gibber_xmpp_node_add_child_with_content_ns(GibberXmppNode *node, 
                                                       const gchar *name,
                                                       const gchar *content,
                                                       const gchar *ns);

/* Setting/Getting namespaces */
void gibber_xmpp_node_set_ns(GibberXmppNode *node, const gchar *ns);
const gchar *gibber_xmpp_node_get_ns(GibberXmppNode *node);

/* Setting/Getting language */
const gchar *gibber_xmpp_node_get_language(GibberXmppNode *node);
void gibber_xmpp_node_set_language(GibberXmppNode *node, const gchar *lang);
void gibber_xmpp_node_set_language_n(GibberXmppNode *node, 
                                    const gchar *lang,
                                    gsize lang_size);


/* Setting or adding content */
void gibber_xmpp_node_set_content(GibberXmppNode *node, const gchar *content);
void gibber_xmpp_node_append_content(GibberXmppNode *node, const gchar *content);
void gibber_xmpp_node_append_content_n(GibberXmppNode *node, 
                                      const gchar *content,
                                       gsize size);

/* Create a new standalone node, usually only used by the stanza object */
GibberXmppNode *gibber_xmpp_node_new(const char *name);

/* Frees the node and all it's children! */
void gibber_xmpp_node_free(GibberXmppNode *node);

G_END_DECLS

#endif /* #ifndef __GIBBER_XMPP_NODE_H__*/
