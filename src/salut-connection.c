/*
 * salut-connection.c - Source for SalutConnection
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

#define DBUS_API_SUBJECT_TO_CHANGE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>

#include <dbus/dbus-protocol.h>
#include <dbus/dbus-glib-bindings.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>


#include "salut-connection.h"

#include "salut-avahi-client.h"
#include "salut-avahi-entry-group.h"
#include "salut-contact-manager.h"
#include "salut-contact-channel.h"
#include "salut-im-manager.h"
#include "salut-muc-manager.h"
#include "salut-contact.h"
#include "salut-self.h"

#include "salut-presence.h"

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/error.h>
#include <avahi-glib/glib-watch.h>

#include <telepathy-glib/util.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/handle-repo-dynamic.h>
#include <telepathy-glib/handle-repo-static.h>
#include <telepathy-glib/handle-repo.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG DEBUG_CONNECTION
#include "debug.h"

#define SALUT_TP_ALIAS_PAIR_TYPE (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID))

#ifdef ENABLE_OLPC

#include <extensions/extensions.h>

static void
salut_connection_olpc_buddy_info_iface_init (gpointer g_iface,
    gpointer iface_data);

#endif

static void
salut_connection_connection_service_iface_init (gpointer g_iface,
    gpointer iface_data);

static void
salut_connection_aliasing_service_iface_init (gpointer g_iface,
    gpointer iface_data);

static void
salut_connection_presence_service_iface_init (gpointer g_iface,
    gpointer iface_data);

static void
salut_connection_avatar_service_iface_init (gpointer g_iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE(SalutConnection,
    salut_connection,
    TP_TYPE_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION,
        salut_connection_connection_service_iface_init);
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_INTERFACE_ALIASING,
        salut_connection_aliasing_service_iface_init);
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_INTERFACE_PRESENCE,
       salut_connection_presence_service_iface_init);
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_INTERFACE_AVATARS,
       salut_connection_avatar_service_iface_init);
#ifdef ENABLE_OLPC
    G_IMPLEMENT_INTERFACE (SALUT_TYPE_SVC_OLPC_BUDDY_INFO,
       salut_connection_olpc_buddy_info_iface_init);
#endif
    )

/* properties */
enum {
  PROP_NICKNAME = 1,
  PROP_FIRST_NAME,
  PROP_LAST_NAME,
  PROP_JID,
  PROP_EMAIL,
  PROP_PUBLISHED_NAME,
  LAST_PROP
};

typedef struct _SalutConnectionPrivate SalutConnectionPrivate;

struct _SalutConnectionPrivate
{
  gboolean dispose_has_run;

  /* Connection information */
  gchar *published_name;
  gchar *nickname;
  gchar *first_name;
  gchar *last_name;
  gchar *jid;
  gchar *email;
#ifdef ENABLE_OLPC
  gchar *olpc_color;
  GArray *olpc_key;
#endif

  /* Avahi client for browsing and resolving */
  SalutAvahiClient *avahi_client;

  /* TpHandler for our presence on the lan */
  SalutSelf *self;

  /* Contact manager */
  SalutContactManager *contact_manager;

  /* IM channel manager */
  SalutImManager *im_manager;

  /* MUC channel manager */
  SalutMucManager *muc_manager;

};

#define SALUT_CONNECTION_GET_PRIVATE(o) \
  ((SalutConnectionPrivate *)((SalutConnection *)o)->priv);

typedef struct _ChannelRequest ChannelRequest;

struct _ChannelRequest
{
  DBusGMethodInvocation *context;
  gchar *channel_type;
  guint handle_type;
  guint handle;
  gboolean suppress_handler;
};

static void _salut_connection_disconnect (SalutConnection *self);
static void emit_one_presence_update (SalutConnection *self, TpHandle handle);

static void
salut_connection_create_handle_repos (TpBaseConnection *self,
    TpHandleRepoIface *repos[NUM_TP_HANDLE_TYPES]);

static GPtrArray *
salut_connection_create_channel_factories (TpBaseConnection *self);

static gchar *
salut_connection_get_unique_connection_name (TpBaseConnection *self);

static void
salut_connection_shut_down (TpBaseConnection *self);

static gboolean
salut_connection_start_connecting (TpBaseConnection *self, GError **error);

static void
salut_connection_init (SalutConnection *obj)
{
  SalutConnectionPrivate *priv =
    G_TYPE_INSTANCE_GET_PRIVATE(obj, SALUT_TYPE_CONNECTION,
                                SalutConnectionPrivate);

  obj->priv = priv;
  obj->name = NULL;

  /* allocate any data required by the object here */
  priv->published_name = g_strdup (g_get_user_name ());
  priv->nickname = NULL;
  priv->first_name = NULL;
  priv->last_name = NULL;
  priv->jid = NULL;
  priv->email = NULL;
#ifdef ENABLE_OLPC
  priv->olpc_color = NULL;
  priv->olpc_key = NULL;
#endif

  priv->avahi_client = NULL;
  priv->self = NULL;

  priv->contact_manager = NULL;
}

