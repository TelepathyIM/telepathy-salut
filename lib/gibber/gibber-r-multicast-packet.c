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

#define PACKET_VERSION 1

#define PACKET_PREFIX { 'C', 'l', 'i', 'q', 'u', 'e' }
#define PACKET_PREFIX_LENGTH 6

/* Our packet header is the prefix, version (1 byte), type (1 byte)
 * and sender id (4 bytes) */
#define PACKET_HEADER_SIZE (6 + PACKET_PREFIX_LENGTH)


static void gibber_r_multicast_packet_sender_info_free (
    GibberRMulticastPacketSenderInfo *sender_info);

G_DEFINE_TYPE(GibberRMulticastPacket, gibber_r_multicast_packet, G_TYPE_OBJECT)

GQuark gibber_r_multicast_packet_error_quark (void);

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
};

GQuark
gibber_r_multicast_packet_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string (
        "gibber_r_multicast_packet_error");

  return quark;
}


#define GIBBER_R_MULTICAST_PACKET_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_R_MULTICAST_PACKET, \
      GibberRMulticastPacketPrivate))

static void
gibber_r_multicast_packet_init (GibberRMulticastPacket *obj)
{
  GibberRMulticastPacket *self = GIBBER_R_MULTICAST_PACKET (obj);
  self->version = PACKET_VERSION;
  self->depends = g_array_new (FALSE, FALSE,
      sizeof (GibberRMulticastPacketSenderInfo *));
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
  GibberRMulticastPacketPrivate *priv =
      GIBBER_R_MULTICAST_PACKET_GET_PRIVATE (self);

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
  guint i;

  for (i = 0; i < self->depends->len ; i++) {
    gibber_r_multicast_packet_sender_info_free (
        g_array_index (self->depends, GibberRMulticastPacketSenderInfo *, i));
  }
  g_array_free (self->depends, TRUE);

  /* free any data held directly by the object here */
  switch (self->type) {
    case PACKET_TYPE_WHOIS_REPLY:
      g_free (self->data.whois_reply.sender_name);
      break;
    case PACKET_TYPE_DATA:
      g_free (self->data.data.payload);
      break;
    case PACKET_TYPE_ATTEMPT_JOIN:
      g_array_free (self->data.attempt_join.senders, TRUE);
      break;
    case PACKET_TYPE_JOIN:
      g_array_free (self->data.join.failures, TRUE);
      break;
    case PACKET_TYPE_FAILURE:
      g_array_free (self->data.failure.failures, TRUE);
      break;
    default:
      /* Nothing specific to free */;
  }
  g_free (priv->data);

  G_OBJECT_CLASS (gibber_r_multicast_packet_parent_class)->finalize (object);
}

static GibberRMulticastPacketSenderInfo *
gibber_r_multicast_packet_sender_info_new (guint32 sender_id,
   guint32 expected_packet)
{
  GibberRMulticastPacketSenderInfo *result
      = g_slice_new (GibberRMulticastPacketSenderInfo);
  result->sender_id = sender_id;
  result->packet_id = expected_packet;

  return result;
}

static void
gibber_r_multicast_packet_sender_info_free (
    GibberRMulticastPacketSenderInfo *sender_info)
{
  g_slice_free (GibberRMulticastPacketSenderInfo, sender_info);
}

/* Start a new packet */
GibberRMulticastPacket *
gibber_r_multicast_packet_new (GibberRMulticastPacketType type,
    guint32 sender, gsize max_size)
{
  GibberRMulticastPacket *result =
      g_object_new (GIBBER_TYPE_R_MULTICAST_PACKET, NULL);
  GibberRMulticastPacketPrivate *priv =
      GIBBER_R_MULTICAST_PACKET_GET_PRIVATE(result);

  /* Fixme do this using properties */
  result->type = type;
  result->sender = sender;

  priv->max_data = max_size;

  switch (result->type) {
    case PACKET_TYPE_ATTEMPT_JOIN:
      result->data.attempt_join.senders = g_array_new (FALSE, FALSE,
          sizeof (guint32));
      break;
    case PACKET_TYPE_JOIN:
      result->data.join.failures = g_array_new (FALSE, FALSE,
          sizeof (guint32));
      break;
    case PACKET_TYPE_FAILURE:
      result->data.failure.failures = g_array_new (FALSE, FALSE,
          sizeof (guint32));
      break;
    default:
      break;
  }

  return result;
}

