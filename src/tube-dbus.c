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
#include <errno.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <telepathy-glib/util.h>

#include <gibber/gibber-bytestream-ibb.h>
#include <gibber/gibber-bytestream-muc.h>
#include <gibber/gibber-muc-connection.h>

#define DEBUG_FLAG DEBUG_TUBES
#include "debug.h"
#include "salut-connection.h"
#include "namespaces.h"
#include "tube-iface.h"


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
  GibberBytestreamIface *bytestream;
  gchar *stream_id;
  TpHandle initiator;
  gchar *service;
  GHashTable *parameters;

  /* our unique D-Bus name on the virtual tube bus */
  gchar *dbus_local_name;
  /* the address that we are listening for D-Bus connections on */
  gchar *dbus_srv_addr;
  /* the path of the UNIX socket used by the D-Bus server */
  gchar *socket_path;
  /* the server that's listening on dbus_srv_addr */
  DBusServer *dbus_srv;
  /* the connection to dbus_srv from a local client, or NULL */
  DBusConnection *dbus_conn;
  /* mapping of contact handle -> D-Bus name (NULL for 1-1 D-Bus tubes) */
  GHashTable *dbus_names;

  /* Message reassembly buffer (CONTACT tubes only) */
  GString *reassembly_buffer;
  /* Number of bytes that will be in the next message, 0 if unknown */
  guint32 reassembly_bytes_needed;

  gboolean dispose_has_run;
};

#define SALUT_TUBE_DBUS_GET_PRIVATE(obj) \
    ((SalutTubeDBusPrivate *) obj->priv)

static void data_received_cb (GibberBytestreamIface *bytestream,
    const gchar *from, GString *data, gpointer user_data);

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

  if (priv->dbus_local_name != NULL)
    {
      dbus_message_set_sender (msg, priv->dbus_local_name);
    }

  if (!dbus_message_marshal (msg, &marshalled, &len))
    goto out;

  gibber_bytestream_iface_send (priv->bytestream, len, marshalled);

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
#define SERVER_LISTEN_MAX_TRIES 5
  SalutTubeDBusPrivate *priv = SALUT_TUBE_DBUS_GET_PRIVATE (self);
  guint i;

  g_signal_connect (priv->bytestream, "data-received",
      G_CALLBACK (data_received_cb), self);

  for (i = 0; i < SERVER_LISTEN_MAX_TRIES; i++)
    {
      gchar suffix[8];
      DBusError error;

      g_free (priv->dbus_srv_addr);
      g_free (priv->socket_path);

      generate_ascii_string (8, suffix);
      priv->socket_path = g_strdup_printf ("%s/dbus-salut-%.8s",
          g_get_tmp_dir (), suffix);
      priv->dbus_srv_addr = g_strdup_printf ("unix:path=%s",
          priv->socket_path);

      dbus_error_init (&error);
      priv->dbus_srv = dbus_server_listen (priv->dbus_srv_addr, &error);

      if (priv->dbus_srv_addr != NULL)
        break;

      DEBUG ("dbus_server_listen failed (try %u): %s: %s", i, error.name,
          error.message);
      dbus_error_free (&error);
    }

  if (priv->dbus_srv_addr == NULL)
    {
      DEBUG ("all attempts failed. Close the tube");
      salut_tube_iface_accept (SALUT_TUBE_IFACE (self), NULL);
      return;
    }

  DEBUG ("listening on %s", priv->dbus_srv_addr);

  dbus_server_set_new_connection_function (priv->dbus_srv, new_connection_cb,
      self, NULL);
  dbus_server_setup_with_g_main (priv->dbus_srv, NULL);
}

static void
salut_tube_dbus_init (SalutTubeDBus *self)
{
  SalutTubeDBusPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_TUBE_DBUS, SalutTubeDBusPrivate);

  self->priv = priv;
}

static void
unref_handle_foreach (gpointer key,
                      gpointer value,
                      gpointer user_data)
{
  TpHandle handle = GPOINTER_TO_UINT (key);
  TpHandleRepoIface *contact_repo = (TpHandleRepoIface *) user_data;

  tp_handle_unref (contact_repo, handle);
}

