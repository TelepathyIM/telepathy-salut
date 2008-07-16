/*
 * salut-file-channel.c - Source for SalutFtChannel
 * Copyright (C) 2007 Marco Barisione <marco@barisione.org>
 * Copyright (C) 2005, 2007 Collabora Ltd.
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

#include <glib/gstdio.h>
#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#define DEBUG_FLAG DEBUG_FT
#include "debug.h"

#include "salut-file-channel.h"
#include "signals-marshal.h"

#include "salut-connection.h"
#include "salut-im-manager.h"
#include "salut-contact.h"

#include <gibber/gibber-xmpp-stanza.h>
#include <gibber/gibber-file-transfer.h>
#include <gibber/gibber-oob-file-transfer.h>

#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/dbus.h>

static void
channel_iface_init (gpointer g_iface, gpointer iface_data);
static void
file_transfer_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (SalutFtChannel, salut_ft_channel, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL);
    G_IMPLEMENT_INTERFACE (SALUT_TYPE_SVC_CHANNEL_TYPE_FILE,
                           file_transfer_iface_init);
);

/* signal enum */
/*
enum
{
    RECEIVED_STANZA,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};
*/

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_CONTACT,
  PROP_CONNECTION,
  PROP_XMPP_CONNECTION_MANAGER,
  LAST_PROPERTY
};

/* private structure */
struct _SalutFtChannelPrivate {
  gboolean dispose_has_run;
  gboolean closed;
  gchar *object_path;
  TpHandle handle;
  SalutContact *contact;
  SalutConnection *connection;
  SalutXmppConnectionManager *xmpp_connection_manager;
  GibberXmppConnection *xmpp_connection;
  /* hash table used to convert from string id to numerical id */
  GHashTable *name_to_id;
};

static void
salut_ft_channel_do_close (SalutFtChannel *self)
{
  if (self->priv->closed)
    return;

  DEBUG ("Emitting closed signal for %s", self->priv->object_path);
  tp_svc_channel_emit_closed (self);
  self->priv->closed = TRUE;
}

static void
salut_ft_channel_init (SalutFtChannel *obj)
{
  obj->priv = G_TYPE_INSTANCE_GET_PRIVATE (obj, SALUT_TYPE_FT_CHANNEL,
      SalutFtChannelPrivate);

  /* allocate any data required by the object here */
  obj->priv->object_path = NULL;
  obj->priv->connection = NULL;
  obj->priv->xmpp_connection_manager = NULL;
  obj->priv->contact = NULL;
}