gboolean
gibber_r_multicast_packet_add_sender_info (GibberRMulticastPacket *packet,
    guint32 sender_id, guint32 packet_id, GError **error)
{
  GibberRMulticastPacketSenderInfo *s =
      gibber_r_multicast_packet_sender_info_new (sender_id, packet_id);
  GibberRMulticastPacketPrivate *priv =
      GIBBER_R_MULTICAST_PACKET_GET_PRIVATE (packet);

  g_assert (priv->data == NULL);
  g_assert (GIBBER_R_MULTICAST_PACKET_IS_RELIABLE_PACKET (packet)
      || packet->type == PACKET_TYPE_SESSION);

  g_array_append_val (packet->depends, s);

  return TRUE;
}

void

gibber_r_multicast_packet_set_packet_id (GibberRMulticastPacket *packet,
   guint32 packet_id)
{
  g_assert (GIBBER_R_MULTICAST_PACKET_IS_RELIABLE_PACKET(packet));
  packet->packet_id = packet_id;
}

void
gibber_r_multicast_packet_set_data_info (GibberRMulticastPacket *packet,
    guint16 stream_id, guint8 flags, guint32 size)
{
  g_assert (packet->type == PACKET_TYPE_DATA);

  packet->data.data.flags = flags;
  packet->data.data.total_size = size;
  packet->data.data.stream_id = stream_id;
}

void
gibber_r_multicast_packet_set_repair_request_info (
     GibberRMulticastPacket *packet, guint32 sender_id, guint32 packet_id)
{
  g_assert (packet->type == PACKET_TYPE_REPAIR_REQUEST);

  packet->data.repair_request.packet_id = packet_id;
  packet->data.repair_request.sender_id = sender_id;
}

void
gibber_r_multicast_packet_set_whois_request_info (
    GibberRMulticastPacket *packet,
    guint32 sender_id)
{
  g_assert (packet->type == PACKET_TYPE_WHOIS_REQUEST);

  packet->data.whois_request.sender_id = sender_id;
}

void
gibber_r_multicast_packet_set_whois_reply_info (GibberRMulticastPacket *packet,
   const gchar *name)
{
  g_assert (packet->type == PACKET_TYPE_WHOIS_REPLY);

  packet->data.whois_reply.sender_name = g_strdup (name);
}


static gsize
gibber_r_multicast_packet_calculate_size (GibberRMulticastPacket *packet)
{

    /* 8 bit type, 8 bit version, 32 bit sender */
  gsize result = PACKET_HEADER_SIZE;

  if (GIBBER_R_MULTICAST_PACKET_IS_RELIABLE_PACKET (packet)) {
      /*  32 bit packet id, 8 bit nr sender info */
      result += 5;
      /* 32 bit sender id, 32 bit packet id */
      result += 8 * packet->depends->len;
      result += packet->data.data.payload_size;
  }

  switch (packet->type) {
    case PACKET_TYPE_WHOIS_REQUEST:
      /* 32 bit sender id */
      result += 4;
      break;
    case PACKET_TYPE_WHOIS_REPLY:
      g_assert (packet->data.whois_reply.sender_name != NULL);
      result += 1 + strlen (packet->data.whois_reply.sender_name);
      break;
    case PACKET_TYPE_DATA:
      /* 8 bit flags, 32 bit data size, 16 bit stream id */
      result += 7;
      break;
    case PACKET_TYPE_REPAIR_REQUEST:
      /* 32 bit packet id and 32 sender id*/
      result += 8;
      break;
    case PACKET_TYPE_ATTEMPT_JOIN:
      /* 8 bit nr of senders, 32 bit per sender */
      result += 1 + 4 * packet->data.attempt_join.senders->len;
      break;
    case PACKET_TYPE_JOIN:
      /* 8 bit nr of senders, 32 bit per failure */
      result += 1 + 4 * packet->data.join.failures->len;
      break;
    case PACKET_TYPE_FAILURE:
      /* 8 bit nr of senders, 32 bit per failure */
      result += 1 + 4 * packet->data.failure.failures->len;
      break;
    case PACKET_TYPE_SESSION:
         /* 8 bit nr sender info + N times 32 bit sender id, 32 bit packet id
          */
      result += 1 + 8 * packet->depends->len;
      break;
    default:
      /* Nothing to add */;
  }

  return result;
}

