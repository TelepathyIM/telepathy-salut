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

typedef struct _GibberRMulticastSender GibberRMulticastSender;
typedef struct _GibberRMulticastSenderClass GibberRMulticastSenderClass;

struct _GibberRMulticastSenderClass {
    GObjectClass parent_class;
};

typedef enum {
  GIBBER_R_MULTICAST_SENDER_STATE_NEW = 0,
  GIBBER_R_MULTICAST_SENDER_STATE_RUNNING,
} GibberRMulticastSenderState;

struct _GibberRMulticastSender {
    GObject parent;
    gchar *name;

    GibberRMulticastSenderState state;

    /* Next packet that we expect for the sender */
    guint32 next_packet_id;

    /* Last packet nr. we've seen of a sender */
    guint32 last_packet;
};

GType gibber_r_multicast_sender_get_type(void);

/* TYPE MACROS */
#define GIBBER_TYPE_R_MULTICAST_SENDER \
  (gibber_r_multicast_sender_get_type())
#define GIBBER_R_MULTICAST_SENDER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_R_MULTICAST_SENDER, GibberRMulticastSender))
#define GIBBER_R_MULTICAST_SENDER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_R_MULTICAST_SENDER, GibberRMulticastSenderClass))
#define GIBBER_IS_R_MULTICAST_SENDER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_R_MULTICAST_SENDER))
#define GIBBER_IS_R_MULTICAST_SENDER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_R_MULTICAST_SENDER))
#define GIBBER_R_MULTICAST_SENDER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_R_MULTICAST_SENDER, GibberRMulticastSenderClass))

GibberRMulticastSender *
gibber_r_multicast_sender_new(const gchar *name);

void
gibber_r_multicast_sender_push(GibberRMulticastSender *sender,
                               GibberRMulticastPacket *packet);

/* Returns TRUE if we were up to dated */
gboolean
gibber_r_multicast_sender_seen(GibberRMulticastSender *sender, guint32 id);

void
gibber_r_multicast_sender_repair_request(GibberRMulticastSender *sender, 
                                         guint32 id);

G_END_DECLS

#endif /* #ifndef __GIBBER_R_MULTICAST_SENDER_H__*/