static void
salut_ft_channel_get_property (GObject    *object,
                               guint       property_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  SalutFtChannel *self = SALUT_FT_CHANNEL (object);

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_value_set_string (value, self->priv->object_path);
        break;
      case PROP_CHANNEL_TYPE:
        g_value_set_static_string (value, SALUT_IFACE_CHANNEL_TYPE_FILE);
        break;
      case PROP_HANDLE_TYPE:
        g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
        break;
      case PROP_HANDLE:
        g_value_set_uint (value, self->priv->handle);
        break;
      case PROP_CONTACT:
        g_value_set_object (value, self->priv->contact);
        break;
      case PROP_CONNECTION:
        g_value_set_object (value, self->priv->connection);
        break;
      case PROP_XMPP_CONNECTION_MANAGER:
        g_value_set_object (value, self->priv->xmpp_connection_manager);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_ft_channel_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  SalutFtChannel *self = SALUT_FT_CHANNEL (object);
  const gchar *tmp;

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_free (self->priv->object_path);
        self->priv->object_path = g_value_dup_string (value);
        break;
      case PROP_HANDLE:
        self->priv->handle = g_value_get_uint (value);
        break;
      case PROP_CONTACT:
        self->priv->contact = g_value_get_object (value);
        g_object_ref (self->priv->contact);
        break;
      case PROP_CONNECTION:
        self->priv->connection = g_value_get_object (value);
        break;
      case PROP_HANDLE_TYPE:
        g_assert (g_value_get_uint (value) == 0
                  || g_value_get_uint (value) == TP_HANDLE_TYPE_CONTACT);
        break;
      case PROP_CHANNEL_TYPE:
        tmp = g_value_get_string (value);
        g_assert (tmp == NULL
                  || !tp_strdiff (g_value_get_string (value),
                         SALUT_IFACE_CHANNEL_TYPE_FILE));
        break;
      case PROP_XMPP_CONNECTION_MANAGER:
        self->priv->xmpp_connection_manager = g_value_get_object (value);
        g_object_ref (self->priv->xmpp_connection_manager);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
salut_ft_channel_constructor (GType type, guint n_props,
                              GObjectConstructParam *props)
{
  GObject *obj;
  SalutFtChannel *self;
  DBusGConnection *bus;
  TpBaseConnection *base_conn;
  TpHandleRepoIface *contact_repo;

  /* Parent constructor chain */
  obj = G_OBJECT_CLASS (salut_ft_channel_parent_class)->
          constructor (type, n_props, props);

  self = SALUT_FT_CHANNEL (obj);

  /* Ref our handle */
  base_conn = TP_BASE_CONNECTION (self->priv->connection);

  contact_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);

  tp_handle_ref (contact_repo, self->priv->handle);

  /* Initialize file transfer mixin */
  tp_file_transfer_mixin_init (obj,
      G_STRUCT_OFFSET (SalutFtChannel, file_transfer), contact_repo);

  /* Initialize the hash table used to convert from the id name
   * to the numerical id. */
  self->priv->name_to_id = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, NULL);

  /* Connect to the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, self->priv->object_path, obj);

  return obj;
}

static void
salut_ft_channel_dispose (GObject *object);
static void
salut_ft_channel_finalize (GObject *object);

static void
salut_ft_channel_class_init (SalutFtChannelClass *salut_ft_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_ft_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_ft_channel_class,
      sizeof (SalutFtChannelPrivate));

  object_class->dispose = salut_ft_channel_dispose;
  object_class->finalize = salut_ft_channel_finalize;

  object_class->constructor = salut_ft_channel_constructor;
  object_class->get_property = salut_ft_channel_get_property;
  object_class->set_property = salut_ft_channel_set_property;

  g_object_class_override_property (object_class, PROP_OBJECT_PATH,
                                    "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
                                    "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
                                    "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");

  param_spec = g_param_spec_object ("contact",
                                    "SalutContact object",
                                    "Salut Contact to which this channel"
                                    "is dedicated",
                                    SALUT_TYPE_CONTACT,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONTACT, param_spec);

  param_spec = g_param_spec_object ("connection",
                                    "SalutConnection object",
                                    "Salut Connection that owns the"
                                    "connection for this IM channel",
                                    SALUT_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class,
                                   PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_object (
      "xmpp-connection-manager",
      "SalutXmppConnectionManager object",
      "Salut XMPP Connection manager used for this FT channel",
      SALUT_TYPE_XMPP_CONNECTION_MANAGER,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_XMPP_CONNECTION_MANAGER,
      param_spec);

  tp_file_transfer_mixin_class_init (object_class,
                                     G_STRUCT_OFFSET (SalutFtChannelClass,
                                                      file_transfer_class));
}

void
salut_ft_channel_dispose (GObject *object)
{
  SalutFtChannel *self = SALUT_FT_CHANNEL (object);
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (self->priv->connection);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);

  if (self->priv->dispose_has_run)
    return;

  self->priv->dispose_has_run = TRUE;

  tp_handle_unref (handle_repo, self->priv->handle);

  g_hash_table_unref (self->priv->name_to_id);

  salut_ft_channel_do_close (self);

  if (self->priv->contact)
    {
      g_object_unref (self->priv->contact);
      self->priv->contact = NULL;
    }

  if (self->priv->xmpp_connection_manager != NULL)
    {
      g_object_unref (self->priv->xmpp_connection_manager);
      self->priv->xmpp_connection_manager = NULL;
    }

  if (self->priv->xmpp_connection != NULL)
    {
      g_object_unref (self->priv->xmpp_connection);
      self->priv->xmpp_connection = NULL;
    }

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_ft_channel_parent_class)->dispose)
    G_OBJECT_CLASS (salut_ft_channel_parent_class)->dispose (object);
}

static void
salut_ft_channel_finalize (GObject *object)
{
  SalutFtChannel *self = SALUT_FT_CHANNEL (object);

  /* free any data held directly by the object here */
  g_free (self->priv->object_path);

  tp_file_transfer_mixin_finalize (G_OBJECT (self));

  G_OBJECT_CLASS (salut_ft_channel_parent_class)->finalize (object);
}


