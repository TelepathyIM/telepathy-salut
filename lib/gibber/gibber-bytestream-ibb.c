/*
 * gibber-bytestream-ibb.c - Source for GibberBytestreamIBB
 * Copyright (C) 2007 Ltd.
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

#include "gibber-bytestream-ibb.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#include "gibber-xmpp-connection.h"
#include "gibber-muc-connection.h"
#include "gibber-xmpp-stanza.h"
#include "gibber-namespaces.h"

#define DEBUG_FLAG DEBUG_BYTESTREAM
#include "gibber-debug.h"

#include "gibber-bytestream-ibb-signals-marshal.h"
//#include "bytestream-factory.h"

G_DEFINE_TYPE (GibberBytestreamIBB, gibber_bytestream_ibb, G_TYPE_OBJECT);

/* signals */
enum
{
  DATA_RECEIVED,
  STATE_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_XMPP_CONNECTION = 1,
  PROP_MUC_CONNECTION,
  PROP_SELF_ID,
  PROP_PEER_ID,
  PROP_STREAM_ID,
  PROP_STREAM_INIT_ID,
  PROP_STATE,
  LAST_PROPERTY
};

typedef struct _GibberBytestreamIBBPrivate GibberBytestreamIBBPrivate;
struct _GibberBytestreamIBBPrivate
{
  GibberXmppConnection *xmpp_connnection;
  GibberMucConnection *muc_connection;
  gchar *self_id;
  gchar *peer_id;
  gchar *stream_id;
  gchar *stream_init_id;
  GibberBytestreamIBBState state;

  guint16 seq;
  guint16 last_seq_recv;
};

#define GIBBER_BYTESTREAM_IBB_GET_PRIVATE(obj) \
    ((GibberBytestreamIBBPrivate *) obj->priv)

static void
gibber_bytestream_ibb_init (GibberBytestreamIBB *self)
{
  GibberBytestreamIBBPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GIBBER_TYPE_BYTESTREAM_IBB, GibberBytestreamIBBPrivate);

  self->priv = priv;

  priv->xmpp_connnection = NULL;
  priv->muc_connection = NULL;
  priv->self_id = NULL;
  priv->peer_id = NULL;

  priv->seq = 0;
  priv->last_seq_recv = 0;
}

static gboolean
send_stanza (GibberBytestreamIBB *self,
             GibberXmppStanza *stanza,
             GError **error)
{
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);

  if (priv->muc_connection != NULL)
    return gibber_muc_connection_send (priv->muc_connection, stanza, error);

  if (priv->xmpp_connnection != NULL)
    return gibber_xmpp_connection_send (priv->xmpp_connnection, stanza, error);

  g_assert_not_reached ();
}

static gboolean
stanza_received (GibberBytestreamIBB *self,
                 GibberXmppStanza *stanza)
{
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);
  GibberXmppNode *data;
  GString *str;
  guchar *decoded;
  gsize len;
  const gchar *from, *stream_id;

  data = gibber_xmpp_node_get_child_ns (stanza->node, "data",
      GIBBER_XMPP_NS_IBB);
  if (data == NULL)
    {
      return FALSE;
    }

  stream_id = gibber_xmpp_node_get_attribute (data, "sid");
  if (stream_id == NULL || strcmp (stream_id, priv->stream_id) != 0)
    {
      DEBUG ("bad stream id");
      return FALSE;
    }

  if (priv->state != GIBBER_BYTESTREAM_IBB_STATE_OPEN)
    {
      DEBUG ("can't receive data through a not open bytestream (state: %d)",
          priv->state);
      return FALSE;
    }

  from = gibber_xmpp_node_get_attribute (stanza->node, "from");
  if (from == NULL)
    {
      DEBUG ("got a message without a from field, ignoring");
      return FALSE;
    }

  // XXX check sequence number ?

  decoded = g_base64_decode (data->content, &len);
  str = g_string_new_len ((const gchar *) decoded, len);
  g_signal_emit (G_OBJECT (self), signals[DATA_RECEIVED], 0, from, str);

  g_string_free (str, TRUE);
  g_free (decoded);
  return TRUE;
}

