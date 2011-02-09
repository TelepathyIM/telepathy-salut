#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gibber/gibber-xmpp-node.h>

#include <check.h>
#include "check-helpers.h"
#include "check-gibber.h"

START_TEST (test_instantiation)
{
  WockyNode *node;
  node = wocky_node_new ("test", "http://example.com/badgers/");
  fail_unless (node != NULL);

  wocky_node_free (node);
}
END_TEST

START_TEST (test_language)
{
  WockyNode *node;
  const gchar *lang;

  node = wocky_node_new ("test", "http://example.com/badgers/");
  lang = wocky_node_get_language (node);
  fail_unless (lang == NULL);

  wocky_node_set_language (node, "en");
  lang = wocky_node_get_language (node);
  fail_unless (strcmp (lang, "en") == 0);

  wocky_node_set_language (node, NULL);
  lang = wocky_node_get_language (node);
  fail_unless (lang == NULL);

  wocky_node_set_language_n (node, "en-US", 2);
  lang = wocky_node_get_language (node);
  fail_unless (strcmp (lang, "en") == 0);

  wocky_node_set_language_n (node, NULL, 2);
  lang = wocky_node_get_language (node);
  fail_unless (lang == NULL);

  wocky_node_free (node);
}
END_TEST

START_TEST (test_namespace)
{
  WockyNode *node;
  const gchar *ns;

  node = wocky_node_new ("test", "foo");
  ns = wocky_node_get_ns (node);
  fail_unless (strcmp (ns, "foo") == 0);
  wocky_node_free (node);
}
END_TEST


START_TEST (test_attribute)
{
  WockyNode *node;
  const gchar *attribute;

  node = wocky_node_new ("test", "about:blank");

  attribute = wocky_node_get_attribute (node, "foo");
  fail_unless (attribute == NULL);

  attribute = wocky_node_get_attribute (node, NULL);
  fail_unless (attribute == NULL);

  attribute = wocky_node_get_attribute_ns (node, "foo", "bar");
  fail_unless (attribute == NULL);

  wocky_node_set_attribute (node, "foo", "baz");

  attribute = wocky_node_get_attribute (node, "foo");
  fail_unless (strcmp ( attribute, "baz") == 0);

  attribute = wocky_node_get_attribute_ns (node, "foo", "bar");
  fail_unless (attribute == NULL);

  wocky_node_set_attribute_ns (node, "foobar", "barbaz", "bar");

  attribute = wocky_node_get_attribute (node, "foobar");
  fail_unless (strcmp (attribute, "barbaz") == 0);

  attribute = wocky_node_get_attribute_ns (node, "foobar", "bar");
  fail_unless (strcmp (attribute, "barbaz") == 0);

  attribute = wocky_node_get_attribute_ns (node, "barfoo", "bar");
  fail_unless (attribute == NULL);

  wocky_node_free (node);
}
END_TEST

START_TEST (test_child)
{
  WockyNode *node, *child;

  node = wocky_node_new ("test", "about:blank");

  child = wocky_node_get_child (node, "foo");
  fail_unless (child == NULL);

  wocky_node_add_child (node, "foo");
  child = wocky_node_get_child (node, "foo");
  fail_if (child == NULL);
  fail_unless (strcmp (child->name, "foo") == 0);

  child = wocky_node_get_child_ns (node, "foo", "bar");
  fail_unless (child == NULL);

  wocky_node_add_child_ns (node, "foobar", "bar");
  child = wocky_node_get_child_ns (node, "foobar", "foo");
  fail_unless (child == NULL);
  child = wocky_node_get_child_ns (node, "foobar", "bar");
  fail_if (child == NULL);
  fail_unless (strcmp (child->name, "foobar") == 0);

  wocky_node_add_child_with_content (node, "foo2", "blah");
  child = wocky_node_get_child (node, "foo2");
  fail_if (child->content == NULL);
  fail_unless (strcmp (child->content, "blah") == 0);

  wocky_node_free (node);
}
END_TEST


TCase *
make_wocky_node_tcase (void)
{
    TCase *tc = tcase_create ("XMPP Node");
    tcase_add_test (tc, test_instantiation);
    tcase_add_test (tc, test_language);
    tcase_add_test (tc, test_namespace);
    tcase_add_test (tc, test_attribute);
    tcase_add_test (tc, test_child);
    return tc;
}
