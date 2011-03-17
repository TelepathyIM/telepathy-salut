/*
 * check-gibber-iq-helper.c - Test for GibberIqHelper
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
#include <dbus/dbus-glib.h>

#include <wocky/wocky-stanza.h>
#include <gibber/gibber-iq-helper.h>
#include <gibber/gibber-xmpp-error.h>
#include <gibber/gibber-namespaces.h>

#include "test-transport.h"

static gboolean received_reply = FALSE;

static gboolean
send_hook (GibberTransport *transport,
           const guint8 *data,
           gsize length,
           GError **error,
           gpointer user_data)
{
  test_transport_write (TEST_TRANSPORT (transport), data, length);
  return TRUE;
}

static GibberXmppConnection *
create_xmpp_connection (void)
{
  GibberXmppConnection *xmpp_connection;
  TestTransport *transport;

  transport = test_transport_new (send_hook, NULL);
  xmpp_connection = gibber_xmpp_connection_new (GIBBER_TRANSPORT (transport));
  gibber_xmpp_connection_open (xmpp_connection, "to", "from", "1.0");

  g_object_unref (transport);

  return xmpp_connection;
}

static void
test_iq_helper_new (void)
{
  GibberXmppConnection *xmpp_connection = create_xmpp_connection ();
  GibberIqHelper *iq_helper = gibber_iq_helper_new (xmpp_connection);
  g_assert (iq_helper != NULL);
  g_object_unref (xmpp_connection);
  g_object_unref (iq_helper);
}

static void
reply_func (GibberIqHelper *helper,
            WockyStanza *sent_stanza,
            WockyStanza *reply_stanza,
            GObject *object,
            gpointer user_data)
{
  received_reply = TRUE;
}

static void
send_stanza_and_reply (GibberXmppConnection *xmpp_connection,
                       GibberIqHelper *iq_helper,
                       WockyStanza *stanza,
                       WockyStanza *reply)
{
  gboolean result;

  if (stanza != NULL)
    {
      result = gibber_iq_helper_send_with_reply (iq_helper, stanza, reply_func,
          NULL, NULL, NULL);
      g_assert (result);
    }

  if (reply != NULL)
    {
      result = gibber_xmpp_connection_send (xmpp_connection, reply, NULL);
      g_assert (result);
    }

  while (g_main_context_iteration (NULL, FALSE))
    ;

}

static void
test_send_with_reply (void)
{
  GibberXmppConnection *xmpp_connection = create_xmpp_connection ();
  GibberIqHelper *iq_helper = gibber_iq_helper_new (xmpp_connection);
  WockyStanza *stanza, *reply;

  received_reply = FALSE;

  stanza = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_SET,
      "from", "to",
      WOCKY_NODE_ATTRIBUTE, "id", "69",
      NULL);

  /* Reply of the stanza */
  reply = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_RESULT,
      "to", "from",
      WOCKY_NODE_ATTRIBUTE, "id", "69",
      NULL);

  send_stanza_and_reply (xmpp_connection, iq_helper, stanza, reply);
  g_assert (received_reply);

  g_object_unref (stanza);
  g_object_unref (reply);
  g_object_unref (xmpp_connection);
  g_object_unref (iq_helper);
}

static void
test_send_without_reply (void)
{
  GibberXmppConnection *xmpp_connection = create_xmpp_connection ();
  GibberIqHelper *iq_helper = gibber_iq_helper_new (xmpp_connection);
  WockyStanza *stanza;

  received_reply = FALSE;

  stanza = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_SET,
      "from", "to",
      WOCKY_NODE_ATTRIBUTE, "id", "69",
      NULL);

  send_stanza_and_reply (xmpp_connection, iq_helper, stanza, NULL);
  g_assert (!received_reply);

  g_object_unref (stanza);
  g_object_unref (xmpp_connection);
  g_object_unref (iq_helper);
}

