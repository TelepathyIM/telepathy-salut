/*
 * salut-muc-manager.c - Source for SalutMucManager
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

#include "salut-muc-manager.h"
#include "salut-muc-manager-signals-marshal.h"

#include "salut-muc-connection.h"

#include "salut-muc-channel.h"
#include "salut-contact-manager.h"

#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/interfaces.h>

#include "namespaces.h"

#define DEBUG_FLAG DEBUG_MUC
#include "debug.h"

static void salut_muc_manager_factory_iface_init(gpointer *g_iface, 
                                                     gpointer *iface_data);
G_DEFINE_TYPE_WITH_CODE(SalutMucManager, salut_muc_manager, 
                        G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE, 
                                        salut_muc_manager_factory_iface_init));

/* signal enum */
/*
enum
{
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};
*/

/* private structure */
typedef struct _SalutMucManagerPrivate SalutMucManagerPrivate;

struct _SalutMucManagerPrivate
{
  gboolean dispose_has_run;
  SalutConnection *connection;
  SalutImManager *im_manager;
  GHashTable *channels;
};

#define SALUT_MUC_MANAGER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_MUC_MANAGER, SalutMucManagerPrivate))

static void
salut_muc_manager_init (SalutMucManager *obj)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (obj);
  priv->im_manager = NULL;
  priv->connection = NULL;

  /* allocate any data required by the object here */
  priv->channels = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                         NULL, g_object_unref);
}

static void salut_muc_manager_dispose (GObject *object);
static void salut_muc_manager_finalize (GObject *object);

static void
salut_muc_manager_class_init (SalutMucManagerClass *salut_muc_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_muc_manager_class);

  g_type_class_add_private (salut_muc_manager_class, 
                              sizeof (SalutMucManagerPrivate));

  object_class->dispose = salut_muc_manager_dispose;
  object_class->finalize = salut_muc_manager_finalize;
}

void
salut_muc_manager_dispose (GObject *object)
{
  SalutMucManager *self = SALUT_MUC_MANAGER (object);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  GHashTable *t;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;
  if (priv->im_manager != NULL) {
    g_object_unref(priv->im_manager);
    priv->im_manager = NULL;
  }

  if (priv->channels) {
    t = priv->channels;
    priv->channels = NULL;
    g_hash_table_destroy(t);
  }

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_muc_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_muc_manager_parent_class)->dispose (object);
}

void
salut_muc_manager_finalize (GObject *object)
{
  /*SalutMucManager *self = SALUT_MUC_MANAGER (object);*/
  /*SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);*/

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (salut_muc_manager_parent_class)->finalize (object);
}

/* Channel Factory interface */

static void
salut_muc_manager_factory_iface_close_all(TpChannelFactoryIface *iface) {
  GHashTable *t;
  SalutMucManager *mgr = SALUT_MUC_MANAGER(iface);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE(mgr);

  if (priv->channels) {
    t = priv->channels;
    priv->channels = NULL;
    g_hash_table_destroy(t);
  }
}

static void
salut_muc_manager_factory_iface_connecting(TpChannelFactoryIface *iface) {
}

static void
salut_muc_manager_factory_iface_connected(TpChannelFactoryIface *iface) {
}

static void
salut_muc_manager_factory_iface_disconnected(TpChannelFactoryIface *iface) {
  /* FIMXE close all channels ? */
}

struct foreach_data { 
  TpChannelFunc func;
  gpointer data;
};

static void
salut_muc_manager_iface_foreach_one(gpointer key, 
                                    gpointer value, 
                                    gpointer data) {
  TpChannelIface *chan = TP_CHANNEL_IFACE(value);
  struct foreach_data *f = (struct foreach_data *) data;

  f->func(chan, f->data);
}

static void
salut_muc_manager_factory_iface_foreach(TpChannelFactoryIface *iface,
                                        TpChannelFunc func, gpointer data) {
  SalutMucManager *mgr = SALUT_MUC_MANAGER(iface);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE(mgr);
  struct foreach_data f;
  f.func = func;
  f.data = data;

  g_hash_table_foreach(priv->channels, salut_muc_manager_iface_foreach_one, &f);
}

static void
muc_channel_closed_cb(SalutMucChannel *chan, gpointer user_data) {
  SalutMucManager *self = SALUT_MUC_MANAGER(user_data);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE(self);
  TpHandle handle;

  if (priv->channels) { 
    g_object_get(chan, "handle", &handle, NULL);
    DEBUG("Removing channel with handle %d", handle);
    g_hash_table_remove(priv->channels, GINT_TO_POINTER(handle));
  }
}

