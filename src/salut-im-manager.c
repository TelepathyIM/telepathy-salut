/*
 * salut-im-manager.c - Source for SalutImManager
 * Copyright (C) 2005 Collabora Ltd.
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
#include <string.h>

#include "salut-im-channel.h"
#include "salut-im-manager.h"
#include "salut-im-manager-signals-marshal.h"
#include "salut-contact.h"

#include <gibber/gibber-linklocal-transport.h>
#include <gibber/gibber-xmpp-connection.h>
#include <gibber/gibber-xmpp-stanza.h>
#include <gibber/gibber-namespaces.h>

#include "telepathy-errors.h"
#include "telepathy-interfaces.h"
#include "telepathy-constants.h"
#include "tp-channel-factory-iface.h"
#include "handle-types.h"
#include "handle-repository.h"
#include "gintset.h"

#define DEBUG_FLAG DEBUG_IM
#include "debug.h"

static void salut_im_manager_factory_iface_init(gpointer *g_iface, 
                                                     gpointer *iface_data);

static SalutImChannel *
salut_im_manager_new_channel(SalutImManager *mgr, Handle handle);

G_DEFINE_TYPE_WITH_CODE(SalutImManager, salut_im_manager, 
                        G_TYPE_OBJECT,
              G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE, 
                                     salut_im_manager_factory_iface_init));

/* private structure */
typedef struct _SalutImManagerPrivate SalutImManagerPrivate;

struct _SalutImManagerPrivate
{
  SalutContactManager *contact_manager;
  SalutConnection *connection;
  GHashTable *channels;
  GHashTable *pending_connections;
  gboolean dispose_has_run;
};

#define SALUT_IM_MANAGER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_IM_MANAGER, SalutImManagerPrivate))

static void
contact_list_destroy(gpointer data) {
  GList *list = (GList *)data;
  GList *t = list;
  while (t != NULL) {
    SalutContact *contact;
    contact= SALUT_CONTACT(t->data);
    g_object_unref(contact);
    t = g_list_next(t);
  }
  g_list_free(list);
}

static void
salut_im_manager_init (SalutImManager *obj)
{
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (obj);
  /* allocate any data required by the object here */
  priv->channels = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                         NULL, g_object_unref);
  priv->pending_connections = g_hash_table_new_full(g_direct_hash, 
                                                    g_direct_equal,
                                                    g_object_unref, 
                                                    contact_list_destroy);
}

static void salut_im_manager_dispose (GObject *object);
static void salut_im_manager_finalize (GObject *object);

static void
salut_im_manager_class_init (SalutImManagerClass *salut_im_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_im_manager_class);

  g_type_class_add_private (salut_im_manager_class, sizeof (SalutImManagerPrivate));

  object_class->dispose = salut_im_manager_dispose;
  object_class->finalize = salut_im_manager_finalize;
}

void
salut_im_manager_dispose (GObject *object)
{
  SalutImManager *self = SALUT_IM_MANAGER (object);
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (self);
  GHashTable *t;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->contact_manager) {
    g_object_unref(priv->contact_manager);
    priv->contact_manager = NULL;
  }
  
  if (priv->connection) {
    g_object_unref(priv->connection);
    priv->connection = NULL;
  }

  if (priv->channels) {
    t = priv->channels;
    priv->channels = NULL;
    g_hash_table_destroy(t);
  }

  if (priv->pending_connections) {
    g_hash_table_destroy(priv->pending_connections);
    priv->pending_connections = NULL;
  }

  /* release any references held by the object here */
  tp_channel_factory_iface_close_all (TP_CHANNEL_FACTORY_IFACE (object));

  if (G_OBJECT_CLASS (salut_im_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_im_manager_parent_class)->dispose (object);
}

void
salut_im_manager_finalize (GObject *object)
{
  //SalutImManager *self = SALUT_IM_MANAGER (object);
  //SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (salut_im_manager_parent_class)->finalize (object);
}

static void
salut_im_manager_factory_iface_close_all(TpChannelFactoryIface *iface) {
  SalutImManager *mgr = SALUT_IM_MANAGER(iface);
  SalutImManagerPrivate *priv = 
    SALUT_IM_MANAGER_GET_PRIVATE(mgr);

  if (priv->channels) {
    g_hash_table_destroy(priv->channels);
    priv->channels = NULL;
  }

}

