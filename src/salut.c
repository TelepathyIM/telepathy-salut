#include "config.h"

#include <glib.h>

#include <telepathy-glib/run.h>
#include <telepathy-glib/debug.h>

#include "connection-manager.h"
#include "avahi-discovery-client.h"
#include "debug.h"
#include "plugin-loader.h"
#include "symbol-hacks.h"

static TpBaseConnectionManager *
salut_create_connection_manager (void)
{
  return TP_BASE_CONNECTION_MANAGER (
      g_object_new (SALUT_TYPE_CONNECTION_MANAGER,
                    "backend-type", SALUT_TYPE_AVAHI_DISCOVERY_CLIENT,
                    NULL));
}

int
main (int argc, char **argv)
{
  GLogLevelFlags fatal_mask;
  gint ret;
  SalutPluginLoader *loader;

  g_type_init ();
  g_thread_init (NULL);

  salut_symbol_hacks ();

  /* treat criticals as, well, critical */
  fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
  fatal_mask |= G_LOG_LEVEL_CRITICAL;
  g_log_set_always_fatal (fatal_mask);

#ifdef ENABLE_DEBUG
  tp_debug_divert_messages (g_getenv ("SALUT_LOGFILE"));
  debug_set_flags_from_env ();

  if (g_getenv ("SALUT_PERSIST"))
    tp_debug_set_persistent (TRUE);
#endif

  loader = salut_plugin_loader_dup ();

  ret = tp_run_connection_manager ("telepathy-salut", VERSION,
                                    salut_create_connection_manager,
                                    argc, argv);

  g_object_unref (loader);

  return ret;
}


