/*
 * salut-xmpp-connection-manager.c - Source for SalutXmppConnectionManager
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

#include "salut-xmpp-connection-manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "salut-tubes-channel.h"
#include "salut-connection.h"
#include "salut-contact.h"
#include "salut-contact-manager.h"

#include <gibber/gibber-xmpp-connection-listener.h>
#include <gibber/gibber-namespaces.h>
#include <gibber/gibber-linklocal-transport.h>

#define DEBUG_FLAG DEBUG_XCM
#include "debug.h"

#include "signals-marshal.h"

#define OUTGOING_CONNECTION_TIMEOUT 10000
#define REFCOUNT_CONNECTION_TIMEOUT 10000

G_DEFINE_TYPE (SalutXmppConnectionManager, salut_xmpp_connection_manager, \
    G_TYPE_OBJECT)

static void
new_connection_cb (GibberXmppConnectionListener *listener,
    GibberXmppConnection *connection, struct sockaddr_storage *addr,
    guint size, gpointer user_data);
static gboolean
create_new_outgoing_connection (SalutXmppConnectionManager *self,
    SalutContact *contact, GError **error);
static void
outgoing_connection_failed (SalutXmppConnectionManager *self,
    GibberXmppConnection *connection, SalutContact *contact,
    guint domain, gint code, const gchar *msg);

/* signals */
enum
{
  NEW_CONNECTION,
  CONNECTION_FAILED,
  CONNECTION_CLOSING,
  CONNECTION_CLOSED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_CONTACT_MANAGER,
};

/* private structure */
typedef struct _SalutXmppConnectionManagerPrivate \
          SalutXmppConnectionManagerPrivate;

struct _SalutXmppConnectionManagerPrivate
{
  GibberXmppConnectionListener *listener;
  SalutConnection *connection;
  SalutContactManager *contact_manager;
  /* TODO: we should probably be able to reduce the number of hash tables
   * using a struct ConnectionInfo or something. Hopefully that should allow
   * use to reduce the number of signal callbacks too. */
  GHashTable *connections;
  /* GibberXmppConnection -> GList of SalutContact */
  GHashTable *incoming_pending_connections;
  /* GibberXmppConnection -> SalutContact */
  GHashTable *outgoing_pending_connections;
  /* GibberXmppConnection -> GSList of StanzaFilter */
  GHashTable *stanza_filters;
  GSList *all_connection_filters;
  /* GibberXmppConnection -> guint (source id) */
  GHashTable *connection_timers;
  /* GibberXmppConnection -> guint */
  GHashTable *connection_refcounts;
  /* This hash table contains connections we are waiting they close before
   * be allowed to request a new connection with the contact */
  /* GibberXmppConnection * -> SalutContact */
  GHashTable *connections_waiting_close;

  gboolean dispose_has_run;
};

#define SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE(obj) \
    ((SalutXmppConnectionManagerPrivate *) \
      ((SalutXmppConnectionManager *)obj)->priv)

typedef struct
{
  SalutXmppConnectionManagerStanzaFilterFunc filter_func;
  SalutXmppConnectionManagerStanzaCallbackFunc callback;
  gpointer user_data;
} StanzaFilter;

static StanzaFilter *
stanza_filter_new (void)
{
  return g_slice_new (StanzaFilter);
}

static void
stanza_filter_free (StanzaFilter *filter)
{
  g_slice_free (StanzaFilter, filter);
}

static void
free_stanza_filters_list (GSList *list)
{
  GSList *l;

  for (l = list; l != NULL; l = g_slist_next (l))
    stanza_filter_free ((StanzaFilter *) l->data);

  g_slist_free (list);
}

static void
contact_list_destroy (GList *list)
{
  GList *l;

  for (l = list; l != NULL; l = g_list_next (l))
    {
      SalutContact *contact;
      contact = SALUT_CONTACT (l->data);
      g_object_unref (contact);
    }

  g_list_free (list);
}

GQuark
salut_xmpp_connection_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string (
        "salut_xmpp_connection_error");

  return quark;
}

static void
remove_timer (gpointer user_data)
{
  guint id = GPOINTER_TO_UINT (user_data);
  g_source_remove (id);
}

static guint
increment_connection_refcount (SalutXmppConnectionManager *self,
                               GibberXmppConnection *conn)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
  guint ref;

  ref = GPOINTER_TO_UINT (g_hash_table_lookup (priv->connection_refcounts,
        conn));
  ref++;
  g_hash_table_insert (priv->connection_refcounts, conn,
      GUINT_TO_POINTER (ref));
  return ref;
}

static guint
decrement_connection_refcount (SalutXmppConnectionManager *self,
                               GibberXmppConnection *conn)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
  guint ref;

  ref = GPOINTER_TO_UINT (g_hash_table_lookup (priv->connection_refcounts,
        conn));
  ref--;
  g_hash_table_insert (priv->connection_refcounts, conn,
      GUINT_TO_POINTER (ref));
  return ref;
}

