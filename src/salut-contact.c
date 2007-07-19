/*
 * salut-contact.c - Source for salut_contact
 * Copyright (C) 2005-2006 Collabora Ltd.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "salut-contact.h"
#include "signals-marshal.h"
#include "salut-presence.h"
#include "salut-presence-enumtypes.h"


#include "salut-avahi-enums.h"
#include "salut-avahi-service-resolver.h"
#include "salut-avahi-record-browser.h"
#include <avahi-common/address.h>
#include <avahi-common/defs.h>
#include <avahi-common/malloc.h>

#include <telepathy-glib/util.h>

#define DEBUG_FLAG DEBUG_CONTACTS
#include <debug.h>

#define DEBUG_CONTACT(contact, format, ...) G_STMT_START {      \
  DEBUG ("Contact %s: " format, contact->name, ##__VA_ARGS__);  \
} G_STMT_END

#define DEBUG_RESOLVER(contact, resolver, format, ...) G_STMT_START {       \
  gchar *_name;                                                             \
  gchar *_type;                                                             \
  gint _interface;                                                          \
  gint _protocol;                                                           \
                                                                            \
  g_object_get (G_OBJECT(resolver),                                         \
      "name", &_name,                                                       \
      "type", &_type,                                                       \
      "interface", &_interface,                                             \
      "protocol", &_protocol,                                               \
      NULL                                                                  \
  );                                                                        \
                                                                            \
  DEBUG_CONTACT (contact, "Resolver (%s %s intf: %d proto: %d): " format,   \
    _name, _type, _interface, _protocol, ##__VA_ARGS__);                    \
                                                                            \
  g_free (_name);                                                           \
  g_free (_type);                                                           \
} G_STMT_END


G_DEFINE_TYPE(SalutContact, salut_contact, G_TYPE_OBJECT)

/* signal enum */
enum
{
    FOUND,
    LOST,
    CONTACT_CHANGE,
#ifdef ENABLE_OLPC
    ACTIVITY_CHANGE,
#endif
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};


typedef struct {
  salut_contact_get_avatar_callback callback;
  gpointer user_data;
} AvatarRequest;

static void
salut_contact_avatar_request_flush(SalutContact *contact,
    guint8 *data, gsize size);

#ifdef ENABLE_OLPC
typedef struct {
  char *activity_id;
  TpHandle room;
  TpHandleRepoIface *room_repo;
} SalutContactActivity;

static void
activity_free (SalutContactActivity *activity)
{
  avahi_free (activity->activity_id);
  if (activity->room != 0)
    {
      tp_handle_unref (activity->room_repo, activity->room);
    }
  g_slice_free (SalutContactActivity, activity);
}

static SalutContactActivity *
activity_new (TpHandleRepoIface *room_repo)
{
  SalutContactActivity *activity = g_slice_new0 (SalutContactActivity);

  activity->room_repo = room_repo;
  return activity;
}
#endif

/* private structure */
typedef struct _SalutContactPrivate SalutContactPrivate;

struct _SalutContactPrivate
{
  gboolean dispose_has_run;
  gchar *alias;
  SalutAvahiClient *client;
  GList *resolvers;
  gboolean found;
  SalutAvahiRecordBrowser *record_browser;
  GList *avatar_requests;
  TpHandleRepoIface *room_repo;
#ifdef ENABLE_OLPC
  GHashTable *olpc_activities;
#endif
};

#define SALUT_CONTACT_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_CONTACT, SalutContactPrivate))

static void
salut_contact_init (SalutContact *obj)
{
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  obj->name = NULL;
  obj->status = SALUT_PRESENCE_AVAILABLE;
  obj->status_message = NULL;
  obj->avatar_token = NULL;
  obj->jid = NULL;
#ifdef ENABLE_OLPC
  obj->olpc_key = NULL;
  obj->olpc_color = NULL;
  obj->olpc_cur_act = NULL;
  obj->olpc_cur_act_room = 0;
  priv->olpc_activities = g_hash_table_new_full (g_str_hash, g_str_equal,
      (GDestroyNotify) g_free, (GDestroyNotify) activity_free);
#endif
  priv->client = NULL;
  priv->resolvers = NULL;
  priv->found = FALSE;
  priv->alias = NULL;
}

static void salut_contact_dispose (GObject *object);
static void salut_contact_finalize (GObject *object);

