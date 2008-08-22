/*
 * salut-direct-bytestream-manager.c - Source for SalutDirectBytestreamManager
 * Copyright (C) 2007, 2008 Collabora Ltd.
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

#include "salut-direct-bytestream-manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <gibber/gibber-xmpp-stanza.h>
#include <gibber/gibber-namespaces.h>
#include <gibber/gibber-xmpp-error.h>
#include <gibber/gibber-iq-helper.h>
#include <gibber/gibber-bytestream-direct.h>
#include <gibber/gibber-util.h>

#include "salut-im-manager.h"
#include "salut-muc-manager.h"
#include "salut-tubes-manager.h"
#include "tube-iface.h"

#define DEBUG_FLAG DEBUG_DIRECT_BYTESTREAM_MGR
#include "debug.h"

G_DEFINE_TYPE (SalutDirectBytestreamManager, salut_direct_bytestream_manager,
    G_TYPE_OBJECT)

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

/* private structure */
typedef struct _SalutDirectBytestreamManagerPrivate SalutDirectBytestreamManagerPrivate;

struct _SalutDirectBytestreamManagerPrivate
{
  SalutConnection *connection;
  SalutImManager *im_manager;
  SalutXmppConnectionManager *xmpp_connection_manager;

  /* guint id -> guint listener_watch
   * When used by stream tubes, the id is the tube_id */
  GHashTable *listeners;

  gboolean dispose_has_run;
};

struct _listener_io_in_cb_data
{
  SalutDirectBytestreamManager *mgr;
  SalutContact *contact;
  SalutDirectBytestreamManagerNewConnectionFunc cb;
  gpointer user_data;
};

typedef struct _SalutDirectBytestreamManagerListener
    SalutDirectBytestreamManagerListener;
struct _SalutDirectBytestreamManagerListener
{
  gpointer id;
  GIOChannel *listen_io_channel;
  guint listen_io_channel_source_id;
  struct _listener_io_in_cb_data *data;
};

#define SALUT_DIRECT_BYTESTREAM_MANAGER_GET_PRIVATE(obj) \
    ((SalutDirectBytestreamManagerPrivate *) ((SalutDirectBytestreamManager *)obj)->priv)

static void
salut_direct_bytestream_manager_init (SalutDirectBytestreamManager *self)
{
  SalutDirectBytestreamManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_DIRECT_BYTESTREAM_MANAGER, SalutDirectBytestreamManagerPrivate);

  self->priv = priv;

  priv->dispose_has_run = FALSE;
}


void
salut_direct_bytestream_manager_dispose (GObject *object)
{
  SalutDirectBytestreamManager *self = SALUT_DIRECT_BYTESTREAM_MANAGER (object);
  SalutDirectBytestreamManagerPrivate *priv = SALUT_DIRECT_BYTESTREAM_MANAGER_GET_PRIVATE
      (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_object_unref (priv->im_manager);
  g_object_unref (priv->xmpp_connection_manager);

  if (G_OBJECT_CLASS (salut_direct_bytestream_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_direct_bytestream_manager_parent_class)->dispose (object);
}

void
salut_direct_bytestream_manager_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (salut_direct_bytestream_manager_parent_class)->finalize)
    G_OBJECT_CLASS (salut_direct_bytestream_manager_parent_class)->finalize
        (object);
}

