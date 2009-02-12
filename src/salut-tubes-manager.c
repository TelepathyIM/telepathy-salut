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

#include <gibber/gibber-namespaces.h>
#include <gibber/gibber-iq-helper.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG DEBUG_TUBES

#include "debug.h"
#include "extensions/extensions.h"
#include "salut-connection.h"
#include "salut-capabilities.h"
#include "salut-caps-channel-manager.h"
#include "salut-tubes-channel.h"
#include "salut-muc-manager.h"
#include "salut-muc-channel.h"
#include "salut-self.h"
#include "salut-util.h"
#include "tube-iface.h"
#include "tube-stream.h"


static SalutTubesChannel *new_tubes_channel (SalutTubesManager *fac,
    TpHandle handle, TpHandle initiator, gpointer request_token,
    gboolean requested, GError **error);

static void tubes_channel_closed_cb (SalutTubesChannel *chan,
    gpointer user_data);

static void salut_tubes_manager_iface_init (gpointer g_iface,
    gpointer iface_data);
static void caps_channel_manager_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (SalutTubesManager,
    salut_tubes_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
        salut_tubes_manager_iface_init);
    G_IMPLEMENT_INTERFACE (SALUT_TYPE_CAPS_CHANNEL_MANAGER,
      caps_channel_manager_iface_init));

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
  gulong status_changed_id;
  SalutContactManager *contact_manager;
  SalutXmppConnectionManager *xmpp_connection_manager;

  GHashTable *tubes_channels;

  gboolean dispose_has_run;
};

#define SALUT_TUBES_MANAGER_GET_PRIVATE(obj) \
    ((SalutTubesManagerPrivate *) obj->priv)

typedef struct _TubesCapabilities TubesCapabilities;
struct _TubesCapabilities
{
  /* Stores the list of tubes supported by a contact. We use a hash table. The
   * key is the service name and the value is NULL.
   *
   * It can also be used to store the list of tubes that Salut advertises to
   * support when Salut replies to XEP-0115 Entity Capabilities requests. In
   * this case, a Feature structure is associated with each tube type in order
   * to be returned by salut_tubes_manager_get_feature_list().
   *
   * So the value of the hash table is either NULL (if the variable is related
   * to a contact handle), either a Feature structure (if the variable is
   * related to the self_handle).
   */

  /* gchar *Service -> NULL
   *  or
   * gchar *Service -> Feature *feature
   */
  GHashTable *stream_tube_caps;

  /* gchar *ServiceName -> NULL
   *  or
   * gchar *ServiceName -> Feature *feature
   */
  GHashTable *dbus_tube_caps;

  gboolean tubes_supported;
};

static void
feature_free (gpointer feat)
{
  Feature *feature = (Feature *) feat;

  if (feature == NULL)
    return;

   if (feature->ns != NULL)
     g_free (feature->ns);

  g_slice_free (Feature, feature);
}

static TubesCapabilities *
tubes_capabilities_new (void)
{
  TubesCapabilities *caps;

  caps = g_slice_new (TubesCapabilities);
  caps->stream_tube_caps = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, feature_free);
  caps->dbus_tube_caps = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, feature_free);

  return caps;
}

static void
tubes_capabilities_free (TubesCapabilities *caps)
{
  g_hash_table_destroy (caps->stream_tube_caps);
  g_hash_table_destroy (caps->dbus_tube_caps);
  g_slice_free (TubesCapabilities, caps);
}

