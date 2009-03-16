/*
 * salut-file-transfer-channel.h - Header for SalutFileTransferChannel
 * Copyright (C) 2007 Marco Barisione <marco@barisione.org>
 * Copyright (C) 2005, 2007, 2008 Collabora Ltd.
 *   @author: Sjoerd Simons <sjoerd@luon.net>
 *   @author: Jonny Lamb <jonny.lamb@collabora.co.uk>
 *   @author: Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
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

#ifndef __SALUT_FILE_TRANSFER_CHANNEL_H__
#define __SALUT_FILE_TRANSFER_CHANNEL_H__

#include <glib-object.h>
#include <gibber/gibber-file-transfer.h>

#include <extensions/_gen/svc.h>
#include <extensions/_gen/interfaces.h>
#include <extensions/_gen/enums.h>

#include "salut-contact.h"
#include "salut-connection.h"
#include "salut-contact.h"
#include "salut-xmpp-connection-manager.h"

G_BEGIN_DECLS

typedef struct _SalutFileTransferChannel SalutFileTransferChannel;
typedef struct _SalutFileTransferChannelClass SalutFileTransferChannelClass;
typedef struct _SalutFileTransferChannelPrivate SalutFileTransferChannelPrivate;

struct _SalutFileTransferChannelClass {
    GObjectClass parent_class;
    TpDBusPropertiesMixinClass dbus_props_class;
};

struct _SalutFileTransferChannel {
    GObject parent;

    SalutFileTransferChannelPrivate *priv;
};

GType salut_file_transfer_channel_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_FILE_TRANSFER_CHANNEL \
  (salut_file_transfer_channel_get_type ())
#define SALUT_FILE_TRANSFER_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_FILE_TRANSFER_CHANNEL, SalutFileTransferChannel))
#define SALUT_FILE_TRANSFER_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_FILE_TRANSFER_CHANNEL, \
                           SalutFileTransferChannelClass))
#define SALUT_IS_FILE_TRANSFER_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_FILE_TRANSFER_CHANNEL))
#define SALUT_IS_FILE_TRANSFER_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_FILE_TRANSFER_CHANNEL))
#define SALUT_FILE_TRANSFER_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_FILE_TRANSFER_CHANNEL, \
                              SalutFileTransferChannelClass))

gboolean salut_file_transfer_channel_offer_file (SalutFileTransferChannel *self,
    GError **error);

SalutFileTransferChannel * salut_file_transfer_channel_new (
    SalutConnection *conn, SalutContact *contact,
    TpHandle handle, SalutXmppConnectionManager *xcm, TpHandle initiator_handle,
    TpFileTransferState state, const gchar *content_type,
    const gchar *filename, guint64 size, TpFileHashType hash_type,
    const gchar *content_hash, const gchar *description, guint64 date,
    guint64 initial_offset);

SalutFileTransferChannel * salut_file_transfer_channel_new_from_stanza (
    SalutConnection *connection, SalutContact *contact,
    TpHandle handle, SalutXmppConnectionManager *xcm,
    TpFileTransferState state, GibberXmppStanza *stanza,
    GibberXmppConnection *conn);

G_END_DECLS

#endif /* #ifndef __SALUT_FILE_TRANSFER_CHANNEL_H__*/
