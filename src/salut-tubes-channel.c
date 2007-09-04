/*
 * salut-tubes-channel.c - Source for SalutTubesChannel
 * Copyright (C) 2007 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the tubesplied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "salut-tubes-channel.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>

#include <dbus/dbus-glib.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/dbus.h>

#include <gibber/gibber-muc-connection.h>
#include <gibber/gibber-xmpp-stanza.h>
#include <gibber/gibber-namespaces.h>

#define DEBUG_FLAG DEBUG_TUBES
#include "debug.h"
#include "extensions/extensions.h"
#include "salut-util.h"
#include "salut-connection.h"
#include "salut-contact.h"
#include "salut-muc-channel.h"
#include "tube-iface.h"
#include "tube-dbus.h"

#define SALUT_CHANNEL_TUBE_TYPE \
    (dbus_g_type_get_struct ("GValueArray", \
        G_TYPE_UINT, \
        G_TYPE_UINT, \
        G_TYPE_UINT, \
        G_TYPE_STRING, \
        dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), \
        G_TYPE_UINT, \
        G_TYPE_INVALID))

#define DBUS_NAME_PAIR_TYPE \
    (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID))

static void
channel_iface_init (gpointer g_iface, gpointer iface_data);
static void
tubes_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (SalutTubesChannel, salut_tubes_channel, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (SALUT_TYPE_SVC_CHANNEL_TYPE_TUBES, tubes_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
        tp_external_group_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL);
);

/* signal enum */
enum
{
    READY,
    JOIN_ERROR,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_CONNECTION,
  PROP_MUC,
  LAST_PROPERTY
};

/* private structure */
typedef struct _SalutTubesChannelPrivate SalutTubesChannelPrivate;

struct _SalutTubesChannelPrivate
{
  SalutConnection *conn;
  gchar *object_path;
  TpHandle handle;
  TpHandleType handle_type;
  TpHandleType self_handle;
  GibberMucConnection *muc_connection;

  GHashTable *tubes;

  gboolean closed;
  gboolean dispose_has_run;
};

#define SALUT_TUBES_CHANNEL_GET_PRIVATE(obj) \
  ((SalutTubesChannelPrivate *) obj->priv)

static gboolean update_tubes_info (SalutTubesChannel *self, gboolean request);
static void muc_connection_received_stanza_cb (GibberMucConnection *conn,
    const gchar *sender, GibberXmppStanza *stanza, gpointer user_data);
static void muc_connection_lost_sender_cb (GibberMucConnection *conn,
    const gchar *sender, gpointer user_data);
static gboolean extract_tube_information (SalutTubesChannel *self,
    GibberXmppNode *tube_node, SalutTubeType *type, TpHandle *initiator_handle,
    const gchar **service, GHashTable **parameters, gboolean *offering,
    guint *tube_id);
static void
create_new_tube (SalutTubesChannel *self, SalutTubeType type, TpHandle initiator,
    const gchar *service, GHashTable *parameters, const gchar *stream_id,
    guint tube_id);

static void
salut_tubes_channel_init (SalutTubesChannel *self)
{
  SalutTubesChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_TUBES_CHANNEL, SalutTubesChannelPrivate);

  self->priv = priv;

  priv->tubes = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) g_object_unref);

  priv->dispose_has_run = FALSE;
  priv->closed = FALSE;
}

static void
muc_channel_ready_cb (SalutMucChannel *chan,
                      SalutTubesChannel *self)
{
  self->ready = TRUE;

  /* request tubes infos */
  update_tubes_info (self, TRUE);

  g_signal_emit (self, signals[READY], 0);
}

static void
muc_channel_join_error_cb (SalutMucChannel *chan,
                           GError *error,
                           SalutTubesChannel *self)
{
  g_signal_emit (self, signals[JOIN_ERROR], 0, error);
}

