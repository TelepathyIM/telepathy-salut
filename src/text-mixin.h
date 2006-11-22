/*
 * text-mixin.h - Header for TextMixin
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
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

#ifndef __TEXT_MIXIN_H__
#define __TEXT_MIXIN_H__

#include "handle-repository.h"
#include "handle-set.h"
#include "util.h"

typedef enum {
    CHANNEL_TEXT_SEND_ERROR_UNKNOWN = 0,
    CHANNEL_TEXT_SEND_ERROR_OFFLINE,
    CHANNEL_TEXT_SEND_ERROR_INVALID_CONTACT,
    CHANNEL_TEXT_SEND_ERROR_PERMISSION_DENIED,
    CHANNEL_TEXT_SEND_ERROR_TOO_LONG,
    CHANNEL_TEXT_SEND_ERROR_NOT_IMPLEMENTED,

    CHANNEL_TEXT_SEND_NO_ERROR = -1
} TextMixinSendError;

G_BEGIN_DECLS

typedef struct _TextMixinClass TextMixinClass;
typedef struct _TextMixin TextMixin;

typedef gboolean (*TextMixinSendFunc) (GObject *obj, 
                                       const gchar *text,
                                       LmMessage *message,
                                       GError **error);

struct _TextMixinClass {
  guint lost_message_signal_id;
  guint received_signal_id;
  guint send_error_signal_id;
  guint sent_signal_id;

  TextMixinSendFunc send;
};

struct _TextMixin {
  HandleRepo *handle_repo;
  guint recv_id;
  gboolean message_lost;

  GQueue *pending;

  GArray *msg_types;
};

GType text_mixin_get_type(void);

/* TYPE MACROS */
#define TEXT_MIXIN_CLASS_OFFSET_QUARK (text_mixin_class_get_offset_quark())
#define TEXT_MIXIN_CLASS_OFFSET(o) (GPOINTER_TO_UINT (g_type_get_qdata (G_OBJECT_CLASS_TYPE (o), TEXT_MIXIN_CLASS_OFFSET_QUARK)))
#define TEXT_MIXIN_CLASS(o) ((TextMixinClass *) mixin_offset_cast (o, TEXT_MIXIN_CLASS_OFFSET (o)))

#define TEXT_MIXIN_OFFSET_QUARK (text_mixin_get_offset_quark())
#define TEXT_MIXIN_OFFSET(o) (GPOINTER_TO_UINT (g_type_get_qdata (G_OBJECT_TYPE (o), TEXT_MIXIN_OFFSET_QUARK)))
#define TEXT_MIXIN(o) ((TextMixin *) mixin_offset_cast (o, TEXT_MIXIN_OFFSET (o)))

GQuark text_mixin_class_get_offset_quark (void);
GQuark text_mixin_get_offset_quark (void);

void text_mixin_class_init (GObjectClass *obj_cls, glong offset, 
                            TextMixinSendFunc send);
void text_mixin_init (GObject *obj, glong offset, HandleRepo *handle_repo);
void text_mixin_set_message_types (GObject *obj, ...);
void text_mixin_finalize (GObject *obj);

/* D-Bus method implementations */
gboolean text_mixin_receive (GObject *obj, TpChannelTextMessageType type, Handle sender, time_t timestamp, const char *text);
gboolean text_mixin_acknowledge_pending_messages (GObject *obj, const GArray * ids, GError **error);
gboolean text_mixin_list_pending_messages (GObject *obj, gboolean clear, GPtrArray ** ret, GError **error);
gboolean text_mixin_send (GObject *obj, guint type, guint subtype, const char *from, const char * to, const gchar * text, GError **error);
gboolean text_mixin_get_message_types (GObject *obj, GArray **ret, GError **error);

/* Utility functions for the mixin user */
gboolean text_mixin_parse_incoming_message (LmMessage *message, const gchar **from, TpChannelTextMessageType *msgtype, const gchar **body, const gchar **body_offset, TextMixinSendError *send_error);
void text_mixin_emit_sent (GObject *obj, time_t timestamp, guint type, const char *text);
void text_mixin_emit_send_error(GObject *obj, TextMixinSendError error, time_t timestamp, TpChannelTextMessageType type, const gchar *text);
void text_mixin_clear (GObject *obj);

G_END_DECLS

#endif /* #ifndef __TEXT_MIXIN_H__ */