static void
add_guint8 (guint8 *data, gsize length, gsize *offset, guint8 i)
{
  g_assert (*offset + 1 <= length);
  *(data + *offset) = i;
  (*offset)++;
}

static guint8
get_guint8 (const guint8 *data, gsize length, gsize *offset)
{
  guint8 i;
  g_assert (*offset + 1 <= length);
  i = *(data + *offset);
  (*offset)++;
  return i;
}

static void
add_guint16 (guint8 *data, gsize length, gsize *offset, guint16 i)
{
  guint16 ni = htons (i);

  g_assert (*offset + 2 <= length);

  memcpy (data + *offset, &ni, 2);
  (*offset) += 2;
}

static guint16
get_guint16 (const guint8 *data, gsize length, gsize *offset)
{
  guint16 ni;
  g_assert (*offset + 2 <= length);

  memcpy (&ni, data + *offset, 2);
  (*offset) += 2;

  return ntohs (ni);
}

static void
add_guint32 (guint8 *data, gsize length, gsize *offset, guint32 i)
{
  guint32 ni = htonl (i);

  g_assert (*offset + 4 <= length);

  memcpy (data + *offset, &ni, 4);
  (*offset) += 4;
}

static guint32
get_guint32 (const guint8 *data, gsize length, gsize *offset)
{
  guint32 ni;

  g_assert (*offset + 4 <= length);

  memcpy (&ni, data + *offset, 4);
  (*offset) += 4;
  return ntohl (ni);
}

static void
add_string (guint8 *data, gsize length, gsize *offset, const gchar *str)
{
  gsize len = strlen (str);

  g_assert (len < G_MAXUINT8);
  add_guint8 (data, length, offset, len);

  g_assert (*offset + len <= length);
  memcpy (data + *offset, str, len);
  (*offset) += len;
}

static gchar *
get_string (const guint8 *data, gsize length, gsize *offset)
{
  gsize len;
  gchar *str;

  if (*offset + 1 > length)
    return NULL;

  len = get_guint8 (data, length, offset);

  if (*offset + len > length)
    return NULL;

  str = g_strndup ((gchar *)data + *offset, len);
  (*offset) += len;
  return str;
}

static void
add_sender_info (guint8 *data, gsize length, gsize *offset, GArray *senders)
{
  int i;

  add_guint8 (data, length, offset, senders->len);

  for (i = 0; i < senders->len; i++)
    {
      GibberRMulticastPacketSenderInfo *info =
          g_array_index (senders, GibberRMulticastPacketSenderInfo *, i);
      add_guint32 (data, length, offset, info->sender_id);
      add_guint32 (data, length, offset, info->packet_id);
    }
}

static gboolean
get_sender_info (guint8 *data, gsize length, gsize *offset, GArray *depends)
{
  guint8 nr_items;

  if (*offset + 1 > length)
    return FALSE;

  nr_items = get_guint8 (data, length, offset);

  for (; nr_items > 0; nr_items--)
    {
      GibberRMulticastPacketSenderInfo *sender_info;
      guint32 sender_id;
      guint32 packet_id;

      if (*offset + 8 > length)
        return FALSE;

      sender_id = get_guint32 (data, length, offset);
      packet_id = get_guint32 (data, length, offset);
      sender_info =
        gibber_r_multicast_packet_sender_info_new (sender_id, packet_id);
      g_array_append_val (depends, sender_info);
    }

  return TRUE;
}

static void
packet_add_prefix (guint8 *data, gsize length, gsize *offset)
{
  guint8 prefix[] = PACKET_PREFIX;

  g_assert (*offset + PACKET_PREFIX_LENGTH <= length);

  memcpy (data + *offset, prefix, PACKET_PREFIX_LENGTH);
  *offset += PACKET_PREFIX_LENGTH;
}

