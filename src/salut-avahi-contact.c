/*
 * salut-avahi-contact.c - Source for SalutAvahiContact
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
#include <string.h>
#include <arpa/inet.h>

#include <avahi-gobject/ga-service-resolver.h>
#include <avahi-gobject/ga-record-browser.h>
#include <avahi-common/address.h>
#include <avahi-common/defs.h>
#include <avahi-common/malloc.h>

#include "salut-avahi-contact.h"

#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG DEBUG_MUC
#include "debug.h"

#define DEBUG_CONTACT(contact, format, ...) G_STMT_START {      \
  DEBUG ("Contact %s: " format, \
    SALUT_CONTACT (contact)->name, ##__VA_ARGS__);  \
} G_STMT_END

#define DEBUG_RESOLVER(contact, resolver, format, ...) G_STMT_START {       \
  gchar *_name;                                                             \
  gchar *_type;                                                             \
  gint _interface;                                                          \
  gint _protocol;                                                           \
                                                                            \
  g_object_get (G_OBJECT(resolver),                                         \
      "name", &_name,                                                       \
      "type", &_type,                                                       \
      "interface", &_interface,                                             \
      "protocol", &_protocol,                                               \
      NULL                                                                  \
  );                                                                        \
                                                                            \
  DEBUG_CONTACT (contact, "Resolver (%s %s intf: %d proto: %d): " format,   \
    _name, _type, _interface, _protocol, ##__VA_ARGS__);                    \
                                                                            \
  g_free (_name);                                                           \
  g_free (_type);                                                           \
} G_STMT_END

#define PRESENCE_TIMEOUT (1200 * 1000)

G_DEFINE_TYPE (SalutAvahiContact, salut_avahi_contact,
    SALUT_TYPE_CONTACT);

/* properties */
enum {
  PROP_CLIENT = 1,
  LAST_PROP
};

/* private structure */
typedef struct _SalutAvahiContactPrivate SalutAvahiContactPrivate;

struct _SalutAvahiContactPrivate
{
  SalutAvahiDiscoveryClient *discovery_client;
  GSList *resolvers;
  guint presence_resolver_failed_timer;
  GaRecordBrowser *record_browser;

  gboolean dispose_has_run;
};

#define SALUT_AVAHI_CONTACT_GET_PRIVATE(obj) \
    ((SalutAvahiContactPrivate *) ((SalutAvahiContact *)obj)->priv)

static void
salut_avahi_contact_init (SalutAvahiContact *self)
{
  SalutAvahiContactPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_AVAHI_CONTACT, SalutAvahiContactPrivate);

  self->priv = priv;

  priv->resolvers = NULL;
}

