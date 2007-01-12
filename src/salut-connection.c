  /*
 * salut-connection.c - Source for SalutConnection
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

#define DBUS_API_SUBJECT_TO_CHANGE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dbus/dbus-protocol.h>
#include <dbus/dbus-glib-bindings.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>


#include "salut-connection.h"
#include "salut-connection-signals-marshal.h"

#include "salut-connection-glue.h"

#include "salut-avahi-client.h"
#include "salut-avahi-entry-group.h"
#include "salut-contact-manager.h"
#include "salut-contact-channel.h"
#include "salut-im-manager.h"
#include "salut-contact.h"
#include "salut-self.h"

#include "salut-presence.h"

#include "handle-repository.h"

#include "telepathy-helpers.h"
#include "telepathy-errors.h"
#include "telepathy-interfaces.h"
#include "tp-channel-factory-iface.h"

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/error.h>
#include <avahi-glib/glib-watch.h>

#define DEBUG_FLAG DEBUG_CONNECTION
#include "debug.h"

#define BUS_NAME        "org.freedesktop.Telepathy.Connection.salut"
#define OBJECT_PATH     "/org/freedesktop/Telepathy/Connection/salut"

#define TP_CHANNEL_LIST_ENTRY_TYPE (dbus_g_type_get_struct ("GValueArray", \
      DBUS_TYPE_G_OBJECT_PATH, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, \
            G_TYPE_INVALID))

#define TP_ALIAS_PAIR_TYPE (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID))

/* Protocol as know to telepathy */
#define PROTOCOL "salut"

#define ERROR_IF_NOT_CONNECTED(CONN, ERROR) \
  if ((CONN)->status != TP_CONN_STATUS_CONNECTED) \
        { \
           DEBUG ("rejected request as disconnected"); \
           (ERROR) = g_error_new(TELEPATHY_ERRORS, NotAvailable, \
                                  "Connection is disconnected"); \
           return FALSE; \
        }

#define ERROR_IF_NOT_CONNECTED_ASYNC(CONN, ERROR, CONTEXT) \
  if ((CONN)->status != TP_CONN_STATUS_CONNECTED) \
    { \
      DEBUG ("rejected request as disconnected"); \
      (ERROR) = g_error_new(TELEPATHY_ERRORS, NotAvailable, \
                            "Connection is disconnected"); \
      dbus_g_method_return_error ((CONTEXT), (ERROR)); \
      g_error_free ((ERROR)); \
      return; \
    }

G_DEFINE_TYPE(SalutConnection, salut_connection, G_TYPE_OBJECT)

/* signal enum */
enum
{
    ALIASES_CHANGED,
    NEW_CHANNEL,
    PRESENCE_UPDATE,
    STATUS_CHANGED,
    DISCONNECTED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum {
  PROP_FIRST_NAME = 1,
  PROP_LAST_NAME,
  PROP_JID,
  PROP_EMAIL,
  LAST_PROP
};

typedef struct _SalutConnectionPrivate SalutConnectionPrivate;

struct _SalutConnectionPrivate
{
  gboolean dispose_has_run;

  /* Connection information */
  gchar *published_name;
  gchar *first_name;
  gchar *last_name;
  gchar *jid;
  gchar *email;

  /* Avahi client for browsing and resolving */
  SalutAvahiClient *avahi_client; 

  /* Handler for our presence on the lan */
  SalutSelf *self;

  /* Contact manager */
  SalutContactManager *contact_manager;

  /* IM channel manager */
  SalutImManager *im_manager;

  /* Channel requests */
  GPtrArray *channel_requests; 
  /* Whether the channel was created during a request met supress handler is
   * true */ 
  gboolean suppress_current;
};

#define SALUT_CONNECTION_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_CONNECTION, \
   SalutConnectionPrivate))

typedef struct _ChannelRequest ChannelRequest;

struct _ChannelRequest
{
  DBusGMethodInvocation *context;
  gchar *channel_type;
  guint handle_type;
  guint handle;
  gboolean suppress_handler;
};

static gboolean _salut_connection_disconnect(SalutConnection *self);
static void emit_one_presence_update (SalutConnection *self, Handle handle);

static void
salut_connection_init (SalutConnection *obj)
{
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (obj);

  obj->status = TP_CONN_STATUS_DISCONNECTED;
  obj->self_handle = 0;
  obj->handle_repo = NULL;
  obj->name = NULL;

  /* allocate any data required by the object here */
  priv->published_name = g_strdup(g_get_user_name());
  priv->first_name = NULL;
  priv->last_name = NULL;
  priv->jid = NULL;
  priv->email = NULL;

  priv->avahi_client = NULL;
  priv->self = NULL;

  priv->contact_manager = NULL;
  priv->channel_requests = g_ptr_array_new();
}

