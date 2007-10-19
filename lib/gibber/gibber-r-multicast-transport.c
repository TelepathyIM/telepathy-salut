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

#define MIN_ATTEMPT_JOIN_TIMEOUT 100
#define MAX_ATTEMPT_JOIN_TIMEOUT 400

/* Time between the last attempt join packet and the start of the joining phase
 */
#define MIN_JOINING_START_TIMEOUT 4800
#define MAX_JOINING_START_TIMEOUT 5200

static void stop_send_attempt_join (GibberRMulticastTransport *self);

G_DEFINE_TYPE(GibberRMulticastTransport, gibber_r_multicast_transport,
              GIBBER_TYPE_TRANSPORT)

/* signal enum */
enum
{
    NEW_SENDERS,
    LOST_SENDERS,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum {
  PROP_TRANSPORT = 1,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GibberRMulticastTransportPrivate
    GibberRMulticastTransportPrivate;

typedef enum {
  /* Not connected yet */
  STATE_DISCONNECTED = 0,
  /* Normal traffic */
  STATE_NORMAL,
  /* Gathering new people */
  STATE_GATHERING,
  /* Joining */
  STATE_JOINING
} State;

struct _GibberRMulticastTransportPrivate
{
  gboolean dispose_has_run;
  GibberRMulticastCausalTransport *transport;
  GHashTable *members;

  guint32 attempt_join_id;
  gboolean repeating_join;
  gboolean send_empty;
  guint timeout;

  guint joining_timeout;
  /* People who were in the join message you sent */
  GArray *send_join;
  /* People who were marked as failures in our join */
  GArray *send_join_failures;

  /* People we saw to have failed, but not send a failure packet for yet */
  GArray *pending_failures;

  State state;
};

typedef enum {
  /* Never heard of this guy before */
  MEMBER_STATE_UNKNOWN = 0,
  /* Start of the join attempt (Don't know packet startpoint yet) */
  MEMBER_STATE_ATTEMPT_JOIN_STARTED,
  /* Need to repeat attempt join information to member */
  MEMBER_STATE_ATTEMPT_JOIN_REPEAT,
  /* Is a member of our reliable causal graph */
  MEMBER_STATE_ATTEMPT_JOIN_DONE,
  /* Received a JOIN message from this member */
  MEMBER_STATE_JOINING,
  /* Actually a member */
  MEMBER_STATE_MEMBER,
  /* failure! */
  MEMBER_STATE_FAILING,
  /* Node failing that was a member */
  MEMBER_STATE_MEMBER_FAILING,
  /* Failed before we even know what this node was all about */
  MEMBER_STATE_INSTANT_FAILURE,
} MemberState;

typedef struct {
  MemberState state;
  guint32 id;
  gboolean agreed_join;
  /* Failures recorded by this node */
  GArray *failures;
} MemberInfo;

#define GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_R_MULTICAST_TRANSPORT, \
   GibberRMulticastTransportPrivate))

static void free_member_info (gpointer info);
static MemberInfo *member_get_info (GibberRMulticastTransport *self,
    guint32 id);
static MemberState member_get_state (GibberRMulticastTransport *self,
    guint32 id);

struct HTData {
  GArray *senders;
  gboolean need_repeat;
};

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

  priv->members = g_hash_table_new_full (g_int_hash, g_int_equal,
      NULL, free_member_info);

  priv->pending_failures = g_array_new (FALSE, FALSE, sizeof(guint32));
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

  signals[NEW_SENDERS] =
    g_signal_new("new-senders",
                 G_OBJECT_CLASS_TYPE(gibber_r_multicast_transport_class),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__POINTER,
                 G_TYPE_NONE, 1, G_TYPE_POINTER);

  signals[LOST_SENDERS] =
    g_signal_new("lost-senders",
                 G_OBJECT_CLASS_TYPE(gibber_r_multicast_transport_class),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__POINTER,
                 G_TYPE_NONE, 1, G_TYPE_POINTER);

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

  if (priv->timeout != 0) {
    g_source_remove (priv->timeout);
    priv->timeout =0;
  }

  if (priv->joining_timeout != 0) {
    g_source_remove (priv->joining_timeout);
    priv->timeout =0;
  }

  if (priv->send_join != NULL) {
    g_array_free (priv->send_join, TRUE);
    priv->send_join = NULL;
  }

  if (priv->pending_failures != NULL)
    {
      g_array_free (priv->pending_failures, TRUE);
      priv->pending_failures = NULL;
    }

  /* release any references held by the object here */
  if (G_OBJECT_CLASS (gibber_r_multicast_transport_parent_class)->dispose)
    G_OBJECT_CLASS (gibber_r_multicast_transport_parent_class)->dispose (
        object);
}

