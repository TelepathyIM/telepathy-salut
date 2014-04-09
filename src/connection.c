/*
 * connection.c - Source for SalutConnection
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
#include "connection.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>

#include <dbus/dbus-protocol.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include <salut/caps-channel-manager.h>
#include <salut/plugin-connection.h>

#ifdef USE_BACKEND_AVAHI
#include "avahi-discovery-client.h"
#endif
#include "capabilities.h"
#include "caps-hash.h"
#include "connection-contact-info.h"
#include "contact.h"
#include "contact-manager.h"
#include "disco.h"
#include "discovery-client.h"
#include "im-manager.h"
#include "muc-manager.h"
#include "ft-manager.h"
#include "contact.h"
#include "roomlist-manager.h"
#include "presence.h"
#include "presence-cache.h"
#include "self.h"
#include "si-bytestream-manager.h"
#include "tubes-manager.h"
#include "util.h"
#include "namespaces.h"

#include "plugin-loader.h"

#include <extensions/extensions.h>

#define DEBUG_FLAG DEBUG_CONNECTION
#include "debug.h"

static void
salut_connection_aliasing_service_iface_init (gpointer g_iface,
    gpointer iface_data);

static void
salut_connection_avatar_service_iface_init (gpointer g_iface,
    gpointer iface_data);

static void
salut_conn_contact_caps_iface_init (gpointer, gpointer);

static void
salut_plugin_connection_iface_init (SalutPluginConnectionInterface *iface,
    gpointer iface_data);

static void salut_conn_sidecars_iface_init (gpointer, gpointer);

#define DISCONNECT_TIMEOUT 5

G_DEFINE_TYPE_WITH_CODE(SalutConnection,
    salut_connection,
    TP_TYPE_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_INTERFACE_ALIASING1,
        salut_connection_aliasing_service_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACT_LIST1,
      tp_base_contact_list_mixin_list_iface_init);
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_INTERFACE_PRESENCE1,
       tp_presence_mixin_iface_init);
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_INTERFACE_AVATARS1,
       salut_connection_avatar_service_iface_init);
    G_IMPLEMENT_INTERFACE
      (TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACT_CAPABILITIES1,
      salut_conn_contact_caps_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACT_INFO1,
        salut_conn_contact_info_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_SIDECARS1,
      salut_conn_sidecars_iface_init);
    G_IMPLEMENT_INTERFACE (SALUT_TYPE_PLUGIN_CONNECTION,
      salut_plugin_connection_iface_init);
    )

/* properties */
enum {
  PROP_NICKNAME = 1,
  PROP_FIRST_NAME,
  PROP_LAST_NAME,
  PROP_JID,
  PROP_EMAIL,
  PROP_PUBLISHED_NAME,
  PROP_IM_MANAGER,
  PROP_MUC_MANAGER,
  PROP_TUBES_MANAGER,
  PROP_ROOMLIST_MANAGER,
  PROP_CONTACT_MANAGER,
  PROP_SELF,
  PROP_XCM,
  PROP_SI_BYTESTREAM_MANAGER,
  PROP_BACKEND,
  PROP_DNSSD_NAME,
  LAST_PROP
};

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

  /* Discovery client for browsing and resolving */
  SalutDiscoveryClient *discovery_client;

  /* TpHandler for our presence on the lan */
  SalutSelf *self;
  gboolean self_established;
  SalutPresenceId pre_connect_presence;
  gchar *pre_connect_message;
  GabbleCapabilitySet *pre_connect_caps;
  GPtrArray *pre_connect_data_forms;

  /* Contact manager */
  SalutContactManager *contact_manager;

  /* IM channel manager */
  SalutImManager *im_manager;

  /* MUC channel manager */
  SalutMucManager *muc_manager;

  /* FT channel manager */
  SalutFtManager *ft_manager;

  /* Roomlist channel manager */
  SalutRoomlistManager *roomlist_manager;

  /* Tubes channel manager */
  SalutTubesManager *tubes_manager;

#ifndef USE_BACKEND_BONJOUR
  /* Bytestream manager for stream initiation (XEP-0095) */
  SalutSiBytestreamManager *si_bytestream_manager;
#endif

  /* Sidecars */
  /* gchar *interface → SalutSidecar */
  GHashTable *sidecars;

  /* gchar *interface → GList<GDBusMethodInvocation> */
  GHashTable *pending_sidecars;

  /* timer used when trying to properly disconnect */
  guint disconnect_timer;

  /* Backend type: avahi or dummy */
  GType backend_type;

  /* DNS-SD name, used for the avahi backend */
  gchar *dnssd_name;
};

static void _salut_connection_disconnect (SalutConnection *self);

static void
salut_connection_create_handle_repos (TpBaseConnection *self,
    TpHandleRepoIface *repos[TP_NUM_ENTITY_TYPES]);

static GPtrArray *
salut_connection_create_channel_managers (TpBaseConnection *self);

static gchar *
salut_connection_get_unique_connection_name (TpBaseConnection *self);

static void
salut_connection_shut_down (TpBaseConnection *self);

static gboolean
salut_connection_start_connecting (TpBaseConnection *self, GError **error);

static void connection_capabilities_update_cb (SalutPresenceCache *cache,
    TpHandle handle, gpointer user_data);

static void
conn_avatars_properties_getter (GObject *object, GQuark interface, GQuark name,
    GValue *value, gpointer getter_data);

static const char *mimetypes[] = {
    "image/png", "image/jpeg", NULL };

#define AVATAR_MIN_PX 0
#define AVATAR_REC_PX 64
#define AVATAR_MAX_PX 0
#define AVATAR_MAX_BYTES G_MAXUINT16

static TpDBusPropertiesMixinPropImpl conn_avatars_properties[] = {
      { "MinimumAvatarWidth", GUINT_TO_POINTER (AVATAR_MIN_PX), NULL },
      { "RecommendedAvatarWidth", GUINT_TO_POINTER (AVATAR_REC_PX), NULL },
      { "MaximumAvatarWidth", GUINT_TO_POINTER (AVATAR_MAX_PX), NULL },
      { "MinimumAvatarHeight", GUINT_TO_POINTER (AVATAR_MIN_PX), NULL },
      { "RecommendedAvatarHeight", GUINT_TO_POINTER (AVATAR_REC_PX), NULL },
      { "MaximumAvatarHeight", GUINT_TO_POINTER (AVATAR_MAX_PX), NULL },
      { "MaximumAvatarBytes", GUINT_TO_POINTER (AVATAR_MAX_BYTES), NULL },
      /* special-cased - it's the only one with a non-guint value */
      { "SupportedAvatarMIMETypes", NULL, NULL },
      { NULL }
};

static TpDBusPropertiesMixinPropImpl conn_aliasing_properties[] = {
    { "AliasFlags", GUINT_TO_POINTER (0), NULL },
    { NULL }
};

static void
conn_aliasing_properties_getter (GObject *object,
    GQuark interface,
    GQuark name,
    GValue *value,
    gpointer getter_data)
{
  g_value_set_uint (value, GPOINTER_TO_UINT (getter_data));
}

static void
salut_connection_init (SalutConnection *obj)
{
  SalutConnectionPrivate *priv =
    G_TYPE_INSTANCE_GET_PRIVATE(obj, SALUT_TYPE_CONNECTION,
                                SalutConnectionPrivate);

  obj->priv = priv;
  obj->name = NULL;

  gabble_capabilities_init (obj);

  tp_presence_mixin_init ((GObject *) obj,
      G_STRUCT_OFFSET (SalutConnection, presence_mixin));

  /* create this now so channel managers can use it when created from
   * parent->constructor */
  obj->session = wocky_session_new_ll (NULL);
  obj->porter = wocky_session_get_porter (obj->session);

  /* allocate any data required by the object here */
  priv->published_name = g_strdup (g_get_user_name ());
  priv->nickname = NULL;
  priv->first_name = NULL;
  priv->last_name = NULL;
  priv->jid = NULL;
  priv->email = NULL;

  priv->discovery_client = NULL;
  priv->self = NULL;
  priv->self_established = FALSE;

  priv->pre_connect_presence = SALUT_PRESENCE_AVAILABLE;
  priv->pre_connect_message = NULL;
  priv->pre_connect_caps = NULL;

  priv->contact_manager = NULL;
}

static void
sidecars_conn_status_changed_cb (SalutConnection *conn,
    guint status, guint reason, gpointer unused);

static void _contact_manager_contact_change_cb (SalutContactManager *mgr,
    SalutContact *contact, int changes, gpointer data);

static void
salut_connection_constructed (GObject *obj)
{
  SalutConnection *self = (SalutConnection *) obj;

  self->disco = salut_disco_new (self);
  self->presence_cache = salut_presence_cache_new (self);
  g_signal_connect (self->presence_cache, "capabilities-update", G_CALLBACK
      (connection_capabilities_update_cb), self);

  self->priv->sidecars = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_object_unref);
  self->priv->pending_sidecars = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) g_list_free);

  g_signal_connect (self, "status-changed",
      (GCallback) sidecars_conn_status_changed_cb, NULL);

  if (G_OBJECT_CLASS (salut_connection_parent_class)->constructed)
    G_OBJECT_CLASS (salut_connection_parent_class)->constructed (obj);
}

