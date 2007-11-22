#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#include <glib.h>

#include <gibber/gibber-tcp-transport.h>
#include <gibber/gibber-ssl-transport.h>
#include <gibber/gibber-sasl-auth.h>
#include <gibber/gibber-xmpp-connection.h>
#include <gibber/gibber-namespaces.h>

typedef enum {
  INITIAL,
  SSL,
  SSL_DONE,
  SASL,
  DONE,
} State;


State state = INITIAL;
GMainLoop *mainloop;
GibberXmppConnection *conn;
const gchar *server;
const gchar *username;
const gchar *password;
GibberSaslAuth *sasl = NULL;
GibberSSLTransport *ssl = NULL;

void
tcp_connected(GibberTCPTransport *tcp, gpointer user_data) {
  printf("TCP transport connected!\n");
  gibber_xmpp_connection_open(conn, server, NULL, "1.0");
}

void
tcp_connecting(GibberTCPTransport *tcp, gpointer user_data) {
  printf("TCP transport connecting!\n");
}

void
tcp_disconnected(GibberTCPTransport *tcp, gpointer user_data) {
  printf("TCP transport disconnected!\n");
  g_main_loop_quit(mainloop);
}

void
tcp_error(GibberTCPTransport *tcp, guint domain, 
          guint code, gchar *message, gpointer user_data) {
  printf("TCP transport  got an error: %s\n", message);
    g_main_loop_quit(mainloop);
}

void
ssl_connected(GibberSSLTransport *ssl, gpointer user_data) {
  printf("SSL transport connected!\n");
  state = SSL_DONE;
  gibber_xmpp_connection_open(conn, server, NULL, "1.0");
}

void
ssl_connecting(GibberSSLTransport *ssl, gpointer user_data) {
  printf("SSL transport connecting!\n");
}

void
ssl_disconnected(GibberSSLTransport *ssl, gpointer user_data) {
  printf("TCP transport disconnected!\n");
  g_main_loop_quit(mainloop);
}

void
ssl_error(GibberSSLTransport *ssl, guint domain, 
          guint code, gchar *message, gpointer user_data) {
  printf("SSL transport  got an error: %s\n", message);
  g_main_loop_quit(mainloop);
}

void
conn_parse_error(GibberXmppConnection *connection, gpointer user_data) {
  fprintf(stderr, "PARSE ERROR\n");
  exit(1);
}

void
conn_stream_opened(GibberXmppConnection *connection, 
              const gchar *to, const gchar *from, const gchar *version,
              gpointer user_data) {
  printf("Stream opened -- from: %s version: %s\n", from, version);
  if (version == NULL || strcmp(version, "1.0")) {
    printf("Server is not xmpp compliant\n");
    g_main_loop_quit(mainloop);
  }
}

void
conn_stream_closed(GibberXmppConnection *connection, gpointer user_data) {
  printf("Stream opened\n");
  gibber_xmpp_connection_close(connection);
}

gchar *
return_str(GibberSaslAuth *auth, gpointer user_data) {
  return g_strdup(user_data);
}

void
auth_success(GibberSaslAuth *sasl, gpointer user_data) {
  printf("Authentication successfull!!\n");
  state = DONE;
  /* Reopen the connection */
  gibber_xmpp_connection_restart(conn);
  gibber_xmpp_connection_open(conn, server, NULL, "1.0");
}

void
auth_failed(GibberSaslAuth *sasl, GQuark domain, 
    int code, gchar *message, gpointer user_data) {
  printf("Authentication failed: %s\n", message);
  g_main_loop_quit(mainloop);
}

static void
start_ssl(GibberXmppConnection *connection, GibberXmppStanza *stanza) {
  GibberTransport *transport;
  GError *error = NULL;

  if (strcmp(stanza->node->name, "proceed") 
    || strcmp(gibber_xmpp_node_get_ns(stanza->node), 
          GIBBER_XMPP_NS_TLS)) {
    printf("Server doesn't want to start tls");
    g_main_loop_quit(mainloop);
  }

  transport = g_object_ref(connection->transport);
  gibber_xmpp_connection_disengage(connection);

  ssl = gibber_ssl_transport_new(GIBBER_TRANSPORT(transport));
  gibber_xmpp_connection_restart(connection);
  gibber_xmpp_connection_engage(connection, GIBBER_TRANSPORT(ssl));

  g_signal_connect(ssl, "connected", 
                   G_CALLBACK(ssl_connected), NULL);
  g_signal_connect(ssl, "connecting", 
                   G_CALLBACK(ssl_connecting), NULL);
  g_signal_connect(ssl, "disconnected", 
                   G_CALLBACK(ssl_disconnected), NULL);
  g_signal_connect(ssl, "error", 
                   G_CALLBACK(ssl_error), NULL);

  if (!gibber_ssl_transport_connect(ssl, server, &error)) {
     printf("SSL auth start failed: %s\n", error->message);
     g_main_loop_quit(mainloop);
  }
}

