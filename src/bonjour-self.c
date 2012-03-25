/*
 * bonjour-self.c - Source for SalutBonjourSelf
 * Copyright (C) 2012 Collabora Ltd.
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

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG_FLAG DEBUG_SELF
#include "debug.h"

#include "bonjour-self.h"

#include "sha1/sha1-util.h"

#ifdef ENABLE_OLPC
#define KEY_SEGMENT_SIZE 200
#endif

#define RETURN_FALSE_IF_FAIL(error_type) \
   if (error_type != kDNSServiceErr_NoError) return FALSE;

#define RETURN_ERROR_IF_FAIL(error_type, error) \
   if (error_type != kDNSServiceErr_NoError) \
   { \
      *error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE, \
          "bonjour-self failed with (%d)", error_type); \
      return FALSE; \
    }

G_DEFINE_TYPE (SalutBonjourSelf, salut_bonjour_self, SALUT_TYPE_SELF);

/* properties */
enum
{
  PROP_DISCOVERY_CLIENT = 1,
  LAST_PROPERTY
};

struct _SalutBonjourSelfPrivate
{
  SalutBonjourDiscoveryClient *discovery_client;
  DNSServiceRef bonjour_service;
  DNSRecordRef presence_record;
  DNSRecordRef avatar_record;
  TXTRecordRef txt_record_presence;
  gboolean dispose_has_run;
};

