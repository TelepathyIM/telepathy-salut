/*
 * salut-im-channel.c - Source for SalutImChannel
 * Copyright (C) 2005 Collabora Ltd.
 *   @author: Sjoerd Simons <sjoerd@luon.net>
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
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>

#define DEBUG_FLAG DEBUG_IM
#include "debug.h"

#include "salut-im-channel.h"
#include "signals-marshal.h"

#include "salut-connection.h"
#include "salut-contact.h"
#include "text-helper.h"
#include "salut-xmpp-connection-manager.h"

#include <gibber/gibber-linklocal-transport.h>
#include <gibber/gibber-xmpp-connection.h>
#include <gibber/gibber-xmpp-stanza.h>
#include <gibber/gibber-namespaces.h>

#include <telepathy-glib/text-mixin.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/dbus.h>

static void
channel_iface_init (gpointer g_iface, gpointer iface_data);
static void
text_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (SalutImChannel, salut_im_channel, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_TEXT, text_iface_init);
);

/* Channel state */
typedef enum
{
  CHANNEL_NOT_CONNECTED = 0,
  CHANNEL_CONNECTING,
  CHANNEL_CONNECTED
} ChannelState;

/* signal enum */
enum
{
  RECEIVED_STANZA,
  CONNECTED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_CONTACT,
  PROP_CONNECTION,
  PROP_XMPP_CONNECTION_MANAGER,
  LAST_PROPERTY
};

/* private structure */
typedef struct _SalutImChannelPrivate SalutImChannelPrivate;

struct _SalutImChannelPrivate
{
  gboolean dispose_has_run;
  gchar *object_path;
  TpHandle handle;
  SalutContact *contact;
  SalutConnection *connection;
  GibberXmppConnection *xmpp_connection;
  SalutXmppConnectionManager *xmpp_connection_manager;
  /* Outcoming and incoming message queues */
  GQueue *out_queue;
  ChannelState state;
};

#define SALUT_IM_CHANNEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE \
    ((o), SALUT_TYPE_IM_CHANNEL, SalutImChannelPrivate))

typedef struct _SalutImChannelMessage SalutImChannelMessage;

struct _SalutImChannelMessage {
  guint time;
  guint type;
  gchar *text;
  GibberXmppStanza *stanza;
};

static void
salut_im_channel_do_close (SalutImChannel *self)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  ChannelState oldstate = priv->state;

  priv->state = CHANNEL_NOT_CONNECTED;

  switch (oldstate)
    {
      case CHANNEL_NOT_CONNECTED:
        /* FIXME return an error ? */
        break;
      case CHANNEL_CONNECTING:
      case CHANNEL_CONNECTED:
        /* FIXME decent connection closing? */
        gibber_xmpp_connection_close (priv->xmpp_connection);
        gibber_transport_disconnect (priv->xmpp_connection->transport);
        break;
    }

  DEBUG ("Emitting closed signal for %s", priv->object_path);
  tp_svc_channel_emit_closed (self);
}

static SalutImChannelMessage *
salut_im_channel_message_new (guint type,
                              const gchar *text,
                              GibberXmppStanza *stanza)
{
  SalutImChannelMessage *msg;
  msg = g_new0 (SalutImChannelMessage, 1);
  msg->type = type;
  msg->text = g_strdup (text);
  msg->time = time (NULL);
  msg->stanza = stanza;
  if (stanza != NULL)
    g_object_ref (G_OBJECT (stanza));

  return msg;
}

static SalutImChannelMessage *
salut_im_channel_message_new_from_stanza (GibberXmppStanza *stanza)
{
  SalutImChannelMessage *msg;
  g_assert (stanza != NULL);

  msg = g_new (SalutImChannelMessage, 1);
  msg->type = 0;
  msg->text = NULL;
  msg->time = 0;

  g_object_ref (stanza);
  msg->stanza = stanza;

  return msg;
}

static void
salut_im_channel_message_free (SalutImChannelMessage *message)
{
  g_free (message->text);
  if (message->stanza)
    g_object_unref (message->stanza);

  g_free (message);
}