static void
gibber_r_multicast_packet_build (GibberRMulticastPacket *packet)
{
  GibberRMulticastPacketPrivate *priv =
     GIBBER_R_MULTICAST_PACKET_GET_PRIVATE (packet);
  gsize needed_size;

  if (priv->data != NULL)
    {
      /* Already serialized return cached version */
      return;
    }

  needed_size = gibber_r_multicast_packet_calculate_size (packet);

  g_assert (needed_size <= priv->max_data);

  /* Trim down the maximum data size to what we actually need */
  priv->max_data = needed_size;
  priv->data = g_malloc0 (priv->max_data);
  priv->size = 0;

  packet_add_prefix (priv->data, priv->max_data, &(priv->size));
  add_guint8 (priv->data, priv->max_data, &(priv->size), packet->version);
  add_guint8 (priv->data, priv->max_data, &(priv->size), packet->type);
  add_guint32 (priv->data, priv->max_data, &(priv->size), packet->sender);

  if (GIBBER_R_MULTICAST_PACKET_IS_RELIABLE_PACKET (packet))
    {
      /* Add common reliable packet data */
      add_guint32 (priv->data, priv->max_data, &(priv->size),
        packet->packet_id);
      add_sender_info (priv->data, priv->max_data, &(priv->size),
        packet->depends);
  }

  switch (packet->type) {
    case PACKET_TYPE_WHOIS_REQUEST:
      add_guint32 (priv->data, priv->max_data, &(priv->size),
          packet->data.whois_request.sender_id);
      break;
    case PACKET_TYPE_WHOIS_REPLY:
      add_string (priv->data, priv->max_data, &(priv->size),
          packet->data.whois_reply.sender_name);
      break;
    case PACKET_TYPE_DATA:
      add_guint8 (priv->data, priv->max_data, &(priv->size),
          packet->data.data.flags);
      add_guint16 (priv->data, priv->max_data, &(priv->size),
          packet->data.data.stream_id);
      add_guint32 (priv->data, priv->max_data, &(priv->size),
          packet->data.data.total_size);

      g_assert (priv->size + packet->data.data.payload_size == priv->max_data);

      memcpy (priv->data + priv->size, packet->data.data.payload,
          packet->data.data.payload_size);
      priv->size += packet->data.data.payload_size;
      break;
    case PACKET_TYPE_REPAIR_REQUEST:
      add_guint32 (priv->data, priv->max_data, &(priv->size),
            packet->data.repair_request.sender_id);
      add_guint32 (priv->data, priv->max_data, &(priv->size),
            packet->data.repair_request.packet_id);
      break;
    case PACKET_TYPE_ATTEMPT_JOIN: {
      int i;
      add_guint8 (priv->data, priv->max_data, &(priv->size),
            packet->data.attempt_join.senders->len);

      for (i = 0; i < packet->data.attempt_join.senders->len; i++) {
        add_guint32 (priv->data, priv->max_data, &(priv->size),
          g_array_index (packet->data.attempt_join.senders, guint32, i));
      }
      break;
    }
    case PACKET_TYPE_JOIN: {
      int i;
      add_guint8 (priv->data, priv->max_data, &(priv->size),
            packet->data.join.failures->len);

      for (i = 0; i < packet->data.join.failures->len; i++) {
        add_guint32 (priv->data, priv->max_data, &(priv->size),
          g_array_index (packet->data.join.failures, guint32, i));
      }
      break;
    }
    case PACKET_TYPE_FAILURE: {
      int i;
      add_guint8 (priv->data, priv->max_data, &(priv->size),
            packet->data.failure.failures->len);

      for (i = 0; i < packet->data.failure.failures->len; i++) {
        add_guint32 (priv->data, priv->max_data, &(priv->size),
          g_array_index (packet->data.failure.failures, guint32, i));
      }
      break;
    }
    case PACKET_TYPE_SESSION:
      add_sender_info (priv->data, priv->max_data, &(priv->size),
          packet->depends);
      break;
    case PACKET_TYPE_BYE:
      break;
    case PACKET_TYPE_NO_DATA:
      break;
    default:
      g_assert_not_reached ();
  }

  /* If this fails our size precalculation is buggy */
  g_assert (priv->size == priv->max_data);
}

gsize
gibber_r_multicast_packet_add_payload (GibberRMulticastPacket *packet,
  const guint8 *data, gsize size)
{
  GibberRMulticastPacketPrivate *priv =
     GIBBER_R_MULTICAST_PACKET_GET_PRIVATE (packet);
  gsize avail;

  g_assert (packet->type == PACKET_TYPE_DATA);
  g_assert (packet->data.data.payload == NULL);
  g_assert (priv->data == NULL);

  avail = MIN (size, priv->max_data -
    gibber_r_multicast_packet_calculate_size (packet));

  packet->data.data.payload = g_memdup (data, avail);
  packet->data.data.payload_size = avail;

  return avail;
}

