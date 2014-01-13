/*
 * connection.h - Header for SalutConnection
 * Copyright © 2005-2012 Collabora Ltd.
 * Copyright © 2005-2010 Nokia Corporation
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

#ifndef __SALUT_CONNECTION_H__
#define __SALUT_CONNECTION_H__

#include "config.h"

#include <glib-object.h>
#include <dbus/dbus-glib.h>

#include <telepathy-glib/telepathy-glib.h>

#include <wocky/wocky.h>

#include "salut/plugin-connection.h"

G_BEGIN_DECLS

#define SALUT_TYPE_CONNECTION (salut_connection_get_type ())
#define SALUT_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_CONNECTION, SalutConnection))
#define SALUT_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_CONNECTION, \
      SalutConnectionClass))
#define SALUT_IS_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_CONNECTION))
#define SALUT_IS_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_CONNECTION))
#define SALUT_CONNECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_CONNECTION, \
      SalutConnectionClass))

typedef struct _SalutConnection SalutConnection;
typedef struct _SalutConnectionClass SalutConnectionClass;

GType salut_connection_get_type (void);

typedef struct _SalutPresenceCache SalutPresenceCache;
typedef struct _SalutDisco SalutDisco;

typedef struct _SalutConnectionPrivate SalutConnectionPrivate;

struct _SalutConnectionClass {
  TpBaseConnectionClass parent_class;
  TpDBusPropertiesMixinClass properties_mixin;
  TpPresenceMixinClass presence_mixin;
};

struct _SalutConnection {
  TpBaseConnection parent;
  TpPresenceMixin presence_mixin;

  SalutPresenceCache *presence_cache;
  SalutDisco *disco;

  WockySession *session;
  WockyPorter *porter;

  /* Our name on the network */
  gchar *name;

  SalutConnectionPrivate *priv;
};

typedef enum {
  LIST_HANDLE_PUBLISH = 1,
  LIST_HANDLE_SUBSCRIBE,
  LIST_HANDLE_KNOWN,

  LIST_HANDLE_FIRST = LIST_HANDLE_PUBLISH,
  LIST_HANDLE_LAST = LIST_HANDLE_KNOWN
} SalutConnectionListHandle;

#ifdef ENABLE_OLPC
void
salut_connection_olpc_observe_invitation (SalutConnection *connection,
    TpHandle room, TpHandle invitor_handle, WockyNode *invite_node);

gboolean
salut_connection_olpc_observe_muc_stanza (SalutConnection *self, TpHandle room,
    TpHandle sender, WockyStanza *stanza);
#endif

const gchar * const *salut_connection_get_implemented_interfaces (void);

gchar *salut_normalize_non_empty (const gchar *id, GError **error);

WockySession * salut_connection_get_session (SalutPluginConnection *connection);

const gchar * salut_connection_get_name (SalutPluginConnection *connection);

void salut_connection_dup_avatar_requirements (GStrv *supported_mime_types,
    guint *min_height,
    guint *min_width,
    guint *rec_height,
    guint *rec_width,
    guint *max_height,
    guint *max_width,
    guint *max_bytes);

const TpPresenceStatusSpec * salut_connection_get_presence_statuses (void);

G_END_DECLS

#endif /* #ifndef __SALUT_CONNECTION_H__*/
