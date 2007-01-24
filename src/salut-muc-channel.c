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
#include "salut-muc-channel-glue.h"

#include "salut-muc-transport-iface.h"
#include "salut-transport.h"
#include "salut-xmpp-connection.h"

#include "salut-connection.h"
#include "salut-im-manager.h"
#include "telepathy-interfaces.h"
#include "telepathy-helpers.h"
#include "telepathy-errors.h"
#include "tp-channel-iface.h"

#include "namespaces.h"

G_DEFINE_TYPE_WITH_CODE(SalutMucChannel, salut_muc_channel, G_TYPE_OBJECT,
                G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL));

/* signal enum */
enum
{
    CLOSED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_TRANSPORT,
  PROP_CONNECTION,
  PROP_IM_MANAGER,
  LAST_PROPERTY
};

/* private structure */
typedef struct _SalutMucChannelPrivate SalutMucChannelPrivate;

struct _SalutMucChannelPrivate
{
  gboolean dispose_has_run;
  gchar *object_path;
  Handle handle;
  SalutConnection *connection;
  SalutImManager *im_manager;
  SalutTransport *transport;
  SalutXmppConnection *xmpp_connection;
  gchar *muc_name;
  guint presence_timeout_id;
};

#define SALUT_MUC_CHANNEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_MUC_CHANNEL, SalutMucChannelPrivate))

/* Callback functions */
static void
salut_muc_channel_send_presence(SalutMucChannel *self, gboolean joining);
static gboolean salut_muc_channel_send_stanza(GObject *object, guint type, 
                                               const gchar *text,
                                               SalutXmppStanza *stanza,
                                               GError **error);
static void salut_muc_channel_received_stanza(SalutXmppConnection *conn,
                                              SalutXmppStanza *stanza,
                                              gpointer user_data);
static void salut_muc_channel_received_presence(SalutMucChannel *channel, 
                                                SalutXmppStanza *stanza);
static void salut_muc_channel_connected(SalutTransport *transport,
                                             gpointer user_data);
