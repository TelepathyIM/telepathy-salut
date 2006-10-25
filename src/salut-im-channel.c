/*
 * salut-im-channel.c - Source for SalutIMChannel
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

#define A_ARRAY "__salut_im_channel_address_array__"
#define A_INDEX "__salut_im_channel_address_index__"

G_DEFINE_TYPE_WITH_CODE(SalutIMChannel, salut_im_channel, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(TP_TYPE_CHANNEL_IFACE, NULL));

/* SendError types */
enum {
  CHANNEL_TEXT_SEND_ERROR_UNKNOWN = 0,
  CHANNEL_TEXT_SEND_ERROR_OFFLINE,
  CHANNEL_TEXT_SEND_ERROR_INVALID_CONTACT,
  CHANNEL_TEXT_SEND_ERROR_PERMISSION_DENIED,
  CHANNEL_TEXT_SEND_ERROR_TOO_LONG
};

/* Channel state */
typedef enum {
  CHANNEL_NOT_CONNECTED = 0, 
  CHANNEL_CONNECTING,
  CHANNEL_CONNECTED
} ChannelState;
 

/* signal enum */
enum {
    ESTABLISHED,
    CLOSED,
    LOST_MESSAGE,
    RECEIVED,
    SEND_ERROR,
    SENT,
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
typedef struct _SalutIMChannelPrivate SalutIMChannelPrivate;

struct _SalutIMChannelPrivate
{
  gboolean dispose_has_run;
  gchar *object_path;
  Handle handle;
  SalutContact *contact;
  SalutConnection *connection;
  SalutLmConnection *lm_connection;
  /* Outcoming and incoming message queues */
  GQueue *out_queue;
  GQueue *in_queue;
  ChannelState state;
};

#define SALUT_IM_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_IM_CHANNEL, SalutIMChannelPrivate))

typedef struct _SalutIMChannelMessage SalutIMChannelMessage;

struct _SalutIMChannelMessage {
  guint id;
  guint time;
  guint type;
  gchar *text;
};

static SalutIMChannelMessage *
salut_im_channel_message_new(guint type, const gchar *text) {
  SalutIMChannelMessage *msg;
  msg = g_new(SalutIMChannelMessage, 1);
  msg->type = type;
  msg->text = g_strdup(text);
  return msg;
}

static SalutIMChannelMessage *
salut_im_channel_message_new_received(LmMessage *message) {
  SalutIMChannelMessage *msg;
  static guint id = 1;
  LmMessageNode *node;
  const gchar *type;
  const gchar *body = "";

  /* FIXME decent id generation */
  msg = g_new(SalutIMChannelMessage, 1);
  msg->time = time(NULL);
  msg->id = id++;
  lm_message_ref(message);

  node = lm_message_node_get_child(message->node, "body");
  type = lm_message_node_get_attribute(message->node, "type");

  msg->type = TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE;
  if (node) {
    body = lm_message_node_get_value(node);
    if (strncmp(body, "/me ", 4) == 0) {
      msg->type = TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION;
      body += 4;
    } else if (type != NULL && strcmp("type", "chat") == 0) {
      msg->type = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
    }
  }
  msg->text = g_strdup(body);
  DEBUG("Received message: %s(%d)", msg->text, msg->type);

  return msg;
}

static void 
salut_im_channel_message_free(SalutIMChannelMessage *message) {
  /* FIXME UGLY!*/
  g_free(message->text);
  g_free(message);
}

static void
salut_im_channel_init (SalutIMChannel *obj)
{
  SalutIMChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (obj); 
  /* allocate any data required by the object here */
  priv->object_path = NULL;
  priv->contact = NULL;
  priv->lm_connection = NULL;
  priv->in_queue = g_queue_new();
  priv->out_queue = g_queue_new();
  priv->state = CHANNEL_NOT_CONNECTED;
}


static void
salut_im_channel_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  SalutIMChannel *chan = SALUT_IM_CHANNEL (object);
  SalutIMChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (chan);

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
  SalutIMChannel *chan = SALUT_IM_CHANNEL (object);
  SalutIMChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (chan);

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
  SalutIMChannelPrivate *priv;

  /* Parent constructor chain */
  obj = G_OBJECT_CLASS(salut_im_channel_parent_class)->
        constructor(type, n_props, props);

  priv = SALUT_IM_CHANNEL_GET_PRIVATE (SALUT_IM_CHANNEL (obj));

