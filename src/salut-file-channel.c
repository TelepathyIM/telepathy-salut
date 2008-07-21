/*
 * salut-file-channel.c - Source for SalutFileChannel
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
#include <telepathy-glib/svc-generic.h>

static void
channel_iface_init (gpointer g_iface, gpointer iface_data);
static void
file_transfer_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (SalutFileChannel, salut_file_channel, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
                           tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL);
    G_IMPLEMENT_INTERFACE (SALUT_TYPE_SVC_CHANNEL_TYPE_FILE,
                           file_transfer_iface_init);
);

#define G_STR_EMPTY(x) ((x) == NULL || (x)[0] == '\0')

/* signal enum */
/*
enum
{
    RECEIVED_STANZA,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};
*/

const char *salut_file_channel_interfaces[] = { NULL };

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_CONTACT,
  PROP_CONNECTION,
  PROP_INTERFACES,
  PROP_XMPP_CONNECTION_MANAGER,
  PROP_DIRECTION,
  PROP_STATE,
  PROP_CONTENT_TYPE,
  PROP_FILENAME,
  PROP_SIZE,
  PROP_ESTIMATED_SIZE,
  PROP_CONTENT_MD5,
  PROP_DESCRIPTION,
  PROP_AVAILABLE_SOCKET_TYPES,
  PROP_TRANSFERRED_BYTES,
  PROP_SOCKET_PATH,
  LAST_PROPERTY
};

/* private structure */
struct _SalutFileChannelPrivate {
  gboolean dispose_has_run;
  gboolean closed;
  gchar *object_path;
  TpHandle handle;
  SalutContact *contact;
  SalutConnection *connection;
  SalutXmppConnectionManager *xmpp_connection_manager;
  GibberXmppConnection *xmpp_connection;
  GibberFileTransfer *ft;
  gchar *local_unix_path;
  /* hash table used to convert from string id to numerical id */
  GHashTable *name_to_id;

  /* properties */
  SalutFileTransferDirection direction;
  SalutFileTransferState state;
  gchar *content_type;
  gchar *filename;
  guint64 size;
  guint64 estimated_size;
  gchar *content_md5;
  gchar *description;
  GHashTable *available_socket_types;
  guint64 transferred_bytes;
  gchar *socket_path;

};

static void
salut_file_channel_do_close (SalutFileChannel *self)
{
  if (self->priv->closed)
    return;

  DEBUG ("Emitting closed signal for %s", self->priv->object_path);
  tp_svc_channel_emit_closed (self);
  self->priv->closed = TRUE;
}

static void
salut_file_channel_init (SalutFileChannel *obj)
{
  obj->priv = G_TYPE_INSTANCE_GET_PRIVATE (obj, SALUT_TYPE_FILE_CHANNEL,
      SalutFileChannelPrivate);

  /* allocate any data required by the object here */
  obj->priv->object_path = NULL;
  obj->priv->connection = NULL;
  obj->priv->xmpp_connection_manager = NULL;
  obj->priv->contact = NULL;
}

static void salut_file_channel_check_and_send (SalutFileChannel *channel);
static void salut_file_channel_set_state (SalutSvcChannelTypeFile *iface,
                                          SalutFileTransferState state,
                                          SalutFileTransferStateChangeReason reason);

