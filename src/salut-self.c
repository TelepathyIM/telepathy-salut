/*
 * salut-self.c - Source for SalutSelf
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#include "salut-self.h"

#include <gibber/gibber-linklocal-transport.h>
#include <gibber/gibber-namespaces.h>
#include <gibber/gibber-muc-connection.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/util.h>

#include "salut-contact-manager.h"
#include "salut-util.h"
#include "salut-muc-manager.h"
#include "salut-xmpp-connection-manager.h"

#ifdef ENABLE_OLPC
#include "salut-olpc-activity.h"
#include "salut-olpc-activity-manager.h"
#endif

#define DEBUG_FLAG DEBUG_SELF
#include <debug.h>

G_DEFINE_TYPE (SalutSelf, salut_self, G_TYPE_OBJECT)

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_NICKNAME,
  PROP_FIRST_NAME,
  PROP_LAST_NAME,
  PROP_JID,
  PROP_EMAIL,
  PROP_PUBLISHED_NAME,
#ifdef ENABLE_OLPC
  PROP_OLPC_KEY,
  PROP_OLPC_COLOR
#endif
};

/* signal enum */
enum
{
  ESTABLISHED,
  FAILURE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _SalutSelfPrivate SalutSelfPrivate;

struct _SalutSelfPrivate
{
  SalutContactManager *contact_manager;
  TpHandleRepoIface *room_repo;
#ifdef ENABLE_OLPC
  SalutOlpcActivityManager *olpc_activity_manager;
#endif

  GIOChannel *listener;
  guint io_watch_in;

#ifdef ENABLE_OLPC
  /* room handle owned by the SalutOlpcActivity -> SalutOlpcActivity */
  GHashTable *olpc_activities;
#endif

  gboolean dispose_has_run;
};

#ifdef ENABLE_OLPC
void
contact_manager_contact_change_cb (SalutContactManager *mgr,
    SalutContact *contact, int changes, gpointer user_data);
#endif

#define SALUT_SELF_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_SELF, SalutSelfPrivate))

static void
salut_self_init (SalutSelf *obj)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  obj->status = SALUT_PRESENCE_AVAILABLE;
  obj->status_message = NULL;
  obj->jid = NULL;
#ifdef ENABLE_OLPC
  obj->olpc_key = NULL;
  obj->olpc_color = NULL;
  obj->olpc_cur_act = NULL;
  obj->olpc_cur_act_room = 0;
#endif

  obj->first_name = NULL;
  obj->last_name = NULL;
  obj->email = NULL;
  obj->published_name = NULL;

#ifdef ENABLE_OLPC
  priv->olpc_activities = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) g_object_unref);
#endif
  priv->listener = NULL;
}

static void
salut_self_get_property (GObject *object,
                         guint property_id,
                         GValue *value,
                         GParamSpec *pspec)
{
  SalutSelf *self = SALUT_SELF (object);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, self->connection);
        break;
      case PROP_NICKNAME:
        g_value_set_string (value, self->nickname);
        break;
      case PROP_FIRST_NAME:
        g_value_set_string (value, self->first_name);
        break;
      case PROP_LAST_NAME:
        g_value_set_string (value, self->last_name);
        break;
      case PROP_JID:
        g_value_set_string (value, self->jid);
        break;
      case PROP_EMAIL:
        g_value_set_string (value, self->email);
        break;
      case PROP_PUBLISHED_NAME:
        g_value_set_string (value, self->published_name);
        break;
#ifdef ENABLE_OLPC
      case PROP_OLPC_KEY:
        g_value_set_pointer (value, self->olpc_key);
        break;
      case PROP_OLPC_COLOR:
        g_value_set_string (value, self->olpc_color);
        break;
