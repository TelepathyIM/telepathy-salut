/*
 * salut-contact-manager.c - Source for SalutContactManager
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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "salut-connection.h"
#include "salut-contact-channel.h"
#include "salut-contact-manager.h"
#include "signals-marshal.h"
#include "salut-contact.h"
#include "salut-presence-enumtypes.h"

#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG DEBUG_CONTACTS
#include "debug.h"

static void salut_contact_manager_factory_iface_init(gpointer *g_iface,
    gpointer *iface_data);

static SalutContactChannel *salut_contact_manager_get_channel
    (SalutContactManager *mgr, TpHandle handle, gboolean *created);

static void
_contact_finalized_cb(gpointer data, GObject *old_object);

G_DEFINE_TYPE_WITH_CODE(SalutContactManager, salut_contact_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE,
      salut_contact_manager_factory_iface_init));

enum
{
  PROP_CONNECTION = 1,
  LAST_PROP
};

/* signal enum */
enum
{
  CONTACT_CHANGE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _SalutContactManagerPrivate SalutContactManagerPrivate;

struct _SalutContactManagerPrivate
{
  GHashTable *channels;
  gboolean dispose_has_run;
};

#define SALUT_CONTACT_MANAGER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_CONTACT_MANAGER, SalutContactManagerPrivate))

static void
salut_contact_manager_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
  SalutContactManager *self = SALUT_CONTACT_MANAGER (object);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, self->connection);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_contact_manager_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  SalutContactManager *self = SALUT_CONTACT_MANAGER (object);

  switch (property_id)
    {
      case PROP_CONNECTION:
        self->connection = g_value_get_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
salut_contact_manager_constructor (GType type,
                                   guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj;

  obj = G_OBJECT_CLASS (salut_contact_manager_parent_class)->
    constructor (type, n_props, props);

  return obj;
}

static void
salut_contact_manager_init (SalutContactManager *obj)
{
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  priv->channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);
  obj->contacts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void salut_contact_manager_dispose (GObject *object);
static void salut_contact_manager_finalize (GObject *object);

static void
salut_contact_manager_class_init (SalutContactManagerClass *salut_contact_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_contact_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_contact_manager_class, sizeof (SalutContactManagerPrivate));

  object_class->constructor = salut_contact_manager_constructor;
  object_class->get_property = salut_contact_manager_get_property;
  object_class->set_property = salut_contact_manager_set_property;

  object_class->dispose = salut_contact_manager_dispose;
  object_class->finalize = salut_contact_manager_finalize;

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "The Salut Connection associated with this contact manager",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION,
      param_spec);

  signals[CONTACT_CHANGE] = g_signal_new("contact-change",
      G_OBJECT_CLASS_TYPE(salut_contact_manager_class),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      salut_signals_marshal_VOID__OBJECT_INT,
      G_TYPE_NONE, 2,
      SALUT_TYPE_CONTACT,
      G_TYPE_INT);
}

static gboolean
dispose_contact(gpointer key, gpointer value, gpointer object) {
  SalutContact *contact = SALUT_CONTACT(value);
  SalutContactManager *self = SALUT_CONTACT_MANAGER (object);

  g_object_weak_unref(G_OBJECT(contact), _contact_finalized_cb, object);
  g_signal_handlers_disconnect_matched(contact, G_SIGNAL_MATCH_DATA,
                                       0, 0, NULL, NULL, object);

  SALUT_CONTACT_MANAGER_GET_CLASS (self)->dispose_contact (self, contact);
  return TRUE;
}

