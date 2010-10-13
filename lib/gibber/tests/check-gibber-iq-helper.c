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

#include <gibber/gibber-xmpp-stanza.h>
#include <gibber/gibber-iq-helper.h>
#include <gibber/gibber-xmpp-error.h>
#include <gibber/gibber-namespaces.h>

#include <check.h>
#include "check-helpers.h"
#include "check-gibber.h"

#include "test-transport.h"

gboolean received_reply = FALSE;

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

START_TEST (test_iq_helper_new)
{
  GibberXmppConnection *xmpp_connection = create_xmpp_connection ();
  GibberIqHelper *iq_helper = gibber_iq_helper_new (xmpp_connection);
  fail_unless (iq_helper != NULL);
  g_object_unref (xmpp_connection);
  g_object_unref (iq_helper);
}
END_TEST

static void
reply_func (GibberIqHelper *helper,
            GibberXmppStanza *sent_stanza,
            GibberXmppStanza *reply_stanza,
            GObject *object,
            gpointer user_data)
{
  received_reply = TRUE;
}

static void
send_stanza_and_reply (GibberXmppConnection *xmpp_connection,
                       GibberIqHelper *iq_helper,
                       GibberXmppStanza *stanza,
                       GibberXmppStanza *reply)
{
  gboolean result;

  if (stanza != NULL)
    {
      result = gibber_iq_helper_send_with_reply (iq_helper, stanza, reply_func,
          NULL, NULL, NULL);
      fail_unless (result);
    }

  if (reply != NULL)
    {
      result = gibber_xmpp_connection_send (xmpp_connection, reply, NULL);
      fail_unless (result);
    }

  while (g_main_context_iteration (NULL, FALSE))
    ;

}

START_TEST (test_send_with_reply)
{
  GibberXmppConnection *xmpp_connection = create_xmpp_connection ();
  GibberIqHelper *iq_helper = gibber_iq_helper_new (xmpp_connection);
  GibberXmppStanza *stanza, *reply;

  received_reply = FALSE;

  stanza = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_IQ,
      GIBBER_STANZA_SUB_TYPE_SET,
      "from", "to",
      GIBBER_NODE_ATTRIBUTE, "id", "69",
      GIBBER_STANZA_END);

  /* Reply of the stanza */
  reply = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_IQ,
      GIBBER_STANZA_SUB_TYPE_RESULT,
      "to", "from",
      GIBBER_NODE_ATTRIBUTE, "id", "69",
      GIBBER_STANZA_END);

  send_stanza_and_reply (xmpp_connection, iq_helper, stanza, reply);
  fail_unless (received_reply);

  g_object_unref (stanza);
  g_object_unref (reply);
  g_object_unref (xmpp_connection);
  g_object_unref (iq_helper);
}
END_TEST

START_TEST (test_send_without_reply)
{
  GibberXmppConnection *xmpp_connection = create_xmpp_connection ();
  GibberIqHelper *iq_helper = gibber_iq_helper_new (xmpp_connection);
  GibberXmppStanza *stanza;

  received_reply = FALSE;

  stanza = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_IQ,
      GIBBER_STANZA_SUB_TYPE_SET,
      "from", "to",
      GIBBER_NODE_ATTRIBUTE, "id", "69",
      GIBBER_STANZA_END);

  send_stanza_and_reply (xmpp_connection, iq_helper, stanza, NULL);
  fail_unless (!received_reply);

  g_object_unref (stanza);
  g_object_unref (xmpp_connection);
  g_object_unref (iq_helper);
}
END_TEST

START_TEST (test_send_with_bad_reply_type)
{
  GibberXmppConnection *xmpp_connection = create_xmpp_connection ();
  GibberIqHelper *iq_helper = gibber_iq_helper_new (xmpp_connection);
  GibberXmppStanza *stanza, *reply;

  received_reply = FALSE;

  stanza = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_IQ,
      GIBBER_STANZA_SUB_TYPE_SET,
      "from", "to",
      GIBBER_NODE_ATTRIBUTE, "id", "69",
      GIBBER_STANZA_END);

  /* Reply can't be of sub type "get" */
  reply = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_IQ,
      GIBBER_STANZA_SUB_TYPE_GET,
      "to", "from",
      GIBBER_NODE_ATTRIBUTE, "id", "69",
      GIBBER_STANZA_END);

  send_stanza_and_reply (xmpp_connection, iq_helper, stanza, reply);
  fail_unless (!received_reply);

  g_object_unref (stanza);
  g_object_unref (reply);
  g_object_unref (xmpp_connection);
  g_object_unref (iq_helper);
}
END_TEST

