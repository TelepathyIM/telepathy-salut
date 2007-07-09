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

#define SESSION_TIMEOUT_MIN 500
#define SESSION_TIMEOUT_MAX 800

#define NR_JOIN_REQUESTS_TO_SEND 3
#define PASSIVE_JOIN_TIME  500
#define ACTIVE_JOIN_INTERVAL 250

#define DEBUG_TRANSPORT(format,...) \
  DEBUG("group %s (%p): " format, priv->name, priv, ##__VA_ARGS__)

struct hash_data {
  GibberRMulticastSender *sender;
  GibberRMulticastPacket *packet;
};

static void
repair_message_cb(GibberRMulticastSender *sender,
                  GibberRMulticastPacket *packet,
                  gpointer user_data);
static void
whois_reply_cb(GibberRMulticastSender *sender, gpointer user_data);

static void
schedule_session_message(GibberRMulticastTransport *transport);

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
  GibberTransport *transport;
  guint32 packet_id;
  GHashTable *senders;
  GibberRMulticastSender *self;
  guint timer;
  gchar *name;
  guint32 sender_id;

  gint nr_join_requests;
  gint nr_join_requests_seen;
};

#define GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_R_MULTICAST_TRANSPORT, \
   GibberRMulticastTransportPrivate))

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
gibber_r_multicast_transport_set_property (GObject *object,
                                           guint property_id,
                                           const GValue *value,
                                           GParamSpec *pspec) {
  GibberRMulticastTransport *transport = GIBBER_R_MULTICAST_TRANSPORT(object);
  GibberRMulticastTransportPrivate *priv =
      GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE(transport);
  switch (property_id) {
    case PROP_NAME:
      priv->name = g_value_dup_string (value);
      break;
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
    case PROP_NAME:
      g_value_set_string(value, priv->self->name);
      break;
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

  /* allocate any data required by the object here */
  priv->senders = g_hash_table_new(g_direct_hash, g_direct_equal);
  priv->packet_id = g_random_int();
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

  param_spec = g_param_spec_string("name",
                                   "name",
                                   "The name to use on the protocol",
                                   NULL,
                                   G_PARAM_CONSTRUCT_ONLY |
                                   G_PARAM_READWRITE      |
                                   G_PARAM_STATIC_NAME    |
                                   G_PARAM_STATIC_BLURB);
  g_object_class_install_property(object_class, PROP_NAME,
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

  if (priv->timer != 0) {
    g_source_remove(priv->timer);
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

  /* free any data held directly by the object here */
  g_hash_table_destroy (priv->senders);

  G_OBJECT_CLASS (
      gibber_r_multicast_transport_parent_class)->finalize (object);
}

static gboolean
sendout_packet(GibberRMulticastTransport *transport,
               GibberRMulticastPacket *packet, GError **error) {
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (transport);
  guint8 *rawdata;
  gsize rawsize;

  rawdata = gibber_r_multicast_packet_get_raw_data(packet, &rawsize);
  return gibber_transport_send(GIBBER_TRANSPORT(priv->transport),
                               rawdata, rawsize, error);
}

static void
add_sender_info(gpointer key, gpointer value, gpointer user_data) {
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER(value);
  GibberRMulticastPacket *packet = GIBBER_R_MULTICAST_PACKET(user_data);
  gboolean r;

  if (sender->state == GIBBER_R_MULTICAST_SENDER_STATE_NEW) {
    return;
  }

  r = gibber_r_multicast_packet_add_sender_info(packet, sender->id,
               sender->next_input_packet, NULL);
  g_assert(r);
}

static gboolean
sendout_session_cb(gpointer data) {
  GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (data);
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);

  GibberRMulticastPacket *packet =
      gibber_r_multicast_packet_new(PACKET_TYPE_SESSION, priv->self->id,
                                    priv->transport->max_packet_size);

  g_hash_table_foreach(priv->senders, add_sender_info, packet);
  DEBUG_TRANSPORT ("Sending out session message");
  sendout_packet(self, packet, NULL);
  g_object_unref(packet);

  priv->timer = 0;
  schedule_session_message(self);

  return FALSE;
}

static void
schedule_session_message(GibberRMulticastTransport *transport) {
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (transport);

  if (priv->timer != 0) {
    g_source_remove(priv->timer);
  }

  priv->timer =
      g_timeout_add(
          g_random_int_range(SESSION_TIMEOUT_MIN, SESSION_TIMEOUT_MAX),
                    sendout_session_cb, transport);
}


static void
connected (GibberRMulticastTransport *transport)
{
  GibberRMulticastTransportPrivate *priv =
      GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE(transport);
  GibberRMulticastPacket *packet;

  DEBUG_TRANSPORT ("Connected to group");

  priv->self = gibber_r_multicast_sender_new (priv->sender_id, priv->name,
      priv->senders);

  g_hash_table_insert(priv->senders, GUINT_TO_POINTER(priv->self->id),
      priv->self);
  g_signal_connect(priv->self, "repair-message",
      G_CALLBACK(repair_message_cb), transport);
  g_signal_connect(priv->self, "whois-reply",
      G_CALLBACK(whois_reply_cb), transport);

  gibber_transport_set_state(GIBBER_TRANSPORT(transport),
         GIBBER_TRANSPORT_CONNECTED);

  /* Send out an unsolicited whois reply */
  packet = gibber_r_multicast_packet_new (PACKET_TYPE_WHOIS_REPLY,
        priv->sender_id, priv->transport->max_packet_size);

  gibber_r_multicast_packet_set_whois_reply_info (packet, priv->name);

  sendout_packet (transport, packet, NULL);
  g_object_unref (packet);

  schedule_session_message(transport);
}

static gboolean
next_join_step (gpointer data)
{
  GibberRMulticastTransport *transport = GIBBER_R_MULTICAST_TRANSPORT(data);
  GibberRMulticastTransportPrivate *priv =
      GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE(transport);

  DEBUG_TRANSPORT ("Next join step: %d", priv->nr_join_requests);

  if (priv->nr_join_requests < NR_JOIN_REQUESTS_TO_SEND)
    {
      GibberRMulticastPacket *packet;

      priv->nr_join_requests++;

      /* Set sender to 0 as we don't have an official id yet */
      packet = gibber_r_multicast_packet_new (PACKET_TYPE_WHOIS_REQUEST,
          0, priv->transport->max_packet_size);

      gibber_r_multicast_packet_set_whois_request_info (packet,
          priv->sender_id);

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
start_joining (GibberRMulticastTransport *transport)
{
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (transport);

  priv->sender_id = _random_nonzero_uint ();
  priv->nr_join_requests = 0;
  priv->nr_join_requests_seen = 0;

  DEBUG_TRANSPORT ("Started joining, id: %x", priv->sender_id);

  if (priv->timer != 0)
  {
    g_source_remove (priv->timer);
  }

  priv->timer = g_timeout_add (PASSIVE_JOIN_TIME,
    next_join_step, transport);
}

static void
senders_updated(gpointer key, gpointer value, gpointer user_data) {
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER(value);
  gibber_r_multicast_senders_updated(sender);
}

static void
data_received_cb(GibberRMulticastSender *sender, guint8 stream_id,
                 guint8 *data, gsize size, gpointer user_data) {
  GibberRMulticastTransport *self =
    GIBBER_R_MULTICAST_TRANSPORT(user_data);
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);
  GibberRMulticastBuffer rmbuffer;

  rmbuffer.buffer.data = data;
  rmbuffer.buffer.length = size;
  rmbuffer.sender = sender->name;
  rmbuffer.stream_id = stream_id;

  gibber_transport_received_data_custom(GIBBER_TRANSPORT(user_data),
      (GibberBuffer *)&rmbuffer);
  g_hash_table_foreach(priv->senders, senders_updated, self);
}

static void
repair_request_cb(GibberRMulticastSender *sender, guint id,
    gpointer user_data) {
  GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (user_data);
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);
  GibberRMulticastPacket *packet =
    gibber_r_multicast_packet_new (PACKET_TYPE_REPAIR_REQUEST,
        priv->self->id, priv->transport->max_packet_size);

  gibber_r_multicast_packet_set_repair_request_info (packet, sender->id, id);

  sendout_packet (self, packet, NULL);
  g_object_unref (packet);
}

static void
repair_message_cb(GibberRMulticastSender *sender,
                  GibberRMulticastPacket *packet,
                  gpointer user_data) {
  GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (user_data);

  sendout_packet(self, packet, NULL);
}

static void
whois_reply_cb(GibberRMulticastSender *sender, gpointer user_data) {
  GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (user_data);
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);
  GibberRMulticastPacket *packet =
    gibber_r_multicast_packet_new (PACKET_TYPE_WHOIS_REPLY,
        sender->id, priv->transport->max_packet_size);

  gibber_r_multicast_packet_set_whois_reply_info (packet, sender->name);

  sendout_packet (self, packet, NULL);
  g_object_unref (packet);
}

static void
whois_request_cb(GibberRMulticastSender *sender, gpointer user_data) {
  GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (user_data);
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);
  GibberRMulticastPacket *packet =
    gibber_r_multicast_packet_new (PACKET_TYPE_WHOIS_REQUEST,
        priv->self->id, priv->transport->max_packet_size);

  gibber_r_multicast_packet_set_whois_request_info (packet, sender->id);

  sendout_packet (self, packet, NULL);
  g_object_unref (packet);
}

