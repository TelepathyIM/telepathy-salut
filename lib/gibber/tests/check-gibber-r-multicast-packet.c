#include "config.h"

#include <stdio.h>
#include <string.h>

#include <gibber/gibber-r-multicast-packet.h>

#define COMPARE(x) G_STMT_START { \
  g_assert (a->x == b->x); \
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

static void
test_r_multicast_packet_diff (gint _i)
{
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
  gint32 result = gibber_r_multicast_packet_diff (c->a, c->b);
  g_assert (c->result == result);
}

static void
test_r_multicast_packet_diff_loop (void)
{
  gint i;
  for (i = 0; i < NUMBER_OF_DIFF_TESTS; ++i)
    test_r_multicast_packet_diff (i);
}

static void
test_data_packet (void)
{
  GibberRMulticastPacket *a;
  GibberRMulticastPacket *b;
  guint32 sender_id = 1234;
  guint32 packet_id = 1200;
  guint8 flags = GIBBER_R_MULTICAST_DATA_PACKET_START;
  guint32 total_size = 800;
  guint16 stream_id = 56;
  guint8 *data;
  gsize len;
  guint8 *pdata;
  gsize plen;
  guint i,n;
  sender_t senders[] =
    { { 0x300, 500, FALSE }, { 0x400, 600, FALSE }, { 0, 0, FALSE } };
  gchar *payload = "1234567890";

  g_type_init ();

  a = gibber_r_multicast_packet_new (PACKET_TYPE_DATA, sender_id, 1500);
  gibber_r_multicast_packet_set_packet_id (a, packet_id);
  gibber_r_multicast_packet_set_data_info (a, stream_id, flags, total_size);

  for (i = 0 ; senders[i].sender_id != 0; i++)
    {
      gibber_r_multicast_packet_add_sender_info (a,
          senders[i].sender_id, senders[i].packet_id, NULL);
    }

  gibber_r_multicast_packet_add_payload (a, (guint8 *) payload,
      strlen (payload));

  data = gibber_r_multicast_packet_get_raw_data (a, &len);

  b = gibber_r_multicast_packet_parse (data, len, NULL);
  g_assert (b != NULL);

  COMPARE (type);
  COMPARE (version);
  COMPARE (data.data.flags);
  COMPARE (data.data.total_size);
  COMPARE (packet_id);
  COMPARE (data.data.stream_id);

  g_assert (a->sender == b->sender);

  for (n = 0 ; n < b->depends->len; n++)
    {
      for (i = 0; senders[i].sender_id != 0 ; i++)
        {
          GibberRMulticastPacketSenderInfo *s = g_array_index (b->depends,
                  GibberRMulticastPacketSenderInfo *, n);
          if (senders[i].sender_id == s->sender_id)
            {
              g_assert (senders[i].packet_id == s->packet_id);
              g_assert (senders[i].seen == FALSE);
              senders[i].seen = TRUE;
              break;
            }
        }

      g_assert (senders[i].sender_id != 0);
    }

  for (i = 0; senders[i].sender_id != 0 ; i++)
    {
      g_assert (senders[i].seen == TRUE);
    }


  pdata = gibber_r_multicast_packet_get_payload (b, &plen);
  g_assert (plen == strlen (payload));

  g_assert (memcmp (payload, pdata, plen) == 0);

  g_object_unref (a);
  g_object_unref (b);
}

static void
test_attempt_join_packet (void)
{
  GibberRMulticastPacket *a;
  GibberRMulticastPacket *b;
  guint32 sender_id = 1234;
  guint32 packet_id = 1200;
  guint8 *data;
  gsize len;
  guint i, n;
  sender_t senders[] =
    { { 0x300, 500, FALSE }, { 0x400, 600, FALSE }, { 0, 0, FALSE } };
  sender_t new_senders[] =
    { { 0x500, 0, FALSE }, { 0x600, 0, FALSE }, { 0, 0, FALSE } };

  g_type_init ();

  a = gibber_r_multicast_packet_new (PACKET_TYPE_ATTEMPT_JOIN,
      sender_id, 1500);
  gibber_r_multicast_packet_set_packet_id (a, packet_id);

  for (i = 0; senders[i].sender_id != 0; i++)
    {
      gibber_r_multicast_packet_add_sender_info (a,
          senders[i].sender_id, senders[i].packet_id, NULL);
    }
  for (i = 0; new_senders[i].sender_id != 0; i++)
    {
      gibber_r_multicast_packet_attempt_join_add_sender (a,
          new_senders[i].sender_id, NULL);
    }

  data = gibber_r_multicast_packet_get_raw_data (a, &len);

  b = gibber_r_multicast_packet_parse (data, len, NULL);

  g_assert (b != NULL);

  COMPARE (type);
  COMPARE (version);
  COMPARE (packet_id);
  COMPARE (data.attempt_join.senders->len);

  g_assert (a->sender == b->sender);

  for (n = 0; n < b->depends->len; n++)
    {
      for (i = 0; senders[i].sender_id != 0 ; i++)
        {
          GibberRMulticastPacketSenderInfo *s = g_array_index (b->depends,
                  GibberRMulticastPacketSenderInfo *, n);
          if (senders[i].sender_id == s->sender_id)
            {
              g_assert (senders[i].packet_id == s->packet_id);
              g_assert (senders[i].seen == FALSE);
              senders[i].seen = TRUE;
              break;
            }
        }

      g_assert (senders[i].sender_id != 0);
    }

  for (i = 0; senders[i].sender_id != 0; i++)
    {
      g_assert (senders[i].seen == TRUE);
    }

  for (i = 0; new_senders[i].sender_id != 0; i++)
    {
       g_assert (new_senders[i].sender_id ==
           g_array_index (b->data.attempt_join.senders, guint32, i));
       new_senders[i].seen = TRUE;
       break;
    }

  for (i = 0; new_senders[i].sender_id != 0; i++)
    {
      g_assert (senders[i].seen == TRUE);
    }

  g_object_unref (a);
  g_object_unref (b);
}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_type_init ();

  g_test_add_func ("/gibber/r-multicast-packet/data-packet", test_data_packet);
  g_test_add_func ("/gibber/r-multicast-packet/attempt-join-packet",
      test_attempt_join_packet);
  g_test_add_func ("/gibber/r-multicast-packet/diff",
      test_r_multicast_packet_diff_loop);

  return g_test_run ();
}
