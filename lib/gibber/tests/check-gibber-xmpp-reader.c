#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gibber/gibber-xmpp-reader.h>
#include <wocky/wocky-stanza.h>

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

  g_assert (reader != NULL);
  g_assert (stanza != NULL);
  g_assert (events_queue != NULL);

  g_object_ref (stanza);

  event = g_new (ReceivedStanzaEvent, 1);
  event->reader = reader;
  event->stanza = stanza;

  g_queue_push_tail (events_queue, event);
}


static void
test_instantiation (void)
{
  GibberXmppReader *reader;
  reader = gibber_xmpp_reader_new_no_stream ();
  g_assert (reader != NULL);
  g_object_unref (reader);
}

static void
test_simple_message (void)
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

  g_assert (g_file_get_contents (file, &data, &length, NULL));
  g_free (file);

  valid = gibber_xmpp_reader_push (reader, (guint8 *) data, length, NULL);
  g_assert (valid);

  g_assert (g_queue_get_length (received_stanzas) == 2);

  event = g_queue_pop_head (received_stanzas);

  g_assert (event->reader == reader);

  node = wocky_stanza_get_top_node (event->stanza);
  g_assert (node != NULL);
  g_assert_cmpstr (node->name, ==, "message");
  g_assert_cmpstr (wocky_node_get_language (node), ==, "en");
  g_assert_cmpstr (wocky_node_get_attribute (node, "to"), ==,
      "juliet@example.com");
  g_assert_cmpstr (wocky_node_get_attribute (node, "id"), ==, "0");

  g_object_unref (event->stanza);
  g_free (event);

  event = g_queue_pop_head (received_stanzas);

  g_assert (event->reader == reader);

  node = wocky_stanza_get_top_node (event->stanza);
  g_assert_cmpstr (node->name, ==, "message");
  g_assert_cmpstr (wocky_node_get_language (node), ==, "en");
  g_assert_cmpstr (wocky_node_get_attribute (node, "to"), ==,
      "juliet@example.com");
  g_assert_cmpstr (wocky_node_get_attribute (node, "id"), ==, "1");

  g_free (data);
  g_queue_free (received_stanzas);
  g_object_unref (event->stanza);
  g_free (event);
  g_object_unref (reader);
}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_type_init ();

  g_test_add_func ("/gibber/xmpp-reader/instantiation",
      test_instantiation);
  g_test_add_func ("/gibber/xmpp-reader/simple-message",
      test_simple_message);

  return g_test_run ();
}
