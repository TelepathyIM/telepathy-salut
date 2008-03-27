/*
 * salut-avahi-muc-channel.c - Source for SalutAvahiMucChannel
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

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include <string.h>

#define DEBUG_FLAG DEBUG_MUC
#include "debug.h"

#include "salut-avahi-muc-channel.h"

#include <avahi-gobject/ga-entry-group.h>

G_DEFINE_TYPE (SalutAvahiMucChannel, salut_avahi_muc_channel,
    SALUT_TYPE_MUC_CHANNEL);

/* properties */
enum
{
  PROP_DISCOVERY_CLIENT = 1,
  LAST_PROPERTY
};

/* private structure */
typedef struct _SalutAvahiMucChannelPrivate SalutAvahiMucChannelPrivate;

struct _SalutAvahiMucChannelPrivate
{
  SalutAvahiDiscoveryClient *discovery_client;
  GaEntryGroup *muc_group;
  GaEntryGroupService *service;

  gboolean dispose_has_run;
};

#define SALUT_AVAHI_MUC_CHANNEL_GET_PRIVATE(obj) \
    ((SalutAvahiMucChannelPrivate *) (SalutAvahiMucChannel *)obj->priv)


static void
salut_avahi_muc_channel_get_property (GObject *object,
                                      guint property_id,
                                      GValue *value,
                                      GParamSpec *pspec)
{
  SalutAvahiMucChannel *chan = SALUT_AVAHI_MUC_CHANNEL (object);
  SalutAvahiMucChannelPrivate *priv =
    SALUT_AVAHI_MUC_CHANNEL_GET_PRIVATE (chan);

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
salut_avahi_muc_channel_set_property (GObject *object,
                                      guint property_id,
                                      const GValue *value,
                                      GParamSpec *pspec)
{
  SalutAvahiMucChannel *chan = SALUT_AVAHI_MUC_CHANNEL (object);
  SalutAvahiMucChannelPrivate *priv =
    SALUT_AVAHI_MUC_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_DISCOVERY_CLIENT:
      priv->discovery_client = g_value_get_object (value);
      g_object_ref (priv->discovery_client);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static GObject *
salut_avahi_muc_channel_constructor (GType type,
                                     guint n_props,
                                     GObjectConstructParam *props)
{
  GObject *obj;
  TpHandleRepoIface *contact_repo;
  TpBaseConnection *base_conn;
  SalutMucChannel *chan;

  obj = G_OBJECT_CLASS (salut_avahi_muc_channel_parent_class)->
    constructor (type, n_props, props);

  chan = SALUT_MUC_CHANNEL (obj);

  base_conn = TP_BASE_CONNECTION (chan->connection);
  contact_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);

  /* FIXME: This is an ugly workaround. See fd.o #15092 */
  tp_group_mixin_init (obj, G_STRUCT_OFFSET (SalutMucChannel, group),
      contact_repo, base_conn->self_handle);
  tp_group_mixin_change_flags (obj,
       TP_CHANNEL_GROUP_FLAG_CAN_ADD|TP_CHANNEL_GROUP_FLAG_MESSAGE_ADD, 0);

  return obj;
}

static void
salut_avahi_muc_channel_init (SalutAvahiMucChannel *self)
{
  SalutAvahiMucChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_AVAHI_MUC_CHANNEL, SalutAvahiMucChannelPrivate);

  self->priv = priv;

  priv->discovery_client = NULL;
}

static void salut_avahi_muc_channel_dispose (GObject *object);