static void
salut_xmpp_connection_manager_init (SalutXmppConnectionManager *self)
{
  SalutXmppConnectionManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_XMPP_CONNECTION_MANAGER, SalutXmppConnectionManagerPrivate);

  self->priv = priv;

  priv->connection = NULL;
  priv->listener = NULL;
  priv->contact_manager = NULL;

  priv->connections = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      g_object_unref, g_object_unref);
  priv->incoming_pending_connections = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, g_object_unref, (GDestroyNotify) contact_list_destroy);
  priv->stanza_filters = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) free_stanza_filters_list);
  priv->outgoing_pending_connections = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, g_object_unref, g_object_unref);
  priv->connection_timers = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, g_object_unref, remove_timer);
  priv->connection_refcounts = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, NULL);
  priv->connections_waiting_close = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, g_object_unref, g_object_unref);

  priv->all_connection_filters = NULL;

  priv->listener = gibber_xmpp_connection_listener_new ();
  g_signal_connect (priv->listener, "new-connection",
      G_CALLBACK (new_connection_cb), self);

  priv->dispose_has_run = FALSE;
}

static gboolean
has_transport (gpointer key,
               gpointer value,
               gpointer user_data)
{
  GibberXmppConnection *conn = GIBBER_XMPP_CONNECTION (key);

  return conn->transport == user_data;
}

static void
remove_connection (SalutXmppConnectionManager *self,
                   GibberXmppConnection *connection)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);

  g_hash_table_remove (priv->connections, connection);
  g_hash_table_remove (priv->stanza_filters, connection);
  g_hash_table_remove (priv->connection_refcounts, connection);
  g_hash_table_remove (priv->connections_waiting_close, connection);
}

static void
check_if_waiting_for_connection_closed (SalutXmppConnectionManager *self,
                                        GibberXmppConnection *connection)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
  SalutContact *contact;

  contact = g_hash_table_lookup (priv->connections_waiting_close, connection);
  if (contact != NULL)
    {
      GError *error = NULL;

      DEBUG ("connection with %s closed. We can now request the new one",
          contact->name);
      if (!create_new_outgoing_connection (self, contact, &error))
        {
          outgoing_connection_failed (self, connection, contact,
              error->domain, error->code, error->message);
          g_error_free (error);
        }
    }

  g_hash_table_remove (priv->connections_waiting_close, connection);
}

static void
close_connection (SalutXmppConnectionManager *self,
                  GibberXmppConnection *connection)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
  SalutContact *contact;

  contact = g_hash_table_lookup (priv->connections, connection);
  g_assert (contact != NULL);

  if ((connection->stream_flags & GIBBER_XMPP_CONNECTION_CLOSE_SENT) == 0)
    {
      /* We didn't close the connection, let's do it now */
      DEBUG ("send close to connection with %s", contact->name);
      gibber_xmpp_connection_close (connection);
    }

  if (connection->stream_flags == GIBBER_XMPP_CONNECTION_CLOSE_FULLY_CLOSED)
    {
      /* Connection is fully closed, let's remove it */

      DEBUG ("connection with %s fully closed. Remove it", contact->name);
      gibber_transport_disconnect (connection->transport);

      g_signal_emit (self, signals[CONNECTION_CLOSED], 0, connection, contact);
      check_if_waiting_for_connection_closed (self, connection);

      remove_connection (self, connection);
    }
  else
    {
      /* Wait for the remote contact close the connection too */
      DEBUG ("Wait for %s closing", contact->name);
      g_signal_emit (self, signals[CONNECTION_CLOSING], 0, connection,
          contact);
    }
}

static void
connection_stream_closed_cb (GibberXmppConnection *connection,
                             gpointer userdata)
{
  SalutXmppConnectionManager *self =
    SALUT_XMPP_CONNECTION_MANAGER (userdata);

  DEBUG ("Received stream closed");
  /* Other side closed the stream, do the same */
  close_connection (self, connection);
}

static void
apply_filters (SalutXmppConnectionManager *self,
               GibberXmppConnection *conn,
               GSList *list,
               GibberXmppStanza *stanza,
               SalutContact *contact)
{
  GSList *l;

  l = list;
  while (l != NULL)
    {
      StanzaFilter *filter = l->data;

      /* We iter on the list now because if the callback calls
       * remove_filter this list element will be freed */
      l = g_slist_next (l);

      if (filter->filter_func (self, conn, stanza, contact, filter->user_data))
        filter->callback (self, conn, stanza, contact, filter->user_data);
    }
}

struct connection_timeout_data
{
  SalutXmppConnectionManager *self;
  GibberXmppConnection *connection;
  SalutContact *contact;
};

static struct connection_timeout_data *
connection_timeout_data_new (void)
{
  return g_slice_new (struct connection_timeout_data);
}

static void
connection_timeout_data_free (struct connection_timeout_data *data)
{
  g_slice_free (struct connection_timeout_data, data);
}

static gboolean
outgoing_pending_connection_timeout (struct connection_timeout_data *data)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (data->self);

  if (g_hash_table_remove (priv->outgoing_pending_connections,
        data->connection))
    {
      /* Connection was still pending, let's raise an error */
      outgoing_connection_failed (data->self, data->connection, data->contact,
          SALUT_XMPP_CONNECTION_MANAGER_ERROR,
          SALUT_XMPP_CONNECTION_MANAGER_ERROR_TIMEOUT,
          "Outgoing connection timeout: remote contact didn't open it");
    }

  g_hash_table_remove (priv->connection_timers, data->connection);
  g_hash_table_remove (priv->connection_refcounts, data->connection);
  return FALSE;
}

