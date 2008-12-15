#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gibber/gibber-r-multicast-sender.h>

#include <check.h>
#include "check-gibber.h"

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

static GibberRMulticastPacket *
generate_packet (guint32 serial)
{
  GibberRMulticastPacket *p;
  guint8 flags = 0;
  gint total = 1;
  gchar *payload;
  guint8 stream_id = 0;
  int i;

  switch (serial % 3)
    {
      case 0:
       flags = GIBBER_R_MULTICAST_DATA_PACKET_START
           | GIBBER_R_MULTICAST_DATA_PACKET_END;
       stream_id = serial % G_MAXUINT8;
       break;
      case 1:
       flags = GIBBER_R_MULTICAST_DATA_PACKET_START;
       stream_id = serial % G_MAXUINT8;
       total = 2;
       break;
      case 2:
       flags = GIBBER_R_MULTICAST_DATA_PACKET_END;
       stream_id = (serial - 1) % G_MAXUINT8;
       total = 2;
       break;
    }

  p = gibber_r_multicast_packet_new (PACKET_TYPE_DATA, SENDER, 1500);

  gibber_r_multicast_packet_set_packet_id (p, serial);
  gibber_r_multicast_packet_set_data_info (p, stream_id, flags, total * 11);

  for (i = 0 ; receivers[i].receiver_id != 0; i++)
    {
      gibber_r_multicast_packet_add_sender_info (p,
          receivers[i].receiver_id, receivers[i].packet_id, NULL);
    }

  payload = g_strdup_printf ("%010d\n", serial);
  gibber_r_multicast_packet_add_payload (p, (guint8 *) payload,
      strlen (payload));

  g_free (payload);
  return p;
}

static void
data_received_cb (GibberRMulticastSender *sender, guint8 stream_id,
    guint8 *data, gsize size, gpointer user_data)
{
  gchar *str;
  gchar **lines;
  int i;

  str = g_strndup ((const gchar *)data, size);

  lines = g_strsplit (str, "\n", 0);
  for (i = 0 ; lines[i] != NULL && *lines[i] != '\0'; i++) {
    guint32 v = atoi (lines[i]);

    fail_unless (v == expected);
    fail_unless ((v % G_MAXUINT8) - i == stream_id);
    expected++;
  }
  /* serial % 3 is send out in a single packet the other two together.
   * So expected can't be  % 3 == 2 here */
  fail_if (expected % 3 == 2);

  if (expected == serial_offset + NR_PACKETS
      || expected == serial_offset + NR_PACKETS + EXTRA_SEEN) {
    g_main_loop_quit ((GMainLoop *) user_data);
  }

  g_strfreev (lines);
  g_free (str);

}

static void
repair_request_cb (GibberRMulticastSender *sender, guint id, gpointer data)
{
  GibberRMulticastPacket *p;

  fail_unless (gibber_r_multicast_packet_diff (serial_offset, id) >= 0
               || gibber_r_multicast_packet_diff (id,
                  serial_offset + NR_PACKETS + EXTRA_SEEN) < 0);

  p = generate_packet (id);
  gibber_r_multicast_sender_push (sender, p);
  g_object_unref (p);
}

static void
repair_message_cb (GibberRMulticastSender *sender,
    GibberRMulticastPacket *packet, gpointer user_data)
{

  fail_unless (packet->type == PACKET_TYPE_DATA);
  fail_unless (packet->packet_id == REPAIR_PACKET + serial_offset);

  g_main_loop_quit ((GMainLoop *) user_data);
}

static gboolean
add_packet (gpointer data)
{
  static guint32 i = 0;
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER (data);
  GibberRMulticastPacket *p;

  if (i == NR_PACKETS)
    {
      i = 0;
      return FALSE;
    }

  if (i % 5 != 3)
    {
      p = generate_packet (i + serial_offset);
      gibber_r_multicast_sender_push (sender, p);
      g_object_unref (p);
    }

  i++;
  return TRUE;
}


#define NUMBER_OF_TESTS 3

