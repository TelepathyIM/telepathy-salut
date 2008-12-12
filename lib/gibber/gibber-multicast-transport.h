/*
 * gibber-multicast-muc-transport.h - Header for GibberMulticastTransport
 * Copyright (C) 2006 Collabora Ltd.
 * @author: Sjoerd Simons <sjoerd@luon.net>
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

#ifndef __GIBBER_MULTICAST_TRANSPORT_H__
#define __GIBBER_MULTICAST_TRANSPORT_H__

#include <glib-object.h>
#include "gibber-transport.h"

G_BEGIN_DECLS

GQuark gibber_multicast_transport_error_quark (void);
#define GIBBER_MULTICAST_TRANSPORT_ERROR \
  gibber_multicast_transport_error_quark ()

typedef enum
{
  GIBBER_MULTICAST_TRANSPORT_ERROR_JOIN_FAILED,
  GIBBER_MULTICAST_TRANSPORT_ERROR_INVALID_ADDRESS,
  GIBBER_MULTICAST_TRANSPORT_ERROR_MESSAGE_TOO_BIG,
  GIBBER_MULTICAST_TRANSPORT_ERROR_NETWORK,
} GibberMulticastTransportError;


typedef struct _GibberMulticastTransport GibberMulticastTransport;
typedef struct _GibberMulticastTransportClass GibberMulticastTransportClass;

struct _GibberMulticastTransportClass {
    GibberTransportClass parent_class;
};

struct _GibberMulticastTransport {
    GibberTransport parent;
};

GibberMulticastTransport * gibber_multicast_transport_new (void);

gboolean gibber_multicast_transport_connect (
  GibberMulticastTransport *mtransport, const gchar *address,
  const gchar *port);

gsize
gibber_multicast_transport_get_max_packet_size (
  GibberMulticastTransport *mtransport);

GType gibber_multicast_transport_get_type (void);

/* TYPE MACROS */
#define GIBBER_TYPE_MULTICAST_TRANSPORT \
  (gibber_multicast_transport_get_type ())
#define GIBBER_MULTICAST_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_MULTICAST_TRANSPORT, \
   GibberMulticastTransport))
#define GIBBER_MULTICAST_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_MULTICAST_TRANSPORT, \
   GibberMulticastTransportClass))
#define GIBBER_IS_MULTICAST_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_MULTICAST_TRANSPORT))
#define GIBBER_IS_MULTICAST_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_MULTICAST_TRANSPORT))
#define GIBBER_MULTICAST_TRANSPORT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_MULTICAST_TRANSPORT, \
   GibberMulticastTransportClass))


G_END_DECLS
#endif /* #ifndef __GIBBER_MULTICAST_TRANSPORT_H__*/
