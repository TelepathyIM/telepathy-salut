/*
 * gibber-sasl-auth.c - Source for GibberSaslAuth
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

#include "gibber-sasl-auth.h"
#include "gibber-sasl-auth-signals-marshal.h"
#include "gibber-namespaces.h"

#include "libmd5-rfc/md5.h"

#define DEBUG_FLAG DEBUG_SASL
#include "gibber-debug.h"

G_DEFINE_TYPE(GibberSaslAuth, gibber_sasl_auth, G_TYPE_OBJECT)

/* signal enum */
enum
{
    USERNAME_REQUESTED,
    PASSWORD_REQUESTED,
    AUTHENTICATION_SUCCEEDED,
    AUTHENTICATION_FAILED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

typedef enum {
  GIBBER_SASL_AUTH_STATE_NO_MECH = 0,
  GIBBER_SASL_AUTH_STATE_PLAIN_STARTED,
  GIBBER_SASL_AUTH_STATE_DIGEST_MD5_STARTED,
  GIBBER_SASL_AUTH_STATE_DIGEST_MD5_SENT_AUTH_RESPONSE,
  GIBBER_SASL_AUTH_STATE_DIGEST_MD5_SENT_FINAL_RESPONSE,
} GibberSaslAuthState;

typedef enum {
  GIBBER_SASL_AUTH_PLAIN = 0, 
  GIBBER_SASL_AUTH_DIGEST_MD5,
  GIBBER_SASL_AUTH_NR_MECHANISMS,
} GibberSaslAuthMechanism;

/* private structure */
typedef struct _GibberSaslAuthPrivate GibberSaslAuthPrivate;

struct _GibberSaslAuthPrivate
{
  gboolean dispose_has_run;
  GibberXmppConnection *connection;
  gchar *server;
  gchar *digest_md5_rspauth;
  gulong stanza_signal_id; 
  GibberSaslAuthState state;
  GibberSaslAuthMechanism mech;
};

#define GIBBER_SASL_AUTH_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_SASL_AUTH, GibberSaslAuthPrivate))

GQuark
gibber_sasl_auth_error_quark (void) {
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("gibber_sasl_auth_error");

  return quark;
}


static void
gibber_sasl_auth_init (GibberSaslAuth *obj)
{
  //GibberSaslAuthPrivate *priv = GIBBER_SASL_AUTH_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
}

static void gibber_sasl_auth_dispose (GObject *object);
static void gibber_sasl_auth_finalize (GObject *object);

static void
gibber_sasl_auth_class_init (GibberSaslAuthClass *gibber_sasl_auth_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_sasl_auth_class);

  g_type_class_add_private (gibber_sasl_auth_class, sizeof (GibberSaslAuthPrivate));

  signals[USERNAME_REQUESTED] = 
    g_signal_new("username-requested",
                 G_OBJECT_CLASS_TYPE(gibber_sasl_auth_class),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 gibber_sasl_auth_marshal_STRING__VOID,
                 G_TYPE_STRING, 0);

  signals[PASSWORD_REQUESTED] = 
    g_signal_new("password-requested",
                 G_OBJECT_CLASS_TYPE(gibber_sasl_auth_class),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 gibber_sasl_auth_marshal_STRING__VOID,
                 G_TYPE_STRING, 0);
  signals[AUTHENTICATION_SUCCEEDED] = 
    g_signal_new("authentication-succeeded",
                 G_OBJECT_CLASS_TYPE(gibber_sasl_auth_class),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);
  signals[AUTHENTICATION_FAILED] = 
    g_signal_new("authentication-failed",
                 G_OBJECT_CLASS_TYPE(gibber_sasl_auth_class),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 gibber_sasl_auth_marshal_VOID__UINT_INT_STRING,
                 G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_INT, G_TYPE_STRING);

  object_class->dispose = gibber_sasl_auth_dispose;
  object_class->finalize = gibber_sasl_auth_finalize;

}

