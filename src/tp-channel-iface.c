/*
 * tp-channel-iface.c - Stubs for Telepathy Channel interface
 *
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

#include "tp-channel-iface.h"

static void
tp_channel_iface_base_init (gpointer klass)
{
  static gboolean initialized = FALSE;

  if (!initialized) {
    GParamSpec *param_spec;

    initialized = TRUE;

    param_spec = g_param_spec_string ("object-path", "D-Bus object path",
                                      "The D-Bus object path used for this "
                                      "object on the bus.",
                                      NULL,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
    g_object_interface_install_property (klass, param_spec);

    param_spec = g_param_spec_string ("channel-type", "Telepathy channel type",
                                      "The D-Bus interface representing the "
                                      "type of this channel.",
                                      NULL,
                                      G_PARAM_READABLE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
    g_object_interface_install_property (klass, param_spec);

    param_spec = g_param_spec_uint ("handle-type", "Contact handle type",
                                    "The TpHandleType representing a "
                                    "contact handle.",
                                    0, G_MAXUINT32, 0,
                                    G_PARAM_READABLE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
    g_object_interface_install_property (klass, param_spec);

    param_spec = g_param_spec_uint ("handle", "Contact handle",
                                    "The GabbleHandle representing the contact "
                                    "with whom this channel communicates.",
                                    0, G_MAXUINT32, 0,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
    g_object_interface_install_property (klass, param_spec);
  }
}

GType
tp_channel_iface_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (TpChannelIfaceClass),
      tp_channel_iface_base_init,   /* base_init */
      NULL,   /* base_finalize */
      NULL,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      0,
      0,      /* n_preallocs */
      NULL    /* instance_init */
    };

    type = g_type_register_static (G_TYPE_INTERFACE, "TpChannelIface", &info, 0);
  }

  return type;
}
