/*
 * salut-util.c - Code for Salut utility functions
 * Copyright (C) 2007 Collabora Ltd.
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

#include "salut-util.h"

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <telepathy-glib/util.h>

struct _xmpp_node_extract_property_data
{
  const gchar *prop;
  GHashTable *properties;
};

static gboolean
xmpp_node_extract_property (GibberXmppNode *node,
                            gpointer user_data)
{
  struct _xmpp_node_extract_property_data *data =
    (struct _xmpp_node_extract_property_data *) user_data;
  const gchar *name;
  const gchar *type;
  const gchar *value;
  GValue *gvalue;

  if (0 != strcmp (node->name, data->prop))
    return TRUE;

  name = gibber_xmpp_node_get_attribute (node, "name");

  if (!name)
    return TRUE;

  type = gibber_xmpp_node_get_attribute (node, "type");
  value = node->content;

  if (type == NULL || value == NULL)
    return TRUE;

  if (0 == strcmp (type, "bytes"))
    {
      GArray *arr;
      guchar *decoded;
      gsize len;

      decoded = g_base64_decode (value, &len);
      if (!decoded)
        return TRUE;

      arr = g_array_new (FALSE, FALSE, sizeof (guchar));
      g_array_append_vals (arr, decoded, len);
      gvalue = g_slice_new0 (GValue);
      g_value_init (gvalue, DBUS_TYPE_G_UCHAR_ARRAY);
      g_value_take_boxed (gvalue, arr);
      g_hash_table_insert (data->properties, g_strdup (name), gvalue);
      g_free (decoded);
    }
  else if (0 == strcmp (type, "str"))
    {
      gvalue = g_slice_new0 (GValue);
      g_value_init (gvalue, G_TYPE_STRING);
      g_value_set_string (gvalue, value);
      g_hash_table_insert (data->properties, g_strdup (name), gvalue);
    }
  else if (0 == strcmp (type, "int"))
    {
      gvalue = g_slice_new0 (GValue);
      g_value_init (gvalue, G_TYPE_INT);
      g_value_set_int (gvalue, atoi (value));
      g_hash_table_insert (data->properties, g_strdup (name), gvalue);
    }
  else if (0 == strcmp (type, "uint"))
    {
      gvalue = g_slice_new0 (GValue);
      g_value_init (gvalue, G_TYPE_UINT);
      g_value_set_uint (gvalue, atoi (value));
      g_hash_table_insert (data->properties, g_strdup (name), gvalue);
    }

  return TRUE;
}

/**
 * salut_gibber_xmpp_node_extract_properties
 *
 * Map a XML node to a properties hash table
 *
 * Example:
 *
 * <node>
 *   <prop name="prop1" type="str">prop1_value</prop>
 *   <prop name="prop2" type="uint">7</prop>
 * </node>
 *
 * salut_gibber_xmpp_node_extract_properties (node, "prop");
 *
 * --> { "prop1" : "prop1_value", "prop2" : 7 }
 *
 * Returns a hash table mapping names to GValue of the specified type.
 * Valid types are: str, int, uint, bytes.
 *
 */
GHashTable *
salut_gibber_xmpp_node_extract_properties (GibberXmppNode *node,
                                           const gchar *prop)
{
  GHashTable *properties;
  struct _xmpp_node_extract_property_data data;

  properties = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) tp_g_value_slice_free);

  if (node == NULL)
    return properties;

  data.prop = prop;
  data.properties = properties;

  gibber_xmpp_node_each_child (node, xmpp_node_extract_property, &data);

  return properties;
}

struct _set_child_from_property_data
{
  GibberXmppNode *node;
  const gchar *prop;
};

static void
set_child_from_property (gpointer key,
                         gpointer value,
                         gpointer user_data)
{
  GValue *gvalue = value;
  struct _set_child_from_property_data *data =
    (struct _set_child_from_property_data *) user_data;
  GibberXmppNode *child;
  const char *type = NULL;

  if (G_VALUE_TYPE (gvalue) == G_TYPE_STRING)
    {
      type = "str";
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_INT)
    {
      type = "int";
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_UINT)
    {
      type = "uint";
    }
  else if (G_VALUE_TYPE (gvalue) == DBUS_TYPE_G_UCHAR_ARRAY)
    {
      type = "bytes";
    }
  else
    {
      /* a type we don't know how to handle: ignore it */
      g_critical ("property with unknown type \"%s\"",
          g_type_name (G_VALUE_TYPE (gvalue)));
      return;
    }

  child = gibber_xmpp_node_add_child (data->node, data->prop);

  if (G_VALUE_TYPE (gvalue) == G_TYPE_STRING)
    {
      gibber_xmpp_node_set_content (child,
        g_value_get_string (gvalue));
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_INT)
    {
      gchar *str;

      str = g_strdup_printf ("%d", g_value_get_int (gvalue));
      gibber_xmpp_node_set_content (child, str);

      g_free (str);
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_UINT)
    {
      gchar *str;

      str = g_strdup_printf ("%d", g_value_get_uint (gvalue));
      gibber_xmpp_node_set_content (child, str);

      g_free (str);
    }
  else if (G_VALUE_TYPE (gvalue) == DBUS_TYPE_G_UCHAR_ARRAY)
    {
      GArray *arr;
      gchar *str;

      type = "bytes";
      arr = g_value_get_boxed (gvalue);
      str = g_base64_encode ((const guchar *) arr->data, arr->len);
      gibber_xmpp_node_set_content (child, str);

      g_free (str);
    }
  else
    {
      g_assert_not_reached ();
    }

  gibber_xmpp_node_set_attribute (child, "name", key);
  gibber_xmpp_node_set_attribute (child, "type", type);
}

/**
 *
 * gibber_xmpp_node_set_children_from_properties
 *
 * Map a properties hash table to a XML node.
 *
 * Example:
 *
 * properties = { "prop1" : "prop1_value", "prop2" : 7 }
 *
 * salut_gibber_xmpp_node_add_children_from_properties (node, properties,
 *     "prop");
 *
 * --> <node>
 *       <prop name="prop1" type="str">prop1_value</prop>
 *       <prop name="prop2" type="uint">7</prop>
 *     </node>
 *
 */
void
salut_gibber_xmpp_node_add_children_from_properties (GibberXmppNode *node,
                                                     GHashTable *properties,
                                                     const gchar *prop)
{
  struct _set_child_from_property_data data;

  data.node = node;
  data.prop = prop;

  g_hash_table_foreach (properties, set_child_from_property, &data);
}
