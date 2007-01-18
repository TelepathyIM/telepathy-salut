#include <stdio.h>
#include <glib.h>

#include <salut-xmpp-connection.h>
#include <salut-transport.h>
#include "test-transport.h"

#define BUFSIZE 10

FILE *treefile;

gboolean
send_hook(SalutTransport *transport, const guint8 *data, 
          gsize length, GError **error) {
  /* Nothing for now */
  return TRUE;
}

void
parse_error(SalutXmppConnection *connection, gpointer user_data) {
  fprintf(treefile, "PARSE ERROR\n");
}

void
stream_opened(SalutXmppConnection *connection, gpointer user_data) {
  fprintf(treefile, "STREAM OPENED\n");
}

void
stream_closed(SalutXmppConnection *connection, gpointer user_data) {
  fprintf(treefile, "STREAM CLOSED\n");
}

gboolean 
print_attribute(const gchar *key, const gchar *value, const gchar *ns, 
                gpointer user_data) {
  fprintf(treefile, "%*s |-- Attribute: %s -> %s (ns: %s)\n", 
    GPOINTER_TO_INT(user_data), " ",
    key, value, ns);
  return TRUE;
}

void print_node(SalutXmppNode *node, gint ident);

gboolean 
print_child(SalutXmppNode *node, gpointer user_data) {
  print_node(node, GPOINTER_TO_INT(user_data));
  return TRUE;
}

void 
print_node(SalutXmppNode *node, gint ident) {
  fprintf(treefile, "%*s`-+-- Name: %s (ns: %s)\n", ident - 1, " ", 
                                    node->name, salut_xmpp_node_get_ns(node)); 
  salut_xmpp_node_each_attribute(node, print_attribute, GINT_TO_POINTER(ident));
  if (node->content)  
    fprintf(treefile, "%*s |-- Content: %s\n", ident, " ", node->content);

  salut_xmpp_node_each_child(node, print_child, GINT_TO_POINTER(ident + 2));
}

void
received_stanza(SalutXmppConnection *connnection, 
                SalutXmppStanza *stanza,
                gpointer user_data) {
  fprintf(treefile, "-|\n");
  print_node(stanza->node, 2);
}

int
main(int argc, char **argv) {
  SalutXmppConnection *connection;
  TestTransport *transport;
  FILE *file;
  int ret;
  guint8 buf[BUFSIZE];
  
  g_type_init();

  transport = test_transport_new(send_hook);
  connection = salut_xmpp_connection_new(SALUT_TRANSPORT(transport));

  g_signal_connect(connection, "parse-error",
                   G_CALLBACK(parse_error), NULL);
  g_signal_connect(connection, "stream-opened",
                   G_CALLBACK(stream_opened), NULL);
  g_signal_connect(connection, "stream-closed",
                   G_CALLBACK(stream_closed), NULL);
  g_signal_connect(connection, "received-stanza",
                   G_CALLBACK(received_stanza), NULL);

  g_assert(argc >= 2);

  file = fopen(argv[1], "r");
  g_assert(file != NULL);

  if (argc >= 3) {
    treefile = fopen(argv[2], "w+");
  } else {
    treefile = stdout;
  }
  g_assert(treefile != NULL);

  while ((ret = fread(buf, 1, BUFSIZE, file)) > 0) {
    test_transport_write(transport, buf, ret);
  }
  g_assert(ret == 0);
  fclose(file);

  return 0;
}