static void
add_timeout (SalutXmppConnectionManager *self,
             GibberXmppConnection *connection,
             SalutContact *contact)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
  guint id;
  struct connection_timeout_data *data;

  data = connection_timeout_data_new ();
  data->self = self;
  data->connection = connection;
  data->contact = contact;

  id = g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE,
      OUTGOING_CONNECTION_TIMEOUT,
      (GSourceFunc) outgoing_pending_connection_timeout, data,
      (GDestroyNotify) connection_timeout_data_free);
  g_hash_table_insert (priv->connection_timers,
      g_object_ref (connection), GUINT_TO_POINTER (id));
}

static void
connection_stanza_received_cb (GibberXmppConnection *conn,
                               GibberXmppStanza *stanza,
                               gpointer user_data)
{
  SalutXmppConnectionManager *self = SALUT_XMPP_CONNECTION_MANAGER (user_data);
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
  SalutContact *contact;
  GSList *list;

  contact = g_hash_table_lookup (priv->connections, conn);
  if (contact == NULL)
    {
      DEBUG ("unknown connection, stanza ignored");
      return;
    }

  if (g_hash_table_remove (priv->connection_timers, conn))
    {
      /* reset the timer */
      add_timeout (self, conn, contact);
    }

  /* Connection specific filters */
  list = g_hash_table_lookup (priv->stanza_filters, conn);
  apply_filters (self, conn, list, stanza, contact);
  /* FIXME: if a filter removes filter that are after it in the list we are
   * fucked. */

  /* Filters for all connections */
  apply_filters (self, conn, priv->all_connection_filters, stanza, contact);
}

struct remove_connection_having_transport_data
{
  SalutXmppConnectionManager *self;
  GibberLLTransport *transport;
};

static gboolean
remove_connection_having_transport (gpointer key,
                                    gpointer value,
                                    gpointer user_data)
{
  GibberXmppConnection *conn = GIBBER_XMPP_CONNECTION (key);
  struct remove_connection_having_transport_data *data =
    (struct remove_connection_having_transport_data *) user_data;

  if (conn->transport == GIBBER_TRANSPORT (data->transport))
    {
      SalutXmppConnectionManagerPrivate *priv =
        SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (data->self);
      SalutContact *contact = SALUT_CONTACT (value);

      g_signal_emit (data->self, signals[CONNECTION_FAILED], 0,
          conn, contact, SALUT_XMPP_CONNECTION_MANAGER_ERROR,
          SALUT_XMPP_CONNECTION_MANAGER_ERROR_TRANSPORT_DISCONNECTED,
          "transport disconnected");

      check_if_waiting_for_connection_closed (data->self, conn);
      g_hash_table_remove (priv->stanza_filters, conn);
      g_hash_table_remove (priv->connection_refcounts, conn);
      return TRUE;
    }

  return FALSE;
}

static void
connection_transport_disconnected_cb (GibberLLTransport *transport,
                                      gpointer userdata)
{
  SalutXmppConnectionManager *self = SALUT_XMPP_CONNECTION_MANAGER (userdata);
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
  struct remove_connection_having_transport_data data;

  DEBUG ("Connection transport disconnected");
  data.self = self;
  data.transport = transport;
  g_hash_table_foreach_remove (priv->connections,
      remove_connection_having_transport, &data);
}

static void
connection_parse_error_cb (GibberXmppConnection *connection,
                           gpointer userdata)
{
  SalutXmppConnectionManager *self = SALUT_XMPP_CONNECTION_MANAGER (userdata);
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
  SalutContact *contact;

  contact = g_hash_table_lookup (priv->connections, connection);
  g_assert (contact != NULL);

  DEBUG ("Parse error on xml stream, closing connection with %s",
      contact->name);

  g_signal_handlers_disconnect_matched (connection, G_SIGNAL_MATCH_DATA,
      0, 0, NULL, NULL, self);
  gibber_transport_disconnect (connection->transport);

  g_signal_emit (self, signals[CONNECTION_FAILED], 0,
      connection, contact, SALUT_XMPP_CONNECTION_MANAGER_ERROR,
      SALUT_XMPP_CONNECTION_MANAGER_ERROR_PARSE_ERROR, "parse error");

  remove_connection (self, connection);
}

static void
connection_fully_open (SalutXmppConnectionManager *self,
                       GibberXmppConnection *connection,
                       SalutContact *contact)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);

  g_hash_table_insert (priv->connections, g_object_ref (connection),
      g_object_ref (contact));

  g_signal_connect (connection, "stream-closed",
      G_CALLBACK (connection_stream_closed_cb), self);
  g_signal_connect (connection, "received-stanza",
      G_CALLBACK (connection_stanza_received_cb), self);
  g_signal_connect (connection->transport, "disconnected",
      G_CALLBACK (connection_transport_disconnected_cb), self);
  g_signal_connect (connection, "parse-error",
      G_CALLBACK (connection_parse_error_cb), self);

  salut_xmpp_connection_manager_take_connection (self, connection);
  g_signal_emit (self, signals[NEW_CONNECTION], 0, connection, contact);
  /* connection timer will be started if no component are interested
   * by this connection */
  salut_xmpp_connection_manager_release_connection (self, connection);
}

