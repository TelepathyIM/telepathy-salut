/*
 * test-transport.h - Header for TestTransport
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

#ifndef __TEST_TRANSPORT_H__
#define __TEST_TRANSPORT_H__

#include <glib-object.h>

#include <gibber/gibber-transport.h>

G_BEGIN_DECLS

typedef struct _TestTransport TestTransport;
typedef struct _TestTransportClass TestTransportClass;

struct _TestTransportClass {
    GibberTransportClass parent_class;
};

struct _TestTransport {
    GibberTransport parent;
};

GType test_transport_get_type(void);

/* TYPE MACROS */
#define TEST_TYPE_TRANSPORT \
  (test_transport_get_type())
#define TEST_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TEST_TYPE_TRANSPORT, TestTransport))
#define TEST_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TEST_TYPE_TRANSPORT, TestTransportClass))
#define TEST_IS_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TEST_TYPE_TRANSPORT))
#define TEST_IS_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TEST_TYPE_TRANSPORT))
#define TEST_TRANSPORT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TEST_TYPE_TRANSPORT, TestTransportClass))

typedef gboolean (*test_transport_send_hook)(GibberTransport *transport,
                                             const guint8 *data,
                                             gsize length,
                                             GError **error);
TestTransport *test_transport_new(test_transport_send_hook send);

void test_transport_write(TestTransport *transport,
                          const guint8 *buf, gsize size);

G_END_DECLS

#endif /* #ifndef __TEST_TRANSPORT_H__*/
