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
#include "salut-contact-manager-signals-marshal.h"
#include "salut-contact.h"
#include "salut-presence-enumtypes.h"

#include "salut-avahi-service-browser.h"
#include "salut-avahi-enums.h"

#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG DEBUG_CONTACTS
#include "debug.h"

static void salut_contact_manager_factory_iface_init(gpointer *g_iface, 
                                                     gpointer *iface_data);
static SalutContactChannel *
salut_contact_manager_get_channel(SalutContactManager *mgr, 
                                  TpHandle handle,
                                  gboolean *created); 

static void
_contact_finalized_cb(gpointer data, GObject *old_object);
                                                    
G_DEFINE_TYPE_WITH_CODE(SalutContactManager, salut_contact_manager, 
                        G_TYPE_OBJECT,
              G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE, 
                                     salut_contact_manager_factory_iface_init));

/* signal enum */
enum
{
  CONTACT_CHANGE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

typedef struct
{
  SalutContactManager *mgr;
  TpHandle room;
  gchar *activity_id;
  gchar *color;
  gchar *name;
  gchar *type;
  size_t refcount;
} SalutContactManagerActivity;

/* private structure */
typedef struct _SalutContactManagerPrivate SalutContactManagerPrivate;

struct _SalutContactManagerPrivate
{
  SalutAvahiServiceBrowser *presence_browser;
#ifdef ENABLE_OLPC
  SalutAvahiServiceBrowser *activity_browser;
  GHashTable *olpc_activities_by_mdns;
  GHashTable *olpc_activities_by_room;
#endif
  SalutConnection *connection;
  SalutAvahiClient *client;
  GHashTable *channels;
  GHashTable *contacts;
  gboolean dispose_has_run;
};

#define SALUT_CONTACT_MANAGER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_CONTACT_MANAGER, SalutContactManagerPrivate))

static SalutContactManagerActivity *
activity_new (SalutContactManager *mgr,
              TpHandle room_handle)
{
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (mgr);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles
    (TP_BASE_CONNECTION (priv->connection), TP_HANDLE_TYPE_ROOM);
  SalutContactManagerActivity *activity = g_slice_new0
    (SalutContactManagerActivity);

  activity->refcount = 1;
  tp_handle_ref (room_repo, room_handle);
  activity->room = room_handle;

  DEBUG ("Creating SalutContactManagerActivity: handle %u", room_handle);
  g_hash_table_insert (priv->olpc_activities_by_room,
      GUINT_TO_POINTER (room_handle), activity);

  return activity;
}

static void
activity_unref (SalutContactManagerActivity *activity)
{
  SalutContactManagerPrivate *priv;
  TpHandleRepoIface *room_repo;

  if (--activity->refcount != 0)
    return;

  priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (activity->mgr);
  room_repo = tp_base_connection_get_handles
    ((TpBaseConnection *) priv->connection, TP_HANDLE_TYPE_ROOM);

  DEBUG ("Dropping SalutContactManagerActivity: handle %u", activity->room);
  g_hash_table_remove (priv->olpc_activities_by_room,
      GUINT_TO_POINTER (activity->room));
  tp_handle_unref (room_repo, activity->room);

  g_free (activity->activity_id);
  g_free (activity->color);
  g_free (activity->name);
  g_free (activity->type);
  g_slice_free (SalutContactManagerActivity, activity);
}

static void
salut_contact_manager_init (SalutContactManager *obj)
{
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  priv->channels = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                         NULL, g_object_unref);
  priv->contacts = g_hash_table_new_full(g_str_hash, g_str_equal,
                                         g_free, NULL);

#ifdef ENABLE_OLPC
  priv->olpc_activities_by_mdns = g_hash_table_new_full (g_str_hash,
      g_str_equal, g_free, (GDestroyNotify) activity_unref);
  priv->olpc_activities_by_room = g_hash_table_new (g_direct_hash,
      g_direct_equal);
#endif
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

