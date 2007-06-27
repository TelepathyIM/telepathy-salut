#include <stdio.h>
#include <string.h>

#include <gibber/gibber-r-multicast-packet.h>

#include <check.h>

#define COMPARE(x) G_STMT_START { \
  fail_unless(a->x == b->x); \
} G_STMT_END

typedef struct {
  const gchar *name;
  guint32 packet_id;
} recv_t;

typedef struct {
  guint32 a;
  guint32 b;
  gint32 result;
} diff_testcase;

#define NUMBER_OF_DIFF_TESTS 15

START_TEST (test_r_multicast_packet_diff) {
  diff_testcase cases[NUMBER_OF_DIFF_TESTS] =
    { {                 0,                 0,            0 },
      {                10,                10,            0 },
      {                 5,                10,            5 },
      {                10,                 5,           -5 },
      {  G_MAXUINT32 - 10,                10,           21 },
      {       G_MAXUINT32,                 0,            1 },
      {                0 ,       G_MAXUINT32,           -1 },
      {       G_MAXUINT32,                10,           11 },
      {                10,       G_MAXUINT32,          -11 },
      {     G_MAXUINT32/2,       G_MAXUINT32,   G_MAXINT32 },
      {       G_MAXUINT32,     G_MAXUINT32/2,  -G_MAXINT32 },
      { G_MAXUINT32/2 - 1,       G_MAXUINT32,   G_MAXINT32 },
      {       G_MAXUINT32, G_MAXUINT32/2 - 1,  -G_MAXINT32 },
      {   G_MAXUINT32 - 5,                 5,           11 },
      {                 5,   G_MAXUINT32 - 5,          -11 },

    };

  diff_testcase *c = cases + _i;
  gint32 result = gibber_r_multicast_packet_diff(c->a, c->b);
  fail_unless (c->result == result);
} END_TEST

START_TEST (test_packet) {
  GibberRMulticastPacket *a;
  GibberRMulticastPacket *b;
  gchar *sender = "testsender";
  guint32 packet_id = 1200;
  guint8 part = 2, total = 3;
  guint8 stream_id = 56;
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

  a = gibber_r_multicast_packet_new(PACKET_TYPE_DATA, sender, packet_id,
       stream_id, 1500);
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
  COMPARE(stream_id);

  fail_unless(strcmp(a->sender, b->sender) == 0);

  l = b->receivers;
  for (i = 0;
       receivers[i].name != NULL && l != NULL; i++, l = g_list_next(l)) {
    GibberRMulticastReceiver *r = (GibberRMulticastReceiver *)l->data;

    fail_unless(receivers[i].packet_id == r->packet_id);
    fail_unless(strcmp(receivers[i].name, r->name) == 0);
  }
  fail_unless(receivers[i].name == NULL && l == NULL);

  pdata = gibber_r_multicast_packet_get_payload(b, &plen);
  fail_unless(plen == strlen(payload));

  fail_unless(memcmp(payload, pdata, plen) == 0);

  g_object_unref (a);
  g_object_unref (b);
} END_TEST

TCase *
make_gibber_r_multicast_packet_tcase (void)
{
    TCase *tc = tcase_create ("RMulticast Packet");
    tcase_add_test (tc, test_packet);
    tcase_add_loop_test (tc, test_r_multicast_packet_diff, 0, NUMBER_OF_DIFF_TESTS);
    return tc;
}