static void
salut_im_manager_factory_iface_connecting(TpChannelFactoryIface *iface) {
}

static void
salut_im_manager_factory_iface_connected(TpChannelFactoryIface *iface) {
}

static void
salut_im_manager_factory_iface_disconnected(TpChannelFactoryIface *iface) {
  /* FIMXE close all channels ? */
}

struct foreach_data { 
  TpChannelFunc func;
  gpointer data;
};

static void
salut_im_manager_iface_foreach_one(gpointer key, 
                                        gpointer value, 
                                        gpointer data)  {
  TpChannelIface *chan = TP_CHANNEL_IFACE(value);
  struct foreach_data *f = (struct foreach_data *) data;

  f->func(chan, f->data);
}

static void
salut_im_manager_factory_iface_foreach(TpChannelFactoryIface *iface,
                                            TpChannelFunc func, gpointer data) {
  SalutImManager *mgr = SALUT_IM_MANAGER(iface);
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE(mgr);
  struct foreach_data f;
  f.func = func;
  f.data = data;

  g_hash_table_foreach(priv->channels, 
                       salut_im_manager_iface_foreach_one,
                       &f);

}

static TpChannelFactoryRequestStatus
salut_im_manager_factory_iface_request(TpChannelFactoryIface *iface,
                                             const gchar *chan_type, 
                                             TpHandleType handle_type,
                                             guint handle, 
                                             TpChannelIface **ret) {
  SalutImManager *mgr = SALUT_IM_MANAGER(iface);
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE(mgr);
  SalutImChannel *chan;

  /* We only support text channels */
  if (strcmp(chan_type, TP_IFACE_CHANNEL_TYPE_TEXT)) {
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;
  }

  /* And thus only support contact handles */
  if (handle_type != TP_HANDLE_TYPE_CONTACT) {
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;
  }

  /* Most be a valid contact handle */
  if (!handle_is_valid(priv->connection->handle_repo, TP_HANDLE_TYPE_CONTACT,
                       handle, NULL)) {
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;
  }

  /* Don't support opening a channel to our self handle */
  if (handle == priv->connection->self_handle) {
     return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;
  }

  chan = g_hash_table_lookup(priv->channels, GINT_TO_POINTER(handle));
  if (chan != NULL) { 
    *ret = TP_CHANNEL_IFACE(chan);
  } else {
    *ret = TP_CHANNEL_IFACE(salut_im_manager_new_channel(mgr, handle));
  }

  return *ret != NULL ? TP_CHANNEL_FACTORY_REQUEST_STATUS_DONE
                      : TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;
}

static void salut_im_manager_factory_iface_init(gpointer *g_iface, 
                                                     gpointer *iface_data) {
   TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *)g_iface;

   klass->close_all = salut_im_manager_factory_iface_close_all;
   klass->connecting = salut_im_manager_factory_iface_connecting;
   klass->connected = salut_im_manager_factory_iface_connected;
   klass->disconnected = salut_im_manager_factory_iface_disconnected;
   klass->foreach = salut_im_manager_factory_iface_foreach;
   klass->request = salut_im_manager_factory_iface_request;
}

/* private functions */
static void
im_channel_closed_cb(SalutImChannel *chan, gpointer user_data) {
  SalutImManager *self = SALUT_IM_MANAGER(user_data);
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE(self);
  Handle handle;

  if (priv->channels) { 
    g_object_get(chan, "handle", &handle, NULL);
    DEBUG("Removing channel with handle %d", handle);
    g_hash_table_remove(priv->channels, GINT_TO_POINTER(handle));
  }
}

static SalutImChannel *
salut_im_manager_new_channel(SalutImManager *mgr, Handle handle) {
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE(mgr);
  SalutImChannel *chan;
  SalutContact *contact;
  const gchar *name;
  gchar *path = NULL;

  g_assert(g_hash_table_lookup(priv->channels, GINT_TO_POINTER(handle)) 
             == NULL);
  DEBUG("Requested channel for handle: %d", handle);

  contact = salut_contact_manager_get_contact(priv->contact_manager, handle);
  if (contact == NULL) {
    return NULL;
  }
  name = handle_inspect(priv->connection->handle_repo, 
                        TP_HANDLE_TYPE_CONTACT, handle);
  path = g_strdup_printf("%s/IMChannel/%u", 
                         priv->connection->object_path, handle);
  chan = g_object_new(SALUT_TYPE_IM_CHANNEL,
                      "connection", priv->connection,
                      "contact", contact,
                      "object-path", path,
                      "handle", handle,
                      NULL);
  g_object_unref(contact);
  g_free(path);
  g_hash_table_insert(priv->channels, GINT_TO_POINTER(handle), chan);
  g_signal_emit_by_name(mgr, "new-channel", chan);
  g_signal_connect(chan, "closed", G_CALLBACK(im_channel_closed_cb), mgr);

  return chan;
}