static gboolean
incoming_pending_connection_got_from (SalutXmppConnectionManager *self,
                                      GibberXmppConnection *conn,
                                      const gchar *from)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
  GList *t;

  if (from == NULL)
    {
      DEBUG ("No valid ``from'' pending connection");
      goto error;
    }

  DEBUG ("Got stream from %s on pending connection", from);

  for (t = g_hash_table_lookup (priv->incoming_pending_connections, conn);
      t != NULL;
      t = g_list_next (t))
    {
      SalutContact *contact = SALUT_CONTACT (t->data);
      if (!tp_strdiff (contact->name, from))
        {
          struct sockaddr_storage addr;
          socklen_t size = sizeof (struct sockaddr_storage);

          g_signal_handlers_disconnect_matched (conn, G_SIGNAL_MATCH_DATA,
              0, 0, NULL, NULL, self);
          g_signal_handlers_disconnect_matched (conn->transport,
              G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);

          if (!gibber_ll_transport_get_address (
                GIBBER_LL_TRANSPORT (conn->transport), &addr, &size))
            {
              DEBUG ("Contact %s no longer alive", contact->name);
              goto error;
            }

          if (!salut_contact_has_address (contact, &addr))
            {
              DEBUG ("Contact %s doesn't have that address", contact->name);
              goto error;
            }

          DEBUG ("identify incoming pending connection with %s. "
              "It's now fully open", contact->name);
          connection_fully_open (self, conn, contact);
          g_hash_table_remove (priv->incoming_pending_connections, conn);

          return TRUE;
        }
  }

error:
  gibber_xmpp_connection_close (conn);
  gibber_transport_disconnect (conn->transport);
  g_hash_table_remove (priv->incoming_pending_connections, conn);
  return FALSE;
}

static void
incoming_pending_connection_stream_opened_cb (GibberXmppConnection *conn,
                                              const gchar *to,
                                              const gchar *from,
                                              const gchar *version,
                                              gpointer user_data)
{
  SalutXmppConnectionManager *self = SALUT_XMPP_CONNECTION_MANAGER (user_data);
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);

  DEBUG ("incoming pending connection with %s opened. Open it too", from);
  gibber_xmpp_connection_open (conn, from, priv->connection->name, "1.0");

  if (!tp_strdiff (version, "1.0"))
    {
      GibberXmppStanza *stanza;
      /* Send empty stream features */
      stanza = gibber_xmpp_stanza_new ("features");
      gibber_xmpp_node_set_ns (stanza->node, GIBBER_XMPP_NS_STREAM);
      gibber_xmpp_connection_send (conn, stanza, NULL);
      g_object_unref (stanza);
    }

  /* According to the xep-0174 revision >= there should
   * be a to and from.. But clients implementing older revision might not
   * support that yet.
   * */
  if (from != NULL)
    incoming_pending_connection_got_from (self, conn, from);
}

static void
incoming_pending_connection_stanza_received_cb (GibberXmppConnection *conn,
                                                GibberXmppStanza *stanza,
                                                gpointer userdata)
{
  SalutXmppConnectionManager *self = SALUT_XMPP_CONNECTION_MANAGER (conn);
  const gchar *from;

  /* If the identity wasn't clear from the stream opening we only wait to the
   * very first message */
  from = gibber_xmpp_node_get_attribute (stanza->node, "from");
  if (incoming_pending_connection_got_from (self, conn, from))
    {
      /* We can filter the stanza now */
      connection_stanza_received_cb (conn, stanza, self);
    }
}

static void
incoming_pending_connection_transport_disconnected_cb (
    GibberLLTransport *transport,
    gpointer userdata)
{
  SalutXmppConnectionManager *self = SALUT_XMPP_CONNECTION_MANAGER (userdata);
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);

  DEBUG ("Pending incoming connection disconnected");
  g_hash_table_foreach_remove (priv->incoming_pending_connections,
      has_transport, transport);
}

static void
incoming_pending_connection_stream_closed_cb (GibberXmppConnection *connection,
                                              gpointer userdata)
{
  SalutXmppConnectionManager *self =
    SALUT_XMPP_CONNECTION_MANAGER (userdata);
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);

  DEBUG ("Pending incoming connection stream closed");
  gibber_xmpp_connection_close (connection);
  gibber_transport_disconnect (connection->transport);
  g_hash_table_remove (priv->incoming_pending_connections, connection);
}

static void
incoming_pending_connection_parse_error_cb (GibberXmppConnection *conn,
                                            gpointer userdata)
{
  DEBUG ("Parse error on xml stream, closing pending incoming connection");
  /* Just close the transport, the disconnected callback will do the cleanup */
  gibber_transport_disconnect (conn->transport);
}

static void
new_connection_cb (GibberXmppConnectionListener *listener,
                   GibberXmppConnection *connection,
                   struct sockaddr_storage *addr,
                   guint size,
                   gpointer user_data)
{
  SalutXmppConnectionManager *self = SALUT_XMPP_CONNECTION_MANAGER (user_data);
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
  GList *contacts;

  DEBUG("Handling new incoming connection");

  contacts = salut_contact_manager_find_contacts_by_address (
      priv->contact_manager, addr);
  if (contacts == NULL)
    {
      DEBUG ("Couldn't find a contact for the incoming connection");
      gibber_transport_disconnect (connection->transport);
      gibber_xmpp_connection_close (connection);
      return;
    }

  /* If it's a transport to just one contacts machine, hook it up right away.
   * This is needed because iChat doesn't send message with to and
   * from data...
   */
  if (g_list_length (contacts) == 1)
    {
      SalutContact *contact = contacts->data;

      DEBUG ("incoming connection to just one contact machine (%s). "
          "Open it and consider it fully open", contact->name);

      gibber_xmpp_connection_open (connection, contact->name,
          priv->connection->name, "1.0");
      connection_fully_open (self, connection, contact);

      contact_list_destroy (contacts);
      return;
    }

  DEBUG ("Have to wait to know the contact before announce this incoming "
      "connection");
  /* We have to wait to know the contact before announce the connection */
  g_hash_table_insert (priv->incoming_pending_connections,
      g_object_ref (connection), contacts);

  g_signal_connect (connection, "stream-opened",
      G_CALLBACK (incoming_pending_connection_stream_opened_cb), self);
  g_signal_connect (connection, "received-stanza",
      G_CALLBACK (incoming_pending_connection_stanza_received_cb), self);
  g_signal_connect (connection->transport, "disconnected",
      G_CALLBACK (incoming_pending_connection_transport_disconnected_cb),
      self);
  g_signal_connect (connection, "stream-closed",
      G_CALLBACK (incoming_pending_connection_stream_closed_cb), self);
  g_signal_connect (connection, "parse-error",
      G_CALLBACK (incoming_pending_connection_parse_error_cb), self);
}

