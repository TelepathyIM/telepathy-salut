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
#include "gibber-util.h"
#include "signals-marshal.h"

#define DEBUG_FLAG DEBUG_RMULTICAST_SENDER
#include "gibber-debug.h"

#define DEBUG_SENDER(sender, format, ...) \
  DEBUG("%s %x: " format, sender->name, sender->id, ##__VA_ARGS__)

#define PACKET_CACHE_SIZE 256

#define MIN_DO_REPAIR_TIMEOUT 50
#define MAX_DO_REPAIR_TIMEOUT 100

#define MIN_REPAIR_TIMEOUT 150
#define MAX_REPAIR_TIMEOUT 250

#define MIN_FIRST_WHOIS_TIMEOUT 50
#define MAX_FIRST_WHOIS_TIMEOUT 200

#define MIN_WHOIS_TIMEOUT 400
#define MAX_WHOIS_TIMEOUT 600

#define MIN_WHOIS_REPLY_TIMEOUT 50
#define MAX_WHOIS_REPLY_TIMEOUT 200

/* At least one packet must be popped every 5 minutes.. Reliable keepalives
 * are send out every three minutes.. */
#define MAX_PROGRESS_TIMEOUT 300000

/* The senders name should be discovered within about 10 seconds or else it is
 * fauly */
#define NAME_DISCOVERY_TIME  10000

static void set_state (GibberRMulticastSender *sender,
   GibberRMulticastSenderState state);

typedef struct {
  guint32 sender_id;
  guint32 packet_id;
  /* First packet that had this ack */
  guint32 first_packet_id;
} AckInfo;

static void
ack_info_free(gpointer data) {
  g_slice_free(AckInfo, data);
}

static AckInfo *
ack_info_new(guint32 sender_id) {
  AckInfo *result;
  result = g_slice_new0(AckInfo);
  result->sender_id = sender_id;
  return result;
}


struct _group_ht_data {
  GibberRMulticastSenderGroup *group;
  GibberRMulticastSender *target;
  GibberRMulticastSender *sender;
};

static AckInfo *
gibber_r_multicast_sender_get_ackinfo (GibberRMulticastSender *sender,
    guint32 sender_id);

GibberRMulticastSenderGroup *
gibber_r_multicast_sender_group_new (void)
{
  GibberRMulticastSenderGroup *result;
  result = g_slice_new0 (GibberRMulticastSenderGroup);

  result->senders = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);
  result->pop_queue = g_queue_new();
  return result;
}

void
gibber_r_multicast_sender_group_free (GibberRMulticastSenderGroup *group)
{
  GHashTable *h;
  g_assert (group->popping == FALSE);
  h = group->senders;
  group->senders = NULL;
  g_hash_table_destroy (h);
  g_queue_free (group->pop_queue);
  g_slice_free (GibberRMulticastSenderGroup, group);
}

static void
stop_sender (gpointer key, gpointer value, gpointer user_data)
{
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER(value);

  gibber_r_multicast_sender_stop (sender);
}

void
gibber_r_multicast_sender_group_stop (GibberRMulticastSenderGroup *group)
{
  g_hash_table_foreach (group->senders, stop_sender, NULL);
  group->stopped = TRUE;
}

void
gibber_r_multicast_sender_group_add (GibberRMulticastSenderGroup *group,
    GibberRMulticastSender *sender)
{
  DEBUG ("Adding %x to sender group", sender->id);
  g_hash_table_insert (group->senders, GUINT_TO_POINTER (sender->id), sender);
}


GibberRMulticastSender *
gibber_r_multicast_sender_group_lookup (GibberRMulticastSenderGroup *group,
    guint32 sender_id)
{
  return g_hash_table_lookup (group->senders, GUINT_TO_POINTER (sender_id));
}

static gboolean
find_by_name (gpointer key, gpointer value, gpointer user_data)
{
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER (value);
  const gchar *name = (gchar *)user_data;

  if (sender->state == GIBBER_R_MULTICAST_SENDER_STATE_PENDING_REMOVAL)
    return FALSE;

  return !gibber_strdiff (sender->name, name);
}

GibberRMulticastSender *
gibber_r_multicast_sender_group_lookup_by_name (
    GibberRMulticastSenderGroup *group, const gchar *name)
{
  return g_hash_table_find (group->senders, find_by_name, (gpointer) name);
}

void
gibber_r_multicast_sender_group_remove (GibberRMulticastSenderGroup *group,
    guint32 sender_id)
{
  GibberRMulticastSender *s;

  DEBUG ("Removing %x from sender group", sender_id);

  s = g_hash_table_lookup (group->senders, GUINT_TO_POINTER(sender_id));

  g_queue_remove (group->pop_queue, s);
  if (gibber_r_multicast_sender_packet_cache_size (s) > 0)
    {
      DEBUG ("Keeping %x in cache, %d items left", sender_id,
          gibber_r_multicast_sender_packet_cache_size(s));
      gibber_r_multicast_sender_stop (s);
      set_state (s, GIBBER_R_MULTICAST_SENDER_STATE_PENDING_REMOVAL);
    }
  else
   {
     g_hash_table_remove (group->senders, GUINT_TO_POINTER(sender_id));
   }

}

