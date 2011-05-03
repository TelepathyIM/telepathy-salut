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
#include "disco.h"

#include <string.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <telepathy-glib/dbus.h>
#include <gibber/gibber-namespaces.h>
#include <wocky/wocky-data-form.h>
#include <wocky/wocky-xep-0115-capabilities.h>

#define DEBUG_FLAG DEBUG_DISCO

#include "debug.h"
#include "capabilities.h"
#include "caps-hash.h"
#include "connection.h"

/* Properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

G_DEFINE_TYPE(SalutDisco, salut_disco, G_TYPE_OBJECT);

struct _SalutDiscoPrivate
{
  SalutConnection *connection;

  guint caps_req_stanza_id;
  guint caps_req_stanza_id_broken;

  GList *requests;

  gboolean dispose_has_run;
};

struct _SalutDiscoRequest
{
  SalutDisco *disco;

  SalutDiscoType type;
  SalutContact *contact;

  GCancellable *cancellable;

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
    quark = g_quark_from_static_string ("disco-error");
  return quark;
}

static void
salut_disco_init (SalutDisco *obj)
{
  SalutDiscoPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, SALUT_TYPE_DISCO, SalutDiscoPrivate);
  obj->priv = priv;
}

static void salut_disco_constructed (GObject *obj);
static void salut_disco_set_property (GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void salut_disco_get_property (GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);
static void salut_disco_dispose (GObject *object);

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

  /* if we've already disposed the SalutDisco object, we should not
   * mess around with anything referenced by that. */
  if (disco != NULL)
    {
      g_assert (SALUT_IS_DISCO (disco));

      priv = disco->priv;

      g_assert (NULL != g_list_find (priv->requests, request));

      priv->requests = g_list_remove (priv->requests, request);
    }

  if (NULL != request->bound_object)
    {
      g_object_weak_unref (request->bound_object, notify_delete_request,
          request);
    }

  g_object_unref (request->contact);
  g_object_unref (request->cancellable);
  g_free (request->node);
  g_slice_free (SalutDiscoRequest, request);
}

static void
notify_delete_request (gpointer data, GObject *obj)
{
  SalutDiscoRequest *request = (SalutDiscoRequest *) data;
  request->bound_object = NULL;

  /* This will cause the callback to be called with the cancelled
     error. */
  g_cancellable_cancel (request->cancellable);
}

static void
salut_disco_class_init (SalutDiscoClass *salut_disco_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_disco_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_disco_class, sizeof (SalutDiscoPrivate));

  object_class->constructed = salut_disco_constructed;

  object_class->get_property = salut_disco_get_property;
  object_class->set_property = salut_disco_set_property;

  object_class->dispose = salut_disco_dispose;

  param_spec = g_param_spec_object ("connection", "SalutConnection object",
                                    "Salut connection object that owns this "
                                    "XMPP Discovery object.",
                                    SALUT_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);
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
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
send_item_not_found (WockyPorter *porter,
    WockyStanza *iq,
    const gchar *node)
{
  WockyStanza *result;

  /* Return <item-not-found>. It is possible that the remote contact
   * requested an old version (old hash) of our capabilities. In the
   * meantime, it will have gotten a new hash, and query the new hash
   * anyway. */
  result = wocky_stanza_build_iq_error (iq,
      '(', "query",
        ':', NS_DISCO_INFO,
        '@', "node", node,
        '(', "error",
          '@', "type", "cancel",
          '(', "item-not-found",
            ':', GIBBER_XMPP_NS_STANZAS,
          ')',
        ')',
      ')',
      NULL);

  DEBUG ("sending item-not-found as disco response");

  wocky_porter_send_async (porter, result, NULL, NULL, NULL);

  g_object_unref (result);
}

static void
add_feature_foreach (gpointer ns,
    gpointer result_query)
{
  WockyNode *feature_node;

  feature_node = wocky_node_add_child (result_query, "feature");
  wocky_node_set_attribute (feature_node, "var", ns);
}

static void
add_data_form_foreach (gpointer data,
    gpointer user_data)
{
  WockyDataForm *form = data;
  WockyNode *query = user_data;

  wocky_data_form_add_to_node (form, query);
}

