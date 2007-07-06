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
#include "salut-xmpp-connection-manager.h"
/*
#include "salut-tubes-manager.h"
*/

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

#define ACTIVITY_PAIR_TYPE \
  (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_UINT, \
      G_TYPE_INVALID))

#include <extensions/extensions.h>

static void
salut_connection_olpc_buddy_info_iface_init (gpointer g_iface,
    gpointer iface_data);

static void
salut_connection_olpc_activity_properties_iface_init (gpointer g_iface,
    gpointer iface_data);

#endif

static void
salut_connection_aliasing_service_iface_init (gpointer g_iface,
    gpointer iface_data);

static void
salut_connection_avatar_service_iface_init (gpointer g_iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE(SalutConnection,
    salut_connection,
    TP_TYPE_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_INTERFACE_ALIASING,
        salut_connection_aliasing_service_iface_init);
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_INTERFACE_PRESENCE,
       tp_presence_mixin_iface_init);
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_INTERFACE_AVATARS,
       salut_connection_avatar_service_iface_init);
#ifdef ENABLE_OLPC
    G_IMPLEMENT_INTERFACE (SALUT_TYPE_SVC_OLPC_BUDDY_INFO,
       salut_connection_olpc_buddy_info_iface_init);
    G_IMPLEMENT_INTERFACE (SALUT_TYPE_SVC_OLPC_ACTIVITY_PROPERTIES,
       salut_connection_olpc_activity_properties_iface_init);
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

  /* XMPP connection manager */
  SalutXmppConnectionManager *xmpp_connection_manager;

  /* Contact manager */
  SalutContactManager *contact_manager;

  /* IM channel manager */
  SalutImManager *im_manager;

  /* MUC channel manager */
  SalutMucManager *muc_manager;

  /* Tubes channel manager */
  /* XXX disabled while private tubes aren't implemented */
  /* SalutTubesManager *tubes_manager; */
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

  tp_presence_mixin_init ((GObject *) obj,
      G_STRUCT_OFFSET (SalutConnection, presence_mixin));

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
  priv->xmpp_connection_manager = NULL;
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

/* presence bits and pieces */

static const TpPresenceStatusOptionalArgumentSpec presence_args[] = {
      { "message", "s" },
      { NULL }
};

/* keep these in the same order as SalutPresenceId... */
static const TpPresenceStatusSpec presence_statuses[] = {
      { "available", TP_CONNECTION_PRESENCE_TYPE_AVAILABLE, TRUE,
        presence_args },
      { "away", TP_CONNECTION_PRESENCE_TYPE_AWAY, TRUE, presence_args },
      { "dnd", TP_CONNECTION_PRESENCE_TYPE_AWAY, TRUE, presence_args },
      { "offline", TP_CONNECTION_PRESENCE_TYPE_OFFLINE, FALSE, NULL },
      { NULL }
};
/* ... and these too (declared in salut-presence.h) */
const char *salut_presence_status_txt_names[] = {
  "avail",
  "away",
  "dnd",
  "offline",
  NULL
};

static gboolean
is_presence_status_available (GObject *obj,
                              guint index)
{
  return (index >= 0 && index < SALUT_PRESENCE_NR_PRESENCES);
}

static GHashTable *
make_presence_opt_args (SalutPresenceId presence, const gchar *message)
{
  GHashTable *ret;
  GValue *value;

  /* Omit missing or empty messages from the hash table.
   * Also, offline has no message in Salut, it wouldn't make sense. */
  if (presence == SALUT_PRESENCE_OFFLINE || message == NULL ||
      *message == '\0')
    {
      return NULL;
    }

  ret = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  value = g_slice_new0 (GValue);
  g_value_init (value, G_TYPE_STRING);
  g_value_set_string (value, message);
  g_hash_table_insert (ret, "message", value);

  return ret;
}

static GHashTable *
get_contact_statuses (GObject *obj,
                      const GArray *handles,
                      GError **error)
{
  SalutConnection *self = SALUT_CONNECTION (obj);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (base,
    TP_HANDLE_TYPE_CONTACT);
  GHashTable *ret;
  guint i;

  if (!tp_handles_are_valid (handle_repo, handles, FALSE, error))
    {
      return NULL;
    }

  ret = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) tp_presence_status_free);

  for (i = 0; i < handles->len; i++)
    {
      TpHandle handle = g_array_index (handles, TpHandle, i);
      TpPresenceStatus *ps = tp_presence_status_new
          (SALUT_PRESENCE_OFFLINE, NULL);
      const gchar *message = NULL;

      if (handle == base->self_handle)
        {
          ps->index = priv->self->status;
          message = priv->self->status_message;
        }
      else
        {
          SalutContact *contact = salut_contact_manager_get_contact
              (priv->contact_manager, handle);

          if (contact != NULL)
            {
              ps->index = contact->status;
              message = contact->status_message;
              g_object_unref (contact);
            }
        }

      ps->optional_arguments = make_presence_opt_args (ps->index, message);

      g_hash_table_insert (ret, GUINT_TO_POINTER (handle), ps);
    }

  return ret;
}

