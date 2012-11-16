/*
 * avahi-olpc-activity.c - Source for SalutAvahiOlpcActivity
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
#include <avahi-gobject/ga-entry-group.h>
#include <avahi-gobject/ga-service-resolver.h>
#include <avahi-common/malloc.h>

#include "avahi-olpc-activity.h"

#define DEBUG_FLAG DEBUG_OLPC_ACTIVITY
#include "debug.h"

G_DEFINE_TYPE (SalutAvahiOlpcActivity, salut_avahi_olpc_activity,
    SALUT_TYPE_OLPC_ACTIVITY);

/* properties */
enum {
  PROP_CLIENT = 1,
  LAST_PROP
};

/* private structure */
typedef struct _SalutAvahiOlpcActivityPrivate SalutAvahiOlpcActivityPrivate;

struct _SalutAvahiOlpcActivityPrivate
{
  SalutAvahiDiscoveryClient *discovery_client;
  GSList *resolvers;
  /* group and service can be NULL if we are not announcing this activity */
  GaEntryGroup *group;
  GaEntryGroupService *service;

  gboolean dispose_has_run;
};

#define SALUT_AVAHI_OLPC_ACTIVITY_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_AVAHI_OLPC_ACTIVITY, SalutAvahiOlpcActivityPrivate))

static void
salut_avahi_olpc_activity_init (SalutAvahiOlpcActivity *obj)
{
  SalutAvahiOlpcActivityPrivate *priv = SALUT_AVAHI_OLPC_ACTIVITY_GET_PRIVATE (
      obj);

  priv->resolvers = NULL;
}

static void
salut_avahi_olpc_activity_get_property (GObject *object,
                                guint property_id,
                                GValue *value,
                                GParamSpec *pspec)
{
  SalutAvahiOlpcActivity *self = SALUT_AVAHI_OLPC_ACTIVITY (object);
  SalutAvahiOlpcActivityPrivate *priv = SALUT_AVAHI_OLPC_ACTIVITY_GET_PRIVATE (
      self);

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
salut_avahi_olpc_activity_set_property (GObject *object,
                                guint property_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
  SalutAvahiOlpcActivity *self = SALUT_AVAHI_OLPC_ACTIVITY (object);
  SalutAvahiOlpcActivityPrivate *priv = SALUT_AVAHI_OLPC_ACTIVITY_GET_PRIVATE (
      self);

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
activity_is_announced (SalutAvahiOlpcActivity *self)
{
  SalutAvahiOlpcActivityPrivate *priv = SALUT_AVAHI_OLPC_ACTIVITY_GET_PRIVATE (
      self);

  return (priv->group != NULL && priv->service != NULL);
}

static gboolean
update_activity_service (SalutAvahiOlpcActivity *self,
                         GError **error)
{
  SalutOlpcActivity *activity = SALUT_OLPC_ACTIVITY (self);
  SalutAvahiOlpcActivityPrivate *priv = SALUT_AVAHI_OLPC_ACTIVITY_GET_PRIVATE (
      self);
  GError *err = NULL;

  if (!activity_is_announced (self))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
        "Trying to update an activity that's not announced");
      return FALSE;
    }

  ga_entry_group_service_freeze (priv->service);

  if (activity->name != NULL)
    ga_entry_group_service_set (priv->service, "name",
        activity->name, NULL);

  if (activity->color != NULL)
    ga_entry_group_service_set (priv->service, "color",
        activity->color, NULL);

  if (activity->type != NULL)
    ga_entry_group_service_set (priv->service, "type",
        activity->type, NULL);

  if (activity->tags != NULL)
    ga_entry_group_service_set (priv->service, "tags",
        activity->tags, NULL);

  return ga_entry_group_service_thaw (priv->service, &err);
}

