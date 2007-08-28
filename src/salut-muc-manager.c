/*
 * salut-muc-manager.c - Source for SalutMucManager
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

#include "salut-muc-manager.h"

#include <gibber/gibber-muc-connection.h>

#include "salut-muc-channel.h"
#include "salut-contact-manager.h"
#include "salut-avahi-service-browser.h"
#include "salut-tubes-channel.h"
#include "salut-xmpp-connection-manager.h"

#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/interfaces.h>

#include "extensions/extensions.h"
#include "namespaces.h"

#define DEBUG_FLAG DEBUG_MUC
#include "debug.h"

static gboolean
invite_stanza_filter (SalutXmppConnectionManager *mgr,
    GibberXmppConnection *conn, GibberXmppStanza *stanza,
    SalutContact *contact, gpointer user_data);

static void
invite_stanza_callback (SalutXmppConnectionManager *mgr,
    GibberXmppConnection *conn, GibberXmppStanza *stanza,
    SalutContact *contact, gpointer user_data);


static void salut_muc_manager_factory_iface_init(gpointer *g_iface,
                                                     gpointer *iface_data);
G_DEFINE_TYPE_WITH_CODE(SalutMucManager, salut_muc_manager,
                        G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE,
                                        salut_muc_manager_factory_iface_init));

/* signal enum */
/*
enum
{
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};
*/

/* private structure */
typedef struct _SalutMucManagerPrivate SalutMucManagerPrivate;

struct _SalutMucManagerPrivate
{
  SalutConnection *connection;
  SalutXmppConnectionManager *xmpp_connection_manager;

  /* GUINT_TO_POINTER (room_handle) => (SalutMucChannel *) */
  GHashTable *text_channels;
   /* GUINT_TO_POINTER(room_handle) => (SalutTubesChannel *) */
  GHashTable *tubes_channels;
  /* Tubes channel requests which will be satisfied when the corresponding
   * text channel is created.
   * (SalutMucChannel *) => (SalutMucChannel *) */
  GHashTable *text_needed_for_tubes;

  gboolean dispose_has_run;
  SalutAvahiClient *client;
  SalutAvahiServiceBrowser *browser;
  /* room name => (SalutAvahiServiceResolver *) */
  GHashTable *room_resolvers;
};

#define SALUT_MUC_MANAGER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_MUC_MANAGER, SalutMucManagerPrivate))

static void
salut_muc_manager_init (SalutMucManager *obj)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (obj);
  priv->connection = NULL;
  priv->client = NULL;
  priv->browser = salut_avahi_service_browser_new ("_salut-room._udp");

  /* allocate any data required by the object here */
  priv->text_channels = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                         NULL, g_object_unref);

  priv->room_resolvers = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_object_unref);
  priv->tubes_channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);
  priv->text_needed_for_tubes = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, NULL);
}

static void salut_muc_manager_dispose (GObject *object);
static void salut_muc_manager_finalize (GObject *object);

static void
salut_muc_manager_class_init (SalutMucManagerClass *salut_muc_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_muc_manager_class);

  g_type_class_add_private (salut_muc_manager_class,
                              sizeof (SalutMucManagerPrivate));

  object_class->dispose = salut_muc_manager_dispose;
  object_class->finalize = salut_muc_manager_finalize;
}

void
salut_muc_manager_dispose (GObject *object)
{
  SalutMucManager *self = SALUT_MUC_MANAGER (object);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  salut_xmpp_connection_manager_remove_stanza_filter (
      priv->xmpp_connection_manager, NULL,
      invite_stanza_filter, invite_stanza_callback, self);

  tp_channel_factory_iface_close_all (TP_CHANNEL_FACTORY_IFACE (object));
  g_assert (priv->text_channels == NULL);
  g_assert (priv->tubes_channels == NULL);

  if (priv->room_resolvers)
    {
      g_hash_table_destroy (priv->room_resolvers);
      priv->room_resolvers = NULL;
    }

  if (priv->xmpp_connection_manager != NULL)
    {
      g_object_unref (priv->xmpp_connection_manager);
      priv->xmpp_connection_manager = NULL;
    }

  g_object_unref (priv->browser);

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_muc_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_muc_manager_parent_class)->dispose (object);
}

void
salut_muc_manager_finalize (GObject *object)
{
  /*SalutMucManager *self = SALUT_MUC_MANAGER (object);*/
  /*SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);*/

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (salut_muc_manager_parent_class)->finalize (object);
}

/* Channel Factory interface */

