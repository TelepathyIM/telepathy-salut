#include <stdio.h>
#include <unistd.h>
#include <glib.h>

#include <gibber/gibber-xmpp-connection.h>
#include <gibber/gibber-transport.h>
#include "test-transport.h"

#include <check.h>

START_TEST (test_instantiation)
{
  GibberXmppConnection *connection;
  TestTransport *transport;

  g_type_init();

  transport = test_transport_new(NULL, NULL);
  connection = gibber_xmpp_connection_new(GIBBER_TRANSPORT(transport));

  fail_if (connection == NULL);

  connection = gibber_xmpp_connection_new(NULL);

  fail_if (connection == NULL);
}
END_TEST

void
parse_error_cb (GibberXmppConnection *connection, gpointer user_data) {
  gboolean *parse_error_found = user_data;
  *parse_error_found = TRUE;
}

START_TEST (test_simple_message) {
  GibberXmppConnection *connection;
  TestTransport *transport;
  const gchar *xml_input = "inputs/simple-message.input";
  gchar *buffer;
  gsize buffer_length, chunk_length, chunk_offset;
  const gsize chunk_size = 10;
  gboolean parse_error_found = FALSE;

  g_type_init ();

  transport = test_transport_new (NULL, NULL);
  connection = gibber_xmpp_connection_new (GIBBER_TRANSPORT(transport));

  g_signal_connect (connection, "parse-error",
                    G_CALLBACK(parse_error_cb), &parse_error_found);

  fail_unless (g_file_get_contents (xml_input, &buffer, &buffer_length, NULL));

  chunk_offset = 0;
  while (!parse_error_found && chunk_offset < buffer_length) {
    chunk_length = MIN (buffer_length - chunk_offset, chunk_size);
    test_transport_write (transport, (guint8*)(buffer + chunk_offset), chunk_length);
    chunk_offset += chunk_length;
  }

  fail_if (parse_error_found);
} END_TEST

TCase *
make_gibber_xmpp_connection_tcase (void)
{
    TCase *tc = tcase_create ("XMPP Connection");
    tcase_add_test (tc, test_instantiation);
    tcase_add_test (tc, test_simple_message);
    return tc;
}
