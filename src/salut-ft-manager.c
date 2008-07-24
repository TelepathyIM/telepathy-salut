/*
 * salut-ft-manager.c - Source for SalutFtManager
 * Copyright (C) 2007 Marco Barisione <marco@barisione.org>
 * Copyright (C) 2006, 2008 Collabora Ltd.
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

#include <gibber/gibber-file-transfer.h>

#include "salut-ft-manager.h"
#include "signals-marshal.h"

#include "salut-file-channel.h"
#include "salut-contact-manager.h"

#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG DEBUG_FT
#include "debug.h"

static void
salut_ft_manager_factory_iface_init (gpointer *g_iface, gpointer *iface_data);

static SalutFileChannel *
salut_ft_manager_new_channel (SalutFtManager *mgr, TpHandle handle, gboolean incoming);

G_DEFINE_TYPE_WITH_CODE (SalutFtManager, salut_ft_manager, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE,
                           salut_ft_manager_factory_iface_init));

/* signal enum */
/*
enum
{
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};
*/

/* private structure */
typedef struct _SalutFtManagerPrivate SalutFtManagerPrivate;

struct _SalutFtManagerPrivate
{
  gboolean dispose_has_run;
  SalutConnection *connection;
  SalutXmppConnectionManager *xmpp_connection_manager;
  SalutContactManager *contact_manager;
  GList *channels;
};

#define SALUT_FT_MANAGER_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_FT_MANAGER, \
                                SalutFtManagerPrivate))

static void
salut_ft_manager_init (SalutFtManager *obj)
{
  SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (obj);
  priv->xmpp_connection_manager = NULL;
  priv->contact_manager = NULL;
  priv->connection = NULL;

  /* allocate any data required by the object here */
  priv->channels = g_list_alloc ();
}

static gboolean
message_stanza_filter (SalutXmppConnectionManager *mgr,
                       GibberXmppConnection *conn,
                       GibberXmppStanza *stanza,
                       SalutContact *contact,
                       gpointer user_data)
{
  return gibber_file_transfer_is_file_offer (stanza);
}

static void
message_stanza_callback (SalutXmppConnectionManager *mgr,
                         GibberXmppConnection *conn,
                         GibberXmppStanza *stanza,
                         SalutContact *contact,
                         gpointer user_data)
{
  SalutFtManager *self = SALUT_FT_MANAGER (user_data);
  SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (self);
  SalutFileChannel *chan;
  TpHandle handle;
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->connection);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (base_conn,
       TP_HANDLE_TYPE_CONTACT);

  handle = tp_handle_lookup (handle_repo, contact->name, NULL, NULL);
  g_assert (handle != 0);

  chan = salut_ft_manager_new_channel (self, handle, TRUE);
  salut_file_channel_received_file_offer (chan, stanza, conn);
}

static void salut_ft_manager_dispose (GObject *object);
static void salut_ft_manager_finalize (GObject *object);

static void
salut_ft_manager_class_init (SalutFtManagerClass *salut_ft_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_ft_manager_class);

  g_type_class_add_private (salut_ft_manager_class,
                            sizeof (SalutFtManagerPrivate));

  object_class->dispose = salut_ft_manager_dispose;
  object_class->finalize = salut_ft_manager_finalize;
}

void
salut_ft_manager_dispose (GObject *object)
{
  SalutFtManager *self = SALUT_FT_MANAGER (object);
  SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->xmpp_connection_manager != NULL)
    {
      salut_xmpp_connection_manager_remove_stanza_filter (
          priv->xmpp_connection_manager, NULL,
          message_stanza_filter, message_stanza_callback, self);

      g_object_unref (priv->xmpp_connection_manager);
      priv->xmpp_connection_manager = NULL;
    }

  if (priv->contact_manager != NULL)
    {
      g_object_unref (priv->contact_manager);
      priv->contact_manager = NULL;
    }

  if (priv->channels)
    g_list_free (priv->channels);

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_ft_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_ft_manager_parent_class)->dispose (object);
}

void
salut_ft_manager_finalize (GObject *object)
{
  /*SalutFtManager *self = SALUT_FT_MANAGER (object);*/
  /*SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (self);*/

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (salut_ft_manager_parent_class)->finalize (object);
}

/* Channel Factory interface */

static void
salut_ft_manager_factory_iface_close_all (TpChannelFactoryIface *iface)
{
  SalutFtManager *mgr = SALUT_FT_MANAGER (iface);
  SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (mgr);

  if (priv->channels)
    g_list_free (priv->channels);
}

static void
salut_ft_manager_factory_iface_connecting (TpChannelFactoryIface *iface)
{
}

static void
salut_ft_manager_factory_iface_connected (TpChannelFactoryIface *iface)
{
}

static void
salut_ft_manager_factory_iface_disconnected (TpChannelFactoryIface *iface)
{
  /* FIXME close all channels ? */
}

struct foreach_data {
  TpChannelFunc func;
  gpointer data;
};

static void
salut_ft_manager_iface_foreach_one (gpointer value,
                                    gpointer data)
{
  if (!value)
    return;
  TpChannelIface *chan = TP_CHANNEL_IFACE (value);
  struct foreach_data *f = (struct foreach_data *) data;

  f->func (chan, f->data);
}

