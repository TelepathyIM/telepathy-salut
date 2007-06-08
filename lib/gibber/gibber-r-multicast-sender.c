/*
 * gibber-r-multicast-sender.c - Source for GibberRMulticastSender
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

#include "gibber-r-multicast-sender.h"
#include "gibber-r-multicast-sender-signals-marshal.h"

#define DEBUG_FLAG DEBUG_RMULTICAST_SENDER
#include "gibber-debug.h"

#define DEBUG_SENDER(sender, format, ...) \
  DEBUG("%s (%p): " format, sender->name, sender, ##__VA_ARGS__)

#define PACKET_CACHE_SIZE 256

#define MIN_DO_REPAIR_TIMEOUT 50
#define MAX_DO_REPAIR_TIMEOUT 100

#define MIN_REPAIR_TIMEOUT 150
#define MAX_REPAIR_TIMEOUT 250

static void schedule_repair(GibberRMulticastSender *sender, guint32 id);
static void schedule_do_repair(GibberRMulticastSender *sender, guint32 id);

G_DEFINE_TYPE(GibberRMulticastSender, gibber_r_multicast_sender, G_TYPE_OBJECT)

/* signal enum */
enum
{
    REPAIR_REQUEST,
    REPAIR_MESSAGE,
    DATA_RECEIVED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

typedef struct {
  guint32 packet_id;
  guint timeout;
  GibberRMulticastPacket *packet;
  GibberRMulticastSender *sender;
} PacketInfo;

static void
packet_info_free(gpointer data) {
  PacketInfo *p = (PacketInfo *)data;
  if (p->packet != NULL) {
    g_object_unref(p->packet);
  }

  if (p->timeout != 0) {
    g_source_remove(p->timeout);
  }
  g_slice_free(PacketInfo, data);
}

static PacketInfo *
packet_info_new(GibberRMulticastSender*sender, guint32 packet_id) {
  PacketInfo *result; 
  result = g_slice_new0(PacketInfo);
  result->packet_id = packet_id;
  result->sender = sender;
  return result;
}

/* private structure */
typedef struct _GibberRMulticastSenderPrivate GibberRMulticastSenderPrivate;

struct _GibberRMulticastSenderPrivate
{
  gboolean dispose_has_run;
  /* hash table with packets */
  GHashTable *packet_cache;
  /* Very first packet number in the current window */
  guint32 first_packet;

  /* Timer to wait for extra packets */
  guint timer;
};

#define GIBBER_R_MULTICAST_SENDER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_R_MULTICAST_SENDER, GibberRMulticastSenderPrivate))

static void
gibber_r_multicast_sender_init (GibberRMulticastSender *obj)
{
  GibberRMulticastSenderPrivate *priv = 
    GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  priv->packet_cache = g_hash_table_new_full(g_int_hash, g_int_equal,
                                             NULL, packet_info_free);
}

static void gibber_r_multicast_sender_dispose (GObject *object);
static void gibber_r_multicast_sender_finalize (GObject *object);

static void
gibber_r_multicast_sender_class_init (GibberRMulticastSenderClass *gibber_r_multicast_sender_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_r_multicast_sender_class);

  g_type_class_add_private (gibber_r_multicast_sender_class, sizeof (GibberRMulticastSenderPrivate));

  object_class->dispose = gibber_r_multicast_sender_dispose;
  object_class->finalize = gibber_r_multicast_sender_finalize;

  signals[REPAIR_REQUEST] =
      g_signal_new("repair-request",
                   G_OBJECT_CLASS_TYPE(gibber_r_multicast_sender_class),
                   G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                   0,
                   NULL, NULL,
                   g_cclosure_marshal_VOID__UINT,
                   G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[REPAIR_MESSAGE] =
      g_signal_new("repair-message",
                   G_OBJECT_CLASS_TYPE(gibber_r_multicast_sender_class),
                   G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                   0,
                   NULL, NULL,
                   g_cclosure_marshal_VOID__OBJECT,
                   G_TYPE_NONE, 1, GIBBER_TYPE_R_MULTICAST_PACKET);

  signals[DATA_RECEIVED] =
      g_signal_new("data-received",
                   G_OBJECT_CLASS_TYPE(gibber_r_multicast_sender_class),
                   G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                   0,
                   NULL, NULL,
                   gibber_r_multicast_sender_marshal_VOID__POINTER_ULONG,
                   G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_ULONG);
}

void
gibber_r_multicast_sender_dispose (GObject *object)
{
  GibberRMulticastSender *self = GIBBER_R_MULTICAST_SENDER (object);
  GibberRMulticastSenderPrivate *priv = 
     GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (self);

  g_hash_table_destroy(priv->packet_cache);
  if (priv->timer != 0) {
    g_source_remove(priv->timer);
  }

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gibber_r_multicast_sender_parent_class)->dispose)
    G_OBJECT_CLASS (gibber_r_multicast_sender_parent_class)->dispose (object);
}