static void
salut_connection_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  SalutConnection *self = SALUT_CONNECTION(object);
  SalutConnectionPrivate *priv = self->priv;

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
    case PROP_IM_MANAGER:
      g_value_set_object (value, priv->im_manager);
      break;
#ifndef USE_BACKEND_BONJOUR
    case PROP_MUC_MANAGER:
      g_value_set_object (value, priv->muc_manager);
      break;
#endif
    case PROP_TUBES_MANAGER:
      g_value_set_object (value, priv->tubes_manager);
      break;
    case PROP_ROOMLIST_MANAGER:
      g_value_set_object (value, priv->roomlist_manager);
      break;
    case PROP_CONTACT_MANAGER:
      g_value_set_object (value, priv->contact_manager);
      break;
    case PROP_SELF:
      g_value_set_object (value, priv->self);
      break;
#ifndef USE_BACKEND_BONJOUR
    case PROP_SI_BYTESTREAM_MANAGER:
      g_value_set_object (value, priv->si_bytestream_manager);
      break;
#endif
    case PROP_BACKEND:
      g_value_set_gtype (value, priv->backend_type);
      break;
    case PROP_DNSSD_NAME:
      g_value_set_string (value, priv->dnssd_name);
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
  SalutConnectionPrivate *priv = self->priv;

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
    case PROP_BACKEND:
      priv->backend_type = g_value_get_gtype (value);
      /* Create the backend object */
      priv->discovery_client = g_object_new (priv->backend_type,
          "dnssd-name", priv->dnssd_name,
          NULL);
      g_assert (priv->discovery_client != NULL);
      break;
    case PROP_DNSSD_NAME:
      priv->dnssd_name = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}


static void salut_connection_dispose (GObject *object);
static void salut_connection_finalize (GObject *object);

/* presence bits and pieces */

/* keep these in the same order as SalutPresenceId... */
static const TpPresenceStatusSpec presence_statuses[] = {
      { "available", TP_CONNECTION_PRESENCE_TYPE_AVAILABLE, TRUE, TRUE },
      { "away", TP_CONNECTION_PRESENCE_TYPE_AWAY, TRUE, TRUE },
      { "dnd", TP_CONNECTION_PRESENCE_TYPE_BUSY, TRUE, TRUE },
      { "offline", TP_CONNECTION_PRESENCE_TYPE_OFFLINE, FALSE, FALSE },
      { NULL }
};
/* ... and these too (declared in presence.h) */
const char *salut_presence_status_txt_names[] = {
  "avail",
  "away",
  "dnd",
  "offline",
  NULL
};

static gboolean
is_presence_status_available (GObject *obj,
                              guint index_)
{
  return (index_ < SALUT_PRESENCE_OFFLINE);
}

static TpPresenceStatus *
get_contact_status (GObject *obj,
    TpHandle handle)
{
  SalutConnection *self = SALUT_CONNECTION (obj);
  SalutConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandle self_handle = tp_base_connection_get_self_handle (base);
  SalutPresenceId presence;
  const gchar *message = NULL;

  if (handle == self_handle)
    {
      presence = priv->self->status;
      message = priv->self->status_message;
    }
  else
    {
      SalutContact *contact = salut_contact_manager_get_contact
          (priv->contact_manager, handle);

      if (contact == NULL)
        {
          presence = SALUT_PRESENCE_OFFLINE;
          message = "";
        }
      else
        {
          presence = contact->status;
          message = contact->status_message;
          g_object_unref (contact);
        }
    }

  return tp_presence_status_new (presence, message);
}

static void
set_self_presence (SalutConnection *self,
    SalutPresenceId presence,
    const gchar *message,
    GError **error)
{
  SalutConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = TP_BASE_CONNECTION (self);

  if (priv->self == NULL || !priv->self_established)
    {
      g_free (priv->pre_connect_message);
      priv->pre_connect_presence = presence;
      priv->pre_connect_message = g_strdup (message);
      return;
    }

  if (salut_self_set_presence (priv->self, presence, message, error))
    {
      TpHandle self_handle = tp_base_connection_get_self_handle (base);
      TpPresenceStatus ps = { priv->self->status,
              priv->self->status_message };

      tp_presence_mixin_emit_one_presence_update ((GObject *) self,
          self_handle, &ps);
    }
}

static gboolean
set_own_status (GObject *obj,
                const TpPresenceStatus *status,
                GError **error)
{
  SalutConnection *self = SALUT_CONNECTION (obj);
  GError *err = NULL;
  const gchar *message = NULL;
  SalutPresenceId presence = SALUT_PRESENCE_AVAILABLE;

  if (status != NULL)
    {
      /* mixin has already validated the index */
      presence = status->index;
      message = status->message;
    }

  set_self_presence (self, presence, message, &err);

  if (err != NULL)
    {
      *error = g_error_new_literal (TP_ERROR, TP_ERROR_NETWORK_ERROR,
          err->message);
    }

  return TRUE;
}

static const gchar *interfaces [] = {
  TP_IFACE_CONNECTION_INTERFACE_ALIASING1,
  TP_IFACE_CONNECTION_INTERFACE_AVATARS1,
  TP_IFACE_CONNECTION_INTERFACE_PRESENCE1,
  TP_IFACE_CONNECTION_INTERFACE_CONTACT_CAPABILITIES1,
  TP_IFACE_CONNECTION_INTERFACE_CONTACT_INFO1,
  TP_IFACE_CONNECTION_INTERFACE_CONTACT_LIST1,
  TP_IFACE_CONNECTION_INTERFACE_SIDECARS1,
  NULL };

static GPtrArray *
get_interfaces (TpBaseConnection *base)
{
  GPtrArray *arr;
  const gchar **iter;

  arr = TP_BASE_CONNECTION_CLASS (
      salut_connection_parent_class)->get_interfaces_always_present (base);

  for (iter = interfaces; *iter != NULL; iter++)
    g_ptr_array_add (arr, (gchar *) *iter);

  return arr;
}

const gchar * const *
salut_connection_get_implemented_interfaces (void)
{
  return interfaces;
}

static void salut_connection_fill_contact_attributes (TpBaseConnection *base,
    const gchar *dbus_interface,
    TpHandle handle,
    TpContactAttributeMap *attributes);

