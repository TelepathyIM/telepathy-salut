/*
 * gibber-r-multicast-sender.c - Source for GibberRMulticastSender
 * Copyright (C) 2006-2007 Collabora Ltd.
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
#include "signals-marshal.h"

#define DEBUG_FLAG DEBUG_RMULTICAST_SENDER
#include "gibber-debug.h"

#define DEBUG_SENDER(sender, format, ...) \
  DEBUG("%s %x (%p): " format, sender->name, sender->id, sender, ##__VA_ARGS__)

#define PACKET_CACHE_SIZE 256

#define MIN_DO_REPAIR_TIMEOUT 50
#define MAX_DO_REPAIR_TIMEOUT 100

#define MIN_REPAIR_TIMEOUT 150
#define MAX_REPAIR_TIMEOUT 250

#define MIN_WHOIS_TIMEOUT 150
#define MAX_WHOIS_TIMEOUT 250

static void schedule_repair(GibberRMulticastSender *sender, guint32 id);
static void schedule_do_repair(GibberRMulticastSender *sender, guint32 id);
static void schedule_whois_request(GibberRMulticastSender *sender);

G_DEFINE_TYPE(GibberRMulticastSender, gibber_r_multicast_sender, G_TYPE_OBJECT)

/* signal enum */
enum
{
    REPAIR_REQUEST,
    REPAIR_MESSAGE,
    WHOIS_REPLY,
    WHOIS_REQUEST,
    NAME_DISCOVERED,
    RECEIVED_DATA,
    RECEIVED_CONTROL_PACKET,
    LAST_SIGNAL
};

/* properties */
enum {
  PROP_SENDERS_HASH = 1,
  LAST_PROPERTY
};

static guint signals[LAST_SIGNAL] = {0};

