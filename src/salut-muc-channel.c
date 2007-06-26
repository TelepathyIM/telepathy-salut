/*
 * salut-muc-channel.c - Source for SalutMucChannel
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
#include <arpa/inet.h>

#include <string.h>

#define DEBUG_FLAG DEBUG_MUC
#include "debug.h"

#include "salut-muc-channel.h"
#include "salut-muc-channel-signals-marshal.h"

#include <gibber/gibber-transport.h>

#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/util.h>

#include <gibber/gibber-muc-connection.h>

#include "salut-connection.h"
#include "salut-im-manager.h"
#include "salut-avahi-entry-group.h"

#include "namespaces.h"
#include "text-helper.h"

static void
channel_iface_init(gpointer g_iface, gpointer iface_data);
static void
text_iface_init(gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE(SalutMucChannel, salut_muc_channel, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE(TP_TYPE_CHANNEL_IFACE, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
        tp_group_mixin_iface_init);
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CHANNEL_TYPE_TEXT, text_iface_init);
)

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_MUCCONNECTION,
  PROP_CONNECTION,
  PROP_IM_MANAGER,
  PROP_NAME,
  PROP_CLIENT,
  LAST_PROPERTY
};

/* private structure */
typedef struct _SalutMucChannelPrivate SalutMucChannelPrivate;

struct _SalutMucChannelPrivate
{
  gboolean dispose_has_run;
  gchar *object_path;
  TpHandle handle;
  SalutConnection *connection;
  SalutImManager *im_manager;
  GibberMucConnection *muc_connection;
  gchar *muc_name;
  SalutAvahiClient *client;
  SalutAvahiEntryGroup *muc_group;
  SalutAvahiEntryGroupService *service;
};

#define SALUT_MUC_CHANNEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_MUC_CHANNEL, SalutMucChannelPrivate))

/* Callback functions */
static gboolean salut_muc_channel_send_stanza(SalutMucChannel *self, 
                                              guint type, 
                                              const gchar *text,
                                              GibberXmppStanza *stanza,
                                              GError **error);
static void salut_muc_channel_received_stanza(GibberMucConnection *conn,
                                              const gchar *sender,
                                              GibberXmppStanza *stanza,
                                              gpointer user_data);
static gboolean
salut_muc_channel_connect(SalutMucChannel *channel, GError **error);
static void salut_muc_channel_disconnected(GibberTransport *transport,
                                             gpointer user_data);

static void
salut_muc_channel_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  SalutMucChannel *chan = SALUT_MUC_CHANNEL (object);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_NAME:
      g_value_set_string(value, priv->muc_name);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_TEXT);
      break;
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, TP_HANDLE_TYPE_ROOM);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, priv->handle);
      break;
    case PROP_CONNECTION:
      g_value_set_object (value, priv->connection);
      break;
    case PROP_IM_MANAGER:
      g_value_set_object (value, priv->im_manager);
      break;
    case PROP_MUCCONNECTION:
      g_value_set_object (value, priv->muc_connection);
      break;
    case PROP_CLIENT:
      g_value_set_object (value, priv->client);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
salut_muc_channel_set_property (GObject     *object,
                                guint        property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  SalutMucChannel *chan = SALUT_MUC_CHANNEL (object);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (chan);
  const gchar *tmp;

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_NAME:
      g_free (priv->muc_name);
      priv->muc_name = g_value_dup_string (value);
      break;
    case PROP_HANDLE:
      priv->handle = g_value_get_uint (value);
      break;
    case PROP_CONNECTION:
      priv->connection = g_value_get_object (value);
      break;
    case PROP_IM_MANAGER:
      priv->im_manager = g_value_get_object (value);
      break;
    case PROP_MUCCONNECTION:
      priv->muc_connection = g_value_get_object (value);
      break;
    case PROP_HANDLE_TYPE:
      g_assert(g_value_get_uint(value) == 0 
               || g_value_get_uint(value) == TP_HANDLE_TYPE_ROOM);
      break;
    case PROP_CHANNEL_TYPE:
      tmp = g_value_get_string(value);
      g_assert(tmp == NULL 
               || !tp_strdiff(g_value_get_string(value),
                       TP_IFACE_CHANNEL_TYPE_TEXT));
      break;
    case PROP_CLIENT:
      priv->client = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}



