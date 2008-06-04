/*
 * salut-olpc-activity.h - Header for SalutOlpcActivity
 * Copyright (C) 2008 Collabora Ltd.
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

#ifndef __SALUT_OLPC_ACTIVITY_H__
#define __SALUT_OLPC_ACTIVITY_H__

#include <glib-object.h>

#include <telepathy-glib/handle.h>

#include "salut-connection.h"

G_BEGIN_DECLS

typedef struct _SalutOlpcActivity SalutOlpcActivity;
typedef struct _SalutOlpcActivityClass SalutOlpcActivityClass;

struct _SalutOlpcActivityClass {
    GObjectClass parent_class;

    /* private abstract methods */
    gboolean (*announce) (SalutOlpcActivity *activity, GError **error);
    void (*stop_announce) (SalutOlpcActivity *activity);

    gboolean (*update) (SalutOlpcActivity *activity, GError **error);
};

struct _SalutOlpcActivity {
    GObject parent;

    TpHandle room;
    gchar *id;
    gchar *name;
    gchar *type;
    gchar *color;
    gchar *tags;
    gboolean is_private;

    /* private */
    SalutConnection *connection;
};

GType salut_olpc_activity_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_OLPC_ACTIVITY \
  (salut_olpc_activity_get_type ())
#define SALUT_OLPC_ACTIVITY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_OLPC_ACTIVITY, SalutOlpcActivity))
#define SALUT_OLPC_ACTIVITY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_OLPC_ACTIVITY, SalutOlpcActivityClass))
#define SALUT_IS_OLPC_ACTIVITY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_OLPC_ACTIVITY))
#define SALUT_IS_OLPC_ACTIVITY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_OLPC_ACTIVITY))
#define SALUT_OLPC_ACTIVITY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_OLPC_ACTIVITY, SalutOlpcActivityClass))

G_END_DECLS

gboolean salut_olpc_activity_update (SalutOlpcActivity *activity,
    TpHandle room, const gchar *id, const gchar *name,
    const gchar *type, const gchar *color, const gchar *tags,
    gboolean is_private);

gboolean salut_olpc_activity_joined (SalutOlpcActivity *activity,
    GError **error);

void salut_olpc_activity_left (SalutOlpcActivity *activity);

void salut_olpc_activity_revoke_invitations (SalutOlpcActivity *activity);

GHashTable * salut_olpc_activity_create_properties_table (
    SalutOlpcActivity *activity);

void salut_olpc_activity_augment_invitation (SalutOlpcActivity *activity,
    TpHandle contact, GibberXmppNode *invite_node);

gboolean salut_olpc_activity_remove_invited (SalutOlpcActivity *activity,
    TpHandle contact);

#endif /* #ifndef __SALUT_OLPC_ACTIVITY_H__*/
