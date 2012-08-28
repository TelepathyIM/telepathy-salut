/*
 * muc-channel.h - Header for SalutMucChannel
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

#include <telepathy-glib/base-channel.h>
#include <telepathy-glib/group-mixin.h>
#include <telepathy-glib/message-mixin.h>

#include <gibber/gibber-muc-connection.h>

#include "connection.h"
#include "tube-iface.h"

G_BEGIN_DECLS

typedef struct _SalutMucChannel SalutMucChannel;
typedef struct _SalutMucChannelClass SalutMucChannelClass;
typedef struct _SalutMucChannelPrivate SalutMucChannelPrivate;

struct _SalutMucChannelClass {
  TpBaseChannelClass parent_class;
  TpGroupMixinClass group_class;
  TpDBusPropertiesMixinClass dbus_props_class;

  /* Virtual method */
  gboolean (*publish_service) (SalutMucChannel *self,
      GibberMucConnection *muc_connection, const gchar *muc_name);
};

struct _SalutMucChannel {
    TpBaseChannel parent;
    TpGroupMixin group;
    TpMessageMixin message_mixin;

    /* private */
    SalutMucChannelPrivate *priv;
};

GType salut_muc_channel_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_MUC_CHANNEL \
  (salut_muc_channel_get_type ())
#define SALUT_MUC_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_MUC_CHANNEL, SalutMucChannel))
#define SALUT_MUC_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_MUC_CHANNEL, \
   SalutMucChannelClass))
#define SALUT_IS_MUC_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_MUC_CHANNEL))
#define SALUT_IS_MUC_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_MUC_CHANNEL))
#define SALUT_MUC_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_MUC_CHANNEL, SalutMucChannelClass))

gboolean
salut_muc_channel_invited (SalutMucChannel *self,
                          TpHandle invitor, const gchar *message,
                          GError **error);

gboolean
salut_muc_channel_send_invitation (SalutMucChannel *self,
    TpHandle handle, const gchar *message, GError **error);

gboolean salut_muc_channel_publish_service (SalutMucChannel *self);

/* FIXME: This is an ugly workaround. See fd.o #15092
 * We shouldn't export this function */
gboolean salut_muc_channel_add_member (GObject *iface, TpHandle handle,
    const gchar *message, GError **error);

SalutTubeIface * salut_muc_channel_tube_request (SalutMucChannel *self,
    GHashTable *request_properties);

void salut_muc_channel_foreach (SalutMucChannel *self,
    TpExportableChannelFunc func, gpointer user_data);

void salut_muc_channel_bytestream_offered (SalutMucChannel *self,
    GibberBytestreamIface *bytestream,
    WockyStanza *msg);

void salut_muc_channel_set_autoclose (SalutMucChannel *chan,
    gboolean autoclose);

gboolean salut_muc_channel_get_autoclose (SalutMucChannel *chan);

gboolean salut_muc_channel_can_be_closed (SalutMucChannel *chan);

gboolean salut_muc_channel_is_ready (SalutMucChannel *self);

G_END_DECLS

#endif /* #ifndef __SALUT_MUC_CHANNEL_H__*/
