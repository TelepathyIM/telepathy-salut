/*
 * gibber-ssl-transport.h - Header for GibberSSLTransport
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

#ifndef __GIBBER_SSL_TRANSPORT_H__
#define __GIBBER_SSL_TRANSPORT_H__

#include <glib-object.h>

#include "gibber-transport.h"

G_BEGIN_DECLS

GQuark gibber_ssl_transport_error_quark (void);
#define GIBBER_SSL_TRANSPORT_ERROR gibber_ssl_transport_error_quark()

typedef enum
{
  /* Connection error */
  GIBBER_SSL_TRANSPORT_ERROR_CONNECTION_OPEN,
} GibberSSLTransportError;

typedef struct _GibberSSLTransport GibberSSLTransport;
typedef struct _GibberSSLTransportClass GibberSSLTransportClass;

struct _GibberSSLTransportClass {
    GibberTransportClass parent_class;
};

struct _GibberSSLTransport {
    GibberTransport parent;
};

GType gibber_ssl_transport_get_type(void);

/* TYPE MACROS */
#define GIBBER_TYPE_SSL_TRANSPORT \
  (gibber_ssl_transport_get_type())
#define GIBBER_SSL_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_SSL_TRANSPORT, GibberSSLTransport))
#define GIBBER_SSL_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_SSL_TRANSPORT, GibberSSLTransportClass))
#define GIBBER_IS_SSL_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_SSL_TRANSPORT))
#define GIBBER_IS_SSL_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_SSL_TRANSPORT))
#define GIBBER_SSL_TRANSPORT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_SSL_TRANSPORT, GibberSSLTransportClass))


GibberSSLTransport *
gibber_ssl_transport_new(GibberTransport *transport);

gboolean
gibber_ssl_transport_connect(GibberSSLTransport *ssl,
                             const gchar *server, GError **error);

G_END_DECLS

#endif /* #ifndef __GIBBER_SSL_TRANSPORT_H__*/
