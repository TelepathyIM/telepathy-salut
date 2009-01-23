/*
 * salut-caps-hash.c - Computing verification string hash (XEP-0115 v1.5)
 * Copyright (C) 2006-2008 Collabora Ltd.
 * Copyright (C) 2006-2008 Nokia Corporation
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

/* Computing verification string hash (XEP-0115 v1.5)
 *
 * Salut does not do anything with dataforms (XEP-0128) included in
 * capabilities.  However, it needs to parse them in order to compute the hash
 * according to XEP-0115.
 */

#include "config.h"

#include <string.h>

#define DEBUG_FLAG SALUT_DEBUG_PRESENCE

#include "debug.h"
#include "salut-capabilities.h"
#include "salut-caps-hash.h"
#include "salut-self.h"

typedef struct _DataFormField DataFormField;

struct _DataFormField {
  gchar *field_name;
  /* array of strings */
  GPtrArray *values;
};

typedef struct _DataForm DataForm;

struct _DataForm {
  gchar *form_type;
  /* array of DataFormField */
  GPtrArray *fields;
};

typedef struct _AllCapsData AllCapsData;

struct _AllCapsData {
  GPtrArray *features;
  GPtrArray *identities;
  GPtrArray *dataforms;
};

typedef struct _DataformParsingContext DataformParsingContext;

struct _DataformParsingContext {
    DataForm *form;
};

typedef struct _DataformFieldParsingContext DataformFieldParsingContext;

struct _DataformFieldParsingContext {
    DataformParsingContext *dataform_context;
    DataFormField *field;
};

static gint
char_cmp (gconstpointer a, gconstpointer b)
{
  gchar *left = *(gchar **) a;
  gchar *right = *(gchar **) b;

  return strcmp (left, right);
}

static gint
fields_cmp (gconstpointer a, gconstpointer b)
{
  DataFormField *left = *(DataFormField **) a;
  DataFormField *right = *(DataFormField **) b;

  return strcmp (left->field_name, right->field_name);
}

static gint
dataforms_cmp (gconstpointer a, gconstpointer b)
{
  DataForm *left = *(DataForm **) a;
  DataForm *right = *(DataForm **) b;

  return strcmp (left->form_type, right->form_type);
}

static void
_free_field (gpointer data, gpointer user_data)
{
  DataFormField *field = data;

  g_free (field->field_name);
  g_ptr_array_foreach (field->values, (GFunc) g_free, NULL);

  g_slice_free (DataFormField, field);
}

static void
_free_form (gpointer data, gpointer user_data)
{
  DataForm *form = data;

  g_free (form->form_type);

  g_ptr_array_foreach (form->fields, _free_field, NULL);

  g_slice_free (DataForm, form);
}

static void
salut_presence_free_xep0115_hash (
    GPtrArray *features,
    GPtrArray *identities,
    GPtrArray *dataforms)
{
  g_ptr_array_foreach (features, (GFunc) g_free, NULL);
  g_ptr_array_foreach (identities, (GFunc) g_free, NULL);
  g_ptr_array_foreach (dataforms, _free_form, NULL);

  g_ptr_array_free (features, TRUE);
  g_ptr_array_free (identities, TRUE);
  g_ptr_array_free (dataforms, TRUE);
}

static gchar *
caps_hash_compute (
    GPtrArray *features,
    GPtrArray *identities,
    GPtrArray *dataforms)
{
  GString *s;
  GChecksum *checksum;
  guchar *sha1;
  gsize out_len;
  guint i;
  gchar *encoded;

  out_len = g_checksum_type_get_length (G_CHECKSUM_SHA1);
  sha1 = g_malloc (out_len * sizeof (guchar));

  g_ptr_array_sort (identities, char_cmp);
  g_ptr_array_sort (features, char_cmp);
  g_ptr_array_sort (dataforms, dataforms_cmp);

  s = g_string_new ("");

  for (i = 0 ; i < identities->len ; i++)
    {
      g_string_append (s, g_ptr_array_index (identities, i));
      g_string_append_c (s, '<');
    }

  for (i = 0 ; i < features->len ; i++)
    {
      g_string_append (s, g_ptr_array_index (features, i));
      g_string_append_c (s, '<');
    }

  for (i = 0 ; i < dataforms->len ; i++)
    {
      guint j;
      DataForm *form = g_ptr_array_index (dataforms, i);

      g_assert (form->form_type != NULL);

      g_string_append (s, form->form_type);
      g_string_append_c (s, '<');

      g_ptr_array_sort (form->fields, fields_cmp);

      for (j = 0 ; j < form->fields->len ; j++)
        {
          guint k;
          DataFormField *field = g_ptr_array_index (form->fields, j);

          g_string_append (s, field->field_name);
          g_string_append_c (s, '<');

          g_ptr_array_sort (field->values, char_cmp);

          for (k = 0 ; k < field->values->len ; k++)
            {
              g_string_append (s, g_ptr_array_index (field->values, k));
              g_string_append_c (s, '<');
            }
        }
    }

  checksum = g_checksum_new (G_CHECKSUM_SHA1);
  g_checksum_update (checksum, (guchar *) s->str, s->len);
  g_checksum_get_digest (checksum, sha1, &out_len);
  g_string_free (s, TRUE);
  g_checksum_free (checksum);

  encoded = g_base64_encode (sha1, out_len);
  g_free (sha1);

  return encoded;
}

