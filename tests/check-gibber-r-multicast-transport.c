/*
 * check-gibber-r-multicast-transport.c - R Multicast Transport test
 * Copyright (C) 2007 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <gibber/gibber-r-multicast-transport.h>
#include <gibber/gibber-r-multicast-packet.h>
#include "test-transport.h"

#include <check.h>

/* Numer of polls we expect the id generation to do */
#define ID_GENERATION_EXPECTED_POLLS 3

#define TEST_DATA_SIZE 300
GMainLoop *loop;

GibberRMulticastTransport *
create_rmulticast_transport (TestTransport **testtransport,
                             const gchar *name,
                             test_transport_send_hook test_send_hook,
                             gpointer user_data)
{
  TestTransport *t;
  GibberRMulticastTransport *rmtransport;

  t = test_transport_new (test_send_hook, user_data);
  fail_unless (t != NULL);
  GIBBER_TRANSPORT (t)->max_packet_size = 150;

  rmtransport = gibber_r_multicast_transport_new (
      GIBBER_TRANSPORT(t), "test123");

  if (testtransport != NULL)
    {
      *testtransport = t;
    }

  test_transport_set_echoing (t, TRUE);
  g_object_unref (t);

  return rmtransport;
}

/* test depends test */
struct {
  gchar *name;
  guint32 sender_id;
  guint32 packet_id;
  gboolean seen;
} senders[] = {
                { "test0", 1,    0xff, FALSE },
                { "test1", 2,  0xffff, FALSE },
                { "test2", 3, 0xffffff, FALSE },
                { "test3", 4, 0xaaaaaa, FALSE },
                { "test4", 5, 0xabcdab, FALSE },
                { NULL,    0,        0, FALSE }
};

gboolean
depends_send_hook (GibberTransport *transport,
                   const guint8 *data,
                   gsize length,
                   GError **error,
                   gpointer user_data)
{
  GibberRMulticastPacket *packet;
  int i;
  GList *l;

  packet = gibber_r_multicast_packet_parse (data, length, NULL);
  fail_unless (packet != NULL);

  if (packet->type == PACKET_TYPE_WHOIS_REQUEST) {
    GibberRMulticastPacket *reply;
    guint8 *pdata;
    gsize psize;

    for (i = 0; senders[i].name != NULL; i++) {
      if (senders[i].sender_id == packet->data.whois_request.sender_id) {
        break;
      }
    }
    if (senders[i].name == NULL && packet->sender == 0) {
      /* unique id polling */
      goto out;
    }
    fail_unless(senders[i].name != NULL);

    reply = gibber_r_multicast_packet_new(PACKET_TYPE_WHOIS_REPLY,
      senders[i].sender_id, transport->max_packet_size);

    gibber_r_multicast_packet_set_whois_reply_info (reply, senders[i].name);

    pdata = gibber_r_multicast_packet_get_raw_data (reply, &psize);
    test_transport_write (TEST_TRANSPORT(transport), pdata, psize);
    g_object_unref(reply);
  }

  if (packet->type != PACKET_TYPE_DATA)
    {
      goto out;
    }

  fail_unless(g_list_length (packet->data.data.depends) > 0);

  for (l = packet->data.data.depends; l != NULL; l = g_list_next (l))
    {
      for (i = 0; senders[i].name != NULL ; i++)
        {
          GibberRMulticastPacketSenderInfo *sender_info =
              (GibberRMulticastPacketSenderInfo *)l->data;
          if (senders[i].sender_id == sender_info->sender_id)
            {
              fail_unless (senders[i].seen == FALSE);
              fail_unless (senders[i].packet_id == sender_info->packet_id);
              senders[i].seen = TRUE;
              break;
            }
        }
        fail_unless (senders[i].name != NULL);
      }

  for (i = 0; senders[i].name != NULL ; i++)
    {
      fail_unless (senders[i].seen, "Not all senders in depends");
    }

  g_object_unref (packet);
  g_main_loop_quit (loop);
out:
  return TRUE;
}

static gboolean
depends_send_test_data (gpointer data)
{
  GibberRMulticastTransport *t = GIBBER_R_MULTICAST_TRANSPORT (data);
  guint8 testdata[] = { 1, 2, 3 };

  fail_unless (gibber_transport_send (GIBBER_TRANSPORT (t), testdata,
    3, NULL));

  return FALSE;
}