static void
create_sender_array (gpointer key, gpointer value, gpointer user_data)
{
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER (value);
  GArray *array = (GArray *)user_data;
  AckInfo info;

  if (sender->state >= GIBBER_R_MULTICAST_SENDER_STATE_STOPPED)
    return;

  info.sender_id =  sender->id;
  info.packet_id = sender->next_input_packet;

  g_array_append_val (array, info);
}

static void
update_sender_acks (gpointer key, gpointer value, gpointer user_data)
{
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER (value);
  GArray *array = (GArray *)user_data;
  gint i;

  if (sender->state >= GIBBER_R_MULTICAST_SENDER_STATE_STOPPED)
    return;

  for (i = 0; i < array->len ; i++)
    {
      AckInfo *info = &g_array_index (array, AckInfo, i);
      AckInfo *ack;

      if (sender->id == info->sender_id)
        continue;

      if ((ack = gibber_r_multicast_sender_get_ackinfo (sender,
           info->sender_id)) != NULL)
        {
          if (gibber_r_multicast_packet_diff (ack->packet_id,
               info->packet_id) > 0)
            info->packet_id = ack->packet_id;
        }
      else
       {
         g_array_remove_index_fast (array, i);
         /* The last element is now placed at location i, so retry i */
         i--;
         continue;
       }
    }
}

static AckInfo *
get_direct_ack (GibberRMulticastSender *sender, GibberRMulticastSender *target)
{
  AckInfo *ack;

  ack = gibber_r_multicast_sender_get_ackinfo (sender, target->id);

  if (G_LIKELY (ack != NULL))
   {
     /* Returning the direct ack if there is one */
     if (G_LIKELY (gibber_r_multicast_packet_diff (
           target->next_output_packet, ack->packet_id) >= 0))
       return ack;
   }

  return NULL;
}

static gboolean
find_indirect_ack (gpointer key, gpointer value, gpointer user_data)
{
  struct _group_ht_data *hd = (struct _group_ht_data *) user_data;
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER (value);
  AckInfo *target_ack, *ack;

  if (sender == hd->sender)
    return FALSE;

  target_ack = get_direct_ack (sender, hd->target);

  if (target_ack == NULL)
    return FALSE;

  ack = gibber_r_multicast_sender_get_ackinfo (hd->sender, sender->id);
  if (ack == NULL)
    return FALSE;

  return gibber_r_multicast_packet_diff (target_ack->first_packet_id,
      ack->packet_id) > 0;
}

static gboolean
failure_not_acked (gpointer key, gpointer value, gpointer user_data)
{
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER (value);
  struct _group_ht_data *hd = (struct _group_ht_data *) user_data;

  if (sender->state == GIBBER_R_MULTICAST_SENDER_STATE_PENDING_REMOVAL)
    return FALSE;

  /* A failure is acked iff each sender has acked it's last packet (direct ack)
   * or a sender acked a packet of another sender acking the failures last
   * packet (indirect ack) or if the sender never has even heard of this node.
   * */

  if (get_direct_ack (sender, hd->target) != NULL)
    return FALSE;

  hd->sender = sender;

  return g_hash_table_find (hd->group->senders, find_indirect_ack, hd) == NULL;
}

static gboolean
gc_failures (gpointer key, gpointer value, gpointer user_data)
{
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER (value);
  GibberRMulticastSenderGroup *group =
      (GibberRMulticastSenderGroup *) user_data;
  struct _group_ht_data hd;

  if (sender->state < GIBBER_R_MULTICAST_SENDER_STATE_PENDING_REMOVAL)
    return FALSE;

  hd.group = group;
  hd.target = sender;

  return g_hash_table_find (group->senders, failure_not_acked, &hd) == NULL;
}

void
gibber_r_multicast_sender_group_gc (GibberRMulticastSenderGroup *group)
{
  GArray *array;
  guint i;

  array = g_array_sized_new (FALSE, TRUE, sizeof (AckInfo),
      g_hash_table_size (group->senders));

  g_hash_table_foreach (group->senders, create_sender_array, array);
  g_hash_table_foreach (group->senders, update_sender_acks, array);

  for (i = 0; i < array->len ; i++)
    {
      AckInfo *info = &g_array_index (array, AckInfo, i);
      GibberRMulticastSender *sender = g_hash_table_lookup (group->senders,
        GUINT_TO_POINTER (info->sender_id));

      gibber_r_multicast_sender_ack (sender, info->packet_id);
    }

  g_array_free (array, TRUE);

  g_hash_table_foreach_remove (group->senders, gc_failures, group);
}

static void schedule_repair(GibberRMulticastSender *sender, guint32 id);
static void schedule_do_repair(GibberRMulticastSender *sender, guint32 id);
static void schedule_whois_request(GibberRMulticastSender *sender,
    gboolean rescheduled);
