#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gibber/gibber-xmpp-reader.h>

#include <check.h>

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
  gchar *data;
  gsize length;
  gboolean valid;

  g_type_init();
  reader = gibber_xmpp_reader_new_no_stream ();
  fail_unless (g_file_get_contents ("inputs/simple-message.input",
                                    &data, &length, NULL));

  valid = gibber_xmpp_reader_push (reader, (guint8 *)data, length, NULL);
  fail_unless (valid);
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
