/*
 * contact-manager.c - Source for SalutContactManager
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

#include "config.h"
#include "contact-manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <salut/caps-channel-manager.h>

#include "connection.h"
#include "contact.h"
#include "enumtypes.h"

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG DEBUG_CONTACTS
#include "debug.h"

static void salut_contact_manager_close_all (SalutContactManager *mgr);

static void
_contact_finalized_cb (gpointer data, GObject *old_object);

G_DEFINE_TYPE_WITH_CODE(SalutContactManager, salut_contact_manager,
    TP_TYPE_BASE_CONTACT_LIST,
     G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER, NULL))

/* signal enum */
enum
{
  CONTACT_CHANGE,
  ALL_FOR_NOW,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _SalutContactManagerPrivate SalutContactManagerPrivate;

struct _SalutContactManagerPrivate
{
  TpHandleSet *handles;
  gulong status_changed_id;
  gboolean dispose_has_run;
};

#define SALUT_CONTACT_MANAGER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_CONTACT_MANAGER, SalutContactManagerPrivate))

static TpHandleSet *
contact_manager_dup_contacts (TpBaseContactList *base)
{
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (base);

  return tp_handle_set_copy (priv->handles);
}

static void
contact_manager_dup_states (TpBaseContactList *base,
    TpHandle contact,
    TpSubscriptionState *subscribe,
    TpSubscriptionState *publish,
    gchar **publish_request)
{
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (base);

  if (tp_handle_set_is_member (priv->handles, contact))
    {
      if (subscribe != NULL)
        *subscribe = TP_SUBSCRIPTION_STATE_YES;

      if (publish != NULL)
        *publish = TP_SUBSCRIPTION_STATE_YES;

      if (publish_request != NULL)
        *publish_request = NULL;
    }
  else
    {
      if (subscribe != NULL)
        *subscribe = TP_SUBSCRIPTION_STATE_NO;

      if (publish != NULL)
        *publish = TP_SUBSCRIPTION_STATE_NO;

      if (publish_request != NULL)
        *publish_request = NULL;
    }
}

static void
salut_contact_manager_init (SalutContactManager *obj)
{
  /* allocate any data required by the object here */
  obj->contacts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void salut_contact_manager_constructed (GObject *obj);
static void salut_contact_manager_dispose (GObject *object);
static void salut_contact_manager_finalize (GObject *object);

static void
salut_contact_manager_class_init (SalutContactManagerClass *salut_contact_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_contact_manager_class);
  TpBaseContactListClass *base_class = (TpBaseContactListClass *) object_class;

  g_type_class_add_private (salut_contact_manager_class, sizeof (SalutContactManagerPrivate));

  object_class->constructed = salut_contact_manager_constructed;
  object_class->dispose = salut_contact_manager_dispose;
  object_class->finalize = salut_contact_manager_finalize;

  base_class->dup_states = contact_manager_dup_states;
  base_class->dup_contacts = contact_manager_dup_contacts;
  base_class->get_contact_list_persists = tp_base_contact_list_false_func;

  signals[CONTACT_CHANGE] = g_signal_new ("contact-change",
      G_OBJECT_CLASS_TYPE(salut_contact_manager_class),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL, NULL,
      G_TYPE_NONE, 2,
      SALUT_TYPE_CONTACT,
      G_TYPE_INT);

  signals[ALL_FOR_NOW] = g_signal_new ("all-for-now",
      G_OBJECT_CLASS_TYPE(salut_contact_manager_class),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);
}

static void
connection_status_changed_cb (SalutConnection *conn,
                              guint status,
                              guint reason,
                              SalutContactManager *self)
{
  if (status == TP_CONNECTION_STATUS_DISCONNECTED)
    {
      salut_contact_manager_close_all (self);
    }
}

static void
salut_contact_manager_constructed (GObject *obj)
{
  SalutContactManager *self = (SalutContactManager *) obj;
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo;
  TpBaseConnection *base_connection;

  base_connection = tp_base_contact_list_get_connection (
      (TpBaseContactList *) self, NULL);
  self->connection = g_object_ref (base_connection);

  contact_repo = tp_base_connection_get_handles (base_connection,
      TP_HANDLE_TYPE_CONTACT);
  priv->handles = tp_handle_set_new (contact_repo);

  priv->status_changed_id = g_signal_connect (self->connection,
      "status-changed", (GCallback) connection_status_changed_cb, self);

  if (G_OBJECT_CLASS (salut_contact_manager_parent_class)->constructed)
    G_OBJECT_CLASS (salut_contact_manager_parent_class)->constructed (obj);
}

static gboolean
dispose_contact (gpointer key, gpointer value, gpointer object)
{
  SalutContact *contact = SALUT_CONTACT(value);
  SalutContactManager *self = SALUT_CONTACT_MANAGER (object);

  g_object_weak_unref (G_OBJECT(contact), _contact_finalized_cb, object);
  g_signal_handlers_disconnect_matched (contact, G_SIGNAL_MATCH_DATA,
      0, 0, NULL, NULL, object);

  SALUT_CONTACT_MANAGER_GET_CLASS (self)->dispose_contact (self, contact);
  return TRUE;
}

static void
salut_contact_manager_dispose (GObject *object)
{
  SalutContactManager *self = SALUT_CONTACT_MANAGER (object);
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  DEBUG("Disposing contact manager");

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */
  salut_contact_manager_close_all (self);

  if (G_OBJECT_CLASS (salut_contact_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_contact_manager_parent_class)->dispose (object);
}

void
salut_contact_manager_finalize (GObject *object)
{
  //SalutContactManager *self = SALUT_CONTACT_MANAGER (object);
  //SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (salut_contact_manager_parent_class)->finalize (object);
}

static void
contact_found_cb (SalutContact *contact, gpointer userdata)
{
  SalutContactManager *self = userdata;
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (self);

  tp_handle_set_add (priv->handles, contact->handle);
  tp_base_contact_list_one_contact_changed ((TpBaseContactList *) self,
      contact->handle);
}

static void
contact_change_cb (SalutContact *contact, gint changes, gpointer userdata)
{
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER (userdata);

  DEBUG("Emitting contact changes for %s: %d", contact->name, changes);

  g_signal_emit (mgr, signals[CONTACT_CHANGE], 0, contact, changes);
}

static void
contact_lost_cb (SalutContact *contact, gpointer userdata)
{
  SalutContactManager *self = userdata;
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (self);

  tp_handle_set_remove (priv->handles, contact->handle);
  tp_base_contact_list_one_contact_removed ((TpBaseContactList *) self,
      contact->handle);
}

static gboolean
_contact_remove_finalized (gpointer key, gpointer value, gpointer data)
{
  return data == value;
}

static void
_contact_finalized_cb (gpointer data, GObject *old_object)
{
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER(data);

  g_hash_table_foreach_remove (mgr->contacts, _contact_remove_finalized,
      old_object);
}

void
salut_contact_manager_contact_created (SalutContactManager *self,
                                       SalutContact *contact)
{
  g_assert (g_hash_table_lookup (self->contacts, contact->name) == NULL);

  g_hash_table_insert (self->contacts, g_strdup (contact->name), contact);
  DEBUG("Adding %s to contacts", contact->name);

  g_signal_connect (contact, "found",
      G_CALLBACK(contact_found_cb), self);
  g_signal_connect (contact, "contact-change",
      G_CALLBACK(contact_change_cb), self);
  g_signal_connect (contact, "lost",
      G_CALLBACK(contact_lost_cb), self);

  g_object_weak_ref (G_OBJECT (contact), _contact_finalized_cb , self);
}

SalutContact *
salut_contact_manager_ensure_contact (SalutContactManager *self,
                                      const gchar *name)
{
  SalutContact *contact;

  contact = g_hash_table_lookup (self->contacts, name);
  if (contact == NULL)
    {
      DEBUG ("contact %s doesn't exist yet. Creating it", name);
      contact = SALUT_CONTACT_MANAGER_GET_CLASS (self)->create_contact (self,
          name);
      salut_contact_manager_contact_created (self, contact);
    }
  else
    {
      g_object_ref (contact);
    }

  return contact;
}

static void
salut_contact_manager_close_all (SalutContactManager *mgr)
{
  SalutContactManagerPrivate *priv =
    SALUT_CONTACT_MANAGER_GET_PRIVATE (mgr);

  SALUT_CONTACT_MANAGER_GET_CLASS (mgr)->close_all (mgr);

  tp_clear_pointer (&priv->handles, tp_handle_set_destroy);

  if (mgr->contacts)
    {
      g_hash_table_foreach_remove (mgr->contacts, dispose_contact, mgr);
      g_hash_table_unref (mgr->contacts);
      mgr->contacts = NULL;
    }

  if (priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (mgr->connection, priv->status_changed_id);
      priv->status_changed_id = 0;
    }
  tp_clear_object (&mgr->connection);
}

static void
salut_contact_manager_all_for_now_cb (SalutContactManager *self)
{
  TpBaseContactList *base = (TpBaseContactList *) self;

  DEBUG ("Contact list received");

  g_assert (tp_base_contact_list_get_state (base, NULL) ==
      TP_CONTACT_LIST_STATE_WAITING);

  tp_base_contact_list_set_list_received (base);
}

/* public functions */
gboolean
salut_contact_manager_start (SalutContactManager *self,
    GError **error)
{
  TpBaseContactList *base = (TpBaseContactList *) self;
  gboolean success;
  GError *err = NULL;

  g_assert (tp_base_contact_list_get_state (base, NULL) ==
      TP_CONTACT_LIST_STATE_NONE);

  g_signal_connect (self, "all-for-now",
      G_CALLBACK (salut_contact_manager_all_for_now_cb), NULL);

  success = SALUT_CONTACT_MANAGER_GET_CLASS (self)->start (self, &err);
  if (success)
    {
      /* Wait for all-for-now */
      tp_base_contact_list_set_list_pending (base);
    }
  else
    {
      tp_base_contact_list_set_list_failed (base, err->domain, err->code,
          err->message);
    }

  if (err != NULL)
    g_propagate_error (error, err);

  return success;
}

SalutContact *
salut_contact_manager_get_contact (SalutContactManager *mgr, TpHandle handle)
{
  TpHandleRepoIface *handle_repo;
  const char *name;
  SalutContact *ret;

  /* have we already closed everything? */
  if (mgr->connection == NULL || mgr->contacts == NULL)
    return NULL;

  handle_repo = tp_base_connection_get_handles (
      TP_BASE_CONNECTION (mgr->connection), TP_HANDLE_TYPE_CONTACT);
  name = tp_handle_inspect (handle_repo, handle);

  g_return_val_if_fail (name, NULL);

  DEBUG ("Getting contact for: %s", name);
  ret = g_hash_table_lookup (mgr->contacts, name);

  if (ret != NULL)
    g_object_ref (ret);
  else
    DEBUG ("Failed to get contact for %s", name);

  return ret;
}

static void
_find_by_address (gpointer key, gpointer value, gpointer user_data)
{
  struct sockaddr *address =
    (struct sockaddr *)((gpointer *) user_data)[0];
  GList **list = (GList **)((gpointer *) user_data)[1];
  guint size = GPOINTER_TO_UINT (((gpointer *) user_data)[2]);
  SalutContact *contact = SALUT_CONTACT (value);

  if (salut_contact_has_address (contact, address, size)) {
    g_object_ref (contact);
    *list = g_list_append (*list, contact);
  }
}

/* FIXME function name is just too long */
GList *
salut_contact_manager_find_contacts_by_address (SalutContactManager *mgr,
    struct sockaddr *address, guint size)
{
  GList *list = NULL;
  gpointer data[3];

  data[0] = address;
  data[1] = &list;
  data[2] = GUINT_TO_POINTER (size);
  g_hash_table_foreach (mgr->contacts, _find_by_address, data);
  return list;
}
