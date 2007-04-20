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

#include "salut-muc-transport-iface.h"
#include "salut-muc-connection.h"

#include "salut-connection.h"
#include "salut-im-manager.h"

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
  SalutMucConnection *muc_connection;
  gchar *muc_name;
  guint presence_timeout_id;
};

#define SALUT_MUC_CHANNEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_MUC_CHANNEL, SalutMucChannelPrivate))

/* Callback functions */
static void
salut_muc_channel_send_presence(SalutMucChannel *self, gboolean joining);
static gboolean salut_muc_channel_send_stanza(SalutMucChannel *self, 
                                              guint type, 
                                              const gchar *text,
                                              GibberXmppStanza *stanza,
                                              GError **error);
static void salut_muc_channel_received_stanza(GibberXmppConnection *conn,
                                              GibberXmppStanza *stanza,
                                              gpointer user_data);
static void salut_muc_channel_received_presence(SalutMucChannel *channel, 
                                                GibberXmppStanza *stanza);
static gboolean 
salut_muc_channel_connect(SalutMucChannel *channel, GError **error);
static void salut_muc_channel_connected(GibberTransport *transport,
                                             gpointer user_data);
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
      g_object_ref(priv->connection);
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
  priv->presence_timeout_id = 0;
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
                                         "You got an chatroom invitation");
  x_node = gibber_xmpp_node_add_child_ns(msg->node, "x", NS_LLMUC);

  invite_node = gibber_xmpp_node_add_child(x_node, "invite");
  gibber_xmpp_node_set_attribute(invite_node, "protocol", 
        salut_muc_connection_get_protocol(priv->muc_connection));

  if (message != NULL && *message != '\0') {
    gibber_xmpp_node_add_child_with_content(invite_node, "reason", message);
  }

  gibber_xmpp_node_add_child_with_content(invite_node, "roomname", 
                       tp_handle_inspect(room_repo, priv->handle));
  g_hash_table_foreach(
    (GHashTable *)salut_muc_connection_get_parameters(priv->muc_connection), 
    invitation_append_parameter, invite_node);

  return msg;
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
                                    "The SalutMucConnection",
                                    "muc connection  object",
                                    G_TYPE_OBJECT,
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
  if (priv->presence_timeout_id != 0) {
    g_source_remove(priv->presence_timeout_id);
    priv->presence_timeout_id = 0;
  }

  if (priv->muc_connection != NULL) {
    g_object_unref(priv->muc_connection);
    priv->muc_connection = NULL;
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

  tp_text_mixin_finalize(object);
  tp_group_mixin_finalize(object);

  G_OBJECT_CLASS (salut_muc_channel_parent_class)->finalize (object);
}