void
gibber_r_multicast_transport_finalize (GObject *object) { GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (object);
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
  GibberRMulticastCausalBuffer *cbuffer =
    (GibberRMulticastCausalBuffer *)buffer;

  if (member_get_state (self, cbuffer->sender_id)
        == MEMBER_STATE_MEMBER) {
    gibber_transport_received_data_custom (GIBBER_TRANSPORT (self),
        buffer);
  }
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

static MemberInfo *
new_member (GibberRMulticastTransport *self, guint32 id)
{
  GibberRMulticastTransportPrivate *priv =
        GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);
  MemberInfo *info;

  g_assert (id != priv->transport->sender_id);

  info = g_slice_new0 (MemberInfo);
  info->id = id;
  info->failures = g_array_new (FALSE, FALSE, sizeof (guint32));
  g_hash_table_insert (priv->members, &info->id, info);

  return info;
}

static void
free_member_info (gpointer data)
{
  MemberInfo *info = (MemberInfo *)data;

  g_array_free (info->failures, TRUE);
  g_slice_free (MemberInfo, info);
}

static MemberState
member_get_state (GibberRMulticastTransport *self, guint32 id) {
  GibberRMulticastTransportPrivate *priv =
        GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);

  MemberInfo *info = g_hash_table_lookup (priv->members, &id);
  if (info == NULL) {
    return MEMBER_STATE_UNKNOWN;
  }
  return info->state;
}

static MemberInfo *
member_get_info (GibberRMulticastTransport *self, guint32 id) {
  GibberRMulticastTransportPrivate *priv =
        GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);

  MemberInfo *info = g_hash_table_lookup (priv->members, &id);

  if (info == NULL) {
    info = new_member (self, id);
  }

  return info;
}

static void
member_set_state (GibberRMulticastTransport *self, guint32 id,
    MemberState state) {
  GibberRMulticastTransportPrivate *priv =
        GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);

  MemberInfo *info = g_hash_table_lookup (priv->members, &id);
  if (info == NULL) {
    info = new_member (self, id);
  }

  info->state = state;
}

static void
str_add_member (gpointer key, MemberInfo *value, GString *str) {
  switch (value->state)
    {
      case MEMBER_STATE_UNKNOWN:
      case MEMBER_STATE_ATTEMPT_JOIN_STARTED:
        g_string_append_printf (str, "%x U, ", value->id);
        break;
     case MEMBER_STATE_ATTEMPT_JOIN_REPEAT:
     case MEMBER_STATE_ATTEMPT_JOIN_DONE:
     case MEMBER_STATE_JOINING:
        g_string_append_printf (str, "%x N, ", value->id);
        break;
     case MEMBER_STATE_MEMBER:
        g_string_append_printf (str, "%x M, ", value->id);
        break;
     case MEMBER_STATE_FAILING:
        g_string_append_printf (str, "%x F, ", value->id);
        break;
     case MEMBER_STATE_MEMBER_FAILING:
        g_string_append_printf (str, "%x MF, ", value->id);
        break;
     case MEMBER_STATE_INSTANT_FAILURE:
        g_string_append_printf (str, "%x IF, ", value->id);
        break;
    }
}


static gchar *
member_list_to_str (GibberRMulticastTransport *self) {
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);
  GString *str = g_string_sized_new (
      3 + 9 * g_hash_table_size (priv->members));

  g_string_append (str, "{ ");
  g_hash_table_foreach (priv->members, (GHFunc) str_add_member, str);
  g_string_insert (str, str->len - 1, " }");

  return g_string_free (str, FALSE);
}

static void
add_to_join (gpointer key, gpointer value, gpointer user_data) {
  MemberInfo *info = (MemberInfo *)value;
  GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (user_data);
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);

  g_assert (info->state >= MEMBER_STATE_ATTEMPT_JOIN_REPEAT);

  if (info->state <=  MEMBER_STATE_FAILING) {
    g_array_append_val (priv->send_join, info->id);
  }

  if (info->state >= MEMBER_STATE_FAILING)
    {
      g_array_append_val (priv->send_join_failures, info->id);
    }

  info->agreed_join = FALSE;
}

