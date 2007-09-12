/*
 * gibber-r-multicast-transport.h - Header for GibberRMulticastTransport
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

#ifndef __GIBBER_R_MULTICAST_TRANSPORT_H__
#define __GIBBER_R_MULTICAST_TRANSPORT_H__

#include <glib-object.h>
#include "gibber-transport.h"
#include "gibber-r-multicast-causal-transport.h"

G_BEGIN_DECLS

typedef struct _GibberRMulticastTransport GibberRMulticastTransport;
typedef struct _GibberRMulticastTransportClass GibberRMulticastTransportClass;

struct _GibberRMulticastTransportClass {
    GibberTransportClass parent_class;
};

struct _GibberRMulticastTransport {
    GibberTransport parent;
};

typedef struct {
  GibberBuffer buffer;
  const gchar *sender;
  guint8 stream_id;
} GibberRMulticastBuffer;

GType gibber_r_multicast_transport_get_type(void);

/* TYPE MACROS */
#define GIBBER_TYPE_R_MULTICAST_TRANSPORT \
  (gibber_r_multicast_transport_get_type())
#define GIBBER_R_MULTICAST_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_R_MULTICAST_TRANSPORT, GibberRMulticastTransport))
#define GIBBER_R_MULTICAST_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_R_MULTICAST_TRANSPORT, GibberRMulticastTransportClass))
#define GIBBER_IS_R_MULTICAST_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_R_MULTICAST_TRANSPORT))
#define GIBBER_IS_R_MULTICAST_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_R_MULTICAST_TRANSPORT))
#define GIBBER_R_MULTICAST_TRANSPORT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_R_MULTICAST_TRANSPORT, GibberRMulticastTransportClass))

GibberRMulticastTransport *
gibber_r_multicast_transport_new(GibberRMulticastCausalTransport *transport);

gboolean
gibber_r_multicast_transport_connect(GibberRMulticastTransport *transport,
                                     GError **error);

gboolean
gibber_r_multicast_transport_send(GibberRMulticastTransport *transport,
                                  guint8 stream_id,
                                  const guint8 *data,
                                  gsize size,
                                  GError **error);

G_END_DECLS

#endif /* #ifndef __GIBBER_R_MULTICAST_TRANSPORT_H__*/
