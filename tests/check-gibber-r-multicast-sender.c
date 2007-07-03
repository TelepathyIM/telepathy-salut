#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gibber/gibber-r-multicast-sender.h>

#include <check.h>

#define SENDER 4321
#define SENDER_NAME "testsender"

#define REPAIR_PACKET ((guint32)15)

#define EXTRA_SEEN ((guint32)11)
#define NR_PACKETS ((guint32)40)

guint32 serial_offset;
int expected;

typedef struct {
  guint32 serial_offset;
  gboolean test_seen;
} test_t;

typedef struct {
  guint32 receiver_id;
  const gchar *name;
  guint32 packet_id;
} recv_t;

GMainLoop *loop;
recv_t receivers[] = {
    { 0x500, "sender1", 500 },
    { 0x600, "sender2", 600 },
    {     0,      NULL,   0 }
};

GibberRMulticastPacket *
generate_packet(guint32 serial) {
  GibberRMulticastPacket *p;
  guint8 part = 0, total = 1;
  gchar *payload;
  int i;

  if (serial % 3 > 0) {
    part = serial % 3 - 1;
    total = 2;
  }
  p = gibber_r_multicast_packet_new(PACKET_TYPE_DATA, SENDER, 1500);

  gibber_r_multicast_packet_set_data_info(p, serial,
      (serial % G_MAXUINT8) - part, part, total);

  for (i = 0 ; receivers[i].receiver_id != 0; i++) {
    gibber_r_multicast_packet_add_sender_info(p,
        receivers[i].receiver_id, receivers[i].packet_id, NULL);
  }

  payload = g_strdup_printf("%010d\n", serial);
  gibber_r_multicast_packet_add_payload(p, (guint8 *)payload, strlen(payload));
  g_free(payload);
  return p;
}

void
data_received_cb(GibberRMulticastSender *sender,
                 guint8 stream_id,
                 guint8 *data,
                 gsize size,
                 gpointer user_data) {
  gchar *str;
  gchar **lines;
  int i;
  str = g_strndup((const gchar *)data, size);

  lines = g_strsplit(str, "\n", 0);
  for (i = 0 ; lines[i] != NULL && *lines[i] != '\0'; i++) {
    guint32 v = atoi(lines[i]);

    fail_unless (v == expected);
    fail_unless ((v % G_MAXUINT8) - i == stream_id);
    expected++;
  }
  /* serial % 3 is send out in a single packet the other two together.
   * So expected can't be  % 3 == 2 here */
  fail_if (expected % 3 == 2);

  if (expected == serial_offset + NR_PACKETS
      || expected == serial_offset + NR_PACKETS + EXTRA_SEEN) {
    g_main_loop_quit((GMainLoop *)user_data);
  }

  g_strfreev(lines);
  g_free(str);

}

void
repair_request_cb(GibberRMulticastSender *sender, guint id, gpointer data) {
  GibberRMulticastPacket *p;

  fail_unless (gibber_r_multicast_packet_diff(serial_offset, id) >= 0
               || gibber_r_multicast_packet_diff(id,
                  serial_offset + NR_PACKETS + EXTRA_SEEN) < 0);

  p = generate_packet(id);
  gibber_r_multicast_sender_push(sender, p);
  g_object_unref(p);
}

void
repair_message_cb(GibberRMulticastSender *sender,
                  GibberRMulticastPacket *packet,
                  gpointer user_data) {

  fail_unless(packet->type == PACKET_TYPE_DATA);
  fail_unless (packet->data.data.packet_id == REPAIR_PACKET + serial_offset);

  g_main_loop_quit((GMainLoop *)user_data);
}

static gboolean
add_packet(gpointer data) {
  static guint32 i = 0;
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER(data);
  GibberRMulticastPacket *p;

  if (i == NR_PACKETS) {
    i = 0;
    return FALSE;
  }

  if (i % 5 != 3) {
    p = generate_packet(i + serial_offset);
    gibber_r_multicast_sender_push(sender, p);
    g_object_unref(p);
  }

  i++;
  return TRUE;
}


#define NUMBER_OF_TESTS 3

START_TEST (test_sender) {
  GibberRMulticastSender *s;
  test_t tests[NUMBER_OF_TESTS] = {
    { (guint32)(~0 - NR_PACKETS/2), TRUE },
    { 0xff, TRUE },
    { 0xff, FALSE },
  };
  int i;

  g_type_init();
  GHashTable *senders = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                              NULL, g_object_unref);
  loop = g_main_loop_new(NULL, FALSE);

  serial_offset = tests[_i].serial_offset;
  expected = serial_offset;

  for (i = 0 ; receivers[i].receiver_id != 0; i++) {
    s = gibber_r_multicast_sender_new(receivers[i].receiver_id,
        receivers[i].name, senders);
    gibber_r_multicast_sender_seen(s, receivers[i].packet_id + 1);
    g_hash_table_insert(senders, GUINT_TO_POINTER(s->id), s);
  }
  s = gibber_r_multicast_sender_new(SENDER, SENDER_NAME, senders);
  g_signal_connect(s, "received-data", G_CALLBACK(data_received_cb), loop);
  g_signal_connect(s, "repair-request", G_CALLBACK(repair_request_cb), loop);

  if (tests[_i].test_seen) {
    gibber_r_multicast_sender_seen(s, serial_offset);
  } else {
   gibber_r_multicast_sender_repair_request(s, serial_offset);
  }

  g_timeout_add(10, add_packet, s);

  g_main_loop_run(loop);

  /* tell the sender we've seen some extra pakcets */
  gibber_r_multicast_sender_seen(s, serial_offset + NR_PACKETS + EXTRA_SEEN);
  g_main_loop_run(loop);

  /* Ask for a repair */
  g_signal_connect(s, "repair-message", G_CALLBACK(repair_message_cb), loop);

  gibber_r_multicast_sender_repair_request(s, serial_offset + REPAIR_PACKET);

  g_main_loop_run(loop);

  g_hash_table_destroy (senders);
  g_object_unref (s);
} END_TEST

TCase *
make_gibber_r_multicast_sender_tcase (void)
{
    TCase *tc = tcase_create ("RMulticast Sender");
    tcase_set_timeout (tc, 20);
    tcase_add_loop_test (tc, test_sender, 0, NUMBER_OF_TESTS);
    return tc;
}