static void
xmpp_connection_received_stanza_cb (GibberXmppConnection *conn,
                                    GibberXmppStanza *stanza,
                                    gpointer user_data)
{
  GibberBytestreamIBB *self = (GibberBytestreamIBB *) user_data;

  stanza_received (self, stanza);
}

static void
muc_connection_received_stanza_cb (GibberMucConnection *conn,
                                   const gchar *from,
                                   GibberXmppStanza *stanza,
                                   gpointer user_data)
{
  GibberBytestreamIBB *self = (GibberBytestreamIBB *) user_data;

  stanza_received (self, stanza);
}

static void
gibber_bytestream_ibb_dispose (GObject *object)
{
  GibberBytestreamIBB *self = GIBBER_BYTESTREAM_IBB (object);
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);

  if (priv->state != GIBBER_BYTESTREAM_IBB_STATE_CLOSED)
    {
      gibber_bytestream_ibb_close (self);
    }

  G_OBJECT_CLASS (gibber_bytestream_ibb_parent_class)->dispose (object);
}

static void
gibber_bytestream_ibb_finalize (GObject *object)
{
  GibberBytestreamIBB *self = GIBBER_BYTESTREAM_IBB (object);
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);

  g_free (priv->stream_id);

  if (priv->stream_init_id)
    g_free (priv->stream_init_id);

  g_free (priv->self_id);
  g_free (priv->peer_id);

  G_OBJECT_CLASS (gibber_bytestream_ibb_parent_class)->finalize (object);
}

static void
gibber_bytestream_ibb_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
  GibberBytestreamIBB *self = GIBBER_BYTESTREAM_IBB (object);
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_XMPP_CONNECTION:
        g_value_set_object (value, priv->xmpp_connnection);
        break;
      case PROP_MUC_CONNECTION:
        g_value_set_object (value, priv->muc_connection);
        break;
      case PROP_SELF_ID:
        g_value_set_string (value, priv->self_id);
        break;
      case PROP_PEER_ID:
        g_value_set_string (value, priv->peer_id);
        break;
      case PROP_STREAM_ID:
        g_value_set_string (value, priv->stream_id);
        break;
      case PROP_STREAM_INIT_ID:
        g_value_set_string (value, priv->stream_init_id);
        break;
      case PROP_STATE:
        g_value_set_uint (value, priv->state);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gibber_bytestream_ibb_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  GibberBytestreamIBB *self = GIBBER_BYTESTREAM_IBB (object);
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_XMPP_CONNECTION:
        priv->xmpp_connnection = g_value_get_object (value);
        if (priv->xmpp_connnection != NULL)
          g_signal_connect (priv->xmpp_connnection, "received-stanza",
              G_CALLBACK (xmpp_connection_received_stanza_cb), self);
        break;
      case PROP_MUC_CONNECTION:
        priv->muc_connection = g_value_get_object (value);
        if (priv->muc_connection != NULL)
          g_signal_connect (priv->muc_connection, "received-stanza",
              G_CALLBACK (muc_connection_received_stanza_cb), self);
        break;
      case PROP_SELF_ID:
        g_free (priv->self_id);
        priv->self_id = g_value_dup_string (value);
        break;
      case PROP_PEER_ID:
        g_free (priv->peer_id);
        priv->peer_id = g_value_dup_string (value);
        break;
      case PROP_STREAM_ID:
        g_free (priv->stream_id);
        priv->stream_id = g_value_dup_string (value);
        break;
      case PROP_STREAM_INIT_ID:
        g_free (priv->stream_init_id);
        priv->stream_init_id = g_value_dup_string (value);
        break;
      case PROP_STATE:
        if (priv->state != g_value_get_uint (value))
            {
              priv->state = g_value_get_uint (value);
              g_signal_emit (object, signals[STATE_CHANGED], 0, priv->state);
            }
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
gibber_bytestream_ibb_constructor (GType type,
                                   guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj;
  GibberBytestreamIBBPrivate *priv;

  obj = G_OBJECT_CLASS (gibber_bytestream_ibb_parent_class)->
           constructor (type, n_props, props);

  priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (GIBBER_BYTESTREAM_IBB (obj));

  /* We can't be a private *and* a muc bytestream */
  g_assert (priv->xmpp_connnection == NULL || priv->muc_connection == NULL);
  g_assert (priv->self_id != NULL);
  g_assert (priv->peer_id != NULL);

  return obj;
}

