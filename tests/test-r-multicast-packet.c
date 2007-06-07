#include <stdio.h>
#include <string.h>

#include <gibber/gibber-r-multicast-packet.h>

#define COMPARE(x) G_STMT_START { \
  g_assert(a->x == b->x); \
} G_STMT_END

typedef struct {
  const gchar *name;
  guint32 packet_id;
} recv_t;

int
main(int argc, char **argv) {
  GibberRMulticastPacket *a;
  GibberRMulticastPacket *b;
  gchar *sender = "testsender";
  guint32 packet_id = 1200;
  guint8 part = 2, total = 3;
  guint8 *data;
  gsize len;
  guint8 *pdata;
  gsize plen;
  GList *l;
  int i;
  recv_t receivers[] =  
    { { "receiver1", 500 }, { "receiver2", 600 }, { NULL, 0 } };
  gchar *payload = "1234567890";

  g_type_init();

  a = gibber_r_multicast_packet_new(PACKET_TYPE_DATA, sender, packet_id, 1500);
  gibber_r_multicast_packet_set_part(a, part, total);

  for (i = 0 ; receivers[i].name != NULL; i++) {
    gibber_r_multicast_packet_add_receiver(a, 
        receivers[i].name, receivers[i].packet_id, NULL);
  }
  gibber_r_multicast_packet_add_payload(a, (guint8 *)payload, strlen(payload));

  data = gibber_r_multicast_packet_get_raw_data(a, &len);

  b = gibber_r_multicast_packet_parse(data, len, NULL);

  COMPARE(type);
  COMPARE(version);
  COMPARE(packet_part);
  COMPARE(packet_total);
  COMPARE(packet_id);

  g_assert(strcmp(a->sender, b->sender) == 0);

  l = b->receivers;
  for (i = 0;
       receivers[i].name != NULL && l != NULL; i++, l = g_list_next(l)) {
    GibberRMulticastReceiver *r = (GibberRMulticastReceiver *)l->data;

    g_assert(receivers[i].packet_id == r->packet_id);
    g_assert(strcmp(receivers[i].name, r->name) == 0);
  }
  g_assert(receivers[i].name == NULL && l == NULL);

  pdata = gibber_r_multicast_packet_get_payload(b, &plen);
  g_assert(plen == strlen(payload));

  g_assert(memcmp(payload, pdata, plen) == 0);

  return 0;
}
