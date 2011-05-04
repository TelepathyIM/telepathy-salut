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
#include <wocky/wocky-namespaces.h>
#include <wocky/wocky-stanza.h>
#include <wocky/wocky-porter.h>
#include <wocky/wocky-namespaces.h>

#include "gibber-muc-connection.h"
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
  PROP_PORTER = 1,
  PROP_CONTACT,
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
  WockyPorter *porter;
  WockyContact *contact;
  gchar *self_id;
  gchar *peer_id;
  gchar *stream_id;
  gchar *stream_init_id;
  guint stanza_received_id;
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

static gboolean
received_stanza_cb (WockyPorter *porter,
                    WockyStanza *stanza,
                    gpointer user_data)
{
  GibberBytestreamIBB *self = (GibberBytestreamIBB *) user_data;
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);
  WockyNode *node = wocky_stanza_get_top_node (stanza);
  WockyNode *data;
  GString *str;
  guchar *decoded;
  gsize len;
  const gchar *from, *stream_id;

  data = wocky_node_get_child_ns (node, "data", WOCKY_XMPP_NS_IBB);
  if (data == NULL)
    {
      return FALSE;
    }

  stream_id = wocky_node_get_attribute (data, "sid");
  if (stream_id == NULL || strcmp (stream_id, priv->stream_id) != 0)
    {
      DEBUG ("bad stream id");
      return FALSE;
    }

  if (priv->state != GIBBER_BYTESTREAM_STATE_OPEN)
    {
      DEBUG ("can't receive data through a not open bytestream (state: %d)",
          priv->state);
      return FALSE;
    }

  from = wocky_node_get_attribute (node, "from");
  if (from == NULL)
    {
      DEBUG ("got a message without a from field, ignoring");
      return FALSE;
    }

  // XXX check sequence number ?

  decoded = g_base64_decode (data->content, &len);
  str = g_string_new_len ((const gchar *) decoded, len);
  g_signal_emit_by_name (G_OBJECT (self), "data-received", from, str);

  g_string_free (str, TRUE);
  g_free (decoded);

  return TRUE;
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
      case PROP_PORTER:
        g_value_set_object (value, priv->porter);
        break;
      case PROP_CONTACT:
        g_value_set_object (value, priv->contact);
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
        g_value_set_string (value, WOCKY_XMPP_NS_IBB);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
make_porter_connections (GibberBytestreamIBB *self)
{
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);
  gchar *jid;

  jid = wocky_contact_dup_jid (priv->contact);

  priv->stanza_received_id = wocky_porter_register_handler_from (priv->porter,
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_TYPE_NONE, jid,
      WOCKY_PORTER_HANDLER_PRIORITY_NORMAL, received_stanza_cb, self, NULL);

  g_free (jid);
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
      case PROP_PORTER:
        priv->porter = g_value_dup_object (value);
        break;
      case PROP_CONTACT:
        priv->contact = g_value_dup_object (value);
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

  g_assert (priv->porter != NULL);
  g_assert (priv->contact != NULL);
  g_assert (priv->stream_init_id != NULL);
  g_assert (priv->self_id != NULL);
  g_assert (priv->peer_id != NULL);

  return obj;
}

static void
gibber_bytestream_ibb_constructed (GObject *obj)
{
  GibberBytestreamIBB *self = GIBBER_BYTESTREAM_IBB (obj);
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);

  if (G_OBJECT_CLASS (gibber_bytestream_ibb_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gibber_bytestream_ibb_parent_class)->constructed (obj);

  if (priv->porter != NULL && priv->contact != NULL)
    make_porter_connections (self);
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
  object_class->constructed = gibber_bytestream_ibb_constructed;

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
      "porter",
      "WockyPorter object",
      "Wocky porter object used for communication by this "
      "bytestream if it's a private one",
      WOCKY_TYPE_PORTER,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PORTER,
      param_spec);

  param_spec = g_param_spec_object (
      "contact",
      "WockyContact object",
      "Contact object used for communication by this "
      "bytestream if it's a private one",
      WOCKY_TYPE_CONTACT,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONTACT,
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
  WockyStanza *stanza;
  gchar *seq, *encoded;

  if (priv->state != GIBBER_BYTESTREAM_STATE_OPEN)
    {
      DEBUG ("can't send data through a not open bytestream (state: %d)",
          priv->state);
      return FALSE;
    }

  seq = g_strdup_printf ("%u", priv->seq++);

  encoded = g_base64_encode ((const guchar *) str, len);

  stanza = wocky_stanza_build_to_contact (WOCKY_STANZA_TYPE_MESSAGE,
      WOCKY_STANZA_SUB_TYPE_NONE,
      priv->self_id, priv->contact,
      '(', "data",
        ':', WOCKY_XMPP_NS_IBB,
        '@', "sid", priv->stream_id,
        '@', "seq", seq,
        '$', encoded,
      ')',
      '(', "amp",
        ':', WOCKY_XMPP_NS_AMP,
        '(', "rule",
          '@', "condition", "deliver-at",
          '@', "value", "stored",
          '@', "action", "error",
        ')',
        '(', "rule",
          '@', "condition", "match-resource",
          '@', "value", "exact",
          '@', "action", "error",
        ')',
      ')',
      NULL);

  DEBUG ("send %d bytes", len);
  wocky_porter_send (priv->porter, stanza);

  g_object_unref (stanza);
  g_free (encoded);
  g_free (seq);

  return TRUE;
}

