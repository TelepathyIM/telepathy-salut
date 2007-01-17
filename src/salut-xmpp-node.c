/*
 * salut-xmpp-node.c - Code for Salut xmpp nodes
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

#include <glib.h>
#include <string.h>

#include "salut-xmpp-node.h"

typedef struct {
  gchar *key;
  gchar *value;
} Tuple;

SalutXmppNode *
salut_xmpp_node_new(const char *name, const gchar *content) {
  SalutXmppNode *result = g_slice_new0(SalutXmppNode);

  result->name = g_strdup(name);
  result->content = g_strdup(content);

  return result;
}

/* Frees the node and all it's children */
void 
salut_xmpp_node_free(SalutXmppNode *node) {
  GSList *l;

  if (node == NULL)  {
    return ;
  }

  g_free(node->name);
  g_free(node->content);

  for (l = node->children; l != NULL ; l = l->next) {
    salut_xmpp_node_free((SalutXmppNode *)l->data);
  }

  for (l = node->attributes; l != NULL ; l = l->next) {
    Tuple *t = (Tuple *)l->data;
    g_free(t->key);
    g_free(t->value);
    g_slice_free(Tuple, t);
  }
  g_slice_free(SalutXmppNode, node);
}

void 
salut_xmpp_node_each_attribute(SalutXmppNode *node,
                               salut_xmpp_node_each_attr_func func,
                               gpointer user_data) {
  GSList *l;
  for (l = node->attributes; l != NULL ; l = l->next) {
    Tuple *t = (Tuple *)l->data;
    if (!func(t->key, t->value, user_data)) {
      return;
    }
  }
}

void 
salut_xmpp_node_each_child(SalutXmppNode *node,
                           salut_xmpp_node_each_child_func func,
                           gpointer user_data) {
  GSList *l;
  for (l = node->children; l != NULL ; l = l->next) {
    SalutXmppNode *n = (SalutXmppNode *)l->data;
    if (!func(n, user_data)) {
      return;
    }
  }
}

static gint 
tuple_compare_key(gconstpointer a, gconstpointer b) {
  const Tuple *tuple = (const Tuple *)a;
  const gchar *key = (const gchar *)b;

  return strcmp(tuple->key, key);
}


const gchar *
salut_xmpp_node_get_attribute(SalutXmppNode *node, const gchar *key) {
  GSList *link;

  link = g_slist_find_custom(node->children, key, tuple_compare_key); 

  return (link == NULL) ? NULL : ((Tuple *)(link->data))->value;
}

void  
salut_xmpp_node_set_attribute(SalutXmppNode *node, 
                              const gchar *key, const gchar *value) {
  Tuple *t = g_slice_new0(Tuple);
  t->key = g_strdup(key);
  t->value = g_strdup(value);

  node->attributes = g_slist_append(node->attributes, t);
}

void  
salut_xmpp_node_set_attribute_n(SalutXmppNode *node, 
                                const gchar *key, 
                                const gchar *value,
                                gsize value_size) {
  Tuple *t = g_slice_new0(Tuple);
  t->key = g_strdup(key);
  t->value = g_strndup(value, value_size);

  node->attributes = g_slist_append(node->attributes, t);
}

static gint 
node_compare_name(gconstpointer a, gconstpointer b) {
  const SalutXmppNode *node = (const SalutXmppNode *)a;
  const gchar *name = (const gchar *)b;

  return strcmp(node->name, name);
}

SalutXmppNode *
salut_xmpp_node_get_child(SalutXmppNode *node, const gchar *name) {
  GSList *link;

  link = g_slist_find_custom(node->children, name, node_compare_name); 

  return (link == NULL) ? NULL : (SalutXmppNode *)(link->data);
}

SalutXmppNode *
salut_xmpp_node_add_child(SalutXmppNode *node, const gchar *name,
                          const gchar *content) {
  SalutXmppNode *new = salut_xmpp_node_new(name, content);
  node->children = g_slist_append(node->children, new);

  return new;
}

void 
salut_xmpp_node_set_content(SalutXmppNode *node, 
                            const gchar *content) {
  g_free(node->content);
  node->content = g_strdup(content); 
}

void salut_xmpp_node_append_content(SalutXmppNode *node, 
                                    const gchar *content) {
  gchar *t = node->content;
  node->content = g_strconcat(t, content, NULL);
  g_free(t);
}

void salut_xmpp_node_append_content_n(SalutXmppNode *node, 
                                      const gchar *content,
                                       gsize size) {
  gsize csize = node->content != NULL ? strlen(node->content) : 0; 
  node->content = g_realloc(node->content, csize + size + 1);
  g_strlcpy(node->content + csize, content, size + 1);
}
