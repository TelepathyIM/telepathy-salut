/*
 * salut-im-channel.c - Source for SalutImChannel
 * Copyright (C) 2005 Collabora Ltd.
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

#include <loudmouth/loudmouth.h>

#define DEBUG_FLAG DEBUG_IM
#include "debug.h"

#include "salut-im-channel.h"
#include "salut-im-channel-signals-marshal.h"
#include "salut-im-channel-glue.h"

#include "salut-lm-connection.h"

#include "salut-connection.h"
#include "salut-contact.h"

#include "handle-repository.h"
#include "tp-channel-iface.h"
#include "telepathy-helpers.h"
#include "telepathy-interfaces.h"
#include "telepathy-errors.h"

#include "text-mixin.h"

#define A_ARRAY "__salut_im_channel_address_array__"
#define A_INDEX "__salut_im_channel_address_index__"

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
  SalutLmConnection *lm_connection;
  /* Outcoming and incoming message queues */
  GQueue *out_queue;
  ChannelState state;
};

#define SALUT_IM_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_IM_CHANNEL, SalutImChannelPrivate))

typedef struct _SalutImChannelMessage SalutImChannelMessage;

struct _SalutImChannelMessage {
  guint time;
  guint type;
  gchar *text;
  LmMessage *message;
};

static SalutImChannelMessage *
salut_im_channel_message_new(guint type, const gchar *text, 
                             LmMessage *message) {
  SalutImChannelMessage *msg;
  msg = g_new0(SalutImChannelMessage, 1);
  msg->type = type;
  msg->text = g_strdup(text);
  msg->time = time(NULL);
  msg->message = message;
  return msg;
}

static SalutImChannelMessage *
salut_im_channel_message_new_from_message(LmMessage *message) {
  SalutImChannelMessage *msg;
  g_assert(message != NULL);

  msg = g_new(SalutImChannelMessage, 1);
  msg->type = 0;
  msg->text = NULL;
  msg->time = 0;

  lm_message_ref(message);
  msg->message = message;

  return msg;
}

static void 
salut_im_channel_message_free(SalutImChannelMessage *message) {
  g_free(message->text);
  if (message->message) {
    lm_message_unref(message->message);
  }
  g_free(message);
}

static gboolean _send_message(GObject *object, guint type, const gchar *text, 
                              LmMessage *message, GError **error);