static void
name_discovered_cb(GibberRMulticastSender *sender, gchar *name,
    gpointer user_data)
{
  GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (user_data);
  g_signal_emit(self, signals[NEW_SENDER], 0, name);
}

static GibberRMulticastSender *
add_sender(GibberRMulticastTransport *self, guint32 sender_id,
           const gchar *name) {
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);
  GibberRMulticastSender *sender;

  sender = gibber_r_multicast_sender_new(sender_id, name, priv->senders);

  g_hash_table_insert(priv->senders, GUINT_TO_POINTER(sender->id), sender);

  g_signal_connect(sender, "received-data",
      G_CALLBACK(data_received_cb), self);

  g_signal_connect(sender, "repair-request",
      G_CALLBACK(repair_request_cb), self);

  g_signal_connect(sender, "whois-request",
      G_CALLBACK(whois_request_cb), self);
  g_signal_connect(sender, "whois-reply",
      G_CALLBACK(whois_reply_cb), self);
  g_signal_connect(sender, "name-discovered",
      G_CALLBACK(name_discovered_cb), self);

  return sender;
}

static void
handle_session_message(GibberRMulticastTransport *self,
                       GibberRMulticastPacket *packet) {
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);
  GList *l;
  int num = 0;
  gboolean outdated = FALSE;

  g_assert (packet->type == PACKET_TYPE_SESSION);

  for (l = packet->data.session.senders ; l != NULL ; l = g_list_next(l))
    {
      GibberRMulticastPacketSenderInfo *sender_info =
          (GibberRMulticastPacketSenderInfo *) l->data;
      GibberRMulticastSender *sender =
          g_hash_table_lookup(priv->senders,
              GUINT_TO_POINTER(sender_info->sender_id));

    num++;

    if (sender == NULL) {
      sender = add_sender(self, sender_info->sender_id, NULL);
    } else if (gibber_r_multicast_packet_diff(sender_info->packet_id,
                   sender->next_input_packet) > 0) {

      g_assert(sender->state > GIBBER_R_MULTICAST_SENDER_STATE_NEW);
      outdated = TRUE;
    }
    gibber_r_multicast_sender_seen(sender, sender_info->packet_id);
  }

  /* Reschedule the sending out of a session message if the received session
   * message was at least as up to date as us */
  if (!outdated && g_hash_table_size(priv->senders) == num) {
    DEBUG_TRANSPORT ("Rescheduling session message");
    schedule_session_message(self);
  }
}

