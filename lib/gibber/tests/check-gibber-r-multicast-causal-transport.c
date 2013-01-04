/*
 * check-gibber-r-multicast-causal-transport.c
 *    - R Multicast CausalTransport test
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

#include "config.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <gibber/gibber-r-multicast-causal-transport.h>
#include <gibber/gibber-r-multicast-packet.h>
#include "test-transport.h"

/* Numer of polls we expect the id generation to do */
#define ID_GENERATION_EXPECTED_POLLS 3

/* Assume mtu is 1500, we want at least 3 packets */
#define TEST_DATA_SIZE 3300
GMainLoop *loop;

static GibberRMulticastCausalTransport *
create_rmulticast_transport (TestTransport **testtransport,
                             const gchar *name,
                             test_transport_send_hook test_send_hook,
                             gpointer user_data)
{
  TestTransport *t;
  GibberRMulticastCausalTransport *rmctransport;

  t = test_transport_new (test_send_hook, user_data);
  g_assert (t != NULL);
  GIBBER_TRANSPORT (t)->max_packet_size = 150;

  rmctransport = gibber_r_multicast_causal_transport_new
      (GIBBER_TRANSPORT(t), "test123");
  g_object_unref (t);

  if (testtransport != NULL)
    {
      *testtransport = t;
    }

  test_transport_set_echoing (t, TRUE);

  return rmctransport;
}

static void
rmulticast_connect (GibberRMulticastCausalTransport *transport)
{

  g_assert (transport != NULL);

  g_assert (gibber_r_multicast_causal_transport_connect (transport,
      FALSE, NULL));
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

static gboolean
depends_send_hook (GibberTransport *transport,
                   const guint8 *data,
                   gsize length,
                   GError **error,
                   gpointer user_data)
{
  GibberRMulticastPacket *packet;
  guint i, n;

  packet = gibber_r_multicast_packet_parse (data, length, NULL);
  g_assert (packet != NULL);

  if (packet->type == PACKET_TYPE_WHOIS_REQUEST)
    {
      GibberRMulticastPacket *reply;
      guint8 *pdata;
      gsize psize;

      for (i = 0; senders[i].name != NULL; i++)
        {
          if (senders[i].sender_id == packet->data.whois_request.sender_id)
            {
              break;
            }
        }

      if (senders[i].name == NULL && packet->sender == 0)
        {
          /* unique id polling */
          goto out;
        }

      g_assert (senders[i].name != NULL);

      reply = gibber_r_multicast_packet_new (PACKET_TYPE_WHOIS_REPLY,
          senders[i].sender_id, transport->max_packet_size);

      gibber_r_multicast_packet_set_whois_reply_info (reply,
          senders[i].name);

      pdata = gibber_r_multicast_packet_get_raw_data (reply, &psize);
      test_transport_write (TEST_TRANSPORT(transport), pdata, psize);
      g_object_unref (reply);
    }

  if (packet->type != PACKET_TYPE_DATA)
    {
      goto out;
    }

  g_assert (packet->depends->len > 0);

  for (n = 0; n < packet->depends->len; n++)
    {
      for (i = 0; senders[i].name != NULL ; i++)
        {
          GibberRMulticastPacketSenderInfo *sender_info =
              g_array_index (packet->depends,
                  GibberRMulticastPacketSenderInfo *, n);
          if (senders[i].sender_id == sender_info->sender_id)
            {
              g_assert (senders[i].seen == FALSE);
              g_assert (senders[i].packet_id + 1 == sender_info->packet_id);
              senders[i].seen = TRUE;
              break;
            }
        }
        g_assert (senders[i].name != NULL);
      }

  for (i = 0; senders[i].name != NULL ; i++)
    {
      g_assert (senders[i].seen && "Not all senders in depends");
    }

  g_main_loop_quit (loop);
out:
  g_object_unref (packet);
  return TRUE;
}

static gboolean
depends_send_test_data (gpointer data)
{
  GibberRMulticastCausalTransport *t =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (data);
  guint8 testdata[] = { 1, 2, 3 };

  g_assert (gibber_transport_send (GIBBER_TRANSPORT (t), testdata,
      3, NULL));

  return FALSE;
}

static void
depends_connected (GibberTransport *transport,
                   gpointer user_data)
{
  GibberRMulticastCausalTransport *rmctransport
    = GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (transport);
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

      gibber_r_multicast_causal_transport_add_sender (rmctransport,
        senders[i].sender_id);
      gibber_r_multicast_causal_transport_update_sender_start (rmctransport,
        senders[i].sender_id, senders[i].packet_id);

      gibber_r_multicast_packet_set_packet_id (packet, senders[i].packet_id);
      gibber_r_multicast_packet_set_data_info (packet, 0, 0, 1);

      data = gibber_r_multicast_packet_get_raw_data (packet, &size);
      test_transport_write (testtransport, data, size);
      g_object_unref (packet);
    }

  /* Wait more then 200 ms, so all senders can get go to running */
  g_timeout_add (300, depends_send_test_data, rmctransport);
}

