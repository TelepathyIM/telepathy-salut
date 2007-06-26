/*
 * tube-dbus.c - Source for SalutTubeDBus
 * Copyright (C) 2007 Ltd.
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

#include "tube-dbus.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#define DEBUG_FLAG DEBUG_TUBES
#include "debug.h"

#include "salut-connection.h"
#include "namespaces.h"
#include "tube-iface.h"

#include <gibber/gibber-bytestream-ibb.h>
#include <gibber/gibber-muc-connection.h>

#include <telepathy-glib/svc-unstable.h>
#include <telepathy-glib/util.h>

/*
#include "bytestream-factory.h"
*/
#include "tube-dbus-signals-marshal.h"

static void
tube_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (SalutTubeDBus, salut_tube_dbus, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (SALUT_TYPE_TUBE_IFACE, tube_iface_init));

/* signals */
enum
{
  OPENED,
  CLOSED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_HANDLE,
  PROP_HANDLE_TYPE,
  PROP_SELF_HANDLE,
  PROP_MUC_CONNECTION,
  PROP_ID,
  PROP_BYTESTREAM,
  PROP_STREAM_ID,
  PROP_TYPE,
  PROP_INITIATOR,
  PROP_SERVICE,
  PROP_PARAMETERS,
  PROP_STATE,
  PROP_DBUS_ADDRESS,
  PROP_DBUS_NAME,
  PROP_DBUS_NAMES,
  LAST_PROPERTY
};

typedef struct _SalutTubeDBusPrivate SalutTubeDBusPrivate;
struct _SalutTubeDBusPrivate
{
  SalutConnection *conn;
  TpHandle handle;
  TpHandleType handle_type;
  TpHandle self_handle;
  GibberMucConnection *muc_connection;
  guint id;
  GibberBytestreamIBB *bytestream;
  gchar *stream_id;
  TpHandle initiator;
  gchar *service;
  GHashTable *parameters;

  /* our unique D-Bus name on the virtual tube bus */
  gchar *dbus_local_name;
  /* the address that we are listening for D-Bus connections on */
  gchar *dbus_srv_addr;
  /* the server that's listening on dbus_srv_addr */
  DBusServer *dbus_srv;
  /* the connection to dbus_srv from a local client, or NULL */
  DBusConnection *dbus_conn;
  /* mapping of contact handle -> D-Bus name */
  GHashTable *dbus_names;

  gboolean dispose_has_run;
};

#define SALUT_TUBE_DBUS_GET_PRIVATE(obj) \
    ((SalutTubeDBusPrivate *) obj->priv)

static void data_received_cb (GibberBytestreamIBB *ibb, const gchar *from,
    GString *data, gpointer user_data);

/*
 * Characters used are permissible both in filenames and in D-Bus names. (See
 * D-Bus specification for restrictions.)
 */
static void
generate_ascii_string (guint len,
                       gchar *buf)
{
  const gchar *chars =
    "0123456789"
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "_-";
  guint i;

  for (i = 0; i < len; i++)
    buf[i] = chars[g_random_int_range (0, 64)];
}

struct _find_contact_data
{
  const gchar *contact;
  TpHandle handle;
};