static void
salut_file_channel_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  SalutFileChannel *self = SALUT_FILE_CHANNEL (object);

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
      case PROP_INTERFACES:
        g_value_set_boxed (value, salut_file_channel_interfaces);
        break;
      case PROP_XMPP_CONNECTION_MANAGER:
        g_value_set_object (value, self->priv->xmpp_connection_manager);
        break;
      case PROP_DIRECTION:
        g_value_set_uint (value, self->priv->direction);
        break;
      case PROP_STATE:
        g_value_set_uint (value, self->priv->state);
        break;
      case PROP_CONTENT_TYPE:
        g_value_set_string (value, self->priv->content_type);
        break;
      case PROP_FILENAME:
        g_value_set_string (value, self->priv->filename);
        break;
      case PROP_SIZE:
        g_value_set_uint64 (value, self->priv->size);
        break;
      case PROP_ESTIMATED_SIZE:
        g_value_set_uint64 (value, self->priv->estimated_size);
        break;
      case PROP_CONTENT_MD5:
        g_value_set_string (value, self->priv->content_md5);
        break;
      case PROP_DESCRIPTION:
        g_value_set_string (value, self->priv->description);
        break;
      case PROP_AVAILABLE_SOCKET_TYPES:
        g_value_set_boxed (value, self->priv->available_socket_types);
        break;
      case PROP_TRANSFERRED_BYTES:
        g_value_set_uint64 (value, self->priv->transferred_bytes);
        break;
      case PROP_SOCKET_PATH:
        g_value_set_string (value, self->priv->socket_path);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_file_channel_set_property (GObject *object,
                                 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
  SalutFileChannel *self = SALUT_FILE_CHANNEL (object);
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
      case PROP_STATE:
        salut_file_channel_set_state (SALUT_SVC_CHANNEL_TYPE_FILE (object),
                                      g_value_get_uint (value),
                                      SALUT_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);
        break;
      case PROP_TRANSFERRED_BYTES:
        self->priv->transferred_bytes = g_value_get_uint64 (value);
        break;
      case PROP_DIRECTION:
        /* TODO: the new request API will remove the need for this property */
        self->priv->direction = g_value_get_uint (value);
        break;
      case PROP_CONTENT_TYPE:
        /* This should not be writeable with the new request API */
        self->priv->content_type = g_value_dup_string (value);
        break;
      case PROP_FILENAME:
        /* This should not be writeable with the new request API */
        self->priv->filename = g_value_dup_string (value);
        break;
      case PROP_SIZE:
        /* This should not be writeable with the new request API */
        self->priv->size = g_value_get_uint64 (value);
        break;
      case PROP_ESTIMATED_SIZE:
        /* This should not be writeable with the new request API */
        self->priv->estimated_size = g_value_get_uint64 (value);
        break;
      case PROP_CONTENT_MD5:
        /* This should not be writeable with the new request API */
        self->priv->content_md5 = g_value_dup_string (value);
        break;
      case PROP_DESCRIPTION:
        /* This should not be writeable with the new request API */
        self->priv->description = g_value_dup_string (value);
        break;
      case PROP_AVAILABLE_SOCKET_TYPES:
        /* This should not be writeable with the new request API */
        self->priv->available_socket_types = g_value_get_boxed (value);
        break;
      case PROP_SOCKET_PATH:
        self->priv->socket_path = g_value_dup_string (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }

  if (self->priv->direction == SALUT_FILE_TRANSFER_DIRECTION_OUTGOING)
    salut_file_channel_check_and_send (SALUT_FILE_CHANNEL (object));

}

static GObject *
salut_file_channel_constructor (GType type, guint n_props,
                                GObjectConstructParam *props)
{
  GObject *obj;
  SalutFileChannel *self;
  DBusGConnection *bus;
  TpBaseConnection *base_conn;
  TpHandleRepoIface *contact_repo;

  /* Parent constructor chain */
  obj = G_OBJECT_CLASS (salut_file_channel_parent_class)->
          constructor (type, n_props, props);

  self = SALUT_FILE_CHANNEL (obj);

  /* Ref our handle */
  base_conn = TP_BASE_CONNECTION (self->priv->connection);

  contact_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);

  tp_handle_ref (contact_repo, self->priv->handle);

  /* Initialize the hash table used to convert from the id name
   * to the numerical id. */
  self->priv->name_to_id = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, NULL);

  /* Connect to the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, self->priv->object_path, obj);

  /* Initialise the available socket types hash table*/
  self->priv->available_socket_types = g_hash_table_new (g_int_hash, g_int_equal);

  return obj;
}

static void
salut_file_channel_dispose (GObject *object);
static void
salut_file_channel_finalize (GObject *object);

