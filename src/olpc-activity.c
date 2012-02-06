/*
 * olpc-activity.c - Source for SalutOlpcActivity
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

#include <wocky/wocky.h>

#include "contact-manager.h"
#include "olpc-activity.h"
#include "muc-manager.h"
#include "util.h"
#include "namespaces.h"

#include "signals-marshal.h"

#define DEBUG_FLAG DEBUG_OLPC_ACTIVITY
#include "debug.h"

G_DEFINE_TYPE (SalutOlpcActivity, salut_olpc_activity, G_TYPE_OBJECT);

/* properties */
enum {
  PROP_CONNECTION = 1,
  LAST_PROP
};

/* signal enum */
enum
{
  MODIFIED,
  VALID,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _SalutOlpcActivityPrivate SalutOlpcActivityPrivate;

struct _SalutOlpcActivityPrivate
{
  /* Handles of contacts we invited to join this activity */
  TpHandleSet *invited;
  /* can be NULL if we are not in the activity */
  SalutMucChannel *muc;

  gboolean dispose_has_run;
};

#define SALUT_OLPC_ACTIVITY_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_OLPC_ACTIVITY, SalutOlpcActivityPrivate))

static void
salut_olpc_activity_init (SalutOlpcActivity *self)
{
  self->connection = NULL;

  self->is_private = TRUE;
}

static void
salut_olpc_activity_get_property (GObject *object,
                                guint property_id,
                                GValue *value,
                                GParamSpec *pspec)
{
  SalutOlpcActivity *self = SALUT_OLPC_ACTIVITY (object);

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
salut_olpc_activity_set_property (GObject *object,
                                guint property_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
  SalutOlpcActivity *self = SALUT_OLPC_ACTIVITY (object);

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

static GObject *
salut_olpc_activity_constructor (GType type,
                                 guint n_props,
                                 GObjectConstructParam *props)
{
  GObject *obj;
  SalutOlpcActivity *self;
  SalutOlpcActivityPrivate *priv;
  TpHandleRepoIface *contact_repo;

  obj = G_OBJECT_CLASS (salut_olpc_activity_parent_class)->
    constructor (type, n_props, props);

  self = SALUT_OLPC_ACTIVITY (obj);
  priv = SALUT_OLPC_ACTIVITY_GET_PRIVATE (self);

  g_assert (self->connection != NULL);
  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->connection, TP_HANDLE_TYPE_CONTACT);

  priv->invited = tp_handle_set_new (contact_repo);

  return obj;
}

static void salut_olpc_activity_dispose (GObject *object);
static void salut_olpc_activity_finalize (GObject *object);

static void
salut_olpc_activity_class_init (SalutOlpcActivityClass *salut_olpc_activity_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_olpc_activity_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_olpc_activity_class,
                              sizeof (SalutOlpcActivityPrivate));

  object_class->get_property = salut_olpc_activity_get_property;
  object_class->set_property = salut_olpc_activity_set_property;

  object_class->constructor = salut_olpc_activity_constructor;
  object_class->dispose = salut_olpc_activity_dispose;
  object_class->finalize = salut_olpc_activity_finalize;

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "The Salut Connection associated with this muc manager",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION,
      param_spec);

  signals[MODIFIED] = g_signal_new ("modified",
      G_OBJECT_CLASS_TYPE (salut_olpc_activity_class),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      salut_signals_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  signals[VALID] = g_signal_new ("valid",
      G_OBJECT_CLASS_TYPE (salut_olpc_activity_class),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      salut_signals_marshal_VOID__VOID,
      G_TYPE_NONE, 0);
}

static void
salut_olpc_activity_dispose (GObject *object)
{
  SalutOlpcActivity *self = SALUT_OLPC_ACTIVITY (object);
  SalutOlpcActivityPrivate *priv = SALUT_OLPC_ACTIVITY_GET_PRIVATE (self);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles
    ((TpBaseConnection *) self->connection, TP_HANDLE_TYPE_ROOM);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (self->room != 0)
    {
      tp_handle_unref (room_repo, self->room);
      self->room = 0;
    }

  if (priv->muc != NULL)
    {
      g_signal_handlers_disconnect_matched (priv->muc, G_SIGNAL_MATCH_DATA,
          0, 0, NULL, NULL, self);
      g_object_unref (priv->muc);
      priv->muc = NULL;
    }

  tp_handle_set_destroy (priv->invited);

  if (G_OBJECT_CLASS (salut_olpc_activity_parent_class)->dispose)
    G_OBJECT_CLASS (salut_olpc_activity_parent_class)->dispose (object);
}

static void
salut_olpc_activity_finalize (GObject *object)
{
  SalutOlpcActivity *self = SALUT_OLPC_ACTIVITY (object);

  g_free (self->id);
  g_free (self->name);
  g_free (self->type);
  g_free (self->color);
  g_free (self->tags);

  G_OBJECT_CLASS (salut_olpc_activity_parent_class)->finalize (object);
}

GHashTable *
salut_olpc_activity_create_properties_table (SalutOlpcActivity *self)
{
  GHashTable *properties;
  GValue *val;

  properties = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);

