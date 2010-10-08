#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gibber/gibber-xmpp-node.h>

#include <check.h>
#include "check-helpers.h"
#include "check-gibber.h"

START_TEST (test_instantiation)
{
  GibberXmppNode *node;
  node = gibber_xmpp_node_new_ns ("test", "http://example.com/badgers/");
  fail_unless (node != NULL);

  gibber_xmpp_node_free (node);
}
END_TEST

START_TEST (test_language)
{
  GibberXmppNode *node;
  const gchar *lang;

  node = gibber_xmpp_node_new_ns ("test", "http://example.com/badgers/");
  lang = gibber_xmpp_node_get_language (node);
  fail_unless (lang == NULL);

  gibber_xmpp_node_set_language (node, "en");
  lang = gibber_xmpp_node_get_language (node);
  fail_unless (strcmp (lang, "en") == 0);

  gibber_xmpp_node_set_language (node, NULL);
  lang = gibber_xmpp_node_get_language (node);
  fail_unless (lang == NULL);

  gibber_xmpp_node_set_language_n (node, "en-US", 2);
  lang = gibber_xmpp_node_get_language (node);
  fail_unless (strcmp (lang, "en") == 0);

  gibber_xmpp_node_set_language_n (node, NULL, 2);
  lang = gibber_xmpp_node_get_language (node);
  fail_unless (lang == NULL);

  gibber_xmpp_node_free (node);
}
END_TEST

START_TEST (test_namespace)
{
  GibberXmppNode *node;
  const gchar *ns;

  node = gibber_xmpp_node_new_ns ("test", "foo");
  ns = gibber_xmpp_node_get_ns (node);
  fail_unless (strcmp (ns, "foo") == 0);
  gibber_xmpp_node_free (node);
}
END_TEST


START_TEST (test_attribute)
{
  GibberXmppNode *node;
  const gchar *attribute;

  node = gibber_xmpp_node_new_ns ("test", "about:blank");

  attribute = gibber_xmpp_node_get_attribute (node, "foo");
  fail_unless (attribute == NULL);

  attribute = gibber_xmpp_node_get_attribute (node, NULL);
  fail_unless (attribute == NULL);

  attribute = gibber_xmpp_node_get_attribute_ns (node, "foo", "bar");
  fail_unless (attribute == NULL);

  gibber_xmpp_node_set_attribute (node, "foo", "baz");

  attribute = gibber_xmpp_node_get_attribute (node, "foo");
  fail_unless (strcmp ( attribute, "baz") == 0);

  attribute = gibber_xmpp_node_get_attribute_ns (node, "foo", "bar");
  fail_unless (attribute == NULL);

  gibber_xmpp_node_set_attribute_ns (node, "foobar", "barbaz", "bar");

  attribute = gibber_xmpp_node_get_attribute (node, "foobar");
  fail_unless (strcmp (attribute, "barbaz") == 0);

  attribute = gibber_xmpp_node_get_attribute_ns (node, "foobar", "bar");
  fail_unless (strcmp (attribute, "barbaz") == 0);

  attribute = gibber_xmpp_node_get_attribute_ns (node, "barfoo", "bar");
  fail_unless (attribute == NULL);

  gibber_xmpp_node_free (node);
}
END_TEST

START_TEST (test_child)
{
  GibberXmppNode *node, *child;

  node = gibber_xmpp_node_new_ns ("test", "about:blank");

  child = gibber_xmpp_node_get_child (node, "foo");
  fail_unless (child == NULL);

  gibber_xmpp_node_add_child (node, "foo");
  child = gibber_xmpp_node_get_child (node, "foo");
  fail_if (child == NULL);
  fail_unless (strcmp (child->name, "foo") == 0);

  child = gibber_xmpp_node_get_child_ns (node, "foo", "bar");
  fail_unless (child == NULL);

  gibber_xmpp_node_add_child_ns (node, "foobar", "bar");
  child = gibber_xmpp_node_get_child_ns (node, "foobar", "foo");
  fail_unless (child == NULL);
  child = gibber_xmpp_node_get_child_ns (node, "foobar", "bar");
  fail_if (child == NULL);
  fail_unless (strcmp (child->name, "foobar") == 0);

  gibber_xmpp_node_add_child_with_content (node, "foo2", "blah");
  child = gibber_xmpp_node_get_child (node, "foo2");
  fail_if (child->content == NULL);
  fail_unless (strcmp (child->content, "blah") == 0);

  gibber_xmpp_node_free (node);
}
END_TEST


TCase *
make_gibber_xmpp_node_tcase (void)
{
    TCase *tc = tcase_create ("XMPP Node");
    tcase_add_test (tc, test_instantiation);
    tcase_add_test (tc, test_language);
    tcase_add_test (tc, test_namespace);
    tcase_add_test (tc, test_attribute);
    tcase_add_test (tc, test_child);
    return tc;
}
