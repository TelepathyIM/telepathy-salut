/*
 * handles.h - mechanism to store and retrieve handles on a connection
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

#ifndef __HANDLE_REPOSITORY_H__
#define __HANDLE_REPOSITORY_H__

#include <glib.h>

#include "telepathy-constants.h"
#include "handle-types.h"

G_BEGIN_DECLS

typedef enum
{
  LIST_HANDLE_PUBLISH = 1,
  LIST_HANDLE_SUBSCRIBE,
  LIST_HANDLE_KNOWN,

  LIST_HANDLE_LAST = LIST_HANDLE_KNOWN,
  LIST_HANDLE_FIRST = LIST_HANDLE_PUBLISH
} ListHandle;

gboolean handle_name_is_valid (TpHandleType type, const gchar *name, 
                               GError **error);

gboolean handle_type_is_valid (TpHandleType type, GError **error);

HandleRepo *handle_repo_new ();
void handle_repo_destroy (HandleRepo *repo);

gboolean handle_is_valid (HandleRepo *repo, TpHandleType type, 
                          Handle handle, GError **error);
gboolean handles_are_valid (HandleRepo *repo, TpHandleType type, const GArray *array, gboolean allow_zero, GError **error);

gboolean handle_ref (HandleRepo *repo, TpHandleType type, Handle handle);
gboolean handle_unref (HandleRepo *repo, TpHandleType type, Handle handle);
const char *handle_inspect (HandleRepo *repo, TpHandleType type, Handle handle);

Handle handle_for_contact (HandleRepo *repo, const char *name);
gboolean handle_for_room_exists (HandleRepo *repo, const gchar *name);
Handle handle_for_room (HandleRepo *repo, const gchar *name);
Handle handle_for_list (HandleRepo *repo, const gchar *list);

Handle handle_for_type (HandleRepo *repo, TpHandleType type, const gchar *name);

gboolean handle_set_qdata (HandleRepo *repo, TpHandleType type,
    Handle handle, GQuark key_id, gpointer data, GDestroyNotify destroy);

gpointer handle_get_qdata (HandleRepo *repo, TpHandleType type,
    Handle handle, GQuark key_id);

gboolean handle_client_hold (HandleRepo *repo, const gchar *client_name, Handle handle, TpHandleType type, GError **error);

gboolean handle_client_release (HandleRepo *repo, const gchar *client_name, Handle handle, TpHandleType type, GError **error);

G_END_DECLS

#endif /* #ifndef __HANDLES_H__ */
