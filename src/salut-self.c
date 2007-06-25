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

#ifdef ENABLE_OLPC
#define KEY_SEGMENT_SIZE 200
#endif

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

#ifdef ENABLE_OLPC

typedef struct
{
  TpHandleRepoIface *room_repo;
  TpHandle room;
  SalutAvahiEntryGroup *group;
  SalutAvahiEntryGroupService *service;
} SalutOLPCActivity;

static void
activity_free (SalutOLPCActivity *activity)
{
  if (activity == NULL)
    return;
  if (activity->room != 0)
    tp_handle_unref (activity->room_repo, activity->room);
  activity->room = 0;
  activity->room_repo = NULL;
  if (activity->group != NULL)
    g_object_unref (activity->group);
  activity->group = NULL;
  g_slice_free (SalutOLPCActivity, activity);
}

static SalutOLPCActivity *
activity_new (TpHandleRepoIface *room_repo, TpHandle room,
              SalutAvahiClient *client, GError **error)
{
  SalutOLPCActivity *activity = g_slice_new0 (SalutOLPCActivity);

  g_return_val_if_fail (room_repo != NULL, NULL);
  g_return_val_if_fail (room != 0, NULL);
  g_return_val_if_fail (client != NULL, NULL);

  activity->room_repo = room_repo;
  activity->group = salut_avahi_entry_group_new ();
  tp_handle_ref (room_repo, room);
  activity->room = room;

  if (!salut_avahi_entry_group_attach (activity->group, client, error))
    {
      activity_free (activity);
      return NULL;
    }
  return activity;
}

#endif

struct _SalutSelfPrivate
{
  TpHandleRepoIface *room_repo;

  gchar *nickname;
  gchar *first_name;
  gchar *last_name;
  gchar *email;
  gchar *published_name;

  gchar *alias;

  GIOChannel *listener;
  guint io_watch_in;

  SalutAvahiClient *client;
  SalutAvahiEntryGroup *presence_group;
  SalutAvahiEntryGroupService *presence;
#ifdef ENABLE_OLPC
  /* dup'd activity ID -> SalutOLPCActivity */
  GHashTable *olpc_activities;
#endif

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
  obj->jid = NULL;
#ifdef ENABLE_OLPC
  obj->olpc_key = NULL;
  obj->olpc_color = NULL;
  obj->olpc_cur_act = NULL;
  obj->olpc_cur_act_room = 0;
#endif

  priv->first_name = NULL;
  priv->last_name = NULL;
  priv->email = NULL;
  priv->published_name = NULL;

  priv->client = NULL;
  priv->presence_group = NULL;
  priv->presence = NULL;
#ifdef ENABLE_OLPC
  priv->olpc_activities = g_hash_table_new_full (g_str_hash, g_str_equal,
      (GDestroyNotify) g_free, (GDestroyNotify) activity_free);
#endif
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

#ifdef ENABLE_OLPC
  if (priv->olpc_activities != NULL)
    g_hash_table_destroy (priv->olpc_activities);

  if (self->olpc_cur_act_room != 0)
    {
      tp_handle_unref (priv->room_repo, self->olpc_cur_act_room);
      self->olpc_cur_act_room = 0;
    }
#endif

  priv->room_repo = NULL;

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

  g_free (self->jid);
  g_free (self->name);

  g_free(priv->first_name);
  g_free(priv->last_name);
  g_free(priv->email);
  g_free(priv->published_name);
#ifdef ENABLE_OLPC
  if (self->olpc_key != NULL)
    g_array_free (self->olpc_key, TRUE);
  g_free (self->olpc_color);
  g_free (self->olpc_cur_act);
#endif

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
salut_self_new (SalutAvahiClient *client,
                TpHandleRepoIface *room_repo,
                const gchar *nickname,
                const gchar *first_name,
                const gchar *last_name,
                const gchar *jid,
                const gchar *email,
                const gchar *published_name,
                const GArray *olpc_key,
                const gchar *olpc_color) {
  SalutSelfPrivate *priv;
  GString *alias = NULL;

  g_assert(client != NULL);

  SalutSelf *ret = g_object_new(SALUT_TYPE_SELF, NULL);
  priv = SALUT_SELF_GET_PRIVATE (ret);

  priv->room_repo = room_repo;

  priv->client = client;
  g_object_ref(client);

  ret->jid = g_strdup(jid);

  priv->nickname = g_strdup(nickname);
  priv->first_name = g_strdup(first_name);
  priv->last_name = g_strdup(last_name);
  priv->email = g_strdup(email);
  priv->published_name = g_strdup(published_name);
#ifdef ENABLE_OLPC
  if (olpc_key != NULL)
    {
      ret->olpc_key = g_array_sized_new (FALSE, FALSE, sizeof (guint8),
          olpc_key->len);
      g_array_append_vals (ret->olpc_key, olpc_key->data, olpc_key->len);
    }
  ret->olpc_color = g_strdup (olpc_color);
  priv->alias = NULL;
#endif

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
        alias = g_string_new (last_name);
      } 
    }

    if (alias != NULL && *(alias->str) != '\0') {
      priv->alias = alias->str;
      g_string_free(alias, FALSE);
    } else {
      g_string_free(alias, TRUE);
    }
  }

  if (published_name == NULL) {
    priv->published_name = g_strdup(g_get_user_name());
  }

  return ret;
}

