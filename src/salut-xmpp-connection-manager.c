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

G_DEFINE_TYPE (SalutXmppConnectionManager, salut_xmpp_connection_manager, \
    G_TYPE_OBJECT)

/* signals */
enum
{
  NEW_CONNECTION,
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
  GHashTable *connections;
  GHashTable *pending_connections;
  /* GibberXmppConnection -> GSList of StanzaFilter */
  GHashTable *stanza_filters;
  GSList *all_connection_filters;

  gboolean dispose_has_run;
};

#define SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE(obj) \
    ((SalutXmppConnectionManagerPrivate *) obj->priv)

typedef struct
{
  SalutXmppConnectionManagerStanzaFilterFunc filter_func;
  SalutXmppConnectionManagerStanzaCallbackFunc callback;
  gpointer user_data;
} StanzaFilter;

static void
free_stanza_filters_list (GSList *list)
{
  GSList *l;

  for (l = list; l != NULL; l = l->next)
    g_slice_free (StanzaFilter, l->data);

  g_slist_free (list);
}

static void
contact_list_destroy (gpointer data)
{
  GList *list = (GList *) data;
  GList *t = list;
  while (t != NULL)
    {
      SalutContact *contact;
      contact = SALUT_CONTACT (t->data);
      g_object_unref (contact);
      t = g_list_next (t);
    }
  g_list_free (list);
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
  priv->pending_connections = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, g_object_unref, contact_list_destroy);
  priv->stanza_filters = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) free_stanza_filters_list);

  priv->all_connection_filters = NULL;

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
connection_stream_closed_cb (GibberXmppConnection *connection,
                             gpointer userdata)
{
  SalutXmppConnectionManager *self =
    SALUT_XMPP_CONNECTION_MANAGER (userdata);
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);

  DEBUG ("Connection stream closed");
  g_hash_table_remove (priv->connections, connection);
  g_hash_table_remove (priv->stanza_filters, connection);
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
      l = l->next;

      if (filter->filter_func (self, conn, stanza, contact, filter->user_data))
        filter->callback (self, conn, stanza, contact, filter->user_data);
    }
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

  /* Connection specific filters */
  list = g_hash_table_lookup (priv->stanza_filters, conn);
  apply_filters (self, conn, list, stanza, contact);

  /* Filters for all connections */
  apply_filters (self, conn, priv->all_connection_filters, stanza, contact);
}

static void
found_contact_for_connection (SalutXmppConnectionManager *self,
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

  g_signal_emit (self, signals[NEW_CONNECTION], 0, connection, contact);
}

static void
pending_connection_got_from (SalutXmppConnectionManager *self,
                             GibberXmppConnection *conn,
                             GibberXmppStanza *stanza,
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
  t = g_hash_table_lookup (priv->pending_connections, conn);

  while (t != NULL)
    {
      SalutContact *contact = SALUT_CONTACT (t->data);
      if (strcmp (contact->name, from) == 0)
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
              DEBUG ("Contact no longer alive");
              goto error;
            }

          if (!salut_contact_has_address (contact, &addr))
            {
              DEBUG ("Contact doesn't have that address");
              goto error;
            }

          found_contact_for_connection (self, conn, contact);
          g_hash_table_remove (priv->pending_connections, conn);

          if (stanza != NULL)
            /* We can filter the stanza now */
            connection_stanza_received_cb (conn, stanza, self);

          return;
        }
    t = g_list_next (t);
  }

error:
  gibber_xmpp_connection_close (conn);
  gibber_transport_disconnect (conn->transport);
  g_hash_table_remove (priv->pending_connections, conn);
}

static void
pending_connection_stream_opened_cb (GibberXmppConnection *conn,
                                     const gchar *to,
                                     const gchar *from,
                                     const gchar *version,
                                     gpointer user_data)
{
  SalutXmppConnectionManager *self = SALUT_XMPP_CONNECTION_MANAGER (user_data);
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
  GibberXmppStanza *stanza;

  gibber_xmpp_connection_open (conn, from, priv->connection->name, "1.0");
  /* Send empty stream features */
  stanza = gibber_xmpp_stanza_new ("features");
  gibber_xmpp_node_set_ns (stanza->node, GIBBER_XMPP_NS_STREAM);
  gibber_xmpp_connection_send (conn, stanza, NULL);
  g_object_unref (stanza);

  /* According to the xep-0174 revision >= there should
   * be a to and from.. But clients implementing older revision might not
   * support that yet.
   * */
  if (from != NULL)
    pending_connection_got_from (self, conn, NULL, from);
}