static void
salut_connection_get_property(GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec) {
  SalutConnection *self = SALUT_CONNECTION(object);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE(self);
  switch (property_id) {
    case PROP_FIRST_NAME:
      g_value_set_string(value, priv->first_name);
      break;
    case PROP_LAST_NAME:
      g_value_set_string(value, priv->last_name);
      break;
    case PROP_JID:
      g_value_set_string(value, priv->jid);
      break;
    case PROP_EMAIL:
      g_value_set_string(value, priv->email);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
salut_connection_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{   
  SalutConnection *self = SALUT_CONNECTION(object);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);

  switch (property_id) {
    case PROP_FIRST_NAME:
      g_free(priv->first_name);
      priv->first_name = g_value_dup_string(value);
      break;
    case PROP_LAST_NAME:
      g_free(priv->last_name);
      priv->last_name = g_value_dup_string(value);
      break;
    case PROP_JID:
      g_free(priv->jid);
      priv->jid = g_value_dup_string(value);
      break;
    case PROP_EMAIL:
      g_free(priv->email);
      priv->email = g_value_dup_string(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


static void salut_connection_dispose (GObject *object);
static void salut_connection_finalize (GObject *object);

static void
salut_connection_class_init (SalutConnectionClass *salut_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_connection_class);
  GParamSpec *param_spec;

  object_class->get_property = salut_connection_get_property;
  object_class->set_property = salut_connection_set_property;

  g_type_class_add_private (salut_connection_class, sizeof (SalutConnectionPrivate));

  object_class->dispose = salut_connection_dispose;
  object_class->finalize = salut_connection_finalize;

  param_spec = g_param_spec_string("first-name", "First name",
                                   "First name used in the published data",
                                   NULL,
                                   G_PARAM_READWRITE |
                                   G_PARAM_STATIC_NAME |
                                   G_PARAM_STATIC_BLURB);
  g_object_class_install_property(object_class, PROP_FIRST_NAME, param_spec);

  param_spec = g_param_spec_string("last-name", "Last name",
                                   "Last name used in the published data",
                                   NULL,
                                   G_PARAM_READWRITE |
                                   G_PARAM_STATIC_NAME |
                                   G_PARAM_STATIC_BLURB);
  g_object_class_install_property(object_class, PROP_LAST_NAME, param_spec);

  param_spec = g_param_spec_string("email", "E-mail address",
                                   "E-mail address used in the published data",
                                   NULL,
                                   G_PARAM_READWRITE |
                                   G_PARAM_STATIC_NAME |
                                   G_PARAM_STATIC_BLURB);
  g_object_class_install_property(object_class, PROP_EMAIL, param_spec);

  param_spec = g_param_spec_string("jid", "Jabber id",
                                   "Jabber idused in the published data",
                                   NULL,
                                   G_PARAM_READWRITE |
                                   G_PARAM_STATIC_NAME |
                                   G_PARAM_STATIC_BLURB);
  g_object_class_install_property(object_class, PROP_JID, param_spec);

  signals[ALIASES_CHANGED] =
    g_signal_new ("aliases-changed",
                  G_OBJECT_CLASS_TYPE (salut_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID)))));

  signals[NEW_CHANNEL] =
    g_signal_new ("new-channel",
                  G_OBJECT_CLASS_TYPE (salut_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_connection_marshal_VOID__STRING_STRING_UINT_UINT_BOOLEAN,
                  G_TYPE_NONE, 5, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_BOOLEAN);

  signals[PRESENCE_UPDATE] =
    g_signal_new ("presence-update",
                  G_OBJECT_CLASS_TYPE (salut_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_map ("GHashTable", G_TYPE_UINT, (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)))), G_TYPE_INVALID)))));

  signals[STATUS_CHANGED] =
    g_signal_new ("status-changed",
                  G_OBJECT_CLASS_TYPE (salut_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_connection_marshal_VOID__UINT_UINT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[DISCONNECTED] =
    g_signal_new ("disconnected",
                  G_OBJECT_CLASS_TYPE (salut_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (salut_connection_class), &dbus_glib_salut_connection_object_info);
}

void
salut_connection_dispose (GObject *object)
{
  SalutConnection *self = SALUT_CONNECTION (object);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  DBusGProxy *bus_proxy;  


  if (priv->dispose_has_run)
    return;
  DEBUG("Disposing connection");
  bus_proxy = tp_get_bus_proxy();;

  priv->dispose_has_run = TRUE;

  if (priv->contact_manager) {
    g_object_unref(priv->contact_manager);
    priv->contact_manager = NULL;
  }

  if (priv->channel_requests) {
    g_assert(priv->channel_requests->len == 0);
    g_ptr_array_free(priv->channel_requests, TRUE);
    priv->channel_requests = NULL;
  }

  if (priv->self) {
    g_object_unref(priv->self);
    priv->self = NULL;
  }

  if (priv->avahi_client) {
    g_object_unref(priv->avahi_client);
    priv->avahi_client = NULL;
  }

  if (NULL != self->bus_name) {
      dbus_g_proxy_call_no_reply (bus_proxy, "ReleaseName",
                                  G_TYPE_STRING, self->bus_name,
                                  G_TYPE_INVALID);
  }

  /* release any references held by the object here */
  if (G_OBJECT_CLASS (salut_connection_parent_class)->dispose)
    G_OBJECT_CLASS (salut_connection_parent_class)->dispose (object);
}

static ChannelRequest *
channel_request_new (DBusGMethodInvocation *context,
                     const char *channel_type,
                     guint handle_type,
                     guint handle,
                     gboolean suppress_handler)
{
  ChannelRequest *ret;

  g_assert (NULL != context);
  g_assert (NULL != channel_type);

  ret = g_new0 (ChannelRequest, 1);
  ret->context = context;
  ret->channel_type = g_strdup (channel_type);
  ret->handle_type = handle_type;
  ret->handle = handle;
  ret->suppress_handler = suppress_handler;

  return ret;
}

static void
channel_request_free (ChannelRequest *request)
{
  g_assert (NULL == request->context);
  g_free (request->channel_type);
  g_free (request);
}

static void
channel_request_cancel (gpointer data, gpointer user_data)
{
  ChannelRequest *request = (ChannelRequest *) data;
  GError *error;

  DEBUG ("cancelling request for %s/%d/%d", request->channel_type, request->handle_type, request->handle);

  error = g_error_new (TELEPATHY_ERRORS, Disconnected, "unable to "
      "service this channel request, we're disconnecting!");

  dbus_g_method_return_error (request->context, error);
  request->context = NULL;

  g_error_free (error);
  channel_request_free (request);
}

static GPtrArray *
find_matching_channel_requests (SalutConnection *conn,
                                const gchar *channel_type,
                                guint handle_type,
                                guint handle,
                                gboolean *suppress_handler) {
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (conn);
  GPtrArray *requests;
  guint i;

  requests = g_ptr_array_sized_new (1);

  for (i = 0; i < priv->channel_requests->len; i++)
    {
      ChannelRequest *request = g_ptr_array_index (priv->channel_requests, i);

      if (0 != strcmp (request->channel_type, channel_type))
        continue;

      if (handle_type != request->handle_type)
        continue;

      if (handle != request->handle)
        continue;

      /* As soon as one requests wants to suppress, send out a signal
       * with suppress_handler TRUE */
      if (request->suppress_handler && suppress_handler)
        *suppress_handler = TRUE;

      g_ptr_array_add (requests, request);
    }

  return requests;
}


void
salut_connection_finalize (GObject *object)
{
  SalutConnection *self = SALUT_CONNECTION (object);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  g_free(self->name);
  g_free(priv->published_name);
  g_free(priv->first_name);
  g_free(priv->last_name);
  g_free(priv->email);
  g_free(priv->jid);

  DEBUG("Finalizing connection");

  if (self->handle_repo) {
    handle_repo_destroy(self->handle_repo);
    self->handle_repo = NULL;
  }

  G_OBJECT_CLASS (salut_connection_parent_class)->finalize (object);
}

static GHashTable *
get_statuses_arguments(void) {
  static GHashTable *arguments = NULL;

  if (arguments == NULL) {
    arguments = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(arguments, "message", "s");
  }

  return arguments;
}

static void
connection_status_change(SalutConnection *self, 
                         TpConnectionStatus status,
                         TpConnectionStatusReason reason)  {
  if (self->status == status) 
    return;
  self->status = status;
  g_signal_emit (self, signals[STATUS_CHANGED], 0, status, reason);
  if (status == TP_CONN_STATUS_DISCONNECTED) {
    g_signal_emit(self, signals[DISCONNECTED], 0);
  }
}

void
_channel_iface_new_channel_cb(TpChannelFactoryIface *channel_iface, 
                                SalutContactChannel *channel,
                                gpointer data) {
  SalutConnection *self = SALUT_CONNECTION(data);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE(self);
  gchar *object_path = NULL;
  gchar *channel_type = NULL;
  guint handle_type = 0;
  Handle handle = 0;
  gboolean surpress_handler = priv->suppress_current;
  GPtrArray *requests;
  int i;

  g_object_get(channel,
               "object-path", &object_path,
               "channel-type", &channel_type,
               "handle-type", &handle_type,
               "handle", &handle,
               NULL);
  requests = find_matching_channel_requests(self, channel_type, handle_type,
                                            handle, &surpress_handler);
  
  g_signal_emit(self, signals[NEW_CHANNEL], 0, 
               object_path, channel_type, 
               handle_type, handle, surpress_handler);

  for (i = 0; i < requests->len; i++) {
    ChannelRequest *request = g_ptr_array_index(requests, i);

    DEBUG ("completing queued request, channel_type=%s, handle_type=%u, "
          "handle=%u, suppress_handler=%u", request->channel_type,
          request->handle_type, request->handle, request->suppress_handler);

      dbus_g_method_return (request->context, object_path);
      request->context = NULL;

      g_ptr_array_remove (priv->channel_requests, request);

      channel_request_free (request);
  }

  g_ptr_array_free(requests, TRUE);
  g_free(object_path);
  g_free(channel_type);
}

void
_contact_manager_contact_status_cb(SalutContactManager *mgr, 
                                   SalutContact *contact,
                                   SalutPresenceId status,
                                   gchar *message,
                                   gpointer data) {
  SalutConnection *self = SALUT_CONNECTION(data);
  /* TODO, this can be shortcutted as we have all info needed right here */
  emit_one_presence_update(self, 
                          handle_for_contact(self->handle_repo, contact->name));
}

void
_contact_manager_contact_alias_cb(SalutContactManager *mgr, 
                                  SalutContact *contact,
                                  gchar *alias,
                                  gpointer data) {
  SalutConnection *self = SALUT_CONNECTION(data);
  GPtrArray *aliases;
  GValue entry = {0, };
  guint handle = handle_for_contact(self->handle_repo, contact->name);

  g_value_init(&entry, TP_ALIAS_PAIR_TYPE);
  g_value_take_boxed(&entry, 
                     dbus_g_type_specialized_construct(TP_ALIAS_PAIR_TYPE));

  dbus_g_type_struct_set(&entry, 
                         0, handle,
                         1, alias,
                         G_MAXUINT);
  aliases = g_ptr_array_sized_new(1);
  g_ptr_array_add(aliases, g_value_get_boxed(&entry));

  DEBUG("Emitting AliasesChanged");

  g_signal_emit(self, signals[ALIASES_CHANGED], 0, aliases);
}

static void
_self_established_cb(SalutSelf *s, gpointer data) {
  SalutConnection *self = SALUT_CONNECTION(data);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE(self);

  self->self_handle = handle_for_contact(self->handle_repo, s->name);
  g_free(self->name);
  self->name = g_strdup(s->name);

  priv->contact_manager = salut_contact_manager_new(self, priv->avahi_client);
  g_signal_connect(priv->contact_manager, "new-channel",
                   G_CALLBACK(_channel_iface_new_channel_cb), self);
  g_signal_connect(priv->contact_manager, "contact-status-changed",
                   G_CALLBACK(_contact_manager_contact_status_cb), self);
  g_signal_connect(priv->contact_manager, "contact-alias-changed",
                   G_CALLBACK(_contact_manager_contact_alias_cb), self);

  priv->im_manager = salut_im_manager_new(self, priv->contact_manager);
  g_signal_connect(priv->im_manager, "new-channel",
                   G_CALLBACK(_channel_iface_new_channel_cb), self);


  if (!salut_contact_manager_start(priv->contact_manager, NULL)) {
    /* FIXME handle error */
    _salut_connection_disconnect(self);
    return;
  }

  connection_status_change(self, 
                           TP_CONN_STATUS_CONNECTED, 
                           TP_CONN_STATUS_REASON_NONE_SPECIFIED);
}


static void
_self_failed_cb(SalutSelf *s, GError *error, gpointer data) {
  SalutConnection *self = SALUT_CONNECTION(data);
  /* FIXME better error handling */
  connection_status_change(self, 
                               TP_CONN_STATUS_DISCONNECTED, 
                               TP_CONN_STATUS_REASON_NONE_SPECIFIED);
}

static void
_self_new_connection_cb(SalutSelf *s, SalutLLTransport *transport, 
                        gpointer data) {
  SalutConnection *self = SALUT_CONNECTION(data);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE(self);

  if (priv->im_manager == NULL) {
    /* Got a connection before we had an im manager.. Ignore */
    DEBUG("Connection but no IM manager yet!?");
    return;
  }
  DEBUG("New connection, passing to the IM manager");
  /* Get a ref and let the lower layers handle it */
  g_object_ref(transport);
  salut_im_manager_handle_connection(priv->im_manager, transport);
}

static void
_salut_avahi_client_failure_cb(SalutAvahiClient *c, 
                              SalutAvahiClientState state,
                              gpointer data) {
  SalutConnection *self = SALUT_CONNECTION(data);
  /* FIXME better error messages */
  /* FIXME instead of full disconnect we could handle the avahi restart */
  _salut_connection_disconnect(self);
}

static void
_salut_avahi_client_running_cb(SalutAvahiClient *c, 
                              SalutAvahiClientState state,
                              gpointer data) {
  SalutConnection *self = SALUT_CONNECTION(data);
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE(self);
  priv->self = salut_self_new(priv->avahi_client, 
                              priv->first_name,
                              priv->last_name,
                              priv->jid,
                              priv->email);
  g_signal_connect(priv->self, "established", 
                   G_CALLBACK(_self_established_cb), self);
  g_signal_connect(priv->self, "failure", 
                   G_CALLBACK(_self_failed_cb), self);
  g_signal_connect(priv->self, "new-connection", 
                   G_CALLBACK(_self_new_connection_cb), self);
  if (!salut_self_announce(priv->self, NULL)) {
    _salut_connection_disconnect(self);
  }
}

/* public functions */
gboolean _salut_connection_register(SalutConnection *conn, char **bus_name,
                                     char **object_path, GError **error) {
  SalutConnectionPrivate *priv;
  GError *request_error;
  guint request_name_result;
  DBusGConnection *bus;
  DBusGProxy *bus_proxy;

  bus = tp_get_bus ();
  bus_proxy = tp_get_bus_proxy ();
  priv = SALUT_CONNECTION_GET_PRIVATE (conn);


  conn->bus_name = 
    g_strdup_printf(BUS_NAME ".%s.%s", PROTOCOL, priv->published_name);
  conn->object_path = 
    g_strdup_printf(OBJECT_PATH "/%s/%s", PROTOCOL, priv->published_name);

  if (!org_freedesktop_DBus_request_name(bus_proxy,
                                         conn->bus_name,
                                         DBUS_NAME_FLAG_DO_NOT_QUEUE,
                                         &request_name_result,
                                         &request_error)) {
    *error = g_error_new (TELEPATHY_ERRORS, NotAvailable, "Error acquiring "
          "bus name %s: %s", conn->bus_name, request_error->message);
    g_error_free (request_error);
  }
  if (request_name_result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
    gchar *msg;
    switch (request_name_result) {
      case DBUS_REQUEST_NAME_REPLY_IN_QUEUE:
        msg = "Request has been queued, though we request non-queueing.";
        break;
      case DBUS_REQUEST_NAME_REPLY_EXISTS:
        msg = "A connection manger already has this busname.";
        break;
      case DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER:
        msg = "Connection manager already has a connection.";
        break;
      default:
        msg = "Unknown error return from RequestName";
    }
    DEBUG("Registering connection failed: %s", msg);
    *error = g_error_new (TELEPATHY_ERRORS, NotAvailable, "Error acquiring "
      "bus name %s: %s", conn->bus_name, msg);

    g_free (conn->bus_name);
    conn->bus_name = NULL;
    g_free (conn->object_path);
    conn->object_path = NULL;
    return FALSE;
  }

  dbus_g_connection_register_g_object(bus, conn->object_path, G_OBJECT(conn));
  *bus_name = g_strdup(conn->bus_name);
  *object_path = g_strdup(conn->object_path);
  return TRUE;
}


gboolean
_salut_connection_disconnect(SalutConnection *self) {
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  if (priv->contact_manager) {
    g_object_unref(priv->contact_manager);
    priv->contact_manager = NULL;
  }

  if (priv->im_manager) {
    g_object_unref(priv->im_manager);
    priv->im_manager = NULL;
  }

  if (self->handle_repo) {
    handle_repo_destroy(self->handle_repo);
    self->handle_repo = NULL;
  }
  if (priv->self) {
    g_object_unref(priv->self);
    priv->self = NULL;
  }
  if (priv->avahi_client) {
    g_object_unref(priv->avahi_client);
    priv->avahi_client = NULL;
  }
  self->self_handle = 0;

  g_ptr_array_foreach(priv->channel_requests, 
                      (GFunc) channel_request_cancel, NULL);

  connection_status_change(self, TP_CONN_STATUS_DISCONNECTED,
                            TP_CONN_STATUS_REASON_REQUESTED);
  return TRUE;
}

gboolean
_salut_connection_connect(SalutConnection *self, GError **error) {
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  GError *client_error = NULL;

  connection_status_change(self, TP_CONN_STATUS_CONNECTING,
                            TP_CONN_STATUS_REASON_REQUESTED);

  self->handle_repo = handle_repo_new();

  priv->avahi_client = salut_avahi_client_new(SALUT_AVAHI_CLIENT_FLAG_NO_FAIL);

  g_signal_connect(priv->avahi_client, "state-changed::running", 
                   G_CALLBACK(_salut_avahi_client_running_cb), self);
  g_signal_connect(priv->avahi_client, "state-changed::failure", 
                   G_CALLBACK(_salut_avahi_client_failure_cb), self);

  if (!salut_avahi_client_start(priv->avahi_client, &client_error)) {
    *error = g_error_new(TELEPATHY_ERRORS, NotAvailable, 
                         "Unstable to initialize the avahi client: %s",
                         client_error->message);
    g_error_free(client_error);
    goto error;
  }

  return TRUE;

error:
  _salut_connection_disconnect(self);
  return FALSE;
}

static void
destroy_value(GValue *value) {
  g_value_unset(value);
  g_free(value);
}

/**
 * emit_presence_update:
 * @self: A #SalutConnection
 * @contact_handles: A zero-terminated array of #Handle for
 *                    the contacts to emit presence for
 *
 * Emits the Telepathy PresenceUpdate signal with the current
 * stored presence information for the given contact.
 */
static void
emit_presence_update (SalutConnection *self,
                      const GArray *contact_handles)
{
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  GHashTable *presence_hash;
  GValueArray *vals;
  GHashTable *contact_status, *parameters;
  guint timestamp = 0; /* this is never set at the moment*/
  guint i;

  presence_hash = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
                                    (GDestroyNotify) g_value_array_free);

  for (i = 0; i < contact_handles->len; i++) {
      Handle handle = g_array_index (contact_handles, Handle, i);
      GValue *message;
      SalutPresenceId status;
      gchar *status_message = NULL;
      SalutContact *contact = NULL;

      g_assert(handle_is_valid(self->handle_repo, 
                               TP_HANDLE_TYPE_CONTACT, handle, NULL));

      if (handle == self->self_handle) { 
        status = priv->self->status;
        status_message = priv->self->status_message;
      } else {
        contact = salut_contact_manager_get_contact(priv->contact_manager,
                                                    handle);
        if (contact) {
          status = contact->status;
          status_message = contact->status_message;
        } else {
          status = SALUT_PRESENCE_OFFLINE;
        }
        if (status_message == NULL) {
          status_message = "unknown";
        }
      }

      message = g_new0 (GValue, 1);
      g_value_init (message, G_TYPE_STRING);
      g_value_set_string (message, status_message);
      if (contact) 
        g_object_unref(contact);

      parameters =
        g_hash_table_new_full (g_str_hash, g_str_equal,
                               NULL, (GDestroyNotify) destroy_value);

      g_hash_table_insert (parameters, "message", message);

      contact_status =
        g_hash_table_new_full (g_str_hash, g_str_equal,
                               NULL, (GDestroyNotify) g_hash_table_destroy);
      g_hash_table_insert (
        contact_status, (gchar *) salut_presence_statuses[status].name,
        parameters);

      vals = g_value_array_new (2);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 0), G_TYPE_UINT);
      g_value_set_uint (g_value_array_get_nth (vals, 0), timestamp);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 1),
          dbus_g_type_get_map ("GHashTable", G_TYPE_STRING,
            dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)));
      g_value_take_boxed (g_value_array_get_nth (vals, 1), contact_status);

      g_hash_table_insert (presence_hash, GINT_TO_POINTER (handle),
                           vals);
    }

  g_signal_emit (self, signals[PRESENCE_UPDATE], 0, presence_hash);
  g_hash_table_destroy (presence_hash);
}