/**
 * salut_ft_channel_close
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_ft_channel_close (TpSvcChannel *iface,
                        DBusGMethodInvocation *context)
{
  salut_ft_channel_do_close (SALUT_FT_CHANNEL (iface));
  tp_svc_channel_return_from_close (context);
}

/**
 * salut_ft_channel_get_channel_type
 *
 * Implements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_ft_channel_get_channel_type (TpSvcChannel *iface,
                                   DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      SALUT_IFACE_CHANNEL_TYPE_FILE);
}

/**
 * salut_ft_channel_get_handle
 *
 * Implements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_ft_channel_get_handle (TpSvcChannel *iface,
                             DBusGMethodInvocation *context)
{
  SalutFtChannel *self = SALUT_FT_CHANNEL (iface);

  tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_CONTACT,
                                         self->priv->handle);
}

/**
 * salut_ft_channel_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_ft_channel_get_interfaces (TpSvcChannel *iface,
                                 DBusGMethodInvocation *context)
{
  const char *interfaces[] = { NULL };

  tp_svc_channel_return_from_get_interfaces (context, interfaces);
}

static void
channel_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, salut_ft_channel_##x)
  IMPLEMENT (close);
  IMPLEMENT (get_channel_type);
  IMPLEMENT (get_handle);
  IMPLEMENT (get_interfaces);
#undef IMPLEMENT
}

static GibberFileTransfer *
get_file_transfer (SalutFtChannel *self,
                   guint id,
                   GError **error)
{
  GibberFileTransfer *ft;

  ft = tp_file_transfer_mixin_get_user_data (G_OBJECT (self), id);
  if (ft != NULL)
    {
      return ft;
    }
  else
    {
      DEBUG ("Invalid transfer id %u", id);
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Invalid transfer id %u", id);
      return NULL;
    }
}

static void
error_cb (GibberFileTransfer *ft,
          guint domain,
          gint code,
          const gchar *message,
          SalutFtChannel *self)
{
}

static void
ft_finished_cb (GibberFileTransfer *ft,
                SalutFtChannel *self)
{
}

static void
remote_accepted_cb (GibberFileTransfer *ft,
                    SalutFtChannel *self)
{
  guint id;

  id = GPOINTER_TO_INT (g_hash_table_lookup (self->priv->name_to_id, ft->id));
  tp_file_transfer_mixin_set_state (G_OBJECT (self), id,
    TP_FILE_TRANSFER_STATE_OPEN, NULL);

  g_signal_connect (ft, "finished", G_CALLBACK (ft_finished_cb), self);
}

static gboolean
setup_local_socket (SalutFtChannel *self, guint id);

static void
send_file_offer (SalutFtChannel *self,
                 guint id)
{
  GValue *val;
  GValueArray *val_array;
  GibberFileTransfer *ft;
  const gchar *filename;
  GHashTable *information;

  /* retrieve the file name and the additional information */
  if (!tp_file_transfer_mixin_get_file_transfer (G_OBJECT (self), id,
        &val, NULL))
    {
      DEBUG ("Invalid transfer id %u", id);
      return;
    }
  val_array = g_value_get_boxed (val);
  g_free (val);
  filename = g_value_get_string (g_value_array_get_nth (val_array, 4));
  information = g_value_get_boxed (g_value_array_get_nth (val_array, 5));

  ft = g_object_new (GIBBER_TYPE_OOB_FILE_TRANSFER,
      "self-jid", self->priv->connection->name,
      "peer-jid", self->priv->contact->name,
      "filename", filename,
      "connection", self->priv->xmpp_connection,
      NULL);
  g_signal_connect (ft, "remote-accepted",
      G_CALLBACK (remote_accepted_cb), self);
  g_signal_connect (ft, "error", G_CALLBACK (error_cb), self);

  g_hash_table_insert (self->priv->name_to_id, (gchar *) ft->id,
      GINT_TO_POINTER (id));
  tp_file_transfer_mixin_set_user_data (G_OBJECT (self), id, ft);

  setup_local_socket (self, id);

  val = g_hash_table_lookup (information, "size");
  if (val != NULL)
    ft->size = g_value_get_uint64 (val);

  gibber_file_transfer_offer (ft);
}

