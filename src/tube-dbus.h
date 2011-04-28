/*
 * tube-dbus.h - Header for SalutTubeDBus
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

#ifndef __SALUT_TUBE_DBUS_H__
#define __SALUT_TUBE_DBUS_H__

#include <glib-object.h>

#include <telepathy-glib/enums.h>
#include <telepathy-glib/interfaces.h>

#include "connection.h"
#include "tubes-channel.h"
#include <gibber/gibber-muc-connection.h>
#include <gibber/gibber-bytestream-iface.h>

G_BEGIN_DECLS

typedef struct _SalutTubeDBus SalutTubeDBus;
typedef struct _SalutTubeDBusClass SalutTubeDBusClass;

struct _SalutTubeDBusClass {
  GObjectClass parent_class;

  TpDBusPropertiesMixinClass dbus_props_class;
};

struct _SalutTubeDBus {
  GObject parent;

  gpointer priv;
};

GType salut_tube_dbus_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_TUBE_DBUS \
  (salut_tube_dbus_get_type ())
#define SALUT_TUBE_DBUS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_TUBE_DBUS, SalutTubeDBus))
#define SALUT_TUBE_DBUS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_TUBE_DBUS,\
                           SalutTubeDBusClass))
#define SALUT_IS_TUBE_DBUS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_TUBE_DBUS))
#define SALUT_IS_TUBE_DBUS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_TUBE_DBUS))
#define SALUT_TUBE_DBUS_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_TUBE_DBUS,\
                              SalutTubeDBusClass))

SalutTubeDBus *
salut_tube_dbus_new (SalutConnection *conn, SalutTubesChannel *tubes_channel,
    TpHandle handle, TpHandleType handle_type, TpHandle self_handle,
    GibberMucConnection *muc_connection, TpHandle initiator,
    const gchar *service, GHashTable *parameters, guint id);

gboolean salut_tube_dbus_add_name (SalutTubeDBus *self, TpHandle handle,
    const gchar *name);

gboolean salut_tube_dbus_remove_name (SalutTubeDBus *self, TpHandle handle);

gboolean salut_tube_dbus_handle_in_names (SalutTubeDBus *self,
    TpHandle handle);

gboolean salut_tube_dbus_offer (SalutTubeDBus *self, GError **error);

const gchar * const * salut_tube_dbus_channel_get_allowed_properties (void);

G_END_DECLS

#endif /* #ifndef __SALUT_TUBE_DBUS_H__ */
