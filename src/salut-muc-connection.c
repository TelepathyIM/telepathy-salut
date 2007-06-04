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
#include <stdlib.h>

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "salut-muc-connection.h"
#include "salut-muc-connection-signals-marshal.h"

#include <gibber/gibber-xmpp-connection.h>
#include <gibber/gibber-multicast-transport.h>
#include <gibber/gibber-r-multicast-transport.h>

#define PROTO_MULTICAST   "multicast"
#define PROTO_RMULTICAST  "rmulticast"

#define ADDRESS_KEY "address"
#define PORT_KEY "port"

#define DEBUG_FLAG DEBUG_MUC_CONNECTION
#include "debug.h"

G_DEFINE_TYPE(SalutMucConnection, salut_muc_connection, 
              GIBBER_TYPE_XMPP_CONNECTION)

/* signal enum */
/*
enum
{
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};
*/

/* private structure */
typedef struct _SalutMucConnectionPrivate SalutMucConnectionPrivate;

struct _SalutMucConnectionPrivate
{
  gboolean dispose_has_run;
  gchar *name;
  gchar *protocol;
  gchar *address;
  gchar *port;
  gboolean rmulticast;
  GHashTable *parameters;
  GibberMulticastTransport *mtransport;
  GibberRMulticastTransport *rmtransport;
};

#define SALUT_MUC_CONNECTION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_MUC_CONNECTION, SalutMucConnectionPrivate))

GQuark
salut_muc_connection_error_quark (void) {
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("salut_muc_connection_error");

    return quark;
}


static void
salut_muc_connection_init (SalutMucConnection *obj)
{

  /* allocate any data required by the object here */
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

}

void
salut_muc_connection_dispose (GObject *object)
{
  SalutMucConnection *self = SALUT_MUC_CONNECTION (object);
  SalutMucConnectionPrivate *priv = SALUT_MUC_CONNECTION_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_muc_connection_parent_class)->dispose)
    G_OBJECT_CLASS (salut_muc_connection_parent_class)->dispose (object);
}

void
salut_muc_connection_finalize (GObject *object)
{
  SalutMucConnection *self = SALUT_MUC_CONNECTION (object);
  SalutMucConnectionPrivate *priv = SALUT_MUC_CONNECTION_GET_PRIVATE (self);

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

  G_OBJECT_CLASS (salut_muc_connection_parent_class)->finalize (object);
}

const gchar **
salut_muc_connection_get_protocols(void) {
  static const gchar *protocols[] = { PROTO_MULTICAST, PROTO_RMULTICAST, NULL };
  return protocols;
} 