static void
pending_connection_stanza_received_cb (GibberXmppConnection *conn,
                                       GibberXmppStanza *stanza,
                                       gpointer userdata)
{
  SalutXmppConnectionManager *self = SALUT_XMPP_CONNECTION_MANAGER (conn);
  const gchar *from;

  /* If the identity wasn't clear from the stream opening we only wait to the
   * very first message */
  from = gibber_xmpp_node_get_attribute (stanza->node, "from");
  pending_connection_got_from (self, conn, stanza, from);
}

static void
pending_connection_transport_disconnected_cb (GibberLLTransport *transport,
                                              gpointer userdata)
{
  SalutXmppConnectionManager *self = SALUT_XMPP_CONNECTION_MANAGER (userdata);
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);

  DEBUG ("Pending connection disconnected");
  g_hash_table_foreach_remove (priv->pending_connections,
      has_transport, transport);
}

static void
pending_connection_stream_closed_cb (GibberXmppConnection *connection,
                                     gpointer userdata)
{
  SalutXmppConnectionManager *self =
    SALUT_XMPP_CONNECTION_MANAGER (userdata);
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);

  DEBUG ("Pending connection stream closed");
  gibber_xmpp_connection_close (connection);
  gibber_transport_disconnect (connection->transport);
  g_hash_table_remove (priv->pending_connections, connection);
}

static void
connection_parse_error_cb (GibberXmppConnection *conn,
                           gpointer userdata)
{
  DEBUG ("Parse error on xml stream, closing connection");
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

  DEBUG("Handling new connection");

  contacts = salut_contact_manager_find_contacts_by_address (
      priv->contact_manager, addr);
  if (contacts == NULL)
    {
      DEBUG ("Couldn't find a contact for the connection");
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
      found_contact_for_connection (self, connection,
          SALUT_CONTACT (contacts->data));

      contact_list_destroy (contacts);
      return;
    }

  /* We have to wait to know the contact before announce the connection */
  g_hash_table_insert (priv->pending_connections, g_object_ref (connection),
      contacts);

  g_signal_connect (connection, "stream-opened",
      G_CALLBACK (pending_connection_stream_opened_cb), self);
  g_signal_connect (connection, "received-stanza",
      G_CALLBACK (pending_connection_stanza_received_cb), self);
  g_signal_connect (connection->transport, "disconnected",
      G_CALLBACK (pending_connection_transport_disconnected_cb), self);
  g_signal_connect (connection, "stream-closed",
      G_CALLBACK (pending_connection_stream_closed_cb), self);
  g_signal_connect (connection, "parse-error",
      G_CALLBACK (connection_parse_error_cb), self);
}

static GObject *
salut_xmpp_connection_manager_constructor (GType type,
                                           guint n_props,
                                           GObjectConstructParam *props)
{
  GObject *obj;
  SalutXmppConnectionManager *self;
  SalutXmppConnectionManagerPrivate *priv;

  obj = G_OBJECT_CLASS (salut_xmpp_connection_manager_parent_class)->
    constructor (type, n_props, props);

  self = SALUT_XMPP_CONNECTION_MANAGER (obj);
  priv = SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);

  g_assert (priv->connection != NULL);
  g_assert (priv->contact_manager != NULL);

  priv->listener = gibber_xmpp_connection_listener_new ();
  g_signal_connect (priv->listener, "new-connection",
      G_CALLBACK (new_connection_cb), self);

  return obj;
}

