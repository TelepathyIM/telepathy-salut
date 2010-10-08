/*
 * disco.c - Source for Salut service discovery
 *
 * Copyright (C) 2006-2008 Collabora Ltd.
 * Copyright (C) 2006-2008 Nokia Corporation
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
 *
 * -- LET'S DISCO!!!  \o/ \o_ _o/ /\o/\ _/o/- -\o\_ --
 */

#include "config.h"
#include "salut-disco.h"

#include <string.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <telepathy-glib/dbus.h>
#include <gibber/gibber-iq-helper.h>
#include <gibber/gibber-namespaces.h>

#define DEBUG_FLAG DEBUG_DISCO

#include "debug.h"
#include "salut-capabilities.h"
#include "salut-caps-hash.h"
#include "salut-connection.h"
#include "salut-xmpp-connection-manager.h"
#include "salut-signals-marshal.h"

#define DEFAULT_REQUEST_TIMEOUT 20000

/* signals */
enum
{
  ITEM_FOUND,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* Properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_XMPP_CONNECTION_MANAGER,
  LAST_PROPERTY
};

G_DEFINE_TYPE(SalutDisco, salut_disco, G_TYPE_OBJECT);

struct _SalutDiscoPrivate
{
  SalutConnection *connection;
  SalutXmppConnectionManager *xmpp_connection_manager;

  /* list of SalutDiscoRequest* */
  GList *requests;
  gboolean dispose_has_run;
};

struct _SalutDiscoRequest
{
  SalutDisco *disco;

  /* The request cannot be sent immediately, we have to wait the
   * XmppConnection to be established. Meanwhile, requested=FALSE. */
  gboolean requested;

  GibberIqHelper *iq_helper;
  guint timer_id;
  SalutDiscoType type;
  SalutContact *contact;
  GibberXmppConnection *conn;

  /* uri as in XEP-0115 */
  gchar *node;

  SalutDiscoCb callback;
  gpointer user_data;
  GObject *bound_object;
};

GQuark
salut_disco_error_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("salut-disco-error");
  return quark;
}

static void
salut_disco_init (SalutDisco *obj)
{
  SalutDiscoPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, SALUT_TYPE_DISCO, SalutDiscoPrivate);
  obj->priv = priv;
}

static GObject *salut_disco_constructor (GType type, guint n_props,
    GObjectConstructParam *props);
