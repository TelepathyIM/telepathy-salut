/*
 * gibber-r-multicast-packet.c - Source for GibberRMulticastPacket
 * Copyright (C) 2007 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
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

#include <arpa/inet.h>

#include "gibber-r-multicast-packet.h"

static void
gibber_r_multicast_receiver_free(GibberRMulticastReceiver *receiver);

G_DEFINE_TYPE(GibberRMulticastPacket, gibber_r_multicast_packet, G_TYPE_OBJECT)

/* private structure */
typedef struct _GibberRMulticastPacketPrivate GibberRMulticastPacketPrivate;

struct _GibberRMulticastPacketPrivate
{
  gboolean dispose_has_run;
  /* Actually needed data size untill this point */
  gsize size;

  guint8 *data;
  /* Maximum data size */
  gsize max_data;

  guint8 *payload;
};

#define GIBBER_R_MULTICAST_PACKET_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_R_MULTICAST_PACKET, \
      GibberRMulticastPacketPrivate))

static void
gibber_r_multicast_packet_init (GibberRMulticastPacket *obj)
{
  /* allocate any data required by the object here */
}

static void gibber_r_multicast_packet_dispose (GObject *object);
static void gibber_r_multicast_packet_finalize (GObject *object);

static void
gibber_r_multicast_packet_class_init (GibberRMulticastPacketClass *gibber_r_multicast_packet_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_r_multicast_packet_class);

  g_type_class_add_private (gibber_r_multicast_packet_class, sizeof (GibberRMulticastPacketPrivate));

  object_class->dispose = gibber_r_multicast_packet_dispose;
  object_class->finalize = gibber_r_multicast_packet_finalize;

}

void
gibber_r_multicast_packet_dispose (GObject *object)
{
  GibberRMulticastPacket *self = GIBBER_R_MULTICAST_PACKET (object);
  GibberRMulticastPacketPrivate *priv = GIBBER_R_MULTICAST_PACKET_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gibber_r_multicast_packet_parent_class)->dispose)
    G_OBJECT_CLASS (gibber_r_multicast_packet_parent_class)->dispose (object);
}

void
gibber_r_multicast_packet_finalize (GObject *object)
{
  GibberRMulticastPacket *self = GIBBER_R_MULTICAST_PACKET (object);
  GibberRMulticastPacketPrivate *priv = 
      GIBBER_R_MULTICAST_PACKET_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  g_free(self->sender);
  g_list_foreach(self->receivers, (GFunc)gibber_r_multicast_receiver_free, 
     NULL);
  g_list_free(self->receivers);
  g_free(priv->data);

  G_OBJECT_CLASS (gibber_r_multicast_packet_parent_class)->finalize (object);
}

static GibberRMulticastReceiver *
gibber_r_multicast_receiver_new(gchar *id, guint32 expected_packet) {
  GibberRMulticastReceiver *result = g_slice_new(GibberRMulticastReceiver);
  result->id = id;
  result->expected_packet = expected_packet;

  return result;
}

static void
gibber_r_multicast_receiver_free(GibberRMulticastReceiver *receiver) {
  g_free(receiver->id);
  g_slice_free(GibberRMulticastReceiver, receiver);
}

/* Start a new packet */
GibberRMulticastPacket *
gibber_r_multicast_packet_new(GibberRMulticastPacketType type,
                              const gchar *sender, guint32 packet_id,
                              gsize max_size) {
  GibberRMulticastPacket *result = g_object_new(GIBBER_TYPE_R_MULTICAST_PACKET,
                                                NULL);
  GibberRMulticastPacketPrivate *priv = 
      GIBBER_R_MULTICAST_PACKET_GET_PRIVATE(result);

  /* Fixme do this using properties */
  result->type = type;
  result->sender = g_strdup(sender);
  result->packet_id = packet_id;

  priv->max_data = max_size;

  return result;
}

gboolean
gibber_r_multicast_packet_add_receiver(GibberRMulticastPacket *packet,
                                       const gchar *receiver, 
                                       guint32 next_packet, 
                                       GError **error) {
  GibberRMulticastReceiver *r = 
      gibber_r_multicast_receiver_new(g_strdup(receiver), next_packet);

  packet->receivers = g_list_append(packet->receivers, r);
  return TRUE;
}

void
gibber_r_multicast_packet_set_part(GibberRMulticastPacket *packet,
                                   guint8 part, guint8 total) {
  g_assert(part < total);

  packet->packet_part = part;
  packet->packet_total = total;
}

static gsize
gibber_r_multicast_packet_calculate_size(GibberRMulticastPacket *packet,
                                         gsize payload_size, gsize max_size) {
  GList *l;
  gsize result = 10; /* 8 bit type, 8 bit version, 8 bit part, 8 bit total, 
                       32 bit identifier, 8 bit sender length, 
                       8 bit nr receivers */
  result += strlen(packet->sender);
  for (l = packet->receivers; l != NULL; l = g_list_next(l)) {
    GibberRMulticastReceiver *r;
    r = (GibberRMulticastReceiver *)l->data;
    result += 5 + strlen(r->id); /* 32 bit expect packet, 8 bit length */
  }

  g_assert(result < max_size);

  return MIN(result + payload_size, max_size);
}

static void
add_guint8(GibberRMulticastPacketPrivate *p, guint8 i) {
  g_assert(p->size + 1 <= p->max_data);
  *(p->data + p->size) = i;
  p->size++;
}

static guint8
get_guint8(GibberRMulticastPacketPrivate *p) {
  guint8 i;
  g_assert(p->size + 1 <= p->max_data);
  i = *(p->data + p->size);
  p->size++;
  return i;
}