static GObject *
salut_tubes_channel_constructor (GType type,
                                 guint n_props,
                                 GObjectConstructParam *props)
{
  GObject *obj;
  SalutTubesChannel *self;
  SalutTubesChannelPrivate *priv;
  DBusGConnection *bus;
  TpHandleRepoIface *handle_repo;

  obj = G_OBJECT_CLASS (salut_tubes_channel_parent_class)->
        constructor (type, n_props, props);

  self = SALUT_TUBES_CHANNEL (obj);
  priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  g_assert (priv->conn != NULL);
  handle_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, priv->handle_type);

  tp_handle_ref (handle_repo, priv->handle);

  switch (priv->handle_type)
    {
    case TP_HANDLE_TYPE_CONTACT:
      g_assert (self->muc == NULL);
      priv->self_handle = ((TpBaseConnection *)
          (priv->conn))->self_handle;
      self->ready = TRUE;
      break;

    case TP_HANDLE_TYPE_ROOM:
      g_assert (self->muc != NULL);
      priv->self_handle = self->muc->group.self_handle;
      tp_external_group_mixin_init (obj, (GObject *) self->muc);
      g_object_get (self->muc,
          "muc-connection", &(priv->muc_connection),
          NULL);
      g_assert (priv->muc_connection != NULL);

      g_signal_connect (priv->muc_connection, "received-stanza",
          G_CALLBACK (muc_connection_received_stanza_cb), self);
      g_signal_connect (priv->muc_connection, "lost-sender",
          G_CALLBACK (muc_connection_lost_sender_cb), self);

      if (priv->muc_connection->state == GIBBER_MUC_CONNECTION_CONNECTED)
        {
          muc_channel_ready_cb (self->muc, self);
        }
      else
        {
          g_signal_connect (self->muc, "ready",
              G_CALLBACK (muc_channel_ready_cb), self);
          g_signal_connect (self->muc, "join-error",
              G_CALLBACK (muc_channel_join_error_cb), self);
        }

      break;
    default:
      g_assert_not_reached ();
    }

  /* Connect to the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  DEBUG ("Registering at '%s'", priv->object_path);

  return obj;
}

static void
salut_tubes_channel_get_property (GObject *object,
                                  guint property_id,
                                  GValue *value,
                                  GParamSpec *pspec)
{
  SalutTubesChannel *chan = SALUT_TUBES_CHANNEL (object);
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (chan);

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_value_set_string (value, priv->object_path);
        break;
      case PROP_CHANNEL_TYPE:
        g_value_set_static_string (value, SALUT_IFACE_CHANNEL_TYPE_TUBES);
        break;
      case PROP_HANDLE_TYPE:
        g_value_set_uint (value, priv->handle_type);
        break;
      case PROP_HANDLE:
        g_value_set_uint (value, priv->handle);
        break;
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;
      case PROP_MUC:
        g_value_set_object (value, chan->muc);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_tubes_channel_set_property (GObject *object,
                                  guint property_id,
                                  const GValue *value,
                                  GParamSpec *pspec)
{
  SalutTubesChannel *chan = SALUT_TUBES_CHANNEL (object);
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (chan);

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_free (priv->object_path);
        priv->object_path = g_value_dup_string (value);
        break;
      case PROP_HANDLE_TYPE:
        priv->handle_type = g_value_get_uint (value);
        break;
      case PROP_HANDLE:
        priv->handle = g_value_get_uint (value);
        break;
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;
      case PROP_MUC:
        chan->muc = g_value_get_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
d_bus_names_changed_added (SalutTubesChannel *self,
                           guint tube_id,
                           TpHandle contact,
                           const gchar *new_name)
{
  GPtrArray *added = g_ptr_array_sized_new (1);
  GArray *removed = g_array_new (FALSE, FALSE, sizeof (guint));
  GValue tmp = {0,};
  guint i;

  g_value_init (&tmp, DBUS_NAME_PAIR_TYPE);
  g_value_take_boxed (&tmp,
      dbus_g_type_specialized_construct (DBUS_NAME_PAIR_TYPE));
  dbus_g_type_struct_set (&tmp,
      0, contact,
      1, new_name,
      G_MAXUINT);
  g_ptr_array_add (added, g_value_get_boxed (&tmp));

  salut_svc_channel_type_tubes_emit_d_bus_names_changed (self,
      tube_id, added, removed);

  for (i = 0; i < added->len; i++)
    g_boxed_free (DBUS_NAME_PAIR_TYPE, added->pdata[i]);
  g_ptr_array_free (added, TRUE);
  g_array_free (removed, TRUE);
}

static void
d_bus_names_changed_removed (SalutTubesChannel *self,
                             guint tube_id,
                             TpHandle contact)
{
  GPtrArray *added = g_ptr_array_new ();
  GArray *removed = g_array_new (FALSE, FALSE, sizeof (guint));

  g_array_append_val (removed, contact);

  salut_svc_channel_type_tubes_emit_d_bus_names_changed (self,
      tube_id, added, removed);

  g_ptr_array_free (added, TRUE);
  g_array_free (removed, TRUE);
}

static void
add_name_in_dbus_names (SalutTubesChannel *self,
                        guint tube_id,
                        TpHandle handle,
                        const gchar *dbus_name)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  SalutTubeDBus *tube;
  GHashTable *names;

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));
  if (tube == NULL)
    return;

  g_object_get (tube,
      "dbus-names", &names,
      NULL);

  g_hash_table_insert (names, GUINT_TO_POINTER (handle), g_strdup (dbus_name));
  tp_handle_ref (contact_repo, handle);

  /* Emit the DBusNamesChanged signal */
  d_bus_names_changed_added (self, tube_id, handle, dbus_name);

  g_hash_table_unref (names);
}

static void
add_yourself_in_dbus_names (SalutTubesChannel *self,
                            guint tube_id)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  SalutTubeDBus *tube;
  gchar *dbus_name;

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));
  if (tube == NULL)
    return;

  g_object_get (tube,
      "dbus-name", &dbus_name,
      NULL);

  add_name_in_dbus_names (self, tube_id, priv->self_handle, dbus_name);

  g_free (dbus_name);
}

/**
 * salut_tubes_channel_get_available_tube_types
 *
 * Implements D-Bus method GetAvailableTubeTypes
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_get_available_tube_types (SalutSvcChannelTypeTubes *iface,
                                              DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  GArray *ret;
  SalutTubeType type;

  g_assert (SALUT_IS_TUBES_CHANNEL (self));

  ret = g_array_sized_new (FALSE, FALSE, sizeof (SalutTubeType), 1);
  type = SALUT_TUBE_TYPE_DBUS;
  g_array_append_val (ret, type);
  /*
  type = TP_TUBE_TYPE_STREAM_UNIX;
  g_array_append_val (ret, type);
  */

  salut_svc_channel_type_tubes_return_from_get_available_tube_types (context,
      ret);

  g_array_free (ret, TRUE);
}

struct _add_in_old_dbus_tubes_data
{
  GHashTable *old_dbus_tubes;
  TpHandle contact;
};

static void
add_in_old_dbus_tubes (gpointer key,
                       gpointer value,
                       gpointer user_data)
{
  guint tube_id = GPOINTER_TO_UINT (key);
  SalutTubeIface *tube = SALUT_TUBE_IFACE (value);
  struct _add_in_old_dbus_tubes_data *data =
    (struct _add_in_old_dbus_tubes_data *) user_data;
  SalutTubeType type;
  GHashTable *names;

  g_object_get (tube, "type", &type, NULL);

  if (type != SALUT_TUBE_TYPE_DBUS)
    return;

  g_object_get (tube, "dbus-names", &names, NULL);
  g_assert (names);

  if (g_hash_table_lookup (names, GUINT_TO_POINTER (data->contact)))
    {
      /* contact was in this tube */
      g_hash_table_insert (data->old_dbus_tubes, GUINT_TO_POINTER (tube_id),
          tube);
    }

  g_hash_table_unref (names);
}

struct
_emit_d_bus_names_changed_foreach_data
{
  SalutTubesChannel *self;
  TpHandle contact;
};

