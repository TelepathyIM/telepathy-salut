/*
 * salut-contact-manager.c - Source for SalutContactManager
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <salut/caps-channel-manager.h>

#include "salut-connection.h"
#include "salut-contact-channel.h"
#include "salut-contact-manager.h"
#include "salut-signals-marshal.h"
#include "salut-contact.h"
#include "salut-presence-enumtypes.h"

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG DEBUG_CONTACTS
#include "debug.h"

static void salut_contact_manager_manager_iface_init (gpointer g_iface,
    gpointer iface_data);

static SalutContactChannel *salut_contact_manager_get_channel
    (SalutContactManager *mgr, TpHandle handle, gpointer request_token,
    gboolean *created);

static void salut_contact_manager_close_all (SalutContactManager *mgr);

static void
_contact_finalized_cb (gpointer data, GObject *old_object);

G_DEFINE_TYPE_WITH_CODE(SalutContactManager, salut_contact_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      salut_contact_manager_manager_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER, NULL))

enum
{
  PROP_CONNECTION = 1,
  LAST_PROP
};

/* signal enum */
enum
{
  CONTACT_CHANGE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _SalutContactManagerPrivate SalutContactManagerPrivate;

struct _SalutContactManagerPrivate
{
  GHashTable *channels;
  gulong status_changed_id;
  gboolean dispose_has_run;
};

#define SALUT_CONTACT_MANAGER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_CONTACT_MANAGER, SalutContactManagerPrivate))

static void
salut_contact_manager_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
  SalutContactManager *self = SALUT_CONTACT_MANAGER (object);

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
salut_contact_manager_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  SalutContactManager *self = SALUT_CONTACT_MANAGER (object);

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

static void
salut_contact_manager_init (SalutContactManager *obj)
{
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  priv->channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);
  obj->contacts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static GObject *salut_contact_manager_constructor (GType type,
    guint n_props, GObjectConstructParam *props);
static void salut_contact_manager_dispose (GObject *object);
static void salut_contact_manager_finalize (GObject *object);

static void
salut_contact_manager_class_init (SalutContactManagerClass *salut_contact_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_contact_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_contact_manager_class, sizeof (SalutContactManagerPrivate));

  object_class->get_property = salut_contact_manager_get_property;
  object_class->set_property = salut_contact_manager_set_property;

  object_class->constructor = salut_contact_manager_constructor;
  object_class->dispose = salut_contact_manager_dispose;
  object_class->finalize = salut_contact_manager_finalize;

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "The Salut Connection associated with this contact manager",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION,
      param_spec);

  signals[CONTACT_CHANGE] = g_signal_new ("contact-change",
      G_OBJECT_CLASS_TYPE(salut_contact_manager_class),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      salut_signals_marshal_VOID__OBJECT_INT,
      G_TYPE_NONE, 2,
      SALUT_TYPE_CONTACT,
      G_TYPE_INT);
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