static void
salut_avahi_contact_get_property (GObject *object,
                                  guint property_id,
                                  GValue *value,
                                  GParamSpec *pspec)
{
  SalutAvahiContact *self = SALUT_AVAHI_CONTACT (object);
  SalutAvahiContactPrivate *priv = SALUT_AVAHI_CONTACT_GET_PRIVATE (self);

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
salut_avahi_contact_set_property (GObject *object,
                                  guint property_id,
                                  const GValue *value,
                                  GParamSpec *pspec)
{
  SalutAvahiContact *self = SALUT_AVAHI_CONTACT (object);
  SalutAvahiContactPrivate *priv = SALUT_AVAHI_CONTACT_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CLIENT:
        priv->discovery_client = g_value_get_object (value);
        g_object_ref (priv->discovery_client);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
_avahi_address_to_sockaddr (AvahiAddress *address,
                            guint16 port,
                            AvahiIfIndex index_,
                            struct sockaddr *sockaddr)
{
  switch (address->proto)
    {
      case AVAHI_PROTO_INET:
        {
          struct sockaddr_in *sockaddr4 = (struct sockaddr_in *) sockaddr;
          sockaddr4->sin_family = AF_INET;
          sockaddr4->sin_port = htons (port);
          /* ->address is already in network byte order */
          sockaddr4->sin_addr.s_addr = address->data.ipv4.address;
          break;
        }
      case AVAHI_PROTO_INET6:
        {
          struct sockaddr_in6 *sockaddr6 = (struct sockaddr_in6 *) sockaddr;
          sockaddr6->sin6_family = AF_INET6;
          sockaddr6->sin6_port = htons (port);
          memcpy (sockaddr6->sin6_addr.s6_addr, address->data.ipv6.address, 16);

          sockaddr6->sin6_flowinfo = 0;
          sockaddr6->sin6_scope_id = index_;
          break;
       }
    default:
      g_assert_not_reached ();
  }
}

static void
_add_address (GaServiceResolver *resolver,
              GArray *addresses)
{
  salut_contact_address_t s_address;
  AvahiAddress address;
  guint16 port;
  AvahiIfIndex ifindex;

  g_object_get (resolver, "interface", &ifindex, NULL);
  if (ga_service_resolver_get_address (resolver, &address, &port))
    {
      _avahi_address_to_sockaddr (&address, port, ifindex,
          (struct sockaddr *) &s_address.address);
      g_array_append_val (addresses, s_address);
    }
}

static GArray *
salut_avahi_contact_get_addresses (SalutContact *contact)
{
  SalutAvahiContact *self = SALUT_AVAHI_CONTACT (contact);
  SalutAvahiContactPrivate *priv = SALUT_AVAHI_CONTACT_GET_PRIVATE (self);
  GArray *addresses;

  addresses = g_array_sized_new (TRUE, TRUE, sizeof (salut_contact_address_t),
      g_slist_length (priv->resolvers));
  g_slist_foreach (priv->resolvers, (GFunc) _add_address, addresses);

  return addresses;
}

static gint
_compare_address (GaServiceResolver *resolver,
                  struct sockaddr *addr_b)
{
  struct sockaddr addr_a;
  AvahiIfIndex ifindex;
  AvahiAddress address;
  uint16_t port;

  g_object_get (resolver, "interface", &ifindex, NULL);
  if (!ga_service_resolver_get_address (resolver, &address, &port))
    return -1;

  _avahi_address_to_sockaddr (&address, port, ifindex, &addr_a);

  if (addr_a.sa_family != addr_b->sa_family)
    return -1;

  switch (addr_a.sa_family)
    {
      case AF_INET:
        {
          struct sockaddr_in *a4 = (struct sockaddr_in *) &addr_a;
          struct sockaddr_in *b4 = (struct sockaddr_in *) addr_b;
          return b4->sin_addr.s_addr - a4->sin_addr.s_addr;
        }
      case AF_INET6:
        {
          struct sockaddr_in6 *a6 = (struct sockaddr_in6 *) &addr_a;
          struct sockaddr_in6 *b6 = (struct sockaddr_in6 *) addr_b;
          /* FIXME should we compare the scope_id too ? */
          return memcmp (a6->sin6_addr.s6_addr, b6->sin6_addr.s6_addr, 16);
        }
      default:
        g_assert_not_reached ();
    }

  return 0;
}

static gboolean
salut_avahi_contact_has_address (SalutContact *contact,
                                 struct sockaddr *address,
                                 guint size)
{
  SalutAvahiContact *self = SALUT_AVAHI_CONTACT (contact);
  SalutAvahiContactPrivate *priv = SALUT_AVAHI_CONTACT_GET_PRIVATE (self);

  return (g_slist_find_custom (priv->resolvers, address,
        (GCompareFunc) _compare_address) != NULL);
}

static void
salut_avahi_contact_avatar_request_flush (SalutAvahiContact *self,
                                          guint8 *data,
                                          gsize size)
{
  SalutAvahiContactPrivate *priv = SALUT_AVAHI_CONTACT_GET_PRIVATE (self);

  if (priv->record_browser != NULL)
    g_object_unref (priv->record_browser);
  priv->record_browser = NULL;

  salut_contact_avatar_request_flush (SALUT_CONTACT (self), data, size);
}

static void
salut_contact_avatar_all_for_now (GaRecordBrowser *browser,
                                  SalutAvahiContact *self)
{
  DEBUG ("All for now for resolving %s's record", SALUT_CONTACT (self)->name);
  salut_avahi_contact_avatar_request_flush (self, NULL, 0);
}

static void
salut_contact_avatar_failure (GaRecordBrowser *browser,
                              GError *error,
                              SalutAvahiContact *self)
{

  DEBUG ("Resolving record for %s failed: %s", SALUT_CONTACT(self)->name,
      error->message);

  salut_avahi_contact_avatar_request_flush (self, NULL, 0);
}

static void
salut_contact_avatar_found (GaRecordBrowser *browser,
                            AvahiIfIndex interface,
                            AvahiProtocol protocol,
                            gchar *name,
                            guint16 clazz,
                            guint16 type,
                            guint8 *rdata,
                            gsize rdata_size,
                            AvahiLookupFlags flags,
                            SalutAvahiContact *self)
{
  DEBUG ("Found avatar for %s for size %" G_GSIZE_FORMAT,
      SALUT_CONTACT (self)->name, rdata_size);

  if (rdata_size <= 0)
    salut_avahi_contact_avatar_request_flush (self, NULL, 0);
  else
    salut_avahi_contact_avatar_request_flush (self, rdata, rdata_size);
}

static void
salut_avahi_contact_retrieve_avatar (SalutContact *contact)
{
  SalutAvahiContact *self = SALUT_AVAHI_CONTACT (contact);
  SalutAvahiContactPrivate *priv = SALUT_AVAHI_CONTACT_GET_PRIVATE (self);
  gchar *name;
  GError *error = NULL;

  if (priv->record_browser != NULL)
    {
      g_object_unref (priv->record_browser);
      priv->record_browser = NULL;
    }

  if (contact->avatar_token == NULL)
    {
      salut_avahi_contact_avatar_request_flush (self, NULL, 0);
      return;
    }

  name = g_strdup_printf ("%s." SALUT_DNSSD_PRESENCE ".local", contact->name);
  priv->record_browser = ga_record_browser_new (name, 0xA);
  g_free (name);

  g_signal_connect (priv->record_browser, "new-record",
      G_CALLBACK (salut_contact_avatar_found), contact);
  g_signal_connect (priv->record_browser, "all-for-now",
      G_CALLBACK (salut_contact_avatar_all_for_now), contact);
  g_signal_connect (priv->record_browser, "failure",
      G_CALLBACK (salut_contact_avatar_failure), contact);

  if (!ga_record_browser_attach (priv->record_browser,
      priv->discovery_client->avahi_client, &error))
    {
      DEBUG ("browser attached failed: %s", error->message);
      g_error_free (error);

      salut_avahi_contact_avatar_request_flush (self, NULL, 0);
    }
}

static void salut_avahi_contact_dispose (GObject *object);

static void
salut_avahi_contact_class_init (
    SalutAvahiContactClass *salut_avahi_contact_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_avahi_contact_class);
  SalutContactClass *contact_class = SALUT_CONTACT_CLASS (
      salut_avahi_contact_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_avahi_contact_class,
                              sizeof (SalutAvahiContactPrivate));

  object_class->get_property = salut_avahi_contact_get_property;
  object_class->set_property = salut_avahi_contact_set_property;

  object_class->dispose = salut_avahi_contact_dispose;

  contact_class->get_addresses = salut_avahi_contact_get_addresses;
  contact_class->has_address = salut_avahi_contact_has_address;
  contact_class->retrieve_avatar = salut_avahi_contact_retrieve_avatar;

  param_spec = g_param_spec_object (
      "discovery-client",
      "SalutAvahiDiscoveryClient object",
      "The Salut Avahi Discovery client associated with this muc manager",
      SALUT_TYPE_AVAHI_DISCOVERY_CLIENT,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CLIENT,
      param_spec);
}

void
salut_avahi_contact_dispose (GObject *object)
{
  SalutAvahiContact *self = SALUT_AVAHI_CONTACT (object);
  SalutAvahiContactPrivate *priv = SALUT_AVAHI_CONTACT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->presence_resolver_failed_timer > 0)
    {
      g_source_remove (priv->presence_resolver_failed_timer);
      priv->presence_resolver_failed_timer = 0;
    }

  g_slist_foreach (priv->resolvers, (GFunc) g_object_unref, NULL);
  g_slist_free (priv->resolvers);
  priv->resolvers = NULL;

  if (priv->discovery_client != NULL)
    {
      g_object_unref (priv->discovery_client);
      priv->discovery_client = NULL;
    }

  if (G_OBJECT_CLASS (salut_avahi_contact_parent_class)->dispose)
    G_OBJECT_CLASS (salut_avahi_contact_parent_class)->dispose (object);
}

