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

static gint
char_cmp (gconstpointer a, gconstpointer b)
{
  gchar *left = *(gchar **) a;
  gchar *right = *(gchar **) b;

  return strcmp (left, right);
}

static void
salut_presence_free_xep0115_hash (
    GPtrArray *features,
    GPtrArray *identities)
{
  g_ptr_array_foreach (features, (GFunc) g_free, NULL);
  g_ptr_array_foreach (identities, (GFunc) g_free, NULL);

  g_ptr_array_free (features, TRUE);
  g_ptr_array_free (identities, TRUE);
}

static gchar *
caps_hash_compute (
    GPtrArray *features,
    GPtrArray *identities)
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

  str = caps_hash_compute (features, identities);

  salut_presence_free_xep0115_hash (features, identities);
  g_slist_free (features_list);

  return str;
}