static void
salut_connection_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  SalutConnection *self = SALUT_CONNECTION(object);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE(self);

  switch (property_id)
    {
    case PROP_NICKNAME:
      g_value_set_string (value, priv->nickname);
      break;
    case PROP_FIRST_NAME:
      g_value_set_string (value, priv->first_name);
      break;
    case PROP_LAST_NAME:
      g_value_set_string (value, priv->last_name);
      break;
    case PROP_JID:
      g_value_set_string (value, priv->jid);
      break;
    case PROP_EMAIL:
      g_value_set_string (value, priv->email);
      break;
    case PROP_PUBLISHED_NAME:
      g_value_set_string (value, priv->published_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
salut_connection_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  SalutConnection *self = SALUT_CONNECTION(object);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);

  switch (property_id)
    {
    case PROP_NICKNAME:
      g_free (priv->nickname);
      priv->nickname = g_value_dup_string (value);
      break;
    case PROP_FIRST_NAME:
      g_free (priv->first_name);
      priv->first_name = g_value_dup_string (value);
      break;
    case PROP_LAST_NAME:
      g_free (priv->last_name);
      priv->last_name = g_value_dup_string (value);
      break;
    case PROP_JID:
      g_free (priv->jid);
      priv->jid = g_value_dup_string (value);
      break;
    case PROP_EMAIL:
      g_free (priv->email);
      priv->email = g_value_dup_string (value);
      break;
    case PROP_PUBLISHED_NAME:
      g_free (priv->published_name);
      priv->published_name = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}


static void salut_connection_dispose (GObject *object);
static void salut_connection_finalize (GObject *object);

static void
salut_connection_class_init (SalutConnectionClass *salut_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_connection_class);
  TpBaseConnectionClass *tp_connection_class =
      TP_BASE_CONNECTION_CLASS(salut_connection_class);
  GParamSpec *param_spec;

  object_class->get_property = salut_connection_get_property;
  object_class->set_property = salut_connection_set_property;

  g_type_class_add_private (salut_connection_class,
      sizeof (SalutConnectionPrivate));

  object_class->dispose = salut_connection_dispose;
  object_class->finalize = salut_connection_finalize;

  tp_connection_class->create_handle_repos =
      salut_connection_create_handle_repos;
  tp_connection_class->create_channel_factories =
      salut_connection_create_channel_factories;
  tp_connection_class->get_unique_connection_name =
      salut_connection_get_unique_connection_name;
  tp_connection_class->shut_down =
      salut_connection_shut_down;
  tp_connection_class->start_connecting =
      salut_connection_start_connecting;

  param_spec = g_param_spec_string ("nickname", "nickname",
      "Nickname used in the published data", NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_NICKNAME, param_spec);

  param_spec = g_param_spec_string ("first-name", "First name",
      "First name used in the published data", NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_FIRST_NAME, param_spec);

  param_spec = g_param_spec_string ("last-name", "Last name",
      "Last name used in the published data", NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_LAST_NAME, param_spec);

  param_spec = g_param_spec_string ("email", "E-mail address",
      "E-mail address used in the published data", NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_EMAIL, param_spec);

  param_spec = g_param_spec_string ("jid", "Jabber id",
      "Jabber id used in the published data", NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_JID, param_spec);

  param_spec = g_param_spec_string ("published-name", "Published name",
      "Username used in the published data", NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PUBLISHED_NAME,
      param_spec);
}

void
salut_connection_dispose (GObject *object)
{
  SalutConnection *self = SALUT_CONNECTION (object);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  DBusGProxy *bus_proxy;


  if (priv->dispose_has_run)
    return;
  bus_proxy = tp_get_bus_proxy ();

  priv->dispose_has_run = TRUE;

  if (priv->self) {
    g_object_unref (priv->self);
    priv->self = NULL;
  }

  if (priv->avahi_client) {
    g_object_unref (priv->avahi_client);
    priv->avahi_client = NULL;
  }

  /* release any references held by the object here */
  if (G_OBJECT_CLASS (salut_connection_parent_class)->dispose)
    G_OBJECT_CLASS (salut_connection_parent_class)->dispose (object);
}

void
salut_connection_finalize (GObject *object)
{
  SalutConnection *self = SALUT_CONNECTION (object);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  g_free (self->name);
  g_free (priv->published_name);
  g_free (priv->first_name);
  g_free (priv->last_name);
  g_free (priv->email);
  g_free (priv->jid);
#ifdef ENABLE_OLPC
  if (priv->olpc_key != NULL)
    g_array_free (priv->olpc_key, TRUE);
  g_free (priv->olpc_color);
#endif

  DEBUG("Finalizing connection");

  G_OBJECT_CLASS (salut_connection_parent_class)->finalize (object);
}

static GHashTable *
get_statuses_arguments(void) {
  static GHashTable *arguments = NULL;

  if (arguments == NULL) {
    arguments = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(arguments, "message", "s");
  }

  return arguments;
}

void
_contact_manager_contact_status_changed(SalutConnection *self,
                                        SalutContact *contact,
                                        TpHandle handle) {
  emit_one_presence_update(self, handle);
}

static void
_self_established_cb(SalutSelf *s, gpointer data) {
  SalutConnection *self = SALUT_CONNECTION(data);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE(self);
  TpBaseConnection *base = TP_BASE_CONNECTION(self);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles(
      TP_BASE_CONNECTION(self), TP_HANDLE_TYPE_CONTACT);


  g_free(self->name);
  self->name = g_strdup(s->name);

  base->self_handle = tp_handle_ensure(handle_repo, self->name, NULL, NULL);

  if (!salut_contact_manager_start(priv->contact_manager, 
           priv->avahi_client, NULL)) {
    /* FIXME handle error */
    tp_base_connection_change_status(
        TP_BASE_CONNECTION(base), 
        TP_CONNECTION_STATUS_CONNECTING,
        TP_CONNECTION_STATUS_REASON_REQUESTED);
    return;
  }

  tp_base_connection_change_status(base,
      TP_CONNECTION_STATUS_CONNECTED, 
      TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);
}


static void
_self_failed_cb(SalutSelf *s, GError *error, gpointer data) {
  SalutConnection *self = SALUT_CONNECTION(data);
  TpBaseConnection *base = TP_BASE_CONNECTION(self);

  /* FIXME better error handling */
  tp_base_connection_change_status(base,
     TP_CONNECTION_STATUS_DISCONNECTED, 
     TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);
}

static void
_self_new_connection_cb(SalutSelf *s, GibberLLTransport *transport, 
                        gpointer data) {
  SalutConnection *self = SALUT_CONNECTION(data);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE(self);

  if (priv->im_manager == NULL) {
    /* Got a connection before we had an im manager.. Ignore */
    DEBUG("Connection but no IM manager yet!?");
    return;
  }
  DEBUG("New connection, passing to the IM manager");
  /* Get a ref and let the lower layers handle it */
  g_object_ref(transport);
  salut_im_manager_handle_connection(priv->im_manager, transport);
}