/**
 * emit_one_presence_update:
 * Convenience function for calling emit_presence_update with one handle.
 */
static void
emit_one_presence_update (SalutConnection *self, Handle handle)
{
  GArray *handles = g_array_sized_new (FALSE, FALSE, sizeof (Handle), 1);

  g_array_insert_val (handles, 0, handle);
  emit_presence_update (self, handles);
  g_array_free (handles, TRUE);
}

/* Dbus functions */
/**
 * salut_connection_add_status
 *
 * Implements DBus method AddStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean salut_connection_add_status (SalutConnection *obj, const gchar * status, GHashTable * parms, GError **error)
{
   *error = g_error_new (TELEPATHY_ERRORS, NotImplemented, 
        "Only one status is possible at a time with this protocol");
  return FALSE;
}


/**
 * salut_connection_clear_status
 *
 * Implements DBus method ClearStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean salut_connection_clear_status (SalutConnection *self, GError **error)
{
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  gboolean ret;

  ERROR_IF_NOT_CONNECTED(self, *error);
  
  ret = salut_self_set_presence(priv->self, SALUT_PRESENCE_AVAILABLE, 
                                "Available", error);
  /* FIXME turn into a TP ERROR */
  if (ret) {
    emit_one_presence_update(self, self->self_handle);
  }

  return TRUE;
}


