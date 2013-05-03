#include "config.h"

#include <glib.h>

#include <telepathy-glib/telepathy-glib.h>

#include "connection-manager.h"

#ifdef USE_BACKEND_AVAHI
#include "avahi-discovery-client.h"
#elif defined (USE_BACKEND_DUMMY)
#include "dummy-discovery-client.h"
#elif defined (USE_BACKEND_BONJOUR)
#include "bonjour-discovery-client.h"
#endif

#include "debug.h"
#include "plugin-loader.h"
#include "symbol-hacks.h"

static TpBaseConnectionManager *
salut_create_connection_manager (void)
{
  return TP_BASE_CONNECTION_MANAGER (
      g_object_new (SALUT_TYPE_CONNECTION_MANAGER,
                    "backend-type",
#ifdef USE_BACKEND_AVAHI
                    SALUT_TYPE_AVAHI_DISCOVERY_CLIENT,
#elif defined (USE_BACKEND_DUMMY)
                    SALUT_TYPE_DUMMY_DISCOVERY_CLIENT,
#elif defined (USE_BACKEND_BONJOUR)
                    SALUT_TYPE_BONJOUR_DISCOVERY_CLIENT,
#endif
                    NULL));
}

int
#ifdef BUILD_AS_ANDROID_SERVICE
salut_main (int argc, char **argv)
#else
main (int argc, char **argv)
#endif
{
  GLogLevelFlags fatal_mask;
  gint ret;
  SalutPluginLoader *loader;

  g_type_init ();

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


