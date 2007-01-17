/*
 * salut-transport.c - Source for SalutTransport
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

#include "salut-transport.h"
#include "salut-transport-signals-marshal.h"

G_DEFINE_TYPE(SalutTransport, salut_transport, G_TYPE_OBJECT)

/* signal enum */
enum
{
  CONNECTED,
  CONNECTING,
  DISCONNECTED,
  RECEIVED,
  ERROR,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _SalutTransportPrivate SalutTransportPrivate;

struct _SalutTransportPrivate
{
  gboolean dispose_has_run;
};

#define SALUT_TRANSPORT_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_TRANSPORT, SalutTransportPrivate))

static void
salut_transport_init (SalutTransport *obj)
{
  obj->state = SALUT_TRANSPORT_DISCONNECTED;
}

static void salut_transport_dispose (GObject *object);
static void salut_transport_finalize (GObject *object);

static void
salut_transport_class_init (SalutTransportClass *salut_transport_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_transport_class);

  g_type_class_add_private (salut_transport_class, sizeof (SalutTransportPrivate));

  object_class->dispose = salut_transport_dispose;
  object_class->finalize = salut_transport_finalize;
  signals[CONNECTED] = 
    g_signal_new ("connected",
                  G_OBJECT_CLASS_TYPE (salut_transport_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[CONNECTING] =
    g_signal_new ("connecting",
                  G_OBJECT_CLASS_TYPE (salut_transport_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[DISCONNECTED] = 
    g_signal_new ("disconnected",
                  G_OBJECT_CLASS_TYPE (salut_transport_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[RECEIVED] = 
    g_signal_new ("received",
                  G_OBJECT_CLASS_TYPE (salut_transport_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_transport_marshal_VOID__POINTER_ULONG,
                  G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_ULONG);

  signals[ERROR] = 
    g_signal_new ("error",
                  G_OBJECT_CLASS_TYPE (salut_transport_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_transport_marshal_VOID__UINT_INT_STRING,
                  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_INT, G_TYPE_STRING);
}

void
salut_transport_dispose (GObject *object)
{
  SalutTransport *self = SALUT_TRANSPORT (object);
  SalutTransportPrivate *priv = SALUT_TRANSPORT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_transport_parent_class)->dispose)
    G_OBJECT_CLASS (salut_transport_parent_class)->dispose (object);
}

void
salut_transport_finalize (GObject *object)
{
  G_OBJECT_CLASS (salut_transport_parent_class)->finalize (object);
}

void
salut_transport_received_data(SalutTransport *transport, 
                                    const guint8 *data, gsize length) {
  g_signal_emit(transport, signals[RECEIVED], 0, data, length);
}

void 
salut_transport_set_state(SalutTransport *transport, 
                          SalutTransportState state) {
  if (state != transport->state) {
    transport->state = state;
    switch (state) {
      case SALUT_TRANSPORT_DISCONNECTED:
        g_signal_emit(transport, signals[DISCONNECTED], 0);
        break;
      case SALUT_TRANSPORT_CONNECTING:
        g_signal_emit(transport, signals[CONNECTING], 0);
        break;
      case SALUT_TRANSPORT_CONNECTED:
        g_signal_emit(transport, signals[CONNECTED], 0);
        break;
    }
  }
}

SalutTransportState 
salut_transport_get_state(SalutTransport *transport) {
  return transport->state;
}

void 
salut_transport_emit_error(SalutTransport *transport, GError *error) {
  g_signal_emit(transport, signals[ERROR], 0, 
                error->domain, error->code, error->message);
}

gboolean 
salut_transport_send(SalutTransport *transport, const guint8 *data, gsize size, 
                     GError **error) {
  SalutTransportClass *cls = SALUT_TRANSPORT_GET_CLASS(transport);
  return cls->send(transport, data, size, error);
}

void 
salut_transport_disconnect(SalutTransport *transport) {
  SalutTransportClass *cls = SALUT_TRANSPORT_GET_CLASS(transport);
  return cls->disconnect(transport);
}