START_TEST (test_sender)
{
  GibberRMulticastSender *s;
  GibberRMulticastSenderGroup *group;
  test_t tests[NUMBER_OF_TESTS] = {
    { (guint32)(~0 - NR_PACKETS/2), TRUE },
    { 0xff, TRUE },
    { 0xff, FALSE },
  };
  int i;

  g_type_init ();
  group = gibber_r_multicast_sender_group_new ();
  loop = g_main_loop_new (NULL, FALSE);

  serial_offset = tests[_i].serial_offset;
  expected = serial_offset;

  for (i = 0 ; receivers[i].receiver_id != 0; i++)
    {
      s = gibber_r_multicast_sender_new (receivers[i].receiver_id,
          receivers[i].name, group);
      gibber_r_multicast_sender_update_start (s, receivers[i].packet_id);
      gibber_r_multicast_sender_seen (s, receivers[i].packet_id + 1);
      gibber_r_multicast_sender_group_add (group, s);
    }

  s = gibber_r_multicast_sender_new (SENDER, SENDER_NAME, group);
  g_signal_connect (s, "received-data", G_CALLBACK(data_received_cb), loop);
  g_signal_connect (s, "repair-request", G_CALLBACK(repair_request_cb), loop);

  gibber_r_multicast_sender_update_start (s, serial_offset);
  gibber_r_multicast_sender_set_data_start (s, serial_offset);

  if (tests[_i].test_seen)
    {
      gibber_r_multicast_sender_seen (s, serial_offset);
    }
  else
    {
     gibber_r_multicast_sender_repair_request (s, serial_offset);
    }

  g_timeout_add (10, add_packet, s);

  g_main_loop_run (loop);

  /* tell the sender we've seen some extra pakcets */
  gibber_r_multicast_sender_seen (s, serial_offset + NR_PACKETS + EXTRA_SEEN);
  g_main_loop_run (loop);

  /* Ask for a repair */
  g_signal_connect (s, "repair-message", G_CALLBACK (repair_message_cb), loop);

  gibber_r_multicast_sender_repair_request (s, serial_offset + REPAIR_PACKET);

  g_main_loop_run (loop);

  gibber_r_multicast_sender_group_free (group);
} END_TEST

/* Holding test */
guint32 idle_timer = 0;

typedef struct {
  gchar *name;
  guint32 packet_id;
  GibberRMulticastPacketType packet_type;
  gchar *data;
  gchar *depend_node;
  guint32 depend_packet_id;
  guint16 data_stream_id;
  guint8 flags;
  guint32 total_size;
} h_setup_t;

typedef enum {
  EXPECT = 0,
  START_DATA,
  FAIL,
  HOLD,
  UNHOLD,
  UNHOLD_IMMEDIATE,
  DONE
} h_expect_type_t;

typedef struct {
  h_expect_type_t type;
  gchar *expected_node;
  GibberRMulticastPacketType packet_type;
  guint32 hold_id;
  guint32 data_stream_id;
} h_expect_t;

typedef struct {
  int test_step;
  GibberRMulticastSenderGroup *group;
  h_expect_t *expectation;
} h_data_t;

typedef struct {
  h_setup_t *setup;
  h_expect_t *expectation;
} h_test_t;

static void h_next_test_step (h_data_t *d);

static gboolean
h_find_sender (gpointer key, gpointer value, gpointer user_data)
{
  GibberRMulticastSender *s = GIBBER_R_MULTICAST_SENDER (value);

  return strcmp (s->name, (gchar *) user_data) == 0;
}

static gboolean
h_idle_next_step (gpointer user_data)
{
  h_data_t *d = (h_data_t *) user_data;
  h_expect_t *e = &(d->expectation[d->test_step]);
  GibberRMulticastSender *s;

  idle_timer = 0;

  switch (e->type) {
    case UNHOLD_IMMEDIATE:
    case START_DATA:
    case FAIL:
    case EXPECT:
      fail ("Should not be reached");
      break;
    case HOLD:
      s = g_hash_table_find (d->group->senders,
          h_find_sender, e->expected_node);
      fail_unless (s != NULL);
      d->test_step++;
      gibber_r_multicast_sender_hold_data (s, e->hold_id);
      h_next_test_step (d);
      break;
    case UNHOLD:
      s = g_hash_table_find (d->group->senders,
          h_find_sender, e->expected_node);
      fail_unless (s != NULL);
      d->test_step++;
      gibber_r_multicast_sender_release_data (s);
      h_next_test_step (d);
      break;
    case DONE:
      /* And there was great rejoice */
      g_main_loop_quit (loop);
      break;
  }

  return FALSE;
}