static DBusHandlerResult
filter_cb (DBusConnection *conn,
           DBusMessage *msg,
           void *data)
{
  SalutTubeDBus *tube = SALUT_TUBE_DBUS (data);
  SalutTubeDBusPrivate *priv = SALUT_TUBE_DBUS_GET_PRIVATE (tube);
  gchar *marshalled = NULL;
  gint len;

  if (dbus_message_get_type (msg) == DBUS_MESSAGE_TYPE_SIGNAL &&
      !tp_strdiff (dbus_message_get_interface (msg),
        "org.freedesktop.DBus.Local") &&
      !tp_strdiff (dbus_message_get_member (msg), "Disconnected"))
    {
      /* connection was disconnected */
      DEBUG ("connection was disconnected");
      dbus_connection_close (priv->dbus_conn);
      dbus_connection_unref (priv->dbus_conn);
      priv->dbus_conn = NULL;
      goto out;
    }

  dbus_message_set_sender (msg, priv->dbus_local_name);

  if (!dbus_message_marshal (msg, &marshalled, &len))
    goto out;

  gibber_bytestream_ibb_send (priv->bytestream, len, marshalled);

out:
  if (marshalled != NULL)
    g_free (marshalled);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static void
new_connection_cb (DBusServer *server,
                   DBusConnection *conn,
                   void *data)
{
  SalutTubeDBus *tube = SALUT_TUBE_DBUS (data);
  SalutTubeDBusPrivate *priv = SALUT_TUBE_DBUS_GET_PRIVATE (tube);

  if (priv->dbus_conn != NULL)
    /* we already have a connection; drop this new one */
    /* return without reffing conn means it will be dropped */
    return;

  DEBUG ("got connection");

  dbus_connection_ref (conn);
  dbus_connection_setup_with_g_main (conn, NULL);
  dbus_connection_add_filter (conn, filter_cb, tube, NULL);
  priv->dbus_conn = conn;
}

static void
tube_dbus_open (SalutTubeDBus *self)
{
  SalutTubeDBusPrivate *priv = SALUT_TUBE_DBUS_GET_PRIVATE (self);
  DBusError error = {0,};
  gchar suffix[8];

  g_signal_connect (priv->bytestream, "data-received",
      G_CALLBACK (data_received_cb), self);

  generate_ascii_string (8, suffix);
  priv->dbus_srv_addr = g_strdup_printf (
      "unix:path=/tmp/dbus-salut-%.8s", suffix);
  DEBUG ("listening on %s", priv->dbus_srv_addr);
  priv->dbus_srv = dbus_server_listen (priv->dbus_srv_addr, &error);

  /* XXX: if dbus_server_listen fails, we should retry with different
   * addresses, then close the tube if we give up
   */
  g_assert (priv->dbus_srv);

  dbus_server_set_new_connection_function (priv->dbus_srv, new_connection_cb,
      self, NULL);
  dbus_server_setup_with_g_main (priv->dbus_srv, NULL);
}

static void
salut_tube_dbus_init (SalutTubeDBus *self)
{
  SalutTubeDBusPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_TUBE_DBUS, SalutTubeDBusPrivate);
  gchar suffix[8];

  self->priv = priv;

  priv->bytestream = NULL;
  priv->muc_connection = NULL;
  priv->dispose_has_run = FALSE;

  /* XXX: check this doesn't clash with other bus names */
  /* this has to contain at least two dot-separated components */

  generate_ascii_string (8, suffix);
  priv->dbus_local_name = g_strdup_printf (":1.%.8s", suffix);
  priv->dbus_names = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_free);

  DEBUG ("local name: %s", priv->dbus_local_name);
}

static void
unref_handle_foreach (gpointer key,
                      gpointer value,
                      gpointer user_data)
{
  TpHandle handle = GPOINTER_TO_UINT (key);
  SalutTubeDBus *self = (SalutTubeDBus *) user_data;
  SalutTubeDBusPrivate *priv = SALUT_TUBE_DBUS_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  tp_handle_unref (contact_repo, handle);
}

static TpTubeState
get_tube_state (SalutTubeDBus *self)
{
  SalutTubeDBusPrivate *priv = SALUT_TUBE_DBUS_GET_PRIVATE (self);
  GibberBytestreamIBBState bytestream_state;

  if (priv->bytestream == NULL)
    /* bytestream not yet created as we're waiting for the SI reply */
    return TP_TUBE_STATE_REMOTE_PENDING;

  g_object_get (priv->bytestream, "state", &bytestream_state, NULL);

  if (bytestream_state == GIBBER_BYTESTREAM_IBB_STATE_OPEN)
    return TP_TUBE_STATE_OPEN;

  else if (bytestream_state == GIBBER_BYTESTREAM_IBB_STATE_LOCAL_PENDING ||
      bytestream_state == GIBBER_BYTESTREAM_IBB_STATE_ACCEPTED)
    return TP_TUBE_STATE_LOCAL_PENDING;

  else if (bytestream_state == GIBBER_BYTESTREAM_IBB_STATE_INITIATING)
    return TP_TUBE_STATE_REMOTE_PENDING;

  else
    g_assert_not_reached ();
  return TP_TUBE_STATE_REMOTE_PENDING;
}

