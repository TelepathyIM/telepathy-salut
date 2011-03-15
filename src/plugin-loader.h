/*
 * plugin-loader.h — plugin support for telepathy-salut
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
#ifndef __PLUGIN_LOADER_H__
#define __PLUGIN_LOADER_H__

#include <glib-object.h>

#include <telepathy-glib/base-connection-manager.h>
#include <telepathy-glib/base-connection.h>

#include <wocky/wocky-session.h>

#include "salut/sidecar.h"

typedef struct _SalutPluginLoader SalutPluginLoader;
typedef struct _SalutPluginLoaderClass SalutPluginLoaderClass;
typedef struct _SalutPluginLoaderPrivate SalutPluginLoaderPrivate;

struct _SalutPluginLoaderClass {
  GObjectClass parent_class;
};

struct _SalutPluginLoader {
  GObject parent;

  SalutPluginLoaderPrivate *priv;
};

GType salut_plugin_loader_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_PLUGIN_LOADER \
  (salut_plugin_loader_get_type ())
#define SALUT_PLUGIN_LOADER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_PLUGIN_LOADER, \
                              SalutPluginLoader))
#define SALUT_PLUGIN_LOADER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_PLUGIN_LOADER, \
                           SalutPluginLoaderClass))
#define SALUT_IS_PLUGIN_LOADER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_PLUGIN_LOADER))
#define SALUT_IS_PLUGIN_LOADER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_PLUGIN_LOADER))
#define SALUT_PLUGIN_LOADER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_PLUGIN_LOADER, \
                              SalutPluginLoaderClass))

SalutPluginLoader * salut_plugin_loader_dup (void);

void salut_plugin_loader_create_sidecar_async (
    SalutPluginLoader *self,
    const gchar *sidecar_interface,
    SalutConnection *connection,
    WockySession *session,
    GAsyncReadyCallback callback,
    gpointer user_data);

SalutSidecar *salut_plugin_loader_create_sidecar_finish (
    SalutPluginLoader *self,
    GAsyncResult *result,
    GError **error);

void salut_plugin_loader_initialize (
    SalutPluginLoader *self,
    TpBaseConnectionManager *connection_manager);

GPtrArray * salut_plugin_loader_create_channel_managers (
    SalutPluginLoader *self,
    TpBaseConnection *connection);

#endif /* #ifndef __PLUGIN_LOADER_H__ */
