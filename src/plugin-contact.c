/*
 * plugin-contact.c — API for telepathy-salut plugins
 * Copyright © 2012 Collabora Ltd.
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

#include "salut/plugin-contact.h"

#include <glib-object.h>
#include <telepathy-glib/errors.h>
#include <debug.h>

/**
 * SECTION: salut-plugin-contact
 * @title: SalutPluginContact
 * @short_description: Object representing salut contact, implemented by
 * Salut internals.
 *
 * This Object represents Salut Contact.
 *
 * Virtual methods in SalutPluginContactInterface interface are implemented
 * by SalutContact object. And only Salut should implement this interface.
 */
G_DEFINE_INTERFACE (SalutPluginContact,
    salut_plugin_contact,
    G_TYPE_OBJECT);

static void
salut_plugin_contact_default_init (SalutPluginContactInterface *iface)
{
}

GabbleCapabilitySet *
salut_plugin_contact_get_caps (
    SalutPluginContact *plugin_contact)
{
  SalutPluginContactInterface *iface =
    SALUT_PLUGIN_CONTACT_GET_IFACE (plugin_contact);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_caps != NULL, NULL);

  return iface->get_caps (plugin_contact);
}