void
gibber_r_multicast_sender_finalize (GObject *object)
{
  GibberRMulticastSender *self = GIBBER_R_MULTICAST_SENDER (object);
  /*GibberRMulticastSenderPrivate *priv = 
     GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (self);
     */

  /* free any data held directly by the object here */
  g_free(self->name);

  G_OBJECT_CLASS (gibber_r_multicast_sender_parent_class)->finalize (object);
}


GibberRMulticastSender *
gibber_r_multicast_sender_new(const gchar *name) {
  GibberRMulticastSender *sender = 
    g_object_new(GIBBER_TYPE_R_MULTICAST_SENDER, NULL);
  sender->name = g_strdup(name);
  return sender;
}

static void
signal_data(GibberRMulticastSender *sender, guint8 *data, gsize size) {
  g_signal_emit(sender, signals[DATA_RECEIVED], 0, data, size);
}

static gboolean
request_repair(gpointer data) {
  PacketInfo *info = (PacketInfo *)data;

  DEBUG_SENDER(info->sender, "Sending out repair request for 0x%x",
    info->packet_id);

  info->timeout = 0;
  g_signal_emit(info->sender, signals[REPAIR_REQUEST], 0, info->packet_id);
  schedule_repair(info->sender, info->packet_id);

  return FALSE;
}


static void
schedule_repair(GibberRMulticastSender *sender, guint32 id) {
  GibberRMulticastSenderPrivate *priv = 
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);
  PacketInfo *info;
  guint timeout;

  info = g_hash_table_lookup(priv->packet_cache, &id);

  if (info != NULL && (info->packet != NULL || info->timeout != 0)) {
    return;
  } else if (info == NULL) {
    info = packet_info_new(sender, id);
    g_hash_table_insert(priv->packet_cache, &info->packet_id, info);
  }

  timeout = g_random_int_range(MIN_REPAIR_TIMEOUT, MAX_REPAIR_TIMEOUT);
  info->timeout = g_timeout_add(timeout, request_repair, info);
  DEBUG_SENDER(sender, 
    "Scheduled repair request for 0x%x in %d ms", id, timeout);
}

static gboolean
do_repair(gpointer data) {
  PacketInfo *info = (PacketInfo *)data;

  g_assert(info != NULL && info->packet != NULL);

  DEBUG_SENDER(info->sender, "Sending Repair message for 0x%x",
    info->packet_id);

  info->timeout = 0;
  g_signal_emit(info->sender, signals[REPAIR_MESSAGE], 0, info->packet);

  return FALSE;
}

static void
schedule_do_repair(GibberRMulticastSender *sender, guint32 id) {
  GibberRMulticastSenderPrivate *priv = 
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);
  PacketInfo *info;
  guint timeout;

  info = g_hash_table_lookup(priv->packet_cache, &id);

  g_assert(info != NULL && info->packet != NULL);
  if (info->timeout != 0) {
    /* Repair already scheduled, ignore */
    return;
  }


  timeout = g_random_int_range(MIN_DO_REPAIR_TIMEOUT, MAX_DO_REPAIR_TIMEOUT);
  info->timeout = g_timeout_add(timeout, do_repair, info);
  DEBUG_SENDER(sender, 
    "Scheduled repair for 0x%x in %d ms", id, timeout);
}