static gboolean name_discovery_failed_cb (gpointer data);
static void schedule_progress_timer (GibberRMulticastSender *self);

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
    FAILED,
    LAST_SIGNAL
};

/* properties */
enum {
  PROP_SENDER_GROUP = 1,
  LAST_PROPERTY
};

static guint signals[LAST_SIGNAL] = {0};

typedef struct {
  guint32 packet_id;
  guint timeout;
  gboolean repeating;
  GibberRMulticastPacket *packet;
  GibberRMulticastSender *sender;
  gboolean acked;
  gboolean popped;
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

  /* Table with acks per sender
   * guint32 * => owned AckInfo * */
  GHashTable *acks;

  /* Sendergroup to which we belong */
  GibberRMulticastSenderGroup *group;

  /* Very first packet number in the current window */
  guint32 first_packet;

  /* whois reply/request timer */
  guint whois_timer;

  /* timer untill which a failure even occurs  */
  guint fail_timer;

  /* Whether we are holding back data currently */
  gboolean holding_data;
  guint32 holding_point;

  /* Whether we went know the data starting point or not */
  gboolean start_data;
  guint32 start_point;

  /* Endpoint is just there in case we are in failure mode */
  guint32 end_point;
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

  priv->acks = g_hash_table_new_full(g_int_hash, g_int_equal,
                                             NULL, ack_info_free);
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
    case PROP_SENDER_GROUP:
      priv->group =
          (GibberRMulticastSenderGroup *) g_value_get_pointer(value);
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
                   _gibber_signals_marshal_VOID__UINT_POINTER_ULONG,
                   G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_POINTER, G_TYPE_ULONG);

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

  signals[FAILED] =
      g_signal_new("failed",
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

  param_spec = g_param_spec_pointer ("sendergroup",
                                     "Sender Group",
                                     "Group of senders",
                                     G_PARAM_CONSTRUCT_ONLY |
                                     G_PARAM_WRITABLE       |
                                     G_PARAM_STATIC_NAME    |
                                     G_PARAM_STATIC_BLURB);
  g_object_class_install_property(object_class, PROP_SENDER_GROUP,
      param_spec);
}

static void
cleanup_acks (gpointer key, gpointer value, gpointer user_data)
{
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER (value);
  GibberRMulticastSenderPrivate *priv =
     GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);
  GibberRMulticastSender *target = GIBBER_R_MULTICAST_SENDER (user_data);

  g_hash_table_remove (priv->acks, &target->id);
}

void
gibber_r_multicast_sender_dispose (GObject *object)
{
  GibberRMulticastSender *self = GIBBER_R_MULTICAST_SENDER (object);
  GibberRMulticastSenderPrivate *priv =
     GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (self);

  DEBUG_SENDER (self, "disposing");

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->group->senders != NULL)
    g_hash_table_foreach (priv->group->senders, cleanup_acks, self);

  g_hash_table_destroy(priv->packet_cache);
  g_hash_table_destroy(priv->acks);

  if (priv->whois_timer != 0) {
    g_source_remove(priv->whois_timer);
    priv->whois_timer = 0;
  }

  if (priv->fail_timer != 0) {
    g_source_remove(priv->fail_timer);
    priv->fail_timer = 0;
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

static void
set_state (GibberRMulticastSender *sender,
   GibberRMulticastSenderState state)
{
  g_assert (sender->state <= state);

  sender->state = state;
}

GibberRMulticastSender *
gibber_r_multicast_sender_new(guint32 id,
                              const gchar *name,
                              GibberRMulticastSenderGroup *group) {
  GibberRMulticastSender *sender;
  GibberRMulticastSenderPrivate *priv;

  sender = g_object_new(GIBBER_TYPE_R_MULTICAST_SENDER, "sendergroup", group,
        NULL);
  priv = GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);

  g_assert(group != NULL);

  sender->id = id;
  sender->name = g_strdup(name);

  if (sender->name == NULL)
    {
      schedule_whois_request(sender, FALSE);
      priv->fail_timer = g_timeout_add (NAME_DISCOVERY_TIME,
        name_discovery_failed_cb, sender);
    }
  else
   {
     schedule_progress_timer (sender);
   }

  return sender;
}

static void
packet_info_try_gc (GibberRMulticastSender *sender, PacketInfo *info)
{
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);
  guint32 packet_id, i;

  if (!info->acked || !info->popped || info->repeating)
    return;

  packet_id = info->packet_id;
  g_hash_table_remove (priv->packet_cache, &packet_id);

  if (packet_id == priv->first_packet)
    {
      for (i = packet_id; i !=  sender->next_output_data_packet; i++)
        if (g_hash_table_lookup (priv->packet_cache, &i) != NULL)
          break;

      priv->first_packet = i;
    }

}