static void
salut_tubes_manager_init (SalutTubesManager *self)
{
  SalutTubesManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_TUBES_MANAGER, SalutTubesManagerPrivate);

  self->priv = priv;

  priv->tubes_channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
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

      reply = gibber_iq_helper_new_error_reply (stanza, XMPP_ERROR_BAD_REQUEST,
          error->message);
      gibber_xmpp_connection_send (conn, reply, NULL);

      g_error_free (error);
      g_object_unref (reply);
      return;
    }

  DEBUG ("received a tube request, tube id %d", tube_id);

  chan = g_hash_table_lookup (priv->tubes_channels,
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
    SalutTubeIface *tube;
    GHashTable *channels;
    gboolean tubes_channel_created = FALSE;

    if (chan == NULL)
      {
        GError *e = NULL;

        chan = new_tubes_channel (self, initiator_handle, initiator_handle,
            NULL, FALSE, &e);

        if (chan == NULL)
          {
            DEBUG ("couldn't make new tubes channel: %s", e->message);
            g_error_free (e);
            g_hash_table_destroy (parameters);
            return;
          }

        tubes_channel_created = TRUE;
      }

    tube = salut_tubes_channel_message_received (chan, service, tube_type,
        initiator_handle, parameters, tube_id, portnum, stanza);

    if (tube == NULL)
      {
        if (tubes_channel_created)
          {
            /* Destroy the tubes channel we just created as it's now
             * useless */
            g_hash_table_remove (priv->tubes_channels, GUINT_TO_POINTER (
                  initiator_handle));
          }

        g_hash_table_destroy (parameters);
        return;
      }

    /* announce tubes and tube channels */
    channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
        NULL, NULL);

    if (tubes_channel_created)
      g_hash_table_insert (channels, chan, NULL);

    g_hash_table_insert (channels, tube, NULL);

    tp_channel_manager_emit_new_channels (self, channels);

    g_hash_table_destroy (parameters);
    g_hash_table_destroy (channels);
  }
}

static void
salut_tubes_manager_close_all (SalutTubesManager *self)
{
  SalutTubesManagerPrivate *priv = SALUT_TUBES_MANAGER_GET_PRIVATE (self);

  DEBUG ("closing 1-1 tubes tubes channels");

  if (priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (priv->conn,
          priv->status_changed_id);
      priv->status_changed_id = 0;
    }

  if (priv->tubes_channels != NULL)
    {
      GHashTable *tmp;

      tmp = priv->tubes_channels;
      priv->tubes_channels = NULL;
      g_hash_table_destroy (tmp);
    }
}

static void
connection_status_changed_cb (SalutConnection *conn,
                              guint status,
                              guint reason,
                              SalutTubesManager *self)
{
  switch (status)
    {
    case TP_CONNECTION_STATUS_DISCONNECTED:
      salut_tubes_manager_close_all (self);
      break;
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

  priv->status_changed_id = g_signal_connect (priv->conn,
      "status-changed", (GCallback) connection_status_changed_cb, obj);

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

  salut_tubes_manager_close_all (fac);

  salut_xmpp_connection_manager_remove_stanza_filter (
      priv->xmpp_connection_manager, NULL,
      iq_tube_request_filter, iq_tube_request_cb, object);

  if (priv->contact_manager != NULL)
    {
      g_object_unref (priv->contact_manager);
      priv->contact_manager = NULL;
    }

  if (priv->xmpp_connection_manager != NULL)
    {
      g_object_unref (priv->xmpp_connection_manager);
      priv->xmpp_connection_manager = NULL;
    }

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
        priv->contact_manager = g_value_dup_object (value);
        break;
      case PROP_XMPP_CONNECTION_MANAGER:
        priv->xmpp_connection_manager = g_value_dup_object (value);
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

  if (priv->tubes_channels == NULL)
    return;

  g_object_get (chan, "handle", &contact_handle, NULL);

  DEBUG ("removing tubes channel with handle %d", contact_handle);

  g_hash_table_remove (priv->tubes_channels, GUINT_TO_POINTER (contact_handle));
}

/**
 * new_tubes_channel
 *
 * Creates the SalutTubes object associated with the given parameters
 */
static SalutTubesChannel *
new_tubes_channel (SalutTubesManager *fac,
                   TpHandle handle,
                   TpHandle initiator,
                   gpointer request_token,
                   gboolean requested,
                   GError **error)
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
    {
      TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->conn);
      TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
          base_conn, TP_HANDLE_TYPE_CONTACT);

      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "%s is not online", tp_handle_inspect (contact_repo, handle));
      return NULL;
    }

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
                       "requested", requested,
                       NULL);

  DEBUG ("object path %s", object_path);

  g_signal_connect (chan, "closed", G_CALLBACK (tubes_channel_closed_cb), fac);

  g_hash_table_insert (priv->tubes_channels, GUINT_TO_POINTER (handle), chan);

  g_object_unref (contact);
  g_free (object_path);

  return chan;
}

