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

#include "salut-connection.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>

#include <dbus/dbus-protocol.h>
#include <dbus/dbus-glib-bindings.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/handle-repo-dynamic.h>
#include <telepathy-glib/handle-repo.h>
#include <telepathy-glib/handle-repo-static.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/util.h>

#include <gibber/gibber-namespaces.h>

#include <wocky/wocky-meta-porter.h>

#include "salut-avahi-discovery-client.h"
#include "salut-caps-channel-manager.h"
#include "salut-caps-hash.h"
#include "salut-contact-channel.h"
#include "salut-contact.h"
#include "salut-contact-manager.h"
#include "salut-disco.h"
#include "salut-discovery-client.h"
#include "salut-im-manager.h"
#include "salut-muc-manager.h"
#include "salut-ft-manager.h"
#include "salut-contact.h"
#include "salut-roomlist-manager.h"
#include "salut-presence.h"
#include "salut-presence-cache.h"
#include "salut-self.h"
#include "salut-si-bytestream-manager.h"
#include "salut-tubes-manager.h"
#include "salut-util.h"
#include "salut-xmpp-connection-manager.h"

#ifdef ENABLE_OLPC
#include "salut-olpc-activity-manager.h"
#endif

#include <extensions/extensions.h>

#define DEBUG_FLAG DEBUG_CONNECTION
#include "debug.h"

#define SALUT_TP_ALIAS_PAIR_TYPE (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID))

#ifdef ENABLE_OLPC

#define ACTIVITY_PAIR_TYPE \
  (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_UINT, \
      G_TYPE_INVALID))

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

static void
salut_conn_contact_caps_iface_init (gpointer, gpointer);

#define DISCONNECT_TIMEOUT 5

G_DEFINE_TYPE_WITH_CODE(SalutConnection,
    salut_connection,
    TP_TYPE_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_INTERFACE_ALIASING,
        salut_connection_aliasing_service_iface_init);
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_INTERFACE_PRESENCE,
       tp_presence_mixin_iface_init);
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_DBUS_PROPERTIES,
       tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACTS,
       tp_contacts_mixin_iface_init);
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_INTERFACE_SIMPLE_PRESENCE,
       tp_presence_mixin_simple_presence_iface_init);
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_INTERFACE_AVATARS,
       salut_connection_avatar_service_iface_init);
    G_IMPLEMENT_INTERFACE
      (SALUT_TYPE_SVC_CONNECTION_INTERFACE_CONTACT_CAPABILITIES,
      salut_conn_contact_caps_iface_init);
#ifdef ENABLE_OLPC
    G_IMPLEMENT_INTERFACE (SALUT_TYPE_SVC_OLPC_BUDDY_INFO,
       salut_connection_olpc_buddy_info_iface_init);
    G_IMPLEMENT_INTERFACE (SALUT_TYPE_SVC_OLPC_ACTIVITY_PROPERTIES,
       salut_connection_olpc_activity_properties_iface_init);
#endif
    )

#ifdef ENABLE_OLPC
static gboolean uninvite_stanza_filter (SalutXmppConnectionManager *mgr,
    GibberXmppConnection *conn, WockyStanza *stanza,
    SalutContact *contact, gpointer user_data);

static void uninvite_stanza_callback (SalutXmppConnectionManager *mgr,
    GibberXmppConnection *conn, WockyStanza *stanza,
    SalutContact *contact, gpointer user_data);
#endif

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
#ifdef ENABLE_OLPC
  PROP_OLPC_ACTIVITY_MANAGER,
#endif
  PROP_BACKEND,
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
#ifdef ENABLE_OLPC
  gchar *olpc_color;
  GArray *olpc_key;
#endif

  /* Discovery client for browsing and resolving */
  SalutDiscoveryClient *discovery_client;

  /* TpHandler for our presence on the lan */
  SalutSelf *self;
  gboolean self_established;
  SalutPresenceId pre_connect_presence;
  gchar *pre_connect_message;

  /* XMPP connection manager */
  SalutXmppConnectionManager *xmpp_connection_manager;

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

  /* Bytestream manager for stream initiation (XEP-0095) */
  SalutSiBytestreamManager *si_bytestream_manager;

#ifdef ENABLE_OLPC
  SalutOlpcActivityManager *olpc_activity_manager;
#endif

  /* timer used when trying to properly disconnect */
  guint disconnect_timer;

  /* Backend type: avahi or dummy */
  GType backend_type;
};

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

static GPtrArray *
salut_connection_create_channel_managers (TpBaseConnection *self);

static gchar *
salut_connection_get_unique_connection_name (TpBaseConnection *self);

static void
salut_connection_shut_down (TpBaseConnection *self);

static gboolean
salut_connection_start_connecting (TpBaseConnection *self, GError **error);

static void salut_connection_avatars_fill_contact_attributes (GObject *obj,
    const GArray *contacts, GHashTable *attributes_hash);

static void salut_connection_aliasing_fill_contact_attributes (GObject *obj,
    const GArray *contacts, GHashTable *attributes_hash);

static void conn_contact_capabilities_fill_contact_attributes (GObject *obj,
  const GArray *contacts, GHashTable *attributes_hash);

static void connection_capabilities_update_cb (SalutPresenceCache *cache,
    TpHandle handle, GHashTable *old_enhanced_caps,
    GHashTable *new_enhanced_caps, gpointer user_data);

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

  /* create this now so channel managers can use it when created from
   * parent->constructor */
  obj->session = wocky_session_new (NULL);
  obj->porter = wocky_session_get_porter (obj->session);

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

  priv->discovery_client = NULL;
  priv->self = NULL;
  priv->self_established = FALSE;

  priv->pre_connect_presence = SALUT_PRESENCE_AVAILABLE;
  priv->pre_connect_message = NULL;

  priv->contact_manager = NULL;
  priv->xmpp_connection_manager = NULL;
}

static GObject *
salut_connection_constructor (GType type,
                              guint n_props,
                              GObjectConstructParam *props)
{
  GObject *obj;
  SalutConnection *self;
  SalutConnectionPrivate *priv;

  obj = G_OBJECT_CLASS (salut_connection_parent_class)->
           constructor (type, n_props, props);
  self = SALUT_CONNECTION (obj);
  priv = self->priv;

  self->disco = salut_disco_new (self);
  self->presence_cache = salut_presence_cache_new (self);
  g_signal_connect (self->presence_cache, "capabilities-update", G_CALLBACK
      (connection_capabilities_update_cb), self);

  tp_contacts_mixin_init (obj,
      G_STRUCT_OFFSET (SalutConnection, contacts_mixin));

  tp_base_connection_register_with_contacts_mixin (TP_BASE_CONNECTION (obj));
  tp_presence_mixin_simple_presence_register_with_contacts_mixin (obj);

  tp_contacts_mixin_add_contact_attributes_iface (obj,
      TP_IFACE_CONNECTION_INTERFACE_AVATARS,
      salut_connection_avatars_fill_contact_attributes);

  tp_contacts_mixin_add_contact_attributes_iface (obj,
      TP_IFACE_CONNECTION_INTERFACE_ALIASING,
      salut_connection_aliasing_fill_contact_attributes);

  tp_contacts_mixin_add_contact_attributes_iface (G_OBJECT (self),
      SALUT_IFACE_CONNECTION_INTERFACE_CONTACT_CAPABILITIES,
          conn_contact_capabilities_fill_contact_attributes);

  return obj;
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
    case PROP_MUC_MANAGER:
      g_value_set_object (value, priv->muc_manager);
      break;
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
    case PROP_XCM:
      g_value_set_object (value, priv->xmpp_connection_manager);
      break;
    case PROP_SI_BYTESTREAM_MANAGER:
      g_value_set_object (value, priv->si_bytestream_manager);
      break;
#ifdef ENABLE_OLPC
    case PROP_OLPC_ACTIVITY_MANAGER:
      g_value_set_object (value, priv->olpc_activity_manager);
      break;
#endif
    case PROP_BACKEND:
      g_value_set_gtype (value, priv->backend_type);
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
          NULL);
      g_assert (priv->discovery_client != NULL);
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
      { "dnd", TP_CONNECTION_PRESENCE_TYPE_BUSY, TRUE, presence_args },
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
                              guint index_)
{
  return (index_ < SALUT_PRESENCE_OFFLINE);
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
  SalutConnectionPrivate *priv = self->priv;
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
      TpPresenceStatus ps = { priv->self->status,
          make_presence_opt_args (priv->self->status,
              priv->self->status_message) };

      tp_presence_mixin_emit_one_presence_update ((GObject *) self,
          base->self_handle, &ps);

      if (ps.optional_arguments != NULL)
        g_hash_table_destroy (ps.optional_arguments);
    }
}

