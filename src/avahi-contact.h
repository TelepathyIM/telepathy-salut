/*
 * avahi-contact.h - Header for SalutAvahiContact
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

#ifndef __SALUT_AVAHI_CONTACT_H__
#define __SALUT_AVAHI_CONTACT_H__

#include <glib-object.h>

#include "contact.h"
#include "avahi-discovery-client.h"

G_BEGIN_DECLS

typedef struct _SalutAvahiContact SalutAvahiContact;
typedef struct _SalutAvahiContactClass SalutAvahiContactClass;

struct _SalutAvahiContactClass {
    SalutContactClass parent_class;
};

struct _SalutAvahiContact {
    SalutContact parent;

    gpointer priv;
};

GType salut_avahi_contact_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_AVAHI_CONTACT \
  (salut_avahi_contact_get_type ())
#define SALUT_AVAHI_CONTACT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_AVAHI_CONTACT, SalutAvahiContact))
#define SALUT_AVAHI_CONTACT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_AVAHI_CONTACT, SalutAvahiContactClass))
#define SALUT_IS_AVAHI_CONTACT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_AVAHI_CONTACT))
#define SALUT_IS_AVAHI_CONTACT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_AVAHI_CONTACT))
#define SALUT_AVAHI_CONTACT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_AVAHI_CONTACT, SalutAvahiContactClass))

SalutAvahiContact *
salut_avahi_contact_new (SalutConnection *connection, const gchar *name,
    SalutAvahiDiscoveryClient *discovery_client);

gboolean salut_avahi_contact_add_service (SalutAvahiContact *contact,
    AvahiIfIndex interface, AvahiProtocol protocol, const char *name,
    const char *type, const char *domain);

void salut_avahi_contact_remove_service (SalutAvahiContact *contact,
    AvahiIfIndex interface, AvahiProtocol protocol, const char *name,
    const char *type, const char *domain);

gboolean salut_avahi_contact_has_services (SalutAvahiContact *contact);

G_END_DECLS

#endif /* #ifndef __SALUT_AVAHI_CONTACT_H__*/
