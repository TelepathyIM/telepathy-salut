/*
 * channel-manager.h - factory and manager for channels relating to a
 *  particular protocol feature
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

#ifndef SALUT_CHANNEL_MANAGER_H
#define SALUT_CHANNEL_MANAGER_H

#include <glib-object.h>

#include "exportable-channel.h"

G_BEGIN_DECLS

#define SALUT_TYPE_CHANNEL_MANAGER (salut_channel_manager_get_type ())

#define SALUT_CHANNEL_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  SALUT_TYPE_CHANNEL_MANAGER, SalutChannelManager))

#define SALUT_IS_CHANNEL_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  SALUT_TYPE_CHANNEL_MANAGER))

#define SALUT_CHANNEL_MANAGER_GET_INTERFACE(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), \
  SALUT_TYPE_CHANNEL_MANAGER, SalutChannelManagerIface))

typedef struct _SalutChannelManager SalutChannelManager;
typedef struct _SalutChannelManagerIface SalutChannelManagerIface;


/* virtual methods */

typedef void (*SalutChannelManagerForeachChannelFunc) (
    SalutChannelManager *manager, SalutExportableChannelFunc func,
    gpointer user_data);

void salut_channel_manager_foreach_channel (SalutChannelManager *manager,
    SalutExportableChannelFunc func, gpointer user_data);


typedef void (*SalutChannelManagerChannelClassFunc) (
    SalutChannelManager *manager,
    GHashTable *fixed_properties,
    const gchar * const *allowed_properties,
    gpointer user_data);

typedef void (*SalutChannelManagerForeachChannelClassFunc) (
    SalutChannelManager *manager, SalutChannelManagerChannelClassFunc func,
    gpointer user_data);

void salut_channel_manager_foreach_channel_class (
    SalutChannelManager *manager,
    SalutChannelManagerChannelClassFunc func, gpointer user_data);


typedef gboolean (*SalutChannelManagerRequestFunc) (
    SalutChannelManager *manager, gpointer request_token,
    GHashTable *request_properties);

gboolean salut_channel_manager_create_channel (SalutChannelManager *manager,
    gpointer request_token, GHashTable *request_properties);

gboolean salut_channel_manager_request_channel (SalutChannelManager *manager,
    gpointer request_token, GHashTable *request_properties);


struct _SalutChannelManagerIface {
    GTypeInterface parent;

    SalutChannelManagerForeachChannelFunc foreach_channel;

    SalutChannelManagerForeachChannelClassFunc foreach_channel_class;

    SalutChannelManagerRequestFunc create_channel;
    SalutChannelManagerRequestFunc request_channel;
    /* in principle we could have EnsureChannel here too */

    GCallback _future[8];
    gpointer priv;
};


GType salut_channel_manager_get_type (void);


/* signal emission */

void salut_channel_manager_emit_new_channel (gpointer instance,
    SalutExportableChannel *channel, GSList *requests);
void salut_channel_manager_emit_new_channels (gpointer instance,
    GHashTable *channels);

void salut_channel_manager_emit_channel_closed (gpointer instance,
    const gchar *path);
void salut_channel_manager_emit_channel_closed_for_object (gpointer instance,
    SalutExportableChannel *channel);

void salut_channel_manager_emit_request_already_satisfied (
    gpointer instance, gpointer request_token,
    SalutExportableChannel *channel);

void salut_channel_manager_emit_request_failed (gpointer instance,
    gpointer request_token, GQuark domain, gint code, const gchar *message);
void salut_channel_manager_emit_request_failed_printf (gpointer instance,
    gpointer request_token, GQuark domain, gint code, const gchar *format,
    ...) G_GNUC_PRINTF (5, 6);

G_END_DECLS

#endif