static void
bytestream_state_changed_cb (GibberBytestreamIBB *bytestream,
                             GibberBytestreamIBBState state,
                             gpointer user_data)
{
  SalutTubeDBus *self = SALUT_TUBE_DBUS (user_data);
  SalutTubeDBusPrivate *priv = SALUT_TUBE_DBUS_GET_PRIVATE (self);

  if (state == GIBBER_BYTESTREAM_IBB_STATE_CLOSED)
    {
      if (priv->bytestream != NULL)
        {
          g_object_unref (priv->bytestream);
          priv->bytestream = NULL;
        }

      g_signal_emit (G_OBJECT (self), signals[CLOSED], 0);
    }
  else if (state == GIBBER_BYTESTREAM_IBB_STATE_OPEN)
    {
      tube_dbus_open (self);
      g_signal_emit (G_OBJECT (self), signals[OPENED], 0);
    }
}

static void
salut_tube_dbus_dispose (GObject *object)
{
  SalutTubeDBus *self = SALUT_TUBE_DBUS (object);
  SalutTubeDBusPrivate *priv = SALUT_TUBE_DBUS_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  if (priv->dispose_has_run)
    return;

  if (priv->bytestream)
    {
      gibber_bytestream_ibb_close (priv->bytestream);
    }

  if (priv->dbus_conn)
    {
      dbus_connection_close (priv->dbus_conn);
      dbus_connection_unref (priv->dbus_conn);
    }

  if (priv->dbus_srv)
    dbus_server_unref (priv->dbus_srv);

  if (priv->dbus_srv_addr)
    g_free (priv->dbus_srv_addr);

  if (priv->dbus_local_name)
    g_free (priv->dbus_local_name);

  if (priv->dbus_names)
    {
      g_hash_table_foreach (priv->dbus_names, unref_handle_foreach, self);
      g_hash_table_destroy (priv->dbus_names);
    }

  if (priv->muc_connection != NULL)
    {
      g_object_unref (priv->muc_connection);
    }

  tp_handle_unref (contact_repo, priv->initiator);

  priv->dispose_has_run = TRUE;

  if (G_OBJECT_CLASS (salut_tube_dbus_parent_class)->dispose)
    G_OBJECT_CLASS (salut_tube_dbus_parent_class)->dispose (object);
}

static void
salut_tube_dbus_finalize (GObject *object)
{
  SalutTubeDBus *self = SALUT_TUBE_DBUS (object);
  SalutTubeDBusPrivate *priv = SALUT_TUBE_DBUS_GET_PRIVATE (self);

  g_free (priv->stream_id);
  g_free (priv->service);
  g_hash_table_destroy (priv->parameters);

  G_OBJECT_CLASS (salut_tube_dbus_parent_class)->finalize (object);
}

