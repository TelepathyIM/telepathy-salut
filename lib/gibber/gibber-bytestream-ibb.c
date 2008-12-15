/*
 * gibber-bytestream-ibb.c - Source for GibberBytestreamIBB
 * Copyright (C) 2007-2008 Collabora Ltd.
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
#include "gibber-xmpp-error.h"

#define DEBUG_FLAG DEBUG_BYTESTREAM
#include "gibber-debug.h"

#include "gibber-signals-marshal.h"

/* IMPORTANT NOTE: This bytestream is not used anymore by Salut and so probably
 * a bit rotten */

/* FIXME: implement fragmentation using the "block-size" attribute */

static void
bytestream_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GibberBytestreamIBB, gibber_bytestream_ibb,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GIBBER_TYPE_BYTESTREAM_IFACE,
      bytestream_iface_init));

/* properties */
enum
{
  PROP_XMPP_CONNECTION = 1,
  PROP_SELF_ID,
  PROP_PEER_ID,
  PROP_STREAM_ID,
  PROP_STREAM_INIT_ID,
  PROP_STATE,
  PROP_PROTOCOL,
  LAST_PROPERTY
};

typedef struct _GibberBytestreamIBBPrivate GibberBytestreamIBBPrivate;
struct _GibberBytestreamIBBPrivate
{
  GibberXmppConnection *xmpp_connection;
  gchar *self_id;
  gchar *peer_id;
  gchar *stream_id;
  gchar *stream_init_id;
  GibberBytestreamState state;

  guint16 seq;
  guint16 last_seq_recv;

  gboolean dispose_has_run;
};

#define GIBBER_BYTESTREAM_IBB_GET_PRIVATE(obj) \
    ((GibberBytestreamIBBPrivate *) obj->priv)

static void
gibber_bytestream_ibb_init (GibberBytestreamIBB *self)
{
  GibberBytestreamIBBPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GIBBER_TYPE_BYTESTREAM_IBB, GibberBytestreamIBBPrivate);

  self->priv = priv;
}

static void
xmpp_connection_received_stanza_cb (GibberXmppConnection *conn,
                                    GibberXmppStanza *stanza,
                                    gpointer user_data)
{
  GibberBytestreamIBB *self = (GibberBytestreamIBB *) user_data;
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
      return;
    }

  stream_id = gibber_xmpp_node_get_attribute (data, "sid");
  if (stream_id == NULL || strcmp (stream_id, priv->stream_id) != 0)
    {
      DEBUG ("bad stream id");
      return;
    }

  if (priv->state != GIBBER_BYTESTREAM_STATE_OPEN)
    {
      DEBUG ("can't receive data through a not open bytestream (state: %d)",
          priv->state);
      return;
    }

  from = gibber_xmpp_node_get_attribute (stanza->node, "from");
  if (from == NULL)
    {
      DEBUG ("got a message without a from field, ignoring");
      return;
    }

  // XXX check sequence number ?

  decoded = g_base64_decode (data->content, &len);
  str = g_string_new_len ((const gchar *) decoded, len);
  g_signal_emit_by_name (G_OBJECT (self), "data-received", from, str);

  g_string_free (str, TRUE);
  g_free (decoded);
}

static void
gibber_bytestream_ibb_dispose (GObject *object)
{
  GibberBytestreamIBB *self = GIBBER_BYTESTREAM_IBB (object);
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;
  priv->dispose_has_run = TRUE;

  if (priv->state != GIBBER_BYTESTREAM_STATE_CLOSED)
    {
      gibber_bytestream_iface_close (GIBBER_BYTESTREAM_IFACE (self), NULL);
    }

  G_OBJECT_CLASS (gibber_bytestream_ibb_parent_class)->dispose (object);
}

