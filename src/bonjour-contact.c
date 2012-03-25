/*
 * bonjour-contact.c - Source for SalutBonjourContact
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

#undef interface
#include "bonjour-contact.h"

#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG DEBUG_MUC
#include "debug.h"

G_DEFINE_TYPE (SalutBonjourContact, salut_bonjour_contact,
    SALUT_TYPE_CONTACT);

/* properties */
enum {
  PROP_CLIENT = 1,
  LAST_PROP
};

/* private structure */
typedef struct _SalutBonjourContactPrivate SalutBonjourContactPrivate;

struct _SalutBonjourContactPrivate
{
  SalutBonjourDiscoveryClient *discovery_client;
  GSList *resolvers;

  char *full_name;

  gboolean dispose_has_run;
};

struct resolverInfo {
  uint32_t interface;
  const char *name;
  const char *type;
  const char *domain;
};

typedef struct _SalutBonjourResolveCtx
{
  DNSServiceRef resolve_ref;
  DNSServiceRef address_ref;

  SalutBonjourContact *contact;

  char *name;
  char *type;
  char *domain;
  uint32_t interface;
  struct sockaddr *address;
  uint16_t port;

  uint16_t txt_length;
  char *txt_record;
} SalutBonjourResolveCtx;

static void
salut_bonjour_contact_init (SalutBonjourContact *self)
{
  SalutBonjourContactPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_BONJOUR_CONTACT, SalutBonjourContactPrivate);

  self->priv = priv;

  priv->full_name = NULL;
  priv->resolvers = NULL;
}

static void
_salut_bonjour_resolve_ctx_free (SalutBonjourContact *self,
                                 SalutBonjourResolveCtx *ctx)
{
  SalutBonjourContactPrivate *priv = self->priv;

  if (ctx->address_ref != NULL)
    {
      salut_bonjour_discovery_client_drop_svc_ref (priv->discovery_client,
          ctx->address_ref);
    }

  if (ctx->txt_record)
    {
      g_free (ctx->txt_record);
    }

  if (ctx->address)
    g_free (ctx->address);

  if (ctx->name)
    g_free (ctx->name);

  if (ctx->type)
    g_free (ctx->type);

  if (ctx->domain)
    g_free (ctx->domain);

  g_slice_free (SalutBonjourResolveCtx, ctx);
}

static void
salut_bonjour_contact_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
  SalutBonjourContact *self = SALUT_BONJOUR_CONTACT (object);
  SalutBonjourContactPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_CLIENT:
        g_value_set_object (value, priv->discovery_client);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_bonjour_contact_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  SalutBonjourContact *self = SALUT_BONJOUR_CONTACT (object);
  SalutBonjourContactPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_CLIENT:
        priv->discovery_client = g_value_dup_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static gchar *
salut_bonjour_contact_dup_jid (WockyContact *contact)
{
  SalutContact *self = SALUT_CONTACT (contact);

  return g_strdup (self->name);
}

static GList *
salut_bonjour_contact_ll_get_addresses (WockyLLContact *contact)
{
  SalutBonjourContact *self = SALUT_BONJOUR_CONTACT (contact);
  SalutBonjourContactPrivate *priv = self->priv;
  /* omg, GQueue! */
  GQueue queue = G_QUEUE_INIT;
  GSList *l;

  for (l = priv->resolvers; l != NULL; l = l->next)
    {
     SalutBonjourResolveCtx *ctx = l->data;
     uint16_t port;

     if (ctx->address)
       {
         GInetAddress *addr;
         GSocketAddress *socket_address;

         if (ctx->address->sa_family == AF_INET)
           {
             struct sockaddr_in *address = (struct sockaddr_in *) ctx->address;

             port = ctx->port;
             addr = g_inet_address_new_from_bytes (
                 (guint8 *) &(address->sin_addr), G_SOCKET_FAMILY_IPV4);
             DEBUG ("%s", g_inet_address_to_string (addr));
           }
         else if (ctx->address->sa_family == AF_INET6)
           {
             struct sockaddr_in6 *address = (struct sockaddr_in6 *) ctx->address;
             port = ctx->port;
             addr = g_inet_address_new_from_bytes (
                 (uint8_t *) &(address->sin6_addr), G_SOCKET_FAMILY_IPV6);
           }
         else
           g_assert_not_reached ();

         socket_address = g_inet_socket_address_new (addr, port);
         g_object_unref (addr);

         g_queue_push_tail (&queue, socket_address);
       }
    }

  return queue.head;
}

