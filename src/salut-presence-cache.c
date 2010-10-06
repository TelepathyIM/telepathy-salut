/*
 * salut-presence-cache.c - Salut's contact presence cache
 * Copyright (C) 2005-2008 Collabora Ltd.
 * Copyright (C) 2005-2008 Nokia Corporation
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
#include "salut-presence-cache.h"

#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include <gibber/gibber-namespaces.h>
#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/intset.h>

#define DEBUG_FLAG DEBUG_PRESENCE

#include "debug.h"
#include "salut-caps-channel-manager.h"
#include "salut-caps-hash.h"
#include "salut-disco.h"
#include "salut-signals-marshal.h"

G_DEFINE_TYPE (SalutPresenceCache, salut_presence_cache, G_TYPE_OBJECT);

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

/* signal enum */
enum
{
  CAPABILITIES_UPDATE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

#define SALUT_PRESENCE_CACHE_PRIV(cache) ((cache)->priv)

struct _SalutPresenceCachePrivate
{
  SalutConnection *conn;

  /* gchar *uri -> CapabilityInfo */
  GHashTable *capabilities;

  /* gchar *uri -> GSList* of DiscoWaiter* */
  GHashTable *disco_pending;

  guint caps_serial;

  gboolean dispose_has_run;
};

typedef struct _DiscoWaiter DiscoWaiter;

struct _DiscoWaiter
{
  SalutContact *contact;
  gchar *hash;
  gchar *ver;
  /* if a discovery request fails, we will ask another contact */
  gboolean disco_requested;
};

static DiscoWaiter *
disco_waiter_new (SalutContact *contact,
                  const gchar *hash,
                  const gchar *ver)
{
  DiscoWaiter *waiter;

  g_object_ref (contact);

  waiter = g_slice_new0 (DiscoWaiter);
  waiter->contact = contact;
  waiter->hash = g_strdup (hash);
  waiter->ver = g_strdup (ver);

  DEBUG ("created waiter %p for contact %s", waiter, contact->name);

  return waiter;
}

static void
disco_waiter_free (DiscoWaiter *waiter)
{
  g_assert (NULL != waiter);

  DEBUG ("freeing waiter %p for contact %s", waiter,
      waiter->contact->name);

  g_object_unref (waiter->contact);
  g_free (waiter->hash);
  g_free (waiter->ver);
  g_slice_free (DiscoWaiter, waiter);
}

static void
disco_waiter_list_free (GSList *list)
{
  GSList *i;

  DEBUG ("list %p", list);

  for (i = list; NULL != i; i = i->next)
    disco_waiter_free ((DiscoWaiter *) i->data);

  g_slist_free (list);
}

typedef struct _CapabilityInfo CapabilityInfo;

/* struct _CapabilityInfo can be allocated before receiving the contact's
 * caps. In this case, its members are NULL, and are set when the caps are
 * received */
struct _CapabilityInfo
{
  /* key: SalutCapsChannelFactory -> value: gpointer
   *
   * The type of the value depends on the SalutCapsChannelFactory. It is an
   * opaque pointer used by the channel manager to store the capabilities.
   * Some channel manager do not need to store anything, in this case the
   * value can just be NULL.
   *
   * Since the type of the value is not public, the value is allocated, copied
   * and freed by helper functions on the SalutCapsChannelManager interface.
   *
   * For example:
   *   * SalutPrivateTubesFactory -> TubesCapabilities
   *
   * At the moment, only SalutPrivateTubesFactory use this mechanism to store
   * the list of supported tube types (example: stream tube for daap).
   */
  GHashTable *per_channel_manager_caps;
};

static CapabilityInfo *
capability_info_get (SalutPresenceCache *cache, const gchar *uri)
{
  SalutPresenceCachePrivate *priv = SALUT_PRESENCE_CACHE_PRIV (cache);
  CapabilityInfo *info = g_hash_table_lookup (priv->capabilities, uri);

  if (NULL == info)
    {
      info = g_slice_new0 (CapabilityInfo);
      info->per_channel_manager_caps = NULL;
      g_hash_table_insert (priv->capabilities, g_strdup (uri), info);
    }

  return info;
}

static void
capability_info_free (CapabilityInfo *info)
{
  salut_presence_cache_free_cache_entry (info->per_channel_manager_caps);
  info->per_channel_manager_caps = NULL;
  g_slice_free (CapabilityInfo, info);
}

static void salut_presence_cache_init (SalutPresenceCache *presence_cache);
static GObject * salut_presence_cache_constructor (GType type, guint n_props,
    GObjectConstructParam *props);
static void salut_presence_cache_dispose (GObject *object);
static void salut_presence_cache_finalize (GObject *object);
static void salut_presence_cache_set_property (GObject *object, guint
    property_id, const GValue *value, GParamSpec *pspec);
static void salut_presence_cache_get_property (GObject *object, guint
    property_id, GValue *value, GParamSpec *pspec);

static void
salut_presence_cache_class_init (SalutPresenceCacheClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  g_type_class_add_private (object_class, sizeof (SalutPresenceCachePrivate));

  object_class->constructor = salut_presence_cache_constructor;

  object_class->dispose = salut_presence_cache_dispose;
  object_class->finalize = salut_presence_cache_finalize;

  object_class->get_property = salut_presence_cache_get_property;
  object_class->set_property = salut_presence_cache_set_property;

  param_spec = g_param_spec_object ("connection", "SalutConnection object",
                                    "Salut connection object that owns this "
                                    "presence cache.",
                                    SALUT_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class,
                                   PROP_CONNECTION,
                                   param_spec);

  signals[CAPABILITIES_UPDATE] = g_signal_new (
    "capabilities-update",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    salut_signals_marshal_VOID__UINT_POINTER_POINTER, G_TYPE_NONE,
    3, G_TYPE_UINT, G_TYPE_POINTER, G_TYPE_POINTER);
}

static void
salut_presence_cache_init (SalutPresenceCache *cache)
{
  SalutPresenceCachePrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (cache,
      SALUT_TYPE_PRESENCE_CACHE, SalutPresenceCachePrivate);

  cache->priv = priv;

  priv->capabilities = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) capability_info_free);
  priv->disco_pending = g_hash_table_new_full (g_str_hash, g_str_equal,
    g_free, (GDestroyNotify) disco_waiter_list_free);
  priv->caps_serial = 1;
}

