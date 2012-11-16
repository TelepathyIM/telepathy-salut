/*
 * ft-manager.c - Source for SalutFtManager
 * Copyright (C) 2007 Marco Barisione <marco@barisione.org>
 * Copyright (C) 2006, 2008 Collabora Ltd.
 *   @author: Jonny Lamb <jonny.lamb@collabora.co.uk>
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

#include <gibber/gibber-file-transfer.h>

#include "ft-manager.h"
#include "signals-marshal.h"

#include <salut/caps-channel-manager.h>

#include "file-transfer-channel.h"
#include "contact-manager.h"
#include "presence-cache.h"
#include "namespaces.h"

#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/gtypes.h>

#define DEBUG_FLAG DEBUG_FT
#include "debug.h"

static void
channel_manager_iface_init (gpointer, gpointer);
static void gabble_caps_channel_manager_iface_init (
    GabbleCapsChannelManagerIface *);

static void salut_ft_manager_channel_created (SalutFtManager *mgr,
    SalutFileTransferChannel *chan, gpointer request_token);

typedef enum
{
  FT_CAPA_SUPPORTED = 1,
  FT_CAPA_UNSUPPORTED,
} FtCapaStatus;

G_DEFINE_TYPE_WITH_CODE (SalutFtManager, salut_ft_manager, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER,
      gabble_caps_channel_manager_iface_init))

/* private structure */
typedef struct _SalutFtManagerPrivate SalutFtManagerPrivate;

struct _SalutFtManagerPrivate
{
  gboolean dispose_has_run;
  SalutConnection *connection;
  SalutContactManager *contact_manager;
  GList *channels;
  guint message_handler_id;
};

#define SALUT_FT_MANAGER_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_FT_MANAGER, \
                                SalutFtManagerPrivate))

static void
salut_ft_manager_init (SalutFtManager *obj)
{
  SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (obj);
  priv->contact_manager = NULL;
  priv->connection = NULL;

  /* allocate any data required by the object here */
  priv->channels = NULL;
}

static gboolean
message_stanza_callback (WockyPorter *porter,
    WockyStanza *stanza,
    gpointer user_data)
{
  SalutFtManager *self = SALUT_FT_MANAGER (user_data);
  SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (self);
  SalutFileTransferChannel *chan;
  TpHandle handle;
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->connection);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (base_conn,
       TP_HANDLE_TYPE_CONTACT);
  SalutContact *contact;

  /* make sure we can support this kind of ft */
  if (!gibber_file_transfer_is_file_offer (stanza))
    return FALSE;

  contact = SALUT_CONTACT (wocky_stanza_get_from_contact (stanza));

  handle = tp_handle_lookup (handle_repo, contact->name, NULL, NULL);
  g_assert (handle != 0);

  DEBUG ("new incoming channel");

  /* this can fail if the stanza isn't valid */
  chan = salut_file_transfer_channel_new_from_stanza (priv->connection,
      contact, handle,
      TP_FILE_TRANSFER_STATE_PENDING, stanza);

  if (chan != NULL)
    salut_ft_manager_channel_created (self, chan, NULL);

  return TRUE;
}

static void salut_ft_manager_dispose (GObject *object);
static void salut_ft_manager_finalize (GObject *object);

static void
salut_ft_manager_class_init (SalutFtManagerClass *salut_ft_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_ft_manager_class);

  g_type_class_add_private (salut_ft_manager_class,
                            sizeof (SalutFtManagerPrivate));

  object_class->dispose = salut_ft_manager_dispose;
  object_class->finalize = salut_ft_manager_finalize;
}

void
salut_ft_manager_dispose (GObject *object)
{
  SalutFtManager *self = SALUT_FT_MANAGER (object);
  SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (self);
  GList *l;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->connection->porter != NULL)
    {
      wocky_porter_unregister_handler (priv->connection->porter,
          priv->message_handler_id);
      priv->message_handler_id = 0;
    }

  if (priv->contact_manager != NULL)
    {
      g_object_unref (priv->contact_manager);
      priv->contact_manager = NULL;
    }

  for (l = priv->channels; l != NULL; l = g_list_next (l))
    {
      g_signal_handlers_disconnect_matched (l->data,
          G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
      g_object_unref (l->data);
    }

  if (priv->channels)
    g_list_free (priv->channels);

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_ft_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_ft_manager_parent_class)->dispose (object);
}