void
gibber_sasl_auth_dispose (GObject *object)
{
  GibberSaslAuth *self = GIBBER_SASL_AUTH (object);
  GibberSaslAuthPrivate *priv = GIBBER_SASL_AUTH_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */
  if (priv->stanza_signal_id > 0) {
    g_assert(priv->connection != NULL);
    g_signal_handler_disconnect(priv->connection, priv->stanza_signal_id);
  }

  if (priv->connection != NULL) {
    g_object_unref(priv->connection);
  }

  if (G_OBJECT_CLASS (gibber_sasl_auth_parent_class)->dispose)
    G_OBJECT_CLASS (gibber_sasl_auth_parent_class)->dispose (object);
}

void
gibber_sasl_auth_finalize (GObject *object)
{
  GibberSaslAuth *self = GIBBER_SASL_AUTH (object);
  GibberSaslAuthPrivate *priv = GIBBER_SASL_AUTH_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  g_free(priv->server);
  g_free(priv->digest_md5_rspauth);


  G_OBJECT_CLASS (gibber_sasl_auth_parent_class)->finalize (object);
}

static void
auth_reset(GibberSaslAuth *sasl) {
  GibberSaslAuthPrivate *priv = GIBBER_SASL_AUTH_GET_PRIVATE (sasl);

  g_free(priv->server);
  priv->server = NULL;

  g_free(priv->digest_md5_rspauth);
  priv->digest_md5_rspauth = NULL;

  if (priv->stanza_signal_id > 0) {
    g_assert(priv->connection != NULL);
    g_signal_handler_disconnect(priv->connection, priv->stanza_signal_id);
    priv->stanza_signal_id = 0;
  }

  if (priv->connection != NULL) {
    g_object_unref(priv->connection);
    priv->connection = NULL;
  }

  priv->state = GIBBER_SASL_AUTH_STATE_NO_MECH;
}

static void
auth_succeeded(GibberSaslAuth *sasl) {
  auth_reset(sasl);
  DEBUG("Authentication succeeded");
  g_signal_emit(sasl, signals[AUTHENTICATION_SUCCEEDED], 0);
}

static void
auth_failed(GibberSaslAuth *sasl, gint error, const gchar *format, ...) {
  gchar *message;
  va_list args;

  auth_reset(sasl);

  va_start(args, format);
  message = g_strdup_vprintf(format, args);
  va_end(args);

  DEBUG("Authentication failed!: %s", message);

  g_signal_emit(sasl, signals[AUTHENTICATION_FAILED], 0, 
     GIBBER_SASL_AUTH_ERROR, error, message);

  g_free(message);
}


GibberSaslAuth *gibber_sasl_auth_new(void) {
  return g_object_new(GIBBER_TYPE_SASL_AUTH, NULL);
}

static gboolean
each_mechanism(GibberXmppNode *node, gpointer user_data) {
  GSList **list = (GSList **)user_data;
  if (strcmp(node->name, "mechanism")) {
    return TRUE;
  }
  *list = g_slist_append(*list, g_strdup(node->content));
  return TRUE;
}

static GSList *
gibber_sasl_auth_mechanisms_to_list(GibberXmppNode *mechanisms)  {
  GSList *result = NULL;

  if (mechanisms == NULL) 
    return NULL;

  gibber_xmpp_node_each_child(mechanisms, 
                              each_mechanism,
                              &result);
  return result;
}

gboolean
gibber_sasl_auth_has_mechanism(GSList *list, const gchar *mech) {
  GSList *t;
  for (t = list ; t != NULL ; t = g_slist_next(t)) {
    if (!strcmp((gchar *)t->data, mech)) {
      return TRUE;
    }
  }
  return FALSE;
}

static gchar *
strndup_unescaped(const gchar *str, gsize len) {
  const gchar *s;
  gchar *d, *ret;

  ret = g_malloc0(len + 1);
  for (s = str, d = ret ; s < (str + len) ; s++, d++) {
    if (*s == '\\')
      s++;
    *d = *s;
  }

  return ret;
}

