#include "config.h"

#include <glib.h>
#include <dbus/dbus-glib.h>

#include <avahi-glib/glib-watch.h>
#include <avahi-glib/glib-malloc.h>

#include "telepathy-errors.h"
#include "telepathy-errors-enumtypes.h"
#include "salut-connection-manager.h"
#include "debug.h"

GMainLoop *mainloop = NULL;
SalutConnectionManager *manager = NULL;
guint timeout_id;

#define DIE_TIME 5000

static gboolean
kill_connection_manager(gpointer data) {
  if (!debug_flag_is_set(DEBUG_PERSIST)) {
    g_debug("No more connections, time to die");
    g_object_unref(manager);
    g_main_loop_quit(mainloop);
  } else {
    g_debug("Would have exited if the persisted flag hadn't been set");
  }
  timeout_id = 0;
  return FALSE;
}

static void 
new_connection(SalutConnectionManager *conn, gchar *bus_name, 
                 gchar *object_path, gchar *proto) {
  if (timeout_id != 0) {
    g_source_remove(timeout_id);
  }
}

static void
no_more_connections(SalutConnectionManager *manager, gpointer data) {
  if (timeout_id != 0) {
    g_source_remove(timeout_id);
  }
  timeout_id = g_timeout_add(DIE_TIME, kill_connection_manager, NULL);
}

int
main(int argc, char **argv) {
  g_type_init();
  g_set_prgname("telepathy-salut");

  debug_set_flags_from_env();
  mainloop = g_main_loop_new (NULL, FALSE);

  dbus_g_error_domain_register(TELEPATHY_ERRORS,
    "org.freedesktop.Telepathy.Error", TELEPATHY_TYPE_ERRORS);

  timeout_id = g_timeout_add(DIE_TIME, kill_connection_manager, NULL);

  manager = g_object_new(SALUT_TYPE_CONNECTION_MANAGER, NULL);
  g_signal_connect(manager, "new-connection", 
                    G_CALLBACK(new_connection), NULL);
  g_signal_connect(manager, "no-more-connections", 
                    G_CALLBACK(no_more_connections), NULL);

  _salut_connection_manager_register(manager);

  g_main_loop_run(mainloop);

  return 0;
}


