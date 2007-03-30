/*
 * salut-muc-channel.h - Header for SalutMucChannel
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

#ifndef __SALUT_MUC_CHANNEL_H__
#define __SALUT_MUC_CHANNEL_H__

#include <glib-object.h>

#include <telepathy-glib/text-mixin.h>
#include <telepathy-glib/group-mixin.h>

G_BEGIN_DECLS

typedef struct _SalutMucChannel SalutMucChannel;
typedef struct _SalutMucChannelClass SalutMucChannelClass;

struct _SalutMucChannelClass {
  GObjectClass parent_class;
  TpGroupMixinClass group_class;
  TpTextMixinClass text_class; 
};

struct _SalutMucChannel {
    GObject parent;
    TpGroupMixin group;
    TpTextMixin text;
};

GType salut_muc_channel_get_type(void);

/* TYPE MACROS */
#define SALUT_TYPE_MUC_CHANNEL \
  (salut_muc_channel_get_type())
#define SALUT_MUC_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_MUC_CHANNEL, SalutMucChannel))
#define SALUT_MUC_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_MUC_CHANNEL, SalutMucChannelClass))
#define SALUT_IS_MUC_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_MUC_CHANNEL))
#define SALUT_IS_MUC_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_MUC_CHANNEL))
#define SALUT_MUC_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_MUC_CHANNEL, SalutMucChannelClass))

void
salut_muc_channel_invited(SalutMucChannel *self, 
                          TpHandle invitor, const gchar *message);

G_END_DECLS

#endif /* #ifndef __SALUT_MUC_CHANNEL_H__*/