/* public functions */
SalutAvahiContact *
salut_avahi_contact_new (SalutConnection *connection,
                         const gchar *name,
                         SalutAvahiDiscoveryClient *discovery_client)
{
  g_assert (connection != NULL);
  g_assert (name != NULL);
  g_assert (discovery_client != NULL);

  return g_object_new (SALUT_TYPE_AVAHI_CONTACT,
      "connection", connection,
      "name", name,
      "discovery-client", discovery_client,
      NULL);
}

static void
contact_drop_resolver (SalutAvahiContact *self,
                       GaServiceResolver *resolver)
{
  SalutAvahiContactPrivate *priv = SALUT_AVAHI_CONTACT_GET_PRIVATE (self);
  SalutContact *contact = SALUT_CONTACT (self);
  gint resolvers_left;

  priv->resolvers = g_slist_remove (priv->resolvers, resolver);

  resolvers_left = g_slist_length (priv->resolvers);

  DEBUG_RESOLVER (self, resolver, "removed, %d left for %s", resolvers_left,
     SALUT_CONTACT (self)->name);

  g_object_unref (resolver);

  if (resolvers_left == 0)
    {
      salut_contact_lost (contact);
    }
}

struct resolverinfo {
  AvahiIfIndex interface;
  AvahiProtocol protocol;
  const gchar *name;
  const gchar *type;
  const gchar *domain;
};