static void
salut_contact_manager_dispose (GObject *object)
{
  SalutContactManager *self = SALUT_CONTACT_MANAGER (object);
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  DEBUG("Disposing contact manager");

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */
  tp_channel_factory_iface_close_all (TP_CHANNEL_FACTORY_IFACE (object));

  if (G_OBJECT_CLASS (salut_contact_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_contact_manager_parent_class)->dispose (object);
}

void
salut_contact_manager_finalize (GObject *object)
{
  //SalutContactManager *self = SALUT_CONTACT_MANAGER (object);
  //SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (salut_contact_manager_parent_class)->finalize (object);
}

static void
change_all_groups(SalutContactManager *mgr, TpIntSet *add, TpIntSet *rem) {
  TpHandle i;
  SalutContactChannel *c;
  TpIntSet *empty = tp_intset_new();
  for (i = LIST_HANDLE_FIRST; i <= LIST_HANDLE_LAST; i++) {
    c = salut_contact_manager_get_channel(mgr, i, NULL);
    tp_group_mixin_change_members(G_OBJECT(c),
                                  "", add, rem,
                                  empty, empty, 0, 0);
  }
  tp_intset_destroy(empty);
}

static void
contact_found_cb(SalutContact *contact, gpointer userdata) {
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER(userdata);
  TpIntSet *to_add = tp_intset_new();
  TpIntSet *to_rem = tp_intset_new();

  tp_intset_add(to_add, contact->handle);
  change_all_groups(mgr, to_add, to_rem);
  tp_intset_destroy(to_add);
  tp_intset_destroy(to_rem);
}

static void
contact_change_cb(SalutContact *contact, gint changes, gpointer userdata) {
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER(userdata);

  DEBUG("Emitting contact changes for %s: %d", contact->name, changes);

  g_signal_emit(mgr, signals[CONTACT_CHANGE], 0, contact, changes);
}

static void
contact_lost_cb(SalutContact *contact, gpointer userdata) {
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER(userdata);

  TpIntSet *to_add = tp_intset_new();
  TpIntSet *to_rem = tp_intset_new();

  DEBUG("Removing %s from contacts", contact->name);

  tp_intset_add(to_rem, contact->handle);
  change_all_groups(mgr, to_add, to_rem);

  tp_intset_destroy(to_add);
  tp_intset_destroy(to_rem);
  /* FIXME: the contact was reffed in the the avahi-mgr */
  g_object_unref(contact);
}

static gboolean
_contact_remove_finalized(gpointer key, gpointer value, gpointer data) {
  return data == value;
}

static void
_contact_finalized_cb(gpointer data, GObject *old_object) {
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER(data);
  g_hash_table_foreach_remove (mgr->contacts, _contact_remove_finalized,
      old_object);
}

void
salut_contact_manager_contact_created (SalutContactManager *self,
                                       SalutContact *contact)
{
  g_assert (g_hash_table_lookup (self->contacts, contact->name) == NULL);

  g_hash_table_insert (self->contacts, g_strdup (contact->name), contact);
  DEBUG("Adding %s to contacts", contact->name);

  g_signal_connect (contact, "found",
      G_CALLBACK(contact_found_cb), self);
  g_signal_connect (contact, "contact-change",
      G_CALLBACK(contact_change_cb), self);
  g_signal_connect (contact, "lost",
      G_CALLBACK(contact_lost_cb), self);

  g_object_weak_ref (G_OBJECT (contact), _contact_finalized_cb , self);
}

SalutContact *
salut_contact_manager_ensure_contact (SalutContactManager *self,
                                      const gchar *name)
{
  SalutContact *contact;

  contact = g_hash_table_lookup (self->contacts, name);
  if (contact == NULL)
    {
      DEBUG ("contact %s doesn't exist yet. Creating it", name);
      contact = SALUT_CONTACT_MANAGER_GET_CLASS (self)->create_contact (self,
          name);
      salut_contact_manager_contact_created (self, contact);
    }
  else
    {
      g_object_ref (contact);
    }

  return contact;
}

static void
salut_contact_manager_factory_iface_close_all(TpChannelFactoryIface *iface) {
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER(iface);
  SalutContactManagerPrivate *priv =
    SALUT_CONTACT_MANAGER_GET_PRIVATE(mgr);

  SALUT_CONTACT_MANAGER_GET_CLASS (mgr)->close_all (mgr);

  if (priv->channels) {
    g_hash_table_destroy(priv->channels);
    priv->channels = NULL;
  }

  if (mgr->contacts) {
    g_hash_table_foreach_remove (mgr->contacts, dispose_contact, mgr);
    g_hash_table_destroy (mgr->contacts);
    mgr->contacts = NULL;
  }
}

static void
salut_contact_manager_factory_iface_connecting(TpChannelFactoryIface *iface) {
}

static void
salut_contact_manager_factory_iface_connected(TpChannelFactoryIface *iface) {

}

static void
salut_contact_manager_factory_iface_disconnected(TpChannelFactoryIface *iface) {
}

struct foreach_data {
  TpChannelFunc func;
  gpointer data;
};

static void
salut_contact_manager_iface_foreach_one(gpointer key,
                                        gpointer value,
                                        gpointer data)  {
  TpChannelIface *chan = TP_CHANNEL_IFACE(value);
  struct foreach_data *f = (struct foreach_data *) data;

  f->func(chan, f->data);
}

static void
salut_contact_manager_factory_iface_foreach(TpChannelFactoryIface *iface,
                                            TpChannelFunc func, gpointer data) {
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER(iface);
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE(mgr);
  struct foreach_data f;
  f.func = func;
  f.data = data;

  g_hash_table_foreach(priv->channels,
                       salut_contact_manager_iface_foreach_one,
                       &f);

}

static TpChannelFactoryRequestStatus
salut_contact_manager_factory_iface_request(TpChannelFactoryIface *iface,
                                             const gchar *chan_type,
                                             TpHandleType handle_type,
                                             guint handle,
                                             gpointer request,
                                             TpChannelIface **ret,
                                             GError **error) {
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER(iface);
  SalutContactChannel *chan;
  gboolean created;
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles(
      TP_BASE_CONNECTION (mgr->connection), TP_HANDLE_TYPE_LIST);

  /* We only support contact list channels */
  if (tp_strdiff(chan_type, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST)) {
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;
  }

  /* And thus only support list handles */
  if (handle_type != TP_HANDLE_TYPE_LIST) {
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;
  }

  /* Most be a valid list handle */
  if (!tp_handle_is_valid(handle_repo, handle, NULL)) {
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;
  }

  chan = salut_contact_manager_get_channel(mgr, handle, &created);
  *ret = TP_CHANNEL_IFACE(chan);
  return created ? TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED
                 : TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING;
}

static void salut_contact_manager_factory_iface_init(gpointer *g_iface,
                                                     gpointer *iface_data) {
   TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *)g_iface;

   klass->close_all = salut_contact_manager_factory_iface_close_all;
   klass->connecting = salut_contact_manager_factory_iface_connecting;
   klass->connected = salut_contact_manager_factory_iface_connected;
   klass->disconnected = salut_contact_manager_factory_iface_disconnected;
   klass->foreach = salut_contact_manager_factory_iface_foreach;
   klass->request = salut_contact_manager_factory_iface_request;
}

