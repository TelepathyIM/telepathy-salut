/*
 * self.c - Source for SalutSelf
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

#include "config.h"
#include "self.h"

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#ifdef G_OS_UNIX
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#endif

#include <errno.h>

#include <gibber/gibber-linklocal-transport.h>
#include <gibber/gibber-muc-connection.h>

#include <telepathy-glib/telepathy-glib.h>

#include "capabilities.h"
#include "contact-manager.h"
#include "util.h"
#include "muc-manager.h"

#define DEBUG_FLAG DEBUG_SELF
#include <debug.h>

static void xep_0115_capabilities_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (SalutSelf, salut_self, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (WOCKY_TYPE_XEP_0115_CAPABILITIES,
        xep_0115_capabilities_iface_init);
)

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_NICKNAME,
  PROP_FIRST_NAME,
  PROP_LAST_NAME,
  PROP_JID,
  PROP_EMAIL,
  PROP_PUBLISHED_NAME,
};

/* signal enum */
enum
{
  ESTABLISHED,
  FAILURE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */

struct _SalutSelfPrivate
{
  SalutContactManager *contact_manager;
  TpHandleRepoIface *room_repo;

  GIOChannel *listener;
  guint io_watch_in;

  GabbleCapabilitySet *caps;
  GPtrArray *data_forms;