/**
 * salut_connection_connect
 *
 * Implements DBus method Connect
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean salut_connection_connect (SalutConnection *self, GError **error)
{
  if (self->status == TP_CONN_STATUS_DISCONNECTED)
    return _salut_connection_connect(self, error);
  return TRUE;
}


/**
 * salut_connection_disconnect
 *
 * Implements DBus method Disconnect
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean salut_connection_disconnect (SalutConnection *self, GError **error)
{
  DEBUG("Disconnect request");
  if (self->status != TP_CONN_STATUS_DISCONNECTED)
    return _salut_connection_disconnect(self);
  return TRUE;
}


/**
 * salut_connection_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean salut_connection_get_interfaces (SalutConnection *obj, gchar *** ret, GError **error)
{
  const gchar *interfaces [] = {
    TP_IFACE_CONN_INTERFACE_ALIASING,
    TP_IFACE_CONN_INTERFACE_PRESENCE,
    NULL };
  
  *ret = g_strdupv((gchar **)interfaces);
  return TRUE;
}


/**
 * salut_connection_get_protocol
 *
 * Implements DBus method GetProtocol
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean salut_connection_get_protocol (SalutConnection *obj, gchar ** ret, GError **error)
{
  g_assert(SALUT_IS_CONNECTION(obj));

  *ret = g_strdup(PROTOCOL);

  return TRUE;
}


/**
 * salut_connection_get_self_handle
 *
 * Implements DBus method GetSelfHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean 
salut_connection_get_self_handle (SalutConnection *self, guint* ret, 
                                  GError **error) {
  ERROR_IF_NOT_CONNECTED(self, *error);
  *ret = self->self_handle;
  return TRUE;
}


/**
 * salut_connection_get_status
 *
 * Implements DBus method GetStatus
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean 
salut_connection_get_status (SalutConnection *obj, guint* ret, GError **error)
{
  *ret = obj->status;
  return TRUE;
}


/**
 * salut_connection_get_statuses
 *
 * Implements DBus method GetStatuses
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean salut_connection_get_statuses (SalutConnection *obj, GHashTable ** ret, GError **error)
{
  GValueArray *status;
  int i;
  *ret = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, 
                                 (GDestroyNotify) g_value_array_free);
  for (i = 0; salut_presence_statuses[i].name ; i++) {
    status = g_value_array_new(5);

    g_value_array_append(status, NULL);
    g_value_init(g_value_array_get_nth(status, 0), G_TYPE_UINT);
    g_value_set_uint(g_value_array_get_nth(status, 0), 
      salut_presence_statuses[i].presence_type);

    g_value_array_append(status, NULL);
    g_value_init(g_value_array_get_nth(status, 1), G_TYPE_BOOLEAN);
    g_value_set_boolean(g_value_array_get_nth(status, 1), TRUE);
    
    g_value_array_append(status, NULL);
    g_value_init(g_value_array_get_nth(status, 2), G_TYPE_BOOLEAN);
    g_value_set_boolean(g_value_array_get_nth(status, 2), TRUE);

    g_value_array_append(status, NULL);
    g_value_init(g_value_array_get_nth(status, 3), 
        DBUS_TYPE_G_STRING_STRING_HASHTABLE);
    g_value_set_static_boxed(g_value_array_get_nth(status, 3),
       get_statuses_arguments());

    g_hash_table_insert(*ret, (gchar*)salut_presence_statuses[i].name, status);
  }

  return TRUE;
}


/**
 * salut_connection_hold_handles
 *
 * Implements DBus method HoldHandles
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
void salut_connection_hold_handles (SalutConnection *self, 
                                        guint handle_type, 
                                        const GArray * handles, 
                                        DBusGMethodInvocation *context)
{
  GError *error = NULL;
  gchar *sender;
  int i;

  ERROR_IF_NOT_CONNECTED_ASYNC(self, error, context);

  if (!handles_are_valid(self->handle_repo, handle_type, 
                         handles, FALSE, &error)) {
    dbus_g_method_return_error (context, error);
    g_error_free(error);
    return;
  }
  sender = dbus_g_method_get_sender(context);
  for (i = 0; i < handles->len; i++) {
    Handle handle = g_array_index(handles, Handle, i);
    if (!handle_client_hold(self->handle_repo, sender, handle, 
                            handle_type, &error)) {
      dbus_g_method_return_error(context, error);
      g_error_free(error);
      return;
    }
  }

  dbus_g_method_return(context);
}


/**
 * salut_connection_inspect_handles
 *
 * Implements DBus method InspectHandles
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
void
salut_connection_inspect_handles (SalutConnection *self, guint handle_type, 
                                  const GArray * handles, 
                                  DBusGMethodInvocation *context)
{
  GError *error = NULL;
  const gchar **ret;
  int i;

  ERROR_IF_NOT_CONNECTED_ASYNC(self, error, context);

  if (!handles_are_valid(self->handle_repo,
                                handle_type,
                                handles,
                                FALSE,
                                &error)) {
    dbus_g_method_return_error(context, error);
    g_error_free(error);
    return;
  }

  ret = g_new(const gchar *, handles->len + 1);
  for (i = 0 ; i < handles->len ; i++ ) {
    Handle handle;
    const gchar *n;
    handle = g_array_index(handles, Handle, i);
    n = handle_inspect(self->handle_repo, handle_type, handle);
    g_assert(n != NULL);

    ret[i] = n;
  } 

  ret[i] = NULL;
  
  dbus_g_method_return(context, ret);
  g_free(ret);
}

static void
list_channel_factory_foreach_one(TpChannelIface *chan, gpointer data) {
  GObject *channel = G_OBJECT(chan);
  GPtrArray *channels = (GPtrArray *)data;
  gchar *path, *type;
  guint handle_type, handle;

  GValue entry = {0, };
  g_value_init(&entry, TP_CHANNEL_LIST_ENTRY_TYPE);
  g_value_take_boxed(&entry, 
               dbus_g_type_specialized_construct((TP_CHANNEL_LIST_ENTRY_TYPE)));

  g_object_get(channel, 
               "object-path", &path,
               "channel-type", &type,
               "handle-type", &handle_type,
               "handle", &handle,
               NULL);
  dbus_g_type_struct_set(&entry,
    0, path, 
    1, type, 
    2, handle_type, 
    3, handle, G_MAXUINT);
  g_ptr_array_add(channels, g_value_get_boxed(&entry));

  g_free(path);
  g_free(type);
}

/**
 * salut_connection_list_channels
 *
 * Implements DBus method ListChannels
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean 
salut_connection_list_channels (SalutConnection *self, 
                                GPtrArray ** ret, 
                                GError **error) {
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE(self);
  GPtrArray *channels = NULL;

  ERROR_IF_NOT_CONNECTED(self, *error);

  channels = g_ptr_array_sized_new(3);

  if (priv->contact_manager != NULL) {
    tp_channel_factory_iface_foreach(
      TP_CHANNEL_FACTORY_IFACE(priv->contact_manager), 
        list_channel_factory_foreach_one, channels);
  }
  if (priv->im_manager != NULL) {
    tp_channel_factory_iface_foreach(
      TP_CHANNEL_FACTORY_IFACE(priv->im_manager), 
        list_channel_factory_foreach_one, channels);
  }

  *ret = channels;
  return TRUE;
}


/**
 * salut_connection_release_handles
 *
 * Implements DBus method ReleaseHandles
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
void 
salut_connection_release_handles (SalutConnection *self, 
                                  guint handle_type, 
                                  const GArray * handles, 
                                  DBusGMethodInvocation *context) {
  gchar *sender;
  GError *error = NULL;
  int i;

  ERROR_IF_NOT_CONNECTED_ASYNC(self, error, context);
  if (!handles_are_valid(self->handle_repo,
                                handle_type,
                                handles,
                                FALSE,
                                &error)) {
    dbus_g_method_return_error(context, error);
    g_error_free(error);
    return;
  }
  sender = dbus_g_method_get_sender(context);
  for (i = 0; i < handles->len; i++) {
    Handle handle = g_array_index(handles, Handle, i);
    if (!handle_client_release (self->handle_repo, sender, handle,
                                      handle_type, &error)) {
      dbus_g_method_return_error(context, error);
      g_error_free(error);
      return;
    }
  }

  dbus_g_method_return (context);
  return;
}


/**
 * salut_connection_remove_status
 *
 * Implements DBus method RemoveStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean 
salut_connection_remove_status (SalutConnection *self, 
                                const gchar * status, GError **error) {
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE(self);
  gboolean ret = TRUE;

  ERROR_IF_NOT_CONNECTED(self, *error);

  if (strcmp(status, salut_presence_statuses[priv->self->status].name) == 0) {
    ret = salut_self_set_presence(priv->self, SALUT_PRESENCE_AVAILABLE, 
                                "Available", error);
    /* FIXME turn into a TP ERROR */
    if (ret) {
      emit_one_presence_update(self, self->self_handle);
    }
  } else {
   *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument, 
                         "Attempting to remove non-existing presence.");
    ret = FALSE;
  }

  return ret;
}


