/*
 * avahi-muc-manager.h - Header for SalutAvahiMucManager
 * Copyright (C) 2006 Collabora Ltd.
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

#ifndef __SALUT_AVAHI_MUC_MANAGER_H__
#define __SALUT_AVAHI_MUC_MANAGER_H__

#include <glib-object.h>

#include "muc-manager.h"
#include "connection.h"
#include "avahi-discovery-client.h"

G_BEGIN_DECLS

typedef struct _SalutAvahiMucManager SalutAvahiMucManager;
typedef struct _SalutAvahiMucManagerClass SalutAvahiMucManagerClass;

struct _SalutAvahiMucManagerClass {
    SalutMucManagerClass parent_class;
};

struct _SalutAvahiMucManager {
    SalutMucManager parent;

    gpointer priv;
};

GType salut_avahi_muc_manager_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_AVAHI_MUC_MANAGER \
  (salut_avahi_muc_manager_get_type ())
#define SALUT_AVAHI_MUC_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_AVAHI_MUC_MANAGER, \
   SalutAvahiMucManager))
#define SALUT_AVAHI_MUC_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_AVAHI_MUC_MANAGER,  \
   SalutAvahiMucManagerClass))
#define SALUT_IS_AVAHI_MUC_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_AVAHI_MUC_MANAGER))
#define SALUT_IS_AVAHI_MUC_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_AVAHI_MUC_MANAGER))
#define SALUT_AVAHI_MUC_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_AVAHI_MUC_MANAGER, \
   SalutAvahiMucManagerClass))

SalutAvahiMucManager *
salut_avahi_muc_manager_new (SalutConnection *connection,
    SalutAvahiDiscoveryClient *discovery_client);

G_END_DECLS

#endif /* #ifndef __SALUT_AVAHI_MUC_MANAGER_H__*/
