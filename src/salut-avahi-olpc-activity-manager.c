/*
 * salut-avahi-avahi_olpc-activity-manager.c - Source for
 * SalutAvahiOlpcActivityManager
 * Copyright (C) 2008 Collabora Ltd.
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

#include <avahi-gobject/ga-service-browser.h>

#include "salut-avahi-olpc-activity-manager.h"
#include "salut-avahi-olpc-activity.h"
#include "salut-avahi-discovery-client.h"
#include "salut-olpc-activity.h"

#define DEBUG_FLAG DEBUG_OLPC_ACTIVITY
#include "debug.h"

G_DEFINE_TYPE (SalutAvahiOlpcActivityManager, salut_avahi_olpc_activity_manager,
    SALUT_TYPE_OLPC_ACTIVITY_MANAGER);

/* properties */
enum {
  PROP_CLIENT = 1,
  LAST_PROP
};

/* private structure */
typedef struct _SalutAvahiOlpcActivityManagerPrivate SalutAvahiOlpcActivityManagerPrivate;

struct _SalutAvahiOlpcActivityManagerPrivate
{
  SalutAvahiDiscoveryClient *discovery_client;
  GaServiceBrowser *browser;

  gboolean dispose_has_run;
};

#define SALUT_AVAHI_OLPC_ACTIVITY_MANAGER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_AVAHI_OLPC_ACTIVITY_MANAGER, SalutAvahiOlpcActivityManagerPrivate))

static void
salut_avahi_olpc_activity_manager_init (SalutAvahiOlpcActivityManager *self)
{
  SalutAvahiOlpcActivityManagerPrivate *priv =
    SALUT_AVAHI_OLPC_ACTIVITY_MANAGER_GET_PRIVATE (obj);

  priv->browser = ga_service_browser_new (SALUT_DNSSD_OLPC_ACTIVITY);
}

