/*
 * salut-discovery-client.c - Source for SalutDiscoveryClient interface
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

#include "salut-discovery-client.h"

#include <glib.h>

gboolean
salut_discovery_client_start (SalutDiscoveryClient *self,
                              GError **error)
{
  gboolean (*virtual_method)(SalutDiscoveryClient *, GError **) =
    SALUT_DISCOVERY_CLIENT_GET_CLASS (self)->start;
  g_assert (virtual_method != NULL);
  return virtual_method (self, error);
}

SalutMucManager *
salut_discovery_client_create_muc_manager (SalutDiscoveryClient *self,
                                           SalutConnection *connection,
                                           SalutXmppConnectionManager *xcm)
{
  SalutMucManager * (*virtual_method)(SalutDiscoveryClient *,
    SalutConnection *, SalutXmppConnectionManager *) =
    SALUT_DISCOVERY_CLIENT_GET_CLASS (self)->create_muc_manager;
  g_assert (virtual_method != NULL);
  return virtual_method (self, connection, xcm);
}

static void
salut_discovery_client_base_init (gpointer klass)
{
  static gboolean initialized = FALSE;

  if (!initialized)
    {
      GParamSpec *param_spec;

      param_spec = g_param_spec_uint (
          "state",
          "Client state",
          "An enum (SalutDiscoveryClientState) signifying the current state of"
          " this client object",
          0, NUM_SALUT_DISCOVERY_CLIENT_STATE - 1,
          SALUT_DISCOVERY_CLIENT_STATE_DISCONNECTED,
          G_PARAM_READABLE |
          G_PARAM_STATIC_NAME |
          G_PARAM_STATIC_NICK |
          G_PARAM_STATIC_BLURB);
      g_object_interface_install_property (klass, param_spec);

      initialized = TRUE;
    }
}

GType
salut_discovery_client_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (SalutDiscoveryClientClass),
      salut_discovery_client_base_init,   /* base_init */
      NULL,   /* base_finalize */
      NULL,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      0,
      0,      /* n_preallocs */
      NULL    /* instance_init */
    };

    type = g_type_register_static (G_TYPE_INTERFACE, "SalutDiscoveryClient",
        &info, 0);
  }

  return type;
}