static gboolean
salut_avahi_olpc_activity_announce (SalutOlpcActivity *activity,
                                    GError **error)
{
  SalutAvahiOlpcActivity *self = SALUT_AVAHI_OLPC_ACTIVITY (activity);
  SalutAvahiOlpcActivityPrivate *priv = SALUT_AVAHI_OLPC_ACTIVITY_GET_PRIVATE (
      self);
  const gchar *room_name;
  gchar *name;
  AvahiStringList *txt_record;
  TpHandleRepoIface *room_repo;
  gchar *published_name;

  g_return_val_if_fail (!activity->is_private, FALSE);
  g_return_val_if_fail (!activity_is_announced (self), FALSE);

  room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) activity->connection, TP_HANDLE_TYPE_ROOM);

  room_name = tp_handle_inspect (room_repo, activity->room);
  /* caller should already have validated this */
  g_return_val_if_fail (room_name != NULL, FALSE);

  priv->group = ga_entry_group_new ();
  if (!ga_entry_group_attach (priv->group, priv->discovery_client->avahi_client,
        error))
    return FALSE;

  g_object_get (activity->connection, "published-name", &published_name, NULL);

  name = g_strdup_printf ("%s:%s@%s", room_name, published_name,
      avahi_client_get_host_name (
        priv->discovery_client->avahi_client->avahi_client));

  g_free (published_name);

  txt_record = avahi_string_list_new ("txtvers=0", NULL);
  txt_record = avahi_string_list_add_printf (txt_record, "room=%s", room_name);
  if (activity->id != NULL)
    txt_record = avahi_string_list_add_printf (txt_record, "activity-id=%s",
        activity->id);

  priv->service = ga_entry_group_add_service_strlist (priv->group, name,
      SALUT_DNSSD_OLPC_ACTIVITY, 0, error, txt_record);

  if (priv->service == NULL)
    return FALSE;

  DEBUG ("announce activity %s", name);
  g_free (name);
  avahi_string_list_free (txt_record);

  if (!ga_entry_group_commit (priv->group, error))
    return FALSE;

  /* announce activities properties */
  if (!update_activity_service (self, error))
    return FALSE;

  return TRUE;
}

static void
salut_avahi_olpc_activity_stop_announce (SalutOlpcActivity *activity)
{
  SalutAvahiOlpcActivity *self = SALUT_AVAHI_OLPC_ACTIVITY (activity);
  SalutAvahiOlpcActivityPrivate *priv = SALUT_AVAHI_OLPC_ACTIVITY_GET_PRIVATE (
      self);

  /* Announcing the activity could have failed, so check if we're actually
   * announcing it */
  if (!activity_is_announced (self))
    return;

  g_object_unref (priv->group);
  priv->group = NULL;
  priv->service = NULL;

  DEBUG ("stop announce activity %s", activity->id);
}

static gboolean
salut_avahi_update (SalutOlpcActivity *activity,
                    GError **error)
{
  SalutAvahiOlpcActivity *self = SALUT_AVAHI_OLPC_ACTIVITY (activity);

  return update_activity_service (self, error);
}

static void salut_avahi_olpc_activity_dispose (GObject *object);
static void salut_avahi_olpc_activity_finalize (GObject *object);

static void
salut_avahi_olpc_activity_class_init (
    SalutAvahiOlpcActivityClass *salut_avahi_olpc_activity_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_avahi_olpc_activity_class);
  SalutOlpcActivityClass *activity_class = SALUT_OLPC_ACTIVITY_CLASS (
      salut_avahi_olpc_activity_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_avahi_olpc_activity_class,
                              sizeof (SalutAvahiOlpcActivityPrivate));

  object_class->get_property = salut_avahi_olpc_activity_get_property;
  object_class->set_property = salut_avahi_olpc_activity_set_property;

  object_class->dispose = salut_avahi_olpc_activity_dispose;
  object_class->finalize = salut_avahi_olpc_activity_finalize;

  activity_class->announce = salut_avahi_olpc_activity_announce;
  activity_class->stop_announce = salut_avahi_olpc_activity_stop_announce;
  activity_class->update = salut_avahi_update;

  param_spec = g_param_spec_object (
      "discovery-client",
      "SalutAvahiDiscoveryClient object",
      "The Salut Avahi Discovery client associated with this manager",
      SALUT_TYPE_AVAHI_DISCOVERY_CLIENT,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CLIENT,
      param_spec);
}

