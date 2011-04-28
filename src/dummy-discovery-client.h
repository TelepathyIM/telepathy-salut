/*
 * dummy-discovery-client.h - Header for SalutDummyDiscoveryClient
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

#ifndef __SALUT_DUMMY_DISCOVERY_CLIENT_H__
#define __SALUT_DUMMY_DISCOVERY_CLIENT_H__

#include <glib-object.h>

#include "discovery-client.h"

G_BEGIN_DECLS

typedef struct _SalutDummyDiscoveryClient SalutDummyDiscoveryClient;
typedef struct _SalutDummyDiscoveryClientClass SalutDummyDiscoveryClientClass;

struct _SalutDummyDiscoveryClientClass {
  GObjectClass parent_class;
};

struct _SalutDummyDiscoveryClient {
  GObject parent;

  gpointer priv;
};

GType salut_dummy_discovery_client_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_DUMMY_DISCOVERY_CLIENT \
  (salut_dummy_discovery_client_get_type ())
#define SALUT_DUMMY_DISCOVERY_CLIENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_DUMMY_DISCOVERY_CLIENT,\
                              SalutDummyDiscoveryClient))
#define SALUT_DUMMY_DISCOVERY_CLIENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_DUMMY_DISCOVERY_CLIENT,\
                           SalutDummyDiscoveryClientClass))
#define SALUT_IS_DUMMY_DISCOVERY_CLIENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_DUMMY_DISCOVERY_CLIENT))
#define SALUT_IS_DUMMY_DISCOVERY_CLIENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_DUMMY_DISCOVERY_CLIENT))
#define SALUT_DUMMY_DISCOVERY_CLIENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_DUMMY_DISCOVERY_CLIENT,\
                              SalutDummyDiscoveryClientClass))

G_END_DECLS

#endif /* #ifndef __SALUT_DUMMY_DISCOVERY_CLIENT_H__ */