static TpTubeState
get_tube_state (SalutTubeDBus *self)
{
  SalutTubeDBusPrivate *priv = SALUT_TUBE_DBUS_GET_PRIVATE (self);
  GibberBytestreamState bytestream_state;

  if (priv->bytestream == NULL)
    /* bytestream not yet created as we're waiting for the SI reply */
    return TP_TUBE_STATE_REMOTE_PENDING;

  g_object_get (priv->bytestream, "state", &bytestream_state, NULL);

  switch (bytestream_state)
    {
      case GIBBER_BYTESTREAM_STATE_OPEN:
        return TP_TUBE_STATE_OPEN;
        break;
      case GIBBER_BYTESTREAM_STATE_LOCAL_PENDING:
      case GIBBER_BYTESTREAM_STATE_ACCEPTED:
        return TP_TUBE_STATE_LOCAL_PENDING;
        break;
      case GIBBER_BYTESTREAM_STATE_INITIATING:
        return TP_TUBE_STATE_REMOTE_PENDING;
        break;
      default:
        g_assert_not_reached ();
    }
}

static void
bytestream_state_changed_cb (GibberBytestreamIface *bytestream,
                             GibberBytestreamState state,
                             gpointer user_data)
{
  SalutTubeDBus *self = SALUT_TUBE_DBUS (user_data);
  SalutTubeDBusPrivate *priv = SALUT_TUBE_DBUS_GET_PRIVATE (self);

  if (state == GIBBER_BYTESTREAM_STATE_CLOSED)
    {
      if (priv->bytestream != NULL)
        {
          g_object_unref (priv->bytestream);
          priv->bytestream = NULL;
        }

      g_signal_emit (G_OBJECT (self), signals[CLOSED], 0);
    }
  else if (state == GIBBER_BYTESTREAM_STATE_OPEN)
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
      gibber_bytestream_iface_close (priv->bytestream);
    }

  if (priv->dbus_conn)
    {
      dbus_connection_close (priv->dbus_conn);
      dbus_connection_unref (priv->dbus_conn);
    }

  if (priv->dbus_srv)
    dbus_server_unref (priv->dbus_srv);

  if (priv->socket_path != NULL)
    {
      if (g_unlink (priv->socket_path) != 0)
        {
          DEBUG ("unlink of %s failed: %s", priv->socket_path,
              g_strerror (errno));
        }
    }

  g_free (priv->dbus_srv_addr);
  priv->dbus_srv_addr = NULL;
  g_free (priv->socket_path);
  priv->socket_path = NULL;
  g_free (priv->dbus_local_name);
  priv->dbus_local_name = NULL;

  if (priv->dbus_names)
    {
      TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
          (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
      g_hash_table_foreach (priv->dbus_names, unref_handle_foreach,
          contact_repo);
      g_hash_table_destroy (priv->dbus_names);
    }

  if (priv->muc_connection != NULL)
    {
      g_object_unref (priv->muc_connection);
      priv->muc_connection = NULL;
    }

  if (priv->reassembly_buffer)
    g_string_free (priv->reassembly_buffer, TRUE);

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
            GibberBytestreamState state;

            priv->bytestream = g_value_get_object (value);
            g_object_ref (priv->bytestream);

            g_object_get (priv->bytestream, "state", &state, NULL);
            if (state == GIBBER_BYTESTREAM_STATE_OPEN)
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
       * We have to create an MUC bytestream that will be
       * used by this MUC tube to communicate.
       *
       * We don't create the bytestream of private D-Bus tube yet.
       * It will be when we'll receive the answer of the SI request
       */
      GibberBytestreamIBB *bytestream;
      GibberBytestreamState state;
      const gchar *peer_id;
      gchar suffix[8];

      g_assert (priv->muc_connection != NULL);
      g_assert (priv->stream_id != NULL);

      priv->dbus_names = g_hash_table_new_full (g_direct_hash, g_direct_equal,
          NULL, g_free);

      generate_ascii_string (8, suffix);
      priv->dbus_local_name = g_strdup_printf (":1.%.8s", suffix);

      DEBUG ("local name: %s", priv->dbus_local_name);

      peer_id = tp_handle_inspect (handles_repo, priv->handle);
      if (priv->initiator == priv->self_handle)
        {
          /* We create this tube, bytestream is open */
          state = GIBBER_BYTESTREAM_STATE_OPEN;
        }
      else
        {
          /* We don't create this tube, bytestream is local pending */
          state = GIBBER_BYTESTREAM_STATE_LOCAL_PENDING;
        }

      bytestream = g_object_new (GIBBER_TYPE_BYTESTREAM_MUC,
            "muc-connection", priv->muc_connection,
            "stream-id", priv->stream_id,
            "state", state,
            "self-id", priv->conn->name,
            "peer-id", peer_id,
            NULL);

      g_object_set (self, "bytestream", bytestream, NULL);
    }
  else
    {
      /* Private tube */
      g_assert (priv->muc_connection == NULL);

      /* The D-Bus names mapping is used in muc tubes only */
      priv->dbus_local_name = NULL;
      priv->dbus_names = NULL;

      /* For contact tubes we need to be able to reassemble messages. */
      priv->reassembly_buffer = g_string_new ("");
      priv->reassembly_bytes_needed = 0;
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

  g_object_class_override_property (object_class, PROP_CONNECTION,
    "connection");
  g_object_class_override_property (object_class, PROP_HANDLE,
    "handle");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
    "handle-type");
  g_object_class_override_property (object_class, PROP_SELF_HANDLE,
    "self-handle");
  g_object_class_override_property (object_class, PROP_ID,
    "id");
  g_object_class_override_property (object_class, PROP_TYPE,
    "type");
  g_object_class_override_property (object_class, PROP_INITIATOR,
    "initiator");
  g_object_class_override_property (object_class, PROP_SERVICE,
    "service");
  g_object_class_override_property (object_class, PROP_PARAMETERS,
    "parameters");
  g_object_class_override_property (object_class, PROP_STATE,
    "state");

  param_spec = g_param_spec_object (
      "muc-connection",
      "GibberMucConnection object",
      "Gibber MUC connection object used to carry messages for this "
      "tube if it has a HANDLE_TYPE_ROOM handle",
      GIBBER_TYPE_MUC_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_MUC_CONNECTION,
      param_spec);

  param_spec = g_param_spec_object (
      "bytestream",
      "Object implementing the GibberBytestreamIface interface",
      "Bytestream object used for streaming data for this D-Bus",
      G_TYPE_OBJECT,
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
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (salut_tube_dbus_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
message_received (SalutTubeDBus *tube,
                  TpHandle sender,
                  const char *data,
                  size_t len)
{
  SalutTubeDBusPrivate *priv = SALUT_TUBE_DBUS_GET_PRIVATE (tube);
  DBusMessage *msg;
  DBusError error = {0,};
  guint32 serial;

  if (!priv->dbus_conn)
    {
      DEBUG ("no D-Bus connection");
      return;
    }

  msg = dbus_message_demarshal (data, len, &error);

  if (msg == NULL)
    {
      /* message was corrupted */
      DEBUG ("received corrupted message from %d: %s: %s", sender,
          error.name, error.message);
      dbus_error_free (&error);
      return;
    }

  if (priv->handle_type == TP_HANDLE_TYPE_ROOM)
    {
      const gchar *destination;
      const gchar *sender_name;

      destination = dbus_message_get_destination (msg);
      /* If destination is NULL this msg is broadcasted (signals) so we don't
       * have to check it */
      if (destination != NULL && tp_strdiff (priv->dbus_local_name,
            destination))
        {
          /* This message is not intended to this tube.
           * Discard it. */
          DEBUG ("message not intended to this tube (destination = %s)",
              destination);
          goto unref;
        }

      sender_name = g_hash_table_lookup (priv->dbus_names,
          GUINT_TO_POINTER (sender));

      if (tp_strdiff (sender_name, dbus_message_get_sender (msg)))
        {
          DEBUG ("invalid sender %s (expected %s for sender handle %d)",
                 dbus_message_get_sender (msg), sender_name, sender);
          goto unref;
        }
    }

  /* XXX: what do do if this returns FALSE? */
  dbus_connection_send (priv->dbus_conn, msg, &serial);

unref:
  dbus_message_unref (msg);
}

static guint32
collect_le32 (char *str)
{
  unsigned char *bytes = (unsigned char *) str;

  return bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
}

static guint32
collect_be32 (char *str)
{
  unsigned char *bytes = (unsigned char *) str;

  return (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 24) | bytes[3];
}

static void
data_received_cb (GibberBytestreamIface *stream,
                  const gchar *from,
                  GString *data,
                  gpointer user_data)
{
  SalutTubeDBus *tube = SALUT_TUBE_DBUS (user_data);
  SalutTubeDBusPrivate *priv = SALUT_TUBE_DBUS_GET_PRIVATE (tube);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle sender;

  sender = tp_handle_lookup (contact_repo, from, NULL, NULL);
  if (sender == 0)
    {
      DEBUG ("unkown sender: %s", from);
      return;
    }

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      GString *buf = priv->reassembly_buffer;

      g_assert (buf != NULL);

      g_string_append_len (buf, data->str, data->len);
      DEBUG ("Received %" G_GSIZE_FORMAT " bytes, so we now have %"
          G_GSIZE_FORMAT " bytes in reassembly buffer", data->len, buf->len);

      /* Each D-Bus message has a 16-byte fixed header, in which
       *
       * * byte 0 is 'l' (ell) or 'B' for endianness
       * * bytes 4-7 are body length "n" in bytes in that endianness
       * * bytes 12-15 are length "m" of param array in bytes in that
       *   endianness
       *
       * followed by m + n + ((8 - (m % 8)) % 8) bytes of other content.
       */

      while (buf->len >= 16)
        {
          guint32 body_length, params_length, m;

          /* see if we have a whole message and have already calculated
           * how many bytes it needs */

          if (priv->reassembly_bytes_needed != 0)
            {
              if (buf->len >= priv->reassembly_bytes_needed)
                {
                  DEBUG ("Received complete D-Bus message of size %"
                      G_GINT32_FORMAT, priv->reassembly_bytes_needed);
                  message_received (tube, sender, buf->str,
                      priv->reassembly_bytes_needed);
                  g_string_erase (buf, 0, priv->reassembly_bytes_needed);
                  priv->reassembly_bytes_needed = 0;
                }
              else
                {
                  /* we'll have to wait for more data */
                  break;
                }
            }

          if (buf->len < 16)
            break;

          /* work out how big the next message is going to be */

          if (buf->str[0] == DBUS_BIG_ENDIAN)
            {
              body_length = collect_be32 (buf->str + 4);
              m = collect_be32 (buf->str + 12);
            }
          else if (buf->str[0] == DBUS_LITTLE_ENDIAN)
            {
              body_length = collect_le32 (buf->str + 4);
              m = collect_le32 (buf->str + 12);
            }
          else
            {
              DEBUG ("D-Bus message has unknown endianness byte 0x%x, "
                  "closing tube", (unsigned int) buf->str[0]);
              salut_tube_iface_close (SALUT_TUBE_IFACE (tube));
              return;
            }

          /* pad to 8-byte boundary */
          params_length = m + ((8 - (m % 8)) % 8);
          g_assert (params_length % 8 == 0);
          g_assert (params_length >= m);
          g_assert (params_length < m + 8);

          priv->reassembly_bytes_needed = params_length + body_length + 16;

          /* n.b.: this looks as if it could be simplified to just the third
           * test, but that would be wrong if the addition had overflowed, so
           * don't do that. The first and second tests are sufficient to
           * ensure no overflow on 32-bit platforms */
          if (body_length > DBUS_MAXIMUM_MESSAGE_LENGTH ||
              params_length > DBUS_MAXIMUM_ARRAY_LENGTH ||
              priv->reassembly_bytes_needed > DBUS_MAXIMUM_MESSAGE_LENGTH)
            {
              DEBUG ("D-Bus message is too large to be valid, closing tube");
              salut_tube_iface_close (SALUT_TUBE_IFACE (tube));
              return;
            }

          g_assert (priv->reassembly_bytes_needed != 0);
          DEBUG ("We need %" G_GINT32_FORMAT " bytes for the next full "
              "message", priv->reassembly_bytes_needed);
        }
    }
  else
    {
      /* MUC bytestreams are message-boundary preserving, which is necessary,
       * because we can't assume we started at the beginning */
      g_assert (GIBBER_IS_BYTESTREAM_MUC (priv->bytestream));
      message_received (tube, sender, data->str, data->len);
    }
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
static gboolean
salut_tube_dbus_accept (SalutTubeIface *tube,
                        GError **error)
{
  SalutTubeDBus *self = SALUT_TUBE_DBUS (tube);
  SalutTubeDBusPrivate *priv = SALUT_TUBE_DBUS_GET_PRIVATE (self);
  GibberBytestreamState state;

  g_assert (priv->bytestream != NULL);

  g_object_get (priv->bytestream,
      "state", &state,
      NULL);

  if (state != GIBBER_BYTESTREAM_STATE_LOCAL_PENDING)
    return TRUE;

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      /* TODO: SI reply */
    }
  else
    {
      /* No SI so the bytestream is open */
      DEBUG ("no SI, bytestream open");
      g_object_set (priv->bytestream,
          "state", GIBBER_BYTESTREAM_STATE_OPEN,
          NULL);
    }

  return TRUE;
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
      gibber_bytestream_iface_close (priv->bytestream);
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
                                GibberBytestreamIface *bytestream)
{
  DEBUG ("D-Bus doesn't support extra bytestream");
  gibber_bytestream_iface_close (bytestream);
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