static void
h_next_test_step (h_data_t *d)
{
  GibberRMulticastSender *s;
  h_expect_t *e = &(d->expectation[d->test_step]);

  switch (d->expectation[d->test_step].type) {
    case EXPECT:
      break;
    case UNHOLD_IMMEDIATE:
      s = g_hash_table_find (d->group->senders,
          h_find_sender, e->expected_node);
      fail_unless (s != NULL);
      d->test_step++;
      gibber_r_multicast_sender_release_data (s);
      h_next_test_step (d);
      break;
    case START_DATA:
      s = g_hash_table_find (d->group->senders,
          h_find_sender, e->expected_node);
      fail_unless (s != NULL);
      d->test_step++;
      gibber_r_multicast_sender_set_data_start (s, e->hold_id);
      h_next_test_step (d);
      break;
    case FAIL:
      s = g_hash_table_find (d->group->senders,
          h_find_sender, e->expected_node);
      fail_unless (s != NULL);
      d->test_step++;
      gibber_r_multicast_sender_set_failed (s);
      h_next_test_step (d);
      break;
    case HOLD:
    case UNHOLD:
    case DONE:
      if (idle_timer == 0)
        {
          idle_timer = g_idle_add (h_idle_next_step, d);
        }
  }
}

static void
h_received_data_cb (GibberRMulticastSender *sender, guint16 stream_id,
    guint8 *data, gsize size, gpointer user_data)
{
  h_data_t *d = (h_data_t *) user_data;

  fail_unless (d->expectation[d->test_step].type == EXPECT);
  fail_unless (d->expectation[d->test_step].packet_type == PACKET_TYPE_DATA);
  fail_unless (
    strcmp (d->expectation[d->test_step].expected_node, sender->name) == 0);
  fail_unless (d->expectation[d->test_step].data_stream_id == stream_id);

  d->test_step++;
  h_next_test_step (d);
}

static void
h_received_control_packet_cb (GibberRMulticastSender *sender,
    GibberRMulticastPacket *packet, gpointer user_data)
{
  h_data_t *d = (h_data_t *) user_data;

  fail_unless (d->expectation[d->test_step].type == EXPECT);
  fail_unless (d->expectation[d->test_step].packet_type == packet->type);
  fail_unless (
    strcmp (d->expectation[d->test_step].expected_node, sender->name) == 0);

  d->test_step++;
  h_next_test_step (d);
}

h_setup_t h_setup0[] =  {
    { "node0", 0x1, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 0, 3, 3 },
    { "node1", 0x1, PACKET_TYPE_DATA,         "001",  "node0", 0x2, 0, 3, 3 },
    { "node0", 0x2, PACKET_TYPE_DATA,         "002",  "node1", 0x2, 0, 3, 3 },
    { "node1", 0x2, PACKET_TYPE_DATA,         "002",  "node0", 0x3, 0, 3, 3 },
    { "node0", 0x3, PACKET_TYPE_ATTEMPT_JOIN,  NULL,  "node1", 0x3 },
    { "node1", 0x3, PACKET_TYPE_ATTEMPT_JOIN,  NULL,  "node0", 0x4 },
    { "node0", 0x4, PACKET_TYPE_DATA,          "003", "node1", 0x4, 0, 3, 3 },
    { "node1", 0x4, PACKET_TYPE_DATA,          "003", "node0", 0x5, 0, 3, 3 },
    { "node0", 0x5, PACKET_TYPE_JOIN,          NULL,  "node1", 0x5 },
    { "node1", 0x5, PACKET_TYPE_JOIN,          NULL,  "node0", 0x6 },
    { NULL },
  };

h_expect_t h_expectation0[] = {
   { EXPECT, "node0", PACKET_TYPE_ATTEMPT_JOIN },
   { EXPECT, "node1", PACKET_TYPE_ATTEMPT_JOIN },
   { EXPECT, "node0", PACKET_TYPE_JOIN },
   { EXPECT, "node1", PACKET_TYPE_JOIN },
   /* Set the data start of node1 to 0x1, which means all the data should still
    * be popped off */
   { START_DATA, "node1", PACKET_TYPE_INVALID, 0x1 },
   { START_DATA, "node0", PACKET_TYPE_INVALID, 0x1 },
   /* only unhold node1, nothing should happen as they depend on those of
    * node0 */
   { HOLD,   "node1", PACKET_TYPE_INVALID, 0x3 },
   /* unhold node0 too, packets should start flowing */
   { HOLD,   "node0", PACKET_TYPE_INVALID, 0x3 },
   { EXPECT, "node0", PACKET_TYPE_DATA, 0, 0 },
   { EXPECT, "node1", PACKET_TYPE_DATA, 0, 0 },
   { EXPECT, "node0", PACKET_TYPE_DATA, 0, 0 },
   { EXPECT, "node1", PACKET_TYPE_DATA, 0, 0 },
   { UNHOLD, "node1" },
   { UNHOLD, "node0" },
   { EXPECT, "node0", PACKET_TYPE_DATA, 0, 0 },
   { EXPECT, "node1", PACKET_TYPE_DATA, 0, 0 },
   { DONE },
};