void
salut_avahi_olpc_activity_dispose (GObject *object)
{
  SalutAvahiOlpcActivity *self = SALUT_AVAHI_OLPC_ACTIVITY (object);
  SalutAvahiOlpcActivityPrivate *priv = SALUT_AVAHI_OLPC_ACTIVITY_GET_PRIVATE (
      self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_slist_foreach (priv->resolvers, (GFunc) g_object_unref, NULL);
  g_slist_free (priv->resolvers);
  priv->resolvers = NULL;

  if (priv->group != NULL)
    {
      g_object_unref (priv->group);
      priv->group = NULL;
    }

  if (priv->discovery_client != NULL)
    {
      g_object_unref (priv->discovery_client);
      priv->discovery_client = NULL;
    }

  if (G_OBJECT_CLASS (salut_avahi_olpc_activity_parent_class)->dispose)
    G_OBJECT_CLASS (salut_avahi_olpc_activity_parent_class)->dispose (object);
}

void
salut_avahi_olpc_activity_finalize (GObject *object)
{
  G_OBJECT_CLASS (salut_avahi_olpc_activity_parent_class)->finalize (object);
}

SalutAvahiOlpcActivity *
salut_avahi_olpc_activity_new (SalutConnection *connection,
                               SalutAvahiDiscoveryClient *discovery_client)
{
  return g_object_new (SALUT_TYPE_AVAHI_OLPC_ACTIVITY,
      "connection", connection,
      "discovery-client", discovery_client,
      NULL);
}

struct resolverinfo
{
  AvahiIfIndex interface;
  AvahiProtocol protocol;
  const gchar *name;
  const gchar *type;
  const gchar *domain;
};

static gint
compare_resolver (GaServiceResolver *resolver,
                  struct resolverinfo *info)
{
  AvahiIfIndex interface;
  AvahiProtocol protocol;
  gchar *name;
  gchar *type;
  gchar *domain;
  gint result;

  g_object_get (resolver,
      "interface", &interface,
      "protocol", &protocol,
      "name", &name,
      "type", &type,
      "domain", &domain,
      NULL);

  if (interface == info->interface
      && protocol == info->protocol
      && !tp_strdiff (name, info->name)
      && !tp_strdiff (type, info->type)
      && !tp_strdiff (domain, info->domain))
    {
      result = 0;
    }
  else
    {
      result = 1;
    }

  g_free (name);
  g_free (type);
  g_free (domain);
  return result;
}

static GaServiceResolver *
find_resolver (SalutAvahiOlpcActivity *self,
               AvahiIfIndex interface,
               AvahiProtocol protocol,
               const gchar *name,
               const gchar *type,
               const gchar *domain)
{
  SalutAvahiOlpcActivityPrivate *priv = SALUT_AVAHI_OLPC_ACTIVITY_GET_PRIVATE (
      self);
  struct resolverinfo info;
  GSList *ret;

  info.interface = interface;
  info.protocol = protocol;
  info.name = name;
  info.type = type;
  info.domain = domain;
  ret = g_slist_find_custom (priv->resolvers, &info,
      (GCompareFunc) compare_resolver);

  return ret ? GA_SERVICE_RESOLVER (ret->data) : NULL;
}

static void
activity_resolved_cb (GaServiceResolver *resolver,
                      AvahiIfIndex interface,
                      AvahiProtocol protocol,
                      gchar *name,
                      gchar *type,
                      gchar *domain,
                      gchar *host_name,
                      AvahiAddress *a,
                      gint port,
                      AvahiStringList *txt,
                      AvahiLookupResultFlags flags,
                      SalutAvahiOlpcActivity *self)
{
  SalutOlpcActivity *act = SALUT_OLPC_ACTIVITY (self);
  AvahiStringList *t;
  char *activity_id = NULL;
  char *color = NULL;
  char *activity_name = NULL;
  char *activity_type = NULL;
  char *tags = NULL;
  char *room_name = NULL;
  TpHandle room = 0;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles
    ((TpBaseConnection *) act->connection, TP_HANDLE_TYPE_ROOM);

  DEBUG ("called: \"%s\".%s. on %s port %u", name, domain, host_name, port);

  if ((t = avahi_string_list_find (txt, "txtvers")) != NULL)
    {
      char *txtvers;

      avahi_string_list_get_pair (t, NULL, &txtvers, NULL);
      if (tp_strdiff (txtvers, "0"))
        {
          DEBUG ("Ignoring record with txtvers not 0: %s",
              txtvers ? txtvers : "(no value)");
          avahi_free (txtvers);
          return;
        }
      avahi_free (txtvers);
    }

  if ((t = avahi_string_list_find (txt, "room")) != NULL)
    {
      avahi_string_list_get_pair (t, NULL, &room_name, NULL);

      room = tp_handle_ensure (room_repo, room_name, NULL, NULL);
      avahi_free (room_name);
      if (room == 0)
        {
          DEBUG ("Ignoring record with invalid room name: %s", room_name);
          return;
        }
    }

  if ((t = avahi_string_list_find (txt, "activity-id")) != NULL)
    {
      avahi_string_list_get_pair (t, NULL, &activity_id, NULL);
    }

  if ((t = avahi_string_list_find (txt, "color")) != NULL)
    {
      avahi_string_list_get_pair (t, NULL, &color, NULL);
    }

  if ((t = avahi_string_list_find (txt, "name")) != NULL)
    {
      avahi_string_list_get_pair (t, NULL, &activity_name, NULL);
    }

  if ((t = avahi_string_list_find (txt, "type")) != NULL)
    {
      avahi_string_list_get_pair (t, NULL, &activity_type, NULL);
    }

  if ((t = avahi_string_list_find (txt, "tags")) != NULL)
    {
      avahi_string_list_get_pair (t, NULL, &tags, NULL);
    }

  salut_olpc_activity_update (SALUT_OLPC_ACTIVITY (self), room,
      activity_id, activity_name, activity_type, color, tags, FALSE);

  tp_handle_unref (room_repo, room);
  avahi_free (activity_id);
  avahi_free (activity_type);
  avahi_free (activity_name);
  avahi_free (color);
  avahi_free (tags);
}

void
salut_avahi_olpc_activity_add_service (SalutAvahiOlpcActivity *self,
                                       AvahiIfIndex interface,
                                       AvahiProtocol protocol,
                                       const char *name,
                                       const char *type,
                                       const char *domain)
{
  SalutAvahiOlpcActivityPrivate *priv = SALUT_AVAHI_OLPC_ACTIVITY_GET_PRIVATE (
      self);
  GaServiceResolver *resolver;
  GError *error = NULL;

  resolver = find_resolver (self, interface, protocol, name, type, domain);
  if (resolver != NULL)
    return;

  resolver = ga_service_resolver_new (interface, protocol, name, type, domain,
      protocol, 0);

  g_signal_connect (resolver, "found", G_CALLBACK (activity_resolved_cb),
      self);

  if (!ga_service_resolver_attach (resolver,
        priv->discovery_client->avahi_client, &error))
    {
      g_warning ("Failed to attach resolver: %s", error->message);
      g_error_free (error);
    }

  /* DEBUG_RESOLVER (contact, resolver, "added"); */
  priv->resolvers = g_slist_prepend (priv->resolvers, resolver);
}

void
salut_avahi_olpc_activity_remove_service (SalutAvahiOlpcActivity *self,
                                          AvahiIfIndex interface,
                                          AvahiProtocol protocol,
                                          const char *name,
                                          const char *type,
                                          const char *domain)
{
  SalutAvahiOlpcActivityPrivate *priv = SALUT_AVAHI_OLPC_ACTIVITY_GET_PRIVATE (
      self);
  GaServiceResolver *resolver;

  resolver = find_resolver (self, interface, protocol, name, type, domain);

  if (resolver == NULL)
    return;

  priv->resolvers = g_slist_remove (priv->resolvers, resolver);
  g_object_unref (resolver);
}
