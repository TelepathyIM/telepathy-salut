/*
 * salut-util.c - Code for Salut utility functions
 * Copyright (C) 2006-2007 Collabora Ltd.
 * Copyright (C) 2006-2007 Nokia Corporation
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
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <telepathy-glib/util.h>

#include "salut-contact.h"

#ifdef HAVE_UUID
# include <uuid.h>
#endif

struct _xmpp_node_extract_property_data
{
  const gchar *prop;
  GHashTable *properties;
};

static gboolean
xmpp_node_extract_property (WockyNode *node,
                            gpointer user_data)
{
  struct _xmpp_node_extract_property_data *data =
    (struct _xmpp_node_extract_property_data *) user_data;
  const gchar *name;
  const gchar *type;
  const gchar *value;
  GValue *gvalue;

  if (tp_strdiff (node->name, data->prop))
    return TRUE;

  name = wocky_node_get_attribute (node, "name");

  if (!name)
    return TRUE;

  type = wocky_node_get_attribute (node, "type");
  value = node->content;

  if (type == NULL || value == NULL)
    return TRUE;

  if (!tp_strdiff (type, "bytes"))
    {
      GArray *arr;
      guchar *decoded;
      gsize len;

      decoded = g_base64_decode (value, &len);
      if (!decoded)
        return TRUE;

      arr = g_array_new (FALSE, FALSE, sizeof (guchar));
      g_array_append_vals (arr, decoded, len);
      gvalue = tp_g_value_slice_new (DBUS_TYPE_G_UCHAR_ARRAY);
      g_value_take_boxed (gvalue, arr);
      g_hash_table_insert (data->properties, g_strdup (name), gvalue);
      g_free (decoded);
    }
  else if (!tp_strdiff (type, "str"))
    {
      gvalue = tp_g_value_slice_new (G_TYPE_STRING);
      g_value_set_string (gvalue, value);
      g_hash_table_insert (data->properties, g_strdup (name), gvalue);
    }
  else if (!tp_strdiff (type, "int"))
    {
      gvalue = tp_g_value_slice_new (G_TYPE_INT);
      g_value_set_int (gvalue, strtol (value, NULL, 10));
      g_hash_table_insert (data->properties, g_strdup (name), gvalue);
    }
  else if (!tp_strdiff (type, "uint"))
    {
      gvalue = tp_g_value_slice_new (G_TYPE_UINT);
      g_value_set_uint (gvalue, strtoul (value, NULL, 10));
      g_hash_table_insert (data->properties, g_strdup (name), gvalue);
    }
  else if (!tp_strdiff (type, "bool"))
    {
      gboolean val;

      if (!tp_strdiff (value, "0"))
        {
          val = FALSE;
        }
      else if (!tp_strdiff (value, "1"))
        {
          val = TRUE;
        }
      else
        {
          g_debug ("invalid boolean value: %s", value);
          return TRUE;
        }

      gvalue = tp_g_value_slice_new (G_TYPE_BOOLEAN);
      g_value_set_boolean (gvalue, val);
      g_hash_table_insert (data->properties, g_strdup (name), gvalue);
    }

  return TRUE;
}

/**
 * salut_wocky_node_extract_properties
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
 * salut_wocky_node_extract_properties (node, "prop");
 *
 * --> { "prop1" : "prop1_value", "prop2" : 7 }
 *
 * Returns a hash table mapping names to GValue of the specified type.
 * Valid types are: str, int, uint, bytes, b.
 *
 */
GHashTable *
salut_wocky_node_extract_properties (WockyNode *node,
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

  wocky_node_each_child (node, xmpp_node_extract_property, &data);

  return properties;
}