/**
 * salut_connection_request_channel
 *
 * Implements DBus method RequestChannel
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
void 
salut_connection_request_channel (SalutConnection *self, const gchar * type, 
                                  guint handle_type, guint handle, 
                                  gboolean suppress_handler, 
                                  DBusGMethodInvocation *context) {
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE(self);
  TpChannelFactoryRequestStatus status, s;
  TpChannelIface *chan = NULL;
  gchar *object_path = NULL;
  GError *error = NULL; 

  ERROR_IF_NOT_CONNECTED_ASYNC(self, error, context);

  DEBUG("Requested channel of type %s for handle %d of type %d", 
        type, handle, handle_type);

  priv->suppress_current = suppress_handler;
  status = tp_channel_factory_iface_request (
    TP_CHANNEL_FACTORY_IFACE(priv->contact_manager), type, 
      (TpHandleType) handle_type, handle, &chan);

  if (status != TP_CHANNEL_FACTORY_REQUEST_STATUS_DONE ||
      status != TP_CHANNEL_FACTORY_REQUEST_STATUS_QUEUED) {
    s = tp_channel_factory_iface_request (
      TP_CHANNEL_FACTORY_IFACE(priv->im_manager), type, 
        (TpHandleType) handle_type, handle, &chan);
    status = MAX(s, status);
  }
  priv->suppress_current = FALSE;


  switch (status) {
    case TP_CHANNEL_FACTORY_REQUEST_STATUS_DONE:
      g_object_get (chan, "object-path", &object_path, NULL);
      goto got_channel;
    case TP_CHANNEL_FACTORY_REQUEST_STATUS_QUEUED: {
      ChannelRequest *request;
      DEBUG ("queueing request, channel_type=%s, handle_type=%u, "
              "handle=%u, suppress_handler=%u", type, handle_type,
              handle, suppress_handler);
      request = channel_request_new (context, type, handle_type, handle,
                                    suppress_handler);
      g_ptr_array_add (priv->channel_requests, request);
      goto out;
    }
    case TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE:
      error = g_error_new (TELEPATHY_ERRORS, InvalidHandle,
                             "invalid handle %u", handle);
      break;
    case TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE:
      error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                             "requested channel is not available with "
                             "handle type %u", handle_type);
      break;
    case TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED:
      error = g_error_new (TELEPATHY_ERRORS, NotImplemented,
                             "unsupported channel type %s", type);
      break;
    default:
      g_assert_not_reached();
  }

  if (error != NULL) {
    g_assert (object_path == NULL);
    DEBUG("Returned error: %s", error->message);
    dbus_g_method_return_error(context, error);
    g_error_free(error);
    return;
  }

got_channel:  
 DEBUG ("got channel for request, channel_type=%s, handle_type=%u, "
              "handle=%u, suppress_handler=%u", type, handle_type,
              handle, suppress_handler);
  g_assert(object_path != NULL);
  dbus_g_method_return(context, object_path);
  g_free(object_path);
out:
  ;
}

static void
hold_and_return_handles (DBusGMethodInvocation *context,
                         SalutConnection *conn,
                         GArray *handles,
                         guint handle_type) {
  GError *error;
  gchar *sender = dbus_g_method_get_sender(context);
  guint i;

  for (i = 0; i < handles->len; i++)
    {
      Handle handle = (Handle) g_array_index (handles, guint, i);
      if (!handle_client_hold (conn->handle_repo, sender, 
                                 handle, handle_type, &error)) {
          dbus_g_method_return_error (context, error);
          g_error_free (error);
          return;
        }
    }
  dbus_g_method_return (context, handles);
}


/**
 * salut_connection_request_handles
 *
 * Implements DBus method RequestHandles
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
void 
salut_connection_request_handles (SalutConnection *self, 
                                  guint handle_type, 
                                  const gchar ** names, 
                                  DBusGMethodInvocation *context) {
  GError *error = NULL;
  const gchar **n;
  int count = 0;
  int i;
  GArray *handles = NULL;

  ERROR_IF_NOT_CONNECTED_ASYNC(self, error, context);

  if (!handle_type_is_valid(handle_type, &error)) {
    DEBUG("Invalid handle type: %d", handle_type);
    dbus_g_method_return_error(context, error);
    g_error_free(error);
    return;
  }

  for (n = names; *n != NULL; n++)  {
    if (*n == '\0') {
      DEBUG("Request for empty name?!");
      error = g_error_new(TELEPATHY_ERRORS, InvalidArgument,
                           "Empty handle name");
      dbus_g_method_return_error(context, error);
      g_error_free(error);
      return;
    }
    DEBUG("Requested handle of type %d for %s", handle_type, *n);
    count++;
  }

  switch (handle_type)  {
    case TP_HANDLE_TYPE_CONTACT:
    case TP_HANDLE_TYPE_LIST:
      handles = g_array_sized_new(FALSE, FALSE, sizeof(Handle), count);
      for (i = 0; i < count ; i++) {
        Handle handle;
        const gchar *name = names[i];
        if (!handle_name_is_valid(handle_type, name, &error)) {
          dbus_g_method_return_error(context, error);
          g_error_free(error);
          g_array_free(handles, TRUE);
          return;
        }

        handle = handle_for_type (self->handle_repo, handle_type, name);

        if (handle == 0) {
          error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                               "requested handle %s wasn't available", name);
          dbus_g_method_return_error(context, error);
          g_error_free(error);
          g_array_free(handles, TRUE);
          return;
        }
        g_array_append_val(handles, handle);
      }
      hold_and_return_handles (context, self, handles, handle_type);
      g_array_free(handles, TRUE);
      break;
    default:
      DEBUG("Unimplemented handle type");
      error = g_error_new(TELEPATHY_ERRORS, NotAvailable, 
                          "unimplemented handle type %u", handle_type);
      dbus_g_method_return_error(context, error);
      g_error_free(error);
  }

  return;
}

/**
 * salut_connection_request_presence
 *
 * Implements DBus method RequestPresence
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean 
salut_connection_request_presence (SalutConnection *self, 
                                   const GArray * contacts, GError **error) {

  ERROR_IF_NOT_CONNECTED(self, *error);
  if (!handles_are_valid(self->handle_repo, TP_HANDLE_TYPE_CONTACT,
                         contacts, FALSE, error)) 
    return FALSE;

  if (contacts->len) 
    emit_presence_update(self, contacts);

  return TRUE;
}


/**
 * salut_connection_set_last_activity_time
 *
 * Implements DBus method SetLastActivityTime
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean salut_connection_set_last_activity_time (SalutConnection *obj, guint time, GError **error)
{
  *error = g_error_new (TELEPATHY_ERRORS, NotImplemented, 
                         "NotImplemented (%d)", __LINE__);
  return FALSE;
}


/**
 * salut_connection_set_status
 *
 * Implements DBus method SetStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean 
salut_connection_set_status (SalutConnection *self, 
                             GHashTable * statuses, GError **error) {
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  SalutPresenceId id = 0;
  GHashTable *parameters = NULL;
  const gchar *status_message = NULL;
  GValue *message;
  gboolean ret = TRUE;

  ERROR_IF_NOT_CONNECTED(self, *error);

  if (g_hash_table_size(statuses) != 1) {
    DEBUG("Got more then one status");
    *error = g_error_new(TELEPATHY_ERRORS, InvalidArgument,
                       "Only one status may be set at a time in this protocol");
    return FALSE;
  }
  /* Don't feel like jumping through hoops with a foreach */
  for ( ; id < SALUT_PRESENCE_NR_PRESENCES; id++) {
    parameters = g_hash_table_lookup(statuses, 
                                      salut_presence_statuses[id].name);
    if (parameters) {
      break;
    }
  }

  if (id == SALUT_PRESENCE_NR_PRESENCES)  {
    *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument, 
                         "Unknown status");
    return FALSE;
  }

  message = g_hash_table_lookup(parameters, "message");
  if (message) {
    if (!G_VALUE_HOLDS_STRING(message)) {
      *error = g_error_new(TELEPATHY_ERRORS, InvalidArgument,
                            "Status argument 'message' requeires a string");
      return FALSE;
    }
    status_message = g_value_get_string(message);
  }

  ret = salut_self_set_presence(priv->self, id, status_message, error);
  /* FIXME turn into a TP ERROR */
  if (ret) {
    emit_one_presence_update(self, self->self_handle);
  }

  return ret;
}

