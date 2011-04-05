/*
 * salut-roomlist-manager.h - Header for SalutRoomlistManager
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

#ifndef __SALUT_ROOMLIST_MANAGER_H__
#define __SALUT_ROOMLIST_MANAGER_H__

#include <glib-object.h>

#include <gibber/gibber-bytestream-iface.h>

#include <salut-connection.h>
#include "salut-tubes-channel.h"
#include "salut-roomlist-channel.h"

G_BEGIN_DECLS

typedef struct _SalutRoomlistManager SalutRoomlistManager;
typedef struct _SalutRoomlistManagerClass SalutRoomlistManagerClass;

struct _SalutRoomlistManagerClass {
    GObjectClass parent_class;

    /* public abstract methods */
    gboolean (*start) (SalutRoomlistManager *self, GError **error);

    /* private abstract methods */
    gboolean (*find_muc_address) (SalutRoomlistManager *self, const gchar *name,
        gchar **address, guint16 *port);

    GSList * (*get_rooms) (SalutRoomlistManager *self);
};

struct _SalutRoomlistManager {
    GObject parent;
};

GType salut_roomlist_manager_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_ROOMLIST_MANAGER \
  (salut_roomlist_manager_get_type ())
#define SALUT_ROOMLIST_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_ROOMLIST_MANAGER, \
  SalutRoomlistManager))
#define SALUT_ROOMLIST_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_ROOMLIST_MANAGER, \
  SalutRoomlistManagerClass))
#define SALUT_IS_ROOMLIST_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_ROOMLIST_MANAGER))
#define SALUT_IS_ROOMLIST_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_ROOMLIST_MANAGER))
#define SALUT_ROOMLIST_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_ROOMLIST_MANAGER, \
  SalutRoomlistManagerClass))

gboolean
salut_roomlist_manager_start (SalutRoomlistManager *roomlist_manager,
    GError **error);

/* "protected" methods */
void salut_roomlist_manager_room_discovered (
    SalutRoomlistManager *roomlist_manager, const gchar *room);

void salut_roomlist_manager_room_removed (
    SalutRoomlistManager *roomlist_manager, const gchar *room);


G_END_DECLS

#endif /* #ifndef __SALUT_ROOMLIST_MANAGER_H__*/
