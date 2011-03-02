/*
 * plugin.c — API for telepathy-salut plugins
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

#include "salut/plugin.h"

#include <telepathy-glib/util.h>

#define DEBUG_FLAG DEBUG_PLUGINS
#include "debug.h"

G_DEFINE_INTERFACE (SalutPlugin, salut_plugin, G_TYPE_OBJECT)

static void
salut_plugin_default_init (SalutPluginInterface *iface)
{
}

const gchar *
salut_plugin_get_name (SalutPlugin *plugin)
{
  SalutPluginInterface *iface = SALUT_PLUGIN_GET_INTERFACE (plugin);

  return iface->name;
}

const gchar *
salut_plugin_get_version (SalutPlugin *plugin)
{
  SalutPluginInterface *iface = SALUT_PLUGIN_GET_INTERFACE (plugin);

  return iface->version;
}

void
salut_plugin_initialize (SalutPlugin *plugin,
    TpBaseConnectionManager *connection_manager)
{
  SalutPluginInterface *iface = SALUT_PLUGIN_GET_INTERFACE (plugin);
  SalutPluginInitializeImpl func = iface->initialize;

  if (func != NULL)
    func (plugin, connection_manager);
}

GPtrArray *
salut_plugin_create_channel_managers (SalutPlugin *plugin,
    TpBaseConnection *connection)
{
  SalutPluginInterface *iface = SALUT_PLUGIN_GET_INTERFACE (plugin);
  SalutPluginCreateChannelManagersImpl func = iface->create_channel_managers;
  GPtrArray *out = NULL;

  if (func != NULL)
    out = func (plugin, connection);

  return out;
}

