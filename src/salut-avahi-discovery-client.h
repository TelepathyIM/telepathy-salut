/*
 * salut-avahi-discovery-client.h - Header for SalutAvahiDiscoveryClient
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

#ifndef __SALUT_AVAHI_DISCOVERY_CLIENT_H__
#define __SALUT_AVAHI_DISCOVERY_CLIENT_H__

#include <glib-object.h>

#include <netdb.h>

#include <avahi-gobject/ga-client.h>

#include "salut-discovery-client.h"

G_BEGIN_DECLS

typedef struct _SalutAvahiDiscoveryClient SalutAvahiDiscoveryClient;
typedef struct _SalutAvahiDiscoveryClientClass SalutAvahiDiscoveryClientClass;

struct _SalutAvahiDiscoveryClientClass {
  GObjectClass parent_class;
};

struct _SalutAvahiDiscoveryClient {
  GObject parent;

  GaClient *avahi_client;

  gpointer priv;
};

GType salut_avahi_discovery_client_get_type (void);

const gchar * salut_avahi_discovery_client_get_dnssd_name (
    SalutAvahiDiscoveryClient *self);

/* TYPE MACROS */
#define SALUT_TYPE_AVAHI_DISCOVERY_CLIENT \
  (salut_avahi_discovery_client_get_type ())
#define SALUT_AVAHI_DISCOVERY_CLIENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_AVAHI_DISCOVERY_CLIENT,\
                              SalutAvahiDiscoveryClient))
#define SALUT_AVAHI_DISCOVERY_CLIENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_AVAHI_DISCOVERY_CLIENT,\
                           SalutAvahiDiscoveryClientClass))
#define SALUT_IS_AVAHI_DISCOVERY_CLIENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_AVAHI_DISCOVERY_CLIENT))
#define SALUT_IS_AVAHI_DISCOVERY_CLIENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_AVAHI_DISCOVERY_CLIENT))
#define SALUT_AVAHI_DISCOVERY_CLIENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_AVAHI_DISCOVERY_CLIENT,\
                              SalutAvahiDiscoveryClientClass))

G_END_DECLS

#endif /* #ifndef __SALUT_AVAHI_DISCOVERY_CLIENT_H__ */