static void
test_depends (void)
{
  GibberRMulticastCausalTransport *rmctransport;
  TestTransport *testtransport;
  int i;

  loop = g_main_loop_new (NULL, FALSE);

  rmctransport = create_rmulticast_transport (&testtransport, "test123",
       depends_send_hook, NULL);

  g_signal_connect (rmctransport, "connected",
      G_CALLBACK (depends_connected), testtransport);

  rmulticast_connect (rmctransport);

  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  for (i = 0 ; senders[i].name != NULL; i++)
    {
      g_assert (senders[i].seen);
    }

  g_object_unref (rmctransport);
}


/* test fragmentation testing */
static gboolean
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
  g_assert (packet != NULL);

  if (packet->type != PACKET_TYPE_DATA)
    {
      goto out;
    }

  payload = gibber_r_multicast_packet_get_payload (packet, &size);

  if (bytes == 0)
    g_assert
      (packet->data.data.flags == GIBBER_R_MULTICAST_DATA_PACKET_START);
  else if (bytes + size < TEST_DATA_SIZE)
    g_assert (packet->data.data.flags == 0);

  bytes += size;
  g_assert (bytes <= TEST_DATA_SIZE);

  /* check our bytes */
  for (i = 0; i < size; i++)
    {
      g_assert (payload[i] == next_byte);
      next_byte++;
    }

  if (bytes == TEST_DATA_SIZE)
    {
      g_assert
        (packet->data.data.flags == GIBBER_R_MULTICAST_DATA_PACKET_END);
      g_object_unref (packet);
      g_main_loop_quit (loop);
      return FALSE;
    }

out:
  g_object_unref (packet);
  return TRUE;
}

static void
fragmentation_connected (GibberTransport *transport,
                         gpointer user_data)
{
  GibberRMulticastCausalTransport *rmctransport
      = GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (transport);
  guint8 testdata[TEST_DATA_SIZE];
  int i;

  for (i = 0; i < TEST_DATA_SIZE; i++)
    {
      testdata[i] = (guint8) (i & 0xff);
    }

  g_assert (gibber_transport_send (GIBBER_TRANSPORT (rmctransport),
      (guint8 *) testdata, TEST_DATA_SIZE, NULL));
}

static void
test_fragmentation (void)
{
  GibberRMulticastCausalTransport *rmctransport;

  loop = g_main_loop_new (NULL, FALSE);

  rmctransport = create_rmulticast_transport (NULL, "test123",
       fragmentation_send_hook, NULL);

  g_signal_connect (rmctransport, "connected",
      G_CALLBACK (fragmentation_connected), NULL);

  rmulticast_connect (rmctransport);

  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  g_object_unref (rmctransport);
}


/* test unique id */
static gboolean
unique_id_send_hook (GibberTransport *transport,
                     const guint8 *data,
                     gsize length,
                     GError **error,
                     gpointer user_data)
{
  GibberRMulticastPacket *packet;
  guint32 *test_id = (guint32 *) user_data;


  packet = gibber_r_multicast_packet_parse (data, length, NULL);
  g_assert (packet != NULL);

  if (*test_id == 0)
    {
      /* force collision */
      GibberRMulticastPacket *reply;
      guint8 *pdata;
      gsize psize;

      /* First packet must be a whois request to see if the id is taken */
      g_assert (packet->type == PACKET_TYPE_WHOIS_REQUEST);
      /* Sender must be 0 as it couldn't choose a id just yet */
      g_assert (packet->sender == 0);

      *test_id = packet->data.whois_request.sender_id;

      reply = gibber_r_multicast_packet_new (PACKET_TYPE_WHOIS_REPLY,
        *test_id, transport->max_packet_size);

      gibber_r_multicast_packet_set_whois_reply_info (reply, "romeo");

      pdata = gibber_r_multicast_packet_get_raw_data (reply, &psize);
      test_transport_write (TEST_TRANSPORT(transport), pdata, psize);
      g_object_unref (reply);
    }
  else
    {
      g_assert (*test_id != packet->sender);
      switch (packet->type)
        {
          case PACKET_TYPE_WHOIS_REQUEST:
            g_assert (*test_id != packet->data.whois_request.sender_id);
            break;
          case PACKET_TYPE_WHOIS_REPLY:
            /* transport sends a unsolicited whois reply after choosing a
             * identifier */
            g_main_loop_quit (loop);
            break;
          default:
            g_warning ("Unexpected packet type: %x", packet->type);
            g_assert_not_reached ();
            break;
        }
    }

  g_object_unref (packet);
  return TRUE;
}

