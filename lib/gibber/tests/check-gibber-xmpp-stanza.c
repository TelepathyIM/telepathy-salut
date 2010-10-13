/*
 * check-gibber-xmpp-stanza.c - Test for gibber-xmpp-stanza functions
 * Copyright (C) 2007 Collabora Ltd.
 * @author Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
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
#include <stdlib.h>
#include <string.h>

#include <gibber/gibber-xmpp-stanza.h>

#define DEBUG_FLAG DEBUG_XMPP
#include <gibber/gibber-debug.h>

#include <wocky/wocky-namespaces.h>

#include <check.h>
#include "check-gibber.h"

START_TEST (test_build_with_html_message)
{
  GibberXmppStanza *stanza;
  const gchar *body = "Telepathy rocks!",
        *xhtml_ns = "http://www.w3.org/1999/xhtml";
  GibberXmppNode *node;
  const gchar *value;

  g_type_init ();
#ifdef ENABLE_DEBUG
  gibber_debug_set_flags_from_env ();
#endif

  stanza = gibber_xmpp_stanza_build (
      GIBBER_STANZA_TYPE_MESSAGE, GIBBER_STANZA_SUB_TYPE_NONE,
      "alice@collabora.co.uk", "bob@collabora.co.uk",
      GIBBER_NODE, "html",
        GIBBER_NODE_XMLNS, xhtml_ns,
        GIBBER_NODE, "body",
          GIBBER_NODE_ATTRIBUTE, "textcolor", "red",
          GIBBER_NODE_TEXT, body,
        GIBBER_NODE_END,
      GIBBER_NODE_END,
     GIBBER_STANZA_END);

  DEBUG_STANZA (stanza, "check");

  fail_if (stanza == NULL);
  /* <message> */
  node = wocky_stanza_get_top_node (stanza);
  fail_if (node == NULL);
  fail_unless (strcmp (node->name, "message") == 0);
  value = gibber_xmpp_node_get_attribute (node, "type");
  fail_unless (value == NULL);
  value = gibber_xmpp_node_get_attribute (node, "from");
  fail_unless (strcmp (value, "alice@collabora.co.uk") == 0);
  value = gibber_xmpp_node_get_attribute (node, "to");
  fail_unless (strcmp (value, "bob@collabora.co.uk") == 0);

  /* <html> */
  node = gibber_xmpp_node_get_child_ns (node, "html", xhtml_ns);
  fail_if (node == NULL);

  /* <body> */
  node = gibber_xmpp_node_get_child (node, "body");
  fail_if (node == NULL);
  value = gibber_xmpp_node_get_attribute (node, "textcolor");
  fail_unless (strcmp (value, "red") == 0);
  fail_unless (strcmp (node->content, body) == 0);

  g_object_unref (stanza);
} END_TEST

START_TEST (test_get_type_info_with_simple_message)
{
  GibberXmppStanza *stanza;
  GibberStanzaType type;
  GibberStanzaSubType sub_type;

  stanza = gibber_xmpp_stanza_build (
      GIBBER_STANZA_TYPE_MESSAGE, GIBBER_STANZA_SUB_TYPE_NONE,
      "alice@collabora.co.uk", "bob@collabora.co.uk",
     GIBBER_STANZA_END);
  fail_if (stanza == NULL);

  gibber_xmpp_stanza_get_type_info (stanza, &type, &sub_type);
  fail_if (type != GIBBER_STANZA_TYPE_MESSAGE);
  fail_if (sub_type != GIBBER_STANZA_SUB_TYPE_NONE);

  g_object_unref (stanza);
} END_TEST

START_TEST (test_get_type_info_with_iq_set)
{
  GibberXmppStanza *stanza;
  GibberStanzaType type;
  GibberStanzaSubType sub_type;

  stanza = gibber_xmpp_stanza_build (
      GIBBER_STANZA_TYPE_IQ, GIBBER_STANZA_SUB_TYPE_SET,
      "alice@collabora.co.uk", "bob@collabora.co.uk",
     GIBBER_STANZA_END);
  fail_if (stanza == NULL);

  gibber_xmpp_stanza_get_type_info (stanza, &type, &sub_type);
  fail_if (type != GIBBER_STANZA_TYPE_IQ);
  fail_if (sub_type != GIBBER_STANZA_SUB_TYPE_SET);

  g_object_unref (stanza);
} END_TEST

START_TEST (test_get_type_info_with_unknown_type)
{
  GibberXmppStanza *stanza;
  GibberStanzaType type;

  stanza = gibber_xmpp_stanza_new_ns ("goat", WOCKY_XMPP_NS_JABBER_CLIENT);
  fail_if (stanza == NULL);

  gibber_xmpp_stanza_get_type_info (stanza, &type, NULL);
  fail_if (type != GIBBER_STANZA_TYPE_UNKNOWN);

  g_object_unref (stanza);
} END_TEST

START_TEST (test_get_type_info_with_unknown_sub_type)
{
  GibberXmppStanza *stanza;
  GibberStanzaSubType sub_type;

  stanza = gibber_xmpp_stanza_new_ns ("iq", WOCKY_XMPP_NS_JABBER_CLIENT);
  fail_if (stanza == NULL);
  gibber_xmpp_node_set_attribute (wocky_stanza_get_top_node (stanza),
      "type", "goat");

  gibber_xmpp_stanza_get_type_info (stanza, NULL, &sub_type);
  fail_if (sub_type != GIBBER_STANZA_SUB_TYPE_UNKNOWN);

  g_object_unref (stanza);
} END_TEST


TCase *
make_gibber_xmpp_stanza_tcase (void)
{
  TCase *tc = tcase_create ("XMPP Stanza");
  tcase_add_test (tc, test_build_with_html_message);
  tcase_add_test (tc, test_get_type_info_with_simple_message);
  tcase_add_test (tc, test_get_type_info_with_iq_set);
  tcase_add_test (tc, test_get_type_info_with_unknown_type);
  tcase_add_test (tc, test_get_type_info_with_unknown_sub_type);
  return tc;
}
