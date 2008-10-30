/*
 * gibber-bytestream-oob.h - Header for GibberBytestreamOOB
 * Copyright (C) 2007 Collabora Ltd.
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

#ifndef __GIBBER_BYTESTREAM_OOB_H__
#define __GIBBER_BYTESTREAM_OOB_H__

#include <glib-object.h>

#include <netdb.h>

#include "gibber-bytestream-iface.h"

G_BEGIN_DECLS

typedef struct _GibberBytestreamOOB GibberBytestreamOOB;
typedef struct _GibberBytestreamOOBClass GibberBytestreamOOBClass;

typedef gboolean (* GibberBytestreamOOBCheckAddrFunc) (
    GibberBytestreamOOB *bytestream, struct sockaddr_storage *addr,
    socklen_t addrlen, gpointer user_data);


struct _GibberBytestreamOOBClass {
  GObjectClass parent_class;
};

struct _GibberBytestreamOOB {
  GObject parent;

  gpointer priv;
};

GType gibber_bytestream_oob_get_type (void);

/* TYPE MACROS */
#define GIBBER_TYPE_BYTESTREAM_OOB \
  (gibber_bytestream_oob_get_type ())
#define GIBBER_BYTESTREAM_OOB(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_BYTESTREAM_OOB,\
                              GibberBytestreamOOB))
#define GIBBER_BYTESTREAM_OOB_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_BYTESTREAM_OOB,\
                           GibberBytestreamOOBClass))
#define GIBBER_IS_BYTESTREAM_OOB(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_BYTESTREAM_OOB))
#define GIBBER_IS_BYTESTREAM_OOB_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_BYTESTREAM_OOB))
#define GIBBER_BYTESTREAM_OOB_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_BYTESTREAM_OOB,\
                              GibberBytestreamOOBClass))

void gibber_bytestream_oob_set_check_addr_func (
    GibberBytestreamOOB *bytestream, GibberBytestreamOOBCheckAddrFunc func,
    gpointer user_data);

G_END_DECLS

#endif /* #ifndef __GIBBER_BYTESTREAM_OOB_H__ */
