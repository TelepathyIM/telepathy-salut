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

#include <wocky/wocky-disco-identity.h>
#include <wocky/wocky-caps-hash.h>

#define DEBUG_FLAG SALUT_DEBUG_PRESENCE

#include "debug.h"
#include "salut-capabilities.h"
#include "salut-caps-hash.h"
#include "salut-self.h"

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
  GPtrArray *identities = wocky_disco_identity_array_new ();
  gchar *str;
  GSList *i;

  /* get our features list  */
  for (i = features_list; NULL != i; i = i->next)
    g_ptr_array_add (features, ((Feature *) i->data)->ns);

  /* XEP-0030 requires at least 1 identity. We don't need more. */
  g_ptr_array_add (identities,
      wocky_disco_identity_new ("client", "pc",
          NULL, PACKAGE_STRING));

  str = wocky_caps_hash_compute_from_lists (features, identities, NULL);

  g_slist_free (features_list);
  g_ptr_array_free (features, TRUE);
  wocky_disco_identity_array_free (identities);

  return str;
}

