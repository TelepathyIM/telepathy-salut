#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gibber/gibber-xmpp-reader.h>
#include <wocky/wocky-stanza.h>

#include <check.h>

#include "check-gibber.h"

typedef struct _ReceivedStanzaEvent ReceivedStanzaEvent;

struct _ReceivedStanzaEvent {
  GibberXmppReader *reader;
  WockyStanza *stanza;
};

static void received_stanza_cb (GibberXmppReader *reader,
    WockyStanza *stanza, gpointer user_data)
{
  GQueue *events_queue = (GQueue *) user_data;
  ReceivedStanzaEvent *event;

  fail_if (reader == NULL);
  fail_if (stanza == NULL);
  fail_if (events_queue == NULL);

  g_object_ref (stanza);

  event = g_new (ReceivedStanzaEvent, 1);
  event->reader = reader;
  event->stanza = stanza;

  g_queue_push_tail (events_queue, event);
}


START_TEST (test_instantiation)
{
  GibberXmppReader *reader;
  reader = gibber_xmpp_reader_new_no_stream ();
  fail_if (reader == NULL);
  g_object_unref (reader);
}
END_TEST

START_TEST (test_simple_message)
{
  GibberXmppReader *reader;
  WockyNode *node;
  gchar *data;
  gsize length;
  gboolean valid;
  GQueue *received_stanzas;
  ReceivedStanzaEvent *event;
  const gchar *srcdir;
  gchar *file;

  received_stanzas = g_queue_new ();

  reader = gibber_xmpp_reader_new ();
  g_signal_connect (reader, "received-stanza",
      G_CALLBACK (received_stanza_cb), received_stanzas);

  srcdir = g_getenv ("srcdir");
  if (srcdir == NULL)
    {
      file = g_strdup ("inputs/simple-message.input");
    }
  else
    {
      file = g_strdup_printf ("%s/inputs/simple-message.input", srcdir);
    }

  fail_unless (g_file_get_contents (file, &data, &length, NULL));
  g_free (file);

  valid = gibber_xmpp_reader_push (reader, (guint8 *) data, length, NULL);
  fail_unless (valid);

  fail_unless (g_queue_get_length (received_stanzas) == 2);

  event = g_queue_pop_head (received_stanzas);

  fail_unless (event->reader == reader);

  node = wocky_stanza_get_top_node (event->stanza);
  fail_if (node == NULL);
  fail_unless (strcmp (node->name, "message") == 0);
  fail_unless (strcmp (wocky_node_get_language (node), "en") == 0);
  fail_unless (strcmp (wocky_node_get_attribute (node, "to"),
                       "juliet@example.com") == 0);
  fail_unless (strcmp (wocky_node_get_attribute (node, "id"),
                       "0") == 0);

  g_object_unref (event->stanza);
  g_free (event);

  event = g_queue_pop_head (received_stanzas);

  fail_unless (event->reader == reader);

  node = wocky_stanza_get_top_node (event->stanza);
  fail_unless (strcmp (node->name, "message") == 0);
  fail_unless (strcmp (wocky_node_get_language (node), "en") == 0);
  fail_unless (strcmp (wocky_node_get_attribute (node, "to"),
                       "juliet@example.com") == 0);
  fail_unless (strcmp (wocky_node_get_attribute (node, "id"),
                       "1") == 0);

  g_free (data);
  g_queue_free (received_stanzas);
  g_object_unref (event->stanza);
  g_free (event);
  g_object_unref (reader);
}
END_TEST

TCase *
make_gibber_xmpp_reader_tcase (void)
{
    TCase *tc = tcase_create ("XMPP Reader");
    tcase_add_test (tc, test_instantiation);
    tcase_add_test (tc, test_simple_message);
    return tc;
}
