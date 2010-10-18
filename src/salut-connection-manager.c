/*
 * salut-connection-manager.c - Source for SalutConnectionManager
 * Copyright (C) 2005 Nokia Corporation
 * Copyright (C) 2006 Collabora Ltd.
 *   @author: Sjoerd Simons <sjoerd@luon.net>
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
#include "salut-connection-manager.h"

#include <stdio.h>
#include <stdlib.h>

#include <dbus/dbus-protocol.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/debug-sender.h>

#include "protocol.h"
#include "salut-connection.h"
#include "debug.h"

/* properties */
enum
{
  PROP_BACKEND = 1,
  LAST_PROPERTY
};

struct _SalutConnectionManagerPrivate
{
  GType backend_type;
  TpBaseProtocol *protocol;
  TpDebugSender *debug_sender;
};

#define SALUT_CONNECTION_MANAGER_GET_PRIVATE(obj) ((obj)->priv)

G_DEFINE_TYPE(SalutConnectionManager, salut_connection_manager,
              TP_TYPE_BASE_CONNECTION_MANAGER)

static void
salut_connection_manager_init (SalutConnectionManager *self)
{
  SalutConnectionManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_CONNECTION_MANAGER, SalutConnectionManagerPrivate);

  priv->debug_sender = tp_debug_sender_dup ();
  g_log_set_default_handler (tp_debug_sender_log_handler, G_LOG_DOMAIN);

  self->priv = priv;
}

static void
salut_connection_manager_get_property (GObject *object,
                                       guint property_id,
                                       GValue *value,
                                       GParamSpec *pspec)
{
  SalutConnectionManager *self = SALUT_CONNECTION_MANAGER (object);
  SalutConnectionManagerPrivate *priv = SALUT_CONNECTION_MANAGER_GET_PRIVATE
      (self);

  switch (property_id)
    {
      case PROP_BACKEND:
        g_value_set_gtype (value, priv->backend_type);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_connection_manager_set_property (GObject *object,
                                       guint property_id,
                                       const GValue *value,
                                       GParamSpec *pspec)
{
  SalutConnectionManager *self = SALUT_CONNECTION_MANAGER (object);
  SalutConnectionManagerPrivate *priv = SALUT_CONNECTION_MANAGER_GET_PRIVATE
      (self);

  switch (property_id)
    {
      case PROP_BACKEND:
        priv->backend_type = g_value_get_gtype (value);

        if (priv->protocol != NULL)
          g_object_set (priv->protocol,
              "backend-type", priv->backend_type,
              NULL);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_connection_manager_constructed (GObject *object)
{
  SalutConnectionManager *self = SALUT_CONNECTION_MANAGER (object);
  TpBaseConnectionManager *base = (TpBaseConnectionManager *) self;
  void (*constructed) (GObject *) =
      ((GObjectClass *) salut_connection_manager_parent_class)->constructed;

  if (constructed != NULL)
    constructed (object);

  self->priv->protocol = salut_protocol_new (self->priv->backend_type);
  tp_base_connection_manager_add_protocol (base, self->priv->protocol);
}

static void
salut_connection_manager_dispose (GObject *object)
{
  SalutConnectionManager *self = SALUT_CONNECTION_MANAGER (object);
  void (*dispose) (GObject *) =
      ((GObjectClass *) salut_connection_manager_parent_class)->dispose;

  tp_clear_object (&self->priv->protocol);

  if (dispose != NULL)
    dispose (object);
}

static void
salut_connection_manager_finalize (GObject *object)
{
  SalutConnectionManager *self = SALUT_CONNECTION_MANAGER (object);
  SalutConnectionManagerPrivate *priv = self->priv;
  void (*finalize) (GObject *) =
      ((GObjectClass *) salut_connection_manager_parent_class)->finalize;

  if (priv->debug_sender != NULL)
    {
      g_object_unref (priv->debug_sender);
      priv->debug_sender = NULL;
    }

  debug_free ();

  if (finalize != NULL)
    finalize (object);
}

static void
salut_connection_manager_class_init (
    SalutConnectionManagerClass *salut_connection_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_connection_manager_class);
  TpBaseConnectionManagerClass *base_cm_class =
    TP_BASE_CONNECTION_MANAGER_CLASS(salut_connection_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_connection_manager_class,
      sizeof (SalutConnectionManagerPrivate));

  object_class->get_property = salut_connection_manager_get_property;
  object_class->set_property = salut_connection_manager_set_property;
  object_class->constructed = salut_connection_manager_constructed;
  object_class->dispose = salut_connection_manager_dispose;
  object_class->finalize = salut_connection_manager_finalize;

  param_spec = g_param_spec_gtype (
      "backend-type",
      "backend type",
      "a G_TYPE_GTYPE of the backend to use",
      G_TYPE_NONE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_BACKEND,
      param_spec);

  base_cm_class->cm_dbus_name = "salut";
}