void
salut_xmpp_connection_manager_dispose (GObject *object)
{
  SalutXmppConnectionManager *self = SALUT_XMPP_CONNECTION_MANAGER (object);
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

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

  if (priv->pending_connections != NULL)
    {
      g_hash_table_destroy (priv->pending_connections);
      priv->pending_connections = NULL;
    }

  if (priv->stanza_filters != NULL)
    {
      g_hash_table_destroy (priv->stanza_filters);
      priv->stanza_filters = NULL;
    }

  free_stanza_filters_list (priv->all_connection_filters);
  priv->all_connection_filters = NULL;

  priv->dispose_has_run = TRUE;

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

  object_class->constructor = salut_xmpp_connection_manager_constructor;

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
}

SalutXmppConnectionManager *
salut_xmpp_connection_manager_new (SalutConnection *connection,
                                   SalutContactManager *contact_manager)
{
  return g_object_new (
      SALUT_TYPE_XMPP_CONNECTION_MANAGER,
      "connection", connection,
      "contact-manager", contact_manager,
      NULL);
}

int
salut_xmpp_connection_manager_listen (SalutXmppConnectionManager *self)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
  int port;

  for (port = 5298; port < 5400; port++)
    {
      GError *error = NULL;
      if (gibber_xmpp_connection_listener_listen (priv->listener, port,
            &error))
        break;

      if (error->code != GIBBER_XMPP_CONNECTION_LISTENER_ERROR_ADDR_IN_USE)
        {
          g_error_free (error);
          return -1;
        }

      g_error_free (error);
      error = NULL;
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

GibberXmppConnection *
salut_xmpp_connection_get_connection (SalutXmppConnectionManager *self,
                                      SalutContact *contact)
{
  SalutXmppConnectionManagerPrivate *priv =
    SALUT_XMPP_CONNECTION_MANAGER_GET_PRIVATE (self);
  GibberXmppConnection *connection;
  GibberLLTransport *transport;
  struct find_connection_for_contact_data data;
  GArray *addrs;
  gint i;

  data.contact = contact;
  data.connection = NULL;
  g_hash_table_find (priv->connections, find_connection_for_contact, &data);

  if (data.connection != NULL)
    {
      DEBUG ("found existing connection with %s", contact->name);
      return data.connection;
    }

  /* XXX what should we do if there is an existing pending connection with
   * the contact ? */
  DEBUG ("create a new connection");
  transport = gibber_ll_transport_new ();
  connection = gibber_xmpp_connection_new (GIBBER_TRANSPORT (transport));
  /* Let the xmpp connection own the transport */
  g_object_unref (transport);

  addrs = salut_contact_get_addresses (contact);

  for (i = 0; i < addrs->len; i++)
    {
      salut_contact_address_t *addr;

      addr = &(g_array_index (addrs, salut_contact_address_t, i));

      if (gibber_ll_transport_open_sockaddr (transport, &(addr->address),
            NULL))
        {
          g_array_free (addrs, TRUE);
          /* XXX should we really open at this stage or
           * let the caller do it? */
          gibber_xmpp_connection_open (connection, contact->name,
              priv->connection->name, "1.0");

          found_contact_for_connection (self, connection, contact);
          return connection;
        }
    }

  DEBUG ("All connection attempts failed");
  g_array_free (addrs, TRUE);
  g_object_unref (connection);

  return NULL;
}

GSList *
find_filter (GSList *list,
             SalutXmppConnectionManagerStanzaFilterFunc filter_func,
             SalutXmppConnectionManagerStanzaCallbackFunc callback,
             gpointer user_data)
{
  GSList *l;

  for (l = list; l != NULL; l = l->next)
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

  g_slice_free (StanzaFilter, l->data);
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

  filter = g_slice_new (StanzaFilter);
  filter->filter_func = filter_func;
  filter->callback = callback;
  filter->user_data = user_data;

  if (conn == NULL)
    {
      if (find_filter (priv->all_connection_filters, filter_func, callback,
            user_data) != NULL)
        {
          /* No need to add twice the same filter */
          g_slice_free (StanzaFilter, filter);
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
          g_slice_free (StanzaFilter, filter);
          return FALSE;
        }

      list = g_hash_table_lookup (priv->stanza_filters, conn);
      if (find_filter (list, filter_func, callback, user_data) != NULL)
        {
          /* No need to add twice the same filter */
          g_slice_free (StanzaFilter, filter);
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