static void
check_join_state (gpointer key, MemberInfo *value, gpointer user_data)
{
  if (value->state < MEMBER_STATE_ATTEMPT_JOIN_REPEAT)
    {
      value->state = MEMBER_STATE_INSTANT_FAILURE;
    }
}

static void
start_joining_phase (GibberRMulticastTransport *self)
{
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);
  gchar *members;
  GibberRMulticastSender *sender;

  if (priv->state == STATE_GATHERING)
    {
      stop_send_attempt_join (self);
      g_source_remove (priv->joining_timeout);
      /* every member with state >= MEMBER_STATE_ATTEMPT_JOIN_REPEAT, will be
       * in our join */
      g_assert (priv->send_join == NULL);
      g_assert (priv->send_join_failures == NULL);
      priv->send_join = g_array_new (FALSE, FALSE, sizeof (guint32));
      priv->send_join_failures = g_array_new (FALSE, FALSE, sizeof (guint32));

      g_hash_table_foreach (priv->members, (GHFunc)check_join_state, self);
    }
  else
    {
      g_assert (priv->state == STATE_JOINING);
      g_array_free (priv->send_join, TRUE);
      g_array_free (priv->send_join_failures, TRUE);
      priv->send_join = g_array_new (FALSE, FALSE, sizeof (guint32));
    }

  members = member_list_to_str (self);
  DEBUG ("New join state: %s", members);
  g_free(members);

  priv->state = STATE_JOINING;
  g_hash_table_foreach (priv->members, (GHFunc) add_to_join, self);

  sender = gibber_r_multicast_causal_transport_get_sender (priv->transport,
    priv->transport->sender_id);

  /* We'll send them as failures as part of this join instead of in a dedicated
   * failure packet */
  if (priv->pending_failures->len > 0)
    {
      g_array_remove_range (priv->pending_failures, 0,
          priv->pending_failures->len);
    }
  gibber_r_multicast_sender_hold_data (sender,
      sender->next_input_packet);
  gibber_r_multicast_causal_transport_send_join (priv->transport,
    priv->send_join_failures);
}

static gboolean
do_start_joining_phase (gpointer user_data)
{
  GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (user_data);

  start_joining_phase (self);

  return FALSE;
}

static void
continue_gathering_phase (GibberRMulticastTransport *self) {
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);

  g_assert (priv->state != STATE_JOINING);

  if (priv->state != STATE_GATHERING) {
    DEBUG ("Entering gathering state");
    priv->state = STATE_GATHERING;
  }

  if (priv->joining_timeout != 0)
    g_source_remove (priv->joining_timeout);

  priv->joining_timeout = g_timeout_add (
    g_random_int_range (MIN_JOINING_START_TIMEOUT, MAX_JOINING_START_TIMEOUT),
    do_start_joining_phase, self);
}

static gboolean
guint32_array_contains (GArray *array, guint32 id) {
  int i;
  for (i = 0; i < array->len; i++) {
    if (g_array_index (array, guint32, i) == id)
      return TRUE;
  }
  return FALSE;
}

static void
guint32_array_remove (GArray *array, guint32 id)
{
  int i;
  for (i = 0; i < array->len; i++) {
    if (g_array_index (array, guint32, i) == id)
      {
        g_array_remove_index_fast (array, i);
        return;
      }
  }
}

static void
add_to_senders (gpointer key, gpointer value, gpointer user_data) {
  struct HTData *data = (struct HTData *)user_data;
  MemberInfo *info = (MemberInfo *)value;

  if (info->state == MEMBER_STATE_UNKNOWN) {
    info->state = MEMBER_STATE_ATTEMPT_JOIN_STARTED;
  }

  if (info->state == MEMBER_STATE_ATTEMPT_JOIN_STARTED) {
    g_array_append_val (data->senders, info->id);
    data->need_repeat = TRUE;
  }

  data->need_repeat |= (info->state == MEMBER_STATE_ATTEMPT_JOIN_REPEAT);
}

static void
stop_send_attempt_join (GibberRMulticastTransport *self) {
  GibberRMulticastTransportPrivate *priv =
      GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE(self);

  if (priv->repeating_join) {
    gibber_r_multicast_causal_transport_stop_attempt_join (priv->transport,
      priv->attempt_join_id);
    priv->repeating_join = FALSE;
  }

  if (priv->timeout != 0) {
    g_source_remove (priv->timeout);
    priv->timeout = 0;
  }
}