static void
salut_ft_manager_factory_iface_foreach (TpChannelFactoryIface *iface,
                                        TpChannelFunc func, gpointer data)
{
  SalutFtManager *mgr = SALUT_FT_MANAGER (iface);
  SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (mgr);
  struct foreach_data f;
  f.func = func;
  f.data = data;

  g_list_foreach (priv->channels, (GFunc) salut_ft_manager_iface_foreach_one, &f);
}

static void
file_channel_closed_cb (SalutFileChannel *chan, gpointer user_data)
{
  SalutFtManager *self = SALUT_FT_MANAGER (user_data);
  SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (self);
  TpHandle handle;

  if (priv->channels)
    {
      g_object_get (chan, "handle", &handle, NULL);
      DEBUG ("Removing channel with handle %d", handle);
      priv->channels = g_list_remove (priv->channels, chan);
    }
}

static SalutFileChannel *
salut_ft_manager_new_channel (SalutFtManager *mgr,
                              TpHandle handle,
                              gboolean incoming)
{
  SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (mgr);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION (priv->connection);
  TpHandleRepoIface *handle_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_CONTACT);
  SalutFileChannel *chan;
  SalutContact *contact;
  const gchar *name;
  gchar *path = NULL;
  guint direction, state;

  DEBUG ("Requested channel for handle: %d", handle);

  contact = salut_contact_manager_get_contact (priv->contact_manager, handle);
  if (contact == NULL)
    {
      return NULL;
    }

  state = incoming ? SALUT_FILE_TRANSFER_STATE_LOCAL_PENDING : SALUT_FILE_TRANSFER_STATE_REMOTE_PENDING;
  direction = incoming ? SALUT_FILE_TRANSFER_DIRECTION_INCOMING : SALUT_FILE_TRANSFER_DIRECTION_OUTGOING;

  name = tp_handle_inspect (handle_repo, handle);
  path = g_strdup_printf ("%s/FileChannel/%u",
                         base_connection->object_path, handle);
  chan = g_object_new (SALUT_TYPE_FILE_CHANNEL,
                       "connection", priv->connection,
                       "contact", contact,
                       "object-path", path,
                       "handle", handle,
                       "xmpp-connection-manager", priv->xmpp_connection_manager,
                       "direction", direction,
                       "state", state,
                       NULL);
  g_object_unref (contact);
  g_free (path);
  tp_channel_factory_iface_emit_new_channel (mgr, TP_CHANNEL_IFACE (chan),
      NULL);
  g_signal_connect (chan, "closed", G_CALLBACK (file_channel_closed_cb), mgr);

  priv->channels = g_list_append (priv->channels, chan);

  return chan;
}

static TpChannelFactoryRequestStatus
salut_ft_manager_factory_iface_request (TpChannelFactoryIface *iface,
                                        const gchar *chan_type,
                                        TpHandleType handle_type,
                                        guint handle,
                                        gpointer request,
                                        TpChannelIface **ret,
                                        GError **error)
{
  SalutFtManager *mgr = SALUT_FT_MANAGER (iface);
  SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (mgr);
  SalutFileChannel *chan;
  TpBaseConnection *base_connection = TP_BASE_CONNECTION (priv->connection);
  TpHandleRepoIface *handle_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_CONTACT);

  DEBUG ("File transfer request");

  /* We only support file transfer channels */
  if (tp_strdiff (chan_type, SALUT_IFACE_CHANNEL_TYPE_FILE))
    {
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;
    }

  /* And only contact handles */
  if (handle_type != TP_HANDLE_TYPE_CONTACT)
    {
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;
    }

  /* Must be a valid contact handle */
  if (!tp_handle_is_valid (handle_repo, TP_HANDLE_TYPE_CONTACT, NULL))
    {
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;
    }

  /* Don't support opening a channel to our self handle */
  if (handle == base_connection->self_handle)
    {
       return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;
    }

  chan = salut_ft_manager_new_channel (mgr, handle, FALSE);
  *ret = TP_CHANNEL_IFACE (chan);

  return TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED;
}

static void salut_ft_manager_factory_iface_init (gpointer *g_iface,
                                                 gpointer *iface_data)
{
   TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *)g_iface;

   klass->close_all = salut_ft_manager_factory_iface_close_all;
   klass->connecting = salut_ft_manager_factory_iface_connecting;
   klass->connected = salut_ft_manager_factory_iface_connected;
   klass->disconnected = salut_ft_manager_factory_iface_disconnected;
   klass->foreach = salut_ft_manager_factory_iface_foreach;
   klass->request = salut_ft_manager_factory_iface_request;
}

/* public functions */
SalutFtManager *
salut_ft_manager_new (SalutConnection *connection,
                      SalutContactManager *contact_manager,
                      SalutXmppConnectionManager *xmpp_connection_manager)
{
  SalutFtManager *ret = NULL;
  SalutFtManagerPrivate *priv;

  g_assert (connection != NULL);
  g_assert (xmpp_connection_manager != NULL);

  ret = g_object_new (SALUT_TYPE_FT_MANAGER, NULL);
  priv = SALUT_FT_MANAGER_GET_PRIVATE (ret);

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