static void
emit_d_bus_names_changed_foreach (gpointer key,
                                  gpointer value,
                                  gpointer user_data)
{
  guint tube_id = GPOINTER_TO_UINT (key);
  SalutTubeDBus *tube = SALUT_TUBE_DBUS (value);
  struct _emit_d_bus_names_changed_foreach_data *data =
    (struct _emit_d_bus_names_changed_foreach_data *) user_data;
  GHashTable *names;
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (
      data->self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  /* Remove from the D-Bus names mapping */
  g_object_get (tube, "dbus-names", &names, NULL);
  g_hash_table_remove (names, GUINT_TO_POINTER (data->contact));
  g_hash_table_unref (names);

  /* Emit the DBusNamesChanged signal */
  d_bus_names_changed_removed (data->self, tube_id, data->contact);

  tp_handle_unref (contact_repo, data->contact);
}

static void
muc_connection_received_stanza_cb (GibberMucConnection *conn,
                                   const gchar *sender,
                                   GibberXmppStanza *stanza,
                                   gpointer user_data)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (user_data);
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle contact;
  GibberXmppNode *tubes_node;
  GSList *l;
  gboolean request = FALSE;
  GHashTable *old_dbus_tubes;
  struct _add_in_old_dbus_tubes_data add_data;
  struct _emit_d_bus_names_changed_foreach_data emit_data;

  contact = tp_handle_lookup (contact_repo, sender, NULL, NULL);
  if (contact == 0)
    {
      DEBUG ("unknown sender: %s", sender);
      return;
    }

  if (contact == priv->self_handle)
    /* We don't need to inspect our own presence */
    return;

  tubes_node = gibber_xmpp_node_get_child_ns (stanza->node, "tubes",
      GIBBER_TELEPATHY_NS_TUBES);
  if (tubes_node == NULL)
    return;

  if (!tp_strdiff (gibber_xmpp_node_get_attribute (tubes_node, "request"),
        "true"))
    request = TRUE;

  /* Fill old_dbus_tubes with D-BUS tubes previoulsy announced by
   * the contact */
  old_dbus_tubes = g_hash_table_new (g_direct_hash, g_direct_equal);
  add_data.old_dbus_tubes = old_dbus_tubes;
  add_data.contact = contact;
  g_hash_table_foreach (priv->tubes, add_in_old_dbus_tubes, &add_data);

  for (l = tubes_node->children; l != NULL; l = l->next)
    {
      GibberXmppNode *tube_node = (GibberXmppNode *) l->data;
      const gchar *stream_id;
      SalutTubeIface *tube;
      guint tube_id;
      SalutTubeType type;

      stream_id = gibber_xmpp_node_get_attribute (tube_node, "stream-id");
      if (stream_id == NULL)
        {
          DEBUG ("no stream id attribute");
          continue;
        }

      extract_tube_information (self, tube_node, NULL,
          NULL, NULL, NULL, NULL, &tube_id);
      tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));

      if (tube == NULL)
        {
          /* We don't know yet this tube */
          const gchar *service;
          SalutTubeType type;
          TpHandle initiator_handle;
          GHashTable *parameters;
          guint tube_id;

          if (extract_tube_information (self, tube_node, &type,
                &initiator_handle, &service, &parameters, NULL, &tube_id))
            {
              create_new_tube (self, type, initiator_handle,
                  service, parameters, stream_id, tube_id);
              tube = g_hash_table_lookup (priv->tubes,
                  GUINT_TO_POINTER (tube_id));

              /* the tube has reffed its initiator, no need to keep a ref */
              tp_handle_unref (contact_repo, initiator_handle);
            }
        }
      else
        {
          /* The contact is in the tube.
           * Remove it from old_dbus_tubes if needed */
          g_hash_table_remove (old_dbus_tubes, GUINT_TO_POINTER (tube_id));
        }

      if (tube == NULL)
        continue;

      g_object_get (tube, "type", &type, NULL);

      if (type == SALUT_TUBE_TYPE_DBUS)
        {
          /* Update mapping of handle -> D-Bus name. */
          GHashTable *names;
          gchar *name;

          g_object_get (tube, "dbus-names", &names, NULL);
          g_assert (names);
          name = g_hash_table_lookup (names, GUINT_TO_POINTER (contact));

          if (!name)
            {
              /* Contact just joined the tube */
              const gchar *new_name;

              new_name = gibber_xmpp_node_get_attribute (tube_node,
                  "dbus-name");

              if (!new_name)
                {
                  DEBUG ("Contact %u isn't announcing their D-Bus name",
                         contact);
                  continue;
                }

              add_name_in_dbus_names (self, tube_id, contact, new_name);
            }

          g_hash_table_unref (names);
        }
    }

  /* Tubes remaining in old_dbus_tubes was left by the contact */
  emit_data.contact = contact;
  emit_data.self = self;
  g_hash_table_foreach (old_dbus_tubes, emit_d_bus_names_changed_foreach,
      &emit_data);

  g_hash_table_destroy (old_dbus_tubes);

  if (request)
    /* Contact requested tubes information */
    update_tubes_info (self, FALSE);
}

static void
muc_connection_lost_sender_cb (GibberMucConnection *conn,
                               const gchar *sender,
                               gpointer user_data)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (user_data);
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle contact;
  GHashTable *old_dbus_tubes;
  struct _add_in_old_dbus_tubes_data add_data;
  struct _emit_d_bus_names_changed_foreach_data emit_data;

  contact = tp_handle_lookup (contact_repo, sender, NULL, NULL);
  if (contact == 0)
    {
      DEBUG ("unknown sender: %s", sender);
      return;
    }

  old_dbus_tubes = g_hash_table_new (g_direct_hash, g_direct_equal);
  add_data.old_dbus_tubes = old_dbus_tubes;
  add_data.contact = contact;
  g_hash_table_foreach (priv->tubes, add_in_old_dbus_tubes, &add_data);

  /* contact left the muc so he left all its tubes */
  emit_data.contact = contact;
  emit_data.self = self;
  g_hash_table_foreach (old_dbus_tubes, emit_d_bus_names_changed_foreach,
      &emit_data);

  g_hash_table_destroy (old_dbus_tubes);
}