h_setup_t h_setup1[] =  {
    { "node0", 0x1, PACKET_TYPE_ATTEMPT_JOIN, "001",  "node1",    0x2 },
    { "node1", 0x1, PACKET_TYPE_ATTEMPT_JOIN, "001",  NULL },
    { NULL },
};

h_expect_t h_expectation1[] = {
   { EXPECT, "node1", PACKET_TYPE_ATTEMPT_JOIN },
   { EXPECT, "node0", PACKET_TYPE_ATTEMPT_JOIN },
   { DONE }
};

h_setup_t h_setup2[] =  {
    { "node0", 0x1, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 0, 1, 9 },
    { "node0", 0x3, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 0, 0, 9 },
    { "node0", 0x6, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 0, 2, 9 },
    { "node0", 0x2, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 1, 1, 6 },
    { "node0", 0x4, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 2, 3, 3 },
    { "node0", 0x5, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 1, 2, 6 },
    { NULL },
};

h_expect_t h_expectation2[] = {
   { START_DATA, "node0", PACKET_TYPE_INVALID, 0x1 },
   { UNHOLD, "node0" },
   { EXPECT, "node0", PACKET_TYPE_DATA, 0, 2 },
   { EXPECT, "node0", PACKET_TYPE_DATA, 0, 1 },
   { EXPECT, "node0", PACKET_TYPE_DATA, 0, 0 },
   { DONE }
};

h_setup_t h_setup3[] =  {
    { "node0", 0x1, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 0, 1, 9 },
    { "node0", 0x2, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 1, 1, 6 },
    { "node0", 0x3, PACKET_TYPE_ATTEMPT_JOIN,  NULL,  NULL },
    { "node0", 0x4, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 0, 0, 9 },
    { "node0", 0x5, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 2, 3, 3 },
    { "node0", 0x6, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 1, 2, 6 },
    { "node0", 0x7, PACKET_TYPE_ATTEMPT_JOIN,  NULL,  NULL },
    { "node0", 0x8, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 0, 2, 9 },
    { NULL },
};

h_expect_t h_expectation3[] = {
   { EXPECT, "node0", PACKET_TYPE_ATTEMPT_JOIN },
   { START_DATA, "node0", PACKET_TYPE_INVALID, 0x1 },
   { UNHOLD_IMMEDIATE, "node0" },
   { EXPECT, "node0", PACKET_TYPE_DATA, 0, 2 },
   { EXPECT, "node0", PACKET_TYPE_DATA, 0, 1 },
   { EXPECT, "node0", PACKET_TYPE_ATTEMPT_JOIN },
   { EXPECT, "node0", PACKET_TYPE_DATA, 0, 0 },
   { DONE }
};

h_setup_t h_setup4[] =  {
    { "node0", 0x1, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 0, 1, 9 },
    { "node0", 0x2, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 1, 1, 6 },
    { "node0", 0x3, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 0, 0, 9 },
    { "node0", 0x4, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 2, 3, 3 },
    { "node0", 0x5, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 1, 2, 6 },
    { "node0", 0x6, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 0, 2, 9 },
    { NULL },
};

h_expect_t h_expectation4[] = {
   { START_DATA, "node0", PACKET_TYPE_INVALID, 0x2 },
   { UNHOLD, "node0" },
   { EXPECT, "node0", PACKET_TYPE_DATA, 0, 2 },
   { EXPECT, "node0", PACKET_TYPE_DATA, 0, 1 },
   { DONE }
};

/* Test if failing a node correctly pops the minimum amount of packets needed
 * to fulfill all dependencies */
h_setup_t h_setup5[] =  {
    { "node1", 0x1, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 1, 3, 3 },
    { "node1", 0x2, PACKET_TYPE_DATA,         "001",  "node0", 0x3, 2, 3, 3 },

    /* As the very first thing do a Control packet, which isn't hold back. To
     * force the setting of FAIL (as the setup instructions are run as soon as
     * the join packet is received) */
    { "node0", 0x1, PACKET_TYPE_JOIN,         "001",  NULL },
    { "node0", 0x2, PACKET_TYPE_DATA,         "001",  "node1", 0x2, 2, 3, 3 },
    { "node0", 0x3, PACKET_TYPE_DATA,         "001",  NULL,    0x0, 0, 3, 3 },
    { NULL },
};