static gboolean
pop_packet(GibberRMulticastSender *sender) {
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);
  int i;
  int num;
  gsize payload_size, size;
  guint8 *data;
  PacketInfo *p;

  g_assert(sender->state == GIBBER_R_MULTICAST_SENDER_STATE_RUNNING);
  p = g_hash_table_lookup(priv->packet_cache, &sender->next_output_packet);

  DEBUG_SENDER(sender, "Looking at 0x%x", sender->next_output_packet);

  if (p == NULL || p->packet == NULL) {
    /* No packet yet.. too bad :( */
    DEBUG_SENDER(sender, "No new packets to pop");
    return FALSE;
  }

  num = p->packet->packet_total;
  payload_size = p->packet->payload_size;
  /* Need to be at least num behind last_packet */
  if (gibber_r_multicast_packet_diff(p->packet->packet_id, 
        sender->next_input_packet) < num) {
    DEBUG_SENDER(sender, "Not enough packets for defragmentation");
    return FALSE;
  }

  for (i = p->packet_id + 1; i != p->packet_id + num ; i++) {
    /* Check if we have everything */
    PacketInfo *tp  = g_hash_table_lookup(priv->packet_cache, &i);

    if (tp == NULL || tp->packet == NULL) {
      DEBUG_SENDER(sender, "Missing packet for defragmentation");
      /* Nope one missing */
      return FALSE;
    }
    payload_size += tp->packet->payload_size;
  }

  /* Complete packet we can send out */
  DEBUG_SENDER(sender, "Sending out 0x%x - 0x%x",
      p->packet->packet_id, p->packet->packet_id + num - 1);

  sender->next_output_packet = sender->next_output_packet + num;

  if (num == 1) {
    data = gibber_r_multicast_packet_get_payload(p->packet, &size);
    g_assert(size == payload_size);
    signal_data(sender, data, size);
  } else {
    data = g_malloc(payload_size);
    gsize off = 0;
    for (i = p->packet_id; i != p->packet_id + num ; i++) {
      /* Check if we have everything */
      PacketInfo *tp  = g_hash_table_lookup(priv->packet_cache, &i);
      guint8 *d;

      d = gibber_r_multicast_packet_get_payload(tp->packet, &size);
      g_assert(off + size <= payload_size);
      memcpy(data + off, d, size);
      off += size;
    }
    g_assert(off == payload_size);

    signal_data(sender, data, payload_size);
    g_free(data);
  }

  return TRUE;
}

static void
pop_packets(GibberRMulticastSender *sender) {
  while (pop_packet(sender)) 
    /* nothing */;
}

static void
start_repairs(GibberRMulticastSender *sender) {
  guint32 i;
  GibberRMulticastSenderPrivate *priv = 
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);

  if (sender->next_output_packet == sender->next_input_packet) {
    /* No repairs needed */
    DEBUG_SENDER(sender, "No repair needed");
    return;
  }

  for (i = priv->first_packet ; i != sender->next_input_packet; i++) {
    schedule_repair(sender, i);
  }
}

static gboolean
start_running(gpointer data) {
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER(data);
  GibberRMulticastSenderPrivate *priv = 
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);

  sender->state = GIBBER_R_MULTICAST_SENDER_STATE_RUNNING;
  priv->timer = 0;

  pop_packets(sender);
  start_repairs(sender);

  return FALSE;
}

void
insert_packet(GibberRMulticastSender *sender, GibberRMulticastPacket *packet) {
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);
  PacketInfo *info;
  info = g_hash_table_lookup(priv->packet_cache, &packet->packet_id);
  if (info != NULL && info->packet != NULL) {
    /* Already seen this packet */
    DEBUG_SENDER(sender, "Detect resent of packet 0x%x", packet->packet_id);
    return;
  }

  if (info == NULL) {
    info = packet_info_new(sender, packet->packet_id);
    g_hash_table_insert(priv->packet_cache, &info->packet_id, info);
  }

  if (info->timeout != 0){ 
    g_source_remove(info->timeout);
    info->timeout = 0;
  }

  DEBUG_SENDER(sender, "Inserting packet 0x%x", packet->packet_id);
  info->packet = g_object_ref(packet);

  if (gibber_r_multicast_packet_diff(priv->first_packet,
          packet->packet_id) < 0) {
    priv->first_packet = packet->packet_id;
  } else if (gibber_r_multicast_packet_diff(sender->next_input_packet,
                 packet->packet_id) > 0) {
    /* Potentially needs some repairs */
    guint32 i;
    for (i = sender->next_input_packet; i != packet->packet_id; i++) {
      schedule_repair(sender, i);
    }
    sender->next_input_packet = packet->packet_id + 1;
  }

  /* pop out as many packets as we can */
  if (sender->state > GIBBER_R_MULTICAST_SENDER_STATE_PREPARING)
    pop_packets(sender);

  return;
}

