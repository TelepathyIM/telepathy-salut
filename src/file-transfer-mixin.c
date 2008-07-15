/*
 * file-transfer-mixin.c - Source for TpFileTransfertMixin
 * Copyright (C) 2007 Marco Barisione <marco@barisione.org>
 * Copyright (C) 2006, 2007 Collabora Ltd.
 * Copyright (C) 2006, 2007 Nokia Corporation
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

/**
 * SECTION:file-transfer-mixin
 * @title: TpFileTransferMixin
 * @short_description: a mixin implementation of the file transfer channel type
 * @see_also: #SalutSvcChannelTypeFileTransfer
 *
 * This mixin can be added to a channel GObject class to implement the file
 * transfer channel type in a general way. It implements the list of transfers
 * and manages the Unix sockets, so the implementation should only need to
 * implement OfferFile, AcceptFile and CloseFileTransfer.
 *
 * To use the file transfer mixin, include a #TpFileTransferMixinClass
 * somewhere in your class structure and a #TpFileTransferMixin somewhere in
 * your instance structure, and call tp_file_transfer_mixin_class_init() from
 * your class_init function, tp_file_transfer_mixin_init() from your init
 * function or constructor, and tp_file_transfer_mixin_finalize() from your
 * dispose or finalize function.
 *
 * To use the file transfer mixin as the implementation of
 * #SalutSvcFileTransferInterface, in the function you pass to
 * G_IMPLEMENT_INTERFACE, you should first call
 * tp_file_transfer_mixin_iface_init(), then call
 * salut_svc_channel_type_text_implement_*() to register your implementations
 * of OfferFile, AcceptFile and CloseFileTransfer.
 */

/*#include <telepathy-glib/file-transfer-mixin.h>*/
#include "file-transfer-mixin.h"

#include <glib/gstdio.h>
#include <dbus/dbus-glib.h>
#include <string.h>
#include <unistd.h>

#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>

/*#define DEBUG_FLAG TP_DEBUG_FT*/
#define DEBUG_FLAG DEBUG_FT

/*#include "internal-debug.h"*/
#include "debug.h"

#define TP_TYPE_PENDING_TRANSFERS_STRUCT \
  (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), \
      G_TYPE_INVALID))

struct _TpFileTransferMixinPrivate
{
  TpHandleRepoIface *contacts_repo;
  guint transfer_id;
  GHashTable *transfers;
  gchar *local_path;
};

/*
 * _Transfer:
 * @initiator: The handle of the contact who initiated the file transfer
 * @direction: The file transfer's direction
 * @state: The file transfer's state
 * @filename: The filename of the file that is to be transmitted
 * @information: The file's additional information
 * @contacts_repo: The contacts repo used to unref initiator
 * @user_data: User data associated to the transfer
 *
 * Represents a file transfer.
 */
typedef struct
{
  TpHandle initiator;
  TpFileTransferDirection direction;
  TpFileTransferState state;
  gchar *filename;
  GHashTable *information;
  TpHandleRepoIface *contacts_repo;
  gpointer user_data;
} _Transfer;

static _Transfer *
_transfer_new (TpHandleRepoIface *contacts_repo)
{
  _Transfer *transfer = g_new0 (_Transfer, 1);
  transfer->contacts_repo = contacts_repo;
  return transfer;
}

static void
_transfer_free (_Transfer *transfer)
{
  if (transfer == NULL)
    return;

  tp_handle_unref (transfer->contacts_repo, transfer->initiator);
  g_free (transfer->filename);
  g_hash_table_unref (transfer->information);
  g_free (transfer);
}

/**
 * tp_file_transfer_mixin_class_get_offset_quark:
 *
 * <!--no documentation beyond Returns: needed-->
 *
 * Returns: the quark used for storing mixin offset on a GObjectClass
 */
GQuark
tp_file_transfer_mixin_class_get_offset_quark ()
{
  static GQuark offset_quark = 0;
  if (!offset_quark)
    offset_quark = g_quark_from_static_string ("TpFileTransferMixinClassOffsetQuark");
  return offset_quark;
}

/**
 * tp_file_transfer_mixin_get_offset_quark:
 *
 * <!--no documentation beyond Returns: needed-->
 *
 * Returns: the quark used for storing mixin offset on a GObject
 */
