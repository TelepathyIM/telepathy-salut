/*
 * salut-contact.c - Source for salut_contact
 * Copyright (C) 2005 Collabora Ltd.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "salut-contact.h"
#include "salut-contact-signals-marshal.h"
#include "salut-presence.h"
#include "salut-presence-enumtypes.h"


#include "salut-avahi-enums.h"
#include "salut-avahi-service-resolver.h"
#include <avahi-common/address.h>
#include <avahi-common/defs.h>
#include <avahi-common/malloc.h>

#define DEBUG_FLAG DEBUG_CONTACTS
#include <debug.h>

G_DEFINE_TYPE(SalutContact, salut_contact, G_TYPE_OBJECT)

/* signal enum */
enum
{
    FOUND,
    STATUS_CHANGED,
    LOST,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};


/* private structure */
typedef struct _SalutContactPrivate SalutContactPrivate;

struct _SalutContactPrivate
{
  gboolean dispose_has_run;
  SalutAvahiClient *client;
  GList *resolvers;
  gboolean found;
};

#define SALUT_CONTACT_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_CONTACT, SalutContactPrivate))

static void
salut_contact_init (SalutContact *obj)
{
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  obj->name = NULL;
  obj->status = SALUT_PRESENCE_AVAILABLE;
  obj->status_message = NULL;
  priv->client = NULL;
  priv->resolvers = NULL;
  priv->found = FALSE;
}

static void salut_contact_dispose (GObject *object);
static void salut_contact_finalize (GObject *object);

static void
salut_contact_class_init (SalutContactClass *salut_contact_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_contact_class);

  g_type_class_add_private (salut_contact_class, sizeof (SalutContactPrivate));

  object_class->dispose = salut_contact_dispose;
  object_class->finalize = salut_contact_finalize;
  
  signals[FOUND] = g_signal_new("found",
                                G_OBJECT_CLASS_TYPE(salut_contact_class),
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL, NULL,
                                g_cclosure_marshal_VOID__VOID,
                                G_TYPE_NONE, 0);

  signals[STATUS_CHANGED] = g_signal_new("status-changed",
                                G_OBJECT_CLASS_TYPE(salut_contact_class),
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL, NULL,
                                salut_contact_marshal_VOID__INT_STRING,
                                G_TYPE_NONE, 2,
                                SALUT_TYPE_PRESENCE_ID,
                                G_TYPE_STRING);

  signals[LOST] = g_signal_new("lost",
                                G_OBJECT_CLASS_TYPE(salut_contact_class),
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL, NULL,
                                g_cclosure_marshal_VOID__VOID,
                                G_TYPE_NONE, 0);
}

void
salut_contact_dispose (GObject *object)
{
  SalutContact *self = SALUT_CONTACT (object);
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */
  if (priv->client) 
    g_object_unref(priv->client);
  priv->client = NULL;

  g_list_foreach(priv->resolvers, (GFunc)g_object_unref, NULL);
  g_list_free(priv->resolvers);
  priv->resolvers = NULL;

  if (G_OBJECT_CLASS (salut_contact_parent_class)->dispose)
    G_OBJECT_CLASS (salut_contact_parent_class)->dispose (object);
}

void
salut_contact_finalize (GObject *object) {
  SalutContact *self = SALUT_CONTACT (object);
  //SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  g_free(self->name);
  g_free(self->status_message);

  G_OBJECT_CLASS (salut_contact_parent_class)->finalize (object);
}

struct resolverinfo { 
  AvahiIfIndex interface; 
  AvahiProtocol protocol;
  const gchar *name; 
  const gchar *type; 
  const gchar *domain;
};

static gint
compare_resolver(gconstpointer a, gconstpointer b) {
  struct resolverinfo *info = (struct resolverinfo *) b;

  AvahiIfIndex interface; 
  AvahiProtocol protocol;
  gchar *name; 
  gchar *type; 
  gchar *domain;

  g_object_get((gpointer)a, 
               "interface", &interface,
               "protocol", &protocol,
               "name", &name,
               "type", &type,
               "domain", &domain,
               NULL);
  if (interface == info->interface 
      && protocol == info->protocol
      && !strcmp(name, info->name)
      && !strcmp(type, info->type)
      && !strcmp(domain, info->domain)) {
    return 0;
  }
  return 1;
}

