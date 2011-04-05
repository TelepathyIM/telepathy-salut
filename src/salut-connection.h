/*
 * salut-connection.h - Header for SalutConnection
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

#ifndef __SALUT_CONNECTION_H__
#define __SALUT_CONNECTION_H__

#include "config.h"

#include <glib-object.h>
#include <dbus/dbus-glib.h>

#include <telepathy-glib/enums.h>
#include <telepathy-glib/base-connection.h>
#include <telepathy-glib/contacts-mixin.h>
#include <telepathy-glib/dbus-properties-mixin.h>
#include <telepathy-glib/presence-mixin.h>
#include <telepathy-glib/svc-connection.h>

#include <wocky/wocky-stanza.h>
#include <wocky/wocky-session.h>

#include "salut/connection.h"

G_BEGIN_DECLS

typedef struct _SalutPresenceCache SalutPresenceCache;
typedef struct _SalutDisco SalutDisco;

typedef struct _SalutConnectionPrivate SalutConnectionPrivate;

struct _SalutConnectionClass {
  TpBaseConnectionClass parent_class;
  TpDBusPropertiesMixinClass properties_mixin;
  TpPresenceMixinClass presence_mixin;
  TpContactsMixinClass contacts_mixin;
};

struct _SalutConnection {
  TpBaseConnection parent;
  TpPresenceMixin presence_mixin;
  TpContactsMixin contacts_mixin;

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

G_END_DECLS

#endif /* #ifndef __SALUT_CONNECTION_H__*/
