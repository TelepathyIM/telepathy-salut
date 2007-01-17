/*
 * salut-linklocal-transport.c - Source for SalutLLTransport
 * Copyright (C) 2006 Collabora Ltd.
 *   @author: Sjoerd Simons <sjoerd@luon.net>
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
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "salut-linklocal-transport.h"
#include "telepathy-errors.h"

#define DEBUG_FLAG DEBUG_NET
#include "debug.h"

/* Buffer size used for reading input */
#define BUFSIZE 1024

static gboolean
_channel_io_out(GIOChannel *source, GIOCondition condition, gpointer data);


gboolean
salut_ll_transport_send(SalutTransport *transport, 
                        const guint8 *data, gsize size,
                        GError **error);
void
salut_ll_transport_disconnect(SalutTransport *transport);

static void _do_disconnect(SalutLLTransport *self);

G_DEFINE_TYPE(SalutLLTransport, salut_ll_transport, SALUT_TYPE_TRANSPORT)

/* private structure */
typedef struct _SalutLLTransportPrivate SalutLLTransportPrivate;

struct _SalutLLTransportPrivate
{
  GIOChannel *channel;
  gboolean incoming;
  gboolean dispose_has_run;
  guint watch_in;
  guint watch_out;
  guint watch_err;
  int fd;
  GString *output_buffer;
};

#define SALUT_LL_TRANSPORT_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_LL_TRANSPORT, SalutLLTransportPrivate))

static void
salut_ll_transport_init (SalutLLTransport *self)
{
  SalutLLTransportPrivate *priv = SALUT_LL_TRANSPORT_GET_PRIVATE (self);
  priv->incoming = FALSE;
  priv->channel = NULL;
  priv->output_buffer = NULL;
  priv->watch_in = 0;
  priv->watch_out = 0;
  priv->watch_err = 0;
  priv->fd = -1;
}

static void salut_ll_transport_dispose (GObject *object);
static void salut_ll_transport_finalize (GObject *object);

static void
salut_ll_transport_class_init (SalutLLTransportClass *salut_ll_transport_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_ll_transport_class);
  SalutTransportClass *transport_class =
    SALUT_TRANSPORT_CLASS(salut_ll_transport_class);

  g_type_class_add_private (salut_ll_transport_class, 
                            sizeof (SalutLLTransportPrivate));

  object_class->dispose = salut_ll_transport_dispose;
  object_class->finalize = salut_ll_transport_finalize;

  transport_class->send = salut_ll_transport_send;
  transport_class->disconnect = salut_ll_transport_disconnect;
}

void
salut_ll_transport_dispose (GObject *object)
{
  SalutLLTransport *self = SALUT_LL_TRANSPORT (object);
  SalutLLTransportPrivate *priv = SALUT_LL_TRANSPORT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  _do_disconnect(self);

  if (G_OBJECT_CLASS (salut_ll_transport_parent_class)->dispose)
    G_OBJECT_CLASS (salut_ll_transport_parent_class)->dispose (object);
}

void
salut_ll_transport_finalize (GObject *object)
{
  G_OBJECT_CLASS (salut_ll_transport_parent_class)->finalize (object);
}

static void
_do_disconnect(SalutLLTransport *self) {
  SalutLLTransportPrivate *priv = SALUT_LL_TRANSPORT_GET_PRIVATE (self);

  if (SALUT_TRANSPORT(self)->state == SALUT_TRANSPORT_DISCONNECTED) {
    return;
  }
  DEBUG("Closing the transport");
  if (priv->channel != NULL) {
    g_source_remove(priv->watch_in);
    if (priv->watch_out) 
      g_source_remove(priv->watch_out);
    g_source_remove(priv->watch_err);
    g_io_channel_shutdown(priv->channel, FALSE, NULL);
    g_io_channel_unref(priv->channel);
    priv->channel = NULL;
  } else {
    close(priv->fd);
  }
  priv->fd = -1;

  if (priv->output_buffer) {
    g_string_free(priv->output_buffer, TRUE);
    priv->output_buffer = NULL;
  }

  salut_transport_set_state(SALUT_TRANSPORT(self), 
                            SALUT_TRANSPORT_DISCONNECTED);
}

