/*
 * im-manager.c - Source for SalutImManager
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
#include "im-manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <salut/caps-channel-manager.h>

#include "extensions/extensions.h"
#include "im-channel.h"
#include "contact.h"

#include <gibber/gibber-linklocal-transport.h>
#include <wocky/wocky.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#define DEBUG_FLAG DEBUG_IM
#include "debug.h"

static void salut_im_manager_channel_manager_iface_init (gpointer g_iface,
    gpointer iface_data);
static void gabble_caps_channel_manager_iface_init (
    GabbleCapsChannelManagerIface *);

static SalutImChannel *
salut_im_manager_new_channel (SalutImManager *mgr, TpHandle handle,
    TpHandle initiator, gpointer request);

G_DEFINE_TYPE_WITH_CODE (SalutImManager, salut_im_manager, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      salut_im_manager_channel_manager_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER,
      gabble_caps_channel_manager_iface_init))

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_CONTACT_MANAGER,
  LAST_PROPERTY
};

/* private structure */
typedef struct _SalutImManagerPrivate SalutImManagerPrivate;

struct _SalutImManagerPrivate
{
  SalutContactManager *contact_manager;
  SalutConnection *connection;
  GHashTable *channels;
  gulong status_changed_id;
  guint message_handler_id;
  gboolean dispose_has_run;
};

#define SALUT_IM_MANAGER_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_IM_MANAGER, \
                                SalutImManagerPrivate))

static void
salut_im_manager_init (SalutImManager *obj)
{
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (obj);
  /* allocate any data required by the object here */
  priv->channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);
}

static gboolean
message_stanza_callback (WockyPorter *porter,
    WockyStanza *stanza,
    gpointer user_data)
{
  SalutImManager *self = SALUT_IM_MANAGER (user_data);
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (self);
  SalutImChannel *chan;
  TpHandle handle;
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->connection);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (base_conn,
       TP_HANDLE_TYPE_CONTACT);
  SalutContact *contact;

  contact = SALUT_CONTACT (wocky_stanza_get_from_contact (stanza));

  handle = tp_handle_lookup (handle_repo, contact->name, NULL, NULL);
  g_assert (handle != 0);

  if (g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (handle)) != NULL)
    return FALSE; /* we only care about opening new channels */

  chan = salut_im_manager_new_channel (self, handle, handle, NULL);
  salut_im_channel_received_stanza (chan, stanza);

  return TRUE;
}

static void
salut_im_factory_close_all (SalutImManager *self)
{
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (self);

  if (priv->channels != NULL)
    {
      GHashTable *tmp = priv->channels;

      DEBUG ("closing channels");
      priv->channels = NULL;
      g_hash_table_unref (tmp);
    }

  if (priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (priv->connection, priv->status_changed_id);
      priv->status_changed_id = 0;
    }
}

static void
connection_status_changed_cb (SalutConnection *conn,
                              guint status,
                              guint reason,
                              SalutImManager *self)
{
  if (status == TP_CONNECTION_STATUS_DISCONNECTED)
    {
      salut_im_factory_close_all (self);
    }
}


static void salut_im_manager_dispose (GObject *object);
static void salut_im_manager_finalize (GObject *object);

static void
salut_im_manager_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  SalutImManager *self = SALUT_IM_MANAGER (object);
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->connection);
        break;
      case PROP_CONTACT_MANAGER:
        g_value_set_object (value, priv->contact_manager);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_im_manager_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  SalutImManager *self = SALUT_IM_MANAGER (object);
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->connection = g_value_get_object (value);
        break;
      case PROP_CONTACT_MANAGER:
        priv->contact_manager = g_value_dup_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
salut_im_manager_constructor (GType type,
                              guint n_props,
                              GObjectConstructParam *props)
{
  GObject *obj;
  SalutImManager *self;
  SalutImManagerPrivate *priv;

  obj = G_OBJECT_CLASS (salut_im_manager_parent_class)->
           constructor (type, n_props, props);

  self = SALUT_IM_MANAGER (obj);
  priv = SALUT_IM_MANAGER_GET_PRIVATE (self);

  priv->message_handler_id = wocky_porter_register_handler_from_anyone (
      priv->connection->porter, WOCKY_STANZA_TYPE_MESSAGE,
      WOCKY_STANZA_SUB_TYPE_NONE,
      WOCKY_PORTER_HANDLER_PRIORITY_NORMAL,
      message_stanza_callback, self, NULL);

  priv->status_changed_id = g_signal_connect (priv->connection,
      "status-changed", (GCallback) connection_status_changed_cb, self);

  return obj;
}

static void
salut_im_manager_class_init (SalutImManagerClass *salut_im_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_im_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_im_manager_class,
      sizeof (SalutImManagerPrivate));

  object_class->constructor = salut_im_manager_constructor;
  object_class->dispose = salut_im_manager_dispose;
  object_class->finalize = salut_im_manager_finalize;
  object_class->get_property = salut_im_manager_get_property;
  object_class->set_property = salut_im_manager_set_property;

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "Salut connection object that owns this text channel factory object.",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_object (
      "contact-manager",
      "SalutContactManager object",
      "Salut Contact Manager associated with the Salut Connection of this "
      "manager",
      SALUT_TYPE_CONTACT_MANAGER,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONTACT_MANAGER,
      param_spec);
}

