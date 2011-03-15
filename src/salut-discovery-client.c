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

SalutRoomlistManager *
salut_discovery_client_create_roomlist_manager (SalutDiscoveryClient *self,
                                                SalutConnection *connection,
                                                SalutXmppConnectionManager *xcm)
{
  SalutRoomlistManager * (*virtual_method)(SalutDiscoveryClient *,
    SalutConnection *, SalutXmppConnectionManager *) =
    SALUT_DISCOVERY_CLIENT_GET_CLASS (self)->create_roomlist_manager;
  g_assert (virtual_method != NULL);
  return virtual_method (self, connection, xcm);
}

SalutContactManager *
salut_discovery_client_create_contact_manager (SalutDiscoveryClient *self,
                                               SalutConnection *connection)
{
  SalutContactManager * (*virtual_method)(SalutDiscoveryClient *,
    SalutConnection *) =
    SALUT_DISCOVERY_CLIENT_GET_CLASS (self)->create_contact_manager;
  g_assert (virtual_method != NULL);
  return virtual_method (self, connection);
}

#ifdef ENABLE_OLPC
SalutOlpcActivityManager *
salut_discovery_client_create_olpc_activity_manager (SalutDiscoveryClient *self,
                                                     SalutConnection *connection)
{
  SalutOlpcActivityManager * (*virtual_method)(SalutDiscoveryClient *,
    SalutConnection *) =
    SALUT_DISCOVERY_CLIENT_GET_CLASS (self)->create_olpc_activity_manager;
  g_assert (virtual_method != NULL);
  return virtual_method (self, connection);
}
#endif

SalutSelf *
salut_discovery_client_create_self (SalutDiscoveryClient *self,
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
  SalutSelf * (*virtual_method)(SalutDiscoveryClient *, SalutConnection *,
      const gchar *, const gchar *, const gchar *, const gchar *,
      const gchar *, const gchar *, const GArray *, const gchar *) =
    SALUT_DISCOVERY_CLIENT_GET_CLASS (self)->create_self;
  g_assert (virtual_method != NULL);
  return virtual_method (self, connection, nickname, first_name, last_name,
      jid, email, published_name, olpc_key, olpc_color);
}

const gchar *
salut_discovery_client_get_host_name_fqdn (
    SalutDiscoveryClient *self)
{
  const gchar * (*virtual_method)( SalutDiscoveryClient *) =
      SALUT_DISCOVERY_CLIENT_GET_CLASS (self)->get_host_name_fqdn;
  g_assert (virtual_method != NULL);
  return virtual_method (self);
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
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
      g_object_interface_install_property (klass, param_spec);

      /* Defined here so we can g_object_set this property on the
       * discovery client without needing to define it everywhere. Now
       * classes which implement this interface just need to override
       * the property to use it.. */
      param_spec = g_param_spec_string (
          "dnssd-name", "DNS-SD name",
          "The DNS-SD name of the protocol", "",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
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
