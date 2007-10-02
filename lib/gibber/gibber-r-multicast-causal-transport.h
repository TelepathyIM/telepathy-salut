/*
 * gibber-r-multicast-transport.h - Header for GibberRMulticastCausalTransport
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

#ifndef __GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_H__
#define __GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_H__

#include <glib-object.h>
#include "gibber-transport.h"

G_BEGIN_DECLS

typedef struct _GibberRMulticastCausalTransport GibberRMulticastCausalTransport;
typedef struct _GibberRMulticastCausalTransportClass
    GibberRMulticastCausalTransportClass;

struct _GibberRMulticastCausalTransportClass {
    GibberTransportClass parent_class;
};

struct _GibberRMulticastCausalTransport {
    GibberTransport parent;
    guint32 sender_id;
};

GType gibber_r_multicast_causal_transport_get_type(void);

typedef struct {
  GibberBuffer buffer;
  const gchar *sender;
  guint8 stream_id;
  guint32 sender_id;
} GibberRMulticastCausalBuffer;

#define GIBBER_R_MULTICAST_CAUSAL_DEFAULT_STREAM 0

/* TYPE MACROS */
#define GIBBER_TYPE_R_MULTICAST_CAUSAL_TRANSPORT \
  (gibber_r_multicast_causal_transport_get_type())
#define GIBBER_R_MULTICAST_CAUSAL_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_R_MULTICAST_CAUSAL_TRANSPORT, GibberRMulticastCausalTransport))
#define GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_R_MULTICAST_CAUSAL_TRANSPORT, GibberRMulticastCausalTransportClass))
#define GIBBER_IS_R_MULTICAST_CAUSAL_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_R_MULTICAST_CAUSAL_TRANSPORT))
#define GIBBER_IS_R_MULTICAST_CAUSAL_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_R_MULTICAST_CAUSAL_TRANSPORT))
#define GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_R_MULTICAST_CAUSAL_TRANSPORT, GibberRMulticastCausalTransportClass))

GibberRMulticastCausalTransport *
gibber_r_multicast_causal_transport_new(GibberTransport *transport,
    const gchar *name);

gboolean
gibber_r_multicast_causal_transport_connect(
    GibberRMulticastCausalTransport *transport,
    gboolean initial, GError **error);

gboolean
gibber_r_multicast_causal_transport_send(
    GibberRMulticastCausalTransport *transport,
    guint8 stream_id,
    const guint8 *data,
    gsize size,
    GError **error);

void gibber_r_multicast_causal_transport_add_sender (
    GibberRMulticastCausalTransport *transport, guint32 sender_id);

void gibber_r_multicast_causal_transport_update_sender_start (
    GibberRMulticastCausalTransport *transport,
    guint32 sender_id,
    guint32 packet_id);

guint32 gibber_r_multicast_causal_transport_send_attempt_join (
    GibberRMulticastCausalTransport *transport,
    GArray *new_senders,
    gboolean repeat);

void gibber_r_multicast_causal_transport_stop_attempt_join (
    GibberRMulticastCausalTransport *transport,
    guint32 attempt_join_id);

void gibber_r_multicast_causal_transport_send_join (
    GibberRMulticastCausalTransport *transport);

G_END_DECLS

#endif /* #ifndef __GIBBER_R_MULTICAST_CAUSAL_TRANSPORT_H__*/