static void
_bonjour_add_address (SalutBonjourResolveCtx *ctx,
                      GArray *addresses)
{
  salut_contact_address_t s_address;
  const struct sockaddr *address = ctx->address;

  if (ctx->address)
    {
      if (address->sa_family == AF_INET)
        {
          struct sockaddr_in *_addr4 =
             (struct sockaddr_in *) &s_address.address;
          struct sockaddr_in *address4 = (struct sockaddr_in *) ctx->address;

          _addr4->sin_family = AF_INET;
          _addr4->sin_port = address4->sin_port;
          _addr4->sin_addr.s_addr = address4->sin_addr.s_addr;
        }
      else if (address->sa_family == AF_INET6)
        {
          struct sockaddr_in6 *_addr6 =
              (struct sockaddr_in6 *) &s_address.address;
          struct sockaddr_in6 *address6 = (struct sockaddr_in6 *) ctx->address;

          _addr6->sin6_family = AF_INET6;
          _addr6->sin6_port = address6->sin6_port;
          _addr6->sin6_flowinfo = 0;
          _addr6->sin6_scope_id = address6->sin6_scope_id;
          memcpy (_addr6->sin6_addr.s6_addr, address6->sin6_addr.s6_addr, 16);
        }
      else
        g_assert_not_reached ();

      g_array_append_val (addresses, s_address);
    }
}

static GArray *
salut_bonjour_contact_get_addresses (SalutContact *self)
{
  SalutBonjourContact *_self = SALUT_BONJOUR_CONTACT (self);
  SalutBonjourContactPrivate *priv = _self->priv;
  GArray *addresses;

  addresses =
    g_array_sized_new (TRUE, TRUE, sizeof (salut_contact_address_t),
        g_slist_length (priv->resolvers));
  g_slist_foreach (priv->resolvers, (GFunc) _bonjour_add_address, addresses);

  return addresses;
}

static gint
_compare_sockaddr (SalutBonjourResolveCtx *ctx,
                   struct sockaddr *b)
{
  const struct sockaddr *a = ctx->address;

  if (a->sa_family != b->sa_family)
    return -1;

  if (a->sa_family == AF_INET)
    {
      struct sockaddr_in *a4 = (struct sockaddr_in *) a;
      struct sockaddr_in *b4 = (struct sockaddr_in *) b;

      return a4->sin_addr.s_addr - b4->sin_addr.s_addr;
    }
  else if (a->sa_family == AF_INET6)
    {
      struct sockaddr_in6 *a6 = (struct sockaddr_in6 *) a;
      struct sockaddr_in6 *b6 = (struct sockaddr_in6 *) b;

      return memcmp (a6->sin6_addr.s6_addr, b6->sin6_addr.s6_addr, 16);
    }
  else
    g_assert_not_reached ();

  return 0;
}

static gint
compare_resolver (SalutBonjourResolveCtx *a,
                  struct resolverInfo *b)
{
  gint result;

  if (a->interface == b->interface
      && !tp_strdiff (a->name, b->name)
      && !tp_strdiff (a->type, b->type)
      && !tp_strdiff (a->domain, b->domain))
    {
      result = 0;
    }
  else
    {
      result = 1;
    }

  return result;
}

static SalutBonjourResolveCtx *
find_resolver (SalutBonjourContact *contact,
               uint32_t interface,
               const char *name,
               const char *type,
               const char *domain)
{
  SalutBonjourContactPrivate *priv = contact->priv;
  struct resolverInfo info;
  GSList *ret;
  info.interface = interface;
  info.name = name;
  info.type = type;
  info.domain = domain;

  ret = g_slist_find_custom (priv->resolvers, &info,
      (GCompareFunc) compare_resolver);

  return ret ? (SalutBonjourResolveCtx *) ret->data : NULL;
}

