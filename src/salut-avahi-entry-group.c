/*
 * salut-avahi-entry-group.c - Source for SalutAvahiEntryGroup
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

#include <dbus/dbus-glib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "salut-avahi-errors.h"
#include "salut-avahi-entry-group.h"
#include "salut-avahi-entry-group-signals-marshal.h"
#include "salut-avahi-entry-group-enumtypes.h"

G_DEFINE_TYPE(SalutAvahiEntryGroup, salut_avahi_entry_group, G_TYPE_OBJECT)

static void _free_service(gpointer data);

/* signal enum */
enum
{
  STATE_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum {
  PROP_STATE = 1
};

/* private structures */
typedef struct _SalutAvahiEntryGroupPrivate SalutAvahiEntryGroupPrivate;

struct _SalutAvahiEntryGroupPrivate
{
  SalutAvahiEntryGroupState state; 
  SalutAvahiClient *client;
  AvahiEntryGroup *group;
  GHashTable *services;
  gboolean dispose_has_run;
};

typedef struct _SalutAvahiEntryGroupServicePrivate 
                SalutAvahiEntryGroupServicePrivate;

struct _SalutAvahiEntryGroupServicePrivate {
  SalutAvahiEntryGroupService public;
  SalutAvahiEntryGroup *group;
  gboolean frozen;
  GHashTable *entries;
};

#define SALUT_AVAHI_ENTRY_GROUP_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_AVAHI_ENTRY_GROUP, SalutAvahiEntryGroupPrivate))

static void
salut_avahi_entry_group_init (SalutAvahiEntryGroup *obj)
{
  SalutAvahiEntryGroupPrivate *priv = SALUT_AVAHI_ENTRY_GROUP_GET_PRIVATE (obj);
  /* allocate any data required by the object here */
  priv->state = SALUT_AVAHI_ENTRY_GROUP_STATE_UNCOMMITED;
  priv->client = NULL;
  priv->group = NULL;
  priv->services = g_hash_table_new_full(g_direct_hash,
                                         g_direct_equal,
                                         NULL,
                                         _free_service);
}

static void salut_avahi_entry_group_dispose (GObject *object);
static void salut_avahi_entry_group_finalize (GObject *object);

static void
salut_avahi_entry_group_get_property (GObject *object,
                                      guint property_id,
                                      GValue *value,
                                      GParamSpec *pspec) {
 SalutAvahiEntryGroup *group = SALUT_AVAHI_ENTRY_GROUP(object);
 SalutAvahiEntryGroupPrivate *priv = SALUT_AVAHI_ENTRY_GROUP_GET_PRIVATE(group);

 switch (property_id) {
   case PROP_STATE:
     g_value_set_enum(value, priv->state);
     break;
   default:
     G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
     break;
 }
}

static void
salut_avahi_entry_group_class_init (SalutAvahiEntryGroupClass *salut_avahi_entry_group_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_avahi_entry_group_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_avahi_entry_group_class, sizeof (SalutAvahiEntryGroupPrivate));

  object_class->dispose = salut_avahi_entry_group_dispose;
  object_class->finalize = salut_avahi_entry_group_finalize;
  object_class->get_property = salut_avahi_entry_group_get_property;

  param_spec = g_param_spec_enum("state", "Entry Group state",
                                 "The state of the salut avahi entry group",
                                 SALUT_TYPE_AVAHI_ENTRY_GROUP_STATE,
                                 SALUT_AVAHI_ENTRY_GROUP_STATE_UNCOMMITED,
                                 G_PARAM_READABLE  |
                                 G_PARAM_STATIC_NAME |
                                 G_PARAM_STATIC_BLURB);
  g_object_class_install_property(object_class, PROP_STATE, param_spec);

