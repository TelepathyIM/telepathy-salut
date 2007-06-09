#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gibber/gibber-xmpp-reader.h>
#include <gibber/gibber-xmpp-stanza.h>

#include <check.h>

typedef struct _ReceivedStanzaEvent ReceivedStanzaEvent;

struct _ReceivedStanzaEvent {
  GibberXmppReader *reader;
  GibberXmppStanza *stanza;
};

static void received_stanza_cb (GibberXmppReader *reader,
                                GibberXmppStanza *stanza,
                                gpointer user_data) {
  GQueue *events_queue = (GQueue *)user_data;

  fail_if (reader == NULL);
  fail_if (stanza == NULL);
  fail_if (events_queue == NULL);

  g_object_ref (stanza);

  ReceivedStanzaEvent *event;
  event = g_new (ReceivedStanzaEvent, 1);
  event->reader = reader;
  event->stanza = stanza;

  g_queue_push_tail (events_queue, event);
}


START_TEST (test_instantiation)
{
  GibberXmppReader *reader;
  g_type_init();
  reader = gibber_xmpp_reader_new_no_stream ();
  fail_if (reader == NULL);
}
END_TEST

START_TEST (test_simple_message)
{
  GibberXmppReader *reader;
  GibberXmppNode *node;
  gchar *data;
  gsize length;
  gboolean valid;
  GQueue *received_stanzas;
  ReceivedStanzaEvent *event;

  g_type_init();

  received_stanzas = g_queue_new ();

  reader = gibber_xmpp_reader_new ();
  g_signal_connect(reader, "received-stanza",
                   G_CALLBACK(received_stanza_cb), received_stanzas);

  fail_unless (g_file_get_contents ("inputs/simple-message.input",
                                    &data, &length, NULL));

  valid = gibber_xmpp_reader_push (reader, (guint8 *)data, length, NULL);
  fail_unless (valid);

  fail_unless (g_queue_get_length (received_stanzas) == 2);

  event = g_queue_pop_head (received_stanzas);

  fail_unless (event->reader == reader);

  node = event->stanza->node;
  fail_if (node == NULL);
  fail_unless (strcmp (node->name, "message") == 0);
  fail_unless (strcmp (gibber_xmpp_node_get_language (node), "en") == 0);
  fail_unless (strcmp (gibber_xmpp_node_get_attribute (node, "to"),
                       "juliet@example.com") == 0);

  event = g_queue_pop_head (received_stanzas);

  fail_unless (event->reader == reader);

  node = event->stanza->node;
  fail_unless (strcmp (node->name, "message") == 0);
  fail_unless (strcmp (gibber_xmpp_node_get_language (node), "en") == 0);
  fail_unless (strcmp (gibber_xmpp_node_get_attribute (node, "to"),
                       "juliet@example.com") == 0);
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
