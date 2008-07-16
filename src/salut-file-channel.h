/*
 * salut-file-channel.h - Header for SalutFtChannel
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

#ifndef __SALUT_FT_CHANNEL_H__
#define __SALUT_FT_CHANNEL_H__

#include <glib-object.h>
#include <gibber/gibber-file-transfer.h>

#include <telepathy-glib/text-mixin.h>
#include "file-transfer-mixin.h"


G_BEGIN_DECLS

typedef struct _SalutFtChannel SalutFtChannel;
typedef struct _SalutFtChannelClass SalutFtChannelClass;
typedef struct _SalutFtChannelPrivate SalutFtChannelPrivate;

struct _SalutFtChannelClass {
    GObjectClass parent_class;
    TpTextMixinClass text_class;
    TpFileTransferMixinClass file_transfer_class;
};

struct _SalutFtChannel {
    GObject parent;
    TpTextMixin text;
    TpFileTransferMixin file_transfer;

    SalutFtChannelPrivate *priv;
};

GType salut_ft_channel_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_FT_CHANNEL \
  (salut_ft_channel_get_type ())
#define SALUT_FT_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_FT_CHANNEL, SalutFtChannel))
#define SALUT_FT_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_FT_CHANNEL, \
                           SalutFtChannelClass))
#define SALUT_IS_FT_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_FT_CHANNEL))
#define SALUT_IS_FT_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_FT_CHANNEL))
#define SALUT_FT_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_FT_CHANNEL, \
                              SalutFtChannelClass))

void
salut_ft_channel_received_file_offer (SalutFtChannel *self,
    GibberXmppStanza *stanza, GibberXmppConnection *conn);

G_END_DECLS

#endif /* #ifndef __SALUT_FT_CHANNEL_H__*/
