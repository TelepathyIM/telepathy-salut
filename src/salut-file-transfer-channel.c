/*
 * salut-file-transfer-channel.c - Source for SalutFileTransferChannel
 * Copyright (C) 2007 Marco Barisione <marco@barisione.org>
 * Copyright (C) 2005, 2007, 2008 Collabora Ltd.
 *   @author: Sjoerd Simons <sjoerd@luon.net>
 *   @author: Jonny Lamb <jonny.lamb@collabora.co.uk>
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

#include "salut-file-transfer-channel.h"
#include "signals-marshal.h"

#include "salut-connection.h"
#include "salut-im-manager.h"
#include "salut-contact.h"

#include <gibber/gibber-xmpp-stanza.h>
#include <gibber/gibber-file-transfer.h>
#include <gibber/gibber-oob-file-transfer.h>
#include <gibber/gibber-iq-helper.h>
#include <gibber/gibber-xmpp-error.h>

#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/svc-generic.h>

static void
channel_iface_init (gpointer g_iface, gpointer iface_data);
static void
file_transfer_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (SalutFileTransferChannel, salut_file_transfer_channel, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
                           tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL);
    G_IMPLEMENT_INTERFACE (SALUT_TYPE_SVC_CHANNEL_TYPE_FILE_TRANSFER,
                           file_transfer_iface_init);
);

#define CHECK_STR_EMPTY(x) ((x) == NULL || (x)[0] == '\0')

#define SALUT_UNDEFINED_FILE_SIZE G_MAXUINT64

static const char *salut_file_transfer_channel_interfaces[] = { NULL };

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,

   /* org.freedesktop.Telepathy.Channel D-Bus properties */
  PROP_CHANNEL_TYPE,
  PROP_INTERFACES,
  PROP_HANDLE,
  PROP_TARGET_ID,
  PROP_HANDLE_TYPE,
  PROP_REQUESTED,
  PROP_INITIATOR_HANDLE,
  PROP_INITIATOR_ID,

  PROP_CHANNEL_DESTROYED,
  PROP_CHANNEL_PROPERTIES,

  /* org.freedesktop.Telepathy.Channel.Type.FileTransfer D-Bus properties */
  PROP_STATE,
  PROP_CONTENT_TYPE,
  PROP_FILENAME,
  PROP_SIZE,
  PROP_CONTENT_HASH_TYPE,
  PROP_CONTENT_HASH,
  PROP_DESCRIPTION,
  PROP_DATE,
  PROP_AVAILABLE_SOCKET_TYPES,
  PROP_TRANSFERRED_BYTES,
  PROP_INITIAL_OFFSET,

  PROP_CONTACT,
  PROP_CONNECTION,
  PROP_XMPP_CONNECTION_MANAGER,
  LAST_PROPERTY
};

/* private structure */
struct _SalutFileTransferChannelPrivate {
  gboolean dispose_has_run;
  gboolean closed;
  gchar *object_path;
  TpHandle handle;
  SalutContact *contact;
  SalutConnection *connection;
  SalutXmppConnectionManager *xmpp_connection_manager;
  GibberXmppConnection *xmpp_connection;
  GibberFileTransfer *ft;
  glong last_transferred_bytes_emitted;
  gchar *socket_path;
  TpHandle initiator;

  /* properties */
  SalutFileTransferState state;
  gchar *content_type;
  gchar *filename;
  guint64 size;
  SalutFileHashType content_hash_type;
  gchar *content_hash;
  gchar *description;
  GHashTable *available_socket_types;
  guint64 transferred_bytes;
  guint64 initial_offset;
  guint64 date;
};

static void
salut_file_transfer_channel_do_close (SalutFileTransferChannel *self)
{
  if (self->priv->closed)
    return;

  DEBUG ("Emitting closed signal for %s", self->priv->object_path);
  tp_svc_channel_emit_closed (self);
  self->priv->closed = TRUE;
}

static void
salut_file_transfer_channel_init (SalutFileTransferChannel *obj)
{
  obj->priv = G_TYPE_INSTANCE_GET_PRIVATE (obj, SALUT_TYPE_FILE_TRANSFER_CHANNEL,
      SalutFileTransferChannelPrivate);

  /* allocate any data required by the object here */
  obj->priv->object_path = NULL;
  obj->priv->connection = NULL;
  obj->priv->xmpp_connection_manager = NULL;
  obj->priv->contact = NULL;
}

