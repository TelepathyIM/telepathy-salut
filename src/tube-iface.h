/*
 * tube-iface.h - Header for SalutTube interface
 * Copyright (C) 2007 Collabora Ltd.
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

#ifndef __SALUT_TUBE_IFACE_H__
#define __SALUT_TUBE_IFACE_H__

#include <glib-object.h>

#include <gibber/gibber-bytestream-iface.h>

G_BEGIN_DECLS

typedef struct _SalutTubeIface SalutTubeIface;
typedef struct _SalutTubeIfaceClass SalutTubeIfaceClass;

struct _SalutTubeIfaceClass {
  GTypeInterface parent;

  gboolean (*accept) (SalutTubeIface *tube, GError **error);
  gboolean (*offer_needed) (SalutTubeIface *tube);
  void (*close) (SalutTubeIface *tube);
  void (*add_bytestream) (SalutTubeIface *tube,
      GibberBytestreamIface *bytestream);
};

GType salut_tube_iface_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_TUBE_IFACE \
  (salut_tube_iface_get_type ())
#define SALUT_TUBE_IFACE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_TUBE_IFACE, SalutTubeIface))
#define SALUT_IS_TUBE_IFACE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_TUBE_IFACE))
#define SALUT_TUBE_IFACE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), SALUT_TYPE_TUBE_IFACE,\
                              SalutTubeIfaceClass))

/* return TRUE if the <iq> to offer the tube has never been sent */
gboolean
salut_tube_iface_offer_needed (SalutTubeIface *tube);

gboolean
salut_tube_iface_accept (SalutTubeIface *tube, GError **error);

void
salut_tube_iface_close (SalutTubeIface *tube);

void
salut_tube_iface_add_bytestream (SalutTubeIface *tube,
    GibberBytestreamIface *bytestream);

G_END_DECLS

#endif /* #ifndef __SALUT_TUBE_IFACE_H__ */