static GObject *
salut_muc_channel_constructor (GType type, guint n_props,
                              GObjectConstructParam *props) {
  GObject *obj;
  DBusGConnection *bus;
  SalutMucChannelPrivate *priv;
  TpBaseConnection *base_conn;
  TpHandleRepoIface *handle_repo;
  TpHandleRepoIface *contact_repo;

  /* Parent constructor chain */
  obj = G_OBJECT_CLASS(salut_muc_channel_parent_class)->
        constructor(type, n_props, props);

  priv = SALUT_MUC_CHANNEL_GET_PRIVATE (SALUT_MUC_CHANNEL (obj));

  /* Ref our handle */
  base_conn = TP_BASE_CONNECTION(priv->connection);

  handle_repo = tp_base_connection_get_handles(base_conn, 
      TP_HANDLE_TYPE_ROOM);

  tp_handle_ref(handle_repo, priv->handle);

  /* Text mixin initialisation */
  contact_repo = tp_base_connection_get_handles(base_conn, 
                                                TP_HANDLE_TYPE_CONTACT);
  tp_text_mixin_init(obj, G_STRUCT_OFFSET(SalutMucChannel, text),
                     contact_repo);

  tp_text_mixin_set_message_types(obj,
                               TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
                               TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION,
                               G_MAXUINT);

  /* Connect to the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object(bus, priv->object_path, obj);

  tp_group_mixin_init(obj, 
      G_STRUCT_OFFSET(SalutMucChannel, group), 
      contact_repo, base_conn->self_handle);

  tp_group_mixin_change_flags(obj, 
       TP_CHANNEL_GROUP_FLAG_CAN_ADD|TP_CHANNEL_GROUP_FLAG_MESSAGE_ADD, 0);

  return obj;
}

static void
salut_muc_channel_init (SalutMucChannel *self)
{
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);
  /* allocate any data required by the object here */
  priv->object_path = NULL;
  priv->connection = NULL;
  priv->muc_name = NULL;
  priv->client = NULL;
  priv->service = NULL;
}

static void salut_muc_channel_dispose (GObject *object);
static void salut_muc_channel_finalize (GObject *object);

static void 
invitation_append_parameter(gpointer key, gpointer value, gpointer data) {
  GibberXmppNode *node = (GibberXmppNode *)data;
 gibber_xmpp_node_add_child_with_content(node, (gchar *)key, (gchar *)value);
}

