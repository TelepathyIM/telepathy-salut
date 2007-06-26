#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gibber/gibber-r-multicast-sender.h>

#include <check.h>

#define SENDER "testsender"

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
  const gchar *id;
  guint32 packet_id;
} recv_t;

GMainLoop *loop;
recv_t receivers[] = {
    { "receiver1", 500 },
    { "receiver2", 600 },
    { NULL, 0 }
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
  p = gibber_r_multicast_packet_new(PACKET_TYPE_DATA, SENDER, serial,
      (serial % G_MAXUINT8) - part, 1500);

  gibber_r_multicast_packet_set_part(p, part, total);

  for (i = 0 ; receivers[i].id != NULL; i++) {
    gibber_r_multicast_packet_add_receiver(p,
        receivers[i].id, receivers[i].packet_id, NULL);
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
  fail_unless (packet->packet_id == REPAIR_PACKET + serial_offset);

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
  GHashTable *senders = g_hash_table_new_full(g_str_hash, g_str_equal,
                                              NULL, g_object_unref);
  loop = g_main_loop_new(NULL, FALSE);

  serial_offset = tests[_i].serial_offset;
  expected = serial_offset;

  for (i = 0 ; receivers[i].id != NULL; i++) {
    s = gibber_r_multicast_sender_new(receivers[i].id, senders);
    gibber_r_multicast_sender_seen(s, receivers[i].packet_id + 1);
    g_hash_table_insert(senders, s->name, s);
  }
  s = gibber_r_multicast_sender_new(SENDER, senders);
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

  g_hash_table_unref(senders);
} END_TEST

TCase *
make_gibber_r_multicast_sender_tcase (void)
{
    TCase *tc = tcase_create ("RMulticast Sender");
    tcase_set_timeout (tc, 20);
    tcase_add_loop_test (tc, test_sender, 0, NUMBER_OF_TESTS);
    return tc;
}
