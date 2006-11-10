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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "salut-connection.h"
#include "salut-contact-channel.h"
#include "salut-contact-manager.h"
#include "salut-contact-manager-signals-marshal.h"
#include "salut-contact.h"
#include "salut-presence-enumtypes.h"

#include "salut-avahi-service-browser.h"
#include "salut-avahi-enums.h"

#include "telepathy-errors.h"
#include "telepathy-interfaces.h"
#include "telepathy-constants.h"
#include "tp-channel-factory-iface.h"
#include "handle-types.h"
#include "gintset.h"

#define DEBUG_FLAG DEBUG_CONTACTS
#include "debug.h"


static void salut_contact_manager_factory_iface_init(gpointer *g_iface, 
                                                     gpointer *iface_data);
static SalutContactChannel *
salut_contact_manager_get_channel(SalutContactManager *mgr, Handle handle); 
                                                    
G_DEFINE_TYPE_WITH_CODE(SalutContactManager, salut_contact_manager, 
                        G_TYPE_OBJECT,
              G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE, 
                                     salut_contact_manager_factory_iface_init));

/* signal enum */
enum
{
  CONTACT_STATUS_CHANGED,
  CONTACT_ALIAS_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _SalutContactManagerPrivate SalutContactManagerPrivate;

struct _SalutContactManagerPrivate
{
  SalutAvahiServiceBrowser *browser;
  SalutConnection *connection;
  SalutAvahiClient *client;
  GHashTable *channels;
  GHashTable *contacts;
  gboolean dispose_has_run;
};

#define SALUT_CONTACT_MANAGER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_CONTACT_MANAGER, SalutContactManagerPrivate))

static void
salut_contact_manager_init (SalutContactManager *obj)
{
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  priv->channels = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                         NULL, g_object_unref);
  priv->contacts = g_hash_table_new_full(g_str_hash, g_str_equal,
                                         g_free, NULL);
}

static void salut_contact_manager_dispose (GObject *object);
static void salut_contact_manager_finalize (GObject *object);

static void
salut_contact_manager_class_init (SalutContactManagerClass *salut_contact_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_contact_manager_class);

  g_type_class_add_private (salut_contact_manager_class, sizeof (SalutContactManagerPrivate));

  object_class->dispose = salut_contact_manager_dispose;
  object_class->finalize = salut_contact_manager_finalize;

  signals[CONTACT_STATUS_CHANGED] = g_signal_new("contact-status-changed",
                                G_OBJECT_CLASS_TYPE(salut_contact_manager_class),
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL, NULL,
                                salut_contact_manager_marshal_VOID__OBJECT_INT_STRING,
                                G_TYPE_NONE, 3,
                                SALUT_TYPE_CONTACT,
                                SALUT_TYPE_PRESENCE_ID,
                                G_TYPE_STRING);

  signals[CONTACT_ALIAS_CHANGED] = g_signal_new("contact-alias-changed",
                                G_OBJECT_CLASS_TYPE(salut_contact_manager_class),
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL, NULL,
                                salut_contact_manager_marshal_VOID__OBJECT_STRING,
                                G_TYPE_NONE, 2,
                                SALUT_TYPE_CONTACT,
                                G_TYPE_STRING);
}

