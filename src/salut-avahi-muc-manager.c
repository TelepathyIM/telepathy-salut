/*
 * salut-avahi-muc-manager.c - Source for SalutAvahiMucManager
 * Copyright (C) 2006 Collabora Ltd.
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
#include <arpa/inet.h>

#include "salut-avahi-muc-manager.h"

#include <avahi-gobject/ga-service-browser.h>
#include <avahi-gobject/ga-service-resolver.h>

#include <gibber/gibber-muc-connection.h>
#include <gibber/gibber-namespaces.h>
#include <gibber/gibber-xmpp-error.h>

#include "salut-muc-channel.h"
#include "salut-contact-manager.h"
#include "salut-tubes-channel.h"
#include "salut-roomlist-channel.h"
#include "salut-xmpp-connection-manager.h"
#include "salut-avahi-muc-channel.h"

#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG DEBUG_MUC
#include "debug.h"

G_DEFINE_TYPE (SalutAvahiMucManager, salut_avahi_muc_manager,
    SALUT_TYPE_MUC_MANAGER);

/* properties */
enum {
  PROP_CLIENT = 1,
  LAST_PROP
};

/* private structure */
typedef struct _SalutAvahiMucManagerPrivate SalutAvahiMucManagerPrivate;

struct _SalutAvahiMucManagerPrivate
{
  SalutAvahiDiscoveryClient *discovery_client;
  GaServiceBrowser *browser;

  /* room name => (GaServiceResolver *) */
  GHashTable *room_resolvers;

  gboolean dispose_has_run;
};

#define SALUT_AVAHI_MUC_MANAGER_GET_PRIVATE(obj) \
    ((SalutAvahiMucManagerPrivate *) ((SalutAvahiMucManager *)obj)->priv)

static void
room_resolver_removed (gpointer data)
{
  GArray *arr = (GArray *) data;
  int i;
  for (i = 0; i < arr->len; i++)
    {
      g_object_unref (g_array_index (arr, GObject *, i));
    }
  g_array_free (arr, TRUE);
}

static void
salut_avahi_muc_manager_init (SalutAvahiMucManager *self)
{
  SalutAvahiMucManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_AVAHI_MUC_MANAGER, SalutAvahiMucManagerPrivate);

  self->priv = priv;
  priv->browser = ga_service_browser_new (SALUT_DNSSD_CLIQUE);

  priv->room_resolvers = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, room_resolver_removed);
}