/* passed as user_data to the callbacl for the "new-connection" signal
 * emitted by the SalutXmppConnectionManager */
typedef struct {
  SalutFtChannel *self;
  guint id;
} NewConnectionData;

static void
xmpp_connection_manager_new_connection_cb (SalutXmppConnectionManager *mgr,
                                           GibberXmppConnection *connection,
                                           SalutContact *contact,
                                           gpointer user_data)
{
  NewConnectionData *data = user_data;

  data->self->priv->xmpp_connection = g_object_ref (connection);
  g_signal_handlers_disconnect_by_func (mgr,
      xmpp_connection_manager_new_connection_cb, user_data);
  send_file_offer (data->self, data->id);
  g_free (data);
}

static void
value_free (GValue *value)
{
  if (!value)
    return;
  g_value_unset (value);
  g_free (value);
}

void
salut_ft_channel_received_file_offer (SalutFtChannel *self,
                                      GibberXmppStanza *stanza,
                                      GibberXmppConnection *conn)
{
  GibberFileTransfer *ft;
  GHashTable *information;
  guint id;

  ft = gibber_file_transfer_new_from_stanza (stanza, conn);
  g_signal_connect (ft, "error", G_CALLBACK (error_cb), self);

  DEBUG ("Received file offer with id '%s'", ft->id);

  information = g_hash_table_new_full (g_str_hash, g_str_equal,
      (GDestroyNotify) g_free, (GDestroyNotify) value_free);
  GValue *val = g_new0 (GValue, 1);
  g_value_init (val, G_TYPE_UINT64);
  g_value_set_uint64 (val, ft->size);
  g_hash_table_insert (information, g_strdup ("size"), val);

  id = tp_file_transfer_mixin_add_transfer (G_OBJECT (self),
      self->priv->handle, TP_FILE_TRANSFER_DIRECTION_INCOMING,
      TP_FILE_TRANSFER_STATE_LOCAL_PENDING, ft->filename,
      information, ft);

  g_hash_table_insert (self->priv->name_to_id, (gchar *) ft->id,
      GINT_TO_POINTER (id));
}

/**
 * salut_ft_channel_accept_file
 *
 * Implements D-Bus method AcceptFile
 * on interface org.freedesktop.Telepathy.Channel.Type.File
 */
