/*
 * salut-self.c - Source for SalutSelf
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
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#include "salut-self.h"
#include "salut-self-signals-marshal.h"

#include <gibber/gibber-linklocal-transport.h>
#include <telepathy-glib/errors.h>

#include "salut-avahi-entry-group.h"

#define DEBUG_FLAG DEBUG_SELF
#include <debug.h>

#include "sha1/sha1-util.h"

G_DEFINE_TYPE(SalutSelf, salut_self, G_TYPE_OBJECT)

/* signal enum */
enum
{
  ESTABLISHED,
  FAILURE,
  NEW_CONNECTION,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _SalutSelfPrivate SalutSelfPrivate;

struct _SalutSelfPrivate
{
  gchar *nickname;
  gchar *first_name;
  gchar *last_name;
  gchar *jid;
  gchar *email;

  gchar *alias;

  GIOChannel *listener;
  guint io_watch_in;

  SalutAvahiClient *client;
  SalutAvahiEntryGroup *presence_group;
  SalutAvahiEntryGroupService *presence;

  SalutAvahiEntryGroup *avatar_group;

  gboolean dispose_has_run;
};

#define SALUT_SELF_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_SELF, SalutSelfPrivate))

static void
salut_self_init (SalutSelf *obj)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  obj->status = SALUT_PRESENCE_AVAILABLE;
  obj->status_message = NULL;

  priv->first_name = NULL;
  priv->last_name = NULL;
  priv->jid = NULL;
  priv->email = NULL;

  priv->client = NULL;
  priv->presence_group = NULL;
  priv->presence = NULL;
  priv->listener = NULL;
}

static void salut_self_dispose (GObject *object);
static void salut_self_finalize (GObject *object);