static void
cancel_failure_timers (GibberRMulticastSender *sender)
{
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);
  /* Cancel timers that are not needed anymore now the sender has failed */
  if (priv->fail_timer != 0)
    {
      g_source_remove (priv->fail_timer);
      priv->fail_timer = 0;
    }

  /* failed, no need to get our name anymore */
  if (priv->whois_timer != 0)
    {
      g_source_remove (priv->whois_timer);
      priv->whois_timer = 0;
    }
}

static void
signal_data(GibberRMulticastSender *sender, guint16 stream_id,
            guint8 *data, gsize size) {
  set_state (sender,
    MAX(GIBBER_R_MULTICAST_SENDER_STATE_DATA_RUNNING, sender->state));

  g_signal_emit(sender, signals[RECEIVED_DATA], 0, stream_id, data, size);
}

static void
signal_control_packet(GibberRMulticastSender *sender,
    GibberRMulticastPacket *packet)
{
  set_state (sender,
    MAX(GIBBER_R_MULTICAST_SENDER_STATE_RUNNING, sender->state));

  g_signal_emit (sender, signals[RECEIVED_CONTROL_PACKET], 0, packet);
}

static void
signal_failure (GibberRMulticastSender *sender)
{
  if (sender->state >= GIBBER_R_MULTICAST_SENDER_STATE_FAILED)
    return;

  DEBUG_SENDER (sender, "Signalling senders failure");
  cancel_failure_timers (sender);
  g_signal_emit (sender, signals[FAILED], 0);
}

static gboolean
name_discovery_failed_cb (gpointer data)
{
  GibberRMulticastSender *self = GIBBER_R_MULTICAST_SENDER (data);
  GibberRMulticastSenderPrivate *priv =
    GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (self);

  DEBUG_SENDER (self, "Failed to discover name in time");

  g_assert (priv->whois_timer != 0);

  g_source_remove (priv->whois_timer);
  priv->whois_timer = 0;
  priv->fail_timer = 0;

  signal_failure (self);

  return FALSE;
}

static gboolean
progress_failed_cb (gpointer data)
{
  GibberRMulticastSender *self = GIBBER_R_MULTICAST_SENDER (data);
  GibberRMulticastSenderPrivate *priv =
    GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (self);

  DEBUG_SENDER (self, "Failed to make progress in time");

  priv->fail_timer = 0;

  signal_failure (self);

  return FALSE;
}

static void
schedule_progress_timer (GibberRMulticastSender *self)
{
  GibberRMulticastSenderPrivate *priv =
    GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (self);

  /* If we didn't discover the name, that timer is still running */
  if (self->name == NULL)
    return;

  /* No need for a watchdog if it has failed already */
  if (self->state >= GIBBER_R_MULTICAST_SENDER_STATE_FAILED)
    return;

  if (priv->fail_timer != 0)
    g_source_remove (priv->fail_timer);

  priv->fail_timer = g_timeout_add (MAX_PROGRESS_TIMEOUT,
      progress_failed_cb, self);
}

static void
name_discovered (GibberRMulticastSender *self, const gchar *name)
{
  GibberRMulticastSenderPrivate *priv =
    GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (self);

  if (priv->whois_timer != 0)
    {
      g_source_remove (priv->whois_timer);
      priv->whois_timer = 0;
    }

  if (priv->fail_timer != 0)
    {
      g_source_remove (priv->fail_timer);
      priv->fail_timer = 0;
    }

  self->name = g_strdup(name);
  DEBUG_SENDER(self, "Name discovered");
  g_signal_emit(self, signals[NAME_DISCOVERED], 0, self->name);

  schedule_progress_timer (self);
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

  if (sender->state > GIBBER_R_MULTICAST_SENDER_STATE_STOPPED)
    return;

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

  priv->whois_timer = 0;

  DEBUG_SENDER(sender, "Sending out whois request");
  g_signal_emit(sender, signals[WHOIS_REQUEST], 0);

  schedule_whois_request(sender, TRUE);

  return FALSE;
}

static void
schedule_whois_request(GibberRMulticastSender *sender, gboolean rescheduled) {
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE(sender);
   gint timeout;

   if (sender->state >= GIBBER_R_MULTICAST_SENDER_STATE_FAILED)
     return;

   if (rescheduled)
    timeout = g_random_int_range(MIN_WHOIS_TIMEOUT, MAX_WHOIS_TIMEOUT);
   else
    timeout = g_random_int_range(MIN_FIRST_WHOIS_TIMEOUT,
        MAX_FIRST_WHOIS_TIMEOUT);

   DEBUG_SENDER(sender, "(Re)Scheduled whois request in %d ms", timeout);

   if (priv->whois_timer != 0)
     g_source_remove (priv->whois_timer);

   priv->whois_timer = g_timeout_add(timeout, do_whois_request, sender);
}