void
salut_ft_manager_finalize (GObject *object)
{
  /*SalutFtManager *self = SALUT_FT_MANAGER (object);*/
  /*SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (self);*/

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (salut_ft_manager_parent_class)->finalize (object);
}

/* Channel Manager interface */

struct foreach_data {
  TpExportableChannelFunc func;
  gpointer data;
};

static void
salut_ft_manager_iface_foreach_one (gpointer value,
                                    gpointer data)
{
  TpExportableChannel *chan;
  struct foreach_data *f = (struct foreach_data *) data;

  if (!value)
    return;

  chan = TP_EXPORTABLE_CHANNEL (value);

  f->func (chan, f->data);
}

static void
salut_ft_manager_foreach_channel (TpChannelManager *iface,
                                  TpExportableChannelFunc func,
                                  gpointer data)
{
  SalutFtManager *mgr = SALUT_FT_MANAGER (iface);
  SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (mgr);
  struct foreach_data f;
  f.func = func;
  f.data = data;

  g_list_foreach (priv->channels, (GFunc) salut_ft_manager_iface_foreach_one,
      &f);
}

static void
file_channel_closed (SalutFtManager *self,
                     SalutFileTransferChannel *chan)
{
  SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (self);
  TpHandle handle;

  if (priv->channels)
    {
      g_object_get (chan, "handle", &handle, NULL);
      DEBUG ("Removing channel with handle %d", handle);
      priv->channels = g_list_remove (priv->channels, chan);
      g_object_unref (chan);
    }
}

static void
file_channel_closed_cb (SalutFileTransferChannel *chan, gpointer user_data)
{
  SalutFtManager *self = SALUT_FT_MANAGER (user_data);

  file_channel_closed (self, chan);
}

static void
salut_ft_manager_channel_created (SalutFtManager *self,
                                  SalutFileTransferChannel *chan,
                                  gpointer request_token)
{
  SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (self);
  GSList *requests = NULL;

  tp_base_channel_register (TP_BASE_CHANNEL (chan));

  g_signal_connect (chan, "closed", G_CALLBACK (file_channel_closed_cb), self);

  priv->channels = g_list_append (priv->channels, chan);

  if (request_token != NULL)
    requests = g_slist_prepend (requests, request_token);

  tp_channel_manager_emit_new_channel (self, TP_EXPORTABLE_CHANNEL (chan),
      requests);

  g_slist_free (requests);
}