typedef struct {
  guint32 packet_id;
  guint timeout;
  gboolean repeating;
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
  /* Other senders on the network 
   * (sender id -> GibberRMulticastSender object)
   */
  GHashTable *senders;
  /* Very first packet number in the current window */
  guint32 first_packet;

  /* whois reply/request timer */
  guint whois_timer;

  /* Whether we are holding back data currently */
  gboolean holding_data;
  guint32 holding_point;

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
gibber_r_multicast_sender_set_property(GObject *object,
                                       guint property_id,
                                       const GValue *value,
                                       GParamSpec *pspec) {
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER(object);
  GibberRMulticastSenderPrivate *priv =
    GIBBER_R_MULTICAST_SENDER_GET_PRIVATE(sender);

  switch (property_id) {
    case PROP_SENDERS_HASH:
      priv->senders = (GHashTable *)g_value_get_pointer(value);
      if (priv->senders != NULL) {
        g_hash_table_ref(priv->senders);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void
gibber_r_multicast_sender_class_init (GibberRMulticastSenderClass *gibber_r_multicast_sender_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_r_multicast_sender_class);

  g_type_class_add_private (gibber_r_multicast_sender_class, sizeof (GibberRMulticastSenderPrivate));
  GParamSpec *param_spec;

  object_class->dispose = gibber_r_multicast_sender_dispose;
  object_class->finalize = gibber_r_multicast_sender_finalize;

  object_class->set_property = gibber_r_multicast_sender_set_property;

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

  signals[RECEIVED_DATA] =
      g_signal_new("received-data",
                   G_OBJECT_CLASS_TYPE(gibber_r_multicast_sender_class),
                   G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                   0,
                   NULL, NULL,
                   _gibber_signals_marshal_VOID__UCHAR_POINTER_ULONG,
                   G_TYPE_NONE, 3, G_TYPE_UCHAR, G_TYPE_POINTER, G_TYPE_ULONG);

  signals[RECEIVED_CONTROL_PACKET] =
      g_signal_new("received-control-packet",
                   G_OBJECT_CLASS_TYPE(gibber_r_multicast_sender_class),
                   G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                   0,
                   NULL, NULL,
                   g_cclosure_marshal_VOID__OBJECT,
                   G_TYPE_NONE, 1, GIBBER_TYPE_R_MULTICAST_PACKET);

  signals[WHOIS_REPLY] =
      g_signal_new("whois-reply",
                   G_OBJECT_CLASS_TYPE(gibber_r_multicast_sender_class),
                   G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                   0,
                   NULL, NULL,
                   g_cclosure_marshal_VOID__VOID,
                   G_TYPE_NONE, 0);

  signals[WHOIS_REQUEST] =
      g_signal_new("whois-request",
                   G_OBJECT_CLASS_TYPE(gibber_r_multicast_sender_class),
                   G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                   0,
                   NULL, NULL,
                   g_cclosure_marshal_VOID__VOID,
                   G_TYPE_NONE, 0);

  signals[NAME_DISCOVERED] =
      g_signal_new("name-discovered",
                   G_OBJECT_CLASS_TYPE(gibber_r_multicast_sender_class),
                   G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                   0,
                   NULL, NULL,
                   g_cclosure_marshal_VOID__STRING,
                   G_TYPE_NONE, 1, G_TYPE_STRING);

  param_spec = g_param_spec_pointer ("senderhash",
                                     "Sender Hash",
                                     "Hash of other senders",
                                     G_PARAM_CONSTRUCT_ONLY |
                                     G_PARAM_WRITABLE       |
                                     G_PARAM_STATIC_NAME    |
                                     G_PARAM_STATIC_BLURB);
  g_object_class_install_property(object_class, PROP_SENDERS_HASH,
      param_spec);
}

void
gibber_r_multicast_sender_dispose (GObject *object)
{
  GibberRMulticastSender *self = GIBBER_R_MULTICAST_SENDER (object);
  GibberRMulticastSenderPrivate *priv = 
     GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_hash_table_destroy(priv->packet_cache);

  /* release any references held by the object here */
  if (priv->senders != NULL) {
    g_hash_table_unref(priv->senders);
  }
  if (priv->whois_timer != 0) {
    g_source_remove(priv->whois_timer);
    priv->whois_timer = 0;
  }

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
gibber_r_multicast_sender_new(guint32 id,
                              const gchar *name,
                              GHashTable *senders) {
  GibberRMulticastSender *sender =
    g_object_new(GIBBER_TYPE_R_MULTICAST_SENDER, "senderhash", senders,
        NULL);

  sender->id = id;
  sender->name = g_strdup(name);

  if (sender->name == NULL) {
    schedule_whois_request(sender);
  }

  g_assert(senders != NULL);
  return sender;
}

static void
signal_data(GibberRMulticastSender *sender, guint8 stream_id,
            guint8 *data, gsize size) {
  sender->state = GIBBER_R_MULTICAST_SENDER_STATE_RUNNING;
  g_signal_emit(sender, signals[RECEIVED_DATA], 0, stream_id, data, size);
}

static void
signal_control_packet(GibberRMulticastSender *sender,
    GibberRMulticastPacket *packet)
{
  sender->state = GIBBER_R_MULTICAST_SENDER_STATE_RUNNING;
  g_signal_emit (sender, signals[RECEIVED_CONTROL_PACKET], 0, packet);
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

  if (info->repeating) {
    schedule_do_repair (info->sender, info->packet_id);
  }

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
do_whois_reply(gpointer data) {
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER(data);
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE(sender);

  DEBUG_SENDER(sender, "Sending out whois reply");
  g_signal_emit(sender, signals[WHOIS_REPLY], 0);
  priv->whois_timer = 0;
  return FALSE;
}

static gboolean
do_whois_request(gpointer data) {
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER(data);
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE(sender);

  DEBUG_SENDER(sender, "Sending out whois request");
  g_signal_emit(sender, signals[WHOIS_REQUEST], 0);

  priv->whois_timer = 0;
  schedule_whois_request(sender);
  return FALSE;
}

static void
schedule_whois_request(GibberRMulticastSender *sender) {
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE(sender);
   gint timeout = g_random_int_range(MIN_WHOIS_TIMEOUT,
                                     MAX_WHOIS_TIMEOUT);
   DEBUG_SENDER(sender, "Scheduled whois request in %d ms", timeout);
   priv->whois_timer = g_timeout_add(timeout, do_whois_request, sender);
}

static gboolean
check_depends(GibberRMulticastSender *sender,
              GibberRMulticastPacket *packet,
              gboolean data) {
  int i;
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);

  g_assert (IS_RELIABLE_PACKET(packet));

  for (i = 0; i < packet->depends->len; i++) {
    GibberRMulticastSender *s;
    GibberRMulticastPacketSenderInfo *sender_info;
    guint32 other;

    sender_info = g_array_index (packet->depends,
        GibberRMulticastPacketSenderInfo *, i);

    s = g_hash_table_lookup(priv->senders,
        GUINT_TO_POINTER(sender_info->sender_id));

    if (s == NULL || s->state == GIBBER_R_MULTICAST_SENDER_STATE_NEW) {
      /* If the node depends on a sender that's unknown to us, the depends
       * can't be satisfied */
      DEBUG_SENDER(sender, "Unknown node in dependency list: %x",
          sender_info->sender_id);
      return FALSE;
    }

    if (data) {
      other = s->next_output_data_packet;
    } else {
      other = s->next_output_packet;
    }

    if (gibber_r_multicast_packet_diff(sender_info->packet_id, other) < 0) {
        DEBUG_SENDER(sender,
            "Waiting node %x to complete it's messages up to %x",
            sender_info->sender_id, sender_info->packet_id);
        return FALSE;
    }
  }
  return TRUE;
}

static gboolean
pop_data_packet (GibberRMulticastSender *sender, guint32 start)
{
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);
  guint8 *data;
  PacketInfo *p;
  gsize payload_size, size;
  int num, i;

  p = g_hash_table_lookup(priv->packet_cache, &start);

  g_assert (p != NULL);

  num = p->packet->data.data.packet_total;
  payload_size = p->packet->data.data.payload_size;
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
    payload_size += tp->packet->data.data.payload_size;
  }

  if (start == sender->next_output_packet)
    sender->next_output_packet = sender->next_output_packet + num;

  if (priv->holding_data &&
      gibber_r_multicast_packet_diff (p->packet->packet_id,
      priv->holding_point) <= 0) {
    DEBUG_SENDER (sender, "Holding back 0x%x - 0x%x",
      p->packet->packet_id, p->packet->packet_id + num - 1);

    /* We did actually have the data needed to pop */
    return TRUE;
  }

  if (!check_depends (sender, p->packet, TRUE))
    {
      return FALSE;
    }

  sender->next_output_data_packet = start + num;

  /* Complete packet we can send out */
  DEBUG_SENDER(sender, "Sending out 0x%x - 0x%x",
      p->packet->packet_id, p->packet->packet_id + num - 1);

  if (num == 1) {
    data = gibber_r_multicast_packet_get_payload(p->packet, &size);
    g_assert(size == payload_size);
    signal_data(sender, p->packet->data.data.stream_id, data, size);
  } else {
    data = g_malloc(payload_size);
    gsize off = 0;
    for (i = p->packet_id; i != p->packet->packet_id + num ; i++) {
      /* Check if we have everything */
      PacketInfo *tp  = g_hash_table_lookup(priv->packet_cache, &i);
      guint8 *d;

      d = gibber_r_multicast_packet_get_payload(tp->packet, &size);
      g_assert(off + size <= payload_size);
      memcpy(data + off, d, size);
      off += size;
    }
    g_assert(off == payload_size);

    signal_data(sender, p->packet->data.data.stream_id, data, payload_size);
    g_free(data);
  }

  return TRUE;
}

static gboolean
pop_packet(GibberRMulticastSender *sender) {
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);
  PacketInfo *p;
  gboolean popped = FALSE;
  guint32 to_pop;

  if (sender->next_output_data_packet != sender->next_output_packet) {
     if (!priv->holding_data ||
          gibber_r_multicast_packet_diff (sender->next_output_data_packet,
         priv->holding_point) > 0) {
       to_pop = sender->next_output_data_packet;
       DEBUG_SENDER (sender, "Popping old data packet");
     } else {
       /* We already popped all the data we could, continue on the normal
        * packets */
       DEBUG_SENDER (sender, "old data already popped");
       to_pop = sender->next_output_packet;
     }
  } else {
    to_pop = sender->next_output_packet;
  }

  p = g_hash_table_lookup(priv->packet_cache, &to_pop);

  DEBUG_SENDER(sender, "Looking at 0x%x", to_pop);

  if (p == NULL || p->packet == NULL) {
    /* No packet yet.. too bad :( */
    DEBUG_SENDER(sender, "No new packets to pop"); goto out;
  }

  g_assert (IS_RELIABLE_PACKET (p->packet));
  if (!check_depends(sender, p->packet, FALSE)) {
    goto out;
  }

  if (p->packet->type != PACKET_TYPE_DATA) {
    if (to_pop == sender->next_output_packet)
      {
       /* If both are the same, they can stay the same... */
       if (sender->next_output_packet == sender->next_output_data_packet)
         {
           sender->next_output_data_packet++;
         }
       sender->next_output_packet++;
       if (p->packet->type != PACKET_TYPE_NO_DATA)
         {
           signal_control_packet (sender, p->packet);
         }
      }
    else
      {
        sender->next_output_data_packet++;
      }
    popped = TRUE;
    goto out;
  }

  popped = pop_data_packet (sender, to_pop);

out:
  return popped;
}

static void
senders_updated(gpointer key, gpointer value, gpointer user_data) {
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER(value);
  gibber_r_multicast_senders_updated(sender);
}

static void
pop_packets(GibberRMulticastSender *sender) {
  gboolean popped = FALSE;
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);

  if (sender->name == NULL
      || sender->state < GIBBER_R_MULTICAST_SENDER_STATE_PREPARING)
  {
    /* No popping untill we know the senders mapped name and we at least have
     * some packets */
    return;
  }

  while (pop_packet(sender))
    popped = TRUE;

  if (popped)
    {
      g_hash_table_foreach(priv->senders, senders_updated, NULL);
    }

}