static gboolean
check_depends(GibberRMulticastSender *sender,
              GibberRMulticastPacket *packet,
              gboolean data) {
  guint i;
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);

  g_assert (GIBBER_R_MULTICAST_PACKET_IS_RELIABLE_PACKET(packet));

  for (i = 0; i < packet->depends->len; i++) {
    GibberRMulticastSender *s;
    GibberRMulticastPacketSenderInfo *sender_info;
    guint32 other;

    sender_info = g_array_index (packet->depends,
        GibberRMulticastPacketSenderInfo *, i);

    s = gibber_r_multicast_sender_group_lookup(priv->group,
        sender_info->sender_id);

    if (s == NULL
        || s->state == GIBBER_R_MULTICAST_SENDER_STATE_NEW
        || s->state == GIBBER_R_MULTICAST_SENDER_STATE_UNKNOWN_FAILED) {
      DEBUG_SENDER(sender, "Unknown node in dependency list of packet %x: %x",
          sender_info->sender_id, packet->packet_id);
      continue;
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
        if (s->state == GIBBER_R_MULTICAST_SENDER_STATE_FAILED)
          {
            DEBUG_SENDER(sender,
              "Asking failed node %x to complete it's messages up to %x",
              sender_info->sender_id, sender_info->packet_id);
            gibber_r_multicast_sender_update_end (s, sender_info->packet_id);
          }
        return FALSE;
    }
  }
  return TRUE;
}

static void
update_next_data_output_state (GibberRMulticastSender *self)
{
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (self);

  self->next_output_data_packet++;

  for (; self->next_output_data_packet != self->next_output_packet;
      self->next_output_data_packet++)
    {
      PacketInfo *p;
      p = g_hash_table_lookup(priv->packet_cache,
          &(self->next_output_data_packet));

      if (p == NULL)
        continue;

      if (p->packet->type == PACKET_TYPE_DATA
        && (p->packet->data.data.flags & GIBBER_R_MULTICAST_DATA_PACKET_END))
        {
          break;
        }
    }
}

static gboolean
pop_data_packet (GibberRMulticastSender *sender)
{
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);
  PacketInfo *p;
  guint16 stream_id;

  /* If we're holding before this, skip */
  if (priv->holding_data &&
    gibber_r_multicast_packet_diff (sender->next_output_data_packet,
      priv->holding_point)
      <= 0)
    {
      DEBUG_SENDER (sender, "Holding back data finishing at %x",
        sender->next_output_data_packet);
      return FALSE;
    }

  DEBUG_SENDER (sender, "Trying to pop data finishing at %x",
    sender->next_output_data_packet);

  p = g_hash_table_lookup(priv->packet_cache,
    &sender->next_output_data_packet);
  g_assert (p != NULL);

  g_assert (p->packet->data.data.flags & GIBBER_R_MULTICAST_DATA_PACKET_END);

  stream_id = p->packet->data.data.stream_id;

  /* Backwards search for the start, validate the pieces and check the size */
  if (!(p->packet->data.data.flags & GIBBER_R_MULTICAST_DATA_PACKET_START))
    {
      guint32 i;
      gboolean found = FALSE;

      for (i = p->packet->packet_id - 1;
        gibber_r_multicast_packet_diff (priv->first_packet, i) >= 0; i--)
        {
           p = g_hash_table_lookup(priv->packet_cache, &i);
           if (p == NULL)
             continue;

           if (p->packet->type == PACKET_TYPE_DATA
             && p->packet->data.data.stream_id == stream_id
             &&  (p->packet->data.data.flags &
                  GIBBER_R_MULTICAST_DATA_PACKET_START))
               {
                 found = TRUE;
                 break;
               }
        }

      if (!found)
        {
          /* If we couldn't find the start it must have happened before we
           * joined the causal ordering */
          DEBUG_SENDER (sender,
            "Ignoring data starting before our first packet");
          update_next_data_output_state (sender);
          return TRUE;
        }
    }

  /* p is guaranteed to be the PacketInfo of the first packet */

  /* If there is data from before our startpoint, ignore it */
  if (sender->state != GIBBER_R_MULTICAST_SENDER_STATE_DATA_RUNNING
      && !priv->start_data)
  {
     DEBUG_SENDER (sender,
         "Ignoring data as we don't have a data startpoint yet");
     update_next_data_output_state (sender);
     return TRUE;
  }

  if (priv->start_data &&
      gibber_r_multicast_packet_diff (priv->start_point, p->packet_id) < 0)
    {
       DEBUG_SENDER (sender,
           "Ignoring data from before the data startpoint");
       update_next_data_output_state (sender);
       return TRUE;
    }


  if (!check_depends(sender, p->packet, TRUE)) {
    return FALSE;
  }

  /* Everything is fine, now do a forward pass to gather all the payload, we
   * could have cached this info, but oh well */
  DEBUG_SENDER (sender, "Popping data 0x%x -> 0x%x stream_id: %x",
    p->packet_id, sender->next_output_data_packet,
    p->packet->data.data.stream_id);

  if (p->packet->packet_id == sender->next_output_data_packet) {
    gsize size;
    guint8 *data;

    data = gibber_r_multicast_packet_get_payload(p->packet, &size);

    if (size != p->packet->data.data.total_size)
      goto incorrect_data_size;

    update_next_data_output_state (sender);
    signal_data(sender, p->packet->data.data.stream_id, data, size);

    p->popped = TRUE;
    packet_info_try_gc (sender, p);
  } else {
    gsize off = 0;
    guint8 *data = NULL, *d;
    guint32 payload_size, i;
    gsize size;

    payload_size = p->packet->data.data.total_size;
    data = g_malloc(payload_size);

    for (i = p->packet_id ; i != sender->next_output_data_packet + 1 ; i++)
      {
        PacketInfo *tp  = g_hash_table_lookup(priv->packet_cache, &i);

        if (tp == NULL)
          continue;

        if (tp->packet->type == PACKET_TYPE_DATA
          && tp->packet->data.data.stream_id == stream_id)
          {
             d = gibber_r_multicast_packet_get_payload(tp->packet, &size);
             if (off + size > payload_size)
               {
                 off += size;
                 break;
               }

             memcpy(data + off, d, size);
             off += size;

             tp->popped = TRUE;
             packet_info_try_gc (sender, tp);
          }
      }

    if (off != payload_size)
      {
        g_free (data);
        goto incorrect_data_size;
      }

    update_next_data_output_state (sender);
    signal_data(sender, stream_id, data, payload_size);
    g_free(data);
  }

  return TRUE;