static void
_salut_avahi_client_failure_cb(SalutAvahiClient *c, 
                              SalutAvahiClientState state,
                              gpointer data) {
  /* FIXME better error messages */
  /* FIXME instead of full disconnect we could handle the avahi restart */
  tp_base_connection_change_status(
        TP_BASE_CONNECTION(data), 
        TP_CONNECTION_STATUS_DISCONNECTED,
        TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
}

static void
_salut_avahi_client_running_cb(SalutAvahiClient *c, 
                              SalutAvahiClientState state,
                              gpointer data) {
  SalutConnection *self = SALUT_CONNECTION(data);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE(self);

  g_assert(c == priv->avahi_client);

  priv->self = salut_self_new(priv->avahi_client, 
                              priv->nickname,
                              priv->first_name,
                              priv->last_name,
                              priv->jid,
                              priv->email,
                              priv->published_name,
#ifdef ENABLE_OLPC
                              priv->olpc_key,
                              priv->olpc_color
#else
                              NULL, NULL
#endif
                              );
  g_signal_connect(priv->self, "established", 
                   G_CALLBACK(_self_established_cb), self);
  g_signal_connect(priv->self, "failure", 
                   G_CALLBACK(_self_failed_cb), self);
  g_signal_connect(priv->self, "new-connection", 
                   G_CALLBACK(_self_new_connection_cb), self);
  if (!salut_self_announce(priv->self, NULL)) {
    tp_base_connection_change_status(
          TP_BASE_CONNECTION(self), 
          TP_CONNECTION_STATUS_DISCONNECTED,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
  }
}

/* public functions */
static void
_salut_connection_disconnect(SalutConnection *self) {
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);

  if (priv->self) {
    g_object_unref(priv->self);
    priv->self = NULL;
  }

  if (priv->avahi_client) {
    g_object_unref(priv->avahi_client);
    priv->avahi_client = NULL;
  }
}




/* Presence interface */
static void
destroy_value(GValue *value) {
  g_value_unset(value);
  g_free(value);
}

static void
get_presence_info(SalutConnection *self, const GArray *contact_handles,
                  GHashTable **info) {
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  TpBaseConnection *base = TP_BASE_CONNECTION(self);
  GHashTable *presence_hash;
  GValueArray *vals;
  GHashTable *contact_status, *parameters;
  guint timestamp = 0; /* this is never set at the moment*/
  guint i;

  presence_hash = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
                                    (GDestroyNotify) g_value_array_free);

  for (i = 0; i < contact_handles->len; i++) {
      TpHandle handle = g_array_index (contact_handles, TpHandle, i);
      GValue *message;
      SalutPresenceId status;
      gchar *status_message = NULL;
      SalutContact *contact = NULL;

      if (handle == base->self_handle) {
        status = priv->self->status;
        status_message = priv->self->status_message;
      } else {
        contact = salut_contact_manager_get_contact(priv->contact_manager,
                                                    handle);
        if (contact) {
          status = contact->status;
          status_message = contact->status_message;
        } else {
          status = SALUT_PRESENCE_OFFLINE;
        }
      }

      if (contact) 
        g_object_unref(contact);

      parameters =
        g_hash_table_new_full (g_str_hash, g_str_equal,
                               NULL, (GDestroyNotify) destroy_value);

      if (status_message != NULL) {
        message = g_new0 (GValue, 1);
        g_value_init (message, G_TYPE_STRING);
        g_value_set_string (message, status_message);
        g_hash_table_insert (parameters, "message", message);
      }

      contact_status =
        g_hash_table_new_full (g_str_hash, g_str_equal,
                               NULL, (GDestroyNotify) g_hash_table_destroy);
      g_hash_table_insert (
        contact_status, (gchar *) salut_presence_statuses[status].name,
        parameters);

      vals = g_value_array_new (2);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 0), G_TYPE_UINT);
      g_value_set_uint (g_value_array_get_nth (vals, 0), timestamp);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 1),
          dbus_g_type_get_map ("GHashTable", G_TYPE_STRING,
            dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)));
      g_value_take_boxed (g_value_array_get_nth (vals, 1), contact_status);

      g_hash_table_insert (presence_hash, GINT_TO_POINTER (handle),
                           vals);
  }
  *info = presence_hash;
}


/**
 * emit_presence_update:
 * @self: A #SalutConnection
 * @contact_handles: A zero-terminated array of #Handle for
 *                    the contacts to emit presence for
 *
 * Emits the Telepathy PresenceUpdate signal with the current
 * stored presence information for the given contact.
 */
static void
emit_presence_update (SalutConnection *self,
                      const GArray *contact_handles)
{
  GHashTable *presence_hash;
  
  get_presence_info(self, contact_handles, &presence_hash);
  
  tp_svc_connection_interface_presence_emit_presence_update (self,
        presence_hash);

  g_hash_table_destroy (presence_hash);
}

/**
 * emit_one_presence_update:
 * Convenience function for calling emit_presence_update with one handle.
 */
static void
emit_one_presence_update (SalutConnection *self, TpHandle handle)
{
  GArray *handles = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle), 1);

  g_array_insert_val (handles, 0, handle);
  emit_presence_update (self, handles);
  g_array_free (handles, TRUE);
}

/**
 * salut_connection_clear_status
 *
 * Implements DBus method ClearStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_connection_clear_status (TpSvcConnectionInterfacePresence *iface,
                               DBusGMethodInvocation *context) {
  SalutConnection *self = SALUT_CONNECTION(iface);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  TpBaseConnection *base = TP_BASE_CONNECTION(self);
  gboolean ret;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED(base, context);

  ret = salut_self_set_presence(priv->self, SALUT_PRESENCE_AVAILABLE,
                                NULL, NULL);
  /* FIXME turn into a TP ERROR */
  if (ret) {
    emit_one_presence_update(self, base->self_handle);
  }

  tp_svc_connection_interface_presence_return_from_clear_status(context);
}

/**
 * salut_connection_get_presence
 *
 * Implements D-Bus method GetPresence
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */

static void 
salut_connection_get_presence (TpSvcConnectionInterfacePresence *iface,
                               const GArray *contacts,
                               DBusGMethodInvocation *context) 
{
  SalutConnection *self = SALUT_CONNECTION(iface);
  TpBaseConnection *base = TP_BASE_CONNECTION(self);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles(
      base,TP_HANDLE_TYPE_CONTACT);
  GError *error = NULL;
  GHashTable *ret;

  if (!tp_handles_are_valid(handle_repo, contacts, FALSE, &error)) {
    dbus_g_method_return_error(context, error);
    g_error_free(error);
    return;
  }

  get_presence_info(self, contacts, &ret);
  tp_svc_connection_interface_presence_return_from_get_presence(context, ret);
  g_hash_table_destroy(ret);
}