  signals[CONTACT_CHANGE] = g_signal_new("contact-change",
      G_OBJECT_CLASS_TYPE(salut_contact_manager_class),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      salut_contact_manager_marshal_VOID__OBJECT_INT,
      G_TYPE_NONE, 2,
      SALUT_TYPE_CONTACT,
      G_TYPE_INT);
}

static gboolean
dispose_contact(gpointer key, gpointer value, gpointer object) {
  SalutContact *contact = SALUT_CONTACT(value);

  g_object_weak_unref(G_OBJECT(contact), _contact_finalized_cb, object); 
  g_signal_handlers_disconnect_matched(contact, G_SIGNAL_MATCH_DATA,
                                       0, 0, NULL, NULL, object);

  if (salut_contact_has_services(contact)) {
    g_object_unref(contact);
  }

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
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE(mgr);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles(
      TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_CONTACT);

  TpIntSet *to_add = tp_intset_new();
  TpIntSet *to_rem = tp_intset_new();
  TpHandle handle;

  handle = tp_handle_ensure(handle_repo, contact->name, NULL, NULL);
  tp_intset_add(to_add, handle);
  change_all_groups(mgr, to_add, to_rem);
  /* Add an extra ref, to ensure keeping this untill we got the lost signal */
  tp_intset_destroy(to_add);
  tp_intset_destroy(to_rem);
}

#ifdef ENABLE_OLPC
static void
activity_change_cb(SalutContact *contact,
                   const gchar *service_name,
                   TpHandle room_handle,
                   const gchar *activity_id,
                   const gchar *color,
                   const gchar *name,
                   const gchar *type,
                   gpointer userdata)
{
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER (userdata);
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (mgr);
  gboolean changed = FALSE;
  SalutContactManagerActivity *activity;

  DEBUG ("enter: sn=%s, h=%u, aid=%s, c=%s, n=%s, t=%s",
      service_name, room_handle,
      activity_id ? activity_id : "<NULL>",
      color ? color : "<NULL>",
      name ? name : "<NULL>",
      type ? type : "<NULL>");

  activity = g_hash_table_lookup (priv->olpc_activities_by_mdns, service_name);
  if (activity == NULL)
    {
      activity = g_hash_table_lookup (priv->olpc_activities_by_room,
          GUINT_TO_POINTER (room_handle));
      if (activity == NULL)
        {
          activity = activity_new (mgr, room_handle);
          changed = TRUE;
        }
      else
        {
          activity->refcount++;
        }
      DEBUG ("Activity %u now advertised as %s", room_handle, service_name);
      g_hash_table_insert (priv->olpc_activities_by_mdns,
          g_strdup (service_name), activity);
    }
  else
    {
      DEBUG ("Activity %u already known to be advertised as %s", room_handle,
          service_name);
    }

  if (name != NULL && tp_strdiff (activity->name, name))
    {
      g_free (activity->name);
      activity->name = g_strdup (name);
      changed = TRUE;
    }

  if (type != NULL && tp_strdiff (activity->type, type))
    {
      g_free (activity->type);
      activity->type = g_strdup (type);
      changed = TRUE;
    }

  if (color != NULL && tp_strdiff (activity->color, color))
    {
      g_free (activity->color);
      activity->color = g_strdup (color);
      changed = TRUE;
    }
}
#endif

static void
contact_change_cb(SalutContact *contact, gint changes, gpointer userdata) {
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER(userdata);

  DEBUG("Emitting contact changes for %s: %d", contact->name, changes);

  g_signal_emit(mgr, signals[CONTACT_CHANGE], 0, contact, changes);
}