static void
salut_ft_channel_accept_file (SalutSvcChannelTypeFile *iface,
                              guint id,
                              DBusGMethodInvocation *context)
{
  SalutFtChannel *self = SALUT_FT_CHANNEL (iface);
  GibberFileTransfer *ft;
  GError *error = NULL;
  GValue *out_address = { 0 };

  ft = get_file_transfer (self, id, &error);
  if (ft == NULL)
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }

  g_signal_connect (ft, "finished", G_CALLBACK (ft_finished_cb), self);

  setup_local_socket (self, id);

  if (!tp_file_transfer_mixin_set_state (G_OBJECT (self), id,
        TP_FILE_TRANSFER_STATE_OPEN, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  g_value_init (out_address, G_TYPE_STRING);

  salut_svc_channel_type_file_return_from_accept_file (context, out_address);
}

static void
file_transfer_iface_init (gpointer g_iface,
                          gpointer iface_data)
{
  SalutSvcChannelTypeFileClass *klass =
      (SalutSvcChannelTypeFileClass *)g_iface;

  tp_file_transfer_mixin_iface_init (g_iface, iface_data);
  salut_svc_channel_type_file_implement_accept_file (klass,
        (salut_svc_channel_type_file_accept_file_impl) salut_ft_channel_accept_file);
}


/*
 * Return a GIOChannel for the unix socket returned by
 * GetLocalUnixSocketPath().
 */
static GIOChannel *
get_socket_channel (SalutFtChannel *self,
                    guint id)
{
  gint fd;
  gchar *path;
  size_t path_len;
  struct sockaddr_un addr;
  GIOChannel *io_channel;

  if (!tp_file_transfer_mixin_get_local_unix_socket_path (G_OBJECT (self), id,
        &path, NULL))
    {
      DEBUG ("Impossible to get the socket path");
      return NULL;
    }

  fd = socket (PF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    {
      DEBUG("socket() failed");
      g_free (path);
      return NULL;
    }

  memset (&addr, 0, sizeof (addr));
  addr.sun_family = AF_UNIX;
  path_len = strlen (path);
  strncpy (addr.sun_path, path, path_len);
  g_unlink (path);
  g_free (path);

  if (bind (fd, (struct sockaddr*) &addr,
        G_STRUCT_OFFSET (struct sockaddr_un, sun_path) + path_len) < 0)
    {
      DEBUG ("bind failed");
      close (fd);
      return NULL;
    }

  if (listen (fd, 5) < 0)
    {
      DEBUG ("listen failed");
      close (fd);
      return NULL;
    }

  io_channel = g_io_channel_unix_new (fd);
  g_io_channel_set_close_on_unref (io_channel, TRUE);
  return io_channel;
}

typedef struct {
  SalutFtChannel *self;
  guint id;
} LocalSocketWatchData;

/*
 * Some client is connecting to the Unix socket.
 */
static gboolean
accept_local_socket_connection (GIOChannel *source,
                                GIOCondition condition,
                                gpointer user_data)
{
  LocalSocketWatchData *watch_data = user_data;
  GibberFileTransfer *ft;

  int new_fd;
  struct sockaddr_un addr;
  socklen_t addrlen;
  GIOChannel *channel;

  if (condition & G_IO_IN)
    {
      DEBUG ("Client connected to local socket");

      ft = get_file_transfer (watch_data->self, watch_data->id, NULL);
      if (ft == NULL)
        return FALSE;

      addrlen = sizeof (addr);
      new_fd = accept (g_io_channel_unix_get_fd (source),
          (struct sockaddr *) &addr, &addrlen);
      if (new_fd < 0)
        {
          DEBUG ("accept() failed");
          return FALSE;
        }

      channel = g_io_channel_unix_new (new_fd);
      g_io_channel_set_close_on_unref (channel, TRUE);
      g_io_channel_set_encoding (channel, NULL, NULL);
      if (ft->direction == GIBBER_FILE_TRANSFER_DIRECTION_INCOMING)
        gibber_file_transfer_receive (ft, channel);
      else
        /* FIXME what to do if the chat client connects to the
         * local socket before receiving the "remote-accepted"
         * signal? */
        gibber_file_transfer_send (ft, channel);
      g_io_channel_unref (channel);
    }

  g_free (watch_data);

  return FALSE;
}

static gboolean
setup_local_socket (SalutFtChannel *self, guint id)
{
  GIOChannel *io_channel;
  LocalSocketWatchData *watch_data;

  io_channel = get_socket_channel (self, id);
  if (io_channel == NULL)
    {
      return FALSE;
    }

  watch_data = g_new0 (LocalSocketWatchData, 1);
  watch_data->self = self;
  watch_data->id = id;
  g_io_add_watch (io_channel, G_IO_IN | G_IO_HUP,
      accept_local_socket_connection, watch_data);
  g_io_channel_unref (io_channel);

  return TRUE;
}

