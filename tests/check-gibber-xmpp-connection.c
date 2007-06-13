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

TCase *
make_gibber_xmpp_connection_tcase (void)
{
    TCase *tc = tcase_create ("XMPP Connection");
    tcase_add_test (tc, test_instantiation);
    return tc;
}