static gboolean
salut_ft_manager_handle_request (TpChannelManager *manager,
                                 gpointer request_token,
                                 GHashTable *request_properties)
{
  SalutFtManager *self = SALUT_FT_MANAGER (manager);
  SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (self);
  SalutFileTransferChannel *chan;
  TpBaseConnection *base_connection = TP_BASE_CONNECTION (priv->connection);
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_CONTACT);
  TpHandle handle;
  const gchar *content_type, *filename, *content_hash, *description;
  guint64 size, date, initial_offset;
  const gchar *file_uri, *service_name;
  const GHashTable *metadata;
  TpFileHashType content_hash_type;
  GError *error = NULL;
  gboolean valid;
  SalutContact *contact;

  DEBUG ("File transfer request");

  /* We only support file transfer channels */
  if (tp_strdiff (tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL ".ChannelType"),
        TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER))
    return FALSE;

  /* And only contact handles */
  if (tp_asv_get_uint32 (request_properties,
        TP_IFACE_CHANNEL ".TargetHandleType", NULL) != TP_HANDLE_TYPE_CONTACT)
    return FALSE;

  handle = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandle", NULL);

  /* Must be a valid contact handle */
  if (!tp_handle_is_valid (contact_repo, handle, &error))
    goto error;

  /* Don't support opening a channel to our self handle */
  if (handle == base_connection->self_handle)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "Can't open a file transfer channel to yourself");
      goto error;
    }

  content_type = tp_asv_get_string (request_properties,
      TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".ContentType");
  if (content_type == NULL)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "ContentType property is mandatory");
      goto error;
    }

  filename = tp_asv_get_string (request_properties,
      TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Filename");
  if (filename == NULL)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Filename property is mandatory");
      goto error;
    }

  size = tp_asv_get_uint64 (request_properties,
      TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Size", NULL);
  if (size == 0)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Size property is mandatory");
      goto error;
    }

  content_hash_type = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".ContentHashType", &valid);
  if (!valid)
    {
      /* Assume File_Hash_Type_None */
      content_hash_type = TP_FILE_HASH_TYPE_NONE;
    }
  else
    {
      if (content_hash_type >= NUM_TP_FILE_HASH_TYPES)
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "%u is not a valid ContentHashType", content_hash_type);
          goto error;
        }
    }

  if (content_hash_type != TP_FILE_HASH_TYPE_NONE)
    {
      content_hash = tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".ContentHash");
      if (content_hash == NULL)
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "ContentHash property is mandatory if ContentHashType is "
              "not None");
          goto error;
        }
    }
  else
    {
      content_hash = NULL;
    }

  description = tp_asv_get_string (request_properties,
      TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Description");

  date = tp_asv_get_uint64 (request_properties,
      TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Date", NULL);

  initial_offset = tp_asv_get_uint64 (request_properties,
      TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".InitialOffset", NULL);

  file_uri = tp_asv_get_string (request_properties,
      TP_PROP_CHANNEL_TYPE_FILE_TRANSFER_URI);

  service_name = tp_asv_get_string (request_properties,
      TP_PROP_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA_SERVICE_NAME);

  metadata = tp_asv_get_boxed (request_properties,
      TP_PROP_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA_METADATA,
      TP_HASH_TYPE_METADATA);

  if (metadata != NULL && g_hash_table_lookup ((GHashTable *) metadata, "FORM_TYPE"))
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Metadata cannot contain an item with key 'FORM_TYPE'");
      goto error;
    }

  contact = salut_contact_manager_get_contact (priv->contact_manager, handle);
  if (contact == NULL)
    {
      const gchar *name = tp_handle_inspect (contact_repo, handle);

      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "%s is not online", name);

      goto error;
    }

  if (service_name != NULL || metadata != NULL)
    {
      if (!gabble_capability_set_has (contact->caps, NS_TP_FT_METADATA))
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_CAPABLE,
              "The specified contact does not support the "
              "Metadata extension; you should ensure both ServiceName and "
              "Metadata properties are not present in the channel "
              "request");
          goto error;
        }
    }

  DEBUG ("Requested outgoing channel with contact: %s",
      tp_handle_inspect (contact_repo, handle));

  chan = salut_file_transfer_channel_new (priv->connection, contact,
      handle, base_connection->self_handle,
      TP_FILE_TRANSFER_STATE_PENDING, content_type, filename, size,
      content_hash_type, content_hash, description, date, initial_offset,
      file_uri, service_name, metadata);

  g_object_unref (contact);

  if (!salut_file_transfer_channel_offer_file (chan, &error))
    {
      /* Pretend the chan was closed so it's removed from the channels
       * list and unreffed. */
      file_channel_closed (self, chan);
      goto error;
    }

  salut_ft_manager_channel_created (self, chan, request_token);

  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}

/* Keep in sync with values set in
 * salut_ft_manager_type_foreach_channel_class */
static const gchar * const file_transfer_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};

  /* ContentHashType has to be first so we can easily skip it if needed (we
   * currently don't as Salut doesn't support any hash mechanism) */