static void salut_muc_channel_disconnected(SalutMucTransportIface *iface,
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
    case PROP_TRANSPORT:
      g_value_set_object (value, priv->transport);
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

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
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
    case PROP_TRANSPORT:
      priv->transport = g_value_get_object (value);
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
  gboolean valid;
  SalutMucChannelPrivate *priv;

  /* Parent constructor chain */
  obj = G_OBJECT_CLASS(salut_muc_channel_parent_class)->
        constructor(type, n_props, props);

  priv = SALUT_MUC_CHANNEL_GET_PRIVATE (SALUT_MUC_CHANNEL (obj));

  /* Ref our handle */
  valid = handle_ref(priv->connection->handle_repo, TP_HANDLE_TYPE_ROOM, 
                     priv->handle);
  g_assert(valid);
  
  priv->xmpp_connection = salut_xmpp_connection_new(priv->transport);
  /* Transport is now owned by the xmpp connection */
  g_object_unref(priv->transport);

  g_signal_connect(priv->xmpp_connection, "received-stanza",
                   G_CALLBACK(salut_muc_channel_received_stanza), obj);

  g_signal_connect(priv->transport, "connected", 
                   G_CALLBACK(salut_muc_channel_connected), obj);
  g_signal_connect(priv->transport, "disconnected", 
                   G_CALLBACK(salut_muc_channel_disconnected), obj);

  /* FIXME catch errors */
  salut_muc_transport_iface_connect(SALUT_MUC_TRANSPORT_IFACE(priv->transport),
                                    NULL);

  /* Text mixin initialisation */
  text_mixin_init(obj, G_STRUCT_OFFSET(SalutMucChannel, text),
                  priv->connection->handle_repo);

  text_mixin_set_message_types(obj,
                               TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
                               TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION,
                               G_MAXUINT);

  /* Connect to the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object(bus, priv->object_path, obj);

  group_mixin_init(obj, G_STRUCT_OFFSET(SalutMucChannel, group), 
                    priv->connection->handle_repo, 
                    priv->connection->self_handle);
  group_mixin_change_flags(obj, 
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
  priv->muc_name = "blaat";
  priv->presence_timeout_id = 0;
}

static void salut_muc_channel_dispose (GObject *object);
static void salut_muc_channel_finalize (GObject *object);

static void 
invitation_append_parameter(gpointer key, gpointer value, gpointer data) {
  SalutXmppNode *node = (SalutXmppNode *)data;
 salut_xmpp_node_add_child_with_content(node, (gchar *)key, (gchar *)value);
}

static SalutXmppStanza *
create_invitation(SalutMucChannel *self, Handle handle, const gchar *message) { 
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);
  SalutXmppStanza *msg;
  SalutXmppNode *x_node, *invite_node;
  const gchar *name = handle_inspect(priv->connection->handle_repo,
                                     TP_HANDLE_TYPE_CONTACT, handle);

  msg = salut_xmpp_stanza_new("message");
  
  salut_xmpp_node_set_attribute(msg->node, "from", priv->connection->name); 
  salut_xmpp_node_set_attribute(msg->node, "to", name); 

  salut_xmpp_node_add_child_with_content(msg->node, "body", 
                                         "You got an chatroom invitation");
  x_node = salut_xmpp_node_add_child_ns(msg->node, "x", NS_LLMUC);

  invite_node = salut_xmpp_node_add_child(x_node, "invite");
  salut_xmpp_node_set_attribute(invite_node, "protocol", 
        salut_muc_transport_get_protocol(
          SALUT_MUC_TRANSPORT_IFACE(priv->transport)));
  if (message != NULL && *message != '\0') {
    salut_xmpp_node_add_child_with_content(invite_node, "reason", message);
  }
  salut_xmpp_node_add_child_with_content(invite_node, "roomname", 
                       handle_inspect(priv->connection->handle_repo,
                                      TP_HANDLE_TYPE_ROOM, priv->handle));
  g_hash_table_foreach(
    (GHashTable *)salut_muc_transport_get_parameters(
                                   SALUT_MUC_TRANSPORT_IFACE(priv->transport)), 
    invitation_append_parameter, invite_node);

  return msg;
}

static gboolean 
muc_channel_add_member(GObject *obj, Handle handle, 
                       const gchar *message, GError **error) {
  SalutMucChannel *self = SALUT_MUC_CHANNEL(obj);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);
  SalutImChannel *im_channel;
  SalutXmppStanza *stanza;

  if (handle == priv->connection->self_handle) {
    GIntSet *empty;
    GIntSet *add;
    empty = g_intset_new();
    add = g_intset_new();
    g_intset_add(add, handle);
    /* Add to members */
    group_mixin_change_members(G_OBJECT(self), message,
                             add, empty, empty, empty, 
                             priv->connection->self_handle,
                             TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);
    g_intset_destroy(empty);
    g_intset_destroy(add);
    return TRUE;
  }

  im_channel = salut_im_manager_get_channel_for_handle(priv->im_manager,
                                                       handle);
  if (im_channel == NULL) {
    *error = g_error_new(TELEPATHY_ERRORS, NotAvailable, 
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

  param_spec = g_param_spec_object ("transport", 
                                    "Object implementing a SalutMucTransport",
                                    "transport object",
                                    G_TYPE_OBJECT,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, 
                                   PROP_TRANSPORT, param_spec);
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

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (salut_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  text_mixin_class_init(object_class,
                        G_STRUCT_OFFSET(SalutMucChannelClass, text_class),
                        salut_muc_channel_send_stanza);

  group_mixin_class_init(object_class, 
    G_STRUCT_OFFSET(SalutMucChannelClass, group_class),
    muc_channel_add_member, NULL);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (salut_muc_channel_class), &dbus_glib_salut_muc_channel_object_info);
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

  if (priv->xmpp_connection != NULL) {
    g_object_unref(priv->xmpp_connection);
    priv->xmpp_connection = NULL;
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

  text_mixin_finalize(object);

  G_OBJECT_CLASS (salut_muc_channel_parent_class)->finalize (object);
}

void
salut_muc_channel_invited(SalutMucChannel *self, Handle invitor, 
                          const gchar *stanza) {
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(self);

  /* Got invited to this muc channel */
  DEBUG("Got an invitation to %s from %s", 
    handle_inspect(priv->connection->handle_repo, TP_HANDLE_TYPE_ROOM, 
                   priv->handle),
    handle_inspect(priv->connection->handle_repo, TP_HANDLE_TYPE_CONTACT,
                   invitor)
    );
  /* If we are already a member, no further actions are needed */
  if (handle_set_is_member(self->group.members, 
                           priv->connection->self_handle)) {
    return;
  }
  
  if (invitor == priv->connection->self_handle) {
    /* Invited ourselves, go straight to members */
    GError *error = NULL;
    GArray *members =  g_array_sized_new (FALSE, FALSE, sizeof (Handle), 1);
    g_array_append_val(members, priv->connection->self_handle);
    group_mixin_add_members(G_OBJECT(self), members, "", &error);
    g_assert(error == NULL);
    g_array_free(members, TRUE);
  } else {
    GIntSet *empty = g_intset_new();
    GIntSet *local_pending = g_intset_new();
    g_intset_add(local_pending, priv->connection->self_handle);
    group_mixin_change_members(G_OBJECT(self), stanza, 
                               empty, empty,
                               local_pending, empty,
                               invitor,
                               TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);
    g_intset_destroy(local_pending);
    g_intset_destroy(empty);
  }
}

/**
 * salut_muc_channel_acknowledge_pending_messages
 *
 * Implements D-Bus method AcknowledgePendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_muc_channel_acknowledge_pending_messages (SalutMucChannel *self,
                                                const GArray *ids,
                                                GError **error) {
  return text_mixin_acknowledge_pending_messages(G_OBJECT(self), ids, error);
}


/**
 * salut_muc_channel_add_members
 *
 * Implements D-Bus method AddMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_muc_channel_add_members (SalutMucChannel *self,
                               const GArray *contacts,
                               const gchar *stanza,
                               GError **error) {
  return group_mixin_add_members (G_OBJECT (self), contacts, stanza, 
                                         error);
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
gboolean
salut_muc_channel_close (SalutMucChannel *self,
                         GError **error) {
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);


  if (priv->presence_timeout_id != 0) {
    g_source_remove(priv->presence_timeout_id);
    priv->presence_timeout_id = 0;
  }

  salut_muc_channel_send_presence(self, FALSE);
  salut_transport_disconnect(priv->transport);

  return TRUE;
}

/**
 * salut_muc_channel_get_all_members
 *
 * Implements D-Bus method GetAllMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_muc_channel_get_all_members (SalutMucChannel *self,
                                   GArray **ret,
                                   GArray **ret1,
                                   GArray **ret2,
                                   GError **error)
{
  return group_mixin_get_all_members (G_OBJECT (self), ret, ret1, ret2, error);
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
gboolean
salut_muc_channel_get_channel_type (SalutMucChannel *self,
                                    gchar **ret,
                                    GError **error) {
  *ret = g_strdup (TP_IFACE_CHANNEL_TYPE_TEXT);
  return TRUE;
}


/**
 * salut_muc_channel_get_group_flags
 *
 * Implements D-Bus method GetGroupFlags
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_muc_channel_get_group_flags (SalutMucChannel *self,
                                   guint *ret,
                                   GError **error)
{
  return group_mixin_get_group_flags (G_OBJECT (self), ret, error);
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
gboolean
salut_muc_channel_get_handle (SalutMucChannel *self,
                              guint *ret,
                              guint *ret1,
                              GError **error)
{
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE (self);
  *ret = TP_HANDLE_TYPE_ROOM;
  *ret1 = priv->handle;

  return TRUE;
}


/**
 * salut_muc_channel_get_handle_owners
 *
 * Implements D-Bus method GetHandleOwners
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_muc_channel_get_handle_owners (SalutMucChannel *self,
                                     const GArray *handles,
                                     GArray **ret,
                                     GError **error)
{
  *ret = g_array_sized_new(FALSE, FALSE, 
                           sizeof(Handle), handles->len);
  *ret = g_array_insert_vals(*ret, 0, handles->data, handles->len);
  return TRUE;
}


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
gboolean
salut_muc_channel_get_interfaces (SalutMucChannel *self,
                                  gchar ***ret,
                                  GError **error) {
  const char *interfaces[] = { TP_IFACE_CHANNEL_INTERFACE_GROUP, 
                               NULL };
  
  *ret = g_strdupv ((gchar **) interfaces);
  return TRUE;
}


/**
 * salut_muc_channel_get_local_pending_members
 *
 * Implements D-Bus method GetLocalPendingMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_muc_channel_get_local_pending_members (SalutMucChannel *self,
                                             GArray **ret,
                                             GError **error) {
  return group_mixin_get_local_pending_members (G_OBJECT (self), ret, error);
}


/**
 * salut_muc_channel_get_members
 *
 * Implements D-Bus method GetMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_muc_channel_get_members (SalutMucChannel *self,
                               GArray **ret,
                               GError **error)
{
  return group_mixin_get_members (G_OBJECT (self), ret, error);
}


/**
 * salut_muc_channel_get_message_types
 *
 * Implements D-Bus method GetMessageTypes
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_muc_channel_get_message_types (SalutMucChannel *self,
                                     GArray **ret,
                                     GError **error) {
  return text_mixin_get_message_types(G_OBJECT(self), ret, error);
}


/**
 * salut_muc_channel_get_remote_pending_members
 *
 * Implements D-Bus method GetRemotePendingMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_muc_channel_get_remote_pending_members (SalutMucChannel *self,
                                              GArray **ret,
                                              GError **error) {
  return group_mixin_get_remote_pending_members (G_OBJECT (self), ret, error);
}


/**
 * salut_muc_channel_get_self_handle
 *
 * Implements D-Bus method GetSelfHandle
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_muc_channel_get_self_handle (SalutMucChannel *self,
                                   guint *ret,
                                   GError **error) {
  return group_mixin_get_self_handle (G_OBJECT (self), ret, error);
}


/**
 * salut_muc_channel_list_pending_messages
 *
 * Implements D-Bus method ListPendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_muc_channel_list_pending_messages (SalutMucChannel *self,
                                         gboolean clear,
                                         GPtrArray **ret,
                                         GError **error) {
  return text_mixin_list_pending_messages(G_OBJECT(self), clear, ret, error);
}


/**
 * salut_muc_channel_remove_members
 *
 * Implements D-Bus method RemoveMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_muc_channel_remove_members (SalutMucChannel *self,
                                  const GArray *contacts,
                                  const gchar *stanza,
                                  GError **error)
{
  return group_mixin_remove_members(G_OBJECT (self), contacts, stanza, error);
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
gboolean
salut_muc_channel_send (SalutMucChannel *self,
                        guint type, const gchar *text,
                        GError **error) { 
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(self);
  return text_mixin_send(G_OBJECT(self), type,
                         priv->connection->name,
                         priv->muc_name, text, error);
}

/* Private functions */
static gboolean salut_muc_channel_send_stanza(GObject *object, guint type, 
                                               const gchar *text,
                                               SalutXmppStanza *stanza,
                                               GError **error) {
  SalutMucChannel *self = SALUT_MUC_CHANNEL(object);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(object);

  if (!salut_xmpp_connection_send(priv->xmpp_connection, stanza, error)) {
    text_mixin_emit_send_error(G_OBJECT(self), CHANNEL_TEXT_SEND_ERROR_UNKNOWN,
                               time(NULL), type, text);
    return FALSE;
  }
  text_mixin_emit_sent(G_OBJECT(self), time(NULL), type, text);
  return TRUE;
}

static void
salut_muc_channel_send_presence(SalutMucChannel *self, 
                                gboolean joining) {
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(self);
  SalutXmppStanza *stanza;

  stanza = salut_xmpp_stanza_new("presence");
  if (!joining) {
    salut_xmpp_node_set_attribute(stanza->node, "type", "unavailable");
  }
  salut_xmpp_node_set_attribute(stanza->node, "from", priv->connection->name);
  salut_xmpp_node_set_attribute(stanza->node, "to", priv->muc_name);

  /* FIXME should disconnect if we couldn't sent */
  salut_xmpp_connection_send(priv->xmpp_connection, stanza, NULL);
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
                                 Handle from_handle, 
                                 gboolean joining) {
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(self);
  gboolean is_member;
  GIntSet *empty, *changes;

  is_member = handle_set_is_member(self->group.members, from_handle);
  if (is_member == joining) {
    return;
  }

  if (joining && priv->presence_timeout_id != 0) {
    /* New member joined start a timeout for our presence */
    priv->presence_timeout_id = 
      g_timeout_add(g_random_int_range(200, 2000),
                    salut_muc_channel_presence_timeout, self);
  }

  empty = g_intset_new();
  changes = g_intset_new();
  g_intset_add(changes, from_handle);
  group_mixin_change_members(G_OBJECT(self),
                                "", 
                                joining ? changes : empty, 
                                joining ? empty : changes,
                                empty, empty,
                                from_handle,
                                TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
  g_intset_destroy(changes);
  g_intset_destroy(empty);
}

static void 
salut_muc_channel_received_stanza(SalutXmppConnection *conn,
                                  SalutXmppStanza *stanza,
                                  gpointer user_data) {
  SalutMucChannel *self = SALUT_MUC_CHANNEL(user_data);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(self);
  const gchar *from, *body, *body_offset;
  TpChannelTextMessageType msgtype;
  Handle from_handle;

  if (!strcmp(stanza->node->name, "presence")) {
    salut_muc_channel_received_presence(self, stanza);
  }

  if (!text_mixin_parse_incoming_message(stanza, &from, &msgtype,
                                         &body, &body_offset)) {
    DEBUG("Couldn't parse stanza");
    return;
  }

  if (body == NULL) {
    DEBUG("Message with an empty body");
    return;
  }

  from_handle = handle_for_contact(priv->connection->handle_repo, from);
  if (from_handle == 0) {
    /* FIXME, unknown contact.. Need some way to handle this safely,
     * just adding the contact is somewhat scary */
    DEBUG("Got stanza from unknown contact, discarding");
    return;
  }

  salut_muc_channel_change_members(self, from_handle, TRUE);
  /* FIXME validate the from and the to */
  /* FIXME fix the text-mixin to actually get a to */
  text_mixin_receive(G_OBJECT(self), msgtype, from_handle,
                     time(NULL), body_offset);
}

static void salut_muc_channel_received_presence(SalutMucChannel *channel, 
                                                SalutXmppStanza *stanza) {
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(channel);
  gboolean joining = TRUE;
  const gchar *type;
  const gchar *from;
  Handle from_handle;

  type = salut_xmpp_node_get_attribute(stanza->node, "type");
  if (type != NULL && strcmp(type, "unavailable") == 0) {
    joining = FALSE;
  }

  from = salut_xmpp_node_get_attribute(stanza->node, "from");
  if (from == NULL) {
    DEBUG("Presence without a from");
    return;
  }

  DEBUG("Presence from: %s (joining: %d)", from, joining); 

  from_handle = handle_for_contact(priv->connection->handle_repo, from);
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

static void salut_muc_channel_connected(SalutTransport *transport,
                                        gpointer user_data) {
  SalutMucChannel *self = SALUT_MUC_CHANNEL(user_data);
  SalutMucChannelPrivate *priv = SALUT_MUC_CHANNEL_GET_PRIVATE(self);

  /* FIXME race */
  priv->presence_timeout_id = g_timeout_add(4000,
                                           salut_muc_channel_dummy_timeout, 
                                           self);
  salut_muc_channel_send_presence(self, TRUE);
}

static void salut_muc_channel_disconnected(SalutMucTransportIface *iface,
                                             gpointer user_data) {
  SalutMucChannel *self = SALUT_MUC_CHANNEL(user_data);
  g_signal_emit(self, signals[CLOSED], 0);
}