static void
salut_avahi_muc_manager_get_property (GObject *object,
                                      guint property_id,
                                      GValue *value,
                                      GParamSpec *pspec)
{
  SalutAvahiMucManager *self = SALUT_AVAHI_MUC_MANAGER (object);
  SalutAvahiMucManagerPrivate *priv = SALUT_AVAHI_MUC_MANAGER_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CLIENT:
        g_value_set_object (value, priv->discovery_client);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_avahi_muc_manager_set_property (GObject *object,
                                      guint property_id,
                                      const GValue *value,
                                      GParamSpec *pspec)
{
  SalutAvahiMucManager *self = SALUT_AVAHI_MUC_MANAGER (object);
  SalutAvahiMucManagerPrivate *priv = SALUT_AVAHI_MUC_MANAGER_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CLIENT:
        priv->discovery_client = g_value_get_object (value);
        g_object_ref (priv->discovery_client);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void salut_avahi_muc_manager_dispose (GObject *object);

static void
browser_found (GaServiceBrowser *browser,
               AvahiIfIndex interface,
               AvahiProtocol protocol,
               const char *name,
               const char *type,
               const char *domain,
               GaLookupResultFlags flags,
               SalutAvahiMucManager *self)
{
  SalutAvahiMucManagerPrivate *priv = SALUT_AVAHI_MUC_MANAGER_GET_PRIVATE (self);
  GArray *arr;
  GaServiceResolver *resolver;
  GError *error = NULL;

  DEBUG ("found room: %s.%s.%s", name, type, domain);
  resolver = ga_service_resolver_new (interface, protocol,
      name, type, domain, protocol, 0);

  if (!ga_service_resolver_attach (resolver,
        priv->discovery_client->avahi_client, &error))
    {
      DEBUG ("resolver attach failed: %s", error->message);
      g_object_unref (resolver);
      g_error_free (error);
      return;
    }

  arr = g_hash_table_lookup (priv->room_resolvers, name);
  if (arr == NULL)
    {
      arr = g_array_new (FALSE, FALSE, sizeof (GObject *));
      g_hash_table_insert (priv->room_resolvers, g_strdup (name), arr);
      salut_muc_manager_room_discovered (SALUT_MUC_MANAGER (self),
          name);
    }
  g_array_append_val (arr, resolver);
}

static void
browser_removed (GaServiceBrowser *browser,
                 AvahiIfIndex interface,
                 AvahiProtocol protocol,
                 const char *name,
                 const char *type,
                 const char *domain,
                 GaLookupResultFlags flags,
                 SalutAvahiMucManager *self)
{
  SalutAvahiMucManagerPrivate *priv = SALUT_AVAHI_MUC_MANAGER_GET_PRIVATE (self);
  GArray *arr;
  int i;

  arr = g_hash_table_lookup (priv->room_resolvers, name);

  if (arr == NULL) {
    DEBUG ("Browser removed for %s, but didn't have any resolvers", name);
    return;
  }

  for (i = 0; i < arr->len; i++)
    {
      GaServiceResolver *resolver;
      AvahiIfIndex r_interface;
      AvahiProtocol r_protocol;
      gchar *r_name;
      gchar *r_type;
      gchar *r_domain;

      resolver = g_array_index (arr, GaServiceResolver *, i);
      g_object_get ((gpointer) resolver,
          "interface", &r_interface,
          "protocol", &r_protocol,
          "name", &r_name,
          "type", &r_type,
          "domain", &r_domain,
          NULL);
      if (interface == r_interface
          && protocol == r_protocol
          && !tp_strdiff (name, r_name)
          && !tp_strdiff (type, r_type)
          && !tp_strdiff (domain, r_domain))
        {
          g_free (r_name);
          g_free (r_type);
          g_free (r_domain);
          g_object_unref (resolver);
          g_array_remove_index_fast (arr, i);
          break;
        }

      g_free (r_name);
      g_free (r_type);
      g_free (r_domain);
    }

  if (arr->len > 0)
    return;

  DEBUG ("remove room: %s.%s.%s", name, type, domain);

  g_hash_table_remove (priv->room_resolvers, name);

  salut_muc_manager_room_removed (SALUT_MUC_MANAGER (self), name);
}

static void
browser_failed (GaServiceBrowser *browser,
                GError *error,
                SalutAvahiMucManager *self)
{
  /* FIXME proper error handling */
  DEBUG ("browser failed -> %s", error->message);
}

static gboolean
salut_avahi_muc_manager_start (SalutMucManager *mgr,
                               GError **error)
{
  SalutAvahiMucManager *self = SALUT_AVAHI_MUC_MANAGER (mgr);
  SalutAvahiMucManagerPrivate *priv = SALUT_AVAHI_MUC_MANAGER_GET_PRIVATE (self);

  g_signal_connect (priv->browser, "new-service",
      G_CALLBACK (browser_found), self);
  g_signal_connect (priv->browser, "removed-service",
      G_CALLBACK (browser_removed), self);
  g_signal_connect (priv->browser, "failure",
      G_CALLBACK (browser_failed), self);

  if (!ga_service_browser_attach (priv->browser,
        priv->discovery_client->avahi_client, error))
    {
      DEBUG ("browser attach failed");
      return FALSE;
   }

  return TRUE;
}

static gchar *
_avahi_address_to_string_address (const AvahiAddress *address)
{
  gchar str[AVAHI_ADDRESS_STR_MAX];

  if (avahi_address_snprint (str, sizeof (str), address) == NULL)
    {
      DEBUG ("Failed to convert AvahiAddress to string");
      return NULL;
    }
  return g_strdup (str);
}

static gboolean
salut_avahi_muc_manager_find_muc_address (SalutMucManager *mgr,
                                          const gchar *name,
                                          gchar **address,
                                          guint16 *port)
{
  SalutAvahiMucManager *self = SALUT_AVAHI_MUC_MANAGER (mgr);
  SalutAvahiMucManagerPrivate *priv = SALUT_AVAHI_MUC_MANAGER_GET_PRIVATE (self);
  GArray *arr;
  AvahiAddress avahi_address;
  guint i;

  arr = g_hash_table_lookup (priv->room_resolvers, name);
  if (arr == NULL || arr->len == 0)
    return FALSE;

  for (i = 0; i < arr->len; i++)
    {
      GaServiceResolver *resolver;
      resolver = g_array_index (arr, GaServiceResolver *, i);

       if (!ga_service_resolver_get_address (resolver, &avahi_address, port))
         {
           DEBUG ("..._get_address failed: creating a new MUC room instead");
           return FALSE;
         }
       else
         {
           *address = _avahi_address_to_string_address (&avahi_address);
           return TRUE;
        }
    }

  return FALSE;
}

static void
add_room_to_list (const gchar *room,
                  GaServiceResolver *resolver,
                  GSList **list)
{
  *list = g_slist_prepend (*list, (gchar *) room);
}

static GSList *
salut_avahi_muc_manager_get_rooms (SalutMucManager *mgr)
{
  SalutAvahiMucManager *self = SALUT_AVAHI_MUC_MANAGER (mgr);
  SalutAvahiMucManagerPrivate *priv = SALUT_AVAHI_MUC_MANAGER_GET_PRIVATE (self);
  GSList *rooms = NULL;

  g_hash_table_foreach (priv->room_resolvers, (GHFunc) add_room_to_list,
      &rooms);

  return rooms;
}

static SalutMucChannel * 
salut_avahi_muc_manager_create_muc_channel (
    SalutMucManager *mgr,
    SalutConnection *connection,
    const gchar *path,
    GibberMucConnection *muc_connection,
    TpHandle handle,
    const gchar *name,
    gboolean creator,
    SalutXmppConnectionManager *xcm)
{
  SalutAvahiMucManager *self = SALUT_AVAHI_MUC_MANAGER (mgr);
  SalutAvahiMucManagerPrivate *priv = SALUT_AVAHI_MUC_MANAGER_GET_PRIVATE (self);

  return SALUT_MUC_CHANNEL (salut_avahi_muc_channel_new (connection,
        path, muc_connection, handle, name, priv->discovery_client, creator,
        xcm));
}

static void
salut_avahi_muc_manager_class_init (
    SalutAvahiMucManagerClass *salut_avahi_muc_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_avahi_muc_manager_class);
  SalutMucManagerClass *muc_manager_class = SALUT_MUC_MANAGER_CLASS (
      salut_avahi_muc_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_avahi_muc_manager_class,
                              sizeof (SalutAvahiMucManagerPrivate));

  object_class->get_property = salut_avahi_muc_manager_get_property;
  object_class->set_property = salut_avahi_muc_manager_set_property;

  object_class->dispose = salut_avahi_muc_manager_dispose;

  muc_manager_class->start = salut_avahi_muc_manager_start;
  muc_manager_class->find_muc_address =
    salut_avahi_muc_manager_find_muc_address;
  muc_manager_class->get_rooms = salut_avahi_muc_manager_get_rooms;
  muc_manager_class->create_muc_channel =
    salut_avahi_muc_manager_create_muc_channel;

  param_spec = g_param_spec_object (
      "discovery-client",
      "SalutAvahiDiscoveryClient object",
      "The Salut Avahi Discovery client associated with this muc manager",
      SALUT_TYPE_AVAHI_DISCOVERY_CLIENT,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CLIENT,
      param_spec);
}

static void
salut_avahi_muc_manager_dispose (GObject *object)
{
  SalutAvahiMucManager *self = SALUT_AVAHI_MUC_MANAGER (object);
  SalutAvahiMucManagerPrivate *priv = SALUT_AVAHI_MUC_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->room_resolvers != NULL)
    {
      g_hash_table_destroy (priv->room_resolvers);
      priv->room_resolvers = NULL;
    }

  if (priv->browser != NULL)
    {
      g_object_unref (priv->browser);
      priv->browser = NULL;
    }

  if (priv->discovery_client != NULL)
    {
      g_object_unref (priv->discovery_client);
      priv->discovery_client = NULL;
    }

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_avahi_muc_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_avahi_muc_manager_parent_class)->dispose (object);
}

/* public functions */
SalutAvahiMucManager *
salut_avahi_muc_manager_new (SalutConnection *connection,
                             SalutXmppConnectionManager *xmpp_connection_manager,
                             SalutAvahiDiscoveryClient *discovery_client)
{
  g_assert (connection != NULL);
  g_assert (xmpp_connection_manager != NULL);
  g_assert (discovery_client != NULL);

  return g_object_new (SALUT_TYPE_AVAHI_MUC_MANAGER,
      "connection", connection,
      "xmpp-connection-manager", xmpp_connection_manager,
      "discovery-client", discovery_client,
      NULL);
}
