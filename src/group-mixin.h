/*
 * group-mixin.h - Header for GroupMixin
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *   @author Ole Andre Vadla Ravnaas <ole.andre.ravnaas@collabora.co.uk>
 *   @author Robert McQueen <robert.mcqueen@collabora.co.uk>
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

#ifndef __GROUP_MIXIN_H__
#define __GROUP_MIXIN_H__

#include "handle-repository.h"
#include "handle-set.h"

G_BEGIN_DECLS

typedef struct _GroupMixinClass GroupMixinClass;
typedef struct _GroupMixin GroupMixin;
typedef struct _GroupMixinPrivate GroupMixinPrivate;

typedef gboolean (*GroupMixinAddMemberFunc) (GObject *obj, Handle handle, const gchar *message, GError **error);
typedef gboolean (*GroupMixinRemMemberFunc) (GObject *obj, Handle handle, const gchar *message, GError **error);

struct _GroupMixinClass {
  GroupMixinAddMemberFunc add_member;
  GroupMixinRemMemberFunc remove_member;

  guint group_flags_changed_signal_id;
  guint members_changed_signal_id;
};

struct _GroupMixin {
  HandleRepo *handle_repo;
  Handle self_handle;

  TpChannelGroupFlags group_flags;

  HandleSet *members;
  HandleSet *local_pending;
  HandleSet *remote_pending;

  GroupMixinPrivate *priv;
};

/* TYPE MACROS */
#define GROUP_MIXIN_CLASS_OFFSET_QUARK (group_mixin_class_get_offset_quark())
#define GROUP_MIXIN_CLASS_OFFSET(o) (GPOINTER_TO_UINT (g_type_get_qdata (G_OBJECT_CLASS_TYPE (o), GROUP_MIXIN_CLASS_OFFSET_QUARK)))
#define GROUP_MIXIN_CLASS(o) ((GroupMixinClass *)((guchar *) o + GROUP_MIXIN_CLASS_OFFSET (o)))

#define GROUP_MIXIN_OFFSET_QUARK (group_mixin_get_offset_quark())
#define GROUP_MIXIN_OFFSET(o) (GPOINTER_TO_UINT (g_type_get_qdata (G_OBJECT_TYPE (o), GROUP_MIXIN_OFFSET_QUARK)))
#define GROUP_MIXIN(o) ((GroupMixin *)((guchar *) o + GROUP_MIXIN_OFFSET (o)))

GQuark group_mixin_class_get_offset_quark (void);
GQuark group_mixin_get_offset_quark (void);

void group_mixin_class_init (GObjectClass *obj_cls, glong offset, GroupMixinAddMemberFunc add_func, GroupMixinRemMemberFunc rem_func);

void group_mixin_init (GObject *obj, glong offset, HandleRepo *handle_repo, Handle self_handle);
void group_mixin_finalize (GObject *obj);

gboolean group_mixin_get_self_handle (GObject *obj, guint *ret, GError **error);
gboolean group_mixin_get_group_flags (GObject *obj, guint *ret, GError **error);

gboolean group_mixin_add_members (GObject *obj, const GArray *contacts, const gchar *message, GError **error);
gboolean group_mixin_remove_members (GObject *obj, const GArray *contacts, const gchar *message, GError **error);

gboolean group_mixin_get_members (GObject *obj, GArray **ret, GError **error);
gboolean group_mixin_get_local_pending_members (GObject *obj, GArray **ret, GError **error);
gboolean group_mixin_get_remote_pending_members (GObject *obj, GArray **ret, GError **error);
gboolean group_mixin_get_all_members (GObject *obj, GArray **ret, GArray **ret1, GArray **ret2, GError **error);

gboolean group_mixin_get_handle_owners (GObject *obj, const GArray *handles, GArray **ret, GError **error);

void group_mixin_change_flags (GObject *obj, TpChannelGroupFlags add, TpChannelGroupFlags remove);
gboolean group_mixin_change_members (GObject *obj, const gchar *message, GIntSet *add, GIntSet *remove, GIntSet *local_pending, GIntSet *remote_pending, Handle actor, guint reason);

void group_mixin_add_handle_owner (GObject *obj, Handle local_handle, Handle owner_handle);

G_END_DECLS

#endif /* #ifndef __GROUP_MIXIN_H__ */
