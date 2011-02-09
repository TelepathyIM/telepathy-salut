/*
 * gibber-iq-helper.c - Source for GibberIqHelper
 * Copyright (C) 2007 Collabora Ltd.
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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "gibber-iq-helper.h"

#include "gibber-xmpp-reader.h"
#include "gibber-xmpp-writer.h"
#include "gibber-transport.h"
#include <wocky/wocky-stanza.h>

G_DEFINE_TYPE (GibberIqHelper, gibber_iq_helper, G_TYPE_OBJECT);

enum
{
  PROP_XMPP_CONNECTION = 1,
};

typedef struct _GibberIqHelperPrivate GibberIqHelperPrivate;

struct _GibberIqHelperPrivate
{
  GibberXmppConnection *xmpp_connection;
  GHashTable *id_handlers;

  gboolean dispose_has_run;
};

#define GIBBER_IQ_HELPER_GET_PRIVATE(obj) \
  ((GibberIqHelperPrivate *) ((GibberIqHelper *) obj)->priv)

static void
reply_handler_object_destroy_notify_cb (gpointer _data, GObject *object);

typedef struct
{
  GibberIqHelper *self;
  gchar *id;
  GibberIqHelperStanzaReplyFunc reply_func;
  WockyStanza *sent_stanza;
  GObject *object;
  gpointer user_data;
} ReplyHandlerData;

static void
free_reply_handler_data (ReplyHandlerData *data)
{
  g_object_unref (data->sent_stanza);
  g_free (data->id);

  if (data->object != NULL)
    {
      g_object_weak_unref (data->object,
          reply_handler_object_destroy_notify_cb, data);
    }

  g_slice_free (ReplyHandlerData, data);
}

static void
gibber_iq_helper_init (GibberIqHelper *self)
{
  GibberIqHelperPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GIBBER_TYPE_IQ_HELPER, GibberIqHelperPrivate);

  self->priv = priv;

  priv->id_handlers = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) free_reply_handler_data);

  priv->dispose_has_run = FALSE;
}

static void
xmpp_connection_received_stanza_cb (GibberXmppConnection *conn,
                                    WockyStanza *stanza,
                                    gpointer user_data)
{
  WockyNode *node = wocky_stanza_get_top_node (stanza);
  GibberIqHelper *self = GIBBER_IQ_HELPER (user_data);
  GibberIqHelperPrivate *priv = GIBBER_IQ_HELPER_GET_PRIVATE (self);
  const gchar *id;
  ReplyHandlerData *data;

  id = wocky_node_get_attribute (node, "id");
  if (id == NULL)
    return;

  data = g_hash_table_lookup (priv->id_handlers, id);
  if (data == NULL)
    return;

  /* Reply have to be an iq stanza */
  if (strcmp (node->name, "iq"))
    return;

  /* Its subtype have to be "result" or "error" */
  if (strcmp (wocky_node_get_attribute (node, "type"), "result")
      != 0 &&
      strcmp (wocky_node_get_attribute (node, "type"), "error")
      != 0)
    return;

  /* the user callback may want to free the GibberIqHelper object. Delay this.
   */
  g_object_ref (self);

  data->reply_func (self, data->sent_stanza,
      stanza, data->object, data->user_data);

  g_hash_table_remove (priv->id_handlers, id);

  g_object_unref (self);
}

static GObject *
gibber_iq_helper_constructor (GType type,
                              guint n_props,
                              GObjectConstructParam *props)
{
  GObject *obj;
  GibberIqHelper *self;
  GibberIqHelperPrivate *priv;

  obj = G_OBJECT_CLASS(gibber_iq_helper_parent_class)->
        constructor (type, n_props, props);

  self = GIBBER_IQ_HELPER (obj);
  priv = GIBBER_IQ_HELPER_GET_PRIVATE (self);

  g_assert (priv->xmpp_connection != NULL);

  g_signal_connect (priv->xmpp_connection, "received-stanza",
      G_CALLBACK (xmpp_connection_received_stanza_cb), obj);

  return obj;
}

static void
gibber_iq_helper_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  GibberIqHelper *self = GIBBER_IQ_HELPER (object);
  GibberIqHelperPrivate *priv = GIBBER_IQ_HELPER_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_XMPP_CONNECTION:
        g_value_set_object (value, priv->xmpp_connection);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gibber_iq_helper_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  GibberIqHelper *self = GIBBER_IQ_HELPER (object);
  GibberIqHelperPrivate *priv = GIBBER_IQ_HELPER_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_XMPP_CONNECTION:
        priv->xmpp_connection = g_value_get_object (value);
        g_object_ref (priv->xmpp_connection);
        break;
     default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void gibber_iq_helper_dispose (GObject *object);
static void gibber_iq_helper_finalize (GObject *object);

static void
gibber_iq_helper_class_init (GibberIqHelperClass *gibber_iq_helper_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_iq_helper_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gibber_iq_helper_class,
      sizeof (GibberIqHelperPrivate));

  object_class->constructor = gibber_iq_helper_constructor;

  object_class->dispose = gibber_iq_helper_dispose;
  object_class->finalize = gibber_iq_helper_finalize;

  object_class->get_property = gibber_iq_helper_get_property;
  object_class->set_property = gibber_iq_helper_set_property;

  param_spec = g_param_spec_object (
      "xmpp-connection",
      "GibberXmppConnection object",
      "Gibber XMPP Connection associated with this GibberIqHelper object",
      GIBBER_TYPE_XMPP_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_XMPP_CONNECTION,
      param_spec);
}

