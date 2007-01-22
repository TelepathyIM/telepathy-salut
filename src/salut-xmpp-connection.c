/*
 * salut-xmpp-connection.c - Source for SalutXmppConnection
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

#include "salut-xmpp-connection.h"
#include "salut-xmpp-connection-signals-marshal.h"

#include "salut-xmpp-reader.h"
#include "salut-xmpp-writer.h"
#include "salut-transport.h"
#include "salut-xmpp-stanza.h"

#define XMPP_STREAM_NAMESPACE "http://etherx.jabber.org/streams"

static void _xmpp_connection_received_data(SalutTransport *transport,
                                           const guint8 *data,
                                           gsize length,
                                           gpointer user_data);

G_DEFINE_TYPE(SalutXmppConnection, salut_xmpp_connection, G_TYPE_OBJECT)

/* signal enum */
enum
{
  STREAM_OPENED,
  STREAM_CLOSED,
  PARSE_ERROR,
  RECEIVED_STANZA,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

static void 
_reader_stream_opened_cb(SalutXmppReader *reader, 
                         const gchar *to, const gchar *from,
                         const gchar *version,
                         gpointer user_data);

static void 
_reader_stream_closed_cb(SalutXmppReader *reader, gpointer user_data);

static void _reader_received_stanza_cb(SalutXmppReader *reader, 
                                       SalutXmppStanza *stanza,
                                       gpointer user_data);

/* private structure */
typedef struct _SalutXmppConnectionPrivate SalutXmppConnectionPrivate;

struct _SalutXmppConnectionPrivate
{
  SalutXmppReader *reader;
  SalutXmppWriter *writer;
  gboolean dispose_has_run;
};

#define SALUT_XMPP_CONNECTION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_XMPP_CONNECTION, SalutXmppConnectionPrivate))

static void
salut_xmpp_connection_init (SalutXmppConnection *obj) {
  SalutXmppConnectionPrivate *priv = SALUT_XMPP_CONNECTION_GET_PRIVATE (obj);
  obj->transport = NULL;

  priv->writer = salut_xmpp_writer_new();

  priv->reader = salut_xmpp_reader_new();
  g_signal_connect(priv->reader, "stream-opened", 
                    G_CALLBACK(_reader_stream_opened_cb), obj);
  g_signal_connect(priv->reader, "received-stanza", 
                    G_CALLBACK(_reader_received_stanza_cb), obj);
  g_signal_connect(priv->reader, "stream-closed", 
                    G_CALLBACK(_reader_stream_closed_cb), obj);
}

static void salut_xmpp_connection_dispose (GObject *object);
static void salut_xmpp_connection_finalize (GObject *object);

static void
salut_xmpp_connection_class_init (SalutXmppConnectionClass *salut_xmpp_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_xmpp_connection_class);

  g_type_class_add_private (salut_xmpp_connection_class, sizeof (SalutXmppConnectionPrivate));

  object_class->dispose = salut_xmpp_connection_dispose;
  object_class->finalize = salut_xmpp_connection_finalize;

