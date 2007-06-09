#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gibber/gibber-xmpp-node.h>

#include <check.h>

START_TEST (test_instantiation)
{
  GibberXmppNode *node;
  node = gibber_xmpp_node_new ("test");
  fail_unless (node != NULL);

  node = gibber_xmpp_node_new (NULL);
  fail_unless (node != NULL);
}
END_TEST

START_TEST (test_language)
{
  GibberXmppNode *node;
  const gchar *lang;

  lang = gibber_xmpp_node_get_language (NULL);
  fail_unless (lang == NULL);

  node = gibber_xmpp_node_new ("test");
  lang = gibber_xmpp_node_get_language (node);
  fail_unless (lang == NULL);

  gibber_xmpp_node_set_language (node, "en");
  lang = gibber_xmpp_node_get_language (node);
  fail_unless (strcmp(lang, "en") == 0);

  gibber_xmpp_node_set_language (node, NULL);
  lang = gibber_xmpp_node_get_language (node);
  fail_unless (lang == NULL);

  gibber_xmpp_node_set_language_n (node, "en-US", 2);
  lang = gibber_xmpp_node_get_language (node);
  fail_unless (strcmp(lang, "en") == 0);

  gibber_xmpp_node_set_language_n (node, NULL, 2);
  lang = gibber_xmpp_node_get_language (node);
  fail_unless (lang == NULL);
}
END_TEST

TCase *
make_gibber_xmpp_node_tcase (void)
{
    TCase *tc = tcase_create ("XMPP Node");
    tcase_add_test (tc, test_instantiation);
    tcase_add_test (tc, test_language);
    return tc;
}