static void
gibber_bytestream_ibb_finalize (GObject *object)
{
  GibberBytestreamIBB *self = GIBBER_BYTESTREAM_IBB (object);
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);

  g_free (priv->stream_id);
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
        g_value_set_object (value, priv->xmpp_connection);
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
      case PROP_PROTOCOL:
        g_value_set_string (value, GIBBER_XMPP_NS_IBB);
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
        priv->xmpp_connection = g_value_get_object (value);
        if (priv->xmpp_connection != NULL)
          g_signal_connect (priv->xmpp_connection, "received-stanza",
              G_CALLBACK (xmpp_connection_received_stanza_cb), self);
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
              g_signal_emit_by_name (object, "state-changed", priv->state);
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

  g_assert (priv->xmpp_connection != NULL);
  g_assert (priv->stream_init_id != NULL);
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

  g_object_class_override_property (object_class, PROP_SELF_ID,
      "self-id");
  g_object_class_override_property (object_class, PROP_PEER_ID,
      "peer-id");
  g_object_class_override_property (object_class, PROP_STREAM_ID,
      "stream-id");
  g_object_class_override_property (object_class, PROP_STATE,
      "state");
  g_object_class_override_property (object_class, PROP_PROTOCOL,
      "protocol");

  param_spec = g_param_spec_object (
      "xmpp-connection",
      "GibberXmppConnection object",
      "Gibber XMPP connection object used for communication by this "
      "bytestream if it's a private one",
      GIBBER_TYPE_XMPP_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_XMPP_CONNECTION,
      param_spec);

  param_spec = g_param_spec_string (
      "stream-init-id",
      "stream init ID",
      "the iq ID of the SI request, if any",
      "",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STREAM_INIT_ID,
      param_spec);
}

/*
 * gibber_bytestream_ibb_send
 *
 * Implements gibber_bytestream_iface_send on GibberBytestreamIface
 */
static gboolean
gibber_bytestream_ibb_send (GibberBytestreamIface *bytestream,
                            guint len,
                            const gchar *str)
{
  GibberBytestreamIBB *self = GIBBER_BYTESTREAM_IBB (bytestream);
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);
  GibberXmppStanza *stanza;
  gchar *seq, *encoded;
  gboolean ret;

  if (priv->state != GIBBER_BYTESTREAM_STATE_OPEN)
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

  DEBUG ("send %d bytes", len);
  ret = gibber_xmpp_connection_send (priv->xmpp_connection, stanza, NULL);

  g_object_unref (stanza);
  g_free (encoded);
  g_free (seq);

  return ret;
}

static GibberXmppStanza *
create_si_accept_iq (GibberBytestreamIBB *self)
{
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);

  return gibber_xmpp_stanza_build (
      GIBBER_STANZA_TYPE_IQ, GIBBER_STANZA_SUB_TYPE_RESULT,
      priv->self_id, priv->peer_id,
      GIBBER_NODE_ATTRIBUTE, "id", priv->stream_init_id,
      GIBBER_NODE, "si",
        GIBBER_NODE_XMLNS, GIBBER_XMPP_NS_SI,
        GIBBER_NODE, "feature",
          GIBBER_NODE_XMLNS, GIBBER_XMPP_NS_FEATURENEG,
          GIBBER_NODE, "x",
            GIBBER_NODE_XMLNS, GIBBER_XMPP_NS_DATA,
            GIBBER_NODE_ATTRIBUTE, "type", "submit",
            GIBBER_NODE, "field",
              GIBBER_NODE_ATTRIBUTE, "var", "stream-method",
              GIBBER_NODE, "value",
                GIBBER_NODE_TEXT, GIBBER_XMPP_NS_IBB,
              GIBBER_NODE_END,
            GIBBER_NODE_END,
          GIBBER_NODE_END,
        GIBBER_NODE_END,
      GIBBER_NODE_END, GIBBER_STANZA_END);
}

/*
 * gibber_bytestream_ibb_accept
 *
 * Implements gibber_bytestream_iface_accept on GibberBytestreamIface
 */
static void
gibber_bytestream_ibb_accept (GibberBytestreamIface *bytestream,
                              GibberBytestreamAugmentSiAcceptReply func,
                              gpointer user_data)
{
  GibberBytestreamIBB *self = GIBBER_BYTESTREAM_IBB (bytestream);
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);
  GibberXmppStanza *stanza;
  GibberXmppNode *si;

  if (priv->state != GIBBER_BYTESTREAM_STATE_LOCAL_PENDING)
    {
      /* The stream was previoulsy or automatically accepted */
      DEBUG ("stream was already accepted");
      return;
    }

  stanza = create_si_accept_iq (self);
  si = gibber_xmpp_node_get_child_ns (stanza->node, "si", GIBBER_XMPP_NS_SI);
  g_assert (si != NULL);

  if (func != NULL)
    {
      /* let the caller add his profile specific data */
      func (si, user_data);
    }

  gibber_xmpp_connection_send (priv->xmpp_connection, stanza, NULL);

  DEBUG ("stream is now accepted");
  g_object_set (self, "state", GIBBER_BYTESTREAM_STATE_ACCEPTED, NULL);
}