/**
 * salut_connection_get_statuses
 *
 * Implements DBus method GetStatuses
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void 
salut_connection_get_statuses (TpSvcConnectionInterfacePresence *iface,
                               DBusGMethodInvocation *context) {
  GValueArray *status;
  int i;
  GHashTable *ret;

  ret = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, 
                                 (GDestroyNotify) g_value_array_free);
  for (i = 0; salut_presence_statuses[i].name ; i++) {
    status = g_value_array_new(5);

    g_value_array_append(status, NULL);
    g_value_init(g_value_array_get_nth(status, 0), G_TYPE_UINT);
    g_value_set_uint(g_value_array_get_nth(status, 0), 
      salut_presence_statuses[i].presence_type);

    g_value_array_append(status, NULL);
    g_value_init(g_value_array_get_nth(status, 1), G_TYPE_BOOLEAN);
    g_value_set_boolean(g_value_array_get_nth(status, 1), TRUE);
    
    g_value_array_append(status, NULL);
    g_value_init(g_value_array_get_nth(status, 2), G_TYPE_BOOLEAN);
    g_value_set_boolean(g_value_array_get_nth(status, 2), TRUE);

    g_value_array_append(status, NULL);
    g_value_init(g_value_array_get_nth(status, 3), 
        DBUS_TYPE_G_STRING_STRING_HASHTABLE);
    g_value_set_static_boxed(g_value_array_get_nth(status, 3),
       get_statuses_arguments());

    g_hash_table_insert(ret, (gchar*)salut_presence_statuses[i].name, status);
  }

  tp_svc_connection_interface_presence_return_from_get_statuses(context,
                                                                ret);
  g_hash_table_destroy(ret);
}

/**
 * salut_connection_remove_status
 *
 * Implements DBus method RemoveStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void 
salut_connection_remove_status (TpSvcConnectionInterfacePresence *iface,
                                const gchar *status,
                                DBusGMethodInvocation *context) {
  SalutConnection *self = SALUT_CONNECTION(iface);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE(self);
  TpBaseConnection *base = TP_BASE_CONNECTION(self);
  gboolean ret = TRUE;
  GError *error = NULL;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED(base, context);

  if (!tp_strdiff(status, salut_presence_statuses[priv->self->status].name)) {
    ret = salut_self_set_presence(priv->self, SALUT_PRESENCE_AVAILABLE, 
                                "Available", NULL);
    /* FIXME turn into a TP ERROR */
    if (ret) {
      emit_one_presence_update(self, base->self_handle);
    }
  } else {
    error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, 
                         "Attempting to remove non-existing presence.");
    dbus_g_method_return_error(context, error);
    g_error_free(error);
    return;
  }
  
  tp_svc_connection_interface_presence_return_from_remove_status(context);
}

/**
 * salut_connection_request_presence
 *
 * Implements DBus method RequestPresence
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void 
salut_connection_request_presence (TpSvcConnectionInterfacePresence *iface,
                                   const GArray * contacts, 
                                   DBusGMethodInvocation *context) {
  SalutConnection *self = SALUT_CONNECTION (iface);
  TpBaseConnection *base = TP_BASE_CONNECTION(self);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles(
      TP_BASE_CONNECTION(self), TP_HANDLE_TYPE_CONTACT);
  GError *error = NULL;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED(base, context);

  if (!tp_handles_are_valid(handle_repo, contacts, FALSE, &error)) {
    dbus_g_method_return_error(context, error);
    g_error_free(error);
    return;
  }

  if (contacts->len) 
    emit_presence_update(self, contacts);
  
  tp_svc_connection_interface_presence_return_from_request_presence(context);
  return;
}


/**
 * salut_connection_set_status
 *
 * Implements DBus method SetStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void 
salut_connection_set_status (TpSvcConnectionInterfacePresence *iface,
                             GHashTable * statuses, 
                              DBusGMethodInvocation *context) 
{
  SalutConnection *self = SALUT_CONNECTION(iface);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  TpBaseConnection *base = TP_BASE_CONNECTION(self);
  SalutPresenceId id = 0;
  GHashTable *parameters = NULL;
  const gchar *status_message = NULL;
  GValue *message;
  gboolean ret = TRUE;
  GError *error = NULL;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED(base, context);

  if (g_hash_table_size(statuses) != 1) {
    DEBUG("Got more then one status");
    error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                       "Only one status may be set at a time in this protocol");
    dbus_g_method_return_error(context, error);
    g_error_free(error);
    return;
  }

  /* Don't feel like jumping through hoops with a foreach */
  for ( ; id < SALUT_PRESENCE_NR_PRESENCES; id++) {
    parameters = g_hash_table_lookup(statuses, 
                                      salut_presence_statuses[id].name);
    if (parameters) {
      break;
    }
  }

  if (id == SALUT_PRESENCE_NR_PRESENCES)  {
    error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, 
                         "Unknown status");
    dbus_g_method_return_error(context, error);
    g_error_free(error);
    return;
  }

  message = g_hash_table_lookup(parameters, "message");
  if (message) {
    if (!G_VALUE_HOLDS_STRING(message)) {
      error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                            "Status argument 'message' requeires a string");
      dbus_g_method_return_error(context, error);
      g_error_free(error);
      return;
    }
    status_message = g_value_get_string(message);
  }

  ret = salut_self_set_presence(priv->self, id, status_message, NULL);
  /* FIXME turn into a TP ERROR */
  if (ret) {
    emit_one_presence_update(self, base->self_handle);
  }

  tp_svc_connection_interface_presence_return_from_set_status(context);
}