const gchar **
salut_muc_connection_get_required_parameters(const gchar *protocol) {
  int i;
  static const gchar *parameters[] = { ADDRESS_KEY, PORT_KEY, NULL };
  struct {
    const gchar *protocol; 
    const gchar **parameters;
  } protocols[] = { { PROTO_MULTICAST, parameters },
                    { PROTO_RMULTICAST, parameters },
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
salut_muc_connection_validate_address(const gchar *address,
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
                SALUT_MUC_CONNECTION_ERROR,
                SALUT_MUC_CONNECTION_ERROR_INVALID_ADDRESS,
                "Getaddrinfo failed: %s", gai_strerror(ret));

    goto err;
  }

  if (ans == NULL) {
    DEBUG("Couldn't find address");
    g_set_error(error,
                SALUT_MUC_CONNECTION_ERROR,
                SALUT_MUC_CONNECTION_ERROR_INVALID_ADDRESS,
                "Couldn't find address");
    goto err;
  }

  if (ans->ai_next != NULL) {
    g_set_error(error,
                SALUT_MUC_CONNECTION_ERROR,
                SALUT_MUC_CONNECTION_ERROR_INVALID_ADDRESS,
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
salut_muc_connection_create_random_address(SalutMucConnection *self) {
  SalutMucConnectionPrivate *priv =
    SALUT_MUC_CONNECTION_GET_PRIVATE(self);
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
  ret = salut_muc_connection_validate_address(priv->address, priv->port, NULL);

  DEBUG("Generated random address: %s:%s", priv->address, priv->port);

  g_assert(ret);
}

SalutMucConnection *
salut_muc_connection_new(const gchar *name, 
                         const gchar *protocol,
                         GHashTable *parameters,
                         GError **error) {
  const gchar *address = NULL;
  const gchar *port = NULL;
  SalutMucConnection *result;
  SalutMucConnectionPrivate *priv;

  if (protocol != NULL &&
      strcmp(protocol, PROTO_MULTICAST) != 0 &&
      strcmp(protocol, PROTO_RMULTICAST) != 0) {
    g_set_error(error, 
                SALUT_MUC_CONNECTION_ERROR,
                SALUT_MUC_CONNECTION_ERROR_INVALID_PROTOCOL,
                "Invalid protocol: %s", protocol);
  }


  if (parameters != NULL) {
    address = g_hash_table_lookup(parameters, ADDRESS_KEY);
    port = g_hash_table_lookup(parameters, PORT_KEY);
    if (address == NULL || port == NULL) {
      g_set_error(error, 
                  SALUT_MUC_CONNECTION_ERROR,
                  SALUT_MUC_CONNECTION_ERROR_INVALID_PARAMETERS,
                  "Missing address or port parameter");
      goto err;
    }
    if (!salut_muc_connection_validate_address(address, port, error)) {
      goto err;
    }
  }

  /* Got an address, so we can init the transport */
  result = g_object_new(SALUT_TYPE_MUC_CONNECTION, 
                        "streaming", FALSE, NULL);
  priv = SALUT_MUC_CONNECTION_GET_PRIVATE (result);

  priv = SALUT_MUC_CONNECTION_GET_PRIVATE (result);
  priv->name = g_strdup(name);
  if (protocol != NULL) { 
    priv->protocol = g_strdup(protocol);
  } else {
    priv->protocol = g_strdup(PROTO_RMULTICAST);
  }
  priv->address = g_strdup(address);
  priv->port = g_strdup(port);
  priv->rmulticast = (strcmp(priv->protocol, PROTO_RMULTICAST) == 0);

  priv->mtransport = gibber_multicast_transport_new();
  if (priv->rmulticast) {
    priv->rmtransport = gibber_r_multicast_transport_new(
        GIBBER_TRANSPORT(priv->mtransport),
        priv->name);
    gibber_xmpp_connection_engage(GIBBER_XMPP_CONNECTION(result),
                                  GIBBER_TRANSPORT(priv->rmtransport));
  } else {
    gibber_xmpp_connection_engage(GIBBER_XMPP_CONNECTION(result),
                                  GIBBER_TRANSPORT(priv->mtransport));
  }


  return result;
err:
  g_assert(error == NULL || *error != NULL);
  return NULL;

}

gboolean 
salut_muc_connection_connect(SalutMucConnection *connection, GError **error) {
  SalutMucConnectionPrivate *priv = 
      SALUT_MUC_CONNECTION_GET_PRIVATE(connection); 
  int ret = FALSE;

  if (priv->address == NULL) {
    int attempts = 10;
    do {
      salut_muc_connection_create_random_address(connection);
      g_free(priv->protocol);
      priv->protocol = g_strdup(PROTO_MULTICAST);
      if (gibber_multicast_transport_connect(priv->mtransport, 
                                             priv->address, priv->port)) {
        if (priv->rmulticast) { 
          if (gibber_r_multicast_transport_connect(priv->rmtransport, 
                                                   TRUE, NULL)) {
            ret = TRUE;
          }
        } else {
          ret = TRUE;
        }
        break;
      }
    } while (--attempts);
  } else {
    if (gibber_multicast_transport_connect(priv->mtransport, 
                                             priv->address, priv->port)) {
      if (priv->rmulticast) { 
        if (gibber_r_multicast_transport_connect(priv->rmtransport, 
                                                   TRUE, NULL)) {
          ret = TRUE;
        }
      } else {
        ret = TRUE;
      }
    }
  }
  if (!ret) {
    g_set_error(error, SALUT_MUC_CONNECTION_ERROR,
        SALUT_MUC_CONNECTION_ERROR_CONNECTION_FAILED,
        "Failed to connect to multicast group");
  }
  return ret;
}

const gchar *
salut_muc_connection_get_protocol(SalutMucConnection *connection) {
  SalutMucConnectionPrivate *priv = 
      SALUT_MUC_CONNECTION_GET_PRIVATE(connection); 
  return priv->protocol;
}

/* Current parameters of the transport. str -> str */
const GHashTable *
salut_muc_connection_get_parameters(SalutMucConnection *connection) {
  SalutMucConnectionPrivate *priv = 
    SALUT_MUC_CONNECTION_GET_PRIVATE(connection);

  g_assert(GIBBER_XMPP_CONNECTION(connection)->transport != NULL && 
      gibber_transport_get_state(
          GIBBER_TRANSPORT(GIBBER_XMPP_CONNECTION(connection)->transport)) == 
              GIBBER_TRANSPORT_CONNECTED);

  if (priv->parameters == NULL) {
    priv->parameters = g_hash_table_new(g_str_hash, g_str_equal); 
    g_hash_table_insert(priv->parameters, ADDRESS_KEY, priv->address);
    g_hash_table_insert(priv->parameters, PORT_KEY, priv->port);
  }
  return priv->parameters;
}
