/*
 * protocol.c - SalutProtocol
 * Copyright © 2007-2010 Collabora Ltd.
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
#include "protocol.h"

#include <dbus/dbus-glib.h>
#include <dbus/dbus-protocol.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "connection.h"
#include "contact-manager.h"
#include "ft-manager.h"
#include "im-manager.h"
#include "muc-manager.h"
#include "roomlist-manager.h"
#include "tubes-manager.h"

#ifdef USE_BACKEND_AVAHI
#include "avahi-discovery-client.h"
#elif defined (USE_BACKEND_DUMMY)
#include "dummy-discovery-client.h"
#elif defined (USE_BACKEND_BONJOUR)
#include "bonjour-discovery-client.h"
#endif

/* there is no appropriate vCard field for this protocol */
#define VCARD_FIELD_NAME ""

G_DEFINE_TYPE (SalutProtocol,
    salut_protocol,
    TP_TYPE_BASE_PROTOCOL)

enum {
    PROP_BACKEND = 1,
    PROP_DNSSD_NAME,
    PROP_ENGLISH_NAME,
    PROP_ICON_NAME
};

struct _SalutProtocolPrivate
{
  GType backend_type;
  gchar *english_name;
  gchar *icon_name;
  gchar *dnssd_name;
};

static const TpCMParamSpec salut_params[] = {
  { "nickname", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, 0,
     tp_cm_param_filter_string_nonempty, NULL },
  { "first-name", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
     TP_CONN_MGR_PARAM_FLAG_REQUIRED, NULL, 0 },
  { "last-name", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
     TP_CONN_MGR_PARAM_FLAG_REQUIRED, NULL, 0 },
  { "jid", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, 0 },
  { "email", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, 0 },
  { "published-name", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, 0,
     tp_cm_param_filter_string_nonempty, NULL },
  { NULL, NULL, 0, 0, NULL, 0 }
};

static void
salut_protocol_init (SalutProtocol *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, SALUT_TYPE_PROTOCOL,
      SalutProtocolPrivate);
}

static const TpCMParamSpec *
get_parameters (TpBaseProtocol *self G_GNUC_UNUSED)
{
  return salut_params;
}

static TpBaseConnection *
new_connection (TpBaseProtocol *protocol,
                GHashTable *params,
                GError **error)
{
  SalutProtocol *self = SALUT_PROTOCOL (protocol);
  GObject *obj;
  guint i;

  obj = g_object_new (SALUT_TYPE_CONNECTION,
      "protocol", tp_base_protocol_get_name (protocol),
      /* deliberately set :dnssd-name before backend-type */
      "dnssd-name", self->priv->dnssd_name,
      "backend-type", self->priv->backend_type,
      NULL);

  for (i = 0; salut_params[i].name != NULL; i++)
    {
      GValue *val = g_hash_table_lookup (params, salut_params[i].name);

      if (val != NULL)
        {
          g_object_set_property (obj, salut_params[i].name, val);
        }
    }

  return TP_BASE_CONNECTION (obj);
}

static gchar *
normalize_contact (TpBaseProtocol *self G_GNUC_UNUSED,
                   const gchar *contact,
                   GError **error)
{
  return salut_normalize_non_empty (contact, error);
}

static gchar *
identify_account (TpBaseProtocol *self G_GNUC_UNUSED,
    GHashTable *asv,
    GError **error)
{
  /* Nothing uniquely identifies a particular Salut account. The published
   * name is part of our identifier, but can be changed at any time;
   * the best an account manager can do is to number accounts. */
  return g_strdup ("");
}

static void
get_connection_details (TpBaseProtocol *self,
    GStrv *connection_interfaces,
    GType **channel_managers,
    gchar **icon_name,
    gchar **english_name,
    gchar **vcard_field)
{
  SalutProtocolPrivate *priv = SALUT_PROTOCOL (self)->priv;

  if (connection_interfaces != NULL)
    {
      *connection_interfaces = g_strdupv (
          (GStrv) salut_connection_get_implemented_interfaces ());
    }

  if (channel_managers != NULL)
    {
      GType types[] = {
          SALUT_TYPE_FT_MANAGER,
          SALUT_TYPE_IM_MANAGER,
          SALUT_TYPE_MUC_MANAGER,
          SALUT_TYPE_ROOMLIST_MANAGER,
          SALUT_TYPE_TUBES_MANAGER,
          G_TYPE_INVALID };

      *channel_managers = g_memdup (types, sizeof(types));
    }

  if (icon_name != NULL)
    {
      *icon_name = g_strdup (priv->icon_name);
    }

  if (vcard_field != NULL)
    {
      *vcard_field = g_strdup (VCARD_FIELD_NAME);
    }

  if (english_name != NULL)
    {
      *english_name = g_strdup (priv->english_name);
    }
}