  signals[STATE_CHANGED] = 
    g_signal_new("state-changed",
                 G_OBJECT_CLASS_TYPE(salut_avahi_entry_group_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 salut_avahi_entry_group_marshal_VOID__INT,
                 G_TYPE_NONE, 1, SALUT_TYPE_AVAHI_ENTRY_GROUP_STATE);
}

void
salut_avahi_entry_group_dispose (GObject *object)
{
  SalutAvahiEntryGroup *self = SALUT_AVAHI_ENTRY_GROUP (object);
  SalutAvahiEntryGroupPrivate *priv = 
    SALUT_AVAHI_ENTRY_GROUP_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;
  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */
  if (priv->group) {
    avahi_entry_group_free(priv->group);
    priv->group = NULL;
  }

  if (priv->client) {
    g_object_unref(priv->client);
    priv->client = NULL;
  }

  if (G_OBJECT_CLASS (salut_avahi_entry_group_parent_class)->dispose)
    G_OBJECT_CLASS (salut_avahi_entry_group_parent_class)->dispose (object);
}

void
salut_avahi_entry_group_finalize (GObject *object)
{
  SalutAvahiEntryGroup *self = SALUT_AVAHI_ENTRY_GROUP (object);
  SalutAvahiEntryGroupPrivate *priv = SALUT_AVAHI_ENTRY_GROUP_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  g_hash_table_destroy(priv->services);
  priv->services = NULL;

  G_OBJECT_CLASS (salut_avahi_entry_group_parent_class)->finalize (object);
}

static 
void _free_service (gpointer data) {
  SalutAvahiEntryGroupService *s = (SalutAvahiEntryGroupService *)data;
  SalutAvahiEntryGroupServicePrivate *p = 
    (SalutAvahiEntryGroupServicePrivate *)s;
  g_free(s->name);
  g_free(s->type);
  g_free(s->domain);
  g_free(s->host);
  g_hash_table_destroy(p->entries);
  g_free(s);
}

static GQuark
detail_for_state(AvahiEntryGroupState state) {
  static struct { AvahiClientState state; gchar *name; GQuark quark; } 
    states[]  = { { AVAHI_ENTRY_GROUP_UNCOMMITED,  "uncommited", 0 },
                  { AVAHI_ENTRY_GROUP_REGISTERING, "registering", 0 },
                  { AVAHI_ENTRY_GROUP_ESTABLISHED, "established", 0 },
                  { AVAHI_ENTRY_GROUP_COLLISION,  "collistion", 0 },
                  { AVAHI_ENTRY_GROUP_FAILURE,     "failure", 0 },
                  { 0, NULL, 0 }
    };
  int i;

  for (i = 0; states[i].name != NULL; i++) {
    if (state != states[i].state)
      continue;

    if (!states[i].quark)
      states[i].quark = g_quark_from_static_string(states[i].name);
    return states[i].quark;
  }
  g_assert_not_reached();
}

static void
_avahi_entry_group_cb(AvahiEntryGroup *g, 
                      AvahiEntryGroupState state, void *data) {
  SalutAvahiEntryGroup *self = SALUT_AVAHI_ENTRY_GROUP(data);
  SalutAvahiEntryGroupPrivate *priv = 
    SALUT_AVAHI_ENTRY_GROUP_GET_PRIVATE (self);

  /* Avahi can call the callback before return from _client_new */
  if (priv->group == NULL)
    priv->group = g;

  g_assert(g == priv->group);
  priv->state = state;
  g_signal_emit(self, signals[STATE_CHANGED], 
                detail_for_state(state),  state);
}

SalutAvahiEntryGroup *
salut_avahi_entry_group_new(void) {
  return g_object_new(SALUT_TYPE_AVAHI_ENTRY_GROUP, NULL);
}

static GHashTable *
_string_list_to_hash(AvahiStringList *list) {
  GHashTable *ret;
  ret = g_hash_table_new_full(g_str_hash,
                              g_str_equal,
                              g_free,
                              g_free);
  AvahiStringList *t;
  for (t = list ; t != NULL; t = t->next) {
    gchar *value;
    value = g_strstr_len((gchar *)t->text, t->size, "=");
    if (value == NULL) {
      g_hash_table_insert(ret, g_strndup((gchar *)t->text, t->size), NULL);
    } else {
      int offset = value - (gchar *)t->text;
      g_hash_table_insert(ret, g_strndup((gchar *)t->text, offset),
                               g_strndup(value + 1, t->size - offset - 1));
    }
  }
  return ret;
}

static void
_hash_to_string_list_foreach(gpointer key, gpointer value, gpointer data) {
  AvahiStringList **list = (AvahiStringList **)data;
  *list = avahi_string_list_add_pair(*list, key, value);
}

static AvahiStringList *
_hash_to_string_list(GHashTable *table) {
  AvahiStringList *list = NULL;
  g_hash_table_foreach(table, _hash_to_string_list_foreach, (gpointer )&list);
  return list;
}



SalutAvahiEntryGroupService *
salut_avahi_entry_group_add_service_strlist(SalutAvahiEntryGroup *group, 
                                            const gchar *name, 
                                            const gchar *type, 
                                            guint16 port,
                                            GError **error,
                                            AvahiStringList *txt) {
  return salut_avahi_entry_group_add_service_full_strlist(group, 
                                                          AVAHI_IF_UNSPEC, 
                                                          AVAHI_PROTO_UNSPEC,
                                                          0,
                                                          name, type,
                                                          NULL, NULL,
                                                          port, error, txt);
}

SalutAvahiEntryGroupService *
salut_avahi_entry_group_add_service_full_strlist(SalutAvahiEntryGroup *group, 
                                                 AvahiIfIndex interface,
                                                 AvahiProtocol protocol,
                                                 AvahiPublishFlags flags,
                                                 const gchar *name, 
                                                 const gchar *type, 
                                                 const gchar *domain, 
                                                 const gchar *host, 
                                                 guint16 port,
                                                 GError **error,
                                                 AvahiStringList *txt) {
  SalutAvahiEntryGroupPrivate *priv = 
    SALUT_AVAHI_ENTRY_GROUP_GET_PRIVATE (group);
  SalutAvahiEntryGroupServicePrivate *service = NULL;
  int ret;

  ret = avahi_entry_group_add_service_strlst(priv->group, 
                                             interface, protocol,
                                             flags,
                                             name, type,
                                             domain, host,
                                             port,
                                             txt);
  if (ret) {
    if (error != NULL) {
      *error = g_error_new(SALUT_AVAHI_ERRORS, ret, 
                           "Adding service to group failed: %s",
                           avahi_strerror(ret));
    }
    goto out;
  } 

  service = g_new0(SalutAvahiEntryGroupServicePrivate, 1);
  service->public.interface  = interface;
  service->public.protocol   = protocol;
  service->public.flags      = flags;
  service->public.name       = g_strdup(name);
  service->public.type       = g_strdup(type);
  service->public.domain     = g_strdup(domain);
  service->public.host       = g_strdup(host);
  service->public.port       = port;
  service->group               = group;
  service->frozen             = FALSE;
  service->entries            = _string_list_to_hash(txt); 
out:
  return (SalutAvahiEntryGroupService *)service;
}

SalutAvahiEntryGroupService *
salut_avahi_entry_group_add_service(SalutAvahiEntryGroup *group, 
                                    const gchar *name, 
                                    const gchar *type, 
                                    guint16 port,
                                    GError **error,
                                    ...) {
  SalutAvahiEntryGroupService *ret;
  AvahiStringList *txt = NULL;
  va_list va;
  va_start(va, error);
  txt = avahi_string_list_new_va(va);

  ret = salut_avahi_entry_group_add_service_full_strlist(group, 
                                                         AVAHI_IF_UNSPEC, 
                                                         AVAHI_PROTO_UNSPEC,
                                                         0,
                                                         name, type,
                                                         NULL, NULL,
                                                         port, error, txt);
  avahi_string_list_free(txt);
  va_end(va);
  return ret;
}

SalutAvahiEntryGroupService *
salut_avahi_entry_group_add_service_full(SalutAvahiEntryGroup *group, 
                                         AvahiIfIndex interface,
                                         AvahiProtocol protocol,
                                         AvahiPublishFlags flags,
                                         const gchar *name, 
                                         const gchar *type, 
                                         const gchar *domain, 
                                         const gchar *host, 
                                         guint16 port,
                                         GError **error,
                                         ...) {
  SalutAvahiEntryGroupService *ret;
  AvahiStringList *txt = NULL;
  va_list va;

  va_start(va, error);
  txt = avahi_string_list_new_va(va);

  ret = salut_avahi_entry_group_add_service_full_strlist(group,
                                                         interface, protocol,
                                                         flags,
                                                         name, type,
                                                         domain, host,
                                                         port, error, txt);
  avahi_string_list_free(txt);
  va_end(va);
  return ret;
}

void 
salut_avahi_entry_group_service_freeze(SalutAvahiEntryGroupService *service) {
  SalutAvahiEntryGroupServicePrivate *p = 
    (SalutAvahiEntryGroupServicePrivate *) service;
  p->frozen = TRUE;
}

gboolean
salut_avahi_entry_group_service_thaw(SalutAvahiEntryGroupService *service, 
                                     GError **error) {
  SalutAvahiEntryGroupServicePrivate *priv = 
    (SalutAvahiEntryGroupServicePrivate *) service;
  int ret;
  gboolean result = TRUE;

  AvahiStringList *txt = _hash_to_string_list(priv->entries);
  ret = avahi_entry_group_update_service_txt_strlst(
               SALUT_AVAHI_ENTRY_GROUP_GET_PRIVATE(priv->group)->group,
               service->interface,
               service->protocol,
               service->flags,
               service->name,
               service->type,
               service->domain,
               txt);
  if (ret) {
    if (error != NULL ) {
      *error = g_error_new(SALUT_AVAHI_ERRORS, ret,
                           "Updating txt record failed: %s", 
                           avahi_strerror(ret));
    }
    result = FALSE;
  }

  avahi_string_list_free(txt);
  priv->frozen = FALSE;
  return result;
}

gboolean
salut_avahi_entry_group_service_set(SalutAvahiEntryGroupService *service,
                                     const gchar *key, const gchar *value, 
                                     GError **error) {
  SalutAvahiEntryGroupServicePrivate *priv = 
    (SalutAvahiEntryGroupServicePrivate *) service;

  g_hash_table_insert(priv->entries, g_strdup(key), g_strdup(value));

  if (!priv->frozen) 
    return salut_avahi_entry_group_service_thaw(service, error);
  else 
    return TRUE;
}

gboolean
salut_avahi_entry_group_service_remove_key(SalutAvahiEntryGroupService *service,
                                           const gchar *key, GError **error) {
  SalutAvahiEntryGroupServicePrivate *priv = 
    (SalutAvahiEntryGroupServicePrivate *) service;

  g_hash_table_remove(priv->entries, key);

  if (!priv->frozen) 
    return salut_avahi_entry_group_service_thaw(service, error);
  else 
    return TRUE;
}


gboolean
salut_avahi_entry_group_attach(SalutAvahiEntryGroup *group, 
                               SalutAvahiClient *client, GError **error) {
  SalutAvahiEntryGroupPrivate *priv = 
    SALUT_AVAHI_ENTRY_GROUP_GET_PRIVATE (group);

  g_assert(priv->client == NULL || priv->client == client);
  g_assert(priv->group == NULL);

  priv->client = client;
  g_object_ref(client);

  priv->group = avahi_entry_group_new(client->avahi_client,
                                      _avahi_entry_group_cb,
                                     group);
  if (priv->group == NULL) {
    if (error != NULL ) {
      int aerrno = avahi_client_errno(client->avahi_client);
      *error = g_error_new(SALUT_AVAHI_ERRORS, aerrno,
                           "Attaching group failed: %s", 
                           avahi_strerror(aerrno));
    }
    return FALSE;
  }
  return TRUE;
}

gboolean
salut_avahi_entry_group_commit(SalutAvahiEntryGroup *group, GError **error) {
  SalutAvahiEntryGroupPrivate *priv = 
    SALUT_AVAHI_ENTRY_GROUP_GET_PRIVATE (group);
  int ret;
  ret = avahi_entry_group_commit(priv->group);
  if (ret) {
    if (error != NULL ) {
      *error = g_error_new(SALUT_AVAHI_ERRORS, ret,
                           "Committing group failed: %s", avahi_strerror(ret));
    }
    return FALSE;
  }
  return TRUE;
}

gboolean
salut_avahi_entry_group_reset(SalutAvahiEntryGroup *group, GError **error) {
  SalutAvahiEntryGroupPrivate *priv = 
    SALUT_AVAHI_ENTRY_GROUP_GET_PRIVATE (group);
  int ret;
  ret = avahi_entry_group_reset(priv->group);
  if (ret) {
    if (error != NULL ) {
      *error = g_error_new(SALUT_AVAHI_ERRORS, ret,
                           "Resetting group failed: %s", avahi_strerror(ret));
    }
    return FALSE;
  }
  return TRUE;
}