static gint
compare_resolver (GaServiceResolver *a,
                  struct resolverinfo *info)
{
  AvahiIfIndex interface;
  AvahiProtocol protocol;
  gchar *name;
  gchar *type;
  gchar *domain;
  gint result;

  g_object_get (a,
      "interface", &interface,
       "protocol", &protocol,
       "name", &name,
       "type", &type,
       "domain", &domain,
       NULL);

  if (interface == info->interface
      && protocol == info->protocol
      && !tp_strdiff (name, info->name)
      && !tp_strdiff (type, info->type)
      && !tp_strdiff (domain, info->domain))
    result = 0;
  else
    result = 1;

  g_free (name);
  g_free (type);
  g_free (domain);
  return result;
}

static GaServiceResolver *
find_resolver (SalutAvahiContact *contact,
               AvahiIfIndex interface, AvahiProtocol protocol,
               const gchar *name,
               const gchar *type,
               const gchar *domain)
{
  SalutAvahiContactPrivate *priv = SALUT_AVAHI_CONTACT_GET_PRIVATE (contact);
  struct resolverinfo info;
  GSList *ret;
  info.interface = interface;
  info.protocol = protocol;
  info.name = name;
  info.type = type;
  info.domain = domain;
  ret = g_slist_find_custom (priv->resolvers, &info,
      (GCompareFunc) compare_resolver);
  return ret ? GA_SERVICE_RESOLVER (ret->data) : NULL;
}

static void
update_alias (SalutAvahiContact *self,
              const gchar *nick,
              const gchar *first,
              const gchar *last)
{
#define STREMPTY(x) (x == NULL || *x == '\0')

  if (!STREMPTY(nick))
    {
      salut_contact_change_alias (SALUT_CONTACT (self), nick);
      return;
    }

  if (!STREMPTY(first) && !STREMPTY(last))
    {
      gchar *s = g_strdup_printf ("%s %s", first, last);

      salut_contact_change_alias (SALUT_CONTACT (self), s);

      g_free (s);
      return;
    }

  if (!STREMPTY(first))
    {
      salut_contact_change_alias (SALUT_CONTACT (self), first);
      return;
    }

  if (!STREMPTY(last))
    {
      salut_contact_change_alias (SALUT_CONTACT (self), last);
      return;
    }

  salut_contact_change_alias (SALUT_CONTACT (self), NULL);

#undef STREMPTY
}