static gboolean
do_send_attempt_join (gpointer user_data) {
  GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (user_data);
  GibberRMulticastTransportPrivate *priv =
      GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);
  struct HTData data;

  priv->timeout = 0;
  stop_send_attempt_join (self);

  /* Overestimate the number of senders.. Prefer a bit more memory usage for a
   * short time over allocations */
  data.senders = g_array_sized_new (FALSE, FALSE, sizeof (guint32),
      g_hash_table_size (priv->members));
  data.need_repeat = FALSE;

  /* Collect all senders from which we still need startpoint */
  g_hash_table_foreach (priv->members, add_to_senders, &data);

  priv->repeating_join = data.need_repeat;

  if (priv->send_empty || priv->repeating_join) {
    priv->attempt_join_id =
      gibber_r_multicast_causal_transport_send_attempt_join (priv->transport,
          data.senders, priv->repeating_join);
    priv->send_empty = FALSE;
  }
  g_array_free (data.senders, TRUE);

  return FALSE;
}

static void
send_attempt_join (GibberRMulticastTransport *self, gboolean send_empty) {
  GibberRMulticastTransportPrivate *priv =
      GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE(self);

  g_assert (priv->state != STATE_JOINING);

  continue_gathering_phase (self);

  /* schedule a send attempt for later on. If there wasn't one scheduled just
   * yet */
  priv->send_empty |= send_empty;

  if (priv->timeout == 0) {
    /* No send attempt scheduled yet, schedule one now */
    priv->timeout = g_timeout_add (
      g_random_int_range (MIN_ATTEMPT_JOIN_TIMEOUT, MAX_ATTEMPT_JOIN_TIMEOUT),
      do_send_attempt_join, self);
  }

}

static gboolean
depends_list_contains (GArray *depends, guint32 id) {
  int i;

  for (i = 0; i < depends->len; i++) {
    GibberRMulticastPacketSenderInfo *info =
        g_array_index (depends, GibberRMulticastPacketSenderInfo *, i);

    if (info->sender_id == id)
      return TRUE;
  }

  return FALSE;
}

static gboolean
update_member (GibberRMulticastTransport *self,
  guint32 sender_id, MemberState newstate, guint32 packet_id) {
  GibberRMulticastTransportPrivate *priv =
      GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE(self);
  MemberInfo *info;
  gboolean changed;

  if (sender_id == priv->transport->sender_id)
    return FALSE;

  gibber_r_multicast_causal_transport_add_sender (priv->transport,
      sender_id);

  info = member_get_info (self, sender_id);

  changed = (info->state < newstate);

  info->state = MAX(info->state, newstate);

  if (changed && info->state >= MEMBER_STATE_ATTEMPT_JOIN_REPEAT
        && info->state < MEMBER_STATE_JOINING) {
    /* We are guaranteed that this member didn't send a join before this
     * packet_id */
    gibber_r_multicast_causal_transport_update_sender_start (priv->transport,
        sender_id, packet_id);
  }

  return changed;
}

static gboolean
update_foreign_member_list (GibberRMulticastTransport *self,
   GibberRMulticastPacket *packet, MemberState state) {
  int i;
  gboolean changed = FALSE;

  changed |= update_member (self, packet->sender, state, packet->packet_id);
  for (i = 0 ; i < packet->depends->len; i++) {
    GibberRMulticastPacketSenderInfo *info =
        g_array_index (packet->depends, GibberRMulticastPacketSenderInfo *, i);
    changed |= update_member (self, info->sender_id, state, info->packet_id);
  }
  return changed;
}