static void
handle_data_depends(GibberRMulticastTransport *self,
                       GibberRMulticastPacket *packet) {
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);
  GList *l;

  g_assert (packet->type == PACKET_TYPE_DATA);

  for (l = packet->data.data.depends; l != NULL ; l = g_list_next(l)) {
    GibberRMulticastPacketSenderInfo *sender_info =
        (GibberRMulticastPacketSenderInfo *) l->data;
    GibberRMulticastSender *sender =
        g_hash_table_lookup (priv->senders,
            GUINT_TO_POINTER (sender_info->sender_id));

    if (sender == NULL) {
      sender = add_sender (self, sender_info->sender_id, NULL);
    }
    gibber_r_multicast_sender_seen (sender, sender_info->packet_id + 1);
  }
}

static void
joining_multicast_receive (GibberRMulticastTransport *self,
  GibberRMulticastPacket *packet)
{

  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);

  DEBUG_TRANSPORT ("Received packet type: %x", packet->type);

  if (packet->sender == priv->sender_id)
    {
      DEBUG_TRANSPORT ("Detected collision with existing sender, "
        "restarting join process");
      start_joining (self);
      return;
    }

  if (packet->type == PACKET_TYPE_WHOIS_REQUEST &&
      packet->data.whois_request.sender_id == priv->sender_id)
    {
      if (packet->sender != 0)
        {
          DEBUG_TRANSPORT ("Detected existing node quering for the same id,"
              " restarting join process");
          start_joining (self);
        }
      else
        {
          priv->nr_join_requests_seen++;
          if (priv->nr_join_requests < priv->nr_join_requests_seen)
            {
              DEBUG_TRANSPORT ("Detected another node probing for the same id,"
                  " restarting join process");
              start_joining (self);
            }
        }
    }
}