static void
salut_connection_class_init (SalutConnectionClass *salut_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_connection_class);
  TpBaseConnectionClass *tp_connection_class =
      TP_BASE_CONNECTION_CLASS(salut_connection_class);
  GParamSpec *param_spec;
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
        { TP_IFACE_CONNECTION_INTERFACE_AVATARS1,
          conn_avatars_properties_getter,
          NULL,
          conn_avatars_properties,
        },
        { TP_IFACE_CONNECTION_INTERFACE_ALIASING1,
          conn_aliasing_properties_getter,
          NULL,
          conn_aliasing_properties,
        },
        { NULL }
  };

  object_class->get_property = salut_connection_get_property;
  object_class->set_property = salut_connection_set_property;

  g_type_class_add_private (salut_connection_class,
      sizeof (SalutConnectionPrivate));

  object_class->constructed = salut_connection_constructed;

  object_class->dispose = salut_connection_dispose;
  object_class->finalize = salut_connection_finalize;

  tp_connection_class->create_handle_repos =
      salut_connection_create_handle_repos;
  tp_connection_class->create_channel_managers =
      salut_connection_create_channel_managers;
  tp_connection_class->get_unique_connection_name =
      salut_connection_get_unique_connection_name;
  tp_connection_class->shut_down =
      salut_connection_shut_down;
  tp_connection_class->start_connecting =
      salut_connection_start_connecting;
  tp_connection_class->get_interfaces_always_present = get_interfaces;
  tp_connection_class->fill_contact_attributes =
      salut_connection_fill_contact_attributes;

  salut_connection_class->properties_mixin.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (SalutConnectionClass, properties_mixin));

  tp_presence_mixin_class_init (object_class,
      G_STRUCT_OFFSET (SalutConnectionClass, presence_mixin),
      is_presence_status_available, get_contact_status, set_own_status,
      presence_statuses);

  tp_presence_mixin_init_dbus_properties (object_class);

  salut_conn_contact_info_class_init (salut_connection_class);

  tp_base_contact_list_mixin_class_init (tp_connection_class);

  param_spec = g_param_spec_string ("nickname", "nickname",
      "Nickname used in the published data", NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_NICKNAME, param_spec);

  param_spec = g_param_spec_string ("first-name", "First name",
      "First name used in the published data", NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_FIRST_NAME, param_spec);

  param_spec = g_param_spec_string ("last-name", "Last name",
      "Last name used in the published data", NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_LAST_NAME, param_spec);

  param_spec = g_param_spec_string ("email", "E-mail address",
      "E-mail address used in the published data", NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_EMAIL, param_spec);

  param_spec = g_param_spec_string ("jid", "Jabber id",
      "Jabber id used in the published data", NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_JID, param_spec);

  param_spec = g_param_spec_string ("published-name", "Published name",
      "Username used in the published data", NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PUBLISHED_NAME,
      param_spec);

  param_spec = g_param_spec_object (
      "im-manager",
      "SalutImManager object",
      "The Salut IM Manager associated with this Salut Connection",
      SALUT_TYPE_IM_MANAGER,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_IM_MANAGER,
      param_spec);

  param_spec = g_param_spec_object (
      "muc-manager",
      "SalutMucManager object",
      "The Salut MUC Manager associated with this Salut Connection",
      SALUT_TYPE_MUC_MANAGER,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MUC_MANAGER,
      param_spec);

  param_spec = g_param_spec_object (
      "tubes-manager",
      "SalutTubesManager object",
      "The Salut Tubes Manager associated with this Salut Connection",
      SALUT_TYPE_TUBES_MANAGER,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TUBES_MANAGER,
      param_spec);

  param_spec = g_param_spec_object (
      "roomlist-manager",
      "SalutRoomlistManager object",
      "The Salut Roomlist Manager associated with this Salut Connection",
      SALUT_TYPE_ROOMLIST_MANAGER,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ROOMLIST_MANAGER,
      param_spec);

  param_spec = g_param_spec_object (
      "contact-manager",
      "SalutContactManager object",
      "The Salut Contact Manager associated with this Salut Connection",
      SALUT_TYPE_CONTACT_MANAGER,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONTACT_MANAGER,
      param_spec);

  param_spec = g_param_spec_object (
      "self",
      "SalutSelf object",
      "The Salut Self object associated with this Salut Connection",
      SALUT_TYPE_SELF,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SELF,
      param_spec);

  param_spec = g_param_spec_object (
      "si-bytestream-manager",
      "SalutSiBytestreamManager object",
      "The Salut SI Bytestream Manager associated with this Salut Connection",
      SALUT_TYPE_SI_BYTESTREAM_MANAGER,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SI_BYTESTREAM_MANAGER,
      param_spec);

  param_spec = g_param_spec_gtype (
      "backend-type",
      "backend type",
      "a G_TYPE_GTYPE of the backend to use",
      G_TYPE_NONE,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_BACKEND,
      param_spec);

  param_spec = g_param_spec_string ("dnssd-name", "DNS-SD name",
      "The DNS-SD name of the protocol", "",
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_DNSSD_NAME,
      param_spec);
}

