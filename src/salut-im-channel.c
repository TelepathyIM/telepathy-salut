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
#include "salut-im-channel-signals-marshal.h"
#include "salut-im-channel-glue.h"

#include "salut-linklocal-transport.h"
#include "salut-xmpp-connection.h"
#include "salut-xmpp-stanza.h"

#include "salut-connection.h"
#include "salut-contact.h"

#include "handle-repository.h"
#include "tp-channel-iface.h"
#include "telepathy-helpers.h"
#include "telepathy-interfaces.h"
#include "telepathy-errors.h"

#include "text-mixin.h"

G_DEFINE_TYPE_WITH_CODE(SalutImChannel, salut_im_channel, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(TP_TYPE_CHANNEL_IFACE, NULL));

/* Channel state */
typedef enum {
  CHANNEL_NOT_CONNECTED = 0, 
  CHANNEL_CONNECTING,
  CHANNEL_CONNECTED
} ChannelState;
 

/* signal enum */
enum {
    CLOSED,
    RECEIVED_STANZA,
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
  LAST_PROPERTY
};

/* private structure */
typedef struct _SalutImChannelPrivate SalutImChannelPrivate;

struct _SalutImChannelPrivate
{
  gboolean dispose_has_run;
  gchar *object_path;
  Handle handle;
  SalutContact *contact;
  SalutConnection *connection;
  SalutXmppConnection *xmpp_connection;
  /* Outcoming and incoming message queues */
  GQueue *out_queue;
  ChannelState state;
  /* Hold the address array when connection */
  GArray *addresses;
  gint address_index;
};

#define SALUT_IM_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_IM_CHANNEL, SalutImChannelPrivate))

typedef struct _SalutImChannelMessage SalutImChannelMessage;

struct _SalutImChannelMessage {
  guint time;
  guint type;
  gchar *text;
  SalutXmppStanza *stanza;
};

static SalutImChannelMessage *
salut_im_channel_message_new(guint type, const gchar *text, 
                             SalutXmppStanza *stanza) {
  SalutImChannelMessage *msg;
  msg = g_new0(SalutImChannelMessage, 1);
  msg->type = type;
  msg->text = g_strdup(text);
  msg->time = time(NULL);
  msg->stanza = stanza;
  if (stanza != NULL) {
    g_object_ref(G_OBJECT(stanza));
  }
  return msg;
}

static SalutImChannelMessage *
salut_im_channel_message_new_from_stanza(SalutXmppStanza *stanza) {
  SalutImChannelMessage *msg;
  g_assert(stanza != NULL);

  msg = g_new(SalutImChannelMessage, 1);
  msg->type = 0;
  msg->text = NULL;
  msg->time = 0;

  g_object_ref(stanza);
  msg->stanza = stanza;

  return msg;
}

static void 
salut_im_channel_message_free(SalutImChannelMessage *message) {
  g_free(message->text);
  if (message->stanza) {
    g_object_unref(message->stanza);
  }
  g_free(message);
}

static gboolean _send_message(GObject *object, guint type, const gchar *text, 
                              SalutXmppStanza *stanza, GError **error);
static void
salut_im_channel_init (SalutImChannel *obj)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (obj); 
  /* allocate any data required by the object here */
  priv->object_path = NULL;
  priv->contact = NULL;
  priv->xmpp_connection = NULL;
  priv->out_queue = g_queue_new();
  priv->state = CHANNEL_NOT_CONNECTED;
  priv->addresses = NULL;
  priv->address_index = -1;
}


