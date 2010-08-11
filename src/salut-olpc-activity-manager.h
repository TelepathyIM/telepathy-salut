/*
 * salut-olpc-activity-managere.h - Header for SalutOlpcActivityManager
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

#ifndef __SALUT_OLPC_ACTIVITY_MANAGER_H__
#define __SALUT_OLPC_ACTIVITY_MANAGER_H__

#include <glib-object.h>

#include <telepathy-glib/handle.h>

#include "salut-connection.h"
#include "salut-contact.h"
#include "salut-olpc-activity.h"

G_BEGIN_DECLS

typedef struct _SalutOlpcActivityManager SalutOlpcActivityManager;
typedef struct _SalutOlpcActivityManagerClass SalutOlpcActivityManagerClass;

struct _SalutOlpcActivityManagerClass {
    GObjectClass parent_class;

    /* public abstract methods */
    gboolean (*start) (SalutOlpcActivityManager *self, GError **error);

    /* private abstract methods */
    SalutOlpcActivity * (*create_activity) (SalutOlpcActivityManager *self);
};

struct _SalutOlpcActivityManager {
    GObject parent;

    /* private */
    SalutConnection *connection;
};

GType salut_olpc_activity_manager_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_OLPC_ACTIVITY_MANAGER \
  (salut_olpc_activity_manager_get_type ())
#define SALUT_OLPC_ACTIVITY_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_OLPC_ACTIVITY_MANAGER, SalutOlpcActivityManager))
#define SALUT_OLPC_ACTIVITY_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_OLPC_ACTIVITY_MANAGER, SalutOlpcActivityManagerClass))
#define SALUT_IS_OLPC_ACTIVITY_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_OLPC_ACTIVITY_MANAGER))
#define SALUT_IS_OLPC_ACTIVITY_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_OLPC_ACTIVITY_MANAGER))
#define SALUT_OLPC_ACTIVITY_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_OLPC_ACTIVITY_MANAGER, SalutOlpcActivityManagerClass))

gboolean salut_olpc_activity_manager_start (SalutOlpcActivityManager *mgr,
    GError **error);

SalutOlpcActivity * salut_olpc_activity_manager_get_activity_by_room (
    SalutOlpcActivityManager *mgr, TpHandle room);

SalutOlpcActivity * salut_olpc_activity_manager_get_activity_by_id (
    SalutOlpcActivityManager *mgr, const gchar *activity_id);

SalutOlpcActivity * salut_olpc_activity_manager_ensure_activity_by_room (
    SalutOlpcActivityManager *mgr, TpHandle room);

SalutOlpcActivity * salut_olpc_activity_manager_got_invitation (
    SalutOlpcActivityManager *mgr, TpHandle room, SalutContact *inviter,
    const gchar *id, const gchar *name, const gchar *type, const gchar *color,
    const gchar *tags);

/* restricted methods */
SalutOlpcActivity * salut_olpc_activity_manager_create_activity (
    SalutOlpcActivityManager *mgr, TpHandle room);

void salut_olpc_activity_manager_contact_joined (SalutOlpcActivityManager *mgr,
    SalutContact *contact, SalutOlpcActivity *activity);

void salut_olpc_activity_manager_contact_left (SalutOlpcActivityManager *mgr,
    SalutContact *contact, SalutOlpcActivity *activity);

G_END_DECLS

#endif /* #ifndef __SALUT_OLPC_ACTIVITY_MANAGER_H__*/