  gboolean dispose_has_run;
};

static void
salut_self_init (SalutSelf *obj)
{
  SalutSelfPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (obj, SALUT_TYPE_SELF,
      SalutSelfPrivate);

  obj->priv = priv;

  /* allocate any data required by the object here */
  obj->status = SALUT_PRESENCE_AVAILABLE;
  obj->status_message = NULL;
  obj->jid = NULL;

  obj->first_name = NULL;
  obj->last_name = NULL;
  obj->email = NULL;
  obj->published_name = NULL;

  priv->listener = NULL;
}

static void
salut_self_get_property (GObject *object,
                         guint property_id,
                         GValue *value,
                         GParamSpec *pspec)
{
  SalutSelf *self = SALUT_SELF (object);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, self->connection);
        break;
      case PROP_NICKNAME:
        g_value_set_string (value, self->nickname);
        break;
      case PROP_FIRST_NAME:
        g_value_set_string (value, self->first_name);
        break;
      case PROP_LAST_NAME:
        g_value_set_string (value, self->last_name);
        break;
      case PROP_JID:
        g_value_set_string (value, self->jid);
        break;
      case PROP_EMAIL:
        g_value_set_string (value, self->email);
        break;
      case PROP_PUBLISHED_NAME:
        g_value_set_string (value, self->published_name);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_self_set_property (GObject *object,
                         guint property_id,
                         const GValue *value,
                         GParamSpec *pspec)
{
  SalutSelf *self = SALUT_SELF (object);

  switch (property_id)
    {
      case PROP_CONNECTION:
        self->connection = g_value_get_object (value);
        break;
      case PROP_NICKNAME:
        g_free (self->nickname);
        self->nickname = g_value_dup_string (value);
        break;
      case PROP_FIRST_NAME:
        g_free (self->first_name);
        self->first_name = g_value_dup_string (value);
        break;
      case PROP_LAST_NAME:
        g_free (self->last_name);
        self->last_name = g_value_dup_string (value);
        break;
      case PROP_JID:
        g_free (self->jid);
        self->jid = g_value_dup_string (value);
        break;
      case PROP_EMAIL:
        g_free (self->email);
        self->email = g_value_dup_string (value);
        break;
      case PROP_PUBLISHED_NAME:
        g_free (self->published_name);
        self->published_name = g_value_dup_string (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
salut_self_constructor (GType type,
                        guint n_props,
                        GObjectConstructParam *props)
{
  GObject *obj;
  SalutSelf *self;
  SalutSelfPrivate *priv;

  obj = G_OBJECT_CLASS (salut_self_parent_class)->
    constructor (type, n_props, props);

  self = SALUT_SELF (obj);
  priv = self->priv;

  g_assert (self->connection != NULL);
  g_object_get (self->connection,
      "contact-manager", &(priv->contact_manager),
      NULL);
  g_assert (priv->contact_manager != NULL);

  priv->room_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self->connection, TP_ENTITY_TYPE_ROOM);

  /* Prefer using the nickname as alias */
  if (self->nickname != NULL)
    {
      self->alias = g_strdup (self->nickname);
    }
  else
    {
      if (self->first_name != NULL)
        {
          if (self->last_name != NULL)
            self->alias = g_strdup_printf ("%s %s", self->first_name,
                self->last_name);
          else
            self->alias = g_strdup (self->first_name);
        }
      else if (self->last_name != NULL)
        {
          self->alias = g_strdup (self->last_name);
        }
    }

  priv->caps = salut_dup_self_advertised_caps ();
  priv->data_forms = g_ptr_array_new ();

  return obj;
}

static void salut_self_dispose (GObject *object);
static void salut_self_finalize (GObject *object);

static void
salut_self_class_init (SalutSelfClass *salut_self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_self_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_self_class, sizeof (SalutSelfPrivate));

  object_class->constructor = salut_self_constructor;
  object_class->get_property = salut_self_get_property;
  object_class->set_property = salut_self_set_property;

  object_class->dispose = salut_self_dispose;
  object_class->finalize = salut_self_finalize;

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "The Salut Connection associated with this self object",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION,
      param_spec);

  param_spec = g_param_spec_string (
      "nickname",
      "the nickname",
      "The nickname of the self user",
      NULL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_NICKNAME,
      param_spec);

  param_spec = g_param_spec_string (
      "first-name",
      "the first name",
      "The first name of the self user",
      NULL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_FIRST_NAME,
      param_spec);

  param_spec = g_param_spec_string (
      "last-name",
      "the last name",
      "The last name of the self user",
      NULL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_LAST_NAME,
      param_spec);

  param_spec = g_param_spec_string (
      "jid",
      "the jid",
      "The jabber ID of the self user",
      NULL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_JID,
      param_spec);

  param_spec = g_param_spec_string (
      "email",
      "the email",
      "The email of the self user",
      NULL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_EMAIL,
      param_spec);

  param_spec = g_param_spec_string (
      "published-name",
      "the published name",
      "The name used to publish the presence service",
      g_get_user_name (),
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PUBLISHED_NAME,
      param_spec);

  signals[ESTABLISHED] =
    g_signal_new ("established",
                  G_OBJECT_CLASS_TYPE (salut_self_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[FAILURE] =
    g_signal_new ("failure",
                  G_OBJECT_CLASS_TYPE (salut_self_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__POINTER,
                  G_TYPE_NONE, 0);
}

void
salut_self_dispose (GObject *object)
{
  SalutSelf *self = SALUT_SELF (object);
  SalutSelfPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  gabble_capability_set_free (self->priv->caps);

  if (priv->contact_manager != NULL)
    {
      g_object_unref (priv->contact_manager);
      priv->contact_manager = NULL;
    }

  priv->room_repo = NULL;

  if (priv->listener)
    {
      g_io_channel_unref (priv->listener);
      g_source_remove (priv->io_watch_in);
      priv->listener = NULL;
    }

  if (priv->data_forms != NULL)
    {
      g_ptr_array_unref (priv->data_forms);
      priv->data_forms = NULL;
    }


  if (G_OBJECT_CLASS (salut_self_parent_class)->dispose)
    G_OBJECT_CLASS (salut_self_parent_class)->dispose (object);
}

void
salut_self_finalize (GObject *object)
{
  SalutSelf *self = SALUT_SELF (object);

  /* free any data held directly by the object here */

  g_free (self->jid);
  g_free (self->name);

  g_free (self->first_name);
  g_free (self->last_name);
  g_free (self->email);
  g_free (self->published_name);
  g_free (self->alias);
  g_free (self->node);
  g_free (self->hash);
  g_free (self->ver);

  G_OBJECT_CLASS (salut_self_parent_class)->finalize (object);
}

/* Start announcing our presence on the network */
gboolean
salut_self_announce (SalutSelf *self,
                     guint16 port,
                     GError **error)
{
  return SALUT_SELF_GET_CLASS (self)->announce (self, port, error);
}

gboolean
salut_self_set_presence (SalutSelf *self, SalutPresenceId status,
    const gchar *message, GError **error)
{

  g_assert (status < SALUT_PRESENCE_NR_PRESENCES);

  self->status = status;
  g_free (self->status_message);
  if (tp_strdiff (message, ""))
    self->status_message = g_strdup (message);
  else
    self->status_message = NULL;

  return SALUT_SELF_GET_CLASS (self)->set_presence (self, error);
}

gboolean
salut_self_set_caps (SalutSelf *self,
                     const gchar *node,
                     const gchar *hash,
                     const gchar *ver,
                     GError **error)
{
  gboolean out;

  g_free (self->node);
  self->node = g_strdup (node);
  g_free (self->hash);
  self->hash = g_strdup (hash);
  g_free (self->ver);
  self->ver = g_strdup (ver);

  out = SALUT_SELF_GET_CLASS (self)->set_caps (self, error);

  g_signal_emit_by_name (self, "capabilities-changed");

  return out;
}

const gchar *
salut_self_get_alias (SalutSelf *self)
{
  if (self->alias == NULL)
    {
      return self->name;
    }
  return self->alias;
}

gboolean
salut_self_set_alias (SalutSelf *self, const gchar *alias, GError **error)
{
  gboolean ret;
  GError *err = NULL;

  g_free (self->alias);
  g_free (self->nickname);
  self->alias = g_strdup (alias);
  self->nickname = g_strdup (alias);

  ret = SALUT_SELF_GET_CLASS (self)->set_alias (self, &err);
  if (!ret)
    {
      if (error != NULL)
        *error = g_error_new_literal (TP_ERROR, TP_ERROR_NETWORK_ERROR,
            err->message);
      g_error_free (err);
    }
  return ret;
}

static void
salut_self_remove_avatar (SalutSelf *self)
{
  DEBUG ("Removing avatar");

  SALUT_SELF_GET_CLASS (self)->remove_avatar (self);
}

gboolean
salut_self_set_avatar (SalutSelf *self, guint8 *data,
    gsize size, GError **error)
{
  gboolean ret = TRUE;
  GError *err = NULL;

  g_free (self->avatar_token);
  self->avatar_token = NULL;

  g_free (self->avatar);
  self->avatar = NULL;

  self->avatar_size = 0;

  if (size == 0)
    {
      self->avatar_token = g_strdup ("");
      salut_self_remove_avatar (self);
      return TRUE;
    }

  ret = SALUT_SELF_GET_CLASS (self)->set_avatar (self, data, size, &err);

  if (!ret)
    {
      salut_self_remove_avatar (self);
      if (err != NULL)
        *error = g_error_new_literal (TP_ERROR, TP_ERROR_NETWORK_ERROR,
            err->message);
      g_error_free (err);
    }

  return ret;
}

void
salut_self_established (SalutSelf *self)
{
  g_signal_emit (self, signals[ESTABLISHED], 0, NULL);
}

const GabbleCapabilitySet *
salut_self_get_caps (SalutSelf *self)
{
  return self->priv->caps;
}

static const GPtrArray *
salut_self_get_data_forms (WockyXep0115Capabilities *caps)
{
  SalutSelf *self = SALUT_SELF (caps);

  return self->priv->data_forms;
}

void
salut_self_take_caps (SalutSelf *self,
    GabbleCapabilitySet *set)
{
  g_return_if_fail (SALUT_IS_SELF (self));
  g_return_if_fail (set != NULL);

  gabble_capability_set_free (self->priv->caps);
  self->priv->caps = set;
}

void
salut_self_take_data_forms (SalutSelf *self,
    GPtrArray *data_forms)
{
  g_return_if_fail (SALUT_IS_SELF (self));
  g_return_if_fail (data_forms != NULL);

  g_ptr_array_unref (self->priv->data_forms);
  self->priv->data_forms = data_forms;
}

static void
xep_0115_capabilities_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  WockyXep0115CapabilitiesInterface *iface = g_iface;

  iface->get_data_forms = salut_self_get_data_forms;
}
