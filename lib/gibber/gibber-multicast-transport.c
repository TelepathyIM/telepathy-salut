/*
 * gibber-multicast-muc-transport.c - Source for GibberMulticastTransport
 * Copyright (C) 2006 Collabora Ltd.
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
#include <unistd.h>
#include <errno.h>
#include <string.h>

/* Networking stuff */
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

#include <gibber-multicast-transport.h>

#define DEBUG_FLAG DEBUG_NET
#include <gibber-debug.h>

#define BUFSIZE 1500
#define MAX_PACKET_SIZE 1440

static gboolean 
gibber_multicast_transport_send (GibberTransport *transport, 
                                    const guint8 *data, gsize size,
                                    GError **error);

static void 
gibber_multicast_transport_disconnect(GibberTransport *transport);

G_DEFINE_TYPE(GibberMulticastTransport, gibber_multicast_transport, 
              GIBBER_TYPE_TRANSPORT);

/* Privates */
typedef struct _GibberMulticastTransportPrivate GibberMulticastTransportPrivate;

struct _GibberMulticastTransportPrivate
{
  gboolean dispose_has_run;
  GIOChannel *channel;
  int fd;
  guint watch_in;
  guint watch_err;
  struct sockaddr_storage address;
  socklen_t addrlen;
};

#define GIBBER_MULTICAST_TRANSPORT_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_MULTICAST_TRANSPORT, GibberMulticastTransportPrivate))

GQuark 
gibber_multicast_transport_error_quark (void) {
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("gibber_multicast_transport_error");

  return quark;
}

static void
gibber_multicast_transport_init (GibberMulticastTransport *obj)
{
  GibberMulticastTransportPrivate *priv = GIBBER_MULTICAST_TRANSPORT_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  priv->fd = -1;
  priv->watch_in = 0;
  priv->watch_err = 0;
  priv->channel = NULL;
}

static void gibber_multicast_transport_dispose (GObject *object);
static void gibber_multicast_transport_finalize (GObject *object);

static void
gibber_multicast_transport_class_init(GibberMulticastTransportClass *gibber_multicast_transport_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_multicast_transport_class);
  GibberTransportClass *transport_class =
      GIBBER_TRANSPORT_CLASS(gibber_multicast_transport_class);


  g_type_class_add_private (gibber_multicast_transport_class, sizeof (GibberMulticastTransportPrivate));

  object_class->dispose = gibber_multicast_transport_dispose;
  object_class->finalize = gibber_multicast_transport_finalize;

  transport_class->send = gibber_multicast_transport_send;
  transport_class->disconnect = gibber_multicast_transport_disconnect;
}

void
gibber_multicast_transport_dispose (GObject *object)
{
  GibberMulticastTransport *self = GIBBER_MULTICAST_TRANSPORT (object);
  GibberMulticastTransportPrivate *priv = GIBBER_MULTICAST_TRANSPORT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;
  if (priv->channel) {
    gibber_multicast_transport_disconnect(GIBBER_TRANSPORT(self));
  }

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gibber_multicast_transport_parent_class)->dispose)
    G_OBJECT_CLASS (gibber_multicast_transport_parent_class)->dispose (object);
}

void
gibber_multicast_transport_finalize (GObject *object)
{
  /*GibberMulticastTransport *self = GIBBER_MULTICAST_TRANSPORT (object);
  GibberMulticastTransportPrivate *priv = GIBBER_MULTICAST_TRANSPORT_GET_PRIVATE (self);
  */

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (gibber_multicast_transport_parent_class)->finalize (object);
}

static gboolean
_channel_io_in(GIOChannel *source, GIOCondition condition, gpointer data) {
  GibberMulticastTransport *self = 
    GIBBER_MULTICAST_TRANSPORT(data);
  GibberMulticastTransportPrivate *priv = 
    GIBBER_MULTICAST_TRANSPORT_GET_PRIVATE(self);
  guint8 buf[BUFSIZE + 1];

  struct sockaddr_storage from;
  int ret;
  socklen_t len = sizeof(struct sockaddr_storage);

  ret = recvfrom(priv->fd, buf, BUFSIZE, 
                 0, (struct sockaddr *)&from, &len);
  if (ret < 0) {
    DEBUG("recv failed: %s", strerror(errno)); 
    /* FIXME should throw error */
    return TRUE;
  }

  buf[ret]  = '\0';
  DEBUG("Received %d bytes", ret);

  gibber_transport_received_data(GIBBER_TRANSPORT(self), buf, ret);

  return TRUE;
}

static gboolean
_channel_io_err(GIOChannel *source, GIOCondition condition, gpointer data) {
  /* Either _HUP or _ERR */
  /* Should disconnect */
  DEBUG("ERR");
  return TRUE;
}