static void
salut_connection_presence_service_iface_init(gpointer g_iface, 
                                             gpointer iface_data) 
{
  TpSvcConnectionInterfacePresenceClass *klass =
    (TpSvcConnectionInterfacePresenceClass *) g_iface;
#define IMPLEMENT(x) tp_svc_connection_interface_presence_implement_##x \
    (klass, salut_connection_##x)
  IMPLEMENT(set_status);
  IMPLEMENT(remove_status);
  IMPLEMENT(request_presence);
  IMPLEMENT(get_presence);
  IMPLEMENT(get_statuses);
  IMPLEMENT(clear_status);
  
#undef IMPLEMENT
}

/* Aliasing interface */
/**
 * salut_connection_get_alias_flags
 *
 * Implements D-Bus method GetAliasFlags
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 */
void
salut_connection_get_alias_flags (TpSvcConnectionInterfaceAliasing *self,
                                  DBusGMethodInvocation *context)
{
  /* Aliases are set by the contacts 
   * Actually we concat the first and lastname property */

  tp_svc_connection_interface_aliasing_return_from_get_alias_flags (context, 0);
}

/**
 * salut_connection_request_aliases
 *
 * Implements D-Bus method RequestAliases
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 */
void
salut_connection_request_aliases (TpSvcConnectionInterfaceAliasing *iface,
                                  const GArray *contacts,
                                  DBusGMethodInvocation *context) 
{
  SalutConnection *self = SALUT_CONNECTION(iface);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  TpBaseConnection *base = TP_BASE_CONNECTION(self);
  int i;
  const gchar **aliases;
  GError *error = NULL;
  TpHandleRepoIface *contact_handles = 
      tp_base_connection_get_handles(base, TP_HANDLE_TYPE_CONTACT);

  DEBUG("Alias requested");

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED(base, context);

  if (!tp_handles_are_valid(contact_handles, contacts, FALSE, &error)) {
    dbus_g_method_return_error(context, error);
    g_error_free(error);
    return;
  }

  aliases = g_new0(const gchar *, contacts->len + 1);
  for (i = 0; i < contacts->len; i++) {
    TpHandle handle = g_array_index (contacts, TpHandle, i);
    SalutContact *contact;
    if (handle == TP_BASE_CONNECTION(self)->self_handle) {
      aliases[i] = salut_self_get_alias(priv->self);
    } else {
      contact = salut_contact_manager_get_contact(priv->contact_manager, handle);
      if (contact == NULL) {
        DEBUG("RequestAliases called for offline contact");
        aliases[i] = "";
      } else {
        aliases[i] = salut_contact_get_alias(contact);
        g_object_unref(contact);
      }
    }
  }

  tp_svc_connection_interface_aliasing_return_from_request_aliases(
    context, aliases);

  g_free(aliases);
  return;
}

static void
salut_connection_set_aliases (TpSvcConnectionInterfaceAliasing *iface,
                              GHashTable *aliases,
                              DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  GError *error = NULL;
  const gchar *alias = g_hash_table_lookup (aliases,
      GUINT_TO_POINTER (base->self_handle));

  if (alias == NULL || g_hash_table_size (aliases) != 1)
    {
      GError e = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
                   "In Salut you can only set your own alias" };

      dbus_g_method_return_error (context, &e);
      return;
    }

  if (!salut_self_set_alias (priv->self, alias, &error)) {
    dbus_g_method_return_error(context, error);
    g_error_free(error);
    return;
  }
  tp_svc_connection_interface_aliasing_return_from_set_aliases (context);
}

void
_contact_manager_contact_alias_changed(SalutConnection *self,
                                       SalutContact *contact,
                                       TpHandle handle) {
  GPtrArray *aliases;
  GValue entry = {0, };

  g_value_init(&entry, SALUT_TP_ALIAS_PAIR_TYPE);
  g_value_take_boxed(&entry, 
                 dbus_g_type_specialized_construct(SALUT_TP_ALIAS_PAIR_TYPE));

  dbus_g_type_struct_set(&entry, 
                         0, handle,
                         1, salut_contact_get_alias(contact),
                         G_MAXUINT);
  aliases = g_ptr_array_sized_new(1);
  g_ptr_array_add(aliases, g_value_get_boxed(&entry));

  DEBUG("Emitting AliasesChanged");

  tp_svc_connection_interface_aliasing_emit_aliases_changed(self,
      aliases);

  g_value_unset(&entry);
  g_ptr_array_free(aliases, TRUE);
}

static void
salut_connection_aliasing_service_iface_init(gpointer g_iface, 
    gpointer iface_data)
{
  TpSvcConnectionInterfaceAliasingClass *klass =
    (TpSvcConnectionInterfaceAliasingClass *) g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_aliasing_implement_##x \
    (klass, salut_connection_##x)
  IMPLEMENT(get_alias_flags);
  IMPLEMENT(request_aliases);
  IMPLEMENT(set_aliases);
#undef IMPLEMENT
}

/* Avatar service implementation */
void
_contact_manager_contact_avatar_changed(SalutConnection *self,
                                        SalutContact *contact,
                                        TpHandle handle) {

  tp_svc_connection_interface_avatars_emit_avatar_updated(self, 
      (guint)handle, contact->avatar_token);
}

static void
salut_connection_clear_avatar(TpSvcConnectionInterfaceAvatars *iface,
                              DBusGMethodInvocation *context) {
  SalutConnection *self = SALUT_CONNECTION(iface);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE(self);
  GError *error = NULL;

  if (!salut_self_set_avatar(priv->self, NULL, 0, &error)) {
    dbus_g_method_return_error(context, error);
    g_error_free(error);
    return;
  }
  tp_svc_connection_interface_avatars_return_from_clear_avatar(context);
}

static void
salut_connection_set_avatar(TpSvcConnectionInterfaceAvatars *iface,
                            const GArray *avatar,
                            const gchar *mime_type,
                            DBusGMethodInvocation *context) {
  SalutConnection *self = SALUT_CONNECTION(iface);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE(self);
  GError *error = NULL;
  
  if (!salut_self_set_avatar(priv->self, (guint8 *)avatar->data, 
                             avatar->len, &error)) {
    dbus_g_method_return_error(context, error);
    g_error_free(error);
    return;
  }

  tp_svc_connection_interface_avatars_return_from_set_avatar(
     context, priv->self->avatar_token); 
}

static void
salut_connection_get_avatar_tokens(TpSvcConnectionInterfaceAvatars *iface,
                                   const GArray *contacts,
                                   DBusGMethodInvocation *context) {
  int i;
  gchar **ret;
  GError *err = NULL;
  SalutConnection *self = SALUT_CONNECTION(iface);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE(self);
  TpBaseConnection *base = TP_BASE_CONNECTION(self);

  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles(
      base, TP_HANDLE_TYPE_CONTACT);

  if (!tp_handles_are_valid(handle_repo, contacts, FALSE, &err)) {
    dbus_g_method_return_error(context, err);
    g_error_free(err);
    return;
  }

  ret = g_new0(gchar *, contacts->len + 1);

  for (i = 0; i < contacts->len ; i++) {
    TpHandle handle = g_array_index(contacts, TpHandle, i);
    if (base->self_handle == handle) {
      ret[i] = priv->self->avatar_token;
    } else {
      SalutContact *contact;
      contact = 
         salut_contact_manager_get_contact(priv->contact_manager, handle);
      if (contact != NULL) {
        ret[i] = contact->avatar_token;
        g_object_unref(contact);
      }
    }
    if (ret[i] == NULL)
      ret[i] = "";
  }

  tp_svc_connection_interface_avatars_return_from_get_avatar_tokens(context,
    (const gchar **)ret);

  g_free(ret);
}

static void
_request_avatar_cb(SalutContact *contact, guint8 *avatar, gsize size,
                   gpointer user_data) {
  DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

  GError *err = NULL;
  GArray *arr;

  if (size == 0) {
    err = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, 
                      "Unable to get avatar");
    dbus_g_method_return_error(context, err);
    g_error_free(err);
    return;
  }

  arr = g_array_sized_new(FALSE, FALSE, sizeof(guint8), size);
  arr = g_array_append_vals(arr, avatar, size);
  tp_svc_connection_interface_avatars_return_from_request_avatar(
    context, arr, "");
  g_array_free(arr, TRUE);
}