static void
salut_direct_bytestream_manager_get_property (GObject *object,
                                          guint property_id,
                                          GValue *value,
                                          GParamSpec *pspec)
{
  SalutDirectBytestreamManager *self = SALUT_DIRECT_BYTESTREAM_MANAGER (object);
  SalutDirectBytestreamManagerPrivate *priv = SALUT_DIRECT_BYTESTREAM_MANAGER_GET_PRIVATE (
      self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->connection);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_direct_bytestream_manager_set_property (GObject *object,
                                          guint property_id,
                                          const GValue *value,
                                          GParamSpec *pspec)
{
  SalutDirectBytestreamManager *self = SALUT_DIRECT_BYTESTREAM_MANAGER (object);
  SalutDirectBytestreamManagerPrivate *priv = SALUT_DIRECT_BYTESTREAM_MANAGER_GET_PRIVATE (
      self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->connection = g_value_get_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
salut_direct_bytestream_manager_constructor (GType type,
                                      guint n_props,
                                      GObjectConstructParam *props)
{
  GObject *obj;
  SalutDirectBytestreamManager *self;
  SalutDirectBytestreamManagerPrivate *priv;

  obj = G_OBJECT_CLASS (salut_direct_bytestream_manager_parent_class)->
           constructor (type, n_props, props);

  self = SALUT_DIRECT_BYTESTREAM_MANAGER (obj);
  priv = SALUT_DIRECT_BYTESTREAM_MANAGER_GET_PRIVATE (self);

  g_assert (priv->connection != NULL);
  g_object_get (priv->connection,
      "im-manager", &(priv->im_manager),
      "xmpp-connection-manager", &(priv->xmpp_connection_manager),
      NULL);
  g_assert (priv->im_manager != NULL);
  g_assert (priv->xmpp_connection_manager != NULL);

  priv->listeners = g_hash_table_new (NULL, NULL);

  return obj;
}

static void
salut_direct_bytestream_manager_class_init (
    SalutDirectBytestreamManagerClass *salut_direct_bytestream_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS
      (salut_direct_bytestream_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_direct_bytestream_manager_class,
      sizeof (SalutDirectBytestreamManagerPrivate));

  object_class->constructor = salut_direct_bytestream_manager_constructor;
  object_class->dispose = salut_direct_bytestream_manager_dispose;
  object_class->finalize = salut_direct_bytestream_manager_finalize;

  object_class->get_property = salut_direct_bytestream_manager_get_property;
  object_class->set_property = salut_direct_bytestream_manager_set_property;

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "Salut Connection that owns the connection for this bytestream channel",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);
}

SalutDirectBytestreamManager *
salut_direct_bytestream_manager_new (SalutConnection *conn)
{
  g_return_val_if_fail (SALUT_IS_CONNECTION (conn), NULL);

  return g_object_new (
      SALUT_TYPE_DIRECT_BYTESTREAM_MANAGER,
      "connection", conn,
      NULL);
}

/* callback when receiving a connection from the remote CM */
static gboolean
listener_io_in_cb (GIOChannel *source,
                   GIOCondition condition,
                   gpointer user_data)
{
  struct _listener_io_in_cb_data *data = user_data;
  SalutDirectBytestreamManagerPrivate *priv;
  GibberBytestreamIface *bytestream;
  int listen_fd;

  listen_fd = g_io_channel_unix_get_fd (source);

  priv = SALUT_DIRECT_BYTESTREAM_MANAGER_GET_PRIVATE (data->mgr);

  bytestream = g_object_new (GIBBER_TYPE_BYTESTREAM_DIRECT,
      "state", GIBBER_BYTESTREAM_STATE_LOCAL_PENDING,
      "self-id", priv->connection->name,
      "peer-id", data->contact->name,
      NULL);

  /* call this before accepting the socket, in order to let the opportunity to
   * register a callback for the "new-connection" bytestream's signal. */
  data->cb (bytestream, data->user_data);

  gibber_bytestream_direct_accept_socket (bytestream, listen_fd);

  return FALSE;
}


/**
 * salut_direct_bytestream_manager_listen:
 * @self: the direct bytestream manager
 * @contact: the contact allowed to connect
 * @new_connection_cb: callback when a new connection comes
 * @id: opaquer pointer given to the callback
 *
 * Listen on a random TCP port for incoming connections. Only connections from
 * the contact given as parameter will be accepted. A GibberBystreamDirect
 * object is created for each successful connection. A callback is called
 * each time a new GibberBystreamDirect object is created before it actually
 * connects to let the opportunity to the caller to connect on a bytestream
 * signal.
 *
 * Returns: TCP port number
 */
int
salut_direct_bytestream_manager_listen (SalutDirectBytestreamManager *self,
    SalutContact *contact,
    SalutDirectBytestreamManagerNewConnectionFunc new_connection_cb,
    gpointer id)
{
  SalutDirectBytestreamManagerPrivate *priv;
  priv = SALUT_DIRECT_BYTESTREAM_MANAGER_GET_PRIVATE (self);
  struct _listener_io_in_cb_data *data;
  SalutDirectBytestreamManagerListener *listener;
  int port;
  int fd = -1, ret, yes = 1;
  struct addrinfo req, *ans = NULL;
  struct sockaddr *addr;
  struct sockaddr_in addr4;
  struct sockaddr_in6 addr6;
  socklen_t len;
  #define BACKLOG 1

  memset (&req, 0, sizeof (req));
  req.ai_flags = AI_PASSIVE;
  req.ai_family = AF_UNSPEC;
  req.ai_socktype = SOCK_STREAM;
  req.ai_protocol = IPPROTO_TCP;

  ret = getaddrinfo (NULL, "0", &req, &ans);
  if (ret != 0)
    {
      DEBUG ("getaddrinfo failed: %s", gai_strerror (ret));
      goto error;
    }

  fd = socket (ans->ai_family, ans->ai_socktype, ans->ai_protocol);
  if (fd == -1)
    {
      DEBUG ("socket failed: %s", g_strerror (errno));
      goto error;
    }

  ret = setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (int));
  if (ret == -1)
    {
      DEBUG ("setsockopt failed: %s", g_strerror (errno));
      goto error;
    }

  ret = bind (fd, ans->ai_addr, ans->ai_addrlen);
  if (ret  < 0)
    {
      DEBUG ("bind failed: %s", g_strerror (errno));
      goto error;
    }

  if (ans->ai_family == AF_INET)
    {
      len = sizeof (struct sockaddr_in);
      addr = (struct sockaddr *) &addr4;
    }
  else
    {
      len = sizeof (struct sockaddr_in6);
      addr = (struct sockaddr *) &addr6;
    }

  if (getsockname (fd, addr, &len) == -1)
  {
    DEBUG ("getsockname failed: %s", g_strerror (errno));
    goto error;
  }

  if (ans->ai_family == AF_INET)
    {
      port = ntohs (addr4.sin_port);
    }
  else
    {
      port = ntohs (addr6.sin6_port);
    }

  ret = listen (fd, BACKLOG);
  if (ret == -1)
    {
      DEBUG ("listen failed: %s", g_strerror (errno));
      goto error;
    }

  DEBUG ("listen on port %d", port);

  data = g_slice_new0 (struct _listener_io_in_cb_data);
  data->mgr = self;
  data->contact = contact,
  data->cb = new_connection_cb;
  data->user_data = id;

  listener = g_slice_new0 (SalutDirectBytestreamManagerListener);
  listener->id = id;
  listener->data = data;

  listener->listen_io_channel = g_io_channel_unix_new (fd);
  g_io_channel_set_close_on_unref (listener->listen_io_channel, TRUE);
  listener->listen_io_channel_source_id = g_io_add_watch
      (listener->listen_io_channel, G_IO_IN, listener_io_in_cb, data);

  g_hash_table_insert (priv->listeners, id, listener);

  freeaddrinfo (ans);
  return port;

error:
  if (fd > 0)
    close (fd);

  if (ans != NULL)
    freeaddrinfo (ans);
  return -1;
}

/**
 * salut_direct_bytestream_manager_stop_listen:
 * @self: the direct bytestream manager
 * @id: opaquer pointer
 *
 * Stop to listen on a TCP port after a call to
 * salut_direct_bytestream_manager_listen.
 */
void salut_direct_bytestream_manager_stop_listen (
    SalutDirectBytestreamManager *self, gpointer id)
{
  SalutDirectBytestreamManagerPrivate *priv;
  SalutDirectBytestreamManagerListener *listener;

  priv = SALUT_DIRECT_BYTESTREAM_MANAGER_GET_PRIVATE (self);

  listener = g_hash_table_lookup (priv->listeners, id);
  g_assert (listener != NULL);

  if (listener->listen_io_channel_source_id != 0)
    {
      GSource* source = g_main_context_find_source_by_id (NULL,
          listener->listen_io_channel_source_id);

      /* FIXME: why source is already destroyed here!? */
      if (source != NULL)
        {
          g_source_destroy (source);
        }
      listener->listen_io_channel_source_id = 0;
    }
  
  g_slice_free (struct _listener_io_in_cb_data, listener->data);
  g_slice_free (SalutDirectBytestreamManagerListener, listener);
}

GibberBytestreamIface *
salut_direct_bytestream_manager_new_stream (SalutDirectBytestreamManager *self,
                                            SalutContact *contact,
                                            int portnum)
{
  GibberBytestreamIface *bytestream;
  SalutDirectBytestreamManagerPrivate *priv;

  priv = SALUT_DIRECT_BYTESTREAM_MANAGER_GET_PRIVATE (self);

  bytestream = g_object_new (GIBBER_TYPE_BYTESTREAM_DIRECT,
      "addresses", salut_contact_get_addresses (contact),
      "state", GIBBER_BYTESTREAM_STATE_LOCAL_PENDING,
      "peer-id", contact->name,
      "port", portnum,
      NULL);

  g_assert (bytestream != NULL);

  return bytestream;
}