GQuark
tp_file_transfer_mixin_get_offset_quark ()
{
  static GQuark offset_quark = 0;
  if (!offset_quark)
    offset_quark = g_quark_from_static_string ("TpFileTransferMixinOffsetQuark");
  return offset_quark;
}


/**
 * tp_file_transfer_mixin_class_init:
 * @obj_cls: The class of the implementation that uses this mixin
 * @offset: The byte offset of the TpFileTransferMixinClass within the class
 * structure
 *
 * Initialize the file transfer mixin. Should be called from the
 * implementation's class_init function like so:
 *
 * <informalexample><programlisting>
 * tp_file_transfer_mixin_class_init ((GObjectClass *)klass,
 *                                    G_STRUCT_OFFSET (SomeObjectClass,
 *                                                     file_transfer_mixin));
 * </programlisting></informalexample>
 */
void
tp_file_transfer_mixin_class_init (GObjectClass *obj_cls,
                                   glong offset)
{
  TpFileTransferMixinClass *mixin_cls;

  g_assert (G_IS_OBJECT_CLASS (obj_cls));

  g_type_set_qdata (G_OBJECT_CLASS_TYPE (obj_cls),
      TP_FILE_TRANSFER_MIXIN_CLASS_OFFSET_QUARK,
      GINT_TO_POINTER (offset));

  mixin_cls = TP_FILE_TRANSFER_MIXIN_CLASS (obj_cls);
}


/**
 * tp_file_transfer_mixin_init:
 * @obj: An instance of the implementation that uses this mixin
 * @offset: The byte offset of the TpFileTransferMixin within the object structure
 * @contacts_repo: The connection's %TP_HANDLE_TYPE_CONTACT repository
 *
 * Initialize the file transfer mixin. Should be called from the
 * implementation's instance init function like so:
 *
 * <informalexample><programlisting>
 * tp_file_transfer_mixin_init ((GObject *)self,
 *                              G_STRUCT_OFFSET (SomeObject,
 *                                               file_transfer_mixin),
 *                              self->contact_repo);
 * </programlisting></informalexample>
 */
void
tp_file_transfer_mixin_init (GObject *obj,
                             glong offset,
                             TpHandleRepoIface *contacts_repo)
{
  TpFileTransferMixin *mixin;

  g_assert (G_IS_OBJECT (obj));

  g_type_set_qdata (G_OBJECT_TYPE (obj),
                    TP_FILE_TRANSFER_MIXIN_OFFSET_QUARK,
                    GINT_TO_POINTER (offset));

  mixin = TP_FILE_TRANSFER_MIXIN (obj);

  mixin->priv = g_slice_new0 (TpFileTransferMixinPrivate);

  mixin->priv->transfers = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                      NULL, (GDestroyNotify)_transfer_free);
  mixin->priv->contacts_repo = contacts_repo;
  mixin->priv->transfer_id = 0;
}

/**
 * tp_file_transfer_mixin_finalize:
 * @obj: An object with this mixin.
 *
 * Free resources held by the file transfer mixin.
 */
void
tp_file_transfer_mixin_finalize (GObject *obj)
{
  TpFileTransferMixin *mixin = TP_FILE_TRANSFER_MIXIN (obj);

  DEBUG ("%p", obj);

  /* free any data held directly by the object here */

  if (mixin->priv->local_path != NULL)
    {
      g_rmdir (mixin->priv->local_path);
      g_free (mixin->priv->local_path);
    }

  g_hash_table_unref (mixin->priv->transfers);

  g_slice_free (TpFileTransferMixinPrivate, mixin->priv);
}

gboolean
tp_file_transfer_mixin_set_state (GObject *obj,
                                  guint id,
                                  TpFileTransferState state,
                                  GError **error)
{
  TpFileTransferMixin *mixin = TP_FILE_TRANSFER_MIXIN (obj);
  _Transfer *transfer;

  transfer = g_hash_table_lookup (mixin->priv->transfers, GINT_TO_POINTER (id));
  if (transfer != NULL)
    {
      transfer->state = state;
      salut_svc_channel_type_file_transfer_emit_file_transfer_state_changed (
              obj, id, state);
      return TRUE;
    }
  else
    {
      DEBUG ("invalid transfer id %u", id);
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                   "invalid transfer id %u", id);
      return FALSE;
    }
}

