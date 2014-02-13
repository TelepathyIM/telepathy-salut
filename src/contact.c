/*
 * contact.c - Source for salut_contact
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

#include "config.h"
#include "contact.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "presence.h"
#include "presence-cache.h"
#include "enumtypes.h"

#include <telepathy-glib/telepathy-glib.h>

#define DEBUG_FLAG DEBUG_CONTACTS
#include <debug.h>

#define DEBUG_CONTACT(contact, format, ...) G_STMT_START {      \
  DEBUG ("Contact %s: " format, contact->name, ##__VA_ARGS__);  \
} G_STMT_END

static void xep_0115_capabilities_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (SalutContact, salut_contact, WOCKY_TYPE_LL_CONTACT,
    G_IMPLEMENT_INTERFACE (WOCKY_TYPE_XEP_0115_CAPABILITIES,
        xep_0115_capabilities_iface_init);
)

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

struct _SalutContactPrivate
{
  gboolean dispose_has_run;
  gchar *alias;
  GList *avatar_requests;
  gboolean found;
  gboolean frozen;
  guint pending_changes;
};

static GObject *
salut_contact_constructor (GType type,
                           guint n_props,
                           GObjectConstructParam *props)
{
  GObject *obj;
  SalutContact *self;
  TpHandleRepoIface *contact_repo;

  obj = G_OBJECT_CLASS (salut_contact_parent_class)->
    constructor (type, n_props, props);

  self = SALUT_CONTACT (obj);

  g_assert (self->connection != NULL);
  g_assert (self->name != NULL);

  contact_repo = tp_base_connection_get_handles
      ((TpBaseConnection *) self->connection, TP_ENTITY_TYPE_CONTACT);

  self->handle = tp_handle_ensure (contact_repo, self->name, NULL, NULL);

  self->caps = gabble_capability_set_new ();
  self->data_forms = g_ptr_array_new_with_free_func (g_object_unref);

  return obj;
}

static void
connection_status_changed_cb (SalutConnection *connection,
    guint status,
    guint reason,
    SalutContact *self)
{
  if (status == TP_CONNECTION_STATUS_DISCONNECTED)
    self->connection = NULL;
}

static void
salut_contact_constructed (GObject *obj)
{
  SalutContact *self = SALUT_CONTACT (obj);

  if (G_OBJECT_CLASS (salut_contact_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (salut_contact_parent_class)->constructed (obj);

  tp_g_signal_connect_object (self->connection,
      "status-changed", G_CALLBACK (connection_status_changed_cb), self, 0);
}

static void
salut_contact_init (SalutContact *obj)
{
  SalutContactPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (obj,
      SALUT_TYPE_CONTACT, SalutContactPrivate);

  obj->priv = priv;

  /* allocate any data required by the object here */
  obj->name = NULL;
  obj->status = SALUT_PRESENCE_AVAILABLE;
  obj->status_message = NULL;
  obj->avatar_token = NULL;
  obj->jid = NULL;
  priv->found = FALSE;
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
  object_class->constructed = salut_contact_constructed;
  object_class->get_property = salut_contact_get_property;
  object_class->set_property = salut_contact_set_property;

  object_class->dispose = salut_contact_dispose;
  object_class->finalize = salut_contact_finalize;

  signals[FOUND] = g_signal_new ("found",
      G_OBJECT_CLASS_TYPE(salut_contact_class),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  signals[CONTACT_CHANGE] = g_signal_new ("contact-change",
      G_OBJECT_CLASS_TYPE(salut_contact_class),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__INT,
      G_TYPE_NONE, 1,
      G_TYPE_INT);

  signals[LOST] = g_signal_new ("lost",
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
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION,
      param_spec);

  param_spec = g_param_spec_string (
      "name",
      "name",
      "The name of this contact",
      NULL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_NAME,
      param_spec);
}

void
salut_contact_dispose (GObject *object)
{
  SalutContact *self = SALUT_CONTACT (object);
  SalutContactPrivate *priv = self->priv;

  DEBUG_CONTACT (self, "Disposing contact");

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  salut_contact_avatar_request_flush (self, NULL, 0);

  /* release any references held by the object here */

  if (self->data_forms != NULL)
    {
      g_ptr_array_unref (self->data_forms);
      self->data_forms = NULL;
    }

  if (G_OBJECT_CLASS (salut_contact_parent_class)->dispose)
    G_OBJECT_CLASS (salut_contact_parent_class)->dispose (object);
}