static gboolean
set_own_status (GObject *obj,
                const TpPresenceStatus *status,
                GError **error)
{
  SalutConnection *self = SALUT_CONNECTION (obj);
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

  set_self_presence (self, presence, message, &err);

  if (err != NULL)
    {
      *error = g_error_new_literal (TP_ERRORS, TP_ERROR_NETWORK_ERROR,
          err->message);
    }

  return TRUE;
}

static const gchar *interfaces [] = {
  TP_IFACE_CONNECTION_INTERFACE_ALIASING,
  TP_IFACE_CONNECTION_INTERFACE_AVATARS,
  TP_IFACE_CONNECTION_INTERFACE_CONTACTS,
  TP_IFACE_CONNECTION_INTERFACE_PRESENCE,
  TP_IFACE_CONNECTION_INTERFACE_SIMPLE_PRESENCE,
  TP_IFACE_CONNECTION_INTERFACE_REQUESTS,
#ifdef ENABLE_OLPC
  SALUT_IFACE_OLPC_BUDDY_INFO,
  SALUT_IFACE_OLPC_ACTIVITY_PROPERTIES,
#endif
  NULL };

const gchar * const *
salut_connection_get_implemented_interfaces (void)
{
  return interfaces;
}

static void
salut_connection_class_init (SalutConnectionClass *salut_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_connection_class);
  TpBaseConnectionClass *tp_connection_class =
      TP_BASE_CONNECTION_CLASS(salut_connection_class);
  GParamSpec *param_spec;
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
        { TP_IFACE_CONNECTION_INTERFACE_AVATARS,
          conn_avatars_properties_getter,
          NULL,
          conn_avatars_properties,
        },
        { NULL }
  };

  object_class->get_property = salut_connection_get_property;
  object_class->set_property = salut_connection_set_property;

  g_type_class_add_private (salut_connection_class,
      sizeof (SalutConnectionPrivate));

  object_class->constructor = salut_connection_constructor;

  object_class->dispose = salut_connection_dispose;
  object_class->finalize = salut_connection_finalize;

  tp_connection_class->create_handle_repos =
      salut_connection_create_handle_repos;
  tp_connection_class->create_channel_factories =
      salut_connection_create_channel_factories;
  tp_connection_class->create_channel_managers =
      salut_connection_create_channel_managers;
  tp_connection_class->get_unique_connection_name =
      salut_connection_get_unique_connection_name;
  tp_connection_class->shut_down =
      salut_connection_shut_down;
  tp_connection_class->start_connecting =
      salut_connection_start_connecting;
  tp_connection_class->interfaces_always_present = interfaces;

  salut_connection_class->properties_mixin.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (SalutConnectionClass, properties_mixin));

  tp_presence_mixin_class_init (object_class,
      G_STRUCT_OFFSET (SalutConnectionClass, presence_mixin),
      is_presence_status_available, get_contact_statuses, set_own_status,
      presence_statuses);

  tp_presence_mixin_simple_presence_init_dbus_properties (object_class);

  tp_contacts_mixin_class_init (object_class,
        G_STRUCT_OFFSET (SalutConnectionClass, contacts_mixin));

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
      "xmpp-connection-manager",
      "SalutXmppConnectionManager object",
      "The Salut XMPP Connection Manager associated with this Salut "
      "Connection",
      SALUT_TYPE_XMPP_CONNECTION_MANAGER,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_XCM,
      param_spec);

  param_spec = g_param_spec_object (
      "si-bytestream-manager",
      "SalutSiBytestreamManager object",
      "The Salut SI Bytestream Manager associated with this Salut Connection",
      SALUT_TYPE_SI_BYTESTREAM_MANAGER,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SI_BYTESTREAM_MANAGER,
      param_spec);

#ifdef ENABLE_OLPC
  param_spec = g_param_spec_object (
      "olpc-activity-manager",
      "SalutOlpcActivityManager object",
      "The OLPC activity Manager associated with this Salut Connection",
      SALUT_TYPE_OLPC_ACTIVITY_MANAGER,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_OLPC_ACTIVITY_MANAGER,
      param_spec);
