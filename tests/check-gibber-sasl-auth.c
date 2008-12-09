#include <stdio.h>

#include "test-sasl-auth-server.h"
#include "test-transport.h"
#include <gibber/gibber-xmpp-connection.h>
#include <gibber/gibber-sasl-auth.h>

#include <check.h>

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
got_error (GQuark domain, int code, const gchar *message)
{
  g_set_error (&error, domain, code, "%s", message);
  run_done = TRUE;
  g_main_loop_quit (mainloop);
}

gboolean
send_hook (GibberTransport *transport, const guint8 *data,
   gsize length, GError **error, gpointer user_data)
{
  GibberTransport *target =
     (servertransport == transport) ? clienttransport : servertransport;

  test_transport_write (TEST_TRANSPORT (target), data, length);
  return TRUE;
}

gchar *
return_str (GibberSaslAuth *auth, gpointer user_data)
{
  return g_strdup (user_data);
}

void
auth_success (GibberSaslAuth *sasl, gpointer user_data)
{
  authenticated = TRUE;
  /* Reopen the connection */
  gibber_xmpp_connection_restart (conn);
  gibber_xmpp_connection_open (conn, servername, NULL, "1.0");
}

void
auth_failed (GibberSaslAuth *sasl, GQuark domain,
    int code, gchar *message, gpointer user_data)
{
  got_error (domain, code, message);
}

static void
parse_error (GibberXmppConnection *connection, gpointer user_data)
{
  fail ();
}

static void
stream_opened (GibberXmppConnection *connection,
              gchar *from, gchar *to, gchar *version, gpointer user_data)
{
  if (authenticated)
    gibber_xmpp_connection_close (conn);
}

static void
stream_closed (GibberXmppConnection *connection, gpointer user_data)
{
  run_done = TRUE;
  g_main_loop_quit (mainloop);
}

static void
received_stanza (GibberXmppConnection *connection, GibberXmppStanza *stanza,
   gpointer user_data)
{
  if (sasl == NULL)
    {
      GError *error = NULL;
      sasl = gibber_sasl_auth_new ();

      g_signal_connect (sasl, "username-requested",
          G_CALLBACK (return_str), (gpointer)username);
      g_signal_connect (sasl, "password-requested",
          G_CALLBACK (return_str), (gpointer)password);
      g_signal_connect (sasl, "authentication-succeeded",
          G_CALLBACK (auth_success), NULL);
      g_signal_connect (sasl, "authentication-failed",
          G_CALLBACK (auth_failed), NULL);

      if (!gibber_sasl_auth_authenticate (sasl, servername, connection, stanza,
          current_test->allow_plain, &error))
        {
          got_error (error->domain, error->code, error->message);
          g_error_free (error);
        }
    }
}

void
run_rest (test_t *test)
{
  TestSaslAuthServer *server;

  servertransport = GIBBER_TRANSPORT(test_transport_new (send_hook, NULL));

  server = test_sasl_auth_server_new (servertransport, test->mech, username,
      password, test->problem);

  authenticated = FALSE;
  run_done = FALSE;
  current_test = test;

  clienttransport = GIBBER_TRANSPORT (test_transport_new (send_hook, NULL));
  conn = gibber_xmpp_connection_new (clienttransport);

  g_signal_connect (conn, "parse-error", G_CALLBACK (parse_error), NULL);
  g_signal_connect (conn, "stream-opened", G_CALLBACK (stream_opened), NULL);
  g_signal_connect (conn, "stream-closed", G_CALLBACK (stream_closed), NULL);
  g_signal_connect (conn, "received-stanza",
      G_CALLBACK (received_stanza), NULL);
  gibber_xmpp_connection_open (conn, servername, NULL, "1.0");

  if (!run_done)
    {
      g_main_loop_run (mainloop);
    }

  if (sasl != NULL)
    {
      g_object_unref (sasl);
      sasl = NULL;
    }

  g_object_unref (servertransport);
  g_object_unref (clienttransport);
  g_object_unref (conn);

  fail_if (test->domain == 0 && error != NULL);
  fail_if (test->domain != 0 && error == NULL);

  if (error != NULL)
    {
      fail_if (test->domain != error->domain);
      fail_if (test->code != error->code);
    }

  if (error != NULL)
    g_error_free (error);

  error = NULL;
}

#define SUCCESS(desc, mech, allow_plain)                 \
 { desc, mech, allow_plain, 0, 0, SERVER_PROBLEM_NO_PROBLEM }

#define NUMBER_OF_TEST 6

START_TEST (test_auth)
{
  test_t tests[NUMBER_OF_TEST] = {
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
  };

  g_type_init ();

  mainloop = g_main_loop_new (NULL, FALSE);

  run_rest (&(tests[_i]));
} END_TEST

TCase *
make_gibber_sasl_auth_tcase (void)
{
    TCase *tc = tcase_create ("SASL Auth");
    tcase_add_loop_test (tc, test_auth, 0, NUMBER_OF_TEST);
    return tc;
}