static void
salut_tubes_manager_foreach_channel (TpChannelManager *manager,
                                     TpExportableChannelFunc foreach,
                                     gpointer user_data)
{
  SalutTubesManager *fac = SALUT_TUBES_MANAGER (manager);
  SalutTubesManagerPrivate *priv =
    SALUT_TUBES_MANAGER_GET_PRIVATE (fac);
  GHashTableIter iter;
  gpointer value;

  g_hash_table_iter_init (&iter, priv->tubes_channels);
  while (g_hash_table_iter_next (&iter, NULL, &value))
  {
    TpExportableChannel *chan = TP_EXPORTABLE_CHANNEL (value);

    /* Add channels of type Channel.Type.Tubes */
    foreach (chan, user_data);

    /* Add channels of type Channel.Type.{Stream|DBus}Tube which live in the
     * SalutTubesChannel object */
    salut_tubes_channel_foreach (SALUT_TUBES_CHANNEL (chan), foreach,
        user_data);
  }
}

static const gchar * const tubes_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};

static const gchar * const old_tubes_channel_allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    NULL
};

static const gchar * const stream_tube_channel_allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    TP_IFACE_CHANNEL_TYPE_STREAM_TUBE ".Service",
    NULL
};

/* Temporarily disabled since the implementation is incomplete. */
#if 0
static const gchar * const dbus_tube_channel_allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    TP_IFACE_CHANNEL_TYPE_DBUS_TUBE ".ServiceName",
    NULL
};
#endif

static void
salut_tubes_manager_foreach_channel_class (
    TpChannelManager *manager,
    TpChannelManagerChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table;
  GValue *value;

  /* 1-1 Channel.Type.Tubes */
  table = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_TUBES);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType",
      value);

  value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetHandleType",
      value);

  func (manager, table, old_tubes_channel_allowed_properties, user_data);

  g_hash_table_destroy (table);

  /* 1-1 Channel.Type.StreamTube */
  table = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType",
      value);

  value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetHandleType",
      value);

  func (manager, table, salut_tube_stream_channel_get_allowed_properties (),
      user_data);

  g_hash_table_destroy (table);

  /* 1-1 Channel.Type.DBusTube */
  /* Temporarily disabled since the implementation is incomplete. */
#if 0
  table = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType",
      value);

  value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetHandleType",
      value);

  func (manager, table, dbus_tube_channel_allowed_properties, user_data);

  g_hash_table_destroy (table);
#endif
}

static gboolean
salut_tubes_manager_requestotron (SalutTubesManager *self,
                                  gpointer request_token,
                                  GHashTable *request_properties,
                                  gboolean require_new)
{
  SalutTubesManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_TUBES_MANAGER, SalutTubesManagerPrivate);
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->conn);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      base_conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle handle;
  GError *error = NULL;
  const gchar *channel_type;
  SalutTubesChannel *tubes_channel;
  const gchar *service = NULL;

  if (tp_asv_get_uint32 (request_properties,
        TP_IFACE_CHANNEL ".TargetHandleType", NULL) != TP_HANDLE_TYPE_CONTACT)
    return FALSE;

  channel_type = tp_asv_get_string (request_properties,
            TP_IFACE_CHANNEL ".ChannelType");

  if (tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_TUBES) &&
  /* Temporarily disabled since the implementation is incomplete. */
  /*  tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE) && */
      tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE))
    return FALSE;

  if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_TUBES))
    {
      if (tp_channel_manager_asv_has_unknown_properties (request_properties,
              tubes_channel_fixed_properties,
              old_tubes_channel_allowed_properties,
              &error))
        goto error;
    }
  else if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE))
    {
      if (tp_channel_manager_asv_has_unknown_properties (request_properties,
              tubes_channel_fixed_properties,
              salut_tube_stream_channel_get_allowed_properties (),
              &error))
        goto error;

      /* "Service" is a mandatory, not-fixed property */
      service = tp_asv_get_string (request_properties,
                TP_IFACE_CHANNEL_TYPE_STREAM_TUBE ".Service");
      if (service == NULL)
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "StreamTube requests must include '%s'",
              TP_IFACE_CHANNEL_TYPE_STREAM_TUBE ".Service");
          goto error;
        }
    }
