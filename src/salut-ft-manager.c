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

#include "salut-ft-manager.h"
#include "signals-marshal.h"

#include "salut-file-transfer-channel.h"
#include "salut-contact-manager.h"

#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/dbus.h>

#define DEBUG_FLAG DEBUG_FT
#include "debug.h"

static void
channel_manager_iface_init (gpointer, gpointer);

static SalutFileTransferChannel *
salut_ft_manager_new_channel (SalutFtManager *mgr, TpHandle handle,
    gboolean requested, GError **error);

G_DEFINE_TYPE_WITH_CODE (SalutFtManager, salut_ft_manager, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init));

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

  chan = salut_ft_manager_new_channel (self, handle, FALSE, NULL);

  /* This will set the extra properties on the ft channel */
  if (salut_file_transfer_channel_received_file_offer (chan, stanza, conn))
    {
      tp_channel_manager_emit_new_channel (self, TP_EXPORTABLE_CHANNEL (chan),
          NULL);
    }
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
    g_object_unref (l->data);

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
  if (!value)
    return;

  TpExportableChannel *chan = TP_EXPORTABLE_CHANNEL (value);
  struct foreach_data *f = (struct foreach_data *) data;

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
file_channel_closed_cb (SalutFileTransferChannel *chan, gpointer user_data)
{
  SalutFtManager *self = SALUT_FT_MANAGER (user_data);
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

static SalutFileTransferChannel *
salut_ft_manager_new_channel (SalutFtManager *mgr,
                              TpHandle handle,
                              gboolean requested,
                              GError **error)
{
  SalutFtManagerPrivate *priv = SALUT_FT_MANAGER_GET_PRIVATE (mgr);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION (priv->connection);
  TpHandleRepoIface *handle_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_CONTACT);
  SalutFileTransferChannel *chan;
  SalutContact *contact;
  const gchar *name;
  gchar *path = NULL;
  guint state;
  TpHandle initiator;
  /* Increasing guint to make sure object paths are random */
  static guint id = 0;

  DEBUG ("Requested channel for handle: %d", handle);

  name = tp_handle_inspect (handle_repo, handle);

  contact = salut_contact_manager_get_contact (priv->contact_manager, handle);
  if (contact == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "%s is not online", name);
      return NULL;
    }

  DEBUG ("%s channel requested", requested ? "Outgoing" : "Incoming");

  state = SALUT_FILE_TRANSFER_STATE_PENDING;
  if (!requested)
    {
      /* incoming channel */
      initiator = handle;
    }
  else
    {
      /* outgoing channel */
      initiator = base_connection->self_handle;
    }

  path = g_strdup_printf ("%s/FileTransferChannel/%u/%u",
                         base_connection->object_path, handle, id++);

  DEBUG ("Object path of file channel is %s", path);

  chan = g_object_new (SALUT_TYPE_FILE_TRANSFER_CHANNEL,
      "connection", priv->connection,
      "contact", contact,
      "object-path", path,
      "handle", handle,
      "xmpp-connection-manager", priv->xmpp_connection_manager,
      "initiator-handle", initiator,
      "state", state,
      NULL);

  g_object_unref (contact);
  g_free (path);

  /* Don't fire the new channel signal now so the caller of this function can
   * set the extra properties on the ft channel. */

  g_signal_connect (chan, "closed", G_CALLBACK (file_channel_closed_cb), mgr);

  priv->channels = g_list_append (priv->channels, chan);

  return chan;
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
  SalutFileHashType content_hash_type;
  GError *error = NULL;
  gboolean valid;
  GSList *requests = NULL;

  DEBUG ("File transfer request");

  /* We only support file transfer channels */
  if (tp_strdiff (tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL ".ChannelType"),
        SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER))
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
      SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".ContentType");
  if (content_type == NULL)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "ContentType property is mandatory");
      goto error;
    }

  filename = tp_asv_get_string (request_properties,
      SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Filename");
  if (filename == NULL)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Filename property is mandatory");
      goto error;
    }

  size = tp_asv_get_uint64 (request_properties,
      SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Size", NULL);
  if (size == 0)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Size property is mandatory");
      goto error;
    }

  content_hash_type = tp_asv_get_uint32 (request_properties,
      SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".ContentHashType", &valid);
  if (!valid)
    {
      /* Assume File_Hash_Type_None */
      content_hash_type = SALUT_FILE_HASH_TYPE_NONE;
    }
  else
    {
      if (content_hash_type >= NUM_SALUT_FILE_HASH_TYPES)
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "%u is not a valid ContentHashType", content_hash_type);
          goto error;
        }
    }

  if (content_hash_type != SALUT_FILE_HASH_TYPE_NONE)
    {
      content_hash = tp_asv_get_string (request_properties,
          SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".ContentHash");
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
      SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Description");

  date = tp_asv_get_uint64 (request_properties,
      SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Date", NULL);

  initial_offset = tp_asv_get_uint64 (request_properties,
      SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".InitialOffset", NULL);

  chan = salut_ft_manager_new_channel (self, handle, TRUE, &error);
  if (chan == NULL)
    {
      goto error;
    }

  g_object_set (chan,
      "content-type", content_type,
      "filename", filename,
      "size", size,
      "content-hash-type", content_hash_type,
      "content-hash", content_hash,
      "description", description,
      "date", date,
      "initial-offset", initial_offset,
      NULL);

  if (!salut_file_transfer_channel_offer_file (chan, &error))
    {
      /* Destroying the channel will emit the "closed" signal. */
      g_object_unref (chan);
      goto error;
    }

  requests = g_slist_prepend (requests, request_token);
  tp_channel_manager_emit_new_channel (manager, TP_EXPORTABLE_CHANNEL (chan),
      requests);
  g_slist_free (requests);

  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}

static const gchar * const file_transfer_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};

static const gchar * const file_transfer_channel_allowed_properties[] =
{
   TP_IFACE_CHANNEL ".TargetHandle",
   TP_IFACE_CHANNEL ".TargetID",
   SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".ContentType",
   SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Filename",
   SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Size",
   SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".ContentHashType",
   SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".ContentHash",
   SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Description",
   SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Date",
   SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".InitialOffset",
    NULL
};

static void
salut_ft_manager_foreach_channel_class (TpChannelManager *manager,
                                        TpChannelManagerChannelClassFunc func,
                                        gpointer user_data)
{
  GHashTable *table;
  GValue *value;

  table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);

  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, SALUT_IFACE_CHANNEL_TYPE_FILE_TRANSFER);
  g_hash_table_insert (table,
      (gchar *) file_transfer_channel_fixed_properties[0], value);

  value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (table,
      (gchar *) file_transfer_channel_fixed_properties[1], value);

  func (manager, table, file_transfer_channel_allowed_properties,
      user_data);

  g_hash_table_destroy (table);
}

static void
channel_manager_iface_init (gpointer g_iface,
                            gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = salut_ft_manager_foreach_channel;
  iface->foreach_channel_class = salut_ft_manager_foreach_channel_class;
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