GObject *
_get_connection(SalutMucManager *mgr, const gchar *name, 
               const gchar *protocol, GHashTable *parameters, GError **error) {
  SalutMucConnection *connection;

  connection = salut_muc_connection_new(name, protocol, parameters, error);
  return connection == NULL ? NULL : G_OBJECT(connection);
}

const gchar **
_get_connection_parameters(SalutMucManager *mgr, const gchar *protocol) {
  return salut_muc_connection_get_required_parameters(protocol); 
}

static SalutMucChannel *
salut_muc_manager_new_channel(SalutMucManager *mgr, TpHandle handle,
                              GObject *connection) {
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE(mgr);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION(priv->connection);
  TpHandleRepoIface *room_repo = 
      tp_base_connection_get_handles(base_connection, TP_HANDLE_TYPE_ROOM);
  SalutMucChannel *chan;
  const gchar *name;
  gchar *path = NULL;

  g_assert(g_hash_table_lookup(priv->channels, GINT_TO_POINTER(handle)) 
             == NULL);
  DEBUG("Requested channel for handle: %d", handle);

  /* FIXME The name of the muc and the handel might need to be different at
   * some point.. E.g. if two rooms are called the same */
  name = tp_handle_inspect(room_repo, handle);
  path = g_strdup_printf("%s/MucChannel/%u", 
                         base_connection->object_path, handle);
  chan = g_object_new(SALUT_TYPE_MUC_CHANNEL,
                      "connection", priv->connection,
                      "im-manager", priv->im_manager,
                      "object-path", path,
                      "muc_connection", connection,
                      "handle", handle,
                      "name", name,
                      NULL);
  g_free(path);

  g_hash_table_insert(priv->channels, GINT_TO_POINTER(handle), chan);
  g_signal_connect(chan, "closed", G_CALLBACK(muc_channel_closed_cb), mgr);
  tp_channel_factory_iface_emit_new_channel(mgr, TP_CHANNEL_IFACE(chan), NULL);

  return chan;
}

static TpChannelFactoryRequestStatus
salut_muc_manager_factory_iface_request(TpChannelFactoryIface *iface,
                                             const gchar *chan_type, 
                                             TpHandleType handle_type,
                                             guint handle, 
                                             gpointer request,
                                             TpChannelIface **ret,
                                             GError **error) {
  SalutMucManager *mgr = SALUT_MUC_MANAGER(iface);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE(mgr);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION(priv->connection);
  TpHandleRepoIface *room_repo = 
      tp_base_connection_get_handles(base_connection, TP_HANDLE_TYPE_ROOM);
  SalutMucChannel *chan;
  gboolean created = FALSE;

  DEBUG("Muc request");

  /* We only support text channels */
  if (tp_strdiff(chan_type, TP_IFACE_CHANNEL_TYPE_TEXT)) {
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;
  }

  /* And only room handles */
  if (handle_type != TP_HANDLE_TYPE_ROOM) {
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;
  }

  /* Most be a valid room handle */
  if (!tp_handle_is_valid(room_repo, handle, NULL)) {
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;
  }

  chan = g_hash_table_lookup(priv->channels, GINT_TO_POINTER(handle));
  if (chan != NULL) { 
    *ret = TP_CHANNEL_IFACE(chan);
    created = FALSE;
  } else {
    GObject *connection = _get_connection(mgr,
                                        tp_handle_inspect(
                                          room_repo, handle), 
                                          NULL, NULL, NULL);
    created = TRUE;
    if (connection == NULL) {
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;
    }
    chan = salut_muc_manager_new_channel(mgr, handle, connection);
    /* We requested the channel, so invite ourselves to it */
    if (chan)  {
      salut_muc_channel_invited(chan, base_connection->self_handle, "");
      *ret = TP_CHANNEL_IFACE(chan);
    }
    g_assert(chan != NULL);
  }

  return created ? TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED 
                 : TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING;
}

static void salut_muc_manager_factory_iface_init(gpointer *g_iface, 
                                                     gpointer *iface_data) {
   TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *)g_iface;

   klass->close_all = salut_muc_manager_factory_iface_close_all;
   klass->connecting = salut_muc_manager_factory_iface_connecting;
   klass->connected = salut_muc_manager_factory_iface_connected;
   klass->disconnected = salut_muc_manager_factory_iface_disconnected;
   klass->foreach = salut_muc_manager_factory_iface_foreach;
   klass->request = salut_muc_manager_factory_iface_request;
}

