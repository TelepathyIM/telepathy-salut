/*
 * salut-presence-cache.h - Headers for Salut's contact presence cache
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

#ifndef __SALUT_PRESENCE_CACHE_H__
#define __SALUT_PRESENCE_CACHE_H__

#include <glib.h>

G_BEGIN_DECLS

/* loop on CapabilityInfo::per_channel_manager_caps and call
 * salut_caps_channel_manager_free_capabilities */
void salut_presence_cache_free_cache_entry (
    GHashTable *per_channel_manager_caps);

/* loop on CapabilityInfo::per_channel_manager_caps and call
 * salut_caps_channel_manager_copy_capabilities */
void salut_presence_cache_copy_cache_entry (GHashTable **out,
    GHashTable *in);

/* loop on CapabilityInfo::per_channel_manager_caps and call
 * salut_caps_channel_manager_update_capabilities */
void salut_presence_cache_update_cache_entry (GHashTable *out,
    GHashTable *in);

G_END_DECLS

#endif /* __SALUT_PRESENCE_CACHE_H__ */