void
salut_xmpp_connection_manager_dispose (GObject *object)
{
  SalutXmppConnectionManager *self = SALUT_XMPP_CONNECTION_MANAGER (object);
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->contact_manager != NULL)
    {
      g_object_unref (priv->contact_manager);
      priv->contact_manager = NULL;
    }

  if (priv->listener != NULL)
    {
      g_object_unref (priv->listener);
      priv->listener = NULL;
    }

  if (priv->connections != NULL)
    {
      g_hash_table_destroy (priv->connections);
      priv->connections = NULL;
    }

  if (priv->incoming_pending_connections != NULL)
    {
      g_hash_table_destroy (priv->incoming_pending_connections);
      priv->incoming_pending_connections = NULL;
    }

  if (priv->outgoing_pending_connections != NULL)
    {
      g_hash_table_destroy (priv->outgoing_pending_connections);
      priv->outgoing_pending_connections = NULL;
    }

  if (priv->stanza_filters != NULL)
    {
      g_hash_table_destroy (priv->stanza_filters);
      priv->stanza_filters = NULL;
    }

  if (priv->connection_timers != NULL)
    {
      g_hash_table_destroy (priv->connection_timers);
      priv->connection_timers = NULL;
    }

  if (priv->connection_refcounts != NULL)
    {
      g_hash_table_destroy (priv->connection_refcounts);
      priv->connection_refcounts = NULL;
    }

  if (priv->connections_waiting_close != NULL)
    {
      g_hash_table_destroy (priv->connections_waiting_close);
      priv->connections_waiting_close = NULL;
    }

  free_stanza_filters_list (priv->all_connection_filters);
  priv->all_connection_filters = NULL;

  if (G_OBJECT_CLASS (salut_xmpp_connection_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_xmpp_connection_manager_parent_class)->dispose (
        object);
}

static void
salut_xmpp_connection_manager_get_property (GObject *object,
                                            guint property_id,
                                            GValue *value,
                                            GParamSpec *pspec)
{
  SalutXmppConnectionManager *self = SALUT_XMPP_CONNECTION_MANAGER (object);
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->connection);
        break;
      case PROP_CONTACT_MANAGER:
        g_value_set_object (value, priv->contact_manager);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_xmpp_connection_manager_set_property (GObject *object,
                                            guint property_id,
                                            const GValue *value,
                                            GParamSpec *pspec)
{
  SalutXmppConnectionManager *self = SALUT_XMPP_CONNECTION_MANAGER (object);
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->connection = g_value_get_object (value);
        break;
      case PROP_CONTACT_MANAGER:
        priv->contact_manager = g_value_get_object (value);
        g_object_ref (priv->contact_manager);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
salut_xmpp_connection_manager_class_init (
    SalutXmppConnectionManagerClass *salut_xmpp_connection_manager_class)
{
  GObjectClass *object_class =
    G_OBJECT_CLASS (salut_xmpp_connection_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_xmpp_connection_manager_class,
      sizeof (SalutXmppConnectionManagerPrivate));

  object_class->dispose = salut_xmpp_connection_manager_dispose;

  object_class->get_property = salut_xmpp_connection_manager_get_property;
  object_class->set_property = salut_xmpp_connection_manager_set_property;

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "Salut Connection associated with this manager ",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION,
      param_spec);

  param_spec = g_param_spec_object (
      "contact-manager",
      "SalutContactManager object",
      "Salut Contact Manager associated with the Salut Connection of this "
      "manager",
      SALUT_TYPE_CONTACT_MANAGER,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONTACT_MANAGER,
      param_spec);

  signals[NEW_CONNECTION] =
    g_signal_new (
        "new-connection",
        G_OBJECT_CLASS_TYPE (salut_xmpp_connection_manager_class),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
        0,
        NULL, NULL,
        salut_signals_marshal_VOID__OBJECT_OBJECT,
        G_TYPE_NONE, 2, GIBBER_TYPE_XMPP_CONNECTION, SALUT_TYPE_CONTACT);

  signals[CONNECTION_FAILED] =
    g_signal_new (
        "connection-failed",
        G_OBJECT_CLASS_TYPE (salut_xmpp_connection_manager_class),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
        0,
        NULL, NULL,
        salut_signals_marshal_VOID__OBJECT_OBJECT_UINT_UINT_STRING,
        G_TYPE_NONE, 5, GIBBER_TYPE_XMPP_CONNECTION, SALUT_TYPE_CONTACT,
        G_TYPE_UINT, G_TYPE_INT, G_TYPE_STRING);

  signals[CONNECTION_CLOSED] =
    g_signal_new (
        "connection-closed",
        G_OBJECT_CLASS_TYPE (salut_xmpp_connection_manager_class),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
        0,
        NULL, NULL,
        salut_signals_marshal_VOID__OBJECT_OBJECT,
        G_TYPE_NONE, 2, GIBBER_TYPE_XMPP_CONNECTION, SALUT_TYPE_CONTACT);

  signals[CONNECTION_CLOSING] =
    g_signal_new (
        "connection-closing",
        G_OBJECT_CLASS_TYPE (salut_xmpp_connection_manager_class),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
        0,
        NULL, NULL,
        salut_signals_marshal_VOID__OBJECT_OBJECT,
        G_TYPE_NONE, 2, GIBBER_TYPE_XMPP_CONNECTION, SALUT_TYPE_CONTACT);
}

