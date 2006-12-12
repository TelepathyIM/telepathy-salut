/*
 * salut-muc-transport-iface.c - muc transport interface
 *
 * Copyright (C) 2006 Collabora Ltd.
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

#include "salut-muc-transport-iface.h"

#include "salut-connection.h"

static void
salut_muc_transport_iface_base_init (gpointer klass)
{
  static gboolean initialized = FALSE;

  if (!initialized) {
    GParamSpec *param_spec;

    initialized = TRUE;

    param_spec = g_param_spec_object ("connection", 
                                    "SalutConnection object",
                                    "Salut Connection that owns the"
                                    "connection for this IM channel",
                                    SALUT_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
    g_object_interface_install_property (klass, param_spec);

    param_spec = g_param_spec_string ("muc-name", 
                                      "Muc name ",
                                      "Muc name for the channel",
                                      NULL,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NICK |
                                      G_PARAM_STATIC_BLURB);
    g_object_interface_install_property (klass, param_spec);

    g_signal_new("message-received", 
                 G_OBJECT_CLASS_TYPE(klass),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__POINTER,
                 G_TYPE_NONE, 1, G_TYPE_POINTER);
    g_signal_new("connected", G_OBJECT_CLASS_TYPE(klass),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);
    g_signal_new("disconnected", G_OBJECT_CLASS_TYPE(klass),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);
  }
}

GType
salut_muc_transport_iface_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (SalutMucTransportIfaceClass),
      salut_muc_transport_iface_base_init,   /* base_init */
      NULL,   /* base_finalize */
      NULL,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      0,
      0,      /* n_preallocs */
      NULL    /* instance_init */
    };

    type = g_type_register_static (G_TYPE_INTERFACE, "SalutMucTransportIface", &info, 0);
  }

  return type;
}

gboolean 
salut_muc_transport_iface_connect(SalutMucTransportIface *iface, 
                                  GError **error) {
  SalutMucTransportIfaceClass *klass = 
    SALUT_MUC_TRANSPORT_IFACE_GET_CLASS(iface);
  g_assert(klass->connect != NULL);
  return klass->connect(iface, error);
}

void 
salut_muc_transport_iface_close(SalutMucTransportIface *iface) {
  SalutMucTransportIfaceClass *klass = 
    SALUT_MUC_TRANSPORT_IFACE_GET_CLASS(iface);
  g_assert(klass->close != NULL);
  return klass->close(iface);
}

gboolean 
salut_muc_transport_iface_send(SalutMucTransportIface *iface, 
                               LmMessage *message, 
                               GError **error, 
                               gint *text_error) {
  SalutMucTransportIfaceClass *klass = 
    SALUT_MUC_TRANSPORT_IFACE_GET_CLASS(iface);
  g_assert(klass->send != NULL);
  return klass->send(iface, message, error, text_error);
}

const gchar *
salut_muc_transport_get_protocol(SalutMucTransportIface *iface) {
  SalutMucTransportIfaceClass *klass = 
    SALUT_MUC_TRANSPORT_IFACE_GET_CLASS(iface);
  g_assert(klass->get_protocol != NULL);
  return klass->get_protocol(iface);
}

const GHashTable *
salut_muc_transport_get_parameters(SalutMucTransportIface *iface) { 
  SalutMucTransportIfaceClass *klass = 
    SALUT_MUC_TRANSPORT_IFACE_GET_CLASS(iface);
  g_assert(klass->get_parameters != NULL);
  return klass->get_parameters(iface);
}

void salut_muc_transport_iface_close(SalutMucTransportIface *iface); 