static gboolean
set_own_status (GObject *obj,
                const TpPresenceStatus *status,
                GError **error)
{
  SalutConnection *self = SALUT_CONNECTION (obj);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  TpBaseConnection *base = (TpBaseConnection *) self;
  gboolean ret;
  GError *err = NULL;
  const GValue *value;
  const gchar *message = NULL;
  SalutPresenceId presence = SALUT_PRESENCE_AVAILABLE;

  if (status != NULL)
    {
      /* mixin has already validated the index */
      presence = status->index;

      if (status->optional_arguments != NULL)
        {
          value = g_hash_table_lookup (status->optional_arguments, "message");
          if (value)
            {
              /* TpPresenceMixin should validate this for us, but doesn't */
              if (!G_VALUE_HOLDS_STRING (value))
                {
                  g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                      "Status argument 'message' requires a string");
                  return FALSE;
                }
              message = g_value_get_string (value);
            }
        }
    }

  ret = salut_self_set_presence (priv->self, presence, message, &err);

  if (ret)
    {
      TpPresenceStatus ps = { priv->self->status,
          make_presence_opt_args (priv->self->status,
              priv->self->status_message) };

      tp_presence_mixin_emit_one_presence_update ((GObject *) self,
          base->self_handle, &ps);

      if (ps.optional_arguments != NULL)
        g_hash_table_destroy (ps.optional_arguments);
    }
  else
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR, err->message);
    }

  return TRUE;
}