static void
salut_muc_manager_factory_iface_close_all(TpChannelFactoryIface *iface) {
  SalutMucManager *mgr = SALUT_MUC_MANAGER(iface);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE(mgr);

  if (priv->text_channels)
    {
      GHashTable *tmp = priv->text_channels;
      priv->text_channels = NULL;
      g_hash_table_destroy(tmp);
  }

  if (priv->client != NULL)
    {
      g_object_unref (priv->client);
      priv->client = NULL;
    }

  if (priv->tubes_channels != NULL)
    {
      GHashTable *tmp = priv->tubes_channels;
      priv->tubes_channels = NULL;
      g_hash_table_destroy (tmp);
    }

  if (priv->text_needed_for_tubes != NULL)
    {
      g_hash_table_destroy (priv->text_needed_for_tubes);
      priv->text_needed_for_tubes = NULL;
    }
}

static void
salut_muc_manager_factory_iface_connecting(TpChannelFactoryIface *iface) {
}

static void
salut_muc_manager_factory_iface_connected(TpChannelFactoryIface *iface) {
}

static void
salut_muc_manager_factory_iface_disconnected(TpChannelFactoryIface *iface) {
  /* FIMXE close all channels ? */
}

struct foreach_data {
  TpChannelFunc func;
  gpointer data;
};

static void
salut_muc_manager_iface_foreach_one(gpointer key,
                                    gpointer value,
                                    gpointer data) {
  TpChannelIface *chan = TP_CHANNEL_IFACE(value);
  struct foreach_data *f = (struct foreach_data *) data;

  f->func(chan, f->data);
}

static void
salut_muc_manager_factory_iface_foreach(TpChannelFactoryIface *iface,
                                        TpChannelFunc func, gpointer data) {
  SalutMucManager *mgr = SALUT_MUC_MANAGER(iface);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE(mgr);
  struct foreach_data f;
  f.func = func;
  f.data = data;

  g_hash_table_foreach(priv->text_channels, salut_muc_manager_iface_foreach_one, &f);
  g_hash_table_foreach (priv->tubes_channels,
      salut_muc_manager_iface_foreach_one, &f);
}

static void
muc_channel_closed_cb (SalutMucChannel *chan,
                       gpointer user_data)
{
  SalutMucManager *self = SALUT_MUC_MANAGER (user_data);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  TpHandle handle;

  if (priv->text_channels)
    {
      g_object_get(chan, "handle", &handle, NULL);
      DEBUG ("Removing channel with handle %u", handle);

      if (priv->tubes_channels != NULL)
        {
          SalutTubesChannel *tubes;

          tubes = g_hash_table_lookup (priv->tubes_channels,
              GUINT_TO_POINTER (handle));
          if (tubes != NULL)
            salut_tubes_channel_close (tubes);
        }

      g_hash_table_remove (priv->text_channels, GUINT_TO_POINTER (handle));
    }
}

static void
muc_channel_ready_cb (SalutMucChannel *chan,
                      SalutMucManager *self)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  SalutTubesChannel *tubes_chan;

  tp_channel_factory_iface_emit_new_channel (self, TP_CHANNEL_IFACE (chan),
      NULL);

  tubes_chan = g_hash_table_lookup (priv->text_needed_for_tubes, chan);
  if (tubes_chan != NULL)
    {
      g_hash_table_remove (priv->text_needed_for_tubes, chan);
      tp_channel_factory_iface_emit_new_channel (self,
          (TpChannelIface *) tubes_chan, NULL);
    }
}

/**
 * tubes_channel_closed_cb:
 *
 * Signal callback for when a tubes channel is closed. Removes the references
 * that MucManager holds to them.
 */
static void
tubes_channel_closed_cb (SalutTubesChannel *chan, gpointer user_data)
{
  SalutMucManager *fac = SALUT_MUC_MANAGER (user_data);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (fac);
  TpHandle room_handle;

  if (priv->tubes_channels != NULL)
    {
      g_object_get (chan, "handle", &room_handle, NULL);

      DEBUG ("removing MUC tubes channel with handle %u", room_handle);

      g_hash_table_remove (priv->tubes_channels,
          GUINT_TO_POINTER (room_handle));

      /* The channel will probably reopen soon due to an incoming tube message,
       * but closing the corresponding text channel would be too astonishing */
    }
}


static GibberMucConnection *
_get_connection (SalutMucManager *mgr,
                 const gchar *protocol,
                 GHashTable *parameters,
                 GError **error)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (mgr);

  return gibber_muc_connection_new (priv->connection->name,
      protocol, parameters, error);
}

