/*
 * gibber-bytestream-muc.c - Source for GibberBytestreamMuc
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

#include "gibber-bytestream-muc.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#include "gibber-bytestream-iface.h"
#include "gibber-muc-connection.h"
#include "gibber-linklocal-transport.h"

#define DEBUG_FLAG DEBUG_BYTESTREAM
#include "gibber-debug.h"

#include "signals-marshal.h"

static void bytestream_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GibberBytestreamMuc, gibber_bytestream_muc,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GIBBER_TYPE_BYTESTREAM_IFACE,
      bytestream_iface_init));

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
  PROP_MUC_CONNECTION = 1,
  PROP_SELF_ID,
  PROP_PEER_ID,
  PROP_STREAM_ID,
  PROP_STATE,
  PROP_PROTOCOL,
  LAST_PROPERTY
};

typedef struct _GibberBytestreamMucPrivate GibberBytestreamMucPrivate;
struct _GibberBytestreamMucPrivate
{
  GibberMucConnection *muc_connection;
  gchar *self_id;
  gchar *peer_id;
  gchar *stream_id;
  GibberBytestreamState state;
  guint16 stream_id_multicast;
  /* gchar *sender -> guint16 stream-id */
  GHashTable *senders;

  gboolean dispose_has_run;
};

#define GIBBER_BYTESTREAM_MUC_GET_PRIVATE(obj) \
    ((GibberBytestreamMucPrivate *) obj->priv)

static void
gibber_bytestream_muc_init (GibberBytestreamMuc *self)
{
  GibberBytestreamMucPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GIBBER_TYPE_BYTESTREAM_MUC, GibberBytestreamMucPrivate);

  self->priv = priv;

  priv->senders = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, NULL);

  priv->dispose_has_run = FALSE;
}

static void
muc_connection_received_data_cb (GibberMucConnection *muc_connection,
                                 const gchar *sender,
                                 guint stream_id,
                                 const guint16 *data,
                                 gsize length,
                                 GibberBytestreamMuc *self)
{
  GibberBytestreamMucPrivate *priv = GIBBER_BYTESTREAM_MUC_GET_PRIVATE (self);
  GString *str;
  guint sender_stream_id;

  sender_stream_id = GPOINTER_TO_UINT (g_hash_table_lookup (priv->senders,
        sender));

  if (sender_stream_id == 0 || (guint16) sender_stream_id != stream_id)
    return;

  str = g_string_new_len ((const gchar *) data, length);
  g_signal_emit (G_OBJECT (self), signals[DATA_RECEIVED], 0, sender,
    str);

  g_string_free (str, TRUE);
}

static void
gibber_bytestream_muc_dispose (GObject *object)
{
  GibberBytestreamMuc *self = GIBBER_BYTESTREAM_MUC (object);
  GibberBytestreamMucPrivate *priv = GIBBER_BYTESTREAM_MUC_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->state != GIBBER_BYTESTREAM_STATE_CLOSED)
    {
      gibber_bytestream_iface_close (GIBBER_BYTESTREAM_IFACE (self), NULL);
    }

  g_hash_table_destroy (priv->senders);

  G_OBJECT_CLASS (gibber_bytestream_muc_parent_class)->dispose (object);
}

static void
gibber_bytestream_muc_finalize (GObject *object)
{
  GibberBytestreamMuc *self = GIBBER_BYTESTREAM_MUC (object);
  GibberBytestreamMucPrivate *priv = GIBBER_BYTESTREAM_MUC_GET_PRIVATE (self);

  g_free (priv->stream_id);
  g_free (priv->self_id);
  g_free (priv->peer_id);

  G_OBJECT_CLASS (gibber_bytestream_muc_parent_class)->finalize (object);
}

static gboolean
check_stream_id (GibberBytestreamMuc *self)
{
  GibberBytestreamMucPrivate *priv = GIBBER_BYTESTREAM_MUC_GET_PRIVATE (self);

  if (priv->stream_id_multicast != 0)
    return TRUE;

  /* No stream allocated yet. Request one now */
  priv->stream_id_multicast = gibber_muc_connection_new_stream (
      priv->muc_connection);
  if (priv->stream_id_multicast == 0)
    {
      DEBUG ("Can't allocate a new stream. Bytestream closed");
      gibber_bytestream_iface_close (GIBBER_BYTESTREAM_IFACE (self));
      return FALSE;
    }

  priv->stream_id = g_strdup_printf ("%u", priv->stream_id_multicast);

  return TRUE;
}