static GObject *
salut_contact_manager_constructor (GType type,
    guint n_props,
    GObjectConstructParam *props)
{
  GObject *obj;
  SalutContactManager *self;
  SalutContactManagerPrivate *priv;

  obj = G_OBJECT_CLASS (salut_contact_manager_parent_class)->
           constructor (type, n_props, props);

  self = SALUT_CONTACT_MANAGER (obj);
  priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (self);

  priv->status_changed_id = g_signal_connect (self->connection,
      "status-changed", (GCallback) connection_status_changed_cb, self);

  return obj;
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
change_all_groups (SalutContactManager *mgr, TpIntSet *add, TpIntSet *rem)
{
  TpHandle i;
  SalutContactChannel *c;
  TpIntSet *empty = tp_intset_new ();

  for (i = LIST_HANDLE_FIRST; i <= LIST_HANDLE_LAST; i++)
    {
      c = salut_contact_manager_get_channel (mgr, i, NULL, NULL);
      tp_group_mixin_change_members (G_OBJECT(c),
                                     "", add, rem,
                                     empty, empty, 0, 0);
    }
  tp_intset_destroy (empty);
}

static void
contact_found_cb (SalutContact *contact, gpointer userdata)
{
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER (userdata);
  TpIntSet *to_add = tp_intset_new ();
  TpIntSet *to_rem = tp_intset_new ();

  tp_intset_add (to_add, contact->handle);
  change_all_groups (mgr, to_add, to_rem);
  tp_intset_destroy (to_add);
  tp_intset_destroy (to_rem);
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
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER (userdata);

  TpIntSet *to_add = tp_intset_new ();
  TpIntSet *to_rem = tp_intset_new ();

  DEBUG("Removing %s from contacts", contact->name);

  tp_intset_add (to_rem, contact->handle);
  change_all_groups (mgr, to_add, to_rem);

  tp_intset_destroy (to_add);
  tp_intset_destroy (to_rem);
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

  if (priv->channels)
    {
      g_hash_table_destroy (priv->channels);
      priv->channels = NULL;
    }

  if (mgr->contacts)
    {
      g_hash_table_foreach_remove (mgr->contacts, dispose_contact, mgr);
      g_hash_table_destroy (mgr->contacts);
      mgr->contacts = NULL;
    }

  if (priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (mgr->connection, priv->status_changed_id);
      priv->status_changed_id = 0;
    }
}

/* TpChannelManager implementation */
struct foreach_channel_data {
  TpExportableChannelFunc func;
  gpointer data;
};

static void
salut_contact_manager_foreach_one (gpointer key,
    gpointer value,
    gpointer data)
{
  TpExportableChannel *chan = TP_EXPORTABLE_CHANNEL (value);
  struct foreach_channel_data *f = data;

  f->func (chan, f->data);
}

static void
salut_contact_manager_foreach_channel (TpChannelManager *iface,
    TpExportableChannelFunc func,
    gpointer data)
{
  SalutContactManager *mgr = SALUT_CONTACT_MANAGER (iface);
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (mgr);
  struct foreach_channel_data f;
  f.func = func;
  f.data = data;

  g_hash_table_foreach (priv->channels,
                        salut_contact_manager_foreach_one, &f);
}

static const gchar * const list_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};

static const gchar * const list_channel_allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    NULL
};

static void
salut_contact_manager_type_foreach_channel_class (GType type,
    TpChannelManagerTypeChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);
  GValue *value, *handle_type_value;

  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType", value);

  handle_type_value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (handle_type_value, TP_HANDLE_TYPE_LIST);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetHandleType",
      handle_type_value);

  func (type, table, list_channel_allowed_properties, user_data);

  g_hash_table_destroy (table);
}

static gboolean
salut_contact_manager_request (SalutContactManager *self,
    gpointer request_token,
    GHashTable *request_properties,
    gboolean require_new)
{
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (
      TP_BASE_CONNECTION (self->connection), TP_HANDLE_TYPE_LIST);
  TpHandleType handle_type;
  TpHandle handle;
  SalutContactChannel *channel;
  gboolean created;
  GError *error = NULL;

  if (tp_strdiff (tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL ".ChannelType"),
        TP_IFACE_CHANNEL_TYPE_CONTACT_LIST))
    return FALSE;

  handle_type = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandleType", NULL);

  if (handle_type != TP_HANDLE_TYPE_LIST)
    return FALSE;

  handle = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandle", NULL);
  g_assert (tp_handle_is_valid (handle_repo, handle, NULL));

  /* Check if there are any other properties that we don't understand */
  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          list_channel_fixed_properties, list_channel_allowed_properties,
          &error))
    {
      goto error;
    }

  channel = salut_contact_manager_get_channel (self, handle, request_token,
      &created);

  if (created)
    {
      /* Do nothing; salut_contact_manager_new_channel emits the new-channel
       * signal
       */
    }
  else
    {
      if (require_new)
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
            "Contact list channel #%u already exists", handle);
          goto error;
        }
      else
        {
          tp_channel_manager_emit_request_already_satisfied (self,
              request_token, TP_EXPORTABLE_CHANNEL (channel));
        }
    }
  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}

