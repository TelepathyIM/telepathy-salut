/*
 * olpc-activity-manager.c - Source for SalutOlpcActivityManager
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

#include "config.h"

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "olpc-activity-manager.h"

#include "connection.h"
#include "signals-marshal.h"

#define DEBUG_FLAG DEBUG_OLPC_ACTIVITY
#include "debug.h"

G_DEFINE_TYPE (SalutOlpcActivityManager, salut_olpc_activity_manager,
    G_TYPE_OBJECT);

/* properties */
enum {
  PROP_CONNECTION = 1,
  LAST_PROP
};

/* signal enum */
enum
{
  ACTIVITY_MODIFIED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _SalutOlpcActivityManagerPrivate SalutOlpcActivityManagerPrivate;

struct _SalutOlpcActivityManagerPrivate
{
  /* TpHandle (owned by the activity) => SalutOlpcActivity */
  GHashTable *activities_by_room;

  gboolean dispose_has_run;
};

#define SALUT_OLPC_ACTIVITY_MANAGER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_OLPC_ACTIVITY_MANAGER, SalutOlpcActivityManagerPrivate))

static void
salut_olpc_activity_manager_init (SalutOlpcActivityManager *self)
{
  SalutOlpcActivityManagerPrivate *priv =
    SALUT_OLPC_ACTIVITY_MANAGER_GET_PRIVATE (self);
   /* We just keep a weak reference on the activity object so we'll remove
    * it from the hash when no one is using anymore */
  priv->activities_by_room = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, NULL);
}

static void
salut_olpc_activity_manager_get_property (GObject *object,
                                          guint property_id,
                                          GValue *value,
                                          GParamSpec *pspec)
{
  SalutOlpcActivityManager *self = SALUT_OLPC_ACTIVITY_MANAGER (object);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, self->connection);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_olpc_activity_manager_set_property (GObject *object,
                                          guint property_id,
                                          const GValue *value,
                                          GParamSpec *pspec)
{
  SalutOlpcActivityManager *self = SALUT_OLPC_ACTIVITY_MANAGER (object);

  switch (property_id)
    {
      case PROP_CONNECTION:
        self->connection = g_value_get_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void salut_olpc_activity_manager_dispose (GObject *object);
static void salut_olpc_activity_manager_finalize (GObject *object);

static void
salut_olpc_activity_manager_class_init (SalutOlpcActivityManagerClass *salut_olpc_activity_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_olpc_activity_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_olpc_activity_manager_class,
                              sizeof (SalutOlpcActivityManagerPrivate));

  object_class->get_property = salut_olpc_activity_manager_get_property;
  object_class->set_property = salut_olpc_activity_manager_set_property;

  object_class->dispose = salut_olpc_activity_manager_dispose;
  object_class->finalize = salut_olpc_activity_manager_finalize;

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "The Salut Connection associated with this muc manager",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION,
      param_spec);

  signals[ACTIVITY_MODIFIED] = g_signal_new ("activity-modified",
      G_OBJECT_CLASS_TYPE (salut_olpc_activity_manager_class),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      salut_signals_marshal_VOID__OBJECT,
      G_TYPE_NONE, 1, SALUT_TYPE_OLPC_ACTIVITY);
}

static gboolean
remove_activity_foreach (gpointer room,
                         gpointer act,
                         gpointer activity)
{
  return act == activity;
}

static void
activity_finalized_cb (gpointer data,
                       GObject *activity)
{
  SalutOlpcActivityManager *self = SALUT_OLPC_ACTIVITY_MANAGER (data);
  SalutOlpcActivityManagerPrivate *priv =
    SALUT_OLPC_ACTIVITY_MANAGER_GET_PRIVATE (self);

  g_hash_table_foreach_remove (priv->activities_by_room,
      remove_activity_foreach, activity);
}

static gboolean
dispose_activity_foreach (gpointer room,
                          gpointer activity,
                          gpointer user_data)
{
  SalutOlpcActivityManager *self = SALUT_OLPC_ACTIVITY_MANAGER (user_data);

  g_object_weak_unref (G_OBJECT (activity), activity_finalized_cb, self);
  g_signal_handlers_disconnect_matched (activity, G_SIGNAL_MATCH_DATA,
      0, 0, NULL, NULL, self);

  return TRUE;
}

static void
salut_olpc_activity_manager_dispose (GObject *object)
{
  SalutOlpcActivityManager *self = SALUT_OLPC_ACTIVITY_MANAGER (object);
  SalutOlpcActivityManagerPrivate *priv = SALUT_OLPC_ACTIVITY_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->activities_by_room != NULL)
    {
      g_hash_table_foreach_remove (priv->activities_by_room,
          dispose_activity_foreach, self);
      g_hash_table_unref (priv->activities_by_room);
      priv->activities_by_room = NULL;
    }

  if (G_OBJECT_CLASS (salut_olpc_activity_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_olpc_activity_manager_parent_class)->dispose (object);
}

