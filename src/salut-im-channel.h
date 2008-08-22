/*
 * salut-im-channel.h - Header for SalutImChannel
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

#ifndef __SALUT_IM_CHANNEL_H__
#define __SALUT_IM_CHANNEL_H__

#include <glib-object.h>
#include <gibber/gibber-xmpp-stanza.h>
#include <gibber/gibber-xmpp-connection.h>

#include <telepathy-glib/text-mixin.h>

G_BEGIN_DECLS

typedef struct _SalutImChannel SalutImChannel;
typedef struct _SalutImChannelClass SalutImChannelClass;

struct _SalutImChannelClass {
    GObjectClass parent_class;

    TpTextMixinClass text_class;
    TpDBusPropertiesMixinClass dbus_props_class;
};

struct _SalutImChannel {
    GObject parent;
    TpTextMixin text;
};

GType salut_im_channel_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_IM_CHANNEL \
  (salut_im_channel_get_type ())
#define SALUT_IM_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_IM_CHANNEL, SalutImChannel))
#define SALUT_IM_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_IM_CHANNEL, \
                           SalutImChannelClass))
#define SALUT_IS_IM_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_IM_CHANNEL))
#define SALUT_IS_IM_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_IM_CHANNEL))
#define SALUT_IM_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_IM_CHANNEL, \
                              SalutImChannelClass))

void salut_im_channel_add_connection (SalutImChannel *chan,
    GibberXmppConnection *conn);

void salut_im_channel_received_stanza (SalutImChannel *chan,
    GibberXmppStanza *stanza);

gboolean
salut_im_channel_is_text_message (GibberXmppStanza *stanza);

G_END_DECLS

#endif /* #ifndef __SALUT_IM_CHANNEL_H__*/