incorrect_data_size:
  DEBUG_SENDER (sender, "Data packet didn't have the claimed amount of data");
  signal_failure (sender);
  return FALSE;
}

static void
update_acks (GibberRMulticastSender *sender, GibberRMulticastPacket *packet)
{
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);

  guint i;
  gboolean updated = FALSE;

  for (i = 0; i < packet->depends->len; i++)
    {
      GibberRMulticastPacketSenderInfo *senderinfo;
      AckInfo *info;

      senderinfo = g_array_index (packet->depends,
        GibberRMulticastPacketSenderInfo *, i);

      info = (AckInfo *) g_hash_table_lookup (priv->acks,
          &senderinfo->sender_id);

      if (G_UNLIKELY(info == NULL))
        {
          info = ack_info_new (senderinfo->sender_id);
          g_hash_table_insert (priv->acks, &info->sender_id, info);
          info->packet_id = senderinfo->packet_id;
          info->first_packet_id = packet->packet_id;
          updated = TRUE;
        }

      if (gibber_r_multicast_packet_diff (info->packet_id,
           senderinfo->packet_id) < 0) {
        DEBUG_SENDER (sender, "Acks are going backward!");
        signal_failure (sender);
        return;
      }

      if (gibber_r_multicast_packet_diff (info->packet_id,
          senderinfo->packet_id) > 0)
        {
           info->packet_id = senderinfo->packet_id;
           info->first_packet_id = packet->packet_id;
           updated = TRUE;
        }
    }

    if (updated)
      {
        gibber_r_multicast_sender_group_gc (priv->group);
      }
}

static gboolean
pop_packet(GibberRMulticastSender *sender) {
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);
  PacketInfo *p;

  DEBUG_SENDER (sender, "Next output: 0x%x Next output data: 0x%x",
      sender->next_output_packet, sender->next_output_data_packet);

  if (sender->next_output_data_packet != sender->next_output_packet) {
    /* We saw the end of some data message before the end of the data stream,
     * first try if we can pop this */
     if (pop_data_packet (sender))
         return TRUE;
  }

  if (sender->state == GIBBER_R_MULTICAST_SENDER_STATE_FAILED
      && gibber_r_multicast_packet_diff(priv->end_point,
        sender->next_output_packet) >= 0)
    {
       DEBUG_SENDER (sender, "Not looking at packets behind the endpoint");
       return FALSE;
    }

  p = g_hash_table_lookup(priv->packet_cache, &(sender->next_output_packet));

  DEBUG_SENDER(sender, "Looking at 0x%x", sender->next_output_packet);

  if (p == NULL || p->packet == NULL) {
    /* No packet yet.. too bad :( */
    DEBUG_SENDER(sender, "No new packets to pop");
    return FALSE;
  }

  g_assert (GIBBER_R_MULTICAST_PACKET_IS_RELIABLE_PACKET (p->packet));

  update_acks (sender, p->packet);

  if (!check_depends (sender, p->packet, FALSE))
    {
      return FALSE;
    }

  if (p->packet->type == PACKET_TYPE_DATA)
    {
      /* A data packet. If we had a potential end before this one, skip it
       * we're holding back the data for some reason otherwise check
       * if it's an end */

      if (sender->next_output_data_packet == sender->next_output_packet)
        {
          sender->next_output_packet++;
          /* If this is the end, try to pop it. Otherwise ignore */
          if (p->packet->data.data.flags & GIBBER_R_MULTICAST_DATA_PACKET_END)
            {
              /* If we could pop this, then advance next_output_data_packet
               * otherwise keep it at this location */
              pop_data_packet (sender);
            }
          else
            {
              sender->next_output_data_packet++;
            }
        }
      else
        sender->next_output_packet++;
    }
  else
    {
      if (sender->next_output_packet == sender->next_output_data_packet)
        sender->next_output_data_packet++;

      sender->next_output_packet++;

      if (p->packet->type != PACKET_TYPE_NO_DATA)
        {
         signal_control_packet (sender, p->packet);
        }

      p->popped = TRUE;
      packet_info_try_gc (sender, p);
    }

  /* We successfully popped a new packet (weee), reschedule our watch dog */
  schedule_progress_timer (sender);

  return TRUE;
}

