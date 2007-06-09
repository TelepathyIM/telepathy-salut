/*
 * gibber-xmpp-connection.c - Source for GibberXmppConnection
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

#include "gibber-xmpp-connection.h"
#include "gibber-xmpp-connection-signals-marshal.h"

#include "gibber-xmpp-reader.h"
#include "gibber-xmpp-writer.h"
#include "gibber-transport.h"
#include "gibber-xmpp-stanza.h"

#define XMPP_STREAM_NAMESPACE "http://etherx.jabber.org/streams"

static void _xmpp_connection_received_data(GibberTransport *transport,
                                           GibberBuffer *buffer,
                                           gpointer user_data);

G_DEFINE_TYPE(GibberXmppConnection, gibber_xmpp_connection, G_TYPE_OBJECT)

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
_reader_stream_opened_cb(GibberXmppReader *reader, 
                         const gchar *to, const gchar *from,
                         const gchar *version,
                         gpointer user_data);

static void 
_reader_stream_closed_cb(GibberXmppReader *reader, gpointer user_data);

static void _reader_received_stanza_cb(GibberXmppReader *reader, 
                                       GibberXmppStanza *stanza,
                                       gpointer user_data);

/* private structure */
typedef struct _GibberXmppConnectionPrivate GibberXmppConnectionPrivate;

struct _GibberXmppConnectionPrivate
{
  GibberXmppReader *reader;
  GibberXmppWriter *writer;
  gboolean dispose_has_run;
  gboolean stream_opened;
};

#define GIBBER_XMPP_CONNECTION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_XMPP_CONNECTION, GibberXmppConnectionPrivate))

static GObject *
gibber_xmpp_connection_constructor(GType type,
                                   guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj;
  GibberXmppConnectionPrivate *priv;
  
  obj = G_OBJECT_CLASS(gibber_xmpp_connection_parent_class)->
        constructor(type, n_props, props);

  priv = GIBBER_XMPP_CONNECTION_GET_PRIVATE (obj);

  priv->writer = gibber_xmpp_writer_new();
  priv->reader = gibber_xmpp_reader_new();
  priv->stream_opened = FALSE;

  g_signal_connect(priv->reader, "stream-opened", 
                   G_CALLBACK(_reader_stream_opened_cb), obj);
  g_signal_connect(priv->reader, "stream-closed", 
                   G_CALLBACK(_reader_stream_closed_cb), obj);
  g_signal_connect(priv->reader, "received-stanza", 
                    G_CALLBACK(_reader_received_stanza_cb), obj);

  return obj;
}

static void
gibber_xmpp_connection_init (GibberXmppConnection *obj) {
  obj->transport = NULL;
}

static void gibber_xmpp_connection_dispose (GObject *object);
static void gibber_xmpp_connection_finalize (GObject *object);