static void
salut_self_class_init (SalutSelfClass *salut_self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_self_class);

  g_type_class_add_private (salut_self_class, sizeof (SalutSelfPrivate));

  object_class->dispose = salut_self_dispose;
  object_class->finalize = salut_self_finalize;

  signals[ESTABLISHED] =
    g_signal_new ("established",
                  G_OBJECT_CLASS_TYPE (salut_self_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[FAILURE] =
    g_signal_new ("failure",
                  G_OBJECT_CLASS_TYPE (salut_self_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__POINTER,
                  G_TYPE_NONE, 0);

  signals[NEW_CONNECTION] =
    g_signal_new ("new-connection",
                  G_OBJECT_CLASS_TYPE (salut_self_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 
                  1,
                  GIBBER_TYPE_LL_TRANSPORT);

}

void
salut_self_dispose (GObject *object)
{
  SalutSelf *self = SALUT_SELF (object);
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */
  if (priv->client != NULL)
    g_object_unref(priv->client); 
  priv->client = NULL;

  if (priv->presence_group != NULL)
    g_object_unref(priv->presence_group); 

  priv->presence_group = NULL;
  priv->presence = NULL;

  if (priv->avatar_group != NULL)
    g_object_unref(priv->avatar_group); 

  priv->avatar_group = NULL;

  if (priv->listener) {
    g_io_channel_unref(priv->listener);
    g_source_remove(priv->io_watch_in);
    priv->listener = NULL;
  }

  if (G_OBJECT_CLASS (salut_self_parent_class)->dispose)
    G_OBJECT_CLASS (salut_self_parent_class)->dispose (object);
}

void
salut_self_finalize (GObject *object)
{
  SalutSelf *self = SALUT_SELF (object);
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  g_free(priv->first_name);
  g_free(priv->last_name);
  g_free(priv->jid);
  g_free(priv->email);

  G_OBJECT_CLASS (salut_self_parent_class)->finalize (object);
}

static gboolean 
_listener_io_in(GIOChannel *source, GIOCondition condition, gpointer data) {
  SalutSelf *self = SALUT_SELF(data);
  int fd;
  int nfd;
  char host[NI_MAXHOST];
  char port[NI_MAXSERV];
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof(struct sockaddr_storage);
  GibberLLTransport *transport;


  fd = g_io_channel_unix_get_fd(source);
  nfd = accept(fd, (struct sockaddr *)&addr, &addrlen);

  transport = gibber_ll_transport_new();
  gibber_ll_transport_open_fd(transport, nfd);
  if (getnameinfo((struct sockaddr *)&addr, addrlen,
      host, NI_MAXHOST, port, NI_MAXSERV, 
      NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
    DEBUG("New connection from %s port %s", host, port);
  } else {
    DEBUG("New connection..");
  }
  g_signal_emit(self, signals[NEW_CONNECTION], 0, transport);
  g_object_unref(transport);

  return TRUE;
}

SalutSelf *
salut_self_new(SalutAvahiClient *client,
               gchar *nickname, gchar *first_name, gchar *last_name, 
               gchar *jid, gchar *email) {
  SalutSelfPrivate *priv;
  GString *alias = NULL;

  g_assert(client != NULL);

  SalutSelf *ret = g_object_new(SALUT_TYPE_SELF, NULL);
  priv = SALUT_SELF_GET_PRIVATE (ret);

  priv->client = client;
  g_object_ref(client);

  priv->nickname = g_strdup(nickname);
  priv->first_name = g_strdup(first_name);
  priv->last_name = g_strdup(last_name);
  priv->jid  = g_strdup(jid);
  priv->email = g_strdup(email);
  priv->alias = NULL;

  /* Prefer using the nickname as alias */
  if (nickname != NULL) {
    priv->alias = g_strdup(nickname);
  } else { 
    if (first_name != NULL) {
      alias = g_string_new(first_name);
    }

    if (last_name != NULL) {
      if (alias != NULL) {
        alias = g_string_append_c(alias, ' ');
        alias = g_string_append(alias, last_name);
      } else {
        alias = g_string_new(first_name);
      } 
    }

    if (alias != NULL && *(alias->str) != '\0') {
      priv->alias = alias->str;
      g_string_free(alias, FALSE);
    } else {
      g_string_free(alias, TRUE);
    }
  }
  return ret;
}

static int
self_try_listening_on_port(SalutSelf *self, int port) {
  int fd = -1, ret, yes = 1;
  struct addrinfo req, *ans;
  #define BACKLOG 5

  memset(&req, 0, sizeof(req));
  req.ai_flags = AI_PASSIVE;
  req.ai_family = AF_UNSPEC;
  req.ai_socktype = SOCK_STREAM;
  req.ai_protocol = IPPROTO_TCP;

  if ((ret = getaddrinfo(NULL, "0", &req, &ans)) != 0) {
    DEBUG("getaddrinfo failed: %s", gai_strerror(ret));
    goto error;
  }

  ((struct sockaddr_in *) ans->ai_addr)->sin_port = ntohs(port);

  fd = socket(ans->ai_family, ans->ai_socktype, ans->ai_protocol);

  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    DEBUG( "%s", strerror(errno));
    freeaddrinfo(ans);
    goto error;
  }

  if (bind(fd, ans->ai_addr, ans->ai_addrlen) < 0) {
    DEBUG( "bind failed: %s", strerror(errno));
    goto error;
  }

  ret = listen(fd, BACKLOG);
  if (ret == -1) {
    DEBUG( "listen: %s", strerror(errno));
    goto error;
  }
  
  return fd;
error:
  if (fd > 0) {
    close(fd);
  }
  return -1;
}

static int
self_start_listening(SalutSelf *self) {
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  int port = 5298;
  int fd = -1;

  for (; port < 5400 ; port++) {
    DEBUG("Trying to listen on port %d\n", port);
    fd = self_try_listening_on_port(self, port);
    if (fd > 0)
      break;
  }

  if (fd < 0) {
    return -1;
  }
  DEBUG("Listening on port %d",port);
  priv->listener = g_io_channel_unix_new(fd);
  g_io_channel_set_close_on_unref(priv->listener, TRUE);
  priv->io_watch_in = g_io_add_watch(priv->listener, G_IO_IN, 
                                     _listener_io_in, self);

  return port;
}

static 
AvahiStringList *create_txt_record(SalutSelf *self, int port) {
  AvahiStringList *ret;
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);

   ret = avahi_string_list_new("txtvers=1", NULL);
   
   /* Some silly clients still use this */
   ret = avahi_string_list_add_printf(ret, "port.p2pj=%d", port);

   if (priv->nickname) 
     ret = avahi_string_list_add_printf(ret, "nick=%s", priv->nickname);
   if (priv->first_name)
     ret = avahi_string_list_add_printf(ret, "1st=%s", priv->first_name);
   if (priv->last_name)
     ret = avahi_string_list_add_printf(ret, "last=%s", priv->last_name);
   if (priv->email)
     ret = avahi_string_list_add_printf(ret, "email=%s", priv->email);
   if (priv->jid)
     ret = avahi_string_list_add_printf(ret, "jid=%s", priv->jid);

   ret = avahi_string_list_add_printf(ret, "status=%s", 
                                salut_presence_statuses[self->status].txt_name);
   if (self->status_message)
     ret = avahi_string_list_add_printf(ret, "msg=%s", self->status_message);
   
   return ret;
}

static void
_avahi_presence_group_established(SalutAvahiEntryGroup *group, 
                                  SalutAvahiEntryGroupState state,
                                  gpointer data) {
  SalutSelf *self = SALUT_SELF(data);
  g_signal_emit(self, signals[ESTABLISHED], 0, NULL);
}

static void
_avahi_presence_group_failed(SalutAvahiEntryGroup *group, 
                             SalutAvahiEntryGroupState state,
                             gpointer data) {
  printf("FAILED\n");
}

/* Start announcing our presence on the network */
gboolean
salut_self_announce(SalutSelf *self, GError **error) {
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  AvahiStringList *txt_record = NULL;
  int port;
 

  port = self_start_listening(self);
  if (port < 0) {
    if (error != NULL) {
      *error = g_error_new(TP_ERRORS, TP_ERROR_NETWORK_ERROR, 
                           "Failed to start listening");
    }
    return FALSE;
  }

  priv->presence_group = salut_avahi_entry_group_new();

  g_signal_connect(priv->presence_group, 
                   "state-changed::established",
                   G_CALLBACK(_avahi_presence_group_established), self);
  g_signal_connect(priv->presence_group, 
                   "state-changed::collision",
                   G_CALLBACK(_avahi_presence_group_failed), self);
  g_signal_connect(priv->presence_group, 
                   "state-changed::failure",
                   G_CALLBACK(_avahi_presence_group_failed), self);

  if (!salut_avahi_entry_group_attach(priv->presence_group, 
                                      priv->client, error)) {
    goto error;
  };

  self->name = g_strdup_printf("%s@%s", g_get_user_name(),
                       avahi_client_get_host_name(priv->client->avahi_client));
  txt_record = create_txt_record(self, port);

  if ((priv->presence = 
          salut_avahi_entry_group_add_service_strlist(priv->presence_group,
                                                      self->name, 
                                                      "_presence._tcp",
                                                      port,
                                                      error,
                                                      txt_record)) == NULL) {
    goto error;
  }
  
  if (!salut_avahi_entry_group_commit(priv->presence_group, error)) {
    goto error;
  }

  avahi_string_list_free(txt_record);
  return TRUE;

error:
  avahi_string_list_free(txt_record);
  return FALSE;
}


gboolean 
salut_self_set_presence(SalutSelf *self, SalutPresenceId status, 
                        const gchar *message, GError **error) {
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);

  g_assert(status >= 0 && status < SALUT_PRESENCE_NR_PRESENCES);

  self->status = status;
  g_free(self->status_message);
  self->status_message = g_strdup(message);

  salut_avahi_entry_group_service_freeze(priv->presence);
  salut_avahi_entry_group_service_set(priv->presence, "status",
                               salut_presence_statuses[self->status].txt_name,
                               NULL);
  if (self->status_message) {
    salut_avahi_entry_group_service_set(priv->presence, "msg",
                                        self->status_message, NULL);
  } else {
    salut_avahi_entry_group_service_remove_key(priv->presence, "msg", NULL);
  }
  return salut_avahi_entry_group_service_thaw(priv->presence, error);
}

const gchar *
salut_self_get_alias(SalutSelf *self) {
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  if (priv->alias == NULL) {
    return self->name;
  }
  return priv->alias;
}

static gboolean
salut_self_publish_avatar(SalutSelf *self, guint8 *data,
                          gsize size, GError **error) {
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  gchar *name;
  gboolean ret;
  gboolean is_new = FALSE;
  name = g_strdup_printf("%s._presence._tcp.local", self->name);

  if (priv->avatar_group == NULL) {
    priv->avatar_group = salut_avahi_entry_group_new();
    salut_avahi_entry_group_attach(priv->avatar_group, priv->client, NULL);
    is_new = TRUE;
  }

  ret = salut_avahi_entry_group_add_record(priv->avatar_group,
                                           is_new ? 0 : AVAHI_PUBLISH_UPDATE,
                                           name, 0xA, 120, data, size, error);
  g_free(name);

  if (is_new) {
    salut_avahi_entry_group_commit(priv->avatar_group, error);
  }


  return ret;
}

static void
salut_self_remove_avatar(SalutSelf *self) {
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);

  DEBUG("Removing avatar");
  salut_avahi_entry_group_service_remove_key(priv->presence, "phsh", NULL);
  if (priv->avatar_group) {
    g_object_unref(priv->avatar_group);
    priv->avatar_group = NULL;
  }
}