static GibberXmppStanza *
create_invitation(SalutMucChannel *self, TpHandle handle, const gchar *message) { 
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION(priv->connection);
  TpHandleRepoIface *contact_repo = 
      tp_base_connection_get_handles(base_connection, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo = 
      tp_base_connection_get_handles(base_connection, TP_HANDLE_TYPE_ROOM);
  GibberXmppStanza *msg;
  GibberXmppNode *x_node, *invite_node;

  const gchar *name = tp_handle_inspect(contact_repo, handle);

  msg = gibber_xmpp_stanza_new("message");

  gibber_xmpp_node_set_attribute(msg->node, "from", priv->connection->name); 
  gibber_xmpp_node_set_attribute(msg->node, "to", name); 

  gibber_xmpp_node_add_child_with_content(msg->node, "body", 
                                         "You got a chatroom invitation");
  x_node = gibber_xmpp_node_add_child_ns(msg->node, "x", NS_LLMUC);

  invite_node = gibber_xmpp_node_add_child(x_node, "invite");
  gibber_xmpp_node_set_attribute(invite_node, "protocol", 
        gibber_muc_connection_get_protocol(priv->muc_connection));

  if (message != NULL && *message != '\0') {
    gibber_xmpp_node_add_child_with_content(invite_node, "reason", message);
  }

  gibber_xmpp_node_add_child_with_content(invite_node, "roomname", 
                       tp_handle_inspect(room_repo, priv->handle));
  g_hash_table_foreach(
    (GHashTable *)gibber_muc_connection_get_parameters(priv->muc_connection), 
    invitation_append_parameter, invite_node);

  return msg;
}

static gboolean
muc_channel_publish_service (SalutMucChannel *self)
{
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);
  AvahiStringList *txt_record = NULL;
  GError *error = NULL;
  const GHashTable *params;
  const gchar *address, *port_str;
  gchar *host = NULL;
  guint16 port;
  struct in_addr addr;

  g_assert (priv->muc_group == NULL);
  g_assert (priv->service == NULL);

  priv->muc_group = salut_avahi_entry_group_new ();

  if (!salut_avahi_entry_group_attach (priv->muc_group, priv->client, &error))
    {
      DEBUG ("entry group attach failed: %s", error->message);
      goto publish_service_error;
    }

  params = gibber_muc_connection_get_parameters (priv->muc_connection);
  address = g_hash_table_lookup ((GHashTable *) params, "address");
  if (address == NULL)
    {
      DEBUG ("can't find connection address");
      goto publish_service_error;
    }
  port_str = g_hash_table_lookup ((GHashTable *) params, "port");
  if (port_str == NULL)
    {
      DEBUG ("can't find connection port");
      goto publish_service_error;
    }

  memset (&addr, 0, sizeof (addr));
  /* XXX that won't work with IPV6 for sure */
  if (inet_pton (AF_INET, address, &addr) <= 0)
    {
      DEBUG ("can't convert address %s", address);
      goto publish_service_error;
    }

  host = g_strdup_printf ("%s._salut-room._udp.local", priv->muc_name);

  /* Add the A record */
  if (!salut_avahi_entry_group_add_record (priv->muc_group, 0, host,
        AVAHI_DNS_TYPE_A, AVAHI_DEFAULT_TTL_HOST_NAME,
        (const void *) &(addr.s_addr), sizeof (addr.s_addr), &error))
    {
      DEBUG ("add A record failed: %s", error->message);
      goto publish_service_error;
    }

  port = atoi (port_str);

  txt_record = avahi_string_list_new ("txtvers=1", NULL);

  /* We shouldn't add the service but manually create the SRV record so
   * we'll be able to allow multiple announcers */
  priv->service = salut_avahi_entry_group_add_service_full_strlist (
      priv->muc_group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0, priv->muc_name,
      "_salut-room._udp", NULL, host, port, &error, txt_record);
  if (priv->service == NULL)
    {
      DEBUG ("add service failed: %s", error->message);
      goto publish_service_error;
    }

  if (!salut_avahi_entry_group_commit (priv->muc_group, &error))
    {
      DEBUG ("entry group commit failed: %s", error->message);
      goto publish_service_error;
    }

  DEBUG ("service created");
  avahi_string_list_free (txt_record);
  g_free (host);
  return TRUE;

publish_service_error:
  if (priv->muc_group != NULL)
    {
      g_object_unref (priv->muc_group);
      priv->muc_group = NULL;
    }

  priv->service = NULL;

  if (txt_record != NULL)
    avahi_string_list_free (txt_record);

  if (host != NULL)
    g_free (host);

  if (error != NULL)
    g_error_free (error);
  return FALSE;
}

static gboolean 
muc_channel_add_member(GObject *iface, TpHandle handle, 
                       const gchar *message, GError **error) {
  SalutMucChannel *self = SALUT_MUC_CHANNEL(iface);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION(priv->connection);
  SalutImChannel *im_channel;
  GibberXmppStanza *stanza;

  if (handle == base_connection->self_handle) {
    TpIntSet *empty;
    TpIntSet *add;
    gboolean ret = TRUE;
    empty = tp_intset_new();
    add = tp_intset_new();
    tp_intset_add(add, handle);
    /* Add to members */
    if (salut_muc_channel_connect(self, NULL)) {
      tp_group_mixin_change_members(G_OBJECT(self),
          message, add, empty, empty, empty, base_connection->self_handle,
          TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);
    } else {
      g_set_error(error, 
                  TP_ERRORS, TP_ERROR_NETWORK_ERROR, 
                  "Failed to connect to the group");
      ret = FALSE;
    }
    tp_intset_destroy(empty);
    tp_intset_destroy(add);
    muc_channel_publish_service (self);
    return ret;
  }

  im_channel = salut_im_manager_get_channel_for_handle(priv->im_manager,
                                                       handle);
  if (im_channel == NULL) {
    *error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, 
                           "Couldn't contact the contact");
    return FALSE;
  }
  DEBUG("Trying to add handle %u to %s over channel: %p\n", 
        handle ,priv->object_path, im_channel);

  stanza = create_invitation(self, handle, message);
  salut_im_channel_send_stanza(im_channel, stanza);

  g_object_unref(im_channel);
  return TRUE;
}