static void
test_unique_id (void)
{
  /* Test if the multicast transport correctly handles the case that it gets a
   * WHOIS_REPLY on one of it's WHOIS_REQUESTS when it's determining a unique
   * id for itself */
  GibberRMulticastCausalTransport *rmctransport;
  guint32 test_id;

  test_id = 0;
  loop = g_main_loop_new (NULL, FALSE);

  rmctransport = create_rmulticast_transport (NULL, "test123",
       unique_id_send_hook, &test_id);

  rmulticast_connect (rmctransport);

  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  g_object_unref (rmctransport);
}

/* test id generation conflict */
typedef struct {
  guint32 id;
  gint count;
  gint wait;
} unique_id_conflict_test_t;

static gboolean
id_generation_conflict_send_hook (GibberTransport *transport,
                                  const guint8 *data,
                                  gsize length,
                                  GError **error,
                                  gpointer user_data)
{
  GibberRMulticastPacket *packet;
  unique_id_conflict_test_t *test = (unique_id_conflict_test_t *) user_data;

  packet = gibber_r_multicast_packet_parse (data, length, NULL);
  g_assert (packet != NULL);

  if (test->id == 0)
    {
      /* First packet must be a whois request to see if the id is taken */
      g_assert (packet->type == PACKET_TYPE_WHOIS_REQUEST);
      /* Sender must be 0 as it couldn't choose a id just yet */
      g_assert (packet->sender == 0);

      test->id = packet->data.whois_request.sender_id;
    }

  switch (packet->type)
    {
      case PACKET_TYPE_WHOIS_REQUEST:
        test->count++;

        if (test->count < test->wait)
          {
            g_assert (test->id == packet->data.whois_request.sender_id);
          }
        else if (test->count == test->wait)
          {
            /* force collision */
            GibberRMulticastPacket *reply;
            guint8 *pdata;
            gsize psize;

            g_assert (test->id == packet->data.whois_request.sender_id);

            reply = gibber_r_multicast_packet_new (PACKET_TYPE_WHOIS_REQUEST,
              0, transport->max_packet_size);

            gibber_r_multicast_packet_set_whois_request_info (reply, test->id);

            pdata = gibber_r_multicast_packet_get_raw_data (reply, &psize);
            test_transport_write (TEST_TRANSPORT(transport), pdata, psize);
            g_object_unref (reply);
          }
        else if (test->count > test->wait)
          {
            g_assert (test->id != packet->data.whois_request.sender_id);
          }

        break;

      case PACKET_TYPE_WHOIS_REPLY:
        /* transport sends a unsolicited whois reply after choosing a
         * identifier */
        g_assert (packet->sender != test->id);
        g_assert_cmpuint (test->count, ==,
            ID_GENERATION_EXPECTED_POLLS + test->wait);
        g_main_loop_quit (loop);
        break;

      default:
        g_warning ("Unexpected packet type: %x", packet->type);
        g_assert_not_reached ();
        break;
    }

  g_object_unref (packet);
  return TRUE;
}

static void
test_id_generation_conflict (gint _i)
{
  /* Test if the multicast transport correctly handles the case that it sees
   * another WHOIS_REQUEST on one of its WHOIS_REQUESTS when it's determining
   * a unique id for itself */
  GibberRMulticastCausalTransport *rmtransport;
  unique_id_conflict_test_t test;

  test.id = 0;
  test.count = 0;
  test.wait = _i + 1;

  loop = g_main_loop_new (NULL, FALSE);

  rmtransport = create_rmulticast_transport (NULL, "test123",
       id_generation_conflict_send_hook, &test);

  rmulticast_connect (rmtransport);

  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  g_object_unref (rmtransport);
}

static void
test_id_generation_conflict_loop (void)
{
  gint i;
  for (i = 0; i < ID_GENERATION_EXPECTED_POLLS; ++i)
    test_id_generation_conflict (i);
}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_type_init ();

  g_test_add_func ("/gibber/r-multicast-casual-transport/unique-id",
      test_unique_id);
  g_test_add_func (
      "/gibber/r-multicast-casual-transport/id-generation-conflict",
      test_id_generation_conflict_loop);
  g_test_add_func ("/gibber/r-multicast-casual-transport/fragmentation",
      test_fragmentation);
  g_test_add_func ("/gibber/r-multicast-casual-transport/depends",
      test_depends);

  return g_test_run ();
}

#include "test-transport.c"
