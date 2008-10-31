/*
 * salut-tubes-manager.c - Source for SalutTubesManager
 * Copyright (C) 2006-2008 Collabora Ltd.
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

#include "salut-tubes-manager.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#define DEBUG_FLAG DEBUG_TUBES

#include "debug.h"
#include "extensions/extensions.h"
#include "salut-connection.h"
#include "salut-tubes-channel.h"
#include "salut-muc-manager.h"
#include "salut-muc-channel.h"
#include "salut-util.h"
#include <gibber/gibber-namespaces.h>
#include <gibber/gibber-iq-helper.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-factory-iface.h>

static SalutTubesChannel *new_tubes_channel (SalutTubesManager *fac,
    TpHandle handle, TpHandle initiator);

static void tubes_channel_closed_cb (SalutTubesChannel *chan,
    gpointer user_data);

static void salut_tubes_manager_iface_init (gpointer g_iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (SalutTubesManager,
    salut_tubes_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE,
        salut_tubes_manager_iface_init));

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_CONTACT_MANAGER,
  PROP_XMPP_CONNECTION_MANAGER,
  LAST_PROPERTY
};

typedef struct _SalutTubesManagerPrivate \
          SalutTubesManagerPrivate;
struct _SalutTubesManagerPrivate
{
  SalutConnection *conn;
  SalutContactManager *contact_manager;
  SalutXmppConnectionManager *xmpp_connection_manager;

  GHashTable *channels;

  gboolean dispose_has_run;
};

#define SALUT_TUBES_MANAGER_GET_PRIVATE(obj) \
    ((SalutTubesManagerPrivate *) obj->priv)

static void
salut_tubes_manager_init (SalutTubesManager *self)
{
  SalutTubesManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_TUBES_MANAGER, SalutTubesManagerPrivate);

  self->priv = priv;

  priv->channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);

  priv->conn = NULL;
  priv->dispose_has_run = FALSE;
}

/* Filter for 1-1 tube request (XEP-proto-tubes)
 * http://telepathy.freedesktop.org/xmpp/tubes.html */
static gboolean
iq_tube_request_filter (SalutXmppConnectionManager *xcm,
                        GibberXmppConnection *conn,
                        GibberXmppStanza *stanza,
                        SalutContact *contact,
                        gpointer user_data)
{
  GibberStanzaType type;
  GibberStanzaSubType sub_type;

  gibber_xmpp_stanza_get_type_info (stanza, &type, &sub_type);
  if (type != GIBBER_STANZA_TYPE_IQ)
    return FALSE;

  if (sub_type != GIBBER_STANZA_SUB_TYPE_SET)
    return FALSE;

  return (gibber_xmpp_node_get_child_ns (stanza->node, "tube",
        GIBBER_TELEPATHY_NS_TUBES) != NULL) ||
         (gibber_xmpp_node_get_child_ns (stanza->node, "close",
                 GIBBER_TELEPATHY_NS_TUBES) != NULL);
}

/* similar to the same function in salut-tubes-channel.c but extract
 * information from a 1-1 <iq> message */
