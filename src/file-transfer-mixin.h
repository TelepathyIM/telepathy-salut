/*
 * text-file-transfer-mixin.h - Header for TpFileTransferMixin
 * Copyright (C) 2007 Marco Barisione <marco@barisione.org>
 * Copyright (C) 2006, 2007 Collabora Ltd.
 * Copyright (C) 2006, 2007 Nokia Corporation
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

#ifndef __TP_FILE_TRANSFER_MIXIN_H__
#define __TP_FILE_TRANSFER_MIXIN_H__

#include <telepathy-glib/handle-repo.h>
#include <telepathy-glib/svc-channel.h>
#include <telepathy-glib/util.h>

#include <extensions/_gen/svc.h>
#include <extensions/_gen/enums.h>
#include <extensions/_gen/interfaces.h>

G_BEGIN_DECLS

/* FIXME these should be automatically generated */
#define TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER "org.freedesktop.Telepathy.Channel.Type.FileTransfer"

typedef enum {
  TP_FILE_TRANSFER_DIRECTION_INCOMING,
  TP_FILE_TRANSFER_DIRECTION_OUTGOING
} TpFileTransferDirection;
typedef enum {
  TP_FILE_TRANSFER_STATE_LOCAL_PENDING,
  TP_FILE_TRANSFER_STATE_REMOTE_PENDING,
  TP_FILE_TRANSFER_STATE_OPEN
} TpFileTransferState;

typedef struct _TpFileTransferMixinClass TpFileTransferMixinClass;
typedef struct _TpFileTransferMixinClassPrivate TpFileTransferMixinClassPrivate;
typedef struct _TpFileTransferMixin TpFileTransferMixin;
typedef struct _TpFileTransferMixinPrivate TpFileTransferMixinPrivate;

/**
 * TpFileTransferMixinClass:
 *
 * Structure to be included in the class structure of objects that
 * use this mixin. Initialize it with tp_file_transfer_mixin_class_init().
 *
 * There are no public fields.
 */
struct _TpFileTransferMixinClass {
  /*<private>*/
  TpFileTransferMixinClassPrivate *priv;
};

/**
 * TpFileTransferMixin:
 *
 * Structure to be included in the instance structure of objects that
 * use this mixin. Initialize it with tp_file_transfer_mixin_init().
 *
 * There are no public fields.
 */
struct _TpFileTransferMixin {
  /*<private>*/
  TpFileTransferMixinPrivate *priv;
};

/* TYPE MACROS */
#define TP_FILE_TRANSFER_MIXIN_CLASS_OFFSET_QUARK \
  (tp_file_transfer_mixin_class_get_offset_quark ())
#define TP_FILE_TRANSFER_MIXIN_CLASS_OFFSET(o) \
  (GPOINTER_TO_UINT (g_type_get_qdata (G_OBJECT_CLASS_TYPE (o), \
                                       TP_FILE_TRANSFER_MIXIN_CLASS_OFFSET_QUARK)))
#define TP_FILE_TRANSFER_MIXIN_CLASS(o) \
  ((TpFileTransferMixinClass *) tp_mixin_offset_cast (o, \
    TP_FILE_TRANSFER_MIXIN_CLASS_OFFSET (o)))

#define TP_FILE_TRANSFER_MIXIN_OFFSET_QUARK (tp_file_transfer_mixin_get_offset_quark ())
#define TP_FILE_TRANSFER_MIXIN_OFFSET(o) \
  (GPOINTER_TO_UINT (g_type_get_qdata (G_OBJECT_TYPE (o), \
                                       TP_FILE_TRANSFER_MIXIN_OFFSET_QUARK)))
#define TP_FILE_TRANSFER_MIXIN(o) \
  ((TpFileTransferMixin *) tp_mixin_offset_cast (o, TP_FILE_TRANSFER_MIXIN_OFFSET (o)))

GQuark tp_file_transfer_mixin_class_get_offset_quark (void);
GQuark tp_file_transfer_mixin_get_offset_quark (void);

void tp_file_transfer_mixin_class_init (GObjectClass *obj_cls, glong offset);

void tp_file_transfer_mixin_init (GObject *obj, glong offset,
    TpHandleRepoIface *contacts_repo);
void tp_file_transfer_mixin_finalize (GObject *obj);
void tp_file_transfer_mixin_iface_init (gpointer g_iface, gpointer iface_data);

gboolean tp_file_transfer_mixin_set_user_data (GObject *obj, guint id,
    gpointer user_data);
gpointer tp_file_transfer_mixin_get_user_data (GObject *obj, guint id);

gboolean tp_file_transfer_mixin_set_state (GObject *obj, guint id,
    TpFileTransferState state, GError **error);
TpFileTransferState tp_file_transfer_mixin_get_state (GObject *obj, guint id,
    GError **error);

guint tp_file_transfer_mixin_add_transfer (GObject *obj, TpHandle initiator,
    TpFileTransferDirection direction, TpFileTransferState state,
    const char *filename, GHashTable *information, gpointer user_data);
gboolean tp_file_transfer_mixin_get_file_transfer (GObject *obj, guint id,
    GValue **ret, GError **error);
gboolean tp_file_transfer_mixin_list_file_transfers (GObject *obj,
    GPtrArray **ret, GError **error);
gboolean tp_file_transfer_mixin_get_local_unix_socket_path (GObject *obj,
    guint id, gchar **ret, GError **error);

G_END_DECLS

#endif /* #ifndef __TP_FILE_TRANSFER_MIXIN_H__ */
