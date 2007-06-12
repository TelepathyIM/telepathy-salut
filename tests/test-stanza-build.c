/*
 * test-stanza-build.c - Test for gibber_xmpp_stanza_build
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

static void
check_html_message (void)
{
  GibberXmppStanza *stanza;
  const gchar *body = "Telepathy rocks!",
        *xhtml_ns = "http://www.w3.org/1999/xhtml";
  GibberXmppNode *node;
  const gchar *value;

  g_type_init();
  gibber_debug_set_flags_from_env ();

  stanza = gibber_xmpp_stanza_build (
      GIBBER_STANZA_TYPE_MESSAGE, GIBBER_STANZA_SUB_TYPE_NOT_SET,
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

  /* <message> */
  node = stanza->node;
  g_assert (node != NULL);
  g_assert (strcmp (node->name, "message") == 0);
  value = gibber_xmpp_node_get_attribute (node, "type");
  g_assert (value == NULL);
  value = gibber_xmpp_node_get_attribute (node, "from");
  g_assert (strcmp (value, "alice@collabora.co.uk") == 0);
  value = gibber_xmpp_node_get_attribute (node, "to");
  g_assert (strcmp (value, "bob@collabora.co.uk") == 0);

  /* <html> */
  node = gibber_xmpp_node_get_child_ns (node, "html", xhtml_ns);
  g_assert (node != NULL);

  /* <body> */
  node = gibber_xmpp_node_get_child (node, "body");
  g_assert (node != NULL);
  value = gibber_xmpp_node_get_attribute (node, "textcolor");
  g_assert (strcmp (value, "red") == 0);
  g_assert (strcmp (node->content, body) == 0);

  gibber_xmpp_node_free (stanza->node);
}

int main (void)
{
  check_html_message ();

  return 0;
}