  signals[STREAM_OPENED] = 
    g_signal_new("stream-opened", 
                 G_OBJECT_CLASS_TYPE(salut_xmpp_connection_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 salut_xmpp_connection_marshal_VOID__STRING_STRING_STRING,
                 G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
  signals[STREAM_CLOSED] = 
    g_signal_new("stream-closed", 
                 G_OBJECT_CLASS_TYPE(salut_xmpp_connection_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);
  signals[RECEIVED_STANZA] = 
    g_signal_new("received-stanza", 
                 G_OBJECT_CLASS_TYPE(salut_xmpp_connection_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__OBJECT,
                 G_TYPE_NONE, 1, SALUT_TYPE_XMPP_STANZA);
  signals[PARSE_ERROR] = 
    g_signal_new("parse-error", 
                 G_OBJECT_CLASS_TYPE(salut_xmpp_connection_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);
}

void
salut_xmpp_connection_dispose (GObject *object)
{
  SalutXmppConnection *self = SALUT_XMPP_CONNECTION (object);
  SalutXmppConnectionPrivate *priv = SALUT_XMPP_CONNECTION_GET_PRIVATE (self);

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

  if (G_OBJECT_CLASS (salut_xmpp_connection_parent_class)->dispose)
    G_OBJECT_CLASS (salut_xmpp_connection_parent_class)->dispose (object);
}

void
salut_xmpp_connection_finalize (GObject *object) {
  G_OBJECT_CLASS (salut_xmpp_connection_parent_class)->finalize (object);
}


SalutXmppConnection *
salut_xmpp_connection_new(SalutTransport *transport)  {
  SalutXmppConnection * result;

  result = g_object_new(SALUT_TYPE_XMPP_CONNECTION, NULL);
  result->transport = g_object_ref(transport);

  g_signal_connect(transport, "received",
                    G_CALLBACK(_xmpp_connection_received_data), result);
  return result;
}

void 
salut_xmpp_connection_open(SalutXmppConnection *connection,
                            const gchar *to, const gchar *from,
                            const gchar *version) {
  SalutXmppConnectionPrivate *priv = 
    SALUT_XMPP_CONNECTION_GET_PRIVATE (connection);
  const guint8 *data;
  gsize length;

  salut_xmpp_writer_stream_open(priv->writer, to, from, 
                                  version, &data, &length);
  salut_transport_send(connection->transport, data, length, NULL);
}

void 
salut_xmpp_connection_close(SalutXmppConnection *connection) {
  SalutXmppConnectionPrivate *priv = 
    SALUT_XMPP_CONNECTION_GET_PRIVATE (connection);
  const guint8 *data;
  gsize length;

  salut_xmpp_writer_stream_close(priv->writer, &data, &length);
  salut_transport_send(connection->transport, data, length, NULL);
}

gboolean
salut_xmpp_connection_send(SalutXmppConnection *connection, 
                                SalutXmppStanza *stanza, GError **error) {
  SalutXmppConnectionPrivate *priv = 
    SALUT_XMPP_CONNECTION_GET_PRIVATE (connection);
  const guint8 *data;
  gsize length;

  if (!salut_xmpp_writer_write_stanza(priv->writer, stanza,
                                      &data, &length, error)) {
    return FALSE;
  }

  return salut_transport_send(connection->transport, data, length, error);
}

static void 
_xmpp_connection_received_data(SalutTransport *transport,
                               const guint8 *data, gsize length,
                               gpointer user_data) {
  SalutXmppConnection *self = SALUT_XMPP_CONNECTION (user_data);
  SalutXmppConnectionPrivate *priv = SALUT_XMPP_CONNECTION_GET_PRIVATE (self);
  gboolean ret;
  GError *error = NULL;

  g_assert(length > 0);

  /* Ensure we're not disposed inside while running the reader is busy */
  g_object_ref(self);
  ret = salut_xmpp_reader_push(priv->reader, data, length, &error);
  if (!ret) {
    g_signal_emit(self, signals[PARSE_ERROR], 0); 
  }
  g_object_unref(self);
}

static void 
_reader_stream_opened_cb(SalutXmppReader *reader, 
                         const gchar *to, const gchar *from, 
                         const gchar *version,
                         gpointer user_data) {
  SalutXmppConnection *self = SALUT_XMPP_CONNECTION (user_data);
  g_signal_emit(self, signals[STREAM_OPENED], 0, to, from, version);
}

static void 
_reader_stream_closed_cb(SalutXmppReader *reader, 
                         gpointer user_data) {
  SalutXmppConnection *self = SALUT_XMPP_CONNECTION (user_data);
  g_signal_emit(self, signals[STREAM_CLOSED], 0);
}

static void 
_reader_received_stanza_cb(SalutXmppReader *reader, SalutXmppStanza *stanza,
                 gpointer user_data) {
  SalutXmppConnection *self = SALUT_XMPP_CONNECTION (user_data);
  g_signal_emit(self, signals[RECEIVED_STANZA], 0, stanza);
}
