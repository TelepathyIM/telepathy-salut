/*
 * handles.c - mechanism to store and retrieve handles on a connection
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#include <glib.h>
#include <dbus/dbus-glib.h>
#include <string.h>

#include "gheap.h"
#include "handle-repository.h"
#include "handle-set.h"
#include "telepathy-errors.h"
#include "telepathy-helpers.h"

#include "config.h"

#ifdef ENABLE_HANDLE_LEAK_DEBUG
#include <stdlib.h>
#include <stdio.h>
#include <execinfo.h>

typedef struct _HandleLeakTrace HandleLeakTrace;

struct _HandleLeakTrace
{
  char **trace;
  int len;
};

static void
handle_leak_trace_free (HandleLeakTrace *hltrace)
{
  free (hltrace->trace);
  g_free (hltrace);
}

static void
handle_leak_trace_free_gfunc (gpointer data, gpointer user_data)
{
  return handle_leak_trace_free ((HandleLeakTrace *) data);
}

#endif /* ENABLE_HANDLE_LEAK_DEBUG */

typedef struct _HandlePriv HandlePriv;

struct _HandlePriv
{
  guint refcount;
  gchar *string;
#ifdef ENABLE_HANDLE_LEAK_DEBUG
  GSList *traces;
#endif /* ENABLE_HANDLE_LEAK_DEBUG */
  GData *datalist;
};

struct _HandleRepo
{
  GHashTable *contact_handles;
  GHashTable *room_handles;
  GData *list_handles;
  GHashTable *contact_strings;
  GHashTable *room_strings;
  GHeap *free_contact_handles;
  GHeap *free_room_handles;
  guint contact_serial;
  guint room_serial;
  GData *client_contact_handle_sets;
  GData *client_room_handle_sets;
  DBusGProxy *bus_service_proxy;
};

static const char *list_handle_strings[LIST_HANDLE_LAST] =
{
    "publish",      /* LIST_HANDLE_PUBLISH */
    "subscribe",    /* LIST_HANDLE_SUBSCRIBE */
    "known",        /* LIST_HANDLE_KNOWN */
};

/* private functions */

static HandlePriv *
handle_priv_new ()
{
  HandlePriv *priv;

  priv = g_new0 (HandlePriv, 1);

  g_datalist_init (&(priv->datalist));
  return priv;
}

static void
handle_priv_free (HandlePriv *priv)
{
  g_assert (priv != NULL);

  g_free(priv->string);
  g_datalist_clear (&(priv->datalist));
#ifdef ENABLE_HANDLE_LEAK_DEBUG
  g_slist_foreach (priv->traces, handle_leak_trace_free_gfunc, NULL);
  g_slist_free (priv->traces);
#endif /* ENABLE_HANDLE_LEAK_DEBUG */
  g_free (priv);
}

HandlePriv *
handle_priv_lookup (HandleRepo *repo,
                    TpHandleType type,
                    Handle handle)
{
  HandlePriv *priv = NULL;

  g_assert (repo != NULL);
  g_assert (handle_type_is_valid (type, NULL));
  g_assert (handle != 0);

  switch (type) {
    case TP_HANDLE_TYPE_CONTACT:
      priv = g_hash_table_lookup (repo->contact_handles, GINT_TO_POINTER (handle));
      break;
    case TP_HANDLE_TYPE_ROOM:
      priv = g_hash_table_lookup (repo->room_handles, GINT_TO_POINTER (handle));
      break;
    case TP_HANDLE_TYPE_LIST:
      priv = g_datalist_id_get_data (&repo->list_handles, handle);
      break;
    default:
      g_assert_not_reached();
    }

  return priv;
}

