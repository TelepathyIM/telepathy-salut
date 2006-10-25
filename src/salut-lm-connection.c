/*
 * salut-lm-connection.c - Source for SalutLmConnection
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

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "lm-parser.h"

#include "telepathy-errors.h"
#include "salut-lm-connection.h"
#include "salut-lm-connection-signals-marshal.h"

#define XML_STREAM_INIT "<?xml version='1.0' encoding='UTF-8'?>" \
                        "<streamm:stream xmlns='jabber:client' "\
                        "xmlns:stream='http://etherx.jabber.org/streams'>"

#define DEBUG_FLAG DEBUG_NET
#include <debug.h>

#define BUFSIZE 1024

static gboolean
_channel_io_out(GIOChannel *source, GIOCondition condition, gpointer data);

static void _do_disconnect(SalutLmConnection *self);

G_DEFINE_TYPE(SalutLmConnection, salut_lm_connection, G_TYPE_OBJECT)

/* signal enum */
enum
{
  STATE_CHANGED,
  MESSAGE_RECEIVED,
  MESSAGE_SENT,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _SalutLmConnectionPrivate SalutLmConnectionPrivate;

struct _SalutLmConnectionPrivate
{
  GIOChannel *channel;
  LmParser *parser;
  gboolean incoming;
  gboolean dispose_has_run;
  guint watch_in;
  guint watch_out;
  guint watch_err;
  int fd;
  GString *output_buffer;
};

#define SALUT_LM_CONNECTION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_LM_CONNECTION, SalutLmConnectionPrivate))

static void
salut_lm_connection_init (SalutLmConnection *self)
{
  SalutLmConnectionPrivate *priv = SALUT_LM_CONNECTION_GET_PRIVATE (self);
  /* allocate any data required by the object here */
  self->state = SALUT_LM_DISCONNECTED;
  priv->incoming = FALSE;
  priv->channel = NULL;
  priv->output_buffer = NULL;
  priv->watch_in = 0;
  priv->watch_out = 0;
  priv->watch_err = 0;
  priv->fd = -1;
}

static void salut_lm_connection_dispose (GObject *object);
static void salut_lm_connection_finalize (GObject *object);

static void
salut_lm_connection_class_init (SalutLmConnectionClass *salut_lm_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_lm_connection_class);

  g_type_class_add_private (salut_lm_connection_class, sizeof (SalutLmConnectionPrivate));

  object_class->dispose = salut_lm_connection_dispose;
  object_class->finalize = salut_lm_connection_finalize;