static GHashTable *
digest_md5_challenge_to_hash(const gchar * challenge) {
  GHashTable *result = g_hash_table_new_full(g_str_hash, 
                                             g_str_equal,
                                             g_free, g_free);
  const gchar *keystart, *keyend, *valstart;
  const gchar *c = challenge;
  gchar *key, *val;

  do { 
    keystart = c;
    for (; *c != '\0' && *c != '='; c++) 
      ;

    if (*c == '\0' || c == keystart)
      goto error;

    keyend = c; 
    c++;

    if (*c == '"') {
      c++;
      valstart = c;
      for (; *c != '\0' && *c != '"'; c++)
        ;
      if (*c == '\0' || c == valstart)
        goto error;
      val = strndup_unescaped(valstart, c - valstart);
      c++;
    } else {
      valstart = c;
      for (; *c !=  '\0' && *c != ','; c++)
        ;
      if (c == valstart) 
        goto error;
      val = g_strndup(valstart, c - valstart);
    }

    key = g_strndup(keystart, keyend - keystart);

    g_hash_table_insert(result, key, val);

    if (*c == ',')
      c++;
  } while (*c != '\0');

  return result;
error:
  DEBUG("Failed to parse challenge: %s", challenge);
  g_hash_table_destroy(result);
  return NULL;
}

static gchar *
md5_hex_hash(gchar *value, gsize len) {
  md5_byte_t digest_md5[16];
  md5_state_t md5_calc;
  GString *str = g_string_sized_new(32);
  int i;

  md5_init(&md5_calc);
  md5_append(&md5_calc, (const md5_byte_t *)value, len);
  md5_finish(&md5_calc, digest_md5);
  for (i = 0 ; i < 16 ; i++) {
    g_string_append_printf(str, "%02x", digest_md5[i]);
  }
  return g_string_free(str, FALSE);
}

static gchar *
digest_md5_generate_cnonce(void) {
  /* RFC 2831 recommends the the nonce to be either hexadecimal or base64 with
   * at least 64 bits of entropy */
#define NR 8
  guint32 n[NR]; 
  int i;
  for (i = 0; i < NR; i++)
    n[i] = g_random_int();
  return g_base64_encode((guchar *)n, sizeof(n));
}