static void
gibber_bytestream_ibb_class_init (
    GibberBytestreamIBBClass *gibber_bytestream_ibb_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_bytestream_ibb_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gibber_bytestream_ibb_class,
      sizeof (GibberBytestreamIBBPrivate));

  object_class->dispose = gibber_bytestream_ibb_dispose;
  object_class->finalize = gibber_bytestream_ibb_finalize;

  object_class->get_property = gibber_bytestream_ibb_get_property;
  object_class->set_property = gibber_bytestream_ibb_set_property;
  object_class->constructor = gibber_bytestream_ibb_constructor;

  param_spec = g_param_spec_object (
      "xmpp-connection",
      "GibberXmppConnection object",
      "Gibber XMPP connection object used for communication by this "
      "bytestream if it's a private one",
      GIBBER_TYPE_XMPP_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_XMPP_CONNECTION,
      param_spec);

  param_spec = g_param_spec_object (
      "muc-connection",
      "GibberMucConnection object",
      "Gibber MUC connection object used for communication by this "
      "bytestream if it's a muc one",
      GIBBER_TYPE_MUC_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_MUC_CONNECTION,
      param_spec);

 param_spec = g_param_spec_string (
      "self-id",
      "self ID",
      "the ID of the local user",
      "",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SELF_ID, param_spec);

 param_spec = g_param_spec_string (
      "peer-id",
      "peer JID",
      "the ID of the muc or the remote user associated with this bytestream",
      "",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PEER_ID, param_spec);

  param_spec = g_param_spec_string (
      "stream-id",
      "stream ID",
      "the ID of the stream",
      "",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STREAM_ID, param_spec);

  param_spec = g_param_spec_string (
      "stream-init-id",
      "stream init ID",
      "the iq ID of the SI request, if any",
      "",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STREAM_INIT_ID,
      param_spec);

  param_spec = g_param_spec_uint (
      "state",
      "Bytestream state",
      "An enum (BytestreamIBBState) signifying the current state of"
      "this bytestream object",
      0, LAST_GIBBER_BYTESTREAM_IBB_STATE - 1,
      GIBBER_BYTESTREAM_IBB_STATE_LOCAL_PENDING,
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  signals[DATA_RECEIVED] =
    g_signal_new ("data-received",
                  G_OBJECT_CLASS_TYPE (gibber_bytestream_ibb_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gibber_bytestream_ibb_marshal_VOID__STRING_POINTER,
                  G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_POINTER);

  signals[STATE_CHANGED] =
    g_signal_new ("state-changed",
                  G_OBJECT_CLASS_TYPE (gibber_bytestream_ibb_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);
}

gboolean
send_data_to (GibberBytestreamIBB *self,
              const gchar *to,
              gboolean groupchat,
              guint len,
              gchar *str)
{
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);
  GibberXmppStanza *stanza;
  gchar *seq, *encoded;
  gboolean ret;

  if (priv->state != GIBBER_BYTESTREAM_IBB_STATE_OPEN)
    {
      DEBUG ("can't send data through a not open bytestream (state: %d)",
          priv->state);
      return FALSE;
    }

  seq = g_strdup_printf ("%u", priv->seq++);

  encoded = g_base64_encode ((const guchar *) str, len);

  stanza = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_MESSAGE,
      GIBBER_STANZA_SUB_TYPE_NONE,
      priv->self_id, priv->peer_id,
      GIBBER_NODE, "data",
        GIBBER_NODE_XMLNS, GIBBER_XMPP_NS_IBB,
        GIBBER_NODE_ATTRIBUTE, "sid", priv->stream_id,
        GIBBER_NODE_ATTRIBUTE, "seq", seq,
        GIBBER_NODE_TEXT, encoded,
      GIBBER_NODE_END,
      GIBBER_NODE, "amp",
        GIBBER_NODE_XMLNS, GIBBER_XMPP_NS_AMP,
        GIBBER_NODE, "rule",
          GIBBER_NODE_ATTRIBUTE, "condition", "deliver-at",
          GIBBER_NODE_ATTRIBUTE, "value", "stored",
          GIBBER_NODE_ATTRIBUTE, "action", "error",
        GIBBER_NODE_END,
        GIBBER_NODE, "rule",
          GIBBER_NODE_ATTRIBUTE, "condition", "match-resource",
          GIBBER_NODE_ATTRIBUTE, "value", "exact",
          GIBBER_NODE_ATTRIBUTE, "action", "error",
        GIBBER_NODE_END,
      GIBBER_NODE_END,
      GIBBER_STANZA_END);

  if (groupchat)
    {
      gibber_xmpp_node_set_attribute (stanza->node, "type", "groupchat");
    }

  DEBUG ("send %d bytes", len);
  ret = send_stanza (self, stanza, NULL);

  g_object_unref (stanza);
  g_free (encoded);
  g_free (seq);

  return ret;
}