static gboolean
extract_tube_information (TpHandleRepoIface *contact_repo,
                          GibberXmppStanza *stanza,
                          gboolean *close,
                          TpTubeType *type,
                          TpHandle *initiator_handle,
                          const gchar **service,
                          GHashTable **parameters,
                          guint *tube_id,
                          guint *portnum,
                          GError **error)
{
  GibberXmppNode *iq;
  GibberXmppNode *tube_node, *close_node, *node;
  gboolean _close;

  iq = stanza->node;

  if (initiator_handle != NULL)
    {
      const gchar *from;
      from = gibber_xmpp_node_get_attribute (stanza->node, "from");
      if (from == NULL)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "got a message without a from field");
          return FALSE;
        }
      *initiator_handle = tp_handle_ensure (contact_repo, from, NULL,
          NULL);

      if (*initiator_handle == 0)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "invalid initiator ID %s", from);
          return FALSE;
        }
    }

  tube_node = gibber_xmpp_node_get_child_ns (iq, "tube",
      GIBBER_TELEPATHY_NS_TUBES);
  close_node = gibber_xmpp_node_get_child_ns (iq, "close",
      GIBBER_TELEPATHY_NS_TUBES);

  if (tube_node == NULL && close_node == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "The <iq> does not have a <tube> nor a <close>");
      return FALSE;
    }
  if (tube_node != NULL && close_node != NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "The <iq> has both a <tube> nor a <close>");
      return FALSE;
    }
  if (tube_node != NULL)
    {
      node = tube_node;
    }
  else
    {
      node = close_node;
    }

  _close = (close_node != NULL);
  if (close != NULL)
    {
      *close = _close;
    }

  if (tube_id != NULL)
    {
      const gchar *str;
      gchar *endptr;
      long int tmp;

      str = gibber_xmpp_node_get_attribute (node, "id");
      if (str == NULL)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "no tube id in tube request");
          return FALSE;
        }

      tmp = strtol (str, &endptr, 10);
      if (!endptr || *endptr)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "tube id is not numeric: %s", str);
          return FALSE;
        }
      *tube_id = (int) tmp;
    }

  /* next fields are not in the close stanza */
  if (_close)
    return TRUE;

  if (type != NULL)
    {
      const gchar *tube_type;

      tube_type = gibber_xmpp_node_get_attribute (tube_node, "type");
      if (!tp_strdiff (tube_type, "stream"))
        *type = TP_TUBE_TYPE_STREAM;
      else if (!tp_strdiff (tube_type, "dbus"))
        *type = TP_TUBE_TYPE_DBUS;
      else
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "The <iq><tube> does not have a correct type: '%s'", tube_type);
          return FALSE;
        }
    }

  if (service != NULL)
    {
      *service = gibber_xmpp_node_get_attribute (tube_node, "service");
    }

  if (parameters != NULL)
    {
      GibberXmppNode *parameters_node;

      parameters_node = gibber_xmpp_node_get_child (tube_node, "parameters");
      *parameters = salut_gibber_xmpp_node_extract_properties (parameters_node,
          "parameter");
    }

  if (portnum != NULL)
    {
      GibberXmppNode *transport_node;
      const gchar *str;
      gchar *endptr;
      long int tmp;

      transport_node = gibber_xmpp_node_get_child (tube_node, "transport");
      if (transport_node == NULL)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "no transport to connect to in the tube request");
          return FALSE;
        }

      str = gibber_xmpp_node_get_attribute (transport_node, "port");
      if (str == NULL)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "no port to connect to in the tube request");
          return FALSE;
        }

      tmp = strtol (str, &endptr, 10);
      if (!endptr || *endptr)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "port is not numeric: %s", str);
          return FALSE;
        }
      *portnum = (int) tmp;
    }

  return TRUE;
}

