/*
 * gibber-oob-file-transfer.h - Header for GibberOobFileTransfer
 * Copyright (C) 2007 Marco Barisione <marco@barisione.org>
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

#ifndef __GIBBER_OOB_FILE_TRANSFER_H__
#define __GIBBER_OOB_FILE_TRANSFER_H__

#include <glib.h>
#include <glib-object.h>
#include "gibber-xmpp-connection.h"
#include "gibber-file-transfer.h"

G_BEGIN_DECLS

typedef struct _GibberOobFileTransfer GibberOobFileTransfer;
typedef struct _GibberOobFileTransferClass GibberOobFileTransferClass;

struct _GibberOobFileTransferClass
{
    GibberFileTransferClass parent_class;
};

typedef struct _GibberOobFileTransferPrivate GibberOobFileTransferPrivate;

struct _GibberOobFileTransfer {
    GibberFileTransfer parent;

    GibberOobFileTransferPrivate *priv;
};

GType gibber_oob_file_transfer_get_type (void);

/* TYPE MACROS */
#define GIBBER_TYPE_OOB_FILE_TRANSFER \
  (gibber_oob_file_transfer_get_type ())
#define GIBBER_OOB_FILE_TRANSFER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GIBBER_TYPE_OOB_FILE_TRANSFER, GibberOobFileTransfer))
#define GIBBER_OOB_FILE_TRANSFER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GIBBER_TYPE_OOB_FILE_TRANSFER, GibberOobFileTransferClass))
#define GIBBER_IS_OOB_FILE_TRANSFER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GIBBER_TYPE_OOB_FILE_TRANSFER))
#define GIBBER_IS_OOB_FILE_TRANSFER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GIBBER_TYPE_OOB_FILE_TRANSFER))
#define GIBBER_OOB_FILE_TRANSFER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_OOB_FILE_TRANSFER, GibberOobFileTransferClass))


gboolean gibber_oob_file_transfer_is_file_offer (GibberXmppStanza *stanza);
GibberFileTransfer *gibber_oob_file_transfer_new_from_stanza (
    GibberXmppStanza *stanza, GibberXmppConnection *connection);


G_END_DECLS

#endif /* #ifndef __GIBBER_OOB_FILE_TRANSFER_H__*/
