#include <stdio.h>
#include <unistd.h>
#include <glib.h>

#include <gibber/gibber-xmpp-connection.h>
#include <gibber/gibber-transport.h>
#include <wocky/wocky.h>
#include "test-transport.h"

#define BUFSIZE 10

FILE *treefile = NULL;
FILE *xmlfile = NULL;
gboolean parsing_failed = FALSE;

static gboolean
send_hook (GibberTransport *transport, const guint8 *data,
    gsize length, GError **error, gpointer user_data)
{
  size_t written;
  /* Nothing for now */
  written = fwrite (data, 1, length, xmlfile);
  g_assert (written == length);
  return TRUE;
}

static void
parse_error (GibberXmppConnection *connection, gpointer user_data)
{
  fprintf (treefile, "PARSE ERROR\n");
  fprintf (stderr, "PARSING FAILED\n");
  parsing_failed = TRUE;
}

static void
stream_opened (GibberXmppConnection *connection, const gchar *to,
    const gchar *from, const gchar *version, gpointer user_data)
{
  fprintf (treefile, "STREAM OPENED to=%s from=%s version=%s\n", to, from,
      version);

  gibber_xmpp_connection_open (connection, to, from, version);
}

static void
stream_closed (GibberXmppConnection *connection, gpointer user_data)
{
  fprintf (treefile, "STREAM CLOSED\n");
  gibber_xmpp_connection_close (connection);
}

static gboolean
print_attribute (const gchar *key, const gchar *value, const gchar *pref,
    const gchar *ns, gpointer user_data)
{
  fprintf (treefile, "%*s |-- Attribute: %s -> %s (ns: %s)\n",
    GPOINTER_TO_INT (user_data), " ", key, value, ns);
  return TRUE;
}

static void print_node (GibberXmppNode *node, gint ident);

static gboolean
print_child (GibberXmppNode *node, gpointer user_data)
{
  print_node (node, GPOINTER_TO_INT(user_data));
  return TRUE;
}

static void
print_node (GibberXmppNode *node, gint ident)
{
  fprintf (treefile, "%*s`-+-- Name: %s (ns: %s)\n", ident - 1, " ",
      node->name, gibber_xmpp_node_get_ns (node));
  wocky_node_each_attribute (node, print_attribute,
      GINT_TO_POINTER(ident));

  if (node->content)
    fprintf (treefile, "%*s |-- Content: %s\n", ident, " ", node->content);
  if (gibber_xmpp_node_get_language (node))
    fprintf (treefile, "%*s |-- Language: %s\n", ident, " ",
      gibber_xmpp_node_get_language (node));

  gibber_xmpp_node_each_child (node, print_child, GINT_TO_POINTER (ident + 2));
}

static void
received_stanza (GibberXmppConnection *connection, WockyStanza *stanza,
    gpointer user_data)
{
  fprintf (treefile, "-|\n");
  print_node (wocky_stanza_get_top_node (stanza), 2);
  g_assert (gibber_xmpp_connection_send (connection, stanza, NULL));
}

int
main (int argc, char **argv)
{
  GibberXmppConnection *connection;
  TestTransport *transport;
  FILE *file;
  int ret = 0;
  guint8 buf[BUFSIZE];


  g_type_init ();
  wocky_init ();

  transport = test_transport_new (send_hook, NULL);
  connection = gibber_xmpp_connection_new (GIBBER_TRANSPORT(transport));

  g_signal_connect (connection, "parse-error",
      G_CALLBACK (parse_error), NULL);
  g_signal_connect (connection, "stream-opened",
      G_CALLBACK (stream_opened), NULL);
  g_signal_connect (connection, "stream-closed",
                   G_CALLBACK (stream_closed), NULL);
  g_signal_connect (connection, "received-stanza",
                   G_CALLBACK (received_stanza), NULL);

  g_assert (argc >= 2 && argc < 5);

  file = fopen (argv[1], "r");
  g_assert (file != NULL);

  if (argc >= 3)
    {
      treefile = fopen (argv[2], "w+");
    }
  else
    {
      treefile = stdout;
    }
  g_assert (treefile != NULL);

  if (argc >= 4)
    {
      xmlfile = fopen (argv[3], "w+");
    }
  else
    {
      xmlfile = stderr;
    }
  g_assert (xmlfile != NULL);

  while (!parsing_failed && (ret = fread (buf, 1, BUFSIZE, file)) > 0)
    {
      test_transport_write (transport, buf, ret);
    }

  while (g_main_context_iteration (NULL, FALSE))
    ;


  g_assert (parsing_failed || ret == 0);
  fclose (file);

  wocky_deinit ();
  return parsing_failed ? 1 : 0;
}
