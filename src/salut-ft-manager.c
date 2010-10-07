/*
 * salut-ft-manager.c - Source for SalutFtManager
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

#include <gibber/gibber-file-transfer.h>
#include <gibber/gibber-namespaces.h>

#include "salut-ft-manager.h"
#include "salut-signals-marshal.h"

#include "caps-channel-manager.h"
#include "salut-file-transfer-channel.h"
#include "salut-contact-manager.h"
#include "salut-presence-cache.h"

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
  SalutXmppConnectionManager *xmpp_connection_manager;
  SalutContactManager *contact_manager;
  GList *channels;
};

#define SALUT_FT_MANAGER_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_FT_MANAGER, \
                                SalutFtManagerPrivate))

static void
salut_ft_manager_init (SalutFtManager *obj)
{
  SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (obj);
  priv->xmpp_connection_manager = NULL;
  priv->contact_manager = NULL;
  priv->connection = NULL;

  /* allocate any data required by the object here */
  priv->channels = NULL;
}

static gboolean
message_stanza_filter (SalutXmppConnectionManager *mgr,
                       GibberXmppConnection *conn,
                       GibberXmppStanza *stanza,
                       SalutContact *contact,
                       gpointer user_data)
{
  return gibber_file_transfer_is_file_offer (stanza);
}

static void
message_stanza_callback (SalutXmppConnectionManager *mgr,
                         GibberXmppConnection *conn,
                         GibberXmppStanza *stanza,
                         SalutContact *contact,
                         gpointer user_data)
{
  SalutFtManager *self = SALUT_FT_MANAGER (user_data);
  SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (self);
  SalutFileTransferChannel *chan;
  TpHandle handle;
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->connection);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (base_conn,
       TP_HANDLE_TYPE_CONTACT);

  handle = tp_handle_lookup (handle_repo, contact->name, NULL, NULL);
  g_assert (handle != 0);

  DEBUG ("new incoming channel");

  /* this can fail if the stanza isn't valid */
  chan = salut_file_transfer_channel_new_from_stanza (priv->connection,
      contact, handle, priv->xmpp_connection_manager,
      TP_FILE_TRANSFER_STATE_PENDING, stanza, conn);

  if (chan != NULL)
    salut_ft_manager_channel_created (self, chan, NULL);
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

  if (priv->xmpp_connection_manager != NULL)
    {
      salut_xmpp_connection_manager_remove_stanza_filter (
          priv->xmpp_connection_manager, NULL,
          message_stanza_filter, message_stanza_callback, self);

      g_object_unref (priv->xmpp_connection_manager);
      priv->xmpp_connection_manager = NULL;
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

  contact = salut_contact_manager_get_contact (priv->contact_manager, handle);
  if (contact == NULL)
    {
      const gchar *name = tp_handle_inspect (contact_repo, handle);

      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "%s is not online", name);

      goto error;
    }

  DEBUG ("Requested outgoing channel with contact: %s",
      tp_handle_inspect (contact_repo, handle));

  chan = salut_file_transfer_channel_new (priv->connection, contact,
      handle, priv->xmpp_connection_manager, base_connection->self_handle,
      TP_FILE_TRANSFER_STATE_PENDING, content_type, filename, size,
      content_hash_type, content_hash, description, date, initial_offset);

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

static const gchar * const file_transfer_channel_allowed_properties[] =
{
   TP_IFACE_CHANNEL ".TargetHandle",
   TP_IFACE_CHANNEL ".TargetID",
   TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".ContentType",
   TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Filename",
   TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Size",
   TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".ContentHashType",
   TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".ContentHash",
   TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Description",
   TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Date",
   TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".InitialOffset",
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

  func (type, table, file_transfer_channel_allowed_properties,
      user_data);

  g_hash_table_destroy (table);
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
                      SalutContactManager *contact_manager,
                      SalutXmppConnectionManager *xmpp_connection_manager)
{
  SalutFtManager *ret = NULL;
  SalutFtManagerPrivate *priv;

  g_assert (connection != NULL);
  g_assert (xmpp_connection_manager != NULL);

  ret = g_object_new (SALUT_TYPE_FT_MANAGER, NULL);
  priv = SALUT_FT_MANAGER_GET_PRIVATE (ret);

  priv->contact_manager = contact_manager;
  g_object_ref (contact_manager);

  priv->xmpp_connection_manager = xmpp_connection_manager;
  g_object_ref (xmpp_connection_manager);

  salut_xmpp_connection_manager_add_stanza_filter (
      priv->xmpp_connection_manager, NULL,
      message_stanza_filter, message_stanza_callback, ret);

  priv->connection = connection;

  return ret;
}

