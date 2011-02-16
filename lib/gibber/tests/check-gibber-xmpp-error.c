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

static void
test_xmpp_error_to_node_with_bad_request (void)
{
  WockyStanza *stanza;
  WockyNode *node;
  const gchar *code;
  const gchar *type;

  stanza = wocky_stanza_build (
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_ERROR,
      "from", "to",
      NULL);
  node = gibber_xmpp_error_to_node (XMPP_ERROR_BAD_REQUEST,
      wocky_stanza_get_top_node (stanza), NULL);

  g_assert (node != NULL);
  g_assert_cmpstr (node->name, ==, "error");

  code = wocky_node_get_attribute (node, "code");
  g_assert (!(code == NULL || strcmp (code, "400") != 0));

  type = wocky_node_get_attribute (node, "type");
  g_assert (!(type == NULL || strcmp (type, "modify") != 0));

  g_assert (wocky_node_get_child_ns (node, "bad-request",
        "urn:ietf:params:xml:ns:xmpp-stanzas") != NULL);

  g_object_unref (stanza);
}

static void
test_xmpp_error_to_node_with_si_bad_profile (void)
{
  WockyStanza *stanza;
  WockyNode *node;
  const gchar *code;
  const gchar *type;

  stanza = wocky_stanza_build (
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_ERROR,
      "from", "to",
      NULL);
  node = gibber_xmpp_error_to_node (XMPP_ERROR_SI_BAD_PROFILE,
      wocky_stanza_get_top_node (stanza), NULL);

  g_assert (node != NULL);
  g_assert_cmpstr (node->name, ==, "error");

  code = wocky_node_get_attribute (node, "code");
  g_assert (!(code == NULL || strcmp (code, "400") != 0));

  type = wocky_node_get_attribute (node, "type");
  g_assert (!(type == NULL || strcmp (type, "modify") != 0));

  g_assert (wocky_node_get_child_ns (node, "bad-request",
        "urn:ietf:params:xml:ns:xmpp-stanzas") != NULL);

  g_assert (wocky_node_get_child_ns (node, "bad-profile",
        "http://jabber.org/protocol/si") != NULL);

  g_object_unref (stanza);
}

static void
test_xmpp_error_to_node (void)
{
  test_xmpp_error_to_node_with_bad_request ();
  test_xmpp_error_to_node_with_si_bad_profile ();
}

static void
test_message_get_xmpp_error (void)
{
  GibberXmppError xmpp_error;

  for (xmpp_error = 1; xmpp_error < NUM_XMPP_ERRORS; xmpp_error++)
    {
      WockyStanza *stanza;
      GError *error = NULL;

      stanza = wocky_stanza_build (
          WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_ERROR,
          "from", "to",
          NULL);
      gibber_xmpp_error_to_node (xmpp_error,
          wocky_stanza_get_top_node (stanza), NULL);

      error = gibber_message_get_xmpp_error (stanza);
      g_assert (error != NULL);

      g_assert (error->domain == GIBBER_XMPP_ERROR);
      g_assert (error->code == (gint) xmpp_error);
      g_assert_cmpstr (error->message, ==,
           gibber_xmpp_error_description (xmpp_error));

      g_object_unref (stanza);
      g_error_free (error);
    }

}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_type_init ();

  g_test_add_func ("/gibber/xmpp-error/to-node",
      test_xmpp_error_to_node);
  g_test_add_func ("/gibber/xmpp-error/message-get-xmpp-error",
      test_message_get_xmpp_error);

  return g_test_run ();
}
