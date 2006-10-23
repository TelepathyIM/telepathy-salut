/*
 * tp-channel-iface.h - Headers for Telepathy Channel interface
 *
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
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

#ifndef __TP_CHANNEL_IFACE_H__
#define __TP_CHANNEL_IFACE_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define TP_TYPE_CHANNEL_IFACE tp_channel_iface_get_type()

#define TP_CHANNEL_IFACE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  TP_TYPE_CHANNEL_IFACE, TpChannelIface))

#define TP_CHANNEL_IFACE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  TP_TYPE_CHANNEL_IFACE, TpChannelIfaceClass))

#define TP_IS_CHANNEL_IFACE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  TP_TYPE_CHANNEL_IFACE))

#define TP_IS_CHANNEL_IFACE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  TP_TYPE_CHANNEL_IFACE))

#define TP_CHANNEL_IFACE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), \
  TP_TYPE_CHANNEL_IFACE, TpChannelIfaceClass))

typedef struct _TpChannelIface TpChannelIface;
typedef struct _TpChannelIfaceClass TpChannelIfaceClass;
typedef void (* TpChannelFunc) (TpChannelIface *, gpointer);

struct _TpChannelIfaceClass {
  GTypeInterface parent_class;
};

GType tp_channel_iface_get_type (void);

G_END_DECLS

#endif /* __TP_CHANNEL_IFACE_H__ */