/**
 * parse FORM_TYPE values of a XEP-0128 dataform
 *
 * helper function for _parse_dataform_field
 */
static gboolean
_parse_dataform_field_form_type (GibberXmppNode *value_node, gpointer user_data)
{
  DataformFieldParsingContext *dataform_field_context =
    (DataformFieldParsingContext *) user_data;

  if (tp_strdiff (value_node->name, "value"))
    return TRUE;

  /* If the stanza is correctly formed, there is only one
   * FORM_TYPE and this check is useless. Otherwise, just
   * use the first one */
  if (dataform_field_context->dataform_context->form->form_type == NULL)
    dataform_field_context->dataform_context->form->form_type =
      g_strdup (value_node->content);

  return TRUE;
}


/**
 * parse values of a field of a XEP-0128 dataform
 *
 * helper function for _parse_caps_item
 */
static gboolean
_parse_dataform_field_values (GibberXmppNode *value_node, gpointer user_data)
{
  DataformFieldParsingContext *dataform_field_context =
    (DataformFieldParsingContext *) user_data;

  if (tp_strdiff (value_node->name, "value"))
    return TRUE;

  g_ptr_array_add (dataform_field_context->field->values,
      g_strdup (value_node->content));

  return TRUE;
}

/**
 * parse a field of a XEP-0128 dataform
 *
 * helper function for _parse_caps_item
 */
static gboolean
_parse_dataform_field (GibberXmppNode *field_node, gpointer user_data)
{
  DataformParsingContext *dataform_context =
    (DataformParsingContext *) user_data;
  const gchar *var;

  if (tp_strdiff (field_node->name, "field"))
    return TRUE;

  var = gibber_xmpp_node_get_attribute (field_node, "var");

  if (NULL == var)
    return TRUE;

  if (!tp_strdiff (var, "FORM_TYPE"))
    {
      DataformFieldParsingContext *dataform_field_context;
      dataform_field_context = g_slice_new0 (DataformFieldParsingContext);
      dataform_field_context->dataform_context = dataform_context;
      dataform_field_context->field = NULL;

      gibber_xmpp_node_each_child (field_node,
          _parse_dataform_field_form_type, dataform_field_context);

      g_slice_free (DataformFieldParsingContext, dataform_field_context);
    }
  else
    {
      DataformFieldParsingContext *dataform_field_context;
      DataFormField *field = NULL;

      field = g_slice_new0 (DataFormField);
      field->values = g_ptr_array_new ();
      field->field_name = g_strdup (var);

      dataform_field_context = g_slice_new0 (DataformFieldParsingContext);
      dataform_field_context->dataform_context = dataform_context;
      dataform_field_context->field = field;

      gibber_xmpp_node_each_child (field_node,
          _parse_dataform_field_values, dataform_field_context);

      g_slice_free (DataformFieldParsingContext, dataform_field_context);

      g_ptr_array_add (dataform_context->form->fields, (gpointer) field);
    }

  return TRUE;
}

/**
 * parse a XEP-0128 dataform
 *
 * helper function for _parse_caps_item
 */