static void
depends_connected (GibberTransport *transport, gpointer user_data)
{
  GibberRMulticastTransport *rmtransport
    = GIBBER_R_MULTICAST_TRANSPORT(transport);
  TestTransport *testtransport = TEST_TRANSPORT (user_data);
  int i;

  /* First input some data packets, so the transport is forced to generate
   * dependency info */
  for (i = 0 ; senders[i].name != NULL; i++)
    {
      GibberRMulticastPacket *packet;
      guint8 *data;
      gsize size;
      packet = gibber_r_multicast_packet_new (PACKET_TYPE_DATA,
         senders[i].sender_id,
         GIBBER_TRANSPORT (testtransport)->max_packet_size);

      gibber_r_multicast_packet_set_data_info (packet, senders[i].packet_id,
          0, 0, 1);
      data = gibber_r_multicast_packet_get_raw_data (packet, &size);
      test_transport_write (testtransport, data, size);
      g_object_unref (packet);
    }

  /* Wait more then 200 ms, so all senders can get go to running */
  g_timeout_add (300, depends_send_test_data, rmtransport);
}

START_TEST (test_depends)
{
  GibberRMulticastTransport *rmtransport;
  TestTransport *testtransport;
  int i;

  loop = g_main_loop_new (NULL, FALSE);

  rmtransport = create_rmulticast_transport (&testtransport, "test123",
       depends_send_hook, NULL);

  g_signal_connect(rmtransport, "connected",
      G_CALLBACK(depends_connected), testtransport);

  fail_unless (gibber_r_multicast_transport_connect (rmtransport,
      FALSE, NULL));

  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  for (i = 0 ; senders[i].name != NULL; i++) 
    {
      fail_unless (senders[i].seen);
    }

  g_object_unref (rmtransport);
}
END_TEST


/* test fragmentation testing */
gboolean
fragmentation_send_hook (GibberTransport *transport,
                         const guint8 *data,
                         gsize length,
                         GError **error,
                         gpointer user_data)
{
  GibberRMulticastPacket *packet;
  static gsize bytes = 0;
  static guint8 next_byte = 0;
  gsize i;
  gsize size;
  guint8 *payload;

  packet = gibber_r_multicast_packet_parse (data, length, NULL);
  fail_unless (packet != NULL);

  if (packet->type != PACKET_TYPE_DATA)
    {
      goto out;
    }

  payload = gibber_r_multicast_packet_get_payload (packet, &size);
  bytes += size;

  fail_unless (bytes <= TEST_DATA_SIZE);

  /* check our bytes */
  for (i = 0; i < size; i++)
    {
      fail_unless (payload[i] == next_byte);
      next_byte++;
    }

  if (bytes == TEST_DATA_SIZE)
    {
      g_object_unref (packet);
      g_main_loop_quit (loop);
      return FALSE;
    }

out:
  g_object_unref (packet);
  return TRUE;
}

static void
fragmentation_connected (GibberTransport *transport, gpointer user_data) {
  GibberRMulticastTransport *rmtransport
    = GIBBER_R_MULTICAST_TRANSPORT(transport);
  guint8 testdata[TEST_DATA_SIZE];
  int i;

  for (i = 0; i < TEST_DATA_SIZE; i++)
    {
      testdata[i] = (guint8) (i & 0xff);
    }

  fail_unless (gibber_transport_send (GIBBER_TRANSPORT (rmtransport),
      (guint8 *)testdata, TEST_DATA_SIZE, NULL));
}

START_TEST (test_fragmentation)
{
  GibberRMulticastTransport *rmtransport;

  loop = g_main_loop_new (NULL, FALSE);

  rmtransport = create_rmulticast_transport (NULL, "test123",
       fragmentation_send_hook, NULL);

  g_signal_connect(rmtransport, "connected",
      G_CALLBACK(fragmentation_connected), NULL);

  fail_unless (gibber_r_multicast_transport_connect (rmtransport,
     FALSE, NULL));

  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  g_object_unref (rmtransport);
}
END_TEST


/* test unique id */
gboolean
unique_id_send_hook (GibberTransport *transport,
                     const guint8 *data,
                     gsize length,
                     GError **error,
                     gpointer user_data)
{
  GibberRMulticastPacket *packet;
  guint32 *test_id = (guint32 *)user_data;


  packet = gibber_r_multicast_packet_parse (data, length, NULL);

  if (*test_id == 0) {
    /* force collision */
    GibberRMulticastPacket *reply;
    guint8 *pdata;
    gsize psize;

    /* First packet most be a whois request to see if the id is taken */
    fail_unless (packet->type == PACKET_TYPE_WHOIS_REQUEST);
    /* Sender must be 0 as it couldn't choose a id just yet */
    fail_unless (packet->sender == 0);

    *test_id = packet->data.whois_request.sender_id;

    reply = gibber_r_multicast_packet_new(PACKET_TYPE_WHOIS_REPLY,
      *test_id, transport->max_packet_size);

    gibber_r_multicast_packet_set_whois_reply_info(reply, "romeo");

    pdata = gibber_r_multicast_packet_get_raw_data (reply, &psize);
    test_transport_write (TEST_TRANSPORT(transport), pdata, psize);
    g_object_unref (reply);
  } else {
    fail_unless (*test_id != packet->sender);
    switch (packet->type)
      {
        case PACKET_TYPE_WHOIS_REQUEST:
          fail_unless (*test_id != packet->data.whois_request.sender_id);
          break;
        case PACKET_TYPE_WHOIS_REPLY:
          /* transport sends a unsolicited whois reply after choosing a
           * identifier */
          g_main_loop_quit (loop);
          break;
        default:
          fail ("Unexpected packet type: %x", packet->type);
      }
  }

  g_object_unref(packet);
  return TRUE;
}

