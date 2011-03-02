/*
 * salut-avahi-muc-manager.c - Source for SalutAvahiMucManager
 * Copyright (C) 2006 Collabora Ltd.
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

#include "salut-avahi-muc-manager.h"

#include <avahi-gobject/ga-service-browser.h>
#include <avahi-gobject/ga-service-resolver.h>

#include <gibber/gibber-muc-connection.h>
#include <gibber/gibber-namespaces.h>
#include <gibber/gibber-xmpp-error.h>

#include "salut-muc-channel.h"
#include "salut-contact-manager.h"
#include "salut-tubes-channel.h"
#include "salut-roomlist-channel.h"
#include "salut-avahi-muc-channel.h"

#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG DEBUG_MUC
#include "debug.h"

G_DEFINE_TYPE (SalutAvahiMucManager, salut_avahi_muc_manager,
    SALUT_TYPE_MUC_MANAGER);

/* properties */
enum {
  PROP_CLIENT = 1,
  LAST_PROP
};

/* private structure */
typedef struct _SalutAvahiMucManagerPrivate SalutAvahiMucManagerPrivate;

struct _SalutAvahiMucManagerPrivate
{
  SalutAvahiDiscoveryClient *discovery_client;

  gboolean dispose_has_run;
};

#define SALUT_AVAHI_MUC_MANAGER_GET_PRIVATE(obj) \
    ((SalutAvahiMucManagerPrivate *) ((SalutAvahiMucManager *) obj)->priv)

static void
salut_avahi_muc_manager_init (SalutAvahiMucManager *self)
{
  SalutAvahiMucManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_AVAHI_MUC_MANAGER, SalutAvahiMucManagerPrivate);

  self->priv = priv;
}

static void
salut_avahi_muc_manager_get_property (GObject *object,
                                      guint property_id,
                                      GValue *value,
                                      GParamSpec *pspec)
{
  SalutAvahiMucManager *self = SALUT_AVAHI_MUC_MANAGER (object);
  SalutAvahiMucManagerPrivate *priv = SALUT_AVAHI_MUC_MANAGER_GET_PRIVATE (self);

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
salut_avahi_muc_manager_set_property (GObject *object,
                                      guint property_id,
                                      const GValue *value,
                                      GParamSpec *pspec)
{
  SalutAvahiMucManager *self = SALUT_AVAHI_MUC_MANAGER (object);
  SalutAvahiMucManagerPrivate *priv = SALUT_AVAHI_MUC_MANAGER_GET_PRIVATE (self);

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

static void salut_avahi_muc_manager_dispose (GObject *object);

static SalutMucChannel *
salut_avahi_muc_manager_create_muc_channel (
    SalutMucManager *mgr,
    SalutConnection *connection,
    const gchar *path,
    GibberMucConnection *muc_connection,
    TpHandle handle,
    const gchar *name,
    TpHandle initiator,
    gboolean creator,
    gboolean requested)
{
  SalutAvahiMucManager *self = SALUT_AVAHI_MUC_MANAGER (mgr);
  SalutAvahiMucManagerPrivate *priv = SALUT_AVAHI_MUC_MANAGER_GET_PRIVATE (self);

  return SALUT_MUC_CHANNEL (salut_avahi_muc_channel_new (connection,
          path, muc_connection, handle, name, priv->discovery_client, initiator,
        creator, requested));
}

static void
salut_avahi_muc_manager_class_init (
    SalutAvahiMucManagerClass *salut_avahi_muc_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_avahi_muc_manager_class);
  SalutMucManagerClass *muc_manager_class = SALUT_MUC_MANAGER_CLASS (
      salut_avahi_muc_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_avahi_muc_manager_class,
                              sizeof (SalutAvahiMucManagerPrivate));

  object_class->get_property = salut_avahi_muc_manager_get_property;
  object_class->set_property = salut_avahi_muc_manager_set_property;

  object_class->dispose = salut_avahi_muc_manager_dispose;

  muc_manager_class->create_muc_channel =
    salut_avahi_muc_manager_create_muc_channel;

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

static void
salut_avahi_muc_manager_dispose (GObject *object)
{
  SalutAvahiMucManager *self = SALUT_AVAHI_MUC_MANAGER (object);
  SalutAvahiMucManagerPrivate *priv = SALUT_AVAHI_MUC_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->discovery_client != NULL)
    {
      g_object_unref (priv->discovery_client);
      priv->discovery_client = NULL;
    }

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_avahi_muc_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_avahi_muc_manager_parent_class)->dispose (object);
}

/* public functions */
SalutAvahiMucManager *
salut_avahi_muc_manager_new (SalutConnection *connection,
                             SalutAvahiDiscoveryClient *discovery_client)
{
  g_assert (connection != NULL);
  g_assert (discovery_client != NULL);

  return g_object_new (SALUT_TYPE_AVAHI_MUC_MANAGER,
      "connection", connection,
      "discovery-client", discovery_client,
      NULL);
}
