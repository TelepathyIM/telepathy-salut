/*
 * salut-contact-manager.h - Header for SalutImManager
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

#ifndef __SALUT_IM_MANAGER_H__
#define __SALUT_IM_MANAGER_H__

#include <glib-object.h>
#include "salut-contact-manager.h"
#include "salut-im-channel.h"

#include <gibber/gibber-linklocal-transport.h>

G_BEGIN_DECLS

typedef struct _SalutImManager SalutImManager;
typedef struct _SalutImManagerClass SalutImManagerClass;

struct _SalutImManagerClass {
    GObjectClass parent_class;
};

struct _SalutImManager {
    GObject parent;
};


GType salut_im_manager_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_IM_MANAGER \
  (salut_im_manager_get_type ())
#define SALUT_IM_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_IM_MANAGER, SalutImManager))
#define SALUT_IM_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_IM_MANAGER, \
                           SalutImManagerClass))
#define SALUT_IS_IM_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_IM_MANAGER))
#define SALUT_IS_IM_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_IM_MANAGER))
#define SALUT_IM_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_IM_MANAGER, \
                              SalutImManagerClass))

SalutImManager *
salut_im_manager_new (SalutConnection *connection,
    SalutContactManager *contact_manager);

#endif /* #ifndef __SALUT_IM_MANAGER_H__*/