  /* Ref our handle */
  valid = handle_ref(priv->connection->handle_repo, TP_HANDLE_TYPE_CONTACT, 
                     priv->handle);
  g_assert(valid);

  /* Connect to the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object(bus, priv->object_path, obj);

  return obj;
}

static void salut_im_channel_dispose (GObject *object);
static void salut_im_channel_finalize (GObject *object);

static void
salut_im_channel_class_init (SalutIMChannelClass *salut_im_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_im_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_im_channel_class, sizeof (SalutIMChannelPrivate));

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

  signals[LOST_MESSAGE] =
    g_signal_new ("lost-message",
                  G_OBJECT_CLASS_TYPE (salut_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_im_channel_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[RECEIVED] =
    g_signal_new ("received",
                  G_OBJECT_CLASS_TYPE (salut_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_im_channel_marshal_VOID__INT_INT_INT_INT_INT_STRING,
                  G_TYPE_NONE, 6, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  signals[SEND_ERROR] =
    g_signal_new ("send-error",
                  G_OBJECT_CLASS_TYPE (salut_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_im_channel_marshal_VOID__INT_INT_INT_STRING,
                  G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  signals[SENT] =
    g_signal_new ("sent",
                  G_OBJECT_CLASS_TYPE (salut_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_im_channel_marshal_VOID__INT_INT_STRING,
                  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (salut_im_channel_class), &dbus_glib_salut_im_channel_object_info);
}

void
salut_im_channel_dispose (GObject *object)
{
  SalutIMChannel *self = SALUT_IM_CHANNEL (object);
  SalutIMChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

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
  SalutIMChannel *self = SALUT_IM_CHANNEL (object);
  SalutIMChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  g_free(priv->object_path);

  g_queue_foreach(priv->in_queue, (GFunc)salut_im_channel_message_free, NULL);
  g_queue_foreach(priv->out_queue, (GFunc)salut_im_channel_message_free, NULL);
  g_queue_free(priv->in_queue);
  g_queue_free(priv->out_queue);

  G_OBJECT_CLASS (salut_im_channel_parent_class)->finalize (object);
}

static void
_sendout_message(SalutIMChannel * self, guint type, const gchar *text) {
  SalutIMChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  LmMessage *msg;

  guint subtype;
  switch (type) {
    case TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL:
    case TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION:
      subtype = LM_MESSAGE_SUB_TYPE_CHAT;
      break;
    case TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE:
      subtype = LM_MESSAGE_SUB_TYPE_NORMAL;
      break;
  }

  msg = lm_message_new_with_sub_type(priv->contact->name, 
                                    LM_MESSAGE_TYPE_MESSAGE, subtype);

  lm_message_node_set_attribute(msg->node, "from", priv->connection->name); 
  if (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION) {
    gchar *tmp;
    tmp = g_strconcat("/me ", text, NULL);
    lm_message_node_add_child(msg->node, "body", tmp);
    g_free(tmp);
  } else {
    lm_message_node_add_child(msg->node, "body", text);
  }
  
  if (salut_lm_connection_send(priv->lm_connection, msg, NULL)) {
    g_signal_emit(self, signals[SENT], 0, time(NULL), type, text);
  } else  {
    g_signal_emit(self, signals[SEND_ERROR], CHANNEL_TEXT_SEND_ERROR_UNKNOWN, 
                  time(NULL), type, text);
  }
  lm_message_unref(msg);
}

static void
_flush_queue(SalutIMChannel *self) {
  SalutIMChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  SalutIMChannelMessage *msg;
  /*Connected!, flusch the queue ! */
  while ((msg = g_queue_pop_head(priv->out_queue)) != NULL) {
    _sendout_message(self, msg->type, msg->text);
    salut_im_channel_message_free(msg);
  }
}

static void
_connection_got_message(SalutLmConnection *conn, 
                        LmMessage *message, gpointer userdata) {
  SalutIMChannel  *self = SALUT_IM_CHANNEL(userdata);
  SalutIMChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  SalutIMChannelMessage *m;

  m = salut_im_channel_message_new_received(message);
  g_queue_push_tail(priv->in_queue, m);
}