void
salut_im_manager_dispose (GObject *object)
{
  SalutImManager *self = SALUT_IM_MANAGER (object);
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->connection->porter != NULL)
    {
      wocky_porter_unregister_handler (priv->connection->porter,
          priv->message_handler_id);
      priv->message_handler_id = 0;
    }

  if (priv->contact_manager)
    {
      g_object_unref (priv->contact_manager);
      priv->contact_manager = NULL;
    }

  salut_im_factory_close_all (self);

  if (G_OBJECT_CLASS (salut_im_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_im_manager_parent_class)->dispose (object);
}

void
salut_im_manager_finalize (GObject *object)
{
  //SalutImManager *self = SALUT_IM_MANAGER (object);
  //SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (salut_im_manager_parent_class)->finalize (object);
}

struct foreach_data
{
  TpExportableChannelFunc func;
  gpointer data;
};

static void
salut_im_manager_iface_foreach_one (gpointer key,
                                    gpointer value,
                                    gpointer data)
{
  TpExportableChannel *chan = TP_EXPORTABLE_CHANNEL (value);
  struct foreach_data *f = (struct foreach_data *) data;

  f->func (chan, f->data);
}

static void
salut_im_manager_foreach_channel (TpChannelManager *iface,
                                  TpExportableChannelFunc func,
                                  gpointer user_data)
{
  SalutImManager *mgr = SALUT_IM_MANAGER (iface);
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (mgr);
  struct foreach_data f;

  f.func = func;
  f.data = user_data;

  g_hash_table_foreach (priv->channels, salut_im_manager_iface_foreach_one,
      &f);
}

static const gchar * const im_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};


static const gchar * const im_channel_allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    NULL
};

static void
salut_im_manager_type_foreach_channel_class (GType type,
    TpChannelManagerTypeChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);
  GValue *value;

  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_TEXT);
  g_hash_table_insert (table, (gchar *) im_channel_fixed_properties[0],
      value);

  value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (table, (gchar *) im_channel_fixed_properties[1],
      value);

  func (type, table, im_channel_allowed_properties, user_data);

  g_hash_table_unref (table);
}


static gboolean
salut_im_manager_requestotron (SalutImManager *self,
                               gpointer request_token,
                               GHashTable *request_properties,
                               gboolean require_new)
{
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *base_conn = (TpBaseConnection *) priv->connection;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      base_conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle handle;
  GError *error = NULL;
  TpExportableChannel *channel;

  if (tp_strdiff (tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL ".ChannelType"), TP_IFACE_CHANNEL_TYPE_TEXT))
    return FALSE;

  if (tp_asv_get_uint32 (request_properties,
        TP_IFACE_CHANNEL ".TargetHandleType", NULL) != TP_HANDLE_TYPE_CONTACT)
    return FALSE;

  handle = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandle", NULL);

  if (!tp_handle_is_valid (contact_repo, handle, &error))
    goto error;

  /* Check if there are any other properties that we don't understand */
  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          im_channel_fixed_properties, im_channel_allowed_properties,
          &error))
    {
      goto error;
    }

  /* Don't support opening a channel to our self handle */
  if (handle == base_conn->self_handle)
    {
      g_set_error (&error, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
          "Can't open a text channel to yourself");
      goto error;
    }

  channel = g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (handle));

  if (channel == NULL)
    {
      salut_im_manager_new_channel (self, handle, base_conn->self_handle,
          request_token);
      return TRUE;
    }

  if (require_new)
    {
      g_set_error (&error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "Already chatting with contact #%u in another channel", handle);
      goto error;
    }

  tp_channel_manager_emit_request_already_satisfied (self, request_token,
      channel);
  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}


static gboolean
salut_im_manager_create_channel (TpChannelManager *manager,
                                 gpointer request_token,
                                 GHashTable *request_properties)
{
  SalutImManager *self = SALUT_IM_MANAGER (manager);

  return salut_im_manager_requestotron (self, request_token,
      request_properties, TRUE);
}


static gboolean
salut_im_manager_request_channel (TpChannelManager *manager,
                                  gpointer request_token,
                                  GHashTable *request_properties)
{
  SalutImManager *self = SALUT_IM_MANAGER (manager);

  return salut_im_manager_requestotron (self, request_token,
      request_properties, FALSE);
}


static gboolean
salut_im_manager_ensure_channel (TpChannelManager *manager,
                                 gpointer request_token,
                                GHashTable *request_properties)
{
  SalutImManager *self = SALUT_IM_MANAGER (manager);

  return salut_im_manager_requestotron (self, request_token,
      request_properties, FALSE);
}