void
salut_contact_finalize (GObject *object)
{
  SalutContact *self = SALUT_CONTACT (object);
  SalutContactPrivate *priv = self->priv;

  /* free any data held directly by the object here */
  g_free (self->name);
  g_free (self->status_message);
  g_free (priv->alias);
  g_free (self->avatar_token);
  g_free (self->first);
  g_free (self->last);
  g_free (self->full_name);
  g_free (self->email);
  g_free (self->jid);

  G_OBJECT_CLASS (salut_contact_parent_class)->finalize (object);
}

static void
purge_cached_avatar (SalutContact *self,
                    const gchar *token)
{
  SalutContactPrivate *priv = self->priv;

  g_free (self->avatar_token);
  self->avatar_token = g_strdup (token);

  /* the avatar token has changed, restart retrieving the avatar if we were
   * retrieving it */
  if (priv->avatar_requests != NULL)
    SALUT_CONTACT_GET_CLASS (self)->retrieve_avatar (self);
}

GArray *
salut_contact_get_addresses (SalutContact *self)
{
  return SALUT_CONTACT_GET_CLASS (self)->get_addresses (self);
}

gboolean
salut_contact_has_address (SalutContact *self,
                           struct sockaddr *address,
                           guint size)
{
  return SALUT_CONTACT_GET_CLASS (self)->has_address (self, address, size);
}

const gchar *
salut_contact_get_alias (SalutContact *contact)
{
  SalutContactPrivate *priv = contact->priv;
  if (priv->alias == NULL)
    {
      return contact->name;
    }
  return priv->alias;
}

void
salut_contact_avatar_request_flush (SalutContact *contact,
                                    guint8 *data,
                                    gsize size)
{
  SalutContactPrivate *priv = contact->priv;
  GList *list, *liststart;
  AvatarRequest *request;

  liststart = priv->avatar_requests;
  priv->avatar_requests = NULL;

  for (list = liststart; list != NULL; list = g_list_next (list)) {
    request = (AvatarRequest *) list->data;
    request->callback (contact, data, size, request->user_data);
    g_slice_free (AvatarRequest, request);
  }
  g_list_free (liststart);
}

void
salut_contact_get_avatar (SalutContact *contact,
    salut_contact_get_avatar_callback callback, gpointer user_data)
{
  SalutContactPrivate *priv = contact->priv;
  AvatarRequest *request;
  gboolean retrieve;

  g_assert (contact != NULL);

  if (contact->avatar_token == NULL)
    {
      DEBUG ("Avatar requestes for a contact without one (%s)", contact->name);
      callback (contact, NULL, 0, user_data);
      return;
    }

  DEBUG ("Requesting avatar for: %s", contact->name);
  request = g_slice_new0 (AvatarRequest);
  request->callback = callback;
  request->user_data = user_data;
  retrieve = (priv->avatar_requests == NULL);
  priv->avatar_requests = g_list_append (priv->avatar_requests, request);

  if (retrieve)
    SALUT_CONTACT_GET_CLASS (contact)->retrieve_avatar (contact);
}

void
salut_contact_set_capabilities (SalutContact *contact,
    const GabbleCapabilitySet *caps,
    const GPtrArray *data_forms)
{
  guint i;

  gabble_capability_set_free (contact->caps);
  contact->caps = gabble_capability_set_copy (caps);

  g_ptr_array_unref (contact->data_forms);

  contact->data_forms = g_ptr_array_new_with_free_func (g_object_unref);

  for (i = 0; i < data_forms->len; i++)
    {
      GObject *form = g_ptr_array_index (data_forms, i);
      g_ptr_array_add (contact->data_forms, g_object_ref (form));
    }

  g_signal_emit_by_name (contact, "capabilities-changed");
}

static void
salut_contact_change (SalutContact *self, guint changes)
{
  SalutContactPrivate *priv = self->priv;

  priv->pending_changes |= changes;

  if (!priv->frozen && priv->pending_changes != 0)
    {
      g_signal_emit (self, signals[CONTACT_CHANGE], 0,
        priv->pending_changes);
      priv->pending_changes = 0;
      return;
    }
}

void
salut_contact_change_real_name (
    SalutContact *self,
    const gchar *first,
    const gchar *last)
{
  if (tp_str_empty (first))
    first = NULL;

  if (tp_str_empty (last))
    last = NULL;

  if (tp_strdiff (self->first, first) || tp_strdiff (self->last, last))
    {
      g_free (self->first);
      self->first = g_strdup (first);
      g_free (self->last);
      self->last = g_strdup (last);

      g_free (self->full_name);

      if (first != NULL && last != NULL)
        self->full_name = g_strdup_printf ("%s %s", first, last);
      else if (first != NULL)
        self->full_name = g_strdup (first);
      else if (last != NULL)
        self->full_name = g_strdup (last);
      else
        self->full_name = NULL;

      salut_contact_change (self, SALUT_CONTACT_REAL_NAME_CHANGED);
    }
}