static gboolean
salut_bonjour_contact_has_address (SalutContact *contact,
                                   struct sockaddr *address,
                                   guint size)
{
  SalutBonjourContact *_self = SALUT_BONJOUR_CONTACT (contact);
  SalutBonjourContactPrivate *priv = _self->priv;

  return (g_slist_find_custom (priv->resolvers, address,
      (GCompareFunc) _compare_sockaddr) != NULL);
}

static void salut_bonjour_contact_dispose (GObject *object);

static void
salut_bonjour_contact_class_init (
    SalutBonjourContactClass *salut_bonjour_contact_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_bonjour_contact_class);
  SalutContactClass *contact_class = SALUT_CONTACT_CLASS (
      salut_bonjour_contact_class);
  WockyContactClass *w_contact_class = WOCKY_CONTACT_CLASS (
      salut_bonjour_contact_class);
  WockyLLContactClass *ll_contact_class = WOCKY_LL_CONTACT_CLASS (
      salut_bonjour_contact_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_bonjour_contact_class,
                              sizeof (SalutBonjourContactPrivate));

  object_class->get_property = salut_bonjour_contact_get_property;
  object_class->set_property = salut_bonjour_contact_set_property;

  object_class->dispose = salut_bonjour_contact_dispose;

  contact_class->get_addresses = salut_bonjour_contact_get_addresses;
  contact_class->has_address = salut_bonjour_contact_has_address;
  contact_class->retrieve_avatar = NULL;

  w_contact_class->dup_jid = salut_bonjour_contact_dup_jid;
  ll_contact_class->get_addresses = salut_bonjour_contact_ll_get_addresses;

  param_spec = g_param_spec_object (
      "discovery-client",
      "SalutBonjourDiscoveryClient object",
      "The Salut Bonjour Discovery client associated with this muc manager",
      SALUT_TYPE_BONJOUR_DISCOVERY_CLIENT,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CLIENT,
      param_spec);
}

void
salut_bonjour_contact_dispose (GObject *object)
{
  SalutBonjourContact *self = SALUT_BONJOUR_CONTACT (object);
  SalutBonjourContactPrivate *priv = self->priv;
  GSList *l;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  for (l = priv->resolvers; l != NULL; l = l->next)
    {
      SalutBonjourResolveCtx *ctx = l->data;

      salut_bonjour_discovery_client_drop_svc_ref (priv->discovery_client,
          ctx->resolve_ref);
      _salut_bonjour_resolve_ctx_free (self, ctx);
    }

  g_slist_free (priv->resolvers);
  priv->resolvers = NULL;

  if (priv->discovery_client != NULL)
    {
      g_object_unref (priv->discovery_client);
      priv->discovery_client = NULL;
    }

  if (priv->full_name)
    {
      g_free (priv->full_name);
      priv->full_name = NULL;
    }

  if (G_OBJECT_CLASS (salut_bonjour_contact_parent_class)->dispose)
    G_OBJECT_CLASS (salut_bonjour_contact_parent_class)->dispose (object);
}

/* public functions */
SalutBonjourContact *
salut_bonjour_contact_new (SalutConnection *connection,
                           const gchar *name,
                           SalutBonjourDiscoveryClient *discovery_client)
{
  g_assert (connection != NULL);
  g_assert (name != NULL);
  g_assert (discovery_client != NULL);

  return g_object_new (SALUT_TYPE_BONJOUR_CONTACT,
      "connection", connection,
      "name", name,
      "discovery-client", discovery_client,
      NULL);
}

static void
update_alias (SalutBonjourContact *self,
              const gchar *nick)
{
  SalutContact *contact = SALUT_CONTACT (self);

  if (!tp_str_empty (nick))
    {
      salut_contact_change_alias (contact, nick);
      return;
    }

  if (!tp_str_empty (contact->full_name))
    {
      salut_contact_change_alias (contact, contact->full_name);
      return;
    }

  salut_contact_change_alias (contact, NULL);
}

