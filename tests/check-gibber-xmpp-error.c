/*
 * check-gibber-xmpp-error.c - Test for gibber-xmpp-error functions
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

#include <gibber/gibber-xmpp-error.h>

#define DEBUG_FLAG DEBUG_XMPP
#include <gibber/gibber-debug.h>

#include <check.h>

static void
test_xmpp_error_to_node_with_bad_request (void)
{
  GibberXmppStanza *stanza;
  GibberXmppNode *node;
  const gchar *code;
  const gchar *type;

  stanza = gibber_xmpp_stanza_build (
      GIBBER_STANZA_TYPE_IQ, GIBBER_STANZA_SUB_TYPE_ERROR,
      "from", "to",
      GIBBER_STANZA_END);
  node = gibber_xmpp_error_to_node (XMPP_ERROR_BAD_REQUEST, stanza->node,
      NULL);

  fail_if (node == NULL);
  fail_if (strcmp (node->name, "error") != 0);

  code = gibber_xmpp_node_get_attribute (node, "code");
  fail_if (code == NULL || strcmp (code, "400") != 0);

  type = gibber_xmpp_node_get_attribute (node, "type");
  fail_if (type == NULL || strcmp (type, "modify") != 0);

  fail_if (gibber_xmpp_node_get_child_ns (node, "bad-request",
        "urn:ietf:params:xml:ns:xmpp-stanzas") == NULL);

  g_object_unref (stanza);
}

static void
test_xmpp_error_to_node_with_si_bad_profile (void)
{
  GibberXmppStanza *stanza;
  GibberXmppNode *node;
  const gchar *code;
  const gchar *type;

  stanza = gibber_xmpp_stanza_build (
      GIBBER_STANZA_TYPE_IQ, GIBBER_STANZA_SUB_TYPE_ERROR,
      "from", "to",
      GIBBER_STANZA_END);
  node = gibber_xmpp_error_to_node (XMPP_ERROR_SI_BAD_PROFILE, stanza->node,
      NULL);

  fail_if (node == NULL);
  fail_if (strcmp (node->name, "error") != 0);

  code = gibber_xmpp_node_get_attribute (node, "code");
  fail_if (code == NULL || strcmp (code, "400") != 0);

  type = gibber_xmpp_node_get_attribute (node, "type");
  fail_if (type == NULL || strcmp (type, "modify") != 0);

  fail_if (gibber_xmpp_node_get_child_ns (node, "bad-request",
        "urn:ietf:params:xml:ns:xmpp-stanzas") == NULL);

  fail_if (gibber_xmpp_node_get_child_ns (node, "bad-profile",
        "http://jabber.org/protocol/si") == NULL);

  g_object_unref (stanza);
}

START_TEST (test_xmpp_error_to_node)
{
  test_xmpp_error_to_node_with_bad_request ();
  test_xmpp_error_to_node_with_si_bad_profile ();
} END_TEST

START_TEST (test_message_get_xmpp_error)
{
  GibberXmppError xmpp_error;

  for (xmpp_error = 1; xmpp_error < NUM_XMPP_ERRORS; xmpp_error++)
    {
      GibberXmppStanza *stanza;
      GError *error = NULL;

      stanza = gibber_xmpp_stanza_build (
          GIBBER_STANZA_TYPE_IQ, GIBBER_STANZA_SUB_TYPE_ERROR,
          "from", "to",
          GIBBER_STANZA_END);
      gibber_xmpp_error_to_node (xmpp_error, stanza->node, NULL);

      error = gibber_message_get_xmpp_error (stanza);
      fail_if (error == NULL);

      fail_if (error->domain != GIBBER_XMPP_ERROR);
      fail_if (error->code != xmpp_error);
      fail_if (strcmp (error->message, gibber_xmpp_error_description (
              xmpp_error)) != 0);

      g_object_unref (stanza);
      g_error_free (error);
    }

} END_TEST

TCase *
make_gibber_xmpp_error_tcase (void)
{
  TCase *tc = tcase_create ("XMPP Error");
  tcase_add_test (tc, test_xmpp_error_to_node);
  tcase_add_test (tc, test_message_get_xmpp_error);
  return tc;
}