static void
salut_im_channel_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  SalutImChannel *chan = SALUT_IM_CHANNEL (object);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
salut_im_channel_set_property (GObject     *object,
                                guint        property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  SalutImChannel *chan = SALUT_IM_CHANNEL (object);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_HANDLE:
      priv->handle = g_value_get_uint (value);
      break;
    case PROP_CONTACT:
      priv->contact = g_value_get_object (value);
      g_object_ref(priv->contact);
      break;
    case PROP_CONNECTION:
      priv->connection = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


static GObject *
salut_im_channel_constructor (GType type, guint n_props,
                              GObjectConstructParam *props) {
  GObject *obj;
  DBusGConnection *bus;
  gboolean valid;
  SalutImChannelPrivate *priv;

  /* Parent constructor chain */
  obj = G_OBJECT_CLASS(salut_im_channel_parent_class)->
        constructor(type, n_props, props);

  priv = SALUT_IM_CHANNEL_GET_PRIVATE (SALUT_IM_CHANNEL (obj));

  /* Ref our handle */
  valid = handle_ref(priv->connection->handle_repo, TP_HANDLE_TYPE_CONTACT, 
                     priv->handle);
  g_assert(valid);

  /* Initialize text mixin */
  text_mixin_init(obj, G_STRUCT_OFFSET(SalutImChannel, text), 
                  priv->connection->handle_repo); 

  text_mixin_set_message_types(obj, 
                               TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
                               TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION,
                               G_MAXUINT);

  /* Connect to the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object(bus, priv->object_path, obj);

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

  param_spec = g_param_spec_object ("contact", 
                                    "SalutContact object",
                                    "Salut Contact to which this channel"
                                    "is dedicated", 
                                    SALUT_TYPE_CONTACT,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONTACT, param_spec);

  param_spec = g_param_spec_object ("connection", 
                                    "SalutConnection object",
                                    "Salut Connection that owns the"
                                    "connection for this IM channel",
                                    SALUT_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, 
                                   PROP_CONNECTION, param_spec);
  signals[RECEIVED_STANZA] =
    g_signal_new ("received-stanza",
                  G_OBJECT_CLASS_TYPE (salut_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  g_signal_accumulator_true_handled, NULL,
                  salut_im_channel_marshal_BOOLEAN__OBJECT,
                  G_TYPE_BOOLEAN, 1, SALUT_TYPE_XMPP_STANZA);


  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (salut_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_im_channel_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  text_mixin_class_init(object_class, 
                        G_STRUCT_OFFSET(SalutImChannelClass, text_class), 
                        _send_message);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (salut_im_channel_class), &dbus_glib_salut_im_channel_object_info);
}

void
salut_im_channel_dispose (GObject *object)
{
  SalutImChannel *self = SALUT_IM_CHANNEL (object);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);


  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->state != CHANNEL_NOT_CONNECTED) {
    salut_im_channel_close(self, NULL);
  }

  if (priv->xmpp_connection) {
    g_object_unref(priv->xmpp_connection);
    priv->xmpp_connection = NULL;
  }

  handle_unref(priv->connection->handle_repo, TP_HANDLE_TYPE_CONTACT, 
               priv->handle);
  g_object_unref(priv->contact);
  priv->contact = NULL;

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
  g_free(priv->object_path);

  g_queue_foreach(priv->out_queue, (GFunc)salut_im_channel_message_free, NULL);
  g_queue_free(priv->out_queue);

  G_OBJECT_CLASS (salut_im_channel_parent_class)->finalize (object);
}

static void
_sendout_message(SalutImChannel * self, guint timestamp,
                 guint type,  const gchar *text, SalutXmppStanza *stanza) {
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  
  if (salut_xmpp_connection_send(priv->xmpp_connection, stanza, NULL)) {
    text_mixin_emit_sent(G_OBJECT(self), timestamp, type, text);
  } else  {
    text_mixin_emit_send_error(G_OBJECT(self), 
                               CHANNEL_TEXT_SEND_ERROR_UNKNOWN, 
                               timestamp, type, text);
  }
}

static void
_flush_queue(SalutImChannel *self) {
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  SalutImChannelMessage *msg;
  /*Connected!, flusch the queue ! */
  while ((msg = g_queue_pop_head(priv->out_queue)) != NULL) {
    if (msg->text == NULL) {
      if (!salut_xmpp_connection_send(priv->xmpp_connection, 
                                      msg->stanza, NULL)) {
        g_warning("Sending message failed");
      }
    } else {
      _sendout_message(self, msg->time, msg->type, msg->text, msg->stanza);
    }
    salut_im_channel_message_free(msg);
  }
}

static void
_error_flush_queue(SalutImChannel *self) {
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  SalutImChannelMessage *msg;
  /*Connection failed!, flusch the queue ! */
  while ((msg = g_queue_pop_head(priv->out_queue)) != NULL) {
    DEBUG("Sending out SendError for msg: %s", msg->text);
    if (msg->text != NULL) {
      text_mixin_emit_send_error(G_OBJECT(self), 
                               CHANNEL_TEXT_SEND_ERROR_OFFLINE, 
                               msg->time, msg->type, msg->text);
    }
    salut_im_channel_message_free(msg);
  }
}

void
salut_im_channel_received_stanza(SalutImChannel *self, 
                                     SalutXmppStanza *stanza) {
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  gboolean handled = FALSE;
  const gchar *from;
  TpChannelTextMessageType msgtype;
  const gchar *body;
  const gchar *body_offset;

  DEBUG("Got stanza!"); 

  g_signal_emit(self, signals[RECEIVED_STANZA], 0, stanza, &handled);
  if (handled) {
    /* Some other part handled this message, could be muc invite or voip call
     * or whatever */
    return;
  }
  if (!text_mixin_parse_incoming_message(stanza, &from, &msgtype, 
                                         &body, &body_offset))  {
    DEBUG("Stanza not a text message, ignoring"); 
    return;
  }

  if (body == NULL) {
    /* No body ? Ignore */
    DEBUG("Text message without a body"); 
    return;
  }

  /* FIXME validate the from */
  text_mixin_receive(G_OBJECT(self), msgtype, priv->handle, 
                     time(NULL), body_offset);
}

static void
_connection_got_stanza_cb(SalutXmppConnection *conn, 
                          SalutXmppStanza *stanza, gpointer userdata) {
  /* TODO verify the sender */
  SalutImChannel  *self = SALUT_IM_CHANNEL(userdata);
  salut_im_channel_received_stanza(self, stanza);
}

static void
_connect_to_next(SalutImChannel *self, SalutLLTransport *transport) {
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  if (priv->addresses->len <= priv->address_index) {
    /* Failure */
    /* FIXME signal this up, probably sendError all queued outgoing stuff */
    g_array_free(priv->addresses, TRUE);
    g_object_unref(priv->xmpp_connection);
    priv->xmpp_connection = NULL;
    priv->state = CHANNEL_NOT_CONNECTED;
    DEBUG("All connection attempts failed");
    _error_flush_queue(self);
  } else {
    salut_contact_address_t *addr;
    addr = &g_array_index(priv->addresses, salut_contact_address_t, 
                          priv->address_index);
    if (!salut_ll_transport_open_sockaddr(transport, &(addr->address), NULL)) {
      priv->address_index += 1;
      _connect_to_next(self, transport);
    } else {
      g_array_free(priv->addresses, TRUE);
      priv->addresses = NULL;
      salut_xmpp_connection_open(priv->xmpp_connection, NULL, NULL, NULL);
    }
  }
}

static void
_connection_stream_opened_cb(SalutXmppConnection *conn, 
                             const gchar *to, const gchar *from,
                             const gchar *version,
                             gpointer userdata) {
  SalutImChannel  *self = SALUT_IM_CHANNEL(userdata);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  if (salut_ll_transport_is_incoming(SALUT_LL_TRANSPORT(conn->transport))) {
    salut_xmpp_connection_open(conn, NULL, NULL, NULL);
  }
  priv->state = CHANNEL_CONNECTED;
  _flush_queue(self);
}

static void
_connection_stream_closed_cb(SalutXmppConnection *conn, gpointer userdata) {
  SalutImChannel  *self = SALUT_IM_CHANNEL(userdata);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  if (priv->state == CHANNEL_CONNECTED) {
    /* Other side closed the stream, do the same */
    salut_xmpp_connection_close(conn);
  }
  salut_transport_disconnect(conn->transport);
  priv->state = CHANNEL_NOT_CONNECTED;
}

static void
_connection_parse_error_cb(SalutXmppConnection *conn, gpointer userdata) {
  DEBUG("Parse error, closing connection");
  salut_transport_disconnect(conn->transport);
}

static void
_trans_disconnected_cb(SalutLLTransport *transport, gpointer userdata) {
  SalutImChannel *self = SALUT_IM_CHANNEL(userdata);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  /* FIXME cleanup better (should we flush the queue?) */
  DEBUG("Transport disconnected");
  /* Take care not to unref the connection if disposing */
  if (priv->xmpp_connection && !priv->dispose_has_run) {
    g_object_unref(priv->xmpp_connection);
  }
  priv->xmpp_connection = NULL;
  priv->state = CHANNEL_NOT_CONNECTED;
}

static void
_initialise_connection(SalutImChannel *self) {
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  g_signal_connect(priv->xmpp_connection->transport, 
                   "disconnected",
                   G_CALLBACK(_trans_disconnected_cb), self);

  g_signal_connect(priv->xmpp_connection, "stream-opened",
                   G_CALLBACK(_connection_stream_opened_cb), self);
  g_signal_connect(priv->xmpp_connection, "received-stanza",
                   G_CALLBACK(_connection_got_stanza_cb), self);
  g_signal_connect(priv->xmpp_connection, "stream-closed",
                   G_CALLBACK(_connection_stream_closed_cb), self);
  g_signal_connect(priv->xmpp_connection, "parse-error",
                   G_CALLBACK(_connection_parse_error_cb), self);

  /* Sync state with the connection */
  if (priv->xmpp_connection->stream_open) { 
    priv->state = CHANNEL_CONNECTED;
    _flush_queue(self);
  } else {
    priv->state = CHANNEL_CONNECTING;
  }
}

static void
_setup_connection(SalutImChannel *self) {
  /* FIXME do a non-blocking connect */
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  GArray *addrs;
  SalutLLTransport *transport;

  DEBUG("Setting up the xmpp connection...");
  if (priv->xmpp_connection == NULL) {
    transport = salut_ll_transport_new();
    priv->xmpp_connection = 
      salut_xmpp_connection_new(SALUT_TRANSPORT(transport));
    /* Let the xmpp connection own the transport */
    g_object_unref(transport);
    _initialise_connection(self);
  } else {
    transport = SALUT_LL_TRANSPORT(priv->xmpp_connection->transport);
  }

  priv->state = CHANNEL_CONNECTING;
  
  addrs = salut_contact_get_addresses(priv->contact);

  priv->addresses = addrs;
  priv->address_index = 0;

  _connect_to_next(self, transport);
}

static void
_send_channel_message(SalutImChannel *self, SalutImChannelMessage *msg) {
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  switch (priv->state) {
    case CHANNEL_NOT_CONNECTED:
      g_queue_push_tail(priv->out_queue, msg);
      _setup_connection(self);
      break;
    case CHANNEL_CONNECTING:
      g_queue_push_tail(priv->out_queue, msg);
      break;
    default:
      g_assert_not_reached();
      break;
  }
}

static gboolean
_send_message(GObject *object, guint type, const gchar *text, 
              SalutXmppStanza *stanza, GError **error) {
  SalutImChannel *self = SALUT_IM_CHANNEL(object);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  SalutImChannelMessage *msg;

  switch (priv->state) {
    case CHANNEL_NOT_CONNECTED:
    case CHANNEL_CONNECTING:
      g_object_ref(stanza);
      msg = salut_im_channel_message_new(type, text, stanza);
      _send_channel_message(self, msg);
      break;
    case CHANNEL_CONNECTED:
      /* Connected and the queue is empty, so push it out directly */
      _sendout_message(self, time(NULL), type, text, stanza);
      break;
  }
  return TRUE;
}

void
salut_im_channel_send_stanza(SalutImChannel * self, SalutXmppStanza *stanza) {
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  SalutImChannelMessage *msg;

  switch (priv->state) {
    case CHANNEL_NOT_CONNECTED:
    case CHANNEL_CONNECTING:
      msg = salut_im_channel_message_new_from_stanza(stanza);
      _send_channel_message(self, msg);
      break;
    case CHANNEL_CONNECTED:
      if (!salut_xmpp_connection_send(priv->xmpp_connection, stanza, NULL)) {
        g_warning("Sending failed");
      }
      g_object_unref(stanza);
  }
}

void
salut_im_channel_add_connection(SalutImChannel *chan, 
                                SalutXmppConnection *conn) {
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (chan); 
  /* FIXME if we already have a connection, we throw this one out..
   * Which can be not quite what the other side expects.. And strange things
   * can happen when two * sides try to initiate at the same time */
  if (priv->xmpp_connection != NULL) {
    DEBUG("Already had a connection for: %s", priv->contact->name);
    return;
  }
  DEBUG("New connection for: %s", priv->contact->name);
  g_object_ref(conn);
  priv->xmpp_connection = conn;
  _initialise_connection(chan);
}

/**
 * salut_im_channel_acknowledge_pending_messages
 *
 * Implements DBus method AcknowledgePendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean 
salut_im_channel_acknowledge_pending_messages (SalutImChannel *self, 
                                               const GArray * ids, 
                                               GError **error) { 
  return text_mixin_acknowledge_pending_messages(G_OBJECT(self), ids, error);
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
gboolean 
salut_im_channel_close (SalutImChannel *self, GError **error) {
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self); 
  ChannelState oldstate = priv->state;

  priv->state = CHANNEL_NOT_CONNECTED;

  switch (oldstate) {
    case CHANNEL_NOT_CONNECTED:
      /* FIXME return an error ? */
      break;
    case CHANNEL_CONNECTING:
    case CHANNEL_CONNECTED:
      /* FIXME decent connection closing? */
      salut_xmpp_connection_close(priv->xmpp_connection);
      break;
  }

  DEBUG("Emitting closed signal for %s", priv->object_path);
  g_signal_emit(self, signals[CLOSED], 0);
  return TRUE;
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
gboolean 
salut_im_channel_get_channel_type (SalutImChannel *obj, gchar ** ret, 
                                   GError **error) {
  *ret = g_strdup(TP_IFACE_CHANNEL_TYPE_TEXT);

  return TRUE;
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
gboolean 
salut_im_channel_get_handle (SalutImChannel *obj, guint* ret, guint* ret1, 
                             GError **error) {
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (obj); 

  *ret = TP_HANDLE_TYPE_CONTACT;
  *ret1 = priv->handle;

  return TRUE;
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
gboolean 
salut_im_channel_get_interfaces (SalutImChannel *obj, gchar *** ret, 
                                 GError **error) {
  const char *interfaces[] = { NULL };

  *ret =  g_strdupv ((gchar **) interfaces);

  return TRUE;
}


/**
 * salut_im_channel_get_message_types
 *
 * Implements DBus method GetMessageTypes
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean 
salut_im_channel_get_message_types (SalutImChannel *obj, GArray ** ret, 
                                    GError **error) {
  return text_mixin_get_message_types(G_OBJECT(obj), ret, error);
}


/**
 * salut_im_channel_list_pending_messages
 *
 * Implements DBus method ListPendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean 
salut_im_channel_list_pending_messages (SalutImChannel *self, gboolean clear, 
                                        GPtrArray ** ret, GError **error) {
  return text_mixin_list_pending_messages(G_OBJECT(self), clear, ret, error);
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
gboolean 
salut_im_channel_send (SalutImChannel *self, 
                       guint type, const gchar * text, GError **error) {
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE(self);
  text_mixin_send(G_OBJECT(self), type, priv->connection->name, 
                  priv->contact->name, text, error);
  return TRUE;
}