static Handle
handle_alloc (HandleRepo *repo, TpHandleType type)
{
  Handle ret = 0;

  g_assert (repo != NULL);
  g_assert (handle_type_is_valid (type, NULL));

  switch (type) {
    case TP_HANDLE_TYPE_CONTACT:
      if (g_heap_size (repo->free_contact_handles))
        ret = GPOINTER_TO_UINT (g_heap_extract_first (repo->free_contact_handles));
      else
        ret = repo->contact_serial++;
      break;
    case TP_HANDLE_TYPE_ROOM:
      if (g_heap_size (repo->free_room_handles))
        ret = GPOINTER_TO_UINT (g_heap_extract_first (repo->free_room_handles));
      else
        ret = repo->room_serial++;
      break;
    default:
      g_assert_not_reached();
    }

  return ret;
}

static gint
handle_compare_func (gconstpointer a, gconstpointer b)
{
  Handle first = GPOINTER_TO_UINT (a);
  Handle second = GPOINTER_TO_UINT (b);

  return (first == second) ? 0 : ((first < second) ? -1 : 1);
}

void
handle_priv_remove (HandleRepo *repo,
                    TpHandleType type,
                    Handle handle)
{
  HandlePriv *priv;
  const gchar *string;

  g_assert (handle_type_is_valid (type, NULL));
  g_assert (handle != 0);
  g_assert (repo != NULL);

  priv = handle_priv_lookup (repo, type, handle);

  g_assert (priv != NULL);

  string = priv->string;

  switch (type) {
    case TP_HANDLE_TYPE_CONTACT:
      g_hash_table_remove (repo->contact_strings, string);
      g_hash_table_remove (repo->contact_handles, GINT_TO_POINTER (handle));
      if (handle == repo->contact_serial-1)
        repo->contact_serial--;
      else
        g_heap_add (repo->free_contact_handles, GUINT_TO_POINTER (handle));
      break;
    case TP_HANDLE_TYPE_ROOM:
      g_hash_table_remove (repo->room_strings, string);
      g_hash_table_remove (repo->room_handles, GINT_TO_POINTER (handle));
      if (handle == repo->room_serial-1)
        repo->room_serial--;
      else
        g_heap_add (repo->free_room_handles, GUINT_TO_POINTER (handle));
      break;
    case TP_HANDLE_TYPE_LIST:
      g_dataset_id_remove_data (&repo->list_handles, handle);
      break;
    default:
      g_assert_not_reached ();
    }
}

void
handles_name_owner_changed_cb (DBusGProxy *proxy,
                               const gchar *name,
                               const gchar *old_owner,
                               const gchar *new_owner,
                               gpointer data)
{
  HandleRepo *repo = (HandleRepo *) data;

  if (old_owner && strlen (old_owner))
    {
      if (!new_owner || !strlen (new_owner))
        {
          g_datalist_remove_data (&repo->client_contact_handle_sets, old_owner);
          g_datalist_remove_data (&repo->client_room_handle_sets, old_owner);
        }
    }
}

/* public API */

/**
 * handle_name_is_valid
 *
 * Validates a name for given handle type and returns TRUE/FALSE
 * on success/failure. In the latter case further information is
 * provided through error if set.
 */
gboolean
handle_name_is_valid (TpHandleType type, const gchar *name, GError **error)
{
  switch (type) {
    case TP_HANDLE_TYPE_CONTACT:
    case TP_HANDLE_TYPE_ROOM:
    case TP_HANDLE_TYPE_LIST:
      /* FIXME actually do some checking */
      return TRUE;
    default:
      g_assert_not_reached();
  }
  return TRUE;
}

gboolean
handle_type_is_valid (TpHandleType type, GError **error)
{
  gboolean ret;

  if (type > TP_HANDLE_TYPE_NONE && type <= TP_HANDLE_TYPE_LIST)
    {
      ret = TRUE;
    }
  else
    {
      if (error != NULL)
        {
          *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                                "invalid handle type %u", type);
        }

      ret = FALSE;
    }

  return ret;
}