static void
copy_tube_in_ptr_array (gpointer key,
                        gpointer value,
                        gpointer user_data)
{
  SalutTubeIface *tube = (SalutTubeIface *) value;
  guint tube_id = GPOINTER_TO_UINT(key);
  TpHandle initiator;
  gchar *service;
  GHashTable *parameters;
  SalutTubeState state;
  SalutTubeType type;
  GPtrArray *array = (GPtrArray *) user_data;
  GValue entry = {0,};

  g_object_get (tube,
                "type", &type,
                "initiator", &initiator,
                "service", &service,
                "parameters", &parameters,
                "state", &state,
                NULL);

  g_value_init (&entry, SALUT_CHANNEL_TUBE_TYPE);
  g_value_take_boxed (&entry,
          dbus_g_type_specialized_construct (SALUT_CHANNEL_TUBE_TYPE));
  dbus_g_type_struct_set (&entry,
          0, tube_id,
          1, initiator,
          2, type,
          3, service,
          4, parameters,
          5, state,
          G_MAXUINT);

  g_ptr_array_add (array, g_value_get_boxed (&entry));
  g_free (service);
  g_hash_table_unref (parameters);
}

static GPtrArray *
make_tubes_ptr_array (SalutTubesChannel *self,
                      GHashTable *tubes)
{
  GPtrArray *ret;

  ret = g_ptr_array_sized_new (g_hash_table_size (tubes));

  g_hash_table_foreach (tubes, copy_tube_in_ptr_array, ret);

  return ret;
}

/**
 * salut_tubes_channel_list_tubes
 *
 * Implements D-Bus method ListTubes
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_list_tubes (SalutSvcChannelTypeTubes *iface,
                                 DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  SalutTubesChannelPrivate *priv;
  GPtrArray *ret;
  guint i;

  g_assert (SALUT_IS_TUBES_CHANNEL (self));

  priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  ret = make_tubes_ptr_array (self, priv->tubes);
  salut_svc_channel_type_tubes_return_from_list_tubes (context, ret);

  for (i = 0; i < ret->len; i++)
    g_boxed_free (SALUT_CHANNEL_TUBE_TYPE, ret->pdata[i]);

  g_ptr_array_free (ret, TRUE);
}

static void
tube_closed_cb (SalutTubeIface *tube,
                gpointer user_data)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (user_data);
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  guint tube_id;

  if (priv->closed)
    return;

  g_object_get (tube, "id", &tube_id, NULL);
  if (!g_hash_table_remove (priv->tubes, GUINT_TO_POINTER (tube_id)))
    {
      DEBUG ("Can't find tube having this id: %d", tube_id);
    }
  DEBUG ("tube %d removed", tube_id);

  /* Emit the DBusNamesChanged signal */
  d_bus_names_changed_removed (self, tube_id, priv->self_handle);

  update_tubes_info (self, FALSE);

  salut_svc_channel_type_tubes_emit_tube_closed (self, tube_id);
}

static void
tube_opened_cb (SalutTubeIface *tube,
                gpointer user_data)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (user_data);
  guint tube_id;

  g_object_get (tube, "id", &tube_id, NULL);

  salut_svc_channel_type_tubes_emit_tube_state_changed (self, tube_id,
      SALUT_TUBE_STATE_OPEN);
}

static void
create_new_tube (SalutTubesChannel *self,
                 SalutTubeType type,
                 TpHandle initiator,
                 const gchar *service,
                 GHashTable *parameters,
                 const gchar *stream_id,
                 guint tube_id)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  SalutTubeIface *tube;
  SalutTubeState state;
  GibberMucConnection *muc_connection = NULL;

  if (self->muc != NULL)
    g_object_get (self->muc, "muc-connection", &muc_connection, NULL);

  switch (type)
    {
    case SALUT_TUBE_TYPE_DBUS:
      tube = SALUT_TUBE_IFACE (salut_tube_dbus_new (priv->conn,
          priv->handle, priv->handle_type, priv->self_handle, muc_connection,
          initiator, service, parameters, stream_id, tube_id));
      break;
      /*
    case TP_TUBE_TYPE_STREAM_UNIX:
      tube = SALUT_TUBE_IFACE (salut_tube_stream_new (priv->conn,
          priv->handle, priv->handle_type, priv->self_handle, initiator,
          service, parameters, tube_id));
      break;
      */
    default:
      g_assert_not_reached ();
    }

  DEBUG ("create tube %u", tube_id);
  g_hash_table_insert (priv->tubes, GUINT_TO_POINTER (tube_id), tube);
  update_tubes_info (self, FALSE);

  g_object_get (tube, "state", &state, NULL);

  salut_svc_channel_type_tubes_emit_new_tube (self,
      tube_id,
      initiator,
      type,
      service,
      parameters,
      state);

  if (type == SALUT_TUBE_TYPE_DBUS &&
      state != SALUT_TUBE_STATE_LOCAL_PENDING)
    {
      add_yourself_in_dbus_names (self, tube_id);
    }

  g_signal_connect (tube, "opened", G_CALLBACK (tube_opened_cb), self);
  g_signal_connect (tube, "closed", G_CALLBACK (tube_closed_cb), self);

  if (muc_connection != NULL)
    g_object_unref (muc_connection);
}

static gboolean
extract_tube_information (SalutTubesChannel *self,
                          GibberXmppNode *tube_node,
                          SalutTubeType *type,
                          TpHandle *initiator_handle,
                          const gchar **service,
                          GHashTable **parameters,
                          gboolean *offering,
                          guint *tube_id)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  if (type != NULL)
    {
      const gchar *_type;

      _type = gibber_xmpp_node_get_attribute (tube_node, "type");


      /*
      if (!tp_strdiff (_type, "stream"))
        {
          *type = TP_TUBE_TYPE_STREAM_UNIX;
        }
        */
      if (!tp_strdiff (_type, "dbus"))
        {
          *type = SALUT_TUBE_TYPE_DBUS;
        }
      else
        {
          DEBUG ("Unknown tube type: %s", _type);
          return FALSE;
        }
    }

  if (initiator_handle != NULL)
    {
      const gchar *initiator;

      initiator = gibber_xmpp_node_get_attribute (tube_node, "initiator");
      *initiator_handle = tp_handle_ensure (contact_repo, initiator, NULL,
          NULL);

      if (*initiator_handle == 0)
        {
          DEBUG ("invalid initiator ID %s", initiator);
          return FALSE;
        }
    }

  if (service != NULL)
    {
      *service = gibber_xmpp_node_get_attribute (tube_node, "service");
    }

  if (parameters != NULL)
    {
      GibberXmppNode *node;

      node = gibber_xmpp_node_get_child (tube_node, "parameters");
      *parameters = salut_gibber_xmpp_node_extract_properties (node,
          "parameter");
    }

  if (offering != NULL)
    {
      const gchar *_offering;

      _offering = gibber_xmpp_node_get_attribute (tube_node, "offering");
      if (!tp_strdiff (_offering, "false"))
        *offering = FALSE;
      else
        *offering = TRUE;
    }

  if (tube_id != NULL)
    {
      const gchar *str;
      gchar *endptr;
      long int tmp;

      str = gibber_xmpp_node_get_attribute (tube_node, "id");
      if (str == NULL)
        {
          DEBUG ("no tube id in SI request");
          return FALSE;
        }

      tmp = strtol (str, &endptr, 10);
      if (!endptr || *endptr)
        {
          DEBUG ("tube id is not numeric: %s", str);
          return FALSE;
        }
      *tube_id = (int) tmp;
    }

  return TRUE;
}