SalutXmppConnectionManager *
salut_xmpp_connection_manager_new (SalutConnection *connection,
                                   SalutContactManager *contact_manager)
{
  g_assert (connection != NULL);
  g_assert (contact_manager != NULL);

  return g_object_new (
      SALUT_TYPE_XMPP_CONNECTION_MANAGER,
      "connection", connection,
      "contact-manager", contact_manager,
      NULL);
}

int
salut_xmpp_connection_manager_listen (SalutXmppConnectionManager *self,
                                      GError **error)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
  int port;

  for (port = 5298; port < 5400; port++)
    {
      GError *e = NULL;
      if (gibber_xmpp_connection_listener_listen (priv->listener, port,
            &e))
        break;

      if (e->code != GIBBER_XMPP_CONNECTION_LISTENER_ERROR_ADDR_IN_USE)
        {
          g_propagate_error (error, e);
          return -1;
        }

      g_error_free (e);
      e = NULL;
    }

  if (port >= 5400)
    return -1;

  return port;
}

struct find_connection_for_contact_data
{
  SalutContact *contact;
  GibberXmppConnection *connection;
};

static gboolean
find_connection_for_contact (gpointer key,
                             gpointer value,
                             gpointer user_data)
{
  SalutContact *contact = SALUT_CONTACT (value);
  struct find_connection_for_contact_data *data =
    (struct find_connection_for_contact_data *) user_data;

  if (data->contact == contact)
    {
      data->connection = GIBBER_XMPP_CONNECTION (key);
      return TRUE;
    }

  return FALSE;
}

static gboolean
find_connection_for_contact_in_list (gpointer key,
                                     gpointer value,
                                     gpointer user_data)
{
  GList *l, *contacts = (GList *) value;
  struct find_connection_for_contact_data *data =
    (struct find_connection_for_contact_data *) user_data;

  l = g_list_find (contacts, data->contact);
  if (l != NULL)
    {
      data->connection = GIBBER_XMPP_CONNECTION (l->data);
      return TRUE;
    }

  return FALSE;
}

static void
outgoing_pending_connection_fully_open (SalutXmppConnectionManager *self,
                                        GibberXmppConnection *conn)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
  SalutContact *contact;

  g_signal_handlers_disconnect_matched (conn, G_SIGNAL_MATCH_DATA,
      0, 0, NULL, NULL, self);
  g_signal_handlers_disconnect_matched (conn->transport,
      G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);

  contact = g_hash_table_lookup (priv->outgoing_pending_connections, conn);
  g_assert (contact != NULL);
  g_hash_table_remove (priv->outgoing_pending_connections, conn);

  connection_fully_open (self, conn, contact);
}

static void
outgoing_pending_connection_stream_opened_cb (GibberXmppConnection *conn,
                                              const gchar *to,
                                              const gchar *from,
                                              const gchar *version,
                                              gpointer user_data)
{
  SalutXmppConnectionManager *self = SALUT_XMPP_CONNECTION_MANAGER (user_data);

  outgoing_pending_connection_fully_open (self, conn);
}

static void
outgoing_pending_connection_transport_disconnected_cb (
    GibberLLTransport *transport,
    gpointer userdata)
{
  SalutXmppConnectionManager *self = SALUT_XMPP_CONNECTION_MANAGER (userdata);
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);

  DEBUG ("Pending outgoing connection disconnected");
  g_hash_table_foreach_remove (priv->outgoing_pending_connections,
      has_transport, transport);
}

static void
outgoing_pending_connection_stream_closed_cb (GibberXmppConnection *connection,
                                              gpointer userdata)
{
  SalutXmppConnectionManager *self =
    SALUT_XMPP_CONNECTION_MANAGER (userdata);
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);

  DEBUG ("Pending outgoing connection stream closed");
  gibber_xmpp_connection_close (connection);
  gibber_transport_disconnect (connection->transport);
  g_hash_table_remove (priv->outgoing_pending_connections, connection);
}

static void
outgoing_pending_connection_parse_error_cb (GibberXmppConnection *conn,
                                            gpointer userdata)
{
  DEBUG ("Parse error on xml stream, closing pending outgoing connection");
  /* Just close the transport, the disconnected callback will do the cleanup */
  gibber_transport_disconnect (conn->transport);
}

static void
outgoing_connection_failed (SalutXmppConnectionManager *self,
                            GibberXmppConnection *connection,
                            SalutContact *contact,
                            guint domain,
                            gint code,
                            const gchar *msg)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);

  g_hash_table_remove (priv->outgoing_pending_connections, connection);
  g_signal_handlers_disconnect_matched (connection, G_SIGNAL_MATCH_DATA,
      0, 0, NULL, NULL, self);

  g_signal_emit (self, signals[CONNECTION_FAILED], 0,
      connection, contact, domain, code, msg);
}