START_TEST (test_send_without_id)
{
  GibberXmppConnection *xmpp_connection = create_xmpp_connection ();
  GibberIqHelper *iq_helper = gibber_iq_helper_new (xmpp_connection);
  GibberXmppStanza *stanza, *reply;
  gboolean result;
  const gchar *id;

  received_reply = FALSE;

  stanza = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_IQ,
      GIBBER_STANZA_SUB_TYPE_SET,
      "from", "to",
      GIBBER_STANZA_END);

  result = gibber_iq_helper_send_with_reply (iq_helper, stanza, reply_func,
      NULL, NULL, NULL);
  fail_unless (result);

  /* gibber_iq_helper_send_with_reply generated an id */
  id = gibber_xmpp_node_get_attribute (wocky_stanza_get_top_node (stanza),
      "id");

  reply = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_IQ,
      GIBBER_STANZA_SUB_TYPE_RESULT,
      "to", "from",
      GIBBER_NODE_ATTRIBUTE, "id", id,
      GIBBER_STANZA_END);

  result = gibber_xmpp_connection_send (xmpp_connection, reply, NULL);
  fail_unless (result);

  while (g_main_context_iteration (NULL, FALSE))
    ;

  fail_unless (received_reply);

  g_object_unref (stanza);
  g_object_unref (reply);
  g_object_unref (xmpp_connection);
  g_object_unref (iq_helper);
}
END_TEST

START_TEST (test_new_result_reply)
{
  GibberXmppConnection *xmpp_connection = create_xmpp_connection ();
  GibberIqHelper *iq_helper = gibber_iq_helper_new (xmpp_connection);
  GibberXmppStanza *stanza, *reply;
  gboolean result;

  received_reply = FALSE;

  stanza = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_IQ,
      GIBBER_STANZA_SUB_TYPE_SET,
      "from", "to",
      GIBBER_STANZA_END);

  result = gibber_iq_helper_send_with_reply (iq_helper, stanza, reply_func,
      NULL, NULL, NULL);
  fail_unless (result);

  reply = gibber_iq_helper_new_result_reply (stanza);
  fail_unless (reply != NULL);
  fail_unless (strcmp (wocky_stanza_get_top_node (reply)->name, "iq") == 0);
  fail_unless (strcmp (
        gibber_xmpp_node_get_attribute (wocky_stanza_get_top_node (reply),
          "type"),
        "result") == 0);
  result = gibber_xmpp_connection_send (xmpp_connection, reply, NULL);
  fail_unless (result);

  while (g_main_context_iteration (NULL, FALSE))
    ;

  fail_unless (received_reply);

  g_object_unref (stanza);
  g_object_unref (reply);
  g_object_unref (xmpp_connection);
  g_object_unref (iq_helper);
}
END_TEST

START_TEST (test_new_error_reply)
{
  GibberXmppConnection *xmpp_connection = create_xmpp_connection ();
  GibberIqHelper *iq_helper = gibber_iq_helper_new (xmpp_connection);
  GibberXmppStanza *stanza, *reply;
  GibberXmppNode *error_node, *node;
  gboolean result;

  received_reply = FALSE;

  stanza = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_IQ,
      GIBBER_STANZA_SUB_TYPE_SET,
      "from", "to",
      GIBBER_STANZA_END);

  result = gibber_iq_helper_send_with_reply (iq_helper, stanza, reply_func,
      NULL, NULL, NULL);
  fail_unless (result);

  reply = gibber_iq_helper_new_error_reply (stanza,
      XMPP_ERROR_BAD_REQUEST, "test");
  fail_unless (reply != NULL);
  fail_unless (strcmp (wocky_stanza_get_top_node (reply)->name, "iq") == 0);
  fail_unless (strcmp (gibber_xmpp_node_get_attribute (
          wocky_stanza_get_top_node (reply), "type"),
        "error") == 0);

  error_node = gibber_xmpp_node_get_child (wocky_stanza_get_top_node (reply),
      "error");
  fail_if (error_node == NULL);
  fail_if (strcmp (gibber_xmpp_node_get_attribute (error_node, "code"),
        "400") != 0);
  fail_if (strcmp (gibber_xmpp_node_get_attribute (error_node, "type"),
        "modify") != 0);

  node = gibber_xmpp_node_get_child_ns (error_node, "bad-request",
      GIBBER_XMPP_NS_STANZAS);
  fail_if (node == NULL);

  node = gibber_xmpp_node_get_child (error_node, "text");
  fail_if (node == NULL);
  fail_if (strcmp (node->content, "test") != 0);

  result = gibber_xmpp_connection_send (xmpp_connection, reply, NULL);
  fail_unless (result);

  while (g_main_context_iteration (NULL, FALSE))
    ;

  fail_unless (received_reply);

  g_object_unref (stanza);
  g_object_unref (reply);
  g_object_unref (xmpp_connection);
  g_object_unref (iq_helper);
}
END_TEST

