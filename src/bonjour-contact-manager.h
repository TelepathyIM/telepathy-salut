/*
 * bonjour-contact-manager.h - Header for SalutBonjourContactManager
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

#ifndef __SALUT_BONJOUR_CONTACT_MANAGER_H__
#define __SALUT_BONJOUR_CONTACT_MANAGER_H__

#include <glib-object.h>

#include "contact-manager.h"
#include "connection.h"
#include "contact.h"
#include "bonjour-discovery-client.h"

G_BEGIN_DECLS

typedef struct _SalutBonjourContactManager SalutBonjourContactManager;
typedef struct _SalutBonjourContactManagerClass SalutBonjourContactManagerClass;

struct _SalutBonjourContactManagerClass {
    SalutContactManagerClass parent_class;
};

struct _SalutBonjourContactManager {
    SalutContactManager parent;

    gpointer priv;
};


GType salut_bonjour_contact_manager_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_BONJOUR_CONTACT_MANAGER \
  (salut_bonjour_contact_manager_get_type ())
#define SALUT_BONJOUR_CONTACT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_BONJOUR_CONTACT_MANAGER, SalutBonjourContactManager))
#define SALUT_BONJOUR_CONTACT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_BONJOUR_CONTACT_MANAGER, SalutBonjourContactManagerClass))
#define SALUT_IS_BONJOUR_CONTACT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_BONJOUR_CONTACT_MANAGER))
#define SALUT_IS_BONJOUR_CONTACT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_BONJOUR_CONTACT_MANAGER))
#define SALUT_BONJOUR_CONTACT_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_BONJOUR_CONTACT_MANAGER, SalutBonjourContactManagerClass))

SalutBonjourContactManager * salut_bonjour_contact_manager_new (
    SalutConnection *connection, SalutBonjourDiscoveryClient *discovery_client);

#endif /* #ifndef __SALUT_BONJOUR_CONTACT_MANAGER_H__*/
