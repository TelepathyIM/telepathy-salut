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

#include "config.h"
#include "connection-contact-info.h"

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "contact-manager.h"

enum {
    PROP_CONTACT_INFO_FLAGS,
    PROP_SUPPORTED_FIELDS
};

static gchar *i_heart_the_internet[] = { "type=internet", NULL };

static GPtrArray *
get_supported_fields (void)
{
  static TpContactInfoFieldSpec supported_fields[] = {
      /* We omit 'nickname' because it shows up, unmodifiably, as the alias. */
      { "n", NULL,
        TP_CONTACT_INFO_FIELD_FLAG_PARAMETERS_EXACT, 1 },
      /* It's a little bit sketchy to expose 1st + ' ' + last as FN. But such
       * are the limitations of the protocol.
       */
      { "fn", NULL,
        TP_CONTACT_INFO_FIELD_FLAG_PARAMETERS_EXACT, 1 },
      { "email", i_heart_the_internet,
        TP_CONTACT_INFO_FIELD_FLAG_PARAMETERS_EXACT, 1 },
      /* x-jabber is used for compatibility with Gabble */
      { "x-jabber", NULL,
        TP_CONTACT_INFO_FIELD_FLAG_PARAMETERS_EXACT, 1 },
      /* Heh, we could also include the contact's IP address(es) here. */
      { NULL }
  };
  static gsize supported_fields_ptr_array = 0;

  if (g_once_init_enter (&supported_fields_ptr_array))
    {
      GPtrArray *fields = dbus_g_type_specialized_construct (
          TP_ARRAY_TYPE_FIELD_SPECS);
      TpContactInfoFieldSpec *spec;

      for (spec = supported_fields; spec->name != NULL; spec++)
        g_ptr_array_add (fields,
            tp_value_array_build (4,
                G_TYPE_STRING, spec->name,
                G_TYPE_STRV, spec->parameters,
                G_TYPE_UINT, spec->flags,
                G_TYPE_UINT, spec->max,
                G_TYPE_INVALID));

      g_once_init_leave (&supported_fields_ptr_array, (gsize) fields);
    }

  return (GPtrArray *) supported_fields_ptr_array;
}

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
      g_value_set_boxed (value, get_supported_fields ());
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
add_singleton_field (
    GPtrArray *contact_info,
    const gchar *field_name,
    gchar **parameters,
    const gchar *value)
{
  const gchar *field_value[] = { value, NULL };

  g_ptr_array_add (contact_info,
      tp_value_array_build (3,
          G_TYPE_STRING, field_name,
          G_TYPE_STRV, parameters,
          G_TYPE_STRV, field_value,
          G_TYPE_INVALID));
}

static GPtrArray *
build_contact_info (
    const gchar *first,
    const gchar *last,
    const gchar *full_name,
    const gchar *email,
    const gchar *jid)
{
  GPtrArray *contact_info = dbus_g_type_specialized_construct (
      TP_ARRAY_TYPE_CONTACT_INFO_FIELD_LIST);

  if (first != NULL || last != NULL)
    {
      const gchar *field_value[] = {
          last != NULL ? last : "",
          first != NULL ? first : "",
          "",
          "",
          "",
          NULL
      };

      g_ptr_array_add (contact_info,
          tp_value_array_build (3,
              G_TYPE_STRING, "n",
              G_TYPE_STRV, NULL,
              G_TYPE_STRV, field_value,
              G_TYPE_INVALID));

      g_warn_if_fail (full_name != NULL);
      add_singleton_field (contact_info, "fn", NULL, full_name);
    }

  if (email != NULL)
    add_singleton_field (contact_info, "email", i_heart_the_internet, email);

  if (jid != NULL)
    add_singleton_field (contact_info, "x-jabber", NULL, jid);

  return contact_info;
}

static GPtrArray *
build_contact_info_for_contact (
    SalutContact *contact)
{
  g_return_val_if_fail (contact != NULL, NULL);

  return build_contact_info (contact->first, contact->last, contact->full_name,
      contact->email, contact->jid);
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
      GPtrArray *contact_info = NULL;

      if (tp_base_connection_get_self_handle (base) == handle)
        {
          /* TODO: dig contact info out of SalutSelf. There's overlap with
           * connection parameters here … should they be DBus_Property
           * parameters? Should we have a new flag which means “you set this on
           * ContactInfo”? What?
           */
        }
      else
        {
          SalutContact *contact = salut_contact_manager_get_contact (
              contact_manager, handle);
          if (contact != NULL)
            {
              contact_info = build_contact_info_for_contact (contact);
              g_object_unref (contact);
            }
        }

      if (contact_info != NULL)
        tp_contacts_mixin_set_contact_attribute (attributes_hash,
            handle, TP_TOKEN_CONNECTION_INTERFACE_CONTACT_INFO_INFO,
            tp_g_value_slice_new_take_boxed (
                TP_ARRAY_TYPE_CONTACT_INFO_FIELD_LIST, contact_info));
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

void
salut_conn_contact_info_changed (
    SalutConnection *self,
    SalutContact *contact,
    TpHandle handle)
{
  GPtrArray *contact_info = build_contact_info_for_contact (contact);

  tp_svc_connection_interface_contact_info_emit_contact_info_changed (self,
      handle, contact_info);
  g_boxed_free (TP_ARRAY_TYPE_CONTACT_INFO_FIELD_LIST, contact_info);
}

static void
salut_conn_contact_info_request_contact_info (
    TpSvcConnectionInterfaceContactInfo *iface,
    guint handle,
    DBusGMethodInvocation *context)
{
  SalutConnection *self = SALUT_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *contacts_repo =
      tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);
  GError *error = NULL;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (TP_BASE_CONNECTION (iface),
      context);

  if (!tp_handle_is_valid (contacts_repo, handle, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
  else
    {
      SalutContactManager *contact_manager;
      SalutContact *contact;

      g_object_get (self, "contact-manager", &contact_manager, NULL);
      contact = salut_contact_manager_get_contact (contact_manager, handle);
      g_object_unref (contact_manager);

      if (contact != NULL)
        {
          GPtrArray *contact_info = build_contact_info_for_contact (contact);

          tp_svc_connection_interface_contact_info_return_from_request_contact_info (
              context, contact_info);
          g_boxed_free (TP_ARRAY_TYPE_CONTACT_INFO_FIELD_LIST, contact_info);
        }
      else
        {
          error = g_error_new (TP_ERROR, TP_ERROR_NOT_AVAILABLE,
              "No information available for '%s'",
              tp_handle_inspect (contacts_repo, handle));
          dbus_g_method_return_error (context, error);
          g_error_free (error);
        }
    }
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
  IMPLEMENT (request_contact_info);
  IMPLEMENT (refresh_contact_info);
#undef IMPLEMENT
}