static void
add_guint32(GibberRMulticastPacketPrivate *p, guint32 i) {
  guint32 ni = htonl(i);

  g_assert(p->size + 4 <= p->max_data);

  memcpy(p->data + p->size, &ni, 4);
  p->size += 4;
}

static guint32
get_guint32(GibberRMulticastPacketPrivate *p) {
  guint32 ni;

  g_assert(p->size + 4 <= p->max_data);

  memcpy(&ni, p->data + p->size, 4);
  p->size += 4;
  return ntohl(ni);
}

static void
add_string(GibberRMulticastPacketPrivate *p, const gchar *str) {
  gsize len = strlen(str);

  g_assert(len < G_MAXUINT8);
  add_guint8(p, len);

  g_assert(p->size + len <= p->max_data);
  memcpy(p->data + p->size, str, len);
  p->size += len;
}

static gchar *
get_string(GibberRMulticastPacketPrivate *p) {
  gsize len;
  gchar *str;

  len = get_guint8(p);

  g_assert(p->size + len <= p->max_data);

  str = g_strndup((gchar *)p->data + p->size, len);
  p->size += len;
  return str;
}

static void
gibber_r_multicast_packet_build(GibberRMulticastPacket *packet,
                                guint8 *payload, gsize payload_size) {
  /* FIXME build nicer packets when the payload fit in anymore */
  GibberRMulticastPacketPrivate *priv =
     GIBBER_R_MULTICAST_PACKET_GET_PRIVATE (packet);
  GList *l;

  g_assert(payload == NULL || priv->data == NULL);

  if (priv->data != NULL) {
    /* Already serialized return cached version */
    return;
  }

  priv->max_data =
      gibber_r_multicast_packet_calculate_size(packet, payload_size,
          priv->max_data);

  priv->data = g_malloc0(priv->max_data);
  priv->size = 0;

  add_guint8(priv, packet->type);
  add_guint8(priv, packet->version);
  add_guint8(priv, packet->packet_part);
  add_guint8(priv, packet->packet_total);
  add_guint32(priv, packet->packet_id);
  add_string(priv, packet->sender);
  add_guint8(priv, g_list_length(packet->receivers));

  for (l = packet->receivers; l != NULL; l = g_list_next(l)) {
    GibberRMulticastReceiver *r;
    r = (GibberRMulticastReceiver *)l->data;

    add_string(priv, r->id);
    add_guint32(priv, r->expected_packet);
  }

  priv->payload = priv->data + priv->size;

  if (payload != NULL) {
    gsize len;

    len = priv->max_data - priv->size;

    g_assert(len  <= payload_size);

    memcpy(priv->data + priv->size, payload, len);
    priv->size += len;
  }
}

/* Add the actual payload. Should be done as the last step, packet is immutable
 * afterwards */
gsize
gibber_r_multicast_packet_add_payload(GibberRMulticastPacket *packet,
                                      guint8 *data, gsize size) {
  GibberRMulticastPacketPrivate *priv =
     GIBBER_R_MULTICAST_PACKET_GET_PRIVATE (packet);

  g_assert(priv->data == NULL);
  gibber_r_multicast_packet_build(packet, data, size);

  return priv->size - (priv->payload - priv->data);
}

/* Create a packet by parsing raw data, packet is immutable afterwards */
GibberRMulticastPacket *
gibber_r_multicast_packet_parse(guint8 *data, gsize size, GError **error) {
  GibberRMulticastPacket *result = g_object_new(GIBBER_TYPE_R_MULTICAST_PACKET,
                                                NULL);
  GibberRMulticastPacketPrivate *priv = 
      GIBBER_R_MULTICAST_PACKET_GET_PRIVATE(result);
  guint8 receivers;

  priv->data = g_memdup(data, size);
  priv->size = 0;
  priv->max_data = size;

  result->type         = get_guint8(priv);
  result->version      = get_guint8(priv);
  result->packet_part  = get_guint8(priv);
  result->packet_total = get_guint8(priv);
  result->packet_id    = get_guint32(priv);
  result->sender       = get_string(priv);


  for (receivers = get_guint8(priv); receivers > 0; receivers--) {
    GibberRMulticastReceiver *r;
    gchar *str;
    guint32 expected_packet;
    str = get_string(priv);
    expected_packet = get_guint32(priv);
    r = gibber_r_multicast_receiver_new(str, expected_packet);
    result->receivers = g_list_append(result->receivers, r);
  }

  priv->payload = priv->data + priv->size;
  priv->size = priv->max_data;
  return result;
}

/* Get the packets payload */
guint8 *
gibber_r_multicast_packet_get_payload(GibberRMulticastPacket *packet,
                                      gsize *size) {
  GibberRMulticastPacketPrivate *priv =
     GIBBER_R_MULTICAST_PACKET_GET_PRIVATE (packet);
  g_assert(priv->data != NULL);

  *size = priv->size - (priv->payload - priv->data);

  return priv->payload;
}

/* Get the packets raw data, packet is immutable after this call */
guint8 *
gibber_r_multicast_packet_get_raw_data(GibberRMulticastPacket *packet,
                                       gsize *size) {
  GibberRMulticastPacketPrivate *priv = 
     GIBBER_R_MULTICAST_PACKET_GET_PRIVATE (packet);

 /* Ensure the packet is serialized */
 gibber_r_multicast_packet_build(packet, NULL, 0);

 *size = priv->size;

 return priv->data; 
}