static void
salut_file_channel_class_init (SalutFileChannelClass *salut_file_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_file_channel_class);
  GParamSpec *param_spec;

  static TpDBusPropertiesMixinPropImpl channel_props[] = {
    { "TargetHandleType", "handle-type", NULL },
    { "TargetHandle", "handle", NULL },
    { "ChannelType", "channel-type", NULL },
    { "Interfaces", "interfaces", NULL },
    { NULL }
  };

  static TpDBusPropertiesMixinPropImpl file_props[] = {
    { "Direction", "direction", NULL },
    { "State", "state", "state" },
    { "ContentType", "content-type", "content-type" },
    { "Filename", "filename", "filename" },
    { "Size", "size", "size" },
    { "EstimatedSize", "estimated-size", "estimated-size" },
    { "ContentMD5", "content-md5", "content-md5" },
    { "Description", "description", "description" },
    { "AvailableSocketTypes", "available-socket-types", NULL },
    { "SocketPath", "socket-path", "socket-path" },
    { NULL }
  };

  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
    { TP_IFACE_CHANNEL,
      tp_dbus_properties_mixin_getter_gobject_properties,
      NULL,
      channel_props
    },
    { SALUT_IFACE_CHANNEL_TYPE_FILE,
      tp_dbus_properties_mixin_getter_gobject_properties,
      tp_dbus_properties_mixin_setter_gobject_properties,
      file_props
    },
    { NULL }
  };

  g_type_class_add_private (salut_file_channel_class,
      sizeof (SalutFileChannelPrivate));

  object_class->dispose = salut_file_channel_dispose;
  object_class->finalize = salut_file_channel_finalize;

  object_class->constructor = salut_file_channel_constructor;
  object_class->get_property = salut_file_channel_get_property;
  object_class->set_property = salut_file_channel_set_property;

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

  param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
      "Additional Channel.Interface.* interfaces",
      G_TYPE_STRV,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NAME);
  g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

  param_spec = g_param_spec_object (
      "xmpp-connection-manager",
      "SalutXmppConnectionManager object",
      "Salut XMPP Connection manager used for this file channel",
      SALUT_TYPE_XMPP_CONNECTION_MANAGER,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_XMPP_CONNECTION_MANAGER,
      param_spec);

  param_spec = g_param_spec_uint (
      "direction",
      "SalutFileTransferDirection direction",
      "Direction of the file transfer",
      0,
      G_MAXUINT,
      SALUT_FILE_TRANSFER_DIRECTION_OUTGOING,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_DIRECTION, param_spec);

  param_spec = g_param_spec_uint (
      "state",
      "SalutFileTransferState state",
      "State of the file transfer in this channel",
      0,
      G_MAXUINT,
      SALUT_FILE_TRANSFER_STATE_LOCAL_PENDING,
      G_PARAM_CONSTRUCT |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  param_spec = g_param_spec_string (
      "content-type",
      "gchar *content-type",
      "ContentType of the file",
      "",
      /* TODO: change this to CONSTRUCT_ONLY when
       * the new request API is used.
       */
      G_PARAM_CONSTRUCT |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONTENT_TYPE, param_spec);

  param_spec = g_param_spec_string (
      "filename",
      "gchar *filename",
      "Name of the file",
      "",
      /* TODO: change this to CONSTRUCT_ONLY when
       * the new request API is used.
       */
      G_PARAM_CONSTRUCT |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_FILENAME, param_spec);

  param_spec = g_param_spec_uint64 (
      "size",
      "guint size",
      "Size of the file in bytes",
      0,
      G_MAXUINT64,
      G_MAXUINT64,
      /* TODO: change this to CONSTRUCT_ONLY when
       * the new request API is used.
       */
      G_PARAM_CONSTRUCT |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SIZE, param_spec);

  param_spec = g_param_spec_uint64 (
      "estimated-size",
      "guint estimated-size",
      "Estimated size of the file in bytes",
      0,
      G_MAXUINT64,
      G_MAXUINT64,
      /* TODO: change this to CONSTRUCT_ONLY when
       * the new request API is used.
       */
      G_PARAM_CONSTRUCT |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_ESTIMATED_SIZE, param_spec);

  param_spec = g_param_spec_string (
      "content-md5",
      "gchar *content-md5",
      "md5sum of the file contents",
      "",
      /* TODO: change this to CONSTRUCT_ONLY when
       * the new request API is used.
       */
      G_PARAM_CONSTRUCT |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONTENT_MD5, param_spec);

  param_spec = g_param_spec_string (
      "description",
      "gchar *description",
      "Description of the file",
      "",
      /* TODO: change this to CONSTRUCT_ONLY when
       * the new request API is used.
       */
      G_PARAM_CONSTRUCT |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_DESCRIPTION, param_spec);

  param_spec = g_param_spec_boxed (
      "available-socket-types",
      "SalutSupportedSocketMap available-socket-types",
      "Available socket types",
      dbus_g_type_get_map ("GHashTable", G_TYPE_UINT, DBUS_TYPE_G_UINT_ARRAY),
      /* TODO: change this to CONSTRUCT_ONLY when
       * the new request API is used.
       */
      G_PARAM_CONSTRUCT |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_AVAILABLE_SOCKET_TYPES, param_spec);

  param_spec = g_param_spec_uint64 (
      "transferred-bytes",
      "guint transferred-bytes",
      "Bytes transferred",
      0,
      G_MAXUINT64,
      0,
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_TRANSFERRED_BYTES, param_spec);

  param_spec = g_param_spec_string (
      "socket-path",
      "gchar *socket-path",
      "UNIX socket path",
      "",
      G_PARAM_CONSTRUCT |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SOCKET_PATH, param_spec);

  salut_file_channel_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (SalutFileChannelClass, dbus_props_class));
}