static void
iq_tube_request_cb (SalutXmppConnectionManager *xcm,
                    GibberXmppConnection *conn,
                    GibberXmppStanza *stanza,
                    SalutContact *contact,
                    gpointer user_data)
{
  SalutTubesManager *self = SALUT_TUBES_MANAGER (user_data);
  SalutTubesManagerPrivate *priv = SALUT_TUBES_MANAGER_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  /* tube informations */
  const gchar *service;
  TpTubeType tube_type;
  TpHandle initiator_handle;
  GHashTable *parameters;
  guint tube_id;
  guint portnum = 0;
  gboolean close;
  GError *error = NULL;

  SalutTubesChannel *chan;

  /* after this point, the message is for us, so in all cases we either handle
   * it or send an error reply */

  if (!extract_tube_information (contact_repo, stanza, &close, &tube_type,
          &initiator_handle, &service, &parameters, &tube_id, &portnum,
          &error))
    {
      GibberXmppStanza *reply;

      reply = gibber_iq_helper_new_error_reply (
          gibber_iq_helper_get_request_stanza (stanza), XMPP_ERROR_BAD_REQUEST,
          error->message);
      gibber_xmpp_connection_send (conn, reply, NULL);

      g_error_free (error);
      g_object_unref (reply);
      return;
    }

  DEBUG ("received a tube request, tube id %d", tube_id);

  chan = g_hash_table_lookup (priv->channels,
      GUINT_TO_POINTER (initiator_handle));
  if (close)
  {
    if (chan != NULL)
      {
        salut_tubes_channel_message_close_received (chan, initiator_handle,
            tube_id);
      }
  }
  else
  {
    if (chan == NULL)
      {
        chan = new_tubes_channel (self, initiator_handle, initiator_handle);
        tp_channel_factory_iface_emit_new_channel (self,
            (TpChannelIface *) chan, NULL);
      }

    salut_tubes_channel_message_received (chan, service, tube_type,
        initiator_handle, parameters, tube_id, portnum,
        gibber_iq_helper_get_request_stanza (stanza));
  }
}

static GObject *
salut_tubes_manager_constructor (GType type,
                                  guint n_props,
                                  GObjectConstructParam *props)
{
  GObject *obj;
  SalutTubesManager *self;
  SalutTubesManagerPrivate *priv;

  obj = G_OBJECT_CLASS (salut_tubes_manager_parent_class)->
           constructor (type, n_props, props);

  self = SALUT_TUBES_MANAGER (obj);
  priv = SALUT_TUBES_MANAGER_GET_PRIVATE (self);

  salut_xmpp_connection_manager_add_stanza_filter (
      priv->xmpp_connection_manager, NULL,
      iq_tube_request_filter, iq_tube_request_cb, self);

  return obj;
}