static void salut_disco_set_property (GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void salut_disco_get_property (GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);
static void salut_disco_dispose (GObject *object);
static void salut_disco_finalize (GObject *object);

static const char *
disco_type_to_xmlns (SalutDiscoType type)
{
  switch (type) {
    case SALUT_DISCO_TYPE_INFO:
      return NS_DISCO_INFO;
    case SALUT_DISCO_TYPE_ITEMS:
      return NS_DISCO_ITEMS;
    default:
      g_assert_not_reached ();
  }

  return NULL;
}

static void notify_delete_request (gpointer data, GObject *obj);

static void
delete_request (SalutDiscoRequest *request)
{
  SalutDisco *disco = request->disco;
  SalutDiscoPrivate *priv;

  g_assert (NULL != request);
  g_assert (SALUT_IS_DISCO (disco));

  priv = disco->priv;

  g_assert (NULL != g_list_find (priv->requests, request));

  priv->requests = g_list_remove (priv->requests, request);

  if (NULL != request->bound_object)
    {
      g_object_weak_unref (request->bound_object, notify_delete_request,
          request);
    }

  if (0 != request->timer_id)
    {
      g_source_remove (request->timer_id);
    }

  if (request->conn != NULL)
    {
      salut_xmpp_connection_manager_release_connection
        (priv->xmpp_connection_manager, request->conn);
    }
  if (request->iq_helper != NULL)
    g_object_unref (request->iq_helper);

  g_object_unref (request->contact);
  g_free (request->node);
  g_slice_free (SalutDiscoRequest, request);
}

static void
notify_delete_request (gpointer data, GObject *obj)
{
  SalutDiscoRequest *request = (SalutDiscoRequest *) data;
  request->bound_object = NULL;
  delete_request (request);
}

static void
request_reply_cb (GibberIqHelper *helper,
                  GibberXmppStanza *sent_stanza,
                  GibberXmppStanza *reply_stanza,
                  GObject *object,
                  gpointer user_data)
{
  SalutDiscoRequest *request = (SalutDiscoRequest *) user_data;
  SalutDisco *disco = SALUT_DISCO (object);
  SalutDiscoPrivate *priv = disco->priv;
  WockyNode *reply_node = wocky_stanza_get_top_node (reply_stanza);
  GibberXmppNode *query_node;
  GError *err = NULL;
  GibberStanzaSubType sub_type;

  g_assert (request);

  if (!g_list_find (priv->requests, request))
    return;

  query_node = gibber_xmpp_node_get_child_ns (reply_node,
      "query", disco_type_to_xmlns (request->type));

  gibber_xmpp_stanza_get_type_info (reply_stanza, NULL, &sub_type);

  if (sub_type == GIBBER_STANZA_SUB_TYPE_ERROR)
    {
      err = gibber_message_get_xmpp_error (reply_stanza);

      if (err == NULL)
        {
          err = g_error_new (SALUT_DISCO_ERROR,
                             SALUT_DISCO_ERROR_UNKNOWN,
                             "an unknown error occurred");
        }
    }
  else if (NULL == query_node)
    {
      err = g_error_new (SALUT_DISCO_ERROR, SALUT_DISCO_ERROR_UNKNOWN,
          "disco response contained no <query> node");
    }

  request->callback (request->disco, request, request->contact, request->node,
                     query_node, err, request->user_data);
  delete_request (request);

  if (err)
    g_error_free (err);
}

static void
send_disco_request (SalutDisco *self,
                    GibberXmppConnection *conn,
                    SalutContact *contact,
                    SalutDiscoRequest *request)
{
  SalutDiscoPrivate *priv = self->priv;
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->connection);
  GibberXmppStanza *stanza;
  TpHandleRepoIface *contact_repo;
  const gchar *jid_from, *jid_to;
  GError *error = NULL;

  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->connection, TP_HANDLE_TYPE_CONTACT);

  jid_from = tp_handle_inspect (contact_repo, base_conn->self_handle);
  jid_to = tp_handle_inspect (contact_repo, contact->handle);

  stanza = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_IQ,
      GIBBER_STANZA_SUB_TYPE_SET,
      jid_from, jid_to,
      GIBBER_NODE, "query",
        GIBBER_NODE_XMLNS, disco_type_to_xmlns (request->type),
        GIBBER_NODE_ATTRIBUTE, "node", request->node,
      GIBBER_NODE_END,
      GIBBER_STANZA_END);

  request->requested = TRUE;

  request->iq_helper = gibber_iq_helper_new (conn);
  g_assert (request->iq_helper);

  if (!gibber_iq_helper_send_with_reply (request->iq_helper, stanza,
      request_reply_cb, G_OBJECT(self), request, &error))
    {
      DEBUG ("Failed to send caps request: '%s'", error->message);
      g_error_free (error);
    }

  g_object_unref (stanza);
}

static void
xmpp_connection_manager_new_connection_cb (SalutXmppConnectionManager *mgr,
                                           GibberXmppConnection *conn,
                                           SalutContact *contact,
                                           gpointer user_data)
{
  SalutDisco *self = SALUT_DISCO (user_data);
  SalutDiscoPrivate *priv = self->priv;
  GList *req = priv->requests;

  /* send all pending requests on this connection */
  while (req != NULL)
    {
      SalutDiscoRequest *request = req->data;

      if (request->contact == contact && !request->requested)
        {
          request->conn = conn;
          salut_xmpp_connection_manager_take_connection
            (priv->xmpp_connection_manager, request->conn);
          send_disco_request (self, conn, contact, request);
        }
      req = g_list_next (req);
    }
}

