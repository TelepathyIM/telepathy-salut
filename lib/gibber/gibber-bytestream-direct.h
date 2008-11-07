/*
 * gibber-bytestream-direct.h - Header for GibberBytestreamDirect
 * Copyright (C) 2008 Collabora Ltd.
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

#ifndef __GIBBER_BYTESTREAM_DIRECT_H__
#define __GIBBER_BYTESTREAM_DIRECT_H__

#include <glib-object.h>
#include <netdb.h>
#include "gibber-bytestream-iface.h"

G_BEGIN_DECLS

typedef struct _GibberBytestreamDirect GibberBytestreamDirect;
typedef struct _GibberBytestreamDirectClass GibberBytestreamDirectClass;

typedef gboolean (* GibberBytestreamDirectCheckAddrFunc) (
    GibberBytestreamDirect *bytestream, struct sockaddr *addr,
    socklen_t addrlen, gpointer user_data);

struct _GibberBytestreamDirectClass {
  GObjectClass parent_class;
};

struct _GibberBytestreamDirect {
  GObject parent;

  gpointer priv;
};

GType gibber_bytestream_direct_get_type (void);

/* TYPE MACROS */
#define GIBBER_TYPE_BYTESTREAM_DIRECT \
  (gibber_bytestream_direct_get_type ())
#define GIBBER_BYTESTREAM_DIRECT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_BYTESTREAM_DIRECT,\
                              GibberBytestreamDirect))
#define GIBBER_BYTESTREAM_DIRECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_BYTESTREAM_DIRECT,\
                           GibberBytestreamDirectClass))
#define GIBBER_IS_BYTESTREAM_DIRECT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_BYTESTREAM_DIRECT))
#define GIBBER_IS_BYTESTREAM_DIRECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_BYTESTREAM_DIRECT))
#define GIBBER_BYTESTREAM_DIRECT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_BYTESTREAM_DIRECT,\
                              GibberBytestreamDirectClass))

void gibber_bytestream_direct_set_check_addr_func (
    GibberBytestreamDirect *bytestream,
    GibberBytestreamDirectCheckAddrFunc func, gpointer user_data);

gboolean gibber_bytestream_direct_accept_socket (
    GibberBytestreamIface *bytestream, int listen_fd);

G_END_DECLS

#endif /* #ifndef __GIBBER_BYTESTREAM_DIRECT_H__ */