#endif

  param_spec = g_param_spec_gtype (
      "backend-type",
      "backend type",
      "a G_TYPE_GTYPE of the backend to use",
      G_TYPE_NONE,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_BACKEND,
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

  if (priv->self) {
    g_object_unref (priv->self);
    priv->self = NULL;
  }

#ifdef ENABLE_OLPC
  salut_xmpp_connection_manager_remove_stanza_filter (
      priv->xmpp_connection_manager, NULL,
      uninvite_stanza_filter, uninvite_stanza_callback, self);

  if (priv->olpc_activity_manager != NULL)
    {
      g_object_unref (priv->olpc_activity_manager);
      priv->olpc_activity_manager = NULL;
    }
#endif

  if (self->session != NULL)
    {
      g_object_unref (self->session);
      self->session = NULL;
      self->porter = NULL;
    }

  if (priv->xmpp_connection_manager)
    {
      g_signal_handlers_disconnect_matched (priv->xmpp_connection_manager,
          G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);

      g_object_unref (priv->xmpp_connection_manager);
      priv->xmpp_connection_manager = NULL;
    }

  if (priv->discovery_client != NULL)
    {
      g_object_unref (priv->discovery_client);
      g_signal_handlers_disconnect_matched (priv->discovery_client,
          G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
      priv->discovery_client = NULL;
    }

  if (priv->si_bytestream_manager != NULL)
    {
      g_object_unref (priv->si_bytestream_manager);
      priv->si_bytestream_manager = NULL;
    }

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
#ifdef ENABLE_OLPC
  if (priv->olpc_key != NULL)
    g_array_free (priv->olpc_key, TRUE);
  g_free (priv->olpc_color);
#endif

  tp_contacts_mixin_finalize (G_OBJECT(self));

  DEBUG("Finalizing connection");

  G_OBJECT_CLASS (salut_connection_parent_class)->finalize (object);
}

static void
_contact_manager_contact_status_changed (SalutConnection *self,
    SalutContact *contact, TpHandle handle)
{
  TpPresenceStatus ps = { contact->status,
    make_presence_opt_args (contact->status, contact->status_message) };

  tp_presence_mixin_emit_one_presence_update ((GObject *) self, handle,
      &ps);

  if (ps.optional_arguments != NULL)
    g_hash_table_destroy (ps.optional_arguments);
}

static gboolean
announce_self_caps (SalutConnection *self,
                    GError **error)
{
  SalutConnectionPrivate *priv = self->priv;
  gchar *caps_hash;
  gboolean ret;

  caps_hash = caps_hash_compute_from_self_presence (priv->self);

  ret = salut_self_set_caps (priv->self, GIBBER_TELEPATHY_NS_CAPS, "sha-1",
        caps_hash, error);

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
      TP_BASE_CONNECTION (self), TP_HANDLE_TYPE_CONTACT);
  GError *error = NULL;

  priv->self_established = TRUE;

  g_free (self->name);
  self->name = g_strdup (s->name);

  base->self_handle = tp_handle_ensure (handle_repo, self->name, NULL, NULL);

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

  if (!salut_contact_manager_start (priv->contact_manager, NULL))
    {
      tp_base_connection_change_status ( TP_BASE_CONNECTION (base),
          TP_CONNECTION_STATUS_DISCONNECTED,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
      return;
  }

  if (!salut_roomlist_manager_start (priv->roomlist_manager, NULL))
    {
      tp_base_connection_change_status ( TP_BASE_CONNECTION (base),
          TP_CONNECTION_STATUS_DISCONNECTED,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
      return;
    }

#ifdef ENABLE_OLPC
  if (!salut_olpc_activity_manager_start (priv->olpc_activity_manager, NULL))
    {
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
      priv->email, priv->published_name,
#ifdef ENABLE_OLPC
      priv->olpc_key, priv->olpc_color
#else
      NULL, NULL
#endif
      );

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

  if (port == 0 || !salut_self_announce (priv->self, port, NULL))
    {
      tp_base_connection_change_status (
            TP_BASE_CONNECTION (self),
            TP_CONNECTION_STATUS_DISCONNECTED,
            TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
      return;
    }

  /* Create the bytestream manager */
  priv->si_bytestream_manager = salut_si_bytestream_manager_new (self,
    salut_discovery_client_get_host_name_fqdn (priv->discovery_client));
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
/**
 * salut_connection_get_alias_flags
 *
 * Implements D-Bus method GetAliasFlags
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 */
static void
salut_connection_get_alias_flags (TpSvcConnectionInterfaceAliasing *self,
    DBusGMethodInvocation *context)
{
  /* Aliases are set by the contacts
   * Actually we concat the first and lastname property */

  tp_svc_connection_interface_aliasing_return_from_get_alias_flags (context,
      0);
}

static const gchar *
salut_connection_get_alias (SalutConnection *self, TpHandle handle)
{
  SalutConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base,
    TP_HANDLE_TYPE_CONTACT);
  const gchar *alias;

  if (handle == base->self_handle)
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
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 */
static void
salut_connection_request_aliases (TpSvcConnectionInterfaceAliasing *iface,
    const GArray *contacts, DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  guint i;
  const gchar **aliases;
  GError *error = NULL;
  TpHandleRepoIface *contact_handles =
      tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);

  DEBUG ("Alias requested");

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!tp_handles_are_valid (contact_handles, contacts, FALSE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  aliases = g_new0 (const gchar *, contacts->len + 1);
  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);

      aliases[i] = salut_connection_get_alias (self, handle);
    }

  tp_svc_connection_interface_aliasing_return_from_request_aliases (context,
    aliases);

  g_free (aliases);
  return;
}

static void
salut_connection_get_aliases (TpSvcConnectionInterfaceAliasing *iface,
    const GArray *contacts, DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base,
    TP_HANDLE_TYPE_CONTACT);
  guint i;
  GError *error = NULL;
  GHashTable *result = g_hash_table_new_full (g_direct_hash, g_direct_equal,
    NULL, NULL);

  if (!tp_handles_are_valid (contact_repo, contacts, FALSE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);

      g_hash_table_insert (result, GUINT_TO_POINTER (handle),
        (gchar *) salut_connection_get_alias (self, handle));
    }

   tp_svc_connection_interface_aliasing_return_from_get_aliases (context,
       result);

   g_hash_table_destroy (result);
}

static void
salut_connection_aliasing_fill_contact_attributes (GObject *obj,
    const GArray *contacts, GHashTable *attributes_hash)
{
  SalutConnection *self = SALUT_CONNECTION (obj);
  guint i;

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      GValue *val = tp_g_value_slice_new (G_TYPE_STRING);

      g_value_set_string (val, salut_connection_get_alias (self, handle));

      tp_contacts_mixin_set_contact_attribute (attributes_hash, handle,
         TP_IFACE_CONNECTION_INTERFACE_ALIASING"/alias", val);
    }
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

  tp_base_connection_channel_manager_iter_init (&iter, base_conn);
  while (tp_base_connection_channel_manager_iter_next (&iter, &manager))
    {
      /* all channel managers must implement the capability interface */
      g_assert (SALUT_IS_CAPS_CHANNEL_MANAGER (manager));

      salut_caps_channel_manager_get_contact_capabilities (
          SALUT_CAPS_CHANNEL_MANAGER (manager), self, handle, arr);
    }
}

static void
conn_contact_capabilities_fill_contact_attributes (GObject *obj,
  const GArray *contacts, GHashTable *attributes_hash)
{
  SalutConnection *self = SALUT_CONNECTION (obj);
  guint i;
  GPtrArray *array = NULL;

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);

      if (array == NULL)
        array = g_ptr_array_new ();

      salut_connection_get_handle_contact_capabilities (self, handle, array);

      if (array->len > 0)
        {
          GValue *val =  tp_g_value_slice_new (
            TP_ARRAY_TYPE_REQUESTABLE_CHANNEL_CLASS_LIST);

          g_value_take_boxed (val, array);
          tp_contacts_mixin_set_contact_attribute (attributes_hash,
              handle,
              SALUT_IFACE_CONNECTION_INTERFACE_CONTACT_CAPABILITIES"/caps",
              val);

          array = NULL;
        }
    }

    if (array != NULL)
      g_ptr_array_free (array, TRUE);
}

