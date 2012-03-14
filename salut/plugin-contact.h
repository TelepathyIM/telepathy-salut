/*
 * plugin-contact.h — Connection API available to telepathy-salut plugins
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

#ifndef SALUT_PLUGIN_CONTACT_H
#define SALUT_PLUGIN_CONTACT_H

#include <glib-object.h>

#include <wocky/wocky.h>
#include <salut/capability-set.h>

G_BEGIN_DECLS

typedef struct _SalutPluginContact SalutPluginContact;
typedef struct _SalutPluginContactInterface SalutPluginContactInterface;

#define SALUT_TYPE_PLUGIN_CONTACT (salut_plugin_contact_get_type ())
#define SALUT_PLUGIN_CONTACT(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), SALUT_TYPE_PLUGIN_CONTACT, \
                               SalutPluginContact))
#define SALUT_IS_PLUGIN_CONTACT(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), SALUT_TYPE_PLUGIN_CONTACT))
#define SALUT_PLUGIN_CONTACT_GET_IFACE(o) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((o), SALUT_TYPE_PLUGIN_CONTACT, \
                                  SalutPluginContactInterface))

GType salut_plugin_contact_get_type (void) G_GNUC_CONST;

typedef GabbleCapabilitySet * (*SalutPluginContactGetCapsFunc) (
    SalutPluginContact *plugin_contact);

struct _SalutPluginContactInterface
{
  GTypeInterface parent;
  SalutPluginContactGetCapsFunc get_caps;
};

GabbleCapabilitySet *salut_plugin_contact_get_caps (
    SalutPluginContact *plugin_contact);

G_END_DECLS

#endif
