/*
 * salut-linklocal-transport.h - Header for SalutLLTransport
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

#ifndef __SALUT_LL_TRANSPORT_H__
#define __SALUT_LL_TRANSPORT_H__

#include <glib-object.h>

#include <sys/socket.h>
#include <netdb.h>

#include "salut-transport.h"

G_BEGIN_DECLS

typedef struct _SalutLLTransport SalutLLTransport;
typedef struct _SalutLLTransportClass SalutLLTransportClass;


struct _SalutLLTransportClass {
    SalutTransportClass parent_class;
};

struct _SalutLLTransport {
    SalutTransport parent;
};

GType salut_ll_transport_get_type(void);

/* TYPE MACROS */
#define SALUT_TYPE_LL_TRANSPORT \
  (salut_ll_transport_get_type())
#define SALUT_LL_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_LL_TRANSPORT, SalutLLTransport))
#define SALUT_LL_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_LL_TRANSPORT, SalutLLTransportClass))
#define SALUT_IS_LL_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_LL_TRANSPORT))
#define SALUT_IS_LL_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_LL_TRANSPORT))
#define SALUT_LL_TRANSPORT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_LL_TRANSPORT, SalutLLTransportClass))

SalutLLTransport *
salut_ll_transport_new(void);

void
salut_ll_transport_open_fd(SalutLLTransport *connection, int fd);

gboolean
salut_ll_transport_open_sockaddr(SalutLLTransport *connection,
                                  struct sockaddr_storage *addr,
                                  GError **error);

gboolean
salut_ll_transport_is_incoming(SalutLLTransport *connection);

void
salut_ll_transport_set_incoming(SalutLLTransport *connetion,
                                 gboolean incoming);


gboolean
salut_ll_transport_get_address(SalutLLTransport *connection, 
                                struct sockaddr_storage *addr,
                                socklen_t *len);
G_END_DECLS

#endif /* #ifndef __SALUT_LL_TRANSPORT_H__*/