void
salut_file_channel_dispose (GObject *object)
{
  SalutFileChannel *self = SALUT_FILE_CHANNEL (object);
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (self->priv->connection);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);

  if (self->priv->dispose_has_run)
    return;

  self->priv->dispose_has_run = TRUE;

  tp_handle_unref (handle_repo, self->priv->handle);

  g_hash_table_unref (self->priv->name_to_id);

  salut_file_channel_do_close (self);

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

  if (G_OBJECT_CLASS (salut_file_channel_parent_class)->dispose)
    G_OBJECT_CLASS (salut_file_channel_parent_class)->dispose (object);
}

static void
salut_file_channel_finalize (GObject *object)
{
  SalutFileChannel *self = SALUT_FILE_CHANNEL (object);

  /* free any data held directly by the object here */
  g_free (self->priv->object_path);

  G_OBJECT_CLASS (salut_file_channel_parent_class)->finalize (object);
}


/**
 * salut_file_channel_close
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_file_channel_close (TpSvcChannel *iface,
                          DBusGMethodInvocation *context)
{
  salut_file_channel_do_close (SALUT_FILE_CHANNEL (iface));
  tp_svc_channel_return_from_close (context);
}

/**
 * salut_file_channel_get_channel_type
 *
 * Implements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_file_channel_get_channel_type (TpSvcChannel *iface,
                                     DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      SALUT_IFACE_CHANNEL_TYPE_FILE);
}

/**
 * salut_file_channel_get_handle
 *
 * Implements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_file_channel_get_handle (TpSvcChannel *iface,
                               DBusGMethodInvocation *context)
{
  SalutFileChannel *self = SALUT_FILE_CHANNEL (iface);

  tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_CONTACT,
                                         self->priv->handle);
}

/**
 * salut_file_channel_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_file_channel_get_interfaces (TpSvcChannel *iface,
                                   DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_interfaces (context, salut_file_channel_interfaces);
}

static void
channel_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, salut_file_channel_##x)
  IMPLEMENT (close);
  IMPLEMENT (get_channel_type);
  IMPLEMENT (get_handle);
  IMPLEMENT (get_interfaces);
#undef IMPLEMENT
}

static void
error_cb (GibberFileTransfer *ft,
          guint domain,
          gint code,
          const gchar *message,
          SalutFileChannel *self)
{
}

static void
ft_finished_cb (GibberFileTransfer *ft,
                SalutFileChannel *self)
{
  salut_file_channel_set_state (SALUT_SVC_CHANNEL_TYPE_FILE (self),
                                SALUT_FILE_TRANSFER_STATE_COMPLETED,
                                SALUT_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);
}

static void
remote_accepted_cb (GibberFileTransfer *ft,
                    SalutFileChannel *self)
{
  salut_file_channel_set_state (SALUT_SVC_CHANNEL_TYPE_FILE (self),
                                SALUT_FILE_TRANSFER_STATE_OPEN,
                                SALUT_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);

  g_signal_connect (ft, "finished", G_CALLBACK (ft_finished_cb), self);
}

static gboolean
setup_local_socket (SalutFileChannel *self);

static void
send_file_offer (SalutFileChannel *self)
{
  GibberFileTransfer *ft;

  ft = g_object_new (GIBBER_TYPE_OOB_FILE_TRANSFER,
      "self-id", self->priv->connection->name,
      "peer-id", self->priv->contact->name,
      "filename", self->priv->filename,
      "connection", self->priv->xmpp_connection,
      NULL);
  g_signal_connect (ft, "remote-accepted",
      G_CALLBACK (remote_accepted_cb), self);
  g_signal_connect (ft, "error", G_CALLBACK (error_cb), self);

  self->priv->ft = ft;

  setup_local_socket (self);

  if (self->priv->size != G_MAXUINT64)
    ft->size = self->priv->size;
  else
    ft->size = self->priv->estimated_size;

  gibber_file_transfer_offer (ft);
}

static void
xmpp_connection_manager_new_connection_cb (SalutXmppConnectionManager *mgr,
                                           GibberXmppConnection *connection,
                                           SalutContact *contact,
                                           gpointer user_data)
{
  SalutFileChannel *channel = user_data;

  channel->priv->xmpp_connection = g_object_ref (connection);
  g_signal_handlers_disconnect_by_func (mgr,
                                        xmpp_connection_manager_new_connection_cb, user_data);
  send_file_offer (channel);
}

static void
salut_file_channel_check_and_send (SalutFileChannel *channel)
{
  GibberXmppConnection *connection = NULL;
  SalutXmppConnectionManagerRequestConnectionResult request_result;
  GError *error = NULL;

  if (G_STR_EMPTY (channel->priv->content_type))
    return;

  if (G_STR_EMPTY (channel->priv->filename))
    return;

  if (channel->priv->size == 0 && channel->priv->estimated_size == 0)
    return;

  if (G_STR_EMPTY (channel->priv->content_md5))
    return;

  DEBUG ("Starting sending file transfer");

  request_result = salut_xmpp_connection_manager_request_connection (
    channel->priv->xmpp_connection_manager, channel->priv->contact, &connection,
    &error);

  if (request_result == SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_DONE)
    {
      channel->priv->xmpp_connection = connection;
      send_file_offer (channel);
    }
  else if (request_result == SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_PENDING)
    {
      g_signal_connect (channel->priv->xmpp_connection_manager, "new-connection",
                        G_CALLBACK (xmpp_connection_manager_new_connection_cb), channel);
    }
  else
    {
      DEBUG ("Request connection failed");
      g_error_free (error);
    }
}

void
salut_file_channel_received_file_offer (SalutFileChannel *self,
                                        GibberXmppStanza *stanza,
                                        GibberXmppConnection *conn)
{
  GibberFileTransfer *ft;

  ft = gibber_file_transfer_new_from_stanza (stanza, conn);
  g_signal_connect (ft, "error", G_CALLBACK (error_cb), self);

  DEBUG ("Received file offer with id '%s'", ft->id);

  self->priv->ft = ft;

  self->priv->filename = ft->filename;
  self->priv->size = ft->size;
}

static void
salut_file_channel_set_state (SalutSvcChannelTypeFile *iface,
                              SalutFileTransferState state,
                              SalutFileTransferStateChangeReason reason)
{
  SalutFileChannel *self = SALUT_FILE_CHANNEL (iface);

  self->priv->state = state;
  salut_svc_channel_type_file_emit_file_transfer_state_changed (iface,
      state, reason);
}

/**
 * salut_file_channel_accept_file
 *
 * Implements D-Bus method AcceptFile
 * on interface org.freedesktop.Telepathy.Channel.Type.File
 */