static void
salut_contact_class_init (SalutContactClass *salut_contact_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_contact_class);

  g_type_class_add_private (salut_contact_class, sizeof (SalutContactPrivate));

  object_class->dispose = salut_contact_dispose;
  object_class->finalize = salut_contact_finalize;

  signals[FOUND] = g_signal_new("found",
                                G_OBJECT_CLASS_TYPE(salut_contact_class),
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL, NULL,
                                g_cclosure_marshal_VOID__VOID,
                                G_TYPE_NONE, 0);

  signals[CONTACT_CHANGE] = g_signal_new("contact-change",
                                G_OBJECT_CLASS_TYPE(salut_contact_class),
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL, NULL,
                                g_cclosure_marshal_VOID__INT,
                                G_TYPE_NONE, 1,
                                G_TYPE_INT);

  signals[LOST] = g_signal_new("lost",
                                G_OBJECT_CLASS_TYPE(salut_contact_class),
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL, NULL,
                                g_cclosure_marshal_VOID__VOID,
                                G_TYPE_NONE, 0);

#ifdef ENABLE_OLPC
  signals[ACTIVITY_CHANGE] = g_signal_new("activity-change",
      G_OBJECT_CLASS_TYPE (salut_contact_class),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      salut_signals_marshal_VOID__STRING_UINT_STRING_STRING_STRING_STRING,
      G_TYPE_NONE,
      6, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING,
      G_TYPE_STRING, G_TYPE_STRING);
#endif
}

void
salut_contact_dispose (GObject *object)
{
  SalutContact *self = SALUT_CONTACT (object);
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (self);

  DEBUG_CONTACT (self, "Disposing contact");

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

#ifdef ENABLE_OLPC
  if (self->olpc_cur_act_room != 0)
    {
      tp_handle_unref (priv->room_repo, self->olpc_cur_act_room);
      self->olpc_cur_act_room = 0;
    }

  g_hash_table_destroy (priv->olpc_activities);
#endif

  priv->room_repo = NULL;

  salut_contact_avatar_request_flush(self, NULL, 0);

  /* release any references held by the object here */
  if (priv->client)
    g_object_unref(priv->client);
  priv->client = NULL;

  g_list_foreach(priv->resolvers, (GFunc)g_object_unref, NULL);
  g_list_free(priv->resolvers);
  priv->resolvers = NULL;

  if (G_OBJECT_CLASS (salut_contact_parent_class)->dispose)
    G_OBJECT_CLASS (salut_contact_parent_class)->dispose (object);
}

void
salut_contact_finalize (GObject *object) {
  SalutContact *self = SALUT_CONTACT (object);
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  g_free(self->name);
  g_free(self->status_message);
  g_free(priv->alias);
  g_free(self->avatar_token);
  g_free (self->jid);

#ifdef ENABLE_OLPC
  if (self->olpc_key != NULL)
    {
      g_array_free (self->olpc_key, TRUE);
    }
  g_free (self->olpc_color);
  g_free (self->olpc_cur_act);
#endif

  G_OBJECT_CLASS (salut_contact_parent_class)->finalize (object);
}

struct resolverinfo {
  AvahiIfIndex interface;
  AvahiProtocol protocol;
  const gchar *name;
  const gchar *type;
  const gchar *domain;
};