static gboolean
_send_message (SalutImChannel *self, guint type, const gchar *text,
    GibberXmppStanza *stanza, GError **error);

static void
salut_im_channel_init (SalutImChannel *obj)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (obj);
  /* allocate any data required by the object here */
  priv->object_path = NULL;
  priv->contact = NULL;
  priv->xmpp_connection = NULL;
  priv->out_queue = g_queue_new ();
  priv->state = CHANNEL_NOT_CONNECTED;
  priv->xmpp_connection_manager = NULL;
}


static void
salut_im_channel_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  SalutImChannel *chan = SALUT_IM_CHANNEL (object);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (chan);

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_value_set_string (value, priv->object_path);
        break;
      case PROP_CHANNEL_TYPE:
        g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_TEXT);
        break;
      case PROP_HANDLE_TYPE:
        g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
        break;
      case PROP_HANDLE:
        g_value_set_uint (value, priv->handle);
        break;
      case PROP_CONTACT:
        g_value_set_object (value, priv->contact);
        break;
      case PROP_CONNECTION:
        g_value_set_object (value, priv->connection);
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
salut_im_channel_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  SalutImChannel *chan = SALUT_IM_CHANNEL (object);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (chan);
  const gchar *tmp;

  switch (property_id)
    {
      case PROP_OBJECT_PATH:
        g_free (priv->object_path);
        priv->object_path = g_value_dup_string (value);
        break;
      case PROP_HANDLE:
        priv->handle = g_value_get_uint (value);
        break;
      case PROP_CONTACT:
        priv->contact = g_value_get_object (value);
        g_object_ref (priv->contact);
        break;
      case PROP_CONNECTION:
        priv->connection = g_value_get_object (value);
        break;
      case PROP_HANDLE_TYPE:
        g_assert (g_value_get_uint (value) == 0 ||
            g_value_get_uint (value) == TP_HANDLE_TYPE_CONTACT);
        break;
      case PROP_CHANNEL_TYPE:
        tmp = g_value_get_string (value);
        g_assert (tmp == NULL || !tp_strdiff (g_value_get_string (value),
              TP_IFACE_CHANNEL_TYPE_TEXT));
        break;
      case PROP_XMPP_CONNECTION_MANAGER:
        priv->xmpp_connection_manager = g_value_get_object (value);
        g_object_ref (priv->xmpp_connection_manager);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}


