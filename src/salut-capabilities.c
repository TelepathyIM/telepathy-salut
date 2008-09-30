/*
 * capabilities.c - Connection.Interface.Capabilities constants and utilities
 * Copyright (C) 2005-2008 Collabora Ltd.
 * Copyright (C) 2005-2008 Nokia Corporation
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
#include "salut-capabilities.h"

#include <gibber/gibber-namespaces.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-manager.h>

#include "salut-caps-channel-manager.h"

static const Feature self_advertised_features[] =
{
  { FEATURE_FIXED, GIBBER_XMPP_NS_SI},
  { FEATURE_FIXED, GIBBER_XMPP_NS_IBB},
  { FEATURE_FIXED, GIBBER_TELEPATHY_NS_TUBES},

  { 0, NULL}
};

GSList *
capabilities_get_features (GHashTable *per_channel_manager_caps)
{
  GHashTableIter channel_manager_iter;
  SalutCapsChannelManager *manager;
  gpointer cap;
  GSList *features = NULL;

  if (per_channel_manager_caps != NULL)
    {
      g_hash_table_iter_init (&channel_manager_iter, per_channel_manager_caps);
      while (g_hash_table_iter_next (&channel_manager_iter,
                 (gpointer *) &manager, &cap))
        {
          salut_caps_channel_manager_get_feature_list (manager, cap,
              &features);
        }
    }

  return features;
}