static void
salut_connection_request_avatar(TpSvcConnectionInterfaceAvatars *iface,
                                guint handle,
                                DBusGMethodInvocation *context) {
  SalutConnection *self = SALUT_CONNECTION(iface);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE(self);
  TpBaseConnection *base = TP_BASE_CONNECTION(self);
  SalutContact *contact;
  GError *err = NULL;

  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles(
      base, TP_HANDLE_TYPE_CONTACT);

  if (!tp_handle_is_valid(handle_repo, handle, &err)) {
    dbus_g_method_return_error(context, err);
    g_error_free(err);
    return;
  }

  if (handle == base->self_handle) {
    _request_avatar_cb(NULL, priv->self->avatar, 
        priv->self->avatar_size, context); 
    return;
  }

  contact = salut_contact_manager_get_contact(priv->contact_manager, handle); 
  if (contact == NULL || contact->avatar_token == NULL) {
    err = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, 
                      "No known avatar");
    dbus_g_method_return_error(context, err);
    g_error_free(err);
    if (contact != NULL) {
      g_object_unref(contact);
    }
    return;
  }
  salut_contact_get_avatar(contact, _request_avatar_cb, context);
  g_object_unref(contact);
}

static void
salut_connection_get_avatar_requirements(TpSvcConnectionInterfaceAvatars *iface,
                                         DBusGMethodInvocation *context) {
  const gchar *mimetypes [] = {
    "image/png",
    "image/jpeg",
    NULL };

  tp_svc_connection_interface_avatars_return_from_get_avatar_requirements(
      context, mimetypes, 0, 0, 0, 0, 0xffff);
}

static void
salut_connection_avatar_service_iface_init(gpointer g_iface, 
    gpointer iface_data)
{
TpSvcConnectionInterfaceAvatarsClass *klass =
   (TpSvcConnectionInterfaceAvatarsClass *) g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_avatars_implement_##x \
    (klass, salut_connection_##x)
  IMPLEMENT(get_avatar_requirements);
  IMPLEMENT(get_avatar_tokens);
  IMPLEMENT(request_avatar);
  IMPLEMENT(set_avatar);
  IMPLEMENT(clear_avatar);
#undef IMPLEMENT
}

#ifdef ENABLE_OLPC
static GValue *
new_gvalue (GType type)
{
  GValue *result = g_slice_new0 (GValue);
  g_value_init (result, type);
  return result;
}

static GHashTable *
get_properties_hash (const GArray *key,
                     const gchar *color,
                     const gchar *jid)
{
  GHashTable *properties;
  GValue *gvalue;

  properties = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);
  if (key != NULL)
    {
      gvalue = new_gvalue (DBUS_TYPE_G_UCHAR_ARRAY);
      g_value_set_boxed (gvalue, key);
      g_hash_table_insert (properties, "key", gvalue);
    }

  if (color != NULL)
    {
      gvalue = new_gvalue (G_TYPE_STRING);
      g_value_set_string (gvalue, color);
      g_hash_table_insert (properties, "color", gvalue);
    }

  if (jid != NULL)
    {
      gvalue = new_gvalue (G_TYPE_STRING);
      g_value_set_string (gvalue, jid);
      g_hash_table_insert (properties, "jid", gvalue);
    }

  return properties;
}

static void
emit_properties_changed (SalutConnection *connection,
                         TpHandle handle,
                         const GArray *key,
                         const gchar *color,
                         const gchar *jid)
{
  GHashTable *properties;
  properties = get_properties_hash (key, color, jid);

  salut_svc_olpc_buddy_info_emit_properties_changed (connection,
      handle, properties);

  g_hash_table_destroy (properties);
}

static void
_contact_manager_contact_olpc_properties_changed (SalutConnection *self,
                                                  SalutContact *contact,
                                                  TpHandle handle)
{
  emit_properties_changed (self, handle, contact->olpc_key,
      contact->olpc_color, contact->jid);
}

static void
salut_connection_olpc_get_properties (SalutSvcOLPCBuddyInfo *iface,
                                      TpHandle handle,
                                      DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  GHashTable *properties = NULL;

  if (handle == base->self_handle)
    {
      properties = get_properties_hash (priv->self->olpc_key,
          priv->self->olpc_color, priv->self->jid);
    }
  else
    {
      SalutContact *contact;
      contact = salut_contact_manager_get_contact (priv->contact_manager,
          handle);
      if (contact == NULL)
        {
          GError *error;
          error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                             "Unknown contact");
          dbus_g_method_return_error (context, error);
          g_error_free (error);
        }
    properties = get_properties_hash (contact->olpc_key, contact->olpc_color,
        contact->jid);
  }

  salut_svc_olpc_buddy_info_return_from_get_properties (context, properties);
  g_hash_table_destroy (properties);
}


static gboolean
find_unknown_properties (gpointer key,
                         gpointer value,
                         gpointer user_data)
{
  gchar **valid_props = (gchar **) user_data;
  int i;
  for (i = 0; valid_props[i] != NULL; i++)
    {
      if (!tp_strdiff (key, valid_props[i]))
        return FALSE;
    }
  return TRUE;
}