static GObject *
salut_im_channel_constructor (GType type,
                              guint n_props,
                              GObjectConstructParam *props)
{
  GObject *obj;
  DBusGConnection *bus;
  SalutImChannelPrivate *priv;
  TpBaseConnection *base_conn;
  TpHandleRepoIface *contact_repo;

  /* Parent constructor chain */
  obj = G_OBJECT_CLASS (salut_im_channel_parent_class)->
        constructor (type, n_props, props);

  priv = SALUT_IM_CHANNEL_GET_PRIVATE (SALUT_IM_CHANNEL (obj));

  /* Ref our handle */
  base_conn = TP_BASE_CONNECTION (priv->connection);

  contact_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);

  tp_handle_ref (contact_repo, priv->handle);

  /* Initialize text mixin */
  tp_text_mixin_init (obj, G_STRUCT_OFFSET (SalutImChannel, text),
      contact_repo);

  tp_text_mixin_set_message_types (obj, TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION, G_MAXUINT);

  /* Connect to the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  return obj;
}

static void salut_im_channel_dispose (GObject *object);
static void salut_im_channel_finalize (GObject *object);

static void
salut_im_channel_class_init (SalutImChannelClass *salut_im_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_im_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_im_channel_class,
      sizeof (SalutImChannelPrivate));

  object_class->dispose = salut_im_channel_dispose;
  object_class->finalize = salut_im_channel_finalize;

  object_class->constructor = salut_im_channel_constructor;
  object_class->get_property = salut_im_channel_get_property;
  object_class->set_property = salut_im_channel_set_property;

  g_object_class_override_property (object_class, PROP_OBJECT_PATH,
      "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
      "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
      "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");

  param_spec = g_param_spec_object (
      "contact",
      "SalutContact object",
      "Salut Contact to which this channel is dedicated",
      SALUT_TYPE_CONTACT,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONTACT, param_spec);

  param_spec = g_param_spec_object (
      "connection",
      "SalutConnection object",
      "Salut Connection that owns the connection for this IM channel",
      SALUT_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_object (
      "xmpp-connection-manager",
      "SalutXmppConnectionManager object",
      "Salut XMPP Connection manager used for this IM channel",
      SALUT_TYPE_XMPP_CONNECTION_MANAGER,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_XMPP_CONNECTION_MANAGER,
      param_spec);

  signals[RECEIVED_STANZA] =
    g_signal_new (
        "received-stanza",
        G_OBJECT_CLASS_TYPE (salut_im_channel_class),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
        0,
        g_signal_accumulator_true_handled, NULL,
        salut_signals_marshal_BOOLEAN__OBJECT,
        G_TYPE_BOOLEAN, 1, GIBBER_TYPE_XMPP_STANZA);

  signals[CONNECTED] =
    g_signal_new (
        "connected",
        G_OBJECT_CLASS_TYPE (salut_im_channel_class),
        G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
        0,
        NULL, NULL,
        g_cclosure_marshal_VOID__VOID,
        G_TYPE_NONE, 0);

  tp_text_mixin_class_init (object_class,
      G_STRUCT_OFFSET (SalutImChannelClass, text_class));
}

void
salut_im_channel_dispose (GObject *object)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (object);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->connection);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (base_conn,
        TP_HANDLE_TYPE_CONTACT);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  tp_handle_unref (handle_repo, priv->handle);

  if (priv->state != CHANNEL_NOT_CONNECTED)
    salut_im_channel_do_close (self);

  if (priv->xmpp_connection)
    {
      g_object_unref (priv->xmpp_connection);
      priv->xmpp_connection = NULL;
    }

  g_object_unref (priv->contact);
  priv->contact = NULL;

  if (priv->xmpp_connection_manager != NULL)
    {
      g_object_unref (priv->xmpp_connection_manager);
      priv->xmpp_connection_manager = NULL;
    }

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_im_channel_parent_class)->dispose)
    G_OBJECT_CLASS (salut_im_channel_parent_class)->dispose (object);
}

static void
salut_im_channel_finalize (GObject *object)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (object);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  g_free (priv->object_path);

  g_queue_foreach (priv->out_queue, (GFunc) salut_im_channel_message_free,
      NULL);
  g_queue_free (priv->out_queue);

  tp_text_mixin_finalize (G_OBJECT (self));

  G_OBJECT_CLASS (salut_im_channel_parent_class)->finalize (object);
}

static void
_sendout_message (SalutImChannel *self,
                  guint timestamp,
                  guint type,
                  const gchar *text,
                  GibberXmppStanza *stanza)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  if (gibber_xmpp_connection_send (priv->xmpp_connection, stanza, NULL))
    tp_svc_channel_type_text_emit_sent (self, timestamp, type, text);
  else
    tp_svc_channel_type_text_emit_send_error (self,
        TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN, timestamp, type, text);
}

static void
_flush_queue (SalutImChannel *self)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  SalutImChannelMessage *msg;
  /*Connected!, flusch the queue ! */
  while ((msg = g_queue_pop_head (priv->out_queue)) != NULL)
    {
      if (msg->text == NULL)
        {
          if (!gibber_xmpp_connection_send (priv->xmpp_connection,
                msg->stanza, NULL))
            g_warning ("Sending message failed");
        }
      else
        {
          _sendout_message (self, msg->time, msg->type, msg->text,
              msg->stanza);
        }

      salut_im_channel_message_free (msg);
    }
}

