#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <glib.h>

#include <gibber/gibber-r-multicast-transport.h>
#include "test-transport.h"

GMainLoop *loop;

typedef struct {
  TestTransport *t;
  GibberRMulticastTransport *m;
} Info;

void
received_data(GibberTransport *transport, GibberBuffer *buffer,
              gpointer user_data) {
  gchar *b64;

  b64 = g_base64_encode((guchar *)buffer->data, buffer->length);
  printf("%s:%s\n", (gchar *)user_data, b64);
  g_free(b64);
}

gboolean
send_hook(GibberTransport *transport, const guint8 *data,
          gsize length, GError **error, gpointer user_data) {
  gchar *b64;

  b64 = g_base64_encode((guchar *)data, length);
  printf("%s>%s\n", (gchar *)user_data, b64);
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
  Info *info;
  GHashTable *infohash = (GHashTable *)user_data;
  guchar *b64;
  gsize size;

  s = g_io_channel_read_line(source, &buffer, &len, NULL, NULL);
  g_assert( s == G_IO_STATUS_NORMAL);

  for (p = buffer ;  *p != '\0'; p++) {
    if (*p == ':' || *p == '<') {
      packet = (*p == '<');
      *p = '\0';
      p++;
      break;
    }
  }

  info = g_hash_table_lookup(infohash, buffer);

  g_assert(info != NULL);

  b64 = g_base64_decode(p, &size);

  if (packet)  {
    test_transport_write(info->t, b64, size);
  } else {
    g_assert(gibber_transport_send(GIBBER_TRANSPORT(info->m), 
        b64, size, NULL));
  }

  g_free(b64);
  g_free(buffer);

  return TRUE;
}

int
main(int argc, char **argv){ 
  GHashTable *senders;
  int i;
  GIOChannel *io;

  g_assert(argc > 1);

  g_type_init();

  loop = g_main_loop_new(NULL, FALSE);

  senders = g_hash_table_new(g_str_hash, g_str_equal);

  for (i = 1 ; i < argc ; i++) {
    Info *info = g_slice_new0(Info);

    info->t = test_transport_new(send_hook, argv[i]);
    GIBBER_TRANSPORT(info->t)->max_packet_size = 1500;

    info->m = gibber_r_multicast_transport_new(GIBBER_TRANSPORT(info->t),
                                               argv[i]);
    gibber_transport_set_handler(GIBBER_TRANSPORT(info->m),
        received_data, argv[i]);
    g_hash_table_insert(senders, argv[i], info);
  }

  io = g_io_channel_unix_new(STDIN_FILENO);
  g_io_add_watch (io,  G_IO_IN, got_input, senders);

  g_main_loop_run(loop);

  return 0;
}