/* Returned string needs to be freed with avahi_free ! */
static char *
_avahi_txt_get_keyval_with_size (AvahiStringList *txt,
    const gchar *key, gsize *size)
{
  AvahiStringList *t;
  gchar *s = NULL;


  if ((t = avahi_string_list_find (txt, key)) == NULL)
    return NULL;

  avahi_string_list_get_pair (t, NULL, &s, size);

  return s;
}

static char *
_avahi_txt_get_keyval (AvahiStringList *txt, const gchar *key)
{
  return _avahi_txt_get_keyval_with_size (txt, key, NULL);
}

static void
contact_resolved_cb (GaServiceResolver *resolver,
                     AvahiIfIndex interface,
                     AvahiProtocol protocol,
                     gchar *name,
                     gchar *type,
                     gchar *domain,
                     gchar *host_name,
                     AvahiAddress *address,
                     gint port,
                     AvahiStringList *txt,
                     AvahiLookupResultFlags flags,
                     SalutAvahiContact *self)
{
  SalutAvahiContactPrivate *priv = SALUT_AVAHI_CONTACT_GET_PRIVATE (self);
  SalutContact *contact = SALUT_CONTACT (self);
  char *s;
  char *nick, *first, *last;
#ifdef ENABLE_OLPC
  char *activity_id, *room_id;
  char *olpc_key_part;
  gsize size;
#endif

  DEBUG_RESOLVER (self, resolver, "contact %s resolved", contact->name);

  if (priv->presence_resolver_failed_timer != 0)
    {
      DEBUG_CONTACT (self, "remove presence resolver timer");
      g_source_remove (priv->presence_resolver_failed_timer);
      priv->presence_resolver_failed_timer = 0;
    }

  salut_contact_freeze (contact);

  /* status */

  if ((s = _avahi_txt_get_keyval (txt, "status")) != NULL)
    {
      int i;
      for (i = 0; i < SALUT_PRESENCE_NR_PRESENCES ; i++)
        {
          if (!tp_strdiff (s, salut_presence_status_txt_names[i]))
            {
              salut_contact_change_status (contact, i);
              break;
            }
        }
      avahi_free (s);
    }

  /* status message */
  s = _avahi_txt_get_keyval (txt, "msg");
  salut_contact_change_status_message (contact, s);
  avahi_free (s);

  /* nick */
  nick = _avahi_txt_get_keyval (txt, "nick");
  first = _avahi_txt_get_keyval (txt, "1st");
  last = _avahi_txt_get_keyval (txt, "last");

  update_alias (self, nick, first, last);
  avahi_free (nick);
  avahi_free (first);
  avahi_free (last);

  /* avatar token */
  s = _avahi_txt_get_keyval (txt, "phsh");
  salut_contact_change_avatar_token (contact, s);
  avahi_free (s);

  /* jid */
#ifdef ENABLE_OLPC
  s = _avahi_txt_get_keyval (txt, "jid");
  salut_contact_change_jid (contact, s);
  avahi_free (s);

  /* OLPC color */
  s = _avahi_txt_get_keyval (txt, "olpc-color");
  salut_contact_change_olpc_color (contact, s);
  avahi_free (s);

  /* current activity */
  activity_id = _avahi_txt_get_keyval (txt, "olpc-current-activity");
  room_id = _avahi_txt_get_keyval (txt, "olpc-current-activity-room");

  salut_contact_change_current_activity (contact, room_id, activity_id);
  avahi_free (activity_id);
  avahi_free (room_id);

  /* OLPC key */
  olpc_key_part = _avahi_txt_get_keyval_with_size (txt,
      "olpc-key-part0", &size);

  if (olpc_key_part != NULL)
    {
      guint i = 0;
      gchar *name = NULL;
      GArray *olpc_key;

      /* FIXME: how big are OLPC keys anyway? */
      olpc_key = g_array_sized_new (FALSE, FALSE, sizeof (guint8), 512);

      do
        {
          g_array_append_vals (olpc_key, olpc_key_part, size);
          avahi_free (olpc_key_part);

          i++;
          name = g_strdup_printf ("olpc-key-part%u", i);
          olpc_key_part = _avahi_txt_get_keyval_with_size (txt, name,
              &size);
          g_free (name);
        }
      while (olpc_key_part != NULL);

      salut_contact_change_olpc_key (contact, olpc_key);
      g_array_free (olpc_key, TRUE);
    }

  /* address */
  if (address != NULL)
    {
      gchar* saddr = g_malloc0 (AVAHI_ADDRESS_STR_MAX);

      if (avahi_address_snprint (saddr, AVAHI_ADDRESS_STR_MAX, address))
        {
          switch (address->proto)
            {
              case AVAHI_PROTO_INET:
                salut_contact_change_ipv4_addr (contact, saddr);
                break;
              case AVAHI_PROTO_INET6:
                salut_contact_change_ipv6_addr (contact, saddr);
                break;
              default:
                break;
            }
        }
      g_free (saddr);
    }
#endif

  salut_contact_found (contact);
  salut_contact_thaw (contact);
}