static void
salut_file_channel_accept_file (SalutSvcChannelTypeFile *iface,
                                guint address_type,
                                guint access_control,
                                GValue *access_control_param,
                                DBusGMethodInvocation *context)
{
  SalutFileChannel *self = SALUT_FILE_CHANNEL (iface);
  GError *error = NULL;
  GValue out_address = { 0 };
  GibberFileTransfer *ft;

  ft = self->priv->ft;
  if (ft == NULL)
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }

  g_signal_connect (ft, "finished", G_CALLBACK (ft_finished_cb), self);

  setup_local_socket (self);

  salut_file_channel_set_state (iface, SALUT_FILE_TRANSFER_STATE_OPEN,
        SALUT_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);

  g_value_init (&out_address, G_TYPE_STRING);
  g_value_set_string (&out_address, g_build_filename (self->priv->local_unix_path, "tp-ft", NULL));

  salut_svc_channel_type_file_return_from_accept_file (context, &out_address);
}

static void
file_transfer_iface_init (gpointer g_iface,
                          gpointer iface_data)
{
  SalutSvcChannelTypeFileClass *klass =
      (SalutSvcChannelTypeFileClass *)g_iface;

  salut_svc_channel_type_file_implement_accept_file (klass,
        (salut_svc_channel_type_file_accept_file_impl) salut_file_channel_accept_file);
}