static gchar *
md5_prepare_response(GibberSaslAuth *sasl, GHashTable *challenge) {
  GibberSaslAuthPrivate *priv = GIBBER_SASL_AUTH_GET_PRIVATE(sasl);
  GString *response = g_string_new("");
  const gchar *realm, *nonce;
  gchar *a1, *a1h, *a2, *a2h, *kd, *kdh;
  gchar *cnonce = NULL;
  gchar *username = NULL;
  gchar *password = NULL;
  gchar *tmp;
  md5_byte_t digest_md5[16];
  md5_state_t md5_calc;
  gsize len;

  g_signal_emit(sasl, signals[USERNAME_REQUESTED], 0, &username);
  g_signal_emit(sasl, signals[PASSWORD_REQUESTED], 0, &password);

  if (username == NULL || password == NULL) {
    auth_failed(sasl, GIBBER_SASL_AUTH_ERROR_NO_CREDENTIALS,
               "No username or password provided");
    goto error;
  }
  DEBUG("Got username and password");

  nonce = g_hash_table_lookup(challenge, "nonce");
  if (nonce == NULL || nonce == '\0') {
    auth_failed(sasl, GIBBER_SASL_AUTH_ERROR_INVALID_REPLY,
               "Server didn't provide a nonce in the challenge");
    goto error;
  }

  cnonce = digest_md5_generate_cnonce();

  /* FIXME challenge can contain multiple realms */
  realm = g_hash_table_lookup(challenge, "realm");
  if (realm == NULL) {
    realm = priv->server;
  }

  /* FIXME properly escape values */
  g_string_append_printf(response, "username=\"%s\"", username);
  g_string_append_printf(response, ",realm=\"%s\"", realm);
  g_string_append_printf(response, ",digest-uri=\"xmpp/%s\"", realm);
  g_string_append_printf(response, ",nonce=\"%s\",nc=00000001", nonce);
  g_string_append_printf(response, ",cnonce=\"%s\"", cnonce);
  /* FIXME should check if auth is in the cop challenge val */
  g_string_append_printf(response, ",qop=auth,charset=utf-8");

  tmp = g_strdup_printf("%s:%s:%s", username, realm, password);
  md5_init(&md5_calc);
  md5_append(&md5_calc, (const md5_byte_t *)tmp, strlen(tmp));
  md5_finish(&md5_calc, digest_md5);
  g_free(tmp);

  a1 = g_strdup_printf("0123456789012345:%s:%s", nonce, cnonce);
  len = strlen(a1);
  memcpy(a1, digest_md5, 16);
  a1h = md5_hex_hash(a1, len);

  a2 = g_strdup_printf("AUTHENTICATE:xmpp/%s", realm);
  a2h = md5_hex_hash(a2, strlen(a2));

  kd = g_strdup_printf("%s:%s:00000001:%s:auth:%s", a1h, nonce, cnonce, a2h);
  kdh = md5_hex_hash(kd, strlen(kd));
  g_string_append_printf(response, ",response=%s", kdh);

  g_free(kd);
  g_free(kdh);
  g_free(a2);
  g_free(a2h);
  
  /* Calculate the response we expect from the server */
  a2 = g_strdup_printf(":xmpp/%s", realm);
  a2h = md5_hex_hash(a2, strlen(a2));

  kd = g_strdup_printf("%s:%s:00000001:%s:auth:%s", a1h, nonce, cnonce, a2h);
  g_free(priv->digest_md5_rspauth);
  priv->digest_md5_rspauth = md5_hex_hash(kd, strlen(kd));

  g_free(a1);
  g_free(a1h);
  g_free(a2);
  g_free(a2h);
  g_free(kd);

out:
  g_free(cnonce);
  g_free(username);
  g_free(password);
  
  return response != NULL ? g_string_free(response, FALSE) : NULL;

error:
  g_string_free(response, TRUE);
  response = NULL;
  goto out;
}

static void
digest_md5_send_initial_response(GibberSaslAuth *sasl, GHashTable *challenge) {
  GibberSaslAuthPrivate *priv = GIBBER_SASL_AUTH_GET_PRIVATE(sasl);
  GibberXmppStanza *stanza;
  gchar *response, *response64;
  GError *send_error = NULL;

  response = md5_prepare_response(sasl, challenge);
  if (response == NULL) {
    return;
  }
  DEBUG("Prepared response: %s", response);

  response64 = g_base64_encode((guchar *)response, strlen(response));

  stanza = gibber_xmpp_stanza_new("response");
  gibber_xmpp_node_set_ns(stanza->node, GIBBER_XMPP_NS_SASL_AUTH);
  gibber_xmpp_node_set_content(stanza->node, response64);

  priv->state = GIBBER_SASL_AUTH_STATE_DIGEST_MD5_SENT_AUTH_RESPONSE;

  if (!gibber_xmpp_connection_send(priv->connection, stanza, &send_error)) {
    auth_failed(sasl, GIBBER_SASL_AUTH_ERROR_NETWORK,
        "Failed to send response: %s", send_error->message);
    g_error_free(send_error);
  }

  g_free(response);
  g_free(response64);
  g_object_unref(stanza);
}