void
gibber_iq_helper_dispose (GObject *object)
{
  GibberIqHelper *self = GIBBER_IQ_HELPER (object);
  GibberIqHelperPrivate *priv = GIBBER_IQ_HELPER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_assert (priv->xmpp_connection != NULL);
  g_signal_handlers_disconnect_by_func (priv->xmpp_connection,
      xmpp_connection_received_stanza_cb, self);
  g_object_unref (priv->xmpp_connection);
  priv->xmpp_connection = NULL;

  if (G_OBJECT_CLASS (gibber_iq_helper_parent_class)->dispose)
    G_OBJECT_CLASS (gibber_iq_helper_parent_class)->dispose (object);
}

void
gibber_iq_helper_finalize (GObject *object)
{
  GibberIqHelper *self = GIBBER_IQ_HELPER (object);
  GibberIqHelperPrivate *priv = GIBBER_IQ_HELPER_GET_PRIVATE (self);

  g_hash_table_destroy (priv->id_handlers);
  priv->id_handlers = NULL;

  G_OBJECT_CLASS (gibber_iq_helper_parent_class)->finalize (object);
}

GibberIqHelper *
gibber_iq_helper_new (GibberXmppConnection *xmpp_connection)
{
  g_return_val_if_fail (xmpp_connection != NULL, NULL);

  return g_object_new (GIBBER_TYPE_IQ_HELPER,
      "xmpp-connection", xmpp_connection,
      NULL);
}

static void
reply_handler_object_destroy_notify_cb (gpointer _data,
                                        GObject *object)
{
  /* The object was destroyed so we don't care about the
   * reply anymore */
  ReplyHandlerData *data = _data;
  GibberIqHelperPrivate *priv = GIBBER_IQ_HELPER_GET_PRIVATE (data->self);

  data->object = NULL;
  g_hash_table_remove (priv->id_handlers, data->id);
}

/*
 * gibber_iq_send_with_reply
 *
 * Send a WockyStanza and call reply_func when we receive
 * its reply.
 *
 * If object is non-NULL the handler will follow the lifetime of that object,
 * which means that if the object is destroyed the callback will not be invoked
 */
gboolean
gibber_iq_helper_send_with_reply (GibberIqHelper *self,
                                  WockyStanza *iq,
                                  GibberIqHelperStanzaReplyFunc reply_func,
                                  GObject *object,
                                  gpointer user_data,
                                  GError **error)
{
  WockyNode *node = wocky_stanza_get_top_node (iq);
  GibberIqHelperPrivate *priv;
  const gchar *tmp;
  gchar *id;
  ReplyHandlerData *data;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (iq != NULL, FALSE);
  g_return_val_if_fail (reply_func != NULL, FALSE);
  g_return_val_if_fail (strcmp (node->name, "iq") == 0, FALSE);

  priv = GIBBER_IQ_HELPER_GET_PRIVATE (self);

  tmp = wocky_node_get_attribute (node, "id");
  if (tmp == NULL)
    {
      id = gibber_xmpp_connection_new_id (priv->xmpp_connection);
      wocky_node_set_attribute (node, "id", id);
    }
  else
    {
      id = g_strdup (tmp);
    }

  if (!gibber_xmpp_connection_send (priv->xmpp_connection, iq, error))
    {
      g_free (id);
      return FALSE;
    }

  data = g_slice_new (ReplyHandlerData);
  data->reply_func = reply_func;
  data->sent_stanza = g_object_ref (iq);
  data->user_data = user_data;
  data->object = object;
  data->id = id;
  data->self = self;

  if (object != NULL)
    {
      g_object_weak_ref (object, reply_handler_object_destroy_notify_cb,
          data);
    }

  /* XXX set a timout if we don't receive the reply ? */
  g_hash_table_insert (priv->id_handlers, id, data);

  return TRUE;
}

static WockyStanza *
new_reply (WockyStanza *iq,
           WockyStanzaSubType sub_type)
{
  WockyStanza *reply;
  WockyNode *node = wocky_stanza_get_top_node (iq);
  const gchar *id;
  const gchar *iq_from, *iq_to;

  g_return_val_if_fail (sub_type == WOCKY_STANZA_SUB_TYPE_RESULT ||
      sub_type == WOCKY_STANZA_SUB_TYPE_ERROR, NULL);
  g_return_val_if_fail (strcmp (node->name, "iq") == 0, NULL);

  id = wocky_node_get_attribute (node, "id");
  g_return_val_if_fail (id != NULL, NULL);

  iq_from = wocky_node_get_attribute (node, "from");
  iq_to = wocky_node_get_attribute (node, "to");

  reply = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      sub_type,
      iq_to, iq_from,
      WOCKY_NODE_ATTRIBUTE, "id", id,
      NULL);

  return reply;
}

WockyStanza *
gibber_iq_helper_new_result_reply (WockyStanza *iq)
{
  return new_reply (iq, WOCKY_STANZA_SUB_TYPE_RESULT);
}

WockyStanza *
gibber_iq_helper_new_error_reply (WockyStanza *iq,
                                  GibberXmppError error,
                                  const gchar *errmsg)
{
  WockyStanza *stanza;
  WockyNode *node;

  stanza = new_reply (iq, WOCKY_STANZA_SUB_TYPE_ERROR);
  node = wocky_stanza_get_top_node (stanza);
  gibber_xmpp_error_to_node (error, node, errmsg);

  /* TODO: Would be cool to copy <iq> children as in Gabble */

  return stanza;
}