START_TEST (test_send_with_object_living)
{
  GibberXmppConnection *xmpp_connection = create_xmpp_connection ();
  GibberIqHelper *iq_helper = gibber_iq_helper_new (xmpp_connection);
  GibberXmppStanza *stanza, *reply;
  gboolean result;
  GObject *object;

  received_reply = FALSE;

  /* We don't care about the TestTransport, we just need a GObject */
  object = g_object_new (TEST_TYPE_TRANSPORT, NULL);

  stanza = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_IQ,
      GIBBER_STANZA_SUB_TYPE_SET,
      "from", "to",
      GIBBER_STANZA_END);

  result = gibber_iq_helper_send_with_reply (iq_helper, stanza, reply_func,
      object, NULL, NULL);
  fail_unless (result);

  reply = gibber_iq_helper_new_result_reply (stanza);
  fail_unless (reply != NULL);
  result = gibber_xmpp_connection_send (xmpp_connection, reply, NULL);
  fail_unless (result);

  while (g_main_context_iteration (NULL, FALSE))
    ;

  fail_unless (received_reply);

  g_object_unref (stanza);
  g_object_unref (reply);
  g_object_unref (object);
  g_object_unref (xmpp_connection);
  g_object_unref (iq_helper);
}
END_TEST

START_TEST (test_send_with_object_destroyed)
{
  GibberXmppConnection *xmpp_connection = create_xmpp_connection ();
  GibberIqHelper *iq_helper = gibber_iq_helper_new (xmpp_connection);
  GibberXmppStanza *stanza, *reply;
  gboolean result;
  GObject *object;

  received_reply = FALSE;

  /* We don't care about the TestTransport, we just need a GObject */
  object = g_object_new (TEST_TYPE_TRANSPORT, NULL);

  stanza = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_IQ,
      GIBBER_STANZA_SUB_TYPE_SET,
      "from", "to",
      GIBBER_STANZA_END);

  result = gibber_iq_helper_send_with_reply (iq_helper, stanza, reply_func,
      object, NULL, NULL);
  fail_unless (result);

  g_object_unref (object);

  reply = gibber_iq_helper_new_result_reply (stanza);
  fail_unless (reply != NULL);
  result = gibber_xmpp_connection_send (xmpp_connection, reply, NULL);
  fail_unless (result);

  while (g_main_context_iteration (NULL, FALSE))
    ;

  /* Object was destroyed before we send the reply so we don't receive
   * the reply */
  fail_unless (!received_reply);

  g_object_unref (stanza);
  g_object_unref (reply);
  g_object_unref (xmpp_connection);
  g_object_unref (iq_helper);
}
END_TEST

TCase *
make_gibber_iq_helper_tcase (void)
{
  TCase *tc = tcase_create ("IQ helper");
  tcase_add_test (tc, test_iq_helper_new);
  tcase_add_test (tc, test_send_with_reply);
  tcase_add_test (tc, test_send_without_reply);
  tcase_add_test (tc, test_send_with_bad_reply_type);
  tcase_add_test (tc, test_send_without_id);
  tcase_add_test (tc, test_new_result_reply);
  tcase_add_test (tc, test_new_error_reply);
  tcase_add_test (tc, test_send_with_object_living);
  tcase_add_test (tc, test_send_with_object_destroyed);

  return tc;
}
