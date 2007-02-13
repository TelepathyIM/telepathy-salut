#include <stdio.h>
#include <stdlib.h>

#include <glib.h>

#include <gibber/gibber-tcp-transport.h>
#include <gibber/gibber-xmpp-connection.h>

GMainLoop *mainloop;
GibberXmppConnection *conn;
const gchar *server;

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
}

void
tcp_error(GibberTCPTransport *tcp, guint domain, 
          guint code, gchar *message, gpointer user_data) {
  printf("TCP transport  got an error: %s\n", message);
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
}

void
conn_stream_closed(GibberXmppConnection *connection, gpointer user_data) {
  printf("Stream opened\n");
  gibber_xmpp_connection_close(connection);
}

void
conn_received_stanza(GibberXmppConnection *connection, 
                GibberXmppStanza *stanza,
                gpointer user_data) {
  printf("Recieved stanza\n");
}


int
main(int argc, char **argv) {
  GibberTCPTransport *tcp;

  g_type_init();

  g_assert(argc == 3);

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


  server = argv[1];
  gibber_tcp_transport_connect(tcp, server, argv[2]);

  g_main_loop_run(mainloop);
  return 0;
}
