/*
 * avahi-discovery-client.c - Source for SalutAvahiDiscoveryClient
 * Copyright (C) 2007 Collabora Ltd.
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

#include "avahi-discovery-client.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

#include <glib.h>

#include <avahi-gobject/ga-client.h>

#define DEBUG_FLAG DEBUG_DISCOVERY
#include "debug.h"

#include "avahi-muc-manager.h"
#include "avahi-contact-manager.h"
#include "avahi-roomlist-manager.h"
#include "avahi-self.h"
#ifdef ENABLE_OLPC
#include "avahi-olpc-activity-manager.h"
#endif

#include "presence.h"
#include "signals-marshal.h"

static void
discovery_client_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (SalutAvahiDiscoveryClient, salut_avahi_discovery_client,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (SALUT_TYPE_DISCOVERY_CLIENT,
      discovery_client_init));

/* signals */
enum
{
  STATE_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_STATE = 1,
  PROP_DNSSD_NAME,
  LAST_PROPERTY
};

typedef struct _SalutAvahiDiscoveryClientPrivate \
          SalutAvahiDiscoveryClientPrivate;
struct _SalutAvahiDiscoveryClientPrivate
{
  SalutDiscoveryClientState state;

  gchar *dnssd_name;

  gboolean dispose_has_run;
};

#define SALUT_AVAHI_DISCOVERY_CLIENT_GET_PRIVATE(obj) \
    ((SalutAvahiDiscoveryClientPrivate *) \
      ((SalutAvahiDiscoveryClient *) obj)->priv)

static void
salut_avahi_discovery_client_init (SalutAvahiDiscoveryClient *self)
{
  SalutAvahiDiscoveryClientPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_AVAHI_DISCOVERY_CLIENT, SalutAvahiDiscoveryClientPrivate);

  self->priv = priv;
  priv->dispose_has_run = FALSE;

  priv->state = SALUT_DISCOVERY_CLIENT_STATE_DISCONNECTED;
}

static void
change_state (SalutAvahiDiscoveryClient *self,
              SalutDiscoveryClientState state)
{
  SalutAvahiDiscoveryClientPrivate *priv =
    SALUT_AVAHI_DISCOVERY_CLIENT_GET_PRIVATE (self);

  if (priv->state == state)
    return;

  priv->state = state;
  g_signal_emit (G_OBJECT (self), signals[STATE_CHANGED], 0, state);
}

static void
disconnect_client (SalutAvahiDiscoveryClient *self)
{
  change_state (self, SALUT_DISCOVERY_CLIENT_STATE_DISCONNECTING);
  change_state (self, SALUT_DISCOVERY_CLIENT_STATE_DISCONNECTED);
}

static void
salut_avahi_discovery_client_dispose (GObject *object)
{
  SalutAvahiDiscoveryClient *self = SALUT_AVAHI_DISCOVERY_CLIENT (object);
  SalutAvahiDiscoveryClientPrivate *priv =
    SALUT_AVAHI_DISCOVERY_CLIENT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (self->avahi_client != NULL)
    {
      disconnect_client (self);
      g_object_unref (self->avahi_client);
      self->avahi_client = NULL;
    }

  tp_clear_pointer (&priv->dnssd_name, g_free);

  G_OBJECT_CLASS (salut_avahi_discovery_client_parent_class)->dispose (object);
}

