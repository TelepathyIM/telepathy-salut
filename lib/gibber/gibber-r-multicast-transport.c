/*
 * gibber-r-multicast-transport.c - Source for GibberRMulticastTransport
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

#define DEBUG_FLAG DEBUG_RMULTICAST
#include "gibber-debug.h"

#include "gibber-r-multicast-transport.h"
#include "gibber-r-multicast-packet.h"
#include "gibber-r-multicast-sender.h"

#define DEBUG_TRANSPORT(format,...) \
  DEBUG("group %s (%p): " format, priv->name, priv, ##__VA_ARGS__)

G_DEFINE_TYPE(GibberRMulticastTransport, gibber_r_multicast_transport,
              GIBBER_TYPE_TRANSPORT)

/* signal enum */
enum
{
    NEW_SENDER,
    LOST_SENDER,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum {
  PROP_NAME = 1,
  PROP_TRANSPORT,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GibberRMulticastTransportPrivate
    GibberRMulticastTransportPrivate;

struct _GibberRMulticastTransportPrivate
{
  gboolean dispose_has_run;
  GibberRMulticastCausalTransport *transport;
  GHashTable *members;
};

#define GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_R_MULTICAST_TRANSPORT, \
   GibberRMulticastTransportPrivate))

static void
gibber_r_multicast_transport_set_property (GObject *object,
                                           guint property_id,
                                           const GValue *value,
                                           GParamSpec *pspec) {
  GibberRMulticastTransport *transport = GIBBER_R_MULTICAST_TRANSPORT(object);
  GibberRMulticastTransportPrivate *priv =
      GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE(transport);
  switch (property_id) {
    case PROP_TRANSPORT:
      priv->transport = g_object_ref(g_value_get_object(value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gibber_r_multicast_transport_get_property (GObject *object,
                                           guint property_id,
                                           GValue *value,
                                           GParamSpec *pspec) {
  GibberRMulticastTransport *transport = GIBBER_R_MULTICAST_TRANSPORT(object);
  GibberRMulticastTransportPrivate *priv =
      GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE(transport);
  switch (property_id) {
    case PROP_TRANSPORT:
      g_value_set_object(value, priv->transport);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


static void
gibber_r_multicast_transport_init (GibberRMulticastTransport *obj)
{
  GibberRMulticastTransportPrivate *priv =
      GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (obj);

  priv->members = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void gibber_r_multicast_transport_dispose (GObject *object);
static void gibber_r_multicast_transport_finalize (GObject *object);

static gboolean
gibber_r_multicast_transport_do_send(GibberTransport *transport,
                                     const guint8 *data, gsize size,
                                     GError **error);
static void
gibber_r_multicast_transport_disconnect(GibberTransport *transport);

static void
gibber_r_multicast_transport_class_init (
    GibberRMulticastTransportClass *gibber_r_multicast_transport_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (
      gibber_r_multicast_transport_class);
  GibberTransportClass *transport_class =
          GIBBER_TRANSPORT_CLASS(gibber_r_multicast_transport_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gibber_r_multicast_transport_class,
      sizeof (GibberRMulticastTransportPrivate));

  object_class->dispose = gibber_r_multicast_transport_dispose;
  object_class->finalize = gibber_r_multicast_transport_finalize;

  signals[NEW_SENDER] =
    g_signal_new("new-sender",
                 G_OBJECT_CLASS_TYPE(gibber_r_multicast_transport_class),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__STRING,
                 G_TYPE_NONE, 1, G_TYPE_STRING);

  signals[LOST_SENDER] =
    g_signal_new("lost-sender",
                 G_OBJECT_CLASS_TYPE(gibber_r_multicast_transport_class),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__STRING,
                 G_TYPE_NONE, 1, G_TYPE_STRING);

  object_class->set_property = gibber_r_multicast_transport_set_property;
  object_class->get_property = gibber_r_multicast_transport_get_property;

  param_spec = g_param_spec_object("transport",
                                   "transport",
                                   "The underlying Transport",
                                   GIBBER_TYPE_TRANSPORT,
                                   G_PARAM_CONSTRUCT_ONLY |
                                   G_PARAM_READWRITE      |
                                   G_PARAM_STATIC_NAME    |
                                   G_PARAM_STATIC_BLURB);
  g_object_class_install_property(object_class, PROP_TRANSPORT,
                                  param_spec);

  transport_class->send = gibber_r_multicast_transport_do_send;
  transport_class->disconnect = gibber_r_multicast_transport_disconnect;
}

void
gibber_r_multicast_transport_dispose (GObject *object)
{
  GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (object);
  GibberRMulticastTransportPrivate *priv =
      GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;
  if (priv->transport != NULL) {
    g_object_unref(priv->transport);
    priv->transport = NULL;
  }

  /* release any references held by the object here */
  if (G_OBJECT_CLASS (gibber_r_multicast_transport_parent_class)->dispose)
    G_OBJECT_CLASS (gibber_r_multicast_transport_parent_class)->dispose (
        object);
}

void
gibber_r_multicast_transport_finalize (GObject *object)
{
   GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (object);
     GibberRMulticastTransportPrivate *priv =
        GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);

  g_hash_table_destroy (priv->members);

  G_OBJECT_CLASS (
      gibber_r_multicast_transport_parent_class)->finalize (object);
}

static void
received_data (GibberTransport *transport, GibberBuffer *buffer,
    gpointer user_data)
{
  GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (user_data);
  GibberRMulticastTransportPrivate *priv =
        GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);
  GibberRMulticastCausalBuffer *cbuffer =
    (GibberRMulticastCausalBuffer *)buffer;

  /* Temporary hack to send NEW_SENDER on time, will be obsoleted by the new
   * join algorithm */
  if (!g_hash_table_lookup (priv->members, cbuffer->sender)) {
    g_hash_table_insert (priv->members, g_strdup (cbuffer->sender),
      GINT_TO_POINTER(TRUE));
    g_signal_emit (self, signals[NEW_SENDER], 0, cbuffer->sender);
  }

  gibber_transport_received_data_custom (GIBBER_TRANSPORT (user_data), buffer);
}

GibberRMulticastTransport *
gibber_r_multicast_transport_new(GibberRMulticastCausalTransport *transport)
{
  GibberRMulticastTransport *result;

  result =  g_object_new(GIBBER_TYPE_R_MULTICAST_TRANSPORT,
                      "transport", transport,
                      NULL);

  gibber_transport_set_handler(GIBBER_TRANSPORT(transport),
      received_data, result);

  return result;
}

static void
received_foreign_packet_cb (GibberRMulticastCausalTransport *ctransport,
    GibberRMulticastPacket *packet, gpointer user_data) {
  GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (user_data);
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);
  GList *l;

  printf ("Received foreign packet from %x, type %d\n",
    packet->sender, packet->type);

  gibber_r_multicast_causal_transport_add_sender (priv->transport,
      packet->sender);

  if (IS_RELIABLE_PACKET (packet)) {
    gibber_r_multicast_causal_transport_set_sender_start (priv->transport,
      packet->sender, packet->packet_id);
    for (l = packet->depends; l != 0; l = g_list_next (l)) {
      GibberRMulticastPacketSenderInfo *info =
        (GibberRMulticastPacketSenderInfo *)l->data;
      gibber_r_multicast_causal_transport_add_sender (priv->transport,
        info->sender_id);
      gibber_r_multicast_causal_transport_set_sender_start (priv->transport,
        info->sender_id, info->packet_id);
    }
  }
}

gboolean
gibber_r_multicast_transport_connect(GibberRMulticastTransport *transport,
    GError **error)
{
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (transport);

  gibber_transport_set_state(GIBBER_TRANSPORT(transport),
         GIBBER_TRANSPORT_CONNECTED);

  g_signal_connect (priv->transport, "received-foreign-packet",
      G_CALLBACK (received_foreign_packet_cb), transport);

  return TRUE;
}

gboolean
gibber_r_multicast_transport_send(GibberRMulticastTransport *transport,
                                  guint8 stream_id,
                                  const guint8 *data,
                                  gsize size,
                                  GError **error) {
  GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (transport);
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);

  return gibber_r_multicast_causal_transport_send(priv->transport,
     stream_id, data, size, error);
}

static gboolean
gibber_r_multicast_transport_do_send(GibberTransport *transport,
                                     const guint8 *data,
                                     gsize size,
                                     GError **error)
{
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (transport);

  return gibber_transport_send(GIBBER_TRANSPORT(priv->transport),
      data, size, error);
}

static void
gibber_r_multicast_transport_disconnect(GibberTransport *transport) {
  GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (transport);
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);

  gibber_transport_set_state(GIBBER_TRANSPORT(self),
                             GIBBER_TRANSPORT_DISCONNECTED);
  gibber_transport_disconnect(GIBBER_TRANSPORT(priv->transport));
}