static gboolean
presence_resolver_failed_timeout (SalutAvahiContact *self)
{
  SalutAvahiContactPrivate *priv = SALUT_AVAHI_CONTACT_GET_PRIVATE (self);

  DEBUG_CONTACT (self, "presence resolver timer expired. Remove contact");
  priv->presence_resolver_failed_timer = 0;
  salut_contact_lost (SALUT_CONTACT (self));

  return FALSE;
}

static void
contact_failed_cb (GaServiceResolver *resolver,
                   GError *error,
                   SalutAvahiContact *self)
{
  SalutAvahiContactPrivate *priv = SALUT_AVAHI_CONTACT_GET_PRIVATE (self);

  if (priv->presence_resolver_failed_timer != 0)
    /* There is already a timer running */
    return;

  DEBUG_RESOLVER (self, resolver, "failed: %s. Start presence resolver timer",
      error->message);

  priv->presence_resolver_failed_timer = g_timeout_add (
      PRESENCE_TIMEOUT, (GSourceFunc) presence_resolver_failed_timeout,
      self);
}

gboolean
salut_avahi_contact_add_service (SalutAvahiContact *self,
                                 AvahiIfIndex interface,
                                 AvahiProtocol protocol,
                                 const char *name,
                                 const char *type,
                                 const char *domain)
{
  SalutAvahiContactPrivate *priv = SALUT_AVAHI_CONTACT_GET_PRIVATE (self);
  GaServiceResolver *resolver;
  GError *error = NULL;

  resolver = find_resolver (self, interface, protocol, name, type, domain);
  if (resolver != NULL)
    return TRUE;

  resolver = ga_service_resolver_new (interface, protocol, name, type, domain,
      protocol, 0);

  g_signal_connect (resolver, "found", G_CALLBACK (contact_resolved_cb),
      self);
  g_signal_connect (resolver, "failure", G_CALLBACK (contact_failed_cb),
      self);

  if (!ga_service_resolver_attach (resolver,
        priv->discovery_client->avahi_client, &error))
    {
      DEBUG_CONTACT(self, "Failed to attach resolver: %s", error->message);
      g_error_free (error);
      return FALSE;
    }

  DEBUG_RESOLVER (self, resolver, "added");
  priv->resolvers = g_slist_prepend (priv->resolvers, resolver);

  return TRUE;
}

void
salut_avahi_contact_remove_service (SalutAvahiContact *self,
                                    AvahiIfIndex interface,
                                    AvahiProtocol protocol,
                                    const char *name,
                                    const char *type,
                                    const char *domain)
{
  GaServiceResolver *resolver;

  resolver =  find_resolver (self, interface, protocol, name, type, domain);
  if (resolver == NULL)
    return;

  DEBUG_RESOLVER (self, resolver, "remove requested");

  contact_drop_resolver (self, resolver);
}

gboolean
salut_avahi_contact_has_services (SalutAvahiContact *self)
{
  SalutAvahiContactPrivate *priv = SALUT_AVAHI_CONTACT_GET_PRIVATE (self);

  return priv->resolvers != NULL;
}
