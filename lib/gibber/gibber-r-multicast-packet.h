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

typedef enum {
  PACKET_TYPE_WHOIS_REQUEST = 0,
  PACKET_TYPE_WHOIS_REPLY,
  PACKET_TYPE_DATA,
  PACKET_TYPE_REPAIR_REQUEST,
  PACKET_TYPE_SESSION,
  PACKET_TYPE_BYE,
  PACKET_TYPE_INVALID
} GibberRMulticastPacketType;

typedef struct {
  guint32 receiver_id;
  guint32 packet_id;
} GibberRMulticastReceiver;

typedef struct _GibberRMulticastPacket GibberRMulticastPacket;
typedef struct _GibberRMulticastPacketClass GibberRMulticastPacketClass;

struct _GibberRMulticastPacketClass {
    GObjectClass parent_class;
};

struct _GibberRMulticastPacket {
    GObject parent;
    GibberRMulticastPacketType type;
    guint8 version;

    /* Part of packet part/total*/
    guint8 packet_part;
    guint8 packet_total;

    /* payload size */
    gsize payload_size;

    /* packet identifier */
    guint32 packet_id;

    /* substream id */
    guint8 stream_id;

    /* sender */
    guint32 sender;

    /* List of GibberRMulticastReceiver's receivers */ 
    GList *receivers;
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
GibberRMulticastPacket *
gibber_r_multicast_packet_new(GibberRMulticastPacketType type,
                              guint32 sender, guint32 packet_id,
                              guint8 stream_id,
                              gsize max_size);

gboolean
gibber_r_multicast_packet_add_receiver(GibberRMulticastPacket *packet,
                                       guint32 receiver_id,
                                       guint32 packet_id,
                                       GError **error);

void
gibber_r_multicast_packet_set_part(GibberRMulticastPacket *packet,
                                   guint8 part, guint8 total);

/* Add the actual payload. Should be done as the last step, packet is immutable
 * afterwards */
gsize
gibber_r_multicast_packet_add_payload(GibberRMulticastPacket *packet,
                                      const guint8 *data, gsize size);

/* Create a packet by parsing raw data, packet is immutable */
GibberRMulticastPacket *
gibber_r_multicast_packet_parse(const guint8 *data, gsize size, GError **error);

/* Get the packets payload */
guint8 *
gibber_r_multicast_packet_get_payload(GibberRMulticastPacket *packet,
                                      gsize *size);

/* Get the packets raw data, packet is immutable after this call */
guint8 *
gibber_r_multicast_packet_get_raw_data(GibberRMulticastPacket *packet,
                                       gsize *size);

/* Utility function to calculate the difference between two packet */
gint32
gibber_r_multicast_packet_diff(guint32 from, guint32 to);


/* WHOIS packets are very simple and thus aren't GObjects. Maybe at some point
 * gibber_r_multicast_packet should be renamed to 
 * gibber_r_multicast_complex_packet */

typedef struct  {
  /* either PACKET_TYPE_WHOIS_REQUEST or PACKET_TYPE_WHOIS_REPLY */
  GibberRMulticastPacketType type;
  guint8 version;
  /* Identifier of the sender that was answered/queried */
  guint32 id;
  /* Only set if type == PACKET_TYPE_WHOIS_REPLY */
  gchar *name;
} GibberRMulticastWhoisPacket;

/* Generate a new raw whois packet, free with g_free */
guint8 * gibber_r_multicast_whois_packet(GibberRMulticastPacketType type,
    guint32 id, gchar *name, gsize *length);

GibberRMulticastWhoisPacket * gibber_r_multicast_whois_new_from_packet(
  const guint8 *data, gsize length);

void gibber_r_multicast_whois_free(GibberRMulticastWhoisPacket *packet);


GibberRMulticastPacketType gibber_r_multicast_packet_get_packet_type(
    const guint8 *data, const gsize length);


G_END_DECLS

#endif /* #ifndef __GIBBER_R_MULTICAST_PACKET_H__*/