void
salut_connection_dispose (GObject *object)
{
  SalutConnection *self = SALUT_CONNECTION (object);
  SalutConnectionPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (self->disco != NULL)
    {
      g_object_unref (self->disco);
      self->disco = NULL;
    }

  if (self->presence_cache)
    {
      g_object_unref (self->presence_cache);
      self->presence_cache = NULL;
    }

  if (priv->pre_connect_message != NULL)
    {
      g_free (priv->pre_connect_message);
      priv->pre_connect_message = NULL;
    }

  if (priv->pre_connect_caps != NULL)
    {
      gabble_capability_set_free (priv->pre_connect_caps);
      priv->pre_connect_caps = NULL;
    }

  if (priv->pre_connect_data_forms != NULL)
    {
      g_ptr_array_unref (priv->pre_connect_data_forms);
      priv->pre_connect_data_forms = NULL;
    }

  if (priv->self)
    {
      g_object_unref (priv->self);
      priv->self = NULL;
    }

  if (self->session != NULL)
    {
      g_object_unref (self->session);
      self->session = NULL;
      self->porter = NULL;
    }

  if (priv->discovery_client != NULL)
    {
      g_object_unref (priv->discovery_client);
      g_signal_handlers_disconnect_matched (priv->discovery_client,
          G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
      priv->discovery_client = NULL;
    }

#ifndef USE_BACKEND_BONJOUR
  if (priv->si_bytestream_manager != NULL)
    {
      g_object_unref (priv->si_bytestream_manager);
      priv->si_bytestream_manager = NULL;
    }
#endif

  g_warn_if_fail (g_hash_table_size (priv->sidecars) == 0);
  tp_clear_pointer (&priv->sidecars, g_hash_table_unref);

  g_warn_if_fail (g_hash_table_size (priv->pending_sidecars) == 0);
  tp_clear_pointer (&priv->pending_sidecars, g_hash_table_unref);

  /* release any references held by the object here */
  if (G_OBJECT_CLASS (salut_connection_parent_class)->dispose)
    G_OBJECT_CLASS (salut_connection_parent_class)->dispose (object);
}

void
salut_connection_finalize (GObject *object)
{
  SalutConnection *self = SALUT_CONNECTION (object);
  SalutConnectionPrivate *priv = self->priv;

  /* free any data held directly by the object here */
  tp_presence_mixin_finalize (object);
  g_free (self->name);
  g_free (priv->published_name);
  g_free (priv->first_name);
  g_free (priv->last_name);
  g_free (priv->email);
  g_free (priv->jid);
  g_free (priv->dnssd_name);

  gabble_capabilities_finalize (self);

  DEBUG("Finalizing connection");

  G_OBJECT_CLASS (salut_connection_parent_class)->finalize (object);
}

static void
_contact_manager_contact_status_changed (SalutConnection *self,
    SalutContact *contact, TpHandle handle)
{
  TpPresenceStatus ps = { contact->status, contact->status_message };

  tp_presence_mixin_emit_one_presence_update ((GObject *) self, handle,
      &ps);
}

static gboolean
announce_self_caps (SalutConnection *self,
                    GError **error)
{
  SalutConnectionPrivate *priv = self->priv;
  gchar *caps_hash;
  gboolean ret;

  caps_hash = caps_hash_compute_from_self_presence (priv->self);

  ret = salut_self_set_caps (priv->self, WOCKY_TELEPATHY_NS_CAPS, "sha-1",
        caps_hash, error);

  if (ret)
    {
      salut_presence_cache_learn_caps (self->presence_cache,
          WOCKY_TELEPATHY_NS_CAPS, caps_hash, salut_self_get_caps (priv->self),
          wocky_xep_0115_capabilities_get_data_forms (WOCKY_XEP_0115_CAPABILITIES (priv->self)));
    }

  g_free (caps_hash);
  return ret;
}

static void
_self_established_cb (SalutSelf *s, gpointer data)
{
  SalutConnection *self = SALUT_CONNECTION (data);
  SalutConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (
      TP_BASE_CONNECTION (self), TP_ENTITY_TYPE_CONTACT);
  GError *error = NULL;

  priv->self_established = TRUE;

  g_free (self->name);
  self->name = g_strdup (s->name);

  tp_base_connection_set_self_handle (base,
      tp_handle_ensure (handle_repo, self->name, NULL, NULL));

  wocky_session_set_jid (self->session, self->name);

  set_self_presence (self, priv->pre_connect_presence,
      priv->pre_connect_message, &error);

  if (error != NULL)
    {
      DEBUG ("Failed to set presence from pre-connection: %s", error->message);
      g_clear_error (&error);
    }

  g_free (priv->pre_connect_message);
  priv->pre_connect_message = NULL;

  if (!salut_contact_manager_start (priv->contact_manager, &error))
    {
      DEBUG ("failed to start contact manager: %s", error->message);
      g_clear_error (&error);

      tp_base_connection_change_status ( TP_BASE_CONNECTION (base),
          TP_CONNECTION_STATUS_DISCONNECTED,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
      return;
  }

#ifndef USE_BACKEND_BONJOUR
  if (!salut_roomlist_manager_start (priv->roomlist_manager, &error))
    {
      DEBUG ("failed to start roomlist manager: %s", error->message);
      g_clear_error (&error);

      tp_base_connection_change_status ( TP_BASE_CONNECTION (base),
          TP_CONNECTION_STATUS_DISCONNECTED,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
      return;
    }
#endif

  tp_base_connection_change_status (base, TP_CONNECTION_STATUS_CONNECTED,
      TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);
}


static void
_self_failed_cb (SalutSelf *s, GError *error, gpointer data)
{
  SalutConnection *self = SALUT_CONNECTION (data);
  TpBaseConnection *base = TP_BASE_CONNECTION (self);

  /* FIXME better error handling */
  tp_base_connection_change_status (base, TP_CONNECTION_STATUS_DISCONNECTED,
     TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);
}

static void
discovery_client_running (SalutConnection *self)
{
  SalutConnectionPrivate *priv = self->priv;
  GError *error = NULL;
  guint16 port;

  priv->self = salut_discovery_client_create_self (priv->discovery_client,
      self, priv->nickname, priv->first_name, priv->last_name, priv->jid,
      priv->email, priv->published_name);

  if (priv->pre_connect_caps != NULL)
    {
      salut_self_take_caps (priv->self, priv->pre_connect_caps);
      priv->pre_connect_caps = NULL;
    }

  if (priv->pre_connect_data_forms != NULL)
    {
      salut_self_take_data_forms (priv->self, priv->pre_connect_data_forms);
      priv->pre_connect_data_forms = NULL;
    }

  g_signal_connect (priv->self, "established",
                    G_CALLBACK(_self_established_cb), self);
  g_signal_connect (priv->self, "failure",
                    G_CALLBACK(_self_failed_cb), self);

  wocky_session_start (self->session);

  port = wocky_meta_porter_get_port (WOCKY_META_PORTER (self->porter));

  if (!announce_self_caps (self, &error))
    {
      DEBUG ("Can't announce our capabilities: %s", error->message);
      g_error_free (error);
    }

  if (port == 0 || !salut_self_announce (priv->self, port, &error))
    {
      DEBUG ("failed to announce: %s",
          error != NULL ? error->message : "(no error message)");

      tp_base_connection_change_status (
            TP_BASE_CONNECTION (self),
            TP_CONNECTION_STATUS_DISCONNECTED,
            TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);

      g_clear_error (&error);
      return;
    }

  /* Tubes are not currently supported by bonjour backend */
#ifndef USE_BACKEND_BONJOUR
  /* Create the bytestream manager */
  priv->si_bytestream_manager = salut_si_bytestream_manager_new (self,
    salut_discovery_client_get_host_name_fqdn (priv->discovery_client));
#endif
}

static void
_discovery_client_state_changed_cb (SalutDiscoveryClient *client,
                                    SalutDiscoveryClientState state,
                                    SalutConnection *self)
{
  SalutConnectionPrivate *priv = self->priv;

  g_assert (client == priv->discovery_client);

  if (state == SALUT_DISCOVERY_CLIENT_STATE_CONNECTED)
    {
      discovery_client_running (self);
    }
  else if (state == SALUT_DISCOVERY_CLIENT_STATE_DISCONNECTED)
    {
      /* FIXME better error messages */
      /* FIXME instead of full disconnect we could handle the avahi restart */
      DEBUG ("discovery client got disconnected");
      tp_base_connection_change_status (TP_BASE_CONNECTION (self),
          TP_CONNECTION_STATUS_DISCONNECTED,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
    }
}
/* public functions */
static void
_salut_connection_disconnect (SalutConnection *self)
{
  SalutConnectionPrivate *priv = self->priv;

  if (priv->self)
    {
      g_object_unref (priv->self);
      priv->self = NULL;
    }

  if (priv->discovery_client != NULL)
    {
      g_object_unref (priv->discovery_client);
      g_signal_handlers_disconnect_matched (priv->discovery_client,
          G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
      priv->discovery_client = NULL;
    }
}


/* Aliasing interface */

static const gchar *
salut_connection_get_alias (SalutConnection *self, TpHandle handle)
{
  SalutConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base,
    TP_ENTITY_TYPE_CONTACT);
  const gchar *alias;

  if (handle == tp_base_connection_get_self_handle (base))
    {
      alias = salut_self_get_alias (priv->self);
    }
  else
    {
      SalutContact *contact;
      contact = salut_contact_manager_get_contact (priv->contact_manager,
        handle);

      if (contact == NULL)
        {
          alias = tp_handle_inspect (contact_repo, handle);
        }
      else
        {
          alias = salut_contact_get_alias (contact);
          g_object_unref (contact);
        }
    }

  return alias;
}

/**
 * salut_connection_request_aliases
 *
 * Implements D-Bus method RequestAliases
 * on interface im.telepathy.v1.Connection.Interface.Aliasing
 *
 */
static void
salut_connection_request_aliases (TpSvcConnectionInterfaceAliasing1 *iface,
    const GArray *contacts, GDBusMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  guint i;
  const gchar **aliases;
  GError *error = NULL;
  TpHandleRepoIface *contact_handles =
      tp_base_connection_get_handles (base, TP_ENTITY_TYPE_CONTACT);

  DEBUG ("Alias requested");

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!tp_handles_are_valid (contact_handles, contacts, FALSE, &error))
    {
      g_dbus_method_invocation_return_gerror (context, error);
      g_error_free (error);
      return;
    }

  aliases = g_new0 (const gchar *, contacts->len + 1);
  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);

      aliases[i] = salut_connection_get_alias (self, handle);
    }

  tp_svc_connection_interface_aliasing1_return_from_request_aliases (context,
    aliases);

  g_free (aliases);
  return;
}

static gboolean
salut_connection_aliasing_fill_contact_attributes (SalutConnection *self,
    const gchar *dbus_interface,
    TpHandle handle,
    TpContactAttributeMap *attributes)
{
  if (!tp_strdiff (dbus_interface, TP_IFACE_CONNECTION_INTERFACE_ALIASING1))
    {
      GValue *val = tp_g_value_slice_new (G_TYPE_STRING);

      g_value_set_string (val, salut_connection_get_alias (self, handle));

      tp_contact_attribute_map_take_sliced_gvalue (attributes, handle,
         TP_TOKEN_CONNECTION_INTERFACE_ALIASING1_ALIAS, val);
      return TRUE;
    }

  return FALSE;
}

/**
 * salut_connection_get_handle_contact_capabilities
 *
 * Add capabilities of handle to the given GPtrArray
 */
static void
salut_connection_get_handle_contact_capabilities (SalutConnection *self,
  TpHandle handle, GPtrArray *arr)
{
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (self);
  TpChannelManagerIter iter;
  TpChannelManager *manager;
  const GabbleCapabilitySet *set;
  SalutContact *contact = NULL;

  if (handle == tp_base_connection_get_self_handle (base_conn))
    {
      if (self->priv->self == NULL)
        return;

      set = salut_self_get_caps (self->priv->self);
    }
  else
    {
      contact = salut_contact_manager_get_contact (
          self->priv->contact_manager, handle);

      if (contact == NULL)
        return;

      set = contact->caps;
    }

  tp_base_connection_channel_manager_iter_init (&iter, base_conn);

  while (tp_base_connection_channel_manager_iter_next (&iter, &manager))
    {
      /* all channel managers must implement the capability interface */
      g_assert (GABBLE_IS_CAPS_CHANNEL_MANAGER (manager));

      gabble_caps_channel_manager_get_contact_capabilities (
          GABBLE_CAPS_CHANNEL_MANAGER (manager), handle, set, arr);
    }

  tp_clear_object (&contact);
}

static gboolean
conn_contact_capabilities_fill_contact_attributes (SalutConnection *self,
    const gchar *dbus_interface,
    TpHandle handle,
    TpContactAttributeMap *attributes)
{
  if (!tp_strdiff (dbus_interface,
        TP_IFACE_CONNECTION_INTERFACE_CONTACT_CAPABILITIES1))
    {
      GPtrArray *array = g_ptr_array_new ();

      salut_connection_get_handle_contact_capabilities (self, handle, array);

      if (array->len > 0)
        {
          GValue *val =  tp_g_value_slice_new (
            TP_ARRAY_TYPE_REQUESTABLE_CHANNEL_CLASS_LIST);

          g_value_take_boxed (val, array);
          tp_contact_attribute_map_take_sliced_gvalue (attributes,
              handle,
              TP_TOKEN_CONNECTION_INTERFACE_CONTACT_CAPABILITIES1_CAPABILITIES,
              val);
        }
      else
        {
          g_ptr_array_unref (array);
        }

      return TRUE;
    }

  return FALSE;
}

static void
salut_connection_set_aliases (TpSvcConnectionInterfaceAliasing1 *iface,
    GHashTable *aliases, GDBusMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandle self_handle = tp_base_connection_get_self_handle (base);
  SalutConnectionPrivate *priv = self->priv;
  GError *error = NULL;
  const gchar *alias = g_hash_table_lookup (aliases,
      GUINT_TO_POINTER (self_handle));

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (alias == NULL || g_hash_table_size (aliases) != 1)
    {
      GError e = { TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
                   "In Salut you can only set your own alias" };

      g_dbus_method_invocation_return_gerror (context, &e);
      return;
    }

  DEBUG("Setting my alias to: %s", alias);

  if (!salut_self_set_alias (priv->self, alias, &error))
    {
      g_dbus_method_invocation_return_gerror (context, error);
      g_error_free (error);
      return;
    }
  tp_svc_connection_interface_aliasing1_return_from_set_aliases (context);
}

static void
_contact_manager_contact_alias_changed  (SalutConnection *self,
    SalutContact *contact, TpHandle handle)
{
  GHashTable *aliases;

  DEBUG("Emitting AliasesChanged");

  aliases = g_hash_table_new (NULL, NULL);
  g_hash_table_insert (aliases,
      GUINT_TO_POINTER (handle),
      (gchar *) salut_contact_get_alias (contact));

  tp_svc_connection_interface_aliasing1_emit_aliases_changed (self, aliases);

  g_hash_table_unref (aliases);
}

static void
salut_connection_aliasing_service_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  TpSvcConnectionInterfaceAliasing1Class *klass = g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_aliasing1_implement_##x \
    (klass, salut_connection_##x)
  IMPLEMENT (request_aliases);
  IMPLEMENT (set_aliases);
#undef IMPLEMENT
}

/* Avatar service implementation */
static void
_contact_manager_contact_avatar_changed (SalutConnection *self,
    SalutContact *contact, TpHandle handle)
{
  tp_svc_connection_interface_avatars1_emit_avatar_updated (self,
      (guint)handle, contact->avatar_token);
}

static void
salut_connection_clear_avatar (TpSvcConnectionInterfaceAvatars1 *iface,
    GDBusMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = self->priv;
  GError *error = NULL;
  TpBaseConnection *base = (TpBaseConnection *) self;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!salut_self_set_avatar (priv->self, NULL, 0, &error))
    {
      g_dbus_method_invocation_return_gerror (context, error);
      g_error_free (error);
      return;
    }
  tp_svc_connection_interface_avatars1_return_from_clear_avatar (context);
}