static gboolean
create_new_outgoing_connection (SalutXmppConnectionManager *self,
                                SalutContact *contact,
                                GError **error)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
  GibberXmppConnection *connection;
  GibberLLTransport *transport;
  GArray *addrs;
  gint i;
  GError *e = NULL;

  DEBUG ("create a new outgoing connection to %s", contact->name);

  transport = gibber_ll_transport_new ();
  connection = gibber_xmpp_connection_new (GIBBER_TRANSPORT (transport));
  /* Let the xmpp connection own the transport */
  g_object_unref (transport);

  addrs = salut_contact_get_addresses (contact);

  if (addrs->len == 0)
    {
      DEBUG ("can't find addresse for contact %s", contact->name);

      g_set_error (error, SALUT_XMPP_CONNECTION_MANAGER_ERROR,
          SALUT_XMPP_CONNECTION_MANAGER_ERROR_NO_ADDRESSE,
          "can't find addresse for contact %s", contact->name);

      g_array_free (addrs, TRUE);
      g_object_unref (connection);
      return FALSE;
    }

  for (i = 0; i < addrs->len; i++)
    {
      salut_contact_address_t *addr;

      addr = &(g_array_index (addrs, salut_contact_address_t, i));

      if (e != NULL)
        {
          /* We'll return the last GError */
          g_error_free (e);
          e = NULL;
        }

      if (gibber_ll_transport_open_sockaddr (transport, &(addr->address),
            &e))
        {
          DEBUG ("connected to %s. Open the XMPP connection now",
              contact->name);
          gibber_xmpp_connection_open (connection, contact->name,
              priv->connection->name, "1.0");

          /* The remote contact have now to open the connection to fully
           * open it */
          DEBUG ("waiting for remote contact (%s) open the connection",
              contact->name);
          g_hash_table_insert (priv->outgoing_pending_connections, connection,
              g_object_ref (contact));

          add_timeout (self, connection, contact);
          g_signal_connect (connection, "stream-opened",
              G_CALLBACK (outgoing_pending_connection_stream_opened_cb), self);
          g_signal_connect (connection->transport, "disconnected",
              G_CALLBACK (
                outgoing_pending_connection_transport_disconnected_cb), self);
          g_signal_connect (connection, "stream-closed",
              G_CALLBACK (outgoing_pending_connection_stream_closed_cb), self);
          g_signal_connect (connection, "parse-error",
              G_CALLBACK (outgoing_pending_connection_parse_error_cb), self);

          /* init connection refcount to 1.
           * The connection is not in the hash table yet but as
           * GUINT_TO_POINTER (NULL) == 0 that does exactly what
           * we want */
          increment_connection_refcount (self, connection);

          g_array_free (addrs, TRUE);
          return TRUE;
        }
    }

  DEBUG ("All connection attempts to %s failed: %s", contact->name,
      e->message);
  g_propagate_error (error, e);
  g_array_free (addrs, TRUE);
  g_object_unref (connection);
  return FALSE;
}

SalutXmppConnectionManagerRequestConnectionResult
salut_xmpp_connection_manager_request_connection (
    SalutXmppConnectionManager *self,
    SalutContact *contact,
    GibberXmppConnection **conn,
    GError **error)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
  struct find_connection_for_contact_data data;

  g_assert (conn != NULL);

  /* Check for existing fully opened connections */
  data.contact = contact;
  data.connection = NULL;
  g_hash_table_find (priv->connections, find_connection_for_contact, &data);

  if (data.connection != NULL)
    {
      if (data.connection->stream_flags ==
          GIBBER_XMPP_CONNECTION_STREAM_FULLY_OPEN)
        {
          /* Connection is not closing, we can reuse it */
          DEBUG ("found existing connection with %s", contact->name);
          *conn = data.connection;

          salut_xmpp_connection_manager_take_connection (self,
              data.connection);
          return SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_DONE;
        }
      else
        {
          /* We have to wait this connection is fully closed before
           * establish a new one as we can't have more than one connection
           * to the same contact at the same time */

          DEBUG ("found existing closing connection with %s. Wait for this "
              "connection is fully closed before requesting a new one",
              contact->name);
          g_hash_table_insert (priv->connections_waiting_close,
              g_object_ref (data.connection), g_object_ref (contact));

          /* XXX we should maybe set a timer here to avoid to be blocked
           * if this connection is never closed.
           * Or maybe we shouldn't allow a connection to stay in the closing
           * state forever ? */
          return
            SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_PENDING;
        }
    }

  /* check for outgoing pending connection */
  g_hash_table_find (priv->outgoing_pending_connections,
      find_connection_for_contact, &data);
  if (data.connection != NULL)
    {
      DEBUG ("found existing outgoing pending connection with %s",
          contact->name);

      salut_xmpp_connection_manager_take_connection (self, data.connection);
      /* There is already a timer for this outgoing pending connection */
      return SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_PENDING;
    }

  /* check for incoming pending connection */
  g_hash_table_find (priv->incoming_pending_connections,
      find_connection_for_contact_in_list, &data);
  if (data.connection != NULL)
    {
      DEBUG ("found existing incoming pending connection with %s",
          contact->name);

      salut_xmpp_connection_manager_take_connection (self, data.connection);
      /* XXX Here again, maybe we should set a timer to avoid incoming pending
       * connections stay pending forever */
      return SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_PENDING;
    }

  DEBUG ("no existing connection with %s. create a new one", contact->name);
  if (create_new_outgoing_connection (self, contact, error))
    return SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_PENDING;
  else
    return SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_FAILURE;
}