static void
salut_connection_class_init (SalutConnectionClass *salut_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_connection_class);
  TpBaseConnectionClass *tp_connection_class =
      TP_BASE_CONNECTION_CLASS(salut_connection_class);
  GParamSpec *param_spec;
  static const gchar *interfaces [] = {
    TP_IFACE_CONNECTION_INTERFACE_ALIASING,
    TP_IFACE_CONNECTION_INTERFACE_PRESENCE,
    TP_IFACE_CONNECTION_INTERFACE_AVATARS,
#ifdef ENABLE_OLPC
    SALUT_IFACE_OLPC_BUDDY_INFO,
    SALUT_IFACE_OLPC_ACTIVITY_PROPERTIES,
#endif
    NULL };

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
  tp_connection_class->interfaces_always_present = interfaces;

  tp_presence_mixin_class_init (object_class,
      G_STRUCT_OFFSET (SalutConnectionClass, presence_mixin),
      is_presence_status_available, get_contact_statuses, set_own_status,
      presence_statuses);

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

  if (priv->xmpp_connection_manager)
    {
      g_object_unref (priv->xmpp_connection_manager);
      priv->xmpp_connection_manager = NULL;
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
  tp_presence_mixin_finalize (object);
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

void
_contact_manager_contact_status_changed(SalutConnection *self,
                                        SalutContact *contact,
                                        TpHandle handle)
{
  TpPresenceStatus ps = { contact->status,
    make_presence_opt_args (contact->status, contact->status_message) };

  tp_presence_mixin_emit_one_presence_update ((GObject *) self, handle,
      &ps);

  if (ps.optional_arguments != NULL)
    g_hash_table_destroy (ps.optional_arguments);
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

  if (!salut_muc_manager_start (priv->muc_manager, priv->avahi_client, NULL))
    {
      /* XXX handle error */
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
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles(
      (TpBaseConnection *) self, TP_HANDLE_TYPE_ROOM);
  gint port;

  g_assert(c == priv->avahi_client);

  priv->self = salut_self_new(priv->avahi_client,
                              room_repo,
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

  port = salut_xmpp_connection_manager_listen (priv->xmpp_connection_manager);

  if (port == -1 || !salut_self_announce (priv->self, port, NULL))
    {
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

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

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
  TpBaseConnection *base = (TpBaseConnection *) self;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

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
  TpBaseConnection *base = (TpBaseConnection *) self;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

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

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

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

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

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
append_activity (const char *activity_id, TpHandle handle, gpointer user_data)
{
  GPtrArray *arr = user_data;
  GType type = ACTIVITY_PAIR_TYPE;
  GValue gvalue = {0};

  g_value_init (&gvalue, type);
  g_value_take_boxed (&gvalue,
      dbus_g_type_specialized_construct (type));

  dbus_g_type_struct_set (&gvalue,
      0, activity_id,
      1, handle,
      G_MAXUINT);
  g_ptr_array_add (arr, g_value_get_boxed (&gvalue));
}

static void
free_olpc_activities (GPtrArray *arr)
{
  GType type = ACTIVITY_PAIR_TYPE;
  guint i;

  for (i = 0; i < arr->len; i++)
    g_boxed_free (type, arr->pdata[i]);

  g_ptr_array_free (arr, TRUE);
}

static void
_contact_manager_contact_olpc_activities_changed (SalutConnection *self,
                                                  SalutContact *contact,
                                                  TpHandle handle)
{
  GPtrArray *activities = g_ptr_array_new ();

  DEBUG ("called for %u", handle);

  salut_contact_foreach_olpc_activity (contact, append_activity, activities);
  salut_svc_olpc_buddy_info_emit_activities_changed (self,
      handle, activities);
  free_olpc_activities (activities);
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

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

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
          /* FIXME: should this be InvalidHandle? */
          GError e = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "Unknown contact" };
          dbus_g_method_return_error (context, &e);
          return;
        }
      properties = get_properties_hash (contact->olpc_key, contact->olpc_color,
        contact->jid);
      g_object_unref (contact);
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

  /* this function explicitly supports being called when DISCONNECTED
   * or CONNECTING */

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
salut_connection_olpc_get_current_activity (SalutSvcOLPCBuddyInfo *iface,
                                            TpHandle handle,
                                            DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  DEBUG ("called for %u", handle);

  if (handle == base->self_handle)
    {
      DEBUG ("Returning my own cur.act.: %s -> %u",
          priv->self->olpc_cur_act ? priv->self->olpc_cur_act : "",
          priv->self->olpc_cur_act_room);
      salut_svc_olpc_buddy_info_return_from_get_current_activity (context,
          priv->self->olpc_cur_act ? priv->self->olpc_cur_act : "",
          priv->self->olpc_cur_act_room);
    }
  else
    {
      SalutContact *contact = salut_contact_manager_get_contact
        (priv->contact_manager, handle);

      if (contact == NULL)
        {
          /* FIXME: should this be InvalidHandle? */
          GError e = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "Unknown contact" };
          DEBUG ("Returning error: unknown contact");
          dbus_g_method_return_error (context, &e);
          return;
        }

      DEBUG ("Returning buddy %u cur.act.: %s -> %u", handle,
          contact->olpc_cur_act ? contact->olpc_cur_act : "",
          contact->olpc_cur_act_room);
      salut_svc_olpc_buddy_info_return_from_get_current_activity (context,
          contact->olpc_cur_act ? contact->olpc_cur_act : "",
          contact->olpc_cur_act_room);
      g_object_unref (contact);
    }
}

static void
salut_connection_olpc_set_current_activity (SalutSvcOLPCBuddyInfo *iface,
                                            const gchar *activity_id,
                                            TpHandle room_handle,
                                            DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_ROOM);
  GError *error = NULL;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  DEBUG ("called");

  if (activity_id[0] == '\0')
    {
      if (room_handle != 0)
        {
          GError e = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "If activity ID is empty, room handle must be 0" };

          dbus_g_method_return_error (context, &e);
          return;
        }
    }
  else
    {
      if (!tp_handle_is_valid (room_repo, room_handle, &error))
        {
          dbus_g_method_return_error (context, error);
          g_error_free (error);
          return;
        }
    }

  if (!salut_self_set_olpc_current_activity (priv->self, activity_id,
        room_handle, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  salut_svc_olpc_buddy_info_return_from_set_current_activity (context);
}

static void
salut_connection_olpc_get_activities (SalutSvcOLPCBuddyInfo *iface,
                                      TpHandle handle,
                                      DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  TpBaseConnection *base = (TpBaseConnection *) self;
  GPtrArray *arr;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  DEBUG ("called for %u", handle);

  if (handle == base->self_handle)
    {
      arr = g_ptr_array_new ();
      salut_self_foreach_olpc_activity (priv->self, append_activity, arr);
    }
  else
    {
      SalutContact *contact = salut_contact_manager_get_contact
        (priv->contact_manager, handle);

      if (contact == NULL)
        {
          /* FIXME: should this be InvalidHandle? */
          GError e = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "Unknown contact" };
          DEBUG ("Returning error: unknown contact");
          dbus_g_method_return_error (context, &e);
          return;
        }

      arr = g_ptr_array_new ();
      salut_contact_foreach_olpc_activity (contact, append_activity, arr);
      g_object_unref (contact);
    }

  salut_svc_olpc_buddy_info_return_from_get_activities (context, arr);
  free_olpc_activities (arr);
}

static void
salut_connection_olpc_set_activities (SalutSvcOLPCBuddyInfo *iface,
                                      const GPtrArray *activities,
                                      DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_ROOM);
  GHashTable *room_to_act_id = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) g_free);
  GError *error = NULL;
  guint i;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  for (i = 0; i < activities->len; i++)
    {
      GValue pair = {0};
      gchar *activity;
      guint room_handle;

      g_value_init (&pair, ACTIVITY_PAIR_TYPE);
      g_value_set_static_boxed (&pair, g_ptr_array_index (activities, i));
      dbus_g_type_struct_get (&pair,
          0, &activity,
          1, &room_handle,
          G_MAXUINT);

      if (activity[0] == '\0')
        {
          GError e = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "Invalid empty activity ID" };

          DEBUG ("%s", e.message);
          dbus_g_method_return_error (context, &e);
          g_free (activity);
          goto finally;
        }

      if (!tp_handle_is_valid (room_repo, room_handle, &error))
        {
          DEBUG ("Invalid room handle %u: %s", room_handle, error->message);
          dbus_g_method_return_error (context, error);
          g_error_free (error);
          g_free (activity);
          goto finally;
        }

      g_hash_table_insert (room_to_act_id, GUINT_TO_POINTER (room_handle),
          activity);
    }

  if (!salut_self_set_olpc_activities (priv->self, room_to_act_id, &error))
    {
      dbus_g_method_return_error (context, error);
    }
  else
    {
      salut_svc_olpc_buddy_info_return_from_set_activities (context);
    }

