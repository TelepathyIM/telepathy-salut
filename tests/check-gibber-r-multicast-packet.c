#include <stdio.h>
#include <string.h>

#include <gibber/gibber-r-multicast-packet.h>

#include <check.h>

#define COMPARE(x) G_STMT_START { \
  fail_unless(a->x == b->x); \
} G_STMT_END

typedef struct {
  guint32 sender_id;
  guint32 packet_id;
  gboolean seen;
} sender_t;

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
  guint32 sender_id = 1234;
  guint32 packet_id = 1200;
  guint8 part = 2, total = 3;
  guint8 stream_id = 56;
  guint8 *data;
  gsize len;
  guint8 *pdata;
  gsize plen;
  GList *l;
  int i;
  sender_t senders[] =
    { { 0x300, 500, FALSE }, { 0x400, 600, FALSE }, { 0, 0, FALSE } };
  gchar *payload = "1234567890";

  g_type_init();

  a = gibber_r_multicast_packet_new(PACKET_TYPE_DATA, sender_id, 1500);
  gibber_r_multicast_packet_set_data_info(a, packet_id,
      stream_id, part, total);

  for (i = 0 ; senders[i].sender_id != 0; i++) {
    gibber_r_multicast_packet_add_sender_info(a,
        senders[i].sender_id, senders[i].packet_id, NULL);
  }
  gibber_r_multicast_packet_add_payload(a, (guint8 *)payload, strlen(payload));

  data = gibber_r_multicast_packet_get_raw_data(a, &len);

  b = gibber_r_multicast_packet_parse(data, len, NULL);

  COMPARE(type);
  COMPARE(version);
  COMPARE(data.data.packet_part);
  COMPARE(data.data.packet_total);
  COMPARE(packet_id);
  COMPARE(data.data.stream_id);

  fail_unless(a->sender == b->sender);

  for (l = b->depends; l != NULL ; l = g_list_next(l))
    {
      for (i = 0; senders[i].sender_id != 0 ; i++)
        {
          GibberRMulticastPacketSenderInfo *s =
              (GibberRMulticastPacketSenderInfo *)l->data;
          if (senders[i].sender_id == s->sender_id) {
            fail_unless(senders[i].packet_id == s->packet_id);
            fail_unless(senders[i].seen == FALSE);
            senders[i].seen = TRUE;
            break;
          }
        }
      fail_unless(senders[i].sender_id != 0);
    }

  for (i = 0; senders[i].sender_id != 0 ; i++)
    {
      fail_unless(senders[i].seen == TRUE);
    }


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