static SalutAvahiServiceResolver *
find_resolver(SalutContact *contact, 
              AvahiIfIndex interface, AvahiProtocol protocol,
              const gchar *name, const gchar *type, const gchar *domain) {
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (contact);
  struct resolverinfo info;
  GList *ret;
  info.interface = interface;
  info.protocol = protocol;
  info.name = name;
  info.type = type;
  info.domain = domain;
  ret = g_list_find_custom(priv->resolvers, &info, compare_resolver); 
  return ret ? SALUT_AVAHI_SERVICE_RESOLVER(ret) : NULL;
}


SalutContact *
salut_contact_new(SalutAvahiClient *client, const gchar *name) {
  SalutContact *ret;
  SalutContactPrivate *priv;

  ret = g_object_new(SALUT_TYPE_CONTACT, NULL);
  priv = SALUT_CONTACT_GET_PRIVATE (ret);

  ret->name = g_strdup(name);

  g_object_ref(client);
  priv->client = client;

  return ret;
}

static void
contact_resolved_cb(SalutAvahiServiceResolver *resolver, 
                    AvahiIfIndex interface, AvahiProtocol protocol,
                    gchar *name, gchar *type, gchar *domain, gchar *host_name,
                    AvahiAddress *a, gint port,
                    AvahiStringList *txt, AvahiLookupResultFlags flags,
                    gpointer userdata) {
  SalutContact *self = SALUT_CONTACT (userdata);
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (self);
  AvahiStringList *t;
  gboolean status_changed = FALSE;

  if (!priv->found) {
    g_signal_emit(self, signals[FOUND], 0);
    priv->found = TRUE;
  }

  if ((t = avahi_string_list_find(txt, "status")) != NULL) { 
    int i;
    gchar *key, *value;
    avahi_string_list_get_pair(t, &key, &value, NULL);

    for (i = 0; i < SALUT_PRESENCE_NR_PRESENCES ; i++) {
      if (!strcmp(value, salut_presence_statuses[i].txt_name)) {
        break;
      }
    }

    if (i != self->status && i < SALUT_PRESENCE_NR_PRESENCES) {
      status_changed = TRUE;
      self->status = i;
    }

    avahi_free(key);
    avahi_free(value);
  }

  if ((t = avahi_string_list_find(txt, "msg")) != NULL) { 
    gchar *key, *value;
    avahi_string_list_get_pair(t, &key, &value, NULL);
    if (self->status_message == NULL || strcmp(self->status_message, value)) {
      status_changed = TRUE;
      g_free(self->status_message);
      self->status_message = g_strdup(value);
    }
    avahi_free(key);
    avahi_free(value);
  }

  if (status_changed) {
    g_signal_emit(self, signals[STATUS_CHANGED], 0, self->status, 
                                                    self->status_message);
  }
}

static void
contact_failed_cb(SalutAvahiServiceResolver  *resolver, GError *error, 
                   gpointer userdata) {
  SalutContact *self = SALUT_CONTACT (userdata);
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (self);

  priv->resolvers = g_list_remove(priv->resolvers, resolver);
  g_object_unref(resolver);
  if (g_list_length(priv->resolvers) == 0 && priv->found) {
    g_signal_emit(self, signals[LOST], 0);
  }
}

void
salut_contact_add_service(SalutContact *contact, 
                          AvahiIfIndex interface, AvahiProtocol protocol,
                          const char *name, const char *type, 
                          const char *domain) {
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (contact);
  SalutAvahiServiceResolver *resolver;
  resolver = find_resolver(contact, interface, protocol, name, type, domain);

  if (resolver) 
    return;

  resolver = salut_avahi_service_resolver_new_full(interface,
                                                   protocol,
                                                   name, type, domain, 
                                                   protocol, 0);
  g_signal_connect(resolver, "found", G_CALLBACK(contact_resolved_cb), contact);
  g_signal_connect(resolver, "failure", G_CALLBACK(contact_failed_cb), contact);
  if (!salut_avahi_service_resolver_attach(resolver, priv->client, NULL)) {
    g_warning("Failed to attach resolver");
  }
  DEBUG("New resolver for %s", contact->name);
  priv->resolvers = g_list_prepend(priv->resolvers, resolver);
}