void
salut_contact_manager_dispose (GObject *object)
{
  SalutContactManager *self = SALUT_CONTACT_MANAGER (object);
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;
  
  priv->dispose_has_run = TRUE;
  if (priv->connection) {
    g_object_unref(priv->connection);
    priv->connection = NULL;
  }

  if (priv->client) {
    g_object_unref(priv->client);
    priv->client = NULL;
  }

  if (priv->browser) {
    g_object_unref(priv->browser);
    priv->browser = NULL;
  }

  if (priv->contacts) {
    g_hash_table_destroy(priv->contacts);
    priv->contacts = NULL;
  }

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
change_all_groups(SalutContactManager *mgr, GIntSet *add, GIntSet *rem) {
  Handle i;
  SalutContactChannel *c;
  GIntSet *empty = g_intset_new();
  for (i = LIST_HANDLE_FIRST; i <= LIST_HANDLE_LAST; i++) {
    c = salut_contact_manager_get_channel(mgr, i);
    group_mixin_change_members(G_OBJECT(c), "", add, rem, 
                                empty, empty, 0, 0);
  }
  g_intset_destroy(empty);
}

static void 
contact_found_cb(SalutContact *contact, gpointer userdata) {
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER(userdata);
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE(mgr);

  GIntSet *to_add = g_intset_new();
  GIntSet *to_rem = g_intset_new();
  Handle handle;

  handle = handle_for_contact(priv->connection->handle_repo, contact->name);
  g_intset_add(to_add, handle);
  change_all_groups(mgr, to_add, to_rem);
  /* Add an extra ref, to ensure keeping this untill we got the lost signal */
  g_object_ref(contact);
}

static void
contact_state_changed_cb(SalutContact *contact, SalutPresenceId status, 
                      gchar *message, gpointer userdata) {
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER(userdata);

  g_signal_emit(mgr, signals[CONTACT_STATUS_CHANGED], 0,
                contact, status, message);
}

static void
contact_alias_changed_cb(SalutContact *contact, 
                         gchar *alias, gpointer userdata) {
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER(userdata);

  g_signal_emit(mgr, signals[CONTACT_ALIAS_CHANGED], 0, contact, alias);
}

static void
contact_lost_cb(SalutContact *contact, gpointer userdata) {
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER(userdata);
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE(mgr);
  GIntSet *to_add = g_intset_new();
  GIntSet *to_rem = g_intset_new();
  Handle handle;

  DEBUG("Removing %s from contacts", contact->name);
  handle = handle_for_contact(priv->connection->handle_repo, contact->name);
  g_intset_add(to_rem, handle);
  change_all_groups(mgr, to_add, to_rem);
  g_object_unref(contact);
}

static gboolean 
_contact_remove_finalized(gpointer key, gpointer value, gpointer data) {
  return data == value;
}

static void
_contact_finalized_cb(gpointer data, GObject *old_object) {
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER(data);
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE(mgr);
  g_hash_table_foreach_remove(priv->contacts, 
                              _contact_remove_finalized, 
                              old_object);
}

static void
browser_found(SalutAvahiServiceBrowser *browser,
              AvahiIfIndex interface, AvahiProtocol protocol, 
              const char *name, const char *type, const char *domain,
              SalutAvahiLookupResultFlags flags,
              gpointer userdata) {
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER(userdata);
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE(mgr);
  SalutContact *contact;

  if (flags & AVAHI_LOOKUP_RESULT_OUR_OWN) 
    return;

  /* FIXME: For now we assume name is unique on the lan */
  contact = g_hash_table_lookup(priv->contacts, name);
  if (contact == NULL) {
    contact = salut_contact_new(priv->client, name);
    g_hash_table_insert(priv->contacts, g_strdup(contact->name), contact);
    DEBUG("Adding %s to contacts", name);
    g_signal_connect(contact, "found", 
                     G_CALLBACK(contact_found_cb), mgr);
    g_signal_connect(contact, "status-changed", 
                     G_CALLBACK(contact_state_changed_cb), mgr);
    g_signal_connect(contact, "alias-changed", 
                     G_CALLBACK(contact_alias_changed_cb), mgr);
    g_signal_connect(contact, "lost", 
                     G_CALLBACK(contact_lost_cb), mgr);
    g_object_weak_ref(G_OBJECT(contact), _contact_finalized_cb , mgr);
  } else {
    g_object_ref(contact);
  }
  salut_contact_add_service(contact, interface, protocol, name, type, domain);
}

static void
browser_removed(SalutAvahiServiceBrowser *browser,
              AvahiIfIndex interface, AvahiProtocol protocol, 
              const char *name, const char *type, const char *domain,
              SalutAvahiLookupResultFlags flags,
              gpointer userdata) {
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER(userdata);
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE(mgr);
  SalutContact *contact = g_hash_table_lookup(priv->contacts, name);

  DEBUG("Browser removed for %s", name);
  if (contact != NULL) {
    salut_contact_remove_service(contact, interface, protocol, 
                                 name, type, domain);
    g_object_unref(contact);
  } else {
    g_message("Unknown contact removed from service browser");
  }
}

static void
browser_failed(SalutAvahiServiceBrowser *browser, 
               GError *error, gpointer userdata) {
  /* FIXME proper error handling */
  g_warning("browser failed -> %s", error->message);
}

static void
salut_contact_manager_factory_iface_close_all(TpChannelFactoryIface *iface) {
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER(iface);
  SalutContactManagerPrivate *priv = 
    SALUT_CONTACT_MANAGER_GET_PRIVATE(mgr);

  if (priv->channels) {
    g_hash_table_destroy(priv->channels);
    priv->channels = NULL;
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
                                             TpChannelIface **ret) {
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER(iface);
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE(mgr);
  SalutContactChannel *chan;

  /* We only support contact list channels */
  if (strcmp(chan_type, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST)) {
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;
  }

  /* And thus only support list handles */
  if (handle_type != TP_HANDLE_TYPE_LIST) {
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;
  }

  /* Most be a valid list handle */
  if (!handle_is_valid(priv->connection->handle_repo, TP_HANDLE_TYPE_LIST,
                       handle, NULL)) {
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;
  }
  
  chan = salut_contact_manager_get_channel(mgr, handle);
  *ret = TP_CHANNEL_IFACE(chan);
  return TP_CHANNEL_FACTORY_REQUEST_STATUS_DONE;
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
salut_contact_manager_new_channel(SalutContactManager *mgr, 
                                         Handle handle) {
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE(mgr);
  SalutContactChannel *chan;
  const gchar *name;
  gchar *path;

  g_assert(g_hash_table_lookup(priv->channels, GINT_TO_POINTER(handle)) 
             == NULL);

  name = handle_inspect(priv->connection->handle_repo, 
                        TP_HANDLE_TYPE_LIST, handle);
  path = g_strdup_printf("%s/ContactChannel/%s", 
                         priv->connection->object_path, name);

  chan = g_object_new(SALUT_TYPE_CONTACT_CHANNEL,
                      "connection", priv->connection,
                      "object-path", path,
                      "handle", handle,
                      NULL);
  g_free(path);
  g_hash_table_insert(priv->channels, GINT_TO_POINTER(handle), chan);
  g_signal_emit_by_name(mgr, "new-channel", chan);

  return chan;
}

static SalutContactChannel *
salut_contact_manager_get_channel(SalutContactManager *mgr, Handle handle) {
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE(mgr);
  SalutContactChannel *chan;

  g_assert(handle_is_valid(priv->connection->handle_repo, TP_HANDLE_TYPE_LIST, 
                           handle, NULL));
  chan = g_hash_table_lookup(priv->channels, GINT_TO_POINTER(handle));
  if (chan == NULL) 
    chan= salut_contact_manager_new_channel(mgr, handle);

  return chan;
}

/* public functions */
SalutContactManager *
salut_contact_manager_new(SalutConnection *connection,
                          SalutAvahiClient *client) {
  SalutContactManager *ret = NULL; 
  SalutContactManagerPrivate *priv;

  ret = g_object_new(SALUT_TYPE_CONTACT_MANAGER, NULL);
  priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (ret);

  priv->browser = salut_avahi_service_browser_new("_presence._tcp");
  

  priv->client = client;
  g_object_ref(client);

  priv->connection = connection;
  g_object_ref(connection);

  g_signal_connect(priv->browser, "new-service",
                   G_CALLBACK(browser_found), ret);
  g_signal_connect(priv->browser, "removed-service",
                   G_CALLBACK(browser_removed), ret);
  g_signal_connect(priv->browser, "failure",
                   G_CALLBACK(browser_failed), ret);

  return ret;
}

gboolean 
salut_contact_manager_start(SalutContactManager *mgr, GError **error) {
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (mgr);
  if (!salut_avahi_service_browser_attach(priv->browser, priv->client, error)) {
    return FALSE;
  }
  return TRUE;
}

SalutContact *
salut_contact_manager_get_contact(SalutContactManager *mgr, Handle handle) {
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (mgr);
  const char *name = handle_inspect(priv->connection->handle_repo,
                                    TP_HANDLE_TYPE_CONTACT, handle);
  SalutContact *ret;

  g_return_val_if_fail(name, NULL);

  DEBUG("Getting contact for: %s", name);
  ret =  g_hash_table_lookup(priv->contacts, name);
  
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
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (mgr);
  gpointer data[2];

  data[0] = address;
  data[1] = &list;
  g_hash_table_foreach(priv->contacts, _find_by_address, data);
  return list;
}