static void
_error_flush_queue (SalutImChannel *self) {
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  SalutImChannelMessage *msg;
  /*Connection failed!, flusch the queue ! */
  while ((msg = g_queue_pop_head (priv->out_queue)) != NULL)
    {
      DEBUG ("Sending out SendError for msg: %s", msg->text);
      if (msg->text != NULL)
        tp_svc_channel_type_text_emit_send_error (self,
            TP_CHANNEL_TEXT_SEND_ERROR_OFFLINE, msg->time, msg->type,
            msg->text);

      salut_im_channel_message_free (msg);
    }
}

void
salut_im_channel_received_stanza (SalutImChannel *self,
                                  GibberXmppStanza *stanza)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  gboolean handled = FALSE;
  const gchar *from;
  TpChannelTextMessageType msgtype;
  const gchar *body;
  const gchar *body_offset;

  DEBUG ("Got stanza!");

  g_signal_emit (self, signals[RECEIVED_STANZA], 0, stanza, &handled);
  if (handled)
    /* Some other part handled this message, could be muc invite or voip call
     * or whatever */
    return;

  if (!text_helper_parse_incoming_message (stanza, &from, &msgtype,
        &body, &body_offset))
    {
      DEBUG ("Stanza not a text message, ignoring");
      return;
    }

  if (body == NULL)
    {
      /* No body ? Ignore */
      DEBUG ("Text message without a body");
      return;
    }

  /* FIXME validate the from */
  tp_text_mixin_receive (G_OBJECT (self), msgtype, priv->handle,
      time (NULL), body_offset);
}

static gboolean
message_stanza_filter (SalutXmppConnectionManager *mgr,
                       GibberXmppConnection *conn,
                       GibberXmppStanza *stanza,
                       SalutContact *contact,
                       gpointer user_data)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (user_data);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  GibberStanzaType type;

  if (priv->contact != contact)
    return FALSE;

  gibber_xmpp_stanza_get_type_info (stanza, &type, NULL);
  if (type != GIBBER_STANZA_TYPE_MESSAGE)
    return FALSE;

  return TRUE;
}

static void
message_stanza_callback (SalutXmppConnectionManager *mgr,
                         GibberXmppConnection *conn,
                         GibberXmppStanza *stanza,
                         SalutContact *contact,
                         gpointer user_data)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (user_data);

  salut_im_channel_received_stanza (self, stanza);
}

static void
_connection_stream_opened_cb (GibberXmppConnection *conn,
                              const gchar *to,
                              const gchar *from,
                              const gchar *version,
                              gpointer userdata)
{
  SalutImChannel  *self = SALUT_IM_CHANNEL (userdata);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  /* TODO validate to field ? */
  if (gibber_ll_transport_is_incoming (GIBBER_LL_TRANSPORT (conn->transport)))
    {
      GibberXmppStanza *stanza;
      gibber_xmpp_connection_open (conn, from, priv->connection->name, "1.0");
      /* Send empty stream features */
      stanza = gibber_xmpp_stanza_new ("features");
      gibber_xmpp_node_set_ns (stanza->node, GIBBER_XMPP_NS_STREAM);
      gibber_xmpp_connection_send (conn, stanza, NULL);
      g_object_unref (stanza);
    }
  priv->state = CHANNEL_CONNECTED;
  g_signal_emit (self, signals[CONNECTED], 0);
  _flush_queue (self);
}

static void
_connection_stream_closed_cb (GibberXmppConnection *conn,
                              gpointer userdata)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (userdata);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  if (priv->state == CHANNEL_CONNECTED)
    /* Other side closed the stream, do the same */
    gibber_xmpp_connection_close (conn);

  gibber_transport_disconnect (conn->transport);
  priv->state = CHANNEL_NOT_CONNECTED;
}

static void
_connection_parse_error_cb (GibberXmppConnection *conn,
                            gpointer userdata)
{
  DEBUG ("Parse error, closing connection");
  gibber_transport_disconnect (conn->transport);
}