static void salut_file_transfer_channel_set_state (SalutSvcChannelTypeFileTransfer *iface,
    SalutFileTransferState state, SalutFileTransferStateChangeReason reason);

static void
salut_file_transfer_channel_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  SalutFileTransferChannel *self = SALUT_FILE_TRANSFER_CHANNEL (object);
  TpBaseConnection *base_conn = (TpBaseConnection *) self->priv->connection;

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_value_set_string (value, self->priv->object_path);
        break;
      case PROP_CHANNEL_TYPE:
        g_value_set_static_string (value,
            SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER);
        break;
      case PROP_HANDLE_TYPE:
        g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
        break;
      case PROP_TARGET_ID:
        {
           TpHandleRepoIface *repo = tp_base_connection_get_handles (base_conn,
             TP_HANDLE_TYPE_CONTACT);

           g_value_set_string (value, tp_handle_inspect (repo,
                 self->priv->handle));
        }
        break;
      case PROP_HANDLE:
        g_value_set_uint (value, self->priv->handle);
        break;
      case PROP_REQUESTED:
        g_value_set_boolean (value, (self->priv->initiator ==
              base_conn->self_handle));
        break;
      case PROP_INITIATOR_HANDLE:
        g_value_set_uint (value, self->priv->initiator);
        break;
      case PROP_INITIATOR_ID:
          {
            TpHandleRepoIface *repo = tp_base_connection_get_handles (
                base_conn, TP_HANDLE_TYPE_CONTACT);

            g_assert (self->priv->initiator != 0);
            g_value_set_string (value,
                tp_handle_inspect (repo, self->priv->initiator));
          }
        break;
      case PROP_CONTACT:
        g_value_set_object (value, self->priv->contact);
        break;
      case PROP_CONNECTION:
        g_value_set_object (value, self->priv->connection);
        break;
      case PROP_INTERFACES:
        g_value_set_boxed (value, salut_file_transfer_channel_interfaces);
        break;
      case PROP_XMPP_CONNECTION_MANAGER:
        g_value_set_object (value, self->priv->xmpp_connection_manager);
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
      case PROP_CONTENT_HASH_TYPE:
        g_value_set_uint (value, self->priv->content_hash_type);
        break;
      case PROP_CONTENT_HASH:
        g_value_set_string (value, self->priv->content_hash);
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
      case PROP_INITIAL_OFFSET:
        g_value_set_uint64 (value, self->priv->initial_offset);
        break;
      case PROP_DATE:
        g_value_set_uint64 (value, self->priv->date);
        break;
     case PROP_CHANNEL_DESTROYED:
        g_value_set_boolean (value, self->priv->closed);
        break;
      case PROP_CHANNEL_PROPERTIES:
        g_value_set_boxed (value,
            tp_dbus_properties_mixin_make_properties_hash (object,
                TP_IFACE_CHANNEL, "ChannelType",
                TP_IFACE_CHANNEL, "Interfaces",
                TP_IFACE_CHANNEL, "TargetHandle",
                TP_IFACE_CHANNEL, "TargetID",
                TP_IFACE_CHANNEL, "TargetHandleType",
                TP_IFACE_CHANNEL, "Requested",
                TP_IFACE_CHANNEL, "InitiatorHandle",
                TP_IFACE_CHANNEL, "InitiatorID",
                SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "State",
                SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "ContentType",
                SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "Filename",
                SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "Size",
                SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "ContentHashType",
                SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "ContentHash",
                SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "Description",
                SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "Date",
                SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "AvailableSocketTypes",
                SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "TransferredBytes",
                SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "InitialOffset",
                NULL));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_file_transfer_channel_set_property (GObject *object,
                                 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
  SalutFileTransferChannel *self = SALUT_FILE_TRANSFER_CHANNEL (object);

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
        self->priv->contact = g_value_dup_object (value);
        break;
      case PROP_CONNECTION:
        self->priv->connection = g_value_get_object (value);
        break;
      case PROP_HANDLE_TYPE:
        g_assert (g_value_get_uint (value) == 0
                  || g_value_get_uint (value) == TP_HANDLE_TYPE_CONTACT);
        break;
      case PROP_CHANNEL_TYPE:
        /* these properties are writable in the interface, but not actually
         * meaningfully changeable on this channel, so we do nothing */
        break;
      case PROP_XMPP_CONNECTION_MANAGER:
        self->priv->xmpp_connection_manager = g_value_dup_object (value);
        break;
      case PROP_STATE:
        salut_file_transfer_channel_set_state (
            SALUT_SVC_CHANNEL_TYPE_FILE_TRANSFER (object),
            g_value_get_uint (value),
            SALUT_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);
        break;
      case PROP_CONTENT_TYPE:
        self->priv->content_type = g_value_dup_string (value);
        break;
      case PROP_FILENAME:
        self->priv->filename = g_value_dup_string (value);
        break;
      case PROP_SIZE:
        self->priv->size = g_value_get_uint64 (value);
        break;
      case PROP_CONTENT_HASH_TYPE:
        self->priv->content_hash_type = g_value_get_uint (value);
        break;
      case PROP_CONTENT_HASH:
        self->priv->content_hash = g_value_dup_string (value);
        break;
      case PROP_DESCRIPTION:
        self->priv->description = g_value_dup_string (value);
        break;
      case PROP_INITIATOR_HANDLE:
        self->priv->initiator = g_value_get_uint (value);
        g_assert (self->priv->initiator != 0);
        break;
      case PROP_DATE:
        self->priv->date = g_value_get_uint64 (value);
        break;
      case PROP_INITIAL_OFFSET:
        self->priv->initial_offset = g_value_get_uint64 (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
free_array (GArray *array)
{
  g_array_free (array, TRUE);
}

static GObject *
salut_file_transfer_channel_constructor (GType type, guint n_props,
                                GObjectConstructParam *props)
{
  GObject *obj;
  SalutFileTransferChannel *self;
  DBusGConnection *bus;
  TpBaseConnection *base_conn;
  TpHandleRepoIface *contact_repo;
  GArray *unix_access;
  TpSocketAccessControl access;

  /* Parent constructor chain */
  obj = G_OBJECT_CLASS (salut_file_transfer_channel_parent_class)->
          constructor (type, n_props, props);

  self = SALUT_FILE_TRANSFER_CHANNEL (obj);

  /* Ref our handle */
  base_conn = TP_BASE_CONNECTION (self->priv->connection);

  contact_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);

  tp_handle_ref (contact_repo, self->priv->handle);

  /* Connect to the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, self->priv->object_path, obj);

  /* Initialise the available socket types hash table */
  self->priv->available_socket_types = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) free_array);

  /* Socket_Address_Type_Unix */
  unix_access = g_array_sized_new (FALSE, FALSE, sizeof (TpSocketAccessControl),
      1);
  access = TP_SOCKET_ACCESS_CONTROL_LOCALHOST;
  g_array_append_val (unix_access, access);
  g_hash_table_insert (self->priv->available_socket_types,
      GUINT_TO_POINTER (TP_SOCKET_ADDRESS_TYPE_UNIX), unix_access);

  self->priv->last_transferred_bytes_emitted = 0;

  return obj;
}

