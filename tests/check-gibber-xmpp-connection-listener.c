/*
 * check-gibber-xmpp-connection-listener.c - Test for
 * GibberXmppConnectionListener
 * Copyright (C) 2007 Collabora Ltd.
 * @author Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <gibber/gibber-linklocal-transport.h>
#include <gibber/gibber-listener.h>
#include <gibber/gibber-xmpp-connection.h>
#include <gibber/gibber-xmpp-connection-listener.h>

#include <check.h>

gboolean got_connection;

static void
new_connection_cb (GibberXmppConnectionListener *listener,
                   GibberXmppConnection *connection,
                   struct sockaddr *addr,
                   guint size,
                   gpointer user_data)
{
  got_connection = TRUE;
}

static gboolean
connect_to_port (int port)
{
  GibberLLTransport *transport;
  struct sockaddr_in addr;
  gboolean result;

  transport = gibber_ll_transport_new ();

  memset (&addr, 0, sizeof (addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons (port);
  addr.sin_addr.s_addr = inet_addr ("127.0.0.1");

  result = gibber_ll_transport_open_sockaddr (transport,
      (struct sockaddr_storage *) &addr, NULL);

  g_object_unref (transport);
  return result;
}

START_TEST (test_listen)
{
  GibberXmppConnectionListener *listener;
  int port;
  gboolean result;

  got_connection = FALSE;

  listener = gibber_xmpp_connection_listener_new ();
  fail_if (listener == NULL);

  g_signal_connect (listener, "new-connection", G_CALLBACK (new_connection_cb),
      NULL);

  for (port = 5298; port < 5400; port++)
    {
      GError *error = NULL;
      if (gibber_xmpp_connection_listener_listen (listener, port, &error))
        break;

      fail_if (!g_error_matches (error, GIBBER_LISTENER_ERROR,
          GIBBER_LISTENER_ERROR_ADDRESS_IN_USE));
      g_error_free (error);
      error = NULL;
    }
  fail_if (port >= 5400);

  result = connect_to_port (port);
  fail_if (result == FALSE);

  while (g_main_context_iteration (NULL, FALSE))
    ;
  fail_if (got_connection == FALSE);

  g_object_unref (listener);

  /* listener is destroyed, connection should be refused now */
  got_connection = FALSE;
  result = connect_to_port (port);
  fail_if (result == TRUE);
} END_TEST

TCase *
make_gibber_xmpp_connection_listener_tcase (void)
{
  TCase *tc = tcase_create ("GibberXmppConnectionListener");
  tcase_add_test (tc, test_listen);
  return tc;
}