static gboolean
caps_req_stanza_callback (WockyPorter *porter,
    WockyStanza *stanza,
    gpointer user_data)
{
  SalutDisco *self = SALUT_DISCO (user_data);
  SalutDiscoPrivate *priv = self->priv;
  WockyNode *iq, *result_iq, *query, *result_query;
  const gchar *node;
  const gchar *suffix;
  SalutSelf *salut_self;
  WockyStanza *result;
  const GabbleCapabilitySet *caps;
  const GPtrArray *data_forms;

  iq = wocky_stanza_get_top_node (stanza);
  query = wocky_node_get_child_ns (iq, "query", NS_DISCO_INFO);
  g_assert (query != NULL);

  node = wocky_node_get_attribute (query, "node");
  if (node == NULL)
    {
      send_item_not_found (porter, stanza, "");
      return TRUE;
    }

  if (!g_str_has_prefix (node, GIBBER_TELEPATHY_NS_CAPS "#"))
    {
      send_item_not_found (porter, stanza, node);
      return TRUE;
    }
  else
    {
      suffix = node + strlen (GIBBER_TELEPATHY_NS_CAPS) + 1;
    }

  DEBUG ("got disco request for node %s", node);

  g_object_get (priv->connection, "self", &salut_self, NULL);
  /* Salut only supports XEP-0115 version 1.5. Bundles from old version 1.3 are
   * not implemented. */

  if (salut_self == NULL)
    return TRUE;

  if (tp_strdiff (suffix, salut_self->ver))
    {
      g_object_unref (salut_self);
      return TRUE;
    }

  /* Every entity MUST have at least one identity (XEP-0030). Salut publishs
   * one identity. If you change the identity here, you also need to change
   * caps_hash_compute_from_self_presence(). */
  result = wocky_stanza_build_iq_result (stanza,
      '(', "query",
        ':', NS_DISCO_INFO,
        '@', "node", node,
        '(', "identity",
          '@', "category", "client",
          '@', "name", PACKAGE_STRING,
          /* FIXME: maybe we should add a connection property allowing to
           * set the type attribute instead of hardcoding "pc". */
          '@', "type", "pc",
        ')',
      ')',
      NULL);

  result_iq = wocky_stanza_get_top_node (result);
  result_query = wocky_node_get_child_ns (result_iq, "query", NULL);

  caps = salut_self_get_caps (salut_self);
  gabble_capability_set_foreach (caps, add_feature_foreach, result_query);

  data_forms = wocky_xep_0115_capabilities_get_data_forms (
      WOCKY_XEP_0115_CAPABILITIES (salut_self));
  g_ptr_array_foreach ((GPtrArray *) data_forms, add_data_form_foreach,
      result_query);

  DEBUG ("sending disco response");

  wocky_porter_send_async (porter, result, NULL, NULL, NULL);

  g_object_unref (result);
  g_object_unref (salut_self);

  return TRUE;
}