void
salut_contact_change_alias (SalutContact *self, const gchar *alias)
{
  SalutContactPrivate *priv = self->priv;

  if (tp_strdiff (priv->alias, alias))
    {
      g_free (priv->alias);
      priv->alias = g_strdup (alias);
      salut_contact_change (self, SALUT_CONTACT_ALIAS_CHANGED);
    }
}

void
salut_contact_change_status (SalutContact *self, SalutPresenceId status)
{
  if (status != self->status && status < SALUT_PRESENCE_NR_PRESENCES)
    {
      self->status = status;
      salut_contact_change (self, SALUT_CONTACT_STATUS_CHANGED);
    }
}

void
salut_contact_change_status_message (SalutContact *self, const gchar *message)
{
  if (tp_strdiff (self->status_message, message))
    {
      g_free (self->status_message);
      self->status_message = g_strdup (message);
      salut_contact_change (self, SALUT_CONTACT_STATUS_CHANGED);
    }
}

void
salut_contact_change_avatar_token (SalutContact *self,
    const gchar *avatar_token)
{
  if (tp_strdiff (self->avatar_token, avatar_token))
    {
      /* Purge the cache */
      purge_cached_avatar (self, avatar_token);
      salut_contact_change (self, SALUT_CONTACT_AVATAR_CHANGED);
    }
}

void
salut_contact_change_email (SalutContact *self, gchar *email)
{
  if (tp_str_empty (email))
    email = NULL;

  if (tp_strdiff (self->email, email))
    {
      g_free (self->email);
      self->email = g_strdup (email);
      salut_contact_change (self, SALUT_CONTACT_EMAIL_CHANGED);
    }
}

void
salut_contact_change_jid (SalutContact *self, gchar *jid)
{
  if (tp_str_empty (jid))
    jid = NULL;

  if (tp_strdiff (self->jid, jid))
    {
      g_free (self->jid);
      self->jid = g_strdup (jid);
      salut_contact_change (self,
          SALUT_CONTACT_JID_CHANGED
          );
    }
}

void salut_contact_change_capabilities (SalutContact *self,
                                        const gchar *hash,
                                        const gchar *node,
                                        const gchar *ver)
{
  if (self->connection == NULL)
    return;

  salut_presence_cache_process_caps (self->connection->presence_cache, self,
      hash, node, ver);
}

void
salut_contact_found (SalutContact *self)
{
  SalutContactPrivate *priv = self->priv;
  if (priv->found)
    return;

  priv->found = TRUE;
  g_signal_emit (self, signals[FOUND], 0);
  /* When found everything changes */
  g_signal_emit (self, signals[CONTACT_CHANGE], 0, 0xff);
  priv->pending_changes = 0;
}

void
salut_contact_lost (SalutContact *self)
{
  SalutContactPrivate *priv = self->priv;

  self->status = SALUT_PRESENCE_OFFLINE;
  g_free (self->status_message);
  self->status_message = NULL;

  if (!priv->found)
    return;

  DEBUG_CONTACT (self, "disappeared from the local link");

  priv->found = FALSE;
  g_signal_emit (self, signals[CONTACT_CHANGE], 0,
      SALUT_CONTACT_STATUS_CHANGED);
  g_signal_emit (self, signals[LOST], 0);
}

void
salut_contact_freeze (SalutContact *self)
{
  SalutContactPrivate *priv = self->priv;

  priv->frozen = TRUE;
}

void
salut_contact_thaw (SalutContact *self)
{
  SalutContactPrivate *priv = self->priv;

  if (!priv->frozen)
    return;

  priv->frozen = FALSE;
  /* Triggers the emission of the changed signal */
  salut_contact_change (self, 0);
}

static const GPtrArray *
salut_contact_get_data_forms (WockyXep0115Capabilities *caps)
{
  SalutContact *self = SALUT_CONTACT (caps);

  return self->data_forms;
}

static gboolean
salut_contact_has_feature (WockyXep0115Capabilities *caps,
    const gchar *feature)
{
  SalutContact *self = SALUT_CONTACT (caps);

  return gabble_capability_set_has (self->caps, feature);
}

static void
xep_0115_capabilities_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  WockyXep0115CapabilitiesInterface *iface = g_iface;

  iface->get_data_forms = salut_contact_get_data_forms;
  iface->has_feature = salut_contact_has_feature;
}
