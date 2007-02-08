/*
 * salut-muc-connection.c - Source for SalutMucConnection
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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG_FLAG DEBUG_MUC_CONNECTION
#include "debug.h"

#include "salut-muc-connection.h"
#include "salut-muc-connection-signals-marshal.h"

#include <gibber/gibber-xmpp-reader.h>
#include <gibber/gibber-xmpp-writer.h>
#include <gibber/gibber-transport.h>
#include <gibber/gibber-xmpp-stanza.h>

static void _muc_connection_received_data(GibberTransport *transport,
                                           const guint8 *data,
                                           gsize length,
                                           gpointer user_data);

G_DEFINE_TYPE(SalutMucConnection, salut_muc_connection, G_TYPE_OBJECT);

/* signal enum */
enum
{
  PARSE_ERROR,
  RECEIVED_STANZA,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

static void _reader_received_stanza_cb(GibberXmppReader *reader, 
                                       GibberXmppStanza *stanza,
                                       gpointer user_data);

/* private structure */
typedef struct _SalutMucConnectionPrivate SalutMucConnectionPrivate;

struct _SalutMucConnectionPrivate
{
  GibberXmppReader *reader;
  GibberXmppWriter *writer;
  gboolean dispose_has_run;
};

#define SALUT_MUC_CONNECTION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_MUC_CONNECTION, SalutMucConnectionPrivate))

static void
salut_muc_connection_init (SalutMucConnection *obj) {
  SalutMucConnectionPrivate *priv = SALUT_MUC_CONNECTION_GET_PRIVATE (obj);
  obj->transport = NULL;

  priv->writer = gibber_xmpp_writer_new_no_stream();
  priv->reader = gibber_xmpp_reader_new_no_stream();

  g_signal_connect(priv->reader, "received-stanza", 
                    G_CALLBACK(_reader_received_stanza_cb), obj);
}

static void salut_muc_connection_dispose (GObject *object);
static void salut_muc_connection_finalize (GObject *object);

static void
salut_muc_connection_class_init (SalutMucConnectionClass *salut_muc_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_muc_connection_class);

  g_type_class_add_private (salut_muc_connection_class, sizeof (SalutMucConnectionPrivate));

  object_class->dispose = salut_muc_connection_dispose;
  object_class->finalize = salut_muc_connection_finalize;

  signals[RECEIVED_STANZA] = 
    g_signal_new("received-stanza", 
                 G_OBJECT_CLASS_TYPE(salut_muc_connection_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__OBJECT,
                 G_TYPE_NONE, 1, GIBBER_TYPE_XMPP_STANZA);
  signals[PARSE_ERROR] = 
    g_signal_new("parse-error", 
                 G_OBJECT_CLASS_TYPE(salut_muc_connection_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);
}

void
salut_muc_connection_dispose (GObject *object)
{
  SalutMucConnection *self = SALUT_MUC_CONNECTION (object);
  SalutMucConnectionPrivate *priv = SALUT_MUC_CONNECTION_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;
  if (self->transport != NULL) {
    g_object_unref(self->transport);
    self->transport = NULL;
  }

  if (priv->reader != NULL) {
    g_object_unref(priv->reader);
    priv->reader = NULL;
  }

  if (priv->writer != NULL) {
    g_object_unref(priv->writer);
    priv->writer = NULL;
  }

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_muc_connection_parent_class)->dispose)
    G_OBJECT_CLASS (salut_muc_connection_parent_class)->dispose (object);
}

void
salut_muc_connection_finalize (GObject *object) {
  G_OBJECT_CLASS (salut_muc_connection_parent_class)->finalize (object);
}


SalutMucConnection *
salut_muc_connection_new(GibberTransport *transport)  {
  SalutMucConnection * result;

  result = g_object_new(SALUT_TYPE_MUC_CONNECTION, NULL);
  result->transport = g_object_ref(transport);

  g_signal_connect(transport, "received",
                    G_CALLBACK(_muc_connection_received_data), result);
  return result;
}

gboolean
salut_muc_connection_send(SalutMucConnection *connection, 
                                GibberXmppStanza *stanza, GError **error) {
  SalutMucConnectionPrivate *priv = 
    SALUT_MUC_CONNECTION_GET_PRIVATE (connection);
  const guint8 *data;
  gsize length;

  if (!gibber_xmpp_writer_write_stanza(priv->writer, stanza,
                                      &data, &length, error)) {
    return FALSE;
  }

  return gibber_transport_send(connection->transport, data, length, error);
}

static void 
_muc_connection_received_data(GibberTransport *transport,
                               const guint8 *data, gsize length,
                               gpointer user_data) {
  SalutMucConnection *self = SALUT_MUC_CONNECTION (user_data);
  SalutMucConnectionPrivate *priv = SALUT_MUC_CONNECTION_GET_PRIVATE (self);
  gboolean ret;
  GError *error = NULL;

  g_assert(length > 0);

  /* Ensure we're not disposed inside while running the reader is busy */
  g_object_ref(self);
  ret = gibber_xmpp_reader_push(priv->reader, data, length, &error);
  if (!ret) {
    g_signal_emit(self, signals[PARSE_ERROR], 0); 
  }
  g_object_unref(self);
}

static void 
_reader_received_stanza_cb(GibberXmppReader *reader, GibberXmppStanza *stanza,
                 gpointer user_data) {
  SalutMucConnection *self = SALUT_MUC_CONNECTION (user_data);
  DEBUG("Received stanza");
  g_signal_emit(self, signals[RECEIVED_STANZA], 0, stanza);
}