static GObject *
salut_presence_cache_constructor (GType type, guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj;

  obj = G_OBJECT_CLASS (salut_presence_cache_parent_class)->
           constructor (type, n_props, props);

  return obj;
}

static void
salut_presence_cache_dispose (GObject *object)
{
  SalutPresenceCache *self = SALUT_PRESENCE_CACHE (object);
  SalutPresenceCachePrivate *priv = SALUT_PRESENCE_CACHE_PRIV (self);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");

  priv->dispose_has_run = TRUE;

  g_hash_table_destroy (priv->capabilities);
  priv->capabilities = NULL;

  g_hash_table_destroy (priv->disco_pending);
  priv->disco_pending = NULL;

  if (G_OBJECT_CLASS (salut_presence_cache_parent_class)->dispose)
    G_OBJECT_CLASS (salut_presence_cache_parent_class)->dispose (object);
}

static void
salut_presence_cache_finalize (GObject *object)
{
  DEBUG ("called with %p", object);

  G_OBJECT_CLASS (salut_presence_cache_parent_class)->finalize (object);
}

static void
salut_presence_cache_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  SalutPresenceCache *cache = SALUT_PRESENCE_CACHE (object);
  SalutPresenceCachePrivate *priv = SALUT_PRESENCE_CACHE_PRIV (cache);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_presence_cache_set_property (GObject     *object,
                                    guint        property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  SalutPresenceCache *cache = SALUT_PRESENCE_CACHE (object);
  SalutPresenceCachePrivate *priv = SALUT_PRESENCE_CACHE_PRIV (cache);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GHashTable *
create_per_channel_manager_caps (SalutPresenceCache *self,
                                 GibberXmppNode *query_result)
{
  SalutPresenceCachePrivate *priv = SALUT_PRESENCE_CACHE_PRIV (self);
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->conn);
  GHashTable *per_channel_manager_caps;
  TpChannelManagerIter iter;
  TpChannelManager *manager;

  per_channel_manager_caps = g_hash_table_new (NULL, NULL);

  /* parsing for Connection.Interface.ContactCapabilities.DRAFT */
  tp_base_connection_channel_manager_iter_init (&iter, base_conn);
  while (tp_base_connection_channel_manager_iter_next (&iter, &manager))
    {
      gpointer *factory_caps;

      /* all channel managers must implement the capability interface */
      g_assert (SALUT_IS_CAPS_CHANNEL_MANAGER (manager));

      factory_caps = salut_caps_channel_manager_parse_capabilities
          (SALUT_CAPS_CHANNEL_MANAGER (manager), query_result);
      if (factory_caps != NULL)
        g_hash_table_insert (per_channel_manager_caps,
            SALUT_CAPS_CHANNEL_MANAGER (manager), factory_caps);
    }

  return per_channel_manager_caps;
}