static gboolean
packet_check_prefix (const guint8 *data)
{
  int i;
  guint8 prefix[] = PACKET_PREFIX;

  for (i = 0; i < PACKET_PREFIX_LENGTH; i++)
    if (data[i] != prefix[i])
      return FALSE;

  return TRUE;
}

#define GET_GUINT8(target) G_STMT_START {                             \
  if (priv->size + 1 > priv->max_data)                                \
    goto parse_error;                                                 \
  target = get_guint8 (priv->data, priv->max_data, &(priv->size));    \
} G_STMT_END

#define GET_GUINT16(target) G_STMT_START {                            \
  if (priv->size + 2 > priv->max_data)                                \
    goto parse_error;                                                 \
  target = get_guint16 (priv->data, priv->max_data, &(priv->size));   \
} G_STMT_END

#define GET_GUINT32(target) G_STMT_START {                            \
  if (priv->size + 4 > priv->max_data)                                \
    goto parse_error;                                                 \
  target = get_guint32 (priv->data, priv->max_data, &(priv->size));   \
} G_STMT_END

/* Create a packet by parsing raw data, packet is immutable afterwards */
GibberRMulticastPacket *
gibber_r_multicast_packet_parse (const guint8 *data, gsize size,
    GError **error)
{
  GibberRMulticastPacket *result = NULL;

  GibberRMulticastPacketPrivate *priv;

  if (size < PACKET_HEADER_SIZE || !packet_check_prefix (data))
    goto parse_error;

  result = g_object_new (GIBBER_TYPE_R_MULTICAST_PACKET, NULL);
  priv = GIBBER_R_MULTICAST_PACKET_GET_PRIVATE (result);

  priv->data = g_memdup (data, size);
  priv->size = PACKET_PREFIX_LENGTH;
  priv->max_data = size;

  GET_GUINT8 (result->version);
  if (result->version != PACKET_VERSION)
    goto parse_error;

  GET_GUINT8 (result->type);
  GET_GUINT32 (result->sender);


  if (GIBBER_R_MULTICAST_PACKET_IS_RELIABLE_PACKET (result))
    {
      GET_GUINT32 (result->packet_id);
      if (!get_sender_info (priv->data, priv->max_data, &(priv->size),
          result->depends))
        goto parse_error;
    }

  switch (result->type) {
    case PACKET_TYPE_WHOIS_REQUEST:
      GET_GUINT32 (result->data.whois_request.sender_id);
      break;
    case PACKET_TYPE_WHOIS_REPLY:
      result->data.whois_reply.sender_name = get_string (priv->data,
          priv->max_data, &(priv->size));
      if (result->data.whois_reply.sender_name == NULL)
        goto parse_error;
      break;
    case PACKET_TYPE_DATA:
      GET_GUINT8 (result->data.data.flags);
      GET_GUINT16 (result->data.data.stream_id);
      GET_GUINT32 (result->data.data.total_size);

      result->data.data.payload_size = priv->max_data - priv->size;
      result->data.data.payload = g_memdup (priv->data + priv->size,
          result->data.data.payload_size);
      priv->size += result->data.data.payload_size;
      break;
    case PACKET_TYPE_REPAIR_REQUEST:
      GET_GUINT32 (result->data.repair_request.sender_id);
      GET_GUINT32 (result->data.repair_request.packet_id);
      break;
    case PACKET_TYPE_ATTEMPT_JOIN:
      {
        guint8 nr;
        guint8 i;

        GET_GUINT8 (nr);

        result->data.attempt_join.senders = g_array_sized_new (FALSE, FALSE,
            sizeof (guint32), nr);

        for (i = 0; i < nr; i++)
          {
            guint32 sender;
            GET_GUINT32 (sender);
            gibber_r_multicast_packet_attempt_join_add_sender (result,
              sender, NULL);
          }
        break;
      }
    case PACKET_TYPE_JOIN:
      {
        guint8 nr;
        guint8 i;

        GET_GUINT8 (nr);

        result->data.join.failures = g_array_sized_new (FALSE, FALSE,
            sizeof (guint32), nr);

        for (i = 0; i < nr; i++) {
          guint32 failure;

          GET_GUINT32 (failure);
          gibber_r_multicast_packet_join_add_failure (result,
              failure, NULL);
        }
        break;
      }
    case PACKET_TYPE_FAILURE:
      {
        guint8 nr;
        guint8 i;

        GET_GUINT8 (nr);

        result->data.failure.failures = g_array_sized_new (FALSE, FALSE,
            sizeof (guint32), nr);

        for (i = 0; i < nr; i++)
          {
            guint32 sender;

            GET_GUINT32 (sender);
            gibber_r_multicast_packet_failure_add_sender (result,
                sender, NULL);
          }
        break;
      }
    case PACKET_TYPE_SESSION:
      if (!get_sender_info (priv->data, priv->max_data, &(priv->size),
          result->depends))
        goto parse_error;
      break;
    case PACKET_TYPE_NO_DATA:
    case PACKET_TYPE_BYE:
      break;
    default:
      goto parse_error;
  }

  if  (priv->size != priv->max_data)
    goto parse_error;

  return result;

parse_error:
  if (result != NULL)
    g_object_unref (result);

  g_set_error (error,
    GIBBER_R_MULTICAST_PACKET_ERROR,
    GIBBER_R_MULTICAST_PACKET_ERROR_PARSE_ERROR,
    "Failed to parse packet");

  return NULL;
}