static void
salut_disco_class_init (SalutDiscoClass *salut_disco_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_disco_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_disco_class, sizeof (SalutDiscoPrivate));

  object_class->constructor = salut_disco_constructor;

  object_class->get_property = salut_disco_get_property;
  object_class->set_property = salut_disco_set_property;

  object_class->dispose = salut_disco_dispose;
  object_class->finalize = salut_disco_finalize;

  param_spec = g_param_spec_object ("connection", "SalutConnection object",
                                    "Salut connection object that owns this "
                                    "XMPP Discovery object.",
                                    SALUT_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_object (
      "xmpp-connection-manager",
      "SalutXmppConnectionManager object",
      "Salut XMPP Connection manager used for disco to send caps requests",
      SALUT_TYPE_XMPP_CONNECTION_MANAGER,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_XMPP_CONNECTION_MANAGER,
      param_spec);

  signals[ITEM_FOUND] =
    g_signal_new ("item-found",
                  G_OBJECT_CLASS_TYPE (salut_disco_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_signals_marshal_VOID__POINTER,
                  G_TYPE_NONE, 1, G_TYPE_POINTER);
}

static void
salut_disco_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  SalutDisco *self = SALUT_DISCO (object);
  SalutDiscoPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->connection);
        break;
      case PROP_XMPP_CONNECTION_MANAGER:
        g_value_set_object (value, priv->xmpp_connection_manager);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_disco_set_property (GObject     *object,
                           guint        property_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  SalutDisco *self = SALUT_DISCO (object);
  SalutDiscoPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->connection = g_value_get_object (value);
        break;
      case PROP_XMPP_CONNECTION_MANAGER:
        priv->xmpp_connection_manager = g_value_get_object (value);
        g_object_ref (priv->xmpp_connection_manager);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static gboolean
caps_req_stanza_filter (SalutXmppConnectionManager *mgr,
                        GibberXmppConnection *conn,
                        GibberXmppStanza *stanza,
                        SalutContact *contact,
                        gpointer user_data)
{
  GibberStanzaSubType sub_type;
  GibberXmppNode *query;

  gibber_xmpp_stanza_get_type_info (stanza, NULL, &sub_type);

  if (sub_type != GIBBER_STANZA_SUB_TYPE_GET)
    return FALSE;

  query = wocky_node_get_child_ns (wocky_stanza_get_top_node (stanza), "query",
      NS_DISCO_INFO);

  if (!query)
    return FALSE;

  return TRUE;
}

static void
send_item_not_found (GibberXmppConnection *conn,
                     const gchar *node,
                     const gchar *from,
                     const gchar *to)
{
  GibberXmppStanza *result;

  /* Return <item-not-found>. It is possible that the remote contact
   * requested an old version (old hash) of our capabilities. In the
   * meantime, it will have gotten a new hash, and query the new hash
   * anyway. */
  result = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_IQ,
      GIBBER_STANZA_SUB_TYPE_ERROR,
      from, to,
      GIBBER_NODE, "query",
        GIBBER_NODE_XMLNS, NS_DISCO_INFO,
        GIBBER_NODE_ATTRIBUTE, "node", node,
        GIBBER_NODE, "error",
          GIBBER_NODE_ATTRIBUTE, "type", "cancel",
          GIBBER_NODE, "item-not-found",
            GIBBER_NODE_XMLNS, GIBBER_XMPP_NS_STANZAS,
          GIBBER_NODE_END,
        GIBBER_NODE_END,
      GIBBER_NODE_END,
      GIBBER_STANZA_END);

  DEBUG ("sending item-not-found as disco response");

  if (!gibber_xmpp_connection_send (conn, result, NULL))
    {
      DEBUG ("sending item-not-found failed");
    }

  g_object_unref (result);
}