HandleRepo *
handle_repo_new ()
{
  HandleRepo *repo;
  Handle h;

  g_assert(sizeof(list_handle_strings)/sizeof(char *) == LIST_HANDLE_LAST);

  repo = g_new0 (HandleRepo, 1);

  repo->contact_handles = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) handle_priv_free);

  repo->room_handles = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) handle_priv_free);

  repo->contact_strings = g_hash_table_new (g_str_hash, g_str_equal);
  repo->room_strings = g_hash_table_new (g_str_hash, g_str_equal);

  repo->free_contact_handles = g_heap_new (handle_compare_func);
  repo->free_room_handles = g_heap_new (handle_compare_func);

  repo->contact_serial = 1;
  repo->room_serial = 1;

  g_datalist_init (&repo->list_handles);

  for (h = LIST_HANDLE_FIRST; h <= LIST_HANDLE_LAST; h++) {
    g_datalist_id_set_data_full (&repo->list_handles, (GQuark) h,
        handle_priv_new(), (GDestroyNotify) handle_priv_free);
  }

  repo->bus_service_proxy = dbus_g_proxy_new_for_name (tp_get_bus(),
                                                       DBUS_SERVICE_DBUS,
                                                       DBUS_PATH_DBUS,
                                                       DBUS_INTERFACE_DBUS);

  dbus_g_proxy_add_signal (repo->bus_service_proxy,
                           "NameOwnerChanged",
                           G_TYPE_STRING,
                           G_TYPE_STRING,
                           G_TYPE_STRING,
                           G_TYPE_INVALID);
  dbus_g_proxy_connect_signal (repo->bus_service_proxy,
                               "NameOwnerChanged",
                               G_CALLBACK (handles_name_owner_changed_cb),
                               repo,
                               NULL);

  return repo;
}

#ifdef ENABLE_HANDLE_LEAK_DEBUG

static void
handle_leak_debug_printbt_foreach (gpointer data, gpointer user_data)
{
  HandleLeakTrace *hltrace = (HandleLeakTrace *) data;
  int i;

  for (i = 1; i < hltrace->len; i++)
    {
      printf ("\t\t%s\n", hltrace->trace[i]);
    }

  printf ("\n");
}

static void
handle_leak_debug_printhandles_foreach (gpointer key, gpointer value, gpointer ignore)
{
  Handle handle = GPOINTER_TO_UINT (key);
  HandlePriv *priv = (HandlePriv *) value;

  printf ("\t%5u: %s (%u refs), traces:\n", handle, priv->string, priv->refcount);
  
  g_slist_foreach (priv->traces, handle_leak_debug_printbt_foreach, NULL);
}

static void
handle_leak_debug_print_report (HandleRepo *repo)
{
  g_assert (repo != NULL);

  printf ("The following contact handles were not freed:\n");
  g_hash_table_foreach (repo->contact_handles, handle_leak_debug_printhandles_foreach, NULL);
  printf ("The following room handles were not freed:\n");
  g_hash_table_foreach (repo->room_handles, handle_leak_debug_printhandles_foreach, NULL);
}

static HandleLeakTrace *
handle_leak_debug_bt ()
{
  void *bt_addresses[16];
  HandleLeakTrace *ret = g_new0 (HandleLeakTrace, 1);
  
  ret->len = backtrace (bt_addresses, 16);
  ret->trace = backtrace_symbols (bt_addresses, ret->len);

  return ret;
}

#define HANDLE_LEAK_DEBUG_DO(traces_slist) \
  { (traces_slist) =  g_slist_append ((traces_slist), handle_leak_debug_bt ()); }

#else /* !ENABLE_HANDLE_LEAK_DEBUG */

#define HANDLE_LEAK_DEBUG_DO(traces_slist) {}

#endif /* ENABLE_HANDLE_LEAK_DEBUG */

