#include "config.h"

#include "test.h"

#include <salut/plugin.h>

#define DEBUG(msg, ...) \
  g_debug ("%s: " msg, G_STRFUNC, ##__VA_ARGS__)

static void plugin_iface_init (
    gpointer g_iface,
    gpointer data);

G_DEFINE_TYPE_WITH_CODE (TestPlugin, test_plugin, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (SALUT_TYPE_PLUGIN, plugin_iface_init);
    )

static void
test_plugin_init (TestPlugin *object)
{
  DEBUG ("%p", object);
}

static void
test_plugin_class_init (TestPluginClass *klass)
{
}

static void
initialize (SalutPlugin *plugin,
    TpBaseConnectionManager *connection_manager)
{
  DEBUG ("%p on connection manager %p", plugin, connection_manager);
}

static GPtrArray *
create_channel_managers (SalutPlugin *plugin,
    TpBaseConnection *connection)
{
  DEBUG ("%p on connection %p", plugin, connection);

  return NULL;
}

static void
plugin_iface_init (
    gpointer g_iface,
    gpointer data G_GNUC_UNUSED)
{
  SalutPluginInterface *iface = g_iface;

  iface->api_version = SALUT_PLUGIN_CURRENT_VERSION;
  iface->name = "Salut test plugin";
  iface->version = PACKAGE_VERSION;

  iface->initialize = initialize;
  iface->create_channel_managers = create_channel_managers;
}

SalutPlugin *
salut_plugin_create ()
{
  return g_object_new (test_plugin_get_type (), NULL);
}