/**
 * salut_connection_get_alias_flags
 *
 * Implements D-Bus method GetAliasFlags
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_get_alias_flags (SalutConnection *self,
                                  guint *ret,
                                  GError **error)
{
  /* Aliases are set by the contacts 
   * Actually we concat the first and lastname property */
  *ret = 0;
  return TRUE;
}

/**
 * salut_connection_request_aliases
 *
 * Implements D-Bus method RequestAliases
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_request_aliases (SalutConnection *self,
                                  const GArray *contacts,
                                  gchar ***ret,
                                  GError **error) {
  SalutConnectionPrivate *priv = SALUT_CONNECTION_GET_PRIVATE (self);
  int i;
  gchar **aliases;

  DEBUG("Alias requested");

  ERROR_IF_NOT_CONNECTED(self, *error);
  if (!handles_are_valid(self->handle_repo, TP_HANDLE_TYPE_CONTACT,
                                            contacts, FALSE, error)) {
    return FALSE;
  }

  aliases = g_new0(gchar *, contacts->len + 1);
  for (i = 0; i < contacts->len; i++) {
    Handle handle = g_array_index (contacts, Handle, i);
    SalutContact *contact;
    if (handle == self->self_handle) {
      aliases[i] = g_strdup(salut_self_get_alias(priv->self));
    } else {
      contact = salut_contact_manager_get_contact(priv->contact_manager, handle);
      aliases[i] = g_strdup(salut_contact_get_alias(contact));
      g_object_unref(contact);
    }
  }
  *ret = aliases;
  return TRUE;
}

/**
 * salut_connection_set_aliases
 *
 * Implements D-Bus method SetAliases
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_connection_set_aliases (SalutConnection *self,
                              GHashTable *aliases,
                              GError **error) {
  *error = g_error_new(TELEPATHY_ERRORS, InvalidArgument,
                       "Aliases can't be set on salut");
  return FALSE;
}
