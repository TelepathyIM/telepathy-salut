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

static SalutIMChannel *
salut_im_manager_new_channel(SalutImManager *mgr, Handle handle);

G_DEFINE_TYPE_WITH_CODE(SalutImManager, salut_im_manager, 
                        G_TYPE_OBJECT,
              G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE, 
                                     salut_im_manager_factory_iface_init));

/* signal enum */
/*
enum
{
  IM_STATUS_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};
*/

/* private structure */
typedef struct _SalutImManagerPrivate SalutImManagerPrivate;

struct _SalutImManagerPrivate
{
  SalutContactManager *contact_manager;
  SalutConnection *connection;
  GHashTable *channels;
  gboolean dispose_has_run;
};

#define SALUT_IM_MANAGER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_IM_MANAGER, SalutImManagerPrivate))

static void
salut_im_manager_init (SalutImManager *obj)
{
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (obj);
  /* allocate any data required by the object here */
  priv->channels = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                         NULL, g_object_unref);
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
  SalutIMChannel *chan;

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
im_channel_closed_cb(SalutIMChannel *chan, gpointer user_data) {
  SalutImManager *self = SALUT_IM_MANAGER(user_data);
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE(self);
  Handle handle;

  if (priv->channels) { 
    g_object_get(chan, "handle", &handle, NULL);
    DEBUG("Removing channel with handle %d", handle);
    g_hash_table_remove(priv->channels, GINT_TO_POINTER(handle));
  }
}

static SalutIMChannel *
salut_im_manager_new_channel(SalutImManager *mgr, Handle handle) {
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE(mgr);
  SalutIMChannel *chan;
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

void
salut_im_manager_handle_connection(SalutImManager *mgr,
                                        SalutLmConnection *connection) {
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE(mgr);
  SalutContact *contact;
  SalutIMChannel *chan;
  Handle handle;
  struct sockaddr_storage addr;
  size_t size = sizeof(struct sockaddr_storage);
  /* FIXME we assume that one box has only one user... We can only know for
   * sure who we're talking too when they sent the first message */
  if (!salut_lm_connection_get_address(connection, &addr, &size)) {
    goto notfound;
  }

  contact = 
    salut_contact_manager_find_contact_by_address(priv->contact_manager, &addr);

  if (contact == NULL) {
    goto notfound;
  }
  handle = handle_for_contact(priv->connection->handle_repo, contact->name);

  chan = g_hash_table_lookup(priv->channels, GINT_TO_POINTER(handle));
  if (chan == NULL) {
    chan = salut_im_manager_new_channel(mgr, handle);
  }
  salut_im_channel_add_connection(chan, connection);

  return ;
notfound:
  g_object_unref(connection);
}