static void
salut_connection_olpc_set_properties (SalutSvcOLPCBuddyInfo *iface,
                                      GHashTable *properties,
                                      DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);

  GError *error = NULL;
  /* Only three know properties, so handle it quite naively */
  const gchar *known_properties[] = { "color", "key", "jid", NULL };
  const gchar *color = NULL;
  const GArray *key = NULL;
  const gchar *jid = NULL;
  const GValue *val;

  if (g_hash_table_find (properties, find_unknown_properties, known_properties)
      != NULL)
    {
      error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Unknown property given");
      goto error;
    }

  val = (const GValue *) g_hash_table_lookup (properties, "color");
  if (val != NULL)
    {
      if (G_VALUE_TYPE (val) != G_TYPE_STRING)
        {
          error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "Color value should be of type s");
          goto error;
        }
      else
        {
          int len;
          gboolean correct = TRUE;

          color = g_value_get_string (val);

          /* be very anal about the color format */
          len = strlen (color);
          if (len != 15)
            {
              correct = FALSE;
            }
          else
            {
              int i;
              for (i = 0 ; i < len ; i++)
                {
                  switch (i)
                    {
                      case 0:
                      case 8:
                        correct = (color[i] == '#');
                        break;
                      case 7:
                        correct = (color[i] == ',');
                        break;
                      default:
                        correct = isxdigit (color[i]);
                        break;
                    }
                }
            }

          if (!correct)
            {
              error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                  "Color value has an incorrect format");
              goto error;
            }
        }
    }

  if ((val = (const GValue *) g_hash_table_lookup (properties, "key")) != NULL)
    {
      if (G_VALUE_TYPE (val) != DBUS_TYPE_G_UCHAR_ARRAY)
        {
          error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "Key value should be of type ay");
          goto error;
        }
      else
        {
          key = g_value_get_boxed (val);
          if (key->len == 0)
            {
              error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                  "Key value of length 0 not allowed");
              goto error;
            }
        }
    }

  val = g_hash_table_lookup (properties, "jid");
  if (val != NULL)
    {
      if (G_VALUE_TYPE (val) != G_TYPE_STRING)
        {
          error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "JID value should be of type s");
          goto error;
        }

      jid = g_value_get_string (val);

      if (strchr (jid, '@') == NULL)
        {
          error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "JID value has an incorrect format");
          goto error;
        }
    }

  if (priv->self)
    {
      if (!salut_self_set_olpc_properties (priv->self, key, color, jid,
            &error))
        goto error;
    }
  else
    {
      /* queue it up for later */
      if (key)
        {
          if (priv->olpc_key == NULL)
            {
              priv->olpc_key = g_array_sized_new (FALSE, FALSE, sizeof (guint8),
                  key->len);
            }
          else
            {
              g_array_remove_range (priv->olpc_key, 0, priv->olpc_key->len);
            }
          g_array_append_vals (priv->olpc_key, key->data, key->len);
        }
      if (color)
        {
          g_free (priv->olpc_color);
          priv->olpc_color = g_strdup (color);
        }
      if (jid)
        {
          g_free (priv->jid);
          priv->jid = g_strdup (jid);
        }
    }

  salut_svc_olpc_buddy_info_return_from_set_properties (context);
  return;

error:
  dbus_g_method_return_error (context, error);
  g_error_free (error);
}

static void
salut_connection_olpc_buddy_info_iface_init (gpointer g_iface,
                                             gpointer iface_data)
{
  SalutSvcOLPCBuddyInfoClass *klass =
    (SalutSvcOLPCBuddyInfoClass *) g_iface;
#define IMPLEMENT(x) salut_svc_olpc_buddy_info_implement_##x (klass, \
    salut_connection_olpc_##x)
  IMPLEMENT(set_properties);
  IMPLEMENT(get_properties);
#undef IMPLEMENT
}
#endif

/* Connection baseclass function implementations */
static void 
salut_connection_create_handle_repos(TpBaseConnection *self,
    TpHandleRepoIface *repos[NUM_TP_HANDLE_TYPES]) {
  static const char *list_handle_strings[] = {
    "publish",
    "subscribe",
    "known",
    NULL
  };

  repos[TP_HANDLE_TYPE_CONTACT] = 
    TP_HANDLE_REPO_IFACE(
        g_object_new(TP_TYPE_DYNAMIC_HANDLE_REPO,
            "handle-type", TP_HANDLE_TYPE_CONTACT,
            NULL));

  repos[TP_HANDLE_TYPE_ROOM] = 
    TP_HANDLE_REPO_IFACE(
        g_object_new(TP_TYPE_DYNAMIC_HANDLE_REPO,
            "handle-type", TP_HANDLE_TYPE_ROOM,
            NULL));

  repos[TP_HANDLE_TYPE_LIST] = 
    TP_HANDLE_REPO_IFACE(
        g_object_new(TP_TYPE_STATIC_HANDLE_REPO,
            "handle-type", TP_HANDLE_TYPE_LIST,
            "handle-names", list_handle_strings,
            NULL));
}

void
_contact_manager_contact_change_cb(SalutContactManager *mgr, 
                                   SalutContact *contact,
                                   int changes, gpointer data) {
  SalutConnection *self = SALUT_CONNECTION(data);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles(
      TP_BASE_CONNECTION(self), TP_HANDLE_TYPE_CONTACT);
  TpHandle handle;

  handle = tp_handle_lookup(handle_repo, contact->name, NULL, NULL);

  if (changes & SALUT_CONTACT_ALIAS_CHANGED) {
    _contact_manager_contact_alias_changed(self, contact, handle);
  }

  if (changes & SALUT_CONTACT_STATUS_CHANGED) {
    _contact_manager_contact_status_changed(self, contact, handle);
  }

  if (changes & SALUT_CONTACT_AVATAR_CHANGED) {
    _contact_manager_contact_avatar_changed(self, contact, handle);
  }

#ifdef ENABLE_OLPC
  if (changes & SALUT_CONTACT_OLPC_PROPERTIES)
    _contact_manager_contact_olpc_properties_changed (self, contact, handle);
#endif
}


static GPtrArray*  
salut_connection_create_channel_factories(TpBaseConnection *base) {
  SalutConnection *self = SALUT_CONNECTION(base);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE(self);
  GPtrArray *factories = g_ptr_array_sized_new(3);

  priv->contact_manager = salut_contact_manager_new(self);
  g_signal_connect(priv->contact_manager, "contact-change",
                   G_CALLBACK(_contact_manager_contact_change_cb), self);

  priv->im_manager = salut_im_manager_new(self, priv->contact_manager);

  priv->muc_manager = salut_muc_manager_new(self, priv->im_manager);

  g_ptr_array_add(factories, priv->contact_manager);
  g_ptr_array_add(factories, priv->im_manager);
  g_ptr_array_add(factories, priv->muc_manager);

  return factories;
}