static void DNSSD_API
_bonjour_getaddr_cb (DNSServiceRef service_ref,
                     DNSServiceFlags flags,
                     uint32_t interfaceIndex,
                     DNSServiceErrorType error_type,
                     const char *host_name,
                     const struct sockaddr *address,
                     uint32_t ttl,
                     void *context)
{
  SalutBonjourResolveCtx *ctx = (SalutBonjourResolveCtx *) context;
  SalutBonjourContact *self = SALUT_BONJOUR_CONTACT (ctx->contact);
  SalutBonjourContactPrivate *priv = self->priv;
  SalutContact *contact = SALUT_CONTACT (self);
  const char *txt_record = ctx->txt_record;
  uint32_t  txt_length = ctx->txt_length;
  char *status, *status_message, *nick, *first, *last;
  char *node, *hash, *ver;
  char *email, *jid;
  char *tmp;
  uint8_t txt_len;

  if (error_type != kDNSServiceErr_NoError)
    {
      DEBUG ("Resolver failed with : (%d)", error_type);
      salut_bonjour_discovery_client_drop_svc_ref (priv->discovery_client,
          ctx->address_ref);
      ctx->address_ref = NULL;
      g_free (ctx->txt_record);
      ctx->txt_record = NULL;
        return;
    }

  if (ctx->address)
    {
      g_free (ctx->address);
      ctx->address = NULL;
    }

  if (address->sa_family == AF_INET)
    ctx->address = g_memdup (address, sizeof (struct sockaddr_in));
  else if (address->sa_family == AF_INET6)
    ctx->address = g_memdup (address, sizeof (struct sockaddr_in6));
  else
    g_assert_not_reached ();

  salut_bonjour_discovery_client_drop_svc_ref (priv->discovery_client,
      ctx->address_ref);
  ctx->address_ref = NULL;

  salut_contact_freeze (contact);

  /* status */
  tmp = (char *) TXTRecordGetValuePtr
        (txt_length, txt_record, "status", &txt_len);
  status = g_strndup (tmp, txt_len);

  if (status != NULL)
    {
      for (int i = 0; i < SALUT_PRESENCE_NR_PRESENCES; i++)
        {
          if (tp_strdiff (status, salut_presence_status_txt_names[i]))
            {
              salut_contact_change_status (contact, i);
              break;
            }
        }
      free (status);
    }

  /* status message */
  tmp = (char *) TXTRecordGetValuePtr (txt_length, txt_record,
      "msg", &txt_len);
  status_message = g_strndup (tmp, txt_len);
  salut_contact_change_status_message (contact, status_message);
  free (status_message);

  /* real name and nick */
  tmp = (char *) TXTRecordGetValuePtr (txt_length, txt_record,
      "nick", &txt_len);
  nick = g_strndup (tmp, txt_len);
  tmp = (char *) TXTRecordGetValuePtr (txt_length, txt_record,
      "1st", &txt_len);
  first = g_strndup (tmp, txt_len);
  tmp = (char *) TXTRecordGetValuePtr (txt_length, txt_record,
      "last", &txt_len);
  last = g_strndup (tmp, txt_len);

  salut_contact_change_real_name (contact, first, last);
  update_alias (self, nick);

  free (nick);
  free (first);
  free (last);

  /* capabilities */
  tmp = (char *) TXTRecordGetValuePtr (txt_length, txt_record,
      "hash", &txt_len);
  hash = g_strndup (tmp, txt_len);

  tmp = (char *) TXTRecordGetValuePtr (txt_length, txt_record,
      "node", &txt_len);
  node = g_strndup (tmp, txt_len);

  tmp = (char *) TXTRecordGetValuePtr (txt_length, txt_record,
      "ver", &txt_len);
  ver = g_strndup (tmp, txt_len);

  salut_contact_change_capabilities (contact, hash, node, ver);

  free (hash);
  free (node);
  free (ver);

  /* email */
  tmp = (char *) TXTRecordGetValuePtr (txt_length, txt_record,
      "email", &txt_len);
  email = g_strndup (tmp, txt_len);
  tmp = (char *) TXTRecordGetValuePtr (txt_length, txt_record,
      "jid", &txt_len);
  jid = g_strndup (tmp, txt_len);

  salut_contact_change_email (contact, email);
  salut_contact_change_jid (contact, jid);

  free (email);
  free (jid);

  DEBUG ("Announce Contact Found");
  salut_contact_found (contact);
  salut_contact_thaw (contact);

  g_free (ctx->txt_record);
  ctx->txt_length = 0;
  ctx->txt_record = NULL;
}