static void
contact_lost_cb(SalutContact *contact, gpointer userdata) {
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER(userdata);
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE(mgr);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles(
      TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_CONTACT);

  TpIntSet *to_add = tp_intset_new();
  TpIntSet *to_rem = tp_intset_new();
  TpHandle handle;

  DEBUG("Removing %s from contacts", contact->name);
  handle = tp_handle_lookup(handle_repo, contact->name, NULL, NULL);

  g_assert(handle != 0);

  tp_intset_add(to_rem, handle);
  change_all_groups(mgr, to_add, to_rem);

  tp_intset_destroy(to_add);
  tp_intset_destroy(to_rem);
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

#ifdef ENABLE_OLPC
gboolean
salut_contact_manager_get_olpc_activity_properties (SalutContactManager *self,
                                                    TpHandle handle,
                                                    const gchar **color,
                                                    const gchar **name,
                                                    const gchar **type)
{
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (self);
  SalutContactManagerActivity *activity = g_hash_table_lookup (
      priv->olpc_activities_by_room, GUINT_TO_POINTER (handle));

  if (activity == NULL)
    return FALSE;

  if (activity->color != NULL && color != NULL)
    *color = activity->color;
  if (activity->name != NULL && name != NULL)
    *name = activity->name;
  if (activity->type != NULL && type != NULL)
    *type = activity->type;
  return TRUE;
}

static gboolean
split_activity_name (const gchar **contact_name)
{
  const gchar *orig = *contact_name;

  *contact_name = strchr (*contact_name, ':');
  if (*contact_name == NULL)
    {
      *contact_name = orig;
      DEBUG ("Ignoring invalid _olpc-activity with no ':': %s", orig);
      return FALSE;
    }
  (*contact_name)++;
  return TRUE;
}
#endif

static void
browser_found(SalutAvahiServiceBrowser *browser,
              AvahiIfIndex interface, AvahiProtocol protocol, 
              const char *name, const char *type, const char *domain,
              SalutAvahiLookupResultFlags flags,
              gpointer userdata) {
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER(userdata);
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE(mgr);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles
      ((TpBaseConnection *) priv->connection, TP_HANDLE_TYPE_ROOM);
  SalutContact *contact;
  const char *contact_name = name;

  if (flags & AVAHI_LOOKUP_RESULT_OUR_OWN) 
    return;

#ifdef ENABLE_OLPC
  if (browser == priv->activity_browser)
    {
      if (!split_activity_name (&contact_name))
        return;
    }
#endif

  /* FIXME: For now we assume name is unique on the lan */
  contact = g_hash_table_lookup (priv->contacts, contact_name);
  if (contact == NULL) {
    contact = salut_contact_new(priv->client, room_repo, contact_name);
    g_hash_table_insert(priv->contacts, g_strdup(contact->name), contact);
    DEBUG("Adding %s to contacts", contact_name);
    g_signal_connect(contact, "found", 
                     G_CALLBACK(contact_found_cb), mgr);
    g_signal_connect(contact, "contact-change", 
                     G_CALLBACK(contact_change_cb), mgr);
#ifdef ENABLE_OLPC
    g_signal_connect(contact, "activity-change",
        G_CALLBACK(activity_change_cb), mgr);
#endif
    g_signal_connect(contact, "lost", 
                     G_CALLBACK(contact_lost_cb), mgr);
    g_object_weak_ref(G_OBJECT(contact), _contact_finalized_cb , mgr);
  } else  if (!salut_contact_has_services(contact)) {
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
  SalutContact *contact;
  const char *contact_name = name;

  DEBUG("Browser removed for %s", name);

#ifdef ENABLE_OLPC
  if (browser == priv->activity_browser)
    {
      if (!split_activity_name (&contact_name))
        return;
    }

  /* stop caring about this activity advertisement, and also the activity
   * if nobody is advertising it any more */
  DEBUG ("Activity %s no longer advertised", name);
  g_hash_table_remove (priv->olpc_activities_by_mdns, name);
#endif

  contact = g_hash_table_lookup (priv->contacts, contact_name);
  if (contact != NULL) {
    salut_contact_remove_service(contact, interface, protocol, 
                                 name, type, domain);
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

  if (priv->client) {
    g_object_unref(priv->client);
    priv->client = NULL;
  }

  if (priv->presence_browser)
    {
      g_object_unref (priv->presence_browser);
      priv->presence_browser = NULL;
    }

#ifdef ENABLE_OLPC
  if (priv->activity_browser)
    {
      g_object_unref (priv->activity_browser);
      priv->activity_browser = NULL;
    }
#endif

  if (priv->contacts) {
    g_hash_table_foreach_remove(priv->contacts, dispose_contact, mgr); 
    g_hash_table_destroy(priv->contacts);
    priv->contacts = NULL;
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
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE(mgr);
  SalutContactChannel *chan;
  gboolean created;
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles(
      TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_LIST);

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
salut_contact_manager_new_channel(SalutContactManager *mgr, 
                                         TpHandle handle) {
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE(mgr);
  TpBaseConnection *base_conn = TP_BASE_CONNECTION(priv->connection);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles(base_conn, 
       TP_HANDLE_TYPE_LIST);
  SalutContactChannel *chan;
  const gchar *name;
  gchar *path;

  g_assert(g_hash_table_lookup(priv->channels, GINT_TO_POINTER(handle)) 
             == NULL);

  name = tp_handle_inspect(handle_repo, handle);
  path = g_strdup_printf("%s/ContactChannel/%s", 
                         base_conn->object_path, name);

  chan = g_object_new(SALUT_TYPE_CONTACT_CHANNEL,
                      "connection", priv->connection,
                      "object-path", path,
                      "handle", handle,
                      NULL);
  g_free(path);
  g_hash_table_insert(priv->channels, GINT_TO_POINTER(handle), chan);
  tp_channel_factory_iface_emit_new_channel(mgr, TP_CHANNEL_IFACE(chan), NULL);

  return chan;
}

static SalutContactChannel *
salut_contact_manager_get_channel(SalutContactManager *mgr, 
    TpHandle handle, gboolean *created) 
{
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE(mgr);
  SalutContactChannel *chan;

  chan = g_hash_table_lookup(priv->channels, GINT_TO_POINTER(handle));
  if (created != NULL) {
    *created = (chan == NULL);
  }
  if (chan == NULL) {
    chan= salut_contact_manager_new_channel(mgr, handle);
  }

  return chan;
}

/* public functions */
SalutContactManager *
salut_contact_manager_new(SalutConnection *connection) {
  SalutContactManager *ret = NULL; 
  SalutContactManagerPrivate *priv;

  ret = g_object_new(SALUT_TYPE_CONTACT_MANAGER, NULL);
  priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (ret);

  priv->connection = connection;

  priv->presence_browser = salut_avahi_service_browser_new ("_presence._tcp");
#ifdef ENABLE_OLPC
  priv->activity_browser = salut_avahi_service_browser_new
    ("_olpc-activity._udp");
#endif

  return ret;
}

gboolean 
salut_contact_manager_start(SalutContactManager *mgr, 
                            SalutAvahiClient *client, GError **error) {
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (mgr);

  g_assert(priv->client == NULL);

  priv->client = client;
  g_object_ref(client);

  g_signal_connect(priv->presence_browser, "new-service",
                   G_CALLBACK(browser_found), mgr);
  g_signal_connect(priv->presence_browser, "removed-service",
                   G_CALLBACK(browser_removed), mgr);
  g_signal_connect(priv->presence_browser, "failure",
                   G_CALLBACK(browser_failed), mgr);

  if (!salut_avahi_service_browser_attach(priv->presence_browser,
        priv->client, error))
    {
      return FALSE;
    }

#ifdef ENABLE_OLPC
  g_signal_connect (priv->activity_browser, "new-service",
      G_CALLBACK (browser_found), mgr);
  g_signal_connect (priv->activity_browser, "removed-service",
      G_CALLBACK (browser_removed), mgr);
  g_signal_connect (priv->activity_browser, "failure",
      G_CALLBACK (browser_failed), mgr);

  if (!salut_avahi_service_browser_attach(priv->activity_browser,
        priv->client, error))
    {
      return FALSE;
    }
#endif

  return TRUE;
}

SalutContact *
salut_contact_manager_get_contact(SalutContactManager *mgr, TpHandle handle) {
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (mgr);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles(
      TP_BASE_CONNECTION(priv->connection), TP_HANDLE_TYPE_CONTACT);
  const char *name = tp_handle_inspect(handle_repo, handle);
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