static void
salut_file_transfer_channel_dispose (GObject *object);
static void
salut_file_transfer_channel_finalize (GObject *object);

static void
salut_file_transfer_channel_class_init (SalutFileTransferChannelClass *salut_file_transfer_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_file_transfer_channel_class);
  GParamSpec *param_spec;

  static TpDBusPropertiesMixinPropImpl channel_props[] = {
    { "TargetHandleType", "handle-type", NULL },
    { "TargetHandle", "handle", NULL },
    { "TargetID", "target-id", NULL },
    { "ChannelType", "channel-type", NULL },
    { "Interfaces", "interfaces", NULL },
    { "Requested", "requested", NULL },
    { "InitiatorHandle", "initiator-handle", NULL },
    { "InitiatorID", "initiator-id", NULL },
    { NULL }
  };

  static TpDBusPropertiesMixinPropImpl file_props[] = {
    { "State", "state", NULL },
    { "ContentType", "content-type", "content-type" },
    { "Filename", "filename", "filename" },
    { "Size", "size", "size" },
    { "ContentHashType", "content-hash-type", "content-hash-type" },
    { "ContentHash", "content-hash", "content-hash" },
    { "Description", "description", "description" },
    { "Description", "date", "date" },
    { "AvailableSocketTypes", "available-socket-types", NULL },
    { "TransferredBytes", "transferred-bytes", NULL },
    { "InitialOffset", "initial-offset", NULL },
    { "Date", "date", "date" },
    { NULL }
  };

  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
    { TP_IFACE_CHANNEL,
      tp_dbus_properties_mixin_getter_gobject_properties,
      NULL,
      channel_props
    },
    { SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER,
      tp_dbus_properties_mixin_getter_gobject_properties,
      tp_dbus_properties_mixin_setter_gobject_properties,
      file_props
    },
    { NULL }
  };

  g_type_class_add_private (salut_file_transfer_channel_class,
      sizeof (SalutFileTransferChannelPrivate));

  object_class->dispose = salut_file_transfer_channel_dispose;
  object_class->finalize = salut_file_transfer_channel_finalize;

  object_class->constructor = salut_file_transfer_channel_constructor;
  object_class->get_property = salut_file_transfer_channel_get_property;
  object_class->set_property = salut_file_transfer_channel_set_property;

  g_object_class_override_property (object_class, PROP_OBJECT_PATH,
      "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
      "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
      "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");
  g_object_class_override_property (object_class, PROP_CHANNEL_DESTROYED,
      "channel-destroyed");
  g_object_class_override_property (object_class, PROP_CHANNEL_PROPERTIES,
      "channel-properties");

  param_spec = g_param_spec_string ("target-id", "Target JID",
      "The string obtained by inspecting this channel's handle",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TARGET_ID, param_spec);

  param_spec = g_param_spec_boolean ("requested", "Requested?",
      "True if this channel was requested by the local user",
      FALSE,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NAME);
  g_object_class_install_property (object_class, PROP_REQUESTED, param_spec);

 param_spec = g_param_spec_uint ("initiator-handle", "Initiator's handle",
      "The contact who initiated the channel",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NAME);
  g_object_class_install_property (object_class, PROP_INITIATOR_HANDLE,
      param_spec);

  param_spec = g_param_spec_string ("initiator-id", "Initiator's bare JID",
      "The string obtained by inspecting the initiator-handle",
      NULL,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NAME);
  g_object_class_install_property (object_class, PROP_INITIATOR_ID,
      param_spec);

  param_spec = g_param_spec_object ("contact",
      "SalutContact object",
      "Salut Contact to which this channel is dedicated",
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
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
      "Additional Channel.Interface.* interfaces",
      G_TYPE_STRV,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB |
      G_PARAM_STATIC_NAME);
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
      "state",
      "SalutFileTransferState state",
      "State of the file transfer in this channel",
      0,
      G_MAXUINT,
      0,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  /* TODO: most of these properties should be construct only but
   * then incoming FT stanza should be parsed before creating the
   * channel object. */
  param_spec = g_param_spec_string (
      "content-type",
      "gchar *content-type",
      "ContentType of the file",
      "application/octet-stream",
      G_PARAM_CONSTRUCT |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONTENT_TYPE,
      param_spec);

  param_spec = g_param_spec_string (
      "filename",
      "gchar *filename",
      "Name of the file",
      "",
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
      SALUT_UNDEFINED_FILE_SIZE,
      G_PARAM_CONSTRUCT |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SIZE, param_spec);

  param_spec = g_param_spec_uint (
      "content-hash-type",
      "SalutFileHashType content-hash-type",
      "Hash type",
      0,
      G_MAXUINT,
      SALUT_FILE_HASH_TYPE_NONE,
      G_PARAM_CONSTRUCT |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONTENT_HASH_TYPE,
      param_spec);

  param_spec = g_param_spec_string (
      "content-hash",
      "gchar *content-hash",
      "Hash of the file contents",
      "",
      G_PARAM_CONSTRUCT |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONTENT_HASH,
      param_spec);

  param_spec = g_param_spec_string (
      "description",
      "gchar *description",
      "Description of the file",
      "",
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
      G_PARAM_READABLE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_AVAILABLE_SOCKET_TYPES,
      param_spec);

  param_spec = g_param_spec_uint64 (
      "transferred-bytes",
      "guint64 transferred-bytes",
      "Bytes transferred",
      0,
      G_MAXUINT64,
      0,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_TRANSFERRED_BYTES,
      param_spec);

  param_spec = g_param_spec_uint64 (
      "initial-offset",
      "guint64 initial_offset",
      "Offset set at the beginning of the transfer",
      0,
      G_MAXUINT64,
      0,
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_INITIAL_OFFSET,
      param_spec);

  param_spec = g_param_spec_uint64 (
      "date",
      "Epoch time",
      "the last modification time of the file being transferred",
      0,
      G_MAXUINT64,
      0,
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_DATE,
      param_spec);

  salut_file_transfer_channel_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (SalutFileTransferChannelClass, dbus_props_class));
}