static void
salut_bonjour_self_get_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
  SalutBonjourSelf *self = SALUT_BONJOUR_SELF (object);
  SalutBonjourSelfPrivate *priv = self->priv;

  switch (property_id) {
    case PROP_DISCOVERY_CLIENT:
      g_value_set_object (value, priv->discovery_client);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
salut_bonjour_self_set_property (GObject *object,
                                 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
  SalutBonjourSelf *self = SALUT_BONJOUR_SELF (object);
  SalutBonjourSelfPrivate *priv = self->priv;

  switch (property_id) {
    case PROP_DISCOVERY_CLIENT:
      priv->discovery_client = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
salut_bonjour_self_init (SalutBonjourSelf *self)
{
  SalutBonjourSelfPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_BONJOUR_SELF, SalutBonjourSelfPrivate);

  self->priv = priv;

  priv->discovery_client = NULL;
}

static DNSServiceErrorType
_bonjour_publish_record (SalutBonjourSelf *self,
                         DNSRecordRef *dns_record,
                         guint16 rrtype,
                         guint16 size,
                         const void *data,
                         guint16 time_out,
                         gboolean mode)
{
  SalutBonjourSelfPrivate *priv = self->priv;
  DNSServiceErrorType error_type = kDNSServiceErr_NoError;

  if (mode)
    {
      error_type = DNSServiceUpdateRecord (priv->bonjour_service,
          *dns_record, 0, size, data, time_out);
    }
  else
    {
      error_type = DNSServiceAddRecord (priv->bonjour_service,
          dns_record, 0, rrtype, size, data, time_out);
    }

  if (error_type != kDNSServiceErr_NoError)
    {
      DEBUG ("Error Publishing Record : (%d)", error_type);
      return error_type;
    }

  return error_type;
}

static gboolean
salut_bonjour_self_set_caps (SalutSelf *_self,
                             GError **error)
{
  SalutBonjourSelf *self = SALUT_BONJOUR_SELF (_self);
  SalutBonjourSelfPrivate *priv = self->priv;
  DNSServiceErrorType error_type = kDNSServiceErr_NoError;

  /* Service is not announced yet */
  if (TXTRecordGetLength (&priv->txt_record_presence) <= 0)
    {
      return TRUE;
    }

  if (_self->node == NULL)
    {
      error_type = TXTRecordRemoveValue (&priv->txt_record_presence, "node");
      RETURN_ERROR_IF_FAIL (error_type, error);
    }
  else
    {
      error_type = TXTRecordSetValue (&priv->txt_record_presence, "node",
              strlen (_self->node), _self->node);
      RETURN_ERROR_IF_FAIL (error_type, error);
    }

  if (_self->hash == NULL)
    {
      error_type = TXTRecordRemoveValue (&priv->txt_record_presence, "hash");
      RETURN_ERROR_IF_FAIL (error_type, error);
    }
  else
    {
      error_type = TXTRecordSetValue (&priv->txt_record_presence, "hash",
              strlen (_self->hash), _self->hash);
      RETURN_ERROR_IF_FAIL (error_type, error);
    }
  if (_self->ver == NULL)
    {
      error_type = TXTRecordRemoveValue (&priv->txt_record_presence, "ver");
      RETURN_ERROR_IF_FAIL (error_type, error);
    }
  else
    {
      error_type = TXTRecordSetValue (&priv->txt_record_presence, "ver",
              strlen (_self->ver), _self->ver);
      RETURN_ERROR_IF_FAIL (error_type, error);
    }

  error_type = _bonjour_publish_record (self, &priv->presence_record,
      kDNSServiceType_TXT, TXTRecordGetLength (&priv->txt_record_presence),
      TXTRecordGetBytesPtr (&priv->txt_record_presence), 0, TRUE);

  RETURN_ERROR_IF_FAIL (error_type, error);
  return TRUE;
}

static DNSServiceErrorType
_bonjour_self_init_txt_record_presence (SalutBonjourSelf *self,
                                        TXTRecordRef *txt_record_presence,
                                        guint16 port)
{
#define RETURN_TXT_ERROR_IF_FAIL(error_type) \
   if (error_type != kDNSServiceErr_NoError) return error_type

  SalutSelf *_self = SALUT_SELF (self);
  gchar *_port;
  DNSServiceErrorType error_type;

  _port = g_strdup_printf ("%" G_GUINT16_FORMAT, port);

  error_type = TXTRecordSetValue (txt_record_presence, "port.p2pj",
          strlen (_port), _port);

  RETURN_TXT_ERROR_IF_FAIL (error_type);

  if (_self->nickname != NULL)
    {
      error_type = TXTRecordSetValue (txt_record_presence, "nick",
          strlen (_self->nickname), _self->nickname);
      RETURN_TXT_ERROR_IF_FAIL (error_type);
    }
  if (_self->first_name != NULL)
    {
      error_type = TXTRecordSetValue (txt_record_presence, "1st",
          strlen (_self->first_name), _self->first_name);
      RETURN_TXT_ERROR_IF_FAIL (error_type);
    }
  if (_self->last_name != NULL)
    {
      error_type = TXTRecordSetValue (txt_record_presence, "last",
          strlen (_self->last_name), _self->last_name);
      RETURN_TXT_ERROR_IF_FAIL (error_type);
    }
  if (_self->email != NULL)
    {
      error_type = TXTRecordSetValue (txt_record_presence, "email",
          strlen (_self->email), _self->email);
      RETURN_TXT_ERROR_IF_FAIL (error_type);
    }
  if (_self->jid != NULL)
    {
      error_type = TXTRecordSetValue (txt_record_presence, "jid",
          strlen (_self->jid), _self->jid);
      RETURN_TXT_ERROR_IF_FAIL (error_type);
    }

  error_type = TXTRecordSetValue (txt_record_presence, "status",
      strlen (salut_presence_status_txt_names[_self->status]),
      salut_presence_status_txt_names[_self->status]);

  RETURN_TXT_ERROR_IF_FAIL (error_type);

  if (_self->status_message != NULL)
    {
      error_type = TXTRecordSetValue (txt_record_presence, "msg",
          strlen (_self->status_message), _self->status_message);
      RETURN_TXT_ERROR_IF_FAIL (error_type);
    }

  return kDNSServiceErr_NoError;
}

static void DNSSD_API
_bonjour_service_register_cb (DNSServiceRef service_ref,
                              DNSServiceFlags service_flags,
                              DNSServiceErrorType error_type,
                              const char *name,
                              const char *regtype,
                              const char *domain,
                              void *context)
{
  SalutBonjourSelf *self = SALUT_BONJOUR_SELF (context);
  SalutSelf *_self = SALUT_SELF (self);
  SalutBonjourSelfPrivate *priv = self->priv;
  DNSServiceErrorType _error_type = kDNSServiceErr_NoError;
  GError *error = NULL;

  if (!self || error_type != kDNSServiceErr_NoError)
    {
      DEBUG ("Service Registration Failed with : (%d)", error_type);
      salut_bonjour_discovery_client_drop_svc_ref (priv->discovery_client,
          priv->bonjour_service);
    }
   else
     {
       _error_type = _bonjour_publish_record (self,
           &priv->presence_record, kDNSServiceType_TXT,
           TXTRecordGetLength (&priv->txt_record_presence),
           TXTRecordGetBytesPtr (&priv->txt_record_presence), 0, FALSE);

       if (_error_type != kDNSServiceErr_NoError)
         {
           DEBUG ("Publish Text Records failed witih : (%d)", _error_type);
           return;
         }

       if (!salut_bonjour_self_set_caps (_self, &error))
         {
           DEBUG ("Error publishing caps : %s", error->message);
           return;
         }

       salut_self_established (_self);
    }
}

static gboolean
salut_bonjour_self_announce (SalutSelf *_self,
                             guint16 port,
                             GError **error)
{
  SalutBonjourSelf *self = SALUT_BONJOUR_SELF (_self);
  SalutBonjourSelfPrivate *priv = self->priv;
  DNSServiceErrorType error_type = kDNSServiceErr_NoError;
  const gchar *dnssd_name;

  dnssd_name =
    salut_bonjour_discovery_client_get_dnssd_name (priv->discovery_client);

  TXTRecordCreate (&priv->txt_record_presence, 256, NULL);
  error_type = _bonjour_self_init_txt_record_presence (self,
      &priv->txt_record_presence, port);

  RETURN_ERROR_IF_FAIL (error_type, error);

  _self->name = g_strdup_printf ("%s@%s", _self->published_name,
          g_get_host_name ());

  error_type = DNSServiceRegister (&priv->bonjour_service,
      kDNSServiceInterfaceIndexAny, 0, _self->name,
      dnssd_name, NULL, NULL,
      htons (port), 0, NULL, _bonjour_service_register_cb, self);

  RETURN_ERROR_IF_FAIL (error_type, error);

  salut_bonjour_discovery_client_watch_svc_ref (priv->discovery_client,
      priv->bonjour_service);

  return TRUE;
}

static gboolean
salut_bonjour_self_set_presence (SalutSelf *self,
                                 GError **error)
{
  SalutBonjourSelf *_self = SALUT_BONJOUR_SELF (self);
  SalutBonjourSelfPrivate *priv = _self->priv;
  DNSServiceErrorType error_type;

  error_type = TXTRecordSetValue (&priv->txt_record_presence, "status",
         strlen (salut_presence_status_txt_names[self->status]),
         salut_presence_status_txt_names[self->status]);

  RETURN_ERROR_IF_FAIL (error_type, error);

  if (self->status_message != NULL)
    {
      error_type = TXTRecordSetValue (&priv->txt_record_presence, "msg",
              strlen (self->status_message), self->status_message);
    }
  else if ((TXTRecordContainsKey (TXTRecordGetLength (&priv->txt_record_presence),
          &priv->txt_record_presence, "msg")) == TRUE)
    {
      error_type = TXTRecordRemoveValue (&priv->txt_record_presence, "msg");
    }

  RETURN_ERROR_IF_FAIL (error_type, error);

  error_type = _bonjour_publish_record (_self, &priv->presence_record,
      kDNSServiceType_TXT, TXTRecordGetLength (&priv->txt_record_presence),
      TXTRecordGetBytesPtr (&priv->txt_record_presence), 0, TRUE);

  RETURN_ERROR_IF_FAIL (error_type, error);

  return TRUE;
}

static gboolean
salut_bonjour_self_set_alias (SalutSelf *_self,
                              GError **error)
{
  SalutBonjourSelf *self = SALUT_BONJOUR_SELF (_self);
  SalutBonjourSelfPrivate *priv = self->priv;
  DNSServiceErrorType error_type = kDNSServiceErr_NoError;

  error_type = TXTRecordSetValue (&priv->txt_record_presence, "nick",
      strlen (_self->alias), _self->alias);

  RETURN_ERROR_IF_FAIL (error_type, error);

  error_type = _bonjour_publish_record (self, &priv->presence_record,
      kDNSServiceType_TXT, TXTRecordGetLength (&priv->txt_record_presence),
        TXTRecordGetBytesPtr (&priv->txt_record_presence), 0, TRUE);

  RETURN_ERROR_IF_FAIL (error_type, error);

  return TRUE;
}

static void
salut_bonjour_self_remove_avatar (SalutSelf *_self)
{

  SalutBonjourSelf *self = SALUT_BONJOUR_SELF (_self);
  SalutBonjourSelfPrivate *priv = self->priv;
  DNSServiceErrorType error_type = kDNSServiceErr_NoError;

  error_type = TXTRecordRemoveValue (&priv->txt_record_presence, "phsh");

  if (error_type != kDNSServiceErr_NoError)
    {
      DEBUG ("Error Removing value for phsh : (%d)", error_type);
    }

  error_type = _bonjour_publish_record (self, &priv->presence_record,
      kDNSServiceType_TXT, TXTRecordGetLength (&priv->txt_record_presence),
      TXTRecordGetBytesPtr (&priv->txt_record_presence), 0, TRUE);

  if (error_type != kDNSServiceErr_NoError)
    {
      DEBUG ("Error in Remove Avatar Token Update : (%d)", error_type);
    }
}

static gboolean
salut_bonjour_self_publish_avatar (SalutBonjourSelf *self,
                                   guint8 *data,
                                   gsize size,
                                   GError **error)
{
  SalutBonjourSelfPrivate *priv = self->priv;
  SalutSelf *_self = SALUT_SELF (self);
  gchar *name;
  const gchar *dnssd_name;
  gboolean is_new = FALSE;
  DNSServiceErrorType error_type = kDNSServiceErr_NoError;

  dnssd_name = salut_bonjour_discovery_client_get_dnssd_name (
      priv->discovery_client);

  name = g_strdup_printf ("%s.%s.local", _self->name, dnssd_name);

  if (!priv->avatar_record)
    is_new = TRUE;

  error_type = _bonjour_publish_record (self, &priv->avatar_record,
      kDNSServiceType_NULL, size, data, 120, is_new ? FALSE : TRUE);

  g_free (name);

  RETURN_FALSE_IF_FAIL (error_type);

  return TRUE;
}

static gboolean
salut_bonjour_self_set_avatar (SalutSelf *_self,
                               guint8 *data,
                               gsize size,
                               GError **error)
{
  SalutBonjourSelf *self = SALUT_BONJOUR_SELF (_self);
  SalutBonjourSelfPrivate *priv = self->priv;
  DNSServiceErrorType error_type = kDNSServiceErr_NoError;

  if (!salut_bonjour_self_publish_avatar (self, data, size, error))
    {
      *error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "bonjour-self failed with (%d)", error_type);
      return FALSE;
    }

  _self->avatar = g_memdup (data, size);
  _self->avatar_size = size;

  if (size > 0)
    _self->avatar_token = sha1_hex (data, size);

  error_type = TXTRecordSetValue (&priv->txt_record_presence, "phsh",
      strlen (_self->avatar_token), _self->avatar_token);

  RETURN_ERROR_IF_FAIL (error_type, error);

  error_type = _bonjour_publish_record (self, &priv->presence_record,
      kDNSServiceType_TXT, TXTRecordGetLength (&priv->txt_record_presence),
      TXTRecordGetBytesPtr (&priv->txt_record_presence), 0, TRUE);

  RETURN_ERROR_IF_FAIL (error_type, error);

  return TRUE;
}

static void salut_bonjour_self_dispose (GObject *object);

static void
salut_bonjour_self_class_init (
    SalutBonjourSelfClass *salut_bonjour_self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_bonjour_self_class);
  SalutSelfClass *self_class = SALUT_SELF_CLASS (
      salut_bonjour_self_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_bonjour_self_class,
      sizeof (SalutBonjourSelfPrivate));

  object_class->dispose = salut_bonjour_self_dispose;

  object_class->get_property = salut_bonjour_self_get_property;
  object_class->set_property = salut_bonjour_self_set_property;

  self_class->announce = salut_bonjour_self_announce;
  self_class->set_presence = salut_bonjour_self_set_presence;
  self_class->set_caps = salut_bonjour_self_set_caps;
  self_class->set_alias = salut_bonjour_self_set_alias;
  self_class->remove_avatar = salut_bonjour_self_remove_avatar;
  self_class->set_avatar = salut_bonjour_self_set_avatar;

  param_spec = g_param_spec_object (
      "discovery-client",
      "SalutBonjourDiscoveryClient object",
      "The Salut Bonjour Discovery client associated with this self object",
      SALUT_TYPE_BONJOUR_DISCOVERY_CLIENT,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_DISCOVERY_CLIENT,
      param_spec);
}

