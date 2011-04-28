/*
 * contact-manager.h - Header for SalutContactManager
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

#ifndef __SALUT_CONTACT_MANAGER_H__
#define __SALUT_CONTACT_MANAGER_H__

#include <glib-object.h>

#include "connection.h"
#include "contact.h"

G_BEGIN_DECLS

typedef struct _SalutContactManager SalutContactManager;
typedef struct _SalutContactManagerClass SalutContactManagerClass;

struct _SalutContactManagerClass {
    GObjectClass parent_class;

    /* public abstract methods */
    gboolean (*start) (SalutContactManager *self, GError **error);

    /* private abstract methods */
    SalutContact * (*create_contact) (SalutContactManager *self,
        const gchar *name);
    void (*dispose_contact) (SalutContactManager *self,
        SalutContact *contact);
    void (*close_all) (SalutContactManager *self);
};

struct _SalutContactManager {
    GObject parent;

    /* private */
    SalutConnection *connection;
    GHashTable *contacts;
};


GType salut_contact_manager_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_CONTACT_MANAGER \
  (salut_contact_manager_get_type ())
#define SALUT_CONTACT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_CONTACT_MANAGER, SalutContactManager))
#define SALUT_CONTACT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_CONTACT_MANAGER, SalutContactManagerClass))
#define SALUT_IS_CONTACT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_CONTACT_MANAGER))
#define SALUT_IS_CONTACT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_CONTACT_MANAGER))
#define SALUT_CONTACT_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_CONTACT_MANAGER, SalutContactManagerClass))

gboolean salut_contact_manager_start (SalutContactManager *mgr, GError **error);

SalutContact *
salut_contact_manager_get_contact (SalutContactManager *mgr, TpHandle handle);

GList *
salut_contact_manager_find_contacts_by_address (SalutContactManager *mgr,
    struct sockaddr *address, guint size);

SalutContact * salut_contact_manager_ensure_contact (SalutContactManager *mgr,
    const gchar *name);

/* restricted methods */
void salut_contact_manager_contact_created (SalutContactManager *self,
    SalutContact *contact);

#endif /* #ifndef __SALUT_CONTACT_MANAGER_H__*/