START_TEST (test_unique_id)
{
  /* Test if the multicast transport correctly handles the case that it gets a
   * WHOIS_REPLY on one of it's WHOIS_REQUESTS when it's determining a unique
   * id for itself */
  GibberRMulticastTransport *rmtransport;
  guint32 test_id;

  test_id = 0;
  loop = g_main_loop_new (NULL, FALSE);

  rmtransport = create_rmulticast_transport (NULL, "test123",
       unique_id_send_hook, &test_id);

  fail_unless (gibber_r_multicast_transport_connect (rmtransport,
      FALSE, NULL));

  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  g_object_unref (rmtransport);
}
END_TEST

/* test id generation conflict */
typedef struct {
  guint32 id;
  gint count;
  gint wait;
} unique_id_conflict_test_t;

gboolean
id_generation_conflict_send_hook (GibberTransport *transport,
                                  const guint8 *data,
                                  gsize length,
                                  GError **error,
                                  gpointer user_data)
{
  GibberRMulticastPacket *packet;
  unique_id_conflict_test_t *test = (unique_id_conflict_test_t *)user_data;

  packet = gibber_r_multicast_packet_parse (data, length, NULL);

  if (test->id == 0) {
    /* First packet most be a whois request to see if the id is taken */
    fail_unless (packet->type == PACKET_TYPE_WHOIS_REQUEST);
    /* Sender must be 0 as it couldn't choose a id just yet */
    fail_unless (packet->sender == 0);

    test->id = packet->data.whois_request.sender_id;
  }

  switch (packet->type)
    {
      case PACKET_TYPE_WHOIS_REQUEST:
        test->count++;
        if (test->count < test->wait)
          {
            fail_unless (test->id == packet->data.whois_request.sender_id);
          }
        else if (test->count == test->wait)
          {
            fail_unless (test->id == packet->data.whois_request.sender_id);
            /* force collision */
            GibberRMulticastPacket *reply;
            guint8 *pdata;
            gsize psize;

            reply = gibber_r_multicast_packet_new(PACKET_TYPE_WHOIS_REQUEST,
              0, transport->max_packet_size);

            gibber_r_multicast_packet_set_whois_request_info(packet, test->id);

            pdata = gibber_r_multicast_packet_get_raw_data (reply, &psize);
            test_transport_write (TEST_TRANSPORT(transport), pdata, psize);
            g_object_unref (reply);
           }
        else if (test->count > test->wait)
           {
             fail_unless (test->id != packet->data.whois_request.sender_id);
           }
        break;
      case PACKET_TYPE_WHOIS_REPLY:
      /* transport sends a unsolicited whois reply after choosing a
         * identifier */
        fail_unless (packet->sender != test->id);
        fail_unless (test->count ==
            ID_GENERATION_EXPECTED_POLLS + test->wait);
        g_main_loop_quit (loop);
        break;
      default:
        fail ("Unexpected packet type: %x", packet->type);
    }

  g_object_unref(packet);
  return TRUE;
}

START_TEST (test_id_generation_conflict)
{
  /* Test if the multicast transport correctly handles the case that it sees
   * another WHOIS_REQUEST on one of it's WHOIS_REQUESTS when it's determining
   * a unique id for itself */
  GibberRMulticastTransport *rmtransport; unique_id_conflict_test_t test;

  test.id = 0;
  test.count = 0;
  test.wait = _i + 1;

  loop = g_main_loop_new (NULL, FALSE);

  rmtransport = create_rmulticast_transport (NULL, "test123",
       id_generation_conflict_send_hook, &test);

  fail_unless (gibber_r_multicast_transport_connect (rmtransport,
      FALSE, NULL));

  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  g_object_unref (rmtransport);
}
END_TEST


TCase *
make_gibber_r_multicast_transport_tcase (void)
{
  TCase *tc = tcase_create ("Gibber R Multicast transport");
  tcase_add_test (tc, test_unique_id);
  tcase_add_loop_test (tc, test_id_generation_conflict, 0,
      ID_GENERATION_EXPECTED_POLLS);
  tcase_add_test (tc, test_fragmentation);
  tcase_add_test (tc, test_depends);

  return tc;
}
