/*
 * salut-discovery-client.h - Header for SalutDiscoveryClient interface
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

#ifndef __SALUT_DISCOVERY_CLIENT_H__
#define __SALUT_DISCOVERY_CLIENT_H__

#include <glib-object.h>

#include "salut-muc-manager.h"
#include "salut-contact-manager.h"

G_BEGIN_DECLS

typedef enum
{
  SALUT_DISCOVERY_CLIENT_STATE_DISCONNECTED,
  SALUT_DISCOVERY_CLIENT_STATE_CONNECTING,
  SALUT_DISCOVERY_CLIENT_STATE_DISCONNECTING,
  SALUT_DISCOVERY_CLIENT_STATE_CONNECTED,
  NUM_SALUT_DISCOVERY_CLIENT_STATE
} SalutDiscoveryClientState;

typedef struct _SalutDiscoveryClient SalutDiscoveryClient;
typedef struct _SalutDiscoveryClientClass SalutDiscoveryClientClass;

struct _SalutDiscoveryClientClass
{
  GTypeInterface parent;

  gboolean (*start) (SalutDiscoveryClient *clt, GError **error);
  SalutMucManager * (*create_muc_manager) (SalutDiscoveryClient *clt,
      SalutConnection *connection, SalutXmppConnectionManager *xcm);
  SalutContactManager * (*create_contact_manager) (SalutDiscoveryClient *clt,
      SalutConnection *connection);
};

GType salut_discovery_client_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_DISCOVERY_CLIENT \
  (salut_discovery_client_get_type ())
#define SALUT_DISCOVERY_CLIENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_DISCOVERY_CLIENT, \
                              SalutDiscoveryClient))
#define SALUT_IS_DISCOVERY_CLIENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_DISCOVERY_CLIENT))
#define SALUT_DISCOVERY_CLIENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), SALUT_TYPE_DISCOVERY_CLIENT,\
                              SalutDiscoveryClientClass))

gboolean salut_discovery_client_start (SalutDiscoveryClient *clt,
    GError **error);

SalutMucManager * salut_discovery_client_create_muc_manager (
    SalutDiscoveryClient *clt, SalutConnection *connection,
    SalutXmppConnectionManager *xcm);

SalutContactManager * salut_discovery_client_create_contact_manager (
    SalutDiscoveryClient *clt, SalutConnection *connection);

G_END_DECLS

#endif /* #ifndef __SALUT_DISCOVERY_CLIENT_H__ */