void
salut_file_transfer_channel_dispose (GObject *object)
{
  SalutFileTransferChannel *self = SALUT_FILE_TRANSFER_CHANNEL (object);
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (self->priv->connection);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);

  if (self->priv->dispose_has_run)
    return;

  self->priv->dispose_has_run = TRUE;

  tp_handle_unref (handle_repo, self->priv->handle);

  salut_file_transfer_channel_do_close (self);

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

  if (G_OBJECT_CLASS (salut_file_transfer_channel_parent_class)->dispose)
    G_OBJECT_CLASS (salut_file_transfer_channel_parent_class)->dispose (object);
}

static void
salut_file_transfer_channel_finalize (GObject *object)
{
  SalutFileTransferChannel *self = SALUT_FILE_TRANSFER_CHANNEL (object);

  /* free any data held directly by the object here */
  g_free (self->priv->object_path);
  g_free (self->priv->filename);

  G_OBJECT_CLASS (salut_file_transfer_channel_parent_class)->finalize (object);
}


/**
 * salut_file_transfer_channel_close
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_file_transfer_channel_close (TpSvcChannel *iface,
                          DBusGMethodInvocation *context)
{
  SalutFileTransferChannel *self = SALUT_FILE_TRANSFER_CHANNEL (iface);

  if (self->priv->state != SALUT_FILE_TRANSFER_STATE_COMPLETED)
    {
      gibber_file_transfer_cancel (self->priv->ft, 406);
      salut_file_transfer_channel_set_state (SALUT_SVC_CHANNEL_TYPE_FILE_TRANSFER (iface),
          SALUT_FILE_TRANSFER_STATE_CANCELLED,
          SALUT_FILE_TRANSFER_STATE_CHANGE_REASON_LOCAL_STOPPED);
    }

  salut_file_transfer_channel_do_close (SALUT_FILE_TRANSFER_CHANNEL (iface));
  tp_svc_channel_return_from_close (context);
}

/**
 * salut_file_transfer_channel_get_channel_type
 *
 * Implements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_file_transfer_channel_get_channel_type (TpSvcChannel *iface,
                                     DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER);
}

/**
 * salut_file_transfer_channel_get_handle
 *
 * Implements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_file_transfer_channel_get_handle (TpSvcChannel *iface,
                               DBusGMethodInvocation *context)
{
  SalutFileTransferChannel *self = SALUT_FILE_TRANSFER_CHANNEL (iface);

  tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_CONTACT,
                                         self->priv->handle);
}

/**
 * salut_file_transfer_channel_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_file_transfer_channel_get_interfaces (TpSvcChannel *iface,
                                   DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_interfaces (context,
      salut_file_transfer_channel_interfaces);
}

static void
channel_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *) g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, salut_file_transfer_channel_##x)
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
          SalutFileTransferChannel *self)
{
}

static void
ft_finished_cb (GibberFileTransfer *ft,
                SalutFileTransferChannel *self)
{
  salut_file_transfer_channel_set_state (SALUT_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
      SALUT_FILE_TRANSFER_STATE_COMPLETED,
      SALUT_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);

  salut_xmpp_connection_manager_release_connection (
      self->priv->xmpp_connection_manager,
      self->priv->xmpp_connection);
}

static void
ft_remote_canceled_cb (GibberFileTransfer *ft,
                       SalutFileTransferChannel *self)
{
  gibber_file_transfer_cancel (ft, 406);
  salut_file_transfer_channel_set_state (SALUT_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
      SALUT_FILE_TRANSFER_STATE_CANCELLED,
      SALUT_FILE_TRANSFER_STATE_CHANGE_REASON_REMOTE_STOPPED);

  salut_xmpp_connection_manager_release_connection (
      self->priv->xmpp_connection_manager,
      self->priv->xmpp_connection);
}

static void
remote_accepted_cb (GibberFileTransfer *ft,
                    SalutFileTransferChannel *self)
{
  salut_file_transfer_channel_set_state (SALUT_SVC_CHANNEL_TYPE_FILE_TRANSFER (self),
      SALUT_FILE_TRANSFER_STATE_OPEN,
      SALUT_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);

  g_signal_connect (ft, "finished", G_CALLBACK (ft_finished_cb), self);
  g_signal_connect (ft, "canceled", G_CALLBACK (ft_remote_canceled_cb), self);
}

static gboolean setup_local_socket (SalutFileTransferChannel *self);
static void ft_transferred_chunk_cb (GibberFileTransfer *ft, guint64 count,
    SalutFileTransferChannel *self);

static void
send_file_offer (SalutFileTransferChannel *self)
{
  GibberFileTransfer *ft;

  ft = g_object_new (GIBBER_TYPE_OOB_FILE_TRANSFER,
      "self-id", self->priv->connection->name,
      "peer-id", self->priv->contact->name,
      "filename", self->priv->filename,
      "connection", self->priv->xmpp_connection,
      "description", self->priv->description,
      "content-type", self->priv->content_type,
      NULL);
  g_signal_connect (ft, "remote-accepted",
      G_CALLBACK (remote_accepted_cb), self);
  g_signal_connect (ft, "error", G_CALLBACK (error_cb), self);

  self->priv->ft = ft;

  g_signal_connect (ft, "transferred-chunk",
      G_CALLBACK (ft_transferred_chunk_cb), self);

  gibber_file_transfer_set_size (ft, self->priv->size);

  gibber_file_transfer_offer (ft);
}

static void
xmpp_connection_manager_new_connection_cb (SalutXmppConnectionManager *mgr,
                                           GibberXmppConnection *connection,
                                           SalutContact *contact,
                                           gpointer user_data)
{
  SalutFileTransferChannel *channel = user_data;

  channel->priv->xmpp_connection = g_object_ref (connection);
  salut_xmpp_connection_manager_take_connection (mgr, connection);
  g_signal_handlers_disconnect_by_func (mgr,
      xmpp_connection_manager_new_connection_cb, user_data);
  send_file_offer (channel);
}

gboolean
salut_file_transfer_channel_received_file_offer (SalutFileTransferChannel *self,
                                        GibberXmppStanza *stanza,
                                        GibberXmppConnection *conn)
{
  GibberFileTransfer *ft;

  salut_xmpp_connection_manager_take_connection (
      self->priv->xmpp_connection_manager , conn);
  ft = gibber_file_transfer_new_from_stanza (stanza, conn);

  if (ft == NULL)
    {
      /* Reply with an error */
      GibberXmppStanza *reply;

      reply = gibber_iq_helper_new_error_reply (stanza, XMPP_ERROR_BAD_REQUEST,
          "failed to parse file offer");
      gibber_xmpp_connection_send (conn, reply, NULL);
      return FALSE;
    }

  g_signal_connect (ft, "error", G_CALLBACK (error_cb), self);

  DEBUG ("Received file offer with id '%s'", ft->id);

  self->priv->ft = ft;

  self->priv->filename = g_strdup (ft->filename);
  self->priv->size = gibber_file_transfer_get_size (ft);
  self->priv->description = g_strdup (ft->description);
  self->priv->content_type = g_strdup (ft->content_type);

  return TRUE;
}