static void
salut_olpc_activity_manager_finalize (GObject *object)
{
  //SalutOlpcActivityManager *self = SALUT_OLPC_ACTIVITY_MANAGER (object);

  G_OBJECT_CLASS (salut_olpc_activity_manager_parent_class)->finalize (object);
}

gboolean
salut_olpc_activity_manager_start (SalutOlpcActivityManager *self,
                                   GError **error)
{
  return SALUT_OLPC_ACTIVITY_MANAGER_GET_CLASS (self)->start (self, error);
}

SalutOlpcActivity *
salut_olpc_activity_manager_get_activity_by_room (SalutOlpcActivityManager *self,
                                                  TpHandle room)
{
  SalutOlpcActivityManagerPrivate *priv =
    SALUT_OLPC_ACTIVITY_MANAGER_GET_PRIVATE (self);
  return g_hash_table_lookup (priv->activities_by_room,
      GUINT_TO_POINTER (room));
}

SalutOlpcActivity *
salut_olpc_activity_manager_get_activity_by_id (SalutOlpcActivityManager *self,
                                                const gchar *activity_id)
{
  SalutOlpcActivityManagerPrivate *priv =
    SALUT_OLPC_ACTIVITY_MANAGER_GET_PRIVATE (self);
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, priv->activities_by_room);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      SalutOlpcActivity *activity = value;
      if (strcmp (activity->id, activity_id) == 0)
        return activity;
    }

  return NULL;
}

SalutOlpcActivity *
salut_olpc_activity_manager_ensure_activity_by_room (
    SalutOlpcActivityManager *self,
    TpHandle room)
{
  SalutOlpcActivity *activity;
  SalutOlpcActivityManagerPrivate *priv =
    SALUT_OLPC_ACTIVITY_MANAGER_GET_PRIVATE (self);

  activity = g_hash_table_lookup (priv->activities_by_room,
      GUINT_TO_POINTER (room));

  if (activity != NULL)
    {
      return g_object_ref (activity);
    }
  else
    {
      activity = salut_olpc_activity_manager_create_activity (self, room);
      return activity;
    }
}

static void
activity_modified_cb (SalutOlpcActivity *activity,
                      SalutOlpcActivityManager *self)
{
  g_signal_emit (self, signals[ACTIVITY_MODIFIED], 0, activity);
}

SalutOlpcActivity *
salut_olpc_activity_manager_create_activity (SalutOlpcActivityManager *self,
                                             TpHandle room)
{
  SalutOlpcActivity *activity;
  SalutOlpcActivityManagerPrivate *priv =
    SALUT_OLPC_ACTIVITY_MANAGER_GET_PRIVATE (self);

  g_assert (room != 0);
  g_assert (g_hash_table_lookup (priv->activities_by_room, GUINT_TO_POINTER (
        room)) == NULL);

  activity = SALUT_OLPC_ACTIVITY_MANAGER_GET_CLASS (self)->create_activity (
      self);
  salut_olpc_activity_update (activity, room, NULL, NULL, NULL, NULL, NULL,
      TRUE);

  g_hash_table_insert (priv->activities_by_room, GUINT_TO_POINTER (room),
      activity);

  g_signal_connect (activity, "modified", G_CALLBACK (activity_modified_cb),
      self);
  g_object_weak_ref (G_OBJECT (activity), activity_finalized_cb , self);

  return activity;
}

SalutOlpcActivity *
salut_olpc_activity_manager_got_invitation (SalutOlpcActivityManager *self,
                                            TpHandle room,
                                            SalutContact *inviter,
                                            const gchar *id,
                                            const gchar *name,
                                            const gchar *type,
                                            const gchar *color,
                                            const gchar *tags)
{
  SalutOlpcActivity *activity;

  activity = salut_olpc_activity_manager_ensure_activity_by_room (self, room);

  salut_olpc_activity_update (activity, room, id, name, type, color, tags,
      activity->is_private);

  /* FIXME: we shouldn't add it if the local user is already in the activity
   * as, for now, we don't manage private activity membership (it's PS job) */

  /* add the inviter to the activity */
  salut_contact_joined_activity (inviter, activity);

  /* contact reffed the activity if it didn't hold a ref on it yet */
  g_object_unref (activity);

  return activity;
}

void
salut_olpc_activity_manager_contact_joined (SalutOlpcActivityManager *self,
                                            SalutContact *contact,
                                            SalutOlpcActivity *activity)
{
  salut_contact_joined_activity (contact, activity);
}

void
salut_olpc_activity_manager_contact_left (SalutOlpcActivityManager *mgr,
                                          SalutContact *contact,
                                          SalutOlpcActivity *activity)
{
  salut_contact_left_activity (contact, activity);
}