void
insert_packet(GibberRMulticastSender *sender, GibberRMulticastPacket *packet) {
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);
  PacketInfo *info;

  g_assert(sender->state > GIBBER_R_MULTICAST_SENDER_STATE_NEW);

  info = g_hash_table_lookup(priv->packet_cache, &packet->packet_id);
  if (info != NULL && info->packet != NULL) {
    /* Already seen this packet */
    DEBUG_SENDER(sender, "Detect resent of packet 0x%x", 
        packet->packet_id);
    return;
  }

  if (info == NULL) {
    info = packet_info_new(sender, packet->packet_id);
    g_hash_table_insert(priv->packet_cache, &info->packet_id, info);
  }

  if (info->timeout != 0) {
    g_source_remove(info->timeout);
    info->timeout = 0;
  }

  DEBUG_SENDER(sender, "Inserting packet 0x%x", packet->packet_id);
  info->packet = g_object_ref(packet);

  if (gibber_r_multicast_packet_diff(priv->first_packet,
          packet->packet_id) < 0) {
    priv->first_packet = packet->packet_id;
  } else if (gibber_r_multicast_packet_diff(sender->next_input_packet,
                 packet->packet_id) >= 0) {
    /* Potentially needs some repairs */
    guint32 i;
    for (i = sender->next_input_packet; 
        i != packet->packet_id; i++) {
      schedule_repair(sender, i);
    }
    sender->next_input_packet = packet->packet_id + 1;
  }

  /* pop out as many packets as we can */
  pop_packets(sender);

  return;
}