static int
_open_multicast(GibberMulticastTransport *self, GError **error) {
  GibberMulticastTransportPrivate *priv =
    GIBBER_MULTICAST_TRANSPORT_GET_PRIVATE(self);
  int yes = 1;
  unsigned char one = 1;
  unsigned char no = 0;
  int fd = -1;

  g_assert(self != NULL);
#define SETSOCKOPT(s, level, optname, optval, len) G_STMT_START { \
  if (setsockopt(s, level, optname, optval, len) != 0) {          \
    g_set_error(error, GIBBER_MULTICAST_TRANSPORT_ERROR,          \
                GIBBER_MULTICAST_TRANSPORT_ERROR_JOIN_FAILED,     \
                #optname " failed: %s", strerror(errno));         \
    goto err;                                                     \
  }                                                               \
} G_STMT_END


  /* Only try the first! */
  switch (priv->address.ss_family) {
    case AF_INET: {
      struct ip_mreq mreq;
      struct sockaddr_in baddr;
      fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

      if (fd < 0) {
        g_set_error(error, GIBBER_MULTICAST_TRANSPORT_ERROR, 
                  GIBBER_MULTICAST_TRANSPORT_ERROR_JOIN_FAILED,
                  "Failed to open the socket: %s", strerror(errno));
        DEBUG("Failed to open socket: %s", strerror(errno));
        goto err;
      }

      SETSOCKOPT(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
      SETSOCKOPT(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
      SETSOCKOPT(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &no, sizeof(yes));
      SETSOCKOPT(fd, IPPROTO_IP, IP_MULTICAST_TTL, &one, sizeof(one));

      mreq.imr_multiaddr = ((struct sockaddr_in *)&(priv->address))->sin_addr;
      mreq.imr_interface.s_addr = htonl(INADDR_ANY);

      SETSOCKOPT(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

      memset(&baddr, 0, sizeof(baddr));
      baddr.sin_family      = AF_INET;
      baddr.sin_addr.s_addr = htonl (INADDR_ANY);
      baddr.sin_port        =
          ((struct sockaddr_in *)&(priv->address))->sin_port;

      if (bind(fd, (struct sockaddr *)&baddr, sizeof(baddr)) != 0) {
        DEBUG("Failed to bind to socket: %s", strerror(errno));
        g_set_error(error, GIBBER_MULTICAST_TRANSPORT_ERROR, 
                  GIBBER_MULTICAST_TRANSPORT_ERROR_JOIN_FAILED,
                  "Failed to bind to socket: %s", strerror(errno));
        goto err;
      }

      break;
    }
    case AF_INET6: {
      struct ipv6_mreq mreq6;
      fd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);

      if (fd < 0) {
        g_set_error(error, GIBBER_MULTICAST_TRANSPORT_ERROR, 
                  GIBBER_MULTICAST_TRANSPORT_ERROR_JOIN_FAILED,
                  "Failed to open the socket: %s", strerror(errno));
        goto err;
      }

      mreq6.ipv6mr_multiaddr = ((struct sockaddr_in6 *)&priv->address)->sin6_addr;
      mreq6.ipv6mr_interface = 0;
      SETSOCKOPT(fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq6, sizeof(mreq6));

      if (bind(fd, (struct sockaddr *)&(priv->address), priv->addrlen ) != 0) {
        DEBUG("Failed to bind to socket: %s", strerror(errno));
        g_set_error(error, GIBBER_MULTICAST_TRANSPORT_ERROR, 
                  GIBBER_MULTICAST_TRANSPORT_ERROR_JOIN_FAILED,
                  "Failed to bind to socket: %s", strerror(errno));
        goto err;
      }
      break;
    }
    default:
      DEBUG("Address from an unsupported address family: %d", 
             priv->address.ss_family);
      g_set_error(error, GIBBER_MULTICAST_TRANSPORT_ERROR, 
                  GIBBER_MULTICAST_TRANSPORT_ERROR_JOIN_FAILED,
                  "Unknown address family");

  }

  return fd;
err:
  if (fd > 0) {
    close(fd);
  }
  return -1;
#undef SETSOCKOPT
}

static gboolean
gibber_multicast_transport_validate_address(const gchar *address,
    const gchar *port,
    struct sockaddr_storage *sock_addr,
    size_t *socklen,
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
                GIBBER_MULTICAST_TRANSPORT_ERROR,
                GIBBER_MULTICAST_TRANSPORT_ERROR_INVALID_ADDRESS,
                "Getaddrinfo failed: %s", gai_strerror(ret));
                   
    goto err;
  }

  if (ans == NULL) {
    DEBUG("Couldn't find address");
    g_set_error(error, 
                GIBBER_MULTICAST_TRANSPORT_ERROR,
                GIBBER_MULTICAST_TRANSPORT_ERROR_INVALID_ADDRESS,
                "Couldn't find address");
    goto err;
  }

  if (ans->ai_next != NULL) {
    g_set_error(error, 
                GIBBER_MULTICAST_TRANSPORT_ERROR,
                GIBBER_MULTICAST_TRANSPORT_ERROR_INVALID_ADDRESS,
                "Address isn't unique");
    goto err;
  }

  memcpy(sock_addr, ans->ai_addr, ans->ai_addrlen);
  *socklen = ans->ai_addrlen;


  freeaddrinfo(ans);

  return TRUE;
err:
  if (ans != NULL) {
    freeaddrinfo(ans);
  }
  g_assert(error == NULL || *error != NULL);
  return FALSE;
}