#endif
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_self_set_property (GObject *object,
                         guint property_id,
                         const GValue *value,
                         GParamSpec *pspec)
{
  SalutSelf *self = SALUT_SELF (object);
#ifdef ENABLE_OLPC
  GArray *arr;
#endif

  switch (property_id)
    {
      case PROP_CONNECTION:
        self->connection = g_value_get_object (value);
        break;
      case PROP_NICKNAME:
        g_free (self->nickname);
        self->nickname = g_value_dup_string (value);
        break;
      case PROP_FIRST_NAME:
        g_free (self->first_name);
        self->first_name = g_value_dup_string (value);
        break;
      case PROP_LAST_NAME:
        g_free (self->last_name);
        self->last_name = g_value_dup_string (value);
        break;
      case PROP_JID:
        g_free (self->jid);
        self->jid = g_value_dup_string (value);
        break;
      case PROP_EMAIL:
        g_free (self->email);
        self->email = g_value_dup_string (value);
        break;
      case PROP_PUBLISHED_NAME:
        g_free (self->published_name);
        self->published_name = g_value_dup_string (value);
        break;
#ifdef ENABLE_OLPC
      case PROP_OLPC_KEY:
        arr = g_value_get_pointer (value);
        if (arr != NULL)
          {
            if (self->olpc_key != NULL)
              g_array_free (self->olpc_key, TRUE);

            self->olpc_key = g_array_sized_new (FALSE, FALSE, sizeof (guint8),
                arr->len);
            g_array_append_vals (self->olpc_key, arr->data, arr->len);
          }
        break;
      case PROP_OLPC_COLOR:
        g_free (self->olpc_color);
        self->olpc_color = g_value_dup_string (value);
        break;
#endif
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
salut_self_constructor (GType type,
                        guint n_props,
                        GObjectConstructParam *props)
{
  GObject *obj;
  SalutSelf *self;
  SalutSelfPrivate *priv;

  obj = G_OBJECT_CLASS (salut_self_parent_class)->
    constructor (type, n_props, props);

  self = SALUT_SELF (obj);
  priv = SALUT_SELF_GET_PRIVATE (self);

  g_assert (self->connection != NULL);
  g_object_get (self->connection,
      "contact-manager", &(priv->contact_manager),
#ifdef ENABLE_OLPC
      "olpc-activity-manager", &(priv->olpc_activity_manager),
#endif
      NULL);
  g_assert (priv->contact_manager != NULL);
#ifdef ENABLE_OLPC
  g_assert (priv->olpc_activity_manager != NULL);
#endif

  priv->room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->connection, TP_HANDLE_TYPE_ROOM);

  /* Prefer using the nickname as alias */
  if (self->nickname != NULL)
    {
      self->alias = g_strdup (self->nickname);
    }
  else
    {
      if (self->first_name != NULL)
        {
          if (self->last_name != NULL)
            self->alias = g_strdup_printf ("%s %s", self->first_name,
                self->last_name);
          else
            self->alias = g_strdup (self->first_name);
        }
      else if (self->last_name != NULL)
        {
          self->alias = g_strdup (self->last_name);
        }
    }

#ifdef ENABLE_OLPC
  g_signal_connect (priv->contact_manager, "contact-change",
      G_CALLBACK (contact_manager_contact_change_cb), self);
#endif

  return obj;
}

static void salut_self_dispose (GObject *object);
static void salut_self_finalize (GObject *object);

static void
salut_self_class_init (SalutSelfClass *salut_self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_self_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_self_class, sizeof (SalutSelfPrivate));

  object_class->constructor = salut_self_constructor;
  object_class->get_property = salut_self_get_property;
  object_class->set_property = salut_self_set_property;

  object_class->dispose = salut_self_dispose;
  object_class->finalize = salut_self_finalize;

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "The Salut Connection associated with this self object",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION,
      param_spec);

  param_spec = g_param_spec_string (
      "nickname",
      "the nickname",
      "The nickname of the self user",
      NULL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_NICKNAME,
      param_spec);

  param_spec = g_param_spec_string (
      "first-name",
      "the first name",
      "The first name of the self user",
      NULL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_FIRST_NAME,
      param_spec);

  param_spec = g_param_spec_string (
      "last-name",
      "the last name",
      "The last name of the self user",
      NULL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_LAST_NAME,
      param_spec);

  param_spec = g_param_spec_string (
      "jid",
      "the jid",
      "The jabber ID of the self user",
      NULL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_JID,
      param_spec);

  param_spec = g_param_spec_string (
      "email",
      "the email",
      "The email of the self user",
      NULL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_EMAIL,
      param_spec);

  param_spec = g_param_spec_string (
      "published-name",
      "the published name",
      "The name used to publish the presence service",
      g_get_user_name (),
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PUBLISHED_NAME,
      param_spec);

#ifdef ENABLE_OLPC
  param_spec = g_param_spec_pointer (
      "olpc-key",
      "the OLPC key",
      "A pointer to a GArray containing the OLPC key",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_OLPC_KEY,
      param_spec);

  param_spec = g_param_spec_string (
      "olpc-color",
      "the OLPC color",
      "The OLPC color of the self user",
      NULL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_OLPC_COLOR,
      param_spec);
