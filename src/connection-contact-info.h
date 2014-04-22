/*
 * connection-contact-info.h - header for ContactInfo implementation
 * Copyright © 2011 Collabora Ltd.
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
#ifndef SALUT_CONNECTION_CONTACT_INFO_H
#define SALUT_CONNECTION_CONTACT_INFO_H

#include "connection.h"
#include "contact.h"

void salut_conn_contact_info_iface_init (
    gpointer g_iface,
    gpointer iface_data);
void salut_conn_contact_info_class_init (
    SalutConnectionClass *klass);

void salut_conn_contact_info_changed (
    SalutConnection *self,
    SalutContact *contact,
    TpHandle handle);

gboolean salut_conn_contact_info_fill_contact_attributes (
    SalutConnection *self,
    const gchar *dbus_interface,
    TpHandle handle,
    GVariantDict *attributes);

#endif // SALUT_CONNECTION_CONTACT_INFO_H
