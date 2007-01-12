/*
 * salut-transport-mixin.h - Header for SalutTransportMixin
 * Copyright (C) 2006 Collabora Ltd.
 *   @author Sjoerd Simons <sjoerd@luon.net>
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

#ifndef __SALUT_TRANSPORT_MIXIN_H__
#define __SALUT_TRANSPORT_MIXIN_H__

#include "util.h"
#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
  SALUT_TRANSPORT_DISCONNECTED,
  SALUT_TRANSPORT_CONNECTING,
  SALUT_TRANSPORT_CONNECTED,

} SalutTransportState;


typedef struct _SalutTransportMixinClass SalutTransportMixinClass;
typedef struct _SalutTransportMixin SalutTransportMixin;

typedef gboolean (*SalutTransportMixinSendFunc) (GObject *obj, 
                                                 const guint8 *data,
                                                 gsize length,
                                                 GError **error);
typedef void (*SalutTransportMixinDisconnectFunc) (GObject *obj);

struct _SalutTransportMixinClass {
  guint connected_signal_id;
  guint connecting_signal_id;
  guint disconnected_signal_id;
  guint connect_error_signal_id;

  guint received_signal_id;
  guint error_signal_id;

  SalutTransportMixinSendFunc send;
  SalutTransportMixinDisconnectFunc disconnect;
};

struct _SalutTransportMixin {
  SalutTransportState state;
};

GType salut_transport_mixin_get_type(void);

/* TYPE MACROS */
#define SALUT_TRANSPORT_MIXIN_CLASS_OFFSET_QUARK (salut_transport_mixin_class_get_offset_quark())
#define SALUT_TRANSPORT_MIXIN_CLASS_OFFSET(o) (GPOINTER_TO_UINT (g_type_get_qdata (G_OBJECT_CLASS_TYPE (o), SALUT_TRANSPORT_MIXIN_CLASS_OFFSET_QUARK)))
#define SALUT_TRANSPORT_MIXIN_CLASS(o) ((SalutTransportMixinClass *) mixin_offset_cast (o, SALUT_TRANSPORT_MIXIN_CLASS_OFFSET (o)))

#define SALUT_TRANSPORT_MIXIN_OFFSET_QUARK (salut_transport_mixin_get_offset_quark())
#define SALUT_TRANSPORT_MIXIN_OFFSET(o) (GPOINTER_TO_UINT (g_type_get_qdata (G_OBJECT_TYPE (o), SALUT_TRANSPORT_MIXIN_OFFSET_QUARK)))
#define SALUT_TRANSPORT_MIXIN(o) ((SalutTransportMixin *) mixin_offset_cast (o, SALUT_TRANSPORT_MIXIN_OFFSET (o)))

GQuark salut_transport_mixin_class_get_offset_quark (void);
GQuark salut_transport_mixin_get_offset_quark (void);

void salut_transport_mixin_class_init(GObjectClass *obj_cls, glong offset, 
                                  SalutTransportMixinSendFunc send,
                                  SalutTransportMixinDisconnectFunc disconnect);
void salut_transport_mixin_init (GObject *obj, glong offset);
void salut_transport_mixin_finalize (GObject *obj);

/* Utility functions for the mixin user */
void salut_transport_mixin_received_data(GObject *obj, 
                                         const guint8 *data, gsize length);
void salut_transport_mixin_set_state(GObject *obj, SalutTransportState state);
SalutTransportState salut_transport_mixin_get_state(GObject *obj);

void salut_transport_mixin_emit_connect_error (GObject *obj, GError *error);

void salut_transport_mixin_emit_send_error(GObject *obj, GError *error);

/* Public api */
gboolean salut_transport_send(GObject *object, const guint8 *data, 
                              gsize size, GError **error); 
void salut_transport_disconnect(GObject *object);

G_END_DECLS

#endif /* #ifndef __SALUT_TRANSPORT_MIXIN_H__ */
