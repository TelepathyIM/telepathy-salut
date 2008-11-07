/*
 * check-gibber-listener.c - Test for GibberListener
 * Copyright (C) 2007, 2008 Collabora Ltd.
 * @author Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
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

#include <gibber/gibber-tcp-transport.h>
#include <gibber/gibber-listener.h>

#include <check.h>

gboolean got_connection;
gboolean signalled;

static void
new_connection_cb (GibberListener *listener,
                   GibberTransport *connection,
                   struct sockaddr_storage *addr,
                   guint size,
                   GMainLoop *loop)
{
  signalled = TRUE;
  got_connection = TRUE;
  g_main_loop_quit (loop);
}

static void
disconnected_cb (GibberTransport *transport, GMainLoop *loop)
{
  signalled = TRUE;
  got_connection = FALSE;
  g_main_loop_quit (loop);
}

static GibberTransport *
connect_to_port (int port, GMainLoop *loop)
{
  GibberTCPTransport *transport;
  gchar sport[16];

  g_snprintf (sport, 16, "%d", port);

  transport = gibber_tcp_transport_new ();
  g_signal_connect (transport, "disconnected",
    G_CALLBACK (disconnected_cb), loop);

  gibber_tcp_transport_connect (transport, "127.0.0.1", sport);

  return GIBBER_TRANSPORT (transport);
}

START_TEST (test_tcp_listen)
{
  GibberListener *listener;
  GibberListener *listener2;
  int port;
  GMainLoop *mainloop;
  GibberTransport *transport;
  GError *error = NULL;

  mainloop = g_main_loop_new (NULL, FALSE);

  listener = gibber_listener_new ();
  fail_if (listener == NULL);

  g_signal_connect (listener, "new-connection", G_CALLBACK (new_connection_cb),
      mainloop);

  for (port = 5298; port < 5400; port++)
    {
      if (gibber_listener_listen_tcp (listener, port, &error))
        break;

      fail_if (!g_error_matches (error, GIBBER_LISTENER_ERROR,
            GIBBER_LISTENER_ERROR_ADDRESS_IN_USE));
      g_error_free (error);
      error = NULL;
    }
  fail_if (port >= 5400);

  /* try a second listener on the same port */
  listener2 = gibber_listener_new ();
  fail_if (listener2 == NULL);
  fail_if (gibber_listener_listen_tcp (listener2, port, &error));
  fail_if (!g_error_matches (error, GIBBER_LISTENER_ERROR,
        GIBBER_LISTENER_ERROR_ADDRESS_IN_USE));
  g_object_unref (listener2);
  g_error_free (error);
  error = NULL;

  signalled = FALSE;
  transport = connect_to_port (port, mainloop);
  if (!signalled)
    g_main_loop_run (mainloop);

  fail_if (!got_connection, "Failed to connect");

  g_object_unref (listener);
  g_object_unref (transport);

  /* listener is destroyed, connection should be refused now */
  signalled = FALSE;
  transport = connect_to_port (port, mainloop);

  if (!signalled)
    g_main_loop_run (mainloop);

  fail_if (got_connection, "Connected while listening should have stopped");

  g_object_unref (transport);
  g_main_loop_unref (mainloop);

} END_TEST

TCase *
make_gibber_listener_tcase (void)
{
  TCase *tc = tcase_create ("GibberListener");
  tcase_add_test (tc, test_tcp_listen);
  return tc;
}
