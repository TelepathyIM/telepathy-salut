/*
 * gibber-file-transfer.h - Header for GibberFileTransfer
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

#ifndef __GIBBER_FILE_TRANSFER_H__
#define __GIBBER_FILE_TRANSFER_H__

#include <glib.h>
#include <glib-object.h>
#include "gibber-xmpp-stanza.h"
#include "gibber-xmpp-connection.h"

G_BEGIN_DECLS

typedef enum
{
  GIBBER_FILE_TRANSFER_DIRECTION_INCOMING,
  GIBBER_FILE_TRANSFER_DIRECTION_OUTGOING
} GibberFileTransferDirection;

typedef enum
{
  GIBBER_FILE_TRANSFER_ERROR_NOT_CONNECTED,
  GIBBER_FILE_TRANSFER_ERROR_NOT_FOUND,
  GIBBER_FILE_TRANSFER_ERROR_NOT_ACCEPTABLE
} GibberFileTransferError;

#define GIBBER_FILE_TRANSFER_ERROR gibber_file_transfer_error_quark ()

GQuark gibber_file_transfer_error_quark (void);

typedef struct _GibberFileTransfer GibberFileTransfer;
typedef struct _GibberFileTransferClass GibberFileTransferClass;

struct _GibberFileTransferClass
{
  GObjectClass parent_class;

  void (*offer) (GibberFileTransfer *ft);
  void (*send) (GibberFileTransfer *ft,
                GIOChannel *src);
  void (*receive) (GibberFileTransfer *ft,
                   GIOChannel *dest);
  void (*cancel) (GibberFileTransfer *ft,
                  guint error_code);
  void (*received_stanza) (GibberFileTransfer *ft,
                           GibberXmppStanza *stanza);
};

typedef struct _GibberFileTransferPrivate GibberFileTransferPrivate;

struct _GibberFileTransfer
{
  GObject parent;

  GibberFileTransferPrivate *priv;

  /*< public >*/
  gchar *id;

  gchar *self_id;
  gchar *peer_id;

  gchar *filename;
  gchar *description;
  gchar *content_type;

  GibberFileTransferDirection direction;
};

GType gibber_file_transfer_get_type (void);

/* TYPE MACROS */
#define GIBBER_TYPE_FILE_TRANSFER \
  (gibber_file_transfer_get_type ())
#define GIBBER_FILE_TRANSFER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GIBBER_TYPE_FILE_TRANSFER, \
                               GibberFileTransfer))
#define GIBBER_FILE_TRANSFER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GIBBER_TYPE_FILE_TRANSFER, \
                            GibberFileTransferClass))
#define GIBBER_IS_FILE_TRANSFER (obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GIBBER_TYPE_FILE_TRANSFER))
#define GIBBER_IS_FILE_TRANSFER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GIBBER_TYPE_FILE_TRANSFER))
#define GIBBER_FILE_TRANSFER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_FILE_TRANSFER, \
                              GibberFileTransferClass))


gboolean gibber_file_transfer_is_file_offer (GibberXmppStanza *stanza);

GibberFileTransfer *gibber_file_transfer_new_from_stanza (
    GibberXmppStanza *stanza, GibberXmppConnection *connection,
    GError **error);
GibberFileTransfer *gibber_file_transfer_new_from_stanza_with_from (
    GibberXmppStanza *stanza, GibberXmppConnection *connection,
    const gchar *from, GError **error);

void gibber_file_transfer_offer (GibberFileTransfer *self);
void gibber_file_transfer_send (GibberFileTransfer *self, GIOChannel *src);
void gibber_file_transfer_receive (GibberFileTransfer *self, GIOChannel *dest);
void gibber_file_transfer_cancel (GibberFileTransfer *self, guint error_code);

/* these functions should only be used by backends */
/* FIXME move to a private header if gibber becomes a public library */

gboolean gibber_file_transfer_send_stanza (GibberFileTransfer *self,
    GibberXmppStanza *stanza, GError **error);

void gibber_file_transfer_emit_error (GibberFileTransfer *self, GError *error);

void gibber_file_transfer_set_size (GibberFileTransfer *self, guint64 size);
guint64 gibber_file_transfer_get_size (GibberFileTransfer *self);

G_END_DECLS

#endif /* #ifndef __GIBBER_FILE_TRANSFER_H__*/