#define STANDARD_PROPERTIES \
  TP_PROP_CHANNEL_TYPE_FILE_TRANSFER_CONTENT_HASH_TYPE, \
  TP_PROP_CHANNEL_TARGET_HANDLE, \
  TP_PROP_CHANNEL_TARGET_ID, \
  TP_PROP_CHANNEL_TYPE_FILE_TRANSFER_CONTENT_TYPE, \
  TP_PROP_CHANNEL_TYPE_FILE_TRANSFER_FILENAME, \
  TP_PROP_CHANNEL_TYPE_FILE_TRANSFER_SIZE, \
  TP_PROP_CHANNEL_TYPE_FILE_TRANSFER_CONTENT_HASH, \
  TP_PROP_CHANNEL_TYPE_FILE_TRANSFER_DESCRIPTION, \
  TP_PROP_CHANNEL_TYPE_FILE_TRANSFER_DATE, \
  TP_PROP_CHANNEL_TYPE_FILE_TRANSFER_INITIAL_OFFSET, \
  TP_PROP_CHANNEL_TYPE_FILE_TRANSFER_URI

static const gchar * const file_transfer_channel_allowed_properties[] =
{
  STANDARD_PROPERTIES,
  NULL
};

static const gchar * const file_transfer_channel_allowed_properties_with_metadata_prop[] =
{
  STANDARD_PROPERTIES,
  TP_PROP_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA_METADATA,
  NULL
};

static const gchar * const file_transfer_channel_allowed_properties_with_both_metadata_props[] =
{
  STANDARD_PROPERTIES,
  TP_PROP_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA_SERVICE_NAME,
  TP_PROP_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA_METADATA,
  NULL
};

static void
salut_ft_manager_type_foreach_channel_class (GType type,
    TpChannelManagerTypeChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table;
  GValue *value;

  table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);

  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType" , value);

  value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetHandleType", value);

  func (type, table, file_transfer_channel_allowed_properties_with_both_metadata_props,
      user_data);

  g_hash_table_unref (table);
}

static void
channel_manager_iface_init (gpointer g_iface,
                            gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = salut_ft_manager_foreach_channel;
  iface->type_foreach_channel_class =
    salut_ft_manager_type_foreach_channel_class;
  iface->request_channel = salut_ft_manager_handle_request;
  iface->create_channel = salut_ft_manager_handle_request;
  iface->ensure_channel = salut_ft_manager_handle_request;
}

/* public functions */
SalutFtManager *
salut_ft_manager_new (SalutConnection *connection,
                      SalutContactManager *contact_manager)
{
  SalutFtManager *ret = NULL;
  SalutFtManagerPrivate *priv;

  g_assert (connection != NULL);

  ret = g_object_new (SALUT_TYPE_FT_MANAGER, NULL);
  priv = SALUT_FT_MANAGER_GET_PRIVATE (ret);

  priv->contact_manager = contact_manager;
  g_object_ref (contact_manager);

  priv->connection = connection;

  priv->message_handler_id = wocky_porter_register_handler_from_anyone (
      priv->connection->porter,
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
      WOCKY_PORTER_HANDLER_PRIORITY_NORMAL,
      message_stanza_callback, ret, NULL);

  return ret;
}

