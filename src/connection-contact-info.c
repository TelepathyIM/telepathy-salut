/*
 * connection-contact-info.c - ContactInfo implementation
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

#include "connection-contact-info.h"

#include <telepathy-glib/interfaces.h>

#include "contact-manager.h"
#include "contact.h"

enum {
    PROP_CONTACT_INFO_FLAGS,
    PROP_SUPPORTED_FIELDS
};

static void
salut_conn_contact_info_get_property (
    GObject *object,
    GQuark iface,
    GQuark name,
    GValue *value,
    gpointer getter_data)
{
  switch (GPOINTER_TO_UINT (getter_data))
    {
    case PROP_CONTACT_INFO_FLAGS:
      g_value_set_uint (value, TP_CONTACT_INFO_FLAG_PUSH);
      break;
    case PROP_SUPPORTED_FIELDS:
      /* TODO: list supported fields */
      g_value_take_boxed (value, g_ptr_array_new ());
      break;
    default:
      g_assert_not_reached ();
    }
}

void
salut_conn_contact_info_class_init (
    SalutConnectionClass *klass)
{
  static TpDBusPropertiesMixinPropImpl props[] = {
      { "ContactInfoFlags", GUINT_TO_POINTER (PROP_CONTACT_INFO_FLAGS), NULL },
      { "SupportedFields", GUINT_TO_POINTER (PROP_SUPPORTED_FIELDS), NULL },
      { NULL }
  };

  tp_dbus_properties_mixin_implement_interface (
      G_OBJECT_CLASS (klass),
      TP_IFACE_QUARK_CONNECTION_INTERFACE_CONTACT_INFO,
      salut_conn_contact_info_get_property,
      NULL,
      props);
}

static void
salut_conn_contact_info_fill_contact_attributes (
    GObject *obj,
    const GArray *contacts,
    GHashTable *attributes_hash)
{
  guint i;
  SalutConnection *self = SALUT_CONNECTION (obj);
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  SalutContactManager *contact_manager;

  g_object_get (self, "contact-manager", &contact_manager, NULL);

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);

      if (base->self_handle == handle)
        {
          /* TODO */
        }
      else
        {
          SalutContact *contact = salut_contact_manager_get_contact (
              contact_manager, handle);
          if (contact != NULL)
            {
              /* TODO */
              g_object_unref (contact);
            }
        }
    }

  g_object_unref (contact_manager);
}

void salut_conn_contact_info_init (
    SalutConnection *self)
{
  tp_contacts_mixin_add_contact_attributes_iface (
      G_OBJECT (self),
      TP_IFACE_CONNECTION_INTERFACE_CONTACT_INFO,
      salut_conn_contact_info_fill_contact_attributes);
}

static void
salut_conn_contact_info_refresh_contact_info (
    TpSvcConnectionInterfaceContactInfo *iface,
    const GArray *contacts,
    DBusGMethodInvocation *context)
{
  /* This is a no-op on link-local XMPP: everything's always pushed to us. */
  tp_svc_connection_interface_contact_info_return_from_refresh_contact_info (context);
}

void
salut_conn_contact_info_iface_init (
    gpointer g_iface,
    gpointer iface_data)
{
  TpSvcConnectionInterfaceContactInfoClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_contact_info_implement_##x \
    (klass, salut_conn_contact_info_##x)
  IMPLEMENT (refresh_contact_info);
#undef IMPLEMENT
}