static void
copy_parameter (gpointer key,
                gpointer value,
                gpointer user_data)
{
  const gchar *prop = key;
  GValue *gvalue = value;
  GHashTable *parameters = user_data;
  GValue *gvalue_copied;

  gvalue_copied = g_slice_new0 (GValue);
  g_value_init (gvalue_copied, G_VALUE_TYPE (gvalue));
  g_value_copy (gvalue, gvalue_copied);

  g_hash_table_insert (parameters, g_strdup (prop), gvalue_copied);
}

static void
publish_tube_in_node (SalutTubesChannel *self,
                      GibberXmppNode *node,
                      SalutTubeIface *tube)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  GibberXmppNode *parameters_node;
  GHashTable *parameters;
  SalutTubeType type;
  gchar *service, *id_str;
  guint tube_id;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
    (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle initiator_handle;
  const gchar *initiator;

  g_object_get (G_OBJECT (tube),
      "service", &service,
      "parameters", &parameters,
      "type", &type,
      "id", &tube_id,
      "initiator", &initiator_handle,
      NULL);

  id_str = g_strdup_printf ("%u", tube_id);

  gibber_xmpp_node_set_attribute (node, "service", service);
  gibber_xmpp_node_set_attribute (node, "id", id_str);
  initiator = tp_handle_inspect (contact_repo, initiator_handle);
  gibber_xmpp_node_set_attribute (node, "initiator", initiator);

  g_free (id_str);

  switch (type)
    {
      case SALUT_TUBE_TYPE_DBUS:
        gibber_xmpp_node_set_attribute (node, "type", "dbus");
        break;
        /*
      case TP_TUBE_TYPE_STREAM_UNIX:
        gibber_xmpp_node_set_attribute (node, "type", "stream");
        break;
        */
      default:
        g_assert_not_reached ();
    }

  if (type == SALUT_TUBE_TYPE_DBUS)
    {
      gchar *name, *stream_id;

      g_object_get (G_OBJECT (tube),
          "dbus-name", &name,
          "stream-id", &stream_id,
          NULL);

      gibber_xmpp_node_set_attribute (node, "dbus-name", name);
      gibber_xmpp_node_set_attribute (node, "stream-id", stream_id);

      g_free (name);
      g_free (stream_id);
    }

  parameters_node = gibber_xmpp_node_add_child (node, "parameters");
  salut_gibber_xmpp_node_add_children_from_properties (parameters_node,
      parameters, "parameter");

  g_free (service);
  g_hash_table_unref (parameters);
}

struct _i_hate_g_hash_table_foreach
{
  SalutTubesChannel *self;
  GibberXmppNode *tubes_node;
};

static void
publish_tubes_in_node (gpointer key,
                       gpointer value,
                       gpointer user_data)
{
  SalutTubeIface *tube = (SalutTubeIface *) value;
  struct _i_hate_g_hash_table_foreach *data =
    (struct _i_hate_g_hash_table_foreach *) user_data;
  SalutTubeState state;
  GibberXmppNode *tube_node;

  if (tube == NULL)
    return;

  g_object_get (tube,
                "state", &state,
                NULL);

  if (state != SALUT_TUBE_STATE_OPEN)
    return;

  tube_node = gibber_xmpp_node_add_child (data->tubes_node, "tube");
  publish_tube_in_node (data->self, tube_node, tube);
}

static gboolean
update_tubes_info (SalutTubesChannel *self,
                   gboolean request)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *conn = (TpBaseConnection*) priv->conn;
  TpHandleRepoIface *room_repo = tp_base_connection_get_handles (
      conn, TP_HANDLE_TYPE_ROOM);
  GibberXmppStanza *msg;
  GibberXmppNode *node;
  const gchar *jid;
  struct _i_hate_g_hash_table_foreach data;
  GError *error = NULL;

  if (priv->handle_type != TP_HANDLE_TYPE_ROOM)
    return FALSE;

  /* build the message */
  jid = tp_handle_inspect (room_repo, priv->handle);

  msg = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_MESSAGE,
      GIBBER_STANZA_SUB_TYPE_GROUPCHAT,
      priv->conn->name, jid,
      GIBBER_NODE, "tubes",
        GIBBER_NODE_XMLNS, GIBBER_TELEPATHY_NS_TUBES,
      GIBBER_NODE_END, GIBBER_STANZA_END);

  node = gibber_xmpp_node_get_child_ns (msg->node, "tubes",
      GIBBER_TELEPATHY_NS_TUBES);

  if (request)
    gibber_xmpp_node_set_attribute (node, "request", "true");

  data.self = self;
  data.tubes_node = node;

  g_hash_table_foreach (priv->tubes, publish_tubes_in_node, &data);

  /* Send it */
  if (!gibber_muc_connection_send (priv->muc_connection, msg, &error))
    {
      g_warning ("%s: sending tubes info failed: %s", G_STRFUNC,
          error->message);
      g_error_free (error);
      g_object_unref (msg);
      return FALSE;
    }

  g_object_unref (msg);
  return TRUE;
}

static gint
generate_tube_id (void)
{
  return g_random_int_range (0, G_MAXINT);
}

