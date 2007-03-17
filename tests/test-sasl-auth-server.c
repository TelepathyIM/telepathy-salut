/*
 * test-sasl-auth-server.c - Source for TestSaslAuthServer
 * Copyright (C) 2006 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd@luon.net>
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

#include "test-sasl-auth-server.h"

#include <gibber/gibber-xmpp-stanza.h>
#include <gibber/gibber-xmpp-connection.h>
#include <gibber/gibber-transport.h>

#include <gibber/gibber-namespaces.h>

#include <sasl/sasl.h>

#define CHECK_SASL_RETURN(x)                                \
G_STMT_START   {                                            \
    if (x < SASL_OK) {                                     \
      fprintf(stderr, "sasl error (%d): %s\n",              \
           ret, sasl_errdetail(priv->sasl_conn));           \
      g_assert_not_reached();                               \
    }                                                       \
} G_STMT_END

G_DEFINE_TYPE(TestSaslAuthServer, test_sasl_auth_server, G_TYPE_OBJECT)

#if 0
/* signal enum */
enum
{
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};
#endif

typedef enum {
  AUTH_STATE_STARTED,
  AUTH_STATE_CHALLENGE,
  AUTH_STATE_FINAL_CHALLENGE,
  AUTH_STATE_AUTHENTICATED,
} AuthState; 

/* private structure */
typedef struct _TestSaslAuthServerPrivate TestSaslAuthServerPrivate;

struct _TestSaslAuthServerPrivate
{
  gboolean dispose_has_run;
  GibberXmppConnection *conn;
  sasl_conn_t *sasl_conn;
  gchar *username;
  gchar *password;
  gchar *mech;
  AuthState state;
  ServerProblem problem;
};

#define TEST_SASL_AUTH_SERVER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), TEST_TYPE_SASL_AUTH_SERVER, TestSaslAuthServerPrivate))

static void
test_sasl_auth_server_init (TestSaslAuthServer *obj)
{
  TestSaslAuthServerPrivate *priv = TEST_SASL_AUTH_SERVER_GET_PRIVATE (obj);
  priv->username = NULL;
  priv->password = NULL;
  priv->mech = NULL;
  priv->state = AUTH_STATE_STARTED;

  /* allocate any data required by the object here */
}

static void test_sasl_auth_server_dispose (GObject *object);
static void test_sasl_auth_server_finalize (GObject *object);

static void
test_sasl_auth_server_class_init (TestSaslAuthServerClass *test_sasl_auth_server_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (test_sasl_auth_server_class);

  g_type_class_add_private (test_sasl_auth_server_class, sizeof (TestSaslAuthServerPrivate));

  object_class->dispose = test_sasl_auth_server_dispose;
  object_class->finalize = test_sasl_auth_server_finalize;

}

void
test_sasl_auth_server_dispose (GObject *object)
{
  TestSaslAuthServer *self = TEST_SASL_AUTH_SERVER (object);
  TestSaslAuthServerPrivate *priv = TEST_SASL_AUTH_SERVER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */
  g_object_unref(priv->conn);

  if (G_OBJECT_CLASS (test_sasl_auth_server_parent_class)->dispose)
    G_OBJECT_CLASS (test_sasl_auth_server_parent_class)->dispose (object);
}

void
test_sasl_auth_server_finalize (GObject *object)
{
  TestSaslAuthServer *self = TEST_SASL_AUTH_SERVER (object);
  TestSaslAuthServerPrivate *priv = TEST_SASL_AUTH_SERVER_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  g_free(priv->username);
  g_free(priv->password);
  g_free(priv->mech);

  G_OBJECT_CLASS (test_sasl_auth_server_parent_class)->finalize (object);
}

static void
parse_error(GibberXmppConnection *connection, gpointer user_data) {
  fprintf(stderr, "PARSING FAILED IN SASL SERVER\n");
  g_assert_not_reached();
}

