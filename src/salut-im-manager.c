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
#include "salut-contact.h"
#include "salut-xmpp-connection-manager.h"

#include <gibber/gibber-linklocal-transport.h>
#include <gibber/gibber-xmpp-connection.h>
#include <gibber/gibber-xmpp-stanza.h>
#include <gibber/gibber-namespaces.h>

#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG DEBUG_IM
#include "debug.h"

static void salut_im_manager_factory_iface_init (gpointer g_iface,
    gpointer iface_data);

static SalutImChannel *
salut_im_manager_new_channel (SalutImManager *mgr, TpHandle handle,
    TpHandle initiator);

G_DEFINE_TYPE_WITH_CODE (SalutImManager, salut_im_manager, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE,
      salut_im_manager_factory_iface_init));

/* private structure */
typedef struct _SalutImManagerPrivate SalutImManagerPrivate;

struct _SalutImManagerPrivate
{
  SalutContactManager *contact_manager;
  SalutConnection *connection;
  SalutXmppConnectionManager *xmpp_connection_manager;
  GHashTable *channels;
  GHashTable *pending_connections;
  gboolean dispose_has_run;
};

#define SALUT_IM_MANAGER_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_IM_MANAGER, \
                                SalutImManagerPrivate))

static void
contact_list_destroy (gpointer data)
{
  GList *list = (GList *) data;
  GList *t = list;
  while (t != NULL)
    {
      SalutContact *contact;
      contact= SALUT_CONTACT (t->data);
      g_object_unref (contact);
      t = g_list_next (t);
    }
  g_list_free (list);
}

static void
salut_im_manager_init (SalutImManager *obj)
{
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (obj);
  /* allocate any data required by the object here */
  priv->channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);
  priv->pending_connections = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, g_object_unref, contact_list_destroy);
}

static gboolean
message_stanza_filter (SalutXmppConnectionManager *mgr,
                       GibberXmppConnection *conn,
                       GibberXmppStanza *stanza,
                       SalutContact *contact,
                       gpointer user_data)
{
  SalutImManager *self = SALUT_IM_MANAGER (user_data);
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (self);
  TpHandle handle;
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->connection);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (base_conn,
       TP_HANDLE_TYPE_CONTACT);

  if (!salut_im_channel_is_text_message (stanza))
    return FALSE;

  handle = tp_handle_lookup (handle_repo, contact->name, NULL, NULL);
  g_assert (handle != 0);

  /* We are interested by this stanza only if we need to create a new text
   * channel to handle it */
  return (g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (handle))
        == NULL);
}

static void
message_stanza_callback (SalutXmppConnectionManager *mgr,
                         GibberXmppConnection *conn,
                         GibberXmppStanza *stanza,
                         SalutContact *contact,
                         gpointer user_data)
{
  SalutImManager *self = SALUT_IM_MANAGER (user_data);
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (self);
  SalutImChannel *chan;
  TpHandle handle;
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->connection);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (base_conn,
       TP_HANDLE_TYPE_CONTACT);

  handle = tp_handle_lookup (handle_repo, contact->name, NULL, NULL);
  g_assert (handle != 0);

  chan = salut_im_manager_new_channel (self, handle, handle);
  salut_im_channel_add_connection (chan, conn);
  salut_im_channel_received_stanza (chan, stanza);
}

static void salut_im_manager_dispose (GObject *object);
static void salut_im_manager_finalize (GObject *object);