static void
_trans_disconnected_cb (GibberLLTransport *transport,
                        gpointer userdata)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (userdata);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  /* FIXME cleanup better (should we flush the queue?) */
  DEBUG ("Transport disconnected");
  if (priv->state == CHANNEL_CONNECTING)
    {
      DEBUG ("Disconnected, while still connecting, ignoring");
      return;
    }

  /* Take care not to unref the connection if disposing */
  if (priv->xmpp_connection && !priv->dispose_has_run) {
    g_object_unref (priv->xmpp_connection);
  }
  priv->xmpp_connection = NULL;
  priv->state = CHANNEL_NOT_CONNECTED;

  salut_xmpp_connection_manager_remove_stanza_filter (
      priv->xmpp_connection_manager, priv->xmpp_connection,
      message_stanza_filter, message_stanza_callback, self);
}

static void
_initialise_connection (SalutImChannel *self)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  g_signal_connect (priv->xmpp_connection->transport,
      "disconnected", G_CALLBACK (_trans_disconnected_cb), self);

  g_signal_connect (priv->xmpp_connection, "stream-opened",
      G_CALLBACK (_connection_stream_opened_cb), self);
  g_signal_connect (priv->xmpp_connection, "stream-closed",
      G_CALLBACK (_connection_stream_closed_cb), self);
  g_signal_connect (priv->xmpp_connection, "parse-error",
      G_CALLBACK (_connection_parse_error_cb), self);

  salut_xmpp_connection_manager_add_stanza_filter (
      priv->xmpp_connection_manager, priv->xmpp_connection,
      message_stanza_filter, message_stanza_callback, self);

  /* Sync state with the connection */
  if (priv->xmpp_connection->stream_flags
        == GIBBER_XMPP_CONNECTION_STREAM_FULLY_OPEN)
    {
      priv->state = CHANNEL_CONNECTED;
      g_signal_emit (self, signals[CONNECTED], 0);
      _flush_queue (self);
    }
  else
    {
      priv->state = CHANNEL_CONNECTING;
    }
}

static void
xmpp_connection_manager_new_connection_cb (SalutXmppConnectionManager *mgr,
                                           GibberXmppConnection *conn,
                                           SalutContact *contact,
                                           gpointer user_data)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (user_data);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  g_assert (contact == priv->contact);

  DEBUG ("pending connection fully open");
  g_signal_handlers_disconnect_matched (mgr, G_SIGNAL_MATCH_DATA,
    0, 0, NULL, NULL, self);

  priv->xmpp_connection = conn;
  g_object_ref (priv->xmpp_connection);
  _initialise_connection (self);
}

static void
_setup_connection (SalutImChannel *self)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  SalutXmppConnectionManagerRequestConnectionResult result;
  GibberXmppConnection *conn = NULL;

  result = salut_xmpp_connection_request_connection (
      priv->xmpp_connection_manager, priv->contact, &conn);

  if (result == SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_DONE)
    {
      priv->xmpp_connection = conn;
      g_object_ref (priv->xmpp_connection);
      _initialise_connection (self);
    }
  else if (result ==
      SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_PENDING)
    {
      DEBUG ("Requested connection pending");
      /* XXX set a timer to avoid to be blocked if remote contact never open
       * the connection ? */
      g_signal_connect (priv->xmpp_connection_manager, "new-connection",
          G_CALLBACK (xmpp_connection_manager_new_connection_cb), self);
      return;
    }
  else
    {
      priv->state = CHANNEL_NOT_CONNECTED;
      _error_flush_queue (self);
      return;
    }
}

static void
_send_channel_message (SalutImChannel *self,
                       SalutImChannelMessage *msg)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  switch (priv->state)
    {
      case CHANNEL_NOT_CONNECTED:
        g_queue_push_tail (priv->out_queue, msg);
        _setup_connection (self);
        break;
      case CHANNEL_CONNECTING:
        g_queue_push_tail (priv->out_queue, msg);
        break;
      default:
        g_assert_not_reached ();
        break;
    }
}

static gboolean
_send_message (SalutImChannel *self,
               guint type,
               const gchar *text,
               GibberXmppStanza *stanza,
               GError **error)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  SalutImChannelMessage *msg;

  switch (priv->state)
    {
      case CHANNEL_NOT_CONNECTED:
      case CHANNEL_CONNECTING:
        g_object_ref (stanza);
        msg = salut_im_channel_message_new (type, text, stanza);
        _send_channel_message (self, msg);
        break;
      case CHANNEL_CONNECTED:
        /* Connected and the queue is empty, so push it out directly */
        _sendout_message (self, time (NULL), type, text, stanza);
        break;
    }
  return TRUE;
}