static const gchar **
_get_connection_parameters (SalutMucManager *mgr,
                            const gchar *protocol)
{
  return gibber_muc_connection_get_required_parameters (protocol);
}

static SalutMucChannel *
salut_muc_manager_new_muc_channel (SalutMucManager *mgr,
                                   TpHandle handle,
                                   GibberMucConnection *connection)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE(mgr);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION(priv->connection);
  TpHandleRepoIface *room_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_ROOM);
  SalutMucChannel *chan;
  const gchar *name;
  gchar *path = NULL;

  g_assert (g_hash_table_lookup (priv->text_channels,
        GUINT_TO_POINTER (handle)) == NULL);
  DEBUG ("Requested channel for handle: %u", handle);

  /* FIXME The name of the muc and the handle might need to be different at
   * some point.. E.g. if two rooms are called the same */
  name = tp_handle_inspect (room_repo, handle);
  path = g_strdup_printf ("%s/MucChannel/%u", base_connection->object_path,
      handle);
  chan = g_object_new (SALUT_TYPE_MUC_CHANNEL,
      "connection", priv->connection,
      "object-path", path,
      "muc_connection", connection,
      "handle", handle,
      "name", name,
      "client", priv->client,
      "xmpp-connection-manager", priv->xmpp_connection_manager,
      NULL);
  g_free (path);

  g_hash_table_insert (priv->text_channels, GUINT_TO_POINTER (handle), chan);
  g_signal_connect (chan, "closed", G_CALLBACK (muc_channel_closed_cb), mgr);
  g_signal_connect (chan, "ready", G_CALLBACK (muc_channel_ready_cb), mgr);

  return chan;
}

/**
 * new_tubes_channel:
 *
 * Creates the SalutTubesChannel object with the given parameters.
 */
static SalutTubesChannel *
new_tubes_channel (SalutMucManager *self,
                   TpHandle room,
                   SalutMucChannel *muc)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *conn = (TpBaseConnection *) priv->connection;
  SalutTubesChannel *chan;
  char *object_path;

  g_assert (g_hash_table_lookup (priv->tubes_channels,
        GUINT_TO_POINTER (room)) == NULL);

  object_path = g_strdup_printf ("%s/MucTubesChannel%u",
      conn->object_path, room);

  DEBUG ("creating new tubes chan, object path %s", object_path);

  chan = g_object_new (SALUT_TYPE_TUBES_CHANNEL,
      "connection", priv->connection,
      "object-path", object_path,
      "handle", room,
      "handle-type", TP_HANDLE_TYPE_ROOM,
      "muc", muc,
      NULL);

  g_signal_connect (chan, "closed", (GCallback) tubes_channel_closed_cb, self);
  tp_channel_factory_iface_emit_new_channel (self, TP_CHANNEL_IFACE (chan),
      NULL);

  g_hash_table_insert (priv->tubes_channels, GUINT_TO_POINTER (room), chan);

  g_free (object_path);

  return chan;
}

static gchar *
_avahi_address_to_string_address (const AvahiAddress *address)
{
  gchar str[AVAHI_ADDRESS_STR_MAX];

  if (avahi_address_snprint (str, sizeof (str), address) == NULL)
    {
      DEBUG ("Failed to convert AvahiAddress to string");
      return NULL;
    }
  return g_strdup (str);
}