static gboolean
_try_write(SalutLLTransport *self, const guint8 *data, int len, gsize *written) {
  SalutLLTransportPrivate *priv = SALUT_LL_TRANSPORT_GET_PRIVATE (self);
  GIOStatus status;
  GError *error = NULL;

  status = g_io_channel_write_chars(priv->channel, 
                                     (gchar *)data, len, written, &error);
  switch (status) {
    case G_IO_STATUS_NORMAL:
    case G_IO_STATUS_AGAIN:
      break;
    case G_IO_STATUS_ERROR:
      salut_transport_mixin_emit_error(SALUT_TRANSPORT(self), error);
    case G_IO_STATUS_EOF:
      DEBUG("Writing chars failed, closing the transport");
      _do_disconnect(self);
      return FALSE;
      break;
  }

  return TRUE;
}

static void
_writeout(SalutLLTransport *self, const guint8 *data, gsize len) {
  SalutLLTransportPrivate *priv = SALUT_LL_TRANSPORT_GET_PRIVATE (self);
  gsize written = 0;

  DEBUG("OUT: %s", data);
  if (priv->output_buffer == NULL || priv->output_buffer->len == 0) {
    /* We've got nothing buffer yet so try to write out directly */
    if (!_try_write(self, data, len, &written)) {
      return;
    }
  }
  if (written == len) {
    return;
  }

  if (priv->output_buffer) {
    g_string_append_len(priv->output_buffer, (gchar *)data + written, len - written);
  } else {
    priv->output_buffer = g_string_new_len((gchar *)data + written, len - written);
  }
  if (!priv->watch_out) {
    priv->watch_out = 
      g_io_add_watch(priv->channel, G_IO_OUT, _channel_io_out, self);
  }
}

static gboolean
_channel_io_in(GIOChannel *source, GIOCondition condition, gpointer data) {
  SalutLLTransport *self = SALUT_LL_TRANSPORT (data);
  SalutLLTransportPrivate *priv = 
     SALUT_LL_TRANSPORT_GET_PRIVATE (self);
  guint8  buf[BUFSIZE + 1];
  GIOStatus status;
  GError *error = NULL;
  gsize read;

  status = g_io_channel_read_chars(priv->channel, (gchar *)buf, 
                                   BUFSIZE, &read, &error);
  switch (status) {
    case G_IO_STATUS_NORMAL:
      buf[read] = '\0';
      DEBUG("IN: %s", buf);
      salut_transport_received_data(SALUT_TRANSPORT(self), buf, read);
      break;
    case G_IO_STATUS_ERROR:
      salut_transport_mixin_emit_error(SALUT_TRANSPORT(self), error);
    case G_IO_STATUS_EOF:
      DEBUG("Failed to read from the transport, closing..");
      _do_disconnect(self);
      return FALSE;
    case G_IO_STATUS_AGAIN:
      break;
  }
  return TRUE;
}

static gboolean
_channel_io_out(GIOChannel *source, GIOCondition condition, gpointer data) {
  SalutLLTransport *self = SALUT_LL_TRANSPORT (data);
  SalutLLTransportPrivate *priv = 
     SALUT_LL_TRANSPORT_GET_PRIVATE (self);
  gsize written;
  
  g_assert(priv->output_buffer);
  if (!_try_write(self, (guint8 *)priv->output_buffer->str, 
                   priv->output_buffer->len, &written)) {
    return FALSE;
  }
  if (written > 0 ) {
    priv->output_buffer = g_string_erase(priv->output_buffer, 0, written);
  }
  if (priv->output_buffer->len == 0) {
    priv->watch_out = 0;
    return FALSE;
  }

  return TRUE;
}

static gboolean
_channel_io_err(GIOChannel *source, GIOCondition condition, gpointer data) {
  /* Either _HUP or _ERR */
  DEBUG("ERR");
  return TRUE;
}

static void
_setup_transport(SalutLLTransport *self, int fd) {
  /* We did make the tcp transport, so no setup everything */
  SalutLLTransportPrivate *priv = SALUT_LL_TRANSPORT_GET_PRIVATE (self);
  fcntl(fd, F_SETFL, O_NONBLOCK);

  priv->channel = g_io_channel_unix_new(fd);
  g_io_channel_set_close_on_unref(priv->channel, TRUE);
  g_io_channel_set_encoding(priv->channel, NULL, NULL);
  g_io_channel_set_buffered(priv->channel, FALSE);

  priv->watch_in = 
    g_io_add_watch(priv->channel, G_IO_IN, _channel_io_in, self);
  priv->watch_err = 
    g_io_add_watch(priv->channel, G_IO_ERR|G_IO_HUP, _channel_io_err, self);

  salut_transport_set_state(SALUT_TRANSPORT(self), SALUT_TRANSPORT_CONNECTED);
}


SalutLLTransport *
salut_ll_transport_new(void) {
  return g_object_new(SALUT_TYPE_LL_TRANSPORT, NULL);
}