void
salut_muc_channel_invited(SalutMucChannel *self, TpHandle invitor, 
                          const gchar *stanza) {
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(self);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION(priv->connection);
  TpHandleRepoIface *contact_repo = 
      tp_base_connection_get_handles(base_connection, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo = 
      tp_base_connection_get_handles(base_connection, TP_HANDLE_TYPE_ROOM);

  /* Got invited to this muc channel */
  DEBUG("Got an invitation to %s from %s", 
    tp_handle_inspect(room_repo, priv->handle),
    tp_handle_inspect(contact_repo, invitor)
    );

  /* If we are already a member, no further actions are needed */
  if (tp_handle_set_is_member(self->group.members, 
                              base_connection->self_handle)) {
    return;
  }
  
  if (invitor == base_connection->self_handle) {
    /* Invited ourselves, go straight to members */
    GError *error = NULL;
    GArray *members =  g_array_sized_new (FALSE, FALSE, sizeof(TpHandle), 1);
    g_array_append_val(members, base_connection->self_handle);
    tp_group_mixin_add_members(G_OBJECT(self),
        members, "", &error);
    g_array_free(members, TRUE);
  } else {
    TpIntSet *empty = tp_intset_new();
    TpIntSet *local_pending = tp_intset_new();
    tp_intset_add(local_pending, base_connection->self_handle);
    tp_group_mixin_change_members(G_OBJECT(self), stanza,
                                  empty, empty,
                                  local_pending, empty,
                                  invitor,
                                  TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);
    tp_intset_destroy(local_pending);
    tp_intset_destroy(empty);
  }
}

/* Private functions */
static gboolean 
salut_muc_channel_send_stanza(SalutMucChannel *self, guint type, 
                              const gchar *text,
                              GibberXmppStanza *stanza,
                              GError **error) 
{
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(self);

  if (!gibber_xmpp_connection_send(GIBBER_XMPP_CONNECTION(priv->muc_connection),
          stanza, error)) {
    tp_svc_channel_type_text_emit_send_error(self, 
       TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN, time(NULL), type, text);
    return FALSE;
  }

  tp_svc_channel_type_text_emit_sent(self, time(NULL), type, text);
  return TRUE;
}

static void
salut_muc_channel_send_presence(SalutMucChannel *self, 
                                gboolean joining) {
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(self);
  GibberXmppStanza *stanza;

  stanza = gibber_xmpp_stanza_new("presence");
  if (!joining) {
    gibber_xmpp_node_set_attribute(stanza->node, "type", "unavailable");
  }
  gibber_xmpp_node_set_attribute(stanza->node, "from", priv->connection->name);
  gibber_xmpp_node_set_attribute(stanza->node, "to", priv->muc_name);

  /* FIXME should disconnect if we couldn't sent */
  gibber_xmpp_connection_send(GIBBER_XMPP_CONNECTION(priv->muc_connection), 
                              stanza, NULL);
  g_object_unref(stanza);
}

static gboolean
salut_muc_channel_presence_timeout(gpointer data) {
  SalutMucChannel *self = SALUT_MUC_CHANNEL(data);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(self);

  salut_muc_channel_send_presence(self, TRUE);

  priv->presence_timeout_id = 0;
  return FALSE;
}

static void
salut_muc_channel_change_members(SalutMucChannel *self, 
                                 TpHandle from_handle, 
                                 gboolean joining) {
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(self);
  gboolean is_member;
  TpIntSet *empty, *changes;

  is_member = tp_handle_set_is_member(self->group.members, from_handle);
  if (is_member == joining) {
    return;
  }

  if (joining && priv->presence_timeout_id != 0) {
    /* New member joined start a timeout for our presence */
    priv->presence_timeout_id = 
      g_timeout_add(g_random_int_range(200, 2000),
                    salut_muc_channel_presence_timeout, self);
  }

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
salut_muc_channel_received_stanza(GibberXmppConnection *conn,
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

  if (!strcmp(stanza->node->name, "presence")) {
    salut_muc_channel_received_presence(self, stanza);
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

  from_handle = tp_handle_lookup(contact_repo, from, NULL, NULL);
  if (from_handle == 0) {
    /* FIXME, unknown contact.. Need some way to handle this safely,
     * just adding the contact is somewhat scary */
    DEBUG("Got stanza from unknown contact, discarding");
    return;
  }

  salut_muc_channel_change_members(self, from_handle, TRUE);
  /* FIXME validate the from and the to */
  /* FIXME fix the text-mixin to actually get a to */
  tp_text_mixin_receive(G_OBJECT(self), msgtype, from_handle, 
      time(NULL), body_offset);
}

static void salut_muc_channel_received_presence(SalutMucChannel *channel, 
                                                GibberXmppStanza *stanza) {
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(channel);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION(priv->connection);
  TpHandleRepoIface *contact_repo = 
      tp_base_connection_get_handles(base_connection, TP_HANDLE_TYPE_CONTACT);
  gboolean joining = TRUE;
  const gchar *type;
  const gchar *from;
  TpHandle from_handle;

  type = gibber_xmpp_node_get_attribute(stanza->node, "type");
  if (type != NULL && strcmp(type, "unavailable") == 0) {
    joining = FALSE;
  }

  from = gibber_xmpp_node_get_attribute(stanza->node, "from");
  if (from == NULL) {
    DEBUG("Presence without a from");
    return;
  }

  DEBUG("Presence from: %s (joining: %d)", from, joining); 

  from_handle = tp_handle_lookup(contact_repo, from, NULL, NULL);
  if (from_handle == 0) {
    DEBUG("Unknown contact");
    return;
  }

  salut_muc_channel_change_members(channel, from_handle, joining);
}

static gboolean
salut_muc_channel_dummy_timeout(gpointer data) {
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(data);
  priv->presence_timeout_id =  0;
  return FALSE;
}

static gboolean
salut_muc_channel_connect(SalutMucChannel *channel, GError **error) {
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(channel);
  GibberXmppConnection *conn;

  conn = GIBBER_XMPP_CONNECTION(priv->muc_connection);

  /* Transport is now owned by the xmpp connection */
  g_signal_connect(conn, "received-stanza",
                   G_CALLBACK(salut_muc_channel_received_stanza), channel);

  g_signal_connect(conn->transport, "connected", 
                   G_CALLBACK(salut_muc_channel_connected), channel);
  g_signal_connect(conn->transport, "disconnected", 
                   G_CALLBACK(salut_muc_channel_disconnected), channel);

  return salut_muc_connection_connect(priv->muc_connection, error);
}

static void 
salut_muc_channel_connected(GibberTransport *transport,
                                        gpointer user_data) {
  SalutMucChannel *self = SALUT_MUC_CHANNEL(user_data);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(self);

  g_signal_connect(transport, "disconnected", 
                   G_CALLBACK(salut_muc_channel_disconnected), self);

  /* FIXME race */
  g_assert(priv->presence_timeout_id  == 0);
  priv->presence_timeout_id = g_timeout_add(4000,
                                           salut_muc_channel_dummy_timeout, 
                                           self);
  g_assert(priv->presence_timeout_id != 0);
  salut_muc_channel_send_presence(self, TRUE);
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

  tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_CONTACT,
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

  DEBUG("Disposing muc channel: %d", priv->presence_timeout_id);

  if (priv->presence_timeout_id != 0) {
    g_source_remove(priv->presence_timeout_id);
    priv->presence_timeout_id = 0;
  }

  salut_muc_channel_send_presence(self, FALSE);
  gibber_transport_disconnect(
    GIBBER_XMPP_CONNECTION(priv->muc_connection)->transport);

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
      text_helper_create_message(priv->connection->name,
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