gboolean
salut_self_set_avatar(SalutSelf *self, guint8 *data,
                      gsize size, GError **error) {
  gboolean ret = TRUE;
  GError *err = NULL;
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);

  if (size == 0) {
    salut_self_remove_avatar(self);
    return TRUE;
  }


  g_free(self->avatar_token);
  self->avatar_token = NULL;

  g_free(self->avatar);
  self->avatar = NULL;

  ret = salut_self_publish_avatar(self, data, size, &err);

  if (ret) {
    self->avatar = g_memdup(data, size);
    self->avatar_size = size;
    if (size > 0) {
      self->avatar_token = sha1_hex(data, size);
    }
    ret = salut_avahi_entry_group_service_set(priv->presence, "phsh",
                                              self->avatar_token,
                                              &err);
  }

  if (!ret) {
    salut_self_remove_avatar(self);
    g_set_error(error, TP_ERRORS, TP_ERROR_NETWORK_ERROR, err->message);
    g_error_free(err);
  }

  return ret;
}

#ifdef ENABLE_OLPC
gboolean
salut_self_set_olpc_properties(SalutSelf *self,
                               const gchar *key,
                               const gchar *color,
                               const gchar *jid,
                               GError **error)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  GError *err = NULL;

  salut_avahi_entry_group_service_freeze(priv->presence);
  if (key != NULL) {
    g_free(self->key);
    self->key = g_strdup(key);

    salut_avahi_entry_group_service_set(priv->presence, "olpc-key", 
        key, NULL);
  }
  if (color != NULL) {
    g_free(self->color);
    self->color = g_strdup(color);

    salut_avahi_entry_group_service_set(priv->presence, "olpc-color", 
        color, NULL);
  }
  if (jid != NULL)
    {
      g_free (priv->jid);
      priv->jid = g_strdup (jid);

      salut_avahi_entry_group_service_set (priv->presence, "jid",
          jid, NULL);
    }

  if (!salut_avahi_entry_group_service_thaw(priv->presence, &err)) {
    g_set_error(error, TP_ERRORS, TP_ERROR_NETWORK_ERROR, err->message);
    g_error_free(err);
    return FALSE;
  }
  return TRUE;
}

#endif /* ENABLE_OLPC */