static void
salut_im_manager_class_init (SalutImManagerClass *salut_im_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_im_manager_class);

  g_type_class_add_private (salut_im_manager_class,
      sizeof (SalutImManagerPrivate));

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

  salut_xmpp_connection_manager_remove_stanza_filter (
      priv->xmpp_connection_manager, NULL,
      message_stanza_filter, message_stanza_callback, self);

  if (priv->contact_manager)
    {
      g_object_unref (priv->contact_manager);
      priv->contact_manager = NULL;
    }

  if (priv->xmpp_connection_manager != NULL)
    {
      g_object_unref (priv->xmpp_connection_manager);
      priv->xmpp_connection_manager = NULL;
    }

  if (priv->channels)
    {
      t = priv->channels;
      priv->channels = NULL;
      g_hash_table_destroy (t);
    }

  if (priv->pending_connections)
    {
      g_hash_table_destroy (priv->pending_connections);
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
salut_im_manager_factory_iface_close_all (TpChannelFactoryIface *iface)
{
  SalutImManager *mgr = SALUT_IM_MANAGER (iface);
  SalutImManagerPrivate *priv =
    SALUT_IM_MANAGER_GET_PRIVATE (mgr);

  if (priv->channels)
    {
      GHashTable *t = priv->channels;
      priv->channels = NULL;
      g_hash_table_destroy (t);
    }
}

static void
salut_im_manager_factory_iface_connecting (TpChannelFactoryIface *iface)
{
}

static void
salut_im_manager_factory_iface_connected (TpChannelFactoryIface *iface)
{
}

static void
salut_im_manager_factory_iface_disconnected (TpChannelFactoryIface *iface)
{
  /* FIMXE close all channels ? */
}

struct foreach_data
{
  TpChannelFunc func;
  gpointer data;
};

static void
salut_im_manager_iface_foreach_one (gpointer key,
                                    gpointer value,
                                    gpointer data)
{
  TpChannelIface *chan = TP_CHANNEL_IFACE (value);
  struct foreach_data *f = (struct foreach_data *) data;

  f->func (chan, f->data);
}

static void
salut_im_manager_factory_iface_foreach (TpChannelFactoryIface *iface,
                                        TpChannelFunc func,
                                        gpointer data)
{
  SalutImManager *mgr = SALUT_IM_MANAGER (iface);
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (mgr);
  struct foreach_data f;
  f.func = func;
  f.data = data;

  g_hash_table_foreach (priv->channels, salut_im_manager_iface_foreach_one,
      &f);
}

static TpChannelFactoryRequestStatus
salut_im_manager_factory_iface_request (TpChannelFactoryIface *iface,
                                        const gchar *chan_type,
                                        TpHandleType handle_type,
                                        guint handle,
                                        gpointer request,
                                        TpChannelIface **ret,
                                        GError **error)
{
  SalutImManager *mgr = SALUT_IM_MANAGER (iface);
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (mgr);
  SalutImChannel *chan;
  TpBaseConnection *base_connection = TP_BASE_CONNECTION (priv->connection);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles
      (base_connection, TP_HANDLE_TYPE_CONTACT);
  TpChannelFactoryRequestStatus status;

  /* We only support text channels */
  if (tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_TEXT))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;

  /* And thus only support contact handles */
  if (handle_type != TP_HANDLE_TYPE_CONTACT)
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;

  /* Most be a valid contact handle */
  if (!tp_handle_is_valid (handle_repo, TP_HANDLE_TYPE_CONTACT, NULL))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;

  /* Don't support opening a channel to our self handle */
  if (handle == base_connection->self_handle)
     return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;

  chan = g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (handle));
  if (chan != NULL)
    {
      status = TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING;
    }
  else
    {
      chan = salut_im_manager_new_channel (mgr, handle,
          base_connection->self_handle);
      if (chan == NULL)
        return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;

      status = TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED;
    }

  *ret = TP_CHANNEL_IFACE (chan);
  return status;
}

static void salut_im_manager_factory_iface_init (gpointer g_iface,
                                                 gpointer iface_data)
{
   TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *) g_iface;

   klass->close_all = salut_im_manager_factory_iface_close_all;
   klass->connecting = salut_im_manager_factory_iface_connecting;
   klass->connected = salut_im_manager_factory_iface_connected;
   klass->disconnected = salut_im_manager_factory_iface_disconnected;
   klass->foreach = salut_im_manager_factory_iface_foreach;
   klass->request = salut_im_manager_factory_iface_request;
}

/* private functions */
static void
im_channel_closed_cb (SalutImChannel *chan,
                      gpointer user_data)
{
  SalutImManager *self = SALUT_IM_MANAGER (user_data);
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (self);
  TpHandle handle;

  if (priv->channels)
    {
      g_object_get (chan, "handle", &handle, NULL);
      DEBUG ("Removing channel with handle %u", handle);
      g_hash_table_remove (priv->channels, GUINT_TO_POINTER (handle));
    }
}

static SalutImChannel *
salut_im_manager_new_channel (SalutImManager *mgr,
                              TpHandle handle,
                              TpHandle initiator)
{
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (mgr);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION (priv->connection);
  TpHandleRepoIface *handle_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_CONTACT);
  SalutImChannel *chan;
  SalutContact *contact;
  const gchar *name;
  gchar *path = NULL;

  g_assert (g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (handle))
      == NULL);
  DEBUG ("Requested channel for handle: %u", handle);

  contact = salut_contact_manager_get_contact (priv->contact_manager, handle);
  if (contact == NULL)
    return NULL;

  name = tp_handle_inspect (handle_repo, handle);
  path = g_strdup_printf ("%s/IMChannel/%u",
      base_connection->object_path, handle);
  chan = g_object_new (SALUT_TYPE_IM_CHANNEL,
      "connection", priv->connection,
      "contact", contact,
      "object-path", path,
      "handle", handle,
      "initiator-handle", initiator,
      "xmpp-connection-manager", priv->xmpp_connection_manager,
      NULL);
  g_object_unref (contact);
  g_free (path);
  g_hash_table_insert (priv->channels, GUINT_TO_POINTER (handle), chan);
  tp_channel_factory_iface_emit_new_channel (mgr, TP_CHANNEL_IFACE (chan),
      NULL);
  g_signal_connect (chan, "closed", G_CALLBACK (im_channel_closed_cb), mgr);

  return chan;
}

/* public functions */
SalutImManager *
salut_im_manager_new (SalutConnection *connection,
                      SalutContactManager *contact_manager,
                      SalutXmppConnectionManager *xmpp_connection_manager)
{
  SalutImManager *ret = NULL;
  SalutImManagerPrivate *priv;

  ret = g_object_new (SALUT_TYPE_IM_MANAGER, NULL);
  priv = SALUT_IM_MANAGER_GET_PRIVATE (ret);

  priv->contact_manager = contact_manager;
  g_object_ref (contact_manager);
  priv->xmpp_connection_manager = xmpp_connection_manager;
  g_object_ref (xmpp_connection_manager);

  salut_xmpp_connection_manager_add_stanza_filter (
      priv->xmpp_connection_manager, NULL,
      message_stanza_filter, message_stanza_callback, ret);

  priv->connection = connection;

  return ret;
}