static gboolean
salut_contact_manager_request_channel (TpChannelManager *manager,
    gpointer request_token,
    GHashTable *request_properties)
{
  SalutContactManager *self = SALUT_CONTACT_MANAGER (manager);

  return salut_contact_manager_request (self, request_token,
      request_properties, FALSE);
}

static gboolean
salut_contact_manager_create_channel (TpChannelManager *manager,
    gpointer request_token,
    GHashTable *request_properties)
{
  SalutContactManager *self = SALUT_CONTACT_MANAGER (manager);

  return salut_contact_manager_request (self, request_token,
      request_properties, TRUE);
}

static gboolean
salut_contact_manager_ensure_channel (TpChannelManager *manager,
    gpointer request_token,
    GHashTable *request_properties)
{
  SalutContactManager *self = SALUT_CONTACT_MANAGER (manager);

  return salut_contact_manager_request (self, request_token,
      request_properties, FALSE);
}

static void
salut_contact_manager_manager_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = salut_contact_manager_foreach_channel;
  iface->type_foreach_channel_class =
    salut_contact_manager_type_foreach_channel_class;
  iface->request_channel = salut_contact_manager_request_channel;
  iface->create_channel = salut_contact_manager_create_channel;
  iface->ensure_channel = salut_contact_manager_ensure_channel;
}

/* private functions */
static SalutContactChannel *
salut_contact_manager_new_channel (SalutContactManager *mgr,
    gpointer request_token,
    TpHandle handle)
{
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (mgr);
  TpBaseConnection *base_conn = (TpBaseConnection *) (mgr->connection);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (base_conn,
       TP_HANDLE_TYPE_LIST);
  SalutContactChannel *chan;
  const gchar *name;
  gchar *path;
  GSList *requests = NULL;

  g_assert (g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (handle))
             == NULL);

  name = tp_handle_inspect (handle_repo, handle);
  path = g_strdup_printf ("%s/ContactChannel/%s", base_conn->object_path,
      name);

  chan = g_object_new (SALUT_TYPE_CONTACT_CHANNEL,
      "connection", mgr->connection,
      "object-path", path,
      "handle", handle,
      "requested", (request_token != NULL),
      NULL);
  g_free (path);
  g_hash_table_insert (priv->channels, GUINT_TO_POINTER (handle), chan);

  if (request_token != NULL)
    requests = g_slist_prepend (requests, request_token);

  tp_channel_manager_emit_new_channel (mgr, TP_EXPORTABLE_CHANNEL (chan),
      requests);

  g_slist_free (requests);

  return chan;
}

static SalutContactChannel *
salut_contact_manager_get_channel (SalutContactManager *mgr,
    TpHandle handle, gpointer request_token, gboolean *created)
{
  SalutContactManagerPrivate *priv = SALUT_CONTACT_MANAGER_GET_PRIVATE (mgr);
  SalutContactChannel *chan;

  chan = g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (handle));
  if (created != NULL)
    {
      *created = (chan == NULL);
    }
  if (chan == NULL)
    {
      chan = salut_contact_manager_new_channel (mgr, request_token, handle);
    }

  return chan;
}

/* public functions */
gboolean
salut_contact_manager_start (SalutContactManager *self,
    GError **error)
{
  return SALUT_CONTACT_MANAGER_GET_CLASS (self)->start (self, error);
}

SalutContact *
salut_contact_manager_get_contact (SalutContactManager *mgr, TpHandle handle)
{
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (
      TP_BASE_CONNECTION (mgr->connection), TP_HANDLE_TYPE_CONTACT);
  const char *name = tp_handle_inspect (handle_repo, handle);
  SalutContact *ret;

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
