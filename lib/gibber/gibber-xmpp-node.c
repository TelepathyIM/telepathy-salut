/*
 * gibber-xmpp-node.c - Code for Gibber xmpp nodes
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

#include "gibber-xmpp-node.h"

typedef struct {
  gchar *key;
  gchar *value;
  GQuark ns;
} Attribute;

typedef struct {
  const gchar *key;
  GQuark ns;
} Tuple;

GibberXmppNode *
gibber_xmpp_node_new (const char *name)
{
  GibberXmppNode *result = g_slice_new0 (GibberXmppNode);

  result->name = g_strdup (name);

  return result;
}

GibberXmppNode *
gibber_xmpp_node_new_ns (const char *name,
    const char *ns)
{
  GibberXmppNode *result = gibber_xmpp_node_new (name);

  gibber_xmpp_node_set_ns (result, ns);

  return result;
}

/* Frees the node and all it's children */
void
gibber_xmpp_node_free (GibberXmppNode *node)
{
  GSList *l;

  if (node == NULL)
    {
      return ;
    }

  g_free (node->name);
  g_free (node->content);
  g_free (node->language);

  for (l = node->children; l != NULL ; l = l->next)
    {
      gibber_xmpp_node_free ((GibberXmppNode *) l->data);
    }
  g_slist_free (node->children);

  for (l = node->attributes; l != NULL ; l = l->next)
    {
      Attribute *a = (Attribute *) l->data;
      g_free (a->key);
      g_free (a->value);
      g_slice_free (Attribute, a);
    }
  g_slist_free (node->attributes);

  g_slice_free (GibberXmppNode, node);
}

void
gibber_xmpp_node_each_attribute (GibberXmppNode *node,
    gibber_xmpp_node_each_attr_func func, gpointer user_data)
{
  GSList *l;

  for (l = node->attributes; l != NULL ; l = l->next)
    {
      Attribute *a = (Attribute *) l->data;
      if (!func (a->key, a->value, g_quark_to_string (a->ns), user_data))
        {
          return;
        }
    }
}

void
gibber_xmpp_node_each_child (GibberXmppNode *node,
    gibber_xmpp_node_each_child_func func, gpointer user_data)
{
  GSList *l;

  for (l = node->children; l != NULL ; l = l->next)
    {
      GibberXmppNode *n = (GibberXmppNode *) l->data;
      if (!func (n, user_data))
        {
          return;
        }
    }
}

static gint
attribute_compare (gconstpointer a, gconstpointer b)
{
  const Attribute *attr = (const Attribute *)a;
  const Tuple *target = (const Tuple *)b;

  if (target->ns != 0 && attr->ns != target->ns)
    {
      return 1;
    }

  return strcmp (attr->key, target->key);
}


const gchar *
gibber_xmpp_node_get_attribute_ns (GibberXmppNode *node,
    const gchar *key, const gchar *ns)
{
  GSList *link;
  Tuple search;

  search.key = (gchar *) key;
  search.ns = (ns != NULL ? g_quark_from_string (ns) : 0);

  link = g_slist_find_custom (node->attributes, &search, attribute_compare);

  return (link == NULL) ? NULL : ((Attribute *) (link->data))->value;
}

const gchar *
gibber_xmpp_node_get_attribute (GibberXmppNode *node, const gchar *key)
{
  return gibber_xmpp_node_get_attribute_ns (node, key, NULL);
}

void
gibber_xmpp_node_set_attribute (GibberXmppNode *node,
    const gchar *key, const gchar *value)
{
  g_assert (value != NULL);
  gibber_xmpp_node_set_attribute_n_ns (node, key, value, strlen (value), NULL);
}

void
gibber_xmpp_node_set_attribute_ns (GibberXmppNode *node, const gchar *key,
    const gchar *value, const gchar *ns)
{
  gibber_xmpp_node_set_attribute_n_ns (node, key, value, strlen (value), ns);
}

void
gibber_xmpp_node_set_attribute_n_ns (GibberXmppNode *node, const gchar *key,
    const gchar *value, gsize value_size, const gchar *ns)
{
  Attribute *a = g_slice_new0 (Attribute);
  a->key = g_strdup (key);
  a->value = g_strndup (value, value_size);
  a->ns = (ns != NULL) ? g_quark_from_string (ns) : 0;

  node->attributes = g_slist_append (node->attributes, a);
}

void
gibber_xmpp_node_set_attribute_n (GibberXmppNode *node, const gchar *key,
    const gchar *value, gsize value_size)
{
  gibber_xmpp_node_set_attribute_n_ns (node, key, value, value_size, NULL);
}

static gint
node_compare_child (gconstpointer a, gconstpointer b)
{
  const GibberXmppNode *node = (const GibberXmppNode *)a;
  Tuple *target = (Tuple *) b;

  if (target->ns != 0 && target->ns != node->ns)
    {
      return 1;
    }

  return strcmp (node->name, target->key);
}