static void
joined_multicast_receive (GibberRMulticastTransport *self,
    GibberRMulticastPacket *packet) {
  GibberRMulticastSender *sender = NULL;
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);

  DEBUG_TRANSPORT ("Got packet type: 0x%x", packet->type);

  if (packet->sender == 0) {
    if (packet->type != PACKET_TYPE_WHOIS_REQUEST)
      {
        DEBUG_TRANSPORT ("Invalid packet (sender is 0, which is not valid for "
             " type %x)",
          packet->type);
        goto out;
      }
    DEBUG_TRANSPORT ("New sender polling for a unique id");
  } else {
    /* All packets with non-zero sender fall go through here to start detecting
     * new sender as early as possible */
    sender = g_hash_table_lookup (priv->senders,
        GUINT_TO_POINTER (packet->sender));
    if (sender == NULL) {
      sender = add_sender (self, packet->sender, NULL);
    }
  }

  if (sender == priv->self && packet->type != PACKET_TYPE_WHOIS_REQUEST)
    {
      goto out;
    }

  switch (packet->type) {
    case PACKET_TYPE_WHOIS_REQUEST:
      sender = g_hash_table_lookup (priv->senders,
          GUINT_TO_POINTER (packet->data.whois_request.sender_id));
      if (sender == NULL)
        goto out;
      /* fallthrough */
    case PACKET_TYPE_WHOIS_REPLY:
     gibber_r_multicast_sender_whois_push(sender, packet);
     break;
    case PACKET_TYPE_DATA:
      handle_data_depends(self, packet);
      gibber_r_multicast_sender_push(sender, packet);
      break;
    case PACKET_TYPE_REPAIR_REQUEST: {
      GibberRMulticastSender *rsender;
      guint32 sender_id;

      sender_id = packet->data.repair_request.sender_id;
      rsender = g_hash_table_lookup(priv->senders,
          GUINT_TO_POINTER(sender_id));

      g_assert(sender_id != 0);

      if (rsender == NULL)
        {
          rsender = add_sender(self, sender_id, NULL);
        }
      gibber_r_multicast_sender_repair_request(rsender,
         packet->data.repair_request.packet_id);
      break;
    }
    case PACKET_TYPE_SESSION:
      handle_session_message(self, packet);
      break;
    default:
        DEBUG_TRANSPORT ("Received unhandled packet type!!, ignoring");
  }

out:
  return;
}

