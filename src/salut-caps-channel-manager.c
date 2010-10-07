/*
 * caps-channel-manager.c - interface holding capabilities functions for
 * channel managers
 *
 * Copyright (C) 2008 Collabora Ltd.
 * Copyright (C) 2008 Nokia Corporation
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
#include "salut-caps-channel-manager.h"

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/channel-manager.h>


GType
salut_caps_channel_manager_get_type (void)
{
  static GType type = 0;

  if (G_UNLIKELY (type == 0))
    {
      static const GTypeInfo info = {
        sizeof (SalutCapsChannelManagerIface),
        NULL,   /* base_init */
        NULL,   /* base_finalize */
        NULL,   /* class_init */
        NULL,   /* class_finalize */
        NULL,   /* class_data */
        0,
        0,      /* n_preallocs */
        NULL    /* instance_init */
      };

      type = g_type_register_static (G_TYPE_INTERFACE,
          "SalutCapsChannelManager", &info, 0);

      g_type_interface_add_prerequisite (type, TP_TYPE_CHANNEL_MANAGER);
    }

  return type;
}

/* Virtual-method wrappers */

void salut_caps_channel_manager_get_feature_list (
    SalutCapsChannelManager *caps_manager,
    gpointer specific_caps,
    GSList **features)
{
  SalutCapsChannelManagerIface *iface =
    SALUT_CAPS_CHANNEL_MANAGER_GET_INTERFACE (caps_manager);
  SalutCapsChannelManagerGetFeatureListFunc method = iface->get_feature_list;

  if (method != NULL)
    {
      method (caps_manager, specific_caps, features);
    }
  /* ... else nothing to do */
}

gpointer salut_caps_channel_manager_parse_capabilities (
    SalutCapsChannelManager *caps_manager,
    GibberXmppNode *child)
{
  SalutCapsChannelManagerIface *iface =
    SALUT_CAPS_CHANNEL_MANAGER_GET_INTERFACE (caps_manager);
  SalutCapsChannelManagerParseCapsFunc method = iface->parse_caps;

  if (method != NULL)
    {
      return method (caps_manager, child);
    }
  /* ... else assume there is not caps for this kind of channels */
  return NULL;
}

void salut_caps_channel_manager_free_capabilities (
    SalutCapsChannelManager *caps_manager,
    gpointer specific_caps)
{
  SalutCapsChannelManagerIface *iface =
    SALUT_CAPS_CHANNEL_MANAGER_GET_INTERFACE (caps_manager);
  SalutCapsChannelManagerFreeCapsFunc method = iface->free_caps;

  if (method != NULL)
    {
      method (caps_manager, specific_caps);
    }
  /* ... else assume there is no need to free */
}

void salut_caps_channel_manager_copy_capabilities (
    SalutCapsChannelManager *caps_manager,
    gpointer *specific_caps_out,
    gpointer specific_caps_in)
{
  SalutCapsChannelManagerIface *iface =
    SALUT_CAPS_CHANNEL_MANAGER_GET_INTERFACE (caps_manager);
  SalutCapsChannelManagerCopyCapsFunc method = iface->copy_caps;

  if (method != NULL)
    {
      method (caps_manager, specific_caps_out, specific_caps_in);
    }
  else
    *specific_caps_out = NULL;
}

void salut_caps_channel_manager_update_capabilities (
    SalutCapsChannelManager *caps_manager,
    gpointer specific_caps_out,
    gpointer specific_caps_in)
{
  SalutCapsChannelManagerIface *iface =
    SALUT_CAPS_CHANNEL_MANAGER_GET_INTERFACE (caps_manager);
  SalutCapsChannelManagerCopyCapsFunc method = iface->update_caps;

  /* cannot be called if not implemented */
  g_assert (method != NULL);
  method (caps_manager, specific_caps_out, specific_caps_in);
}

gboolean salut_caps_channel_manager_capabilities_diff (
    SalutCapsChannelManager *caps_manager,
    TpHandle handle,
    gpointer specific_old_caps,
    gpointer specific_new_caps)
{
  SalutCapsChannelManagerIface *iface =
    SALUT_CAPS_CHANNEL_MANAGER_GET_INTERFACE (caps_manager);
  SalutCapsChannelManagerCapsDiffFunc method = iface->caps_diff;

  if (method != NULL)
    {
      return method (caps_manager, handle, specific_old_caps,
          specific_new_caps);
    }
  /* ... else, nothing to do */
  return FALSE;
}

void
salut_caps_channel_manager_add_capability (
    SalutCapsChannelManager *caps_manager,
    SalutConnection *conn,
    GHashTable *cap,
    GHashTable *per_channel_manager_caps)
{
  SalutCapsChannelManagerIface *iface =
    SALUT_CAPS_CHANNEL_MANAGER_GET_INTERFACE (caps_manager);
  SalutCapsChannelManagerAddCapFunc method = iface->add_cap;

  if (method != NULL)
    {
      method (caps_manager, conn, cap, per_channel_manager_caps);
    }
  /* ... else, nothing to do */
}