/* Temporarily disabled since the implementation is incomplete. */
#if 0
  else if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE))
    {
      GError *err = NULL;

      if (tp_channel_manager_asv_has_unknown_properties (request_properties,
              tubes_channel_fixed_properties,
              dbus_tube_channel_allowed_properties,
              &error))
        goto error;

      /* "ServiceName" is a mandatory, not-fixed property */
      service = tp_asv_get_string (request_properties,
                TP_IFACE_CHANNEL_TYPE_DBUS_TUBE ".ServiceName");
      if (service == NULL)
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
              "Request missed a mandatory property '%s'",
              TP_IFACE_CHANNEL_TYPE_DBUS_TUBE ".ServiceName");
          goto error;
        }

      if (!tp_dbus_check_valid_bus_name (service, TP_DBUS_NAME_TYPE_WELL_KNOWN,
            &err))
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "Invalid ServiceName: %s", err->message);
          g_error_free (err);
          goto error;
        }
    }
#endif

  handle = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandle", NULL);

  if (!tp_handle_is_valid (contact_repo, handle, &error))
    goto error;

  /* Don't support opening a channel to our self handle */
  if (handle == base_conn->self_handle)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Can't open a channel to your self handle");
      goto error;
    }

  tubes_channel = g_hash_table_lookup (priv->tubes_channels,
      GUINT_TO_POINTER (handle));

  if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_TUBES))
    {
      if (tubes_channel == NULL)
        {
          GSList *tokens = NULL;

          tubes_channel = new_tubes_channel (self, handle,
              base_conn->self_handle, request_token, TRUE, &error);

          if (tubes_channel == NULL)
            goto error;

          tokens = g_slist_prepend (tokens, request_token);

          tp_channel_manager_emit_new_channel (self,
              TP_EXPORTABLE_CHANNEL (tubes_channel), tokens);

          g_slist_free (tokens);
          return TRUE;
        }

      if (require_new)
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "A tube channel with contact #%u already exists", handle);
          DEBUG ("A tube channel with contact #%u already exists", handle);
          goto error;
        }

      tp_channel_manager_emit_request_already_satisfied (self,
          request_token, TP_EXPORTABLE_CHANNEL (tubes_channel));
      return TRUE;
    }
  else
    {
      SalutTubeIface *new_channel;
      GSList *tokens = NULL;
      gboolean tubes_channel_created = FALSE;
      GHashTable *channels;

      if (tubes_channel == NULL)
        {
          tubes_channel = new_tubes_channel (self, handle,
              base_conn->self_handle, NULL, FALSE, &error);
          if (tubes_channel == NULL)
            goto error;

          tubes_channel_created = TRUE;
        }

      new_channel = salut_tubes_channel_tube_request (tubes_channel,
          request_token, request_properties, require_new);
      g_assert (new_channel != NULL);

      if (request_token != NULL)
        tokens = g_slist_prepend (NULL, request_token);

      channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
          NULL, NULL);
      g_hash_table_insert (channels, tubes_channel, NULL);
      g_hash_table_insert (channels, new_channel, tokens);

      tp_channel_manager_emit_new_channels (self, channels);

      g_hash_table_destroy (channels);
      g_slist_free (tokens);
      return TRUE;
    }

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}

static gboolean
salut_tubes_manager_create_channel (TpChannelManager *manager,
                                    gpointer request_token,
                                    GHashTable *request_properties)
{
  SalutTubesManager *self = SALUT_TUBES_MANAGER (manager);

  return salut_tubes_manager_requestotron (self, request_token,
      request_properties, TRUE);
}