static void
received_foreign_packet_cb (GibberRMulticastCausalTransport *ctransport,
    GibberRMulticastPacket *packet, gpointer user_data) {
  GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (user_data);
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);

  if (priv->state == STATE_JOINING) {
    /* Once we started joining, ignore all foreign traffic */
    return;
  }

  if (packet->type == PACKET_TYPE_BYE)
    {
      /* Ignore foreign bye packets, we don't care about people that are
       * leaving */
      return;
    }

  /* Always add sender, regardless of the packet. So that the causal layer can
   * start requesting id -> name mapping */
  gibber_r_multicast_causal_transport_add_sender (priv->transport,
      packet->sender);

  if (IS_RELIABLE_PACKET (packet) || packet->type == PACKET_TYPE_SESSION) {
    if (packet->type == PACKET_TYPE_ATTEMPT_JOIN) {
      /* Either the sender wants a start position from us or has replied to one
       * or our AJ messages.. Or it's some completely unrelated packet and we
       * can handle it just like any other reliable packet */
      int i;
      gboolean self_in_senders = FALSE;

      for (i = 0; i < packet->data.attempt_join.senders->len; i++) {
        guint32 id = g_array_index (packet->data.attempt_join.senders,
            guint32, i);
        if (id == priv->transport->sender_id) {
          self_in_senders = TRUE;
          break;
        }
      }

      if (self_in_senders) {
        /* New depends want start numbers */
        update_foreign_member_list (self, packet,
          MEMBER_STATE_ATTEMPT_JOIN_REPEAT);
        send_attempt_join (self, TRUE);
        return;
      } else if (depends_list_contains (packet->depends,
          priv->transport->sender_id)) {
        if (member_get_state (self, packet->sender) ==
              MEMBER_STATE_ATTEMPT_JOIN_STARTED) {
          update_foreign_member_list (self, packet,
              MEMBER_STATE_ATTEMPT_JOIN_DONE);
          send_attempt_join (self, TRUE);
        } else {
          /* We got startinfo, including our own, but we didn't know we
           * requested it! Ignore for now, some other node in our group will
           * handle it. Don't care if we're the only one to receive this
           * packet, at some point we'll know we did ask for the information
           * and the sender will repeat it anyway.. Thus we'll do better some
           * next time :) */
        }
        return;
      }
    }

    if (packet->type != PACKET_TYPE_DATA
        || packet->data.data.packet_part == 0)
      {
        /* Foreign packet, with no mention of us.. Mark them as unknown */
        update_foreign_member_list (self, packet, MEMBER_STATE_UNKNOWN);
        send_attempt_join (self, FALSE);
      }
  }
}


static gboolean
cmp_member_attempt_join_state (gpointer key, gpointer value,
    gpointer user_data)
{
  MemberInfo *member = (MemberInfo *) value;
  GibberRMulticastPacket *packet = GIBBER_R_MULTICAST_PACKET (user_data);

  if (guint32_array_contains (packet->data.attempt_join.senders, member->id)) {
    return member->state > MEMBER_STATE_ATTEMPT_JOIN_STARTED;
  } else if (depends_list_contains (packet->depends, member->id)) {
    return FALSE;
  } else {
    return TRUE;
  }
}


/* Return 1 if the packets join state is a superset of ours, 0 if the info is
 * the same and -1 if we have info the packet doesn't  */
static gint cmp_attempt_join_state (GibberRMulticastTransport *transport,
    GibberRMulticastPacket *packet)
{
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (transport);

  g_assert (packet->type == PACKET_TYPE_ATTEMPT_JOIN);

  if (g_hash_table_find (priv->members, cmp_member_attempt_join_state,
      packet) != NULL) {
    return -1;
  }

  if (!guint32_array_contains (packet->data.attempt_join.senders,
        priv->transport->sender_id) && !depends_list_contains (packet->depends,
        priv->transport->sender_id))
    {
      return -1;
    }

  if (g_hash_table_size (priv->members) + 1 ==
      packet->data.attempt_join.senders->len + packet->depends->len) {
    return 0;
  }

  return 1;
}

static void
send_failure_packet (GibberRMulticastTransport *self)
{
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);

  g_assert (priv->pending_failures != NULL);
  g_assert (priv->pending_failures->len != 0);

  gibber_r_multicast_causal_transport_send_failure (priv->transport,
    priv->pending_failures);

  priv->pending_failures =
    g_array_remove_range (priv->pending_failures, 0,
      priv->pending_failures->len);
}

static gboolean
find_unfailed_member (gpointer key, gpointer value, gpointer user_data)
{
  int i;
  guint32 *id = (guint32 *)user_data;
  MemberInfo *info = (MemberInfo *)value;

  if (info->state < MEMBER_STATE_ATTEMPT_JOIN_DONE ||
      info->state > MEMBER_STATE_MEMBER)
    return FALSE;

  for (i = 0; i < info->failures->len; i++)
    {
      if (*id == g_array_index (info->failures, guint32, i))
        return FALSE;
    }

  return TRUE;
}