static gint
compare_resolver(gconstpointer a, gconstpointer b) {
  struct resolverinfo *info = (struct resolverinfo *) b;

  AvahiIfIndex interface;
  AvahiProtocol protocol;
  gchar *name;
  gchar *type;
  gchar *domain;
  gint result;

  g_object_get((gpointer)a,
               "interface", &interface,
               "protocol", &protocol,
               "name", &name,
               "type", &type,
               "domain", &domain,
               NULL);
  if (interface == info->interface
      && protocol == info->protocol
      && !strcmp(name, info->name)
      && !strcmp(type, info->type)
      && !strcmp(domain, info->domain)) {
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

static SalutAvahiServiceResolver *
find_resolver(SalutContact *contact,
              AvahiIfIndex interface, AvahiProtocol protocol,
              const gchar *name, const gchar *type, const gchar *domain) {
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (contact);
  struct resolverinfo info;
  GList *ret;
  info.interface = interface;
  info.protocol = protocol;
  info.name = name;
  info.type = type;
  info.domain = domain;
  ret = g_list_find_custom(priv->resolvers, &info, compare_resolver);
  return ret ? SALUT_AVAHI_SERVICE_RESOLVER(ret->data) : NULL;
}

SalutContact *
salut_contact_new(SalutAvahiClient *client,
                  TpHandleRepoIface *room_repo,
                  const gchar *name)
{
  SalutContact *ret;
  SalutContactPrivate *priv;

  ret = g_object_new(SALUT_TYPE_CONTACT, NULL);
  priv = SALUT_CONTACT_GET_PRIVATE (ret);

  ret->name = g_strdup(name);

  g_object_ref(client);
  priv->client = client;
  priv->room_repo = room_repo;

  return ret;
}

/* valid is true if this was a valid alias
 * changed is true if the contacts alias actually changed */
static gboolean
update_alias(SalutContact *self, const gchar *new, gboolean *valid) {
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (self);
  if (new == NULL || *new == '\0') {
    *valid = FALSE;
    return FALSE;
  }

  if (priv->alias == NULL || strcmp(priv->alias, new) != 0)  {
    g_free(priv->alias);
    priv->alias = g_strdup(new);
    *valid = TRUE;
    return TRUE;
  }

  *valid = TRUE;
  return FALSE;
}

static void
purge_cached_avatar(SalutContact *self, const gchar *token) {
  g_free(self->avatar_token);
  self->avatar_token = g_strdup(token);
}

#ifdef ENABLE_OLPC
typedef struct
{
  SalutContactOLPCActivityFunc foreach;
  gpointer user_data;
} foreach_olpc_activity_ctx;

static void
foreach_olpc_activity (gpointer key, gpointer value, gpointer user_data)
{
  foreach_olpc_activity_ctx *ctx = user_data;
  SalutContactActivity *activity = value;

  DEBUG ("%s => %u", activity->activity_id, activity->room);
  (ctx->foreach) (activity->activity_id, activity->room, ctx->user_data);
}

void
salut_contact_foreach_olpc_activity (SalutContact *self,
                                     SalutContactOLPCActivityFunc foreach,
                                     gpointer user_data)
{
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (self);
  foreach_olpc_activity_ctx ctx = { foreach, user_data };

  DEBUG ("called");

  g_hash_table_foreach (priv->olpc_activities, foreach_olpc_activity,
      &ctx);

  DEBUG ("end");
}

static void
activity_resolved_cb (SalutAvahiServiceResolver *resolver,
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
                      gpointer userdata)
{
  SalutContact *self = SALUT_CONTACT (userdata);
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (self);
  AvahiStringList *t;
  TpHandle room_handle = 0;
  char *activity_id = NULL;
  char *color = NULL;
  char *activity_name = NULL;
  char *activity_type = NULL;
  SalutContactActivity *activity;

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

  activity = g_hash_table_lookup (priv->olpc_activities, name);
  if (activity == NULL)
    {
      activity = activity_new (priv->room_repo);
      g_hash_table_insert (priv->olpc_activities, g_strdup (name), activity);
    }

  if ((t = avahi_string_list_find (txt, "activity-id")) != NULL)
    {
      avahi_string_list_get_pair (t, NULL, &activity_id, NULL);
    }

  if ((t = avahi_string_list_find (txt, "room")) != NULL)
    {
      char *room_name;

      avahi_string_list_get_pair (t, NULL, &room_name, NULL);
      room_handle = tp_handle_ensure (priv->room_repo, room_name, NULL, NULL);
      avahi_free (room_name);
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

  if (room_handle == 0 || activity_id == NULL || activity_id[0] == '\0')
    {
      DEBUG ("Activity ID %s, room handle %u: removing from hash",
          activity_id ? activity_id : "<NULL>", room_handle);
      g_hash_table_remove (priv->olpc_activities, name);

      g_signal_emit (self, signals[CONTACT_CHANGE], 0,
          SALUT_CONTACT_OLPC_ACTIVITIES);
    }
  else if (activity->room != room_handle ||
      tp_strdiff (activity->activity_id, activity_id))
    {
      DEBUG ("Activity ID %s, room handle %u: updating hash",
          activity_id, room_handle);

      /* transfer ownerships to the SalutContactActivity, and set to
       * NULL/0 so the end of this function doesn't free them */
      if (activity->activity_id != NULL)
        avahi_free (activity->activity_id);
      activity->activity_id = activity_id;
      activity_id = NULL;

      if (activity->room != 0)
        tp_handle_unref (priv->room_repo, activity->room);
      activity->room = room_handle;
      room_handle = 0;

      g_signal_emit (self, signals[CONTACT_CHANGE], 0,
          SALUT_CONTACT_OLPC_ACTIVITIES);
    }

  if (activity->room != 0)
    {
      DEBUG ("Room handle is nonzero, emitting activity-change signal");
      g_signal_emit (self, signals[ACTIVITY_CHANGE], 0,
          name, activity->room, activity->activity_id,
          color, activity_name, activity_type);
    }
  else
    {
      DEBUG ("Room handle is 0, not emitting activity-change signal");
    }

  if (room_handle != 0)
    tp_handle_unref (priv->room_repo, room_handle);
  avahi_free (activity_id);
  avahi_free (activity_type);
  avahi_free (activity_name);
  avahi_free (color);
}
#endif

static void
contact_resolved_cb(SalutAvahiServiceResolver *resolver,
                    AvahiIfIndex interface, AvahiProtocol protocol,
                    gchar *name, gchar *type, gchar *domain, gchar *host_name,
                    AvahiAddress *a, gint port,
                    AvahiStringList *txt, AvahiLookupResultFlags flags,
                    gpointer userdata) {
  SalutContact *self = SALUT_CONTACT (userdata);
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (self);
  AvahiStringList *t;
  gint changes = 0;
  gboolean alias_seen = FALSE;

  DEBUG_RESOLVER (self, resolver, "contact %s resolved", self->name);

#define SET_CHANGE(x) changes |= x

  if ((t = avahi_string_list_find(txt, "status")) != NULL) {
    int i;
    char *value;
    avahi_string_list_get_pair(t, NULL, &value, NULL);

    for (i = 0; i < SALUT_PRESENCE_NR_PRESENCES ; i++) {
      if (!strcmp(value, salut_presence_status_txt_names[i])) {
        break;
      }
    }

    if (i != self->status && i < SALUT_PRESENCE_NR_PRESENCES) {
      SET_CHANGE(SALUT_CONTACT_STATUS_CHANGED);
      self->status = i;
    }

    avahi_free(value);
  }

  if ((t = avahi_string_list_find(txt, "msg")) != NULL) {
    char *value;
    avahi_string_list_get_pair(t, NULL, &value, NULL);
    if (self->status_message == NULL || strcmp(self->status_message, value)) {
      SET_CHANGE(SALUT_CONTACT_STATUS_CHANGED);
      g_free(self->status_message);
      self->status_message = g_strdup(value);
    }
    avahi_free(value);
  } else if (self->status_message != NULL) {
    SET_CHANGE(SALUT_CONTACT_STATUS_CHANGED);
    g_free(self->status_message);
    self->status_message = NULL;
  }

  if ((t = avahi_string_list_find(txt, "nick")) != NULL) {
    char *value;
    avahi_string_list_get_pair(t, NULL, &value, NULL);
    if (update_alias(self, value, &alias_seen)) {
      SET_CHANGE(SALUT_CONTACT_ALIAS_CHANGED);
    }
    avahi_free(value);
  }

  if (!alias_seen) {
    char *first = NULL;
    char *last = NULL;

    /* Fallback to trying 1st + last as alias */
    if ((t = avahi_string_list_find(txt, "1st")) != NULL) {
      avahi_string_list_get_pair(t, NULL, &first, NULL);
    }

    if ((t = avahi_string_list_find(txt, "last")) != NULL) {
      avahi_string_list_get_pair(t, NULL, &last, NULL);
    }

    if (first != NULL && last != NULL) {
      gchar *alias = NULL;

      alias = g_strdup_printf("%s %s", first, last);

      if (update_alias(self, alias, &alias_seen)) {
        SET_CHANGE(SALUT_CONTACT_ALIAS_CHANGED);
      }
      g_free(alias);
    } else if (first != NULL) {
      if (update_alias(self, first, &alias_seen)) {
        SET_CHANGE(SALUT_CONTACT_ALIAS_CHANGED);
      }
    } else if (last != NULL) {
      if (update_alias(self, last, &alias_seen)) {
        SET_CHANGE(SALUT_CONTACT_ALIAS_CHANGED);
      }
    }
    avahi_free(first);
    avahi_free(last);
  }

  if (!alias_seen && priv->alias != NULL) {
    /* No alias anymore ? */
    g_free(priv->alias);
    priv->alias = NULL;
    SET_CHANGE(SALUT_CONTACT_ALIAS_CHANGED);
  }

  if (!priv->found) {
    g_signal_emit(self, signals[FOUND], 0);
    /* Initially force updates of everything */
    SET_CHANGE(0xff);
    priv->found = TRUE;
  }

  if ((t = avahi_string_list_find(txt, "phsh")) != NULL) {
    char *value;
    avahi_string_list_get_pair(t, NULL, &value, NULL);

    if (tp_strdiff(self->avatar_token, value)) {
      /* Purge the cache */
      purge_cached_avatar(self, value);
      SET_CHANGE(SALUT_CONTACT_AVATAR_CHANGED);
    }

    avahi_free(value);
  } else if (self->avatar_token != NULL) {
    purge_cached_avatar(self, NULL);
    SET_CHANGE(SALUT_CONTACT_AVATAR_CHANGED);
  }

  t = avahi_string_list_find(txt, "jid");
  if (t != NULL)
    {
      char *value;
      avahi_string_list_get_pair (t, NULL, &value, NULL);

      if (tp_strdiff (self->jid, value))
        {
          g_free (self->jid);
          self->jid = g_strdup (value);
#ifdef ENABLE_OLPC
          SET_CHANGE (SALUT_CONTACT_OLPC_PROPERTIES);
#endif
        }
      avahi_free (value);
    }

#ifdef ENABLE_OLPC
  if ((t = avahi_string_list_find (txt, "olpc-color")) != NULL)
    {
      char *value;

      avahi_string_list_get_pair (t, NULL, &value, NULL);
      if (tp_strdiff (self->olpc_color, value))
        {
          g_free (self->olpc_color);
          self->olpc_color = g_strdup (value);
          SET_CHANGE (SALUT_CONTACT_OLPC_PROPERTIES);
        }

      avahi_free (value);
  }

  do
    {
      AvahiStringList *act_link = avahi_string_list_find (txt,
          "olpc-current-activity");
      AvahiStringList *room_link = avahi_string_list_find (txt,
          "olpc-current-activity-room");
      char *act_value = NULL;
      TpHandle room_handle = 0;

      if (act_link != NULL)
        {
          avahi_string_list_get_pair (act_link, NULL, &act_value, NULL);
          if (act_value == NULL || *act_value == '\0')
            {
              DEBUG ("No current activity; ignoring current activity room, if "
                  "any");
              avahi_free (act_value);
              act_value = NULL;
            }
        }

      if (act_value != NULL && room_link != NULL)
        {
          char *room_value;

          avahi_string_list_get_pair (room_link, NULL, &room_value, NULL);
          if (room_value == NULL || *room_value == '\0')
            {
              room_handle = 0;
            }
          else
            {
              room_handle = tp_handle_ensure (priv->room_repo, room_value,
                  NULL, NULL);
            }
          avahi_free (room_value);

          if (room_handle == 0)
            {
              DEBUG ("Invalid room \"%s\" for current activity \"%s\": "
                  "ignoring", room_value, act_value);
              avahi_free (act_value);
              act_value = NULL;
            }
        }

      if (act_value == NULL || room_handle == 0)
        {
          DEBUG ("Unsetting current activity");
          if (self->olpc_cur_act != NULL || self->olpc_cur_act_room != 0)
            {
              g_free (self->olpc_cur_act);
              if (self->olpc_cur_act_room != 0)
                tp_handle_unref (priv->room_repo, self->olpc_cur_act_room);
              self->olpc_cur_act = NULL;
              self->olpc_cur_act_room = 0;
              SET_CHANGE (SALUT_CONTACT_OLPC_CURRENT_ACTIVITY);
            }
        }
      else
        {
          DEBUG ("Current activity %s, room handle %d", act_value,
              room_handle);
          if (tp_strdiff (self->olpc_cur_act, act_value) ||
              self->olpc_cur_act_room != room_handle)
            {
              g_free (self->olpc_cur_act);
              if (self->olpc_cur_act_room != 0)
                tp_handle_unref (priv->room_repo, self->olpc_cur_act_room);
              self->olpc_cur_act_room = room_handle;
              self->olpc_cur_act = g_strdup (act_value);
              SET_CHANGE (SALUT_CONTACT_OLPC_CURRENT_ACTIVITY);
            }
          else
            {
              tp_handle_unref (priv->room_repo, room_handle);
            }
          avahi_free (act_value);
        }
    }
  while (0);

  if ((t = avahi_string_list_find (txt, "olpc-key-part0")) != NULL)
    {
      guint i = 0;
      gchar *name = NULL;
      /* FIXME: how big are OLPC keys anyway? */
      GArray *new_key = g_array_sized_new (FALSE, FALSE, sizeof (guint8),
          512);

      while (t)
        {
          char *v;
          size_t v_size;

          avahi_string_list_get_pair (t, NULL, &v, &v_size);
          g_array_append_vals (new_key, v, v_size);
          avahi_free (v);

          i++;
          g_free (name);
          name = g_strdup_printf ("olpc-key-part%u", i);
          t = avahi_string_list_find (txt, name);
        }
      g_free (name);

      if (self->olpc_key == NULL ||
          self->olpc_key->len != new_key->len ||
          memcmp (self->olpc_key->data, new_key->data, new_key->len) != 0)
        {
          if (self->olpc_key != NULL)
            {
              g_array_free (self->olpc_key, TRUE);
            }
          self->olpc_key = new_key;
          SET_CHANGE (SALUT_CONTACT_OLPC_PROPERTIES);
        }
      else
        {
          g_array_free (new_key, TRUE);
        }
    }
#endif

  if (changes != 0) {
    g_signal_emit(self, signals[CONTACT_CHANGE], 0, changes);
  }
}

static void
contact_lost(SalutContact *contact) {
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (contact);

  contact->status = SALUT_PRESENCE_OFFLINE;
  g_free(contact->status_message);
  contact->status_message = NULL;

  DEBUG_CONTACT (contact, "disappeared from the local link");

  priv->found = FALSE;
  g_signal_emit(contact, signals[CONTACT_CHANGE], 0,
      SALUT_CONTACT_STATUS_CHANGED);
  g_signal_emit(contact, signals[LOST], 0);
}

static void
contact_drop_resolver (SalutContact *self,
                       SalutAvahiServiceResolver *resolver)
{
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (self);
  gint resolvers_left;
#ifdef ENABLE_OLPC
  gchar *type;
  gchar *name;

  g_object_get (resolver,
      "type", &type,
      "name", &name,
      NULL);
  if (!tp_strdiff (type, "_olpc-activity._udp"))
    {
      /* one of their activities has fallen off */
      g_hash_table_remove (priv->olpc_activities, name);
      g_signal_emit (self, signals[CONTACT_CHANGE], 0,
          SALUT_CONTACT_OLPC_ACTIVITIES);
    }
#endif

  /* FIXME: what we actually want to be counting is the number of _presence
   * resolvers, ignoring the _olpc-activity ones. However, if someone's still
   * advertising activities, we can probably consider them to be online? */
  priv->resolvers = g_list_remove(priv->resolvers, resolver);

  resolvers_left = g_list_length (priv->resolvers);

  DEBUG_RESOLVER (self, resolver, "removed, %d left for %s", resolvers_left,
     self->name);

  g_object_unref(resolver);

  if (resolvers_left == 0 && priv->found) {
    contact_lost(self);
  }
}

static void
contact_failed_cb(SalutAvahiServiceResolver *resolver, GError *error,
                   gpointer userdata) {
  SalutContact *self = SALUT_CONTACT (userdata);

  DEBUG_RESOLVER (self, resolver, "failed: %s", error->message);

  contact_drop_resolver (self, resolver);
}

void
salut_contact_add_service(SalutContact *contact,
                          AvahiIfIndex interface, AvahiProtocol protocol,
                          const char *name, const char *type,
                          const char *domain) {
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (contact);
  SalutAvahiServiceResolver *resolver;
  resolver = find_resolver(contact, interface, protocol, name, type, domain);

  if (resolver)
    return;

  resolver = salut_avahi_service_resolver_new(interface,
                                              protocol,
                                              name, type, domain,
                                              protocol, 0);

#ifdef ENABLE_OLPC
  /* FIXME: better way to do this? */
  if (!tp_strdiff (type, "_olpc-activity._udp"))
    {
      g_signal_connect(resolver, "found", G_CALLBACK(activity_resolved_cb),
          contact);
    }
  else
#endif
    {
      g_signal_connect(resolver, "found", G_CALLBACK(contact_resolved_cb),
          contact);
    }

  g_signal_connect(resolver, "failure", G_CALLBACK(contact_failed_cb), contact);
  if (!salut_avahi_service_resolver_attach(resolver, priv->client, NULL)) {
    g_warning("Failed to attach resolver");
  }

  DEBUG_RESOLVER (contact, resolver, "added");
  priv->resolvers = g_list_prepend(priv->resolvers, resolver);
}

void
salut_contact_remove_service(SalutContact *contact,
                          AvahiIfIndex interface, AvahiProtocol protocol,
                          const char *name, const char *type,
                          const char *domain) {
  SalutAvahiServiceResolver *resolver;
  resolver =  find_resolver(contact, interface, protocol, name, type, domain);

  if (!resolver)
    return;

  DEBUG_RESOLVER (contact, resolver, "remove requested");

  contact_drop_resolver (contact, resolver);
}

static void
_avahi_address_to_sockaddr(AvahiAddress *address, guint16 port,
                           AvahiIfIndex index,
                           struct sockaddr_storage *sockaddr) {
  switch (address->proto) {
    case AVAHI_PROTO_INET: {
      struct sockaddr_in *sockaddr4 = (struct sockaddr_in *)sockaddr;
      sockaddr4->sin_family = AF_INET;
      sockaddr4->sin_port = htons(port);
      /* ->address is already in network byte order */
      sockaddr4->sin_addr.s_addr = address->data.ipv4.address;
      break;
    }
    case AVAHI_PROTO_INET6: {
      struct sockaddr_in6 *sockaddr6 = (struct sockaddr_in6 *)sockaddr;
      sockaddr6->sin6_family = AF_INET6;
      sockaddr6->sin6_port = htons(port);
      memcpy(sockaddr6->sin6_addr.s6_addr,
             address->data.ipv6.address, 16);

      sockaddr6->sin6_flowinfo = 0;
      sockaddr6->sin6_scope_id = index;
      break;
    }
    default:
      g_assert_not_reached();
  }
}

static void
_add_address(gpointer data, gpointer user_data) {
  SalutAvahiServiceResolver *resolver = SALUT_AVAHI_SERVICE_RESOLVER(data);
  GArray *addresses = (GArray *)user_data;
  salut_contact_address_t s_address;
  AvahiAddress address;
  guint16 port;
  AvahiIfIndex ifindex;

  g_object_get(resolver, "interface", &ifindex, NULL);
  if (salut_avahi_service_resolver_get_address(resolver, &address, &port)) {
    _avahi_address_to_sockaddr(&address, port, ifindex, &(s_address.address));
    g_array_append_val(addresses, s_address);
  }
}

GArray *
salut_contact_get_addresses(SalutContact *contact) {
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (contact);
  GArray *addresses;

  addresses = g_array_sized_new(TRUE, TRUE,
                                sizeof(salut_contact_address_t),
                                g_list_length(priv->resolvers));
  g_list_foreach(priv->resolvers, _add_address, addresses);

  return addresses;
}

static gint
_compare_address(gconstpointer a, gconstpointer b) {
  SalutAvahiServiceResolver *resolver = SALUT_AVAHI_SERVICE_RESOLVER(a);
  struct sockaddr_storage addr_a;
  struct sockaddr_storage *addr_b = (struct sockaddr_storage *)b;
  AvahiIfIndex ifindex;
  AvahiAddress address;
  uint16_t port;

  g_object_get(resolver, "interface", &ifindex, NULL);
  if (!salut_avahi_service_resolver_get_address(resolver, &address, &port)) {
    return -1;
  }
  _avahi_address_to_sockaddr(&address, port, ifindex, &addr_a);

  if ( ((struct sockaddr *)&addr_a)->sa_family
       != ((struct sockaddr *)addr_b)->sa_family) {
    return -1;
  }

  switch (((struct sockaddr *)&addr_a)->sa_family) {
    case AF_INET: {
      struct sockaddr_in *a4 = (struct sockaddr_in *)&addr_a;
      struct sockaddr_in *b4 = (struct sockaddr_in *)addr_b;
      return b4->sin_addr.s_addr - a4->sin_addr.s_addr;
    }
    case AF_INET6: {
      struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)&addr_a;
      struct sockaddr_in6 *b6 = (struct sockaddr_in6 *)addr_b;
      /* FIXME should we compare the scope_id too ? */
      return memcmp(a6->sin6_addr.s6_addr, b6->sin6_addr.s6_addr, 16);
    }
    default:
      g_assert_not_reached();
  }
  return 0;
}

gboolean
salut_contact_has_address(SalutContact *contact,
                           struct sockaddr_storage *address) {
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (contact);
  return
     (g_list_find_custom(priv->resolvers, address, _compare_address) != NULL);
}

const gchar *
salut_contact_get_alias(SalutContact *contact) {
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (contact);
  if (priv->alias == NULL) {
    return contact->name;
  }
  return priv->alias;
}

gboolean
salut_contact_has_services(SalutContact *contact) {
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (contact);
  return priv->resolvers != NULL;
}

static void
salut_contact_avatar_request_flush(SalutContact *contact,
                                   guint8 *data, gsize size) {
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE(contact);
  GList *list, *liststart;
  AvatarRequest *request;


  if (priv->record_browser != NULL)
    g_object_unref(priv->record_browser);
  priv->record_browser = NULL;

  liststart = priv->avatar_requests;
  priv->avatar_requests = NULL;

  for (list = liststart; list != NULL; list = g_list_next(list)) {
    request = (AvatarRequest *)list->data;
    request->callback(contact, data, size, request->user_data);
    g_slice_free(AvatarRequest, request);
  }
  g_list_free(liststart);
}

static void
salut_contact_avatar_all_for_now(SalutAvahiRecordBrowser *browser,
                                 gpointer user_data) {
  SalutContact *contact = SALUT_CONTACT(user_data);

  DEBUG("All for now for resolving %s's record", contact->name);
  salut_contact_avatar_request_flush(contact, NULL, 0);
}

static void
salut_contact_avatar_failure(SalutAvahiRecordBrowser *browser, GError *error,
                             gpointer user_data) {
  SalutContact *contact = SALUT_CONTACT(user_data);

  DEBUG("Resolving record for %s failed: %s", contact->name, error->message);

  salut_contact_avatar_request_flush(contact, NULL, 0);
}

static void
salut_contact_avatar_found(SalutAvahiRecordBrowser *browser,
                           AvahiIfIndex interface, AvahiProtocol protocol,
                           gchar *name, guint16 clazz, guint16 type,
                           guint8 *rdata, gsize rdata_size,
                           AvahiLookupFlags flags, gpointer user_data) {
  SalutContact *contact = SALUT_CONTACT(user_data);

  DEBUG("Found avatar for %s for size %zd", contact->name, rdata_size);

  if (rdata_size <= 0)
    salut_contact_avatar_request_flush(contact, NULL, 0);

  salut_contact_avatar_request_flush(contact, rdata, rdata_size);
}

static void
salut_contact_retrieve_avatar(SalutContact *contact) {
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (contact);
  gchar *name;
  GError *error = NULL;

  if (priv->record_browser != NULL) {
    return;
  }

  name = g_strdup_printf("%s._presence._tcp.local", contact->name);
  priv->record_browser = salut_avahi_record_browser_new(name, 0xA);
  g_free(name);

  g_signal_connect(priv->record_browser, "new-record",
                   G_CALLBACK(salut_contact_avatar_found), contact);
  g_signal_connect(priv->record_browser, "all-for-now",
                   G_CALLBACK(salut_contact_avatar_all_for_now), contact);
  g_signal_connect(priv->record_browser, "failure",
                   G_CALLBACK(salut_contact_avatar_failure), contact);

  salut_avahi_record_browser_attach(priv->record_browser,
                                    priv->client, &error);
}

void
salut_contact_get_avatar(SalutContact *contact,
                         salut_contact_get_avatar_callback callback,
                         gpointer user_data) {
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (contact);
  AvatarRequest *request;

  g_assert(contact != NULL);

  if (contact->avatar_token == NULL) {
    DEBUG("Avatar requestes for a contact without one (%s)", contact->name);
    callback(contact, NULL, 0, user_data);
    return;
  }

  DEBUG("Requesting avatar for: %s", contact->name);
  request = g_slice_new0(AvatarRequest);
  request->callback = callback;
  request->user_data = user_data;
  priv->avatar_requests = g_list_append(priv->avatar_requests, request);

  salut_contact_retrieve_avatar(contact);
}