/* public functions */
SalutImManager *
salut_im_manager_new(SalutConnection *connection,
                     SalutContactManager *contact_manager) {
  SalutImManager *ret = NULL; 
  SalutImManagerPrivate *priv;

  ret = g_object_new(SALUT_TYPE_IM_MANAGER, NULL);
  priv = SALUT_IM_MANAGER_GET_PRIVATE (ret);

  priv->contact_manager = contact_manager;
  g_object_ref(contact_manager);

  priv->connection = connection;
  g_object_ref(connection);

  return ret;
}

SalutImChannel *
salut_im_manager_get_channel_for_handle(SalutImManager *mgr, 
                                        Handle handle) {
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE(mgr);
  SalutImChannel *chan;
  chan = g_hash_table_lookup(priv->channels, GINT_TO_POINTER(handle));
  if (chan == NULL) {
    chan = salut_im_manager_new_channel(mgr, handle);
  }
  if (chan != NULL) { 
    g_object_ref(chan);
  } 
  return chan;
}

static void
found_contact_for_connection(SalutImManager *mgr, 
                             GibberXmppConnection *connection,
                             SalutContact *contact,
                             GibberXmppStanza *stanza) {
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE(mgr);
  Handle handle;
  SalutImChannel *chan;

  handle = handle_for_contact(priv->connection->handle_repo, contact->name);

  chan = g_hash_table_lookup(priv->channels, GINT_TO_POINTER(handle));
  if (chan == NULL) {
    chan = salut_im_manager_new_channel(mgr, handle);
  }
  salut_im_channel_add_connection(chan, connection);
  if (stanza) {
    salut_im_channel_received_stanza(chan, stanza);
  }
}


static void
pending_connection_stream_closed_cb(GibberXmppConnection *connection, 
                                    gpointer userdata) {
  SalutImManager *mgr = SALUT_IM_MANAGER(userdata);
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE(mgr);
  DEBUG("Pending connection stream closed");
  gibber_xmpp_connection_close(connection);
  gibber_transport_disconnect(connection->transport);
  g_hash_table_remove(priv->pending_connections, connection);
}

static gboolean
has_transport(gpointer key, gpointer value, gpointer user_data) {
  GibberXmppConnection *conn = GIBBER_XMPP_CONNECTION(key);

  return conn->transport == user_data;
}

static void
pending_connection_transport_disconnected_cb(GibberLLTransport *transport,
                                             gpointer userdata) {
  SalutImManager *mgr = SALUT_IM_MANAGER(userdata);
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE(mgr);
  DEBUG("Pending connection disconnected");
  g_hash_table_foreach_remove(priv->pending_connections, 
                              has_transport, transport);
}

static void
connection_parse_error_cb(GibberXmppConnection *conn, gpointer userdata) {
  DEBUG("Parse error on xml stream, closing connection");
  /* Just close the transport, the disconnected callback will do the cleanup */
  gibber_transport_disconnect(conn->transport);
}

static void
pending_connection_got_from(SalutImManager *mgr, 
                            GibberXmppConnection *conn,
                            GibberXmppStanza *stanza,
                            const gchar *from) {
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE(mgr);
  GList *t;

  if (from == NULL) {
    DEBUG("No valid ``from'' pending connection");
    goto error;
  }

  DEBUG("Got stream from %s on pending connection", from);
  t = g_hash_table_lookup(priv->pending_connections, conn);

  while (t != NULL) {
    SalutContact *contact = SALUT_CONTACT(t->data);
    if (strcmp(contact->name, from) == 0) {
      struct sockaddr_storage addr;
      socklen_t size = sizeof(struct sockaddr_storage);
      g_signal_handlers_disconnect_matched(conn, G_SIGNAL_MATCH_DATA,
                                       0, 0, NULL, NULL, mgr);
      g_signal_handlers_disconnect_matched(conn->transport, G_SIGNAL_MATCH_DATA,
                                       0, 0, NULL, NULL, mgr);
      if (!gibber_ll_transport_get_address(GIBBER_LL_TRANSPORT(conn->transport),
                                           &addr, &size)) {
        DEBUG("Contact no longer alive");
        goto error;
      }
      if (!salut_contact_has_address(contact, &addr)) {
        DEBUG("Contact doesn't have that address");
        goto error;
      }
      found_contact_for_connection(mgr, conn, contact, stanza);
      g_hash_table_remove(priv->pending_connections, conn);
      return;
    }
    t = g_list_next(t);
  }

error:
  gibber_xmpp_connection_close(conn);
  gibber_transport_disconnect(conn->transport);
  g_hash_table_remove(priv->pending_connections, conn);
}

