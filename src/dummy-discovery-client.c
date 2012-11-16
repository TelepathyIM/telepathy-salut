/*
 * dummy-discovery-client.c - Source for SalutDummyDiscoveryClient
 * Copyright (C) 2007 Collabora Ltd.
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

#include "dummy-discovery-client.h"

#define DEBUG_FLAG DEBUG_DISCOVERY
#include "debug.h"

static void
discovery_client_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (SalutDummyDiscoveryClient,
    salut_dummy_discovery_client,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (SALUT_TYPE_DISCOVERY_CLIENT,
      discovery_client_init));

/* signals */
enum
{
  STATE_CHANGED,
  LAST_SIGNAL
};

/* properties */
enum
{
  PROP_STATE = 1,
  LAST_PROPERTY
};

typedef struct _SalutDummyDiscoveryClientPrivate \
          SalutDummyDiscoveryClientPrivate;
struct _SalutDummyDiscoveryClientPrivate
{
  SalutDiscoveryClientState state;

  gboolean dispose_has_run;
};

#define SALUT_DUMMY_DISCOVERY_CLIENT_GET_PRIVATE(obj) \
    ((SalutDummyDiscoveryClientPrivate *) \
      ((SalutDummyDiscoveryClient *) obj)->priv)

static void
salut_dummy_discovery_client_init (SalutDummyDiscoveryClient *self)
{
  SalutDummyDiscoveryClientPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_DUMMY_DISCOVERY_CLIENT, SalutDummyDiscoveryClientPrivate);

  self->priv = priv;
  priv->dispose_has_run = FALSE;

  priv->state = SALUT_DISCOVERY_CLIENT_STATE_DISCONNECTED;
}

static void
salut_dummy_discovery_client_dispose (GObject *object)
{
  SalutDummyDiscoveryClient *self = SALUT_DUMMY_DISCOVERY_CLIENT (object);
  SalutDummyDiscoveryClientPrivate *priv =
    SALUT_DUMMY_DISCOVERY_CLIENT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  G_OBJECT_CLASS (salut_dummy_discovery_client_parent_class)->dispose (object);
}

static void
salut_dummy_discovery_client_class_init (
    SalutDummyDiscoveryClientClass *salut_dummy_discovery_client_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (
      salut_dummy_discovery_client_class);

  g_type_class_add_private (salut_dummy_discovery_client_class,
      sizeof (SalutDummyDiscoveryClientPrivate));

  object_class->dispose = salut_dummy_discovery_client_dispose;
}

/*
 * salut_dummy_discovery_client_create_self
 *
 * Implements salut_discovery_client_create_self on SalutDiscoveryClient
 */
static SalutSelf *
salut_dummy_discovery_client_create_self (SalutDiscoveryClient *client,
                                          SalutConnection *connection,
                                          const gchar *nickname,
                                          const gchar *first_name,
                                          const gchar *last_name,
                                          const gchar *jid,
                                          const gchar *email,
                                          const gchar *published_name,
                                          const GArray *olpc_key,
                                          const gchar *olpc_color)
{
  return NULL;
}

static void
discovery_client_init (gpointer g_iface,
                       gpointer iface_data)
{
  SalutDiscoveryClientClass *klass = (SalutDiscoveryClientClass *) g_iface;

  klass->start = NULL;
  klass->create_muc_manager = NULL;
  klass->create_contact_manager = NULL;
#ifdef ENABLE_OLPC
  klass->create_olpc_activity_manager = NULL;
#endif
  klass->create_self = salut_dummy_discovery_client_create_self;
}