static void
salut_tubes_manager_dispose (GObject *object)
{
  SalutTubesManager *fac = SALUT_TUBES_MANAGER (object);
  SalutTubesManagerPrivate *priv =
    SALUT_TUBES_MANAGER_GET_PRIVATE (fac);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  tp_channel_factory_iface_close_all (TP_CHANNEL_FACTORY_IFACE (object));
  g_assert (priv->channels == NULL);

  if (G_OBJECT_CLASS (salut_tubes_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_tubes_manager_parent_class)->dispose (
        object);
}

static void
salut_tubes_manager_get_property (GObject *object,
                                  guint property_id,
                                  GValue *value,
                                  GParamSpec *pspec)
{
  SalutTubesManager *fac = SALUT_TUBES_MANAGER (object);
  SalutTubesManagerPrivate *priv =
    SALUT_TUBES_MANAGER_GET_PRIVATE (fac);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_CONTACT_MANAGER:
        g_value_set_object (value, priv->contact_manager);
        break;
      case PROP_XMPP_CONNECTION_MANAGER:
        g_value_set_object (value, priv->xmpp_connection_manager);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_tubes_manager_set_property (GObject *object,
                                  guint property_id,
                                  const GValue *value,
                                  GParamSpec *pspec)
{
  SalutTubesManager *fac = SALUT_TUBES_MANAGER (object);
  SalutTubesManagerPrivate *priv =
    SALUT_TUBES_MANAGER_GET_PRIVATE (fac);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      case PROP_CONTACT_MANAGER:
        priv->contact_manager = g_value_get_object (value);
        break;
      case PROP_XMPP_CONNECTION_MANAGER:
        priv->xmpp_connection_manager = g_value_get_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_tubes_manager_class_init (
    SalutTubesManagerClass *salut_tubes_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (
      salut_tubes_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_tubes_manager_class,
      sizeof (SalutTubesManagerPrivate));

  object_class->constructor = salut_tubes_manager_constructor;
  object_class->dispose = salut_tubes_manager_dispose;

  object_class->get_property = salut_tubes_manager_get_property;
  object_class->set_property = salut_tubes_manager_set_property;

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "Salut connection object that owns this Tubes channel factory object.",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_object (
      "contact-manager",
      "SalutContactManager object",
      "Salut Contact Manager associated with the Salut Connection of this "
      "manager",
      SALUT_TYPE_CONTACT_MANAGER,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONTACT_MANAGER,
      param_spec);

  param_spec = g_param_spec_object (
      "xmpp-connection-manager",
      "SalutXmppConnectionManager object",
      "Salut Xmpp Connection Manager associated with the Salut Connection of this "
      "manager",
      SALUT_TYPE_XMPP_CONNECTION_MANAGER,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_XMPP_CONNECTION_MANAGER,
      param_spec);
}


/**
 * tubes_channel_closed_cb:
 *
 * Signal callback for when a Tubes channel is closed. Removes the references
 * that TubesManager holds to them.
 */
static void
tubes_channel_closed_cb (SalutTubesChannel *chan,
                         gpointer user_data)
{
  SalutTubesManager *conn = SALUT_TUBES_MANAGER (user_data);
  SalutTubesManagerPrivate *priv =
    SALUT_TUBES_MANAGER_GET_PRIVATE (conn);
  TpHandle contact_handle;

  if (priv->channels == NULL)
    return;

  g_object_get (chan, "handle", &contact_handle, NULL);

  DEBUG ("removing tubes channel with handle %d", contact_handle);

  g_hash_table_remove (priv->channels, GUINT_TO_POINTER (contact_handle));
}

/**
 * new_tubes_channel
 *
 * Creates the SalutTubes object associated with the given parameters
 */
static SalutTubesChannel *
new_tubes_channel (SalutTubesManager *fac,
                   TpHandle handle,
                   TpHandle initiator)
{
  SalutTubesManagerPrivate *priv;
  TpBaseConnection *conn;
  SalutTubesChannel *chan;
  char *object_path;
  SalutContact *contact;

  g_assert (SALUT_IS_TUBES_MANAGER (fac));

  priv = SALUT_TUBES_MANAGER_GET_PRIVATE (fac);
  conn = (TpBaseConnection *) priv->conn;

  contact = salut_contact_manager_get_contact (priv->contact_manager, handle);
  if (contact == NULL)
    return NULL;

  object_path = g_strdup_printf ("%s/TubesChannel%u", conn->object_path,
      handle);

  chan = g_object_new (SALUT_TYPE_TUBES_CHANNEL,
                       "connection", priv->conn,
                       "object-path", object_path,
                       "handle", handle,
                       "handle-type", TP_HANDLE_TYPE_CONTACT,
                       "contact", contact,
                       "initiator-handle", initiator,
                       "xmpp-connection-manager", priv->xmpp_connection_manager,
                       NULL);

  DEBUG ("object path %s", object_path);

  g_signal_connect (chan, "closed", G_CALLBACK (tubes_channel_closed_cb), fac);

  g_hash_table_insert (priv->channels, GUINT_TO_POINTER (handle), chan);

  g_object_unref (contact);
  g_free (object_path);

  return chan;
}

static void
salut_tubes_manager_iface_close_all (TpChannelFactoryIface *iface)
{
  SalutTubesManager *fac = SALUT_TUBES_MANAGER (iface);
  SalutTubesManagerPrivate *priv =
    SALUT_TUBES_MANAGER_GET_PRIVATE (fac);
  GHashTable *tmp;

  DEBUG ("closing 1-1 tubes channels");

  if (priv->channels == NULL)
    return;

  tmp = priv->channels;
  priv->channels = NULL;
  g_hash_table_destroy (tmp);
}

static void
salut_tubes_manager_iface_connecting (TpChannelFactoryIface *iface)
{
  /* nothing to do */
}

static void
salut_tubes_manager_iface_connected (TpChannelFactoryIface *iface)
{
  /* nothing to do */
}

static void
salut_tubes_manager_iface_disconnected (TpChannelFactoryIface *iface)
{
  /* nothing to do */
}

struct _ForeachData
{
  TpChannelFunc foreach;
  gpointer user_data;
};

static void
_foreach_slave (gpointer key,
                gpointer value,
                gpointer user_data)
{
  struct _ForeachData *data = (struct _ForeachData *) user_data;
  TpChannelIface *chan = TP_CHANNEL_IFACE (value);

  data->foreach (chan, data->user_data);
}

static void
salut_tubes_manager_iface_foreach (TpChannelFactoryIface *iface,
                                    TpChannelFunc foreach,
                                    gpointer user_data)
{
  SalutTubesManager *fac = SALUT_TUBES_MANAGER (iface);
  SalutTubesManagerPrivate *priv =
    SALUT_TUBES_MANAGER_GET_PRIVATE (fac);
  struct _ForeachData data;

  data.user_data = user_data;
  data.foreach = foreach;

  g_hash_table_foreach (priv->channels, _foreach_slave, &data);
}

static TpChannelFactoryRequestStatus
salut_tubes_manager_iface_request (TpChannelFactoryIface *iface,
                                    const gchar *chan_type,
                                    TpHandleType handle_type,
                                    guint handle,
                                    gpointer request,
                                    TpChannelIface **ret,
                                    GError **error)
{
  SalutTubesManager *fac = SALUT_TUBES_MANAGER (iface);
  SalutTubesManagerPrivate *priv =
    SALUT_TUBES_MANAGER_GET_PRIVATE (fac);
  TpHandleRepoIface *contacts_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  SalutTubesChannel *chan;
  TpChannelFactoryRequestStatus status;

  if (tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_TUBES))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;

  if (handle_type != TP_HANDLE_TYPE_CONTACT)
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;

  if (!tp_handle_is_valid (contacts_repo, handle, NULL))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;

  /* Don't support opening a channel to our self handle */
  if (handle == ((TpBaseConnection*) priv->conn)->self_handle)
    {
     g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
         "Can't open a channel to your self handle");
     return TP_CHANNEL_FACTORY_REQUEST_STATUS_ERROR;
    }

  chan = g_hash_table_lookup (priv->channels, GUINT_TO_POINTER (handle));

  status = TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING;
  if (chan == NULL)
    {
      status = TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED;
      chan = new_tubes_channel (fac, handle,
          ((TpBaseConnection*) priv->conn)->self_handle);
      if (chan == NULL)
        {
          DEBUG ("Cannot create a tubes channel with handle %u",
              handle);
          return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;
        }
      tp_channel_factory_iface_emit_new_channel (fac, (TpChannelIface *)chan,
          request);
    }

  g_assert (chan);
  *ret = TP_CHANNEL_IFACE (chan);
  return status;
}

SalutTubesManager *
salut_tubes_manager_new (
    SalutConnection *conn,
    SalutContactManager *contact_manager,
    SalutXmppConnectionManager *xmpp_connection_manager)
{
  g_return_val_if_fail (SALUT_IS_CONNECTION (conn), NULL);
  g_return_val_if_fail (SALUT_IS_CONTACT_MANAGER (contact_manager), NULL);
  g_return_val_if_fail (
      SALUT_IS_XMPP_CONNECTION_MANAGER (xmpp_connection_manager), NULL);

  return g_object_new (
      SALUT_TYPE_TUBES_MANAGER,
      "connection", conn,
      "contact-manager", contact_manager,
      "xmpp-connection-manager", xmpp_connection_manager,
      NULL);
}

static void
salut_tubes_manager_iface_init (gpointer g_iface,
                                 gpointer iface_data)
{
  TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *) g_iface;

  klass->close_all = salut_tubes_manager_iface_close_all;
  klass->connecting = salut_tubes_manager_iface_connecting;
  klass->connected = salut_tubes_manager_iface_connected;
  klass->disconnected = salut_tubes_manager_iface_disconnected;
  klass->foreach = salut_tubes_manager_iface_foreach;
  klass->request = salut_tubes_manager_iface_request;
}