static void
_caps_disco_cb (SalutDisco *disco,
                SalutDiscoRequest *request,
                SalutContact *contact,
                const gchar *node,
                GibberXmppNode *query_result,
                GError *error,
                gpointer user_data)
{
  GSList *waiters, *i;
  DiscoWaiter *waiter_self;
  SalutPresenceCache *cache;
  SalutPresenceCachePrivate *priv;
  TpHandleRepoIface *contact_repo;
  gboolean bad_hash = FALSE;
  TpBaseConnection *base_conn;
  CapabilityInfo *info = NULL;

  cache = SALUT_PRESENCE_CACHE (user_data);
  priv = SALUT_PRESENCE_CACHE_PRIV (cache);
  base_conn = TP_BASE_CONNECTION (priv->conn);
  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  if (NULL == node)
    {
      DEBUG ("got disco response with NULL node, ignoring");
      return;
    }

  waiters = g_hash_table_lookup (priv->disco_pending, node);

  if (NULL != error)
    {
      DiscoWaiter *waiter = NULL;

      DEBUG ("disco query failed: %s", error->message);

      for (i = waiters; NULL != i; i = i->next)
        {
          waiter = (DiscoWaiter *) i->data;

          if (!waiter->disco_requested)
            {
              salut_disco_request (disco, SALUT_DISCO_TYPE_INFO,
                  waiter->contact, node, _caps_disco_cb, cache,
                  G_OBJECT(cache), NULL);
              waiter->disco_requested = TRUE;
              break;
            }
        }

      if (NULL != i)
        {
          DEBUG ("sent a retry disco request to %s for URI %s",
              contact->name, node);
        }
      else
        {
          /* The contact sends us an error and we don't have any other
           * contacts to send the discovery request on the same node. We
           * cannot get the caps for this node. */
          DEBUG ("failed to find a suitable candidate to retry disco "
              "request for URI %s", node);
          g_hash_table_remove (priv->disco_pending, node);
        }

      return;
    }

  waiter_self = NULL;
  for (i = waiters; NULL != i;  i = i->next)
    {
      DiscoWaiter *waiter;

      waiter = (DiscoWaiter *) i->data;
      if (waiter->contact == contact)
        {
          waiter_self = waiter;
          break;
        }
    }
  if (NULL == waiter_self)
    {
      DEBUG ("Ignoring non requested disco reply");
      return;
    }

  /* Only 'sha-1' is mandatory to implement by XEP-0115. If the remote contact
   * uses another hash algorithm, don't check the hash and fallback to the old
   * method. The hash method is not included in the discovery request nor
   * response but we saved it in disco_pending when we received the presence
   * stanza. */
  if (!tp_strdiff (waiter_self->hash, "sha-1"))
    {
      gchar *computed_hash;

      computed_hash = caps_hash_compute_from_stanza (query_result);

      if (!g_str_equal (waiter_self->ver, computed_hash))
        bad_hash = TRUE;

      if (!bad_hash)
        {
          info = capability_info_get (cache, node);

          if (info->per_channel_manager_caps == NULL)
            {
              info->per_channel_manager_caps =
                create_per_channel_manager_caps (cache, query_result);
            }
        }
      else
        {
          /* The received reply does not match the */
          DEBUG ("The announced verification string '%s' does not match "
              "our hash '%s'.", waiter_self->ver, computed_hash);
        }

      g_free (computed_hash);
    }
  else
    {
      /* Do not allow tubes caps if the contact does not observe XEP-0115
       * version 1.5: we don't need to bother being compatible with both version
       * 1.3 and tubes caps */
      DEBUG ("Unsupported hash algorithm, ignoring caps: %s",
          waiter_self->hash == NULL ? "(none)" : waiter_self->hash);
    }

  for (i = waiters; NULL != i;)
    {
      DiscoWaiter *waiter;

      waiter = (DiscoWaiter *) i->data;

      if (!bad_hash || waiter->contact == contact)
        {
          GSList *tmp;
          gpointer key;
          gpointer value;

          if (!bad_hash)
            {
              GHashTable *save_enhanced_caps;

              save_enhanced_caps = salut_presence_cache_copy_cache_entry (
                  waiter->contact->per_channel_manager_caps);

              DEBUG ("setting caps for %s (thanks to %s)",
                  waiter->contact->name, contact->name);
              salut_contact_set_capabilities (waiter->contact,
                  info == NULL ? NULL : info->per_channel_manager_caps);
              g_signal_emit (cache, signals[CAPABILITIES_UPDATE], 0,
                contact->handle, save_enhanced_caps,
                waiter->contact->per_channel_manager_caps);
              salut_presence_cache_free_cache_entry (save_enhanced_caps);
            }

          tmp = i;
          i = i->next;

          waiters = g_slist_delete_link (waiters, tmp);

          if (!g_hash_table_lookup_extended (priv->disco_pending, node, &key,
                &value))
            g_assert_not_reached ();

          g_hash_table_steal (priv->disco_pending, node);
          g_hash_table_insert (priv->disco_pending, key, waiters);

          disco_waiter_free (waiter);
        }
      else
        {
          /* if the possible trust, not counting this guy, is too low,
           * we have been poisoned and reset our trust meters - disco
           * anybody we still haven't to be able to get more trusted replies */

          if (!waiter->disco_requested)
            {
              salut_disco_request (disco, SALUT_DISCO_TYPE_INFO,
                  waiter->contact, node, _caps_disco_cb, cache,
                  G_OBJECT(cache), NULL);
              waiter->disco_requested = TRUE;
            }

          i = i->next;
        }
    }

  if (!bad_hash)
    g_hash_table_remove (priv->disco_pending, node);
}


