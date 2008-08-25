/*
 * exportable-channel.h - A channel usable with the Channel Manager
 *
 * Copyright (C) 2008 Collabora Ltd.
 * Copyright (C) 2008 Nokia Corporation
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

#ifndef SALUT_EXPORTABLE_CHANNEL_H
#define SALUT_EXPORTABLE_CHANNEL_H

#include <glib-object.h>

G_BEGIN_DECLS

#define SALUT_TYPE_EXPORTABLE_CHANNEL (salut_exportable_channel_get_type ())

#define SALUT_EXPORTABLE_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  SALUT_TYPE_EXPORTABLE_CHANNEL, SalutExportableChannel))

#define SALUT_IS_EXPORTABLE_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  SALUT_TYPE_EXPORTABLE_CHANNEL))

#define SALUT_EXPORTABLE_CHANNEL_GET_INTERFACE(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), \
  SALUT_TYPE_EXPORTABLE_CHANNEL, SalutExportableChannelIface))

typedef struct _SalutExportableChannel SalutExportableChannel;
typedef struct _SalutExportableChannelIface SalutExportableChannelIface;

typedef void (*SalutExportableChannelFunc) (SalutExportableChannel *channel,
    gpointer user_data);

struct _SalutExportableChannelIface {
    GTypeInterface parent;
};

GType salut_exportable_channel_get_type (void);

GHashTable *salut_tp_dbus_properties_mixin_make_properties_hash (
    GObject *object, const gchar *first_interface,
    const gchar *first_property, ...) G_GNUC_NULL_TERMINATED;

G_END_DECLS

#endif