static gboolean
_received_stanza(SalutImChannel *imchannel, 
                  GibberXmppStanza *message, gpointer data) {
  SalutMucManager *self = SALUT_MUC_MANAGER(data);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE(self);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION(priv->connection);
  TpHandleRepoIface *room_repo = 
      tp_base_connection_get_handles(base_connection, TP_HANDLE_TYPE_ROOM);
  GibberXmppNode *node;
  GibberXmppNode *invite;
  GibberXmppNode *room_node;
  GibberXmppNode *reason_node;
  SalutMucChannel *chan;
  const gchar *room = NULL;
  const gchar *reason = NULL;
  const gchar *protocol = NULL;
  const gchar **params;
  TpHandle room_handle;
  TpHandle invitor_handle;
  const gchar **p;
  GHashTable *params_hash;
  GObject *transport = NULL;

  node = gibber_xmpp_node_get_child_ns(message->node, "x", NS_LLMUC);
  if (node == NULL) 
    return FALSE;

  DEBUG("Got an invitation");

  invite = gibber_xmpp_node_get_child(node, "invite");
  if (invite == NULL) {
    DEBUG("Got invitation, but no invite block!?");
    return FALSE;
  }

  room_node = gibber_xmpp_node_get_child(invite, "roomname");

  if (room_node == NULL) {
    DEBUG("Invalid invitation, discarding");
    return TRUE;
  }

  room = room_node->content;
  reason_node = gibber_xmpp_node_get_child(invite, "reason");
  if (reason_node != NULL) {
    reason = reason_node->content;
  }
  if (reason == NULL) {
    reason = "";
  }
  
  protocol = gibber_xmpp_node_get_attribute(invite, "protocol");
  if (protocol == NULL) { 
    DEBUG("Invalid invitation, (missing protocol) discarding");
    return TRUE;
  }

  params = _get_connection_parameters(self, protocol);
  if (params == NULL) {
    DEBUG("Invalid invitation, (unknown protocol) discarding");
    return TRUE;
  }

  params_hash = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);
  for (p = params ; *p != NULL; p++) {
    GibberXmppNode *param;
    param = gibber_xmpp_node_get_child(invite, *p);
    if (param == NULL) {
      DEBUG("Invalid invitation, (missing parameter) discarding");
      goto discard;
    }
    g_hash_table_insert(params_hash, (gchar *)*p, 
                        g_strdup(param->content));
  }

  /* FIXME proper serialisation of handle name */
  /* Create the group if it doesn't exist and myself to local_pending */
  room_handle = tp_handle_ensure(room_repo, room, NULL, NULL);

  /* FIXME handle properly */
  g_assert(room_handle != 0);

  chan = g_hash_table_lookup(priv->channels, GINT_TO_POINTER(room_handle));

  if (chan == NULL) {
    transport = _get_connection(self, room, protocol, params_hash, NULL);
    if (transport == NULL) {
      DEBUG("Invalid invitation, (wrong protocol parameters) discarding");
      goto discard;
    }

    if (transport == NULL) {
      tp_handle_unref(room_repo, room_handle);
      /* FIXME some kinda error to the user maybe ? Ignore for now */
      goto discard;
    }
    /* Need to create a new one */
    chan = salut_muc_manager_new_channel(self, room_handle, transport);
  }
  /* FIXME handle properly */
  g_assert(chan != NULL);

  g_object_get(G_OBJECT(imchannel), "handle", &invitor_handle, NULL);
  salut_muc_channel_invited(chan, invitor_handle, reason);

  g_hash_table_destroy(params_hash);

  return TRUE;
discard:
  if (params_hash != NULL) {
    g_hash_table_destroy(params_hash);
  }
  return TRUE;
}

static void
_new_im_channel(TpChannelFactoryIface *channel_iface, 
                TpChannelIface *channel, gpointer request, 
                gpointer data) {
  SalutImChannel *imchannel = SALUT_IM_CHANNEL(channel);
  SalutMucManager *self = SALUT_MUC_MANAGER(data);
  g_signal_connect(imchannel, "received-stanza", 
                     G_CALLBACK(_received_stanza),
                     self);
}

/* public functions */
SalutMucManager *
salut_muc_manager_new(SalutConnection *connection,
                      SalutImManager *im_manager) {
  SalutMucManager *ret = NULL; 
  SalutMucManagerPrivate *priv;

  g_assert(connection != NULL);
  g_assert(im_manager != NULL);

  ret = g_object_new(SALUT_TYPE_MUC_MANAGER, NULL);
  priv = SALUT_MUC_MANAGER_GET_PRIVATE (ret);

  priv->im_manager = im_manager;
  g_object_ref(im_manager);
  g_signal_connect(im_manager, "new-channel",
                   G_CALLBACK(_new_im_channel), ret);

  priv->connection = connection;

  return ret;
}
