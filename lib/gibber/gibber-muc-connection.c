/*
 * gibber-muc-connection.c - Source for GibberMucConnection
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

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "gibber-muc-connection.h"
#include "signals-marshal.h"

#include "gibber-xmpp-reader.h"
#include "gibber-xmpp-writer.h"
#include "gibber-multicast-transport.h"
#include "gibber-r-multicast-transport.h"

#define PROTO_RMULTICAST  "rmulticast"

#define ADDRESS_KEY "address"
#define PORT_KEY "port"

#define DEBUG_FLAG DEBUG_MUC_CONNECTION
#include "gibber-debug.h"

static void _reader_received_stanza_cb(GibberXmppReader *reader,
                                       GibberXmppStanza *stanza,
                                       gpointer user_data);

static void _connection_received_data(GibberTransport *transport,
                                      GibberBuffer *buffer,
                                      gpointer user_data);


G_DEFINE_TYPE(GibberMucConnection, gibber_muc_connection, 
              G_TYPE_OBJECT)

/* signal enum */

enum
{
    RECEIVED_STANZA,
    RECEIVED_DATA,
    PARSE_ERROR,
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    NEW_SENDER,
    LOST_SENDER,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _GibberMucConnectionPrivate GibberMucConnectionPrivate;

struct _GibberMucConnectionPrivate
{
  gboolean dispose_has_run;
  gchar *name;
  gchar *protocol;
  gchar *address;
  gchar *port;
  
  GibberXmppReader *reader;
  GibberXmppWriter *writer;

  GHashTable *parameters;

  GibberMulticastTransport *mtransport;
  GibberRMulticastTransport *rmtransport;

  const gchar *current_sender;
};

#define GIBBER_MUC_CONNECTION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_MUC_CONNECTION, GibberMucConnectionPrivate))

GQuark
gibber_muc_connection_error_quark (void) {
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("gibber_muc_connection_error");

    return quark;
}


static void
gibber_muc_connection_init (GibberMucConnection *obj)
{
  GibberMucConnectionPrivate *priv = GIBBER_MUC_CONNECTION_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  priv->reader = gibber_xmpp_reader_new_no_stream();
  priv->writer = gibber_xmpp_writer_new_no_stream();
  g_signal_connect(priv->reader, "received-stanza",
                   G_CALLBACK(_reader_received_stanza_cb), obj);
}

static void gibber_muc_connection_dispose (GObject *object);
static void gibber_muc_connection_finalize (GObject *object);

static void
gibber_muc_connection_class_init (GibberMucConnectionClass *gibber_muc_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_muc_connection_class);

  g_type_class_add_private (gibber_muc_connection_class, sizeof (GibberMucConnectionPrivate));

  object_class->dispose = gibber_muc_connection_dispose;
  object_class->finalize = gibber_muc_connection_finalize;
  