void
handle_repo_destroy (HandleRepo *repo)
{
  g_assert (repo != NULL);
  g_assert (repo->contact_handles);
  g_assert (repo->room_handles);
  g_assert (repo->contact_strings);
  g_assert (repo->room_strings);

  g_datalist_clear (&repo->client_contact_handle_sets);
  g_datalist_clear (&repo->client_room_handle_sets);

#ifdef ENABLE_HANDLE_LEAK_DEBUG
  handle_leak_debug_print_report (repo);
#endif /* ENABLE_HANDLE_LEAK_DEBUG */

  g_hash_table_destroy (repo->contact_handles);
  g_hash_table_destroy (repo->room_handles);
  g_hash_table_destroy (repo->contact_strings);
  g_hash_table_destroy (repo->room_strings);
  g_heap_destroy (repo->free_contact_handles);
  g_heap_destroy (repo->free_room_handles);
  g_datalist_clear (&repo->list_handles);

  dbus_g_proxy_disconnect_signal (repo->bus_service_proxy,
                                  "NameOwnerChanged",
                                  G_CALLBACK (handles_name_owner_changed_cb),
                                  repo);
  g_object_unref (G_OBJECT (repo->bus_service_proxy));

  g_free (repo);
}

gboolean
handle_is_valid (HandleRepo *repo, TpHandleType type, Handle handle, GError **error)
{
  GArray *arr;
  gboolean ret;

  arr = g_array_new (FALSE, FALSE, sizeof (Handle));
  g_array_insert_val (arr, 0, handle);

  ret = handles_are_valid (repo, type, arr, FALSE, error);

  g_array_free (arr, TRUE);

  return ret;
}

gboolean
handles_are_valid (HandleRepo *repo,
                          TpHandleType type,
                          const GArray *array,
                          gboolean allow_zero,
                          GError **error)
{
  int i;

  g_return_val_if_fail (repo != NULL, FALSE);
  g_return_val_if_fail (array != NULL, FALSE);

  if (!handle_type_is_valid (type, error))
    return FALSE;

  for (i = 0; i < array->len; i++)
    {
      Handle handle = g_array_index (array, Handle, i);

      if ((handle == 0 && !allow_zero) ||
          (handle_priv_lookup (repo, type, handle) == NULL))
        {
          if (error != NULL)
            {
              *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                  "invalid handle %u", handle);
            }

          return FALSE;
        }
    }

  return TRUE;
}

gboolean
handle_ref (HandleRepo *repo,
                   TpHandleType type,
                   Handle handle)
{
  HandlePriv *priv;

  if (type == TP_HANDLE_TYPE_LIST)
    {
      if (handle >= LIST_HANDLE_FIRST && handle <= LIST_HANDLE_LAST)
        return TRUE;
      else
        return FALSE;
    }

  priv = handle_priv_lookup (repo, type, handle);

  if (priv == NULL)
    return FALSE;

  priv->refcount++;

  HANDLE_LEAK_DEBUG_DO (priv->traces);

  return TRUE;
}

gboolean
handle_unref (HandleRepo *repo,
                     TpHandleType type,
                     Handle handle)
{
  HandlePriv *priv;

  if (type == TP_HANDLE_TYPE_LIST)
    {
      if (handle >= LIST_HANDLE_FIRST && handle <= LIST_HANDLE_LAST)
        return TRUE;
      else
        return FALSE;
    }

  priv = handle_priv_lookup (repo, type, handle);

  if (priv == NULL)
    return FALSE;

  HANDLE_LEAK_DEBUG_DO (priv->traces);

  g_assert (priv->refcount > 0);

  priv->refcount--;

  if (priv->refcount == 0)
    handle_priv_remove (repo, type, handle);

  return TRUE;
}

const char *
handle_inspect (HandleRepo *repo,
                       TpHandleType type,
                       Handle handle)
{
  HandlePriv *priv;

  if (type == TP_HANDLE_TYPE_LIST)
    {
      g_assert (handle >= LIST_HANDLE_FIRST
                  && handle <= LIST_HANDLE_LAST);
      return list_handle_strings[handle-1];
    }

  priv = handle_priv_lookup (repo, type, handle);

  if (priv == NULL)
    return NULL;
  else
    return priv->string;
}

static Handle
_handle_lookup_by_name (HandleRepo *repo,
                       const gchar *name)
{
  Handle handle;

  handle = GPOINTER_TO_UINT (g_hash_table_lookup (repo->contact_strings, name));

  if (0 == handle)
    return 0;

  return handle;
}

