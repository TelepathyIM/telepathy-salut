/*
 * plugin-connection.c — API for telepathy-salut plugins
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

#include "config.h"
#include "salut/plugin-connection.h"

#include <glib-object.h>

#include <telepathy-glib/telepathy-glib.h>

#include <debug.h>

/**
 * SECTION: salut-plugin-connection
 * @title: SalutPluginConnection
 * @short_description: Object representing salut connection, implemented by
 * Salut internals.
 *
 * This Object represents Salut Connection.
 *
 * Virtual methods in SalutPluginConnectionInterface interface are implemented
 * by SalutConnection object. And only Salut should implement this interface.
 */
G_DEFINE_INTERFACE (SalutPluginConnection,
    salut_plugin_connection,
    G_TYPE_OBJECT);

static void
salut_plugin_connection_default_init (SalutPluginConnectionInterface *iface)
{
}

WockySession *
salut_plugin_connection_get_session (
    SalutPluginConnection *plugin_connection)
{
  SalutPluginConnectionInterface *iface =
    SALUT_PLUGIN_CONNECTION_GET_IFACE (plugin_connection);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_session != NULL, NULL);

  return iface->get_session (plugin_connection);
}

const gchar *
salut_plugin_connection_get_name (
    SalutPluginConnection *plugin_connection)
{
  SalutPluginConnectionInterface *iface =
    SALUT_PLUGIN_CONNECTION_GET_IFACE (plugin_connection);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_name != NULL, NULL);

  return iface->get_name (plugin_connection);
}