static gboolean
salut_tubes_manager_request_channel (TpChannelManager *manager,
                                  gpointer request_token,
                                  GHashTable *request_properties)
{
  SalutTubesManager *self = SALUT_TUBES_MANAGER (manager);

  return salut_tubes_manager_requestotron (self, request_token,
      request_properties, FALSE);
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
  TpChannelManagerIface *iface = (TpChannelManagerIface *) g_iface;

  iface->foreach_channel = salut_tubes_manager_foreach_channel;
  iface->foreach_channel_class = salut_tubes_manager_foreach_channel_class;
  iface->create_channel = salut_tubes_manager_create_channel;
  iface->request_channel = salut_tubes_manager_request_channel;
}

static void
add_service_to_array (gchar *service,
                      GPtrArray *arr,
                      TpTubeType type,
                      TpHandle handle)
{
  GValue monster = {0, };
  GHashTable *fixed_properties;
  GValue *channel_type_value;
  GValue *target_handle_type_value;
  gchar *tube_allowed_properties[] =
      {
        TP_IFACE_CHANNEL ".TargetHandle",
        NULL
      };

  g_assert (type == TP_TUBE_TYPE_STREAM || type == TP_TUBE_TYPE_DBUS);

  g_value_init (&monster, TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS);
  g_value_take_boxed (&monster,
      dbus_g_type_specialized_construct (
        TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS));

  fixed_properties = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  channel_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  if (type == TP_TUBE_TYPE_STREAM)
    g_value_set_static_string (channel_type_value,
        TP_IFACE_CHANNEL_TYPE_STREAM_TUBE);
  else
    g_value_set_static_string (channel_type_value,
        TP_IFACE_CHANNEL_TYPE_DBUS_TUBE);
  g_hash_table_insert (fixed_properties, TP_IFACE_CHANNEL ".ChannelType",
      channel_type_value);

  target_handle_type_value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (target_handle_type_value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (fixed_properties,
      TP_IFACE_CHANNEL ".TargetHandleType", target_handle_type_value);

  target_handle_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_string (target_handle_type_value, service);
  if (type == TP_TUBE_TYPE_STREAM)
    g_hash_table_insert (fixed_properties,
        TP_IFACE_CHANNEL_TYPE_STREAM_TUBE ".Service",
        target_handle_type_value);
  else
    g_hash_table_insert (fixed_properties,
        TP_IFACE_CHANNEL_TYPE_DBUS_TUBE ".ServiceName",
        target_handle_type_value);

  dbus_g_type_struct_set (&monster,
      0, fixed_properties,
      1, tube_allowed_properties,
      G_MAXUINT);

  g_hash_table_destroy (fixed_properties);

  g_ptr_array_add (arr, g_value_get_boxed (&monster));
}

static void
add_generic_tube_caps (GPtrArray *arr)
{
  GValue monster1 = {0,};
  GHashTable *fixed_properties;
  GValue *channel_type_value;
  GValue *target_handle_type_value;

  /* StreamTube */
  g_value_init (&monster1, TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS);
  g_value_take_boxed (&monster1,
      dbus_g_type_specialized_construct (
        TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS));

  fixed_properties = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);

  channel_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (channel_type_value,
      TP_IFACE_CHANNEL_TYPE_STREAM_TUBE);

  g_hash_table_insert (fixed_properties, TP_IFACE_CHANNEL ".ChannelType",
      channel_type_value);

  target_handle_type_value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (target_handle_type_value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (fixed_properties,
      TP_IFACE_CHANNEL ".TargetHandleType", target_handle_type_value);

  dbus_g_type_struct_set (&monster1,
      0, fixed_properties,
      1, stream_tube_channel_allowed_properties,
      G_MAXUINT);

  g_hash_table_destroy (fixed_properties);
  g_ptr_array_add (arr, g_value_get_boxed (&monster1));

  /* FIXME: enable once D-Bus tube new API are implemented */
#if 0
  /* DBusTube */
  g_value_init (&monster2, TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS);
  g_value_take_boxed (&monster2,
      dbus_g_type_specialized_construct (
        TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS));

  fixed_properties = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);

  channel_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (channel_type_value,
      SALUT_IFACE_CHANNEL_TYPE_DBUS_TUBE);

  g_hash_table_insert (fixed_properties, TP_IFACE_CHANNEL ".ChannelType",
      channel_type_value);

  target_handle_type_value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (target_handle_type_value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (fixed_properties,
      TP_IFACE_CHANNEL ".TargetHandleType", target_handle_type_value);

  dbus_g_type_struct_set (&monster2,
      0, fixed_properties,
      1, gabble_tube_dbus_channel_get_allowed_properties (),
      G_MAXUINT);

  g_hash_table_destroy (fixed_properties);
  g_ptr_array_add (arr, g_value_get_boxed (&monster2));
#endif
}

static void
salut_tubes_manager_get_contact_caps (
    SalutCapsChannelManager *manager,
    SalutConnection *conn,
    TpHandle handle,
    GPtrArray *arr)
{
  SalutTubesManager *self = SALUT_TUBES_MANAGER (manager);
  SalutTubesManagerPrivate *priv = SALUT_TUBES_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *base = (TpBaseConnection *) conn;
  TubesCapabilities *caps;
  GHashTable *stream_tube_caps;
  GHashTable *dbus_tube_caps;
  GHashTableIter tube_caps_iter;
  gpointer service;
  GHashTable *per_channel_manager_caps;

  g_assert (handle != 0);

  if (handle == base->self_handle)
    {
      SalutSelf *salut_self;
      g_object_get (conn, "self", &salut_self, NULL);
      per_channel_manager_caps = salut_self->per_channel_manager_caps;
      g_object_unref (salut_self);
    }
  else
    {
      SalutContact *contact;

      contact = salut_contact_manager_get_contact (
          priv->contact_manager, handle);

      if (contact == NULL)
        return;

      per_channel_manager_caps = contact->per_channel_manager_caps;
      g_object_unref (contact);
    }

  if (per_channel_manager_caps == NULL)
    return;

  caps = g_hash_table_lookup (per_channel_manager_caps, manager);
  if (caps == NULL)
    return;

  if (!caps->tubes_supported)
    return;

  add_generic_tube_caps (arr);

  stream_tube_caps = caps->stream_tube_caps;
  dbus_tube_caps = caps->dbus_tube_caps;

  if (stream_tube_caps != NULL)
    {
      g_hash_table_iter_init (&tube_caps_iter, stream_tube_caps);
      while (g_hash_table_iter_next (&tube_caps_iter, &service,
            NULL))
        {
          add_service_to_array (service, arr, TP_TUBE_TYPE_STREAM, handle);
        }
    }

  if (dbus_tube_caps != NULL)
    {
      g_hash_table_iter_init (&tube_caps_iter, dbus_tube_caps);
      while (g_hash_table_iter_next (&tube_caps_iter, &service,
            NULL))
        {
          add_service_to_array (service, arr, TP_TUBE_TYPE_DBUS, handle);
        }
    }
}

static void
salut_tubes_manager_get_feature_list (
    SalutCapsChannelManager *manager,
    gpointer specific_caps,
    GSList **features)
{
  TubesCapabilities *caps = specific_caps;
  GHashTableIter iter;
  gpointer service;
  gpointer feat;

  g_hash_table_iter_init (&iter, caps->stream_tube_caps);
  while (g_hash_table_iter_next (&iter, &service, &feat))
    {
      *features = g_slist_append (*features, feat);
    }

  g_hash_table_iter_init (&iter, caps->dbus_tube_caps);
  while (g_hash_table_iter_next (&iter, &service, &feat))
    {
      *features = g_slist_append (*features, feat);
    }
}

static gboolean
_parse_caps_item (GibberXmppNode *node, gpointer user_data)
{
  TubesCapabilities *caps = (TubesCapabilities *) user_data;
  const gchar *var;

  if (0 != strcmp (node->name, "feature"))
    return TRUE;

  var = gibber_xmpp_node_get_attribute (node, "var");

  if (NULL == var)
    return TRUE;

  if (!g_str_has_prefix (var, GIBBER_TELEPATHY_NS_TUBES))
    return TRUE;

  /* tubes generic cap or service specific */
  caps->tubes_supported = TRUE;

  if (g_str_has_prefix (var, GIBBER_TELEPATHY_NS_TUBES "/"))
    {
      /* http://telepathy.freedesktop.org/xmpp/tubes/$type#$service */
      var += strlen (GIBBER_TELEPATHY_NS_TUBES "/");
      if (g_str_has_prefix (var, "stream#"))
        {
          gchar *service;
          var += strlen ("stream#");
          service = g_strdup (var);
          g_hash_table_insert (caps->stream_tube_caps, service, NULL);
        }
      else if (g_str_has_prefix (var, "dbus#"))
        {
          gchar *service;
          var += strlen ("dbus#");
          service = g_strdup (var);
          g_hash_table_insert (caps->dbus_tube_caps, service, NULL);
        }
    }

  return TRUE;
}

static gpointer
salut_tubes_manager_parse_caps (
    SalutCapsChannelManager *manager,
    GibberXmppNode *node)
{
  TubesCapabilities *caps;

  caps = tubes_capabilities_new ();
  if (node != NULL)
    gibber_xmpp_node_each_child (node, _parse_caps_item, caps);

  return caps;
}

static void
salut_tubes_manager_free_caps (
    SalutCapsChannelManager *manager,
    gpointer data)
{
 TubesCapabilities *caps = data;
 tubes_capabilities_free (caps);
}

static void
copy_caps_helper (gpointer key, gpointer value, gpointer user_data)
{
  GHashTable *out = user_data;
  gchar *str = key;

  g_hash_table_insert (out, g_strdup (str), NULL);
}

static void
salut_tubes_manager_copy_caps (
    SalutCapsChannelManager *manager,
    gpointer *specific_caps_out,
    gpointer specific_caps_in)
{
  TubesCapabilities *caps_in = specific_caps_in;
  TubesCapabilities *caps_out = tubes_capabilities_new ();

  g_hash_table_foreach (caps_in->stream_tube_caps, copy_caps_helper,
      caps_out->stream_tube_caps);

  g_hash_table_foreach (caps_in->dbus_tube_caps, copy_caps_helper,
      caps_out->dbus_tube_caps);

  caps_out->tubes_supported = caps_in->tubes_supported;

  *specific_caps_out = caps_out;
}

static void
salut_tubes_manager_update_caps (
    SalutCapsChannelManager *manager,
    gpointer *specific_caps_out,
    gpointer specific_caps_in)
{
  TubesCapabilities *caps_out = (TubesCapabilities *) specific_caps_out;
  TubesCapabilities *caps_in = (TubesCapabilities *) specific_caps_in;

  if (caps_in == NULL)
    return;

  tp_g_hash_table_update (caps_out->stream_tube_caps,
      caps_in->stream_tube_caps, (GBoxedCopyFunc) g_strdup, NULL);
  tp_g_hash_table_update (caps_out->dbus_tube_caps,
      caps_in->dbus_tube_caps, (GBoxedCopyFunc) g_strdup, NULL);
}

static gboolean
hash_table_is_subset (GHashTable *superset,
                      GHashTable *subset)
{
  GHashTableIter iter;
  gpointer look_for;

  g_hash_table_iter_init (&iter, subset);
  while (g_hash_table_iter_next (&iter, &look_for, NULL))
    {
      if (!g_hash_table_lookup_extended (superset, look_for, NULL, NULL))
        /* One of subset's key is not in superset */
        return FALSE;
    }

  return TRUE;
}

static gboolean
salut_tubes_manager_caps_diff (
    SalutCapsChannelManager *manager,
    TpHandle handle,
    gpointer specific_old_caps,
    gpointer specific_new_caps)
{
  TubesCapabilities *old_caps = specific_old_caps;
  TubesCapabilities *new_caps = specific_new_caps;

  if (old_caps == new_caps)
    return FALSE;

  if (old_caps == NULL || new_caps == NULL)
    return TRUE;

  if (old_caps->tubes_supported != new_caps->tubes_supported)
    return TRUE;

  if (g_hash_table_size (old_caps->stream_tube_caps) !=
      g_hash_table_size (new_caps->stream_tube_caps))
    return TRUE;

  if (g_hash_table_size (old_caps->dbus_tube_caps) !=
      g_hash_table_size (new_caps->dbus_tube_caps))
    return TRUE;

  /* Hash tables have the same size */
  if (!hash_table_is_subset (new_caps->stream_tube_caps,
        old_caps->stream_tube_caps))
    return TRUE;

  if (!hash_table_is_subset (new_caps->dbus_tube_caps,
        old_caps->dbus_tube_caps))
    return TRUE;

  return FALSE;
}

static void
salut_tubes_manager_add_cap (SalutCapsChannelManager *manager,
                             SalutConnection *conn,
                             TpHandle handle,
                             GHashTable *cap)
{
  SalutTubesManager *self = SALUT_TUBES_MANAGER (manager);
  SalutTubesManagerPrivate *priv = SALUT_TUBES_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *base = (TpBaseConnection *) conn;
  /* if the GHashTable is not allocated, we want to do it and update the
   * pointer in SalutSelf or SalutContact. Hence the type "GHashTable **" */
  GHashTable **per_channel_manager_caps;
  TubesCapabilities *caps;
  const gchar *channel_type;
  SalutContact *contact = NULL;

  channel_type = tp_asv_get_string (cap,
            TP_IFACE_CHANNEL ".ChannelType");

  /* this channel is not for this factory */
  if (tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_TUBES) &&
      tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE) &&
      tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE))
    return;

  if (tp_asv_get_uint32 (cap,
        TP_IFACE_CHANNEL ".TargetHandleType", NULL) != TP_HANDLE_TYPE_CONTACT)
    return;

  if (handle == base->self_handle)
    {
      SalutSelf *salut_self;
      g_object_get (conn, "self", &salut_self, NULL);
      per_channel_manager_caps = &salut_self->per_channel_manager_caps;
      g_object_unref (salut_self);
    }
  else
    {
      contact = salut_contact_manager_get_contact (
          priv->contact_manager, handle);

      if (contact == NULL)
        return;

      per_channel_manager_caps = &contact->per_channel_manager_caps;
    }

  if (*per_channel_manager_caps == NULL)
    *per_channel_manager_caps = g_hash_table_new (NULL, NULL);

  caps = g_hash_table_lookup (*per_channel_manager_caps, manager);
  if (caps == NULL)
    {
      caps = tubes_capabilities_new ();
      g_hash_table_insert (*per_channel_manager_caps, manager, caps);

      if (handle == base->self_handle)
        /* We always support generic tubes caps */
        caps->tubes_supported = TRUE;
    }

  if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE))
    {
      Feature *feat = g_slice_new (Feature);
      gchar *service = g_strdup (tp_asv_get_string (cap,
          TP_IFACE_CHANNEL_TYPE_STREAM_TUBE ".Service"));
      feat->feature_type = FEATURE_OPTIONAL;
      feat->ns = g_strdup_printf ("%s/stream#%s", GIBBER_TELEPATHY_NS_TUBES,
          service);
      g_hash_table_insert (caps->stream_tube_caps, service, feat);
    }
  else if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_DBUS_TUBE))
    {
      Feature *feat = g_slice_new (Feature);
      gchar *service = g_strdup (tp_asv_get_string (cap,
          TP_IFACE_CHANNEL_TYPE_DBUS_TUBE ".ServiceName"));
      feat->feature_type = FEATURE_OPTIONAL;
      feat->ns = g_strdup_printf ("%s/dbus#%s", GIBBER_TELEPATHY_NS_TUBES,
          service);
      g_hash_table_insert (caps->dbus_tube_caps, service, feat);
    }

  if (contact != NULL)
    g_object_unref (contact);
}


static void
caps_channel_manager_iface_init (gpointer g_iface,
                                 gpointer iface_data)
{
  SalutCapsChannelManagerIface *iface = g_iface;

  iface->get_contact_caps = salut_tubes_manager_get_contact_caps;
  iface->get_feature_list = salut_tubes_manager_get_feature_list;
  iface->parse_caps = salut_tubes_manager_parse_caps;
  iface->free_caps = salut_tubes_manager_free_caps;
  iface->copy_caps = salut_tubes_manager_copy_caps;
  iface->update_caps = salut_tubes_manager_update_caps;
  iface->caps_diff = salut_tubes_manager_caps_diff;
  iface->add_cap = salut_tubes_manager_add_cap;
}
