#include "config.h"

#include <glib.h>

#include <telepathy-glib/run.h>
#include <telepathy-glib/debug.h>

#include "salut-connection-manager.h"
#include "debug.h"

static TpBaseConnectionManager *
salut_create_connection_manager (void)
{
  return TP_BASE_CONNECTION_MANAGER (
      g_object_new (SALUT_TYPE_CONNECTION_MANAGER, NULL));
}

int
main (int argc, char **argv)
{
  g_type_init ();
  g_set_prgname ("telepathy-salut");

  debug_set_log_file_from_env ();
  debug_set_flags_from_env ();

  if (g_getenv ("SALUT_PERSIST"))
    tp_debug_set_persistent (TRUE);

  return tp_run_connection_manager ("telepathy-salut", VERSION,
                                    salut_create_connection_manager,
                                    argc, argv);
}