/* XXX we should move that in some kind of bytestream factory */
gchar *
generate_stream_id (SalutTubesChannel *self)
{
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  gchar *stream_id;

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      stream_id = g_strdup_printf ("%lu-%u", (unsigned long) time (NULL),
          g_random_int ());
    }
  else
    {
      /* GibberMucConnection's stream-id is a guint8 */
      stream_id = g_strdup_printf ("%u", g_random_int_range (1, G_MAXUINT8));
    }

  return stream_id;
}

/**
 * salut_tubes_channel_offer_d_bus_tube
 *
 * Implements D-Bus method OfferDBusTube
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_offer_d_bus_tube (SalutSvcChannelTypeTubes *iface,
                                      const gchar *service,
                                      GHashTable *parameters,
                                      DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  SalutTubesChannelPrivate *priv;
  TpBaseConnection *base;
  guint tube_id;
  SalutTubeIface *tube;
  GHashTable *parameters_copied;
  gchar *stream_id;

  g_assert (SALUT_IS_TUBES_CHANNEL (self));

  priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  base = (TpBaseConnection*) priv->conn;

  parameters_copied = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) tp_g_value_slice_free);
  g_hash_table_foreach (parameters, copy_parameter, parameters_copied);

  stream_id = generate_stream_id (self);
  tube_id = generate_tube_id ();

  create_new_tube (self, SALUT_TUBE_TYPE_DBUS, priv->self_handle,
      service, parameters_copied, (const gchar*) stream_id, tube_id);

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));

  salut_svc_channel_type_tubes_return_from_offer_d_bus_tube (context, tube_id);

  g_free (stream_id);
}

/**
 * salut_tubes_channel_offer_stream_unix_tube
 *
 * Implements D-Bus method OfferStreamUnixTube
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
#if 0
static void
salut_tubes_channel_offer_stream_unix_tube (SalutSvcChannelTypeTubes *iface,
                                             const gchar *service,
                                             const gchar *socket,
                                             GHashTable *parameters,
                                             DBusGMethodInvocation *context)
{
  GError error = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
      "Stream Unix tube not implemented" };

  dbus_g_method_return_error (context, &error);
  return;
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  SalutTubesChannelPrivate *priv;
  TpBaseConnection *base;
  guint tube_id;
  SalutTubeIface *tube;
  GHashTable *parameters_copied;
  gchar *stream_id;
  struct stat stat_buff;

  g_assert (SALUT_IS_TUBES_CHANNEL (self));

  priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  base = (TpBaseConnection*) priv->conn;

  if (g_stat (socket, &stat_buff) == -1)
    {
      GError *error = NULL;

      DEBUG ("Error calling stat on socket: %s", g_strerror (errno));

      error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "%s: %s",
          socket, g_strerror (errno));

      dbus_g_method_return_error (context, error);

      g_error_free (error);
      return;
    }

  if (!S_ISSOCK (stat_buff.st_mode))
    {
      GError *error = NULL;

      DEBUG ("%s is not a socket", socket);

      error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "%s is not a socket", socket);

      dbus_g_method_return_error (context, error);

      g_error_free (error);
      return;
    }

  parameters_copied = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) tp_g_value_slice_free);
  g_hash_table_foreach (parameters, copy_parameter, parameters_copied);

  stream_id = salut_bytestream_factory_generate_stream_id ();
  tube_id = generate_tube_id ();

  create_new_tube (self, TP_TUBE_TYPE_STREAM_UNIX, priv->self_handle,
      service, parameters_copied, (const gchar*) stream_id, tube_id);

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (tube_id));

  g_object_set (tube, "socket", socket, NULL);

  if (priv->handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      /* Stream initiation */
      GError *error = NULL;

      if (!start_stream_initiation (self, tube, stream_id, &error))
        {
          salut_tube_iface_close (tube);

          dbus_g_method_return_error (context, error);

          g_error_free (error);
          g_free (stream_id);
          return;
        }
    }

  g_signal_connect (tube, "new-connection",
      G_CALLBACK (stream_unix_tube_new_connection_cb), self);

  tp_svc_channel_type_tubes_return_from_offer_d_bus_tube (context, tube_id);

  g_free (stream_id);
}
#endif