static void
digest_md5_check_server_response(GibberSaslAuth *sasl, GHashTable *challenge) {
  GibberXmppStanza *stanza;
  const gchar *rspauth;
  GError *send_error = NULL;
  GibberSaslAuthPrivate *priv = GIBBER_SASL_AUTH_GET_PRIVATE(sasl);

  rspauth = g_hash_table_lookup(challenge, "rspauth");
  if (rspauth == NULL) {
    auth_failed(sasl, GIBBER_SASL_AUTH_ERROR_INVALID_REPLY,
               "Server send an invalid reply (no rspauth)");
    return;
  }

  if (strcmp(priv->digest_md5_rspauth, rspauth)) {
    auth_failed(sasl, GIBBER_SASL_AUTH_ERROR_INVALID_REPLY,
               "Server send an invalid reply (rspauth not matching)");
    return;
  }

  stanza = gibber_xmpp_stanza_new("response");
  gibber_xmpp_node_set_ns(stanza->node, GIBBER_XMPP_NS_SASL_AUTH);

  priv->state = GIBBER_SASL_AUTH_STATE_DIGEST_MD5_SENT_FINAL_RESPONSE;

  if (!gibber_xmpp_connection_send(priv->connection, stanza, &send_error)) {
    auth_failed(sasl, GIBBER_SASL_AUTH_ERROR_NETWORK,
        "Failed to send response: %s", send_error->message);
    g_error_free(send_error);
  }
  g_object_unref(stanza);
}

static void
digest_md5_handle_challenge(GibberSaslAuth *sasl, GibberXmppStanza *stanza) 
{
  GibberSaslAuthPrivate *priv = GIBBER_SASL_AUTH_GET_PRIVATE(sasl);
  gchar *challenge;
  gsize len;
  GHashTable *h;

  challenge = (gchar *)g_base64_decode(stanza->node->content, &len);
  DEBUG("Got digest-md5 challenge: %s", challenge);
  h = digest_md5_challenge_to_hash(challenge);
  g_free(challenge);

  if (h == NULL) {
    auth_failed(sasl, GIBBER_SASL_AUTH_ERROR_INVALID_REPLY, 
        "Server send an invalid challenge", 
        stanza->node->name);
    return;
  }

  switch (priv->state) {
    case GIBBER_SASL_AUTH_STATE_DIGEST_MD5_STARTED:
      digest_md5_send_initial_response(sasl, h); 
      break;
    case GIBBER_SASL_AUTH_STATE_DIGEST_MD5_SENT_AUTH_RESPONSE:
      digest_md5_check_server_response(sasl, h); 
      break;
    default:
      auth_failed(sasl, GIBBER_SASL_AUTH_ERROR_INVALID_REPLY, 
          "Server send a challenge at the wrong time"); 
  } 
  g_hash_table_destroy(h);
}

static void
digest_md5_handle_failure(GibberSaslAuth *sasl, GibberXmppStanza *stanza) 
{
  GibberXmppNode *reason = NULL;
  if (stanza->node->children != NULL) {
    /* TODO add a gibber xmpp node utility to either get the first child or
     * iterate the children list */
    reason = (GibberXmppNode *)stanza->node->children->data;
  }
  /* TODO Handle the different error cases in a different way. i.e.
   * make it clear for the user if it's credentials were wrong, if the server
   * just has a temporary error or if the authentication procedure itself was
   * at fault (too weak, invalid mech etc) */
  auth_failed(sasl, GIBBER_SASL_AUTH_ERROR_FAILURE,
      "Authentication failed: %s", 
      reason == NULL ? "Unknown reason" : reason->name);
}

static void
digest_md5_handle_success(GibberSaslAuth *sasl, GibberXmppStanza *stanza) 
{
  GibberSaslAuthPrivate *priv = GIBBER_SASL_AUTH_GET_PRIVATE(sasl);
  if (priv->state != GIBBER_SASL_AUTH_STATE_DIGEST_MD5_SENT_FINAL_RESPONSE) {
      auth_failed(sasl, GIBBER_SASL_AUTH_ERROR_INVALID_REPLY, 
          "Server send success before finishing authentication"); 
      return;
  }
  auth_succeeded(sasl);
}


static void
plain_handle_challenge(GibberSaslAuth *sasl, GibberXmppStanza *stanza) 
{
  auth_failed(sasl, GIBBER_SASL_AUTH_ERROR_INVALID_REPLY, 
      "Server send an unexpected challenge"); 
}