static void
salut_disco_constructed (GObject *obj)
{
  SalutDisco *disco = SALUT_DISCO (obj);
  SalutDiscoPrivate *priv = disco->priv;
  WockyPorter *porter = priv->connection->porter;

  if (G_OBJECT_CLASS (salut_disco_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (salut_disco_parent_class)->constructed (obj);

  priv->requests = NULL;

  /* receive discovery requests */
  priv->caps_req_stanza_id = wocky_porter_register_handler_from_anyone (porter,
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_GET,
      WOCKY_PORTER_HANDLER_PRIORITY_NORMAL,
      caps_req_stanza_callback, obj,
      '(', "query",
        ':', NS_DISCO_INFO,
      ')', NULL);

  /* Salut used to send disco requests with <iq type='set' ...> so we
   * should listen for that too. */
  priv->caps_req_stanza_id_broken =
    wocky_porter_register_handler_from_anyone (porter,
        WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
        WOCKY_PORTER_HANDLER_PRIORITY_NORMAL,
        caps_req_stanza_callback, obj,
        '(', "query",
          ':', NS_DISCO_INFO,
        ')', NULL);
}

static void
salut_disco_dispose (GObject *object)
{
  SalutDisco *self = SALUT_DISCO (object);
  SalutDiscoPrivate *priv = self->priv;
  WockyPorter *porter = priv->connection->porter;
  GList *l;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  DEBUG ("dispose called");

  wocky_porter_unregister_handler (porter, priv->caps_req_stanza_id);
  priv->caps_req_stanza_id = 0;

  wocky_porter_unregister_handler (porter,
      priv->caps_req_stanza_id_broken);
  priv->caps_req_stanza_id_broken = 0;

  for (l = priv->requests; l != NULL; l = l->next)
    {
      SalutDiscoRequest *r = l->data;

      r->disco = NULL;
      g_cancellable_cancel (r->cancellable);
    }

  g_list_free (priv->requests);

  if (G_OBJECT_CLASS (salut_disco_parent_class)->dispose)
    G_OBJECT_CLASS (salut_disco_parent_class)->dispose (object);
}

/**
 * salut_disco_new:
 * @conn: The #SalutConnection to use for service discovery
 *
 * Creates an object to use for Jabber service discovery (DISCO)
 * There should be one of these per connection
 */
SalutDisco *
salut_disco_new (SalutConnection *connection)
{
  SalutDisco *disco;

  g_return_val_if_fail (SALUT_IS_CONNECTION (connection), NULL);

  disco = SALUT_DISCO (g_object_new (SALUT_TYPE_DISCO,
        "connection", connection,
        NULL));

  return disco;
}

static void
disco_request_sent_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  WockyPorter *porter = WOCKY_PORTER (source_object);
  GError *error = NULL;
  WockyStanza *reply;
  WockyNode *reply_node, *query_node = NULL;
  SalutDiscoRequest *request = user_data;

  reply = wocky_porter_send_iq_finish (porter, result, &error);

  if (reply == NULL)
    {
      DEBUG ("error: %s", error->message);
      goto out;
    }

  if (wocky_stanza_extract_errors (reply, NULL, &error, NULL, NULL))
    goto out;

  reply_node = wocky_stanza_get_top_node (reply);
  query_node = wocky_node_get_child_ns (reply_node, "query",
      disco_type_to_xmlns (request->type));

  if (query_node == NULL)
    {
      error = g_error_new (SALUT_DISCO_ERROR, SALUT_DISCO_ERROR_UNKNOWN,
          "disco response contained no <query> node");
      goto out;
    }

out:
  /* the cancellable is cancelled if the object given to
   * salut_disco_request is disposed, which claims to not call the
   * callback, so let's not. */
  if (!g_cancellable_is_cancelled (request->cancellable))
    {
      request->callback (request->disco, request, request->contact, request->node,
          query_node, error, request->user_data);
    }

  delete_request (request);

  if (error != NULL)
    g_clear_error (&error);
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
 * Make a disco request on the given jid.
 */
SalutDiscoRequest *
salut_disco_request (SalutDisco *self,
    SalutDiscoType type,
    SalutContact *contact,
    const char *node,
    SalutDiscoCb callback,
    gpointer user_data,
    GObject *object,
    GError **error)
{
  SalutDiscoPrivate *priv = self->priv;
  SalutDiscoRequest *request;
  WockyPorter *porter = priv->connection->porter;
  WockyStanza *stanza;

  g_assert (node != NULL);
  g_assert (strlen (node) > 0);

  request = g_slice_new0 (SalutDiscoRequest);
  request->disco = self;
  request->type = type;
  request->contact = g_object_ref (contact);
  if (node)
    request->node = g_strdup (node);
  request->callback = callback;
  request->user_data = user_data;
  request->bound_object = object;
  request->cancellable = g_cancellable_new ();

  if (NULL != object)
    g_object_weak_ref (object, notify_delete_request, request);

  DEBUG ("Creating disco request %p for %s",
           request, request->contact->name);

  stanza = wocky_stanza_build_to_contact (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_GET,
      NULL, WOCKY_CONTACT (contact),
      '(', "query",
        ':', disco_type_to_xmlns (request->type),
        '@', "node", request->node,
      ')',
      NULL);

  wocky_porter_send_iq_async (porter, stanza, request->cancellable,
      disco_request_sent_cb, request);

  priv->requests = g_list_append (priv->requests,
      request);

  g_object_unref (stanza);

  return request;
}

void
salut_disco_cancel_request (SalutDisco *disco,
    SalutDiscoRequest *request)
{
  SalutDiscoPrivate *priv;

  g_return_if_fail (SALUT_IS_DISCO (disco));
  g_return_if_fail (NULL != request);

  priv = disco->priv;

  g_return_if_fail (NULL != g_list_find (priv->requests, request));

  g_cancellable_cancel (request->cancellable);
}