static void
caps_req_stanza_callback (SalutXmppConnectionManager *mgr,
                          GibberXmppConnection *conn,
                          GibberXmppStanza *stanza,
                          SalutContact *contact,
                          gpointer user_data)
{
  SalutDisco *self = SALUT_DISCO (user_data);
  SalutDiscoPrivate *priv = self->priv;
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->connection);
  GibberXmppNode *iq, *result_iq, *query, *result_query;
  const gchar *node;
  const gchar *suffix;
  GSList *i;
  TpHandleRepoIface *contact_repo;
  const gchar *jid_from, *jid_to;
  SalutSelf *salut_self;
  GibberXmppStanza *result;
  GSList *features;

  contact_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);
  jid_from = tp_handle_inspect (contact_repo, base_conn->self_handle);
  jid_to = tp_handle_inspect (contact_repo, contact->handle);

  iq = wocky_stanza_get_top_node (stanza);
  query = gibber_xmpp_node_get_child_ns (iq, "query", NS_DISCO_INFO);
  g_assert (query != NULL);

  node = gibber_xmpp_node_get_attribute (query, "node");
  if (node == NULL)
    {
      send_item_not_found (conn, "", jid_from, jid_to);
      return;
    }

  if (!g_str_has_prefix (node, GIBBER_TELEPATHY_NS_CAPS "#"))
    {
      send_item_not_found (conn, node, jid_from, jid_to);
      return;
    }
  else
    {
      suffix = node + strlen (GIBBER_TELEPATHY_NS_CAPS) + 1;
    }

  DEBUG ("got disco request for node %s", node);

  g_object_get (priv->connection, "self", &salut_self, NULL);
  /* Salut only supports XEP-0115 version 1.5. Bundles from old version 1.3 are
   * not implemented. */

  if (tp_strdiff (suffix, salut_self->ver))
    {
      g_object_unref (salut_self);
      return;
    }

  features = salut_self_get_features (salut_self);
  g_object_unref (salut_self);

  /* Every entity MUST have at least one identity (XEP-0030). Salut publishs
   * one identity. If you change the identity here, you also need to change
   * caps_hash_compute_from_self_presence(). */
  result = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_IQ,
      GIBBER_STANZA_SUB_TYPE_RESULT,
      jid_from, jid_to,
      GIBBER_NODE, "query",
        GIBBER_NODE_XMLNS, NS_DISCO_INFO,
        GIBBER_NODE_ATTRIBUTE, "node", node,
        GIBBER_NODE, "identity",
          GIBBER_NODE_ATTRIBUTE, "category", "client",
          GIBBER_NODE_ATTRIBUTE, "name", PACKAGE_STRING,
          /* FIXME: maybe we should add a connection property allowing to
           * set the type attribute instead of hardcoding "pc". */
          GIBBER_NODE_ATTRIBUTE, "type", "pc",
        GIBBER_NODE_END,
      GIBBER_NODE_END,
      GIBBER_STANZA_END);

  result_iq = wocky_stanza_get_top_node (result);
  result_query = gibber_xmpp_node_get_child_ns (result_iq, "query", NULL);

  for (i = features; NULL != i; i = i->next)
    {
      const Feature *feature = (const Feature *) i->data;
      GibberXmppNode *feature_node;

      feature_node = gibber_xmpp_node_add_child (result_query, "feature");
      gibber_xmpp_node_set_attribute (feature_node, "var", feature->ns);
    }
  g_slist_free (features);

  DEBUG ("sending disco response");

  if (!gibber_xmpp_connection_send (conn, result, NULL))
    {
      DEBUG ("sending disco response failed");
    }

  g_object_unref (result);
}

static GObject *
salut_disco_constructor (GType type, guint n_props,
                          GObjectConstructParam *props)
{
  GObject *obj;
  SalutDisco *disco;
  SalutDiscoPrivate *priv;

  obj = G_OBJECT_CLASS (salut_disco_parent_class)-> constructor (type,
      n_props, props);
  disco = SALUT_DISCO (obj);
  priv = disco->priv;

  g_signal_connect (priv->xmpp_connection_manager, "new-connection",
      G_CALLBACK (xmpp_connection_manager_new_connection_cb), obj);

  /* receive discovery requests */
  salut_xmpp_connection_manager_add_stanza_filter (
      priv->xmpp_connection_manager, NULL,
      caps_req_stanza_filter, caps_req_stanza_callback, obj);

  return obj;
}

static void cancel_request (SalutDiscoRequest *request);

static void
salut_disco_dispose (GObject *object)
{
  SalutDisco *self = SALUT_DISCO (object);
  SalutDiscoPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  DEBUG ("dispose called");

  salut_xmpp_connection_manager_remove_stanza_filter (
      priv->xmpp_connection_manager, NULL,
      caps_req_stanza_filter, caps_req_stanza_callback, self);

  /* cancel request removes the element from the list after cancelling */
  while (priv->requests)
    cancel_request (priv->requests->data);

  if (priv->xmpp_connection_manager != NULL)
    {
      g_signal_handlers_disconnect_matched (priv->xmpp_connection_manager,
          G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);

      g_object_unref (priv->xmpp_connection_manager);
      priv->xmpp_connection_manager = NULL;
    }

  if (G_OBJECT_CLASS (salut_disco_parent_class)->dispose)
    G_OBJECT_CLASS (salut_disco_parent_class)->dispose (object);
}