static void
plain_handle_success(GibberSaslAuth *sasl, GibberXmppStanza *stanza) 
{
  GibberSaslAuthPrivate *priv = GIBBER_SASL_AUTH_GET_PRIVATE(sasl);
  if (priv->state != GIBBER_SASL_AUTH_STATE_PLAIN_STARTED) {
      auth_failed(sasl, GIBBER_SASL_AUTH_ERROR_INVALID_REPLY, 
          "Server send success before finishing authentication"); 
      return;
  }
  auth_succeeded(sasl);
}

static void
plain_handle_failure(GibberSaslAuth *sasl, GibberXmppStanza *stanza) 
{
  GibberXmppNode *reason = NULL;
  if (stanza->node->children != NULL) {
    /* TODO add a gibber xmpp node utility to either get the first child or
     * iterate the children list */
    reason = (GibberXmppNode *)stanza->node->children->data;
  }
  /* TODO Handle the different error cases in a different way. i.e.
   * make it clear for the user if it's credentials were wrong, if the server
   * just has a temporary error or if the authentication procedure itself was
   * at fault (too weak, invalid mech etc) */
  auth_failed(sasl, GIBBER_SASL_AUTH_ERROR_FAILURE,
      "Authentication failed: %s", 
      reason == NULL ? "Unknown reason" : reason->name);
}


#define HANDLE(x, y) { #x, y##_handle_##x }          
#define HANDLERS(x) { HANDLE(challenge, x),  \
                      HANDLE(failure, x),     \
                      HANDLE(success, x),    \
                      { NULL, NULL }         \
                    }        
static void
sasl_auth_stanza_received(GibberXmppConnection *connection, 
    GibberXmppStanza *stanza,
    GibberSaslAuth *sasl) 
{
  int i;
  GibberSaslAuthPrivate *priv = GIBBER_SASL_AUTH_GET_PRIVATE (sasl);
  struct { 
    const gchar *name; 
    void (*func)(GibberSaslAuth *sasl, GibberXmppStanza *stanza);
  } handlers[GIBBER_SASL_AUTH_NR_MECHANISMS][4] = { HANDLERS(plain), 
                                                   HANDLERS(digest_md5) };
  if (strcmp(gibber_xmpp_node_get_ns(stanza->node), GIBBER_XMPP_NS_SASL_AUTH)) {
    auth_failed(sasl, GIBBER_SASL_AUTH_ERROR_INVALID_REPLY, 
        "Server send a reply not in the %s namespace", 
        GIBBER_XMPP_NS_SASL_AUTH);
    return;
  }

  for (i = 0 ; handlers[priv->mech][i].name != NULL; i++) {
    if (!strcmp(stanza->node->name, handlers[priv->mech][i].name)) {
      handlers[priv->mech][i].func(sasl, stanza);
      return;
    }
  }

  auth_failed(sasl, GIBBER_SASL_AUTH_ERROR_INVALID_REPLY, 
      "Server send an invalid reply (%s)", 
      stanza->node->name);
}

