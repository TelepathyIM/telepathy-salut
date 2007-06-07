#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gibber/gibber-r-multicast-sender.h>

#define SENDER "testsender"

#define REPAIR_PACKET 15

#define EXTRA_SEEN 11
#define NR_PACKETS 40
#define SERIAL_OFFSET (~0 - NR_PACKETS/2)

typedef struct {
  const gchar *id;
  guint32 expected_packet;
} recv_t;

GMainLoop *loop;

GibberRMulticastPacket *
generate_packet(guint32 serial) {
  GibberRMulticastPacket *p;
  guint8 part = 0, total = 1;
  gchar *payload;
  int i;
  recv_t receivers[] =  
    { { "receiver1", 500 }, { "receiver2", 600 }, { NULL, 0 } };

  p = gibber_r_multicast_packet_new(PACKET_TYPE_DATA, SENDER, serial, 1500);
  if (serial % 3 > 0) {
    part = serial % 3 - 1;
    total = 2;
  }

  gibber_r_multicast_packet_set_part(p, part, total);

  for (i = 0 ; receivers[i].id != NULL; i++) {
    gibber_r_multicast_packet_add_receiver(p, 
        receivers[i].id, receivers[i].expected_packet, NULL);
  }

  payload = g_strdup_printf("%010d\n", serial);
  gibber_r_multicast_packet_add_payload(p, (guint8 *)payload, strlen(payload));
  g_free(payload);
  return p;
}

void
data_received_cb(GibberRMulticastSender *sender, guint8 *data, 
    gsize size, gpointer user_data) {
  static int expected = SERIAL_OFFSET;
  gchar *str;
  gchar **lines;
  int i;
  str = g_strndup((const gchar *)data, size);

  lines = g_strsplit(str, "\n", 0);
  for (i = 0 ; lines[i] != NULL && *lines[i] != '\0'; i++) {
    int v = atoi(lines[i]);

    g_assert(v == expected);
    expected++;
  }
  /* serial % 3 is send out in a single packet the other two together.
   * So expected can't be  % 3 == 2 here */
  g_assert(expected % 3 != 2);

  if (expected == SERIAL_OFFSET + NR_PACKETS
      || expected == SERIAL_OFFSET + NR_PACKETS + EXTRA_SEEN) {
    g_main_loop_quit((GMainLoop *)user_data);
  }

  g_strfreev(lines);
  g_free(str);

}

void
repair_request_cb(GibberRMulticastSender *sender, guint id, gpointer data) {
  GibberRMulticastPacket *p;

  p = generate_packet(id);
  gibber_r_multicast_sender_push(sender, p);
  g_object_unref(p);
}

void
repair_message_cb(GibberRMulticastSender *sender,
                  GibberRMulticastPacket *packet,
                  gpointer user_data) {
  g_assert(packet->packet_id == REPAIR_PACKET + SERIAL_OFFSET);

  g_main_loop_quit((GMainLoop *)user_data);
}

static gboolean
add_packet(gpointer data) {
  static guint32 i = 0;
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER(data);
  GibberRMulticastPacket *p;

  if (i == NR_PACKETS)
    return FALSE;

  if (i % 5 != 3) {
    p = generate_packet(i + SERIAL_OFFSET);
    gibber_r_multicast_sender_push(sender, p);
    g_object_unref(p);
  }

  i++;
  return TRUE;
}

static gboolean
timeout(gpointer data) {
  printf("Test timeout\n");
  g_assert_not_reached();

  return FALSE;
}

int
main(int argc, char **argv) {
  GibberRMulticastSender *s;

  g_type_init();
  loop = g_main_loop_new(NULL, FALSE);

  s = gibber_r_multicast_sender_new(SENDER);
  g_signal_connect(s, "data-received", G_CALLBACK(data_received_cb), loop);
  g_signal_connect(s, "repair-request", G_CALLBACK(repair_request_cb), loop);

  g_timeout_add(100, add_packet, s);
  g_timeout_add(20000, timeout, loop);

  g_main_loop_run(loop);

  /* tell the sender we've seen some extra pakcets */
  gibber_r_multicast_sender_seen(s, 
      SERIAL_OFFSET + NR_PACKETS + EXTRA_SEEN - 1);
  g_main_loop_run(loop);

  /* Ask for a repair */
  g_signal_connect(s, "repair-message", G_CALLBACK(repair_message_cb), loop);
  gibber_r_multicast_sender_repair_request(s, SERIAL_OFFSET + REPAIR_PACKET);
  g_main_loop_run(loop);

  return 0;
}
