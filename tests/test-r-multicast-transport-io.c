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
GibberRMulticastTransport *m;

void
received_data(GibberTransport *transport, GibberBuffer *buffer,
              gpointer user_data) {
  GibberRMulticastBuffer *rmbuffer = (GibberRMulticastBuffer *)buffer;
  gchar *b64;

  b64 = g_base64_encode((guchar *)buffer->data, buffer->length);
  printf("OUTPUT:%s:%s\n", rmbuffer->sender, b64);
  fflush(stdout);
  g_free(b64);
}

gboolean
send_hook(GibberTransport *transport, const guint8 *data,
          gsize length, GError **error, gpointer user_data) {
  gchar *b64;

  b64 = g_base64_encode((guchar *)data, length);
  printf("SEND:%s\n", b64);
  fflush(stdout);
  g_free(b64);

  return TRUE;
}

gboolean
got_input(GIOChannel *source, GIOCondition condition, gpointer user_data) {
  GIOStatus s;
  gchar *buffer;
  gsize len;
  gchar *p;
  gboolean packet = FALSE;
  guchar *b64;
  gsize size;

  s = g_io_channel_read_line(source, &buffer, &len, NULL, NULL);
  g_assert( s == G_IO_STATUS_NORMAL);

  if (g_str_has_prefix(buffer, "INPUT:")) {
    packet = FALSE;
    p = buffer + strlen("INPUT:");
  } else if (g_str_has_prefix(buffer, "RECV:")) {
    packet = TRUE;
    p = buffer + strlen("RECV:");
  } else {
    g_assert_not_reached();
  }

  b64 = g_base64_decode(p, &size);

  if (packet)  {
    test_transport_write(t, b64, size);
  } else {
    g_assert(gibber_transport_send(GIBBER_TRANSPORT(m), b64, size, NULL));
  }

  g_free(b64);
  g_free(buffer);

  return TRUE;
}

gboolean
got_error(GIOChannel *source, GIOCondition condition, gpointer user_data) {
  g_main_loop_quit(loop);
  fprintf(stderr, "error");
  fflush(stderr);
  return TRUE;
}

static void
new_sender_cb(GibberRMulticastTransport *transport,
              const char *name, gpointer user_data) {
  printf("NEWNODE:%s\n", name);
  fflush(stdout);
}

static void
connected (GibberRMulticastTransport *transport, gpointer user_data) {
  printf("CONNECTED:\n");
  fflush(stdout);
}

int
main(int argc, char **argv){ 
  GIOChannel *io;

  g_assert(argc == 2);

  g_type_init();

  printf("Starting process %d for %s\n", getpid(), argv[1]);

  loop = g_main_loop_new(NULL, FALSE);

  t = test_transport_new(send_hook, argv[1]);
  GIBBER_TRANSPORT(t)->max_packet_size = 1500;
  test_transport_set_echoing (t, TRUE);

  m = gibber_r_multicast_transport_new(GIBBER_TRANSPORT(t), argv[1]);
  gibber_transport_set_handler(GIBBER_TRANSPORT(m), received_data, argv[1]);

  g_signal_connect(m, "new-sender", 
      G_CALLBACK(new_sender_cb), NULL);

  g_signal_connect(m, "connected",
    G_CALLBACK(connected), NULL);

  /* test transport starts out connected */
  g_assert(gibber_r_multicast_transport_connect(m, FALSE, NULL));

  io = g_io_channel_unix_new(STDIN_FILENO);
  g_io_add_watch (io,  G_IO_IN, got_input, NULL);
  g_io_add_watch (io,  G_IO_HUP|G_IO_ERR, got_error, NULL);

  g_main_loop_run(loop);

  return 0;
}