static void
salut_connection_set_aliases (TpSvcConnectionInterfaceAliasing *iface,
    GHashTable *aliases, DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  SalutConnectionPrivate *priv = self->priv;
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

  DEBUG("Setting my alias to: %s", alias);

  if (!salut_self_set_alias (priv->self, alias, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }
  tp_svc_connection_interface_aliasing_return_from_set_aliases (context);
}

static void
_contact_manager_contact_alias_changed  (SalutConnection *self,
    SalutContact *contact, TpHandle handle)
{
  GPtrArray *aliases;
  GValue entry = {0, };

  g_value_init (&entry, SALUT_TP_ALIAS_PAIR_TYPE);
  g_value_take_boxed (&entry,
      dbus_g_type_specialized_construct (SALUT_TP_ALIAS_PAIR_TYPE));

  dbus_g_type_struct_set (&entry,
      0, handle, 1, salut_contact_get_alias (contact), G_MAXUINT);
  aliases = g_ptr_array_sized_new (1);
  g_ptr_array_add (aliases, g_value_get_boxed (&entry));

  DEBUG("Emitting AliasesChanged");

  tp_svc_connection_interface_aliasing_emit_aliases_changed (self, aliases);

  g_value_unset (&entry);
  g_ptr_array_free (aliases, TRUE);
}

static void
salut_connection_aliasing_service_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  TpSvcConnectionInterfaceAliasingClass *klass =
    (TpSvcConnectionInterfaceAliasingClass *) g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_aliasing_implement_##x \
    (klass, salut_connection_##x)
  IMPLEMENT (get_alias_flags);
  IMPLEMENT (request_aliases);
  IMPLEMENT (get_aliases);
  IMPLEMENT (set_aliases);
#undef IMPLEMENT
}

/* Avatar service implementation */
static void
_contact_manager_contact_avatar_changed (SalutConnection *self,
    SalutContact *contact, TpHandle handle)
{
  tp_svc_connection_interface_avatars_emit_avatar_updated (self,
      (guint)handle, contact->avatar_token);
}

static void
salut_connection_clear_avatar (TpSvcConnectionInterfaceAvatars *iface,
    DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = self->priv;
  GError *error = NULL;
  TpBaseConnection *base = (TpBaseConnection *) self;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!salut_self_set_avatar (priv->self, NULL, 0, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }
  tp_svc_connection_interface_avatars_return_from_clear_avatar (context);
}

static void
salut_connection_set_avatar (TpSvcConnectionInterfaceAvatars *iface,
    const GArray *avatar, const gchar *mime_type,
    DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = self->priv;
  GError *error = NULL;
  TpBaseConnection *base = (TpBaseConnection *) self;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!salut_self_set_avatar (priv->self, (guint8 *) avatar->data,
        avatar->len, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  tp_svc_connection_interface_avatars_emit_avatar_updated (self,
      base->self_handle, priv->self->avatar_token);
  tp_svc_connection_interface_avatars_return_from_set_avatar (context,
      priv->self->avatar_token);
}


static void
salut_connection_get_avatar_tokens (TpSvcConnectionInterfaceAvatars *iface,
    const GArray *contacts, DBusGMethodInvocation *context)
{
  guint i;
  gchar **ret;
  GError *err = NULL;
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  TpHandleRepoIface *handle_repo;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  handle_repo = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  if (!tp_handles_are_valid (handle_repo, contacts, FALSE, &err))
    {
      dbus_g_method_return_error (context, err);
      g_error_free (err);
      return;
    }

  ret = g_new0(gchar *, contacts->len + 1);

  for (i = 0; i < contacts->len ; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      if (base->self_handle == handle)
        {
          ret[i] = priv->self->avatar_token;
        }
      else
        {
           SalutContact *contact;

           contact = salut_contact_manager_get_contact (priv->contact_manager,
               handle);
           if (contact != NULL)
             {
               ret[i] = contact->avatar_token;
               g_object_unref (contact);
             }
         }
      if (ret[i] == NULL)
        ret[i] = "";
    }

  tp_svc_connection_interface_avatars_return_from_get_avatar_tokens (context,
      (const gchar **)ret);

  g_free (ret);
}

static void
salut_connection_get_known_avatar_tokens (
    TpSvcConnectionInterfaceAvatars *iface, const GArray *contacts,
    DBusGMethodInvocation *context)
{
  guint i;
  GHashTable *ret;
  GError *err = NULL;
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  TpHandleRepoIface *handle_repo;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  handle_repo = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  if (!tp_handles_are_valid (handle_repo, contacts, FALSE, &err))
    {
      dbus_g_method_return_error (context, err);
      g_error_free (err);
      return;
    }

  ret = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);

  for (i = 0; i < contacts->len ; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      gchar *tokens = NULL;

      if (base->self_handle == handle)
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

  tp_svc_connection_interface_avatars_return_from_get_known_avatar_tokens (
     context, ret);

  g_hash_table_destroy (ret);
}

static void
salut_connection_avatars_fill_contact_attributes (GObject *obj,
    const GArray *contacts, GHashTable *attributes_hash)
{
  guint i;
  SalutConnection *self = SALUT_CONNECTION (obj);
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  SalutConnectionPrivate *priv = self->priv;

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      gchar *token = NULL;

      if (base->self_handle == handle)
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

          tp_contacts_mixin_set_contact_attribute (attributes_hash, handle,
            TP_IFACE_CONNECTION_INTERFACE_AVATARS"/token", val);
        }
    }
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

  tp_svc_connection_interface_avatars_emit_avatar_retrieved (
    (GObject *) user_data, contact->handle,
    contact->avatar_token, arr, "");

  g_array_free (arr, TRUE);
}

static void
salut_connection_request_avatars (
    TpSvcConnectionInterfaceAvatars *iface,
    const GArray *contacts,
    DBusGMethodInvocation *context)
{
  guint i;
  GError *err = NULL;
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  TpHandleRepoIface *handle_repo;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  handle_repo = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  if (!tp_handles_are_valid (handle_repo, contacts, FALSE, &err))
    {
      dbus_g_method_return_error (context, err);
      g_error_free (err);
      return;
    }

  for (i = 0; i < contacts->len ; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);

      if (base->self_handle == handle)
        {
           GArray *arr;

           if (priv->self->avatar != NULL)
             {
               arr = g_array_sized_new (FALSE, FALSE, sizeof (guint8),
                 priv->self->avatar_size);
               arr = g_array_append_vals (arr, priv->self->avatar,
                 priv->self->avatar_size);

               tp_svc_connection_interface_avatars_emit_avatar_retrieved (
                  (GObject *) self, base->self_handle,
                    priv->self->avatar_token, arr, "");
               g_array_free (arr, TRUE);
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

  tp_svc_connection_interface_avatars_return_from_request_avatars (context);
}

static void
_request_avatar_cb (SalutContact *contact, guint8 *avatar, gsize size,
                   gpointer user_data)
{
  DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;

  GError *err = NULL;
  GArray *arr;

  if (size == 0)
    {
      err = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                         "Unable to get avatar");
      dbus_g_method_return_error (context, err);
      g_error_free (err);
      return;
  }

  arr = g_array_sized_new (FALSE, FALSE, sizeof (guint8), size);
  arr = g_array_append_vals (arr, avatar, size);
  tp_svc_connection_interface_avatars_return_from_request_avatar (context,
      arr, "");
  g_array_free (arr, TRUE);
}

static void
salut_connection_request_avatar (TpSvcConnectionInterfaceAvatars *iface,
    guint handle, DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  SalutContact *contact;
  GError *err = NULL;
  TpHandleRepoIface *handle_repo;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  handle_repo = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  if (!tp_handle_is_valid (handle_repo, handle, &err))
    {
      dbus_g_method_return_error (context, err);
      g_error_free (err);
      return;
    }

  if (handle == base->self_handle)
    {
      _request_avatar_cb (NULL, priv->self->avatar, priv->self->avatar_size,
        context);
      return;
    }

  contact = salut_contact_manager_get_contact (priv->contact_manager, handle);
  if (contact == NULL || contact->avatar_token == NULL)
    {
      err = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "No known avatar");
      dbus_g_method_return_error (context, err);
      g_error_free (err);
      if (contact != NULL)
        {
          g_object_unref (contact);
        }
      return;
    }
  salut_contact_get_avatar (contact, _request_avatar_cb, context);
  g_object_unref (contact);
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
salut_connection_get_avatar_requirements (
    TpSvcConnectionInterfaceAvatars *iface, DBusGMethodInvocation *context)
{
  tp_svc_connection_interface_avatars_return_from_get_avatar_requirements (
      context, mimetypes, AVATAR_MIN_PX, AVATAR_MIN_PX, AVATAR_MAX_PX,
      AVATAR_MAX_PX, AVATAR_MAX_BYTES);
}

static void
salut_connection_avatar_service_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  TpSvcConnectionInterfaceAvatarsClass *klass =
       (TpSvcConnectionInterfaceAvatarsClass *) g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_avatars_implement_##x \
    (klass, salut_connection_##x)
  IMPLEMENT (get_avatar_requirements);
  IMPLEMENT (get_avatar_tokens);
  IMPLEMENT (get_known_avatar_tokens);
  IMPLEMENT (request_avatar);
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

  g_ptr_array_free (caps, TRUE);
}

/**
 * salut_connection_get_contact_capabilities
 *
 * Implements D-Bus method GetContactCapabilities
 * on interface
 * org.freedesktop.Telepathy.Connection.Interface.ContactCapabilities
 */
static void
salut_connection_get_contact_capabilities (
    SalutSvcConnectionInterfaceContactCapabilities *iface,
    const GArray *handles,
    DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  guint i;
  GHashTable *ret;
  GError *error = NULL;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!tp_handles_are_valid (contact_handles, handles, FALSE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  ret = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) salut_free_enhanced_contact_capabilities);

  for (i = 0; i < handles->len; i++)
    {
      GPtrArray *arr = g_ptr_array_new ();
      TpHandle handle = g_array_index (handles, TpHandle, i);

      salut_connection_get_handle_contact_capabilities (self, handle, arr);

      g_hash_table_insert (ret, GINT_TO_POINTER (handle), arr);
    }

  salut_svc_connection_interface_contact_capabilities_return_from_get_contact_capabilities
      (context, ret);

  g_hash_table_destroy (ret);
}