GibberXmppNode *
gibber_xmpp_node_get_child_ns (GibberXmppNode *node, const gchar *name,
     const gchar *ns)
{
  GSList *link;
  Tuple t;

  t.key = name;
  t.ns = (ns != NULL ?  g_quark_from_string (ns) : 0);

  link = g_slist_find_custom (node->children, &t, node_compare_child);

  return (link == NULL) ? NULL : (GibberXmppNode *) (link->data);
}

GibberXmppNode *
gibber_xmpp_node_get_child (GibberXmppNode *node, const gchar *name)
{
  return gibber_xmpp_node_get_child_ns (node, name, NULL);
}


GibberXmppNode *
gibber_xmpp_node_add_child (GibberXmppNode *node, const gchar *name)
{
  return gibber_xmpp_node_add_child_with_content_ns (node, name, NULL, NULL);
}

GibberXmppNode *
gibber_xmpp_node_add_child_ns (GibberXmppNode *node, const gchar *name,
    const gchar *ns)
{
  return gibber_xmpp_node_add_child_with_content_ns (node, name, NULL, ns);
}

GibberXmppNode *
gibber_xmpp_node_add_child_with_content (GibberXmppNode *node,
     const gchar *name, const char *content)
{
  return gibber_xmpp_node_add_child_with_content_ns (node, name,
      content, NULL);
}

GibberXmppNode *
gibber_xmpp_node_add_child_with_content_ns (GibberXmppNode *node,
    const gchar *name, const gchar *content, const gchar *ns)
{
  GibberXmppNode *result = gibber_xmpp_node_new_ns (name, ns);

  gibber_xmpp_node_set_content (result, content);

  node->children = g_slist_append (node->children, result);
  return result;
}

void
gibber_xmpp_node_set_ns (GibberXmppNode *node, const gchar *ns)
{
  node->ns = (ns != NULL) ? g_quark_from_string (ns) : 0;
}

const gchar *
gibber_xmpp_node_get_ns (GibberXmppNode *node)
{
  return g_quark_to_string (node->ns);
}

const gchar *
gibber_xmpp_node_get_language (GibberXmppNode *node)
{
  g_return_val_if_fail (node != NULL, NULL);
  return node->language;
}

void
gibber_xmpp_node_set_language_n (GibberXmppNode *node, const gchar *lang,
    gsize lang_size)
{
  g_free (node->language);
  node->language = g_strndup (lang, lang_size);
}

void
gibber_xmpp_node_set_language (GibberXmppNode *node, const gchar *lang)
{
  gsize lang_size = 0;
  if (lang != NULL) {
    lang_size = strlen (lang);
  }
  gibber_xmpp_node_set_language_n (node, lang, lang_size);
}


void
gibber_xmpp_node_set_content (GibberXmppNode *node, const gchar *content)
{
  g_free (node->content);
  node->content = g_strdup (content);
}

void gibber_xmpp_node_append_content (GibberXmppNode *node,
    const gchar *content)
{
  gchar *t = node->content;
  node->content = g_strconcat (t, content, NULL);
  g_free (t);
}

void
gibber_xmpp_node_append_content_n (GibberXmppNode *node, const gchar *content,
    gsize size)
{
  gsize csize = node->content != NULL ? strlen (node->content) : 0;
  node->content = g_realloc (node->content, csize + size + 1);
  g_strlcpy (node->content + csize, content, size + 1);
}

typedef struct
{
  GString *string;
  gchar *indent;
} _NodeToStringData;

static gboolean
attribute_to_string (const gchar *key, const gchar *value, const gchar *ns,
    gpointer user_data)
{
  _NodeToStringData *data = user_data;

  g_string_append_c (data->string, ' ');
  if (ns != NULL)
    {
      g_string_append (data->string, ns);
      g_string_append_c (data->string, ':');
    }
  g_string_append_printf (data->string, "%s='%s'", key, value);

  return TRUE;
}

static gboolean
node_to_string (GibberXmppNode *node, gpointer user_data)
{
  _NodeToStringData *data = user_data;
  gchar *old_indent;
  const gchar *ns;

  g_string_append_printf (data->string, "%s<%s", data->indent, node->name);
  ns = gibber_xmpp_node_get_ns (node);

  if (ns != NULL)
    g_string_append_printf (data->string, " xmlns='%s'", ns);

  gibber_xmpp_node_each_attribute (node, attribute_to_string, data);
  g_string_append_printf (data->string, ">\n");

  old_indent = data->indent;
  data->indent = g_strconcat (data->indent, "  ", NULL);

  if (node->content != NULL)
    g_string_append_printf (data->string, "%s%s\n", data->indent,
        node->content);

  gibber_xmpp_node_each_child (node, node_to_string, data);
  g_free (data->indent);
  data->indent = old_indent;

  g_string_append_printf (data->string, "%s</%s>", data->indent, node->name);
  if (data->indent[0] != '\0')
    g_string_append_c (data->string, '\n');

  return TRUE;
}

gchar *
gibber_xmpp_node_to_string (GibberXmppNode *node)
{
  _NodeToStringData data;
  gchar *result;

  data.string = g_string_new ("");
  data.indent = "";
  node_to_string (node, &data);
  g_string_append_c (data.string, '\n');

  result = data.string->str;
  g_string_free (data.string, FALSE);
  return result;
}
