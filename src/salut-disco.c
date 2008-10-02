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

#define DBUS_API_SUBJECT_TO_CHANGE

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <telepathy-glib/dbus.h>
#include <gibber/gibber-namespaces.h>

#define DEBUG_FLAG DEBUG_DISCO

#include "debug.h"
#include "salut-connection.h"
#include "signals-marshal.h"

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
  LAST_PROPERTY
};

G_DEFINE_TYPE(SalutDisco, salut_disco, G_TYPE_OBJECT);

struct _SalutDiscoPrivate
{
  SalutConnection *connection;
  GList *requests;
  gboolean dispose_has_run;
};

struct _SalutDiscoRequest
{
  SalutDisco *disco;
  guint timer_id;

  SalutDiscoType type;
  SalutContact *contact;
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

#define SALUT_DISCO_GET_PRIVATE(o) ((o)->priv)

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
  SalutDisco *chan = SALUT_DISCO (object);
  SalutDiscoPrivate *priv = SALUT_DISCO_GET_PRIVATE (chan);

  switch (property_id) {
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
  SalutDisco *chan = SALUT_DISCO (object);
  SalutDiscoPrivate *priv = SALUT_DISCO_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->connection = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
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
  priv = SALUT_DISCO_GET_PRIVATE (disco);

  return obj;
}

static void cancel_request (SalutDiscoRequest *request);

static void
salut_disco_dispose (GObject *object)
{
  SalutDisco *self = SALUT_DISCO (object);
  SalutDiscoPrivate *priv = SALUT_DISCO_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  DEBUG ("dispose called");

  /* cancel request removes the element from the list after cancelling */
  while (priv->requests)
    cancel_request (priv->requests->data);

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
salut_disco_new (SalutConnection *conn)
{
  SalutDisco *disco;

  g_return_val_if_fail (SALUT_IS_CONNECTION (conn), NULL);

  disco = SALUT_DISCO (g_object_new (SALUT_TYPE_DISCO,
        "connection", conn,
        NULL));

  return disco;
}


static void notify_delete_request (gpointer data, GObject *obj);

static void
delete_request (SalutDiscoRequest *request)
{
  SalutDisco *disco = request->disco;
  SalutDiscoPrivate *priv;

  g_assert (NULL != request);
  g_assert (SALUT_IS_DISCO (disco));

  priv = SALUT_DISCO_GET_PRIVATE (disco);

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

  g_object_unref (request->contact);
  g_free (request->node);
  g_slice_free (SalutDiscoRequest, request);
}

//static gboolean
//timeout_request (gpointer data)
//{
//  SalutDiscoRequest *request = (SalutDiscoRequest *) data;
//  SalutDisco *disco;
//  GError *err /* doesn't need initializing */;
//  g_return_val_if_fail (data != NULL, FALSE);
//
//  err = g_error_new (SALUT_DISCO_ERROR, SALUT_DISCO_ERROR_TIMEOUT,
//      "Request for %s on %s timed out",
//      (request->type == SALUT_DISCO_TYPE_INFO)?"info":"items",
//      request->jid);
//
//  /* Temporarily ref the disco object to avoid crashing if the callback
//   * destroys us (as seen in test-disco-no-reply.py) */
//  disco = g_object_ref (request->disco);
//
//  /* also, we're about to run the callback, so it's too late to cancel it -
//   * avoid crashing if running the callback destroys the bound object */
//  if (NULL != request->bound_object)
//    {
//      g_object_weak_unref (request->bound_object, notify_delete_request,
//          request);
//      request->bound_object = NULL;
//    }
//
//  (request->callback)(request->disco, request, request->jid, request->node,
//                      NULL, err, request->user_data);
//  g_error_free (err);
//
//  request->timer_id = 0;
//  delete_request (request);
//
//  g_object_unref (disco);
//
//  return FALSE;
//}

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

/*
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

static LmHandlerResult
request_reply_cb (SalutConnection *conn, LmMessage *sent_msg,
                  LmMessage *reply_msg, GObject *object, gpointer user_data)
{
  SalutDiscoRequest *request = (SalutDiscoRequest *) user_data;
  SalutDisco *disco = SALUT_DISCO (object);
  SalutDiscoPrivate *priv = SALUT_DISCO_GET_PRIVATE (disco);
  LmMessageNode *query_node;
  GError *err = NULL;

  g_assert (request);

  if (!g_list_find (priv->requests, request))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  query_node = lm_message_node_get_child_with_namespace (reply_msg->node,
      "query", disco_type_to_xmlns (request->type));

  if (lm_message_get_sub_type (reply_msg) == LM_MESSAGE_SUB_TYPE_ERROR)
    {
      err = salut_message_get_xmpp_error (reply_msg);

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

  request->callback (request->disco, request, request->jid, request->node,
                     query_node, err, request->user_data);
  delete_request (request);

  if (err)
    g_error_free (err);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}
*/

static void
notify_delete_request (gpointer data, GObject *obj)
{
  SalutDiscoRequest *request = (SalutDiscoRequest *) data;
  request->bound_object = NULL;
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
  SalutDiscoPrivate *priv = SALUT_DISCO_GET_PRIVATE (self);
  SalutDiscoRequest *request;
  //LmMessage *msg;
  //LmMessageNode *lm_node;

  request = g_slice_new0 (SalutDiscoRequest);
  request->disco = self;
  request->type = type;
  request->contact = g_object_ref (contact);
  if (node)
    request->node = g_strdup (node);
  request->callback = callback;
  request->user_data = user_data;
  request->bound_object = object;

  if (NULL != object)
    g_object_weak_ref (object, notify_delete_request, request);

  DEBUG ("Creating disco request %p for %s",
           request, request->contact->name);

  priv->requests = g_list_prepend (priv->requests, request);
  /*
  msg = lm_message_new_with_sub_type (jid, LM_MESSAGE_TYPE_IQ,
                                           LM_MESSAGE_SUB_TYPE_GET);
  lm_node = lm_message_node_add_child (msg->node, "query", NULL);

  lm_message_node_set_attribute (lm_node, "xmlns", disco_type_to_xmlns (type));

  if (node)
    {
      lm_message_node_set_attribute (lm_node, "node", node);
    }

  if (! _salut_connection_send_with_reply (priv->connection, msg,
        request_reply_cb, G_OBJECT(self), request, error))
    {
      delete_request (request);
      lm_message_unref (msg);
      return NULL;
    }
  else
    {
      request->timer_id =
          g_timeout_add (DEFAULT_REQUEST_TIMEOUT, timeout_request, request);
      lm_message_unref (msg);
      return request;
    }
*/
/**/ return NULL;
}

void
salut_disco_cancel_request (SalutDisco *disco, SalutDiscoRequest *request)
{
  SalutDiscoPrivate *priv;

  g_return_if_fail (SALUT_IS_DISCO (disco));
  g_return_if_fail (NULL != request);

  priv = SALUT_DISCO_GET_PRIVATE (disco);

  g_return_if_fail (NULL != g_list_find (priv->requests, request));

  cancel_request (request);
}