static SalutMucChannel *
salut_muc_manager_request_new_muc_channel (SalutMucManager *mgr,
                                           TpHandle handle,
                                           GError **error)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (mgr);
  TpBaseConnection *base_connection = (TpBaseConnection *) (priv->connection);
  TpHandleRepoIface *room_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_ROOM);
  GibberMucConnection *connection;
  SalutMucChannel *text_chan;
  GError *connection_error = NULL;
  const gchar *room_name;
  GHashTable *params = NULL;
  SalutAvahiServiceResolver *resolver;
  gboolean r;

  room_name = tp_handle_inspect (room_repo, handle);
  resolver = g_hash_table_lookup (priv->room_resolvers, room_name);
  if (resolver != NULL)
    {
      /* This MUC already exists on the network, so we reuse its
       * address */
      AvahiAddress avahi_address;
      uint16_t p;

      if (!salut_avahi_service_resolver_get_address (resolver,
            &avahi_address, &p))
        {
          DEBUG ("..._get_address failed: creating a new MUC room instead");
        }
      else
        {
          gchar *address = _avahi_address_to_string_address (&avahi_address);

          if (address == NULL)
            {
              DEBUG ("stringifying AvahiAddress failed: creating a new MUC "
                  "room instead");
            }
          else
            {
              gchar *port = g_strdup_printf ("%u", p);

              params = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
                  g_free);
              g_hash_table_insert (params, "address", address);
              g_hash_table_insert (params, "port", port);
              DEBUG ("found %s port %s for room %s", address, port, room_name);
            }
        }
    }
  else
    {
      DEBUG ("Didn't find address for room %s, let's generate one", room_name);
    }

  connection = _get_connection (mgr, NULL, params, &connection_error);

  if (params != NULL)
    g_hash_table_destroy (params);

  if (connection == NULL)
    {
      DEBUG ("get connection failed: %s", connection_error->message);
      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
          connection_error->message);
      g_error_free (connection_error);
      return NULL;
    }

  /* We requested the channel, so invite ourselves to it */
  if (!gibber_muc_connection_connect (connection, &connection_error))
    {
      DEBUG ("Connect failed: %s", connection_error->message);
      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
          connection_error->message);
      g_error_free (connection_error);
      g_object_unref (connection);
      return NULL;
    }
  DEBUG ("Connect succeeded");

  text_chan = salut_muc_manager_new_muc_channel (mgr, handle,
      connection);
  r = salut_muc_channel_invited (text_chan,
        base_connection->self_handle, NULL, NULL);
  /* Inviting ourselves to a connected channel should always
   * succeed */
  g_assert (r);

  return text_chan;
}

static TpChannelFactoryRequestStatus
salut_muc_manager_factory_iface_request (TpChannelFactoryIface *iface,
                                         const gchar *chan_type,
                                         TpHandleType handle_type,
                                         guint handle,
                                         gpointer request,
                                         TpChannelIface **ret,
                                         GError **error)
{
  SalutMucManager *mgr = SALUT_MUC_MANAGER (iface);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (mgr);
  TpBaseConnection *base_connection = (TpBaseConnection *) (priv->connection);
  TpHandleRepoIface *room_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_ROOM);
  SalutMucChannel *text_chan;
  TpChannelFactoryRequestStatus status;

  DEBUG ("MUC request: ctype=%s htype=%u handle=%u", chan_type, handle_type,
      handle);

  /* We only support room handles */
  if (handle_type != TP_HANDLE_TYPE_ROOM)
    {
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;
    }

  /* Most be a valid room handle */
  if (!tp_handle_is_valid (room_repo, handle, NULL))
    {
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;
    }

  if (!tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_TEXT))
    {
      text_chan = g_hash_table_lookup (priv->text_channels,
          GUINT_TO_POINTER (handle));

      if (text_chan != NULL)
        {
          status = TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING;
        }
      else
        {
          text_chan = salut_muc_manager_request_new_muc_channel (mgr,
              handle, error);
          if (text_chan == NULL)
            return TP_CHANNEL_FACTORY_REQUEST_STATUS_ERROR;

          status = TP_CHANNEL_FACTORY_REQUEST_STATUS_QUEUED;
        }

      g_assert (text_chan != NULL);
      *ret = TP_CHANNEL_IFACE (text_chan);
    }
  else if (!tp_strdiff (chan_type, SALUT_IFACE_CHANNEL_TYPE_TUBES))
    {
      SalutTubesChannel *tubes_chan;

      tubes_chan = g_hash_table_lookup (priv->tubes_channels,
          GUINT_TO_POINTER (handle));

      if (tubes_chan != NULL)
        {
          status = TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING;
        }
      else
        {
          text_chan = g_hash_table_lookup (priv->text_channels,
              GUINT_TO_POINTER (handle));

          if (text_chan == NULL)
            {
              DEBUG ("have to create the text channel before the tubes one");
              text_chan = salut_muc_manager_request_new_muc_channel (mgr,
                  handle, error);
              if (text_chan == NULL)
                return TP_CHANNEL_FACTORY_REQUEST_STATUS_ERROR;

              tubes_chan = new_tubes_channel (mgr, handle, text_chan);
              g_hash_table_insert (priv->text_needed_for_tubes,
                  text_chan, tubes_chan);
              status = TP_CHANNEL_FACTORY_REQUEST_STATUS_QUEUED;
            }
          else
            {
              tubes_chan = new_tubes_channel (mgr, handle, text_chan);
              status = TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED;
            }

        }

      g_assert (tubes_chan != NULL);
      *ret = TP_CHANNEL_IFACE (tubes_chan);
    }
  else
    {
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;
    }

  return status;
}