static gboolean
do_pop_packets (GibberRMulticastSender *sender)
{
  gboolean popped = FALSE;
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);

  if (sender->state < GIBBER_R_MULTICAST_SENDER_STATE_PREPARING
      || sender->state > GIBBER_R_MULTICAST_SENDER_STATE_FAILED)
  {
    /* No popping untill we have at least some information */
    return FALSE;
  }

  /* Don't pop if our sender group was stopped */
  if (priv->group->stopped)
    return FALSE;

  g_object_ref (sender);

  while (sender->state <= GIBBER_R_MULTICAST_SENDER_STATE_FAILED)
    {
      if (!pop_packet (sender))
        break;

      popped = TRUE;
    }

  g_object_unref (sender);

  return popped;
}

static void
senders_collect(gpointer key, gpointer value, gpointer user_data) {
  GibberRMulticastSender *s = GIBBER_R_MULTICAST_SENDER(value);
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER (user_data);
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);

  if (s->state < GIBBER_R_MULTICAST_SENDER_STATE_PENDING_REMOVAL)
    g_queue_push_tail (priv->group->pop_queue, s);
}


static void
pop_packets(GibberRMulticastSender *sender) {
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);
  gboolean pop;


  if (priv->group->popping)
    {
      if (!g_queue_find (priv->group->pop_queue, sender))
        {
          /* Ensure that data is popped at the next opportunity */
          g_queue_push_tail (priv->group->pop_queue, sender);
        }
      return;
    }


  priv->group->popping = TRUE;

  g_object_ref(sender);

  pop = do_pop_packets (sender);

  /* If something is popped or a node queued itself for popping, go for it */
  while (pop || g_queue_peek_head(priv->group->pop_queue) != NULL)
    {
      GibberRMulticastSender *s;

      /* If something was popped, try to pop as much as possible from others in
       * this group. Else just pop all senders in the queue */
      if (pop)
        {
          while (g_queue_pop_head (priv->group->pop_queue) != NULL)
            /* pass */;

          g_hash_table_foreach(priv->group->senders, senders_collect, sender);
        }

      pop = FALSE;
      while ((s = g_queue_pop_head (priv->group->pop_queue)) != NULL)
        {
          pop |= do_pop_packets (s);
        }
    }

  priv->group->popping = FALSE;
  g_object_unref(sender);
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

  if (gibber_r_multicast_packet_diff(sender->next_input_packet,
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

  g_assert (sender->state < GIBBER_R_MULTICAST_SENDER_STATE_FAILED);

  DEBUG_SENDER (sender, "Updating start to %x", packet_id);
  if (sender->state == GIBBER_R_MULTICAST_SENDER_STATE_NEW) {
    g_assert(g_hash_table_size(priv->packet_cache) == 0);

    set_state (sender, GIBBER_R_MULTICAST_SENDER_STATE_PREPARING);

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
gibber_r_multicast_sender_update_end (GibberRMulticastSender *sender,
  guint32 packet_id)
{
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);

  g_assert (sender->state == GIBBER_R_MULTICAST_SENDER_STATE_FAILED);

  if  (gibber_r_multicast_packet_diff (priv->end_point, packet_id) >= 0);
    {
      DEBUG_SENDER (sender, "Updating end to %x", packet_id);
      priv->end_point = packet_id;
      pop_packets (sender);
    }
}

void
gibber_r_multicast_sender_set_failed (GibberRMulticastSender *sender)
{
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);

  if (sender->state >= GIBBER_R_MULTICAST_SENDER_STATE_FAILED)
    return;

  if (sender->state < GIBBER_R_MULTICAST_SENDER_STATE_PREPARING)
    {
      DEBUG_SENDER (sender, "Failed before we knew anything");
      set_state (sender, GIBBER_R_MULTICAST_SENDER_STATE_UNKNOWN_FAILED);
    }
  else
    {
      set_state (sender, GIBBER_R_MULTICAST_SENDER_STATE_FAILED);
      priv->end_point = sender->next_output_packet;
      DEBUG_SENDER (sender, "Marked sender as failed. Endpoint %x",
        priv->end_point);
    }

  cancel_failure_timers (sender);
}

void
gibber_r_multicast_sender_set_data_start (GibberRMulticastSender *sender,
    guint32 packet_id)
{
  GibberRMulticastSenderPrivate *priv =
      GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);

  g_assert (sender->state < GIBBER_R_MULTICAST_SENDER_STATE_DATA_RUNNING);

  DEBUG_SENDER (sender, "Setting data start at 0x%x", packet_id);

  priv->start_data = TRUE;
  priv->start_point = packet_id;
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

    if (info != NULL && info->packet != NULL)
      {
        schedule_do_repair(sender, id);
        return;
      }

    if (sender->state >= GIBBER_R_MULTICAST_SENDER_STATE_STOPPED)
      /* Beyond stopped state we only send out repairs for packets we have */
      return;

    if (info == NULL) {
      guint32 i;

      for (i = sender->next_output_packet ; i != id + 1; i++ ){
        schedule_repair(sender, i);
      }
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

  g_assert (sender != NULL);
  DEBUG_SENDER(sender, "Seen next packet 0x%x", id);

  if (sender->state < GIBBER_R_MULTICAST_SENDER_STATE_PREPARING
      || sender->state >= GIBBER_R_MULTICAST_SENDER_STATE_UNKNOWN_FAILED) {
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
  pop_packets(sender);
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

      if (sender->name != NULL)
        {
          if (priv->whois_timer == 0)
            {
              gint timeout = g_random_int_range(MIN_WHOIS_REPLY_TIMEOUT,
                                                MAX_WHOIS_REPLY_TIMEOUT);
              priv->whois_timer =
                g_timeout_add(timeout, do_whois_reply, sender);
              DEBUG_SENDER(sender, "Scheduled whois reply in %d ms", timeout);
            }
        }
      else
        {
          schedule_whois_request(sender, TRUE);
        }
      break;
    case PACKET_TYPE_WHOIS_REPLY:
      g_assert(packet->sender == sender->id);

      if (sender->name == NULL) {
        name_discovered (sender, packet->data.whois_reply.sender_name);
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

  if (repeat)
    {
      if (info->timeout == 0)
         schedule_do_repair (sender, packet_id);
    }
  else
   {
     packet_info_try_gc (sender, info);
   }

  /* FIXME: If repeat is turned off, we repeat it at least once more as there
   * might have been a repair request after the last repeating.. This is
   * ofcourse suboptimal */
}

guint
gibber_r_multicast_sender_packet_cache_size ( GibberRMulticastSender *sender)
{
  GibberRMulticastSenderPrivate *priv =
    GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);

  /* The important cache size is until our cutoff point, which can be less
   * then the last packet we actually did receive from this sender */
  if (sender->state >= GIBBER_R_MULTICAST_SENDER_STATE_FAILED)
    return gibber_r_multicast_packet_diff (priv->first_packet,
        priv->end_point);

  return gibber_r_multicast_packet_diff (priv->first_packet,
      sender->next_input_packet);
}

static AckInfo *
gibber_r_multicast_sender_get_ackinfo (GibberRMulticastSender *sender,
    guint32 sender_id)
{
  GibberRMulticastSenderPrivate *priv =
    GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);

  return (AckInfo *)g_hash_table_lookup (priv->acks, &sender_id);
}

void
gibber_r_multicast_sender_ack (GibberRMulticastSender *sender, guint32 ack)
{
  guint32 i;
  GibberRMulticastSenderPrivate *priv =
    GIBBER_R_MULTICAST_SENDER_GET_PRIVATE (sender);

  if (gibber_r_multicast_packet_diff (priv->first_packet, ack) < 0)
    {
      return;
    }

  for (i = priv->first_packet ; i != ack; i++)
    {
      PacketInfo *info;

      info = g_hash_table_lookup (priv->packet_cache, &i);
      if (info == NULL)
        continue;

      info->acked = TRUE;
      packet_info_try_gc (sender, info);
    }
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
  DEBUG_SENDER (sender, "Holding data starting at %x", packet_id);

  /* Pop packets in case the holding_point moved forward */
  pop_packets (sender);
}

/* Stop holding back data of the sender */
void
gibber_r_multicast_sender_release_data (GibberRMulticastSender *sender)
{
  GibberRMulticastSenderPrivate *priv =
    GIBBER_R_MULTICAST_SENDER_GET_PRIVATE(sender);

  DEBUG_SENDER (sender, "Releasing data");
  priv->holding_data = FALSE;
  pop_packets (sender);
}

static void
stop_packet (gpointer key, gpointer value, gpointer user_data)
{
  PacketInfo *p = (PacketInfo *)value;

  if (p->timeout != 0)
    {
      g_source_remove (p->timeout);
      p->timeout = 0;
    }
}

void
gibber_r_multicast_sender_stop (GibberRMulticastSender *sender)
{
  GibberRMulticastSenderPrivate *priv =
    GIBBER_R_MULTICAST_SENDER_GET_PRIVATE(sender);

  if (priv->whois_timer != 0)
    {
      g_source_remove (priv->whois_timer);
      priv->whois_timer = 0;
    }

  g_hash_table_foreach (priv->packet_cache, stop_packet, NULL);
  set_state (sender, GIBBER_R_MULTICAST_SENDER_STATE_STOPPED);
}

void
_gibber_r_multicast_TEST_sender_fail (GibberRMulticastSender *sender)
{
  signal_failure (sender);
}