static gboolean
refcount_connection_timeout (struct connection_timeout_data *data)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (data->self);

  DEBUG ("refcount timer of connection with %s expired. Connection closed",
      data->contact->name);

  close_connection (data->self, data->connection);

  g_hash_table_remove (priv->connection_timers, data->connection);
  return FALSE;
}

void
salut_xmpp_connection_manager_release_connection (
    SalutXmppConnectionManager *self,
    GibberXmppConnection *connection)
{
  if (decrement_connection_refcount (self, connection) <= 0)
    {
      SalutXmppConnectionManagerPrivate *priv =
        SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
      SalutContact *contact;
      struct connection_timeout_data *data;
      guint id;

      contact = g_hash_table_lookup (priv->connections, connection);
      if (contact == NULL)
        return;

      DEBUG ("refcount of connection with %s failed to 0. Start its timer",
          contact->name);

      data = connection_timeout_data_new ();
      data->self = self;
      data->connection = connection;
      data->contact = contact;

      id = g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE,
          REFCOUNT_CONNECTION_TIMEOUT,
          (GSourceFunc) refcount_connection_timeout, data,
          (GDestroyNotify) connection_timeout_data_free);

      g_hash_table_insert (priv->connection_timers,
          g_object_ref (connection), GUINT_TO_POINTER (id));
    }
}

void
salut_xmpp_connection_manager_take_connection (
    SalutXmppConnectionManager *self,
    GibberXmppConnection *connection)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
  guint ref;

  ref = increment_connection_refcount (self, connection);
  if (ref == 1)
    {
      /* ref count was to 0, remove the ref count timer */
      DEBUG ("connection ref count raised 1. Remove its timer");
      g_hash_table_remove (priv->connection_timers, connection);
    }
}

GSList *
find_filter (GSList *list,
             SalutXmppConnectionManagerStanzaFilterFunc filter_func,
             SalutXmppConnectionManagerStanzaCallbackFunc callback,
             gpointer user_data)
{
  GSList *l;

  for (l = list; l != NULL; l = g_slist_next (l))
    {
      StanzaFilter *filter = l->data;

      if (filter->filter_func == filter_func &&
          filter->callback == callback &&
          filter->user_data == user_data)
        {
          return l;
        }
    }

  return NULL;
}

GSList *
remove_filter (GSList *list,
               SalutXmppConnectionManagerStanzaFilterFunc filter_func,
               SalutXmppConnectionManagerStanzaCallbackFunc callback,
               gpointer user_data)
{
  GSList *l;

  l = find_filter (list, filter_func, callback, user_data);;
  if (l == NULL)
    return list;

  stanza_filter_free ((StanzaFilter *) l->data);
  return g_slist_delete_link (list, l);
}

gboolean
salut_xmpp_connection_manager_add_stanza_filter (
    SalutXmppConnectionManager *self,
    GibberXmppConnection *conn,
    SalutXmppConnectionManagerStanzaFilterFunc filter_func,
    SalutXmppConnectionManagerStanzaCallbackFunc callback,
    gpointer user_data)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
  StanzaFilter *filter;

  filter = stanza_filter_new ();
  filter->filter_func = filter_func;
  filter->callback = callback;
  filter->user_data = user_data;

  if (conn == NULL)
    {
      if (find_filter (priv->all_connection_filters, filter_func, callback,
            user_data) != NULL)
        {
          /* No need to add twice the same filter */
          stanza_filter_free (filter);
          return FALSE;
        }

      priv->all_connection_filters = g_slist_prepend (
          priv->all_connection_filters, filter);
    }
  else
    {
      GSList *list;

      if (g_hash_table_lookup (priv->connections, conn) == NULL)
        {
          DEBUG ("unknown connection");
          stanza_filter_free (filter);
          return FALSE;
        }

      list = g_hash_table_lookup (priv->stanza_filters, conn);
      if (find_filter (list, filter_func, callback, user_data) != NULL)
        {
          /* No need to add twice the same filter */
          stanza_filter_free (filter);
          return FALSE;
        }

      g_hash_table_steal (priv->stanza_filters, conn);
      list = g_slist_prepend (list, filter);
      g_hash_table_insert (priv->stanza_filters, conn, list);
    }

  return TRUE;
}

void
salut_xmpp_connection_manager_remove_stanza_filter (
    SalutXmppConnectionManager *self,
    GibberXmppConnection *conn,
    SalutXmppConnectionManagerStanzaFilterFunc filter_func,
    SalutXmppConnectionManagerStanzaCallbackFunc callback,
    gpointer user_data)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);

  if (conn == NULL)
    {
      priv->all_connection_filters = remove_filter (
          priv->all_connection_filters, filter_func, callback, user_data);
    }
  else
    {
      GSList *list;

      list = g_hash_table_lookup (priv->stanza_filters, conn);
      g_hash_table_steal (priv->stanza_filters, conn);
      list = remove_filter (list, filter_func, callback, user_data);
      g_hash_table_insert (priv->stanza_filters, conn, list);
    }
}