/* Get the packets payload */
guint8 *
gibber_r_multicast_packet_get_payload (GibberRMulticastPacket *packet,
    gsize *size)
{
  g_assert (packet->type == PACKET_TYPE_DATA);
  g_assert (size != NULL);

  *size = packet->data.data.payload_size;

  return packet->data.data.payload;
}

/* Get the packets raw data, packet is immutable after this call */
guint8 *
gibber_r_multicast_packet_get_raw_data (GibberRMulticastPacket *packet,
    gsize *size)
{
  GibberRMulticastPacketPrivate *priv =
     GIBBER_R_MULTICAST_PACKET_GET_PRIVATE (packet);

 /* Ensure the packet is serialized */
 gibber_r_multicast_packet_build (packet);

 *size = priv->size;

 return priv->data;
}

gboolean
gibber_r_multicast_packet_attempt_join_add_sender (
   GibberRMulticastPacket *packet,
   guint32 sender,
   GError **error)
{
  g_assert (packet->type == PACKET_TYPE_ATTEMPT_JOIN);

  g_array_append_val (packet->data.attempt_join.senders, sender);

  return TRUE;
}

/* Add senders that have failed */
gboolean
gibber_r_multicast_packet_attempt_join_add_senders (
   GibberRMulticastPacket *packet, GArray *senders, GError **error)
{
  g_assert (packet->type == PACKET_TYPE_ATTEMPT_JOIN);

  g_array_append_vals (packet->data.attempt_join.senders, senders->data,
      senders->len);

  return TRUE;
}


gboolean
gibber_r_multicast_packet_join_add_failure (GibberRMulticastPacket *packet,
   guint32 failure, GError **error)
{
  g_assert (packet->type == PACKET_TYPE_JOIN);

  g_array_append_val (packet->data.join.failures, failure);

  return TRUE;
}

gboolean
gibber_r_multicast_packet_join_add_failures (GibberRMulticastPacket *packet,
   GArray *failures, GError **error)
{
  g_assert (packet->type == PACKET_TYPE_JOIN);

  g_array_append_vals (packet->data.join.failures, failures->data,
      failures->len);

  return TRUE;
}

gboolean
gibber_r_multicast_packet_failure_add_sender (GibberRMulticastPacket *packet,
   guint32 sender, GError **error)
{
  g_assert (packet->type == PACKET_TYPE_FAILURE);

  g_array_append_val (packet->data.failure.failures, sender);

  return TRUE;
}

gboolean
gibber_r_multicast_packet_failure_add_senders (
   GibberRMulticastPacket *packet, GArray *senders, GError **error)
{
  g_assert (packet->type == PACKET_TYPE_FAILURE);

  g_array_append_vals (packet->data.failure.failures, senders->data,
      senders->len);

  return TRUE;
}

gint32
gibber_r_multicast_packet_diff (guint32 from, guint32 to)
{
  if (from > (G_MAXUINT32 - 0xffff) && to < 0xffff)
    return G_MAXUINT32 - from + to + 1;

  if (to > (G_MAXUINT32 - 0xffff) && from < 0xffff)
    return - from - (G_MAXUINT32 - to) - 1;

  if (from > to)
    return -MIN(from - to, G_MAXINT);

  return MIN(to - from, G_MAXINT);
}
