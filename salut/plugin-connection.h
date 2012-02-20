/*
 * plugin-connection.h — Connection API available to telepathy-salut plugins
 * Copyright © 2012 Collabora Ltd.
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

#ifndef SALUT_PLUGIN_CONNECTION_H
#define SALUT_PLUGIN_CONNECTION_H

#include <glib-object.h>

#include <telepathy-glib/base-connection.h>
#include <telepathy-glib/base-contact-list.h>

#include <wocky/wocky.h>

G_BEGIN_DECLS

typedef struct _SalutPluginConnection SalutPluginConnection;
typedef struct _SalutPluginConnectionInterface SalutPluginConnectionInterface;

#define SALUT_TYPE_PLUGIN_CONNECTION (salut_plugin_connection_get_type ())
#define SALUT_PLUGIN_CONNECTION(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), SALUT_TYPE_PLUGIN_CONNECTION, \
                               SalutPluginConnection))
#define SALUT_IS_PLUGIN_CONNECTION(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), SALUT_TYPE_PLUGIN_CONNECTION))
#define SALUT_PLUGIN_CONNECTION_GET_IFACE(o) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((o), SALUT_TYPE_PLUGIN_CONNECTION, \
                                  SalutPluginConnectionInterface))

GType salut_plugin_connection_get_type (void) G_GNUC_CONST;

typedef WockySession * (*SalutPluginConnectionGetSessionFunc) (
    SalutPluginConnection *plugin_connection);

typedef const gchar * (*SalutPluginConnectionGetNameFunc) (
    SalutPluginConnection *plugin_connection);

struct _SalutPluginConnectionInterface
{
  GTypeInterface parent;
  SalutPluginConnectionGetSessionFunc get_session;
  SalutPluginConnectionGetNameFunc get_name;
};

WockySession *salut_plugin_connection_get_session (
    SalutPluginConnection *plugin_connection);

const gchar * salut_plugin_connection_get_name (
    SalutPluginConnection *plugin_connection);

G_END_DECLS

#endif
