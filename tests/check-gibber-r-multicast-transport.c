#include <stdio.h>

#include <gibber/gibber-r-multicast-transport.h>
#include <gibber/gibber-r-multicast-packet.h>
#include "test-transport.h"

#include <check.h>

#define TEST_DATA_SIZE 300
GMainLoop *loop;

GibberRMulticastTransport *
create_rmulticast_transport (TestTransport **testtransport, const gchar *name,
    test_transport_send_hook test_send_hook) {
  TestTransport *t;
  GibberRMulticastTransport *rmtransport;

  t = test_transport_new (test_send_hook, NULL);
  fail_unless (t != NULL);
  GIBBER_TRANSPORT (t)->max_packet_size = 150;

  rmtransport = gibber_r_multicast_transport_new (
      GIBBER_TRANSPORT(t), "test123");

  if (testtransport != NULL) {
    *testtransport = t;
  }

  return rmtransport;
}

gboolean
fragmentation_send_hook(GibberTransport *transport, const guint8 *data,
          gsize length, GError **error, gpointer user_data)
{
  GibberRMulticastPacket *packet;
  static gsize bytes = 0;
  static guint8 next_byte = 0;
  gsize i;
  gsize size;
  guint8 *payload;

  packet = gibber_r_multicast_packet_parse (data, length, NULL);
  fail_unless (packet != NULL);

  if (packet->type != PACKET_TYPE_DATA) {
    goto out;
  }

  payload = gibber_r_multicast_packet_get_payload(packet, &size);
  bytes += size;

  fail_unless(bytes <= TEST_DATA_SIZE);

  /* check our bytes */
  for (i = 0; i < size; i++) {
    fail_unless(payload[i] == next_byte);
    next_byte++;
  }

  if (bytes == TEST_DATA_SIZE) {
    g_object_unref(packet);
    g_main_loop_quit(loop);
    return FALSE;
  }

out:
  g_object_unref(packet);
  return TRUE;
}


START_TEST (test_fragmentation)
{
  GibberRMulticastTransport *rmtransport;
  guint8 testdata[TEST_DATA_SIZE];
  int i;

  g_type_init();

  for (i = 0; i < TEST_DATA_SIZE; i++) {
    testdata[i] = (guint8) (i & 0xff);
  }

  loop = g_main_loop_new (NULL, FALSE);

  rmtransport = create_rmulticast_transport(NULL, "test123", 
       fragmentation_send_hook);

  fail_unless(gibber_r_multicast_transport_connect(rmtransport, FALSE, NULL));

  fail_unless(gibber_transport_send(GIBBER_TRANSPORT(rmtransport),
      (guint8 *)testdata, TEST_DATA_SIZE, NULL));

  g_main_loop_run(loop);
}
END_TEST

TCase *
make_gibber_r_multicast_transport_tcase (void)
{
  TCase *tc = tcase_create("Gibber R Multicast transport");
  tcase_add_test (tc, test_fragmentation);

  return tc;
}
