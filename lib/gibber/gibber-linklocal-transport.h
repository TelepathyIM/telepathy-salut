/*
 * gibber-linklocal-transport.h - Header for GibberLLTransport
 * Copyright (C) 2006 Collabora Ltd.
 *   @author: Sjoerd Simons <sjoerd@luon.net>
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

#ifndef __GIBBER_LL_TRANSPORT_H__
#define __GIBBER_LL_TRANSPORT_H__

#include <glib-object.h>

#include <sys/socket.h>
#include <netdb.h>

#include "gibber-transport.h"

G_BEGIN_DECLS

typedef struct _GibberLLTransport GibberLLTransport;
typedef struct _GibberLLTransportClass GibberLLTransportClass;


struct _GibberLLTransportClass {
    GibberTransportClass parent_class;
};

struct _GibberLLTransport {
    GibberTransport parent;
};

GType gibber_ll_transport_get_type(void);

/* TYPE MACROS */
#define GIBBER_TYPE_LL_TRANSPORT \
  (gibber_ll_transport_get_type())
#define GIBBER_LL_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_LL_TRANSPORT, GibberLLTransport))
#define GIBBER_LL_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_LL_TRANSPORT, GibberLLTransportClass))
#define GIBBER_IS_LL_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_LL_TRANSPORT))
#define GIBBER_IS_LL_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_LL_TRANSPORT))
#define GIBBER_LL_TRANSPORT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_LL_TRANSPORT, GibberLLTransportClass))

GibberLLTransport *
gibber_ll_transport_new(void);

void
gibber_ll_transport_open_fd(GibberLLTransport *connection, int fd);

gboolean
gibber_ll_transport_open_sockaddr(GibberLLTransport *connection,
                                  struct sockaddr_storage *addr,
                                  GError **error);

gboolean
gibber_ll_transport_is_incoming(GibberLLTransport *connection);

void
gibber_ll_transport_set_incoming(GibberLLTransport *connetion,
                                 gboolean incoming);


gboolean
gibber_ll_transport_get_address(GibberLLTransport *connection, 
                                struct sockaddr_storage *addr,
                                socklen_t *len);
G_END_DECLS

#endif /* #ifndef __GIBBER_LL_TRANSPORT_H__*/