static void
r_multicast_receive(GibberTransport *transport, GibberBuffer *buffer,
                    gpointer user_data) {
  GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (user_data);
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);
  GibberRMulticastPacket *packet = NULL;
  GError *error = NULL;

  packet = gibber_r_multicast_packet_parse(buffer->data,
      buffer->length, &error);

  if (packet == NULL) {
    DEBUG_TRANSPORT ("Failed to parse packet: %s", error->message);
  } else {
    switch (GIBBER_TRANSPORT(self)->state)
      {
        case GIBBER_TRANSPORT_CONNECTING:
          joining_multicast_receive (self, packet);
          break;
        case GIBBER_TRANSPORT_CONNECTED:
          joined_multicast_receive (self, packet);
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

GibberRMulticastTransport *
gibber_r_multicast_transport_new(GibberTransport *transport,
                                 const gchar *name) {
  GibberRMulticastTransport *result;
  g_assert(name != NULL && *name != '\0');

  result =  g_object_new(GIBBER_TYPE_R_MULTICAST_TRANSPORT,
                      "name", name,
                      "transport", transport,
                      NULL);

  gibber_transport_set_handler(GIBBER_TRANSPORT(transport),
      r_multicast_receive, result);

  return result;
}

gboolean
gibber_r_multicast_transport_connect(GibberRMulticastTransport *transport,
                                     gboolean initial, GError **error) {
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (transport);

  g_assert(priv->transport->max_packet_size > 128);

  gibber_transport_set_state(GIBBER_TRANSPORT(transport),
         GIBBER_TRANSPORT_CONNECTING);

  start_joining (transport);

  return TRUE;
}

static void
add_depend(gpointer key, gpointer value, gpointer user_data) {
  GibberRMulticastSender *sender = GIBBER_R_MULTICAST_SENDER(value);
  struct hash_data *d = (struct hash_data *)user_data;
  gboolean r;

  if (sender->state < GIBBER_R_MULTICAST_SENDER_STATE_RUNNING) {
    return;
  }

  if (sender == d->sender) {
    return;
  }

  r = gibber_r_multicast_packet_add_sender_info(d->packet, sender->id,
               sender->last_output_packet, NULL);
  g_assert(r);
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
  struct hash_data hd;
  GibberRMulticastPacket *packet;
  gsize payloaded;

  packet = gibber_r_multicast_packet_new(PACKET_TYPE_DATA, priv->self->id,
      priv->transport->max_packet_size);

  /* Add dependency information */
  hd.sender = priv->self;
  hd.packet = packet;
  g_hash_table_foreach(priv->senders, add_depend, &hd);

  payloaded = gibber_r_multicast_packet_add_payload(packet, data, size);

  if (payloaded < size) {
    GPtrArray *packets = g_ptr_array_sized_new(2);
    g_ptr_array_add(packets, packet);
    int i;
    gboolean ret = TRUE;
    while (payloaded < size) {
      packet = gibber_r_multicast_packet_new(PACKET_TYPE_DATA, priv->self->id,
          priv->transport->max_packet_size);
      payloaded += gibber_r_multicast_packet_add_payload(packet,
          data + payloaded, size - payloaded);
      g_ptr_array_add(packets, packet);
    }
    for (i = 0; i < packets->len && ret; i++) {
      packet = g_ptr_array_index(packets, i);

      gibber_r_multicast_packet_set_data_info(packet, priv->packet_id++,
         stream_id, i, packets->len);

      ret = sendout_packet(self, packet, error);
      gibber_r_multicast_sender_push(priv->self, packet);
      g_object_unref (packet);
    }
    for (; i < packets->len; i++) {
      g_object_unref(g_ptr_array_index(packets, i));
    }
    g_ptr_array_free(packets, TRUE);
    return ret;
  } else {
     gibber_r_multicast_packet_set_data_info(packet, priv->packet_id++,
         stream_id, 0, 1);
     gibber_r_multicast_sender_push(priv->self, packet);
     return sendout_packet(self, packet, error);
  }
}

static gboolean
gibber_r_multicast_transport_do_send(GibberTransport *transport,
                                     const guint8 *data,
                                     gsize size,
                                     GError **error) {
  return gibber_r_multicast_transport_send(
     GIBBER_R_MULTICAST_TRANSPORT(transport),
     GIBBER_R_MULTICAST_DEFAULT_STREAM,
     data, size, error);
}

static void
gibber_r_multicast_transport_disconnect(GibberTransport *transport) {
  GibberRMulticastTransport *self = GIBBER_R_MULTICAST_TRANSPORT (transport);
  GibberRMulticastTransportPrivate *priv =
    GIBBER_R_MULTICAST_TRANSPORT_GET_PRIVATE (self);

  if (priv->timer != 0) {
    g_source_remove(priv->timer);
  }

  gibber_transport_set_state(GIBBER_TRANSPORT(self),
                             GIBBER_TRANSPORT_DISCONNECTED);
  gibber_transport_disconnect(GIBBER_TRANSPORT(priv->transport));
}