void
salut_bonjour_self_dispose (GObject *object)
{
  SalutBonjourSelf *self = SALUT_BONJOUR_SELF (object);
  SalutBonjourSelfPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  salut_bonjour_discovery_client_drop_svc_ref (priv->discovery_client,
      priv->bonjour_service);

  if (priv->discovery_client != NULL)
    {
      g_object_unref (priv->discovery_client);
      priv->discovery_client = NULL;
    }

  if (G_OBJECT_CLASS (salut_bonjour_self_parent_class)->dispose)
    G_OBJECT_CLASS (salut_bonjour_self_parent_class)->dispose (object);
}

SalutBonjourSelf *
salut_bonjour_self_new (SalutConnection *connection,
                        SalutBonjourDiscoveryClient *discovery_client,
                        const gchar *nickname,
                        const gchar *first_name,
                        const gchar *last_name,
                        const gchar *jid,
                        const gchar *email,
                        const gchar *published_name,
                        const GArray *olpc_key,
                        const gchar *olpc_color)
{
  return g_object_new (SALUT_TYPE_BONJOUR_SELF,
      "connection", connection,
      "discovery-client", discovery_client,
      "nickname", nickname,
      "first-name", first_name,
      "last-name", last_name,
      "jid", jid,
      "email", email,
      "published-name", published_name,
#ifdef ENABLE_OLPC
      "olpc-key", olpc_key,
      "olpc-color", olpc_color,
#endif
      NULL);
}