static void
salut_connection_set_avatar (TpSvcConnectionInterfaceAvatars1 *iface,
    const GArray *avatar, const gchar *mime_type,
    GDBusMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = self->priv;
  GError *error = NULL;
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandle self_handle = tp_base_connection_get_self_handle (base);

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!salut_self_set_avatar (priv->self, (guint8 *) avatar->data,
        avatar->len, &error))
    {
      g_dbus_method_invocation_return_gerror (context, error);
      g_error_free (error);
      return;
    }

  tp_svc_connection_interface_avatars1_emit_avatar_updated (self,
      self_handle, priv->self->avatar_token);
  tp_svc_connection_interface_avatars1_return_from_set_avatar (context,
      priv->self->avatar_token);
}


static void
salut_connection_get_known_avatar_tokens (
    TpSvcConnectionInterfaceAvatars1 *iface,
    const GArray *contacts,
    GDBusMethodInvocation *context)
{
  guint i;
  GHashTable *ret;
  GError *err = NULL;
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  TpHandle self_handle = tp_base_connection_get_self_handle (base);
  TpHandleRepoIface *handle_repo;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  handle_repo = tp_base_connection_get_handles (base,
      TP_ENTITY_TYPE_CONTACT);

  if (!tp_handles_are_valid (handle_repo, contacts, FALSE, &err))
    {
      g_dbus_method_invocation_return_gerror (context, err);
      g_error_free (err);
      return;
    }

  ret = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);

  for (i = 0; i < contacts->len ; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      gchar *tokens = NULL;

      if (self_handle == handle)
        {
          tokens = g_strdup (priv->self->avatar_token);
        }
      else
        {
          SalutContact *contact;
          contact =
             salut_contact_manager_get_contact (priv->contact_manager, handle);
          if (contact != NULL)
            {
              if (contact->avatar_token != NULL)
                tokens  = g_strdup (contact->avatar_token);
              else
                /* We always know the tokens, if it's unset then it's "" */
                tokens = g_strdup ("");
              g_object_unref (contact);
            }
        }

      if (tokens != NULL)
        g_hash_table_insert (ret, GUINT_TO_POINTER (handle), tokens);
    }

  tp_svc_connection_interface_avatars1_return_from_get_known_avatar_tokens (
     context, ret);

  g_hash_table_unref (ret);
}

static gboolean
salut_connection_avatars_fill_contact_attributes (SalutConnection *self,
    const gchar *dbus_interface,
    TpHandle handle,
    TpContactAttributeMap *attributes)
{
  if (!tp_strdiff (dbus_interface, TP_IFACE_CONNECTION_INTERFACE_AVATARS1))
    {
      TpBaseConnection *base = TP_BASE_CONNECTION (self);
      TpHandle self_handle = tp_base_connection_get_self_handle (base);
      SalutConnectionPrivate *priv = self->priv;
      gchar *token = NULL;

      if (self_handle == handle)
        {
          token = g_strdup (priv->self->avatar_token);
        }
      else
        {
          SalutContact *contact = salut_contact_manager_get_contact (
              priv->contact_manager, handle);
          if (contact != NULL)
            {
              if (contact->avatar_token != NULL)
                token = g_strdup (contact->avatar_token);
              else
                /* We always know the tokens, if it's unset then it's "" */
                token = g_strdup ("");

              g_object_unref (contact);
            }
        }

      if (token != NULL)
        {
          GValue *val = tp_g_value_slice_new (G_TYPE_STRING);

          g_value_take_string (val, token);

          tp_contact_attribute_map_take_sliced_gvalue (attributes, handle,
            TP_TOKEN_CONNECTION_INTERFACE_AVATARS1_TOKEN, val);
        }

      return TRUE;
    }

  return FALSE;
}


static void
_request_avatars_cb (SalutContact *contact, guint8 *avatar, gsize size,
    gpointer user_data)
{
  GArray *arr;

  if (avatar == NULL)
    return;

  arr = g_array_sized_new (FALSE, FALSE, sizeof (guint8), size);
  arr = g_array_append_vals (arr, avatar, size);

  tp_svc_connection_interface_avatars1_emit_avatar_retrieved (
    (GObject *) user_data, contact->handle,
    contact->avatar_token, arr, "");

  g_array_unref (arr);
}

static void
salut_connection_request_avatars (
    TpSvcConnectionInterfaceAvatars1 *iface,
    const GArray *contacts,
    GDBusMethodInvocation *context)
{
  guint i;
  GError *err = NULL;
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  TpHandle self_handle = tp_base_connection_get_self_handle (base);
  TpHandleRepoIface *handle_repo;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  handle_repo = tp_base_connection_get_handles (base,
      TP_ENTITY_TYPE_CONTACT);

  if (!tp_handles_are_valid (handle_repo, contacts, FALSE, &err))
    {
      g_dbus_method_invocation_return_gerror (context, err);
      g_error_free (err);
      return;
    }

  for (i = 0; i < contacts->len ; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);

      if (self_handle == handle)
        {
           GArray *arr;

           if (priv->self->avatar != NULL)
             {
               arr = g_array_sized_new (FALSE, FALSE, sizeof (guint8),
                 priv->self->avatar_size);
               arr = g_array_append_vals (arr, priv->self->avatar,
                 priv->self->avatar_size);

               tp_svc_connection_interface_avatars1_emit_avatar_retrieved (
                  (GObject *) self, self_handle,
                    priv->self->avatar_token, arr, "");
               g_array_unref (arr);
             }
        }
      else
        {
          SalutContact *contact;
          contact =
             salut_contact_manager_get_contact (priv->contact_manager, handle);
          if (contact != NULL)
            {
              salut_contact_get_avatar (contact, _request_avatars_cb, self);
              g_object_unref (contact);
            }
        }
    }

  tp_svc_connection_interface_avatars1_return_from_request_avatars (context);
}

static void
conn_avatars_properties_getter (GObject *object,
                                GQuark interface,
                                GQuark name,
                                GValue *value,
                                gpointer getter_data)
{
  GQuark q_mime_types = g_quark_from_static_string (
      "SupportedAvatarMIMETypes");

  if (name == q_mime_types)
    {
      g_value_set_static_boxed (value, mimetypes);
    }
  else
    {
      g_value_set_uint (value, GPOINTER_TO_UINT (getter_data));
    }
}

static void
salut_connection_avatar_service_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  TpSvcConnectionInterfaceAvatars1Class *klass = g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_avatars1_implement_##x \
    (klass, salut_connection_##x)
  IMPLEMENT (get_known_avatar_tokens);
  IMPLEMENT (request_avatars);
  IMPLEMENT (set_avatar);
  IMPLEMENT (clear_avatar);
#undef IMPLEMENT
}

static void
salut_free_enhanced_contact_capabilities (GPtrArray *caps)
{
  guint i;

  for (i = 0; i < caps->len; i++)
    {
      GValue monster = {0, };

      g_value_init (&monster, TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS);
      g_value_take_boxed (&monster, g_ptr_array_index (caps, i));
      g_value_unset (&monster);
    }

  g_ptr_array_unref (caps);
}

static void
_emit_contact_capabilities_changed (SalutConnection *conn,
                                    TpHandle handle)
{
  GPtrArray *ret = g_ptr_array_new ();
  GHashTable *caps = g_hash_table_new (g_direct_hash, g_direct_equal);

  salut_connection_get_handle_contact_capabilities (conn, handle, ret);
  g_hash_table_insert (caps, GUINT_TO_POINTER (handle), ret);

  tp_svc_connection_interface_contact_capabilities1_emit_contact_capabilities_changed (
      conn, caps);

  salut_free_enhanced_contact_capabilities (ret);
  g_hash_table_unref (caps);
}