/* private functions */
static SalutContactChannel *
salut_contact_manager_new_channel (SalutContactManager *mgr,
                                   TpHandle handle)
{
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (mgr);
  TpBaseConnection *base_conn = (TpBaseConnection *) (mgr->connection);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (base_conn,
       TP_HANDLE_TYPE_LIST);
  SalutContactChannel *chan;
  const gchar *name;
  gchar *path;

  g_assert (g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (handle))
             == NULL);

  name = tp_handle_inspect (handle_repo, handle);
  path = g_strdup_printf ("%s/ContactChannel/%s", base_conn->object_path,
      name);

  chan = g_object_new (SALUT_TYPE_CONTACT_CHANNEL,
      "connection", mgr->connection,
      "object-path", path,
      "handle", handle,
      NULL);
  g_free (path);
  g_hash_table_insert (priv->channels, GUINT_TO_POINTER (handle), chan);
  tp_channel_factory_iface_emit_new_channel (mgr, TP_CHANNEL_IFACE (chan),
      NULL);

  return chan;
}

static SalutContactChannel *
salut_contact_manager_get_channel (SalutContactManager *mgr,
                                   TpHandle handle,
                                   gboolean *created)
{
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (mgr);
  SalutContactChannel *chan;

  chan = g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (handle));
  if (created != NULL)
    {
      *created = (chan == NULL);
    }
  if (chan == NULL)
    {
      chan = salut_contact_manager_new_channel (mgr, handle);
    }

  return chan;
}

/* public functions */
gboolean
salut_contact_manager_start (SalutContactManager *self,
                             GError **error)
{
  return SALUT_CONTACT_MANAGER_GET_CLASS (self)->start (self, error);
}

SalutContact *
salut_contact_manager_get_contact(SalutContactManager *mgr, TpHandle handle) {
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles(
      TP_BASE_CONNECTION (mgr->connection), TP_HANDLE_TYPE_CONTACT);
  const char *name = tp_handle_inspect(handle_repo, handle);
  SalutContact *ret;

  g_return_val_if_fail(name, NULL);

  DEBUG("Getting contact for: %s", name);
  ret =  g_hash_table_lookup(mgr->contacts, name);

  if (ret != NULL) {
    g_object_ref(ret);
  } else {
    DEBUG("Failed to get contact for %s", name);
  }

  return ret;
}

static void
_find_by_address(gpointer key, gpointer value, gpointer user_data) {
  struct sockaddr_storage *address =
    (struct sockaddr_storage *)((gpointer *)user_data)[0];
  GList **list = (GList **)((gpointer *)user_data)[1];
  SalutContact *contact = SALUT_CONTACT(value);
  if (salut_contact_has_address(contact, address)) {
    g_object_ref(contact);
    *list = g_list_append(*list, contact);
  }
}

/* FIXME function name is just too long */
GList *
salut_contact_manager_find_contacts_by_address(SalutContactManager *mgr,
                                               struct sockaddr_storage *address)
{
  GList *list = NULL;
  gpointer data[2];

  data[0] = address;
  data[1] = &list;
  g_hash_table_foreach (mgr->contacts, _find_by_address, data);
  return list;
}