static void
salut_muc_channel_class_init (SalutMucChannelClass *salut_muc_channel_class) {
  GObjectClass *object_class = G_OBJECT_CLASS (salut_muc_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_muc_channel_class, sizeof (SalutMucChannelPrivate));

  object_class->dispose = salut_muc_channel_dispose;
  object_class->finalize = salut_muc_channel_finalize;

  object_class->constructor = salut_muc_channel_constructor;
  object_class->get_property = salut_muc_channel_get_property;
  object_class->set_property = salut_muc_channel_set_property;

  g_object_class_override_property (object_class, PROP_OBJECT_PATH, 
                                    "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE, 
                                    "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE, 
                                    "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");

  param_spec = g_param_spec_string ("name",
                                    "Name of the muc group",
                                    "The name of the muc group",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, 
                                   PROP_NAME, param_spec);

  param_spec = g_param_spec_object ("muc connection", 
                                    "The GibberMucConnection",
                                    "muc connection  object",
                                    GIBBER_TYPE_MUC_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, 
                                   PROP_MUCCONNECTION, param_spec);
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
  param_spec = g_param_spec_object ("im-manager", 
                                    "SalutIm manager",
                                    "Salut Im manager to use to "
                                    "contact the contacts",
                                    SALUT_TYPE_IM_MANAGER,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, 
                                   PROP_IM_MANAGER, param_spec);

  param_spec = g_param_spec_object (
      "client",
      "SalutAvahiClient object",
      "Salut Avahi client used with the"
      " connection that owns this MUC channel",
      SALUT_TYPE_AVAHI_CLIENT,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class,
      PROP_CLIENT, param_spec);

  tp_text_mixin_class_init(object_class,
                           G_STRUCT_OFFSET(SalutMucChannelClass, text_class));

  tp_group_mixin_class_init(object_class, 
    G_STRUCT_OFFSET(SalutMucChannelClass, group_class),
    muc_channel_add_member, NULL);
}

void
salut_muc_channel_dispose (GObject *object)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (object);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_signal_handlers_disconnect_matched (priv->muc_connection,
      G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);

  if (priv->muc_connection != NULL) {
    g_object_unref(priv->muc_connection);
    priv->muc_connection = NULL;
  }

  if (priv->muc_group != NULL)
    {
      g_object_unref (priv->muc_group);
      priv->muc_group = NULL;
    }

  /* release any references held by the object here */
  if (G_OBJECT_CLASS (salut_muc_channel_parent_class)->dispose)
    G_OBJECT_CLASS (salut_muc_channel_parent_class)->dispose (object);
}

void
salut_muc_channel_finalize (GObject *object)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL (object);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  g_free(priv->object_path);
  g_free (priv->muc_name);

  tp_text_mixin_finalize(object);
  tp_group_mixin_finalize(object);

  G_OBJECT_CLASS (salut_muc_channel_parent_class)->finalize (object);
}