static void
add_file_transfer_channel_class (GPtrArray *arr)
{
  GValue monster = {0, };
  GHashTable *fixed_properties;
  GValue *channel_type_value;
  GValue *target_handle_type_value;

  g_value_init (&monster, TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS);
  g_value_take_boxed (&monster,
      dbus_g_type_specialized_construct (
        TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS));

  fixed_properties = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  channel_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (channel_type_value,
      TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER);
  g_hash_table_insert (fixed_properties, TP_IFACE_CHANNEL ".ChannelType",
      channel_type_value);

  target_handle_type_value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (target_handle_type_value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (fixed_properties, TP_IFACE_CHANNEL ".TargetHandleType",
      target_handle_type_value);

  dbus_g_type_struct_set (&monster,
      0, fixed_properties,
      1, file_transfer_channel_allowed_properties,
      G_MAXUINT);

  g_hash_table_destroy (fixed_properties);

  g_ptr_array_add (arr, g_value_get_boxed (&monster));
}

static void
salut_ft_manager_get_contact_caps_from_set (
    GabbleCapsChannelManager *iface,
    TpHandle handle G_GNUC_UNUSED,
    const GabbleCapabilitySet *caps,
    GPtrArray *arr)
{
  SalutFtManager *self = SALUT_FT_MANAGER (iface);
  SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *base = TP_BASE_CONNECTION (priv->connection);

  if (handle == base->self_handle)
    {
      /* we currently always advertise FT ourselves */
      add_file_transfer_channel_class (arr);
      return;
    }

  /* If we don't receive any capabilities info (QUIRK_NOT_XEP_CAPABILITIES)
   * we assume FT is supported to ensure interoperability with other clients */
  if (gabble_capability_set_has (caps, GIBBER_XMPP_NS_IQ_OOB) ||
      gabble_capability_set_has (caps, GIBBER_XMPP_NS_X_OOB) ||
      gabble_capability_set_has (caps, QUIRK_NOT_XEP_CAPABILITIES))
    add_file_transfer_channel_class (arr);
}

static void
salut_ft_manager_represent_client (
    GabbleCapsChannelManager *iface G_GNUC_UNUSED,
    const gchar *client_name,
    const GPtrArray *filters,
    const gchar * const *cap_tokens G_GNUC_UNUSED,
    GabbleCapabilitySet *cap_set)
{
  guint i;

  for (i = 0; i < filters->len; i++)
    {
      GHashTable *channel_class = g_ptr_array_index (filters, i);

      if (tp_strdiff (tp_asv_get_string (channel_class,
              TP_IFACE_CHANNEL ".ChannelType"),
            TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER))
        continue;

      if (tp_asv_get_uint32 (channel_class,
            TP_IFACE_CHANNEL ".TargetHandleType", NULL)
          != TP_HANDLE_TYPE_CONTACT)
        continue;

      DEBUG ("client %s supports file transfer", client_name);
      gabble_capability_set_add (cap_set, GIBBER_XMPP_NS_IQ_OOB);
      gabble_capability_set_add (cap_set, GIBBER_XMPP_NS_X_OOB);
      /* there's no point in looking at the subsequent filters if we've
       * already added the FT capability */
      break;
    }
}

static void
gabble_caps_channel_manager_iface_init (GabbleCapsChannelManagerIface *iface)
{
  iface->get_contact_caps = salut_ft_manager_get_contact_caps_from_set;
  iface->represent_client = salut_ft_manager_represent_client;
}
