/*
 * check-xmpp-node-properties.c - Test for
 * salut_wocky_node_extract_properties and
 * salut_wocky_node_add_children_from_properties
 * Copyright (C) 2007 Collabora Ltd.
 * @author Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dbus/dbus-glib.h>

#include <wocky/wocky.h>
#include "util.h"

static WockyStanza *
create_sample_stanza (void)
{
  WockyStanza *stanza;

  stanza = wocky_stanza_build (
      WOCKY_STANZA_TYPE_MESSAGE, WOCKY_STANZA_SUB_TYPE_NONE,
      "alice@collabora.co.uk", "bob@collabora.co.uk",
      WOCKY_NODE_START, "properties",
        WOCKY_NODE_START, "prop",
          WOCKY_NODE_ATTRIBUTE, "name", "prop1",
          WOCKY_NODE_ATTRIBUTE, "type", "str",
          WOCKY_NODE_TEXT, "prop1_value",
        WOCKY_NODE_END,
        WOCKY_NODE_START, "prop",
          WOCKY_NODE_ATTRIBUTE, "name", "prop2",
          WOCKY_NODE_ATTRIBUTE, "type", "int",
          WOCKY_NODE_TEXT, "-7",
        WOCKY_NODE_END,
        WOCKY_NODE_START, "prop",
          WOCKY_NODE_ATTRIBUTE, "name", "prop3",
          WOCKY_NODE_ATTRIBUTE, "type", "uint",
          WOCKY_NODE_TEXT, "10",
        WOCKY_NODE_END,
        WOCKY_NODE_START, "prop",
          WOCKY_NODE_ATTRIBUTE, "name", "prop4",
          WOCKY_NODE_ATTRIBUTE, "type", "bytes",
          WOCKY_NODE_TEXT, "YWJjZGU=",
        WOCKY_NODE_END,
        WOCKY_NODE_START, "prop",
          WOCKY_NODE_ATTRIBUTE, "name", "prop5",
          WOCKY_NODE_ATTRIBUTE, "type", "bool",
          WOCKY_NODE_TEXT, "1",
        WOCKY_NODE_END,
      WOCKY_NODE_END,
     NULL);

  return stanza;
}

static void
test_extract_properties (void)
{
  WockyStanza *stanza;
  WockyNode *node;
  GHashTable *properties;
  GValue *value;
  const gchar *prop1_value;
  gint prop2_value;
  guint prop3_value;
  GArray *prop4_value;
  gboolean prop5_value;

  dbus_g_type_specialized_init ();

  stanza = create_sample_stanza ();
  node = wocky_node_get_child (wocky_stanza_get_top_node (stanza),
      "properties");

  g_assert (node != NULL);
  properties = salut_wocky_node_extract_properties (node, "prop");

  g_assert (properties != NULL);
  g_assert_cmpuint (g_hash_table_size (properties), ==, 5);

  /* prop1 */
  value = g_hash_table_lookup (properties, "prop1");
  g_assert (value != NULL);
  g_assert (G_VALUE_TYPE (value) == G_TYPE_STRING);
  prop1_value = g_value_get_string (value);
  g_assert (prop1_value != NULL);
  g_assert_cmpstr (prop1_value, ==, "prop1_value");

  /* prop2 */
  value = g_hash_table_lookup (properties, "prop2");
  g_assert (value != NULL);
  g_assert (G_VALUE_TYPE (value) == G_TYPE_INT);
  prop2_value = g_value_get_int (value);
  g_assert_cmpuint (prop2_value, ==, -7);

  /* prop3 */
  value = g_hash_table_lookup (properties, "prop3");
  g_assert (value != NULL);
  g_assert (G_VALUE_TYPE (value) == G_TYPE_UINT);
  prop3_value = g_value_get_uint (value);
  g_assert_cmpuint (prop3_value, ==, 10);

  /* prop4 */
  value = g_hash_table_lookup (properties, "prop4");
  g_assert (value != NULL);
  g_assert (G_VALUE_TYPE (value) == DBUS_TYPE_G_UCHAR_ARRAY);
  prop4_value = g_value_get_boxed (value);
  g_assert (g_array_index (prop4_value, gchar, 0) == 'a');
  g_assert (g_array_index (prop4_value, gchar, 1) == 'b');
  g_assert (g_array_index (prop4_value, gchar, 2) == 'c');
  g_assert (g_array_index (prop4_value, gchar, 3) == 'd');
  g_assert (g_array_index (prop4_value, gchar, 4) == 'e');

  /* prop 5 */
  value = g_hash_table_lookup (properties, "prop5");
  g_assert (value != NULL);
  g_assert (G_VALUE_TYPE (value) == G_TYPE_BOOLEAN);
  prop5_value = g_value_get_boolean (value);
  g_assert (prop5_value == TRUE);

  g_object_unref (stanza);
  g_hash_table_unref (properties);
}