static void
pending_connection_stream_opened_cb(GibberXmppConnection *conn,
                                    gchar *to, gchar *from,
                                    gpointer user_data) {
  SalutImManager *mgr = SALUT_IM_MANAGER(user_data);
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE(mgr);
  GibberXmppStanza *stanza;

  /* According to the xep-0174 revision >= there should 
   * be a to and from.. But clients implementing older revision might not
   * support that yet. 
   * */
  if (from != NULL) {
    pending_connection_got_from(mgr, conn, NULL, from);
  }

  gibber_xmpp_connection_open(conn, from, priv->connection->name, NULL);
  /* Send empty stream features */
  stanza = gibber_xmpp_stanza_new("features");
  gibber_xmpp_node_set_ns(stanza->node, GIBBER_XMPP_NS_STREAM);
  gibber_xmpp_connection_send(conn, stanza, NULL);
  g_object_unref(stanza);
}

static void
pending_connection_stanza_received_cb(GibberXmppConnection *conn, 
                                      GibberXmppStanza *stanza,
                                      gpointer userdata) {
  /* If the identity wasn't clear from the stream opening we only wait to the
   * very first message */ 
  pending_connection_got_from(SALUT_IM_MANAGER(userdata), 
                              conn, 
                              stanza,
                              gibber_xmpp_node_get_attribute(stanza->node, 
                                  "from"));
}

void
salut_im_manager_handle_connection(SalutImManager *mgr,
                                        GibberLLTransport *transport) {
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE(mgr);
  GibberXmppConnection *connection = NULL;
  GList *contacts;
  struct sockaddr_storage addr;
  socklen_t size = sizeof(struct sockaddr_storage);

  DEBUG("Handling new connection");
  if (!gibber_ll_transport_get_address(transport, &addr, &size)) {
    goto notfound;
  }

  contacts = 
    salut_contact_manager_find_contacts_by_address(priv->contact_manager, 
                                                   &addr);
  if (contacts == NULL) {
    goto notfound;
  }

  /* Transport to somebody we know about, connect the xmpp connection */
  connection = gibber_xmpp_connection_new(GIBBER_TRANSPORT(transport));

  /* Unref the transport, the xmpp connection own it now */
  g_object_unref(transport);

  /* If it's a transport to just one contacts machine, hook it up right away.
   * This is needed because iChat doesn't send message with to and from data...
   */
  if (g_list_length(contacts) == 1) {
    found_contact_for_connection(mgr, connection, 
                                  SALUT_CONTACT(contacts->data), NULL);
    contact_list_destroy(contacts);
    g_object_unref(G_OBJECT(connection));
  } else {
    g_hash_table_insert(priv->pending_connections, connection, contacts); 
    g_signal_connect(connection, "stream-openened",
                     G_CALLBACK(pending_connection_stream_opened_cb), mgr);
    g_signal_connect(connection, "received-stanza",
                     G_CALLBACK(pending_connection_stanza_received_cb), mgr);
    g_signal_connect(transport, "disconnected", 
                    G_CALLBACK(pending_connection_transport_disconnected_cb), 
                    mgr);
    g_signal_connect(connection, "stream-closed", 
                     G_CALLBACK(pending_connection_stream_closed_cb), 
                     mgr);
    g_signal_connect(connection, "parse-error",
                     G_CALLBACK(connection_parse_error_cb), mgr);
  }
  return ;

notfound:
  DEBUG("Couldn't find a contact for the connection");
  gibber_transport_disconnect(GIBBER_TRANSPORT(transport));
  g_object_unref(transport);
}