void
gibber_r_multicast_sender_update_start (GibberRMulticastSender *sender,
    guint32 packet_id)
{
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);

  if (sender->state == GIBBER_R_MULTICAST_SENDER_STATE_NEW) {
    g_assert(g_hash_table_size(priv->packet_cache) == 0);

    sender->state = GIBBER_R_MULTICAST_SENDER_STATE_PREPARING;
    sender->next_input_packet = packet_id;
    sender->next_output_packet = packet_id;
    sender->next_output_data_packet = packet_id;
    priv->first_packet = packet_id;
  } else if (gibber_r_multicast_packet_diff (sender->next_input_packet, 
      packet_id) > 0) {
    /* Remove all repair requests for packets up to this packet_id */
    guint32 i;
    for (i = priv->first_packet; i < packet_id; i++) {
      PacketInfo *info;
      info = g_hash_table_lookup (priv->packet_cache, &i);
      if (info != NULL && info->packet == NULL && info->timeout != 0) {
        g_source_remove (info->timeout);
        info->timeout = 0;
      }
    }

    sender->next_input_packet = packet_id;
    sender->next_output_packet = packet_id;
    sender->next_output_data_packet = packet_id;
  }
}

void
gibber_r_multicast_sender_push(GibberRMulticastSender *sender,
                               GibberRMulticastPacket *packet) {
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);
  gint diff;

  g_assert(sender->id == packet->sender);

  if (sender->state < GIBBER_R_MULTICAST_SENDER_STATE_PREPARING) {
    /* Don't know where to start, so ignore..
     * A potential optimisation would be to cache a limited amount anyway, so
     * we don't have to repair them if we should have catched these anyways */
    return;
  }

  diff = gibber_r_multicast_packet_diff(sender->next_output_packet,
             packet->packet_id);

  if (diff >= 0 && diff < PACKET_CACHE_SIZE) {
    insert_packet(sender, packet);
    return;
  }

  if (diff < 0 && gibber_r_multicast_packet_diff(priv->first_packet,
             packet->packet_id) >= 0) {
    /* We already had this one, silently ignore */
    DEBUG_SENDER(sender, "Detect resent of packet 0x%x",
        packet->packet_id);
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

  if (sender->state < GIBBER_R_MULTICAST_SENDER_STATE_PREPARING) {
    DEBUG_SENDER(sender, "ignore repair request");
    return;
  }

  diff = gibber_r_multicast_packet_diff(sender->next_output_packet, id);

  if (diff >= 0 && diff < PACKET_CACHE_SIZE) {
    PacketInfo *info = g_hash_table_lookup(priv->packet_cache, &id);
    if (info == NULL) {
      guint32 i;

      for (i = sender->next_output_packet ; i != id + 1; i++ ){
        schedule_repair(sender, i);
      }
    } else if (info->packet != NULL) {
      schedule_do_repair(sender, id);
    } else {
      /* else we already knew about the packets existance, but didn't see
       the packet just yet. Which means we already have a repair timeout 
       running */
       g_assert(info->timeout != 0);
       /* Reschedule the repair */
       g_source_remove(info->timeout);
       info->timeout = 0;
       schedule_repair(sender, id);
    }
    return;
  }

  if (diff < 0
      && gibber_r_multicast_packet_diff(priv->first_packet, id) >= 0) {
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

  DEBUG_SENDER(sender, "Seen next packet 0x%x", id);

  if (sender->state < GIBBER_R_MULTICAST_SENDER_STATE_PREPARING) {
    return FALSE;
  }

  diff = gibber_r_multicast_packet_diff(sender->next_input_packet, id);
  if (diff < 0) {
    return TRUE;
  }

  last = sender->next_output_packet + PACKET_CACHE_SIZE;
  /* Ensure that we don't overfill the CACHE */
  last = gibber_r_multicast_packet_diff(last, id) > 0 ? last : id;

  for (i = sender->next_input_packet; i != last; i ++) {
    schedule_repair(sender, i);
  }
  return FALSE;
}

void
gibber_r_multicast_senders_updated(GibberRMulticastSender *sender) {
  if (sender->state == GIBBER_R_MULTICAST_SENDER_STATE_RUNNING) {
    pop_packets(sender);
  }
}

void
gibber_r_multicast_sender_whois_push (GibberRMulticastSender *sender,
    const GibberRMulticastPacket *packet)
{
  GibberRMulticastSenderPrivate *priv =
    GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);

  switch (packet->type) {
    case PACKET_TYPE_WHOIS_REQUEST:
      g_assert (packet->data.whois_request.sender_id == sender->id);

      if (sender->name != NULL && priv->whois_timer == 0) {
        gint timeout = g_random_int_range(MIN_WHOIS_TIMEOUT,
                                          MAX_WHOIS_TIMEOUT);
        priv->whois_timer = g_timeout_add(timeout, do_whois_reply, sender);
        DEBUG_SENDER(sender, "Scheduled whois reply in %d ms", timeout);
      }
      break;
    case PACKET_TYPE_WHOIS_REPLY:
      g_assert(packet->sender == sender->id);

      if (sender->name == NULL) {
        sender->name = g_strdup(packet->data.whois_reply.sender_name);
        DEBUG_SENDER(sender, "Name discovered");
        g_signal_emit(sender, signals[NAME_DISCOVERED], 0, sender->name);
      } else {
        /* FIXME: collision detection */
      }
      if (priv->whois_timer != 0) {
        DEBUG_SENDER(sender, "Cancelled scheduled whois packet");
        g_source_remove(priv->whois_timer);
        priv->whois_timer = 0;
      }
      pop_packets(sender);
      break;
    default:
      g_assert_not_reached();
  }
}

