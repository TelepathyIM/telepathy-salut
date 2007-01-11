/*
 * salut-transport-mixin.c - Source for SalutTransportMixin
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

#include "glib.h"
#include "util.h"

#include "salut-transport-mixin.h"
#include "salut-transport-mixin-signals-marshal.h"

/**
 * salut_transport_mixin_class_get_offset_quark:
 *
 * Returns: the quark used for storing mixin offset on a GObjectClass
 */
GQuark
salut_transport_mixin_class_get_offset_quark ()
{
  static GQuark offset_quark = 0;
  if (!offset_quark)
    offset_quark = g_quark_from_static_string("SalutTransportMixinClassOffsetQuark");
  return offset_quark;
}

/**
 * salut_transport_mixin_get_offset_quark:
 *
 * Returns: the quark used for storing mixin offset on a GObject
 */
GQuark
salut_transport_mixin_get_offset_quark ()
{
  static GQuark offset_quark = 0;
  if (!offset_quark)
    offset_quark = g_quark_from_static_string("SalutTransportMixinOffsetQuark");
  return offset_quark;
}


/* SalutTransportMixin */
void
salut_transport_mixin_class_init (GObjectClass *obj_cls, glong offset, 
                       SalutTransportMixinSendFunc send,
                       SalutTransportMixinDisconnectFunc disconnect) {
  SalutTransportMixinClass *mixin_cls;

  g_assert (G_IS_OBJECT_CLASS (obj_cls));

  g_type_set_qdata (G_OBJECT_CLASS_TYPE (obj_cls),
      SALUT_TRANSPORT_MIXIN_CLASS_OFFSET_QUARK,
      GINT_TO_POINTER (offset));

  mixin_cls = SALUT_TRANSPORT_MIXIN_CLASS (obj_cls);

  mixin_cls->connected_signal_id = 
    g_signal_new ("connected",
                  G_OBJECT_CLASS_TYPE (obj_cls),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  mixin_cls->connect_error_signal_id = 
    g_signal_new ("connect-error",
                  G_OBJECT_CLASS_TYPE (obj_cls),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_transport_mixin_marshal_VOID__UINT_INT_STRING,
                  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_INT, G_TYPE_STRING);

  mixin_cls->disconnected_signal_id = 
    g_signal_new ("disconnected",
                  G_OBJECT_CLASS_TYPE (obj_cls),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  /* FIXME passing a gzise as ulong */
  mixin_cls->connect_error_signal_id = 
    g_signal_new ("received",
                  G_OBJECT_CLASS_TYPE (obj_cls),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_transport_mixin_marshal_VOID__POINTER_ULONG,
                  G_TYPE_NONE, 6, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  mixin_cls->error_signal_id = 
    g_signal_new ("error",
                  G_OBJECT_CLASS_TYPE (obj_cls),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_transport_mixin_marshal_VOID__UINT_INT_STRING,
                  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_INT, G_TYPE_STRING);

  mixin_cls->send = send;
  mixin_cls->disconnect = disconnect;
}

void
salut_transport_mixin_init (GObject *obj,
                        glong offset) {
  SalutTransportMixin *mixin;

  g_assert (G_IS_OBJECT (obj));

  g_type_set_qdata (G_OBJECT_TYPE (obj),
                    SALUT_TRANSPORT_MIXIN_OFFSET_QUARK,
                    GINT_TO_POINTER (offset));

  mixin = SALUT_TRANSPORT_MIXIN (obj);
}

void
salut_transport_mixin_finalize (GObject *obj)
{
  return;

}

void
salut_transport_mixin_received_data(GObject *obj, 
                                    const guint8 *data, gsize length) {
  SalutTransportMixinClass *cls = 
    SALUT_TRANSPORT_MIXIN_CLASS(G_OBJECT_GET_CLASS(obj));
  g_signal_emit(obj, cls->received_signal_id, 0, data, length);
}

void 
salut_transport_mixin_emit_connected (GObject *obj) {
}

#define EMIT_ERROR(obj, signal, error)                                                \
  do {                                                                        \
    SalutTransportMixinClass *cls =                                           \
      SALUT_TRANSPORT_MIXIN_CLASS(G_OBJECT_GET_CLASS(obj));                   \
    g_signal_emit(obj, cls->signal##_signal_id, 0,                            \
                  error->domain, error->code, error->message);                \
  } while (0)                                                                 

void 
salut_transport_mixin_emit_connect_error (GObject *obj, GError *error) {
  EMIT_ERROR(obj, connect_error, error);
}

void salut_transport_mixin_emit_error(GObject *obj, GError *error) {
  EMIT_ERROR(obj, error, error);

void salut_transport_mixin_emit_disconnected (GObject *obj);
  SalutTransportMixinClass *cls = 
    SALUT_TRANSPORT_MIXIN_CLASS(G_OBJECT_GET_CLASS(obj));
  g_signal_emit(obj, cls->received_signal_id, 0);
}

gboolean 
salut_transport_send(GObject *object, const guint8 *data, gsize size, 
                     GError **error) {
  SalutTransportMixinClass *cls = 
    SALUT_TRANSPORT_MIXIN_CLASS(G_OBJECT_GET_CLASS(object));
  return cls->send(object, data, size, error);
}

void 
salut_transport_disconnect(GObject *object) {
  SalutTransportMixinClass *cls = 
    SALUT_TRANSPORT_MIXIN_CLASS(G_OBJECT_GET_CLASS(object));
  return cls->disconnect(object);
}
