#include "config.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include <gibber/gibber-r-multicast-transport.h>
#include <gibber/gibber-debug.h>
#include "test-transport.h"

GMainLoop *loop;

TestTransport *t;
GibberRMulticastTransport *rm;
GibberRMulticastCausalTransport *rmc;
gulong rmc_connected_handler = 0;

static void
received_data (GibberTransport *transport, GibberBuffer *buffer,
    gpointer user_data)
{
  GibberRMulticastBuffer *rmbuffer = (GibberRMulticastBuffer *) buffer;
  gchar *b64;

  b64 = g_base64_encode ((guchar *) buffer->data, buffer->length);
  printf ("OUTPUT:%s:%s\n", rmbuffer->sender, b64);
  fflush (stdout);
  g_free (b64);
}

static gboolean
send_hook (GibberTransport *transport, const guint8 *data, gsize length,
    GError **error, gpointer user_data)
{
  gchar *b64;

  b64 = g_base64_encode ((guchar *) data, length);
  printf ("SEND:%s\n", b64);
  fflush (stdout);
  g_free (b64);

  return TRUE;
}

static void
fail_node (gchar *name)
{
  GibberRMulticastSender *sender;

  name = g_strstrip (name);

  sender = gibber_r_multicast_causal_transport_get_sender_by_name (rmc, name);
  g_assert (sender != NULL);

  _gibber_r_multicast_TEST_sender_fail (sender);
}

static gboolean
got_input (GIOChannel *source, GIOCondition condition, gpointer user_data)
{
  GIOStatus s;
  gchar *buffer;
  gsize len;
  gchar *p;
  gboolean packet = FALSE;
  guchar *b64;
  gsize size;

  s = g_io_channel_read_line (source, &buffer, &len, NULL, NULL);
  g_assert (s == G_IO_STATUS_NORMAL);

  if (g_str_has_prefix (buffer, "INPUT:"))
    {
      packet = FALSE;
      p = buffer + strlen ("INPUT:");
    }
  else if (g_str_has_prefix (buffer, "RECV:"))
    {
      packet = TRUE;
      p = buffer + strlen ("RECV:");
    }
  else if (strcmp (buffer, "DISCONNECT\n") == 0)
    {
      gibber_transport_disconnect (GIBBER_TRANSPORT(rm));
      goto out;
    }
  else if (g_str_has_prefix (buffer, "FAIL:"))
    {
      /* this will modify our buffer */
      fail_node (buffer + strlen ("FAIL:"));
      goto out;
    }
  else
    {
      g_assert_not_reached ();
    }

  b64 = g_base64_decode (p, &size);

  if (packet)
    {
      test_transport_write (t, b64, size);
    }
  else
    {
      g_assert (gibber_transport_send (GIBBER_TRANSPORT (rm),
          b64, size, NULL));
    }

  g_free (b64);
out:
  g_free (buffer);

  return TRUE;
}

static gboolean
got_error (GIOChannel *source, GIOCondition condition, gpointer user_data)
{
  g_main_loop_quit (loop);
  fprintf (stderr, "error");
  fflush (stderr);
  return TRUE;
}

static void
new_senders_cb (GibberRMulticastTransport *transport,
    GArray *names, gpointer user_data)
{
  guint i;
  GString *str = g_string_new ("NEWNODES:");

  for (i = 0; i < names->len; i++)
    {
      g_string_append_printf (str, "%s ", g_array_index (names, gchar *, i));
    }
  printf ("%s\n", str->str);
  g_string_free (str, TRUE);
  fflush (stdout);
}

static void
lost_senders_cb (GibberRMulticastTransport *transport,
    GArray *names, gpointer user_data)
{
  guint i;
  GString *str = g_string_new ("LOSTNODES:");

  for (i = 0; i < names->len; i++)
    {
      g_string_append_printf (str, "%s ", g_array_index (names, gchar *, i));
    }
  printf ("%s\n", str->str);
  g_string_free (str, TRUE);
  fflush (stdout);
}

static void
rm_connected (GibberRMulticastTransport *transport, gpointer user_data)
{
  printf ("CONNECTED:\n");
  fflush (stdout);
}

static void
rm_disconnected (GibberRMulticastTransport *transport, gpointer user_data)
{
  printf ("DISCONNECTED:\n");
  fflush (stdout);
  g_main_loop_quit (loop);
}

static void
rmc_connected (GibberRMulticastTransport *transport, gpointer user_data)
{
  g_assert (gibber_r_multicast_transport_connect (rm, NULL));
  g_signal_handler_disconnect (transport, rmc_connected_handler);
}

int
main (int argc, char **argv)
{
  GIOChannel *io;

  g_assert (argc == 2);

  g_type_init ();

  printf ("Starting process %d for %s\n", getpid (), argv[1]);

  loop = g_main_loop_new (NULL, FALSE);

  t = test_transport_new (send_hook, argv[1]);
  GIBBER_TRANSPORT (t)->max_packet_size = 1500;
  test_transport_set_echoing (t, TRUE);

  rmc = gibber_r_multicast_causal_transport_new (GIBBER_TRANSPORT (t),
      argv[1]);
  g_object_unref (t);

  rm = gibber_r_multicast_transport_new (rmc);
  gibber_transport_set_handler (GIBBER_TRANSPORT (rm), received_data, argv[1]);
  g_object_unref (rmc);

  g_signal_connect (rm, "new-senders", G_CALLBACK (new_senders_cb), NULL);

  g_signal_connect (rm, "lost-senders", G_CALLBACK (lost_senders_cb), NULL);

  rmc_connected_handler = g_signal_connect (rmc, "connected",
    G_CALLBACK (rmc_connected), NULL);

  g_signal_connect (rm, "connected", G_CALLBACK (rm_connected), NULL);

  g_signal_connect (rm, "disconnected", G_CALLBACK (rm_disconnected), NULL);

  /* test transport starts out connected */
  g_assert (gibber_r_multicast_causal_transport_connect (rmc, FALSE, NULL));

  io = g_io_channel_unix_new (STDIN_FILENO);
  g_io_add_watch (io,  G_IO_IN, got_input, NULL);
  g_io_add_watch (io,  G_IO_HUP|G_IO_ERR, got_error, NULL);

  g_main_loop_run (loop);

  return 0;
}