Handle
handle_for_contact (HandleRepo *repo, const char *name)
{
  Handle handle = 0;
  HandlePriv *priv;

  g_assert (name != NULL);
  g_assert (*name != '\0');

  handle = _handle_lookup_by_name (repo, name);

  if (0 != handle) {
     goto OUT;
  }

  handle = handle_alloc (repo, TP_HANDLE_TYPE_CONTACT);
  priv = handle_priv_new ();
  priv->string = g_strdup(name);
  g_hash_table_insert (repo->contact_handles, GINT_TO_POINTER (handle), priv);
  g_hash_table_insert (repo->contact_strings, priv->string, GUINT_TO_POINTER (handle));

  HANDLE_LEAK_DEBUG_DO (priv->traces);

OUT:
  return handle;
}

gboolean
handle_for_room_exists (HandleRepo *repo, const gchar *name)
{
  Handle handle;
  handle = GPOINTER_TO_UINT (g_hash_table_lookup (repo->room_strings,
                                                  name));
  
  if (handle == 0)
    return FALSE;

  return (handle_priv_lookup (repo, TP_HANDLE_TYPE_ROOM, handle) != NULL);
}

Handle
handle_for_room (HandleRepo *repo, const gchar *name)
{
  Handle handle;

  g_assert (name != NULL);
  g_assert (*name != '\0');

  handle = 0;

  handle = GPOINTER_TO_UINT (g_hash_table_lookup (repo->room_strings, name));

  if (handle == 0) {
      HandlePriv *priv;
      handle = handle_alloc (repo, TP_HANDLE_TYPE_ROOM);
      priv = handle_priv_new ();
      priv->string = g_strdup(name);
      g_hash_table_insert (repo->room_handles, GUINT_TO_POINTER (handle), priv);
      g_hash_table_insert (repo->room_strings, priv->string, GUINT_TO_POINTER (handle));
      HANDLE_LEAK_DEBUG_DO (priv->traces);
  }

  return handle;
}

Handle
handle_for_list (HandleRepo *repo,
                        const gchar *list)
{
  Handle handle = 0;
  int i;

  g_assert (repo != NULL);
  g_assert (list != NULL);

  for (i = 0; i < LIST_HANDLE_LAST; i++)
    {
      if (0 == strcmp (list_handle_strings[i], list))
        handle = (Handle) i + 1;
    }

  return handle;
}

Handle 
handle_for_type(HandleRepo *repo, TpHandleType type, const gchar *name) {
  switch (type) {
    case TP_HANDLE_TYPE_CONTACT:
      return handle_for_contact(repo, name);
    case TP_HANDLE_TYPE_ROOM:
      return handle_for_room(repo, name);
    case TP_HANDLE_TYPE_LIST:
      return handle_for_list(repo, name);
    default:
      g_assert_not_reached();
  }
}


/**
 * handle_set_qdata:
 * @repo: A #HandleRepo
 * @type: The handle type
 * @handle: A handle to set data on
 * @key_id: Key id to associate data with
 * @data: data to associate with handle
 * @destroy: A #GDestroyNotify to call to detroy the data,
 *           or NULL if not needed.
 *
 * Associates a blob of data with a given handle and a given key
 *
 * If @destroy is set, then the data is freed when the handle is freed.
 */

gboolean
handle_set_qdata (HandleRepo *repo,
                         TpHandleType type, Handle handle,
                         GQuark key_id, gpointer data, GDestroyNotify destroy)
{
  HandlePriv *priv;
  priv = handle_priv_lookup (repo, type, handle);

  if (!priv)
    return FALSE;

  g_datalist_id_set_data_full (&priv->datalist, key_id, data, destroy);
  return TRUE;
}

/**
 * handle_get_qdata:
 * @repo: A #HandleRepo
 * @type: The handle type
 * @handle: A handle to get data from
 * @key_id: Key id of data to fetch
 *
 * Gets the data associated with a given key on a given handle
 */