  signals[RECEIVED_STANZA] = 
    g_signal_new("received-stanza", 
                 G_OBJECT_CLASS_TYPE(gibber_muc_connection_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 _gibber_signals_marshal_VOID__STRING_OBJECT,
                 G_TYPE_NONE, 2, G_TYPE_STRING, GIBBER_TYPE_XMPP_STANZA);
  signals[RECEIVED_DATA] = 
    g_signal_new("received-data", 
                 G_OBJECT_CLASS_TYPE(gibber_muc_connection_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 _gibber_signals_marshal_VOID__STRING_UCHAR_POINTER_ULONG,
                 G_TYPE_NONE, 4, G_TYPE_STRING,
                 G_TYPE_UCHAR, G_TYPE_POINTER, G_TYPE_ULONG);
  signals[PARSE_ERROR] = 
    g_signal_new("parse-error", 
                 G_OBJECT_CLASS_TYPE(gibber_muc_connection_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__STRING,
                 G_TYPE_NONE, 1, G_TYPE_STRING);

  signals[DISCONNECTED] = 
    g_signal_new("disconnected", 
                 G_OBJECT_CLASS_TYPE(gibber_muc_connection_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);

  signals[CONNECTING] = 
    g_signal_new("connecting", 
                 G_OBJECT_CLASS_TYPE(gibber_muc_connection_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);

  signals[CONNECTED] = 
    g_signal_new("connected", 
                 G_OBJECT_CLASS_TYPE(gibber_muc_connection_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);

  signals[NEW_SENDER] =
    g_signal_new("new-sender",
                 G_OBJECT_CLASS_TYPE(gibber_muc_connection_class),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__STRING,
                 G_TYPE_NONE, 1, G_TYPE_STRING);

  signals[LOST_SENDER] =
    g_signal_new("lost-sender",
                 G_OBJECT_CLASS_TYPE(gibber_muc_connection_class),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__STRING,
                 G_TYPE_NONE, 1, G_TYPE_STRING);
}

void
gibber_muc_connection_dispose (GObject *object)
{
  GibberMucConnection *self = GIBBER_MUC_CONNECTION (object);
  GibberMucConnectionPrivate *priv = GIBBER_MUC_CONNECTION_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */
  g_object_unref(priv->reader);
  g_object_unref(priv->writer);
  g_object_unref(priv->mtransport);
  g_object_unref(priv->rmtransport);

  if (G_OBJECT_CLASS (gibber_muc_connection_parent_class)->dispose)
    G_OBJECT_CLASS (gibber_muc_connection_parent_class)->dispose (object);
}

void
gibber_muc_connection_finalize (GObject *object)
{
  GibberMucConnection *self = GIBBER_MUC_CONNECTION (object);
  GibberMucConnectionPrivate *priv = GIBBER_MUC_CONNECTION_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  g_free(priv->name);
  priv->name = NULL;

  g_free(priv->protocol);
  priv->protocol = NULL;

  g_free(priv->address);
  priv->address = NULL;

  g_free(priv->port);
  priv->port = NULL;

  if (priv->parameters != NULL) {
    g_hash_table_destroy(priv->parameters);
    priv->parameters = NULL;
  }

  G_OBJECT_CLASS (gibber_muc_connection_parent_class)->finalize (object);
}

const gchar **
gibber_muc_connection_get_protocols(void) {
  static const gchar *protocols[] = { PROTO_RMULTICAST, NULL };
  return protocols;
} 

const gchar **
gibber_muc_connection_get_required_parameters(const gchar *protocol) {
  int i;
  static const gchar *parameters[] = { ADDRESS_KEY, PORT_KEY, NULL };
  struct {
    const gchar *protocol; 
    const gchar **parameters;
  } protocols[] = { { PROTO_RMULTICAST, parameters },
                    { NULL, NULL }
                  };

  for (i = 0; protocols[i].protocol != NULL; i++) {
    if (!strcmp(protocols[i].protocol, protocol)) {
      return protocols[i].parameters;
    }
  }
  return NULL;
}

static gboolean
gibber_muc_connection_validate_address(const gchar *address,
    const gchar *port,
    GError **error) {
  int ret;

  struct addrinfo hints;
  struct addrinfo *ans;

  hints.ai_flags = AI_PASSIVE;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  ret = getaddrinfo(address, port, &hints, &ans);
  if (ret < 0) {
    DEBUG("Getaddrinfo failed: %s", gai_strerror(ret));
    g_set_error(error,
                GIBBER_MUC_CONNECTION_ERROR,
                GIBBER_MUC_CONNECTION_ERROR_INVALID_ADDRESS,
                "Getaddrinfo failed: %s", gai_strerror(ret));

    goto err;
  }

  if (ans == NULL) {
    DEBUG("Couldn't find address");
    g_set_error(error,
                GIBBER_MUC_CONNECTION_ERROR,
                GIBBER_MUC_CONNECTION_ERROR_INVALID_ADDRESS,
                "Couldn't find address");
    goto err;
  }

  if (ans->ai_next != NULL) {
    g_set_error(error,
                GIBBER_MUC_CONNECTION_ERROR,
                GIBBER_MUC_CONNECTION_ERROR_INVALID_ADDRESS,
                "Address isn't unique");
    goto err;
  }

  freeaddrinfo(ans);

  return TRUE;
err:
  if (ans != NULL) {
    freeaddrinfo(ans);
  }
  g_assert(error == NULL || *error != NULL);
  return FALSE;
}

static void
gibber_muc_connection_create_random_address(GibberMucConnection *self) {
  GibberMucConnectionPrivate *priv =
    GIBBER_MUC_CONNECTION_GET_PRIVATE(self);
  gboolean ret;
  int p;

  g_free(priv->address);
  g_free(priv->port);

  /* Just pick any port above 1024 */
  p = g_random_int_range(1024, G_MAXUINT16);
  /* Ensure that we never mess with mdns */
  if (p == 5353)
    p++;
  priv->port = g_strdup_printf("%d", p);
  /* For now just pick ipv4 in the link-local scope... */
  priv->address = g_strdup_printf("224.0.0.%d", g_random_int_range(1, 254));

  /* Just to be sure */
  ret = gibber_muc_connection_validate_address(priv->address, priv->port, NULL);

  DEBUG("Generated random address: %s:%s", priv->address, priv->port);

  g_assert(ret);
}

GibberMucConnection *
gibber_muc_connection_new(const gchar *name, 
                         const gchar *protocol,
                         GHashTable *parameters,
                         GError **error) {
  const gchar *address = NULL;
  const gchar *port = NULL;
  GibberMucConnection *result;
  GibberMucConnectionPrivate *priv;

  if (protocol != NULL && strcmp(protocol, PROTO_RMULTICAST) != 0) {
    g_set_error(error, 
                GIBBER_MUC_CONNECTION_ERROR,
                GIBBER_MUC_CONNECTION_ERROR_INVALID_PROTOCOL,
                "Invalid protocol: %s", protocol);
  }

  if (parameters != NULL) {
    address = g_hash_table_lookup(parameters, ADDRESS_KEY);
    port = g_hash_table_lookup(parameters, PORT_KEY);
    if (address == NULL || port == NULL) {
      g_set_error(error, 
                  GIBBER_MUC_CONNECTION_ERROR,
                  GIBBER_MUC_CONNECTION_ERROR_INVALID_PARAMETERS,
                  "Missing address or port parameter");
      goto err;
    }
    if (!gibber_muc_connection_validate_address(address, port, error)) {
      goto err;
    }
  }

  /* Got an address, so we can init the transport */
  result = g_object_new(GIBBER_TYPE_MUC_CONNECTION, NULL);
  priv = GIBBER_MUC_CONNECTION_GET_PRIVATE (result);

  priv = GIBBER_MUC_CONNECTION_GET_PRIVATE (result);
  priv->name = g_strdup(name);
  if (protocol != NULL) { 
    priv->protocol = g_strdup(protocol);
  } else {
    priv->protocol = g_strdup(PROTO_RMULTICAST);
  }

  priv->address = g_strdup(address);
  priv->port = g_strdup(port);

  priv->mtransport = gibber_multicast_transport_new();
  priv->rmtransport = gibber_r_multicast_transport_new(
        GIBBER_TRANSPORT(priv->mtransport),
        priv->name);
  gibber_transport_set_handler(GIBBER_TRANSPORT(priv->rmtransport),
                               _connection_received_data,
                               result);
  return result;

err:
  g_assert(error == NULL || *error != NULL);
  return NULL;

}

static void
_rmtransport_new_sender_cb(GibberRMulticastTransport *transport,
                           gchar *sender, gpointer user_data) {
   g_signal_emit(GIBBER_MUC_CONNECTION(user_data), signals[NEW_SENDER],
       0, sender);
}

static void
_rmtransport_lost_sender_cb(GibberRMulticastTransport *transport,
                           gchar *sender, gpointer user_data) {
   g_signal_emit(GIBBER_MUC_CONNECTION(user_data), signals[LOST_SENDER],
       0, sender);
}


gboolean 
gibber_muc_connection_connect(GibberMucConnection *connection, GError **error) {
  GibberMucConnectionPrivate *priv = 
      GIBBER_MUC_CONNECTION_GET_PRIVATE(connection); 
  int ret = FALSE;

  if (connection->state > GIBBER_MUC_CONNECTION_DISCONNECTED) {
    return TRUE;
  }

  /* FIXME don't abuse the knowledge we connect syn */
  connection->state = GIBBER_MUC_CONNECTION_CONNECTING;
  g_signal_emit(connection, signals[CONNECTING], 0);

  if (priv->address == NULL) {
    int attempts = 10;
    do {
      gibber_muc_connection_create_random_address(connection);
      if (gibber_multicast_transport_connect(priv->mtransport,
                                             priv->address, priv->port)) {
        if (gibber_r_multicast_transport_connect(priv->rmtransport,
                                                   TRUE, NULL)) {
          ret = TRUE;
        }
        break;
      }
    } while (--attempts);
  } else {
    if (gibber_multicast_transport_connect(priv->mtransport,
                                             priv->address, priv->port)) {
      if (gibber_r_multicast_transport_connect(priv->rmtransport,
                                                TRUE, NULL)) {
        ret = TRUE;
      }
    }
  }
  if (!ret) {
    connection->state = GIBBER_MUC_CONNECTION_DISCONNECTED;
    g_signal_emit(connection, signals[DISCONNECTED], 0);

    if (gibber_transport_get_state(GIBBER_TRANSPORT(priv->mtransport)) !=
        GIBBER_TRANSPORT_DISCONNECTED) {
      gibber_transport_disconnect(GIBBER_TRANSPORT(priv->mtransport));
    }
    g_set_error(error, GIBBER_MUC_CONNECTION_ERROR,
      GIBBER_MUC_CONNECTION_ERROR_CONNECTION_FAILED,
      "Failed to connect to multicast group");
  } else {
    g_signal_connect(priv->rmtransport, "new-sender", 
                     G_CALLBACK(_rmtransport_new_sender_cb), connection);
    g_signal_connect(priv->rmtransport, "lost-sender", 
                     G_CALLBACK(_rmtransport_lost_sender_cb), connection);

    connection->state = GIBBER_MUC_CONNECTION_CONNECTED;
    g_signal_emit(connection, signals[CONNECTED], 0);
  }
  return ret;
}

void
gibber_muc_connection_disconnect(GibberMucConnection *connection) {
  GibberMucConnectionPrivate *priv = 
      GIBBER_MUC_CONNECTION_GET_PRIVATE(connection); 

  connection->state = GIBBER_MUC_CONNECTION_DISCONNECTED;
  g_signal_emit(connection, signals[DISCONNECTED], 0);

  gibber_transport_disconnect(GIBBER_TRANSPORT(priv->rmtransport));
}

const gchar *
gibber_muc_connection_get_protocol(GibberMucConnection *connection) {
  GibberMucConnectionPrivate *priv = 
      GIBBER_MUC_CONNECTION_GET_PRIVATE(connection); 
  return priv->protocol;
}

/* Current parameters of the transport. str -> str */
const GHashTable *
gibber_muc_connection_get_parameters(GibberMucConnection *connection) {
  GibberMucConnectionPrivate *priv = 
    GIBBER_MUC_CONNECTION_GET_PRIVATE(connection);

  g_assert(priv->mtransport != NULL && 
      gibber_transport_get_state(
          GIBBER_TRANSPORT(priv->mtransport)) ==
              GIBBER_TRANSPORT_CONNECTED);

  if (priv->parameters == NULL) {
    priv->parameters = g_hash_table_new(g_str_hash, g_str_equal); 
    g_hash_table_insert(priv->parameters, ADDRESS_KEY, priv->address);
    g_hash_table_insert(priv->parameters, PORT_KEY, priv->port);
  }
  return priv->parameters;
}

static void 
_reader_received_stanza_cb(GibberXmppReader *reader, GibberXmppStanza *stanza,
                 gpointer user_data) {
  GibberMucConnection *self = GIBBER_MUC_CONNECTION(user_data);
  GibberMucConnectionPrivate *priv = GIBBER_MUC_CONNECTION_GET_PRIVATE (self);

  g_assert(priv->current_sender != NULL);
  g_signal_emit(self, signals[RECEIVED_STANZA], 0, 
      priv->current_sender, stanza);
}


static void _connection_received_data(GibberTransport *transport,
                                      GibberBuffer *buffer,
                                      gpointer user_data) {
  GibberMucConnection *self = GIBBER_MUC_CONNECTION (user_data);
  GibberMucConnectionPrivate *priv = GIBBER_MUC_CONNECTION_GET_PRIVATE (self);
  GibberRMulticastBuffer *rmbuffer = (GibberRMulticastBuffer *)buffer;
  gboolean ret;
  GError *error = NULL;

  g_assert(buffer->length > 0);

  if (rmbuffer->stream_id != GIBBER_R_MULTICAST_DEFAULT_STREAM) {
    g_signal_emit(self, signals[RECEIVED_DATA], 0,
        rmbuffer->sender, rmbuffer->stream_id, buffer->data, buffer->length);
    return;
  }

  /* Ensure we're not disposed inside while running the reader is busy */
  g_object_ref(self);
  priv->current_sender = rmbuffer->sender;
  ret = gibber_xmpp_reader_push(priv->reader, buffer->data, 
                                buffer->length, &error);
  priv->current_sender = NULL;
  if (!ret) {
    g_signal_emit(self, signals[PARSE_ERROR], 0); 
  }
  g_object_unref(self);
}

gboolean
gibber_muc_connection_send(GibberMucConnection *connection,
                          GibberXmppStanza *stanza,
                          GError **error) {
  GibberMucConnectionPrivate *priv = 
    GIBBER_MUC_CONNECTION_GET_PRIVATE (connection);
  const guint8 *data;
  gsize length;

  if (!gibber_xmpp_writer_write_stanza(priv->writer, stanza,
                                      &data, &length, error)) {
    return FALSE;
  }

  return gibber_r_multicast_transport_send(priv->rmtransport,
      GIBBER_R_MULTICAST_DEFAULT_STREAM,
      data, length, error);
}

gboolean
gibber_muc_connection_send_raw(GibberMucConnection *connection,
                               guint8 stream_id,
                               const guint8 *data,
                               gsize size ,
                               GError **error) {
  GibberMucConnectionPrivate *priv =
    GIBBER_MUC_CONNECTION_GET_PRIVATE (connection);

  return gibber_r_multicast_transport_send(priv->rmtransport,
      stream_id, data, size, error);
}