static gboolean
salut_avahi_muc_channel_publish_service (SalutMucChannel *channel,
                                         GibberMucConnection *muc_connection,
                                         const gchar *muc_name)
{
  SalutAvahiMucChannel *self = SALUT_AVAHI_MUC_CHANNEL (channel);
  SalutAvahiMucChannelPrivate *priv =
    SALUT_AVAHI_MUC_CHANNEL_GET_PRIVATE (self);
  AvahiStringList *txt_record = NULL;
  GError *error = NULL;
  const GHashTable *params;
  const gchar *address, *port_str;
  gchar *host = NULL;
  guint16 port;
  uint16_t dns_type;
  size_t dns_payload_length;
  AvahiAddress addr;

  /* We are already announcing this muc group */
  if (priv->muc_group != NULL)
    return TRUE;

  g_assert (priv->service == NULL);

  /* We didn't connect to this group just yet */
  if (muc_connection->state != GIBBER_MUC_CONNECTION_CONNECTED)
    {
      DEBUG ("Not yet connected to this muc, not announcing");
      return TRUE;
    }

  priv->muc_group = ga_entry_group_new ();

  if (!ga_entry_group_attach (priv->muc_group,
        priv->discovery_client->avahi_client, &error))
    {
      DEBUG ("entry group attach failed: %s", error->message);
      goto publish_service_error;
    }

  params = gibber_muc_connection_get_parameters (muc_connection);
  address = g_hash_table_lookup ((GHashTable *) params, "address");
  if (address == NULL)
    {
      DEBUG ("can't find connection address");
      goto publish_service_error;
    }
  port_str = g_hash_table_lookup ((GHashTable *) params, "port");
  if (port_str == NULL)
    {
      DEBUG ("can't find connection port");
      goto publish_service_error;
    }

  if (avahi_address_parse (address, AVAHI_PROTO_UNSPEC, &addr) == NULL)
    {
      DEBUG ("Can't convert address \"%s\" to AvahiAddress", address);
      goto publish_service_error;
    }

  switch (addr.proto)
    {
    case AVAHI_PROTO_INET:
      dns_type = AVAHI_DNS_TYPE_A;
      dns_payload_length = sizeof (AvahiIPv4Address);
      break;
    case AVAHI_PROTO_INET6:
      dns_type = AVAHI_DNS_TYPE_AAAA;
      dns_payload_length = sizeof (AvahiIPv6Address);
      break;
    default:
      DEBUG ("Don't know how to convert AvahiProtocol 0x%x to DNS record",
          addr.proto);
      goto publish_service_error;
    }

  host = g_strdup_printf ("%s." SALUT_DNSSD_CLIQUE ".local", muc_name);

  /* Add the record */
  if (!ga_entry_group_add_record_full (priv->muc_group,
        AVAHI_IF_UNSPEC, addr.proto, 0,
        host, AVAHI_DNS_CLASS_IN, dns_type, AVAHI_DEFAULT_TTL_HOST_NAME,
        &(addr.data.data), dns_payload_length, &error))
    {
      DEBUG ("add A/AAAA record failed: %s", error->message);
      goto publish_service_error;
    }

  port = atoi (port_str);

  txt_record = avahi_string_list_new ("txtvers=0", NULL);

  /* We shouldn't add the service but manually create the SRV record so
   * we'll be able to allow multiple announcers */
  priv->service = ga_entry_group_add_service_full_strlist (
      priv->muc_group, AVAHI_IF_UNSPEC, addr.proto, 0, muc_name,
      SALUT_DNSSD_CLIQUE, NULL, host, port, &error, txt_record);
  if (priv->service == NULL)
    {
      DEBUG ("add service failed: %s", error->message);
      goto publish_service_error;
    }

  if (!ga_entry_group_commit (priv->muc_group, &error))
    {
      DEBUG ("entry group commit failed: %s", error->message);
      goto publish_service_error;
    }

  DEBUG ("service created: %s %s %d", muc_name, host, port);
  avahi_string_list_free (txt_record);
  g_free (host);
  return TRUE;

publish_service_error:
  if (priv->muc_group != NULL)
    {
      g_object_unref (priv->muc_group);
      priv->muc_group = NULL;
    }

  priv->service = NULL;

  if (txt_record != NULL)
    avahi_string_list_free (txt_record);

  if (host != NULL)
    g_free (host);

  if (error != NULL)
    g_error_free (error);
  return FALSE;
}

static void
salut_avahi_muc_channel_class_init (
    SalutAvahiMucChannelClass *salut_avahi_muc_channel_class) {
  GObjectClass *object_class = G_OBJECT_CLASS (salut_avahi_muc_channel_class);
  SalutMucChannelClass *muc_channel_class = SALUT_MUC_CHANNEL_CLASS (
      salut_avahi_muc_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_avahi_muc_channel_class,
      sizeof (SalutAvahiMucChannelPrivate));

  object_class->dispose = salut_avahi_muc_channel_dispose;

  object_class->constructor = salut_avahi_muc_channel_constructor;
  object_class->get_property = salut_avahi_muc_channel_get_property;
  object_class->set_property = salut_avahi_muc_channel_set_property;

  muc_channel_class->publish_service = salut_avahi_muc_channel_publish_service;

  param_spec = g_param_spec_object (
      "discovery-client",
      "SalutAvahiDiscoveryClient object",
      "The Salut Avahi Discovery client associated with this muc channel",
      SALUT_TYPE_AVAHI_DISCOVERY_CLIENT,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_DISCOVERY_CLIENT,
      param_spec);

  /* FIXME: This is an ugly workaround. See fd.o #15092 */
  tp_group_mixin_class_init(object_class,
    G_STRUCT_OFFSET (SalutMucChannelClass, group_class),
    salut_muc_channel_add_member, NULL);
}

static void
salut_avahi_muc_channel_dispose (GObject *object)
{
  SalutAvahiMucChannel *self = SALUT_AVAHI_MUC_CHANNEL (object);
  SalutAvahiMucChannelPrivate *priv =
    SALUT_AVAHI_MUC_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->discovery_client != NULL)
    {
      g_object_unref (priv->discovery_client);
      priv->discovery_client = NULL;
    }

  if (priv->muc_group != NULL)
    {
      g_object_unref (priv->muc_group);
      priv->muc_group = NULL;
    }

  if (G_OBJECT_CLASS (salut_avahi_muc_channel_parent_class)->dispose)
    G_OBJECT_CLASS (salut_avahi_muc_channel_parent_class)->dispose (object);
}

SalutAvahiMucChannel *
salut_avahi_muc_channel_new (SalutConnection *connection,
                       const gchar *path,
                       GibberMucConnection *muc_connection,
                       TpHandle handle,
                       const gchar *name,
                       SalutAvahiDiscoveryClient *discovery_client,
                       gboolean creator,
                       SalutXmppConnectionManager *xcm)
{
  return g_object_new (SALUT_TYPE_AVAHI_MUC_CHANNEL,
      "connection", connection,
      "object-path", path,
      "muc_connection", muc_connection,
      "handle", handle,
      "name", name,
      "discovery-client", discovery_client,
      "creator", creator,
      "xmpp-connection-manager", xcm,
      NULL);
}
