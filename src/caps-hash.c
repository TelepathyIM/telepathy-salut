/*
 * caps-hash.c - Computing verification string hash (XEP-0115 v1.5)
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
#include "caps-hash.h"

#include <string.h>

#define DEBUG_FLAG SALUT_DEBUG_PRESENCE

#include "debug.h"
#include "capabilities.h"
#include "self.h"

static void
add_to_pointer_array_foreach (gpointer ns,
    gpointer arr)
{
  g_ptr_array_add (arr, g_strdup (ns));
}

/**
 * Compute our hash as defined by the XEP-0115.
 *
 * Returns: the hash. The called must free the returned hash with g_free().
 */
gchar *
caps_hash_compute_from_self_presence (SalutSelf *self)
{
  const GabbleCapabilitySet *caps = salut_self_get_caps (self);
  GPtrArray *features = g_ptr_array_new ();
  GPtrArray *identities = wocky_disco_identity_array_new ();
  const GPtrArray *dataforms =
    wocky_xep_0115_capabilities_get_data_forms (WOCKY_XEP_0115_CAPABILITIES (self));
  gchar *str;

  gabble_capability_set_foreach (caps, add_to_pointer_array_foreach, features);

  /* XEP-0030 requires at least 1 identity. We don't need more. */
  g_ptr_array_add (identities,
      wocky_disco_identity_new ("client", "pc",
          NULL, PACKAGE_STRING));

  str = wocky_caps_hash_compute_from_lists (features, identities,
      (GPtrArray *) dataforms);

  g_ptr_array_unref (features);
  wocky_disco_identity_array_free (identities);

  return str;
}