gboolean
gibber_multicast_transport_connect(GibberMulticastTransport *mtransport,
                                   const gchar *address, const gchar *port) {
  GibberMulticastTransportPrivate *priv = 
    GIBBER_MULTICAST_TRANSPORT_GET_PRIVATE(mtransport);
  GError *error = NULL;
  int fd = -1;
  
  gibber_transport_set_state(GIBBER_TRANSPORT(mtransport), 
                             GIBBER_TRANSPORT_CONNECTING);
  if (!gibber_multicast_transport_validate_address(address, port,
                                                   &(priv->address),
                                                   &(priv->addrlen),
                                                   &error)) {
    goto failed;
  }

  /* Address already set, must use this one */
  fd = _open_multicast(mtransport, &error);

  if (fd < 0 ) {
    goto failed;
  }

  g_assert(priv->channel == NULL);

  priv->fd = fd;

  priv->channel = g_io_channel_unix_new(fd);

  g_io_channel_set_close_on_unref(priv->channel, TRUE);
  g_io_channel_set_encoding(priv->channel, NULL, NULL);
  g_io_channel_set_buffered(priv->channel, FALSE);

  priv->watch_in = 
    g_io_add_watch(priv->channel, G_IO_IN, _channel_io_in, mtransport);
  priv->watch_err = 
      g_io_add_watch(priv->channel, G_IO_ERR|G_IO_HUP, _channel_io_err, 
          mtransport);

  gibber_transport_set_state(GIBBER_TRANSPORT(mtransport), 
      GIBBER_TRANSPORT_CONNECTED);

  return TRUE;

failed:
  g_assert(error != NULL);
  gibber_transport_emit_error(GIBBER_TRANSPORT(mtransport), error);

  g_error_free(error);
  gibber_transport_set_state(GIBBER_TRANSPORT(mtransport), 
      GIBBER_TRANSPORT_DISCONNECTED);

  return FALSE;  
}

void 
gibber_multicast_transport_disconnect (GibberTransport *transport) {
  GibberMulticastTransport *self = 
    GIBBER_MULTICAST_TRANSPORT(transport);
  GibberMulticastTransportPrivate *priv = 
    GIBBER_MULTICAST_TRANSPORT_GET_PRIVATE(self);

  /* Ensure we're connected */
  g_assert(priv->fd > 0);

  if (priv->watch_in)  {
    g_source_remove(priv->watch_in);
    priv->watch_in = 0;
  }

  if (priv->watch_err) {
    g_source_remove(priv->watch_err);
    priv->watch_err = 0;
  }

  if (priv->channel) {
    g_io_channel_shutdown(priv->channel, TRUE, NULL);
    g_io_channel_unref(priv->channel);
    priv->channel = NULL;
  }

  priv->fd = -1;

  gibber_transport_set_state(GIBBER_TRANSPORT(self), GIBBER_TRANSPORT_DISCONNECTED);
}

static gboolean 
gibber_multicast_transport_send (GibberTransport *transport, 
                                    const guint8 *data, gsize size,
                                    GError **error) {
  GibberMulticastTransport *self = 
    GIBBER_MULTICAST_TRANSPORT(transport);
  GibberMulticastTransportPrivate *priv = 
    GIBBER_MULTICAST_TRANSPORT_GET_PRIVATE(self);

  if (size > MAX_PACKET_SIZE) {
    DEBUG("Message too big");
    *error = g_error_new(GIBBER_MULTICAST_TRANSPORT_ERROR,
        GIBBER_MULTICAST_TRANSPORT_ERROR_MESSAGE_TOO_BIG, 
        "Message too big");
    return FALSE;
  }

  if (sendto(priv->fd, data, size, 0, 
             (struct sockaddr *)&(priv->address), 
             sizeof(struct sockaddr_storage)) < 0) {
    DEBUG("send failed: %s", strerror(errno));
    if (error != NULL) {
      *error = g_error_new(GIBBER_MULTICAST_TRANSPORT_ERROR,
           GIBBER_MULTICAST_TRANSPORT_ERROR_NETWORK, 
          "Network error: %s", strerror(errno));
    } 
  }

  return TRUE;
}

gsize
gibber_multicast_transport_get_max_packet_size(
    GibberMulticastTransport *mtransport) 
{
  return MAX_PACKET_SIZE;
}


GibberMulticastTransport *
gibber_multicast_transport_new(void) { 
  GibberMulticastTransport *transport;

  transport = g_object_new(GIBBER_TYPE_MULTICAST_TRANSPORT, NULL);

  return transport;
}