static void
connection_capabilities_update_cb (SalutPresenceCache *cache,
                                   TpHandle handle,
                                   gpointer user_data)
{
  SalutConnection *conn = SALUT_CONNECTION (user_data);

  g_assert (SALUT_IS_CONNECTION (user_data));

  _emit_contact_capabilities_changed (conn, handle);
}

static gboolean
data_forms_equal (GPtrArray *one,
    GPtrArray *two)
{
  guint i;

  /* These data form lists come from the channel managers returning
   * from represent_client so they'll be created new every time
   * represent_client is called. As a result, we can't just look at
   * the object pointers, like how we can in the presence cache. */

  if (one->len != two->len)
    return FALSE;

  for (i = 0; i < one->len; i++)
    {
      WockyDataForm *form = g_ptr_array_index (one, i);
      WockyDataFormField *type_field;
      const gchar *type;
      guint j;
      gboolean found = FALSE;

      type_field = g_hash_table_lookup (form->fields, "FORM_TYPE");
      type = g_value_get_string (type_field->default_value);

      for (j = 0; j < two->len; j++)
        {
          WockyDataForm *two_form = g_ptr_array_index (two, i);
          WockyDataFormField *two_type;

          two_type = g_hash_table_lookup (two_form->fields,
              "FORM_TYPE");

          if (!tp_strdiff (g_value_get_string (two_type->default_value), type))
            {
              found = TRUE;
              break;
            }
        }

      if (!found)
        return FALSE;
    }

  return TRUE;
}

/**
 * salut_connection_update_capabilities
 *
 * Implements D-Bus method UpdateCapabilities
 * on interface
 * im.telepathy.v1.Connection.Interface.ContactCapabilities
 */
static void
salut_connection_update_capabilities (
    TpSvcConnectionInterfaceContactCapabilities1 *iface,
    const GPtrArray *clients,
    GDBusMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  SalutConnectionPrivate *priv = self->priv;
  GabbleCapabilitySet *before = NULL, *after;
  GPtrArray *before_forms = NULL, *after_forms;
  TpChannelManagerIter iter;
  TpChannelManager *manager;
  guint i;
  GError *error = NULL;

  /* these are the caps we were advertising before UpdateCapabilities
   * was called. we'll only have created the salut self once we've
   * connected */
  if (priv->self != NULL)
    {
      before = gabble_capability_set_copy (salut_self_get_caps (priv->self));
      before_forms = g_ptr_array_ref (
          (GPtrArray *) wocky_xep_0115_capabilities_get_data_forms (
              WOCKY_XEP_0115_CAPABILITIES (priv->self)));
    }

  tp_base_connection_channel_manager_iter_init (&iter, base);

  while (tp_base_connection_channel_manager_iter_next (&iter, &manager))
    {
      /* all channel managers must implement the capability interface */
      g_assert (GABBLE_IS_CAPS_CHANNEL_MANAGER (manager));

      gabble_caps_channel_manager_reset_capabilities (
          GABBLE_CAPS_CHANNEL_MANAGER (manager));
    }

  DEBUG ("enter");

  /* we're going to reset our self caps to the bare caps that we
   * advertise and then add to it after iterating the clients.  */
  after = salut_dup_self_advertised_caps ();
  after_forms = g_ptr_array_new ();

  for (i = 0; i < clients->len; i++)
    {
      GValueArray *va = g_ptr_array_index (clients, i);
      const gchar *client_name = g_value_get_string (va->values + 0);
      const GPtrArray *filters = g_value_get_boxed (va->values + 1);
      const gchar * const * cap_tokens = g_value_get_boxed (va->values + 2);

      /* We pass the client through to the caps channel managers
       * because it allows them to update their view on which clients
       * are still around. */

      tp_base_connection_channel_manager_iter_init (&iter, base);

      while (tp_base_connection_channel_manager_iter_next (&iter, &manager))
        {
          /* all channel managers must implement the capability interface */
          g_assert (GABBLE_IS_CAPS_CHANNEL_MANAGER (manager));

          gabble_caps_channel_manager_represent_client (
              GABBLE_CAPS_CHANNEL_MANAGER (manager), client_name, filters,
              cap_tokens, after, after_forms);
        }
    }

  if (priv->self != NULL)
    {
      /* we've connected and have a SalutSelf, so give the caps to it
       * right now */
      salut_self_take_caps (priv->self, after);
      salut_self_take_data_forms (priv->self, after_forms);
    }
  else
    {
      if (priv->pre_connect_caps != NULL)
        gabble_capability_set_free (priv->pre_connect_caps);
      if (priv->pre_connect_data_forms != NULL)
        g_ptr_array_unref (priv->pre_connect_data_forms);

      priv->pre_connect_caps = after;
      priv->pre_connect_data_forms = after_forms;
    }

  if ((before != NULL && !gabble_capability_set_equals (before, after))
      || (before_forms != NULL && !data_forms_equal (before_forms, after_forms)))
    {
      TpHandle self_handle = tp_base_connection_get_self_handle (base);

      if (DEBUGGING)
        {
          gchar *dump = gabble_capability_set_dump (after, "  ");
          DEBUG ("updated caps:\n%s", dump);
          g_free (dump);
        }

      if (!announce_self_caps (self, &error))
        {
          gabble_capability_set_free (before);
          g_dbus_method_invocation_return_gerror (context, error);
          g_error_free (error);
          return;
        }

      _emit_contact_capabilities_changed (self, self_handle);
    }

  /* after now belongs to SalutSelf, or priv->pre_connect_caps */
  if (before != NULL)
    gabble_capability_set_free (before);

  if (before_forms != NULL)
    g_ptr_array_unref (before_forms);

  tp_svc_connection_interface_contact_capabilities1_return_from_update_capabilities (
      context);
}

static void
salut_conn_contact_caps_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionInterfaceContactCapabilities1Class *klass = g_iface;

#define IMPLEMENT(x) \
    tp_svc_connection_interface_contact_capabilities1_implement_##x (\
    klass, salut_connection_##x)
  IMPLEMENT(update_capabilities);
#undef IMPLEMENT
}

gchar *
salut_normalize_non_empty (const gchar *id,
    GError **error)
{
  g_return_val_if_fail (id != NULL, NULL);

  if (*id == '\0')
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_HANDLE,
          "Salut handle names may not be the empty string");
      return NULL;
    }

  return g_strdup (id);
}

static gchar *
handle_normalize_require_nonempty (TpHandleRepoIface *repo G_GNUC_UNUSED,
    const gchar *id,
    gpointer context G_GNUC_UNUSED,
    GError **error)
{
  return salut_normalize_non_empty (id, error);
}

/* Connection baseclass function implementations */
static void
salut_connection_create_handle_repos (TpBaseConnection *self,
    TpHandleRepoIface *repos[TP_NUM_ENTITY_TYPES])
{
  repos[TP_ENTITY_TYPE_CONTACT] = tp_dynamic_handle_repo_new
      (TP_ENTITY_TYPE_CONTACT, handle_normalize_require_nonempty, NULL);

  repos[TP_ENTITY_TYPE_ROOM] = tp_dynamic_handle_repo_new
      (TP_ENTITY_TYPE_ROOM, handle_normalize_require_nonempty, NULL);
}

static void
_contact_manager_contact_change_cb (SalutContactManager *mgr,
    SalutContact *contact, int changes, gpointer data)
{
  SalutConnection *self = SALUT_CONNECTION(data);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (
      TP_BASE_CONNECTION(self), TP_ENTITY_TYPE_CONTACT);
  TpHandle handle;

  handle = tp_handle_lookup (handle_repo, contact->name, NULL, NULL);

  if (changes & SALUT_CONTACT_ALIAS_CHANGED)
    {
      _contact_manager_contact_alias_changed (self, contact, handle);
    }

  if (changes & SALUT_CONTACT_STATUS_CHANGED)
    {
      _contact_manager_contact_status_changed (self, contact, handle);
    }

  if (changes & SALUT_CONTACT_AVATAR_CHANGED)
    {
      _contact_manager_contact_avatar_changed (self, contact, handle);
    }

  if (changes & ( SALUT_CONTACT_REAL_NAME_CHANGED
                | SALUT_CONTACT_EMAIL_CHANGED
                | SALUT_CONTACT_JID_CHANGED
                ))
    {
      salut_conn_contact_info_changed (self, contact, handle);
    }
}

static void
add_to_array (gpointer data,
    gpointer user_data)
{
  g_ptr_array_add (user_data, data);
}

