/*
 * check-xmpp-node-properties.c - Test for
 * salut_gibber_xmpp_node_extract_properties and
 * salut_gibber_xmpp_node_add_children_from_properties
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dbus/dbus-glib.h>

#include <wocky/wocky-stanza.h>
#include "salut-util.h"

#include <check.h>
#include "check-helpers.h"
#include "check-salut.h"

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

START_TEST (test_extract_properties)
{
  WockyStanza *stanza;
  GibberXmppNode *node;
  GHashTable *properties;
  GValue *value;
  const gchar *prop1_value;
  gint prop2_value;
  guint prop3_value;
  GArray *prop4_value;
  gboolean prop5_value;

  stanza = create_sample_stanza ();
  node = gibber_xmpp_node_get_child (wocky_stanza_get_top_node (stanza),
      "properties");

  fail_unless (node != NULL);
  properties = salut_gibber_xmpp_node_extract_properties (node, "prop");

  fail_unless (properties != NULL);
  fail_unless (g_hash_table_size (properties) == 5);

  /* prop1 */
  value = g_hash_table_lookup (properties, "prop1");
  fail_unless (value != NULL);
  fail_unless (G_VALUE_TYPE (value) == G_TYPE_STRING);
  prop1_value = g_value_get_string (value);
  fail_unless (prop1_value != NULL);
  fail_unless (strcmp (prop1_value, "prop1_value") == 0);

  /* prop2 */
  value = g_hash_table_lookup (properties, "prop2");
  fail_unless (value != NULL);
  fail_unless (G_VALUE_TYPE (value) == G_TYPE_INT);
  prop2_value = g_value_get_int (value);
  fail_unless (prop2_value == -7);

  /* prop3 */
  value = g_hash_table_lookup (properties, "prop3");
  fail_unless (value != NULL);
  fail_unless (G_VALUE_TYPE (value) == G_TYPE_UINT);
  prop3_value = g_value_get_uint (value);
  fail_unless (prop3_value == 10);

  /* prop4 */
  value = g_hash_table_lookup (properties, "prop4");
  fail_unless (value != NULL);
  fail_unless (G_VALUE_TYPE (value) == DBUS_TYPE_G_UCHAR_ARRAY);
  prop4_value = g_value_get_boxed (value);
  fail_unless (g_array_index (prop4_value, gchar, 0) == 'a');
  fail_unless (g_array_index (prop4_value, gchar, 1) == 'b');
  fail_unless (g_array_index (prop4_value, gchar, 2) == 'c');
  fail_unless (g_array_index (prop4_value, gchar, 3) == 'd');
  fail_unless (g_array_index (prop4_value, gchar, 4) == 'e');

  /* prop 5 */
  value = g_hash_table_lookup (properties, "prop5");
  fail_unless (value != NULL);
  fail_unless (G_VALUE_TYPE (value) == G_TYPE_BOOLEAN);
  prop5_value = g_value_get_boolean (value);
  fail_unless (prop5_value == TRUE);

  g_object_unref (stanza);
  g_hash_table_destroy (properties);
}
END_TEST

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


START_TEST (test_add_children_from_properties)
{
  GHashTable *properties;
  WockyStanza *stanza;
  WockyNode *top_node;
  GSList *l;

  properties = create_sample_properties ();
  stanza = wocky_stanza_new ("properties",
      "http://example.com/stoats");
  top_node = wocky_stanza_get_top_node (stanza);

  salut_gibber_xmpp_node_add_children_from_properties (top_node,
      properties, "prop");

  fail_unless (g_slist_length (top_node->children) == 5);
  for (l = top_node->children; l != NULL; l = l->next)
    {
      GibberXmppNode *node = (GibberXmppNode *) l->data;
      const gchar *name, *type;

      name = gibber_xmpp_node_get_attribute (node, "name");
      type = gibber_xmpp_node_get_attribute (node, "type");

      if (strcmp (name, "prop1") == 0)
        {
          fail_unless (strcmp (type, "str") == 0);
          fail_unless (strcmp (node->content, "prop1_value") == 0);
        }
      else if (strcmp (name, "prop2") == 0)
        {
          fail_unless (strcmp (type, "int") == 0);
          fail_unless (strcmp (node->content, "-7") == 0);
        }
      else if (strcmp (name, "prop3") == 0)
        {
          fail_unless (strcmp (type, "uint") == 0);
          fail_unless (strcmp (node->content, "10") == 0);
        }
      else if (strcmp (name, "prop4") == 0)
        {
          fail_unless (strcmp (type, "bytes") == 0);
          fail_unless (strcmp (node->content, "YWJjZGU=") == 0);
        }
      else if (strcmp (name, "prop5") == 0)
        {
          fail_unless (strcmp (type, "bool") == 0);
          fail_unless (strcmp (node->content, "1") == 0);
        }
      else
        g_assert_not_reached ();
    }

  g_hash_table_destroy (properties);
  g_object_unref (stanza);
}
END_TEST

TCase *
make_salut_gibber_xmpp_node_properties_tcase (void)
{
  TCase *tc = tcase_create ("XMPP Node");

  /* to initiate D-Bus types */
  dbus_g_bus_get (DBUS_BUS_STARTER, NULL);

  tcase_add_test (tc, test_extract_properties);
  tcase_add_test (tc, test_add_children_from_properties);
  return tc;
}