static void
_emit_contact_capabilities_changed (SalutConnection *conn,
                                    TpHandle handle,
                                    GHashTable *old_caps,
                                    GHashTable *new_caps)
{
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
  TpChannelManagerIter iter;
  TpChannelManager *manager;
  GPtrArray *ret;
  gboolean diff = FALSE;
  GHashTable *caps;

  tp_base_connection_channel_manager_iter_init (&iter, base_conn);
  while (tp_base_connection_channel_manager_iter_next (&iter, &manager))
    {
      gpointer per_channel_manager_caps_old = NULL;
      gpointer per_channel_manager_caps_new = NULL;

      /* all channel managers must implement the capability interface */
      g_assert (SALUT_IS_CAPS_CHANNEL_MANAGER (manager));

      if (old_caps != NULL)
        per_channel_manager_caps_old = g_hash_table_lookup (old_caps, manager);
      if (new_caps != NULL)
        per_channel_manager_caps_new = g_hash_table_lookup (new_caps, manager);

      if (salut_caps_channel_manager_capabilities_diff (
            SALUT_CAPS_CHANNEL_MANAGER (manager), handle,
            per_channel_manager_caps_old, per_channel_manager_caps_new))
        {
          diff = TRUE;
          break;
        }
    }

  /* Don't emit the D-Bus signal if there is no change */
  if (! diff)
    return;

  caps = g_hash_table_new (g_direct_hash, g_direct_equal);
  ret = g_ptr_array_new ();

  salut_connection_get_handle_contact_capabilities (conn, handle, ret);
  g_hash_table_insert (caps, GUINT_TO_POINTER (handle), ret);

  salut_svc_connection_interface_contact_capabilities_emit_contact_capabilities_changed (
      conn, caps);

  salut_free_enhanced_contact_capabilities (ret);
  g_hash_table_destroy (caps);
}

static void
connection_capabilities_update_cb (SalutPresenceCache *cache,
                                   TpHandle handle,
                                   GHashTable *old_enhanced_caps,
                                   GHashTable *new_enhanced_caps,
                                   gpointer user_data)
{
  SalutConnection *conn = SALUT_CONNECTION (user_data);

  g_assert (SALUT_IS_CONNECTION (user_data));

  if (old_enhanced_caps != NULL || new_enhanced_caps != NULL)
    _emit_contact_capabilities_changed (conn, handle,
        old_enhanced_caps, new_enhanced_caps);
}

/**
 * salut_connection_set_self_capabilities
 *
 * Implements D-Bus method SetSelfCapabilities
 * on interface
 * org.freedesktop.Telepathy.Connection.Interface.ContactCapabilities
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_connection_set_self_capabilities (
    SalutSvcConnectionInterfaceContactCapabilities *iface,
    const GPtrArray *caps,
    DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  SalutConnectionPrivate *priv = self->priv;
  guint i;
  GHashTable *save_caps;
  GError *error = NULL;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  /* reset the caps, and fill with the given parameter but keep a backup for
   * diffing: we don't want to emit a signal if nothing has changed */
  save_caps = priv->self->per_channel_manager_caps;
  priv->self->per_channel_manager_caps = NULL;

  for (i = 0; i < caps->len; i++)
    {
      GHashTable *cap_to_add = g_ptr_array_index (caps, i);
      TpChannelManagerIter iter;
      TpChannelManager *manager;

      tp_base_connection_channel_manager_iter_init (&iter, base);
      while (tp_base_connection_channel_manager_iter_next (&iter, &manager))
        {
          /* all channel managers must implement the capability interface */
          g_assert (SALUT_IS_CAPS_CHANNEL_MANAGER (manager));

          salut_caps_channel_manager_add_capability (
              SALUT_CAPS_CHANNEL_MANAGER (manager), self,
              base->self_handle, cap_to_add);
        }
    }

  /* XEP-0115 version 1.5 uses a verification string in the 'ver' attribute */
  if (!announce_self_caps (self, &error))
    {
      salut_presence_cache_free_cache_entry (save_caps);
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  _emit_contact_capabilities_changed (self, base->self_handle,
                                      save_caps,
                                      priv->self->per_channel_manager_caps);
  salut_presence_cache_free_cache_entry (save_caps);


  salut_svc_connection_interface_contact_capabilities_return_from_set_self_capabilities
      (context);
}


static void
salut_conn_contact_caps_iface_init (gpointer g_iface, gpointer iface_data)
{
  SalutSvcConnectionInterfaceContactCapabilitiesClass *klass =
    (SalutSvcConnectionInterfaceContactCapabilitiesClass *) g_iface;

#define IMPLEMENT(x) \
    salut_svc_connection_interface_contact_capabilities_implement_##x (\
    klass, salut_connection_##x)
  IMPLEMENT(get_contact_capabilities);
  IMPLEMENT(set_self_capabilities);
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
get_properties_hash (const GArray *key, const gchar *color, const gchar *jid,
  const gchar *ip4, const gchar *ip6)
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

  if (ip4 != NULL)
    {
      gvalue = new_gvalue (G_TYPE_STRING);
      g_value_set_string (gvalue, ip4);
      g_hash_table_insert (properties, "ip4-address", gvalue);
    }

  if (ip6 != NULL)
    {
      gvalue = new_gvalue (G_TYPE_STRING);
      g_value_set_string (gvalue, ip6);
      g_hash_table_insert (properties, "ip6-address", gvalue);
    }

  return properties;
}