static void
salut_avahi_olpc_activity_manager_get_property (GObject *object,
                                                guint property_id,
                                                GValue *value,
                                                GParamSpec *pspec)
{
  SalutAvahiOlpcActivityManager *self = SALUT_AVAHI_OLPC_ACTIVITY_MANAGER (object);
  SalutAvahiOlpcActivityManagerPrivate *priv = SALUT_AVAHI_OLPC_ACTIVITY_MANAGER_GET_PRIVATE (self);

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
salut_avahi_olpc_activity_manager_set_property (GObject *object,
                                                guint property_id,
                                                const GValue *value,
                                                GParamSpec *pspec)
{
  SalutAvahiOlpcActivityManager *self = SALUT_AVAHI_OLPC_ACTIVITY_MANAGER (object);
  SalutAvahiOlpcActivityManagerPrivate *priv = SALUT_AVAHI_OLPC_ACTIVITY_MANAGER_GET_PRIVATE (self);

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

static gboolean
split_activity_name (const gchar *name,
                     gchar **room_name,
                     gchar **contact_name)
{
  gchar **tmp;

  tmp = g_strsplit (name, ":", 2);
  if (tmp[0] == NULL || tmp[1] == NULL)
    {
      DEBUG ("Ignoring invalid OLPC activity DNS-SD with no ':': %s", name);
      return FALSE;
    }

  if (room_name != NULL)
    *room_name = g_strdup (tmp[0]);
  if (contact_name != NULL)
  *contact_name = g_strdup (tmp[1]);

  g_strfreev (tmp);
  return TRUE;
}

static void
browser_found (GaServiceBrowser *browser,
               AvahiIfIndex interface,
               AvahiProtocol protocol,
               const char *name,
               const char *type,
               const char *domain,
               GaLookupResultFlags flags,
               SalutAvahiOlpcActivityManager *self)
{
  SalutOlpcActivityManager *mgr = SALUT_OLPC_ACTIVITY_MANAGER (self);
  SalutOlpcActivity *activity;
  gchar *room_name = NULL;
  gchar *contact_name = NULL;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles
    ((TpBaseConnection *) mgr->connection, TP_HANDLE_TYPE_ROOM);
  TpHandle room;
  GError *error = NULL;
  SalutContactManager *contact_manager;
  SalutContact *contact;

  if (flags & AVAHI_LOOKUP_RESULT_OUR_OWN)
    return;

  if (!split_activity_name (name, &room_name, &contact_name))
    return;

  room = tp_handle_ensure (room_repo, room_name, NULL, &error);
  if (room == 0)
    {
      DEBUG ("invalid room name %s: %s", room_name, error->message);
      g_free (room_name);
      g_free (contact_name);
      return;
    }

  activity = salut_olpc_activity_manager_ensure_activity_by_room (mgr,
      room);

  salut_avahi_olpc_activity_add_service (SALUT_AVAHI_OLPC_ACTIVITY (activity),
      interface, protocol, name, type, domain);

  g_object_get (mgr->connection,
      "contact-manager", &contact_manager, NULL);
  g_assert (contact_manager != NULL);

  contact = salut_contact_manager_ensure_contact (contact_manager,
      contact_name);
  salut_olpc_activity_manager_contact_joined (mgr, contact, activity);

  g_object_unref (activity);
  tp_handle_unref (room_repo, room);
  g_free (contact_name);
  g_free (room_name);
  g_object_unref (contact);
  g_object_unref (contact_manager);
}

static void
browser_removed (GaServiceBrowser *browser,
                 AvahiIfIndex interface,
                 AvahiProtocol protocol,
                 const char *name,
                 const char *type,
                 const char *domain,
                 GaLookupResultFlags flags,
                 SalutAvahiOlpcActivityManager *self)
{
  SalutOlpcActivityManager *mgr = SALUT_OLPC_ACTIVITY_MANAGER (self);
  SalutOlpcActivity *activity;
  gchar *room_name = NULL;
  gchar *contact_name = NULL;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles
    ((TpBaseConnection *) mgr->connection, TP_HANDLE_TYPE_ROOM);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles
    ((TpBaseConnection *) mgr->connection, TP_HANDLE_TYPE_CONTACT);
  TpHandle room;
  TpHandle contact_handle;
  GError *error = NULL;
  SalutContactManager *contact_manager;
  SalutContact *contact;

  if (!split_activity_name (name, &room_name, &contact_name))
    return;

  room = tp_handle_ensure (room_repo, room_name, NULL, &error);
  g_free (room_name);
  if (room == 0)
    {
      DEBUG ("invalid room name %s: %s", room_name, error->message);
      g_free (contact_name);
      g_error_free (error);
      return;
    }

  contact_handle = tp_handle_ensure (contact_repo, contact_name, NULL, &error);
  if (contact_handle == 0)
    {
      DEBUG ("Invalid contact name %s: %s", contact_name, error->message);
      g_error_free (error);
      g_free (contact_name);
      tp_handle_unref (room_repo, room);
      return;
    }
  g_free (contact_name);

  activity = salut_olpc_activity_manager_get_activity_by_room (mgr, room);
  tp_handle_unref (room_repo, room);
  if (activity == NULL)
    {
      tp_handle_unref (contact_repo, contact_handle);
      return;
    }

  salut_avahi_olpc_activity_remove_service (SALUT_AVAHI_OLPC_ACTIVITY (activity),
      interface, protocol, name, type, domain);

  g_object_get (mgr->connection,
      "contact-manager", &contact_manager, NULL);
  g_assert (contact_manager != NULL);

  contact = salut_contact_manager_get_contact (contact_manager,
      contact_handle);
  tp_handle_unref (contact_repo, contact_handle);
  g_object_unref (contact_manager);
  if (contact == NULL)
    return;

  salut_olpc_activity_manager_contact_left (mgr, contact, activity);
  g_object_unref (contact);
}

static void
browser_failed (GaServiceBrowser *browser,
                GError *error,
                SalutAvahiOlpcActivityManager *self)
{
  g_warning ("browser failed -> %s", error->message);
}

static gboolean
salut_avahi_olpc_activity_manager_start (SalutOlpcActivityManager *mgr,
                                         GError **error)
{
  SalutAvahiOlpcActivityManager *self = SALUT_AVAHI_OLPC_ACTIVITY_MANAGER (mgr);
  SalutAvahiOlpcActivityManagerPrivate *priv =
    SALUT_AVAHI_OLPC_ACTIVITY_MANAGER_GET_PRIVATE (self);

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

static SalutOlpcActivity *
salut_avahi_olpc_activity_manager_create_activity (
    SalutOlpcActivityManager *mgr)
{
  SalutAvahiOlpcActivityManager *self = SALUT_AVAHI_OLPC_ACTIVITY_MANAGER (mgr);
  SalutAvahiOlpcActivityManagerPrivate *priv =
    SALUT_AVAHI_OLPC_ACTIVITY_MANAGER_GET_PRIVATE (self);

  return SALUT_OLPC_ACTIVITY (salut_avahi_olpc_activity_new (
        mgr->connection, priv->discovery_client));
}

static void salut_avahi_olpc_activity_manager_dispose (GObject *object);

static void
salut_avahi_olpc_activity_manager_class_init (SalutAvahiOlpcActivityManagerClass *salut_avahi_olpc_activity_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_avahi_olpc_activity_manager_class);
  SalutOlpcActivityManagerClass *activity_manager_class = SALUT_OLPC_ACTIVITY_MANAGER_CLASS (
      salut_avahi_olpc_activity_manager_class);

  GParamSpec *param_spec;

  g_type_class_add_private (salut_avahi_olpc_activity_manager_class,
                              sizeof (SalutAvahiOlpcActivityManagerPrivate));

  object_class->get_property = salut_avahi_olpc_activity_manager_get_property;
  object_class->set_property = salut_avahi_olpc_activity_manager_set_property;

  object_class->dispose = salut_avahi_olpc_activity_manager_dispose;

  activity_manager_class->start = salut_avahi_olpc_activity_manager_start;
  activity_manager_class->create_activity =
    salut_avahi_olpc_activity_manager_create_activity;

  param_spec = g_param_spec_object (
      "discovery-client",
      "SalutAvahiDiscoveryClient object",
      "The Salut Avahi Discovery client associated with this manager",
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
salut_avahi_olpc_activity_manager_dispose (GObject *object)
{
  SalutAvahiOlpcActivityManager *self = SALUT_AVAHI_OLPC_ACTIVITY_MANAGER (object);
  SalutAvahiOlpcActivityManagerPrivate *priv = SALUT_AVAHI_OLPC_ACTIVITY_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->discovery_client != NULL)
    {
      g_object_unref (priv->discovery_client);
      priv->discovery_client = NULL;
    }

  if (priv->browser != NULL)
    {
      g_object_unref (priv->browser);
      priv->browser = NULL;
    }

  if (G_OBJECT_CLASS (salut_avahi_olpc_activity_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_avahi_olpc_activity_manager_parent_class)->dispose (object);
}

SalutAvahiOlpcActivityManager *
salut_avahi_olpc_activity_manager_new (SalutConnection *connection,
                                       SalutAvahiDiscoveryClient *discovery_client)
{
  return g_object_new (SALUT_TYPE_AVAHI_OLPC_ACTIVITY_MANAGER,
      "connection", connection,
      "discovery-client", discovery_client,
      NULL);
}
