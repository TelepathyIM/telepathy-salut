/*
 * salut-direct-bytestream-manager.c - Source for SalutDirectBytestreamManager
 * Copyright (C) 2007, 2008 Collabora Ltd.
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

#include "salut-direct-bytestream-manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gibber/gibber-xmpp-stanza.h>
#include <gibber/gibber-namespaces.h>
#include <gibber/gibber-xmpp-error.h>
#include <gibber/gibber-iq-helper.h>

#include "salut-im-manager.h"
#include "salut-muc-manager.h"
#include "salut-tubes-manager.h"

#define DEBUG_FLAG DEBUG_DIRECT_BYTESTREAM_MGR
#include "debug.h"

G_DEFINE_TYPE (SalutDirectBytestreamManager, salut_direct_bytestream_manager,
    G_TYPE_OBJECT)

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_HOST_NAME_FQDN,
  LAST_PROPERTY
};

/* private structure */
typedef struct _SalutDirectBytestreamManagerPrivate SalutDirectBytestreamManagerPrivate;

struct _SalutDirectBytestreamManagerPrivate
{
  SalutConnection *connection;
  SalutImManager *im_manager;
  SalutXmppConnectionManager *xmpp_connection_manager;
  gchar *host_name_fqdn;

  gboolean dispose_has_run;
};

#define SALUT_DIRECT_BYTESTREAM_MANAGER_GET_PRIVATE(obj) \
    ((SalutDirectBytestreamManagerPrivate *) ((SalutDirectBytestreamManager *)obj)->priv)

static void
salut_direct_bytestream_manager_init (SalutDirectBytestreamManager *self)
{
  SalutDirectBytestreamManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_DIRECT_BYTESTREAM_MANAGER, SalutDirectBytestreamManagerPrivate);

  self->priv = priv;

  priv->dispose_has_run = FALSE;
}


void
salut_direct_bytestream_manager_dispose (GObject *object)
{
  SalutDirectBytestreamManager *self = SALUT_DIRECT_BYTESTREAM_MANAGER (object);
  SalutDirectBytestreamManagerPrivate *priv = SALUT_DIRECT_BYTESTREAM_MANAGER_GET_PRIVATE
      (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_object_unref (priv->im_manager);
  g_object_unref (priv->xmpp_connection_manager);

  if (G_OBJECT_CLASS (salut_direct_bytestream_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_direct_bytestream_manager_parent_class)->dispose (object);
}

void
salut_direct_bytestream_manager_finalize (GObject *object)
{
  SalutDirectBytestreamManager *self = SALUT_DIRECT_BYTESTREAM_MANAGER (object);
  SalutDirectBytestreamManagerPrivate *priv = SALUT_DIRECT_BYTESTREAM_MANAGER_GET_PRIVATE (
      self);

  g_free (priv->host_name_fqdn);

  if (G_OBJECT_CLASS (salut_direct_bytestream_manager_parent_class)->finalize)
    G_OBJECT_CLASS (salut_direct_bytestream_manager_parent_class)->finalize
        (object);
}

static void
salut_direct_bytestream_manager_get_property (GObject *object,
                                          guint property_id,
                                          GValue *value,
                                          GParamSpec *pspec)
{
  SalutDirectBytestreamManager *self = SALUT_DIRECT_BYTESTREAM_MANAGER (object);
  SalutDirectBytestreamManagerPrivate *priv = SALUT_DIRECT_BYTESTREAM_MANAGER_GET_PRIVATE (
      self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->connection);
        break;
      case PROP_HOST_NAME_FQDN:
        g_value_set_string (value, priv->host_name_fqdn);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_direct_bytestream_manager_set_property (GObject *object,
                                          guint property_id,
                                          const GValue *value,
                                          GParamSpec *pspec)
{
  SalutDirectBytestreamManager *self = SALUT_DIRECT_BYTESTREAM_MANAGER (object);
  SalutDirectBytestreamManagerPrivate *priv = SALUT_DIRECT_BYTESTREAM_MANAGER_GET_PRIVATE (
      self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->connection = g_value_get_object (value);
        break;
      case PROP_HOST_NAME_FQDN:
        g_free (priv->host_name_fqdn);
        priv->host_name_fqdn = g_value_dup_string (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
salut_direct_bytestream_manager_constructor (GType type,
                                      guint n_props,
                                      GObjectConstructParam *props)
{
  GObject *obj;
  SalutDirectBytestreamManager *self;
  SalutDirectBytestreamManagerPrivate *priv;

  obj = G_OBJECT_CLASS (salut_direct_bytestream_manager_parent_class)->
           constructor (type, n_props, props);

  self = SALUT_DIRECT_BYTESTREAM_MANAGER (obj);
  priv = SALUT_DIRECT_BYTESTREAM_MANAGER_GET_PRIVATE (self);

  g_assert (priv->connection != NULL);
  g_object_get (priv->connection,
      "im-manager", &(priv->im_manager),
      "xmpp-connection-manager", &(priv->xmpp_connection_manager),
      NULL);
  g_assert (priv->im_manager != NULL);
  g_assert (priv->xmpp_connection_manager != NULL);
  g_assert (priv->host_name_fqdn != NULL);

  return obj;
}

static void
salut_direct_bytestream_manager_class_init (
    SalutDirectBytestreamManagerClass *salut_direct_bytestream_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS
      (salut_direct_bytestream_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_direct_bytestream_manager_class,
      sizeof (SalutDirectBytestreamManagerPrivate));

  object_class->constructor = salut_direct_bytestream_manager_constructor;
  object_class->dispose = salut_direct_bytestream_manager_dispose;
  object_class->finalize = salut_direct_bytestream_manager_finalize;

  object_class->get_property = salut_direct_bytestream_manager_get_property;
  object_class->set_property = salut_direct_bytestream_manager_set_property;

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "Salut Connection that owns the connection for this bytestream channel",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string (
      "host-name-fqdn",
      "host name FQDN",
      "The FQDN host name that will be used by OOB bytestreams",
      NULL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HOST_NAME_FQDN,
      param_spec);
}

SalutDirectBytestreamManager *
salut_direct_bytestream_manager_new (SalutConnection *conn,
                              const gchar *host_name_fqdn)
{
  g_return_val_if_fail (SALUT_IS_CONNECTION (conn), NULL);

  return g_object_new (
      SALUT_TYPE_DIRECT_BYTESTREAM_MANAGER,
      "connection", conn,
      "host-name-fqdn", host_name_fqdn,
      NULL);
}