static void
stream_opened(GibberXmppConnection *connection,
              const gchar *to, const gchar *from, const gchar *version,
              gpointer user_data) {
  TestSaslAuthServer *self = TEST_SASL_AUTH_SERVER(user_data);
  TestSaslAuthServerPrivate * priv = TEST_SASL_AUTH_SERVER_GET_PRIVATE(self);
  GibberXmppStanza *stanza;
  GibberXmppNode *mechnode = NULL;

  gibber_xmpp_connection_open(connection, NULL, "testserver", "1.0");
  /* Send stream features */
  stanza = gibber_xmpp_stanza_new("features");
  gibber_xmpp_node_set_ns(stanza->node, GIBBER_XMPP_NS_STREAM);

  if (priv->problem != SERVER_PROBLEM_NO_SASL) { 
    mechnode = gibber_xmpp_node_add_child_ns(stanza->node, 
                                             "mechanisms", 
                                             GIBBER_XMPP_NS_SASL_AUTH);
    if (priv->problem == SERVER_PROBLEM_NO_MECHANISMS) {
      /* lalala */
    } else if (priv->mech != NULL) {
      gibber_xmpp_node_add_child_with_content(mechnode, 
          "mechanism", priv->mech);
    } else {
      const gchar *mechs;
      gchar **mechlist;
      gchar **tmp;
      int ret;
      ret = sasl_listmech(priv->sasl_conn, NULL, 
              "","\n","", 
              &mechs, 
              NULL,NULL); 
      CHECK_SASL_RETURN(ret);
      mechlist = g_strsplit(mechs, "\n", -1);
      for (tmp = mechlist; *tmp != NULL; tmp++) {
      gibber_xmpp_node_add_child_with_content(mechnode, "mechanism", *tmp);
      }
      g_strfreev(mechlist);
    }
  }
  g_assert(gibber_xmpp_connection_send(connection, stanza, NULL));
}

static void
stream_closed(GibberXmppConnection *connection, gpointer user_data) {
  gibber_xmpp_connection_close(connection);
}

static void
auth_succeeded(TestSaslAuthServer *self) {
  TestSaslAuthServerPrivate * priv = TEST_SASL_AUTH_SERVER_GET_PRIVATE(self);
  GibberXmppStanza *s;

  g_assert(priv->state < AUTH_STATE_AUTHENTICATED);
  priv->state = AUTH_STATE_AUTHENTICATED;

  s = gibber_xmpp_stanza_new("success");
  gibber_xmpp_node_set_ns(s->node, GIBBER_XMPP_NS_SASL_AUTH);

  /* As a result of how the test works, sending out the success will cause the
   * reopening of the stream, so we need to restart the connection first! */
  gibber_xmpp_connection_restart(priv->conn);

  g_assert(gibber_xmpp_connection_send(priv->conn, s, NULL));

  g_object_unref(s);
}

static void
handle_auth(TestSaslAuthServer *self, GibberXmppStanza *stanza) {
  TestSaslAuthServerPrivate *priv = TEST_SASL_AUTH_SERVER_GET_PRIVATE(self);
  const gchar *mech = gibber_xmpp_node_get_attribute(stanza->node, "mechanism");
  guchar *response = NULL;
  const gchar *challenge; 
  unsigned challenge_len;
  gsize response_len = 0;
  int ret;

  if (stanza->node->content != NULL) {
    response = g_base64_decode(stanza->node->content, &response_len);
  }

  g_assert(priv->state == AUTH_STATE_STARTED);
  priv->state = AUTH_STATE_CHALLENGE;


  ret = sasl_server_start(priv->sasl_conn, 
            mech,
            (gchar *)response,
            (unsigned) response_len,
            &challenge,
            &challenge_len);

  CHECK_SASL_RETURN(ret);
  if (challenge_len > 0)  {
    GibberXmppStanza *c;
    gchar *challenge64;

    if (ret == SASL_OK) {
      priv->state = AUTH_STATE_FINAL_CHALLENGE;
    }

    challenge64 = g_base64_encode((guchar *)challenge, challenge_len);

    c = gibber_xmpp_stanza_new("challenge");
    gibber_xmpp_node_set_ns(c->node, GIBBER_XMPP_NS_SASL_AUTH);
    gibber_xmpp_node_set_content(c->node, challenge64);
    g_assert(gibber_xmpp_connection_send(priv->conn, c, NULL));
    g_object_unref(c);

    g_free(challenge64);
  } else if (ret == SASL_OK) {
    auth_succeeded(self);
  } else {
    g_assert_not_reached();
  }
}

static void
handle_response(TestSaslAuthServer *self, GibberXmppStanza *stanza) {
  TestSaslAuthServerPrivate * priv = TEST_SASL_AUTH_SERVER_GET_PRIVATE(self);
  guchar *response = NULL;
  const gchar *challenge; 
  unsigned challenge_len;
  gsize response_len = 0;
  int ret;


  if (priv->state == AUTH_STATE_FINAL_CHALLENGE) {
    g_assert(stanza->node->content == NULL);
    auth_succeeded(self);
    return;
  }

  g_assert(priv->state == AUTH_STATE_CHALLENGE); 

  if (stanza->node->content != NULL) {
    response = g_base64_decode(stanza->node->content, &response_len);
  }

  ret = sasl_server_step(priv->sasl_conn, (gchar *)response,
            (unsigned) response_len,
            &challenge,
            &challenge_len);

  CHECK_SASL_RETURN(ret);
  if (challenge_len > 0)  {
    GibberXmppStanza *c;
    gchar *challenge64;

    if (ret == SASL_OK) {
      priv->state = AUTH_STATE_FINAL_CHALLENGE;
    }

    challenge64 = g_base64_encode((guchar *)challenge, challenge_len);

    c = gibber_xmpp_stanza_new("challenge");
    gibber_xmpp_node_set_ns(c->node, GIBBER_XMPP_NS_SASL_AUTH);
    gibber_xmpp_node_set_content(c->node, challenge64);
    g_assert(gibber_xmpp_connection_send(priv->conn, c, NULL));
    g_object_unref(c);

    g_free(challenge64);
  } else if (ret == SASL_OK) {
    auth_succeeded(self);
  } else {
    g_assert_not_reached();
  }
  g_free(response);
}