static int
self_try_listening_on_port(SalutSelf *self, int port) {
  int fd = -1, ret, yes = 1;
  struct addrinfo req, *ans = NULL;
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

  freeaddrinfo (ans);
  return fd;
error:
  if (fd > 0) {
    close(fd);
  }
  if (ans != NULL)
    freeaddrinfo (ans);
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
   if (self->jid)
     ret = avahi_string_list_add_printf (ret, "jid=%s", self->jid);

#ifdef ENABLE_OLPC
  if (self->olpc_color)
    ret = avahi_string_list_add_printf (ret, "olpc-color=%s",
         self->olpc_color);
  if (self->olpc_key)
    {
      uint8_t *key = (uint8_t *) self->olpc_key->data;
      size_t key_len = self->olpc_key->len;
      guint i = 0;

      while (key_len > 0)
        {
          size_t step = MIN (key_len, KEY_SEGMENT_SIZE);
          gchar *name = g_strdup_printf ("olpc-key-part%u", i);

          ret = avahi_string_list_add_pair_arbitrary (ret, name, key, step);
          key += step;
          key_len -= step;
          i++;
        }
    }
#endif

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

  self->name = g_strdup_printf("%s@%s", priv->published_name,
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

gboolean
salut_self_set_alias (SalutSelf *self, const gchar *alias, GError **error)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  gboolean ret;
  GError *err = NULL;

  g_free (priv->alias);
  g_free (priv->nickname);
  priv->alias = g_strdup (alias);
  priv->nickname = g_strdup (alias);

  ret = salut_avahi_entry_group_service_set (priv->presence, "nick",
      priv->alias, &err);
  if (!ret)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR, err->message);
      g_error_free (err);
    }
  return ret;
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

static gboolean
salut_self_add_olpc_activity (SalutSelf *self,
                              const gchar *activity_id,
                              TpHandle room,
                              GError **error)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  SalutOLPCActivity *activity;
  AvahiStringList *txt_record;
  gchar *name;
  const gchar *room_name;

  g_return_val_if_fail (activity_id != NULL, FALSE);
  g_return_val_if_fail (room != 0, FALSE);

  room_name = tp_handle_inspect (priv->room_repo, room);
  /* caller should already have validated this */
  g_return_val_if_fail (room_name != NULL, FALSE);

  activity = activity_new (priv->room_repo, room, priv->client, error);
  if (activity == NULL)
    return FALSE;

  /* FIXME: require that the room name does not contain ':'? That would
   * make sure we avoid collisions, unless someone is deliberately
   * interfering with us (not that there's much you can do about that
   * on-link) */

  name = g_strdup_printf ("%s:%s@%s", room_name, priv->published_name,
      avahi_client_get_host_name (priv->client->avahi_client));

  txt_record = avahi_string_list_new ("txtvers=1", NULL);
  txt_record = avahi_string_list_add_printf (txt_record, "room=%s", room_name);
  txt_record = avahi_string_list_add_printf (txt_record, "activity-id=%s",
      activity_id);

  activity->service = salut_avahi_entry_group_add_service_strlist
      (activity->group, name, "_olpc-activity._udp", 0, error, txt_record);

  g_free (name);
  avahi_string_list_free (txt_record);

  if (activity->service == NULL)
    {
      activity_free (activity);
      return FALSE;
    }

  if (!salut_avahi_entry_group_commit (activity->group, error))
    {
      activity_free (activity);
      return FALSE;
    }

  tp_handle_ref (priv->room_repo, room);

  /* if there was an old activity with the same ID and a different room
   * (shouldn't happen anyway), this deletes it */
  g_hash_table_replace (priv->olpc_activities, g_strdup (activity_id),
      activity);
  return TRUE;
}

struct _set_olpc_activities_ctx
{
  SalutSelf *self;
  TpHandleRepoIface *room_repo;
  GHashTable *olpc_activities;
  GHashTable *act_id_to_room;
  GError **error;
};

static void
_set_olpc_activities_add (gpointer key, gpointer value, gpointer user_data)
{
  struct _set_olpc_activities_ctx *data = user_data;
  SalutOLPCActivity *activity;

  if (*(data->error) != NULL)
    {
      /* we already lost */
      return;
    }

  /* add the activity service if it's not in data->olpc_activities, or if
   * the room corresponding to the activity-ID has changed (shouldn't happen
   * in practice) */
  activity = g_hash_table_lookup (data->olpc_activities, key);
  if (activity == NULL || GPOINTER_TO_UINT (value) != activity->room)
    {
      salut_self_add_olpc_activity (data->self, key, GPOINTER_TO_UINT (value),
          data->error);
    }
}