static void
add_file_transfer_channel_class (GPtrArray *arr,
    gboolean include_metadata_properties,
    const gchar *service_name_str)
{
  GValue monster = {0, };
  GHashTable *fixed_properties;
  GValue *channel_type_value;
  GValue *target_handle_type_value;
  GValue *service_name_value;
  const gchar * const *allowed_properties;

  g_value_init (&monster, TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS);
  g_value_take_boxed (&monster,
      dbus_g_type_specialized_construct (
        TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS));

  fixed_properties = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  channel_type_value = tp_g_value_slice_new_static_string (
      TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER);
  g_hash_table_insert (fixed_properties, TP_IFACE_CHANNEL ".ChannelType",
      channel_type_value);

  target_handle_type_value = tp_g_value_slice_new_uint (
      TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (fixed_properties, TP_IFACE_CHANNEL ".TargetHandleType",
      target_handle_type_value);

  if (service_name_str != NULL)
    {
      service_name_value = tp_g_value_slice_new_string (service_name_str);
      g_hash_table_insert (fixed_properties,
          TP_PROP_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA_SERVICE_NAME,
          service_name_value);
    }

  if (include_metadata_properties)
    {
      if (service_name_str == NULL)
        allowed_properties = file_transfer_channel_allowed_properties_with_both_metadata_props;
      else
        allowed_properties = file_transfer_channel_allowed_properties_with_metadata_prop;
    }
  else
    {
      allowed_properties = file_transfer_channel_allowed_properties;
    }

  dbus_g_type_struct_set (&monster,
      0, fixed_properties,
      1, allowed_properties,
      G_MAXUINT);

  g_hash_table_unref (fixed_properties);

  g_ptr_array_add (arr, g_value_get_boxed (&monster));
}

static void
get_contact_caps_foreach (gpointer data,
    gpointer user_data)
{
  const gchar *ns = data;
  GPtrArray *arr = user_data;

  if (!g_str_has_prefix (ns, NS_TP_FT_METADATA "#"))
    return;

  add_file_transfer_channel_class (arr, TRUE,
      ns + strlen (NS_TP_FT_METADATA "#"));
}

static void
salut_ft_manager_get_contact_caps_from_set (
    GabbleCapsChannelManager *iface,
    TpHandle handle,
    const GabbleCapabilitySet *caps,
    GPtrArray *arr)
{
  /* If we don't receive any capabilities info (QUIRK_NOT_XEP_CAPABILITIES)
   * we assume FT is supported to ensure interoperability with other clients */
  if (gabble_capability_set_has (caps, WOCKY_XMPP_NS_IQ_OOB) ||
      gabble_capability_set_has (caps, WOCKY_XMPP_NS_X_OOB) ||
      gabble_capability_set_has (caps, QUIRK_NOT_XEP_CAPABILITIES))
    {
      add_file_transfer_channel_class (arr,
          gabble_capability_set_has (caps, NS_TP_FT_METADATA), NULL);
    }

  gabble_capability_set_foreach (caps, get_contact_caps_foreach, arr);
}

static void
salut_ft_manager_represent_client (
    GabbleCapsChannelManager *iface G_GNUC_UNUSED,
    const gchar *client_name,
    const GPtrArray *filters,
    const gchar * const *cap_tokens G_GNUC_UNUSED,
    GabbleCapabilitySet *cap_set,
    GPtrArray *data_forms)
{
  guint i;

  for (i = 0; i < filters->len; i++)
    {
      GHashTable *channel_class = g_ptr_array_index (filters, i);
      const gchar *service_name;
      gchar *ns;

      if (tp_strdiff (tp_asv_get_string (channel_class,
              TP_IFACE_CHANNEL ".ChannelType"),
            TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER))
        continue;

      if (tp_asv_get_uint32 (channel_class,
            TP_IFACE_CHANNEL ".TargetHandleType", NULL)
          != TP_HANDLE_TYPE_CONTACT)
        continue;

      DEBUG ("client %s supports file transfer", client_name);
      gabble_capability_set_add (cap_set, WOCKY_XMPP_NS_IQ_OOB);
      gabble_capability_set_add (cap_set, WOCKY_XMPP_NS_X_OOB);
      gabble_capability_set_add (cap_set, NS_TP_FT_METADATA);

      /* now look at service names */

      /* capabilities mean being able to RECEIVE said kinds of
       * FTs. hence, skip Requested=true (locally initiated) channel
       * classes */
      if (tp_asv_get_boolean (channel_class,
              TP_PROP_CHANNEL_REQUESTED, FALSE))
        continue;

      service_name = tp_asv_get_string (channel_class,
          TP_PROP_CHANNEL_INTERFACE_FILE_TRANSFER_METADATA_SERVICE_NAME);

      if (service_name == NULL)
        continue;

      ns = g_strconcat (NS_TP_FT_METADATA "#", service_name, NULL);

      DEBUG ("%s: adding capability %s", client_name, ns);
      gabble_capability_set_add (cap_set, ns);
      g_free (ns);
    }
}

static void
gabble_caps_channel_manager_iface_init (GabbleCapsChannelManagerIface *iface)
{
  iface->get_contact_caps = salut_ft_manager_get_contact_caps_from_set;
  iface->represent_client = salut_ft_manager_represent_client;
}