static void
remove_failure (gpointer key, gpointer value, gpointer user_data)
{
  guint32 *id = (guint32 *)user_data;
  MemberInfo *info = (MemberInfo *)value;

  guint32_array_remove (info->failures, *id);
}

static void
check_failure_completion (GibberRMulticastTransport *self, guint32 id)
{
  GibberRMulticastTransportPrivate *priv =
      GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE(self);
  GibberRMulticastSender *sender;
  MemberInfo *info;

  if (g_hash_table_find (priv->members, find_unfailed_member, &id) != NULL)
    {
      return;
    }

  info = member_get_info (self, id);
  g_hash_table_foreach (priv->members, remove_failure, &id);

  DEBUG ("Failure process finished for %x\n", id);
  sender = gibber_r_multicast_causal_transport_get_sender (priv->transport,
      id);
  if (info->state == MEMBER_STATE_MEMBER_FAILING)
    {
      GArray *lost = g_array_new (FALSE, FALSE, sizeof (gchar *));
      g_array_append_val (lost, sender->name);
      g_signal_emit (self, signals[LOST_SENDERS], 0, lost);
      g_array_free (lost, TRUE);
    }

  gibber_r_multicast_causal_transport_remove_sender (priv->transport, id);
  g_hash_table_remove (priv->members, &id);
}

static void
handle_failure_packet (GibberRMulticastTransport *self,
    GibberRMulticastPacket *packet)
{
  GibberRMulticastTransportPrivate *priv =
      GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE(self);
  MemberInfo *info;
  int i;

  info = member_get_info (self, packet->sender);
  for (i = 0; i < packet->data.failure.failures->len; i++)
    {
      guint32 id = g_array_index(packet->data.failure.failures, guint32, i);
      MemberInfo *finfo = member_get_info (self, id);

      DEBUG ("%x failed %x", packet->sender, id);
      if (finfo->state < MEMBER_STATE_FAILING)
        {
          g_array_append_val (priv->pending_failures, id);
          send_failure_packet (self);
          member_set_state (self, packet->sender,
            info->state == MEMBER_STATE_MEMBER ?
              MEMBER_STATE_MEMBER_FAILING : MEMBER_STATE_FAILING
          );
        }
      g_array_append_val (info->failures, id);
      check_failure_completion (self, id);
    }
}

static gint
check_join (GibberRMulticastTransport *self, GibberRMulticastPacket *packet)
{
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);
  guint i;
  guint seen;
  gint result = 0;

  g_assert (packet->type == PACKET_TYPE_JOIN);

  for (i = 0; i < packet->data.join.failures->len; i++)
    {
      MemberInfo *info;
      guint32 id;

      id = g_array_index (packet->data.join.failures, guint32 , i);

      if (id == priv->transport->sender_id)
        {
          /* Uh, oh, we failed! */
          DEBUG ("FAILURE FAILURE FAILURE");
          return 1;
        }

      info = member_get_info (self, id);
      if (info->state < MEMBER_STATE_ATTEMPT_JOIN_REPEAT)
        {
          info->state = MEMBER_STATE_INSTANT_FAILURE;
          result = -1;
        }
      else if (info->state < MEMBER_STATE_MEMBER)
        {
          info->state = MEMBER_STATE_FAILING;
          result = -1;
        }
      else if (info->state == MEMBER_STATE_MEMBER)
        {
          info->state = MEMBER_STATE_MEMBER_FAILING;
          result = -1;
        }
    }

  if (result != 0)
    return result;

  if (!depends_list_contains (packet->depends, priv->transport->sender_id))
    {
      /* Hmm, a join without us.. Odd.. Should not happen unless we're one of
       * the failures.. Was was handle earlier */
      g_assert_not_reached();
    }

  /* We already saw our own id in the depends */
  seen = 1;
  for (i = 0; i < priv->send_join->len; i++)
    {
      guint32 id = g_array_index (priv->send_join, guint32, i);

      if (id != packet->sender && !depends_list_contains (packet->depends,
          g_array_index (priv->send_join, guint32, i)))
        {
          DEBUG ("Node %x not contained in the join",
              g_array_index (priv->send_join, guint32, i));
          result = 1;

        }
      else
       {
         seen++;
       }
    }

  if (seen != packet->depends->len + 1)
    result = -1;

  return result;
}