/**
 * salut_tubes_channel_accept_d_bus_tube
 *
 * Implements D-Bus method AcceptDBusTube
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_accept_d_bus_tube (SalutSvcChannelTypeTubes *iface,
                                       guint id,
                                       DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  SalutTubesChannelPrivate *priv;
  SalutTubeIface *tube;
  SalutTubeState state;
  SalutTubeType type;
  gchar *addr;

  g_assert (SALUT_IS_TUBES_CHANNEL (self));

  priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));
  if (tube == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &error);

      return;
    }

  g_object_get (tube,
      "type", &type,
      "state", &state,
      NULL);

  if (type != SALUT_TUBE_TYPE_DBUS)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not a D-Bus tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  if (state != SALUT_TUBE_STATE_LOCAL_PENDING)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not in the local pending state" };

      dbus_g_method_return_error (context, &error);

      return;
    }

  salut_tube_iface_accept (tube);

  update_tubes_info (self, FALSE);

  g_object_get (tube,
      "dbus-address", &addr,
      NULL);

  add_yourself_in_dbus_names (self, id);

  salut_svc_channel_type_tubes_return_from_accept_d_bus_tube (context, addr);
  g_free (addr);
}

/**
 * salut_tubes_channel_close_tube
 *
 * Implements D-Bus method CloseTube
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_close_tube (SalutSvcChannelTypeTubes *iface,
                                 guint id,
                                 DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  SalutTubesChannelPrivate *priv;
  SalutTubeIface *tube;

  g_assert (SALUT_IS_TUBES_CHANNEL (self));

  priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));
  if (tube == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  salut_tube_iface_close (tube);

  salut_svc_channel_type_tubes_return_from_close_tube (context);
}

/**
 * salut_tubes_channel_get_d_bus_tube_address
 *
 * Implements D-Bus method GetDBusTubeAddress
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_get_d_bus_tube_address (SalutSvcChannelTypeTubes *iface,
                                            guint id,
                                            DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  SalutTubesChannelPrivate *priv;
  SalutTubeIface *tube;
  gchar *addr;
  SalutTubeType type;
  SalutTubeState state;

  g_assert (SALUT_IS_TUBES_CHANNEL (self));

  priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));

  if (tube == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube,
      "type", &type,
      "state", &state,
      NULL);

  if (type != SALUT_TUBE_TYPE_DBUS)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not a D-Bus tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  if (state != SALUT_TUBE_STATE_OPEN)
    {
      GError error = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Tube is not open" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube, "dbus-address", &addr, NULL);
  salut_svc_channel_type_tubes_return_from_get_d_bus_tube_address (context,
      addr);
  g_free (addr);
}

static void
get_d_bus_names_foreach (gpointer key,
                         gpointer value,
                         gpointer user_data)
{
  GPtrArray *ret = user_data;
  GValue tmp = {0,};

  g_value_init (&tmp, DBUS_NAME_PAIR_TYPE);
  g_value_take_boxed (&tmp,
      dbus_g_type_specialized_construct (DBUS_NAME_PAIR_TYPE));
  dbus_g_type_struct_set (&tmp,
      0, key,
      1, value,
      G_MAXUINT);
  g_ptr_array_add (ret, g_value_get_boxed (&tmp));
}

/**
 * salut_tubes_channel_get_d_bus_names
 *
 * Implements D-Bus method GetDBusNames
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_get_d_bus_names (SalutSvcChannelTypeTubes *iface,
                                      guint id,
                                      DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  SalutTubeIface *tube;
  GHashTable *names;
  GPtrArray *ret;
  SalutTubeType type;
  SalutTubeState state;
  guint i;

  g_assert (SALUT_IS_TUBES_CHANNEL (self));

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));

  if (tube == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube,
      "type", &type,
      "state", &state,
      NULL);

  if (type != SALUT_TUBE_TYPE_DBUS)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not a D-Bus tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  if (state != SALUT_TUBE_STATE_OPEN)
    {
      GError error = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Tube is not open" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube, "dbus-names", &names, NULL);
  g_assert (names);

  ret = g_ptr_array_sized_new (g_hash_table_size (names));
  g_hash_table_foreach (names, get_d_bus_names_foreach, ret);

  salut_svc_channel_type_tubes_return_from_get_d_bus_names (context, ret);

  for (i = 0; i < ret->len; i++)
    g_boxed_free (DBUS_NAME_PAIR_TYPE, ret->pdata[i]);
  g_hash_table_unref (names);
  g_ptr_array_free (ret, TRUE);
}

/**
 * salut_tubes_channel_get_stream_unix_socket_address
 *
 * Implements D-Bus method GetStreamSocketAddress
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
#if 0
static void
salut_tubes_channel_get_stream_unix_socket_address (SalutSvcChannelTypeTubes *iface,
                                                    guint id,
                                                    DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  SalutTubesChannelPrivate *priv  = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  SalutTubeIface *tube;
  SalutTubeType type;
  SalutTubeState state;
  gchar *socket;

  tube = g_hash_table_lookup (priv->tubes, GUINT_TO_POINTER (id));
  if (tube == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "Unknown tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube,
      "type", &type,
      "state", &state,
      NULL);

  if (type != TP_TUBE_TYPE_STREAM_UNIX)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Tube is not a Stream tube" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  if (state != SALUT_TUBE_STATE_OPEN)
    {
      GError error = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Tube is not open" };

      dbus_g_method_return_error (context, &error);
      return;
    }

  g_object_get (tube,
      "socket", &socket,
      NULL);

  tp_svc_channel_type_tubes_return_from_get_stream_unix_socket_address (
      context, socket);

  g_free (socket);
}
#endif

/**
 * salut_tubes_channel_get_available_stream_tube_types
 *
 * Implements D-Bus method GetAvailableStreamTubeTypes
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_get_available_stream_tube_types (SalutSvcChannelTypeTubes *iface,
                                                     DBusGMethodInvocation *context)
{
  GHashTable *ret;

  ret = g_hash_table_new (g_direct_hash, g_direct_equal);

  salut_svc_channel_type_tubes_return_from_get_available_stream_tube_types (
      context, ret);

  g_hash_table_destroy (ret);
}

/**
 * salut_tubes_channel_offer_tube
 *
 * Implements D-Bus method OfferTube
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_offer_tube (SalutSvcChannelTypeTubes *iface,
                                guint tube_type,
                                const gchar *service,
                                GHashTable *parameters,
                                DBusGMethodInvocation *context)
{
  if (tube_type == SALUT_TUBE_TYPE_DBUS)
    {
      DEBUG ("deprecated");
      /* they have the same return signature, so it's safe to do: */
      salut_tubes_channel_offer_d_bus_tube (iface, service, parameters,
          context);
    }
  else
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Deprecated method OfferTube only works for D-Bus tubes" };

      dbus_g_method_return_error (context, &error);
    }
}

/**
 * salut_tubes_channel_accept_tube
 *
 * Implements D-Bus method AcceptTube
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_accept_tube (SalutSvcChannelTypeTubes *iface,
                                 guint id,
                                 DBusGMethodInvocation *context)
{
  DEBUG ("deprecated");
  salut_tubes_channel_accept_d_bus_tube (iface, id, context);
}

/**
 * salut_tubes_channel_get_d_bus_server_address
 *
 * Implements D-Bus method GetDBusServerAddress
 * on org.freedesktop.Telepathy.Channel.Type.Tubes
 */
static void
salut_tubes_channel_get_d_bus_server_address (SalutSvcChannelTypeTubes *iface,
                                               guint id,
                                               DBusGMethodInvocation *context)
{
  DEBUG ("deprecated");
  salut_tubes_channel_get_d_bus_tube_address (iface, id, context);
}

static void salut_tubes_channel_dispose (GObject *object);
static void salut_tubes_channel_finalize (GObject *object);