static void
test_g_value_slice_free (GValue *value)
{
  g_value_unset (value);
  g_slice_free (GValue, value);
}

static GHashTable *
create_sample_properties (void)
{
  GHashTable *properties;
  GValue *prop1, *prop2, *prop3, *prop4, *prop5;
  GArray *arr;

  dbus_g_type_specialized_init ();

  properties = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) test_g_value_slice_free);

  prop1 = g_slice_new0 (GValue);
  g_value_init (prop1, G_TYPE_STRING);
  g_value_set_string (prop1, "prop1_value");
  g_hash_table_insert (properties, "prop1", prop1);

  prop2 = g_slice_new0 (GValue);
  g_value_init (prop2, G_TYPE_INT);
  g_value_set_int (prop2, -7);
  g_hash_table_insert (properties, "prop2", prop2);

  prop3 = g_slice_new0 (GValue);
  g_value_init (prop3, G_TYPE_UINT);
  g_value_set_uint (prop3, 10);
  g_hash_table_insert (properties, "prop3", prop3);

  prop4 = g_slice_new0 (GValue);
  g_value_init (prop4, DBUS_TYPE_G_UCHAR_ARRAY);
  arr = g_array_new (FALSE, FALSE, sizeof (guchar));
  g_array_append_vals (arr, "abcde", 5);
  g_value_take_boxed (prop4, arr);
  g_hash_table_insert (properties, "prop4", prop4);

  prop5 = g_slice_new0 (GValue);
  g_value_init (prop5, G_TYPE_BOOLEAN);
  g_value_set_boolean (prop5, TRUE);
  g_hash_table_insert (properties, "prop5", prop5);

  return properties;
}

static void
test_add_children_from_properties (void)
{
  GHashTable *properties;
  WockyStanza *stanza;
  WockyNode *top_node;
  GSList *l;

  properties = create_sample_properties ();
  stanza = wocky_stanza_new ("properties",
      "http://example.com/stoats");
  top_node = wocky_stanza_get_top_node (stanza);

  salut_wocky_node_add_children_from_properties (top_node,
      properties, "prop");

  g_assert_cmpuint (g_slist_length (top_node->children), ==, 5);
  for (l = top_node->children; l != NULL; l = l->next)
    {
      WockyNode *node = (WockyNode *) l->data;
      const gchar *name, *type;

      name = wocky_node_get_attribute (node, "name");
      type = wocky_node_get_attribute (node, "type");

      if (strcmp (name, "prop1") == 0)
        {
          g_assert_cmpstr (type, ==, "str");
          g_assert_cmpstr (node->content, ==, "prop1_value");
        }
      else if (strcmp (name, "prop2") == 0)
        {
          g_assert_cmpstr (type, ==, "int");
          g_assert_cmpstr (node->content, ==, "-7");
        }
      else if (strcmp (name, "prop3") == 0)
        {
          g_assert_cmpstr (type, ==, "uint");
          g_assert_cmpstr (node->content, ==, "10");
        }
      else if (strcmp (name, "prop4") == 0)
        {
          g_assert_cmpstr (type, ==, "bytes");
          g_assert_cmpstr (node->content, ==, "YWJjZGU=");
        }
      else if (strcmp (name, "prop5") == 0)
        {
          g_assert_cmpstr (type, ==, "bool");
          g_assert_cmpstr (node->content, ==, "1");
        }
      else
        g_assert_not_reached ();
    }

  g_hash_table_unref (properties);
  g_object_unref (stanza);
}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_type_init ();

  g_test_add_func ("/node-properties/extract-properties",
      test_extract_properties);
  g_test_add_func ("/node-properties/add-children-from-properties",
      test_add_children_from_properties);

  return g_test_run ();
}