  signals[STATE_CHANGED] =
    g_signal_new ("state_changed",
                  G_OBJECT_CLASS_TYPE (salut_lm_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_lm_connection_marshal_VOID__INT,
                  G_TYPE_NONE, 1,
                  G_TYPE_INT);

  signals[MESSAGE_RECEIVED] =
    g_signal_new ("message_received",
                  G_OBJECT_CLASS_TYPE (salut_lm_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_lm_connection_marshal_VOID__POINTER,
                  G_TYPE_NONE, 1,
                  G_TYPE_POINTER);

  signals[MESSAGE_SENT] =
    g_signal_new ("message_sent",
                  G_OBJECT_CLASS_TYPE (salut_lm_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_lm_connection_marshal_VOID__POINTER,
                  G_TYPE_NONE, 1,
                  G_TYPE_POINTER);
}

void
salut_lm_connection_dispose (GObject *object)
{
  SalutLmConnection *self = SALUT_LM_CONNECTION (object);
  SalutLmConnectionPrivate *priv = SALUT_LM_CONNECTION_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  _do_disconnect(self);

  if (G_OBJECT_CLASS (salut_lm_connection_parent_class)->dispose)
    G_OBJECT_CLASS (salut_lm_connection_parent_class)->dispose (object);
}

void
salut_lm_connection_finalize (GObject *object)
{

  G_OBJECT_CLASS (salut_lm_connection_parent_class)->finalize (object);
}

static void
_do_disconnect(SalutLmConnection *self) {
  SalutLmConnectionPrivate *priv = SALUT_LM_CONNECTION_GET_PRIVATE (self);

  priv->fd = -1;
  if (priv->channel != NULL) {
    g_source_remove(priv->watch_in);
    if (priv->watch_out) 
      g_source_remove(priv->watch_out);
    g_source_remove(priv->watch_err);
    g_io_channel_shutdown(priv->channel, FALSE, NULL);
    g_io_channel_unref(priv->channel);
    priv->channel = NULL;
  }

  if (priv->parser) {
    lm_parser_free(priv->parser);
    priv->parser = NULL;
  }

  if (priv->output_buffer) {
    g_string_free(priv->output_buffer, TRUE);
    priv->output_buffer = NULL;
  }

  self->state = SALUT_LM_DISCONNECTED;
  g_signal_emit(self, signals[STATE_CHANGED], 
                g_quark_from_static_string("disconnected"),
                self->state);
}

static gboolean
_try_write(SalutLmConnection *self, gchar *data, int len, gsize *written) {
  SalutLmConnectionPrivate *priv = SALUT_LM_CONNECTION_GET_PRIVATE (self);
  GIOStatus status;

  status = g_io_channel_write_chars(priv->channel, data, len, written, NULL);
  switch (status) {
    case G_IO_STATUS_NORMAL:
    case G_IO_STATUS_AGAIN:
      break;
    case G_IO_STATUS_ERROR:
    case G_IO_STATUS_EOF:
      DEBUG("Writing chars failed, closing the connection");
      _do_disconnect(self);
      return FALSE;
      break;
  }

  return TRUE;
}

static void
_writeout(SalutLmConnection *self, gchar *data, gsize len) {
  SalutLmConnectionPrivate *priv = SALUT_LM_CONNECTION_GET_PRIVATE (self);
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
    g_string_append_len(priv->output_buffer, data + written, len - written);
  } else {
    priv->output_buffer = g_string_new_len(data + written, len - written);
  }
  if (!priv->watch_out) {
    priv->watch_out = 
      g_io_add_watch(priv->channel, G_IO_OUT, _channel_io_out, self);
  }
}

void
_send_stream_init(SalutLmConnection *self) {
  _writeout(self, XML_STREAM_INIT, strlen(XML_STREAM_INIT));
}

void
_message_parsed(LmParser *parser, LmMessage *message, gpointer data) {
  SalutLmConnection *self = SALUT_LM_CONNECTION_GET_PRIVATE (data);
  SalutLmConnectionPrivate *priv = 
     SALUT_LM_CONNECTION_GET_PRIVATE (connection);

   if (lm_message_get_type(message) == LM_MESSAGE_TYPE_STREAM) {
     if (self->state != SALUT_LM_CONNECTING) {
       _do_disconnect(self);
       return;
     }
     if (priv->incoming) {
       _send_stream_init(SalutLmConnection *self);
     }
     priv->state = SALUT_LM_CONNECTED;
     g_signal_emit(self, signals[STATE_CHANGED], 
                g_quark_from_static_string("connected"),
                self->state);
   }
}

static gboolean
_channel_io_in(GIOChannel *source, GIOCondition condition, gpointer data) {
  SalutLmConnection *self = SALUT_LM_CONNECTION (data);
  SalutLmConnectionPrivate *priv = 
     SALUT_LM_CONNECTION_GET_PRIVATE (self);
  gchar buf[BUFSIZE + 1];
  GIOStatus status;
  GError *error = NULL;
  gsize read;

  status = g_io_channel_read_chars(priv->channel, buf, BUFSIZE, &read, &error);
  switch (status) {
    case G_IO_STATUS_NORMAL:
      buf[read] = '\0';
      DEBUG("IN: %s", buf);
      lm_parser_parse(priv->parser, buf);
      break;
    case G_IO_STATUS_ERROR:
    case G_IO_STATUS_EOF:
      /* FIXME disconnection and tear down the connection */
      DEBUG("Failed to read from the connection, closing..");
      _do_disconnect(self);
      return FALSE;
    case G_IO_STATUS_AGAIN:
      break;
  }
  return TRUE;
}

static gboolean
_channel_io_out(GIOChannel *source, GIOCondition condition, gpointer data) {
  SalutLmConnection *self = SALUT_LM_CONNECTION (data);
  SalutLmConnectionPrivate *priv = 
     SALUT_LM_CONNECTION_GET_PRIVATE (self);
  gsize written;
  
  g_assert(priv->output_buffer);
  if (!_try_write(self, priv->output_buffer->str, 
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
_setup_connection(SalutLmConnection *self, int fd) {
  /* We did make the tcp connection, so no setup everything */
  SalutLmConnectionPrivate *priv = SALUT_LM_CONNECTION_GET_PRIVATE (self);
  fcntl(fd, F_SETFL, O_NONBLOCK);

  priv->channel = g_io_channel_unix_new(fd);
  g_io_channel_set_close_on_unref(priv->channel, TRUE);
  g_io_channel_set_encoding(priv->channel, NULL, NULL);
  g_io_channel_set_buffered(priv->channel, FALSE);

  priv->watch_in = 
    g_io_add_watch(priv->channel, G_IO_IN, _channel_io_in, self);
  priv->watch_err = 
    g_io_add_watch(priv->channel, G_IO_ERR|G_IO_HUP, _channel_io_err, self);

  priv->parser = lm_parser_new(_message_parsed, self, NULL);

  if (!priv->incoming) {
    _send_stream_init(self);
  }
}


SalutLmConnection *
salut_lm_connection_new(void) {
  return g_object_new(SALUT_TYPE_LM_CONNECTION, NULL);
}

SalutLmConnection *
salut_lm_connection_new_from_fd(int fd) {
  SalutLmConnection *self = salut_lm_connection_new();
  SalutLmConnectionPrivate *priv = SALUT_LM_CONNECTION_GET_PRIVATE (self);

  priv->incoming = TRUE;
  priv->fd = fd;

  return self;
}

gboolean
salut_lm_connection_open_sockaddr(SalutLmConnection *connection,
                                  struct sockaddr_storage *addr,
                                  GError **error) {
  SalutLmConnectionPrivate *priv = SALUT_LM_CONNECTION_GET_PRIVATE (connection);
  char host[NI_MAXHOST];
  char port[NI_MAXSERV];
  int fd;
  int ret;

  g_assert(!priv->incoming);

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

  connection->state = SALUT_LM_CONNECTING;
  _setup_connection(connection, fd);
  priv->fd = fd;

  g_signal_emit(connection, signals[STATE_CHANGED], 
                g_quark_from_static_string("connecting"),
                connection->state);

  return TRUE;

failed:
  connection->state = SALUT_LM_DISCONNECTED;
  if (fd >= 0) {
    close(fd);
  }
  return FALSE;
}

gboolean
salut_lm_connection_send(SalutLmConnection *connection,
                         LmMessage *message,
                         GError **error) {
  DEBUG("Sending message");
  return TRUE;
}

void
salut_lm_connection_set_incoming(SalutLmConnection *connection, 
                                 gboolean incoming) {
  SalutLmConnectionPrivate *priv = SALUT_LM_CONNECTION_GET_PRIVATE (connection);
  g_assert(connection->state == SALUT_LM_DISCONNECTED);
  priv->incoming = incoming;
}

void
salut_lm_connection_fd_start(SalutLmConnection *connection) {
   SalutLmConnectionPrivate *priv = 
     SALUT_LM_CONNECTION_GET_PRIVATE (connection);

  g_assert(priv->fd >= 0);
  g_assert(priv->channel == NULL);

  _setup_connection(connection, priv->fd);
}

gboolean
salut_lm_connection_get_address(SalutLmConnection *connection, 
                                struct sockaddr_storage *addr,
                                socklen_t *len) {
  SalutLmConnectionPrivate *priv = SALUT_LM_CONNECTION_GET_PRIVATE (connection); 
  g_assert(priv->fd >= 0);
  g_assert(*len == sizeof(struct sockaddr_storage));

  return (getpeername(priv->fd, (struct sockaddr *) addr, len) == 0);
}

void
salut_lm_connection_close(SalutLmConnection *connection) {
  DEBUG("Connection close requested");
  _do_disconnect(connection);
}