void
salut_ll_transport_open_fd(SalutLLTransport *transport, int fd) {
  SalutLLTransportPrivate *priv = SALUT_LL_TRANSPORT_GET_PRIVATE (transport);

  priv->incoming = TRUE;
  priv->fd = fd;

  salut_transport_set_state(SALUT_TRANSPORT(transport), 
                            SALUT_TRANSPORT_CONNECTING);
  _setup_transport(transport, priv->fd);
}

gboolean
salut_ll_transport_open_sockaddr(SalutLLTransport *transport,
                                  struct sockaddr_storage *addr,
                                  GError **error) {
  SalutLLTransportPrivate *priv = SALUT_LL_TRANSPORT_GET_PRIVATE (transport);
  char host[NI_MAXHOST];
  char port[NI_MAXSERV];
  int fd;
  int ret;

  g_assert(!priv->incoming);

  salut_transport_set_state(SALUT_TRANSPORT(transport), 
                            SALUT_TRANSPORT_CONNECTING);
  if (getnameinfo((struct sockaddr *)addr, sizeof(struct sockaddr_storage),
      host, NI_MAXHOST, port, NI_MAXSERV, 
      NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
    DEBUG("Trying to connect to %s port %s", host, port);
  } else {
    DEBUG("Connecting..");
  }

  fd = socket(addr->ss_family, SOCK_STREAM, 0);
  if (fd < 0) {
    if (error != NULL) {
      /* FIXME, don't abuse TELEPATHY_ERRORS */
      *error = g_error_new(TELEPATHY_ERRORS, 
                           InvalidArgument,
                           "Getting socket failed: %s", strerror(errno));
    }
    DEBUG("Getting socket failed: %s", strerror(errno));
    goto failed;
  } 

  ret = connect(fd, (struct sockaddr *)addr, sizeof(struct sockaddr_storage));
  if (ret < 0) {
    if (error != NULL) {
      /* FIXME, don't abuse TELEPATHY_ERRORS */
      *error = g_error_new(TELEPATHY_ERRORS, 
                           NotAvailable,
                           "Connecting failed: %s", strerror(errno));
    }
    DEBUG("Connecting failed: %s", strerror(errno));
    goto failed;
  }

  _setup_transport(transport, fd);
  priv->fd = fd;
  return TRUE;

failed:
  salut_transport_set_state(SALUT_TRANSPORT(transport), 
                            SALUT_TRANSPORT_DISCONNECTED);
  if (fd >= 0) {
    close(fd);
  }
  return FALSE;
}

gboolean
salut_ll_transport_send(SalutTransport *transport,
                        const guint8 *data, gsize size,
                         GError **error) {
  _writeout(SALUT_LL_TRANSPORT(transport), data, size);    
  return TRUE;
}

gboolean
salut_ll_transport_is_incoming(SalutLLTransport *transport) {
  SalutLLTransportPrivate *priv = SALUT_LL_TRANSPORT_GET_PRIVATE (transport);
  return priv->incoming;
}

void
salut_ll_transport_set_incoming(SalutLLTransport *transport, 
                                 gboolean incoming) {
  SalutLLTransportPrivate *priv = SALUT_LL_TRANSPORT_GET_PRIVATE (transport);
  g_assert(SALUT_TRANSPORT(transport)->state == SALUT_TRANSPORT_DISCONNECTED);
  priv->incoming = incoming;
}

gboolean
salut_ll_transport_get_address(SalutLLTransport *transport, 
                                struct sockaddr_storage *addr,
                                socklen_t *len) {
  SalutLLTransportPrivate *priv = SALUT_LL_TRANSPORT_GET_PRIVATE (transport); 
  gboolean success = FALSE;
  struct sockaddr_in *s4 = (struct sockaddr_in*) addr;
  struct sockaddr_in6 *s6 = (struct sockaddr_in6*) addr;
  g_assert(priv->fd >= 0);
  g_assert(*len == sizeof(struct sockaddr_storage));

  success = (getpeername(priv->fd, (struct sockaddr *) addr, len) == 0);
  if (s6->sin6_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(&(s6->sin6_addr))) {
    /* Normalize to ipv4 address */
    s4->sin_family = AF_INET;
    s4->sin_addr.s_addr = s6->sin6_addr.s6_addr32[3];
  }
  return success;
}

void
salut_ll_transport_disconnect(SalutTransport *transport) {
  DEBUG("Connection close requested");
  _do_disconnect(SALUT_LL_TRANSPORT(transport));
}


