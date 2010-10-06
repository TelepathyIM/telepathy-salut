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
#include <glib-object.h>

#include "capabilities.h"
#include "salut-connection.h"
#include "salut-contact.h"
#include "salut-xmpp-connection-manager.h"

G_BEGIN_DECLS

#define SALUT_TYPE_PRESENCE_CACHE salut_presence_cache_get_type ()

#define SALUT_PRESENCE_CACHE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  SALUT_TYPE_PRESENCE_CACHE, SalutPresenceCache))

#define SALUT_PRESENCE_CACHE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  SALUT_TYPE_PRESENCE_CACHE, SalutPresenceCacheClass))

#define SALUT_IS_PRESENCE_CACHE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  SALUT_TYPE_PRESENCE_CACHE))

#define SALUT_IS_PRESENCE_CACHE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  SALUT_TYPE_PRESENCE_CACHE))

#define SALUT_PRESENCE_CACHE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  SALUT_TYPE_PRESENCE_CACHE, SalutPresenceCacheClass))


typedef struct _SalutPresenceCachePrivate SalutPresenceCachePrivate;

struct _SalutPresenceCache {
    GObject parent;
    SalutPresenceCachePrivate *priv;
};

typedef struct _SalutPresenceCacheClass SalutPresenceCacheClass;

struct _SalutPresenceCacheClass {
    GObjectClass parent_class;
};

GType salut_presence_cache_get_type (void);

SalutPresenceCache *salut_presence_cache_new (SalutConnection *connection);

void salut_presence_cache_process_caps (SalutPresenceCache *self,
    SalutContact *contact, const gchar *hash, const gchar *node,
    const gchar *ver);


/* loop on CapabilityInfo::per_channel_manager_caps and call
 * salut_caps_channel_manager_free_capabilities */
void salut_presence_cache_free_cache_entry (
    GHashTable *per_channel_manager_caps);

/* loop on CapabilityInfo::per_channel_manager_caps and call
 * salut_caps_channel_manager_copy_capabilities */
G_GNUC_WARN_UNUSED_RESULT GHashTable * salut_presence_cache_copy_cache_entry (
    GHashTable *in);

/* loop on CapabilityInfo::per_channel_manager_caps and call
 * salut_caps_channel_manager_update_capabilities */
void salut_presence_cache_update_cache_entry (GHashTable *out,
    GHashTable *in);

/* Salut-specific pseudo-capability: llXMPP clients without XEP-0115 caps */
#define QUIRK_NOT_XEP_CAPABILITIES QUIRK_PREFIX "not-xep-capabilities"

G_END_DECLS

#endif /* __SALUT_PRESENCE_CACHE_H__ */

