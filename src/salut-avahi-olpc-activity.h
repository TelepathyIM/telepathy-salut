/*
 * salut-avahi-olpc-activity.h - Header for SalutAvahiOlpcActivity
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

#ifndef __SALUT_AVAHI_OLPC_ACTIVITY_H__
#define __SALUT_AVAHI_OLPC_ACTIVITY_H__

#include <glib-object.h>

#include "salut-olpc-activity.h"
#include "salut-avahi-discovery-client.h"

G_BEGIN_DECLS

typedef struct _SalutAvahiOlpcActivity SalutAvahiOlpcActivity;
typedef struct _SalutAvahiOlpcActivityClass SalutAvahiOlpcActivityClass;

struct _SalutAvahiOlpcActivityClass {
    SalutOlpcActivityClass parent_class;
};

struct _SalutAvahiOlpcActivity {
    SalutOlpcActivity parent;
};

GType salut_avahi_olpc_activity_get_type(void);

/* TYPE MACROS */
#define SALUT_TYPE_AVAHI_OLPC_ACTIVITY \
  (salut_avahi_olpc_activity_get_type())
#define SALUT_AVAHI_OLPC_ACTIVITY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_AVAHI_OLPC_ACTIVITY, SalutAvahiOlpcActivity))
#define SALUT_AVAHI_OLPC_ACTIVITY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_AVAHI_OLPC_ACTIVITY, SalutAvahiOlpcActivityClass))
#define SALUT_IS_AVAHI_OLPC_ACTIVITY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_AVAHI_OLPC_ACTIVITY))
#define SALUT_IS_AVAHI_OLPC_ACTIVITY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_AVAHI_OLPC_ACTIVITY))
#define SALUT_AVAHI_OLPC_ACTIVITY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_AVAHI_OLPC_ACTIVITY, SalutAvahiOlpcActivityClass))

SalutAvahiOlpcActivity * salut_avahi_olpc_activity_new (
    SalutConnection *connection, SalutAvahiDiscoveryClient *discovery_client);

void salut_avahi_olpc_activity_add_service (SalutAvahiOlpcActivity *activity,
    AvahiIfIndex interface, AvahiProtocol protocol,
    const char *name, const char *type, const char *domain);

void salut_avahi_olpc_activity_remove_service (SalutAvahiOlpcActivity *activity,
    AvahiIfIndex interface, AvahiProtocol protocol,
    const char *name, const char *type, const char *domain);

G_END_DECLS

#endif /* #ifndef __SALUT_AVAHI_OLPC_ACTIVITY_H__*/