static void
emit_properties_changed (SalutConnection *connection,
                         TpHandle handle,
                         const GArray *key,
                         const gchar *color,
                         const gchar *jid,
                         const gchar *ip4,
                         const gchar *ip6)
{
  GHashTable *properties;
  properties = get_properties_hash (key, color, jid, ip4, ip6);

  salut_svc_olpc_buddy_info_emit_properties_changed (connection,
      handle, properties);

  g_hash_table_destroy (properties);
}

static void
append_activity (SalutOlpcActivity *activity,
                 gpointer user_data)
{
  GPtrArray *arr = user_data;
  GType type = ACTIVITY_PAIR_TYPE;
  GValue gvalue = {0};

  g_value_init (&gvalue, type);
  g_value_take_boxed (&gvalue,
      dbus_g_type_specialized_construct (type));

  dbus_g_type_struct_set (&gvalue,
      0, activity->id,
      1, activity->room,
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
      contact->olpc_color, contact->jid, contact->olpc_ip4, contact->olpc_ip6);
}

static gboolean
check_handle (TpHandleRepoIface *handle_repo,
              TpHandle handle,
              DBusGMethodInvocation *context)
{
  GError *error = NULL;

  if (!tp_handle_is_valid (handle_repo, handle, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return FALSE;
    }

  return TRUE;
}

static gboolean
check_contact (TpBaseConnection *base,
               TpHandle contact,
               DBusGMethodInvocation *context)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      base, TP_HANDLE_TYPE_CONTACT);

  return check_handle (contact_repo, contact, context);
}

static gboolean
check_room (TpBaseConnection *base,
            TpHandle contact,
            DBusGMethodInvocation *context)
{
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      base, TP_HANDLE_TYPE_ROOM);

  return check_handle (room_repo, contact, context);
}

static void
salut_connection_olpc_get_properties (SalutSvcOLPCBuddyInfo *iface,
                                      TpHandle handle,
                                      DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  GHashTable *properties = NULL;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!check_contact (base, handle, context))
    return;

  if (handle == base->self_handle)
    {
      properties = get_properties_hash (priv->self->olpc_key,
          priv->self->olpc_color, priv->self->jid, NULL, NULL);
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
        contact->jid, contact->olpc_ip4, contact->olpc_ip6);
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
  SalutConnectionPrivate *priv = self->priv;

  GError *error = NULL;
  /* Only a few known properties, so handle it quite naively */
  const gchar *known_properties[] = { "color", "key", "jid", "ip4-address",
     "ip6-address", NULL };
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
  SalutConnectionPrivate *priv = self->priv;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  DEBUG ("called for %u", handle);

  if (!check_contact (base, handle, context))
    return;

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
  SalutConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = (TpBaseConnection *) self;
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
      if (!check_room (base, room_handle, context))
        return;
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
  SalutConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = (TpBaseConnection *) self;
  GPtrArray *arr;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  DEBUG ("called for %u", handle);

  if (!check_contact (base, handle, context))
    return;

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
  SalutConnectionPrivate *priv = self->priv;
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
salut_connection_olpc_add_activity (SalutSvcOLPCBuddyInfo *iface,
                                    const gchar *id,
                                    TpHandle handle,
                                    DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = (TpBaseConnection *) self;
  GError *error = NULL;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!salut_self_add_olpc_activity (priv->self, id, handle, &error))
    {
      dbus_g_method_return_error (context, error);
    }
  else
    {
      salut_svc_olpc_buddy_info_return_from_set_activities (context);
    }
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
  IMPLEMENT(add_activity);
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
  SalutConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_ROOM);
  GHashTable *properties = NULL;
  GError *error = NULL;
  SalutOlpcActivity *activity;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!tp_handle_is_valid (room_repo, handle, &error))
    goto error;

  activity = salut_olpc_activity_manager_get_activity_by_room (
      priv->olpc_activity_manager, handle);
  if (activity == NULL)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Activity unknown: %u", handle);
      goto error;
    }

  properties = salut_olpc_activity_create_properties_table (activity);

  salut_svc_olpc_buddy_info_return_from_get_properties (context, properties);
  g_hash_table_destroy (properties);

  return;

error:
  dbus_g_method_return_error (context, error);
  g_error_free (error);
}

static gboolean
check_color (const gchar *color)
{
  int len, i;

  /* be very anal about the color format */
  len = strlen (color);
  if (len != 15)
    return FALSE;

  for (i = 0 ; i < len ; i++)
    {
      switch (i)
        {
          case 0:
          case 8:
            if (color[i] != '#')
              return FALSE;
            break;
          case 7:
            if (color[i] != ',')
              return FALSE;
            break;
          default:
            if (!isxdigit (color[i]))
              return FALSE;
            break;
        }
    }

  return TRUE;
}

/* returned strings are only valid as long as the hash table isn't modified */
static gboolean
extract_properties_from_hash (GHashTable *properties,
                              const gchar **id,
                              const gchar **color,
                              const gchar **name,
                              const gchar **type,
                              const gchar **tags,
                              gboolean *is_private,
                              GError **error)
{
  GValue *activity_id_val, *color_val, *activity_name_val, *activity_type_val,
      *tags_val, *is_private_val;

  /* activity ID */
  activity_id_val = g_hash_table_lookup (properties, "id");
  if (activity_id_val != NULL)
    {
      if (G_VALUE_TYPE (activity_id_val) != G_TYPE_STRING)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "Activity ID value should be of type s");
          return FALSE;
        }

      if (id != NULL)
        *id = g_value_get_string (activity_id_val);
    }

  /* color */
  color_val = g_hash_table_lookup (properties, "color");
  if (color_val != NULL)
    {
      if (G_VALUE_TYPE (color_val) != G_TYPE_STRING)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "Color value should be of type s");
          return FALSE;
        }

      if (color != NULL)
        {
          *color = g_value_get_string (color_val);

           if (!check_color (*color))
            {
              g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                  "Color value has an incorrect format");
              return FALSE;
            }
        }
    }

  /* name */
  activity_name_val = g_hash_table_lookup (properties, "name");
  if (activity_name_val != NULL)
    {
      if (G_VALUE_TYPE (activity_name_val) != G_TYPE_STRING)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "name value should be of type s");
          return FALSE;
        }

      if (name != NULL)
        *name = g_value_get_string (activity_name_val);
    }

  /* type */
  activity_type_val = g_hash_table_lookup (properties, "type");
  if (activity_type_val != NULL)
    {
      if (G_VALUE_TYPE (activity_type_val) != G_TYPE_STRING)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "type value should be of type s");
          return FALSE;
        }

      if (type != NULL)
        {
          *type = g_value_get_string (activity_type_val);

          if (*type[0] == '\0')
            {
              g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                  "type value must be non-empty");
              return FALSE;
            }
        }
    }

  /* tags */
  tags_val = g_hash_table_lookup (properties, "tags");
  if (tags_val != NULL)
    {
      if (G_VALUE_TYPE (activity_type_val) != G_TYPE_STRING)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "tags value should be of type s");
          return FALSE;
        }

      if (type != NULL)
        *tags = g_value_get_string (tags_val);
    }

  /* is_private */
  is_private_val = g_hash_table_lookup (properties, "private");
  if (is_private_val != NULL)
    {
     if (G_VALUE_TYPE (is_private_val) != G_TYPE_BOOLEAN)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "private value should be of type b");
          return FALSE;
        }

      if (is_private != NULL)
        *is_private = g_value_get_boolean (is_private_val);
    }

  return TRUE;
}