void
gibber_r_multicast_sender_set_packet_repeat (GibberRMulticastSender *sender,
    guint32 packet_id, gboolean repeat) {
  GibberRMulticastSenderPrivate *priv =
    GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);

  PacketInfo *info;

  info = g_hash_table_lookup(priv->packet_cache, &packet_id);
  g_assert (info != NULL && info->packet != NULL);

  if (info->repeating == repeat) {
    return;
  }

  info->repeating = repeat;

  if (repeat && info->timeout == 0) {
    schedule_do_repair (sender, packet_id);
  }

  /* FIXME: If repeat is turned off, we repeat it at least once more as there
   * might have been a repair request after the last repeating.. This is
   * ofcourse suboptimal */
}

/* Tell the sender to not signal data starting from this packet */
void
gibber_r_multicast_sender_hold_data (GibberRMulticastSender *sender,
  guint32 packet_id)
{
  GibberRMulticastSenderPrivate *priv =
    GIBBER_R_MULTICAST_SENDER_GET_PRIVATE(sender);

  priv->holding_data = TRUE;
  priv->holding_point = packet_id;

  /* Pop packets in case the holding_point moved forward */
  pop_packets (sender);
}

/* Stop holding back data of the sender */
void
gibber_r_multicast_sender_release_data (GibberRMulticastSender *sender)
{
  GibberRMulticastSenderPrivate *priv =
    GIBBER_R_MULTICAST_SENDER_GET_PRIVATE(sender);

  priv->holding_data = FALSE;
  pop_packets (sender);
}