static GPtrArray *
salut_connection_create_channel_managers (TpBaseConnection *base)
{
  SalutConnection *self = SALUT_CONNECTION (base);
  SalutPluginConnection *plugin_connection = SALUT_PLUGIN_CONNECTION (self);
  SalutConnectionPrivate *priv = self->priv;
  GPtrArray *managers = g_ptr_array_sized_new (1);
  GPtrArray *tmp;
  SalutPluginLoader *loader;

  /* Create the contact manager. This is not a channel manager anymore,
   * but still needs to be created from here because others channel managers use
   * it and TpBaseConnection calls ::create_channel_managers() before
   * ::constructed() */
  self->priv->contact_manager = salut_discovery_client_create_contact_manager (
      self->priv->discovery_client, self);
  g_signal_connect (self->priv->contact_manager, "contact-change",
      G_CALLBACK (_contact_manager_contact_change_cb), self);

  priv->im_manager = salut_im_manager_new (self, priv->contact_manager);
  priv->ft_manager = salut_ft_manager_new (self, priv->contact_manager);

#ifndef USE_BACKEND_BONJOUR
  priv->muc_manager = salut_discovery_client_create_muc_manager (
      priv->discovery_client, self);

  priv->roomlist_manager = salut_discovery_client_create_roomlist_manager (
      priv->discovery_client, self);
#endif

#if 0
  priv->tubes_manager = salut_tubes_manager_new (self, priv->contact_manager);
#endif

  g_ptr_array_add (managers, priv->im_manager);
  g_ptr_array_add (managers, priv->ft_manager);
#ifndef USE_BACKEND_BONJOUR
  g_ptr_array_add (managers, priv->muc_manager);
  g_ptr_array_add (managers, priv->roomlist_manager);
#endif

#if 0
  g_ptr_array_add (managers, priv->tubes_manager);
#endif

  /* plugin channel managers */
  loader = salut_plugin_loader_dup ();
  tmp = salut_plugin_loader_create_channel_managers (loader, plugin_connection);
  g_object_unref (loader);

  g_ptr_array_foreach (tmp, add_to_array, managers);
  g_ptr_array_unref (tmp);

  return managers;
}


static gchar *
salut_connection_get_unique_connection_name (TpBaseConnection *base)
{
  SalutConnection *self = SALUT_CONNECTION (base);
  SalutConnectionPrivate *priv = self->priv;

  return g_strdup (priv->published_name);
}

static void
force_close_cb (GObject *source,
    GAsyncResult *res,
    gpointer user_data)
{
  SalutConnection *self = SALUT_CONNECTION (user_data);
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  GError *error = NULL;

  if (!wocky_porter_force_close_finish (WOCKY_PORTER (source),
          res, &error))
    {
      DEBUG ("force close failed: %s", error->message);
      g_error_free (error);
    }
  else
    {
      DEBUG ("connection properly closed (forced)");
    }

  tp_base_connection_finish_shutdown (base);

  g_object_unref (self);
}

static void
closed_cb (GObject *source,
    GAsyncResult *res,
    gpointer user_data)
{
  SalutConnection *self = SALUT_CONNECTION (user_data);
  SalutConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  GError *error = NULL;
  gboolean force_called = priv->disconnect_timer == 0;

  if (priv->disconnect_timer != 0)
    {
      /* stop the timer */
      g_source_remove (priv->disconnect_timer);
      priv->disconnect_timer = 0;
    }

  if (!wocky_porter_close_finish (WOCKY_PORTER (source), res, &error))
    {
      DEBUG ("close failed: %s", error->message);

      if (g_error_matches (error, WOCKY_PORTER_ERROR,
            WOCKY_PORTER_ERROR_FORCIBLY_CLOSED))
        {
          /* Close operation has been aborted because a force_close operation
           * has been started. tp_base_connection_finish_shutdown will be
           * called once this force_close operation is completed so we don't
           * do it here. */

          g_error_free (error);
          goto out;
        }

      g_error_free (error);
    }
  else
    {
      DEBUG ("connection properly closed");
    }

  if (!force_called)
    tp_base_connection_finish_shutdown (base);

out:
  g_object_unref (self);
}

static gboolean
disconnect_timeout_cb (gpointer data)
{
  SalutConnection *self = SALUT_CONNECTION (data);
  SalutConnectionPrivate *priv = self->priv;

  DEBUG ("Close operation timed out. Force closing");
  priv->disconnect_timer = 0;

  wocky_porter_force_close_async (self->porter, NULL, force_close_cb, g_object_ref (self));
  return FALSE;
}

static void
salut_connection_shut_down (TpBaseConnection *base)
{
  SalutConnection *self = SALUT_CONNECTION (base);
  SalutConnectionPrivate *priv = self->priv;

  _salut_connection_disconnect (self);

  if (self->session != NULL)
    {
      DEBUG ("connection may still be open; closing it: %p", self);

      g_assert (priv->disconnect_timer == 0);
      priv->disconnect_timer = g_timeout_add_seconds (DISCONNECT_TIMEOUT,
          disconnect_timeout_cb, self);

      wocky_porter_close_async (self->porter, NULL, closed_cb, g_object_ref (self));
      return;
    }

  DEBUG ("session is not alive; clean up the base connection");
  tp_base_connection_finish_shutdown (base);
}

static gboolean
salut_connection_start_connecting (TpBaseConnection *base, GError **error)
{
  SalutConnection *self = SALUT_CONNECTION (base);
  SalutConnectionPrivate *priv = self->priv;
  GError *client_error = NULL;

  g_signal_connect (priv->discovery_client, "state-changed",
      G_CALLBACK (_discovery_client_state_changed_cb), self);

  if (!salut_discovery_client_start (priv->discovery_client, &client_error))
    {
      *error = g_error_new (TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "Unable to initialize the avahi client: %s",
          client_error->message);
      DEBUG ("%s", (*error)->message);
      g_error_free (client_error);
      goto error;
    }

  return TRUE;

error:
  tp_base_connection_change_status (
        TP_BASE_CONNECTION (base),
        TP_CONNECTION_STATUS_DISCONNECTED,
        TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
  return FALSE;
}

/* sidecar stuff */
static gchar *
make_sidecar_path (
    SalutConnection *conn,
    const gchar *sidecar_iface)
{
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);

  return g_strdelimit (
      g_strdup_printf ("%s/Sidecar/%s",
        tp_base_connection_get_object_path (base_conn), sidecar_iface),
      ".", '/');
}

static gchar *
connection_install_sidecar (
    SalutConnection *conn,
    SalutSidecar *sidecar,
    const gchar *sidecar_iface)
{
  SalutConnectionPrivate *priv = conn->priv;
  GDBusConnection *bus = tp_base_connection_get_dbus_connection (
      (TpBaseConnection *) conn);
  gchar *path = make_sidecar_path (conn, sidecar_iface);

  tp_dbus_connection_register_object (bus, path, G_OBJECT (sidecar));
  g_hash_table_insert (priv->sidecars, g_strdup (sidecar_iface),
      g_object_ref (sidecar));

  return path;
}

typedef struct {
    SalutConnection *conn;
    gchar *sidecar_iface;
} Grr;

static Grr *
grr_new (
    SalutConnection *conn,
    const gchar *sidecar_iface)
{
  Grr *grr = g_slice_new (Grr);

  grr->conn = g_object_ref (conn);
  grr->sidecar_iface = g_strdup (sidecar_iface);

  return grr;
}

static void
grr_free (Grr *grr)
{
  g_object_unref (grr->conn);
  g_free (grr->sidecar_iface);

  g_slice_free (Grr, grr);
}

static void
create_sidecar_cb (
    GObject *loader_obj,
    GAsyncResult *result,
    gpointer user_data)
{
  SalutPluginLoader *loader = SALUT_PLUGIN_LOADER (loader_obj);
  Grr *ctx = user_data;
  SalutConnection *conn = ctx->conn;
  SalutConnectionPrivate *priv = conn->priv;
  const gchar *sidecar_iface = ctx->sidecar_iface;
  SalutSidecar *sidecar;
  GList *contexts;
  GError *error = NULL;

  sidecar = salut_plugin_loader_create_sidecar_finish (loader, result, &error);
  contexts = g_hash_table_lookup (priv->pending_sidecars, sidecar_iface);

  if (contexts == NULL)
    {
      /* We never use the empty list as a value in pending_sidecars, so this
       * must mean we've disconnected and already returned. Jettison the
       * sidecar!
       */
      DEBUG ("creating sidecar %s %s after connection closed; jettisoning!",
          sidecar_iface, (sidecar != NULL ? "succeeded" : "failed"));
      goto out;
    }

  if (sidecar != NULL)
    {
      const gchar *actual_iface = salut_sidecar_get_interface (sidecar);

      if (tp_strdiff (ctx->sidecar_iface, actual_iface))
        {
          /* TODO: maybe this lives in the loader? It knows what the plugin is
           * called. */
          g_set_error (&error, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
              "A buggy plugin created a %s sidecar when asked to create %s",
              actual_iface, ctx->sidecar_iface);
        }
    }
  else /* sidecar == NULL */
    {
      /* If creating the sidecar failed, 'error' should have been set */
      g_return_if_fail (error != NULL);
    }

  if (error == NULL)
    {
      gchar *path = connection_install_sidecar (ctx->conn, sidecar,
          ctx->sidecar_iface);
      GHashTable *props = salut_sidecar_get_immutable_properties (sidecar);
      GList *l;

      for (l = contexts; l != NULL; l = l->next)
        tp_svc_connection_interface_sidecars1_return_from_ensure_sidecar (l->data,
            path, props);

      g_hash_table_unref (props);
      g_free (path);
    }
  else
    {
      g_list_foreach (contexts, (GFunc) g_dbus_method_invocation_return_gerror, error);
    }

  g_hash_table_remove (ctx->conn->priv->pending_sidecars, ctx->sidecar_iface);

out:
  tp_clear_object (&sidecar);
  g_clear_error (&error);

  grr_free (ctx);
}