static void
salut_connection_act_set_properties (SalutSvcOLPCActivityProperties *iface,
                                     TpHandle handle,
                                     GHashTable *properties,
                                     DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = (TpBaseConnection *) self;
  GError *error = NULL;
  const gchar *known_properties[] = { "color", "name", "type", "private",
      "tags", NULL };
  const gchar *color = NULL, *name = NULL, *type = NULL, *tags = NULL;
  gboolean is_private = TRUE;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!check_room (base, handle, context))
    return;

  if (g_hash_table_find (properties, find_unknown_properties, known_properties)
      != NULL)
    {
      error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Unknown property given");
      goto error;
    }

  if (!extract_properties_from_hash (properties, NULL, &color, &name, &type,
        &tags, &is_private, &error))
    goto error;

  if (!salut_self_set_olpc_activity_properties (priv->self, handle, color,
        name, type, tags, is_private, &error))
    goto error;

  salut_svc_olpc_activity_properties_return_from_set_properties (context);
  return;

error:
  dbus_g_method_return_error (context, error);
  g_error_free (error);
}

typedef struct
{
  SalutContact *inviter;
  SalutOlpcActivity *activity;
} muc_ready_ctx;

static muc_ready_ctx *
muc_ready_ctx_new (SalutContact *inviter,
                   SalutOlpcActivity *activity)
{
  muc_ready_ctx *ctx = g_slice_new (muc_ready_ctx);
  ctx->inviter = inviter;
  g_object_ref (inviter);
  ctx->activity = activity;
  g_object_ref (activity);
  return ctx;
}

static void
muc_ready_ctx_free (muc_ready_ctx *ctx)
{
  if (ctx == NULL)
    return;

  g_object_unref (ctx->inviter);
  g_object_unref (ctx->activity);
  g_slice_free (muc_ready_ctx, ctx);
}

static void
muc_ready_cb (SalutMucChannel *muc,
              muc_ready_ctx *ctx)
{
  /* We joined the muc so have to forget about invites */
  salut_contact_left_activity (ctx->inviter, ctx->activity);

  DEBUG ("forget invite received from %s", ctx->inviter->name);
  g_signal_handlers_disconnect_matched (muc, G_SIGNAL_MATCH_DATA, 0, 0, NULL,
      NULL, ctx);
  muc_ready_ctx_free (ctx);
}

static void
muc_closed_cb (SalutMucChannel *muc,
               muc_ready_ctx *ctx)
{
  /* FIXME: should we call left_private_activity here too ? */

  g_signal_handlers_disconnect_matched (muc, G_SIGNAL_MATCH_DATA, 0, 0, NULL,
      NULL, ctx);
  muc_ready_ctx_free (ctx);
}

void
salut_connection_olpc_observe_invitation (SalutConnection *self,
                                          TpHandle room,
                                          TpHandle inviter_handle,
                                          WockyNode *invite_node)
{
  SalutConnectionPrivate *priv = self->priv;
  WockyNode *props_node;
  GHashTable *properties;
  const gchar *activity_id, *color = NULL, *activity_name = NULL,
        *activity_type = NULL, *tags = NULL;
  SalutContact *inviter;
  SalutOlpcActivity *activity;
  SalutMucChannel *muc;
  muc_ready_ctx *ctx;

  props_node = wocky_node_get_child_ns (invite_node, "properties",
      GIBBER_TELEPATHY_NS_OLPC_ACTIVITY_PROPS);

  if (props_node == NULL)
    return;

  inviter = salut_contact_manager_get_contact (priv->contact_manager,
      inviter_handle);
  if (inviter == NULL)
    return;

  properties = salut_wocky_node_extract_properties (props_node,
      "property");

  if (!extract_properties_from_hash (properties, &activity_id, &color,
        &activity_name, &activity_type, &tags, NULL, NULL))
    return;

  activity = salut_olpc_activity_manager_got_invitation (
      priv->olpc_activity_manager,
      room, inviter, activity_id, activity_name, activity_type,
      color, tags);

  muc = salut_muc_manager_get_text_channel (priv->muc_manager, room);
  g_assert (muc != NULL);

  ctx = muc_ready_ctx_new (inviter, activity);
  g_signal_connect (muc, "ready", G_CALLBACK (muc_ready_cb), ctx);
  g_signal_connect (muc, "closed", G_CALLBACK (muc_closed_cb), ctx);

  g_object_unref (muc);
  g_hash_table_destroy (properties);
  g_object_unref (inviter);
}

static void
salut_connection_act_get_activity (SalutSvcOLPCActivityProperties *iface,
                                   const gchar *activity_id,
                                   DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  SalutConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = (TpBaseConnection *) self;
  GError *error = NULL;
  SalutOlpcActivity *activity;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  activity = salut_olpc_activity_manager_get_activity_by_id (
      priv->olpc_activity_manager, activity_id);
  if (activity == NULL)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Activity unknown: %s", activity_id);
      goto error;
    }

  salut_svc_olpc_activity_properties_return_from_get_activity (context,
      activity->room);

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
  IMPLEMENT(get_activity);
#undef IMPLEMENT
}
#endif

