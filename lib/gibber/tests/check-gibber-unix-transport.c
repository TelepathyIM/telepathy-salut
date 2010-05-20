/*
 * check-unix-transport.c - Test for GibberUnixTransport
 * Copyright (C) 2009 Collabora Ltd.
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


#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/un.h>

#include <gibber/gibber-unix-transport.h>
#include <gibber/gibber-listener.h>

#include <check.h>
#include "check-gibber.h"

gboolean got_connection;
gboolean received_credentials;
GibberUnixTransport *unix_transport;

#define DATA "What a nice data"

static void
new_connection_cb (GibberListener *listener,
                   GibberTransport *connection,
                   struct sockaddr *addr,
                   guint size,
                   GMainLoop *loop)
{
#if defined(__linux__)
  int fd, opt, ret;
  struct iovec iov;
  struct msghdr msg;
  ssize_t received;
  char control[CMSG_SPACE (sizeof (struct ucred))];
  struct cmsghdr *ch;
  struct ucred *cred;
  gchar buffer[128];
#endif

  got_connection = TRUE;

  /* Block receiving so the data won't be consummed by transport's GIOSource */
  gibber_transport_block_receiving (connection, TRUE);

#if defined(__linux__)
  g_assert (gibber_unix_transport_send_credentials (unix_transport,
        (guint8 *) DATA, strlen (DATA) + 1));

  fd = GIBBER_FD_TRANSPORT (connection)->fd;

  opt = 1;
  ret = setsockopt (fd, SOL_SOCKET, SO_PASSCRED, &opt, sizeof (opt));
  g_assert (ret != -1);

  memset (buffer, 0, sizeof (buffer));
  memset (&iov, 0, sizeof (iov));
  iov.iov_base = buffer;
  iov.iov_len = sizeof (buffer);

  memset (&msg, 0, sizeof (msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = control;
  msg.msg_controllen = sizeof (control);

  received = recvmsg (fd, &msg, 0);
  /* check the received data */
  g_assert (received != -1);
  g_assert (strcmp (DATA, buffer) == 0);

  /* check the credentials */
  ch = CMSG_FIRSTHDR (&msg);
  g_assert (ch != NULL);
  cred = (struct ucred *) CMSG_DATA (ch);
  g_assert (cred->pid == getpid ());
  g_assert (cred->uid == getuid ());
  g_assert (cred->gid == getgid ());
#else /* not Linux */
  g_assert (!gibber_unix_transport_send_credentials (unix_transport,
        (guint8 *) DATA, strlen (DATA) + 1));
#endif

  g_main_loop_quit (loop);
}

START_TEST (test_send_credentials)
{
  GibberListener *listener_unix;
  int ret;
  GMainLoop *mainloop;
  GError *error = NULL;
  gchar *path = "/tmp/check-gibber-unix-transport-socket";

  ret = unlink (path);
  fail_if (ret == -1 && errno != ENOENT);

  got_connection = FALSE;
  mainloop = g_main_loop_new (NULL, FALSE);

  listener_unix = gibber_listener_new ();
  fail_if (listener_unix == NULL);

  g_signal_connect (listener_unix, "new-connection",
      G_CALLBACK (new_connection_cb), mainloop);

  ret = gibber_listener_listen_socket (listener_unix, path, FALSE, &error);
  fail_if (ret != TRUE);

  unix_transport = gibber_unix_transport_new ();
  ret = gibber_unix_transport_connect (unix_transport, path, &error);
  fail_if (ret != TRUE);

  if (!got_connection)
    g_main_loop_run (mainloop);

  fail_if (!got_connection, "Failed to connect");

  g_object_unref (listener_unix);
  g_object_unref (unix_transport);
  g_main_loop_unref (mainloop);
} END_TEST

static void
get_credentials_cb (GibberUnixTransport *transport,
                    GibberBuffer *buffer,
                    GibberCredentials *credentials,
                    GError *error,
                    gpointer user_data)
{
  GMainLoop *loop = (GMainLoop *) user_data;

  received_credentials = TRUE;

  g_assert (error == NULL);
  g_assert (strcmp ((gchar *) buffer->data, DATA) == 0);
  g_assert (credentials->pid == getpid ());
  g_assert (credentials->uid == getuid ());
  g_assert (credentials->gid == getgid ());

  g_main_loop_quit (loop);
}

static void
receive_new_connection_cb (GibberListener *listener,
                           GibberTransport *connection,
                           struct sockaddr *addr,
                           guint size,
                           GMainLoop *loop)
{
  gboolean ok;

  ok = gibber_unix_transport_recv_credentials (unix_transport,
      get_credentials_cb, loop);

  g_assert (ok == gibber_unix_transport_supports_credentials ());

  ok = gibber_unix_transport_send_credentials (GIBBER_UNIX_TRANSPORT (connection),
      (guint8 *) DATA, strlen (DATA));

  g_assert (ok == gibber_unix_transport_supports_credentials ());
}

START_TEST (test_receive_credentials)
{
  GibberListener *listener_unix;
  int ret;
  GMainLoop *mainloop;
  GError *error = NULL;
  gchar *path = "/tmp/check-gibber-unix-transport-socket";

  ret = unlink (path);
  fail_if (ret == -1 && errno != ENOENT);

  received_credentials = FALSE;
  mainloop = g_main_loop_new (NULL, FALSE);

  listener_unix = gibber_listener_new ();
  fail_if (listener_unix == NULL);

  g_signal_connect (listener_unix, "new-connection",
      G_CALLBACK (receive_new_connection_cb), mainloop);

  ret = gibber_listener_listen_socket (listener_unix, path, FALSE, &error);
  fail_if (ret != TRUE);

  unix_transport = gibber_unix_transport_new ();
  ret = gibber_unix_transport_connect (unix_transport, path, &error);
  fail_if (ret != TRUE);

#if defined(__linux__)
  if (!received_credentials)
    g_main_loop_run (mainloop);

  fail_if (!received_credentials, "Failed to receive credentials");
#endif

  g_object_unref (listener_unix);
  g_object_unref (unix_transport);
  g_main_loop_unref (mainloop);
} END_TEST

TCase *
make_gibber_unix_transport_tcase (void)
{
  TCase *tc = tcase_create ("GibberUnixTransport");
  tcase_add_test (tc, test_send_credentials);
  tcase_add_test (tc, test_receive_credentials);
  return tc;
}