static void
release_data (gpointer key, MemberInfo *value, GibberRMulticastTransport *self)
{
  GibberRMulticastTransportPrivate *priv =
      GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE(self);
  GibberRMulticastSender *sender =
      gibber_r_multicast_causal_transport_get_sender (
          priv->transport, value->id);

  gibber_r_multicast_sender_release_data (sender);
}

static void
check_agreement (GibberRMulticastTransport *self)
{
  guint i;
  MemberInfo *info;
  GibberRMulticastSender *sender;
  GibberRMulticastTransportPrivate *priv =
      GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE(self);
  GArray *new;
  GArray *lost;

  for (i = 0 ; i < priv->send_join->len; i++)
    {
      info = g_hash_table_lookup (priv->members,
        &g_array_index (priv->send_join, guint32, i));

      if (info->state >= MEMBER_STATE_FAILING)
        continue;

      if (!info->agreed_join)
        return;
    }

  DEBUG ("---Finished joining phase!!!!---");
  new = g_array_new (FALSE, FALSE, sizeof(gchar *));
  lost = g_array_new (FALSE, FALSE, sizeof(gchar *));

  for (i = 0 ; i < priv->send_join_failures->len; i++)
    {
      info = member_get_info (self,
        g_array_index (priv->send_join_failures, guint32, i));
      switch (info->state)
        {
          case MEMBER_STATE_MEMBER_FAILING: {
            GibberRMulticastSender *sender =
              gibber_r_multicast_causal_transport_get_sender (priv->transport,
                info->id);
            if (info->state == MEMBER_STATE_MEMBER_FAILING)
              {
                gchar *name = g_strdup (sender->name);
                g_array_append_val (lost, name);
              }
          }
          /* fallthrough */
          case MEMBER_STATE_FAILING:
            g_hash_table_foreach (priv->members, remove_failure, &(info->id));
            gibber_r_multicast_causal_transport_remove_sender (priv->transport,
                info->id);
            break;
          case MEMBER_STATE_INSTANT_FAILURE:
            break;
          default:
            g_assert_not_reached ();
        }
      g_hash_table_remove (priv->members, &(info->id));
    }

  for (i = 0 ; i < priv->send_join->len; i++)
    {
      GibberRMulticastSender *sender;
      info = g_hash_table_lookup (priv->members,
        &g_array_index (priv->send_join, guint32, i));

      if (info == NULL)
        continue;

      sender = gibber_r_multicast_causal_transport_get_sender (
              priv->transport, info->id);

      if (info->state != MEMBER_STATE_MEMBER &&
            info->state < MEMBER_STATE_FAILING)
        {
          DEBUG ("New member: %s (%x)", sender->name, info->id);
          info->state = MEMBER_STATE_MEMBER;
          g_array_append_val (new, sender->name);
        }
    }

  if (lost->len > 0)
    g_signal_emit (self, signals[LOST_SENDERS], 0, lost);

  if (new->len > 0)
    g_signal_emit (self, signals[NEW_SENDERS], 0, new);

  for (i = 0 ; i < lost->len ; i++) {
    g_free (g_array_index (lost, gchar *, i));
  }

  g_array_free (lost, TRUE);
  g_array_free (new, TRUE);

  g_array_free (priv->send_join, TRUE);
  priv->send_join = NULL;
  g_array_free (priv->send_join_failures, TRUE);
  priv->send_join_failures = NULL;
  priv->state = STATE_NORMAL;
  DEBUG ("--------------------------------");

  sender = gibber_r_multicast_causal_transport_get_sender (priv->transport,
      priv->transport->sender_id);
  gibber_r_multicast_sender_release_data (sender);
  g_hash_table_foreach (priv->members, (GHFunc) release_data, self);
}