gchar *
salut_normalize_non_empty (const gchar *id,
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

static void
_contact_manager_contact_change_cb (SalutContactManager *mgr,
    SalutContact *contact, int changes, gpointer data)
{
  SalutConnection *self = SALUT_CONNECTION(data);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (
      TP_BASE_CONNECTION(self), TP_HANDLE_TYPE_CONTACT);
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

#ifdef ENABLE_OLPC
static void
_olpc_activity_manager_activity_modified_cb (SalutOlpcActivityManager *mgr,
  SalutOlpcActivity *activity, SalutConnection *self)
{
  GHashTable *properties;

  properties = salut_olpc_activity_create_properties_table (activity);
  salut_svc_olpc_activity_properties_emit_activity_properties_changed (
      self, activity->room, properties);

  g_hash_table_destroy (properties);
}

gboolean
salut_connection_olpc_observe_muc_stanza (SalutConnection *self,
    TpHandle room, TpHandle sender, WockyStanza *stanza)
{
  WockyNode *node = wocky_stanza_get_top_node (stanza);
  SalutConnectionPrivate *priv = self->priv;
  WockyNode *props_node;
  GHashTable *properties;
  const gchar *activity_id, *color = NULL, *activity_name = NULL,
        *activity_type = NULL, *tags = NULL;
  gboolean is_private = FALSE;
  SalutOlpcActivity *activity;

  props_node = wocky_node_get_child_ns (node, "properties",
      GIBBER_TELEPATHY_NS_OLPC_ACTIVITY_PROPS);

  if (props_node == NULL)
    return FALSE;

  activity = salut_olpc_activity_manager_get_activity_by_room (
      priv->olpc_activity_manager, room);

  if (activity == NULL)
    {
      DEBUG ("no activity in room %d", room);
      return FALSE;
    }

  properties = salut_wocky_node_extract_properties (props_node,
      "property");

  if (!extract_properties_from_hash (properties, &activity_id, &color,
        &activity_name, &activity_type, &tags, &is_private, NULL))
    return TRUE;

  salut_olpc_activity_update (activity, room, activity_id, activity_name,
      activity_type, color, tags, is_private);

  g_hash_table_destroy (properties);

  return TRUE;
}

static gboolean
uninvite_stanza_filter (SalutXmppConnectionManager *mgr,
  GibberXmppConnection *conn, WockyStanza *stanza, SalutContact *contact,
  gpointer user_data)
{
  WockyNode *node = wocky_stanza_get_top_node (stanza);

  return (wocky_node_get_child_ns (node, "uninvite",
        GIBBER_TELEPATHY_NS_OLPC_ACTIVITY_PROPS) != NULL);
}

static void
uninvite_stanza_callback (SalutXmppConnectionManager *mgr,
  GibberXmppConnection *conn, WockyStanza *stanza, SalutContact *contact,
  gpointer user_data)
{
  SalutConnection *self = SALUT_CONNECTION (user_data);
  SalutConnectionPrivate *priv = self->priv;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self, TP_HANDLE_TYPE_ROOM);
  WockyNode *node;
  TpHandle room_handle;
  const gchar *room, *activity_id;
  SalutOlpcActivity *activity;
  WockyNode *top_node = wocky_stanza_get_top_node (stanza);

  node = wocky_node_get_child_ns (top_node, "uninvite",
        GIBBER_TELEPATHY_NS_OLPC_ACTIVITY_PROPS);
  g_assert (node != NULL);

  room = wocky_node_get_attribute (node, "room");
  if (room == NULL)
    {
      DEBUG ("No room attribute");
      return;
    }

  room_handle = tp_handle_lookup (room_repo, room, NULL, NULL);
  if (room_handle == 0)
    {
      DEBUG ("room %s unknown", room);
      return;
    }

  activity_id = wocky_node_get_attribute (node, "id");
  if (activity_id == NULL)
    {
      DEBUG ("No id attribute");
      return;
    }

  DEBUG ("received uninvite from %s", contact->name);

  activity = salut_olpc_activity_manager_get_activity_by_room (
      priv->olpc_activity_manager, room_handle);

  if (activity == NULL)
    return;

  salut_contact_left_activity (contact, activity);
}

#endif

static GPtrArray *
salut_connection_create_channel_factories (TpBaseConnection *base)
{
  SalutConnection *self = SALUT_CONNECTION (base);
  SalutConnectionPrivate *priv = self->priv;
  GPtrArray *factories = g_ptr_array_sized_new (4);

  /* Create the contact manager */
  priv->contact_manager = salut_discovery_client_create_contact_manager (
      priv->discovery_client, self);
  g_signal_connect (priv->contact_manager, "contact-change",
      G_CALLBACK (_contact_manager_contact_change_cb), self);

  /* Create the XMPP connection manager */
  priv->xmpp_connection_manager = salut_xmpp_connection_manager_new (self,
      priv->contact_manager);

#ifdef ENABLE_OLPC
  salut_xmpp_connection_manager_add_stanza_filter (
    priv->xmpp_connection_manager, NULL,
    uninvite_stanza_filter, uninvite_stanza_callback, self);

  /* create the OLPC activity manager */
  priv->olpc_activity_manager =
      salut_discovery_client_create_olpc_activity_manager (
          priv->discovery_client, self);
  g_signal_connect (priv->olpc_activity_manager, "activity-modified",
      G_CALLBACK (_olpc_activity_manager_activity_modified_cb), self);
#endif

  return factories;
}

#ifdef ENABLE_OLPC
static void
muc_channel_closed_cb (SalutMucChannel *chan,
                       SalutOlpcActivity *activity)
{
  SalutConnection *conn;
  SalutConnectionPrivate *priv;
  TpBaseConnection *base;
  GPtrArray *activities = g_ptr_array_new ();

  g_signal_handlers_disconnect_by_func (chan,
      G_CALLBACK (muc_channel_closed_cb), activity);

  g_object_get (activity,
      "connection", &conn,
      NULL);

  priv = conn->priv;
  base = (TpBaseConnection *) conn;

  salut_self_remove_olpc_activity (priv->self, activity);

  salut_self_foreach_olpc_activity (priv->self, append_activity, activities);
  salut_svc_olpc_buddy_info_emit_activities_changed (conn, base->self_handle,
      activities);
  free_olpc_activities (activities);

  /* we were holding a ref since the channel was opened */
  g_object_unref (activity);

  g_object_unref (conn);
}

static void
muc_manager_new_channels_cb (TpChannelManager *channel_manager,
                             GHashTable *channels,
                             SalutConnection *conn)
{
  SalutConnectionPrivate *priv = conn->priv;
  GHashTableIter iter;
  gpointer chan;

  g_hash_table_iter_init (&iter, channels);
  while (g_hash_table_iter_next (&iter, &chan, NULL))
    {
      SalutOlpcActivity *activity;
      TpHandle room_handle;

      if (!SALUT_IS_MUC_CHANNEL (chan))
        return;

      g_object_get (chan,
          "handle", &room_handle,
          NULL);

      /* ref the activity as long as we have a channel open */
      activity = salut_olpc_activity_manager_ensure_activity_by_room (
          priv->olpc_activity_manager,
          room_handle);

      g_signal_connect (chan, "closed", G_CALLBACK (muc_channel_closed_cb),
          activity);
    }
}
#endif

static GPtrArray *
salut_connection_create_channel_managers (TpBaseConnection *base)
{
  SalutConnection *self = SALUT_CONNECTION (base);
  SalutConnectionPrivate *priv = self->priv;
  GPtrArray *managers = g_ptr_array_sized_new (1);

  /* FIXME: The second and third arguments depend on create_channel_factories
   *        being called before this; should telepathy-glib guarantee that or
   *        should we be defensive?
   */
  priv->im_manager = salut_im_manager_new (self, priv->contact_manager);

  priv->ft_manager = salut_ft_manager_new (self, priv->contact_manager);

  priv->muc_manager = salut_discovery_client_create_muc_manager (
      priv->discovery_client, self, priv->xmpp_connection_manager);

  priv->roomlist_manager = salut_discovery_client_create_roomlist_manager (
      priv->discovery_client, self, priv->xmpp_connection_manager);

#if 0
  priv->tubes_manager = salut_tubes_manager_new (self, priv->contact_manager,
      priv->xmpp_connection_manager);
#endif

  g_ptr_array_add (managers, priv->im_manager);
  g_ptr_array_add (managers, priv->contact_manager);
  g_ptr_array_add (managers, priv->ft_manager);
  g_ptr_array_add (managers, priv->muc_manager);
  g_ptr_array_add (managers, priv->roomlist_manager);
#if 0
  g_ptr_array_add (managers, priv->tubes_manager);
#endif

#ifdef ENABLE_OLPC
  g_signal_connect (TP_CHANNEL_MANAGER (priv->muc_manager), "new-channels",
      G_CALLBACK (muc_manager_new_channels_cb), self);
#endif

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
          return;
        }

      g_error_free (error);
    }
  else
    {
      DEBUG ("connection properly closed");
    }

  tp_base_connection_finish_shutdown (base);
}

static gboolean
disconnect_timeout_cb (gpointer data)
{
  SalutConnection *self = SALUT_CONNECTION (data);
  SalutConnectionPrivate *priv = self->priv;

  DEBUG ("Close operation timed out. Force closing");
  priv->disconnect_timer = 0;

  wocky_porter_force_close_async (self->porter, NULL, force_close_cb, self);
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

      wocky_porter_close_async (self->porter, NULL, closed_cb, self);
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
      *error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Unstable to initialize the avahi client: %s",
          client_error->message);
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
