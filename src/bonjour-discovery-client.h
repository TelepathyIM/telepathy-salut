/*
 * bonjour-discovery-client.h - Header for SalutDiscoveryClient
 * Copyright (C) 2012 Collabora Ltd.
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

#ifndef __SALUT_BONJOUR_DISCOVERY_CLIENT_H__
#define __SALUT_BONJOUR_DISCOVERY_CLIENT_H__

#include <glib-object.h>


#include <dns_sd.h>

#undef interface

#include "discovery-client.h"

G_BEGIN_DECLS

typedef struct _SalutBonjourDiscoveryClient SalutBonjourDiscoveryClient;
typedef struct _SalutBonjourDiscoveryClientClass SalutBonjourDiscoveryClientClass;

struct _SalutBonjourDiscoveryClientClass {
  GObjectClass parent_class;
};

struct _SalutBonjourDiscoveryClient {
  GObject parent;
  gpointer priv;
};

GType salut_bonjour_discovery_client_get_type (void);

const gchar * salut_bonjour_discovery_client_get_dnssd_name (
    SalutBonjourDiscoveryClient *self);

void salut_bonjour_discovery_client_watch_svc_ref (
    SalutBonjourDiscoveryClient *self,
    DNSServiceRef *service);

void salut_bonjour_discovery_client_drop_svc_ref (
    SalutBonjourDiscoveryClient *self,
    DNSServiceRef *service);

/* TYPE MACROS */
#define SALUT_TYPE_BONJOUR_DISCOVERY_CLIENT \
  (salut_bonjour_discovery_client_get_type ())
#define SALUT_BONJOUR_DISCOVERY_CLIENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_BONJOUR_DISCOVERY_CLIENT,\
                              SalutBonjourDiscoveryClient))
#define SALUT_BONJOUR_DISCOVERY_CLIENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_BONJOUR_DISCOVERY_CLIENT,\
                           SalutBonjourDiscoveryClientClass))
#define SALUT_IS_BONJOUR_DISCOVERY_CLIENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_BONJOUR_DISCOVERY_CLIENT))
#define SALUT_IS_BONJOUR_DISCOVERY_CLIENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_BONJOUR_DISCOVERY_CLIENT))
#define SALUT_BONJOUR_DISCOVERY_CLIENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_BONJOUR_DISCOVERY_CLIENT,\
                              SalutBonjourDiscoveryClientClass))

G_END_DECLS

#endif /* #ifndef __SALUT_BONJOUR_DISCOVERY_CLIENT_H__ */
