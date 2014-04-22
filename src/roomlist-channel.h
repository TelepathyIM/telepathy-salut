/*
 * roomlist-channel.h - Header for SalutRoomlistChannel
 * Copyright (C) 2007 Collabora Ltd.
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

#ifndef __SALUT_ROOMLIST_CHANNEL_H__
#define __SALUT_ROOMLIST_CHANNEL_H__

#include <glib-object.h>

#include <telepathy-glib/telepathy-glib.h>

#include "connection.h"

G_BEGIN_DECLS

typedef struct _SalutRoomlistChannel SalutRoomlistChannel;
typedef struct _SalutRoomlistChannelClass SalutRoomlistChannelClass;
typedef struct _SalutRoomlistChannelPrivate SalutRoomlistChannelPrivate;

struct _SalutRoomlistChannelClass {
    TpBaseChannelClass parent_class;
};

struct _SalutRoomlistChannel {
    TpBaseChannel parent;

    SalutRoomlistChannelPrivate *priv;
};

GType salut_roomlist_channel_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_ROOMLIST_CHANNEL \
  (salut_roomlist_channel_get_type ())
#define SALUT_ROOMLIST_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_ROOMLIST_CHANNEL,\
                              SalutRoomlistChannel))
#define SALUT_ROOMLIST_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_ROOMLIST_CHANNEL,\
                           SalutRoomlistChannelClass))
#define SALUT_IS_ROOMLIST_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_ROOMLIST_CHANNEL))
#define SALUT_IS_ROOMLIST_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_ROOMLIST_CHANNEL))
#define SALUT_ROOMLIST_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_ROOMLIST_CHANNEL,\
                              SalutRoomlistChannelClass))


SalutRoomlistChannel *
salut_roomlist_channel_new (SalutConnection *conn, const gchar *object_path);

void
salut_roomlist_channel_add_room (SalutRoomlistChannel *self,
    const gchar *room_name);

void
salut_roomlist_channel_remove_room (SalutRoomlistChannel *self,
    const gchar *room_name);

G_END_DECLS

#endif /* #ifndef __SALUT_ROOMLIST_CHANNEL_H__*/