void
salut_im_channel_send_stanza (SalutImChannel * self,
                              GibberXmppStanza *stanza)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  SalutImChannelMessage *msg;

  switch (priv->state)
    {
      case CHANNEL_NOT_CONNECTED:
      case CHANNEL_CONNECTING:
        msg = salut_im_channel_message_new_from_stanza (stanza);
        _send_channel_message (self, msg);
        break;
      case CHANNEL_CONNECTED:
        if (!gibber_xmpp_connection_send (priv->xmpp_connection, stanza, NULL))
          g_warning ("Sending failed");
    }
}

void
salut_im_channel_add_connection (SalutImChannel *chan,
                                 GibberXmppConnection *conn)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (chan);
  /* FIXME if we already have a connection, we throw this one out..
   * Which can be not quite what the other side expects.. And strange things
   * can happen when two * sides try to initiate at the same time */

  if (priv->xmpp_connection == conn)
    /* We requested this connection, no need to add it twice */
    return;

  if (priv->xmpp_connection != NULL)
    {
      DEBUG ("Already had a connection for: %s", priv->contact->name);
      return;
    }
  DEBUG ("New connection for: %s", priv->contact->name);
  priv->xmpp_connection = conn;
  g_object_ref (priv->xmpp_connection);
  _initialise_connection (chan);
}

/**
 * salut_im_channel_close
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_im_channel_close (TpSvcChannel *iface,
                        DBusGMethodInvocation *context)
{
  salut_im_channel_do_close (SALUT_IM_CHANNEL (iface));
  tp_svc_channel_return_from_close (context);
}


/**
 * salut_im_channel_get_channel_type
 *
 * Implements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_im_channel_get_channel_type (TpSvcChannel *iface,
                                   DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      TP_IFACE_CHANNEL_TYPE_TEXT);
}


/**
 * salut_im_channel_get_handle
 *
 * Implements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_im_channel_get_handle (TpSvcChannel *iface,
                             DBusGMethodInvocation *context)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (iface);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_CONTACT,
      priv->handle);
}


/**
 * salut_im_channel_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_im_channel_get_interfaces (TpSvcChannel *iface,
                                 DBusGMethodInvocation *context)
{
  const char *interfaces[] = { NULL };

  tp_svc_channel_return_from_get_interfaces (context, interfaces);
}

static void
channel_iface_init (gpointer g_iface,
                    gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *) g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, salut_im_channel_##x)
  IMPLEMENT(close);
  IMPLEMENT(get_channel_type);
  IMPLEMENT(get_handle);
  IMPLEMENT(get_interfaces);
#undef IMPLEMENT
}

/**
 * salut_im_channel_send
 *
 * Implements DBus method Send
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
salut_im_channel_send (TpSvcChannelTypeText *channel,
                       guint type, const gchar * text,
                       DBusGMethodInvocation *context)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (channel);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  GError *error = NULL;

  GibberXmppStanza *stanza = text_helper_create_message (
      priv->connection->name, priv->contact->name, type, text, &error);

  if (stanza == NULL)
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  if (!_send_message (self, type, text, stanza, &error))
    {
      g_object_unref (G_OBJECT (stanza));
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  g_object_unref (G_OBJECT (stanza));
  tp_svc_channel_type_text_return_from_send (context);
}


static void
text_iface_init (gpointer g_iface,
                 gpointer iface_data)
{
  TpSvcChannelTypeTextClass *klass = (TpSvcChannelTypeTextClass *) g_iface;

  tp_text_mixin_iface_init (g_iface, iface_data);
#define IMPLEMENT(x) tp_svc_channel_type_text_implement_##x (\
    klass, salut_im_channel_##x)
  IMPLEMENT(send);
#undef IMPLEMENT
}