#define HANDLE(x) { #x, handle_##x }
static void
received_stanza(GibberXmppConnection *connection,
                GibberXmppStanza *stanza,
                gpointer user_data) {
  TestSaslAuthServer *self = TEST_SASL_AUTH_SERVER(user_data);
  int i;
  struct {
    const gchar *name;
    void (*func)(TestSaslAuthServer *self, GibberXmppStanza *stanza);
  } handlers[] = { HANDLE(auth), HANDLE(response) };

  if (strcmp(gibber_xmpp_node_get_ns(stanza->node), GIBBER_XMPP_NS_SASL_AUTH)) {
    g_assert_not_reached();
  }

  for (i = 0 ; handlers[i].name != NULL; i++) {
    if (!strcmp(stanza->node->name, handlers[i].name)) {
      handlers[i].func(self, stanza);
      return;
    }
  }

  g_assert_not_reached();
}

static int
test_sasl_server_auth_log(void *context, int level, const gchar *message) {
  //printf("LOG-> %s\n", message);
  return SASL_OK;
}

static int
test_sasl_server_auth_getopt(void *context, 
                             const char *plugin_name,
                             const gchar *option,
                             const gchar **result,
                             guint *len) {
  int i;
  static const struct {
    const gchar *name;
    const gchar *value;
  } options[] = { 
    { "auxprop_plugin", "sasldb"},
    { "sasldb_path", "./sasl-test.db"},
    { NULL, NULL },
  };
  for (i = 0; options[i].name != NULL; i++) {
    if (!strcmp(option, options[i].name)) {
      *result = options[i].value;
      if (len != NULL)
        *len = strlen(options[i].value);
    }
  }
  //printf("getopt: %s\n", option);
  return SASL_OK;
}

TestSaslAuthServer *
test_sasl_auth_server_new(GibberTransport *transport, gchar *mech,
                          const gchar *user, const gchar *password,
                          ServerProblem problem) {
  TestSaslAuthServer *server;
  TestSaslAuthServerPrivate *priv;
  static gboolean sasl_initialized = FALSE;
  int ret;
  static sasl_callback_t callbacks[] = { 
    { SASL_CB_LOG, test_sasl_server_auth_log, NULL },
    { SASL_CB_GETOPT, test_sasl_server_auth_getopt, NULL },
    { SASL_CB_LIST_END, NULL, NULL },
  };

  if (!sasl_initialized) {
    sasl_server_init(NULL, NULL);
    sasl_initialized = TRUE;
  }

  server = g_object_new(TEST_TYPE_SASL_AUTH_SERVER, NULL);
  priv = TEST_SASL_AUTH_SERVER_GET_PRIVATE(server);

  priv->state = AUTH_STATE_STARTED;

  ret = sasl_server_new("xmpp", 
                        NULL, NULL, NULL, NULL,
                        callbacks, SASL_SUCCESS_DATA,
                        &(priv->sasl_conn));
  CHECK_SASL_RETURN(ret);

  ret = sasl_setpass(priv->sasl_conn, user, password, 
                     strlen(password), NULL, 0, SASL_SET_CREATE);

  CHECK_SASL_RETURN(ret);

  priv->username = g_strdup(user);
  priv->password = g_strdup(password);
  priv->mech = g_strdup(mech);
  priv->problem = problem;

  priv->conn = gibber_xmpp_connection_new(transport);
  g_signal_connect(priv->conn, "parse-error",
                      G_CALLBACK(parse_error), server);
  g_signal_connect(priv->conn, "stream-opened",
                      G_CALLBACK(stream_opened), server);
  g_signal_connect(priv->conn, "stream-closed",
                      G_CALLBACK(stream_closed), server);
  g_signal_connect(priv->conn, "received-stanza",
                      G_CALLBACK(received_stanza), server);


  return server;
}