static void
salut_file_transfer_channel_set_state (SalutSvcChannelTypeFileTransfer *iface,
                              SalutFileTransferState state,
                              SalutFileTransferStateChangeReason reason)
{
  SalutFileTransferChannel *self = SALUT_FILE_TRANSFER_CHANNEL (iface);

  self->priv->state = state;
  salut_svc_channel_type_file_transfer_emit_file_transfer_state_changed (iface,
      state, reason);
}

static void
ft_transferred_chunk_cb (GibberFileTransfer *ft,
                         guint64 count,
                         SalutFileTransferChannel *self)
{
  SalutSvcChannelTypeFileTransfer *iface = SALUT_SVC_CHANNEL_TYPE_FILE_TRANSFER (self);
  GTimeVal timeval;

  self->priv->transferred_bytes += count;

  g_get_current_time (&timeval);

  /* Only emit the TransferredBytes signal if it has been one second since its
   * last emission, OR if the transfer has finished.
   */
  if (timeval.tv_sec >= (self->priv->last_transferred_bytes_emitted + 1)
      || self->priv->transferred_bytes == self->priv->size)
    {
      salut_svc_channel_type_file_transfer_emit_transferred_bytes_changed (
          iface, self->priv->transferred_bytes);
      self->priv->last_transferred_bytes_emitted = timeval.tv_sec;
    }
}