static void 
negotiate_ssl(GibberXmppConnection *connection, GibberXmppStanza *stanza) {
  GibberXmppNode *tls;
  GibberXmppStanza *starttls;
  if (strcmp(stanza->node->name, "features") 
    || strcmp(gibber_xmpp_node_get_ns(stanza->node),GIBBER_XMPP_NS_STREAM)) {
    printf("Didn't receive features stanza\n");
    g_main_loop_quit(mainloop);
  } 

  tls = gibber_xmpp_node_get_child_ns(stanza->node, "starttls",
                                      GIBBER_XMPP_NS_TLS);
  if (tls == NULL) {
    printf("Server doesn't support tls\n");
    g_main_loop_quit(mainloop);
  }

  starttls = gibber_xmpp_stanza_new("starttls");
  gibber_xmpp_node_set_ns(starttls->node, GIBBER_XMPP_NS_TLS);

  state = SSL;

  g_assert(gibber_xmpp_connection_send(connection, starttls, NULL));
  g_object_unref(starttls);
}

static void
start_sasl_helper(GibberXmppConnection *connection, GibberXmppStanza *stanza) {
  GError *error;

  state = SASL;
  sasl = gibber_sasl_auth_new();
  g_signal_connect(sasl, "username-requested",
                    G_CALLBACK(return_str), (gpointer)username);
  g_signal_connect(sasl, "password-requested",
                    G_CALLBACK(return_str), (gpointer)password);
  g_signal_connect(sasl, "authentication-succeeded",
                    G_CALLBACK(auth_success), NULL);
  g_signal_connect(sasl, "authentication-failed",
                    G_CALLBACK(auth_failed), NULL);

  if (!gibber_sasl_auth_authenticate(sasl, server, 
                                  connection, stanza, TRUE, &error)) {
     printf("Sasl auth start failed: %s\n", error->message);
     g_main_loop_quit(mainloop);
  }
}

void
conn_received_stanza(GibberXmppConnection *connection, 
                GibberXmppStanza *stanza,
                gpointer user_data) {
  
  switch (state ) {
    case INITIAL:
      negotiate_ssl(connection, stanza);
      break;
    case SSL:
      start_ssl(connection, stanza);
      break;
    case SSL_DONE:
      start_sasl_helper(connection, stanza);
      break;
    case SASL:
    case DONE:
      break;
  }
}


int
main(int argc, char **argv) {
  GibberTCPTransport *tcp;

  g_type_init();

  g_assert(argc >= 5);

  mainloop = g_main_loop_new(NULL, FALSE);

  tcp = gibber_tcp_transport_new();
  g_signal_connect(tcp, "connected", 
                   G_CALLBACK(tcp_connected), NULL);
  g_signal_connect(tcp, "connecting", 
                   G_CALLBACK(tcp_connecting), NULL);
  g_signal_connect(tcp, "disconnected", 
                   G_CALLBACK(tcp_disconnected), NULL);
  g_signal_connect(tcp, "error", 
                   G_CALLBACK(tcp_error), NULL);


  conn = gibber_xmpp_connection_new(GIBBER_TRANSPORT(tcp));
  g_signal_connect(conn, "parse-error",
      G_CALLBACK(conn_parse_error), NULL);
  g_signal_connect(conn, "stream-opened",
      G_CALLBACK(conn_stream_opened), NULL);
  g_signal_connect(conn, "stream-closed",
      G_CALLBACK(conn_stream_closed), NULL);
  g_signal_connect(conn, "received-stanza",
      G_CALLBACK(conn_received_stanza), NULL);


  username = argv[3];
  password = argv[4];
  server = (argc > 5) ? argv[5] : argv[1];
  gibber_tcp_transport_connect(tcp, argv[1], argv[2]);


  g_main_loop_run(mainloop);
  return 0;
}
