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

#include <wocky/wocky-stanza.h>

#include <salut/capabilities-set.h>

#include "salut-connection.h"
#include "salut-presence.h"
#ifdef ENABLE_OLPC
#include "salut-olpc-activity.h"
#endif

G_BEGIN_DECLS

typedef struct _SalutSelf SalutSelf;
typedef struct _SalutSelfClass SalutSelfClass;
typedef struct _SalutSelfPrivate SalutSelfPrivate;

struct _SalutSelfClass {
    GObjectClass parent_class;

    /* public abstract methods */
    gboolean (*announce) (SalutSelf *self, guint16 port, GError **error);
    gboolean (*set_presence) (SalutSelf *self, GError **error);
    gboolean (*set_caps) (SalutSelf *self, GError **error);
    gboolean (*set_alias) (SalutSelf *self, GError **error);
    gboolean (*set_avatar) (SalutSelf *self, guint8 *data, gsize size,
        GError **error);
#ifdef ENABLE_OLPC
    gboolean (*set_olpc_properties) (SalutSelf *self, const GArray *key,
          const gchar *color, const gchar *jid, GError **error);
#endif

    /* private abstract methods */
    void (*remove_avatar) (SalutSelf *self);
    gboolean (*update_current_activity) (SalutSelf *self,
        const gchar *room_name, GError **error);
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
    gchar *node;
    gchar *hash;
    gchar *ver;

    /* private */
    SalutConnection *connection;
    gchar *nickname;
    gchar *first_name;
    gchar *last_name;
    gchar *email;
    gchar *published_name;
    gchar *alias;

    SalutSelfPrivate *priv;
};

GType salut_self_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_SELF \
  (salut_self_get_type ())
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

/* Start announcing our presence on the network */
gboolean salut_self_announce (SalutSelf *self, guint16 port, GError **error);

gboolean salut_self_set_presence (SalutSelf *self,
    SalutPresenceId status, const gchar *message, GError **error);

gboolean salut_self_set_caps (SalutSelf *self, const gchar *node,
    const gchar *hash, const gchar *ver, GError **error);

gboolean salut_self_set_avatar (SalutSelf *self, guint8 *data,
    gsize size, GError **error);

gboolean salut_self_set_alias (SalutSelf *self, const gchar *alias,
    GError **error);

const gchar *salut_self_get_alias (SalutSelf *self);

#ifdef ENABLE_OLPC
gboolean salut_self_set_olpc_properties (SalutSelf *self,
    const GArray *key, const gchar *color, const gchar *jid, GError **error);

gboolean salut_self_set_olpc_activity_properties (SalutSelf *self,
    TpHandle handle,
    const gchar *color, const gchar *name, const gchar *type,
    const gchar *tags, gboolean is_private, GError **error);

gboolean salut_self_set_olpc_activities (SalutSelf *self,
    GHashTable *act_id_to_room, GError **error);

gboolean salut_self_add_olpc_activity (SalutSelf *self,
    const gchar *activity_id, TpHandle room, GError **error);

gboolean salut_self_remove_olpc_activity (SalutSelf *self,
    SalutOlpcActivity *activity);

gboolean salut_self_set_olpc_current_activity (SalutSelf *self,
    const gchar *id, TpHandle room, GError **error);

typedef void (*SalutSelfOLPCActivityFunc)
  (SalutOlpcActivity *activity, gpointer user_data);

void salut_self_foreach_olpc_activity (SalutSelf *self,
    SalutSelfOLPCActivityFunc foreach, gpointer user_data);

void salut_self_olpc_augment_invitation (SalutSelf *self,
    TpHandle room, TpHandle contact, WockyNode *invite_node);
#endif

const GabbleCapabilitySet *salut_self_get_caps (SalutSelf *self);

void salut_self_take_caps (SalutSelf *self, GabbleCapabilitySet *caps);

void salut_self_take_data_forms (SalutSelf *self, GPtrArray *data_forms);

/* protected methods */
void salut_self_established (SalutSelf *self);

G_END_DECLS

#endif /* #ifndef __SALUT_SELF_H__*/
