/*
 * salut-contact-channel.h - Header for SalutContactChannel
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

#ifndef __SALUT_CONTACT_CHANNEL_H__
#define __SALUT_CONTACT_CHANNEL_H__

#include <glib-object.h>
#include "group-mixin.h"

G_BEGIN_DECLS

typedef struct _SalutContactChannel SalutContactChannel;
typedef struct _SalutContactChannelClass SalutContactChannelClass;

struct _SalutContactChannelClass {
    GObjectClass parent_class;
    GroupMixinClass group_class;
};

struct _SalutContactChannel {
    GObject parent;
    GroupMixin group;
};

GType salut_contact_channel_get_type(void);

/* TYPE MACROS */
#define SALUT_TYPE_CONTACT_CHANNEL \
  (salut_contact_channel_get_type())
#define SALUT_CONTACT_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_CONTACT_CHANNEL, SalutContactChannel))
#define SALUT_CONTACT_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_CONTACT_CHANNEL, SalutContactChannelClass))
#define SALUT_IS_CONTACT_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_CONTACT_CHANNEL))
#define SALUT_IS_CONTACT_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_CONTACT_CHANNEL))
#define SALUT_CONTACT_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_CONTACT_CHANNEL, SalutContactChannelClass))


gboolean
salut_contact_channel_add_members (SalutContactChannel *self,
                                   const GArray *contacts,
                                   const gchar *message,
                                   GError **error);

gboolean
salut_contact_channel_close (SalutContactChannel *self,
                             GError **error);

gboolean
salut_contact_channel_get_all_members (SalutContactChannel *self,
                                       GArray **ret,
                                       GArray **ret1,
                                       GArray **ret2,
                                       GError **error);

gboolean
salut_contact_channel_get_channel_type (SalutContactChannel *self,
                                        gchar **ret,
                                        GError **error);

gboolean
salut_contact_channel_get_group_flags (SalutContactChannel *self,
                                       guint *ret,
                                       GError **error);

gboolean
salut_contact_channel_get_handle (SalutContactChannel *self,
                                  guint *ret,
                                  guint *ret1,
                                  GError **error);

gboolean
salut_contact_channel_get_handle_owners (SalutContactChannel *self,
                                         const GArray *handles,
                                         GArray **ret,
                                         GError **error);

gboolean
salut_contact_channel_get_interfaces (SalutContactChannel *self,
                                      gchar ***ret,
                                      GError **error);

gboolean
salut_contact_channel_get_local_pending_members (SalutContactChannel *self,
                                                 GArray **ret,
                                                 GError **error);

gboolean
salut_contact_channel_get_local_pending_members_with_info (SalutContactChannel *self,
                                                           GPtrArray **ret,
                                                           GError **error);

gboolean
salut_contact_channel_get_members (SalutContactChannel *self,
                                   GArray **ret,
                                   GError **error);

gboolean
salut_contact_channel_get_remote_pending_members (SalutContactChannel *self,
                                                  GArray **ret,
                                                  GError **error);

gboolean
salut_contact_channel_get_self_handle (SalutContactChannel *self,
                                       guint *ret,
                                       GError **error);

gboolean
salut_contact_channel_remove_members (SalutContactChannel *self,
                                      const GArray *contacts,
                                      const gchar *message,
                                      GError **error);



G_END_DECLS

#endif /* #ifndef __SALUT_CONTACT_CHANNEL_H__*/