gboolean
gibber_bytestream_ibb_send (GibberBytestreamIBB *self,
                            guint len,
                            gchar *str)
{
  GibberBytestreamIBBPrivate *priv;
  gboolean groupchat = FALSE;

  g_assert (GIBBER_IS_BYTESTREAM_IBB (self));
  priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);

  if (priv->muc_connection != NULL)
    groupchat = TRUE;

  return send_data_to (self, priv->peer_id, groupchat, len, str);
}

/*
GibberXmppStanza *
gibber_bytestream_ibb_make_accept_iq (GibberBytestreamIBB *self)
{
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);
  LmMessage *msg;

  if (priv->peer_handle_type == TP_HANDLE_TYPE_ROOM ||
      priv->stream_init_id == NULL)
    {
      DEBUG ("bytestream was not created due to a SI request");
      return NULL;
    }

  msg = gibber_bytestream_factory_make_accept_iq (priv->peer_jid,
      priv->stream_init_id, NS_IBB);

  return msg;
}
*/

void
gibber_bytestream_ibb_accept (GibberBytestreamIBB *self, GibberXmppStanza *msg)
{
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);
  GError *error = NULL;

  if (priv->state != GIBBER_BYTESTREAM_IBB_STATE_LOCAL_PENDING)
    {
      /* The stream was previoulsy or automatically accepted */
      return;
    }

  if (priv->xmpp_connnection != NULL ||
      priv->stream_init_id == NULL)
    {
      DEBUG ("can't accept a bytestream not created due to a SI request");
      return;
    }

  if (send_stanza (self, msg, &error))
    {
      priv->state = GIBBER_BYTESTREAM_IBB_STATE_ACCEPTED;
    }
  else
    {
      DEBUG ("send accept stanza failed: %s", error->message);
      g_error_free (error);
    }
}

static void
gibber_bytestream_ibb_decline (GibberBytestreamIBB *self)
{
/*
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);
  GibberXmppStanza *stanza

  if (priv->state != GIBBER_BYTESTREAM_IBB_STATE_LOCAL_PENDING)
    {
      DEBUG ("bytestream is not in the local pending state (state %d)",
          priv->state);
      return;
    }

  if (priv->peer_handle_type == TP_HANDLE_TYPE_ROOM ||
      priv->stream_init_id == NULL)
    {
      DEBUG ("can't decline a bytestream not created due to a SI request");
      return;
    }

  msg = gibber_bytestream_factory_make_decline_iq (priv->peer_jid,
      priv->stream_init_id);

  _gibber_connection_send (priv->conn, msg, NULL);

  lm_message_unref (msg);
  */
}