static void
salut_im_manager_channel_manager_iface_init (gpointer g_iface,
                                             gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = salut_im_manager_foreach_channel;
  iface->type_foreach_channel_class =
    salut_im_manager_type_foreach_channel_class;
  iface->create_channel = salut_im_manager_create_channel;
  iface->request_channel = salut_im_manager_request_channel;
  iface->ensure_channel = salut_im_manager_ensure_channel;
}


/* private functions */
static void
im_channel_closed_cb (SalutImChannel *chan,
                      gpointer user_data)
{
  SalutImManager *self = SALUT_IM_MANAGER (user_data);
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (self);
  TpHandle handle;

  tp_channel_manager_emit_channel_closed_for_object (self,
    TP_EXPORTABLE_CHANNEL (chan));

  if (priv->channels)
    {
      g_object_get (chan, "handle", &handle, NULL);
      DEBUG ("Removing channel with handle %u", handle);
      g_hash_table_remove (priv->channels, GUINT_TO_POINTER (handle));
    }
}

static SalutImChannel *
salut_im_manager_new_channel (SalutImManager *mgr,
                              TpHandle handle,
                              TpHandle initiator,
                              gpointer request)
{
  SalutImManagerPrivate *priv = SALUT_IM_MANAGER_GET_PRIVATE (mgr);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION (priv->connection);
  TpHandleRepoIface *handle_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_CONTACT);
  SalutImChannel *chan;
  SalutContact *contact;
  const gchar *name;
  gchar *path = NULL;
  GSList *requests = NULL;

  g_assert (g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (handle))
      == NULL);

  name = tp_handle_inspect (handle_repo, handle);
  DEBUG ("Requested channel for handle: %u (%s)", handle, name);

  contact = salut_contact_manager_get_contact (priv->contact_manager, handle);
  if (contact == NULL)
    {
      gchar *message = g_strdup_printf ("%s is not online", name);
      tp_channel_manager_emit_request_failed (mgr, request, TP_ERROR,
          TP_ERROR_NOT_AVAILABLE, message);
      g_free (message);
      return NULL;
    }

  path = g_strdup_printf ("%s/IMChannel/%u",
      base_connection->object_path, handle);
  chan = g_object_new (SALUT_TYPE_IM_CHANNEL,
      "connection", priv->connection,
      "contact", contact,
      "handle", handle,
      "initiator-handle", initiator,
      "requested", (handle != initiator),
      NULL);
  tp_base_channel_register ((TpBaseChannel *) chan);
  g_object_unref (contact);
  g_free (path);
  g_hash_table_insert (priv->channels, GUINT_TO_POINTER (handle), chan);

  if (request != NULL)
    requests = g_slist_prepend (requests, request);

  tp_channel_manager_emit_new_channel (mgr, TP_EXPORTABLE_CHANNEL (chan),
    requests);

  g_slist_free (requests);

  g_signal_connect (chan, "closed", G_CALLBACK (im_channel_closed_cb), mgr);

  return chan;
}

/* public functions */
SalutImManager *
salut_im_manager_new (SalutConnection *connection,
                      SalutContactManager *contact_manager)
{
  return g_object_new (SALUT_TYPE_IM_MANAGER,
      "connection", connection,
      "contact-manager", contact_manager,
      NULL);
}

static void
salut_im_manager_add_contact_caps (GPtrArray *arr)
{
  GValue monster = {0, };
  GHashTable *fixed_properties;
  GValue *channel_type_value;
  GValue *target_handle_type_value;
  gchar *text_allowed_properties[] =
      {
        TP_IFACE_CHANNEL ".TargetHandle",
        NULL
      };

  g_value_init (&monster, TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS);
  g_value_take_boxed (&monster,
      dbus_g_type_specialized_construct (
        TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS));

  fixed_properties = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  channel_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (channel_type_value, TP_IFACE_CHANNEL_TYPE_TEXT);
  g_hash_table_insert (fixed_properties, TP_IFACE_CHANNEL ".ChannelType",
      channel_type_value);

  target_handle_type_value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (target_handle_type_value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (fixed_properties, TP_IFACE_CHANNEL ".TargetHandleType",
      target_handle_type_value);

  dbus_g_type_struct_set (&monster,
      0, fixed_properties,
      1, text_allowed_properties,
      G_MAXUINT);

  g_hash_table_unref (fixed_properties);

  g_ptr_array_add (arr, g_value_get_boxed (&monster));
}

static void
salut_im_manager_get_contact_caps_from_set (
    GabbleCapsChannelManager *iface G_GNUC_UNUSED,
    TpHandle handle G_GNUC_UNUSED,
    const GabbleCapabilitySet *set G_GNUC_UNUSED,
    GPtrArray *arr)
{
  /* We don't need to check this contact's capabilities, we assume every
   * contact support text channels. */
  salut_im_manager_add_contact_caps (arr);
}

static void
gabble_caps_channel_manager_iface_init (GabbleCapsChannelManagerIface *iface)
{
  iface->get_contact_caps = salut_im_manager_get_contact_caps_from_set;
}