TpFileTransferState
tp_file_transfer_mixin_get_state (GObject *obj,
                                  guint id,
                                  GError **error)
{
  TpFileTransferMixin *mixin = TP_FILE_TRANSFER_MIXIN (obj);
  _Transfer *transfer;

  transfer = g_hash_table_lookup (mixin->priv->transfers, GINT_TO_POINTER (id));
  if (transfer != NULL)
    {
      return transfer->state;
    }
  else
    {
      DEBUG ("invalid transfer id %u", id);
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                   "invalid transfer id %u", id);
      return FALSE;
    }
}

gboolean
tp_file_transfer_mixin_set_user_data (GObject *obj,
                                      guint id,
                                      gpointer user_data)
{
  TpFileTransferMixin *mixin = TP_FILE_TRANSFER_MIXIN (obj);
  _Transfer *transfer;

  transfer = g_hash_table_lookup (mixin->priv->transfers, GINT_TO_POINTER (id));
  if (transfer != NULL)
    {
      transfer->user_data = user_data;
      return FALSE;
    }
  else
    {
      return FALSE;
    }
}

gpointer
tp_file_transfer_mixin_get_user_data (GObject *obj,
                                      guint id)
{
  TpFileTransferMixin *mixin = TP_FILE_TRANSFER_MIXIN (obj);
  _Transfer *transfer;

  transfer = g_hash_table_lookup (mixin->priv->transfers, GINT_TO_POINTER (id));
  return transfer != NULL ? transfer->user_data : NULL;
}

/**
 * tp_file_transfer_mixin_add_transfer:
 *
 * @obj: An object with the file transfer mixin
 * @initiator: The handle of the contact who initiated the file transfer
 * @direction: The file transfer's direction
 * @state: The file transfer's state
 * @filename: The filename of the file that is to be transmitted, for
 * displaying
 * @information: The file's additional information
 * @user_data: user data to associate to this transfer
 *
 * Add a file transfer.
 * This function does not emit NewFileTransfer, you have to emit it on your
 * own using tp_file_transfer_mixin_emit_new_file_transfer().
 *
 * Returns: the ID of the new file transfer.
 */
guint
tp_file_transfer_mixin_add_transfer (GObject *obj,
                                     TpHandle initiator,
                                     TpFileTransferDirection direction,
                                     TpFileTransferState state,
                                     const char *filename,
                                     GHashTable *information,
                                     gpointer user_data)
{
  /* FIXME do we need state? if the transfer is outgoing the transfer can
   * only be remote pending, else local pending. */
  TpFileTransferMixin *mixin = TP_FILE_TRANSFER_MIXIN (obj);
  _Transfer *transfer;
  guint id = mixin->priv->transfer_id++;

  tp_handle_ref (mixin->priv->contacts_repo, initiator);

  transfer = _transfer_new (mixin->priv->contacts_repo);
  transfer->initiator = initiator;
  transfer->direction = direction;
  transfer->state = state;
  transfer->filename = g_strdup (filename);
  transfer->information = g_hash_table_ref (information);
  transfer->user_data = user_data;

  g_hash_table_insert (mixin->priv->transfers, GINT_TO_POINTER (id), transfer);

  DEBUG ("new file transfer %u", id);

  return id;
}