static void
create_socket_path (SalutFileChannel *self)
{
  gint fd;
  gchar *tmp_path = NULL;

  while (tmp_path == NULL)
    {
      fd = g_file_open_tmp ("tp-ft-XXXXXX", &tmp_path, NULL);
      close (fd);
      g_unlink (tmp_path);
      if (g_mkdir (tmp_path, 0700) < 0)
        {
          g_free (tmp_path);
          tmp_path = NULL;
        }
    }

  self->priv->local_unix_path = tmp_path;
}

static gchar *
get_local_unix_socket_path (SalutFileChannel *self)
{
  gchar *path;

  if (self->priv->local_unix_path == NULL)
    create_socket_path (self);

  /* TODO: perhaps this ought to be more random */
  path = g_build_filename (self->priv->local_unix_path, "tp-ft", NULL);

  return path;
}

/*
 * Return a GIOChannel for the local unix socket path.
 */
static GIOChannel *
get_socket_channel (SalutFileChannel *self)
{
  gint fd;
  gchar *path;
  size_t path_len;
  struct sockaddr_un addr;
  GIOChannel *io_channel;

  path = get_local_unix_socket_path (self);
  self->priv->socket_path = g_strdup (path);

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

/*
 * Some client is connecting to the Unix socket.
 */
static gboolean
accept_local_socket_connection (GIOChannel *source,
                                GIOCondition condition,
                                gpointer user_data)
{
  GibberFileTransfer *ft;
  int new_fd;
  struct sockaddr_un addr;
  socklen_t addrlen;
  GIOChannel *channel;

  if (condition & G_IO_IN)
    {
      DEBUG ("Client connected to local socket");

      ft = (GibberFileTransfer *) user_data;

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

  return FALSE;
}

static gboolean
setup_local_socket (SalutFileChannel *self)
{
  GIOChannel *io_channel;

  io_channel = get_socket_channel (self);
  if (io_channel == NULL)
    {
      return FALSE;
    }

  g_io_add_watch (io_channel, G_IO_IN | G_IO_HUP,
      accept_local_socket_connection, self->priv->ft);
  g_io_channel_unref (io_channel);

  return TRUE;
}

