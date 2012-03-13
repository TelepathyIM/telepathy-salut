/*
 * bonjour-discovery-client.c - Source for SalutBonjourDiscoveryClient
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

#include "bonjour-discovery-client.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#define DEBUG_FLAG DEBUG_DISCOVERY
#include "debug.h"

#include "presence.h"
#include "signals-marshal.h"
#include "bonjour-contact-manager.h"
#include "bonjour-self.h"

static void
discovery_client_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (SalutBonjourDiscoveryClient, salut_bonjour_discovery_client,
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

typedef struct _SalutBonjourDiscoveryClientPrivate \
          SalutBonjourDiscoveryClientPrivate;
struct _SalutBonjourDiscoveryClientPrivate
{
  SalutDiscoveryClientState state;

  gchar *dnssd_name;

  gboolean dispose_has_run;
};

#define SALUT_BONJOUR_DISCOVERY_CLIENT_GET_PRIVATE(obj) \
    ((SalutBonjourDiscoveryClientPrivate *) \
      ((SalutBonjourDiscoveryClient *) obj)->priv)

static void
salut_bonjour_discovery_client_init (SalutBonjourDiscoveryClient *self)
{
  SalutBonjourDiscoveryClientPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_BONJOUR_DISCOVERY_CLIENT, SalutBonjourDiscoveryClientPrivate);

  self->priv = priv;
  priv->dispose_has_run = FALSE;

  priv->state = SALUT_DISCOVERY_CLIENT_STATE_DISCONNECTED;
}

static void
change_state (SalutBonjourDiscoveryClient *self,
              SalutDiscoveryClientState state)
{
  SalutBonjourDiscoveryClientPrivate *priv =
    SALUT_BONJOUR_DISCOVERY_CLIENT_GET_PRIVATE (self);

  if (priv->state == state)
    return;

  priv->state = state;
  g_signal_emit (G_OBJECT (self), signals[STATE_CHANGED], 0, state);
}

static void
salut_bonjour_discovery_client_dispose (GObject *object)
{
  SalutBonjourDiscoveryClient *self = SALUT_BONJOUR_DISCOVERY_CLIENT (object);
  SalutBonjourDiscoveryClientPrivate *priv =
    SALUT_BONJOUR_DISCOVERY_CLIENT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  tp_clear_pointer (&priv->dnssd_name, g_free);

  G_OBJECT_CLASS (salut_bonjour_discovery_client_parent_class)->dispose (object);
}

static void
salut_bonjour_discovery_client_get_property (GObject *object,
                                             guint property_id,
                                             GValue *value,
                                             GParamSpec *pspec)
{
  SalutBonjourDiscoveryClient *self = SALUT_BONJOUR_DISCOVERY_CLIENT (object);
  SalutBonjourDiscoveryClientPrivate *priv =
    SALUT_BONJOUR_DISCOVERY_CLIENT_GET_PRIVATE (self);

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
salut_bonjour_discovery_client_set_property (GObject *object,
                                             guint property_id,
                                             const GValue *value,
                                             GParamSpec *pspec)
{
  SalutBonjourDiscoveryClient *self = SALUT_BONJOUR_DISCOVERY_CLIENT (object);
  SalutBonjourDiscoveryClientPrivate *priv =
    SALUT_BONJOUR_DISCOVERY_CLIENT_GET_PRIVATE (self);

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
salut_bonjour_discovery_client_class_init (
    SalutBonjourDiscoveryClientClass *salut_bonjour_discovery_client_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (
      salut_bonjour_discovery_client_class);

  g_type_class_add_private (salut_bonjour_discovery_client_class,
      sizeof (SalutBonjourDiscoveryClientPrivate));

  object_class->dispose = salut_bonjour_discovery_client_dispose;

  object_class->get_property = salut_bonjour_discovery_client_get_property;
  object_class->set_property = salut_bonjour_discovery_client_set_property;

  g_object_class_override_property (object_class, PROP_STATE,
      "state");
  g_object_class_override_property (object_class, PROP_DNSSD_NAME,
      "dnssd-name");

  signals[STATE_CHANGED] =
    g_signal_new ("state-changed",
                  G_OBJECT_CLASS_TYPE (salut_bonjour_discovery_client_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_signals_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);
}

/*
 * salut_bonjour_discovery_client_start
 *
 * Implements salut_discovery_client_start on SalutDiscoveryClient
 */
static gboolean
salut_bonjour_discovery_client_start (SalutDiscoveryClient *client,
                                      GError **error)
{
  SalutBonjourDiscoveryClient *self = SALUT_BONJOUR_DISCOVERY_CLIENT (client);

  /*TODO: Ensure we connect to the daemon before signaling we are connected*/
  change_state (self, SALUT_DISCOVERY_CLIENT_STATE_CONNECTED);

  return TRUE;
}

/*
 * salut_bonjour_discovery_client_create_contact_manager
 *
 * Implements salut_discovery_client_create_contact_manager on
 * SalutDiscoveryClient
 */
static SalutContactManager *
salut_bonjour_discovery_client_create_contact_manager (
    SalutDiscoveryClient *client,
    SalutConnection *connection)
{
  SalutBonjourDiscoveryClient *self = SALUT_BONJOUR_DISCOVERY_CLIENT (client);
  return SALUT_CONTACT_MANAGER (
      salut_bonjour_contact_manager_new (connection, self));
}

/*
 * salut_bonjour_discovery_client_create_self
 *
 * Implements salut_discovery_client_create_self on SalutDiscoveryClient
 */
static SalutSelf *
salut_bonjour_discovery_client_create_self (SalutDiscoveryClient *client,
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
  SalutBonjourDiscoveryClient *self = SALUT_BONJOUR_DISCOVERY_CLIENT (client);

  return SALUT_SELF (salut_bonjour_self_new (connection, self, nickname,
      first_name, last_name, jid, email, published_name, olpc_key, olpc_color));
}

static const gchar *
salut_bonjour_discovery_client_get_host_name_fqdn (SalutDiscoveryClient *clt)
{
  g_warning ("FQDN not supported by Bonjour discovery client");
  return NULL;
}

const gchar *
salut_bonjour_discovery_client_get_dnssd_name (SalutBonjourDiscoveryClient *clt)
{
  SalutBonjourDiscoveryClientPrivate *priv =
    SALUT_BONJOUR_DISCOVERY_CLIENT_GET_PRIVATE (clt);

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

  klass->start = salut_bonjour_discovery_client_start;
  klass->create_muc_manager = NULL;
  klass->create_roomlist_manager = NULL;
  klass->create_contact_manager =
    salut_bonjour_discovery_client_create_contact_manager;
  klass->create_self = salut_bonjour_discovery_client_create_self;
  klass->get_host_name_fqdn = salut_bonjour_discovery_client_get_host_name_fqdn;
}