static void
received_control_packet_cb (GibberRMulticastCausalTransport *ctransport,
    GibberRMulticastSender *sender, GibberRMulticastPacket *packet,
    gpointer user_data)
{
  GibberRMulticastTransport *self =
    GIBBER_R_MULTICAST_TRANSPORT (user_data);
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);

  DEBUG ("Received control packet");
  switch (packet->type) {
    case PACKET_TYPE_ATTEMPT_JOIN: {
      int info_compare;
      int i;
      gboolean changed = FALSE;

      /* We can prevent/stop our join attempts iff some other node in our
       * current group is sending out some with more info */
      info_compare = cmp_attempt_join_state (self, packet);

      changed |= update_foreign_member_list (self, packet,
          MEMBER_STATE_ATTEMPT_JOIN_DONE);

      if (priv->state == STATE_JOINING) {
        /* Already started the joining process, so don't send or process new
         * joiners */
        break;
      }

      for (i = 0; i < packet->data.attempt_join.senders->len; i++)
        {
          guint32 id = g_array_index (packet->data.attempt_join.senders,
              guint32, i);
           changed |=
               update_member (self, id, MEMBER_STATE_ATTEMPT_JOIN_STARTED, 0);
        }

      continue_gathering_phase (self);

      switch (info_compare) {
        case 1:
          g_assert (changed);
          /* Another sender is sending a more usefull version, stop repeating
           * ours */
          stop_send_attempt_join (self);
          break;
        case 0:
          if (priv->timeout != 0 ||
              packet->sender > priv->transport->sender_id) {
            /* We didn't send out the latest info just yet so dont..
             * Or we're sending equavalent info, so use the sender_id to get a
             * winner */
            stop_send_attempt_join (self);
          }
          break;
        case -1:
          /* We have info the sender didn't have. Only start a new attempt if
           * something in our state actually changed */
          if (changed)
            send_attempt_join (self, FALSE);
          break;
      }
      break;
    }

    case PACKET_TYPE_JOIN: {
      DEBUG ("Received join from %s", sender->name);
      gibber_r_multicast_sender_hold_data (sender, packet->packet_id);

      if (priv->state == STATE_GATHERING)
        {
          start_joining_phase (self);
        }

      switch (check_join (self, packet)) {
        case 0: {
          MemberInfo *info;

          DEBUG ("%s agreed with our join", sender->name);

          info = g_hash_table_lookup (priv->members, &packet->sender);
          info->agreed_join = TRUE;
          check_agreement (self);
          break;
        }
        case 1:
          /* Our join had more info, so we don't need to resent it */
          DEBUG ("%s disagreed with our join with less info", sender->name);
          gibber_r_multicast_sender_release_data (
            gibber_r_multicast_causal_transport_get_sender (
              priv->transport, packet->sender));
          break;
        case -1:
          DEBUG ("%s disagreed with our join", sender->name);
          /* Start the joining phase again */
          start_joining_phase (self);
          check_agreement (self);
        break;
      }
      break;
    }
    case PACKET_TYPE_FAILURE:
      handle_failure_packet (self, packet);
      break;
    case PACKET_TYPE_BYE: {
      MemberInfo *info = member_get_info (self, packet->sender);

      if (info->state < MEMBER_STATE_FAILING)
        {
          member_set_state (self, packet->sender,
            info->state == MEMBER_STATE_MEMBER ?
              MEMBER_STATE_MEMBER_FAILING : MEMBER_STATE_FAILING);
          g_array_append_val (priv->pending_failures, packet->sender);

          send_failure_packet (self);
          check_failure_completion (self, packet->sender);
        }
      break;
    }
    default:
      break;
  }
}

gboolean
gibber_r_multicast_transport_connect(GibberRMulticastTransport *transport,
    GError **error)
{
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (transport);

  g_assert (gibber_transport_get_state (GIBBER_TRANSPORT(priv->transport)) ==
      GIBBER_TRANSPORT_CONNECTED);

  gibber_transport_set_state(GIBBER_TRANSPORT(transport),
         GIBBER_TRANSPORT_CONNECTED);

  priv->state = STATE_NORMAL;

  g_signal_connect (priv->transport, "received-foreign-packet",
      G_CALLBACK (received_foreign_packet_cb), transport);

  g_signal_connect (priv->transport, "received-control-packet",
      G_CALLBACK (received_control_packet_cb), transport);

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
transport_disconnected(GibberTransport *transport,
  gpointer user_data)
{
  GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (user_data);

  gibber_transport_set_state(GIBBER_TRANSPORT(self),
                             GIBBER_TRANSPORT_DISCONNECTED);
}

static void
gibber_r_multicast_transport_disconnect(GibberTransport *transport) {
  GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (transport);
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);

  priv->state = STATE_DISCONNECTED;

  gibber_transport_set_state(GIBBER_TRANSPORT(self),
                             GIBBER_TRANSPORT_DISCONNECTING);

  g_signal_connect (priv->transport, "disconnected",
      G_CALLBACK(transport_disconnected), self);
  gibber_transport_disconnect(GIBBER_TRANSPORT(priv->transport));
}