static void salut_muc_manager_factory_iface_init(gpointer *g_iface,
                                                     gpointer *iface_data) {
   TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *)g_iface;

   klass->close_all = salut_muc_manager_factory_iface_close_all;
   klass->connecting = salut_muc_manager_factory_iface_connecting;
   klass->connected = salut_muc_manager_factory_iface_connected;
   klass->disconnected = salut_muc_manager_factory_iface_disconnected;
   klass->foreach = salut_muc_manager_factory_iface_foreach;
   klass->request = salut_muc_manager_factory_iface_request;
}

static gboolean
invite_stanza_filter (SalutXmppConnectionManager *mgr,
                      GibberXmppConnection *conn,
                      GibberXmppStanza *stanza,
                      SalutContact *contact,
                      gpointer user_data)
{
  GibberStanzaType type;

  gibber_xmpp_stanza_get_type_info (stanza, &type, NULL);
  if (type != GIBBER_STANZA_TYPE_MESSAGE)
    return FALSE;

  return (gibber_xmpp_node_get_child_ns (stanza->node, "x", NS_LLMUC) != NULL);
}

static void
invite_stanza_callback (SalutXmppConnectionManager *mgr,
                        GibberXmppConnection *conn,
                        GibberXmppStanza *stanza,
                        SalutContact *contact,
                        gpointer user_data)
{
  SalutMucManager *self = SALUT_MUC_MANAGER (user_data);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *base_connection = TP_BASE_CONNECTION (priv->connection);
  TpHandleRepoIface *room_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_ROOM);
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles (base_connection, TP_HANDLE_TYPE_CONTACT);
  GibberXmppNode *node, *invite, *room_node, *reason_node;
  SalutMucChannel *chan;
  const gchar *room = NULL;
  const gchar *reason = NULL;
  const gchar *protocol = NULL;
  const gchar **params;
  TpHandle room_handle;
  TpHandle inviter_handle;
  const gchar **p;
  GHashTable *params_hash;
  GibberMucConnection *connection = NULL;

  node = gibber_xmpp_node_get_child_ns (stanza->node, "x", NS_LLMUC);
  g_assert (node != NULL);

  DEBUG("Got an invitation");

  invite = gibber_xmpp_node_get_child (node, "invite");
  if (invite == NULL)
    {
      DEBUG ("Got invitation, but no invite block!?");
      return;
    }

  room_node = gibber_xmpp_node_get_child (invite, "roomname");
  if (room_node == NULL)
    {
      DEBUG ("Invalid invitation, discarding");
      return;
    }
  room = room_node->content;

  reason_node = gibber_xmpp_node_get_child (invite, "reason");
  if (reason_node != NULL)
    reason = reason_node->content;

  if (reason == NULL)
    reason = "";

  protocol = gibber_xmpp_node_get_attribute (invite, "protocol");
  if (protocol == NULL)
    {
      DEBUG ("Invalid invitation, (missing protocol) discarding");
      return;
    }

  params = _get_connection_parameters (self, protocol);
  if (params == NULL)
    {
      DEBUG ("Invalid invitation, (unknown protocol) discarding");
      return;
    }

  params_hash = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
  for (p = params ; *p != NULL; p++)
    {
      GibberXmppNode *param;

      param = gibber_xmpp_node_get_child (invite, *p);
      if (param == NULL)
        {
          DEBUG("Invalid invitation, (missing parameter) discarding");
          goto discard;
        }

      g_hash_table_insert (params_hash, (gchar *) *p,
          g_strdup (param->content));
    }

  /* FIXME proper serialisation of handle name */
  /* Create the group if it doesn't exist and myself to local_pending */
  room_handle = tp_handle_ensure (room_repo, room, NULL, NULL);

  /* FIXME handle properly */
  g_assert (room_handle != 0);

  chan = g_hash_table_lookup (priv->text_channels,
      GINT_TO_POINTER (room_handle));

  if (chan == NULL)
    {
      connection = _get_connection (self, protocol, params_hash, NULL);
      if (connection == NULL)
        {
          DEBUG ("Invalid invitation, (wrong protocol parameters) discarding");
          goto discard;
        }

      if (connection == NULL)
        {
          tp_handle_unref (room_repo, room_handle);
          /* FIXME some kinda error to the user maybe ? Ignore for now */
          goto discard;
        }
      /* Need to create a new one */
      chan = salut_muc_manager_new_muc_channel (self, room_handle, connection);
    }

  /* FIXME handle properly */
  g_assert(chan != NULL);

  inviter_handle = tp_handle_ensure (contact_repo, contact->name, NULL, NULL);