static void
gibber_bytestream_ibb_decline (GibberBytestreamIBB *self,
                               GError *error)
{
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);
  GibberXmppStanza *stanza;

  g_return_if_fail (priv->state == GIBBER_BYTESTREAM_STATE_LOCAL_PENDING);

  stanza = gibber_xmpp_stanza_build (
      GIBBER_STANZA_TYPE_IQ, GIBBER_STANZA_SUB_TYPE_ERROR,
      priv->self_id, priv->peer_id,
      GIBBER_NODE_ATTRIBUTE, "id", priv->stream_init_id,
      GIBBER_STANZA_END);

  if (error != NULL && error->domain == GIBBER_XMPP_ERROR)
    {
      gibber_xmpp_error_to_node (error->code, stanza->node, error->message);
    }
  else
    {
      gibber_xmpp_error_to_node (XMPP_ERROR_FORBIDDEN, stanza->node,
          "Offer Declined");
    }

  gibber_xmpp_connection_send (priv->xmpp_connection, stanza, NULL);

  g_object_unref (stanza);
}

/*
 * gibber_bytestream_ibb_close
 *
 * Implements gibber_bytestream_iface_close on GibberBytestreamIface
 */
static void
gibber_bytestream_ibb_close (GibberBytestreamIface *bytestream,
                             GError *error)
{
  GibberBytestreamIBB *self = GIBBER_BYTESTREAM_IBB (bytestream);
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);

  if (priv->state == GIBBER_BYTESTREAM_STATE_CLOSED)
     /* bytestream already closed, do nothing */
     return;

  g_object_set (self, "state", GIBBER_BYTESTREAM_STATE_CLOSING, NULL);

  if (priv->state == GIBBER_BYTESTREAM_STATE_LOCAL_PENDING)
    {
      /* Stream was created using SI so we decline the request */
      gibber_bytestream_ibb_decline (self, error);
    }

  else if (priv->xmpp_connection != NULL)
    {
      GibberXmppStanza *stanza;

      DEBUG ("send IBB close stanza");

      stanza = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_IQ,
          GIBBER_STANZA_SUB_TYPE_SET,
          priv->self_id, priv->peer_id,
          GIBBER_NODE, "close",
            GIBBER_NODE_XMLNS, GIBBER_XMPP_NS_IBB,
            GIBBER_NODE_ATTRIBUTE, "sid", priv->stream_id,
          GIBBER_NODE_END, GIBBER_STANZA_END);

      gibber_xmpp_connection_send (priv->xmpp_connection, stanza, NULL);

      g_object_unref (stanza);
    }

  g_object_set (self, "state", GIBBER_BYTESTREAM_STATE_CLOSED, NULL);
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
      g_object_set (self, "state", GIBBER_BYTESTREAM_STATE_OPEN, NULL);
    }
  else
    {
      DEBUG ("error during IBB initiation");
      g_object_set (self, "state", GIBBER_BYTESTREAM_STATE_CLOSED, NULL);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}
#endif

/*
 * gibber_bytestream_ibb_initiate
 *
 * Implements gibber_bytestream_iface_initiate on GibberBytestreamIface
 */
static gboolean
gibber_bytestream_ibb_initiate (GibberBytestreamIface *bytestream)
{
  GibberBytestreamIBB *self = GIBBER_BYTESTREAM_IBB (bytestream);
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);
  GibberXmppStanza *msg;
  GError *error = NULL;

  if (priv->state != GIBBER_BYTESTREAM_STATE_INITIATING)
    {
      DEBUG ("bytestream is not is the initiating state (state %d",
          priv->state);
      return FALSE;
    }

  if (priv->xmpp_connection == NULL)
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
  if (!gibber_xmpp_connection_send (priv->xmpp_connection, msg, &error))
    {
      DEBUG ("Error when sending IBB init stanza: %s", error->message);

      g_error_free (error);
      g_object_unref (msg);
      return FALSE;
    }

  g_object_unref (msg);

  return TRUE;
}

static void
bytestream_iface_init (gpointer g_iface,
                       gpointer iface_data)
{
  GibberBytestreamIfaceClass *klass = (GibberBytestreamIfaceClass *) g_iface;

  klass->initiate = gibber_bytestream_ibb_initiate;
  klass->send = gibber_bytestream_ibb_send;
  klass->close = gibber_bytestream_ibb_close;
  klass->accept = gibber_bytestream_ibb_accept;
}
