/*
 * gibber-r-multicast-packet.h - Header for GibberRMulticastPacket
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

#ifndef __GIBBER_R_MULTICAST_PACKET_H__
#define __GIBBER_R_MULTICAST_PACKET_H__

#include <glib-object.h>

G_BEGIN_DECLS

GQuark gibber_r_multicast_packet_error_quark (void);
#define GIBBER_R_MULTICAST_PACKET_ERROR \
  gibber_r_multicast_packet_error_quark()

typedef enum {
  GIBBER_R_MULTICAST_PACKET_ERROR_PARSE_ERROR
} GibberRMulticastPacketErrors;

typedef enum {
  /* Unreliable packets */
  PACKET_TYPE_WHOIS_REQUEST = 0,
  PACKET_TYPE_WHOIS_REPLY,
  PACKET_TYPE_REPAIR_REQUEST,
  PACKET_TYPE_SESSION,
  /* Reliable packets */
  FIRST_RELIABLE_PACKET = 0xf,
  PACKET_TYPE_DATA = FIRST_RELIABLE_PACKET,
  /* No data just acknowledgement */
  PACKET_TYPE_NO_DATA,
  /* Some nodes failed */
  PACKET_TYPE_FAILURE,
  /* Start a joining attempt */
  PACKET_TYPE_ATTEMPT_JOIN,
  /* The real join */
  PACKET_TYPE_JOIN,
  /* Leaving now, bye */
  PACKET_TYPE_BYE,
  PACKET_TYPE_INVALID
} GibberRMulticastPacketType;

#define IS_RELIABLE_PACKET(p) (p->type >= FIRST_RELIABLE_PACKET)

typedef struct {
  guint32 sender_id;
  guint32 packet_id;
} GibberRMulticastPacketSenderInfo;

struct _GibberRMulticastPacketClass {
    GObjectClass parent_class;
};

typedef struct _GibberRMulticastWhoisRequestPacket
    GibberRMulticastWhoisRequestPacket;
struct _GibberRMulticastWhoisRequestPacket {
  guint32 sender_id;
};

typedef struct _GibberRMulticastWhoisReplyPacket
    GibberRMulticastWhoisReplyPacket;

struct _GibberRMulticastWhoisReplyPacket {
    gchar *sender_name;
};

#define GIBBER_R_MULTICAST_DATA_PACKET_START 0x1
#define GIBBER_R_MULTICAST_DATA_PACKET_END  0x2

typedef struct _GibberRMulticastDataPacket GibberRMulticastDataPacket;
struct _GibberRMulticastDataPacket {
    /* These are actually 24 bits in the wire protocol */
    guint8 flags;
    guint32 total_size;

    /* payload */
    guint8 *payload;
    gsize payload_size;

    /* substream id */
    guint16 stream_id;
};

typedef struct _GibberRMulticastRepairRequestPacket
    GibberRMulticastRepairRequestPacket;
struct _GibberRMulticastRepairRequestPacket {
    /* Sender identifier */
    guint32 sender_id;
    /* packet identifier */
    guint32 packet_id;
};

typedef struct _GibberRMulticastAttemptJoinPacket
    GibberRMulticastAttemptJoinPacket;
struct _GibberRMulticastAttemptJoinPacket {
    /* Unknown sender identifiers */
    GArray *senders;
};

typedef struct _GibberRMulticastFailurePacket GibberRMulticastFailurePacket;
struct _GibberRMulticastFailurePacket {
    /* failed sender identifiers */
    GArray *failures;
};

typedef struct _GibberRMulticastJoinPacket
    GibberRMulticastJoinPacket;
struct _GibberRMulticastJoinPacket {
    /* Unknown sender identifiers */
    GArray *failures;
};


typedef struct _GibberRMulticastPacket GibberRMulticastPacket;
typedef struct _GibberRMulticastPacketClass GibberRMulticastPacketClass;

struct _GibberRMulticastPacket {
    GObject parent;
    GibberRMulticastPacketType type;
    guint8 version;
    /* sender */
    guint32 sender;

    /* packet identifier for reliable packets */
    guint32 packet_id;

    /* List of GibberRMulticastSenderInfo encoding dependency information for
     * reliable packets or session information for session packets */
    GArray *depends;

    union {
      GibberRMulticastWhoisRequestPacket whois_request;
      GibberRMulticastWhoisReplyPacket whois_reply;
      GibberRMulticastDataPacket data;
      GibberRMulticastRepairRequestPacket repair_request;
      GibberRMulticastAttemptJoinPacket attempt_join;
      GibberRMulticastJoinPacket join;
      GibberRMulticastFailurePacket failure;
    } data;
};