static void
salut_tubes_channel_class_init (
    SalutTubesChannelClass *salut_tubes_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_tubes_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_tubes_channel_class,
      sizeof (SalutTubesChannelPrivate));

  object_class->constructor = salut_tubes_channel_constructor;

  object_class->dispose = salut_tubes_channel_dispose;
  object_class->finalize = salut_tubes_channel_finalize;

  object_class->get_property = salut_tubes_channel_get_property;
  object_class->set_property = salut_tubes_channel_set_property;

  g_object_class_override_property (object_class, PROP_OBJECT_PATH,
      "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
      "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
      "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "Salut Connection that owns the connection for this tubes channel",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_object (
      "muc",
      "SalutMucChannel object",
      "Salut text MUC channel corresponding to this Tubes channel object, "
      "if the handle type is ROOM.",
      SALUT_TYPE_MUC_CHANNEL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_MUC, param_spec);

  signals[READY] = g_signal_new (
        "ready",
        G_OBJECT_CLASS_TYPE (salut_tubes_channel_class),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL,
        g_cclosure_marshal_VOID__VOID,
        G_TYPE_NONE, 0);

  signals[JOIN_ERROR] = g_signal_new (
        "join-error",
        G_OBJECT_CLASS_TYPE (salut_tubes_channel_class),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL,
        g_cclosure_marshal_VOID__POINTER,
        G_TYPE_NONE, 1, G_TYPE_POINTER);
}

void
salut_tubes_channel_dispose (GObject *object)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (object);
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, priv->handle_type);

  if (priv->dispose_has_run)
    return;

  if (priv->muc_connection != NULL)
    {
      g_signal_handlers_disconnect_matched (priv->muc_connection,
          G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);

      g_object_unref (priv->muc_connection);
      priv->muc_connection = NULL;
    }

  priv->dispose_has_run = TRUE;

  tp_handle_unref (handle_repo, priv->handle);

  if (self->muc != NULL)
      tp_external_group_mixin_finalize (object);

  salut_tubes_channel_close (self);

  /*
  g_object_unref (priv->contact);
  priv->contact = NULL;
  */

  if (G_OBJECT_CLASS (salut_tubes_channel_parent_class)->dispose)
    G_OBJECT_CLASS (salut_tubes_channel_parent_class)->dispose (object);
}

static void
salut_tubes_channel_finalize (GObject *object)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (object);
  SalutTubesChannelPrivate *priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  g_free (priv->object_path);

  G_OBJECT_CLASS (salut_tubes_channel_parent_class)->finalize (object);
}

static void
emit_tube_closed_signal (gpointer key,
                         gpointer value,
                         gpointer user_data)
{
  guint id = GPOINTER_TO_UINT (key);
  SalutTubesChannel *self = (SalutTubesChannel *) user_data;

  salut_svc_channel_type_tubes_emit_tube_closed (self, id);
}

void
salut_tubes_channel_close (SalutTubesChannel *self)
{
  SalutTubesChannelPrivate *priv;

  g_assert (SALUT_IS_TUBES_CHANNEL (self));

  DEBUG ("called on %p", self);

  priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  if (priv->closed)
    {
      return;
    }

  priv->closed = TRUE;

  g_hash_table_foreach (priv->tubes, emit_tube_closed_signal, self);
  g_hash_table_destroy (priv->tubes);

  priv->tubes = NULL;

  tp_svc_channel_emit_closed (self);
}

/**
 * salut_tubes_channel_close_async:
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_tubes_channel_close_async (TpSvcChannel *iface,
                                 DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);

  g_assert (SALUT_IS_TUBES_CHANNEL (self));

  salut_tubes_channel_close (self);
  tp_svc_channel_return_from_close (context);
}

/**
 * salut_tubes_channel_get_channel_type
 *
 * Tubesplements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_tubes_channel_get_channel_type (TpSvcChannel *iface,
                                      DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      SALUT_IFACE_CHANNEL_TYPE_TUBES);
}


/**
 * salut_tubes_channel_get_handle
 *
 * Tubesplements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_tubes_channel_get_handle (TpSvcChannel *iface,
                                DBusGMethodInvocation *context)
{
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);
  SalutTubesChannelPrivate *priv;

  g_assert (SALUT_IS_TUBES_CHANNEL (self));
  priv = SALUT_TUBES_CHANNEL_GET_PRIVATE (self);

  tp_svc_channel_return_from_get_handle (context, priv->handle_type,
      priv->handle);
}


/**
 * salut_tubes_channel_get_interfaces
 *
 * Tubesplements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
salut_tubes_channel_get_interfaces (TpSvcChannel *iface,
                                    DBusGMethodInvocation *context)
{
  const char *interfaces[] = {
      TP_IFACE_CHANNEL_INTERFACE_GROUP,
      NULL };
  SalutTubesChannel *self = SALUT_TUBES_CHANNEL (iface);

  if (self->muc)
    {
      tp_svc_channel_return_from_get_interfaces (context, interfaces);
    }
  else
    {
      /* only show the NULL */
      tp_svc_channel_return_from_get_interfaces (context, interfaces + 1);
    }
}

static void
tubes_iface_init (gpointer g_iface,
                  gpointer iface_data)
{
  SalutSvcChannelTypeTubesClass *klass = (SalutSvcChannelTypeTubesClass *)g_iface;

#define IMPLEMENT(x) salut_svc_channel_type_tubes_implement_##x (\
    klass, salut_tubes_channel_##x)
  IMPLEMENT(get_available_tube_types);
  IMPLEMENT(list_tubes);
  IMPLEMENT(close_tube);
  IMPLEMENT(offer_d_bus_tube);
  IMPLEMENT(accept_d_bus_tube);
  IMPLEMENT(get_d_bus_tube_address);
  IMPLEMENT(get_d_bus_names);
  /*
  IMPLEMENT(offer_stream_tube);
  IMPLEMENT(accept_stream_tube);
  IMPLEMENT(get_stream_tube_socket_address);
  */
  IMPLEMENT(get_available_stream_tube_types);
 /* DEPRECATED */
  IMPLEMENT(offer_tube);
  IMPLEMENT(accept_tube);
  IMPLEMENT(get_d_bus_server_address);
#undef IMPLEMENT
}

static void
channel_iface_init (gpointer g_iface,
                    gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *) g_iface;

#define IMPLEMENT(x, suffix) tp_svc_channel_implement_##x (\
    klass, salut_tubes_channel_##x##suffix)
  IMPLEMENT(close,_async);
  IMPLEMENT(get_channel_type,);
  IMPLEMENT(get_handle,);
  IMPLEMENT(get_interfaces,);
#undef IMPLEMENT
}
