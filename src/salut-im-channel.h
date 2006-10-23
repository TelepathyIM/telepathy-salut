/*
 * salut-im-channel.h - Header for SalutIMChannel
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

#ifndef __SALUT_IM_CHANNEL_H__
#define __SALUT_IM_CHANNEL_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _SalutIMChannel SalutIMChannel;
typedef struct _SalutIMChannelClass SalutIMChannelClass;

struct _SalutIMChannelClass {
    GObjectClass parent_class;
};

struct _SalutIMChannel {
    GObject parent;
};

GType salut_im_channel_get_type(void);

/* TYPE MACROS */
#define SALUT_TYPE_IM_CHANNEL \
  (salut_im_channel_get_type())
#define SALUT_IM_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_IM_CHANNEL, SalutIMChannel))
#define SALUT_IM_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_IM_CHANNEL, SalutIMChannelClass))
#define SALUT_IS_IM_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_IM_CHANNEL))
#define SALUT_IS_IM_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_IM_CHANNEL))
#define SALUT_IM_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_IM_CHANNEL, SalutIMChannelClass))


gboolean salut_im_channel_acknowledge_pending_messages (SalutIMChannel *obj, const GArray * ids, GError **error);
gboolean salut_im_channel_close (SalutIMChannel *obj, GError **error);
gboolean salut_im_channel_get_channel_type (SalutIMChannel *obj, gchar ** ret, GError **error);
gboolean salut_im_channel_get_handle (SalutIMChannel *obj, guint* ret, guint* ret1, GError **error);
gboolean salut_im_channel_get_interfaces (SalutIMChannel *obj, gchar *** ret, GError **error);
gboolean salut_im_channel_get_message_types (SalutIMChannel *obj, GArray ** ret, GError **error);
gboolean salut_im_channel_list_pending_messages (SalutIMChannel *obj, gboolean clear, GPtrArray ** ret, GError **error);
gboolean salut_im_channel_send (SalutIMChannel *obj, guint type, const gchar * text, GError **error);


G_END_DECLS

#endif /* #ifndef __SALUT_IM_CHANNEL_H__*/