void
gibber_bytestream_ibb_close (GibberBytestreamIBB *self)
{
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);

  if (priv->state == GIBBER_BYTESTREAM_IBB_STATE_CLOSED)
     /* bytestream already closed, do nothing */
     return;

  if (priv->state == GIBBER_BYTESTREAM_IBB_STATE_LOCAL_PENDING)
    {
      if (priv->stream_init_id != NULL)
        {
          /* Stream was created using SI so we decline the request */
          gibber_bytestream_ibb_decline (self);
        }
    }

  else if (priv->xmpp_connnection != NULL)
    {
      /* XXX : Does it make sense to send a close message in a
       * muc bytestream ? */
      GibberXmppStanza *stanza;

      DEBUG ("send IBB close stanza");

      stanza = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_IQ,
          GIBBER_STANZA_SUB_TYPE_SET,
          priv->self_id, priv->peer_id,
          GIBBER_NODE, "close",
            GIBBER_NODE_XMLNS, GIBBER_XMPP_NS_IBB,
            GIBBER_NODE_ATTRIBUTE, "sid", priv->stream_id,
          GIBBER_NODE_END, GIBBER_STANZA_END);

      send_stanza (self, stanza, NULL);

      g_object_unref (stanza);
    }

  g_object_set (self, "state", GIBBER_BYTESTREAM_IBB_STATE_CLOSED, NULL);
}

#if 0
static LmHandlerResult
ibb_init_reply_cb (GibberConnection *conn,
                   LmMessage *sent_msg,
                   LmMessage *reply_msg,
                   GObject *obj,
                   gpointer user_data)
{
  GibberBytestreamIBB *self = GIBBER_BYTESTREAM_IBB (obj);

  if (lm_message_get_sub_type (reply_msg) == LM_MESSAGE_SUB_TYPE_RESULT)
    {
      /* yeah, stream initiated */
      DEBUG ("IBB stream initiated");
      g_object_set (self, "state", GIBBER_BYTESTREAM_IBB_STATE_OPEN, NULL);
    }
  else
    {
      DEBUG ("error during IBB initiation");
      g_object_set (self, "state", GIBBER_BYTESTREAM_IBB_STATE_CLOSED, NULL);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}
#endif

gboolean
gibber_bytestream_ibb_initiation (GibberBytestreamIBB *self)
{
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);
  GibberXmppStanza *msg;
  GError *error = NULL;

  if (priv->state != GIBBER_BYTESTREAM_IBB_STATE_INITIATING)
    {
      DEBUG ("bytestream is not is the initiating state (state %d",
          priv->state);
      return FALSE;
    }

  if (priv->xmpp_connnection == NULL)
    {
      DEBUG ("Can only initiate a private bytestream");
      return FALSE;
    }

  if (priv->stream_id == NULL)
    {
      DEBUG ("stream doesn't have an ID");
      return FALSE;
    }

  msg = gibber_xmpp_stanza_build (
      GIBBER_STANZA_TYPE_IQ, GIBBER_STANZA_SUB_TYPE_SET,
      priv->self_id, priv->peer_id,
      GIBBER_NODE, "open",
        GIBBER_NODE_XMLNS, GIBBER_XMPP_NS_IBB,
        GIBBER_NODE_ATTRIBUTE, "sid", priv->stream_id,
        GIBBER_NODE_ATTRIBUTE, "block-size", "4096",
      GIBBER_NODE_END, GIBBER_STANZA_END);

  /* XXX should send using _with_reply (ibb_init_reply_cb) */
  if (!send_stanza (self, msg, &error))
    {
      DEBUG ("Error when sending IBB init stanza: %s", error->message);

      g_error_free (error);
      g_object_unref (msg);
      return FALSE;
    }

  g_object_unref (msg);

  return TRUE;
}