static gchar *
salut_connection_get_unique_connection_name(TpBaseConnection *base) {
  SalutConnection *self = SALUT_CONNECTION(base);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE(self);
  
  return g_strdup(priv->published_name);
}

static void
salut_connection_shut_down(TpBaseConnection *self) {
  _salut_connection_disconnect(SALUT_CONNECTION(self)); 
  tp_base_connection_finish_shutdown(self);
}

static gboolean
salut_connection_start_connecting(TpBaseConnection *base, GError **error) {
  SalutConnection *self = SALUT_CONNECTION(base);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  GError *client_error = NULL;

/*
  tp_base_connection_change_status(
      TP_BASE_CONNECTION(base), 
      TP_CONNECTION_STATUS_CONNECTING,
      TP_CONNECTION_STATUS_REASON_REQUESTED);
  */

  priv->avahi_client = salut_avahi_client_new(SALUT_AVAHI_CLIENT_FLAG_NO_FAIL);

  g_signal_connect(priv->avahi_client, "state-changed::running", 
                   G_CALLBACK(_salut_avahi_client_running_cb), self);
  g_signal_connect(priv->avahi_client, "state-changed::failure", 
                   G_CALLBACK(_salut_avahi_client_failure_cb), self);

  if (!salut_avahi_client_start(priv->avahi_client, &client_error)) {
    *error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, 
                         "Unstable to initialize the avahi client: %s",
                         client_error->message);
    g_error_free(client_error);
    goto error;
  }

  return TRUE;

error:
  tp_base_connection_change_status(
        TP_BASE_CONNECTION(base), 
        TP_CONNECTION_STATUS_DISCONNECTED,
        TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
  return FALSE;
}

/* Connection interface implementations */ 
static void 
salut_connection_get_interfaces (TpSvcConnection *self, 
                                 DBusGMethodInvocation  *context)
{
  const gchar *interfaces [] = {
    TP_IFACE_CONNECTION_INTERFACE_ALIASING,
    TP_IFACE_CONNECTION_INTERFACE_PRESENCE,
    TP_IFACE_CONNECTION_INTERFACE_AVATARS,
#ifdef ENABLE_OLPC
    SALUT_IFACE_OLPC_BUDDY_INFO,
#endif
    NULL };

  tp_svc_connection_return_from_get_interfaces(context, interfaces);
}

static void
hold_unref_and_return_handles (DBusGMethodInvocation *context,
                               TpHandleRepoIface *repo, 
                               GArray *handles) {
  GError *error;
  gchar *sender = dbus_g_method_get_sender(context);
  int i,j = 0;

  for (i = 0; i < handles->len; i++)
    {
      TpHandle handle = (TpHandle) g_array_index (handles, guint, i);
      if (!tp_handle_client_hold (repo, sender, handle,  &error)) {
          goto error;
        }
      tp_handle_unref(repo, handle);
    }
  dbus_g_method_return (context, handles);
  return;

error:
  dbus_g_method_return_error (context, error);
  g_error_free (error);
  for (j = 0; j < i; j++) {
    TpHandle handle = (TpHandle) g_array_index (handles, guint, j);
    tp_handle_client_release(repo, sender, handle, NULL);
  }
  /* j == i */
  for (; j < handles->len; j++) {
    TpHandle handle = (TpHandle) g_array_index (handles, guint, j);
    tp_handle_unref(repo, handle);
  }
}


/**
 * salut_connection_request_handles
 *
 * Implements DBus method RequestHandles
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
void 
salut_connection_request_handles (TpSvcConnection *iface, 
                                  guint handle_type, 
                                  const gchar ** names, 
                                  DBusGMethodInvocation *context) {
  SalutConnection *self = SALUT_CONNECTION(iface);
  TpBaseConnection *base = TP_BASE_CONNECTION(self);
  GError *error = NULL;
  const gchar **n;
  int count = 0;
  int i, j;
  GArray *handles = NULL;
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles(
      TP_BASE_CONNECTION(self), handle_type);
  

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED(base, context);

  if (!tp_handle_type_is_valid(handle_type, &error)) {
    DEBUG("Invalid handle type: %d", handle_type);
    dbus_g_method_return_error(context, error);
    g_error_free(error);
    return;
  }

  for (n = names; *n != NULL; n++)  {
    if (*n == '\0') {
      DEBUG("Request for empty name?!");
      error = g_error_new(TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                           "Empty handle name");
      dbus_g_method_return_error(context, error);
      g_error_free(error);
      return;
    }
    DEBUG("Requested handle of type %d for %s", handle_type, *n);
    count++;
  }

  switch (handle_type)  {
    case TP_HANDLE_TYPE_CONTACT:
    case TP_HANDLE_TYPE_LIST:
    case TP_HANDLE_TYPE_ROOM:
      handles = g_array_sized_new(FALSE, FALSE, sizeof(TpHandle), count);
      for (i = 0; i < count ; i++) {
        TpHandle handle;
        const gchar *name = names[i];
        /*
        if (!tp_handle_name_is_valid(handle_type, name, &error)) {
          dbus_g_method_return_error(context, error);
          g_error_free(error);
          g_array_free(handles, TRUE);
          return;
        }
        */

        handle = tp_handle_ensure(handle_repo, name, NULL, &error);

        if (handle == 0) {
          error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                               "requested handle %s wasn't available", name);
          dbus_g_method_return_error(context, error);
          g_error_free(error);
          for (j = 0; j < i; j++) {
            tp_handle_unref(handle_repo,
                (TpHandle) g_array_index (handles, TpHandle, j));
          }
          g_array_free(handles, TRUE);
          return;
        }
        g_array_append_val(handles, handle);
      }
      hold_unref_and_return_handles (context, handle_repo, handles);
      g_array_free(handles, TRUE);
      break;
    default:
      DEBUG("Unimplemented handle type");
      error = g_error_new(TP_ERRORS, TP_ERROR_NOT_AVAILABLE, 
                          "unimplemented handle type %u", handle_type);
      dbus_g_method_return_error(context, error);
      g_error_free(error);
  }

  return;
}

static  void
salut_connection_connection_service_iface_init(gpointer g_iface, 
    gpointer iface_data)
{
  TpSvcConnectionClass *klass =
    (TpSvcConnectionClass *) g_iface;
#define IMPLEMENT(x) tp_svc_connection_implement_##x (klass, \
    salut_connection_##x)
  IMPLEMENT(get_interfaces);
  IMPLEMENT(request_handles);
#undef IMPLEMENT
}


