/*
 * sha1-util.c - sha1-utils
 * Copyright (C) 2006-2007 Collabora Ltd.
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

#include "sha1/sha1-util.h"

#include <stdio.h>
#include <stdint.h>


gchar *
sha1_hex (const guint8 *bytes,
          guint len)
{
  gchar *hex = g_compute_checksum_for_data (G_CHECKSUM_SHA1, bytes, len);
  guint i;

  for (i = 0; i < SHA1_HASH_SIZE * 2; i++)
    {
      g_assert (hex[i] != '\0');
      hex[i] = g_ascii_tolower (hex[i]);
    }

  g_assert (hex[SHA1_HASH_SIZE * 2] == '\0');

  return hex;
}

void
sha1_bin (const gchar *bytes,
          guint len,
          guchar out[SHA1_HASH_SIZE])
{
  GChecksum *checksum = g_checksum_new (G_CHECKSUM_SHA1);
  gsize out_len = SHA1_HASH_SIZE;

  g_assert (g_checksum_type_get_length (G_CHECKSUM_SHA1) == SHA1_HASH_SIZE);
  g_checksum_update (checksum, (const guchar *) bytes, len);
  g_checksum_get_digest (checksum, out, &out_len);
  g_assert (out_len == SHA1_HASH_SIZE);
  g_checksum_free (checksum);
}
