#include "config.h"

#include <glib.h>

#include <telepathy-glib/run.h>
#include <telepathy-glib/debug.h>

#include "connection-manager.h"
#include "dummy-discovery-client.h"
#include "debug.h"

static TpBaseConnectionManager *
salut_create_connection_manager (void)
{
  return TP_BASE_CONNECTION_MANAGER (
      g_object_new (SALUT_TYPE_CONNECTION_MANAGER,
                    "backend-type", SALUT_TYPE_DUMMY_DISCOVERY_CLIENT,
                    NULL));
}

int
main (int argc, char **argv)
{
  g_type_init ();
  g_set_prgname ("telepathy-salut");

#ifdef ENABLE_DEBUG
  tp_debug_divert_messages (g_getenv ("SALUT_LOGFILE"));
  debug_set_flags_from_env ();

  if (g_getenv ("SALUT_PERSIST"))
    tp_debug_set_persistent (TRUE);
#endif

  return tp_run_connection_manager ("telepathy-salut", VERSION,
                                    salut_create_connection_manager,
                                    argc, argv);
}


