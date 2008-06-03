/*
 * salut-avahi-contact-manager.h - Header for SalutAvahiContactManager
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

#include "config.h"

#ifndef __SALUT_AVAHI_CONTACT_MANAGER_H__
#define __SALUT_AVAHI_CONTACT_MANAGER_H__

#include <glib-object.h>
#include <avahi-gobject/ga-client.h>

#include "salut-contact-manager.h"
#include "salut-connection.h"
#include "salut-contact.h"
#include "salut-avahi-discovery-client.h"

G_BEGIN_DECLS

typedef struct _SalutAvahiContactManager SalutAvahiContactManager;
typedef struct _SalutAvahiContactManagerClass SalutAvahiContactManagerClass;

struct _SalutAvahiContactManagerClass {
    SalutContactManagerClass parent_class;
};

struct _SalutAvahiContactManager {
    SalutContactManager parent;

    gpointer priv;
};


GType salut_avahi_contact_manager_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_AVAHI_CONTACT_MANAGER \
  (salut_avahi_contact_manager_get_type ())
#define SALUT_AVAHI_CONTACT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_AVAHI_CONTACT_MANAGER, SalutAvahiContactManager))
#define SALUT_AVAHI_CONTACT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_AVAHI_CONTACT_MANAGER, SalutAvahiContactManagerClass))
#define SALUT_IS_AVAHI_CONTACT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_AVAHI_CONTACT_MANAGER))
#define SALUT_IS_AVAHI_CONTACT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_AVAHI_CONTACT_MANAGER))
#define SALUT_AVAHI_CONTACT_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_AVAHI_CONTACT_MANAGER, SalutAvahiContactManagerClass))

SalutAvahiContactManager * salut_avahi_contact_manager_new (
    SalutConnection *connection, SalutAvahiDiscoveryClient *discovery_client);

#endif /* #ifndef __SALUT_AVAHI_CONTACT_MANAGER_H__*/
