/*
 * bonjour-contact-manager.c - Source for SalutBonjourContactManager
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

#define DEBUG_FLAG DEBUG_MUC
#include "debug.h"

#include "bonjour-contact-manager.h"

G_DEFINE_TYPE (SalutBonjourContactManager, salut_bonjour_contact_manager,
    SALUT_TYPE_CONTACT_MANAGER);

/* properties */
enum
{
  PROP_DISCOVERY_CLIENT = 1,
  LAST_PROPERTY
};

/* private structure */
typedef struct _SalutBonjourContactManagerPrivate SalutBonjourContactManagerPrivate;

struct _SalutBonjourContactManagerPrivate
{
  SalutBonjourDiscoveryClient *discovery_client;

  gboolean dispose_has_run;
};

#define SALUT_BONJOUR_CONTACT_MANAGER_GET_PRIVATE(obj) \
    ((SalutBonjourContactManagerPrivate *) \
      ((SalutBonjourContactManager *) obj)->priv)


static void
salut_bonjour_contact_manager_get_property (GObject *object,
                                            guint property_id,
                                            GValue *value,
                                            GParamSpec *pspec)
{
  SalutBonjourContactManager *chan = SALUT_BONJOUR_CONTACT_MANAGER (object);
  SalutBonjourContactManagerPrivate *priv =
    SALUT_BONJOUR_CONTACT_MANAGER_GET_PRIVATE (chan);

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
salut_bonjour_contact_manager_set_property (GObject *object,
                                            guint property_id,
                                            const GValue *value,
                                            GParamSpec *pspec)
{
  SalutBonjourContactManager *chan = SALUT_BONJOUR_CONTACT_MANAGER (object);
  SalutBonjourContactManagerPrivate *priv =
    SALUT_BONJOUR_CONTACT_MANAGER_GET_PRIVATE (chan);

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

static void
salut_bonjour_contact_manager_init (SalutBonjourContactManager *self)
{
  SalutBonjourContactManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_BONJOUR_CONTACT_MANAGER, SalutBonjourContactManagerPrivate);

  self->priv = priv;

  priv->discovery_client = NULL;
}

static SalutContact *
salut_bonjour_contact_manager_create_contact (SalutContactManager *mgr,
                                              const gchar *name)
{
  return NULL;
}

static gboolean
salut_bonjour_contact_manager_start (SalutContactManager *mgr,
                                     GError **error)
{
  return TRUE;
}

static void
salut_bonjour_contact_manager_dispose_contact (SalutContactManager *mgr,
                                               SalutContact *contact)
{
}

static void
salut_bonjour_contact_manager_close_all (SalutContactManager *mgr)
{
  SalutBonjourContactManager *self = SALUT_BONJOUR_CONTACT_MANAGER (mgr);
  SalutBonjourContactManagerPrivate *priv =
    SALUT_BONJOUR_CONTACT_MANAGER_GET_PRIVATE (self);

  if (priv->discovery_client != NULL)
    {
      g_object_unref (priv->discovery_client);
      priv->discovery_client = NULL;
    }
}

static void
salut_bonjour_contact_manager_constructed (GObject *object)
{
  SalutBonjourContactManager *self = SALUT_BONJOUR_CONTACT_MANAGER (object);
  SalutBonjourContactManagerPrivate *priv =
    SALUT_BONJOUR_CONTACT_MANAGER_GET_PRIVATE (self);

  if (G_OBJECT_CLASS (salut_bonjour_contact_manager_parent_class)->constructed)
    G_OBJECT_CLASS (salut_bonjour_contact_manager_parent_class)->constructed (object);
}

static void
salut_bonjour_contact_manager_class_init (
    SalutBonjourContactManagerClass *salut_bonjour_contact_manager_class) {
  GObjectClass *object_class = G_OBJECT_CLASS (salut_bonjour_contact_manager_class);
  SalutContactManagerClass *contact_manager_class = SALUT_CONTACT_MANAGER_CLASS (
      salut_bonjour_contact_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_bonjour_contact_manager_class,
      sizeof (SalutBonjourContactManagerPrivate));

  object_class->get_property = salut_bonjour_contact_manager_get_property;
  object_class->set_property = salut_bonjour_contact_manager_set_property;
  object_class->constructed = salut_bonjour_contact_manager_constructed;

  contact_manager_class->start = salut_bonjour_contact_manager_start;
  contact_manager_class->create_contact =
    salut_bonjour_contact_manager_create_contact;
  contact_manager_class->dispose_contact =
    salut_bonjour_contact_manager_dispose_contact;
  contact_manager_class->close_all = salut_bonjour_contact_manager_close_all;

  param_spec = g_param_spec_object (
      "discovery-client",
      "SalutBonjourDiscoveryClient object",
      "The Salut Bonjour Discovery client associated with this muc channel",
      SALUT_TYPE_BONJOUR_DISCOVERY_CLIENT,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_DISCOVERY_CLIENT,
      param_spec);
}

SalutBonjourContactManager *
salut_bonjour_contact_manager_new (SalutConnection *connection,
                                   SalutBonjourDiscoveryClient *discovery_client)
{
  return g_object_new (SALUT_TYPE_BONJOUR_CONTACT_MANAGER,
      "connection", connection,
      "discovery-client", discovery_client,
      NULL);
}
