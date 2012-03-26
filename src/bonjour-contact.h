/*
 * bonjour-contact.h - Header for SalutBonjourContact
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

#ifndef __SALUT_BONJOUR_CONTACT_H__
#define __SALUT_BONJOUR_CONTACT_H__

#include <glib-object.h>

#include "contact.h"
#include "bonjour-discovery-client.h"

G_BEGIN_DECLS

typedef struct _SalutBonjourContact SalutBonjourContact;
typedef struct _SalutBonjourContactClass SalutBonjourContactClass;

struct _SalutBonjourContactClass {
    SalutContactClass parent_class;
};

struct _SalutBonjourContact {
    SalutContact parent;

    gpointer priv;
};

GType salut_bonjour_contact_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_BONJOUR_CONTACT \
  (salut_bonjour_contact_get_type ())
#define SALUT_BONJOUR_CONTACT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_BONJOUR_CONTACT, SalutBonjourContact))
#define SALUT_BONJOUR_CONTACT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_BONJOUR_CONTACT, SalutBonjourContactClass))
#define SALUT_IS_BONJOUR_CONTACT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_BONJOUR_CONTACT))
#define SALUT_IS_BONJOUR_CONTACT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_BONJOUR_CONTACT))
#define SALUT_BONJOUR_CONTACT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_BONJOUR_CONTACT, SalutBonjourContactClass))

SalutBonjourContact *
salut_bonjour_contact_new (SalutConnection *connection, const gchar *name,
    SalutBonjourDiscoveryClient *discovery_client);

gboolean salut_bonjour_contact_add_service (SalutBonjourContact *contact,
    uint32_t interface, const char *name,
    const char *type, const char *domain);

void salut_bonjour_contact_remove_service (SalutBonjourContact *contact,
    uint32_t interface, const char *name,
    const char *type, const char *domain);

gboolean salut_bonjour_contact_has_services (SalutBonjourContact *contact);

G_END_DECLS

#endif /* #ifndef __SALUT_BONJOUR_CONTACT_H__*/