gboolean
salut_muc_channel_invited(SalutMucChannel *self, TpHandle invitor, 
                          const gchar *stanza, GError **error) {
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(self);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION(priv->connection);
  TpHandleRepoIface *contact_repo = 
      tp_base_connection_get_handles(base_connection, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo = 
      tp_base_connection_get_handles(base_connection, TP_HANDLE_TYPE_ROOM);
  gboolean ret = TRUE;

  /* Got invited to this muc channel */
  DEBUG("Got an invitation to %s from %s", 
    tp_handle_inspect(room_repo, priv->handle),
    tp_handle_inspect(contact_repo, invitor)
    );

  /* If we are already a member, no further actions are needed */
  if (tp_handle_set_is_member(self->group.members, 
                              base_connection->self_handle)) {
    return TRUE;
  }

  if (invitor == base_connection->self_handle) {
    /* Invited ourselves, go straight to members */
    gboolean r;
    GArray *members =  g_array_sized_new (FALSE, FALSE, sizeof(TpHandle), 1);
    g_array_append_val(members, base_connection->self_handle);
    r = tp_group_mixin_add_members(G_OBJECT(self), members, "", error);
    g_assert(r);
    g_array_free(members, TRUE);
  } else {
    TpIntSet *empty = tp_intset_new();
    TpIntSet *local_pending = tp_intset_new();

    g_assert(stanza != NULL);

    tp_intset_add(local_pending, base_connection->self_handle);
    tp_group_mixin_change_members(G_OBJECT(self), stanza,
                                  empty, empty,
                                  local_pending, empty,
                                  invitor,
                                  TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);
    tp_intset_destroy(local_pending);
    tp_intset_destroy(empty);
  }
  return ret;
}

/* Private functions */
static gboolean 
salut_muc_channel_send_stanza(SalutMucChannel *self, guint type, 
                              const gchar *text,
                              GibberXmppStanza *stanza,
                              GError **error) 
{
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(self);

  if (!gibber_muc_connection_send(priv->muc_connection, stanza, error)) {
    tp_svc_channel_type_text_emit_send_error(self, 
       TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN, time(NULL), type, text);
    return FALSE;
  }

  tp_svc_channel_type_text_emit_sent(self, time(NULL), type, text);
  return TRUE;
}

static void
salut_muc_channel_change_members(SalutMucChannel *self, 
                                 TpHandle from_handle, 
                                 gboolean joining) {
  TpIntSet *empty, *changes;

  empty = tp_intset_new();
  changes = tp_intset_new();
  tp_intset_add(changes, from_handle);
  tp_group_mixin_change_members(G_OBJECT(self),
                                "",
                                joining ? changes : empty,
                                joining ? empty : changes,
                                empty, empty,
                                from_handle,
                                TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
  tp_intset_destroy(changes);
  tp_intset_destroy(empty);
}

static void 
salut_muc_channel_received_stanza(GibberMucConnection *conn,
                                  const gchar *sender,
                                  GibberXmppStanza *stanza,
                                  gpointer user_data) {
  SalutMucChannel *self = SALUT_MUC_CHANNEL(user_data);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(self);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION(priv->connection);
  TpHandleRepoIface *contact_repo = 
      tp_base_connection_get_handles(base_connection, TP_HANDLE_TYPE_CONTACT);

  const gchar *from, *to, *body, *body_offset;
  TpChannelTextMessageType msgtype;
  TpHandle from_handle;

  to = gibber_xmpp_node_get_attribute(stanza->node, "to");
  if (strcmp(to, priv->muc_name)) {
    DEBUG("Stanza to another muc group, discarding");
    return;
  }

  if (!text_helper_parse_incoming_message(stanza, &from, &msgtype,
                                          &body, &body_offset)) {
    DEBUG("Couldn't parse stanza");
    return;
  }

  if (body == NULL) {
    DEBUG("Message with an empty body");
    return;
  }

  from_handle = tp_handle_lookup(contact_repo, sender, NULL, NULL);
  if (from_handle == 0) {
    /* FIXME, unknown contact.. Need some way to handle this safely,
     * just adding the contact is somewhat scary */
    DEBUG("Got stanza from unknown contact, discarding");
    return;
  }

  /* FIXME validate the from and the to */
  tp_text_mixin_receive(G_OBJECT(self), msgtype, from_handle,
      time(NULL), body_offset);
}

static void
salut_muc_channel_new_sender(GibberMucConnection *connection, gchar *sender,
                             gpointer user_data) {
  SalutMucChannel *self = SALUT_MUC_CHANNEL(user_data);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(self);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION(priv->connection);
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles(base_connection, TP_HANDLE_TYPE_CONTACT);
  TpHandle handle;

  handle = tp_handle_lookup(contact_repo, sender, NULL, NULL);
  /* FIXME what to do with invalid handles */
  if (handle == 0) {
    DEBUG("New sender, but unknown contact");
    return;
  }

  salut_muc_channel_change_members(self, handle, TRUE);
}

static void
salut_muc_channel_lost_sender(GibberMucConnection *connection, gchar *sender,
                             gpointer user_data) {
  SalutMucChannel *self = SALUT_MUC_CHANNEL(user_data);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(self);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION(priv->connection);
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles(base_connection, TP_HANDLE_TYPE_CONTACT);
  TpHandle handle;

  handle = tp_handle_lookup(contact_repo, sender, NULL, NULL);
  /* FIXME what to do with invalid handles */
  if (handle == 0) {
    DEBUG("Lost sender, but unknown contact");
    return;
  }

  salut_muc_channel_change_members(self, handle, FALSE);
}

static gboolean
salut_muc_channel_connect(SalutMucChannel *channel, GError **error) {
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(channel);

  g_signal_connect(priv->muc_connection, "received-stanza",
                   G_CALLBACK(salut_muc_channel_received_stanza), channel);

  g_signal_connect(priv->muc_connection, "disconnected",
                   G_CALLBACK(salut_muc_channel_disconnected), channel);

  g_signal_connect(priv->muc_connection, "new-sender",
                   G_CALLBACK(salut_muc_channel_new_sender), channel);

  g_signal_connect(priv->muc_connection, "lost-sender",
                   G_CALLBACK(salut_muc_channel_lost_sender), channel);

  return gibber_muc_connection_connect(priv->muc_connection, error);
}

static void
salut_muc_channel_disconnected(GibberTransport *transport,
                                             gpointer user_data) {
  SalutMucChannel *self = SALUT_MUC_CHANNEL(user_data);
  tp_svc_channel_emit_closed(self);
}

/* channel interfaces */
/**
 * salut_muc_channel_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_muc_channel_get_interfaces (TpSvcChannel *iface, 
                                  DBusGMethodInvocation *context) {
  const char *interfaces[] = { TP_IFACE_CHANNEL_INTERFACE_GROUP,  NULL };
  
  tp_svc_channel_return_from_get_interfaces (context, interfaces);
}


/**
 * salut_muc_channel_get_handle
 *
 * Implements D-Bus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void 
salut_muc_channel_get_handle (TpSvcChannel *iface, 
                              DBusGMethodInvocation *context)
{
  SalutMucChannel *self = SALUT_MUC_CHANNEL(iface);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);

  tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_ROOM,
                                         priv->handle);
}
/**
 * salut_muc_channel_get_channel_type
 *
 * Implements D-Bus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void 
salut_muc_channel_get_channel_type (TpSvcChannel *iface, 
                                   DBusGMethodInvocation *context) {
  tp_svc_channel_return_from_get_channel_type(context,
      TP_IFACE_CHANNEL_TYPE_TEXT);
}

/**
 * salut_muc_channel_close
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void 
salut_muc_channel_close (TpSvcChannel *iface, DBusGMethodInvocation *context) {
  SalutMucChannel *self = SALUT_MUC_CHANNEL(iface);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);

  gibber_muc_connection_disconnect(priv->muc_connection);

  tp_svc_channel_return_from_close(context);
}

static void
channel_iface_init(gpointer g_iface, gpointer iface_data) {
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, salut_muc_channel_##x)
  IMPLEMENT(close);
  IMPLEMENT(get_channel_type);
  IMPLEMENT(get_handle);
  IMPLEMENT(get_interfaces);
#undef IMPLEMENT
}


/**
 * salut_muc_channel_send
 *
 * Implements D-Bus method Send
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void 
salut_muc_channel_send (TpSvcChannelTypeText *channel, 
                       guint type, const gchar * text, 
                       DBusGMethodInvocation *context) {
  SalutMucChannel *self = SALUT_MUC_CHANNEL(channel);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(self);
  GError *error = NULL;

  GibberXmppStanza *stanza = 
      text_helper_create_message_groupchat (priv->connection->name,
          priv->muc_name, type, text, &error);

  if (stanza == NULL) {
    dbus_g_method_return_error(context, error);
    g_error_free(error);
    return;
  }

  if (!salut_muc_channel_send_stanza(self, type, text, stanza, &error)) {
    g_object_unref(G_OBJECT(stanza));
    dbus_g_method_return_error(context, error);
    g_error_free(error);
  }

  g_object_unref(G_OBJECT(stanza));
  tp_svc_channel_type_text_return_from_send(context);
}

static void
text_iface_init(gpointer g_iface, gpointer iface_data) {
  TpSvcChannelTypeTextClass *klass = (TpSvcChannelTypeTextClass *)g_iface;

  tp_text_mixin_iface_init (g_iface, iface_data);
#define IMPLEMENT(x) tp_svc_channel_type_text_implement_##x (\
    klass, salut_muc_channel_##x)
  IMPLEMENT(send);
#undef IMPLEMENT
}

