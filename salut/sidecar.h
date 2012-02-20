/*
 * sidecar.h — sidecar API available to telepathy-salut plugins
 * Copyright © 2009-2011 Collabora Ltd.
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

#ifndef SALUT_PLUGINS_SIDECAR_H
#define SALUT_PLUGINS_SIDECAR_H

#include <glib-object.h>


G_BEGIN_DECLS

#define SALUT_TYPE_SIDECAR (salut_sidecar_get_type ())
#define SALUT_SIDECAR(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), SALUT_TYPE_SIDECAR, SalutSidecar))
#define SALUT_IS_SIDECAR(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SALUT_TYPE_SIDECAR))
#define SALUT_SIDECAR_GET_INTERFACE(obj) \
    (G_TYPE_INSTANCE_GET_INTERFACE ((obj), SALUT_TYPE_SIDECAR, \
        SalutSidecarInterface))

typedef struct _SalutSidecar SalutSidecar;
typedef struct _SalutSidecarInterface SalutSidecarInterface;

typedef GHashTable * (*SalutSidecarGetImmutablePropertiesImpl) (
    SalutSidecar *);

struct _SalutSidecarInterface
{
  GTypeInterface parent;

  /**
   * The D-Bus interface implemented by this sidecar.
   */
  const gchar *interface;

  /**
   * An implementation of salut_sidecar_get_immutable_properties().
   */
  SalutSidecarGetImmutablePropertiesImpl get_immutable_properties;
};

GType salut_sidecar_get_type (void);

const gchar * salut_sidecar_get_interface (SalutSidecar *sidecar);
GHashTable * salut_sidecar_get_immutable_properties (SalutSidecar *sidecar);

G_END_DECLS

#endif
