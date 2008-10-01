/*
 * salut-presence-cache.c - Salut's contact presence cache
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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
#include "salut-presence-cache.h"

#include <stdlib.h>
#include <string.h>
#include <glib.h>

#define DEBUG_FLAG SALUT_DEBUG_PRESENCE

#include <gibber/gibber-namespaces.h>
#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/intset.h>

#define DEBUG_FLAG SALUT_DEBUG_PRESENCE

#include "salut-caps-channel-manager.h"
#include "salut-caps-hash.h"
#include "debug.h"

static void
free_caps_helper (gpointer key, gpointer value, gpointer user_data)
{
  SalutCapsChannelManager *manager = SALUT_CAPS_CHANNEL_MANAGER (key);
  salut_caps_channel_manager_free_capabilities (manager, value);
}

void
salut_presence_cache_free_cache_entry (
    GHashTable *per_channel_manager_caps)
{
  if (per_channel_manager_caps == NULL)
    return;

  g_hash_table_foreach (per_channel_manager_caps, free_caps_helper,
      NULL);
  g_hash_table_destroy (per_channel_manager_caps);
}

static void
copy_caps_helper (gpointer key, gpointer value, gpointer user_data)
{
  GHashTable *table_out = user_data;
  SalutCapsChannelManager *manager = SALUT_CAPS_CHANNEL_MANAGER (key);
  gpointer out;
  salut_caps_channel_manager_copy_capabilities (manager, &out, value);
  g_hash_table_insert (table_out, key, out);
}

void
salut_presence_cache_copy_cache_entry (
    GHashTable **out, GHashTable *in)
{
  *out = g_hash_table_new (NULL, NULL);
  if (in != NULL)
    g_hash_table_foreach (in, copy_caps_helper,
        *out);
}

static void
update_caps_helper (gpointer key, gpointer value, gpointer user_data)
{
  GHashTable *table_out = user_data;
  SalutCapsChannelManager *manager = SALUT_CAPS_CHANNEL_MANAGER (key);
  gpointer out;

  out = g_hash_table_lookup (table_out, key);
  if (out == NULL)
    {
      salut_caps_channel_manager_copy_capabilities (manager, &out, value);
      g_hash_table_insert (table_out, key, out);
    }
  else
    {
      salut_caps_channel_manager_update_capabilities (manager, out, value);
    }
}

void
salut_presence_cache_update_cache_entry (
    GHashTable *out, GHashTable *in)
{
  g_hash_table_foreach (in, update_caps_helper,
      out);
}