static void
gibber_xmpp_connection_class_init (GibberXmppConnectionClass *gibber_xmpp_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_xmpp_connection_class);

  g_type_class_add_private (gibber_xmpp_connection_class, sizeof (GibberXmppConnectionPrivate));

  object_class->dispose = gibber_xmpp_connection_dispose;
  object_class->finalize = gibber_xmpp_connection_finalize;

  object_class->constructor = gibber_xmpp_connection_constructor;

  signals[STREAM_OPENED] = 
    g_signal_new("stream-opened", 
                 G_OBJECT_CLASS_TYPE(gibber_xmpp_connection_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 gibber_xmpp_connection_marshal_VOID__STRING_STRING_STRING,
                 G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
  signals[STREAM_CLOSED] = 
    g_signal_new("stream-closed", 
                 G_OBJECT_CLASS_TYPE(gibber_xmpp_connection_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);
  signals[RECEIVED_STANZA] = 
    g_signal_new("received-stanza", 
                 G_OBJECT_CLASS_TYPE(gibber_xmpp_connection_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__OBJECT,
                 G_TYPE_NONE, 1, GIBBER_TYPE_XMPP_STANZA);
  signals[PARSE_ERROR] = 
    g_signal_new("parse-error", 
                 G_OBJECT_CLASS_TYPE(gibber_xmpp_connection_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);
}

void
gibber_xmpp_connection_dispose (GObject *object)
{
  GibberXmppConnection *self = GIBBER_XMPP_CONNECTION (object);
  GibberXmppConnectionPrivate *priv = GIBBER_XMPP_CONNECTION_GET_PRIVATE (self);

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

  if (G_OBJECT_CLASS (gibber_xmpp_connection_parent_class)->dispose)
    G_OBJECT_CLASS (gibber_xmpp_connection_parent_class)->dispose (object);
}

void
gibber_xmpp_connection_finalize (GObject *object) {
  G_OBJECT_CLASS (gibber_xmpp_connection_parent_class)->finalize (object);
}



static GibberXmppConnection *
new_connection(GibberTransport *transport, gboolean stream)  {
  GibberXmppConnection * result;

  result = g_object_new(GIBBER_TYPE_XMPP_CONNECTION, NULL);

  if (transport != NULL) {
    gibber_xmpp_connection_engage(result, transport);
  }

  return result;
}

GibberXmppConnection *
gibber_xmpp_connection_new(GibberTransport *transport) { 
  return new_connection(transport, TRUE);
}

GibberXmppConnection *
gibber_xmpp_connection_new_no_stream(GibberTransport *transport) {
  return new_connection(transport, FALSE);
}

void 
gibber_xmpp_connection_open(GibberXmppConnection *connection,
                            const gchar *to, const gchar *from,
                            const gchar *version) {
  GibberXmppConnectionPrivate *priv = 
    GIBBER_XMPP_CONNECTION_GET_PRIVATE (connection);
  const guint8 *data;
  gsize length;

  gibber_xmpp_writer_stream_open(priv->writer, to, from, 
                                  version, &data, &length);
  if (priv->stream_opened) {
    /* Stream was already opened, ropening it */
    gibber_xmpp_reader_reset(priv->reader);
  }
  priv->stream_opened = TRUE;
  gibber_transport_send(connection->transport, data, length, NULL);
}

void
gibber_xmpp_connection_restart(GibberXmppConnection *connection) {
  GibberXmppConnectionPrivate *priv = 
    GIBBER_XMPP_CONNECTION_GET_PRIVATE (connection);

  g_assert(priv->stream_opened);
  gibber_xmpp_reader_reset(priv->reader);
  priv->stream_opened = FALSE;
}

void 
gibber_xmpp_connection_close(GibberXmppConnection *connection) {
  GibberXmppConnectionPrivate *priv = 
    GIBBER_XMPP_CONNECTION_GET_PRIVATE (connection);
  const guint8 *data;
  gsize length;

  gibber_xmpp_writer_stream_close(priv->writer, &data, &length);
  gibber_transport_send(connection->transport, data, length, NULL);
}

void 
gibber_xmpp_connection_engage(GibberXmppConnection *connection, 
    GibberTransport *transport) {
  g_assert(connection->transport == NULL);

  connection->transport = g_object_ref(transport);
  gibber_transport_set_handler(transport,
                               _xmpp_connection_received_data,
                               connection);
}

void 
gibber_xmpp_connection_disengage(GibberXmppConnection *connection) {
  g_assert(connection->transport != NULL);

  gibber_transport_set_handler(connection->transport, NULL, NULL);

  g_object_unref(connection->transport);
  connection->transport = NULL;
}

gboolean
gibber_xmpp_connection_send(GibberXmppConnection *connection, 
                                GibberXmppStanza *stanza, GError **error) {
  GibberXmppConnectionPrivate *priv = 
    GIBBER_XMPP_CONNECTION_GET_PRIVATE (connection);
  const guint8 *data;
  gsize length;

  if (!gibber_xmpp_writer_write_stanza(priv->writer, stanza,
                                      &data, &length, error)) {
    return FALSE;
  }

  return gibber_transport_send(connection->transport, data, length, error);
}

static void _xmpp_connection_received_data(GibberTransport *transport,
                                           GibberBuffer *buffer,
                                           gpointer user_data) {
  GibberXmppConnection *self = GIBBER_XMPP_CONNECTION (user_data);
  GibberXmppConnectionPrivate *priv = GIBBER_XMPP_CONNECTION_GET_PRIVATE (self);
  gboolean ret;
  GError *error = NULL;

  g_assert(buffer->length > 0);

  /* Ensure we're not disposed inside while running the reader is busy */
  g_object_ref(self);
  ret = gibber_xmpp_reader_push(priv->reader, buffer->data, 
                                buffer->length, &error);
  if (!ret) {
    g_signal_emit(self, signals[PARSE_ERROR], 0); 
  }
  g_object_unref(self);
}

static void 
_reader_stream_opened_cb(GibberXmppReader *reader, 
                         const gchar *to, const gchar *from, 
                         const gchar *version,
                         gpointer user_data) {
  GibberXmppConnection *self = GIBBER_XMPP_CONNECTION (user_data);

  g_signal_emit(self, signals[STREAM_OPENED], 0, to, from, version);
}

static void 
_reader_stream_closed_cb(GibberXmppReader *reader, 
                         gpointer user_data) {
  GibberXmppConnection *self = GIBBER_XMPP_CONNECTION (user_data);

  g_signal_emit(self, signals[STREAM_CLOSED], 0);
}

static void 
_reader_received_stanza_cb(GibberXmppReader *reader, GibberXmppStanza *stanza,
                 gpointer user_data) {
  GibberXmppConnection *self = GIBBER_XMPP_CONNECTION (user_data);
  g_signal_emit(self, signals[RECEIVED_STANZA], 0, stanza);
}