static void
salut_disco_finalize (GObject *object)
{
  DEBUG ("called with %p", object);

  G_OBJECT_CLASS (salut_disco_parent_class)->finalize (object);
}

/**
 * salut_disco_new:
 * @conn: The #SalutConnection to use for service discovery
 *
 * Creates an object to use for Jabber service discovery (DISCO)
 * There should be one of these per connection
 */
SalutDisco *
salut_disco_new (SalutConnection *connection,
                 SalutXmppConnectionManager *xmpp_connection_manager)
{
  SalutDisco *disco;

  g_return_val_if_fail (SALUT_IS_CONNECTION (connection), NULL);

  disco = SALUT_DISCO (g_object_new (SALUT_TYPE_DISCO,
        "connection", connection,
        "xmpp-connection-manager", xmpp_connection_manager,
        NULL));

  return disco;
}

static void
cancel_request (SalutDiscoRequest *request)
{
  GError *err /* doesn't need initializing */;

  g_assert (request != NULL);

  err = g_error_new (SALUT_DISCO_ERROR, SALUT_DISCO_ERROR_CANCELLED,
      "Request for %s on %s cancelled",
      (request->type == SALUT_DISCO_TYPE_INFO)?"info":"items",
      request->contact->name);
  (request->callback)(request->disco, request, request->contact, request->node,
                      NULL, err, request->user_data);
  g_error_free (err);

  delete_request (request);
}

/**
 * salut_disco_request:
 * @self: #SalutDisco object to use for request
 * @type: type of request
 * @jid: Jabber ID to request on
 * @node: node to request on @jid, or NULL
 * @callback: #SalutDiscoCb to call on request fullfilment
 * @object: GObject to bind request to. the callback will not be
 *          called if this object has been unrefed. NULL if not needed
 * @error: #GError to return a telepathy error in if unable to make
 *         request, NULL if unneeded.
 *
 * Make a disco request on the given jid, which will fail unless a reply
 * is received within the given timeout interval.
 */
SalutDiscoRequest *
salut_disco_request (SalutDisco *self, SalutDiscoType type,
                     SalutContact *contact, const char *node,
                     SalutDiscoCb callback,
                     gpointer user_data, GObject *object,
                     GError **error)
{
  SalutDiscoPrivate *priv = self->priv;
  SalutDiscoRequest *request;
  SalutXmppConnectionManagerRequestConnectionResult result;
  GibberXmppConnection *conn = NULL;

  g_assert (node != NULL);
  g_assert (strlen (node) > 0);

  request = g_slice_new0 (SalutDiscoRequest);
  request->disco = self;
  request->requested = FALSE;
  request->type = type;
  request->contact = g_object_ref (contact);
  if (node)
    request->node = g_strdup (node);
  request->callback = callback;
  request->user_data = user_data;
  request->bound_object = object;
  request->conn = NULL;
  request->iq_helper = NULL;

  if (NULL != object)
    g_object_weak_ref (object, notify_delete_request, request);

  DEBUG ("Creating disco request %p for %s",
           request, request->contact->name);

  priv->requests = g_list_prepend (priv->requests, request);

  result = salut_xmpp_connection_manager_request_connection (
      priv->xmpp_connection_manager, contact, &conn, NULL);

  if (result == SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_DONE)
    {
      DEBUG ("connection done.");
      request->conn = conn;
      send_disco_request (self, conn, contact, request);
      return request;
    }
  else if (result ==
      SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_PENDING)
    {
      DEBUG ("Requested connection pending");
      return request;
    }
  else
    {
      delete_request (request);
      return NULL;
    }
}

void
salut_disco_cancel_request (SalutDisco *disco, SalutDiscoRequest *request)
{
  SalutDiscoPrivate *priv;

  g_return_if_fail (SALUT_IS_DISCO (disco));
  g_return_if_fail (NULL != request);

  priv = disco->priv;

  g_return_if_fail (NULL != g_list_find (priv->requests, request));

  cancel_request (request);
}

