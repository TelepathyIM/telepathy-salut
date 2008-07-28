/*
 * gibber-bytestream-direct.c - Source for GibberBytestreamDirect
 * Copyright (C) 2008 Collabora Ltd.
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

#include "gibber-bytestream-direct.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#include "gibber-xmpp-connection.h"

#define DEBUG_FLAG DEBUG_BYTESTREAM
#include "gibber-debug.h"

#include "signals-marshal.h"

static void
bytestream_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (GibberBytestreamDirect, gibber_bytestream_direct,
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
  PROP_SELF_ID = 1,
  PROP_PEER_ID,
  PROP_STREAM_ID,
  PROP_STREAM_INIT_ID,
  PROP_STATE,
  PROP_PROTOCOL,
  LAST_PROPERTY
};

typedef struct _GibberBytestreamDirectPrivate GibberBytestreamDirectPrivate;
struct _GibberBytestreamDirectPrivate
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

#define GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE(obj) \
    ((GibberBytestreamDirectPrivate *) obj->priv)

static void
gibber_bytestream_direct_init (GibberBytestreamDirect *self)
{
  GibberBytestreamDirectPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GIBBER_TYPE_BYTESTREAM_DIRECT, GibberBytestreamDirectPrivate);

  self->priv = priv;
}

static void
gibber_bytestream_direct_dispose (GObject *object)
{
  GibberBytestreamDirect *self = GIBBER_BYTESTREAM_DIRECT (object);
  GibberBytestreamDirectPrivate *priv = GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;
  priv->dispose_has_run = TRUE;

  if (priv->state != GIBBER_BYTESTREAM_STATE_CLOSED)
    {
      gibber_bytestream_iface_close (GIBBER_BYTESTREAM_IFACE (self), NULL);
    }

  G_OBJECT_CLASS (gibber_bytestream_direct_parent_class)->dispose (object);
}

static void
gibber_bytestream_direct_finalize (GObject *object)
{
  GibberBytestreamDirect *self = GIBBER_BYTESTREAM_DIRECT (object);
  GibberBytestreamDirectPrivate *priv = GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE (self);

  g_free (priv->stream_id);
  g_free (priv->stream_init_id);
  g_free (priv->self_id);
  g_free (priv->peer_id);

  G_OBJECT_CLASS (gibber_bytestream_direct_parent_class)->finalize (object);
}

static void
gibber_bytestream_direct_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
  GibberBytestreamDirect *self = GIBBER_BYTESTREAM_DIRECT (object);
  GibberBytestreamDirectPrivate *priv = GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE (self);

  switch (property_id)
    {
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
        g_value_set_string (value, (const gchar *)"");
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gibber_bytestream_direct_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  GibberBytestreamDirect *self = GIBBER_BYTESTREAM_DIRECT (object);
  GibberBytestreamDirectPrivate *priv = GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE (self);

  switch (property_id)
    {
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
gibber_bytestream_direct_constructor (GType type,
                                   guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj;
  GibberBytestreamDirectPrivate *priv;

  obj = G_OBJECT_CLASS (gibber_bytestream_direct_parent_class)->
           constructor (type, n_props, props);

  priv = GIBBER_BYTESTREAM_DIRECT_GET_PRIVATE (GIBBER_BYTESTREAM_DIRECT (obj));

  g_assert (priv->stream_init_id != NULL);
  g_assert (priv->self_id != NULL);
  g_assert (priv->peer_id != NULL);

  return obj;
}

static void
gibber_bytestream_direct_class_init (
    GibberBytestreamDirectClass *gibber_bytestream_direct_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_bytestream_direct_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gibber_bytestream_direct_class,
      sizeof (GibberBytestreamDirectPrivate));

  object_class->dispose = gibber_bytestream_direct_dispose;
  object_class->finalize = gibber_bytestream_direct_finalize;

  object_class->get_property = gibber_bytestream_direct_get_property;
  object_class->set_property = gibber_bytestream_direct_set_property;
  object_class->constructor = gibber_bytestream_direct_constructor;

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

  signals[DATA_RECEIVED] =
    g_signal_new ("data-received",
                  G_OBJECT_CLASS_TYPE (gibber_bytestream_direct_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  _gibber_signals_marshal_VOID__STRING_POINTER,
                  G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_POINTER);

  signals[STATE_CHANGED] =
    g_signal_new ("state-changed",
                  G_OBJECT_CLASS_TYPE (gibber_bytestream_direct_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);
}

/*
 * gibber_bytestream_direct_send
 *
 * Implements gibber_bytestream_iface_send on GibberBytestreamIface
 */
static gboolean
gibber_bytestream_direct_send (GibberBytestreamIface *bytestream,
                            guint len,
                            const gchar *str)
{
  return TRUE;
}


/*
 * gibber_bytestream_direct_accept
 *
 * Implements gibber_bytestream_iface_accept on GibberBytestreamIface
 */
static void
gibber_bytestream_direct_accept (GibberBytestreamIface *bytestream,
                              GibberBytestreamAugmentSiAcceptReply func,
                              gpointer user_data)
{
}

/*
 * gibber_bytestream_direct_close
 *
 * Implements gibber_bytestream_iface_close on GibberBytestreamIface
 */
static void
gibber_bytestream_direct_close (GibberBytestreamIface *bytestream,
                             GError *error)
{
}

/*
 * gibber_bytestream_direct_initiate
 *
 * Implements gibber_bytestream_iface_initiate on GibberBytestreamIface
 */
static gboolean
gibber_bytestream_direct_initiate (GibberBytestreamIface *bytestream)
{
  return FALSE;
}

static void
bytestream_iface_init (gpointer g_iface,
                       gpointer iface_data)
{
  GibberBytestreamIfaceClass *klass = (GibberBytestreamIfaceClass *) g_iface;

  klass->initiate = gibber_bytestream_direct_initiate;
  klass->send = gibber_bytestream_direct_send;
  klass->close = gibber_bytestream_direct_close;
  klass->accept = gibber_bytestream_direct_accept;
}