/**
 * tp_file_transfer_mixin_emit_new_file_transfer:
 *
 * @obj: An object with the file transfer mixin
 * @id: The ID of the file transfer
 *
 * Emit NewFileTransfer for the file transfer with ID @id.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
tp_file_transfer_mixin_emit_new_file_transfer (GObject *obj,
                                               guint id,
                                               GError **error)
{
  TpFileTransferMixin *mixin = TP_FILE_TRANSFER_MIXIN (obj);
  _Transfer *transfer;

  transfer = g_hash_table_lookup (mixin->priv->transfers, GINT_TO_POINTER (id));
  if (transfer == NULL)
    {
      DEBUG ("invalid transfer id %u", id);
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                   "invalid transfer id %u", id);
      return FALSE;
    }

  DEBUG ("emitting NewFileTransfer for id %u", id);

  salut_svc_channel_type_file_transfer_emit_new_file_transfer (obj, id,
          transfer->initiator, transfer->direction, transfer->state,
          transfer->filename, transfer->information);

  return TRUE;
}

static GValue *
get_file_transfer (guint id,
                   _Transfer *transfer)
{
  GValue *ret;

  ret = g_new0 (GValue, 1);
  g_value_init (ret, TP_TYPE_PENDING_TRANSFERS_STRUCT);
  g_value_take_boxed (ret,
      dbus_g_type_specialized_construct (TP_TYPE_PENDING_TRANSFERS_STRUCT));
  dbus_g_type_struct_set (ret,
      0, id,
      1, transfer->initiator,
      2, transfer->direction,
      3, transfer->state,
      4, transfer->filename,
      5, transfer->information,
      G_MAXUINT);

  return ret;
}

gboolean
tp_file_transfer_mixin_get_file_transfer (GObject *obj,
                                          guint id,
                                          GValue **ret,
                                          GError **error)
{
  TpFileTransferMixin *mixin = TP_FILE_TRANSFER_MIXIN (obj);
  _Transfer *transfer;

  transfer = g_hash_table_lookup (mixin->priv->transfers, GINT_TO_POINTER (id));
  if (transfer == NULL)
    {
      DEBUG ("invalid transfer id %u", id);
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                   "invalid transfer id %u", id);
      return FALSE;
    }

  *ret = get_file_transfer (id, transfer);
  return TRUE;
}

static void
list_file_transfers_hash_cb (gpointer key,
                             gpointer value,
                             gpointer user_data)
{
  guint id = GPOINTER_TO_INT (key);
  _Transfer *transfer = (_Transfer *) value;
  GPtrArray *transfers = user_data;
  GValue *val;

  val = get_file_transfer (id, transfer);
  g_ptr_array_add (transfers, g_value_get_boxed (val));
  g_free (val);
}

/**
 * tp_file_transfer_mixin_list_file_transfers:
 *
 * @obj: An object with this mixin
 * @ret: Used to return a pointer to a new GPtrArray of D-Bus structures
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns false.
 *
 * Implements D-Bus method ListFileTransfers
 * on interface org.freedesktop.Telepathy.Channel.Type.FileTransfer
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
tp_file_transfer_mixin_list_file_transfers (GObject *obj,
                                            GPtrArray **ret,
                                            GError **error)
{
  TpFileTransferMixin *mixin = TP_FILE_TRANSFER_MIXIN (obj);
  guint count;
  GPtrArray *transfers;

  count = g_hash_table_size (mixin->priv->transfers);
  transfers = g_ptr_array_sized_new (count);
  g_hash_table_foreach (mixin->priv->transfers,
                        list_file_transfers_hash_cb, transfers);

  *ret = transfers;
  return TRUE;
}

static void
tp_file_transfer_mixin_list_file_transfers_async (SalutSvcChannelTypeFileTransfer *iface,
                                                  DBusGMethodInvocation *context)
{
  GPtrArray *ret;
  GError *error = NULL;

  if (tp_file_transfer_mixin_list_file_transfers (G_OBJECT (iface), &ret,
      &error))
    {
      salut_svc_channel_type_file_transfer_return_from_list_file_transfers (
          context, ret);
      g_ptr_array_free (ret, TRUE);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

static void
create_socket_path (TpFileTransferMixin *mixin)
{
  gint fd;
  gchar *tmp_path = NULL;

  while (tmp_path == NULL)
    {
      fd = g_file_open_tmp ("tp-ft-XXXXXX", &tmp_path, NULL);
      close (fd);
      g_unlink (tmp_path);
      if (g_mkdir (tmp_path, 0700) < 0)
        {
          g_free (tmp_path);
          tmp_path = NULL;
        }
    }

  mixin->priv->local_path = tmp_path;
}

static gchar *
get_local_unix_socket_path (TpFileTransferMixin *mixin,
                            guint id)
{
  gchar *id_str;
  gchar *path;

  id_str = g_strdup_printf ("id-%d", id);
  path = g_build_filename (mixin->priv->local_path, id_str, NULL);
  g_free (id_str);

  return path;
}

/**
 * tp_file_transfer_mixin_get_local_unix_path:
 *
 * @obj: An object with this mixin
 * @id: The ID of the file transfer to get an path for
 * @ret: Used to return a pointer to a new string containing the Unix
 * socket path
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns false.
 *
 * Implements D-Bus method GetLocalUnixSocketPath
 * on interface org.freedesktop.Telepathy.Channel.Type.FileTransfer
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
tp_file_transfer_mixin_get_local_unix_socket_path (GObject *obj,
                                                   guint id,
                                                   gchar **ret,
                                                   GError **error)
{
  TpFileTransferMixin *mixin = TP_FILE_TRANSFER_MIXIN (obj);

  if (mixin->priv->local_path == NULL)
    create_socket_path (mixin);

  *ret = get_local_unix_socket_path (mixin, id);

  return TRUE;
}

static void
tp_file_transfer_mixin_get_local_unix_socket_path_async (SalutSvcChannelTypeFileTransfer *iface,
                                                         guint id,
                                                         DBusGMethodInvocation *context)
{
  GError *error = NULL;
  gchar *path;

  if (tp_file_transfer_mixin_get_local_unix_socket_path (G_OBJECT (iface), id, &path,
      &error))
    {
      salut_svc_channel_type_file_transfer_return_from_get_local_unix_socket_path (
          context, path);
      g_free (path);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

/**
 * tp_file_transfer_mixin_close_file_transfer:
 *
 * @obj: An object with this mixin
 * @id: The ID of the file transfer to close
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred.
 *
 * Close the file transfer with ID @id and emit FileTransferClosed.
 * Call this function from your implementation of the CloseFileTransfer
 * method on interface org.freedesktop.Telepathy.Channel.Type.FileTransfer.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
tp_file_transfer_mixin_close_file_transfer (GObject *obj,
                                            guint id,
                                            SalutFileTransferCloseReason reason,
                                            GError **error)
{
  TpFileTransferMixin *mixin = TP_FILE_TRANSFER_MIXIN (obj);
  _Transfer *transfer;

  transfer = g_hash_table_lookup (mixin->priv->transfers,
                                  GINT_TO_POINTER (id));
  if (transfer != NULL)
    {
      if (mixin->priv->local_path != NULL)
        {
          gchar *local_socket;
          local_socket = get_local_unix_socket_path (mixin, id);
          g_unlink (local_socket);
          g_free (local_socket);
        }
      g_hash_table_remove (mixin->priv->transfers, GINT_TO_POINTER (id));
      salut_svc_channel_type_file_transfer_emit_file_transfer_closed (obj,
          id, reason);
      return TRUE;
    }
  else
    {
      DEBUG ("invalid transfer id %u", id);
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "invalid transfer id %u", id);
      return FALSE;
    }
}

/**
 * tp_file_transfer_mixin_iface_init:
 * @g_iface: A pointer to the #SalutSvcChannelTypeFileTransferClass in an object
 * class
 * @iface_data: Ignored
 *
 * Fill in this mixin's ListFileTransfers and GetLocalUnixSocketPath
 * implementations in the given interface vtable.
 * In addition to calling this function during interface initialization, the
 * implementor is expected to call
 * salut_svc_channel_type_text_implement_offer_file(),
 * salut_svc_channel_type_text_implement_accept_file() and
 * salut_svc_channel_type_text_implement_close_file_transfer() providing
 * implementations for OfferFile, AcceptFile and CloseFileTransfer.
 */
void
tp_file_transfer_mixin_iface_init (gpointer g_iface, gpointer iface_data)
{
  SalutSvcChannelTypeFileTransferClass *klass =
      (SalutSvcChannelTypeFileTransferClass *)g_iface;

#define IMPLEMENT(x) salut_svc_channel_type_file_transfer_implement_##x (klass,\
    tp_file_transfer_mixin_##x##_async)
  IMPLEMENT (list_file_transfers);
  IMPLEMENT (get_local_unix_socket_path);
  /* OfferFile, AcceptFile and CloseFileTransfer not implemented here */
#undef IMPLEMENT
}