  if (self->color != NULL)
    {
      val = tp_g_value_slice_new (G_TYPE_STRING);
      g_value_set_static_string (val, self->color);
      g_hash_table_insert (properties, "color", val);
    }

  if (self->name != NULL)
    {
      val = tp_g_value_slice_new (G_TYPE_STRING);
      g_value_set_static_string (val, self->name);
      g_hash_table_insert (properties, "name", val);
    }

  if (self->type != NULL)
    {
      val = tp_g_value_slice_new (G_TYPE_STRING);
      g_value_set_static_string (val, self->type);
      g_hash_table_insert (properties, "type", val);
    }

  if (self->tags != NULL)
    {
      val = tp_g_value_slice_new (G_TYPE_STRING);
      g_value_set_static_string (val, self->tags);
      g_hash_table_insert (properties, "tags", val);
    }

  val = tp_g_value_slice_new (G_TYPE_BOOLEAN);
  g_value_set_boolean (val, self->is_private);
  g_hash_table_insert (properties, "private", val);

  return properties;
}

static gboolean
send_properties_change_msg (SalutOlpcActivity *self,
                            GError **error)
{
  SalutOlpcActivityPrivate *priv = SALUT_OLPC_ACTIVITY_GET_PRIVATE (self);
  GHashTable *properties;
  GValue *activity_id_val;
  WockyStanza *stanza;
  WockyNode *top_node;
  WockyNode *properties_node;
  gchar *muc_name;
  GibberMucConnection *muc_connection;
  gboolean result;
  GError *err = NULL;

  if (priv->muc == NULL)
    /* we are not in the muc */
    return TRUE;

  g_object_get (priv->muc,
      "name", &muc_name,
      "muc-connection", &muc_connection,
      NULL);

  if (muc_connection->state != GIBBER_MUC_CONNECTION_CONNECTED)
    {
      DEBUG ("Muc connection not connected yet. Drop activity change message");
      g_object_unref (muc_connection);
      g_free (muc_name);
      return TRUE;
    }

  properties = salut_olpc_activity_create_properties_table (self);

  /* add the activity id */
  activity_id_val = g_slice_new0 (GValue);
  g_value_init (activity_id_val, G_TYPE_STRING);
  g_value_set_static_string (activity_id_val, self->id);
  g_hash_table_insert (properties, "id", activity_id_val);

  stanza = wocky_stanza_build (WOCKY_STANZA_TYPE_MESSAGE,
      WOCKY_STANZA_SUB_TYPE_GROUPCHAT,
      self->connection->name, muc_name,
      WOCKY_NODE_START, "properties",
        WOCKY_NODE_XMLNS, NS_OLPC_ACTIVITY_PROPS,
      WOCKY_NODE_END, NULL);
  top_node = wocky_stanza_get_top_node (stanza);

  properties_node = wocky_node_get_child_ns (top_node, "properties",
      NS_OLPC_ACTIVITY_PROPS);

  salut_wocky_node_add_children_from_properties (properties_node,
      properties, "property");

  result = gibber_muc_connection_send (muc_connection, stanza, &err);
  if (!result)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR, "%s",
          err->message);
      g_error_free (err);
    }

  g_object_unref (stanza);
  g_object_unref (muc_connection);
  g_free (muc_name);
  g_hash_table_unref (properties);

  return result;
}

static void
resend_invite_foreach (TpHandleSet *set,
                       TpHandle handle,
                       SalutOlpcActivity *self)
{
  SalutOlpcActivityPrivate *priv = SALUT_OLPC_ACTIVITY_GET_PRIVATE (self);
  GError *error = NULL;

  if (!salut_muc_channel_send_invitation (priv->muc, handle,
        "OLPC activity properties update", &error))
    {
      DEBUG ("failed to re-invite contact %d to activity %s", handle,
          self->id);
    }
}

static void
resend_invite (SalutOlpcActivity *self)
{
  SalutOlpcActivityPrivate *priv = SALUT_OLPC_ACTIVITY_GET_PRIVATE (self);

  if (priv->muc == NULL)
    /* we are not in the muc */
    return;

  /* Resend pending invitations so contacts will know about new properties */
  tp_handle_set_foreach (priv->invited,
      (TpHandleSetMemberFunc) resend_invite_foreach, self);
}