static void
salut_connection_ensure_sidecar (
    TpSvcConnectionInterfaceSidecars1 *iface,
    const gchar *sidecar_iface,
    GDBusMethodInvocation *context)
{
  SalutConnection *conn = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = conn->priv;
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
  SalutSidecar *sidecar;
  gpointer key, value;
  GError *error = NULL;

  if (tp_base_connection_is_destroyed (base_conn))
    {
      GError e = { TP_ERROR, TP_ERROR_DISCONNECTED,
          "This connection has already disconnected" };

      DEBUG ("already disconnected, declining request for %s", sidecar_iface);
      g_dbus_method_invocation_return_gerror (context, &e);
      return;
    }

  if (!tp_dbus_check_valid_interface_name (sidecar_iface, &error))
    {
      error->domain = TP_ERROR;
      error->code = TP_ERROR_INVALID_ARGUMENT;
      DEBUG ("%s is malformed: %s", sidecar_iface, error->message);
      g_dbus_method_invocation_return_gerror (context, error);
      g_clear_error (&error);
      return;
    }

  sidecar = g_hash_table_lookup (priv->sidecars, sidecar_iface);

  if (sidecar != NULL)
    {
      gchar *path = make_sidecar_path (conn, sidecar_iface);
      GHashTable *props = salut_sidecar_get_immutable_properties (sidecar);

      DEBUG ("sidecar %s already exists at %s", sidecar_iface, path);
      tp_svc_connection_interface_sidecars1_return_from_ensure_sidecar (context, path,
          props);

      g_free (path);
      g_hash_table_unref (props);
      return;
    }

  if (g_hash_table_lookup_extended (priv->pending_sidecars, sidecar_iface,
          &key, &value))
    {
      GList *contexts = value;

      DEBUG ("already awaiting %s, joining a queue of %u", sidecar_iface,
          g_list_length (contexts));

      contexts = g_list_prepend (contexts, context);
      g_hash_table_steal (priv->pending_sidecars, key);
      g_hash_table_insert (priv->pending_sidecars, key, contexts);
      return;
    }

  DEBUG ("enqueuing first request for %s", sidecar_iface);
  g_hash_table_insert (priv->pending_sidecars, g_strdup (sidecar_iface),
      g_list_prepend (NULL, context));

  if (tp_base_connection_get_status (base_conn) ==
      TP_CONNECTION_STATUS_CONNECTED)
    {
      SalutPluginLoader *loader = salut_plugin_loader_dup ();

      DEBUG ("requesting %s from the plugin loader", sidecar_iface);
      salut_plugin_loader_create_sidecar_async (loader, sidecar_iface, conn,
          conn->session, create_sidecar_cb, grr_new (conn, sidecar_iface));
      g_object_unref (loader);
    }
  else
    {
      DEBUG ("not yet connected; waiting.");
    }
}

static void
sidecars_conn_status_changed_cb (
    SalutConnection *conn,
    guint status,
    guint reason,
    gpointer unused)
{
  SalutConnectionPrivate *priv = conn->priv;
  GDBusConnection *bus = tp_base_connection_get_dbus_connection (
      (TpBaseConnection *) conn);
  GHashTableIter iter;
  gpointer key, value;

  if (status == TP_CONNECTION_STATUS_DISCONNECTED)
    {
      g_hash_table_iter_init (&iter, priv->sidecars);

      while (g_hash_table_iter_next (&iter, NULL, &value))
        {
          DEBUG ("removing %s from the bus", salut_sidecar_get_interface (value));
          tp_dbus_connection_unregister_object (bus, G_OBJECT (value));
        }

      g_hash_table_iter_init (&iter, priv->pending_sidecars);

      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          const gchar *sidecar_iface = key;
          GList *contexts = value;
          GError *error = g_error_new (TP_ERROR, TP_ERROR_CANCELLED,
              "Disconnected before %s could be created", sidecar_iface);

          DEBUG ("failing all %u requests for %s", g_list_length (contexts),
              sidecar_iface);
          g_list_foreach (contexts, (GFunc) g_dbus_method_invocation_return_gerror, error);
          g_error_free (error);
        }

      g_hash_table_remove_all (priv->sidecars);
      g_hash_table_remove_all (priv->pending_sidecars);
    }
  else if (status == TP_CONNECTION_STATUS_CONNECTED)
    {
      SalutPluginLoader *loader = salut_plugin_loader_dup ();

      DEBUG ("connected; requesting sidecars from plugins");
      g_hash_table_iter_init (&iter, priv->pending_sidecars);

      while (g_hash_table_iter_next (&iter, &key, NULL))
        {
          const gchar *sidecar_iface = key;

          DEBUG ("requesting %s from the plugin loader", sidecar_iface);
          salut_plugin_loader_create_sidecar_async (loader, sidecar_iface, conn,
              conn->session, create_sidecar_cb, grr_new (conn, sidecar_iface));
        }

      g_object_unref (loader);
    }
}

static void
salut_conn_sidecars_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  TpSvcConnectionInterfaceSidecars1Class *klass = g_iface;

#define IMPLEMENT(x) \
    tp_svc_connection_interface_sidecars1_implement_##x (\
    klass, salut_connection_##x)
  IMPLEMENT(ensure_sidecar);
#undef IMPLEMENT
}

static void
salut_plugin_connection_iface_init (SalutPluginConnectionInterface *iface,
    gpointer iface_data)
{
  iface->get_session = salut_connection_get_session;
  iface->get_name = salut_connection_get_name;
}

WockySession *
salut_connection_get_session (SalutPluginConnection *plugin_connection)
{
  SalutConnection *connection = SALUT_CONNECTION (plugin_connection);

  g_return_val_if_fail (SALUT_IS_CONNECTION (connection), NULL);

  return connection->session;
}

const gchar *
salut_connection_get_name (SalutPluginConnection *plugin_connection)
{
  SalutConnection *connection = SALUT_CONNECTION (plugin_connection);

  g_return_val_if_fail (SALUT_IS_CONNECTION (connection), NULL);

  return connection->name;
}

void
salut_connection_dup_avatar_requirements (GStrv *supported_mime_types,
    guint *min_height,
    guint *min_width,
    guint *rec_height,
    guint *rec_width,
    guint *max_height,
    guint *max_width,
    guint *max_bytes)
{
  if (supported_mime_types != NULL)
    {
      *supported_mime_types = g_strdupv ((gchar **) mimetypes);
    }

  if (min_height != NULL)
    *min_height = AVATAR_MIN_PX;
  if (min_width != NULL)
    *min_width = AVATAR_MIN_PX;

  if (rec_height != NULL)
    *rec_height = AVATAR_REC_PX;
  if (rec_width != NULL)
    *rec_width = AVATAR_REC_PX;

  if (max_height != NULL)
    *max_height = AVATAR_MAX_PX;
  if (max_width != NULL)
    *max_width = AVATAR_MAX_PX;

  if (max_bytes != NULL)
    *max_bytes = AVATAR_MAX_BYTES;
}

const TpPresenceStatusSpec *
salut_connection_get_presence_statuses (void)
{
  return presence_statuses;
}

static void
salut_connection_fill_contact_attributes (TpBaseConnection *base,
    const gchar *dbus_interface,
    TpHandle handle,
    TpContactAttributeMap *attributes)
{
  SalutConnection *self = SALUT_CONNECTION (base);

  if (salut_connection_aliasing_fill_contact_attributes (self,
        dbus_interface, handle, attributes))
    return;

  if (salut_connection_avatars_fill_contact_attributes (self,
        dbus_interface, handle, attributes))
    return;

  if (conn_contact_capabilities_fill_contact_attributes (self,
        dbus_interface, handle, attributes))
    return;

  if (salut_conn_contact_info_fill_contact_attributes (self,
        dbus_interface, handle, attributes))
    return;

  if (tp_base_contact_list_fill_contact_attributes (
        TP_BASE_CONTACT_LIST (self->priv->contact_manager),
        dbus_interface, handle, attributes))
    return;

  if (tp_presence_mixin_fill_contact_attributes ((GObject *) self,
        dbus_interface, handle, attributes))
    return;

  TP_BASE_CONNECTION_CLASS (salut_connection_parent_class)->
    fill_contact_attributes (base, dbus_interface, handle, attributes);
}
