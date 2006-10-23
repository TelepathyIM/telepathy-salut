/*
 * salut-avahi-service-resolver.h - Header for SalutAvahiServiceResolver
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

#ifndef __SALUT_AVAHI_SERVICE_RESOLVER_H__
#define __SALUT_AVAHI_SERVICE_RESOLVER_H__

G_BEGIN_DECLS

#include <avahi-common/address.h>

#include <glib-object.h>
#include "salut-avahi-client.h"
#include "salut-avahi-enums.h"


typedef struct _SalutAvahiServiceResolver SalutAvahiServiceResolver;
typedef struct _SalutAvahiServiceResolverClass SalutAvahiServiceResolverClass;

struct _SalutAvahiServiceResolverClass {
    GObjectClass parent_class;
};

struct _SalutAvahiServiceResolver {
    GObject parent;
};

GType salut_avahi_service_resolver_get_type(void);

/* TYPE MACROS */
#define SALUT_TYPE_AVAHI_SERVICE_RESOLVER \
  (salut_avahi_service_resolver_get_type())
#define SALUT_AVAHI_SERVICE_RESOLVER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_AVAHI_SERVICE_RESOLVER, SalutAvahiServiceResolver))
#define SALUT_AVAHI_SERVICE_RESOLVER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_AVAHI_SERVICE_RESOLVER, SalutAvahiServiceResolverClass))
#define SALUT_IS_AVAHI_SERVICE_RESOLVER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_AVAHI_SERVICE_RESOLVER))
#define SALUT_IS_AVAHI_SERVICE_RESOLVER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_AVAHI_SERVICE_RESOLVER))
#define SALUT_AVAHI_SERVICE_RESOLVER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_AVAHI_SERVICE_RESOLVER, SalutAvahiServiceResolverClass))

SalutAvahiServiceResolver *
salut_avahi_service_resolver_new(const gchar *name, const gchar *type, 
                                 const gchar *domain, 
                                 AvahiProtocol address_protocol,
                                 SalutAvahiLookupFlags flags);
SalutAvahiServiceResolver *
salut_avahi_service_resolver_new_full(AvahiIfIndex interface, 
                                      AvahiProtocol protocol,
                                      const gchar *name, const gchar *type, 
                                      const gchar *domain, 
                                      AvahiProtocol address_protocol,
                                      SalutAvahiLookupFlags flags);

gboolean 
salut_avahi_service_resolver_attach(SalutAvahiServiceResolver *resolver,
                                   SalutAvahiClient *client, GError  **error);

gboolean
salut_avahi_service_resolver_get_address(SalutAvahiServiceResolver *resolver,
                                         AvahiAddress *address,
                                         uint16_t *port);
G_END_DECLS

#endif /* #ifndef __SALUT_AVAHI_SERVICE_RESOLVER_H__*/