static WockyStanza *
create_si_accept_iq (GibberBytestreamIBB *self)
{
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);

  return wocky_stanza_build_to_contact (
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_RESULT,
      priv->self_id, priv->contact,
      '@', "id", priv->stream_init_id,
      '(', "si",
        ':', WOCKY_XMPP_NS_SI,
        '(', "feature",
          ':', WOCKY_XMPP_NS_FEATURENEG,
          '(', "x",
            ':', WOCKY_XMPP_NS_DATA,
            '@', "type", "submit",
            '(', "field",
              '@', "var", "stream-method",
              '(', "value",
                '$', WOCKY_XMPP_NS_IBB,
              ')',
            ')',
          ')',
        ')',
      ')', NULL);
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
  WockyStanza *stanza;
  WockyNode *node;
  WockyNode *si;

  if (priv->state != GIBBER_BYTESTREAM_STATE_LOCAL_PENDING)
    {
      /* The stream was previoulsy or automatically accepted */
      DEBUG ("stream was already accepted");
      return;
    }

  stanza = create_si_accept_iq (self);
  node = wocky_stanza_get_top_node (stanza);
  si = wocky_node_get_child_ns (node, "si", WOCKY_XMPP_NS_SI);
  g_assert (si != NULL);

  if (func != NULL)
    {
      /* let the caller add his profile specific data */
      func (si, user_data);
    }

  wocky_porter_send (priv->porter, stanza);

  g_object_unref (stanza);

  DEBUG ("stream is now accepted");
  g_object_set (self, "state", GIBBER_BYTESTREAM_STATE_ACCEPTED, NULL);
}

static void
gibber_bytestream_ibb_decline (GibberBytestreamIBB *self,
                               GError *error)
{
  GibberBytestreamIBBPrivate *priv = GIBBER_BYTESTREAM_IBB_GET_PRIVATE (self);
  WockyStanza *stanza;
  WockyNode *node;

  g_return_if_fail (priv->state == GIBBER_BYTESTREAM_STATE_LOCAL_PENDING);

  stanza = wocky_stanza_build_to_contact (
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_ERROR,
      priv->self_id, priv->contact,
      '@', "id", priv->stream_init_id,
      NULL);
  node = wocky_stanza_get_top_node (stanza);

  if (error != NULL && error->domain == GIBBER_XMPP_ERROR)
    {
      gibber_xmpp_error_to_node (error->code, node, error->message);
    }
  else
    {
      gibber_xmpp_error_to_node (XMPP_ERROR_FORBIDDEN, node,
          "Offer Declined");
    }

  wocky_porter_send (priv->porter, stanza);

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
  else
    {
      WockyStanza *stanza;

      DEBUG ("send IBB close stanza");

      stanza = wocky_stanza_build_to_contact (WOCKY_STANZA_TYPE_IQ,
          WOCKY_STANZA_SUB_TYPE_SET,
          priv->self_id, priv->contact,
          '(', "close",
            ':', WOCKY_XMPP_NS_IBB,
            '@', "sid", priv->stream_id,
          ')', NULL);

      wocky_porter_send (priv->porter, stanza);

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
  WockyStanza *msg;

  if (priv->state != GIBBER_BYTESTREAM_STATE_INITIATING)
    {
      DEBUG ("bytestream is not is the initiating state (state %d",
          priv->state);
      return FALSE;
    }

  if (priv->stream_id == NULL)
    {
      DEBUG ("stream doesn't have an ID");
      return FALSE;
    }

  msg = wocky_stanza_build_to_contact (
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
      priv->self_id, priv->contact,
      '(', "open",
        ':', WOCKY_XMPP_NS_IBB,
        '@', "sid", priv->stream_id,
        '@', "block-size", "4096",
      ')', NULL);

  /* XXX should send using _with_reply (ibb_init_reply_cb) */
  wocky_porter_send (priv->porter, msg);

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