static void
test_send_with_bad_reply_type (void)
{
  GibberXmppConnection *xmpp_connection = create_xmpp_connection ();
  GibberIqHelper *iq_helper = gibber_iq_helper_new (xmpp_connection);
  WockyStanza *stanza, *reply;

  received_reply = FALSE;

  stanza = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_SET,
      "from", "to",
      WOCKY_NODE_ATTRIBUTE, "id", "69",
      NULL);

  /* Reply can't be of sub type "get" */
  reply = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_GET,
      "to", "from",
      WOCKY_NODE_ATTRIBUTE, "id", "69",
      NULL);

  send_stanza_and_reply (xmpp_connection, iq_helper, stanza, reply);
  g_assert (!received_reply);

  g_object_unref (stanza);
  g_object_unref (reply);
  g_object_unref (xmpp_connection);
  g_object_unref (iq_helper);
}

static void
test_send_without_id (void)
{
  GibberXmppConnection *xmpp_connection = create_xmpp_connection ();
  GibberIqHelper *iq_helper = gibber_iq_helper_new (xmpp_connection);
  WockyStanza *stanza, *reply;
  gboolean result;
  const gchar *id;

  received_reply = FALSE;

  stanza = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_SET,
      "from", "to",
      NULL);

  result = gibber_iq_helper_send_with_reply (iq_helper, stanza, reply_func,
      NULL, NULL, NULL);
  g_assert (result);

  /* gibber_iq_helper_send_with_reply generated an id */
  id = wocky_node_get_attribute (wocky_stanza_get_top_node (stanza),
      "id");

  reply = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_RESULT,
      "to", "from",
      WOCKY_NODE_ATTRIBUTE, "id", id,
      NULL);

  result = gibber_xmpp_connection_send (xmpp_connection, reply, NULL);
  g_assert (result);

  while (g_main_context_iteration (NULL, FALSE))
    ;

  g_assert (received_reply);

  g_object_unref (stanza);
  g_object_unref (reply);
  g_object_unref (xmpp_connection);
  g_object_unref (iq_helper);
}

static void
test_new_result_reply (void)
{
  GibberXmppConnection *xmpp_connection = create_xmpp_connection ();
  GibberIqHelper *iq_helper = gibber_iq_helper_new (xmpp_connection);
  WockyStanza *stanza, *reply;
  gboolean result;

  received_reply = FALSE;

  stanza = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_SET,
      "from", "to",
      NULL);

  result = gibber_iq_helper_send_with_reply (iq_helper, stanza, reply_func,
      NULL, NULL, NULL);
  g_assert (result);

  reply = gibber_iq_helper_new_result_reply (stanza);
  g_assert (reply != NULL);
  g_assert_cmpstr (wocky_stanza_get_top_node (reply)->name, ==, "iq");
  g_assert_cmpstr (
      wocky_node_get_attribute (wocky_stanza_get_top_node (reply), "type"),
      ==, "result");
  result = gibber_xmpp_connection_send (xmpp_connection, reply, NULL);
  g_assert (result);

  while (g_main_context_iteration (NULL, FALSE))
    ;

  g_assert (received_reply);

  g_object_unref (stanza);
  g_object_unref (reply);
  g_object_unref (xmpp_connection);
  g_object_unref (iq_helper);
}

static void
test_new_error_reply (void)
{
  GibberXmppConnection *xmpp_connection = create_xmpp_connection ();
  GibberIqHelper *iq_helper = gibber_iq_helper_new (xmpp_connection);
  WockyStanza *stanza, *reply;
  WockyNode *error_node, *node;
  gboolean result;

  received_reply = FALSE;

  stanza = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_SET,
      "from", "to",
      NULL);

  result = gibber_iq_helper_send_with_reply (iq_helper, stanza, reply_func,
      NULL, NULL, NULL);
  g_assert (result);

  reply = gibber_iq_helper_new_error_reply (stanza,
      XMPP_ERROR_BAD_REQUEST, "test");
  g_assert (reply != NULL);
  g_assert_cmpstr (wocky_stanza_get_top_node (reply)->name, ==, "iq");
  g_assert_cmpstr (
      wocky_node_get_attribute (wocky_stanza_get_top_node (reply), "type"),
      ==, "error");

  error_node = wocky_node_get_child (wocky_stanza_get_top_node (reply),
      "error");
  g_assert (error_node != NULL);
  g_assert_cmpstr (wocky_node_get_attribute (error_node, "code"), ==, "400");
  g_assert_cmpstr (wocky_node_get_attribute (error_node, "type"), ==, "modify");

  node = wocky_node_get_child_ns (error_node, "bad-request",
      GIBBER_XMPP_NS_STANZAS);
  g_assert (node != NULL);

  node = wocky_node_get_child (error_node, "text");
  g_assert (node != NULL);
  g_assert_cmpstr (node->content, ==, "test");

  result = gibber_xmpp_connection_send (xmpp_connection, reply, NULL);
  g_assert (result);

  while (g_main_context_iteration (NULL, FALSE))
    ;

  g_assert (received_reply);

  g_object_unref (stanza);
  g_object_unref (reply);
  g_object_unref (xmpp_connection);
  g_object_unref (iq_helper);
}

