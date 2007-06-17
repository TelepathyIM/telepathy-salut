/*
 * test-transport.c - Source for TestTransport
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


#include <stdio.h>
#include <stdlib.h>

#include "test-transport.h"

static gboolean
test_transport_send(GibberTransport *transport, const guint8 *data, gsize size,
                                                GError **error);
void
test_transport_disconnect(GibberTransport *transport);


G_DEFINE_TYPE(TestTransport, test_transport, GIBBER_TYPE_TRANSPORT)

/* private structure */
typedef struct _TestTransportPrivate TestTransportPrivate;

struct _TestTransportPrivate
{
  gboolean dispose_has_run;
  test_transport_send_hook send;
  GQueue *buffers;
  guint send_id;
  gpointer user_data;
};

#define TEST_TRANSPORT_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), TEST_TYPE_TRANSPORT, TestTransportPrivate))

static void
test_transport_init (TestTransport *obj)
{
  TestTransportPrivate *priv = TEST_TRANSPORT_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  priv->send = NULL;
  priv->buffers = g_queue_new();
  priv->send_id = 0;
}

static void test_transport_dispose (GObject *object);
static void test_transport_finalize (GObject *object);

static void
test_transport_class_init (TestTransportClass *test_transport_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (test_transport_class);
  GibberTransportClass *transport_class = 
    GIBBER_TRANSPORT_CLASS(test_transport_class);

  g_type_class_add_private (test_transport_class, sizeof (TestTransportPrivate));

  object_class->dispose = test_transport_dispose;
  object_class->finalize = test_transport_finalize;

  transport_class->send = test_transport_send;
  transport_class->disconnect = test_transport_disconnect;
}

void
test_transport_dispose (GObject *object)
{
  TestTransport *self = TEST_TRANSPORT (object);
  TestTransportPrivate *priv = TEST_TRANSPORT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  if (priv->send_id != 0) 
    g_source_remove(priv->send_id);

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (test_transport_parent_class)->dispose)
    G_OBJECT_CLASS (test_transport_parent_class)->dispose (object);
}

static void
free_array(gpointer data, gpointer user_data) {
  g_array_free((GArray *)data, TRUE);
}

void
test_transport_finalize (GObject *object)
{
  TestTransport *self = TEST_TRANSPORT (object);
  TestTransportPrivate *priv = TEST_TRANSPORT_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  g_queue_foreach(priv->buffers, free_array, NULL);
  g_queue_free(priv->buffers);
  G_OBJECT_CLASS (test_transport_parent_class)->finalize (object);
}

static gboolean
send_data(gpointer data) {
  TestTransport *self = TEST_TRANSPORT (data);
  TestTransportPrivate *priv = TEST_TRANSPORT_GET_PRIVATE (self);
  GArray *arr;

  g_assert(priv->send != NULL);

  arr = (GArray *)g_queue_pop_head(priv->buffers);

  priv->send(GIBBER_TRANSPORT(self), 
             (guint8 *)arr->data, arr->len,
             NULL, priv->user_data);

  if (g_queue_is_empty(priv->buffers)) {
    priv->send_id = 0;
    return FALSE;
  }

  return TRUE;
}

static gboolean
test_transport_send(GibberTransport *transport, 
                    const guint8 *data, gsize size, GError **error) {
  TestTransport *self = TEST_TRANSPORT (transport);
  TestTransportPrivate *priv = TEST_TRANSPORT_GET_PRIVATE (self);

  GArray *arr;

  arr = g_array_sized_new(FALSE, TRUE, sizeof(guint8), size);
  g_array_append_vals(arr, data, size);

  g_queue_push_tail(priv->buffers, arr);

  if (priv->send_id == 0) {
    priv->send_id = g_idle_add(send_data, transport);
  }

  return TRUE;
}

void
test_transport_disconnect(GibberTransport *transport) {
  gibber_transport_set_state(GIBBER_TRANSPORT(transport), 
                            GIBBER_TRANSPORT_DISCONNECTED);
}


TestTransport *
test_transport_new(test_transport_send_hook send, gpointer user_data) {
  TestTransport *self;
  TestTransportPrivate *priv;

  self = g_object_new(TEST_TYPE_TRANSPORT, NULL);
  priv  = TEST_TRANSPORT_GET_PRIVATE (self);
  priv->send = send;
  priv->user_data = user_data;

  gibber_transport_set_state(GIBBER_TRANSPORT(self),
                            GIBBER_TRANSPORT_CONNECTED);

  return self;
}

void 
test_transport_write(TestTransport *transport, const guint8 *buf, gsize size) {
  gibber_transport_received_data(GIBBER_TRANSPORT(transport), buf, size);
}
