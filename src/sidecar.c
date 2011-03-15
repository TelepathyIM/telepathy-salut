/*
 * sidecar.c — interface for connection sidecars
 * Copyright © 2009 Collabora Ltd.
 * Copyright © 2009 Nokia Corporation
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

#include "salut/sidecar.h"

G_DEFINE_INTERFACE (SalutSidecar, salut_sidecar, G_TYPE_OBJECT)

static void
salut_sidecar_default_init (SalutSidecarInterface *iface)
{
}

const gchar *
salut_sidecar_get_interface (SalutSidecar *sidecar)
{
  SalutSidecarInterface *iface = SALUT_SIDECAR_GET_INTERFACE (sidecar);

  return iface->interface;
}

GHashTable *
salut_sidecar_get_immutable_properties (SalutSidecar *sidecar)
{
  SalutSidecarInterface *iface = SALUT_SIDECAR_GET_INTERFACE (sidecar);

  if (iface->get_immutable_properties)
    return iface->get_immutable_properties (sidecar);
  else
    return g_hash_table_new (NULL, NULL);
}