static void DNSSD_API
_bonjour_service_resolve_cb (DNSServiceRef service_ref,
                             DNSServiceFlags flags,
                             uint32_t interfaceIndex,
                             DNSServiceErrorType error_type,
                             const char *full_name,
                             const char *host_target,
                             uint16_t port,
                             uint16_t txt_length,
                             const unsigned char *txt_record,
                             void *context)
{
  SalutBonjourResolveCtx *ctx = (SalutBonjourResolveCtx *) context;
  SalutBonjourContact *self = SALUT_BONJOUR_CONTACT (ctx->contact);
  SalutBonjourContactPrivate *priv = self->priv;
  DNSServiceErrorType _error_type = kDNSServiceErr_NoError;

  if (ctx->address_ref != NULL)
    {
      salut_bonjour_discovery_client_drop_svc_ref (priv->discovery_client,
          ctx->address_ref);
      ctx->address_ref = NULL;
    }

  if (ctx->txt_record != NULL)
    {
      g_free (ctx->txt_record);
      ctx->txt_record = NULL;
    }

  ctx->txt_record = g_strndup ((const gchar *) txt_record, (guint) txt_length);
  ctx->txt_length = txt_length;
  ctx->port = ntohs (port);

  _error_type = DNSServiceGetAddrInfo (&ctx->address_ref, 0,
      interfaceIndex, kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6,
      host_target, _bonjour_getaddr_cb, ctx);

  if (_error_type != kDNSServiceErr_NoError)
    {
      DEBUG ("Address resolving failed with : (%d)", _error_type);
      return;
    }

  salut_bonjour_discovery_client_watch_svc_ref (priv->discovery_client,
      ctx->address_ref);
}

void
salut_bonjour_contact_remove_service (SalutBonjourContact *self,
                                      uint32_t interface,
                                      const char *name,
                                      const char *type,
                                      const char *domain)
{
  SalutBonjourContactPrivate *priv = self->priv;
  SalutBonjourResolveCtx *ctx = NULL;

  ctx = find_resolver (self, interface, name, type, domain);

  if (ctx == NULL)
    return;

  salut_bonjour_discovery_client_drop_svc_ref (priv->discovery_client,
      ctx->resolve_ref);

  priv->resolvers = g_slist_remove (priv->resolvers, ctx);

  _salut_bonjour_resolve_ctx_free (self, ctx);

  if (priv->resolvers == NULL)
    salut_contact_lost (SALUT_CONTACT (self));
}

gboolean
salut_bonjour_contact_add_service (SalutBonjourContact *self,
                                   uint32_t interface,
                                   const char *name,
                                   const char *type,
                                   const char *domain)
{
  SalutBonjourContactPrivate *priv = self->priv;
  DNSServiceErrorType error_type = kDNSServiceErr_NoError;
  SalutBonjourResolveCtx *ctx =  NULL;

  ctx = find_resolver (self, interface, name, type, domain);
  if (ctx != NULL)
    return TRUE;

  ctx = g_slice_new0 (SalutBonjourResolveCtx);
  ctx->interface = interface;
  ctx->name = g_strdup (name);
  ctx->type = g_strdup (type);
  ctx->domain = g_strdup (domain);
  ctx->contact = self;
  ctx->address = NULL;
  ctx->txt_length = 0;
  ctx->txt_record = NULL;
  ctx->address_ref = NULL;

  error_type = DNSServiceResolve (&ctx->resolve_ref,
      0, interface, name, type, domain, _bonjour_service_resolve_cb, ctx);

  if (error_type != kDNSServiceErr_NoError)
    {
      DEBUG ("ServiceResolve failed with : (%d)", error_type);
      _salut_bonjour_resolve_ctx_free (self, ctx);
      return FALSE;
    }

  salut_bonjour_discovery_client_watch_svc_ref (priv->discovery_client,
      ctx->resolve_ref);

  priv->resolvers = g_slist_prepend (priv->resolvers, ctx);

  return TRUE;
}

gboolean
salut_bonjour_contact_has_services (SalutBonjourContact *self)
{
  SalutBonjourContactPrivate *priv = self->priv;

  return priv->resolvers != NULL;
}
