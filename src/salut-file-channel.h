/*
 * salut-file-channel.h - Header for SalutFileChannel
 * Copyright (C) 2007 Marco Barisione <marco@barisione.org>
 * Copyright (C) 2005, 2007 Collabora Ltd.
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

#ifndef __SALUT_FILE_CHANNEL_H__
#define __SALUT_FILE_CHANNEL_H__

#include <glib-object.h>
#include <gibber/gibber-file-transfer.h>

#include <telepathy-glib/text-mixin.h>

#include <extensions/_gen/svc.h>
#include <extensions/_gen/interfaces.h>
#include <extensions/_gen/enums.h>

G_BEGIN_DECLS

typedef struct _SalutFileChannel SalutFileChannel;
typedef struct _SalutFileChannelClass SalutFileChannelClass;
typedef struct _SalutFileChannelPrivate SalutFileChannelPrivate;

struct _SalutFileChannelClass {
    GObjectClass parent_class;
    TpTextMixinClass text_class;
    TpDBusPropertiesMixinClass dbus_props_class;
};

struct _SalutFileChannel {
    GObject parent;
    TpTextMixin text;

    SalutFileChannelPrivate *priv;
};

GType salut_file_channel_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_FILE_CHANNEL \
  (salut_file_channel_get_type ())
#define SALUT_FILE_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_FILE_CHANNEL, SalutFileChannel))
#define SALUT_FILE_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_FILE_CHANNEL, \
                           SalutFileChannelClass))
#define SALUT_IS_FILE_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_FILE_CHANNEL))
#define SALUT_IS_FILE_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_FILE_CHANNEL))
#define SALUT_FILE_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_FILE_CHANNEL, \
                              SalutFileChannelClass))

void
salut_file_channel_received_file_offer (SalutFileChannel *self,
    GibberXmppStanza *stanza, GibberXmppConnection *conn);

G_END_DECLS

#endif /* #ifndef __SALUT_FILE_CHANNEL_H__*/