static gboolean
check_address_and_access_control (SalutFileTransferChannel *self,
                                  TpSocketAddressType address_type,
                                  TpSocketAccessControl access_control,
                                  const GValue *access_control_param,
                                  GError **error)
{
  GArray *access;
  guint i;

  /* Do we support this AddressType? */
  access = g_hash_table_lookup (self->priv->available_socket_types,
      GUINT_TO_POINTER (address_type));
  if (access == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "AddressType %u is not implemented", address_type);
      return FALSE;
    }

  /* Do we support this AccesControl? */
  for (i = 0; i < access->len; i++)
    {
      TpSocketAccessControl control;

      control = g_array_index (access, TpSocketAccessControl, i);
      if (control == access_control)
        return TRUE;
    }

  g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
      "AccesControl %u is not implemented with AddressType %u",
      access_control, address_type);

  return FALSE;
}

/**
 * salut_file_transfer_channel_accept_file
 *
 * Implements D-Bus method AcceptFile
 * on interface org.freedesktop.Telepathy.Channel.Type.File
 */
static void
salut_file_transfer_channel_accept_file (SalutSvcChannelTypeFileTransfer *iface,
                                guint address_type,
                                guint access_control,
                                const GValue *access_control_param,
                                guint64 offset,
                                DBusGMethodInvocation *context)
{
  SalutFileTransferChannel *self = SALUT_FILE_TRANSFER_CHANNEL (iface);
  GError *error = NULL;
  GValue out_address = { 0 };
  GibberFileTransfer *ft;

  ft = self->priv->ft;
  if (ft == NULL)
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }

  if (self->priv->state != SALUT_FILE_TRANSFER_STATE_LOCAL_PENDING)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
        "State is not local pending; cannot accept file");
      dbus_g_method_return_error (context, error);
      return;
    }

  if (!check_address_and_access_control (self, address_type, access_control,
        access_control_param, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  g_signal_connect (ft, "finished", G_CALLBACK (ft_finished_cb), self);
  g_signal_connect (ft, "transferred-chunk",
      G_CALLBACK (ft_transferred_chunk_cb), self);
  g_signal_connect (ft, "canceled", G_CALLBACK (ft_remote_canceled_cb), self);

  if (!setup_local_socket (self))
    {
      DEBUG ("Could not set up local socket");
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Could not set up local socket");
      dbus_g_method_return_error (context, error);
    }

  DEBUG ("local socket %s", self->priv->socket_path);

  salut_file_transfer_channel_set_state (iface,
      SALUT_FILE_TRANSFER_STATE_ACCEPTED,
      SALUT_FILE_TRANSFER_STATE_CHANGE_REASON_REQUESTED);

  g_value_init (&out_address, G_TYPE_STRING);
  g_value_set_string (&out_address, self->priv->socket_path);

  salut_svc_channel_type_file_transfer_return_from_accept_file (context,
      &out_address);

  self->priv->initial_offset = 0;
  salut_file_transfer_channel_set_state (iface, SALUT_FILE_TRANSFER_STATE_OPEN,
      SALUT_FILE_TRANSFER_STATE_CHANGE_REASON_NONE);
}

/**
 * salut_file_transfer_channel_offer_file
 *
 * Implements D-Bus method OfferFile
 * on interface org.freedesktop.Telepathy.Channel.Type.File
 */
static void
salut_file_transfer_channel_offer_file (SalutSvcChannelTypeFileTransfer *iface,
                               guint address_type,
                               guint access_control,
                               const GValue *access_control_param,
                               DBusGMethodInvocation *context)
{
  SalutFileTransferChannel *self = SALUT_FILE_TRANSFER_CHANNEL (iface);
  GibberXmppConnection *connection = NULL;
  SalutXmppConnectionManagerRequestConnectionResult request_result;
  GError *error = NULL;
  SalutFileTransferChannel *channel = SALUT_FILE_TRANSFER_CHANNEL (iface);
  GValue out_address = { 0 };

  if (self->priv->state != SALUT_FILE_TRANSFER_STATE_NOT_OFFERED)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "State is not not offered; cannot offer file");
      dbus_g_method_return_error (context, error);
      return;
    }

  if (!check_address_and_access_control (self, address_type, access_control,
        access_control_param, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  g_assert (!CHECK_STR_EMPTY (channel->priv->filename));
  g_assert (channel->priv->size != SALUT_UNDEFINED_FILE_SIZE);

  DEBUG ("Offering file transfer");

  request_result = salut_xmpp_connection_manager_request_connection (
      channel->priv->xmpp_connection_manager, channel->priv->contact,
      &connection, &error);

  if (request_result ==
      SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_DONE)
    {
      channel->priv->xmpp_connection = connection;
      send_file_offer (channel);
    }
  else if (request_result ==
      SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_PENDING)
    {
      g_signal_connect (channel->priv->xmpp_connection_manager,
          "new-connection",
          G_CALLBACK (xmpp_connection_manager_new_connection_cb), channel);
    }
  else
    {
      DEBUG ("Request connection failed");
      g_set_error (&error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
        "Request connection failed");
      dbus_g_method_return_error (context, error);
      return;
    }

  if (!setup_local_socket (self))
    {
      DEBUG ("Could not set up local socket");
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Could not set up local socket");
      dbus_g_method_return_error (context, error);
    }

  g_value_init (&out_address, G_TYPE_STRING);
  g_value_set_string (&out_address, channel->priv->socket_path);

  salut_file_transfer_channel_set_state (iface,
      SALUT_FILE_TRANSFER_STATE_REMOTE_PENDING,
      SALUT_FILE_TRANSFER_STATE_CHANGE_REASON_REQUESTED);

  salut_svc_channel_type_file_transfer_return_from_offer_file (context,
      &out_address);
}

static void
file_transfer_iface_init (gpointer g_iface,
                          gpointer iface_data)
{
  SalutSvcChannelTypeFileTransferClass *klass =
      (SalutSvcChannelTypeFileTransferClass *)g_iface;

#define IMPLEMENT(x) salut_svc_channel_type_file_transfer_implement_##x (\
    klass, salut_file_transfer_channel_##x)
  IMPLEMENT (accept_file);
  IMPLEMENT (offer_file);
#undef IMPLEMENT
}

static const gchar *
get_local_unix_socket_path (SalutFileTransferChannel *self)
{
  gchar *path = NULL;
  gint32 random;
  gchar *random_str;
  struct stat buf;

  while (TRUE)
    {
      random = g_random_int_range (0, G_MAXINT32);
      random_str = g_strdup_printf ("tp-ft-%i", random);
      path = g_build_filename (g_get_tmp_dir (), random_str, NULL);
      g_free (random_str);

      if (g_stat (path, &buf) != 0)
        break;

      g_free (path);
    }

  if (self->priv->socket_path)
    g_free (self->priv->socket_path);

  self->priv->socket_path = path;

  return path;
}

/*
 * Return a GIOChannel for the local unix socket path.
 */
static GIOChannel *
get_socket_channel (SalutFileTransferChannel *self)
{
  gint fd;
  const gchar *path;
  size_t path_len;
  struct sockaddr_un addr;
  GIOChannel *io_channel;

  path = get_local_unix_socket_path (self);

  fd = socket (PF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    {
      DEBUG("socket() failed");
      return NULL;
    }

  memset (&addr, 0, sizeof (addr));
  addr.sun_family = AF_UNIX;
  path_len = strlen (path);
  strncpy (addr.sun_path, path, path_len);
  g_unlink (path);

  if (bind (fd, (struct sockaddr*) &addr,
        G_STRUCT_OFFSET (struct sockaddr_un, sun_path) + path_len) < 0)
    {
      DEBUG ("bind failed");
      close (fd);
      return NULL;
    }

  if (listen (fd, 1) < 0)
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

  ft = SALUT_FILE_TRANSFER_CHANNEL (user_data)->priv->ft;

  g_assert (ft != NULL);

  if (condition & G_IO_IN)
    {
      DEBUG ("Client connected to local socket");

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
        gibber_file_transfer_send (ft, channel);
      g_io_channel_unref (channel);
    }

  return FALSE;
}

static gboolean
setup_local_socket (SalutFileTransferChannel *self)
{
  GIOChannel *io_channel;

  io_channel = get_socket_channel (self);
  if (io_channel == NULL)
    {
      return FALSE;
    }

  g_io_add_watch (io_channel, G_IO_IN | G_IO_HUP,
      accept_local_socket_connection, self);
  g_io_channel_unref (io_channel);

  return TRUE;
}

