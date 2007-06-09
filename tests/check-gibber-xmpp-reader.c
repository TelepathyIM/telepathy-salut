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

TCase *
make_gibber_xmpp_reader_tcase (void)
{
    TCase *tc = tcase_create ("XMPP Reader");
    tcase_add_test (tc, test_instantiation);
    return tc;
}