static void
activity_changed (SalutOlpcActivity *self)
{
  SalutOlpcActivityPrivate *priv = SALUT_OLPC_ACTIVITY_GET_PRIVATE (self);
  GError *error = NULL;

  if (!send_properties_change_msg (self, &error))
    {
      DEBUG ("send properties changes msg failed: %s", error->message);
      g_error_free (error);
      error = NULL;
    }

  if (!self->is_private && priv->muc != NULL)
    {
      /* update announcement */
      if (!SALUT_OLPC_ACTIVITY_GET_CLASS (self)->update (self, &error))
        {
          DEBUG ("update activity failed: %s", error->message);
          g_error_free (error);
        }
    }
  else
    {
      resend_invite (self);
    }
}

static gboolean
salut_olpc_activity_announce (SalutOlpcActivity *self,
                              GError **error)
{
  GError *err = NULL;

  if (!SALUT_OLPC_ACTIVITY_GET_CLASS (self)->announce (self, &err))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR, "%s",
          err->message);
      g_error_free (err);
      return FALSE;
    }

  return TRUE;
}

static void
salut_olpc_activity_stop_announce (SalutOlpcActivity *self)
{
  SALUT_OLPC_ACTIVITY_GET_CLASS (self)->stop_announce (self);
}

gboolean
salut_olpc_activity_update (SalutOlpcActivity *self,
                            TpHandle room,
                            const gchar *id,
                            const gchar *name,
                            const gchar *type,
                            const gchar *color,
                            const gchar *tags,
                            gboolean is_private)
{
  SalutOlpcActivityPrivate *priv = SALUT_OLPC_ACTIVITY_GET_PRIVATE (self);
  TpBaseConnection *base_conn = (TpBaseConnection *) (self->connection);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (base_conn,
       TP_HANDLE_TYPE_ROOM);
  gboolean changed = FALSE;
  GError *error = NULL;
  gboolean become_valid = FALSE;

  if (room != 0 && room != self->room)
    {
      if (self->room != 0)
        {
          tp_handle_unref (room_repo, self->room);
        }

      self->room = room;
      tp_handle_ref (room_repo, room);
    }

  if (id != NULL && tp_strdiff (id, self->id))
    {
      if (self->id == NULL)
        become_valid = TRUE;

      g_free (self->id);
      self->id = g_strdup (id);
      changed = TRUE;
    }

  if (name != NULL && tp_strdiff (name, self->name))
    {
      g_free (self->name);
      self->name = g_strdup (name);
      changed = TRUE;
    }

  if (type != NULL && tp_strdiff (type, self->type))
    {
      g_free (self->type);
      self->type = g_strdup (type);
      changed = TRUE;
    }

  if (color != NULL && tp_strdiff (color, self->color))
    {
      g_free (self->color);
      self->color = g_strdup (color);
      changed = TRUE;
    }

  if (tp_strdiff (tags, self->tags))
    {
      g_free (self->tags);
      self->tags = g_strdup (tags);
      changed = TRUE;
    }

  if (is_private != self->is_private)
    {
      self->is_private = is_private;
      changed = TRUE;

      if (priv->muc != NULL)
        {
          if (is_private)
            {
              DEBUG ("activity is not public anymore. Stop to announce it");
              salut_olpc_activity_stop_announce (self);
            }
          else
            {
              DEBUG ("activity becomes public. Announce it");
             if (!salut_olpc_activity_announce (self, &error))
               {
                 DEBUG ("activity announce failed: %s", error->message);
                 g_error_free (error);
                  error = NULL;
               }
            }
        }
    }

  if (become_valid)
    {
      g_signal_emit (self, signals[VALID], 0);
    }

  if (changed)
    {
      activity_changed (self);

      g_signal_emit (self, signals[MODIFIED], 0);
    }

  return changed;
}

static void
muc_channel_closed_cb (SalutMucChannel *chan,
                       SalutOlpcActivity *self)
{
  SalutOlpcActivityPrivate *priv = SALUT_OLPC_ACTIVITY_GET_PRIVATE (self);

  g_object_unref (priv->muc);
  priv->muc = NULL;
}

