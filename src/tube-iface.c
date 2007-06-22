/*
 * tube-iface.c - Source for SalutTube interface
 * Copyright (C) 2007 Ltd.
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

#include <glib.h>

void
salut_tube_iface_accept (SalutTubeIface *self)
{
  void (*virtual_method)(SalutTubeIface *) =
    SALUT_TUBE_IFACE_GET_CLASS (self)->accept;
  g_assert (virtual_method != NULL);
  virtual_method (self);
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
                                 GibberBytestreamIBB *bytestream)
{
  void (*virtual_method)(SalutTubeIface *, GibberBytestreamIBB *) =
    SALUT_TUBE_IFACE_GET_CLASS (self)->add_bytestream;
  g_assert (virtual_method != NULL);
  virtual_method (self, bytestream);
}

GType
salut_tube_iface_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (SalutTubeIfaceClass),
      NULL,   /* base_init */
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