GType gibber_r_multicast_packet_get_type(void);

/* TYPE MACROS */
#define GIBBER_TYPE_R_MULTICAST_PACKET \
  (gibber_r_multicast_packet_get_type())
#define GIBBER_R_MULTICAST_PACKET(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_R_MULTICAST_PACKET, GibberRMulticastPacket))
#define GIBBER_R_MULTICAST_PACKET_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_R_MULTICAST_PACKET, GibberRMulticastPacketClass))
#define GIBBER_IS_R_MULTICAST_PACKET(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_R_MULTICAST_PACKET))
#define GIBBER_IS_R_MULTICAST_PACKET_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_R_MULTICAST_PACKET))
#define GIBBER_R_MULTICAST_PACKET_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_R_MULTICAST_PACKET, GibberRMulticastPacketClass))

/* Start a new packet */
GibberRMulticastPacket * gibber_r_multicast_packet_new (
    GibberRMulticastPacketType type, guint32 sender, gsize max_size);

/* Add depend if packet type is PACKET_TYPE_DATA otherwise add sender info if
 * PACKET_TYPE_SESSION */
gboolean gibber_r_multicast_packet_add_sender_info (
    GibberRMulticastPacket *packet,
    guint32 receiver_id,
    guint32 packet_id,
    GError **error);

void gibber_r_multicast_packet_set_packet_id (GibberRMulticastPacket *packet,
  guint32 packet_id);

/* Set info for PACKET_TYPE_DATA packets */
void gibber_r_multicast_packet_set_data_info (GibberRMulticastPacket *packet,
    guint16 stream_id, guint8 flags, guint32 size);

/* Set info for PACKET_TYPE_REPAIR_REQUEST packets */
void gibber_r_multicast_packet_set_repair_request_info (
    GibberRMulticastPacket *packet, guint32 sender_id, guint32 packet_id);

/* Set the info for PACKET_TYPE_WHOIS_REQUEST packets */
void gibber_r_multicast_packet_set_whois_request_info (
    GibberRMulticastPacket *packet, const guint32 sender_id);

/* Set the info for PACKET_TYPE_WHOIS_REPLY packets */
void gibber_r_multicast_packet_set_whois_reply_info (
    GibberRMulticastPacket *packet, const gchar *sender_name);

/* Add the actual payload in PACKET_TYPE_DATA packets.
 * No extra data might be set/added after this (extra depends or payload..) */
gsize gibber_r_multicast_packet_add_payload (GibberRMulticastPacket *packet,
    const guint8 *data, gsize size);

/* Create a packet by parsing raw data, packet is immutable */
GibberRMulticastPacket * gibber_r_multicast_packet_parse (const guint8 *data,
    gsize size, GError **error);

/* Get the packets payload */
guint8 * gibber_r_multicast_packet_get_payload (GibberRMulticastPacket *packet,
    gsize *size);

/* Get the packets raw data, packet is immutable after this call */
guint8 * gibber_r_multicast_packet_get_raw_data (GibberRMulticastPacket *packet,
    gsize *size);

/* Add sender we want to start joining with to the attempt_join */
gboolean gibber_r_multicast_packet_attempt_join_add_sender (
   GibberRMulticastPacket *packet,
   guint32 sender,
   GError **error);

gboolean gibber_r_multicast_packet_attempt_join_add_senders (
   GibberRMulticastPacket *packet,
   GArray *senders,
   GError **error);

/* Add senders that have failed */
gboolean gibber_r_multicast_packet_join_add_failure (
   GibberRMulticastPacket *packet,
   guint32 failure,
   GError **error);

gboolean gibber_r_multicast_packet_join_add_failures (
   GibberRMulticastPacket *packet,
   GArray *failures,
   GError **error);

/* Add senders that have failed */
gboolean gibber_r_multicast_packet_failure_add_sender (
   GibberRMulticastPacket *packet,
   guint32 sender,
   GError **error);

gboolean gibber_r_multicast_packet_failure_add_senders (
   GibberRMulticastPacket *packet,
   GArray *sender,
   GError **error);


/* Utility function to calculate the difference between two packet */
gint32
gibber_r_multicast_packet_diff (guint32 from, guint32 to);

G_END_DECLS

#endif /* #ifndef __GIBBER_R_MULTICAST_PACKET_H__*/
