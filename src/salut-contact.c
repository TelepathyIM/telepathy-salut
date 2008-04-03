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

#include <telepathy-glib/util.h>

#define DEBUG_FLAG DEBUG_CONTACTS
#include <debug.h>

#define DEBUG_CONTACT(contact, format, ...) G_STMT_START {      \
  DEBUG ("Contact %s: " format, contact->name, ##__VA_ARGS__);  \
} G_STMT_END

G_DEFINE_TYPE(SalutContact, salut_contact, G_TYPE_OBJECT)

/* properties */
enum {
  PROP_CONNECTION = 1,
  PROP_NAME,
  LAST_PROP
};

/* signal enum */
enum
{
    FOUND,
    LOST,
    CONTACT_CHANGE,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};


typedef struct {
  salut_contact_get_avatar_callback callback;
  gpointer user_data;
} AvatarRequest;

/* private structure */
typedef struct _SalutContactPrivate SalutContactPrivate;

struct _SalutContactPrivate
{
  gboolean dispose_has_run;
  gchar *alias;
  GList *avatar_requests;
#ifdef ENABLE_OLPC
   /* room handle owned by the SalutOlpcActivity -> SalutOlpcActivity */
  GHashTable *olpc_activities;
#endif
};

#define SALUT_CONTACT_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_CONTACT, SalutContactPrivate))

static GObject *
salut_contact_constructor (GType type,
                           guint n_props,
                           GObjectConstructParam *props)
{
  GObject *obj;
  SalutContact *self;
  SalutContactPrivate *priv;
  TpHandleRepoIface *contact_repo;

  obj = G_OBJECT_CLASS (salut_contact_parent_class)->
    constructor (type, n_props, props);

  self = SALUT_CONTACT (obj);
  priv = SALUT_CONTACT_GET_PRIVATE (self);

  g_assert (self->connection != NULL);
  g_assert (self->name != NULL);

  contact_repo = tp_base_connection_get_handles
      ((TpBaseConnection *) self->connection, TP_HANDLE_TYPE_CONTACT);

  self->handle = tp_handle_ensure (contact_repo, self->name, NULL, NULL);

  return obj;
}

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
  obj->olpc_ip4 = NULL;
  obj->olpc_ip6 = NULL;
  priv->olpc_activities = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) g_object_unref);
#endif
  obj->found = FALSE;
  priv->alias = NULL;
}

