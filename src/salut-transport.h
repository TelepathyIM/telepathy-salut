/*
 * salut-transport.h - Header for SalutTransport
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

#ifndef __SALUT_TRANSPORT_H__
#define __SALUT_TRANSPORT_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
  SALUT_TRANSPORT_DISCONNECTED,
  SALUT_TRANSPORT_CONNECTING,
  SALUT_TRANSPORT_CONNECTED,
} SalutTransportState;


typedef struct _SalutTransport SalutTransport;
typedef struct _SalutTransportClass SalutTransportClass;

struct _SalutTransportClass {
    GObjectClass parent_class;
    gboolean (*send) (SalutTransport *transport, 
                          const guint8 *data, gsize length, GError **error);
    void (*disconnect) (SalutTransport *transport);
};

struct _SalutTransport {
    GObject parent;
    SalutTransportState state;
};

GType salut_transport_get_type(void);

/* TYPE MACROS */
#define SALUT_TYPE_TRANSPORT \
  (salut_transport_get_type())
#define SALUT_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_TRANSPORT, SalutTransport))
#define SALUT_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_TRANSPORT, SalutTransportClass))
#define SALUT_IS_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_TRANSPORT))
#define SALUT_IS_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_TRANSPORT))
#define SALUT_TRANSPORT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_TRANSPORT, SalutTransportClass))

/* Utility functions for the classes based on SalutTransport   */
void salut_transport_received_data(SalutTransport *transport,  
                                   const guint8 *data, 
                                   gsize length);
void salut_transport_set_state(SalutTransport *transport, 
                               SalutTransportState state);

SalutTransportState salut_transport_get_state(SalutTransport *transport);

void salut_transport_emit_error (SalutTransport *transport, GError *error);

/* Public api */
gboolean salut_transport_send(SalutTransport *transport, 
                              const guint8 *data, 
                              gsize size, 
                              GError **error); 
void salut_transport_disconnect(SalutTransport *transport);

G_END_DECLS

#endif /* #ifndef __SALUT_TRANSPORT_H__*/