void
salut_contact_remove_service(SalutContact *contact, 
                          AvahiIfIndex interface, AvahiProtocol protocol,
                          const char *name, const char *type, 
                          const char *domain) {
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (contact);
  SalutAvahiServiceResolver *resolver;
  resolver =  find_resolver(contact, interface, protocol, name, type, domain);

  if (!resolver) 
    return;
  g_object_unref(resolver);
  priv->resolvers = g_list_remove(priv->resolvers, resolver);

  if (g_list_length(priv->resolvers) == 0 && priv->found)  {
    g_signal_emit(contact, signals[LOST], 0);
    priv->found = FALSE;
  }
}

static void
_avahi_address_to_sockaddr(AvahiAddress *address, guint16 port,
                           AvahiIfIndex index,
                           struct sockaddr_storage *sockaddr) {
  switch (address->proto) {
    case AVAHI_PROTO_INET: {
      struct sockaddr_in *sockaddr4 = (struct sockaddr_in *)sockaddr;
      sockaddr4->sin_family = AF_INET;
      sockaddr4->sin_port = htons(port);
      /* ->address is already in network byte order */
      sockaddr4->sin_addr.s_addr = address->data.ipv4.address;
      break;
    }
    case AVAHI_PROTO_INET6: {
      struct sockaddr_in6 *sockaddr6 = (struct sockaddr_in6 *)sockaddr;
      sockaddr6->sin6_family = AF_INET6;
      sockaddr6->sin6_port = htons(port);
      memcpy(sockaddr6->sin6_addr.s6_addr,
             address->data.ipv6.address, 16);

      sockaddr6->sin6_flowinfo = 0;
      sockaddr6->sin6_scope_id = index;
      break;
    }
    default:
      g_assert_not_reached();
  }
}

static void
_add_address(gpointer data, gpointer user_data) {
  SalutAvahiServiceResolver *resolver = SALUT_AVAHI_SERVICE_RESOLVER(data);
  GArray *addresses = (GArray *)user_data;
  salut_contact_address_t s_address;
  AvahiAddress address;
  guint16 port;
  AvahiIfIndex ifindex;

  g_object_get(resolver, "interface", &ifindex, NULL);
  if (salut_avahi_service_resolver_get_address(resolver, &address, &port)) {
    _avahi_address_to_sockaddr(&address, port, ifindex, &(s_address.address));
    g_array_append_val(addresses, s_address);
  }
}

GArray *
salut_contact_get_addresses(SalutContact *contact) {
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (contact);
  GArray *addresses; 

  addresses = g_array_sized_new(TRUE, TRUE, 
                                sizeof(salut_contact_address_t),
                                g_list_length(priv->resolvers));
  g_list_foreach(priv->resolvers, _add_address, addresses);

  return addresses;
}

static gint
_compare_address(gconstpointer a, gconstpointer b) {
  SalutAvahiServiceResolver *resolver = SALUT_AVAHI_SERVICE_RESOLVER(a);
  struct sockaddr_storage addr_a;
  struct sockaddr_storage *addr_b = (struct sockaddr_storage *)b;
  AvahiIfIndex ifindex;
  AvahiAddress address;
  uint16_t port;

  g_object_get(resolver, "interface", &ifindex, NULL);
  if (!salut_avahi_service_resolver_get_address(resolver, &address, &port)) {
    return -1;
  }
  _avahi_address_to_sockaddr(&address, port, ifindex, &addr_a);

  if ( ((struct sockaddr *)&addr_a)->sa_family 
       != ((struct sockaddr *)addr_b)->sa_family) {
    return -1;
  }

  switch (((struct sockaddr *)&addr_a)->sa_family) {
    case AF_INET6: {
      struct sockaddr_in *a4 = (struct sockaddr_in *)&addr_a;
      struct sockaddr_in *b4 = (struct sockaddr_in *)addr_b;
      return b4->sin_addr.s_addr - a4->sin_addr.s_addr;
    }
    case AF_INET: {
      struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)&addr_a;
      struct sockaddr_in6 *b6 = (struct sockaddr_in6 *)addr_b;
      /* FIXME should we compare the scope_id too ? */
      return memcmp(a6->sin6_addr.s6_addr, b6->sin6_addr.s6_addr, 16);
    }
    default:
      g_assert_not_reached();
  }
  return 0;
}

gboolean
salut_contact_has_address(SalutContact *contact,
                           struct sockaddr_storage *address) {
  SalutContactPrivate *priv = SALUT_CONTACT_GET_PRIVATE (contact);
  return 
     (g_list_find_custom(priv->resolvers, address, _compare_address) != NULL);
}