static DataForm *
_parse_dataform (GibberXmppNode *node)
{
  DataForm *form;
  DataformParsingContext *dataform_context;

  form = g_slice_new0 (DataForm);
  form->form_type = NULL;
  form->fields = g_ptr_array_new ();

  dataform_context = g_slice_new0 (DataformParsingContext);
  dataform_context->form = form;

  gibber_xmpp_node_each_child (node, _parse_dataform_field, dataform_context);

  g_slice_free (DataformParsingContext, dataform_context);

  /* this should not happen if the stanza is correctly formed. */
  if (form->form_type == NULL)
    form->form_type = g_strdup ("");

  return form;
}

/**
 * parse a XML child node from from a received GibberXmppStanza
 *
 * helper function for caps_hash_compute_from_stanza
 */
static gboolean
_parse_caps_item (GibberXmppNode *node, gpointer user_data)
{
  AllCapsData *caps_data = (AllCapsData *) user_data;

  if (!tp_strdiff (node->name, "identity"))
    {
      const gchar *category;
      const gchar *name;
      const gchar *type;
      const gchar *xmllang;

      category = gibber_xmpp_node_get_attribute (node, "category");
      name = gibber_xmpp_node_get_attribute (node, "name");
      type = gibber_xmpp_node_get_attribute (node, "type");
      xmllang = gibber_xmpp_node_get_attribute (node, "xml:lang");

      if (NULL == category)
        return FALSE;
      if (NULL == name)
        name = "";
      if (NULL == type)
        type = "";
      if (NULL == xmllang)
        xmllang = "";

      g_ptr_array_add (caps_data->identities,
          g_strdup_printf ("%s/%s/%s/%s", category, type, xmllang, name));
    }
  else if (!tp_strdiff (node->name, "feature"))
    {
      const gchar *var;
      var = gibber_xmpp_node_get_attribute (node, "var");

      if (NULL == var)
        return FALSE;

      g_ptr_array_add (caps_data->features, g_strdup (var));
    }
  else if (!tp_strdiff (node->name, "x"))
    {
      const gchar *xmlns;
      const gchar *type;

      xmlns = gibber_xmpp_node_get_attribute (node, "xmlns");
      type = gibber_xmpp_node_get_attribute (node, "type");

      if (tp_strdiff (xmlns, "jabber:x:data"))
        return FALSE;

      if (tp_strdiff (type, "result"))
        return FALSE;

      g_ptr_array_add (caps_data->dataforms, (gpointer) _parse_dataform (node));
    }

  return TRUE;
}


/**
 * Compute the hash as defined by the XEP-0115 from a received
 * GibberXmppStanza
 *
 * Returns: the hash. The called must free the returned hash with g_free().
 */
gchar *
caps_hash_compute_from_stanza (GibberXmppNode *node)
{
  gchar *str;
  AllCapsData *caps_data;

  caps_data = g_slice_new0 (AllCapsData);
  caps_data->features = g_ptr_array_new ();
  caps_data->identities = g_ptr_array_new ();
  caps_data->dataforms = g_ptr_array_new ();

  gibber_xmpp_node_each_child (node, _parse_caps_item, caps_data);

  str = caps_hash_compute (caps_data->features, caps_data->identities,
      caps_data->dataforms);

  salut_presence_free_xep0115_hash (caps_data->features,
      caps_data->identities, caps_data->dataforms);
  g_slice_free (AllCapsData, caps_data);

  return str;
}

/**
 * Compute our hash as defined by the XEP-0115.
 *
 * Returns: the hash. The called must free the returned hash with g_free().
 */
gchar *
caps_hash_compute_from_self_presence (SalutSelf *self)
{
  GSList *features_list = salut_self_get_features (self);
  GPtrArray *features = g_ptr_array_new ();
  GPtrArray *identities = g_ptr_array_new ();
  GPtrArray *dataforms = g_ptr_array_new ();
  gchar *str;
  GSList *i;

  /* get our features list  */
  for (i = features_list; NULL != i; i = i->next)
    {
      const Feature *feat = (const Feature *) i->data;
      g_ptr_array_add (features, g_strdup (feat->ns));
    }

  /* XEP-0030 requires at least 1 identity. We don't need more. */
  g_ptr_array_add (identities, g_strdup ("client/pc//" PACKAGE_STRING));

  /* Salut does not use dataforms, let 'dataforms' be empty */

  str = caps_hash_compute (features, identities, dataforms);

  salut_presence_free_xep0115_hash (features, identities, dataforms);
  g_slist_free (features_list);

  return str;
}

