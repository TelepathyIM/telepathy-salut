/*
 * gibber-r-multicast-causal-transport.c -
 *     Source for GibberRMulticastCausalTransport
 *
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

#define DEBUG_FLAG DEBUG_RMULTICAST
#include "gibber-debug.h"

#include "gibber-r-multicast-causal-transport.h"
#include "gibber-r-multicast-packet.h"
#include "gibber-r-multicast-sender.h"

#include "signals-marshal.h"

#define SESSION_TIMEOUT_MIN 1500
#define SESSION_TIMEOUT_MAX 3000

#define NR_JOIN_REQUESTS_TO_SEND 3
#define PASSIVE_JOIN_TIME  500
#define ACTIVE_JOIN_INTERVAL 250

/* At least send a reliable packet _with_ depends every 3 minutes as keepalive
 * and to ack pending data */
#define KEEPALIVE_TIMEOUT 180000

/* Send three bye's on disconnect at 500 ms intervals */
#define NR_BYE_TO_SEND 3
#define BYE_INTERVAL 500

#define DEBUG_TRANSPORT(transport, format,...) \
  DEBUG("%s (%x): " format, \
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE(transport)->name, \
      transport->sender_id, ##__VA_ARGS__)

struct hash_data {
  GibberRMulticastSender *sender;
  GibberRMulticastPacket *packet;
};

static void repair_message_cb (GibberRMulticastSender *sender,
    GibberRMulticastPacket *packet, gpointer user_data);
static void whois_reply_cb (GibberRMulticastSender *sender,
    gpointer user_data);

static void schedule_session_message (
    GibberRMulticastCausalTransport *transport);
static void schedule_keepalive_message (
    GibberRMulticastCausalTransport *transport);

G_DEFINE_TYPE(GibberRMulticastCausalTransport,
    gibber_r_multicast_causal_transport,
    GIBBER_TYPE_TRANSPORT)

/* signal enum */
enum
{
    SENDER_FAILED,
    RECEIVED_CONTROL_PACKET,
    RECEIVED_FOREIGN_PACKET,
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
typedef struct _GibberRMulticastCausalTransportPrivate
    GibberRMulticastCausalTransportPrivate;

struct _GibberRMulticastCausalTransportPrivate
{
  gboolean dispose_has_run;
  GibberTransport *transport;
  guint32 packet_id;
  GibberRMulticastSenderGroup *sender_group;
  GibberRMulticastSender *self;
  guint timer;
  guint keepalive_timer;
  gchar *name;

  gint nr_join_requests;
  gint nr_join_requests_seen;

  gint nr_bye;

  gboolean resetting;
};

#define GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_R_MULTICAST_CAUSAL_TRANSPORT,\
   GibberRMulticastCausalTransportPrivate))

static guint32
_random_nonzero_uint(void)
{
  guint32 result;

  do {
    result = g_random_int ();
  } while (result == 0);

  return result;
}