#endif

  signals[ESTABLISHED] =
    g_signal_new ("established",
                  G_OBJECT_CLASS_TYPE (salut_self_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[FAILURE] =
    g_signal_new ("failure",
                  G_OBJECT_CLASS_TYPE (salut_self_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__POINTER,
                  G_TYPE_NONE, 0);
}

void
salut_self_dispose (GObject *object)
{
  SalutSelf *self = SALUT_SELF (object);
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (priv->contact_manager != NULL)
    {
      g_object_unref (priv->contact_manager);
      priv->contact_manager = NULL;
    }

#ifdef ENABLE_OLPC
  if (priv->olpc_activity_manager != NULL)
    {
      g_object_unref (priv->olpc_activity_manager);
      priv->olpc_activity_manager = NULL;
    }

  if (priv->olpc_activities != NULL)
    g_hash_table_destroy (priv->olpc_activities);

  if (self->olpc_cur_act_room != 0)
    {
      tp_handle_unref (priv->room_repo, self->olpc_cur_act_room);
      self->olpc_cur_act_room = 0;
    }
#endif

  priv->room_repo = NULL;

  if (priv->listener)
    {
      g_io_channel_unref (priv->listener);
      g_source_remove (priv->io_watch_in);
      priv->listener = NULL;
    }

  if (G_OBJECT_CLASS (salut_self_parent_class)->dispose)
    G_OBJECT_CLASS (salut_self_parent_class)->dispose (object);
}

void
salut_self_finalize (GObject *object)
{
  SalutSelf *self = SALUT_SELF (object);

  /* free any data held directly by the object here */

  g_free (self->jid);
  g_free (self->name);

  g_free (self->first_name);
  g_free (self->last_name);
  g_free (self->email);
  g_free (self->published_name);
  g_free (self->alias);
#ifdef ENABLE_OLPC
  if (self->olpc_key != NULL)
    g_array_free (self->olpc_key, TRUE);
  g_free (self->olpc_color);
  g_free (self->olpc_cur_act);
#endif

  G_OBJECT_CLASS (salut_self_parent_class)->finalize (object);
}

/* Start announcing our presence on the network */
gboolean
salut_self_announce (SalutSelf *self,
                     gint port,
                     GError **error)
{
  return SALUT_SELF_GET_CLASS (self)->announce (self, port, error);
}

gboolean
salut_self_set_presence (SalutSelf *self, SalutPresenceId status,
    const gchar *message, GError **error)
{

  g_assert (status >= 0 && status < SALUT_PRESENCE_NR_PRESENCES);

  self->status = status;
  g_free (self->status_message);
  self->status_message = g_strdup (message);

  return SALUT_SELF_GET_CLASS (self)->set_presence (self, error);
}

const gchar *
salut_self_get_alias (SalutSelf *self)
{
  if (self->alias == NULL)
    {
      return self->name;
    }
  return self->alias;
}

gboolean
salut_self_set_alias (SalutSelf *self, const gchar *alias, GError **error)
{
  gboolean ret;
  GError *err = NULL;

  g_free (self->alias);
  g_free (self->nickname);
  self->alias = g_strdup (alias);
  self->nickname = g_strdup (alias);

  ret = SALUT_SELF_GET_CLASS (self)->set_alias (self, &err);
  if (!ret)
    {
      if (error != NULL)
        *error = g_error_new_literal (TP_ERRORS, TP_ERROR_NETWORK_ERROR,
            err->message);
      g_error_free (err);
    }
  return ret;
}

static void
salut_self_remove_avatar (SalutSelf *self)
{
  DEBUG ("Removing avatar");

  SALUT_SELF_GET_CLASS (self)->remove_avatar (self);
}

gboolean
salut_self_set_avatar (SalutSelf *self, guint8 *data,
    gsize size, GError **error)
{
  gboolean ret = TRUE;
  GError *err = NULL;

  g_free (self->avatar_token);
  self->avatar_token = NULL;

  g_free (self->avatar);
  self->avatar = NULL;

  self->avatar_size = 0;

  if (size == 0)
    {
      self->avatar_token = g_strdup ("");
      salut_self_remove_avatar (self);
      return TRUE;
    }

  ret = SALUT_SELF_GET_CLASS (self)->set_avatar (self, data, size, error);

  if (!ret)
    {
      salut_self_remove_avatar (self);
      if (error != NULL)
        *error = g_error_new_literal (TP_ERRORS, TP_ERROR_NETWORK_ERROR,
            err->message);
      g_error_free (err);
    }

  return ret;
}

#ifdef ENABLE_OLPC

static SalutOlpcActivity *
salut_self_add_olpc_activity (SalutSelf *self, const gchar *activity_id,
    TpHandle room, GError **error)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  SalutOlpcActivity *activity;

  g_return_val_if_fail (activity_id != NULL, NULL);
  g_return_val_if_fail (room != 0, NULL);

  if (strchr (activity_id, ':') != NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Activity IDs may not contain ':'");
      return NULL;
    }

  activity = salut_olpc_activity_manager_ensure_activity_by_room (
      priv->olpc_activity_manager, room);

  if (!salut_olpc_activity_joined (activity, error))
    {
      g_object_unref (activity);
      return NULL;
    }

  salut_olpc_activity_update (activity, room, activity_id, NULL, NULL, NULL,
      NULL, activity->is_private);

  g_hash_table_insert (priv->olpc_activities, GUINT_TO_POINTER (room),
      activity);

  return activity;
}

struct _set_olpc_activities_ctx
{
  SalutSelf *self;
  TpHandleRepoIface *room_repo;
  GHashTable *olpc_activities;
  GHashTable *room_to_act_id;
  GError **error;
};

static void
_set_olpc_activities_add (gpointer key, gpointer value, gpointer user_data)
{
  struct _set_olpc_activities_ctx *data = user_data;
  SalutOlpcActivity *activity;
  const gchar *id = (const gchar *) value;
  TpHandle room = GPOINTER_TO_UINT (key);

  if (*(data->error) != NULL)
    {
      /* we already lost */
      return;
    }

  activity = g_hash_table_lookup (data->olpc_activities, key);
  if (activity == NULL)
    {
      /* add the activity service if it's not in data->olpc_activities */
      activity = salut_self_add_olpc_activity (data->self, id, room,
          data->error);

      if (activity == NULL)
        return;
    }
  else
    {
      /* activity was already known */
      salut_olpc_activity_update (activity, room, id, NULL, NULL, NULL,
          NULL, activity->is_private);
    }
}

static gboolean
_set_olpc_activities_delete (gpointer key, gpointer value, gpointer user_data)
{
  SalutOlpcActivity *activity = (SalutOlpcActivity *) value;
  struct _set_olpc_activities_ctx *data = user_data;
  gboolean remove;

  /* delete the activity service if it's not in data->room_to_act_id */
  remove = (g_hash_table_lookup (data->room_to_act_id, key) == NULL);

  if (remove)
    {
      salut_olpc_activity_left (activity);
      salut_olpc_activity_revoke_invitations (activity);
    }

  return remove;
}

gboolean
salut_self_set_olpc_activities (SalutSelf *self,
                                GHashTable *room_to_act_id,
                                GError **error)
{
  GError *e = NULL;
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  struct _set_olpc_activities_ctx data = { self, priv->room_repo,
      priv->olpc_activities, room_to_act_id, &e };

  /* delete any which aren't in room_to_act_id. Can't fail */
  g_hash_table_foreach_remove (priv->olpc_activities,
      _set_olpc_activities_delete, &data);

  /* add any which aren't in olpc_activities */
  g_hash_table_foreach (room_to_act_id, _set_olpc_activities_add, &data);

  if (error != NULL)
    *error = e;
  return (e == NULL);
}

gboolean
salut_self_set_olpc_current_activity (SalutSelf *self,
                                      const gchar *id,
                                      TpHandle room,
                                      GError **error)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  GError *err = NULL;
  const gchar *room_name;

  g_return_val_if_fail (id != NULL, FALSE);

  /* if one of id and room is empty, require the other to be */
  if (room == 0)
    {
      room_name = "";

      if (id[0] != '\0')
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "In SetCurrentActivity, activity ID must be \"\" if room handle "
              "is 0");
          return FALSE;
        }
    }
  else
    {
      room_name = tp_handle_inspect (priv->room_repo, room);

      if (id[0] == '\0')
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "In SetCurrentActivity, activity ID must not be \"\" if room "
              "handle is non-zero");
          return FALSE;
        }
    }

  g_free (self->olpc_cur_act);
  self->olpc_cur_act = g_strdup (id);

  if (self->olpc_cur_act_room != 0)
    tp_handle_unref (priv->room_repo, self->olpc_cur_act_room);
  self->olpc_cur_act_room = room;
  if (room != 0)
    tp_handle_ref (priv->room_repo, room);

  if (!SALUT_SELF_GET_CLASS (self)->update_current_activity (self, room_name,
        &err))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR, err->message);
      g_error_free (err);
      return FALSE;
    }

  return TRUE;
}

