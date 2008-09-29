/*
 * salut-avahi-muc-channel.h - Header for SalutAvahiMucChannel
 * Copyright (C) 2008 Collabora Ltd.
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

#ifndef __SALUT_AVAHI_MUC_CHANNEL_H__
#define __SALUT_AVAHI_MUC_CHANNEL_H__

#include <glib-object.h>

#include <gibber/gibber-muc-connection.h>

#include "salut-muc-channel.h"
#include "salut-connection.h"
#include "salut-xmpp-connection-manager.h"
#include "salut-avahi-discovery-client.h"

G_BEGIN_DECLS

typedef struct _SalutAvahiMucChannel SalutAvahiMucChannel;
typedef struct _SalutAvahiMucChannelClass SalutAvahiMucChannelClass;

struct _SalutAvahiMucChannelClass
{
  SalutMucChannelClass parent_class;
};

struct _SalutAvahiMucChannel
{
  SalutMucChannel parent;

  gpointer priv;
};

GType salut_avahi_muc_channel_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_AVAHI_MUC_CHANNEL \
  (salut_avahi_muc_channel_get_type ())
#define SALUT_AVAHI_MUC_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_AVAHI_MUC_CHANNEL, SalutAvahiMucChannel))
#define SALUT_AVAHI_MUC_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_AVAHI_MUC_CHANNEL, SalutAvahiMucChannelClass))
#define SALUT_IS_AVAHI_MUC_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_AVAHI_MUC_CHANNEL))
#define SALUT_IS_AVAHI_MUC_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_AVAHI_MUC_CHANNEL))
#define SALUT_AVAHI_MUC_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_AVAHI_MUC_CHANNEL, SalutAvahiMucChannelClass))

SalutAvahiMucChannel * salut_avahi_muc_channel_new (SalutConnection *connection,
    const gchar *path, GibberMucConnection *muc_connection, TpHandle handle,
    const gchar *name, SalutAvahiDiscoveryClient *discovery_client,
    TpHandle initiator, gboolean creator, SalutXmppConnectionManager *xcm);

G_END_DECLS

#endif /* #ifndef __SALUT_AVAHI_MUC_CHANNEL_H__*/