void
gibber_r_multicast_sender_push(GibberRMulticastSender *sender, 
                               GibberRMulticastPacket *packet) {
  GibberRMulticastSenderPrivate *priv = 
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);
  PacketInfo *info;
  gint diff;

  g_assert(strcmp(sender->name, packet->sender) == 0);

  if (g_hash_table_size(priv->packet_cache) == 0) {
    /* Very first packet from this sender, always insert */
    info = packet_info_new(sender, packet->packet_id);
    info->packet = g_object_ref(packet);
    DEBUG_SENDER(sender, "Inserting first packet 0x%x", packet->packet_id);

    g_hash_table_insert(priv->packet_cache, &info->packet_id, info);

    if (sender->state == GIBBER_R_MULTICAST_SENDER_STATE_PREPARING) {
      diff = gibber_r_multicast_packet_diff(sender->next_input_packet, 
                                            packet->packet_id);
      if (diff <= 0) {
        sender->next_input_packet = packet->packet_id + 1;
      }
    } else {
      g_assert(sender->state == GIBBER_R_MULTICAST_SENDER_STATE_NEW);
      sender->next_input_packet = packet->packet_id + 1;
    }

    priv->first_packet = packet->packet_id;
    sender->next_output_packet = priv->first_packet;

    sender->state = GIBBER_R_MULTICAST_SENDER_STATE_PREPARING;
    /* Wait 200 ms for extra packets */
    priv->timer = g_timeout_add(200, start_running, sender);
    return;
  }

  diff = gibber_r_multicast_packet_diff(sender->next_output_packet,
             packet->packet_id);

  if (diff >= 0 && diff < PACKET_CACHE_SIZE) {
    insert_packet(sender, packet);
    return;
  }

  if (sender->state == GIBBER_R_MULTICAST_SENDER_STATE_PREPARING
      && diff < 0 && (gibber_r_multicast_packet_diff(sender->next_input_packet,
                        packet->packet_id) > -PACKET_CACHE_SIZE)) {
    sender->next_output_packet = packet->packet_id;
    insert_packet(sender, packet);
    return;
  }

  if (diff < 0 && gibber_r_multicast_packet_diff(priv->first_packet,
             packet->packet_id) > 0) {
    /* We already had this one, silently ignore */
    DEBUG_SENDER(sender, "Detect resent of packet 0x%x", packet->packet_id);
    return;
  }
  DEBUG_SENDER(sender, "Packet 0x%x out of range, dropping (%x %x %x)", 
    packet->packet_id, priv->first_packet, 
    sender->next_output_packet, sender->next_input_packet);

}

void
gibber_r_multicast_sender_repair_request(GibberRMulticastSender *sender, 
                                         guint32 id) {
  GibberRMulticastSenderPrivate *priv = 
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);
  gint diff;

  if (sender->state != GIBBER_R_MULTICAST_SENDER_STATE_RUNNING) {
    DEBUG_SENDER(sender, "ignore repair request");
    return;
  }

  diff = gibber_r_multicast_packet_diff(sender->next_output_packet, id);

  if (diff >= 0 && diff < PACKET_CACHE_SIZE) {
    PacketInfo *info = g_hash_table_lookup(priv->packet_cache, &id);
    if (info == NULL) {
      guint32 i;

      g_assert(
          gibber_r_multicast_packet_diff(sender->next_input_packet, id) >= 0);

      for (i = sender->next_input_packet ; i != id + 1; i++ ){
        schedule_repair(sender, i);
      }
    } else if (info->packet != NULL) {
      schedule_do_repair(sender, id);
    } else {
      /* else we already knew about the packets existance, but didn't see
       the packet just yet. Which means we already have a repair timeout 
       running */
       g_assert(info->timeout != 0);
    }
    return;
  }

  if (diff < 0 && gibber_r_multicast_packet_diff(priv->first_packet, id) > 0) {
    schedule_do_repair(sender, id);
    return;
  }

  DEBUG_SENDER(sender, "Repair request packet 0x%x out of range, ignoring", 
      id);
}

gboolean
gibber_r_multicast_sender_seen(GibberRMulticastSender *sender, guint32 id) {
  gint diff;
  guint32 i, last;
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);

  DEBUG_SENDER(sender, "Seen next packet 0x%x", id);

  if (sender->state == GIBBER_R_MULTICAST_SENDER_STATE_NEW) {
    sender->state = GIBBER_R_MULTICAST_SENDER_STATE_PREPARING;
    sender->next_input_packet = id;
    sender->next_output_packet = id;
    priv->first_packet = id;
    return FALSE;
  }

  diff = gibber_r_multicast_packet_diff(sender->next_input_packet, id);
  if (diff < 0 || sender->state != GIBBER_R_MULTICAST_SENDER_STATE_RUNNING) {
    /* We're up to date */
    return TRUE;
  }

  last = sender->next_output_packet + PACKET_CACHE_SIZE;
  /* Ensure that we don't overfill the CACHE */
  last = gibber_r_multicast_packet_diff(last, id) > 0 ? last : id;

  for (i = sender->next_input_packet; i != last + 1; i ++) {
    schedule_repair(sender, i);
  }
  return FALSE;
}
