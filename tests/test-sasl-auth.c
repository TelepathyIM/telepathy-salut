#include <stdio.h>

#include "test-sasl-auth-server.h"
#include "test-transport.h"
#include <gibber/gibber-xmpp-connection.h>
#include <gibber/gibber-sasl-auth.h>

typedef struct {
  gchar *description; 
  gchar *mech;
  gboolean allow_plain;
  GQuark domain;
  int code;
  ServerProblem problem;
} test_t; 

GMainLoop *mainloop;
GibberTransport *servertransport;
GibberTransport *clienttransport;
GibberXmppConnection *conn;
GibberSaslAuth *sasl = NULL;

const gchar *username = "test";
const gchar *password = "test123";
const gchar *servername = "testserver";

gboolean authenticated = FALSE;
gboolean run_done = FALSE;

test_t *current_test = NULL;
GError *error = NULL;

static void
got_error(GQuark domain, int code, gchar *message) {
  g_set_error(&error, domain, code, message);
  run_done = TRUE;
  g_main_loop_quit(mainloop);
}

gboolean
send_hook(GibberTransport *transport, const guint8 *data, 
   gsize length, GError **error) {
  GibberTransport *target = 
     (servertransport == transport) ? clienttransport : servertransport;

  test_transport_write(TEST_TRANSPORT(target), data, length);
  return TRUE;
}

gchar *
return_str(GibberSaslAuth *auth, gpointer user_data) {
  return g_strdup(user_data);
}

void
auth_success(GibberSaslAuth *sasl, gpointer user_data) {
  authenticated = TRUE;
  /* Reopen the connection */
  gibber_xmpp_connection_open(conn, servername, NULL, "1.0");
}

void
auth_failed(GibberSaslAuth *sasl, GQuark domain,
    int code, gchar *message, gpointer user_data) {
  got_error(domain, code, message);
}

static void
parse_error(GibberXmppConnection *connection, gpointer user_data) {
  g_assert_not_reached();
}

static void
stream_opened(GibberXmppConnection *connection, 
              gchar *from, gchar *to, gchar *version, gpointer user_data) {
  if (authenticated)
    gibber_xmpp_connection_close(conn); 
}

static void
stream_closed(GibberXmppConnection *connection, gpointer user_data) {
  run_done = TRUE;
  g_main_loop_quit(mainloop);
}

static void
received_stanza(GibberXmppConnection *connection, GibberXmppStanza *stanza, 
   gpointer user_data)
{ 
  if (sasl == NULL) {
    GError *error = NULL;
    sasl = gibber_sasl_auth_new();

    g_signal_connect(sasl, "username-requested",
                      G_CALLBACK(return_str), (gpointer)username);
    g_signal_connect(sasl, "password-requested",
                      G_CALLBACK(return_str), (gpointer)password);
    g_signal_connect(sasl, "authentication-succeeded",
                      G_CALLBACK(auth_success), NULL);
    g_signal_connect(sasl, "authentication-failed",
                      G_CALLBACK(auth_failed), NULL);

    if (!gibber_sasl_auth_authenticate(sasl, servername, 
                                         connection, stanza, 
                                         current_test->allow_plain, &error)) {
      got_error(error->domain, error->code, error->message);
    }
  }
}

int
run_rest(test_t *test) {
  int ret = 0;
  TestSaslAuthServer *server;

  servertransport = GIBBER_TRANSPORT(test_transport_new(send_hook));

  server = test_sasl_auth_server_new(servertransport, 
                                     test->mech, username, password,
                                     test->problem);

  authenticated = FALSE;
  run_done = FALSE;
  current_test = test;

  clienttransport = GIBBER_TRANSPORT(test_transport_new(send_hook));
  conn = gibber_xmpp_connection_new(clienttransport); 

  g_signal_connect(conn, "parse-error",
                   G_CALLBACK(parse_error), NULL);
  g_signal_connect(conn, "stream-opened",
                   G_CALLBACK(stream_opened), NULL);
  g_signal_connect(conn, "stream-closed",
                   G_CALLBACK(stream_closed), NULL);
  g_signal_connect(conn, "received-stanza",
                   G_CALLBACK(received_stanza), NULL);
  gibber_xmpp_connection_open(conn, servername, NULL, "1.0");

  if (!run_done) {
    g_main_loop_run(mainloop);
  }  

  if (sasl != NULL) {
    g_object_unref(sasl);
    sasl = NULL;
  }
  g_object_unref(servertransport);
  g_object_unref(clienttransport);
  g_object_unref(conn);

  if (test->domain == 0 && error != NULL) {
    fprintf(stderr, "%s: unexpected failure -- %s: %d - %s\n", 
      test->description, g_quark_to_string(error->domain), 
      error->code, error->message);
    goto failed;
  } 
  
  if (test->domain != 0 && error == NULL) {
    fprintf(stderr, "%s: unexpected failure -- %s: %d - %s\n", 
      test->description, g_quark_to_string(error->domain), 
      error->code, error->message);
    goto failed;
  }

  if (error != NULL) {
    if (test->domain != error->domain
        || test->code != error->code) {
    fprintf(stderr, "%s: unexpected error %s: %d <> %s: %d - %s\n", 
      test->description, 
      g_quark_to_string(test->domain), test->code,
      g_quark_to_string(error->domain), 
      error->code, error->message);
    }
  }

  fprintf(stderr, "%s: succeeded\n", test->description);

out:
  if (error != NULL)
    g_error_free(error);
  error = NULL;

  return ret;
failed:
  ret = 1;
  goto out;
}

#define SUCCESS(desc, mech, allow_plain)                 \
 { desc, mech, allow_plain, 0, 0, SERVER_PROBLEM_NO_PROBLEM }

int
main (int argc, char **argv) {
  int failed = 0;
  int i;

  test_t tests[] = {
    SUCCESS("Normal authentication", NULL, TRUE),
    SUCCESS("Disallow PLAIN", "PLAIN", TRUE),
    SUCCESS("Plain method authentication", "PLAIN", TRUE),
    SUCCESS("Normal DIGEST-MD5 authentication", "DIGEST-MD5", TRUE),

    { "No supported mechanisms", "NONSENSE", TRUE,
       GIBBER_SASL_AUTH_ERROR, GIBBER_SASL_AUTH_ERROR_NO_SUPPORTED_MECHANISMS,
       SERVER_PROBLEM_NO_PROBLEM },
    { "No sasl support in server", NULL, TRUE,
       GIBBER_SASL_AUTH_ERROR, GIBBER_SASL_AUTH_ERROR_SASL_NOT_SUPPORTED,
       SERVER_PROBLEM_NO_SASL },

    { NULL, }
  };

  g_type_init();

  mainloop = g_main_loop_new(NULL, FALSE); 

  for (i = 0; tests[i].description != NULL ; i++) {
    failed += run_rest(&(tests[i]));
  }

  return failed;
}
