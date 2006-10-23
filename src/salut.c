#include "config.h"

#include "debug.h"
#include <glib.h>
#include <dbus/dbus-glib.h>

#include <avahi-glib/glib-watch.h>
#include <avahi-glib/glib-malloc.h>

#include "telepathy-errors.h"
#include "telepathy-errors-enumtypes.h"
#include "salut-connection-manager.h"

GMainLoop *mainloop = NULL;
SalutConnectionManager *manager = NULL;

int
main(int argc, char **argv) {
  g_type_init();
  g_set_prgname("telepathy-salut");

  debug_set_flags_from_env();
  mainloop = g_main_loop_new (NULL, FALSE);

  dbus_g_error_domain_register(TELEPATHY_ERRORS,
    "org.freedesktop.Telepathy.Error", TELEPATHY_TYPE_ERRORS);

  manager = g_object_new(SALUT_TYPE_CONNECTION_MANAGER, NULL);

  _salut_connection_manager_register(manager);

  g_main_loop_run(mainloop);

  return 0;
}