static void
salut_protocol_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  SalutProtocol *self = SALUT_PROTOCOL (object);

  switch (property_id)
    {
      case PROP_BACKEND:
        g_value_set_gtype (value, self->priv->backend_type);
        break;

      case PROP_DNSSD_NAME:
        g_value_set_string (value, self->priv->dnssd_name);
        break;

      case PROP_ENGLISH_NAME:
        g_value_set_string (value, self->priv->english_name);
        break;

      case PROP_ICON_NAME:
        g_value_set_string (value, self->priv->icon_name);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_protocol_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  SalutProtocol *self = SALUT_PROTOCOL (object);

  switch (property_id)
    {
      case PROP_BACKEND:
        {
          GType type = g_value_get_gtype (value);

          if (type == G_TYPE_NONE)
#ifdef USE_BACKEND_AVAHI
            type = SALUT_TYPE_AVAHI_DISCOVERY_CLIENT;
#elif defined (USE_BACKEND_DUMMY)
            type = SALUT_TYPE_DUMMY_DISCOVERY_CLIENT;
#elif defined (USE_BACKEND_BONJOUR)
            type = SALUT_TYPE_BONJOUR_DISCOVERY_CLIENT;
#endif

          self->priv->backend_type = type;
        }
        break;

      case PROP_DNSSD_NAME:
        self->priv->dnssd_name = g_value_dup_string (value);
        break;

      case PROP_ENGLISH_NAME:
        self->priv->english_name = g_value_dup_string (value);
        break;

      case PROP_ICON_NAME:
        self->priv->icon_name = g_value_dup_string (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_protocol_finalize (GObject *object)
{
  SalutProtocol *self = SALUT_PROTOCOL (object);

  tp_clear_pointer (&self->priv->english_name, g_free);
  tp_clear_pointer (&self->priv->icon_name, g_free);
  tp_clear_pointer (&self->priv->dnssd_name, g_free);

  if (G_OBJECT_CLASS (salut_protocol_parent_class)->finalize)
    G_OBJECT_CLASS (salut_protocol_parent_class)->finalize (object);
}

static GPtrArray *
get_interfaces_array (TpBaseProtocol *self)
{
  GPtrArray *interfaces;

  interfaces = TP_BASE_PROTOCOL_CLASS (
      salut_protocol_parent_class)->get_interfaces_array (self);

  g_ptr_array_add (interfaces, TP_IFACE_PROTOCOL_INTERFACE_AVATARS1);
  g_ptr_array_add (interfaces, TP_IFACE_PROTOCOL_INTERFACE_PRESENCE1);

  return interfaces;
}

static void
get_avatar_details (TpBaseProtocol *base,
    GStrv *supported_mime_types,
    guint *min_height,
    guint *min_width,
    guint *rec_height,
    guint *rec_width,
    guint *max_height,
    guint *max_width,
    guint *max_bytes)
{
  salut_connection_dup_avatar_requirements (supported_mime_types, min_height,
      min_width, rec_height, rec_width, max_height, max_width, max_bytes);
}

static const TpPresenceStatusSpec *
get_presence_statuses (TpBaseProtocol *self)
{
  return salut_connection_get_presence_statuses ();
}

static void
salut_protocol_class_init (SalutProtocolClass *klass)
{
  TpBaseProtocolClass *base_class = (TpBaseProtocolClass *) klass;
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *param_spec;

  g_type_class_add_private (klass, sizeof (SalutProtocolPrivate));

  base_class->get_parameters = get_parameters;
  base_class->new_connection = new_connection;
  base_class->normalize_contact = normalize_contact;
  base_class->identify_account = identify_account;
  base_class->get_connection_details = get_connection_details;
  base_class->get_interfaces_array = get_interfaces_array;
  base_class->get_avatar_details = get_avatar_details;
  base_class->get_statuses = get_presence_statuses;

  object_class->get_property = salut_protocol_get_property;
  object_class->set_property = salut_protocol_set_property;
  object_class->finalize = salut_protocol_finalize;

  param_spec = g_param_spec_gtype ("backend-type", "backend type",
      "a G_TYPE_GTYPE of the backend to use", G_TYPE_NONE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_BACKEND,
      param_spec);

  param_spec = g_param_spec_string ("dnssd-name", "DNS-SD name",
      "The DNS-SD name of the protocol", "",
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_DNSSD_NAME,
      param_spec);

  param_spec = g_param_spec_string ("english-name", "English name",
      "The English name of the protocol", "",
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ENGLISH_NAME,
      param_spec);

  param_spec = g_param_spec_string ("icon-name", "Icon name",
      "The icon name of the protocol", "",
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ICON_NAME,
      param_spec);
}

TpBaseProtocol *
salut_protocol_new (GType backend_type,
    const gchar *dnssd_name,
    const gchar *protocol_name,
    const gchar *english_name,
    const gchar *icon_name)
{
  return g_object_new (SALUT_TYPE_PROTOCOL,
      "name", protocol_name,
      "dnssd-name", dnssd_name,
      "english-name", english_name,
      "backend-type", backend_type,
      "icon-name", icon_name,
      NULL);
}
