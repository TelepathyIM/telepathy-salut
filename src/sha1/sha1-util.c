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

#include <stdio.h>
#include <stdint.h>

#include "sha1/sha1-util.h"
#include "sha1/sha1.h"


gchar *
sha1_hex (const guint8 *bytes, guint len)
{
  SHA1Context sc;
  uint8_t hash[SHA1_HASH_SIZE];
  gchar *hex_hash = g_malloc (SHA1_HASH_SIZE*2 + 1);
  int i;

  SHA1Init (&sc);
  SHA1Update (&sc, bytes, len);
  SHA1Final (&sc, hash);

  for (i = 0; i < SHA1_HASH_SIZE; i++)
    {
      sprintf (hex_hash + 2 * i, "%02x", (unsigned int) hash[i]);
    }

  return hex_hash;
}