#ifdef ENABLE_OLPC
  salut_connection_olpc_observe_invitation (priv->connection, room_handle,
      inviter_handle, invite);
#endif

  salut_muc_channel_invited (chan, inviter_handle, reason, NULL);
  tp_handle_unref (contact_repo, inviter_handle);

discard:
  if (params_hash != NULL)
    g_hash_table_destroy (params_hash);
}

/* public functions */
SalutMucManager *
salut_muc_manager_new (SalutConnection *connection,
                       SalutXmppConnectionManager *xmpp_connection_manager)
{
  SalutMucManager *ret = NULL;
  SalutMucManagerPrivate *priv;

  g_assert(connection != NULL);

  ret = g_object_new(SALUT_TYPE_MUC_MANAGER, NULL);
  priv = SALUT_MUC_MANAGER_GET_PRIVATE (ret);

  priv->xmpp_connection_manager = xmpp_connection_manager;
  g_object_ref (xmpp_connection_manager);

  salut_xmpp_connection_manager_add_stanza_filter (
      priv->xmpp_connection_manager, NULL,
      invite_stanza_filter, invite_stanza_callback, ret);

  priv->connection = connection;

  return ret;
}

static void
browser_found (SalutAvahiServiceBrowser *browser,
               AvahiIfIndex interface,
               AvahiProtocol protocol,
               const char *name,
               const char *type,
               const char *domain,
               SalutAvahiLookupResultFlags flags,
               gpointer userdata)
{
  SalutMucManager *self = SALUT_MUC_MANAGER (userdata);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  SalutAvahiServiceResolver *resolver;
  GError *error = NULL;

  resolver = g_hash_table_lookup (priv->room_resolvers, name);
  if (resolver != NULL)
    return;

  DEBUG ("found room: %s.%s.%s", name, type, domain);
  resolver = salut_avahi_service_resolver_new (interface, protocol,
      name, type, domain, protocol, 0);

  if (!salut_avahi_service_resolver_attach (resolver, priv->client,
        &error))
    {
      DEBUG ("resolver attach failed: %s", error->message);
      g_object_unref (resolver);
      g_error_free (error);
      return;
    }

  g_hash_table_insert (priv->room_resolvers, g_strdup (name), resolver);
}

static void
browser_removed (SalutAvahiServiceBrowser *browser,
                 AvahiIfIndex interface,
                 AvahiProtocol protocol,
                 const char *name,
                 const char *type,
                 const char *domain,
                 SalutAvahiLookupResultFlags flags,
                 gpointer userdata)
{
  SalutMucManager *self = SALUT_MUC_MANAGER (userdata);
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);

  DEBUG ("remove room: %s.%s.%s", name, type, domain);
  g_hash_table_remove (priv->room_resolvers, name);
}

static void
browser_failed (SalutAvahiServiceBrowser *browser,
                GError *error,
                gpointer userdata)
{
  /* FIXME proper error handling */
  DEBUG ("browser failed -> %s", error->message);
}

gboolean
salut_muc_manager_start (SalutMucManager *self,
                         SalutAvahiClient *client,
                         GError **error)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);

  if (priv->client != NULL)
    g_object_unref (priv->client);

  priv->client = client;
  g_object_ref (priv->client);

  g_signal_connect (priv->browser, "new-service",
                   G_CALLBACK (browser_found), self);
  g_signal_connect (priv->browser, "removed-service",
                   G_CALLBACK (browser_removed), self);
  g_signal_connect (priv->browser, "failure",
                   G_CALLBACK (browser_failed), self);

  if (!salut_avahi_service_browser_attach (priv->browser, priv->client,
        error))
    {
      DEBUG ("browser attach failed");
      return FALSE;
   }

  return TRUE;
}

SalutMucChannel *
salut_muc_manager_get_text_channel (SalutMucManager *self,
                                    TpHandle handle)
{
  SalutMucManagerPrivate *priv = SALUT_MUC_MANAGER_GET_PRIVATE (self);
  SalutMucChannel *muc;

  muc = g_hash_table_lookup (priv->text_channels, GUINT_TO_POINTER (handle));
  if (muc == NULL)
    return NULL;

  g_object_ref (muc);
  return muc;
}