static void
salut_avahi_discovery_client_get_property (GObject *object,
                                           guint property_id,
                                           GValue *value,
                                           GParamSpec *pspec)
{
  SalutAvahiDiscoveryClient *self = SALUT_AVAHI_DISCOVERY_CLIENT (object);
  SalutAvahiDiscoveryClientPrivate *priv =
    SALUT_AVAHI_DISCOVERY_CLIENT_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_STATE:
        g_value_set_uint (value, priv->state);
        break;
      case PROP_DNSSD_NAME:
        g_value_set_string (value, priv->dnssd_name);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_avahi_discovery_client_set_property (GObject *object,
                                           guint property_id,
                                           const GValue *value,
                                           GParamSpec *pspec)
{
  SalutAvahiDiscoveryClient *self = SALUT_AVAHI_DISCOVERY_CLIENT (object);
  SalutAvahiDiscoveryClientPrivate *priv =
    SALUT_AVAHI_DISCOVERY_CLIENT_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_DNSSD_NAME:
        priv->dnssd_name = g_value_dup_string (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_avahi_discovery_client_class_init (
    SalutAvahiDiscoveryClientClass *salut_avahi_discovery_client_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (
      salut_avahi_discovery_client_class);

  g_type_class_add_private (salut_avahi_discovery_client_class,
      sizeof (SalutAvahiDiscoveryClientPrivate));

  object_class->dispose = salut_avahi_discovery_client_dispose;

  object_class->get_property = salut_avahi_discovery_client_get_property;
  object_class->set_property = salut_avahi_discovery_client_set_property;

  g_object_class_override_property (object_class, PROP_STATE,
      "state");
  g_object_class_override_property (object_class, PROP_DNSSD_NAME,
      "dnssd-name");

  signals[STATE_CHANGED] =
    g_signal_new ("state-changed",
                  G_OBJECT_CLASS_TYPE (salut_avahi_discovery_client_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_signals_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);
}

static void
_ga_client_running_cb (GaClient *c,
                       GaClientState state,
                       SalutAvahiDiscoveryClient *self)
{
  change_state (self, SALUT_DISCOVERY_CLIENT_STATE_CONNECTED);
}

static void
_ga_client_failure_cb (GaClient *c,
                       GaClientState state,
                       SalutAvahiDiscoveryClient *self)
{
  disconnect_client (self);
}

/*
 * salut_avahi_discovery_client_start
 *
 * Implements salut_discovery_client_start on SalutDiscoveryClient
 */
static gboolean
salut_avahi_discovery_client_start (SalutDiscoveryClient *client,
                                    GError **error)
{
  SalutAvahiDiscoveryClient *self = SALUT_AVAHI_DISCOVERY_CLIENT (client);

  self->avahi_client = ga_client_new (GA_CLIENT_FLAG_NO_FAIL);

  g_signal_connect (self->avahi_client, "state-changed::running",
      G_CALLBACK (_ga_client_running_cb), self);
  g_signal_connect (self->avahi_client, "state-changed::failure",
      G_CALLBACK (_ga_client_failure_cb), self);

  change_state (self, SALUT_DISCOVERY_CLIENT_STATE_CONNECTING);
  return ga_client_start (self->avahi_client, error);
}

/*
 * salut_avahi_discovery_client_create_muc_manager
 *
 * Implements salut_discovery_client_create_muc_manager on SalutDiscoveryClient
 */
static SalutMucManager *
salut_avahi_discovery_client_create_muc_manager (SalutDiscoveryClient *client,
                                                 SalutConnection *connection)
{
  SalutAvahiDiscoveryClient *self = SALUT_AVAHI_DISCOVERY_CLIENT (client);

  return SALUT_MUC_MANAGER (salut_avahi_muc_manager_new (connection,
      self));
}

/*
 * salut_avahi_discovery_client_create_roomlist_manager
 *
 * Implements salut_discovery_client_create_roomlist_manager on
 * SalutDiscoveryClient
 */
static SalutRoomlistManager *
salut_avahi_discovery_client_create_roomlist_manager (
    SalutDiscoveryClient *client,
    SalutConnection *connection)
{
  SalutAvahiDiscoveryClient *self = SALUT_AVAHI_DISCOVERY_CLIENT (client);

  return SALUT_ROOMLIST_MANAGER (salut_avahi_roomlist_manager_new (connection,
        self));
}

/*
 * salut_avahi_discovery_client_create_contact_manager
 *
 * Implements salut_discovery_client_create_contact_manager on
 * SalutDiscoveryClient
 */
static SalutContactManager *
salut_avahi_discovery_client_create_contact_manager (
    SalutDiscoveryClient *client,
    SalutConnection *connection)
{
  SalutAvahiDiscoveryClient *self = SALUT_AVAHI_DISCOVERY_CLIENT (client);

  return SALUT_CONTACT_MANAGER (salut_avahi_contact_manager_new (connection,
        self));
}

#ifdef ENABLE_OLPC
/*
 * salut_avahi_discovery_client_create_olpc_activity_manager
 *
 * Implements salut_discovery_client_create_olpc_activity_manager on
 * SalutDiscoveryClient
 */
static SalutOlpcActivityManager *
salut_avahi_discovery_client_create_olpc_activity_manager (
    SalutDiscoveryClient *client,
    SalutConnection *connection)
{
  SalutAvahiDiscoveryClient *self = SALUT_AVAHI_DISCOVERY_CLIENT (client);

  return SALUT_OLPC_ACTIVITY_MANAGER (salut_avahi_olpc_activity_manager_new (
        connection, self));
}
#endif

/*
 * salut_avahi_discovery_client_create_self
 *
 * Implements salut_discovery_client_create_self on SalutDiscoveryClient
 */
static SalutSelf *
salut_avahi_discovery_client_create_self (SalutDiscoveryClient *client,
                                          SalutConnection *connection,
                                          const gchar *nickname,
                                          const gchar *first_name,
                                          const gchar *last_name,
                                          const gchar *jid,
                                          const gchar *email,
                                          const gchar *published_name,
                                          const GArray *olpc_key,
                                          const gchar *olpc_color)
{
  SalutAvahiDiscoveryClient *self = SALUT_AVAHI_DISCOVERY_CLIENT (client);

  return SALUT_SELF (salut_avahi_self_new (connection, self, nickname, first_name,
      last_name, jid, email, published_name, olpc_key, olpc_color));
}

static const gchar *
salut_avahi_discovery_client_get_host_name_fqdn (SalutDiscoveryClient *clt)
{
  return avahi_client_get_host_name_fqdn (
        SALUT_AVAHI_DISCOVERY_CLIENT (clt)->avahi_client->avahi_client);
}

const gchar *
salut_avahi_discovery_client_get_dnssd_name (SalutAvahiDiscoveryClient *clt)
{
  SalutAvahiDiscoveryClientPrivate *priv =
    SALUT_AVAHI_DISCOVERY_CLIENT_GET_PRIVATE (clt);

  if (priv->dnssd_name != NULL)
    return priv->dnssd_name;
  else
    return SALUT_DNSSD_PRESENCE;
}

static void
discovery_client_init (gpointer g_iface,
                       gpointer iface_data)
{
  SalutDiscoveryClientClass *klass = (SalutDiscoveryClientClass *) g_iface;

  klass->start = salut_avahi_discovery_client_start;
  klass->create_muc_manager = salut_avahi_discovery_client_create_muc_manager;
  klass->create_roomlist_manager =
    salut_avahi_discovery_client_create_roomlist_manager;
  klass->create_contact_manager =
    salut_avahi_discovery_client_create_contact_manager;
#ifdef ENABLE_OLPC
  klass->create_olpc_activity_manager =
    salut_avahi_discovery_client_create_olpc_activity_manager;
#endif
  klass->create_self = salut_avahi_discovery_client_create_self;
  klass->get_host_name_fqdn = salut_avahi_discovery_client_get_host_name_fqdn;
}
