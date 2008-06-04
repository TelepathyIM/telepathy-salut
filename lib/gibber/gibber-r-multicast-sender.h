/*
 * gibber-r-multicast-sender.h - Header for GibberRMulticastSender
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

#ifndef __GIBBER_R_MULTICAST_SENDER_H__
#define __GIBBER_R_MULTICAST_SENDER_H__

#include <glib-object.h>

#include "gibber-r-multicast-packet.h"

G_BEGIN_DECLS

typedef struct _GibberRMulticastSenderGroup GibberRMulticastSenderGroup;

struct _GibberRMulticastSenderGroup {
  /* <public>
   * GUINT_TO_POINTER (sender_id) => owned GibberRMulticastSender */
  GHashTable *senders;
  /* <private> */
  gboolean popping;
  gboolean stopped;
  /* queue of GibberRMulticast GibberRMulticastSender */
  GQueue *pop_queue;
  /* GArray of pending removal GibberRMulticastSenders */
  GPtrArray *pending_removal;
};

typedef struct _GibberRMulticastSender GibberRMulticastSender;
typedef struct _GibberRMulticastSenderClass GibberRMulticastSenderClass;

struct _GibberRMulticastSenderClass {
    GObjectClass parent_class;
};

typedef enum {
  /* We have no info about this sender whatsoever */
  GIBBER_R_MULTICAST_SENDER_STATE_NEW = 0,
  /* We know the sequence number we have to start from, but haven't send out
   * any data yet */
  GIBBER_R_MULTICAST_SENDER_STATE_PREPARING,
  /* Non-data Packets are flowing */
  GIBBER_R_MULTICAST_SENDER_STATE_RUNNING,
  /* Data is flowing */
  GIBBER_R_MULTICAST_SENDER_STATE_DATA_RUNNING,
  /* Node has failed, still pop packets but stop depending on it */
  GIBBER_R_MULTICAST_SENDER_STATE_FAILED,
  GIBBER_R_MULTICAST_SENDER_STATE_UNKNOWN_FAILED,
  GIBBER_R_MULTICAST_SENDER_STATE_STOPPED,
  /* Will be removed as soon as all dependencies are resolved */
  GIBBER_R_MULTICAST_SENDER_STATE_PENDING_REMOVAL,
} GibberRMulticastSenderState;

struct _GibberRMulticastSender {
    GObject parent;
    gchar *name;
    guint32 id;

    GibberRMulticastSenderState state;

    /* Next packet we want to send out */
    guint32 next_output_packet;
    /* Next data packet we want to send out. Can be different from
     * next_output_packet iff holding back data or a fragmented data message is
     * interleaved with control messages.. Guaranteed to be <=
     * next_output_packet */
    guint32 next_output_data_packet;

    /* Next packet we expect from the sender */
    guint32 next_input_packet;
};

GType gibber_r_multicast_sender_get_type (void);

/* TYPE MACROS */
#define GIBBER_TYPE_R_MULTICAST_SENDER \
  (gibber_r_multicast_sender_get_type ())
#define GIBBER_R_MULTICAST_SENDER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_R_MULTICAST_SENDER, \
   GibberRMulticastSender))
#define GIBBER_R_MULTICAST_SENDER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_R_MULTICAST_SENDER, \
   GibberRMulticastSenderClass))
#define GIBBER_IS_R_MULTICAST_SENDER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_R_MULTICAST_SENDER))
#define GIBBER_IS_R_MULTICAST_SENDER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_R_MULTICAST_SENDER))
#define GIBBER_R_MULTICAST_SENDER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_R_MULTICAST_SENDER, \
   GibberRMulticastSenderClass))

GibberRMulticastSenderGroup *gibber_r_multicast_sender_group_new (void);

void gibber_r_multicast_sender_group_free (GibberRMulticastSenderGroup *group);
void gibber_r_multicast_sender_group_stop (GibberRMulticastSenderGroup *group);
void gibber_r_multicast_sender_group_add (GibberRMulticastSenderGroup *group,
    GibberRMulticastSender *sender);

GibberRMulticastSender * gibber_r_multicast_sender_group_lookup (
    GibberRMulticastSenderGroup *group, guint32 sender_id);

GibberRMulticastSender * gibber_r_multicast_sender_group_lookup_by_name (
    GibberRMulticastSenderGroup *group, const gchar *name);

void gibber_r_multicast_sender_group_remove (
    GibberRMulticastSenderGroup *group, guint32 sender_id);

gboolean gibber_r_multicast_sender_group_push_packet (
  GibberRMulticastSenderGroup *group, GibberRMulticastPacket *packet);

GibberRMulticastSender *gibber_r_multicast_sender_new (guint32 id,
    const gchar *name, GibberRMulticastSenderGroup *group);

/* Sequence for this sender starts at packet_id */
void gibber_r_multicast_sender_update_start (GibberRMulticastSender *sender,
    guint32 packet_id);

void gibber_r_multicast_sender_update_end (GibberRMulticastSender *sender,
    guint32 packet_id);

void gibber_r_multicast_sender_set_failed (GibberRMulticastSender *sender);

void gibber_r_multicast_sender_set_data_start (GibberRMulticastSender *sender,
    guint32 packet_id);

/* Tell the sender to not signal data starting from this packet */
void gibber_r_multicast_sender_hold_data (GibberRMulticastSender *sender,
  guint32 packet_id);


/* Stop holding back data of the sender */
void gibber_r_multicast_sender_release_data (GibberRMulticastSender *sender);

void gibber_r_multicast_sender_push (GibberRMulticastSender *sender,
     GibberRMulticastPacket *packet);

void
gibber_r_multicast_senders_updated (GibberRMulticastSender *sender);

/* Returns TRUE if we were up to dated */
gboolean gibber_r_multicast_sender_seen (GibberRMulticastSender *sender,
    guint32 id);

gboolean gibber_r_multicast_sender_repair_request (
    GibberRMulticastSender *sender, guint32 id);

void gibber_r_multicast_sender_whois_push (GibberRMulticastSender *sender,
    const GibberRMulticastPacket *packet);

void gibber_r_multicast_sender_set_packet_repeat (
    GibberRMulticastSender *sender, guint32 packet_id, gboolean repeat);

/* Returns the amount of unpopped or unacked packets */
guint gibber_r_multicast_sender_packet_cache_size (
    GibberRMulticastSender *sender);

/* Ack management */
void gibber_r_multicast_sender_ack (GibberRMulticastSender *sender,
    guint32 ack);

/* Stop all pending requests, only reply to repair requests */
void gibber_r_multicast_sender_stop (GibberRMulticastSender *sender);

/* Trigger failure detection, for testing only */
void _gibber_r_multicast_TEST_sender_fail (GibberRMulticastSender *sender);

G_END_DECLS

#endif /* #ifndef __GIBBER_R_MULTICAST_SENDER_H__*/