static void
gibber_r_multicast_causal_transport_set_property (GObject *object,
                                                  guint property_id,
                                                  const GValue *value,
                                                  GParamSpec *pspec)
{
  GibberRMulticastCausalTransport *transport =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (object);
  GibberRMulticastCausalTransportPrivate *priv =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (transport);

  switch (property_id) {
    case PROP_NAME:
      /* Only set on initialisation */
      priv->name = g_value_dup_string (value);
      break;
    case PROP_TRANSPORT:
      priv->transport = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gibber_r_multicast_causal_transport_get_property (GObject *object,
                                                  guint property_id,
                                                  GValue *value,
                                                  GParamSpec *pspec)
{
  GibberRMulticastCausalTransport *transport =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (object);
  GibberRMulticastCausalTransportPrivate *priv =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (transport);

  switch (property_id) {
    case PROP_NAME:
      g_value_set_string (value, priv->self->name);
      break;
    case PROP_TRANSPORT:
      g_value_set_object (value, priv->transport);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


static void
gibber_r_multicast_causal_transport_init (GibberRMulticastCausalTransport *obj)
{
  GibberRMulticastCausalTransportPrivate *priv =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  priv->sender_group = gibber_r_multicast_sender_group_new ();
  priv->packet_id = g_random_int ();
}

static void gibber_r_multicast_causal_transport_dispose (GObject *object);
static void gibber_r_multicast_causal_transport_finalize (GObject *object);

static gboolean gibber_r_multicast_causal_transport_do_send (
    GibberTransport *transport, const guint8 *data, gsize size,
    GError **error);

static void gibber_r_multicast_causal_transport_disconnect (
    GibberTransport *transport);

static void
gibber_r_multicast_causal_transport_class_init (
    GibberRMulticastCausalTransportClass *
        gibber_r_multicast_causal_transport_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (
      gibber_r_multicast_causal_transport_class);
  GibberTransportClass *transport_class =
          GIBBER_TRANSPORT_CLASS (gibber_r_multicast_causal_transport_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gibber_r_multicast_causal_transport_class,
      sizeof (GibberRMulticastCausalTransportPrivate));

  object_class->dispose = gibber_r_multicast_causal_transport_dispose;
  object_class->finalize = gibber_r_multicast_causal_transport_finalize;

  signals[SENDER_FAILED] =
    g_signal_new ("sender-failed",
        G_OBJECT_CLASS_TYPE (gibber_r_multicast_causal_transport_class),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL,
        g_cclosure_marshal_VOID__OBJECT,
        G_TYPE_NONE, 1, GIBBER_TYPE_R_MULTICAST_SENDER);

  signals[RECEIVED_CONTROL_PACKET] =
    g_signal_new ("received-control-packet",
        G_OBJECT_CLASS_TYPE (gibber_r_multicast_causal_transport_class),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL,
        _gibber_signals_marshal_VOID__OBJECT_OBJECT,
        G_TYPE_NONE, 2, GIBBER_TYPE_R_MULTICAST_SENDER,
        GIBBER_TYPE_R_MULTICAST_PACKET);

  signals[RECEIVED_FOREIGN_PACKET] =
    g_signal_new ("received-foreign-packet",
        G_OBJECT_CLASS_TYPE (gibber_r_multicast_causal_transport_class),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL,
        g_cclosure_marshal_VOID__OBJECT,
        G_TYPE_NONE, 1, GIBBER_TYPE_R_MULTICAST_PACKET);

  object_class->set_property =
      gibber_r_multicast_causal_transport_set_property;
  object_class->get_property =
      gibber_r_multicast_causal_transport_get_property;

  param_spec = g_param_spec_object ("transport", "transport",
      "The underlying Transport", GIBBER_TYPE_TRANSPORT,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property(object_class, PROP_TRANSPORT, param_spec);

  param_spec = g_param_spec_string("name", "name",
      "The name to use on the protocol", NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property(object_class, PROP_NAME, param_spec);

  transport_class->send = gibber_r_multicast_causal_transport_do_send;
  transport_class->disconnect = gibber_r_multicast_causal_transport_disconnect;
}

void
gibber_r_multicast_causal_transport_dispose (GObject *object)
{
  GibberRMulticastCausalTransport *self =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (object);
  GibberRMulticastCausalTransportPrivate *priv =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;
  if (priv->transport != NULL) {
    g_object_unref (priv->transport);
    priv->transport = NULL;
  }

  if (priv->timer != 0) {
    g_source_remove (priv->timer);
  }

  if (priv->keepalive_timer != 0)
    {
      g_source_remove(priv->keepalive_timer);
    }

  if (priv->self != NULL)
    {
      g_object_unref (priv->self);
      priv->self = NULL;
    }

  gibber_r_multicast_sender_group_free (priv->sender_group);

  /* release any references held by the object here */
  if (G_OBJECT_CLASS (
      gibber_r_multicast_causal_transport_parent_class)->dispose)
  {
    G_OBJECT_CLASS (gibber_r_multicast_causal_transport_parent_class)->dispose
       (object);
  }
}

void
gibber_r_multicast_causal_transport_finalize (GObject *object)
{
  GibberRMulticastCausalTransport *self =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (object);
  GibberRMulticastCausalTransportPrivate *priv =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  g_free (priv->name);

  G_OBJECT_CLASS (
      gibber_r_multicast_causal_transport_parent_class)->finalize (object);
}

static gboolean
sendout_packet (GibberRMulticastCausalTransport *transport,
                GibberRMulticastPacket *packet,
                GError **error)
{
  GibberRMulticastCausalTransportPrivate *priv =
    GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (transport);
  guint8 *rawdata;
  gsize rawsize;

  if (GIBBER_R_MULTICAST_PACKET_IS_RELIABLE_PACKET (packet)
      && (packet->depends->len != 0
          || g_hash_table_size(priv->sender_group->senders) == 0))
    schedule_keepalive_message (transport);

  rawdata = gibber_r_multicast_packet_get_raw_data(packet, &rawsize);
  return gibber_transport_send(GIBBER_TRANSPORT(priv->transport),
                               rawdata, rawsize, error);
}

static gchar *
g_array_uint32_to_str (GArray *array)
{
  int i;
  GString *str;

  if (array == NULL || array->len == 0) {
    return g_strdup (" { } ");
  }

  str= g_string_sized_new(4 + 10 * array->len);
  str = g_string_append (str, " {");
  for (i = 0; i < array->len; i++)
  {
    g_string_append_printf (str, " %x,", g_array_index (array, guint32, i));
  }
  g_string_truncate (str, str->len - 1);
  g_string_append (str, " }");

  return g_string_free (str, FALSE);
}

static void
add_sender_info (gpointer key,
                 gpointer value,
                 gpointer user_data)
{
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER (value);
  GibberRMulticastPacket *packet = GIBBER_R_MULTICAST_PACKET (user_data);
  gboolean r;

  if (sender->state == GIBBER_R_MULTICAST_SENDER_STATE_NEW ||
      sender->state >= GIBBER_R_MULTICAST_SENDER_STATE_FAILED)
    return;

  r = gibber_r_multicast_packet_add_sender_info (packet, sender->id,
               sender->next_input_packet, NULL);
  g_assert (r);
}

static gboolean
sendout_session_cb (gpointer data)
{
  GibberRMulticastCausalTransport *self =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (data);
  GibberRMulticastCausalTransportPrivate *priv =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (self);

  GibberRMulticastPacket *packet =
      gibber_r_multicast_packet_new (PACKET_TYPE_SESSION, priv->self->id,
          priv->transport->max_packet_size);

  DEBUG_TRANSPORT (self, "Preparing session message");
  g_hash_table_foreach (priv->sender_group->senders, add_sender_info, packet);
  DEBUG_TRANSPORT (self, "Sending out session message");
  sendout_packet (self, packet, NULL);
  g_object_unref (packet);

  priv->timer = 0;
  schedule_session_message (self);

  return FALSE;
}

static void
schedule_session_message (GibberRMulticastCausalTransport *transport)
{
  GibberRMulticastCausalTransportPrivate *priv =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (transport);

  if (priv->timer != 0)
    g_source_remove(priv->timer);

  priv->timer =
      g_timeout_add (
          g_random_int_range (SESSION_TIMEOUT_MIN, SESSION_TIMEOUT_MAX),
          sendout_session_cb, transport);
}

static void
connected (GibberRMulticastCausalTransport *transport)
{
  GibberRMulticastCausalTransportPrivate *priv =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE(transport);
  GibberRMulticastPacket *packet;

  DEBUG_TRANSPORT (transport, "Connected to group");

  priv->self = gibber_r_multicast_sender_new (transport->sender_id, priv->name,
      priv->sender_group);
  gibber_r_multicast_sender_update_start (priv->self, priv->packet_id);
  gibber_r_multicast_sender_set_data_start (priv->self, priv->packet_id);

  gibber_r_multicast_sender_group_add (priv->sender_group, priv->self);

  g_object_ref (priv->self);

  g_signal_connect (priv->self, "repair-message",
      G_CALLBACK (repair_message_cb), transport);
  g_signal_connect (priv->self, "whois-reply",
      G_CALLBACK (whois_reply_cb), transport);

  gibber_transport_set_state (GIBBER_TRANSPORT (transport),
         GIBBER_TRANSPORT_CONNECTED);

  /* Send out an unsolicited whois reply */
  packet = gibber_r_multicast_packet_new (PACKET_TYPE_WHOIS_REPLY,
        transport->sender_id, priv->transport->max_packet_size);

  gibber_r_multicast_packet_set_whois_reply_info (packet, priv->name);

  sendout_packet (transport, packet, NULL);
  g_object_unref (packet);

  schedule_session_message (transport);
  schedule_keepalive_message (transport);
}

static gboolean
next_join_step (gpointer data)
{
  GibberRMulticastCausalTransport *transport =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (data);
  GibberRMulticastCausalTransportPrivate *priv =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (transport);

  DEBUG_TRANSPORT (transport, "step: %d", priv->nr_join_requests);

  if (priv->nr_join_requests < NR_JOIN_REQUESTS_TO_SEND)
    {
      GibberRMulticastPacket *packet;

      priv->nr_join_requests++;

      /* Set sender to 0 as we don't have an official id yet */
      packet = gibber_r_multicast_packet_new (PACKET_TYPE_WHOIS_REQUEST,
          0, priv->transport->max_packet_size);

      gibber_r_multicast_packet_set_whois_request_info (packet,
          transport->sender_id);

      sendout_packet (transport, packet, NULL);
      g_object_unref (packet);

      priv->timer = g_timeout_add (ACTIVE_JOIN_INTERVAL,
          next_join_step, transport);
    }
  else
    {
      connected (transport);
    }
  return FALSE;
}



static void
start_joining (GibberRMulticastCausalTransport *transport)
{
  GibberRMulticastCausalTransportPrivate *priv =
    GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (transport);

  transport->sender_id = _random_nonzero_uint ();
  priv->nr_join_requests = 0;
  priv->nr_join_requests_seen = 0;

  DEBUG_TRANSPORT (transport, "Started joining");

  if (priv->timer != 0)
  {
    g_source_remove (priv->timer);
  }

  priv->timer = g_timeout_add (PASSIVE_JOIN_TIME,
    next_join_step, transport);
}

static void
data_received_cb (GibberRMulticastSender *sender,
                  guint16 stream_id,
                  guint8 *data,
                  gsize size,
                  gpointer user_data)
{
  GibberRMulticastCausalBuffer rmbuffer;

  rmbuffer.buffer.data = data;
  rmbuffer.buffer.length = size;
  rmbuffer.sender = sender->name;
  rmbuffer.stream_id = stream_id;
  rmbuffer.sender_id = sender->id;

  gibber_transport_received_data_custom (GIBBER_TRANSPORT (user_data),
      (GibberBuffer *) &rmbuffer);
}

static void
control_packet_received_cb (GibberRMulticastSender *sender,
                            GibberRMulticastPacket *packet,
                            gpointer user_data)
{
  GibberRMulticastCausalTransport *self =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (user_data);

  g_signal_emit (self, signals[RECEIVED_CONTROL_PACKET], 0, sender, packet);
}

static void
repair_request_cb (GibberRMulticastSender *sender,
                   guint id,
                   gpointer user_data)
{
  GibberRMulticastCausalTransport *self =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (user_data);
  GibberRMulticastCausalTransportPrivate *priv =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (self);
  GibberRMulticastPacket *packet =
      gibber_r_multicast_packet_new (PACKET_TYPE_REPAIR_REQUEST,
          priv->self->id, priv->transport->max_packet_size);

  gibber_r_multicast_packet_set_repair_request_info (packet, sender->id, id);

  sendout_packet (self, packet, NULL);
  g_object_unref (packet);
}

static void
repair_message_cb (GibberRMulticastSender *sender,
                   GibberRMulticastPacket *packet,
                   gpointer user_data)
{
  GibberRMulticastCausalTransport *self =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (user_data);

  sendout_packet (self, packet, NULL);
}

static void
whois_reply_cb (GibberRMulticastSender *sender,
                gpointer user_data)
{
  GibberRMulticastCausalTransport *self =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (user_data);
  GibberRMulticastCausalTransportPrivate *priv =
    GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (self);
  GibberRMulticastPacket *packet =
    gibber_r_multicast_packet_new (PACKET_TYPE_WHOIS_REPLY,
        sender->id, priv->transport->max_packet_size);

  gibber_r_multicast_packet_set_whois_reply_info (packet, sender->name);

  sendout_packet (self, packet, NULL);
  g_object_unref (packet);
}

static void
whois_request_cb (GibberRMulticastSender *sender,
                  gpointer user_data)
{
  GibberRMulticastCausalTransport *self =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (user_data);
  GibberRMulticastCausalTransportPrivate *priv =
    GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (self);
  GibberRMulticastPacket *packet =
    gibber_r_multicast_packet_new (PACKET_TYPE_WHOIS_REQUEST,
        priv->self->id, priv->transport->max_packet_size);

  gibber_r_multicast_packet_set_whois_request_info (packet, sender->id);

  sendout_packet (self, packet, NULL);
  g_object_unref (packet);
}

static void
sender_failed_cb (GibberRMulticastSender *sender,
                  gpointer user_data)
{
  GibberRMulticastCausalTransport *self =
    GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (user_data);

  g_signal_emit(self, signals[SENDER_FAILED], 0, sender);
}

static void
name_discovered_cb(GibberRMulticastSender *sender, gchar *name,
    gpointer user_data)
{
  /* GibberRMulticastCausalTransport *self =
   *     GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (user_data);
   * g_signal_emit(self, signals[NEW_SENDER], 0, name);
   */
}

GibberRMulticastSender *
gibber_r_multicast_causal_transport_add_sender (
    GibberRMulticastCausalTransport *transport,
    guint32 sender_id)
{
  GibberRMulticastCausalTransportPrivate *priv =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (transport);
  GibberRMulticastSender *sender;

  sender = gibber_r_multicast_sender_group_lookup (priv->sender_group,
      sender_id);

  if (sender != NULL)
    return sender;

  sender = gibber_r_multicast_sender_new (sender_id, NULL, priv->sender_group);

  gibber_r_multicast_sender_group_add (priv->sender_group, sender);

  g_signal_connect (sender, "received-data",
      G_CALLBACK (data_received_cb), transport);

  g_signal_connect (sender, "received-control-packet",
      G_CALLBACK (control_packet_received_cb), transport);

  g_signal_connect (sender, "repair-message",
      G_CALLBACK (repair_message_cb), transport);
  g_signal_connect (sender, "repair-request",
      G_CALLBACK (repair_request_cb), transport);

  g_signal_connect (sender, "whois-request",
      G_CALLBACK (whois_request_cb), transport);
  g_signal_connect (sender, "whois-reply",
      G_CALLBACK (whois_reply_cb), transport);
  g_signal_connect (sender, "name-discovered",
      G_CALLBACK (name_discovered_cb), transport);

  g_signal_connect (sender, "failed",
      G_CALLBACK (sender_failed_cb), transport);

  return sender;
}


void gibber_r_multicast_causal_transport_update_sender_start (
    GibberRMulticastCausalTransport *transport,
    guint32 sender_id,
    guint32 packet_id)
{
  GibberRMulticastCausalTransportPrivate *priv =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (transport);
  GibberRMulticastSender *sender;

  sender = gibber_r_multicast_sender_group_lookup (priv->sender_group,
      sender_id);
  g_assert (sender != NULL);

  gibber_r_multicast_sender_update_start (sender, packet_id);
}

static void
handle_session_message (GibberRMulticastCausalTransport *self,
                        GibberRMulticastPacket *packet)
{
  GibberRMulticastCausalTransportPrivate *priv =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (self);
  int i;
  gboolean outdated = FALSE;

  g_assert (packet->type == PACKET_TYPE_SESSION);

  for (i = 0; i < packet->depends->len ; i++)
    {
      GibberRMulticastPacketSenderInfo *sender_info =
          g_array_index (packet->depends,
              GibberRMulticastPacketSenderInfo *, i);
      GibberRMulticastSender *sender =
          gibber_r_multicast_sender_group_lookup (priv->sender_group,
              sender_info->sender_id);

      if (sender == NULL)
        {
          /* We will hear about this guy in a reliable message we apparently
           * didn't receive yet */
          continue;
        }

      if (gibber_r_multicast_packet_diff (sender_info->packet_id,
                     sender->next_input_packet) > 0)
        {
          g_assert (sender->state > GIBBER_R_MULTICAST_SENDER_STATE_NEW);
          outdated = TRUE;
        }
      gibber_r_multicast_sender_seen (sender, sender_info->packet_id);
    }

  /* Reschedule the sending out of a session message if the received session
   * message was at least as up to date as us */
  if (!outdated &&
        g_hash_table_size (priv->sender_group->senders)
            == packet->depends->len + 1)
    {
      DEBUG_TRANSPORT (self, "Rescheduling session message");
      schedule_session_message(self);
    }
}

static void
handle_packet_depends (GibberRMulticastCausalTransport *self,
                       GibberRMulticastPacket *packet)
{
  GibberRMulticastCausalTransportPrivate *priv =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (self);
  int i;

  g_assert (GIBBER_R_MULTICAST_PACKET_IS_RELIABLE_PACKET (packet));

  for (i = 0 ; i <  packet->depends->len; i++)
    {
      GibberRMulticastPacketSenderInfo *sender_info =
          g_array_index (packet->depends,
              GibberRMulticastPacketSenderInfo *, i);
      GibberRMulticastSender *sender =
          gibber_r_multicast_sender_group_lookup (priv->sender_group,
              sender_info->sender_id);

      /* This might be a resent after which the sender was removed */
      if (sender != NULL)
        gibber_r_multicast_sender_seen (sender, sender_info->packet_id);
    }
}

static void
joining_multicast_receive (GibberRMulticastCausalTransport *self,
                           GibberRMulticastPacket *packet)
{

  GibberRMulticastCausalTransportPrivate *priv =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (self);

  DEBUG_TRANSPORT (self, "Received packet type: 0x%x from %x id %x",
     packet->type, packet->sender, packet->packet_id);

  if (packet->sender == self->sender_id)
    {
      DEBUG_TRANSPORT (self, "Detected collision with existing sender, "
        "restarting join process");
      start_joining (self);
      return;
    }

  if (packet->type == PACKET_TYPE_WHOIS_REQUEST &&
      packet->data.whois_request.sender_id == self->sender_id)
    {
      if (packet->sender != 0)
        {
          DEBUG_TRANSPORT (self, "Detected existing node querying "
             "for the same id, restarting join process");
          start_joining (self);
        }
      else
        {
          priv->nr_join_requests_seen++;
          if (priv->nr_join_requests < priv->nr_join_requests_seen)
            {
              DEBUG_TRANSPORT (self, "Detected another node probing for "
                  "the same id, restarting join process");
              start_joining (self);
            }
        }
    }
}

static void
joined_multicast_receive (GibberRMulticastCausalTransport *self,
                          GibberRMulticastPacket *packet)
{
  GibberRMulticastSender *sender = NULL;
  GibberRMulticastCausalTransportPrivate *priv =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (self);

  if (packet->sender == 0)
    {
      if (packet->type != PACKET_TYPE_WHOIS_REQUEST)
        {
          DEBUG_TRANSPORT (self, "Invalid packet (sender is 0, which is "
              "not valid for type %x)",
            packet->type);
          goto out;
        }

      DEBUG_TRANSPORT (self, "New sender polling for a unique id");
    }
  else
    {
      /* All packets with non-zero sender fall go through here to start
       * detecting foreign packets as early as possible */
      sender = gibber_r_multicast_sender_group_lookup (priv->sender_group,
              packet->sender);
      if (sender == NULL ||
          (sender != priv->self &&
             sender->state == GIBBER_R_MULTICAST_SENDER_STATE_NEW))
        {
          DEBUG_TRANSPORT (self, "Foreign packet Received type: 0x%x from %x",
              packet->type, packet->sender);
          g_signal_emit (self, signals[RECEIVED_FOREIGN_PACKET], 0, packet);

          sender = gibber_r_multicast_sender_group_lookup (priv->sender_group,
              packet->sender);
        }
      }

  if (sender == NULL || sender == priv->self)
    {
      goto out;
    }

  DEBUG_TRANSPORT (self, "Received packet type: 0x%x from %x",
      packet->type, packet->sender);

  switch (packet->type)
    {
    case PACKET_TYPE_WHOIS_REQUEST:
      sender = gibber_r_multicast_sender_group_lookup (priv->sender_group,
          packet->data.whois_request.sender_id);
      if (sender == NULL)
        goto out;
      /* fallthrough */
    case PACKET_TYPE_WHOIS_REPLY:
      gibber_r_multicast_sender_whois_push (sender, packet);
      break;
    case PACKET_TYPE_REPAIR_REQUEST:
        {
          GibberRMulticastSender *rsender;
          guint32 sender_id;

          sender_id = packet->data.repair_request.sender_id;
          rsender = gibber_r_multicast_sender_group_lookup (priv->sender_group,
              sender_id);

          g_assert (sender_id != 0);

          if (rsender != NULL)
            {
              gibber_r_multicast_sender_repair_request (rsender,
                 packet->data.repair_request.packet_id);
            }
          else
            {
              DEBUG ("Ignoring repair request for unknown original sender");
            }
          break;
        }
    case PACKET_TYPE_SESSION:
      handle_session_message (self, packet);
      break;
    default:
      if (GIBBER_R_MULTICAST_PACKET_IS_RELIABLE_PACKET (packet))
        {
          handle_packet_depends (self, packet);
          gibber_r_multicast_sender_push (sender, packet);
        }
      else
        {
          DEBUG_TRANSPORT (self, "Received unhandled packet type!!, ignoring");
        }
  }

out:
  return;
}

static void
r_multicast_receive (GibberTransport *transport,
                     GibberBuffer *buffer,
                     gpointer user_data)
{
  GibberRMulticastCausalTransport *self =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (user_data);
  GibberRMulticastPacket *packet = NULL;
  GError *error = NULL;

  packet = gibber_r_multicast_packet_parse (buffer->data,
      buffer->length, &error);

  if (packet == NULL)
    {
      DEBUG_TRANSPORT (self, "Failed to parse packet: %s", error->message);
    }
  else
    {
      switch (GIBBER_TRANSPORT (self)->state)
        {
          case GIBBER_TRANSPORT_CONNECTING:
            joining_multicast_receive (self, packet);
            break;
          case GIBBER_TRANSPORT_CONNECTED:
            joined_multicast_receive (self, packet);
            break;
          case GIBBER_TRANSPORT_DISCONNECTING:
            break;
          default:
            g_assert_not_reached ();
        }
    }

  if (error != NULL)
    g_error_free(error);

  if (packet != NULL)
    g_object_unref(packet);
}

GibberRMulticastCausalTransport *
gibber_r_multicast_causal_transport_new (GibberTransport *transport,
                                         const gchar *name)
{
  GibberRMulticastCausalTransport *result;

  g_assert (name != NULL && *name != '\0');

  result = g_object_new (GIBBER_TYPE_R_MULTICAST_CAUSAL_TRANSPORT,
      "name", name,
      "transport", transport,
      NULL);

  gibber_transport_set_handler (GIBBER_TRANSPORT (transport),
      r_multicast_receive, result);

  return result;
}

gboolean
gibber_r_multicast_causal_transport_connect (
    GibberRMulticastCausalTransport *transport,
    gboolean initial,
    GError **error)
{
  GibberRMulticastCausalTransportPrivate *priv =
    GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (transport);

  g_assert (priv->transport->max_packet_size > 128);

  gibber_transport_set_state (GIBBER_TRANSPORT (transport),
         GIBBER_TRANSPORT_CONNECTING);

  start_joining (transport);

  return TRUE;
}

static void
add_depend (gpointer key,
            gpointer value,
            gpointer user_data)
{
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER (value);
  struct hash_data *d = (struct hash_data *) user_data;
  gboolean r;

  if (sender->state < GIBBER_R_MULTICAST_SENDER_STATE_PREPARING
      || sender->state >= GIBBER_R_MULTICAST_SENDER_STATE_FAILED)
    return;

  if (sender == d->sender)
    return;

  r = gibber_r_multicast_packet_add_sender_info (d->packet, sender->id,
      sender->next_output_packet, NULL);
  g_assert (r);
}

static void
add_packet_depends (GibberRMulticastCausalTransport *self,
                    GibberRMulticastPacket *packet)
{
  GibberRMulticastCausalTransportPrivate *priv =
    GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (self);
  struct hash_data hd;

  hd.sender = priv->self;
  hd.packet = packet;
  g_hash_table_foreach (priv->sender_group->senders, add_depend, &hd);
}

static gboolean
send_keepalive_cb (gpointer data)
{
  GibberRMulticastCausalTransport *self =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (data);
  GibberRMulticastCausalTransportPrivate *priv =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (self);
  GibberRMulticastPacket *packet;

  /* Sending out a reliable packet will reschedule the keepalive */
  priv->keepalive_timer = 0;

  DEBUG ("Sending out keepalive");
  packet = gibber_r_multicast_packet_new (PACKET_TYPE_NO_DATA,
      priv->self->id, priv->transport->max_packet_size);

  gibber_r_multicast_packet_set_packet_id (packet, priv->packet_id++);
  add_packet_depends (self, packet);

  gibber_r_multicast_sender_push (priv->self, packet);
  sendout_packet (self, packet, NULL);
  g_object_unref (packet);

  return FALSE;
}


static void
schedule_keepalive_message (GibberRMulticastCausalTransport *transport)
{
  GibberRMulticastCausalTransportPrivate *priv =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (transport);

  if (priv->keepalive_timer != 0)
    g_source_remove(priv->keepalive_timer);

  priv->keepalive_timer =
      g_timeout_add (KEEPALIVE_TIMEOUT, send_keepalive_cb, transport);
}


gboolean
gibber_r_multicast_causal_transport_send (
    GibberRMulticastCausalTransport *transport,
    guint16 stream_id,
    const guint8 *data,
    gsize size,
    GError **error)
{
  GibberRMulticastCausalTransport *self =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (transport);
  GibberRMulticastCausalTransportPrivate *priv =
    GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (self);
  GibberRMulticastPacket *packet;
  gsize payloaded;
  gboolean ret = TRUE;

  if (priv->resetting)
    return TRUE;

  g_assert (priv->self != NULL);

  packet = gibber_r_multicast_packet_new (PACKET_TYPE_DATA, priv->self->id,
      priv->transport->max_packet_size);

  add_packet_depends (self, packet);
  payloaded = gibber_r_multicast_packet_add_payload (packet, data, size);

  if (payloaded < size)
    {
      do
        {
          gibber_r_multicast_packet_set_packet_id (packet, priv->packet_id++);
          gibber_r_multicast_packet_set_data_info (packet, stream_id, 0, size);
          ret = sendout_packet (self, packet, error);
          gibber_r_multicast_sender_push (priv->self, packet);
          g_object_unref (packet);

          packet = gibber_r_multicast_packet_new (PACKET_TYPE_DATA,
              priv->self->id, priv->transport->max_packet_size);
          payloaded += gibber_r_multicast_packet_add_payload (packet,
              data + payloaded, size - payloaded);
      } while (payloaded < size);
     gibber_r_multicast_packet_set_data_info (packet, stream_id,
        GIBBER_R_MULTICAST_DATA_PACKET_END, size);
   }
  else
    {
      gibber_r_multicast_packet_set_data_info (packet, stream_id,
        GIBBER_R_MULTICAST_DATA_PACKET_START
        | GIBBER_R_MULTICAST_DATA_PACKET_END, size);

    }
  gibber_r_multicast_packet_set_packet_id (packet, priv->packet_id++);
  gibber_r_multicast_sender_push (priv->self, packet);
  ret = sendout_packet (self, packet, error);
  g_object_unref (packet);

  return ret;
}

static gboolean
gibber_r_multicast_causal_transport_do_send(GibberTransport *transport,
                                            const guint8 *data,
                                            gsize size,
                                            GError **error)
{
  return gibber_r_multicast_causal_transport_send (
     GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (transport),
     GIBBER_R_MULTICAST_CAUSAL_DEFAULT_STREAM,
     data, size, error);
}

static void
reconnect (GibberRMulticastCausalTransport *self)
{
  GibberRMulticastCausalTransportPrivate *priv =
    GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (self);

  /* Remove all data and start connection phase */
  gibber_r_multicast_sender_group_free (priv->sender_group);
  priv->sender_group = gibber_r_multicast_sender_group_new ();
  priv->packet_id = g_random_int ();

  g_assert(gibber_r_multicast_causal_transport_connect (self, FALSE, NULL));
}

static void
disconnect_done (GibberRMulticastCausalTransport *self)
{
  GibberRMulticastCausalTransportPrivate *priv =
    GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (self);

  if (priv->timer != 0)
    {
      g_source_remove(priv->timer);
    }

  gibber_transport_disconnect (GIBBER_TRANSPORT (priv->transport));

  g_object_unref (priv->self);
  priv->self = NULL;

  gibber_transport_set_state (GIBBER_TRANSPORT (self),
    GIBBER_TRANSPORT_DISCONNECTED);
}

static gboolean
send_next_bye (gpointer data)
{
  GibberRMulticastCausalTransport *self =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (data);
  GibberRMulticastCausalTransportPrivate *priv =
    GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (self);
   GibberRMulticastPacket *packet;

   DEBUG ("Sending bye nr %d", priv->nr_bye);

   packet = gibber_r_multicast_packet_new (PACKET_TYPE_BYE,
     priv->self->id, priv->transport->max_packet_size);
   gibber_r_multicast_packet_set_packet_id (packet, priv->packet_id);

   sendout_packet (self, packet, NULL);
   g_object_unref (packet);

   priv->nr_bye++;

  if (priv->nr_bye < NR_BYE_TO_SEND)
    {
      priv->timer = g_timeout_add (BYE_INTERVAL,
          send_next_bye, self);
    }
  else if (priv->resetting)
    {
      reconnect (self);
    }
   else
    {
      disconnect_done (self);
    }

  return FALSE;
}

static void
do_disconnect (GibberRMulticastCausalTransport *transport)
{
  GibberRMulticastCausalTransport *self =
     GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (transport);
  GibberRMulticastCausalTransportPrivate *priv =
    GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (self);

  if (gibber_transport_get_state (GIBBER_TRANSPORT (self))
      == GIBBER_TRANSPORT_DISCONNECTING)
    return;

  if (priv->timer != 0)
    {
      g_source_remove(priv->timer);
    }

  if (priv->keepalive_timer != 0)
    {
      g_source_remove(priv->keepalive_timer);
    }

  gibber_transport_set_state (GIBBER_TRANSPORT (self),
                              GIBBER_TRANSPORT_DISCONNECTING);

  gibber_r_multicast_sender_group_stop (priv->sender_group);

  priv->nr_bye = 0;
  send_next_bye (self);
}

static void
gibber_r_multicast_causal_transport_disconnect (GibberTransport *transport)
{
  GibberRMulticastCausalTransport *self =
     GIBBER_R_MULTICAST_CAUSAL_TRANSPORT (transport);
  GibberRMulticastCausalTransportPrivate *priv =
    GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (self);

  priv->resetting = FALSE;
  if (gibber_transport_get_state (GIBBER_TRANSPORT (self))
       < GIBBER_TRANSPORT_CONNECTED)
    {
      disconnect_done (self);
    }
  else
    {
      do_disconnect (self);
    }
}

void
gibber_r_multicast_causal_transport_reset (
    GibberRMulticastCausalTransport *transport)
{
  GibberRMulticastCausalTransportPrivate *priv =
    GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (transport);

  priv->resetting = TRUE;
  do_disconnect (transport);
}

guint32
gibber_r_multicast_causal_transport_send_attempt_join (
    GibberRMulticastCausalTransport *transport,
    GArray *new_senders,
    gboolean repeat)
{
  GibberRMulticastCausalTransportPrivate *priv =
    GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (transport);
  GibberRMulticastPacket *packet;
  gchar *str;
  guint32 packet_id;

  packet = gibber_r_multicast_packet_new (PACKET_TYPE_ATTEMPT_JOIN,
      priv->self->id, priv->transport->max_packet_size);

  gibber_r_multicast_packet_set_packet_id (packet, priv->packet_id++);
  gibber_r_multicast_packet_attempt_join_add_senders (packet, new_senders,
      NULL);
  add_packet_depends (transport, packet);

  str = g_array_uint32_to_str (new_senders);
  DEBUG_TRANSPORT (transport, "Sending out %sAJ: %s",
      repeat ? "repeating " : "", str);
  g_free (str);

  gibber_r_multicast_sender_push (priv->self, packet);
  gibber_r_multicast_sender_set_packet_repeat (priv->self,
      packet->packet_id, repeat);

  sendout_packet (transport, packet, NULL);

  packet_id = packet->packet_id;
  g_object_unref (packet);

  return packet_id;
}

void gibber_r_multicast_causal_transport_stop_attempt_join (
    GibberRMulticastCausalTransport *transport,
    guint32 attempt_join_id)
{
  GibberRMulticastCausalTransportPrivate *priv =
    GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (transport);

  gibber_r_multicast_sender_set_packet_repeat (priv->self,
      attempt_join_id, FALSE);
}

void
gibber_r_multicast_causal_transport_send_failure (
    GibberRMulticastCausalTransport *transport,
    GArray *failures)
{
  GibberRMulticastCausalTransportPrivate *priv =
    GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (transport);
  GibberRMulticastPacket *packet;
  gchar *str;

  packet = gibber_r_multicast_packet_new (PACKET_TYPE_FAILURE,
      priv->self->id, priv->transport->max_packet_size);

  gibber_r_multicast_packet_set_packet_id (packet, priv->packet_id++);
  gibber_r_multicast_packet_failure_add_senders (packet, failures,
      NULL);
  add_packet_depends (transport, packet);

  str = g_array_uint32_to_str (failures);
  DEBUG_TRANSPORT (transport, "Sending out failure: %s", str);
  g_free (str);

  gibber_r_multicast_sender_push (priv->self, packet);

  sendout_packet (transport, packet, NULL);
}

void
gibber_r_multicast_causal_transport_send_join (
    GibberRMulticastCausalTransport *transport, GArray *failures) {
  GibberRMulticastCausalTransportPrivate *priv =
    GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (transport);
  GibberRMulticastPacket *packet;

  packet = gibber_r_multicast_packet_new(PACKET_TYPE_JOIN,
      priv->self->id, priv->transport->max_packet_size);

  gibber_r_multicast_packet_set_packet_id(packet, priv->packet_id++);
  add_packet_depends (transport, packet);
  gibber_r_multicast_packet_join_add_failures (packet, failures, NULL);

  gibber_r_multicast_sender_push (priv->self, packet);
  sendout_packet (transport, packet, NULL);
  g_object_unref (packet);
}

GibberRMulticastSender *
gibber_r_multicast_causal_transport_get_sender (
    GibberRMulticastCausalTransport *transport,
    guint32 sender_id)
{
  GibberRMulticastCausalTransportPrivate *priv =
    GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (transport);
  GibberRMulticastSender *sender;

  sender = gibber_r_multicast_sender_group_lookup (priv->sender_group,
              sender_id);
  g_assert (sender != NULL);

  return sender;
}

void
gibber_r_multicast_causal_transport_remove_sender (
    GibberRMulticastCausalTransport *transport, guint32 sender_id)
{
  GibberRMulticastCausalTransportPrivate *priv =
      GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_PRIVATE (transport);

  gibber_r_multicast_sender_group_remove (priv->sender_group, sender_id);
}