static gboolean
gibber_sasl_auth_start_mechanism(GibberSaslAuth *sasl, 
    GibberSaslAuthMechanism mech,
    GError **error) 
{
  GibberXmppStanza *stanza;
  GibberSaslAuthPrivate *priv = GIBBER_SASL_AUTH_GET_PRIVATE(sasl);
  GError *send_error = NULL;
  gboolean ret = TRUE;

  priv->mech = mech;

  priv->stanza_signal_id =
      g_signal_connect(priv->connection, "received-stanza", 
          G_CALLBACK(sasl_auth_stanza_received), sasl);

  stanza = gibber_xmpp_stanza_new("auth");
  gibber_xmpp_node_set_ns(stanza->node, GIBBER_XMPP_NS_SASL_AUTH);
  switch (mech) {
    case GIBBER_SASL_AUTH_PLAIN: {
      GString *str = g_string_new("");
      gchar *username = NULL, *password = NULL;

      g_signal_emit(sasl, signals[USERNAME_REQUESTED], 0, &username);
      g_signal_emit(sasl, signals[PASSWORD_REQUESTED], 0, &password);

      if (username == NULL || password == NULL) {
        auth_failed(sasl, GIBBER_SASL_AUTH_ERROR_NO_CREDENTIALS,
                   "No username or password provided");
        goto out;
      }
      DEBUG("Got username and password");
      gchar *cstr;
      g_string_append_c(str, '\0');
      g_string_append(str, username);
      g_string_append_c(str, '\0');
      g_string_append(str, password);
      cstr = g_base64_encode((guchar *)str->str, str->len);

      gibber_xmpp_node_set_attribute(stanza->node, "mechanism", "PLAIN");
      gibber_xmpp_node_set_content(stanza->node, cstr);

      g_string_free(str, TRUE);
      g_free(cstr);

      priv->state = GIBBER_SASL_AUTH_STATE_PLAIN_STARTED;
      break;
    }
    case GIBBER_SASL_AUTH_DIGEST_MD5:
      gibber_xmpp_node_set_attribute(stanza->node, "mechanism", "DIGEST-MD5");
      priv->state = GIBBER_SASL_AUTH_STATE_DIGEST_MD5_STARTED;
      break;
    default:
      g_assert_not_reached();
  }

  if (!gibber_xmpp_connection_send(priv->connection, stanza, &send_error)) {
    g_set_error(error, 
        GIBBER_SASL_AUTH_ERROR,
        GIBBER_SASL_AUTH_ERROR_NETWORK,
        "%s", send_error->message);
    g_error_free(send_error);
    ret = FALSE;
  }

out:
  g_object_unref(stanza);

  return ret;
}

/* Initiate sasl auth. features should containt the stream features stanza as
 * receiver from the server */ 
gboolean 
gibber_sasl_auth_authenticate(GibberSaslAuth *sasl,
                              const gchar *server,
                              GibberXmppConnection *connection,
                              GibberXmppStanza *features,
                              gboolean allow_plain,
                              GError **error) {
  GibberSaslAuthPrivate *priv = GIBBER_SASL_AUTH_GET_PRIVATE(sasl);
  GibberXmppNode *mech_node;
  GSList *mechanisms, *t;
  gboolean ret = TRUE;

  g_assert(sasl != NULL);
  g_assert(server != NULL);
  g_assert(connection != NULL);
  g_assert(features != NULL);

  mech_node = gibber_xmpp_node_get_child_ns(features->node,  
                                             "mechanisms",
                                             GIBBER_XMPP_NS_SASL_AUTH);
  mechanisms = gibber_sasl_auth_mechanisms_to_list(mech_node);

  if (mechanisms == NULL) {
    g_set_error(error, GIBBER_SASL_AUTH_ERROR, 
        GIBBER_SASL_AUTH_ERROR_SASL_NOT_SUPPORTED,
        "Server doesn't have any sasl mechanisms");
    goto error;
  }

  priv->connection = g_object_ref(connection);
  priv->server = g_strdup(server);

  if (gibber_sasl_auth_has_mechanism(mechanisms, "DIGEST-MD5")) {
    DEBUG("Choosing DIGEST-MD5 as auth mechanism");
    ret = gibber_sasl_auth_start_mechanism(sasl, 
              GIBBER_SASL_AUTH_DIGEST_MD5, error);
  } else if (allow_plain && 
      gibber_sasl_auth_has_mechanism(mechanisms, "PLAIN")) {
    DEBUG("Choosing PLAIN as auth mechanism");
    ret = gibber_sasl_auth_start_mechanism(sasl, GIBBER_SASL_AUTH_PLAIN, error);
  } else {
    g_set_error(error, 
        GIBBER_SASL_AUTH_ERROR,
        GIBBER_SASL_AUTH_ERROR_NO_SUPPORTED_MECHANISMS,
        "No supported mechanisms found") ;
    goto error;
  }

out:
  for (t = mechanisms ; t != NULL; t = g_slist_next(t)) {
    g_free(t->data);
  }
  g_slist_free(mechanisms);
  return ret;

error:
  auth_reset(sasl);
  ret = FALSE;
  goto out;
}