static void
test_send_with_object_living (void)
{
  GibberXmppConnection *xmpp_connection = create_xmpp_connection ();
  GibberIqHelper *iq_helper = gibber_iq_helper_new (xmpp_connection);
  WockyStanza *stanza, *reply;
  gboolean result;
  GObject *object;

  received_reply = FALSE;

  /* We don't care about the TestTransport, we just need a GObject */
  object = g_object_new (TEST_TYPE_TRANSPORT, NULL);

  stanza = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_SET,
      "from", "to",
      NULL);

  result = gibber_iq_helper_send_with_reply (iq_helper, stanza, reply_func,
      object, NULL, NULL);
  g_assert (result);

  reply = gibber_iq_helper_new_result_reply (stanza);
  g_assert (reply != NULL);
  result = gibber_xmpp_connection_send (xmpp_connection, reply, NULL);
  g_assert (result);

  while (g_main_context_iteration (NULL, FALSE))
    ;

  g_assert (received_reply);

  g_object_unref (stanza);
  g_object_unref (reply);
  g_object_unref (object);
  g_object_unref (xmpp_connection);
  g_object_unref (iq_helper);
}

static void
test_send_with_object_destroyed (void)
{
  GibberXmppConnection *xmpp_connection = create_xmpp_connection ();
  GibberIqHelper *iq_helper = gibber_iq_helper_new (xmpp_connection);
  WockyStanza *stanza, *reply;
  gboolean result;
  GObject *object;

  received_reply = FALSE;

  /* We don't care about the TestTransport, we just need a GObject */
  object = g_object_new (TEST_TYPE_TRANSPORT, NULL);

  stanza = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_SET,
      "from", "to",
      NULL);

  result = gibber_iq_helper_send_with_reply (iq_helper, stanza, reply_func,
      object, NULL, NULL);
  g_assert (result);

  g_object_unref (object);

  reply = gibber_iq_helper_new_result_reply (stanza);
  g_assert (reply != NULL);
  result = gibber_xmpp_connection_send (xmpp_connection, reply, NULL);
  g_assert (result);

  while (g_main_context_iteration (NULL, FALSE))
    ;

  /* Object was destroyed before we send the reply so we don't receive
   * the reply */
  g_assert (!received_reply);

  g_object_unref (stanza);
  g_object_unref (reply);
  g_object_unref (xmpp_connection);
  g_object_unref (iq_helper);
}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_type_init ();

  g_test_add_func ("/gibber/iq-helper/new", test_iq_helper_new);
  g_test_add_func ("/gibber/iq-helper/send-with-reply", test_send_with_reply);
  g_test_add_func ("/gibber/iq-helper/send-without-reply",
      test_send_without_reply);
  g_test_add_func ("/gibber/iq-helper/send-with-bad-reply-type",
      test_send_with_bad_reply_type);
  g_test_add_func ("/gibber/iq-helper/send-without-id", test_send_without_id);
  g_test_add_func ("/gibber/iq-helper/new-result-reply", test_new_result_reply);
  g_test_add_func ("/gibber/iq-helper/new-error-reply", test_new_error_reply);
  g_test_add_func ("/gibber/iq-helper/send-with-object-living",
      test_send_with_object_living);
  g_test_add_func ("/gibber/iq-helper/send-with-object-destroyed",
      test_send_with_object_destroyed);

  return g_test_run ();
}

#include "test-transport.c"
