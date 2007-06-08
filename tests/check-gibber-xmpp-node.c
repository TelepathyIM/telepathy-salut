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
}
END_TEST

TCase *
make_gibber_xmpp_node_tcase (void)
{
    TCase *tc = tcase_create ("XMPP Node");
    tcase_add_test (tc, test_instantiation);
    return tc;
}