static void
salut_im_channel_init (SalutImChannel *obj)
{
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (obj); 
  /* allocate any data required by the object here */
  priv->object_path = NULL;
  priv->contact = NULL;
  priv->lm_connection = NULL;
  priv->out_queue = g_queue_new();
  priv->state = CHANNEL_NOT_CONNECTED;
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

  if (priv->lm_connection) {
    g_object_unref(priv->lm_connection);
    priv->lm_connection = NULL;
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
                 guint type,  const gchar *text, LmMessage *message) {
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  
  if (salut_lm_connection_send(priv->lm_connection, message, NULL)) {
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
      if (!salut_lm_connection_send(priv->lm_connection, msg->message, NULL)) {
        g_warning("Sending message failed");
      }
    } else {
      _sendout_message(self, msg->time, msg->type, msg->text, msg->message);
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

static void
_connection_got_message_message(SalutImChannel *self, LmMessage *message) {
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  gboolean handled = FALSE;
  const gchar *from;
  TpChannelTextMessageType msgtype;
  const gchar *body;
  const gchar *body_offset;

  if (handled) {
    /* Some other part handled this message, could be muc invite or voip call
     * or whatever */
    return;
  }
  printf("Not handled by anything else\n");

  if (!text_mixin_parse_incoming_message(message, &from, &msgtype, 
                                         &body, &body_offset))  {
    return;
  }

  if (body == NULL) {
    /* No body ? Ignore */
    return;
  }

  /* FIXME validate the from */
  text_mixin_receive(G_OBJECT(self), msgtype, priv->handle, 
                     time(NULL), body_offset);
}

static void
_connection_got_message_message_cb(SalutLmConnection *conn, 
                                LmMessage *message, gpointer userdata) {
  /* TODO verify the sender */
  SalutImChannel  *self = SALUT_IM_CHANNEL(userdata);
  _connection_got_message_message(self, message);
}

static void
_connection_got_message_cb(SalutLmConnection *conn, 
                        LmMessage *message, gpointer userdata) {
  salut_lm_connection_ack(conn, message);
}

static void
_connect_to_next(SalutImChannel *self, SalutLmConnection *conn) {
  GArray *addrs;
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  int i;

  addrs = g_object_get_data(G_OBJECT(conn), A_ARRAY);
  i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(conn), A_INDEX)); 

  if (addrs->len <= i) {
    /* Failure */
    /* FIXME signal this up, probably sendError all queued outgoing stuff */
    g_array_free(addrs, TRUE);
    g_object_set_data(G_OBJECT(conn), A_ARRAY, NULL);
    g_object_unref(priv->lm_connection);
    priv->lm_connection = NULL;
    priv->state = CHANNEL_NOT_CONNECTED;
    DEBUG("All connection attempts failed");
    _error_flush_queue(self);
  } else {
    salut_contact_address_t *addr;
    addr = &g_array_index(addrs, salut_contact_address_t, i);
    g_object_set_data(G_OBJECT(conn), A_INDEX, GINT_TO_POINTER(++i));
    if (!salut_lm_connection_open_sockaddr(conn, &(addr->address), NULL)) {
      _connect_to_next(self, conn);
    }
  }
}

static void
_connection_connected_cb(SalutLmConnection *conn, gint state, 
                         gpointer userdata) {
  SalutImChannel  *self = SALUT_IM_CHANNEL(userdata);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  GArray *addrs;
  
  addrs = g_object_get_data(G_OBJECT(conn), A_ARRAY);
  if (addrs) {
    g_array_free(addrs, TRUE);
    g_object_set_data(G_OBJECT(conn), A_ARRAY, NULL);
  }

  priv->state = SALUT_LM_CONNECTED;
  _flush_queue(self);
}

static void
_connection_disconnected_cb(SalutLmConnection *conn, gint state, 
                            gpointer userdata) {
  SalutImChannel  *self = SALUT_IM_CHANNEL(userdata);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  if (!salut_lm_connection_is_incoming(conn) && 
       priv->state == CHANNEL_CONNECTING) {
    _connect_to_next(self, conn); 
  } else  {
    /* FIXME cleanup */
    g_object_unref(priv->lm_connection);
    priv->lm_connection = NULL;
    priv->state = CHANNEL_NOT_CONNECTED;
  }
}

static void
_initialise_connection(SalutImChannel *self) {
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  g_signal_connect(priv->lm_connection, "state_changed::disconnected",
                   G_CALLBACK(_connection_disconnected_cb), self);
  g_signal_connect(priv->lm_connection, "state_changed::connected",
                   G_CALLBACK(_connection_connected_cb), self);
  g_signal_connect(priv->lm_connection, "message_received::message",
                   G_CALLBACK(_connection_got_message_message_cb), self);
  g_signal_connect(priv->lm_connection, "message_received",
                   G_CALLBACK(_connection_got_message_cb), self);
  /* Sync state with the connection */
  if (priv->lm_connection->state == SALUT_LM_CONNECTING) {
    priv->state = CHANNEL_CONNECTING;
  } else if (priv->lm_connection->state == SALUT_LM_CONNECTED) {
    priv->state = CHANNEL_CONNECTED;
    LmMessage *message;
    _flush_queue(self);
    while ((message = salut_lm_connection_pop(priv->lm_connection)) != NULL) {
      if (lm_message_get_type(message) == LM_MESSAGE_TYPE_MESSAGE)
        _connection_got_message_message(self, message);
      lm_message_unref(message);
    }
  } else {
    g_assert(priv->state == CHANNEL_NOT_CONNECTED);
  }
}

static void
_setup_connection(SalutImChannel *self) {
  /* FIXME do a non-blocking connect */
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  GArray *addrs;

  DEBUG("Setting up the lm connection...");
  if (priv->lm_connection == NULL) {
    priv->lm_connection = salut_lm_connection_new();
    _initialise_connection(self);
  }

  g_assert(priv->lm_connection->state == SALUT_LM_DISCONNECTED);
  priv->state = CHANNEL_CONNECTING;
  
  addrs = salut_contact_get_addresses(priv->contact);

  /* FIXME 
   * Add this with _full so the array is destroyed on object finalisation */
  g_object_set_data(G_OBJECT(priv->lm_connection), A_ARRAY, addrs);
  g_object_set_data(G_OBJECT(priv->lm_connection), A_INDEX, GINT_TO_POINTER(0));

  _connect_to_next(self, priv->lm_connection);
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
              LmMessage *message, GError **error) {
  SalutImChannel *self = SALUT_IM_CHANNEL(object);
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  SalutImChannelMessage *msg;

  switch (priv->state) {
    case CHANNEL_NOT_CONNECTED:
    case CHANNEL_CONNECTING:
      lm_message_ref(message);
      msg = salut_im_channel_message_new(type, text, message);
      _send_channel_message(self, msg);
      break;
    case CHANNEL_CONNECTED:
      /* Connected and the queue is empty, so push it out directly */
      _sendout_message(self, time(NULL), type, text, message);
      break;
  }
  return TRUE;
}

void
salut_im_channel_send_message(SalutImChannel * self, LmMessage *message) {
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  SalutImChannelMessage *msg;

  switch (priv->state) {
    case CHANNEL_NOT_CONNECTED:
    case CHANNEL_CONNECTING:
      msg = salut_im_channel_message_new_from_message(message);
      _send_channel_message(self, msg);
      break;
    case CHANNEL_CONNECTED:
      if (!salut_lm_connection_send(priv->lm_connection, message, NULL)) {
        g_warning("Sending failed");
      }
      lm_message_unref(message);
  }
}

void
salut_im_channel_add_connection(SalutImChannel *chan, SalutLmConnection *conn) {
  SalutImChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (chan); 
  /* FIXME if we already have a connection, we throw this one out..
   * Which can be not quite what the other side expects.. And strange things
   * can happen when two * sides try to initiate at the same time */
  if (priv->lm_connection != NULL) {
    g_object_unref(conn);
    DEBUG("Already had a connection for: %s", priv->contact->name);
    return;
  }
  DEBUG("New connection for: %s", priv->contact->name);
  priv->lm_connection = conn;
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
      /* FIXME shout about queued messages ? */
      salut_lm_connection_close(priv->lm_connection);
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
  text_mixin_send(G_OBJECT(self), type, 0, priv->connection->name, 
                  priv->contact->name, text, error);
  return TRUE;
}
