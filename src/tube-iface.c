/*
 * tube-iface.c - Source for SalutTube interface
 * Copyright (C) 2007-2008 Collabora Ltd.
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

#include "tube-iface.h"

#include "salut-connection.h"

#include <glib.h>

gboolean
salut_tube_iface_accept (SalutTubeIface *self,
                         GError **error)
{
  gboolean (*virtual_method)(SalutTubeIface *, GError **) =
    SALUT_TUBE_IFACE_GET_CLASS (self)->accept;
  g_assert (virtual_method != NULL);
  return virtual_method (self, error);
}

gboolean
salut_tube_iface_offer_needed (SalutTubeIface *self)
{
  gboolean (*virtual_method)(SalutTubeIface *) =
    SALUT_TUBE_IFACE_GET_CLASS (self)->offer_needed;
  g_assert (virtual_method != NULL);
  return virtual_method (self);
}

void
salut_tube_iface_close (SalutTubeIface *self)
{
  void (*virtual_method)(SalutTubeIface *) =
    SALUT_TUBE_IFACE_GET_CLASS (self)->close;
  g_assert (virtual_method != NULL);
  virtual_method (self);
}

void
salut_tube_iface_add_bytestream (SalutTubeIface *self,
                                 GibberBytestreamIface *bytestream)
{
  void (*virtual_method)(SalutTubeIface *, GibberBytestreamIface *) =
    SALUT_TUBE_IFACE_GET_CLASS (self)->add_bytestream;
  g_assert (virtual_method != NULL);
  virtual_method (self, bytestream);
}

static void
salut_tube_iface_base_init (gpointer klass)
{
  static gboolean initialized = FALSE;

  if (!initialized)
    {
      GParamSpec *param_spec;

      param_spec = g_param_spec_object (
          "connection",
          "SalutConnection object",
          "Salut connection object that owns this D-Bus tube object.",
          SALUT_TYPE_CONNECTION,
          G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

     param_spec = g_param_spec_uint (
          "handle",
          "Handle",
          "The TpHandle associated with the tubes channel that"
          "owns this D-Bus tube object.",
          0, G_MAXUINT32, 0,
          G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_uint (
          "handle-type",
          "Handle type",
          "The TpHandleType of the handle associated with the tubes channel"
          "that owns this D-Bus tube object.",
          0, G_MAXUINT32, 0,
          G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_uint (
          "self-handle",
          "Self handle",
          "The handle to use for ourself. This can be different from the "
          "connection's self handle if our handle is a room handle.",
          0, G_MAXUINT, 0,
          G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_uint (
          "id",
          "id",
          "The unique identifier of this tube",
          0, G_MAXUINT32, 0,
          G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_uint (
          "type",
          "Tube type",
          "The TpTubeType this D-Bus tube object.",
          0, G_MAXUINT32, 0,
          G_PARAM_READABLE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_uint (
          "initiator",
          "Initiator handle",
          "The TpHandle of the initiator of this D-Bus tube object.",
          0, G_MAXUINT32, 0,
          G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_string (
          "service",
          "service name",
          "the service associated with this D-BUS tube object.",
          "",
          G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_boxed (
          "parameters",
          "parameters GHashTable",
          "GHashTable containing parameters of this DBUS tube object.",
          G_TYPE_HASH_TABLE,
          G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_READWRITE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_uint (
          "state",
          "Tube state",
          "The SalutTubeState of this DBUS tube object",
          0, G_MAXUINT32, TP_TUBE_STATE_REMOTE_PENDING,
          G_PARAM_READABLE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      initialized = TRUE;
    }
}

GType
salut_tube_iface_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (SalutTubeIfaceClass),
      salut_tube_iface_base_init,   /* base_init */
      NULL,   /* base_finalize */
      NULL,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      0,
      0,      /* n_preallocs */
      NULL    /* instance_init */
    };

    type = g_type_register_static (G_TYPE_INTERFACE, "SalutTubeIface",
        &info, 0);
  }

  return type;
}