finally:
  g_hash_table_destroy (room_to_act_id);
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
  IMPLEMENT(set_activities);
  IMPLEMENT(get_activities);
  IMPLEMENT(set_current_activity);
  IMPLEMENT(get_current_activity);
#undef IMPLEMENT
}


static void
salut_connection_act_get_properties (SalutSvcOLPCActivityProperties *iface,
                                     TpHandle handle,
                                     DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_ROOM);
  GHashTable *properties = NULL;
  const gchar *color = NULL, *name = NULL, *type = NULL;
  GError *error = NULL;
  gboolean known = FALSE;
  GValue color_val = {0,};
  GValue name_val = {0,};
  GValue type_val = {0,};

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!tp_handle_is_valid(room_repo, handle, &error))
    goto error;

  if (salut_contact_manager_merge_olpc_activity_properties
      (priv->contact_manager, handle, &color, &name, &type))
    known = TRUE;

  /* Call this one second so it overwrites values from the first */
  if (salut_self_merge_olpc_activity_properties (priv->self, handle, &color,
        &name, &type))
    known = TRUE;

  if (!known)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Activity unknown: %u", handle);
      goto error;
    }

  properties = g_hash_table_new (g_str_hash, g_str_equal);

  if (color != NULL)
    {
      g_value_init (&color_val, G_TYPE_STRING);
      g_value_set_static_string (&color_val, color);
      g_hash_table_insert (properties, "color", &color_val);
    }
  if (name != NULL)
    {
      g_value_init (&name_val, G_TYPE_STRING);
      g_value_set_static_string (&name_val, name);
      g_hash_table_insert (properties, "name", &name_val);
    }
  if (type != NULL)
    {
      g_value_init (&type_val, G_TYPE_STRING);
      g_value_set_static_string (&type_val, type);
      g_hash_table_insert (properties, "type", &type_val);
    }

  salut_svc_olpc_buddy_info_return_from_get_properties (context, properties);
  g_hash_table_destroy (properties);

  return;

error:
  dbus_g_method_return_error (context, error);
  g_error_free (error);
}