/* Called when the contact update its hash, node and ver txt records
 * If theses variables are absent from the record, the parameters are NULL
 */
void
salut_presence_cache_process_caps (SalutPresenceCache *self,
                                   SalutContact *contact,
                                   const gchar *hash,
                                   const gchar *node,
                                   const gchar *ver)
{
  gchar *uri;
  SalutPresenceCachePrivate *priv;
  CapabilityInfo *info;
  GHashTable *per_channel_manager_caps;
  gboolean caps_set;
  gboolean hash_table_created = FALSE;

  DEBUG ("Called for %s with '%s' '%s' '%s'",
    contact->name, hash, node, ver);

  priv = SALUT_PRESENCE_CACHE_PRIV (self);

  if (hash == NULL || node == NULL || ver == NULL ||
      tp_strdiff (hash, "sha-1"))
    {
      /* if the contact does not support capabilities, we consider the default
       * basic ones */
      caps_set = TRUE;
      /* get the default capabilities */
      per_channel_manager_caps = create_per_channel_manager_caps (self, NULL);
      hash_table_created = TRUE;
    }
  else
    {
      uri = g_strdup_printf ("%s#%s", node, ver);
      info = capability_info_get (self, uri);
      caps_set = (info->per_channel_manager_caps != NULL);
      per_channel_manager_caps = info->per_channel_manager_caps;
    }

  if (caps_set)
    {
      /* we have a caps to apply on the contact */
      GHashTable *save_enhanced_caps;

      DEBUG ("setting caps for %s", contact->name);

      save_enhanced_caps = salut_presence_cache_copy_cache_entry (
          contact->per_channel_manager_caps);

      DEBUG ("setting caps for %s (thanks to %s)",
          contact->name, contact->name);
      salut_contact_set_capabilities (contact, per_channel_manager_caps);
      g_signal_emit (self, signals[CAPABILITIES_UPDATE], 0,
          contact->handle, save_enhanced_caps,
          contact->per_channel_manager_caps);
      salut_presence_cache_free_cache_entry (save_enhanced_caps);
    }
  else
    {
      /* Append the contact to the list of such contacts waiting for
       * capabilities for this uri, and send a disco request if we don't
       * have enough possible trust yet */

      GSList *waiters;
      DiscoWaiter *waiter;
      gpointer key;
      gpointer value = NULL;

      /* If the URI is in the hash table, steal it and its value; we can
       * reuse the same URI for the following insertion. Otherwise, make a
       * copy of the URI for use as a key.
       */
      if (g_hash_table_lookup_extended (priv->disco_pending, uri, &key,
            &value))
        {
          g_hash_table_steal (priv->disco_pending, key);
        }
      else
        {
          key = g_strdup (uri);
        }

      waiters = (GSList *) value;
      waiter = disco_waiter_new (contact, hash, ver);
      waiters = g_slist_prepend (waiters, waiter);
      g_hash_table_insert (priv->disco_pending, key, waiters);


      if (!value)
        {
          /* Nobody was asked for this uri so far. Do it now. */
          salut_disco_request (priv->conn->disco, SALUT_DISCO_TYPE_INFO,
              contact, uri, _caps_disco_cb, self, G_OBJECT (self), NULL);
          waiter->disco_requested = TRUE;
        }
    }

  if (hash_table_created)
    g_hash_table_destroy (per_channel_manager_caps);
}

