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
#include "protocol.h"

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

const gchar * const *
salut_plugin_get_sidecar_interfaces (SalutPlugin *plugin)
{
  SalutPluginInterface *iface = SALUT_PLUGIN_GET_INTERFACE (plugin);

  return iface->sidecar_interfaces;
}

gboolean
salut_plugin_implements_sidecar (
    SalutPlugin *plugin,
    const gchar *sidecar_interface)
{
  SalutPluginInterface *iface = SALUT_PLUGIN_GET_INTERFACE (plugin);

  return tp_strv_contains (iface->sidecar_interfaces, sidecar_interface);
}

void
salut_plugin_create_sidecar_async (
    SalutPlugin *plugin,
    const gchar *sidecar_interface,
    SalutPluginConnection *connection,
    WockySession *session,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  SalutPluginInterface *iface = SALUT_PLUGIN_GET_INTERFACE (plugin);

  if (!salut_plugin_implements_sidecar (plugin, sidecar_interface))
    g_simple_async_report_error_in_idle (G_OBJECT (plugin), callback,
        user_data, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
        "Salut is buggy: '%s' doesn't implement sidecar %s",
        iface->name, sidecar_interface);
  else if (iface->create_sidecar_async == NULL)
    g_simple_async_report_error_in_idle (G_OBJECT (plugin), callback,
        user_data, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
        "'%s' is buggy: it claims to implement %s, but does not implement "
        "create_sidecar", iface->name, sidecar_interface);
  else if (iface->create_sidecar_finish == NULL)
    g_simple_async_report_error_in_idle (G_OBJECT (plugin), callback,
        user_data, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
        "'%s' is buggy: does not implement create_sidecar_finish",
        iface->name);
  else
    iface->create_sidecar_async (plugin, sidecar_interface, connection, session,
        callback, user_data);
}

SalutSidecar *
salut_plugin_create_sidecar_finish (
    SalutPlugin *plugin,
    GAsyncResult *result,
    GError **error)
{
  SalutPluginInterface *iface = SALUT_PLUGIN_GET_INTERFACE (plugin);
  SalutSidecar *sidecar;

  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
          error))
    return NULL;

  if (iface->create_sidecar_finish == NULL) {
      g_warning ("'%s' is buggy: does not implement create_sidecar_finish",
         iface->name);
      return NULL;
  }

  sidecar = iface->create_sidecar_finish (plugin, result, error);

  return g_object_ref (sidecar);
}

void
salut_plugin_initialize (SalutPlugin *plugin,
    TpBaseConnectionManager *connection_manager,
    const SalutPluginInitializeCallbacks *callbacks)
{
  SalutPluginInterface *iface = SALUT_PLUGIN_GET_INTERFACE (plugin);
  SalutPluginInitializeImpl func = iface->initialize;

  if (func != NULL)
    func (plugin, connection_manager, callbacks);
}

GPtrArray *
salut_plugin_create_channel_managers (SalutPlugin *plugin,
    SalutPluginConnection *plugin_connection)
{
  SalutPluginInterface *iface = SALUT_PLUGIN_GET_INTERFACE (plugin);
  SalutPluginCreateChannelManagersImpl func = iface->create_channel_managers;
  GPtrArray *out = NULL;

  if (func != NULL)
    out = func (plugin, plugin_connection);

  return out;
}

