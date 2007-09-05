/*
 * salut-self.h - Header for SalutSelf
 * Copyright (C) 2005 Collabora Ltd.
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

#ifndef __SALUT_SELF_H__
#define __SALUT_SELF_H__

#include "config.h"

#include <glib-object.h>

#include <telepathy-glib/handle-repo.h>

#include <gibber/gibber-xmpp-stanza.h>

#include "salut-connection.h"
#include "salut-avahi-client.h"
#include "salut-presence.h"

G_BEGIN_DECLS

typedef struct _SalutSelf SalutSelf;
typedef struct _SalutSelfClass SalutSelfClass;

struct _SalutSelfClass {
    GObjectClass parent_class;
};

struct _SalutSelf {
    GObject parent;
    gchar *name;
    SalutPresenceId status;
    gchar *status_message;
    gchar *avatar_token;
    guint8 *avatar;
    gsize avatar_size;
    gchar *jid;
#ifdef ENABLE_OLPC
    GArray *olpc_key;
    gchar *olpc_cur_act;
    TpHandle olpc_cur_act_room;
    gchar *olpc_color;
#endif
};

GType salut_self_get_type(void);

/* TYPE MACROS */
#define SALUT_TYPE_SELF \
  (salut_self_get_type())
#define SALUT_SELF(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_SELF, SalutSelf))
#define SALUT_SELF_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_SELF, SalutSelfClass))
#define SALUT_IS_SELF(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_SELF))
#define SALUT_IS_SELF_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_SELF))
#define SALUT_SELF_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_SELF, SalutSelfClass))

SalutSelf *salut_self_new (SalutConnection *conn, SalutAvahiClient *client,
    TpHandleRepoIface *room_repo, const gchar *nickname,
    const gchar *first_name, const gchar *last_name, const gchar *jid,
    const gchar *email, const gchar *published_name,
    const GArray *olpc_key,
    const gchar *olpc_color);

/* Start announcing our presence on the network */
gboolean salut_self_announce (SalutSelf *self, gint port, GError **error);

gboolean salut_self_set_presence (SalutSelf *self,
    SalutPresenceId status, const gchar *message, GError **error);

gboolean salut_self_set_avatar (SalutSelf *self, guint8 *data,
    gsize size, GError **error);

gboolean salut_self_set_alias (SalutSelf *self, const gchar *alias,
    GError **error);

const gchar *salut_self_get_alias (SalutSelf *self);

#ifdef ENABLE_OLPC
gboolean salut_self_set_olpc_properties (SalutSelf *self,
    const GArray *key, const gchar *color, const gchar *jid, GError **error);

gboolean salut_self_merge_olpc_activity_properties (SalutSelf *self,
    TpHandle handle,
    const gchar **color, const gchar **name, const gchar **type,
    const gchar **tags, gboolean *is_private);

gboolean salut_self_set_olpc_activity_properties (SalutSelf *self,
    TpHandle handle,
    const gchar *color, const gchar *name, const gchar *type,
    const gchar *tags, gboolean is_private, GError **error);

gboolean salut_self_olpc_activity_properties_updated (SalutSelf *self,
    TpHandle handle,
    const gchar *color, const gchar *name, const gchar *type,
    const gchar *tags, gboolean is_private);

gboolean salut_self_set_olpc_activities (SalutSelf *self,
    GHashTable *act_id_to_room, GError **error);

gboolean salut_self_set_olpc_current_activity (SalutSelf *self,
    const gchar *id, TpHandle room, GError **error);

typedef void (*SalutSelfOLPCActivityFunc)
    (const gchar *id, TpHandle handle, gpointer user_data);

void salut_self_foreach_olpc_activity (SalutSelf *self,
    SalutSelfOLPCActivityFunc foreach, gpointer user_data);

void salut_self_olpc_augment_invitation (SalutSelf *self,
    TpHandle room, TpHandle contact, GibberXmppNode *invite_node);
#endif

G_END_DECLS

#endif /* #ifndef __SALUT_SELF_H__*/