h_expect_t h_expectation5[] = {
   { EXPECT, "node0", PACKET_TYPE_JOIN, 0, 1 },
   { START_DATA, "node0", PACKET_TYPE_INVALID, 0x1 },
   { START_DATA, "node1", PACKET_TYPE_INVALID, 0x1 },
   { FAIL,   "node0", PACKET_TYPE_INVALID, 0x2 },
   { UNHOLD, "node0" },
   { UNHOLD, "node1" },
   { EXPECT, "node1", PACKET_TYPE_DATA, 0, 1 },
   { EXPECT, "node0", PACKET_TYPE_DATA, 0, 2 },
   { EXPECT, "node1", PACKET_TYPE_DATA, 0, 2 },
   { DONE }
};

#define NUMBER_OF_H_TESTS 6
h_test_t h_tests[NUMBER_OF_H_TESTS] = {
    { h_setup0, h_expectation0 },
    { h_setup1, h_expectation1 },
    { h_setup2, h_expectation2 },
    { h_setup3, h_expectation3 },
    { h_setup4, h_expectation4 },
    { h_setup5, h_expectation5 },
  };


static void
add_h_sender (guint32 sender, gchar *name, GibberRMulticastSenderGroup *group,
  guint32 packet_id, h_data_t *data)
{
  GibberRMulticastSender *s;

  s = gibber_r_multicast_sender_new (sender, name, group);
  gibber_r_multicast_sender_update_start (s, packet_id);
  gibber_r_multicast_sender_hold_data (s, packet_id);
  gibber_r_multicast_sender_group_add (group, s);

  g_signal_connect (s, "received-data",
     G_CALLBACK (h_received_data_cb), data);
  g_signal_connect (s, "received-control-packet",
     G_CALLBACK (h_received_control_packet_cb), data);
}

START_TEST (test_holding) {
  GibberRMulticastSenderGroup *group;
  guint32 sender_offset = 0xf00;
     /* control packets aren't hold back, thus we get them interleaved at first
      */
  h_test_t *test = h_tests + _i;
  h_data_t data = { 0, NULL, test->expectation };
  int i;

  g_type_init ();
  loop = g_main_loop_new (NULL, FALSE);

  group = gibber_r_multicast_sender_group_new ();
  data.group = group;

  for (i = 0; test->setup[i].name != NULL; i++)
    {
      GibberRMulticastSender *s;
      s =  g_hash_table_find (group->senders,
          h_find_sender, test->setup[i].name);
      if (s == NULL)
        {
          add_h_sender (sender_offset++, test->setup[i].name, group,
            test->setup[i].packet_id, &data);
        }
    }

  for (i = 0; test->setup[i].name != NULL; i++)
    {
      GibberRMulticastSender *s0, *s1 = NULL;
      GibberRMulticastPacket *p;

      s0 = g_hash_table_find (group->senders, h_find_sender,
          test->setup[i].name);
      fail_unless (s0 != NULL);

      p = gibber_r_multicast_packet_new (test->setup[i].packet_type, s0->id,
          1500);
      gibber_r_multicast_packet_set_packet_id (p, test->setup[i].packet_id);

      if (test->setup[i].depend_node != NULL)
        {
          s1 = g_hash_table_find (group->senders, h_find_sender,
              test->setup[i].depend_node);
          fail_unless (s1 != NULL);
          fail_unless (gibber_r_multicast_packet_add_sender_info (p, s1->id,
              test->setup[i].depend_packet_id, NULL));
        }
      if (test->setup[i].packet_type == PACKET_TYPE_DATA)
        {
          fail_unless (test->setup[i].data != NULL);

          gibber_r_multicast_packet_set_data_info (p,
            test->setup[i].data_stream_id,
            test->setup[i].flags,
            test->setup[i].total_size);
          gibber_r_multicast_packet_add_payload (p,
              (guint8 *) test->setup[i].data, strlen (test->setup[i].data));
        }
      gibber_r_multicast_sender_push (s0, p);

      g_object_unref (p);
    }

    h_next_test_step (&data);

    do
      {
        g_main_loop_run (loop);
      }
    while (data.expectation[data.test_step].type != DONE);

  fail_unless (idle_timer == 0);

} END_TEST

TCase *
make_gibber_r_multicast_sender_tcase (void)
{
    TCase *tc = tcase_create ("RMulticast Sender");
    tcase_set_timeout (tc, 20);
    tcase_add_loop_test (tc, test_sender, 0, NUMBER_OF_TESTS);
    tcase_add_loop_test (tc, test_holding, 0, NUMBER_OF_H_TESTS );
    return tc;
}
