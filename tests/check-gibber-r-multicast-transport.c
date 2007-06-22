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

#define TEST_DATA_SIZE 300
GMainLoop *loop;

GibberRMulticastTransport *
create_rmulticast_transport (TestTransport **testtransport,
                             const gchar *name,
                             test_transport_send_hook test_send_hook)
{
  TestTransport *t;
  GibberRMulticastTransport *rmtransport;

  t = test_transport_new (test_send_hook, NULL);
  fail_unless (t != NULL);
  GIBBER_TRANSPORT (t)->max_packet_size = 150;

  rmtransport = gibber_r_multicast_transport_new (
      GIBBER_TRANSPORT(t), "test123");

  if (testtransport != NULL)
    {
      *testtransport = t;
    }

  return rmtransport;
}

/* test depends test */
struct {
  gchar *name;
  guint32 packet_id;
  gboolean seen;
} senders[] = {
                { "test0",     0xff, FALSE },
                { "test1",   0xffff, FALSE },
                { "test2", 0xffffff, FALSE },
                { "test3", 0xaaaaaa, FALSE },
                { "test4", 0xabcdab, FALSE },
                { NULL, 0, FALSE }
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

  if (packet->type != PACKET_TYPE_DATA)
    {
      goto out;
    }

  fail_unless(g_list_length (packet->receivers) > 0);

  for (l = packet->receivers; l != NULL; l = g_list_next (l))
    {
      for (i = 0; senders[i].name != NULL ; i++)
        {
          GibberRMulticastReceiver *r = (GibberRMulticastReceiver *)l->data;
          if (strcmp (senders[i].name, r->name) == 0)
            {
              fail_unless (senders[i].seen == FALSE);
              fail_unless (senders[i].packet_id == r->packet_id);
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


START_TEST (test_depends)
{
  GibberRMulticastTransport *rmtransport;
  TestTransport *testtransport;
  int i;

  loop = g_main_loop_new (NULL, FALSE);

  rmtransport = create_rmulticast_transport (&testtransport, "test123",
       depends_send_hook);

  fail_unless (gibber_r_multicast_transport_connect (rmtransport,
      FALSE, NULL));

  /* First input some data packets, so the transport is forced to generate
   * dependency info */
  for (i = 0 ; senders[i].name != NULL; i++) 
    {
      GibberRMulticastPacket *packet;
      guint8 *data;
      gsize size;
      packet = gibber_r_multicast_packet_new (PACKET_TYPE_DATA,
         senders[i].name, senders[i].packet_id, 0,
         GIBBER_TRANSPORT (testtransport)->max_packet_size);

      gibber_r_multicast_packet_set_part (packet, 0, 1);
      data = gibber_r_multicast_packet_get_raw_data (packet, &size);
      test_transport_write (testtransport, data, size);
      g_object_unref (packet);
    }

  /* Wait more then 200 ms, so all senders can get go to running */
  g_timeout_add (300, depends_send_test_data, rmtransport);

  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  for (i = 0 ; senders[i].name != NULL; i++) 
    {
      fail_unless (senders[i].seen);
    }
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


START_TEST (test_fragmentation)
{
  GibberRMulticastTransport *rmtransport;
  guint8 testdata[TEST_DATA_SIZE];
  int i;

  for (i = 0; i < TEST_DATA_SIZE; i++)
    {
      testdata[i] = (guint8) (i & 0xff);
    }

  loop = g_main_loop_new (NULL, FALSE);

  rmtransport = create_rmulticast_transport (NULL, "test123",
       fragmentation_send_hook);

  fail_unless (gibber_r_multicast_transport_connect (rmtransport,
     FALSE, NULL));

  fail_unless (gibber_transport_send (GIBBER_TRANSPORT (rmtransport),
      (guint8 *)testdata, TEST_DATA_SIZE, NULL));

  g_main_loop_run (loop);
  g_main_loop_unref (loop);
}
END_TEST

TCase *
make_gibber_r_multicast_transport_tcase (void)
{
  TCase *tc = tcase_create ("Gibber R Multicast transport");
  tcase_add_test (tc, test_fragmentation);
  tcase_add_test (tc, test_depends);

  return tc;
}