static void
salut_contact_get_property (GObject *object,
                            guint property_id,
                            GValue *value,
                            GParamSpec *pspec)
{
  SalutContact *self = SALUT_CONTACT (object);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, self->connection);
        break;
      case PROP_NAME:
        g_value_set_string (value, self->name);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_contact_set_property (GObject *object,
                            guint property_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
  SalutContact *self = SALUT_CONTACT (object);

  switch (property_id)
    {
      case PROP_CONNECTION:
        self->connection = g_value_get_object (value);
        break;
      case PROP_NAME:
        self->name = g_value_dup_string (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void salut_contact_dispose (GObject *object);
static void salut_contact_finalize (GObject *object);

static void
salut_contact_class_init (SalutContactClass *salut_contact_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_contact_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_contact_class, sizeof (SalutContactPrivate));

  object_class->constructor = salut_contact_constructor;
  object_class->get_property = salut_contact_get_property;
  object_class->set_property = salut_contact_set_property;

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

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "The Salut Connection associated with this contact",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION,
      param_spec);

  param_spec = g_param_spec_string (
      "name",
      "name",
      "The name of this contact",
      NULL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_NAME,
      param_spec);
}

#ifdef ENABLE_OLPC
static void
disconnect_activity_signal_foreach (TpHandle room,
                                    SalutOlpcActivity *activity,
                                    SalutContact *self)
{
  g_signal_handlers_disconnect_matched (activity, G_SIGNAL_MATCH_DATA, 0, 0,
      NULL, NULL, self);
}
#endif

void
salut_contact_dispose (GObject *object)
{
  SalutContact *self = SALUT_CONTACT (object);
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles
      ((TpBaseConnection *) self->connection, TP_HANDLE_TYPE_CONTACT);

  DEBUG_CONTACT (self, "Disposing contact");

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

#ifdef ENABLE_OLPC
  if (self->olpc_cur_act_room != 0)
    {
      TpHandleRepoIface *room_repo = tp_base_connection_get_handles
          ((TpBaseConnection *) self->connection, TP_HANDLE_TYPE_ROOM);

      tp_handle_unref (room_repo, self->olpc_cur_act_room);
      self->olpc_cur_act_room = 0;
    }

  g_hash_table_foreach (priv->olpc_activities,
      (GHFunc) disconnect_activity_signal_foreach, self);
  g_hash_table_destroy (priv->olpc_activities);
#endif

  salut_contact_avatar_request_flush(self, NULL, 0);

  /* release any references held by the object here */

  if (self->handle != 0)
    tp_handle_unref (contact_repo, self->handle);

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
  g_free (self->olpc_ip4);
  g_free (self->olpc_ip6);
#endif

  G_OBJECT_CLASS (salut_contact_parent_class)->finalize (object);
}

static void
purge_cached_avatar (SalutContact *self,
                    const gchar *token)
{
  g_free (self->avatar_token);
  self->avatar_token = g_strdup (token);

  /* the avatar token has changed, restart retrieving the avatar just to be
   * sure */
  SALUT_CONTACT_GET_CLASS (self)->retrieve_avatar (self);
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
  SalutOlpcActivity *activity = value;

  /* ignore activity without ID */
  if (activity->id == NULL)
    return;

  DEBUG ("%s => %u", activity->id, activity->room);
  (ctx->foreach) (activity, ctx->user_data);
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

#endif

GArray *
salut_contact_get_addresses (SalutContact *self)
{
  return SALUT_CONTACT_GET_CLASS (self)->get_addresses (self);
}

gboolean
salut_contact_has_address (SalutContact *self,
                           struct sockaddr_storage *address)
{
  return SALUT_CONTACT_GET_CLASS (self)->has_address (self, address);
}

const gchar *
salut_contact_get_alias(SalutContact *contact) {
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (contact);
  if (priv->alias == NULL) {
    return contact->name;
  }
  return priv->alias;
}

void
salut_contact_avatar_request_flush (SalutContact *contact,
                                    guint8 *data,
                                    gsize size)
{
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE(contact);
  GList *list, *liststart;
  AvatarRequest *request;

  liststart = priv->avatar_requests;
  priv->avatar_requests = NULL;

  for (list = liststart; list != NULL; list = g_list_next(list)) {
    request = (AvatarRequest *)list->data;
    request->callback(contact, data, size, request->user_data);
    g_slice_free(AvatarRequest, request);
  }
  g_list_free(liststart);
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

  SALUT_CONTACT_GET_CLASS (contact)->retrieve_avatar (contact);
}

/* valid is true if this was a valid alias
 * returned value is true if the contacts alias actually changed */
static gboolean
update_alias (SalutContact *self,
              const gchar *new,
              gboolean *valid)
{
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (self);

  if (new == NULL || *new == '\0')
    {
      *valid = FALSE;
      return FALSE;
    }

  *valid = TRUE;
  if (tp_strdiff (priv->alias, new))
    {
      g_free (priv->alias);
      priv->alias = g_strdup (new);
      return TRUE;
    }

  return FALSE;
}

void
salut_contact_change (SalutContact *self,
                      gint status,
                      const gchar *status_msg,
                      const gchar *nick,
                      const gchar *first,
                      const gchar *last,
                      const gchar *avatar_token,
                      const gchar *jid,
                      const gchar *olpc_color,
                      const gchar *current_act_id,
                      TpHandle current_act_room,
                      GArray *olpc_key,
                      const gchar *ip4_addr,
                      const gchar *ip6_addr)
{
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (self);
  gboolean alias_seen = FALSE;
  gint changes = 0;
#ifdef ENABLE_OLPC
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles
    ((TpBaseConnection *) self->connection, TP_HANDLE_TYPE_ROOM);
#endif

#define SET_CHANGE(x) changes |= x

  /* status */
  if (status != self->status && status < SALUT_PRESENCE_NR_PRESENCES)
    {
      SET_CHANGE (SALUT_CONTACT_STATUS_CHANGED);
      self->status = status;
    }

  /* status message */
  if (status_msg != NULL && tp_strdiff (self->status_message, status_msg))
    {
      SET_CHANGE (SALUT_CONTACT_STATUS_CHANGED);
      g_free (self->status_message);
      self->status_message = g_strdup (status_msg);
    }
  else if (status_msg == NULL && self->status_message != NULL)
    {
      SET_CHANGE (SALUT_CONTACT_STATUS_CHANGED);
      g_free (self->status_message);
      self->status_message = NULL;
    }

  /* alias */
  if (update_alias (self, nick, &alias_seen))
    SET_CHANGE (SALUT_CONTACT_ALIAS_CHANGED);

  if (!alias_seen)
    {
      /* Fallback to trying 1st + last as alias */
      if (first != NULL && last != NULL)
        {
          gchar *alias = NULL;

          alias = g_strdup_printf ("%s %s", first, last);

          if (update_alias (self, alias, &alias_seen))
            SET_CHANGE (SALUT_CONTACT_ALIAS_CHANGED);
          g_free (alias);
        }
      else if (first != NULL)
        {
          if (update_alias (self, first, &alias_seen))
            SET_CHANGE (SALUT_CONTACT_ALIAS_CHANGED);
        }
      else if (last != NULL)
        {
          if (update_alias (self, last, &alias_seen))
            SET_CHANGE (SALUT_CONTACT_ALIAS_CHANGED);
        }
    }

  if (!alias_seen && priv->alias != NULL)
    {
      /* No alias anymore ? */
      g_free (priv->alias);
      priv->alias = NULL;
      SET_CHANGE (SALUT_CONTACT_ALIAS_CHANGED);
    }

  /* avatar token */
  if (avatar_token != NULL && tp_strdiff (self->avatar_token, avatar_token))
    {
      /* Purge the cache */
      purge_cached_avatar (self, avatar_token);
      SET_CHANGE (SALUT_CONTACT_AVATAR_CHANGED);
    }
  else if (avatar_token == NULL && self->avatar_token != NULL)
    {
      purge_cached_avatar (self, NULL);
      SET_CHANGE (SALUT_CONTACT_AVATAR_CHANGED);
    }

  /* jid */
  if (jid != NULL && tp_strdiff (self->jid, jid))
    {
      g_free (self->jid);
      self->jid = g_strdup (jid);
#ifdef ENABLE_OLPC
      SET_CHANGE (SALUT_CONTACT_OLPC_PROPERTIES);
#endif
    }

#ifdef  ENABLE_OLPC
  /* OLPC color */
  if (olpc_color != NULL && tp_strdiff (self->olpc_color, olpc_color))
    {
      g_free (self->olpc_color);
      self->olpc_color = g_strdup (olpc_color);
      SET_CHANGE (SALUT_CONTACT_OLPC_PROPERTIES);
    }

  /* current activity */
 if (current_act_id == NULL || current_act_room == 0)
   {
     DEBUG ("Unsetting current activity");
     if (self->olpc_cur_act != NULL || self->olpc_cur_act_room != 0)
       {
         g_free (self->olpc_cur_act);
         if (self->olpc_cur_act_room != 0)
           tp_handle_unref (room_repo, self->olpc_cur_act_room);
         self->olpc_cur_act = NULL;
         self->olpc_cur_act_room = 0;
         SET_CHANGE (SALUT_CONTACT_OLPC_CURRENT_ACTIVITY);
       }
  }
  else
    {
      DEBUG ("Current activity %s, room handle %d", current_act_id,
          current_act_room);
      if (tp_strdiff (self->olpc_cur_act, current_act_id) ||
          self->olpc_cur_act_room != current_act_room)
        {
          g_free (self->olpc_cur_act);
          if (self->olpc_cur_act_room != 0)
            tp_handle_unref (room_repo, self->olpc_cur_act_room);
          self->olpc_cur_act_room = current_act_room;
          tp_handle_ref (room_repo, current_act_room);
          self->olpc_cur_act = g_strdup (current_act_id);
          SET_CHANGE (SALUT_CONTACT_OLPC_CURRENT_ACTIVITY);
        }
    }

  /* OLPC key */
  if (olpc_key != NULL)
    {
      if (self->olpc_key == NULL || self->olpc_key->len != olpc_key->len ||
          memcmp (self->olpc_key->data, olpc_key->data, olpc_key->len) != 0)
            {
              if (self->olpc_key != NULL)
                {
                  g_array_free (self->olpc_key, TRUE);
                }
              self->olpc_key = g_array_sized_new (FALSE, FALSE, sizeof (guint8),
                  olpc_key->len);
              g_array_append_vals (self->olpc_key, olpc_key->data, olpc_key->len);
              SET_CHANGE (SALUT_CONTACT_OLPC_PROPERTIES);
            }
    }

  /* address */
  if (ip4_addr != NULL && tp_strdiff (ip4_addr, self->olpc_ip4))
    {
      g_free (self->olpc_ip4);
      self->olpc_ip4 = g_strdup (ip4_addr);
      SET_CHANGE (SALUT_CONTACT_OLPC_PROPERTIES);
    }

  if (ip6_addr != NULL && tp_strdiff (ip6_addr, self->olpc_ip4))
    {
      g_free (self->olpc_ip6);
      self->olpc_ip6 = g_strdup (ip6_addr);
      SET_CHANGE (SALUT_CONTACT_OLPC_PROPERTIES);
    }
#endif

  if (!self->found)
    {
      g_signal_emit (self, signals[FOUND], 0);
      /* Initially force updates of everything */
      SET_CHANGE (0xff);
      self->found = TRUE;
    }

  if (changes != 0)
    {
      g_signal_emit (self, signals[CONTACT_CHANGE], 0, changes);
    }
}

void
salut_contact_lost (SalutContact *self)
{
  self->status = SALUT_PRESENCE_OFFLINE;
  g_free (self->status_message);
  self->status_message = NULL;

  DEBUG_CONTACT (self, "disappeared from the local link");

  self->found = FALSE;
  g_signal_emit (self, signals[CONTACT_CHANGE], 0,
      SALUT_CONTACT_STATUS_CHANGED);
  g_signal_emit(self, signals[LOST], 0);
}

#ifdef ENABLE_OLPC
static void
activity_valid_cb (SalutOlpcActivity *activity,
                   SalutContact *self)
{
  /* Now we can emit the ActivitiesChanged signal */
  DEBUG ("activity in room %d (%s) is now valid", activity->room, activity->id);
  g_signal_emit (self, signals[CONTACT_CHANGE], 0,
      SALUT_CONTACT_OLPC_ACTIVITIES);
}

gboolean
salut_contact_joined_activity (SalutContact *self,
                               SalutOlpcActivity *activity)
{
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (self);

  if (g_hash_table_lookup (priv->olpc_activities,
        GUINT_TO_POINTER (activity->room)) != NULL)
    return FALSE;

  DEBUG_CONTACT (self, "joined activity %s", activity->id);
  g_hash_table_insert (priv->olpc_activities, GUINT_TO_POINTER (activity->room),
      activity);
  g_object_ref (activity);

  if (activity->id == NULL)
    {
      /* we can't emit the ActivitiesChanged signal right now as we don't have
       * the activity ID. Thanks OLPC interface */
      DEBUG ("activity in room %d isn't valid yet", activity->room);
      g_signal_connect (activity, "valid", G_CALLBACK (activity_valid_cb),
          self);
    }
  else
    {
      g_signal_emit (self, signals[CONTACT_CHANGE], 0,
          SALUT_CONTACT_OLPC_ACTIVITIES);
    }

  return TRUE;
}

void
salut_contact_left_activity (SalutContact *self,
                             SalutOlpcActivity *activity)
{
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (self);

  g_signal_handlers_disconnect_matched (activity, G_SIGNAL_MATCH_DATA, 0, 0,
      NULL, NULL, self);

  DEBUG_CONTACT (self, "left activity %s", activity->id);
  if (!g_hash_table_remove (priv->olpc_activities,
        GUINT_TO_POINTER (activity->room)))
    return;

  g_signal_emit (self, signals[CONTACT_CHANGE], 0,
      SALUT_CONTACT_OLPC_ACTIVITIES);
}
#endif
