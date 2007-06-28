/*
 * salut-tubes-channel.h - Header for SalutTubesChannel
 * Copyright (C) 2007 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the tubesplied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __SALUT_TUBES_CHANNEL_H__
#define __SALUT_TUBES_CHANNEL_H__

#include <glib-object.h>
#include <gibber/gibber-xmpp-stanza.h>
#include <gibber/gibber-xmpp-connection.h>

#include "salut-muc-channel.h"

G_BEGIN_DECLS

typedef struct _SalutTubesChannel SalutTubesChannel;
typedef struct _SalutTubesChannelClass SalutTubesChannelClass;

struct _SalutTubesChannelClass {
    GObjectClass parent_class;
};

struct _SalutTubesChannel {
    GObject parent;

    SalutMucChannel *muc;

    gpointer priv;
};

GType salut_tubes_channel_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_TUBES_CHANNEL \
  (salut_tubes_channel_get_type())
#define SALUT_TUBES_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_TUBES_CHANNEL, \
                              SalutTubesChannel))
#define SALUT_TUBES_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_TUBES_CHANNEL, \
                           SalutTubesChannelClass))
#define SALUT_IS_TUBES_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_TUBES_CHANNEL))
#define SALUT_IS_TUBES_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_TUBES_CHANNEL))
#define SALUT_TUBES_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_TUBES_CHANNEL, \
                              SalutTubesChannelClass))

void salut_tubes_channel_close (SalutTubesChannel *channel);

G_END_DECLS

#endif /* #ifndef __SALUT_TUBES_CHANNEL_H__*/