static void
salut_tube_dbus_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  SalutTubeDBus *self = SALUT_TUBE_DBUS (object);
  SalutTubeDBusPrivate *priv = SALUT_TUBE_DBUS_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_HANDLE:
        g_value_set_uint (value, priv->handle);
        break;
      case PROP_HANDLE_TYPE:
        g_value_set_uint (value, priv->handle_type);
        break;
      case PROP_SELF_HANDLE:
        g_value_set_uint (value, priv->self_handle);
        break;
      case PROP_MUC_CONNECTION:
        g_value_set_object (value, priv->muc_connection);
        break;
      case PROP_ID:
        g_value_set_uint (value, priv->id);
        break;
      case PROP_BYTESTREAM:
        g_value_set_object (value, priv->bytestream);
        break;
      case PROP_STREAM_ID:
        g_value_set_string (value, priv->stream_id);
        break;
      case PROP_TYPE:
        g_value_set_uint (value, TP_TUBE_TYPE_DBUS);
        break;
      case PROP_INITIATOR:
        g_value_set_uint (value, priv->initiator);
        break;
      case PROP_SERVICE:
        g_value_set_string (value, priv->service);
        break;
      case PROP_PARAMETERS:
        g_value_set_boxed (value, priv->parameters);
        break;
      case PROP_STATE:
        g_value_set_uint (value, get_tube_state (self));
        break;
      case PROP_DBUS_ADDRESS:
        g_value_set_string (value, priv->dbus_srv_addr);
        break;
      case PROP_DBUS_NAME:
        g_value_set_string (value, priv->dbus_local_name);
        break;
      case PROP_DBUS_NAMES:
        g_value_set_boxed (value, priv->dbus_names);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_tube_dbus_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  SalutTubeDBus *self = SALUT_TUBE_DBUS (object);
  SalutTubeDBusPrivate *priv = SALUT_TUBE_DBUS_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      case PROP_HANDLE:
        priv->handle = g_value_get_uint (value);
        break;
      case PROP_HANDLE_TYPE:
        priv->handle_type = g_value_get_uint (value);
        break;
      case PROP_SELF_HANDLE:
        priv->self_handle = g_value_get_uint (value);
        break;
      case PROP_MUC_CONNECTION:
        priv->muc_connection = g_value_get_object (value);
        g_object_ref (priv->muc_connection);
        break;
      case PROP_ID:
        priv->id = g_value_get_uint (value);
        break;
      case PROP_BYTESTREAM:
        if (priv->bytestream == NULL)
          {
            GibberBytestreamIBBState state;

            priv->bytestream = g_value_get_object (value);
            g_object_ref (priv->bytestream);

            g_object_get (priv->bytestream, "state", &state, NULL);
            if (state == GIBBER_BYTESTREAM_IBB_STATE_OPEN)
              {
                tube_dbus_open (self);
              }

            g_signal_connect (priv->bytestream, "state-changed",
                G_CALLBACK (bytestream_state_changed_cb), self);
          }
        break;
      case PROP_STREAM_ID:
        g_free (priv->stream_id);
        priv->stream_id = g_value_dup_string (value);
        break;
      case PROP_INITIATOR:
        priv->initiator = g_value_get_uint (value);
        break;
      case PROP_SERVICE:
        g_free (priv->service);
        priv->service = g_value_dup_string (value);
        break;
      case PROP_PARAMETERS:
        priv->parameters = g_value_get_boxed (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static GObject *
salut_tube_dbus_constructor (GType type,
                              guint n_props,
                              GObjectConstructParam *props)
{
  GObject *obj;
  SalutTubeDBus *self;
  SalutTubeDBusPrivate *priv;
  TpHandleRepoIface *contact_repo;
  TpHandleRepoIface *handles_repo;

  obj = G_OBJECT_CLASS (salut_tube_dbus_parent_class)->
           constructor (type, n_props, props);
  self = SALUT_TUBE_DBUS (obj);

  priv = SALUT_TUBE_DBUS_GET_PRIVATE (self);

  handles_repo = tp_base_connection_get_handles
      ((TpBaseConnection *) priv->conn, priv->handle_type);

  /* Ref the initiator handle */
  g_assert (priv->conn != NULL);
  g_assert (priv->initiator != 0);
  contact_repo = tp_base_connection_get_handles
      ((TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  tp_handle_ref (contact_repo, priv->initiator);

  g_assert (priv->self_handle != 0);
  if (priv->handle_type == TP_HANDLE_TYPE_ROOM)
    {
      /*
       * We have to create an IBB bytestream that will be
       * used by this MUC tube to communicate.
       *
       * We don't create the bytestream of private D-Bus tube yet.
       * It will be when we'll receive the answer of the SI request
       */
      GibberBytestreamIBB *bytestream;
      const gchar *peer_id;

      g_assert (priv->muc_connection != NULL);
      g_assert (priv->stream_id != NULL);

      peer_id = tp_handle_inspect (handles_repo, priv->handle);
      if (priv->initiator == priv->self_handle)
        {
          /* We create this tube, bytestream is open */

          bytestream = g_object_new (GIBBER_TYPE_BYTESTREAM_IBB,
              "muc-connection", priv->muc_connection,
              "stream-id", priv->stream_id,
              "state", GIBBER_BYTESTREAM_IBB_STATE_OPEN,
              "self-id", priv->conn->name,
              "peer-id", peer_id,
              "stream-init-id", NULL,
              NULL);
        }
      else
        {
          /* We don't create this tube, bytestream is local pending */
          bytestream = g_object_new (GIBBER_TYPE_BYTESTREAM_IBB,
                "muc-connection", priv->muc_connection,
                "stream-id", priv->stream_id,
                "state", GIBBER_BYTESTREAM_IBB_STATE_LOCAL_PENDING,
                "self-id", priv->conn->name,
                "peer-id", peer_id,
                "stream-init-id", NULL,
                NULL);
        }

      g_object_set (self, "bytestream", bytestream, NULL);
    }
  else
    {
      /* Private tube */
      g_assert (priv->muc_connection == NULL);
    }

  return obj;
}

static void
salut_tube_dbus_class_init (SalutTubeDBusClass *salut_tube_dbus_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_tube_dbus_class);
  GParamSpec *param_spec;

  object_class->get_property = salut_tube_dbus_get_property;
  object_class->set_property = salut_tube_dbus_set_property;
  object_class->constructor = salut_tube_dbus_constructor;

  g_type_class_add_private (salut_tube_dbus_class,
      sizeof (SalutTubeDBusPrivate));

  object_class->dispose = salut_tube_dbus_dispose;
  object_class->finalize = salut_tube_dbus_finalize;

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "Salut connection object that owns this D-Bus tube object.",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

 param_spec = g_param_spec_uint (
      "handle",
      "Handle",
      "The TpHandle associated with the tubes channel that"
      "owns this D-Bus tube object.",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HANDLE, param_spec);

  param_spec = g_param_spec_uint (
      "handle-type",
      "Handle type",
      "The TpHandleType of the handle associated with the tubes channel that"
      "owns this D-Bus tube object.",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HANDLE_TYPE,
      param_spec);

  param_spec = g_param_spec_uint (
      "self-handle",
      "Self handle",
      "The handle to use for ourself. This can be different from the "
      "connection's self handle if our handle is a room handle.",
      0, G_MAXUINT, 0,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SELF_HANDLE, param_spec);

  param_spec = g_param_spec_object (
      "muc-connection",
      "GibberMucConnection object",
      "Gibber MUC connection object used to communicate trought this "
      "tube if it's a muc one",
      GIBBER_TYPE_MUC_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_MUC_CONNECTION,
      param_spec);

  param_spec = g_param_spec_uint (
      "id",
      "id",
      "The unique identifier of this tube",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_ID, param_spec);

  param_spec = g_param_spec_object (
      "bytestream",
      "SalutBytestreamIBB object",
      "Salut bytestream IBB object used for streaming data for this D-Bus"
      "tube object.",
      GIBBER_TYPE_BYTESTREAM_IBB,
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_BYTESTREAM, param_spec);

  param_spec = g_param_spec_string (
      "stream-id",
      "stream id",
      "The identifier of this tube's bytestream",
      "",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STREAM_ID, param_spec);

  param_spec = g_param_spec_uint (
      "type",
      "Tube type",
      "The TpTubeType this D-Bus tube object.",
      0, G_MAXUINT32, TP_TUBE_TYPE_DBUS,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_TYPE, param_spec);

  param_spec = g_param_spec_uint (
      "initiator",
      "Initiator handle",
      "The TpHandle of the initiator of this D-Bus tube object.",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_INITIATOR, param_spec);

  param_spec = g_param_spec_string (
      "service",
      "service name",
      "the service associated with this D-BUS tube object.",
      "",
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SERVICE, param_spec);

  param_spec = g_param_spec_boxed (
      "parameters",
      "parameters GHashTable",
      "GHashTable containing parameters of this DBUS tube object.",
      G_TYPE_HASH_TABLE,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PARAMETERS, param_spec);

  param_spec = g_param_spec_uint (
      "state",
      "Tube state",
      "The TpTubeState of this DBUS tube object",
      0, G_MAXUINT32, TP_TUBE_STATE_REMOTE_PENDING,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  param_spec = g_param_spec_string (
      "dbus-address",
      "D-Bus address",
      "The D-Bus address on which this tube will listen for connections",
      "",
      G_PARAM_READABLE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_DBUS_ADDRESS,
      param_spec);

  param_spec = g_param_spec_string (
      "dbus-name",
      "D-Bus name",
      "The local D-Bus name on the virtual bus.",
      "",
      G_PARAM_READABLE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_DBUS_NAME, param_spec);

  param_spec = g_param_spec_boxed (
      "dbus-names",
      "D-Bus names",
      "Mapping of contact handles to D-Bus names.",
      G_TYPE_HASH_TABLE,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_DBUS_NAMES, param_spec);

  signals[OPENED] =
    g_signal_new ("opened",
                  G_OBJECT_CLASS_TYPE (salut_tube_dbus_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  tube_dbus_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (salut_tube_dbus_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  tube_dbus_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
data_received_cb (GibberBytestreamIBB *ibb,
                  const gchar *from,
                  GString *data,
                  gpointer user_data)
{
  SalutTubeDBus *tube = SALUT_TUBE_DBUS (user_data);
  SalutTubeDBusPrivate *priv = SALUT_TUBE_DBUS_GET_PRIVATE (tube);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle sender;
  DBusMessage *msg;
  DBusError error = {0,};
  guint32 serial;
  const gchar *sender_name;
  const gchar *destination;

  if (!priv->dbus_conn)
    {
      DEBUG ("no D-Bus connection");
      return;
    }

  /* XXX: This naïvely assumes that the underlying transport always gives
   * us complete messages. This is true for IBB, at least.
   */

  msg = dbus_message_demarshal (data->str, data->len, &error);
  if (!msg)
    {
      /* message was corrupted */
      DEBUG ("received corrupted message from %s", from);
      return;
    }

  destination = dbus_message_get_destination (msg);
  if (destination != NULL &&
      tp_strdiff (priv->dbus_local_name, dbus_message_get_destination (msg)))
    {
      /* This message is not intented to this tube.
       * Discard it. */
      DEBUG ("message not intented to this tube (destination = %s)",
          dbus_message_get_destination (msg));
      dbus_message_unref (msg);
      return;
    }

  sender = tp_handle_lookup (contact_repo, from, NULL, NULL);
  if (sender == 0)
    {
      DEBUG ("unkown sender: %s", from);
      dbus_message_unref (msg);
      return;
    }

  sender_name = g_hash_table_lookup (priv->dbus_names,
      GUINT_TO_POINTER (sender));

  if (tp_strdiff (sender_name, dbus_message_get_sender (msg)))
    {
      DEBUG ("invalid sender %s (expected %s for sender handle %d)",
             dbus_message_get_sender (msg), sender_name, sender);
      dbus_message_unref (msg);
      return;
    }

  /* XXX: what do do if this returns FALSE? */
  dbus_connection_send (priv->dbus_conn, msg, &serial);

  dbus_message_unref (msg);
}

SalutTubeDBus *
salut_tube_dbus_new (SalutConnection *conn,
                     TpHandle handle,
                     TpHandleType handle_type,
                     TpHandle self_handle,
                     GibberMucConnection *muc_connection,
                     TpHandle initiator,
                     const gchar *service,
                     GHashTable *parameters,
                     const gchar *stream_id,
                     guint id)
{
  return g_object_new (SALUT_TYPE_TUBE_DBUS,
      "connection", conn,
      "handle", handle,
      "handle-type", handle_type,
      "self-handle", self_handle,
      "muc-connection", muc_connection,
      "initiator", initiator,
      "service", service,
      "parameters", parameters,
      "stream-id", stream_id,
      "id", id,
      NULL);
}

/*
 * salut_tube_dbus_accept
 *
 * Implements salut_tube_iface_accept on SalutTubeIface
 */
static void
salut_tube_dbus_accept (SalutTubeIface *tube)
{
  SalutTubeDBus *self = SALUT_TUBE_DBUS (tube);
  SalutTubeDBusPrivate *priv = SALUT_TUBE_DBUS_GET_PRIVATE (self);
  GibberBytestreamIBBState state;
  gchar *stream_init_id;

  g_assert (priv->bytestream != NULL);

  g_object_get (priv->bytestream,
      "state", &state,
      NULL);

  if (state != GIBBER_BYTESTREAM_IBB_STATE_LOCAL_PENDING)
    return;

  g_object_get (priv->bytestream,
      "stream-init-id", &stream_init_id,
      NULL);

  if (stream_init_id != NULL)
    {
      /* Bytestream was created using a SI request so
       * we have to accept it */
#if 0
      LmMessage *msg;
      LmMessageNode *si, *tube_node;

      DEBUG ("accept the SI request");

      msg = salut_bytestream_ibb_make_accept_iq (priv->bytestream);
      if (msg == NULL)
        {
          DEBUG ("can't create SI accept IQ. Close the bytestream");
          gibber_bytestream_ibb_close (priv->bytestream);
          return;
        }

      si = lm_message_node_get_child_with_namespace (msg->node, "si",
          NS_SI);
      g_assert (si != NULL);

      tube_node = lm_message_node_add_child (si, "tube", "");
      lm_message_node_set_attribute (tube_node, "xmlns", NS_SI_TUBES_OLD);

      lm_message_node_add_child (tube_node, "dbus-name",
          priv->dbus_local_name);

      salut_bytestream_ibb_accept (priv->bytestream, msg);

      lm_message_unref (msg);
      g_free (stream_init_id);
#endif
    }
  else
    {
      /* No SI so the bytestream is open */
      DEBUG ("no SI, bytestream open");
      g_object_set (priv->bytestream,
          "state", GIBBER_BYTESTREAM_IBB_STATE_OPEN,
          NULL);
    }
}

/*
 * salut_tube_dbus_close
 *
 * Implements salut_tube_iface_close on SalutTubeIface
 */
static void
salut_tube_dbus_close (SalutTubeIface *tube)
{
  SalutTubeDBus *self = SALUT_TUBE_DBUS (tube);
  SalutTubeDBusPrivate *priv = SALUT_TUBE_DBUS_GET_PRIVATE (self);

  if (priv->bytestream != NULL)
    {
      gibber_bytestream_ibb_close (priv->bytestream);
    }
  else
    {
      g_signal_emit (G_OBJECT (self), signals[CLOSED], 0);
    }
}

/**
 * salut_tube_dbus_add_bytestream
 *
 * Implements salut_tube_iface_add_bytestream on SalutTubeIface
 */

static void
salut_tube_dbus_add_bytestream (SalutTubeIface *tube,
                                GibberBytestreamIBB *bytestream)
{
  DEBUG ("D-Bus doesn't support extra bytestream");
  gibber_bytestream_ibb_close (bytestream);
}

static void
tube_iface_init (gpointer g_iface,
                 gpointer iface_data)
{
  SalutTubeIfaceClass *klass = (SalutTubeIfaceClass *) g_iface;

  klass->accept = salut_tube_dbus_accept;
  klass->close = salut_tube_dbus_close;
  klass->add_bytestream = salut_tube_dbus_add_bytestream;
}