static gboolean
_set_olpc_activities_delete (gpointer key, gpointer value, gpointer user_data)
{
  struct _set_olpc_activities_ctx *data = user_data;

  /* delete the activity service if it's not in data->act_id_to_room */
  return (g_hash_table_lookup (data->act_id_to_room, key) == NULL);
}

gboolean
salut_self_set_olpc_activities (SalutSelf *self,
                                GHashTable *act_id_to_room,
                                GError **error)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  struct _set_olpc_activities_ctx data = { self, priv->room_repo,
      priv->olpc_activities, act_id_to_room, error };

  /* delete any which aren't in room_to_act_id. Can't fail */
  g_hash_table_foreach_remove (priv->olpc_activities,
      _set_olpc_activities_delete, &data);

  /* add any which aren't in olpc_activities */
  g_hash_table_foreach (act_id_to_room, _set_olpc_activities_add, &data);

  return (*error == NULL);
}

gboolean
salut_self_set_olpc_current_activity (SalutSelf *self,
                                      const gchar *id,
                                      TpHandle room,
                                      GError **error)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  gboolean ret;
  GError *err = NULL;
  const gchar *room_name;

  g_return_val_if_fail (id != NULL, FALSE);

  /* if one of id and room is empty, require the other to be */
  if (room == 0)
    {
      room_name = "";

      if (id[0] != '\0')
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "In SetCurrentActivity, activity ID must be \"\" if room handle "
              "is 0");
          return FALSE;
        }
    }
  else
    {
      room_name = tp_handle_inspect (priv->room_repo, room);

      if (id[0] == '\0')
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "In SetCurrentActivity, activity ID must not be \"\" if room "
              "handle is non-zero");
          return FALSE;
        }
    }

  g_free (self->olpc_cur_act);
  self->olpc_cur_act = g_strdup (id);

  if (self->olpc_cur_act_room != 0)
    tp_handle_unref (priv->room_repo, self->olpc_cur_act_room);
  self->olpc_cur_act_room = room;
  if (room != 0)
    tp_handle_ref (priv->room_repo, room);

  salut_avahi_entry_group_service_freeze(priv->presence);

  salut_avahi_entry_group_service_set (priv->presence,
      "olpc-current-activity", self->olpc_cur_act, NULL);

  salut_avahi_entry_group_service_set (priv->presence,
      "olpc-current-activity-room", room_name, NULL);

  ret = salut_avahi_entry_group_service_thaw(priv->presence, &err);
  if (!ret)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR, err->message);
      g_error_free (err);
    }
  return ret;
}

gboolean
salut_self_set_olpc_properties (SalutSelf *self,
                                const GArray *key,
                                const gchar *color,
                                const gchar *jid,
                                GError **error)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  GError *err = NULL;

  salut_avahi_entry_group_service_freeze(priv->presence);
  if (key != NULL)
    {
      size_t key_len = key->len;
      const guint8 *key_data = (const guint8 *) key->data;
      guint i;
      guint to_remove;

      if (self->olpc_key == NULL)
        {
          to_remove = 0;
          self->olpc_key = g_array_sized_new (FALSE, FALSE, sizeof (guint8),
              key->len);
        }
      else
        {
          to_remove = (self->olpc_key->len + KEY_SEGMENT_SIZE - 1) /
                      KEY_SEGMENT_SIZE;
          g_array_remove_range (self->olpc_key, 0, self->olpc_key->len);
        }
      g_array_append_vals (self->olpc_key, key->data, key->len);

      i = 0;
      while (key_len > 0)
        {
          size_t step = MIN (key_len, KEY_SEGMENT_SIZE);
          gchar *name = g_strdup_printf ("olpc-key-part%u", i);

          salut_avahi_entry_group_service_set_arbitrary (priv->presence, name,
              key_data, step, NULL);
          g_free (name);

          key_data += step;
          key_len -= step;
          i++;
        }

      /* if the new key is shorter than the old, clean up any stray segments */
      while (i < to_remove)
        {
          gchar *name = g_strdup_printf ("olpc-key-part%u", i);

          salut_avahi_entry_group_service_remove_key (priv->presence, name,
              NULL);
          g_free (name);

          i++;
        }
    }
  if (color != NULL)
    {
      g_free (self->olpc_color);
      self->olpc_color = g_strdup (color);

      salut_avahi_entry_group_service_set (priv->presence, "olpc-color",
          color, NULL);
    }
  if (jid != NULL)
    {
      g_free (self->jid);
      self->jid = g_strdup (jid);

      salut_avahi_entry_group_service_set (priv->presence, "jid",
          jid, NULL);
    }

  if (!salut_avahi_entry_group_service_thaw(priv->presence, &err))
    {
      g_set_error(error, TP_ERRORS, TP_ERROR_NETWORK_ERROR, err->message);
      g_error_free(err);
      return FALSE;
    }
  return TRUE;
}

#endif /* ENABLE_OLPC */