gboolean
salut_self_set_olpc_activity_properties (SalutSelf *self,
                                         TpHandle handle,
                                         const gchar *color,
                                         const gchar *name,
                                         const gchar *type,
                                         const gchar *tags,
                                         gboolean is_private,
                                         GError **error)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  SalutOlpcActivity *activity;

  activity = g_hash_table_lookup (priv->olpc_activities,
      GUINT_TO_POINTER (handle));

  if (activity == NULL)
    {
      /* User have to call org.laptop.Telepathy.BuddyInfo.SetActivities
       * to create the activity */
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "No activity associated with room having handle %d", handle);
      return FALSE;
    }

  salut_olpc_activity_update (activity, handle, activity->id,
      name, type, color, tags, is_private);

  return TRUE;
}

gboolean
salut_self_set_olpc_properties (SalutSelf *self,
                                const GArray *key,
                                const gchar *color,
                                const gchar *jid,
                                GError **error)
{
  GError *err = NULL;

  if (key != NULL)
    {
      if (self->olpc_key == NULL)
        {
          self->olpc_key = g_array_sized_new (FALSE, FALSE, sizeof (guint8),
              key->len);
        }
      else
        {
          g_array_remove_range (self->olpc_key, 0, self->olpc_key->len);
        }

      g_array_append_vals (self->olpc_key, key->data, key->len);
    }

  if (color != NULL)
    {
      g_free (self->olpc_color);
      self->olpc_color = g_strdup (color);
    }

  if (jid != NULL)
    {
      g_free (self->jid);
      self->jid = g_strdup (jid);
    }

  if (!SALUT_SELF_GET_CLASS (self)->set_olpc_properties (self, key, color, jid,
        &err))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR, err->message);
      g_error_free (err);
      return FALSE;
    }
  return TRUE;
}

