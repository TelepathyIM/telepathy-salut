#include <stdio.h>

#include "test-sasl-auth-server.h"
#include "test-transport.h"
#include <gibber/gibber-xmpp-connection.h>
#include <gibber/gibber-sasl-auth.h>

GMainLoop *mainloop;
GibberTransport *servertransport;
GibberTransport *clienttransport;
GibberXmppConnection *conn;
GibberSaslAuth *sasl = NULL;
gboolean authenticated = FALSE;
const gchar *username = "test";
const gchar *password = "test123";
const gchar *servername = "testserver";


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
  printf("Authentication successfull!!\n");
  authenticated = TRUE;
  /* Reopen the connection */
  gibber_xmpp_connection_open(conn, servername, NULL, "1.0");
}

void
auth_failed(GibberSaslAuth *sasl, GQuark domain,
    int code, gchar *message, gpointer user_data) {
  printf("Authentication failed: %s\n", message);
  authenticated = TRUE;
  g_main_loop_quit(mainloop);
}

static void
received_stanza(GibberXmppConnection *connection, GibberXmppStanza *stanza, 
   gpointer user_data)
{ 
  if (sasl == NULL) {
    sasl = gibber_sasl_auth_new();

    g_signal_connect(sasl, "username-requested",
                      G_CALLBACK(return_str), (gpointer)username);
    g_signal_connect(sasl, "password-requested",
                      G_CALLBACK(return_str), (gpointer)password);
    g_signal_connect(sasl, "authentication-succeeded",
                      G_CALLBACK(auth_success), NULL);
    g_signal_connect(sasl, "authentication-failed",
                      G_CALLBACK(auth_failed), NULL);

    g_assert(gibber_sasl_auth_authenticate(sasl, servername, 
                                         connection, stanza, TRUE, NULL));
  }
}

int
main (int argc, char **argv) {
  TestSaslAuthServer *server;

  g_type_init();

  mainloop = g_main_loop_new(NULL, FALSE); 

  servertransport = GIBBER_TRANSPORT(test_transport_new(send_hook));
  server = test_sasl_auth_server_new(servertransport, 
                                     NULL, username, password);

  clienttransport = GIBBER_TRANSPORT(test_transport_new(send_hook));
  conn = gibber_xmpp_connection_new(clienttransport); 
  /*gsignal_connect(connection, "parse-error",
                   G_CALLBACK(parse_error), NULL);
  g_signal_connect(connection, "stream-opened",
                   G_CALLBACK(stream_opened), NULL);
  g_signal_connect(connection, "stream-closed",
                   G_CALLBACK(stream_closed), NULL);
  */
  g_signal_connect(conn, "received-stanza",
                   G_CALLBACK(received_stanza), NULL);
  gibber_xmpp_connection_open(conn, servername, NULL, "1.0");

  if (!authenticated) {
    g_main_loop_run(mainloop);
  }  

  return 0;
}
