#include "config.h"

#include "test.h"

#include <salut/plugin.h>

#include "extensions/extensions.h"

#define DEBUG(msg, ...) \
  g_debug ("%s: " msg, G_STRFUNC, ##__VA_ARGS__)

static void plugin_iface_init (
    gpointer g_iface,
    gpointer data);

#define IFACE_TEST "org.freedesktop.Telepathy.Salut.Plugin.Test"

static const gchar * const sidecar_interfaces[] = {
  IFACE_TEST,
  NULL
};

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

  /* If you wanted to add another protocol you could do it here by
   * creating the protocol object and then calling
   * tp_base_connection_manager_add_protocol(). */
}

static GPtrArray *
create_channel_managers (SalutPlugin *plugin,
    TpBaseConnection *connection)
{
  DEBUG ("%p on connection %p", plugin, connection);

  return NULL;
}

static void
test_plugin_create_sidecar (
    SalutPlugin *plugin,
    const gchar *sidecar_interface,
    SalutConnection *connection,
    WockySession *session,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (plugin),
      callback, user_data,
      /* sic: all plugins share salut_plugin_create_sidecar_finish() so we
       * need to use the same source tag.
       */
      salut_plugin_create_sidecar_async);
  SalutSidecar *sidecar = NULL;

  if (!tp_strdiff (sidecar_interface, IFACE_TEST))
    {
      sidecar = g_object_new (TEST_TYPE_SIDECAR, NULL);
    }
  else
    {
      g_simple_async_result_set_error (result, TP_ERRORS,
          TP_ERROR_NOT_IMPLEMENTED, "'%s' not implemented", sidecar_interface);
    }

  if (sidecar != NULL)
    g_simple_async_result_set_op_res_gpointer (result, sidecar, g_object_unref);

  g_simple_async_result_complete_in_idle (result);
  g_object_unref (result);
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

  iface->sidecar_interfaces = sidecar_interfaces;
  iface->create_sidecar = test_plugin_create_sidecar;
  iface->initialize = initialize;
  iface->create_channel_managers = create_channel_managers;
}

SalutPlugin *
salut_plugin_create ()
{
  return g_object_new (test_plugin_get_type (), NULL);
}

/******************************
 * TestSidecar implementation *
 ******************************/

static void sidecar_iface_init (
    gpointer g_iface,
    gpointer data);

G_DEFINE_TYPE_WITH_CODE (TestSidecar, test_sidecar, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (SALUT_TYPE_SIDECAR, sidecar_iface_init);
    G_IMPLEMENT_INTERFACE (SALUT_TYPE_SVC_SALUT_PLUGIN_TEST, NULL);
    )

static void
test_sidecar_init (TestSidecar *object)
{
  DEBUG ("%p", object);
}

static void
test_sidecar_class_init (TestSidecarClass *klass)
{
}

static void sidecar_iface_init (
    gpointer g_iface,
    gpointer data)
{
  SalutSidecarInterface *iface = g_iface;

  iface->interface = IFACE_TEST;
  iface->get_immutable_properties = NULL;
}