SalutPresenceCache *
salut_presence_cache_new (SalutConnection *connection)
{
  return g_object_new (SALUT_TYPE_PRESENCE_CACHE,
                       "connection", connection,
                       NULL);
}


/* helper functions */

static void
free_caps_helper (gpointer key, gpointer value, gpointer user_data)
{
  SalutCapsChannelManager *manager = SALUT_CAPS_CHANNEL_MANAGER (key);
  salut_caps_channel_manager_free_capabilities (manager, value);
}

void
salut_presence_cache_free_cache_entry (
    GHashTable *per_channel_manager_caps)
{
  if (per_channel_manager_caps == NULL)
    return;

  g_hash_table_foreach (per_channel_manager_caps, free_caps_helper,
      NULL);
  g_hash_table_destroy (per_channel_manager_caps);
}

static void
copy_caps_helper (gpointer key, gpointer value, gpointer user_data)
{
  GHashTable *table_out = user_data;
  SalutCapsChannelManager *manager = SALUT_CAPS_CHANNEL_MANAGER (key);
  gpointer out;
  salut_caps_channel_manager_copy_capabilities (manager, &out, value);
  g_hash_table_insert (table_out, key, out);
}

GHashTable *
salut_presence_cache_copy_cache_entry (GHashTable *in)
{
  GHashTable *out;

  out = g_hash_table_new (NULL, NULL);
  if (in != NULL)
    g_hash_table_foreach (in, copy_caps_helper,
        out);

  return out;
}

static void
update_caps_helper (gpointer key, gpointer value, gpointer user_data)
{
  GHashTable *table_out = user_data;
  SalutCapsChannelManager *manager = SALUT_CAPS_CHANNEL_MANAGER (key);
  gpointer out;

  out = g_hash_table_lookup (table_out, key);
  if (out == NULL)
    {
      salut_caps_channel_manager_copy_capabilities (manager, &out, value);
      g_hash_table_insert (table_out, key, out);
    }
  else
    {
      salut_caps_channel_manager_update_capabilities (manager, out, value);
    }
}

void
salut_presence_cache_update_cache_entry (
    GHashTable *out, GHashTable *in)
{
  g_hash_table_foreach (in, update_caps_helper,
      out);
}