typedef struct
{
  SalutSelfOLPCActivityFunc foreach;
  gpointer user_data;
} foreach_olpc_activity_ctx;

static void
foreach_olpc_activity (gpointer key, gpointer value, gpointer user_data)
{
  foreach_olpc_activity_ctx *ctx = user_data;
  SalutOlpcActivity *activity = value;

  DEBUG ("%s -> %u", activity->id, GPOINTER_TO_UINT (key));
  (ctx->foreach) (activity, ctx->user_data);
}

void
salut_self_foreach_olpc_activity (SalutSelf *self,
                                  SalutSelfOLPCActivityFunc foreach,
                                  gpointer user_data)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  foreach_olpc_activity_ctx ctx = { foreach, user_data };

  DEBUG ("called");

  g_hash_table_foreach (priv->olpc_activities, foreach_olpc_activity,
      &ctx);

  DEBUG ("end");
}

void
salut_self_olpc_augment_invitation (SalutSelf *self,
                                    TpHandle room,
                                    TpHandle contact,
                                    GibberXmppNode *invite_node)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  SalutOlpcActivity *activity;

  activity = g_hash_table_lookup (priv->olpc_activities,
      GUINT_TO_POINTER (room));
  if (activity == NULL)
    return;

  salut_olpc_activity_augment_invitation (activity, contact, invite_node);
}

typedef struct
{
  GHashTable *olpc_activities;
  TpHandle contact_handle;
} remove_from_invited_ctx;

static void
remove_from_invited (SalutOlpcActivity *act,
                     gpointer user_data)
{
  SalutOlpcActivity *activity;
  remove_from_invited_ctx *data = (remove_from_invited_ctx *) user_data;

  activity = g_hash_table_lookup (data->olpc_activities,
      GUINT_TO_POINTER (act->room));
  if (activity == NULL)
    return;

  if (salut_olpc_activity_remove_invited (activity, data->contact_handle))
    DEBUG ("contact %d joined activity %s. Remove it from the invited list",
        data->contact_handle, activity->id);
}

/* when a buddy changes his activity list, check if we invited him
 * to this activity and remove him from the invited set */
void
contact_manager_contact_change_cb (SalutContactManager *mgr,
                                   SalutContact *contact,
                                   int changes,
                                   gpointer user_data)
{
  SalutSelf *self = SALUT_SELF (user_data);
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (
      TP_BASE_CONNECTION (self->connection), TP_HANDLE_TYPE_CONTACT);
  TpHandle handle;
  remove_from_invited_ctx data;

  if (!(changes & SALUT_CONTACT_OLPC_ACTIVITIES))
    return;

  handle = tp_handle_lookup (handle_repo, contact->name, NULL, NULL);

  data.olpc_activities = priv->olpc_activities;
  data.contact_handle = handle;
  salut_contact_foreach_olpc_activity (contact, remove_from_invited, &data);
}
#endif /* ENABLE_OLPC */

void
salut_self_established (SalutSelf *self)
{
  g_signal_emit (self, signals[ESTABLISHED], 0, NULL);
}