static void
salut_connection_act_set_properties (SalutSvcOLPCActivityProperties *iface,
                                     TpHandle handle,
                                     GHashTable *properties,
                                     DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_ROOM);
  GError *error = NULL;
  const gchar *known_properties[] = { "color", "name", "type", NULL };
  const gchar *color = NULL;
  const gchar *name = NULL;
  const gchar *type = NULL;
  const GValue *val;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!tp_handle_is_valid(room_repo, handle, &error))
    goto error;

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

  val = g_hash_table_lookup (properties, "type");
  if (val != NULL)
    {
      if (G_VALUE_TYPE (val) != G_TYPE_STRING)
        {
          error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "name value should be of type s");
          goto error;
        }

      type = g_value_get_string (val);

      if (type[0] == '\0')
        {
          error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "type value must be non-empty");
          goto error;
        }
    }

  val = g_hash_table_lookup (properties, "name");
  if (val != NULL)
    {
      if (G_VALUE_TYPE (val) != G_TYPE_STRING)
        {
          error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "name value should be of type s");
          goto error;
        }

      name = g_value_get_string (val);
    }

  if (!salut_self_set_olpc_activity_properties (priv->self, handle, color,
        name, type, &error))
    goto error;

  salut_svc_olpc_activity_properties_return_from_set_properties (context);
  return;

error:
  dbus_g_method_return_error (context, error);
  g_error_free (error);
}

static void
salut_connection_olpc_activity_properties_iface_init (gpointer g_iface,
                                                      gpointer iface_data)
{
  SalutSvcOLPCActivityPropertiesClass *klass =
    (SalutSvcOLPCActivityPropertiesClass *) g_iface;
#define IMPLEMENT(x) salut_svc_olpc_activity_properties_implement_##x \
  (klass, salut_connection_act_##x)
  IMPLEMENT(set_properties);
  IMPLEMENT(get_properties);
#undef IMPLEMENT
}
#endif

static gchar *
handle_normalize_require_nonempty (TpHandleRepoIface *repo,
                                   const gchar *id,
                                   gpointer context,
                                   GError **error)
{
  g_return_val_if_fail (id != NULL, NULL);

  if (*id == '\0')
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_HANDLE,
          "Salut handle names may not be the empty string");
      return NULL;
    }

  return g_strdup (id);
}

/* Connection baseclass function implementations */
static void
salut_connection_create_handle_repos(TpBaseConnection *self,
    TpHandleRepoIface *repos[NUM_TP_HANDLE_TYPES])
{
  static const char *list_handle_strings[] = {
    "publish",
    "subscribe",
    "known",
    NULL
  };

  repos[TP_HANDLE_TYPE_CONTACT] = tp_dynamic_handle_repo_new
      (TP_HANDLE_TYPE_CONTACT, handle_normalize_require_nonempty, NULL);

  repos[TP_HANDLE_TYPE_ROOM] = tp_dynamic_handle_repo_new
      (TP_HANDLE_TYPE_ROOM, handle_normalize_require_nonempty, NULL);

  repos[TP_HANDLE_TYPE_LIST] = tp_static_handle_repo_new (TP_HANDLE_TYPE_LIST,
      list_handle_strings);
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

  if (changes & SALUT_CONTACT_OLPC_CURRENT_ACTIVITY)
    salut_svc_olpc_buddy_info_emit_current_activity_changed (self,
        handle, contact->olpc_cur_act, contact->olpc_cur_act_room);

  if (changes & SALUT_CONTACT_OLPC_ACTIVITIES)
    _contact_manager_contact_olpc_activities_changed (self, contact, handle);
#endif
}

static GPtrArray*
salut_connection_create_channel_factories(TpBaseConnection *base) {
  SalutConnection *self = SALUT_CONNECTION(base);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE(self);
  GPtrArray *factories = g_ptr_array_sized_new(3);

  /* Create the contact manager */
  priv->contact_manager = salut_contact_manager_new (self);
  g_signal_connect (priv->contact_manager, "contact-change",
      G_CALLBACK (_contact_manager_contact_change_cb), self);

  /* Create the XMPP connection manager */
  priv->xmpp_connection_manager = salut_xmpp_connection_manager_new (self,
      priv->contact_manager);

  priv->im_manager = salut_im_manager_new (self, priv->contact_manager,
      priv->xmpp_connection_manager);

  priv->muc_manager = salut_muc_manager_new(self, priv->im_manager);

  /*
  priv->tubes_manager = salut_tubes_manager_new (self, priv->contact_manager);
  */

  g_ptr_array_add(factories, priv->contact_manager);
  g_ptr_array_add(factories, priv->im_manager);
  g_ptr_array_add(factories, priv->muc_manager);
  /*
  g_ptr_array_add (factories, priv->tubes_manager);
  */

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