static void
gibber_bytestream_muc_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
  GibberBytestreamMuc *self = GIBBER_BYTESTREAM_MUC (object);
  GibberBytestreamMucPrivate *priv = GIBBER_BYTESTREAM_MUC_GET_PRIVATE (self);

  switch (property_id)
    {
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
        check_stream_id (self);
        g_value_set_string (value, priv->stream_id);
        break;
      case PROP_STATE:
        g_value_set_uint (value, priv->state);
        break;
      case PROP_PROTOCOL:
        g_value_set_string (value, "rmulticast");
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gibber_bytestream_muc_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  GibberBytestreamMuc *self = GIBBER_BYTESTREAM_MUC (object);
  GibberBytestreamMucPrivate *priv = GIBBER_BYTESTREAM_MUC_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_MUC_CONNECTION:
        priv->muc_connection = g_value_get_object (value);
        if (priv->muc_connection != NULL)
          {
            /* FIXME: at some point it could be useful to modify Gibber's
             * API to only be notified for data from a given stream */
            g_signal_connect (priv->muc_connection, "received-data",
                G_CALLBACK (muc_connection_received_data_cb), self);
          }
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
gibber_bytestream_muc_constructor (GType type,
                                   guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj;
  GibberBytestreamMucPrivate *priv;

  obj = G_OBJECT_CLASS (gibber_bytestream_muc_parent_class)->
           constructor (type, n_props, props);

  priv = GIBBER_BYTESTREAM_MUC_GET_PRIVATE (GIBBER_BYTESTREAM_MUC (obj));

  g_assert (priv->muc_connection != NULL);
  g_assert (priv->self_id != NULL);
  g_assert (priv->peer_id != NULL);

  return obj;
}

static void
gibber_bytestream_muc_class_init (
    GibberBytestreamMucClass *gibber_bytestream_muc_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_bytestream_muc_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gibber_bytestream_muc_class,
      sizeof (GibberBytestreamMucPrivate));

  object_class->dispose = gibber_bytestream_muc_dispose;
  object_class->finalize = gibber_bytestream_muc_finalize;

  object_class->get_property = gibber_bytestream_muc_get_property;
  object_class->set_property = gibber_bytestream_muc_set_property;
  object_class->constructor = gibber_bytestream_muc_constructor;

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

  signals[DATA_RECEIVED] =
    g_signal_new ("data-received",
                  G_OBJECT_CLASS_TYPE (gibber_bytestream_muc_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  _gibber_signals_marshal_VOID__STRING_POINTER,
                  G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_POINTER);

  signals[STATE_CHANGED] =
    g_signal_new ("state-changed",
                  G_OBJECT_CLASS_TYPE (gibber_bytestream_muc_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);
}

/*
 * gibber_bytestream_muc_send
 *
 * Implements gibber_bytestream_iface_send on GibberBytestreamIface
 */
static gboolean
gibber_bytestream_muc_send (GibberBytestreamIface *bytestream,
                            guint len,
                            const gchar *str)
{
  GibberBytestreamMuc *self = GIBBER_BYTESTREAM_MUC (bytestream);
  GibberBytestreamMucPrivate *priv = GIBBER_BYTESTREAM_MUC_GET_PRIVATE (self);
  GError *error = NULL;

  if (!check_stream_id (self))
    {
      return FALSE;
    }

  if (!gibber_muc_connection_send_raw (priv->muc_connection,
        priv->stream_id_multicast, (const guint8 *) str, len, &error))
    {
      DEBUG ("send failed: %s", error->message);
      g_error_free (error);
      return FALSE;
    }

  return TRUE;
}

/*
 * gibber_bytestream_muc_accept
 *
 * Implements gibber_bytestream_iface_accept on GibberBytestreamIface
 */
static void
gibber_bytestream_muc_accept (GibberBytestreamIface *bytestream,
                              GibberBytestreamAugmentSiAcceptReply func,
                              gpointer user_data)
{
  /* Don't have to accept a muc bytestream */
}

/*
 * gibber_bytestream_muc_close
 *
 * Implements gibber_bytestream_iface_close on GibberBytestreamIface
 */
static void
gibber_bytestream_muc_close (GibberBytestreamIface *bytestream,
                             GError *error)
{
  GibberBytestreamMuc *self = GIBBER_BYTESTREAM_MUC (bytestream);
  GibberBytestreamMucPrivate *priv = GIBBER_BYTESTREAM_MUC_GET_PRIVATE (self);

  if (priv->state == GIBBER_BYTESTREAM_STATE_CLOSED)
     /* bytestream already closed, do nothing */
     return;

  g_object_set (self, "state", GIBBER_BYTESTREAM_STATE_CLOSED, NULL);

  if (priv->stream_id_multicast != 0)
    {
      gibber_muc_connection_free_stream (priv->muc_connection,
          priv->stream_id_multicast);
    }
}

/*
 * gibber_bytestream_muc_initiate
 *
 * Implements gibber_bytestream_iface_initiate on GibberBytestreamIface
 */
gboolean
gibber_bytestream_muc_initiate (GibberBytestreamIface *bytestream)
{
  /* Nothing to do */
  return TRUE;
}

/*
 * gibber_bytestream_muc_get_protocol
 *
 * Implements gibber_bytestream_iface_get_protocol on GibberBytestreamIface
 */
static const gchar *
gibber_bytestream_muc_get_protocol (GibberBytestreamIface *bytestream)
{
  return "rmulticast";
}

static void
bytestream_iface_init (gpointer g_iface,
                       gpointer iface_data)
{
  GibberBytestreamIfaceClass *klass = (GibberBytestreamIfaceClass *) g_iface;

  klass->initiate = gibber_bytestream_muc_initiate;
  klass->send = gibber_bytestream_muc_send;
  klass->close = gibber_bytestream_muc_close;
  klass->accept = gibber_bytestream_muc_accept;
}