gboolean
salut_olpc_activity_joined (SalutOlpcActivity *self,
                            GError **error)
{
  SalutOlpcActivityPrivate *priv = SALUT_OLPC_ACTIVITY_GET_PRIVATE (self);
  TpHandleRepoIface *room_repo;
  SalutMucManager *muc_manager;

  if (priv->muc != NULL)
    return TRUE;

  room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->connection, TP_HANDLE_TYPE_ROOM);

  g_object_get (self->connection,
      "muc-manager", &muc_manager,
      NULL);

  priv->muc = salut_muc_manager_get_text_channel (muc_manager, self->room);
  g_object_unref (muc_manager);

  if (priv->muc == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Can't find muc channel for room %s", tp_handle_inspect (
            room_repo, self->room));
      return FALSE;
    }

  if (!self->is_private)
    {
      /* This might fail but that doesn't prevent us from joining the
       * activity.. */
      salut_olpc_activity_announce (self, NULL);
    }

  g_signal_connect (priv->muc, "closed", G_CALLBACK (muc_channel_closed_cb),
      self);

  return TRUE;
}

void
salut_olpc_activity_left (SalutOlpcActivity *self)
{
  SalutOlpcActivityPrivate *priv = SALUT_OLPC_ACTIVITY_GET_PRIVATE (self);

  if (priv->muc == NULL)
    return;

  if (!self->is_private)
    salut_olpc_activity_stop_announce (self);

  g_object_unref (priv->muc);
  g_signal_handlers_disconnect_matched (priv->muc, G_SIGNAL_MATCH_DATA,
      0, 0, NULL, NULL, self);
  priv->muc = NULL;
}

void
salut_olpc_activity_revoke_invitations (SalutOlpcActivity *self)
{
  SalutOlpcActivityPrivate *priv = SALUT_OLPC_ACTIVITY_GET_PRIVATE (self);
  WockyStanza *msg;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->connection, TP_HANDLE_TYPE_CONTACT);
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->connection, TP_HANDLE_TYPE_CONTACT);
  TpIntSetIter iter = TP_INTSET_ITER_INIT (tp_handle_set_peek (
        priv->invited));
  SalutContactManager *contact_mgr;
  WockyNode *top_node;

  if (tp_handle_set_size (priv->invited) <= 0)
    return;

  msg = wocky_stanza_build (WOCKY_STANZA_TYPE_MESSAGE,
      WOCKY_STANZA_SUB_TYPE_NONE,
      self->connection->name, NULL,
      WOCKY_NODE_START, "uninvite",
        WOCKY_NODE_XMLNS, NS_OLPC_ACTIVITY_PROPS,
        WOCKY_NODE_ATTRIBUTE, "room", tp_handle_inspect (room_repo,
          self->room),
        WOCKY_NODE_ATTRIBUTE, "id", self->id,
      WOCKY_NODE_END, NULL);
  top_node = wocky_stanza_get_top_node (msg);

  g_object_get (self->connection,
      "contact-manager", &contact_mgr,
      NULL);
  g_assert (contact_mgr != NULL);

  DEBUG ("revoke invitations for activity %s", self->id);
  while (tp_intset_iter_next (&iter))
    {
      TpHandle contact_handle;
      SalutContact *contact;
      const gchar *to;

      contact_handle = iter.element;
      contact = salut_contact_manager_get_contact (contact_mgr, contact_handle);
      if (contact == NULL)
        {
          DEBUG ("Can't find contact %d", contact_handle);
          continue;
        }

      to = tp_handle_inspect (contact_repo, contact_handle);
      wocky_node_set_attribute (top_node, "to", to);

      wocky_stanza_set_to_contact (msg, WOCKY_CONTACT (contact));
      wocky_porter_send (self->connection->porter, msg);

      g_object_unref (contact);
    }

  g_object_unref (msg);
  g_object_unref (contact_mgr);
}

void
salut_olpc_activity_augment_invitation (SalutOlpcActivity *self,
                                        TpHandle contact,
                                        WockyNode *invite_node)
{
  SalutOlpcActivityPrivate *priv = SALUT_OLPC_ACTIVITY_GET_PRIVATE (self);
  WockyNode *properties_node;
  GHashTable *properties;
  GValue *activity_id_val;

  properties = salut_olpc_activity_create_properties_table (self);

  properties_node = wocky_node_add_child_ns (invite_node, "properties",
      NS_OLPC_ACTIVITY_PROPS);

  /* add the activity id */
  activity_id_val = g_slice_new0 (GValue);
  g_value_init (activity_id_val, G_TYPE_STRING);
  g_value_set_static_string (activity_id_val, self->id);
  g_hash_table_insert (properties, "id", activity_id_val);

  salut_wocky_node_add_children_from_properties (properties_node,
      properties, "property");

  tp_handle_set_add (priv->invited, contact);

  g_hash_table_unref (properties);
}

gboolean
salut_olpc_activity_remove_invited (SalutOlpcActivity *self,
                                   TpHandle contact)
{
  SalutOlpcActivityPrivate *priv = SALUT_OLPC_ACTIVITY_GET_PRIVATE (self);

  return tp_handle_set_remove (priv->invited, contact);
}