static void
_connect_to_next(SalutIMChannel *self, SalutLmConnection *conn) {
  GArray *addrs;
  SalutIMChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  int i;

  addrs = g_object_get_data(G_OBJECT(conn), A_ARRAY);
  i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(conn), A_INDEX)); 

  if (addrs->len <= i) {
    /* Failure */
    /* FIXME signal this up, probably sendError all queued outgoing stuff */
    g_array_free(addrs, TRUE);
    g_object_set_data(G_OBJECT(conn), A_ARRAY, NULL);
    priv->state = CHANNEL_NOT_CONNECTED;
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
_connection_connected(SalutLmConnection *conn, gint state, gpointer userdata) {
  SalutIMChannel  *self = SALUT_IM_CHANNEL(userdata);
  SalutIMChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  GArray *addrs;
  
  addrs = g_object_get_data(G_OBJECT(conn), A_ARRAY);
  g_array_free(addrs, TRUE);
  g_object_set_data(G_OBJECT(conn), A_ARRAY, NULL);

  priv->state = SALUT_LM_CONNECTED;
  _flush_queue(self);
}

static void
_connection_disconnected(SalutLmConnection *conn, gint state, gpointer userdata) {
  SalutIMChannel  *self = SALUT_IM_CHANNEL(userdata);
  SalutIMChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  if (priv->state == CHANNEL_CONNECTING) {
    _connect_to_next(self, conn); 
  } else  {
    /* FIXME cleanup */
    priv->state = CHANNEL_NOT_CONNECTED;
  }
}

static void
_setup_connection(SalutIMChannel *self) {
  /* FIXME do a non-blocking connect */
  SalutIMChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  GArray *addrs;

  DEBUG("Setting up the lm connection...");
  if (priv->lm_connection == NULL) {
    priv->lm_connection = salut_lm_connection_new();
    g_signal_connect(priv->lm_connection, "state_changed::disconnected",
                     G_CALLBACK(_connection_disconnected), self);
    g_signal_connect(priv->lm_connection, "state_changed::connected",
                     G_CALLBACK(_connection_connected), self);
    g_signal_connect(priv->lm_connection, "message_received::message",
                     G_CALLBACK(_connection_got_message), self);
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
_send_message(SalutIMChannel * self, guint type, const gchar *text) {
  SalutIMChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);
  SalutIMChannelMessage *msg;

  switch (priv->state) {
    case CHANNEL_NOT_CONNECTED:
      _setup_connection(self);
      /* fallthrough */
    case CHANNEL_CONNECTING:
      msg = salut_im_channel_message_new(type, text);
      g_queue_push_tail(priv->out_queue, msg);
      break;
    case CHANNEL_CONNECTED:
      /* Connected and the queue is empty, so push it out directly */
      _sendout_message(self, type, text);
      break;
  }
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
gboolean salut_im_channel_acknowledge_pending_messages (SalutIMChannel *obj, const GArray * ids, GError **error)
{
  return TRUE;
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
salut_im_channel_close (SalutIMChannel *self, GError **error) {
  SalutIMChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self); 

  switch (priv->state) {
    case CHANNEL_NOT_CONNECTED:
      /* FIXME return an error ? */
      break;
    case CHANNEL_CONNECTING:
    case CHANNEL_CONNECTED:
      /* FIXME shout about queued messages ? */
      salut_lm_connection_close(priv->lm_connection);
      break;
  }
  priv->state = CHANNEL_NOT_CONNECTED;

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
salut_im_channel_get_channel_type (SalutIMChannel *obj, gchar ** ret, 
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
salut_im_channel_get_handle (SalutIMChannel *obj, guint* ret, guint* ret1, 
                             GError **error) {
  SalutIMChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (obj); 

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
salut_im_channel_get_interfaces (SalutIMChannel *obj, gchar *** ret, 
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
salut_im_channel_get_message_types (SalutIMChannel *obj, GArray ** ret, 
                                    GError **error) {
  guint types[] = { TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
                    TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION,
                    TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE,
                  };
  *ret = g_array_sized_new(FALSE, FALSE, sizeof(guint), 
                           sizeof(types)/sizeof(guint));
  g_array_append_vals(*ret, types, sizeof(types)/sizeof(guint));

  return TRUE;
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
salut_im_channel_list_pending_messages (SalutIMChannel *obj, gboolean clear, 
                                        GPtrArray ** ret, GError **error) {
  *ret = g_ptr_array_sized_new(0);
  return TRUE;
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
salut_im_channel_send (SalutIMChannel *self, 
                       guint type, const gchar * text, GError **error) {
  //SalutIMChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self); 
  /* All messaging is done async */
  DEBUG("Sending: %s (%d)", text, type);
  _send_message(self, type, text);

  return TRUE;
}