gpointer
handle_get_qdata (HandleRepo *repo,
                         TpHandleType type, Handle handle,
                         GQuark key_id)
{
  HandlePriv *priv;
  priv = handle_priv_lookup (repo, type, handle);

  if (!priv)
    return NULL;

  return g_datalist_id_get_data(&priv->datalist, key_id);
}

/**
 * handle_client_hold:
 * @repo: a #HandleRepo
 * @client_name: D-Bus bus name of client to hold the handle for
 * @handle: the handle to hold
 * @type: type of handle to hold
 * @error: used to return a pointer to a GError detailing any error that occurred
 *
 * Marks a handle as held by a given client.
 *
 * Returns: Whether the handle was succesfully marked as held or an error occurred.
 */
gboolean
handle_client_hold (HandleRepo *repo,
                           const gchar *client_name,
                           Handle handle,
                           TpHandleType type,
                           GError **error)
{
  GData **handle_set_list;
  HandleSet *handle_set;

  g_assert (repo != NULL);

  switch (type)
    {
    case TP_HANDLE_TYPE_CONTACT:
      handle_set_list = &repo->client_contact_handle_sets;
      break;
    case TP_HANDLE_TYPE_ROOM:
      handle_set_list = &repo->client_room_handle_sets;
      break;
    case TP_HANDLE_TYPE_LIST:
      /* no-op */
      return TRUE;
    default:
      g_critical ("%s: called with invalid handle type %u", G_STRFUNC, type);
      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument, "invalid handle type %u", type);
      return FALSE;
    }

  if (!client_name || *client_name == '\0')
    {
      g_critical ("%s: called with invalid client name", G_STRFUNC);
      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument, "invalid client name");
      return FALSE;
    }

  handle_set = (HandleSet*) g_datalist_get_data (handle_set_list, client_name);

  if (!handle_set)
    {
      handle_set = handle_set_new (repo, type);
      g_datalist_set_data_full (handle_set_list,
                                client_name,
                                handle_set,
                                (GDestroyNotify) handle_set_destroy);
    }

  handle_set_add (handle_set, handle);

  return TRUE;
}

/**
 * handle_client_release:
 * @repo: a #HandleRepo
 * @client_name: D-Bus bus name of client to release the handle for
 * @handle: the handle to release
 * @type: type of handle to release
 * @error: used to return a pointer to a GError detailing any error that occurred
 *
 * Unmarks a handle as held by a given client.
 *
 * Returns: Whether the handle had been marked as held by the given client and now unmarked or not.
 */
gboolean
handle_client_release (HandleRepo *repo,
                           const gchar *client_name,
                           Handle handle,
                           TpHandleType type,
                           GError **error)
{
  GData **handle_set_list;
  HandleSet *handle_set;

  g_assert (repo != NULL);

  switch (type)
    {
    case TP_HANDLE_TYPE_CONTACT:
      handle_set_list = &repo->client_contact_handle_sets;
      break;
    case TP_HANDLE_TYPE_ROOM:
      handle_set_list = &repo->client_room_handle_sets;
      break;
    case TP_HANDLE_TYPE_LIST:
      /* no-op */
      return TRUE;
    default:
      g_critical ("%s: called with invalid handle type %u", G_STRFUNC, type);
      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument, "invalid handle type %u", type);
      return FALSE;
    }

  if (!client_name || *client_name == '\0')
    {
      g_critical ("%s: called with invalid client name", G_STRFUNC);
      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument, "invalid client name");
      return FALSE;
    }

  handle_set = (HandleSet*) g_datalist_get_data (handle_set_list, client_name);

  if (!handle_set)
    {
      g_critical ("%s: no handle set found for the given client %s", G_STRFUNC, client_name);
      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                            "the given client %s wasn't holding any handles",
                            client_name);
      return FALSE;
    }

  if (!handle_set_remove (handle_set, handle))
    {
      g_critical ("%s: the client %s wasn't holding the handle %u", G_STRFUNC, client_name, handle);
      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                            "the given client %s wasn't holding the handle %u",
                            client_name, handle);
      return FALSE;
    }

  return TRUE;
}