struct _set_child_from_property_data
{
  WockyNode *node;
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
  WockyNode *child;
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
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_BOOLEAN)
    {
      type = "bool";
    }
  else
    {
      /* a type we don't know how to handle: ignore it */
      g_critical ("property with unknown type \"%s\"",
          g_type_name (G_VALUE_TYPE (gvalue)));
      return;
    }

  child = wocky_node_add_child (data->node, data->prop);

  if (G_VALUE_TYPE (gvalue) == G_TYPE_STRING)
    {
      wocky_node_set_content (child,
        g_value_get_string (gvalue));
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_INT)
    {
      gchar *str;

      str = g_strdup_printf ("%d", g_value_get_int (gvalue));
      wocky_node_set_content (child, str);

      g_free (str);
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_UINT)
    {
      gchar *str;

      str = g_strdup_printf ("%u", g_value_get_uint (gvalue));
      wocky_node_set_content (child, str);

      g_free (str);
    }
  else if (G_VALUE_TYPE (gvalue) == DBUS_TYPE_G_UCHAR_ARRAY)
    {
      GArray *arr;
      gchar *str;

      type = "bytes";
      arr = g_value_get_boxed (gvalue);
      str = g_base64_encode ((const guchar *) arr->data, arr->len);
      wocky_node_set_content (child, str);

      g_free (str);
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_BOOLEAN)
    {
      gboolean val;

      val = g_value_get_boolean (gvalue);
      if (val)
        wocky_node_set_content (child, "1");
      else
        wocky_node_set_content (child, "0");
    }
  else
    {
      g_assert_not_reached ();
    }

  wocky_node_set_attribute (child, "name", key);
  wocky_node_set_attribute (child, "type", type);
}

/**
 *
 * wocky_node_set_children_from_properties
 *
 * Map a properties hash table to a XML node.
 *
 * Example:
 *
 * properties = { "prop1" : "prop1_value", "prop2" : 7 }
 *
 * salut_wocky_node_add_children_from_properties (node, properties,
 *     "prop");
 *
 * --> <node>
 *       <prop name="prop1" type="str">prop1_value</prop>
 *       <prop name="prop2" type="uint">7</prop>
 *     </node>
 *
 */
void
salut_wocky_node_add_children_from_properties (WockyNode *node,
                                                     GHashTable *properties,
                                                     const gchar *prop)
{
  struct _set_child_from_property_data data;

  data.node = node;
  data.prop = prop;

  g_hash_table_foreach (properties, set_child_from_property, &data);
}

gchar *
salut_generate_id (void)
{
#ifdef HAVE_UUID
  /* generate random UUIDs */
  uuid_t uu;
  gchar *str;

  str = g_new0 (gchar, 37);
  uuid_generate_random (uu);
  uuid_unparse_lower (uu, str);
  return str;
#else
  /* generate from the time, a counter, and a random integer */
  static gulong last = 0;
  GTimeVal tv;

  g_get_current_time (&tv);
  return g_strdup_printf ("%lx.%lx/%lx/%x", tv.tv_sec, tv.tv_usec,
      last++, g_random_int ());
#endif
}

static void
send_stanza_to_contact (WockyPorter *porter,
    WockyContact *contact,
    WockyStanza *stanza)
{
  WockyStanza *to_send = wocky_stanza_copy (stanza);

  wocky_stanza_set_to_contact (to_send, contact);
  wocky_porter_send (porter, to_send);
  g_object_unref (to_send);
}

void
salut_send_ll_pep_event (WockySession *session,
    WockyStanza *stanza)
{
  WockyContactFactory *contact_factory;
  WockyPorter *porter;
  WockyLLContact *self_contact;
  GList *contacts, *l;
  WockyNode *message, *event, *items;
  gchar *node;

  g_return_if_fail (WOCKY_IS_SESSION (session));
  g_return_if_fail (WOCKY_IS_STANZA (stanza));

  message = wocky_stanza_get_top_node (stanza);
  event = wocky_node_get_first_child (message);
  items = wocky_node_get_first_child (event);

  if (wocky_node_get_ns (items) == NULL)
    return;

  node = g_strdup_printf ("%s+notify", wocky_node_get_ns (items));

  contact_factory = wocky_session_get_contact_factory (session);
  porter = wocky_session_get_porter (session);

  contacts = wocky_contact_factory_get_ll_contacts (contact_factory);

  for (l = contacts; l != NULL; l = l->next)
    {
      SalutContact *contact = l->data;

      if (gabble_capability_set_has (contact->caps, node))
        send_stanza_to_contact (porter, WOCKY_CONTACT (contact), stanza);
    }

  /* now send to self */
  self_contact = wocky_contact_factory_ensure_ll_contact (contact_factory,
      wocky_porter_get_full_jid (porter));

  send_stanza_to_contact (porter, WOCKY_CONTACT (self_contact), stanza);

  g_object_unref (self_contact);
  g_list_free (contacts);
  g_free (node);
}
