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
#include <avahi-gobject/ga-entry-group.h>

#include "salut-self.h"

#include <gibber/gibber-linklocal-transport.h>
#include <gibber/gibber-namespaces.h>
#include <gibber/gibber-muc-connection.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/util.h>

#include "salut-contact-manager.h"
#include "salut-util.h"
#include "salut-muc-manager.h"
#include "salut-xmpp-connection-manager.h"

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
  /* group and service can be NULL if the activity is private */
  GaEntryGroup *group;
  GaEntryGroupService *service;
  SalutMucChannel *muc;
  gchar *activity_id;
  gchar *color;
  gchar *name;
  gchar *type;
  gchar *tags;
  gboolean is_private;
  /* Handles of contacts we invited to join this activity */
  TpHandleSet *invited;
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

  if (activity->muc != NULL)
    g_object_unref (activity->muc);
  activity->muc = NULL;

  g_free (activity->activity_id);
  g_free (activity->color);
  g_free (activity->name);
  g_free (activity->type);
  g_free (activity->tags);
  tp_handle_set_destroy (activity->invited);

  g_slice_free (SalutOLPCActivity, activity);
}

static SalutOLPCActivity *
activity_new (TpHandleRepoIface *room_repo,
              TpHandleRepoIface *contact_repo,
              TpHandle room,
              GError **error)
{
  SalutOLPCActivity *activity = g_slice_new0 (SalutOLPCActivity);

  g_return_val_if_fail (room_repo != NULL, NULL);
  g_return_val_if_fail (room != 0, NULL);

  activity->room_repo = room_repo;
  tp_handle_ref (room_repo, room);
  activity->room = room;

  activity->is_private = TRUE;
  activity->invited = tp_handle_set_new (contact_repo);

  return activity;
}

#endif

struct _SalutSelfPrivate
{
  SalutConnection *connection;
  SalutMucManager *muc_manager;
  SalutContactManager *contact_manager;
  SalutXmppConnectionManager *xmpp_connection_manager;
  TpHandleRepoIface *room_repo;

  gchar *nickname;
  gchar *first_name;
  gchar *last_name;
  gchar *email;
  gchar *published_name;

  gchar *alias;

  GIOChannel *listener;
  guint io_watch_in;

  GaClient *client;
  GaEntryGroup *presence_group;
  GaEntryGroupService *presence;
#ifdef ENABLE_OLPC
  /* handle owned by the SalutOLPCActivity -> SalutOLPCActivity */
  GHashTable *olpc_activities;
#endif

  GaEntryGroup *avatar_group;

  gboolean dispose_has_run;
};

#ifdef ENABLE_OLPC
void
contact_manager_contact_change_cb (SalutContactManager *mgr,
    SalutContact *contact, int changes, gpointer user_data);
#endif

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
  priv->olpc_activities = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) activity_free);
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

  if (priv->contact_manager != NULL)
    {
      g_object_unref (priv->contact_manager);
      priv->contact_manager = NULL;
    }

  if (priv->muc_manager != NULL)
    {
      g_object_unref (priv->muc_manager);
      priv->muc_manager = NULL;
    }

  if (priv->xmpp_connection_manager != NULL)
    {
      g_object_unref (priv->xmpp_connection_manager);
      priv->xmpp_connection_manager = NULL;
    }

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
  g_free (priv->alias);
#ifdef ENABLE_OLPC
  if (self->olpc_key != NULL)
    g_array_free (self->olpc_key, TRUE);
  g_free (self->olpc_color);
  g_free (self->olpc_cur_act);
#endif

  G_OBJECT_CLASS (salut_self_parent_class)->finalize (object);
}

SalutSelf *
salut_self_new (SalutConnection *connection,
                GaClient *client,
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

  g_assert(client != NULL);

  SalutSelf *ret = g_object_new(SALUT_TYPE_SELF, NULL);
  priv = SALUT_SELF_GET_PRIVATE (ret);

  priv->connection = connection;
  g_object_get (connection,
      "contact-manager", &(priv->contact_manager),
      "muc-manager", &(priv->muc_manager),
      "xmpp-connection-manager", &(priv->xmpp_connection_manager),
      NULL);
  g_assert (priv->contact_manager != NULL);
  g_assert (priv->muc_manager != NULL);
  g_assert (priv->xmpp_connection_manager != NULL);

  priv->room_repo = room_repo;

  priv->client = client;
  g_object_ref(client);

  ret->jid = g_strdup(jid);

  priv->nickname = g_strdup(nickname);
  priv->first_name = g_strdup(first_name);
  priv->last_name = g_strdup(last_name);
  priv->email = g_strdup(email);
  priv->published_name = g_strdup(published_name);
  priv->alias = NULL;
#ifdef ENABLE_OLPC
  if (olpc_key != NULL)
    {
      ret->olpc_key = g_array_sized_new (FALSE, FALSE, sizeof (guint8),
          olpc_key->len);
      g_array_append_vals (ret->olpc_key, olpc_key->data, olpc_key->len);
    }
  ret->olpc_color = g_strdup (olpc_color);
#endif

  /* Prefer using the nickname as alias */
  if (nickname != NULL)
    {
      priv->alias = g_strdup (nickname);
    }
  else
    {
      if (first_name != NULL)
        {
          if (last_name != NULL)
            priv->alias = g_strdup_printf ("%s %s", first_name, last_name);
          else
            priv->alias = g_strdup (first_name);
        }
      else if (last_name != NULL)
        {
          priv->alias = g_strdup (last_name);
        }
    }

  if (published_name == NULL) {
    priv->published_name = g_strdup(g_get_user_name());
  }

#ifdef ENABLE_OLPC
  g_signal_connect (priv->contact_manager, "contact-change",
      G_CALLBACK (contact_manager_contact_change_cb), ret);
#endif

  return ret;
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
       salut_presence_status_txt_names[self->status]);
   if (self->status_message)
     ret = avahi_string_list_add_printf(ret, "msg=%s", self->status_message);

   return ret;
}

static void
_avahi_presence_group_established(GaEntryGroup *group,
                                  GaEntryGroupState state,
                                  gpointer data) {
  SalutSelf *self = SALUT_SELF(data);
  g_signal_emit(self, signals[ESTABLISHED], 0, NULL);
}

static void
_avahi_presence_group_failed(GaEntryGroup *group,
                             GaEntryGroupState state,
                             gpointer data) {
  printf("FAILED\n");
}

/* Start announcing our presence on the network */
gboolean
salut_self_announce (SalutSelf *self,
                     gint port,
                     GError **error)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  AvahiStringList *txt_record = NULL;

  priv->presence_group = ga_entry_group_new();

  g_signal_connect(priv->presence_group,
                   "state-changed::established",
                   G_CALLBACK(_avahi_presence_group_established), self);
  g_signal_connect(priv->presence_group,
                   "state-changed::collision",
                   G_CALLBACK(_avahi_presence_group_failed), self);
  g_signal_connect(priv->presence_group,
                   "state-changed::failure",
                   G_CALLBACK(_avahi_presence_group_failed), self);

  if (!ga_entry_group_attach(priv->presence_group,
                                      priv->client, error)) {
    goto error;
  };

  self->name = g_strdup_printf("%s@%s", priv->published_name,
                       avahi_client_get_host_name(priv->client->avahi_client));
  txt_record = create_txt_record(self, port);

  if ((priv->presence =
          ga_entry_group_add_service_strlist(priv->presence_group,
                                                      self->name,
                                                      SALUT_DNSSD_PRESENCE,
                                                      port,
                                                      error,
                                                      txt_record)) == NULL) {
    goto error;
  }

  if (!ga_entry_group_commit(priv->presence_group, error)) {
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

  ga_entry_group_service_freeze(priv->presence);
  ga_entry_group_service_set(priv->presence, "status",
                               salut_presence_status_txt_names[self->status],
                               NULL);
  if (self->status_message) {
    ga_entry_group_service_set(priv->presence, "msg",
                                        self->status_message, NULL);
  } else {
    ga_entry_group_service_remove_key(priv->presence, "msg", NULL);
  }
  return ga_entry_group_service_thaw(priv->presence, error);
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

  ret = ga_entry_group_service_set (priv->presence, "nick",
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
  name = g_strdup_printf("%s." SALUT_DNSSD_PRESENCE ".local", self->name);

  if (priv->avatar_group == NULL) {
    priv->avatar_group = ga_entry_group_new();
    ga_entry_group_attach(priv->avatar_group, priv->client, NULL);
    is_new = TRUE;
  }

  ret = ga_entry_group_add_record(priv->avatar_group,
                                           is_new ? 0 : AVAHI_PUBLISH_UPDATE,
                                           name, 0xA, 120, data, size, error);
  g_free(name);

  if (is_new) {
    ga_entry_group_commit(priv->avatar_group, error);
  }


  return ret;
}

static void
salut_self_remove_avatar(SalutSelf *self) {
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);

  DEBUG("Removing avatar");
  ga_entry_group_service_remove_key(priv->presence, "phsh", NULL);
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

  g_free(self->avatar_token);
  self->avatar_token = NULL;

  g_free(self->avatar);
  self->avatar = NULL;

  self->avatar_size = 0;

  if (size == 0) {
    self->avatar_token = g_strdup("");
    salut_self_remove_avatar (self);
    return TRUE;
  }


  ret = salut_self_publish_avatar(self, data, size, &err);

  if (ret) {
    self->avatar = g_memdup(data, size);
    self->avatar_size = size;
    if (size > 0) {
      self->avatar_token = sha1_hex(data, size);
    }
    ret = ga_entry_group_service_set(priv->presence, "phsh",
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
activity_is_announced (SalutOLPCActivity *activity)
{
  g_return_val_if_fail (activity != NULL, FALSE);
  return (activity->service != NULL && activity->group != NULL);
}

static gboolean
update_activity_service (SalutOLPCActivity *activity,
                         GError **error)
{
  GError *err = NULL;
  g_return_val_if_fail (activity_is_announced (activity), FALSE);

  ga_entry_group_service_freeze (activity->service);

  if (activity->name != NULL)
    ga_entry_group_service_set (activity->service, "name",
        activity->name, NULL);

  if (activity->color != NULL)
    ga_entry_group_service_set (activity->service, "color",
        activity->color, NULL);

  if (activity->type != NULL)
    ga_entry_group_service_set (activity->service, "type",
        activity->type, NULL);

  if (activity->tags != NULL)
    ga_entry_group_service_set (activity->service, "tags",
        activity->tags, NULL);

  if (!ga_entry_group_service_thaw (activity->service, &err))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR, err->message);
      g_error_free (err);
      return FALSE;
    }

  return TRUE;
}

static gboolean
announce_activity (SalutSelf *self,
                   SalutOLPCActivity *activity,
                   GError **error)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  const gchar *room_name;
  gchar *name;
  AvahiStringList *txt_record;

  g_return_val_if_fail (!activity_is_announced (activity), FALSE);
  g_return_val_if_fail (!activity->is_private, FALSE);

  room_name = tp_handle_inspect (priv->room_repo, activity->room);
  /* caller should already have validated this */
  g_return_val_if_fail (room_name != NULL, FALSE);

  activity->group = ga_entry_group_new ();
  if (!ga_entry_group_attach (activity->group, priv->client, error))
    return FALSE;

  name = g_strdup_printf ("%s:%s@%s", room_name, priv->published_name,
      avahi_client_get_host_name (priv->client->avahi_client));

  txt_record = avahi_string_list_new ("txtvers=0", NULL);
  txt_record = avahi_string_list_add_printf (txt_record, "room=%s", room_name);
  if (activity->activity_id != NULL)
    txt_record = avahi_string_list_add_printf (txt_record, "activity-id=%s",
        activity->activity_id);

  activity->service = ga_entry_group_add_service_strlist
      (activity->group, name, SALUT_DNSSD_OLPC_ACTIVITY, 0, error, txt_record);

  DEBUG ("announce activity %s", name);
  g_free (name);
  avahi_string_list_free (txt_record);

  if (activity->service == NULL)
    return FALSE;

  if (!ga_entry_group_commit (activity->group, error))
    return FALSE;

  /* announce activities properties */
  if (!update_activity_service (activity, error))
    return FALSE;

  return TRUE;
}

static gboolean
stop_announce_activity (SalutSelf *self,
                        SalutOLPCActivity *activity,
                        GError **error)
{

  g_return_val_if_fail (activity_is_announced (activity), FALSE);
  g_return_val_if_fail (activity->is_private, FALSE);

  g_object_unref (activity->group);
  activity->group = NULL;
  activity->service = NULL;

  DEBUG ("stop announce activity %s", activity->activity_id);
  return TRUE;
}

typedef struct
{
  SalutSelf *self;
  SalutContact *contact;
  GibberXmppStanza *msg;
} pending_connection_for_uninvite_ctx;

static void
xmpp_connection_manager_new_connection_cb (SalutXmppConnectionManager *mgr,
                                           GibberXmppConnection *connection,
                                           SalutContact *contact,
                                           gpointer user_data)
{
  pending_connection_for_uninvite_ctx *ctx =
    (pending_connection_for_uninvite_ctx *) user_data;

  if (ctx->contact != contact)
    /* Not the connection we are waiting for */
    return;

  DEBUG ("got awaited connection with %s. Send uninvite", contact->name);

  gibber_xmpp_connection_send (connection, ctx->msg, NULL);

  g_signal_handlers_disconnect_matched (mgr, G_SIGNAL_MATCH_DATA, 0, 0, NULL,
      NULL, ctx);

  g_slice_free (pending_connection_for_uninvite_ctx, ctx);
}

static void
xmpp_connection_manager_connection_failed_cb (SalutXmppConnectionManager *mgr,
                                              GibberXmppConnection *connection,
                                              SalutContact *contact,
                                              GQuark domain,
                                              gint code,
                                              gchar *message,
                                              gpointer user_data)
{
  pending_connection_for_uninvite_ctx *ctx =
    (pending_connection_for_uninvite_ctx *) user_data;

  if (ctx->contact != contact)
    /* Not the connection we are waiting for */
    return;

  DEBUG ("awaited connection with %s failed: %s. Can't send uninvite",
    contact->name, message);

  g_signal_handlers_disconnect_matched (mgr, G_SIGNAL_MATCH_DATA, 0, 0, NULL,
      NULL, ctx);

  g_slice_free (pending_connection_for_uninvite_ctx, ctx);
}

static void
revoke_invitations (SalutSelf *self,
                    SalutOLPCActivity *activity)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  GibberXmppStanza *msg;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->connection, TP_HANDLE_TYPE_CONTACT);
  TpIntSetIter iter = TP_INTSET_ITER_INIT (tp_handle_set_peek (
        activity->invited));

  if (tp_handle_set_size (activity->invited) <= 0)
    return;

  msg = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_MESSAGE,
      GIBBER_STANZA_SUB_TYPE_NONE,
      priv->connection->name, NULL,
      GIBBER_NODE, "uninvite",
        GIBBER_NODE_XMLNS, GIBBER_TELEPATHY_NS_OLPC_ACTIVITY_PROPS,
        GIBBER_NODE_ATTRIBUTE, "room", tp_handle_inspect (activity->room_repo,
          activity->room),
        GIBBER_NODE_ATTRIBUTE, "id", activity->activity_id,
      GIBBER_NODE_END, GIBBER_STANZA_END);

  DEBUG ("revoke invitations for activity %s", activity->activity_id);
  while (tp_intset_iter_next (&iter))
    {
      TpHandle contact_handle;
      SalutContact *contact;
      SalutXmppConnectionManagerRequestConnectionResult request_result;
      GibberXmppConnection *connection = NULL;
      const gchar *to;

      contact_handle = iter.element;
      contact = salut_contact_manager_get_contact (priv->contact_manager,
          contact_handle);
      if (contact == NULL)
        {
          DEBUG ("Can't find contact %d", contact_handle);
          continue;
        }

      to = tp_handle_inspect (contact_repo, contact_handle);
      gibber_xmpp_node_set_attribute (msg->node, "to", to);

      request_result = salut_xmpp_connection_manager_request_connection (
          priv->xmpp_connection_manager, contact, &connection, NULL);

      if (request_result ==
          SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_FAILURE)
        {
          DEBUG ("request connection to %s failed", to);
        }
      else if (request_result ==
          SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_DONE)
        {
          DEBUG ("send uninvite to %s", to);
          if (!gibber_xmpp_connection_send (connection, msg, NULL))
            DEBUG ("can't send uninvite to %s", to);
        }
      else
        {
          pending_connection_for_uninvite_ctx *ctx;

          ctx = g_slice_new (pending_connection_for_uninvite_ctx);
          g_signal_connect (priv->xmpp_connection_manager, "new-connection",
              G_CALLBACK (xmpp_connection_manager_new_connection_cb), ctx);
          g_signal_connect (priv->xmpp_connection_manager, "connection-failed",
              G_CALLBACK (xmpp_connection_manager_connection_failed_cb), ctx);
        }
    }

  g_object_unref (msg);
}

static SalutOLPCActivity *
salut_self_add_olpc_activity (SalutSelf *self,
                              const gchar *activity_id,
                              TpHandle room,
                              GError **error)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  SalutOLPCActivity *activity;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->connection, TP_HANDLE_TYPE_CONTACT);

  g_return_val_if_fail (activity_id != NULL, NULL);
  g_return_val_if_fail (room != 0, NULL);

  if (strchr (activity_id, ':') != NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Activity IDs may not contain ':'");
      return NULL;
    }

  activity = activity_new (priv->room_repo, contact_repo, room, error);
  if (activity == NULL)
    return NULL;

  activity->muc = salut_muc_manager_get_text_channel (priv->muc_manager, room);
  if (activity->muc == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Can't find muc channel for room %s", tp_handle_inspect (
            priv->room_repo, room));
      activity_free (activity);
      return NULL;
    }

  activity->activity_id = g_strdup (activity_id);

  tp_handle_ref (priv->room_repo, room);
  activity->room = room;

  g_hash_table_insert (priv->olpc_activities, GUINT_TO_POINTER (room),
      activity);
  return activity;
}

static gboolean
update_activity (SalutOLPCActivity *activity,
                 const gchar *name,
                 const gchar *type,
                 const gchar *color,
                 const gchar *tags)
{
  gboolean changed = FALSE;

  if (name != NULL && tp_strdiff (activity->name, name))
    {
      g_free (activity->name);
      activity->name = g_strdup (name);
      changed = TRUE;
    }

  if (type != NULL && tp_strdiff (activity->type, type))
    {
      g_free (activity->type);
      activity->type = g_strdup (type);
      changed = TRUE;
    }

  if (color != NULL && tp_strdiff (activity->color, color))
    {
      g_free (activity->color);
      activity->color = g_strdup (color);
      changed = TRUE;
    }

  if (tags != NULL && tp_strdiff (activity->tags, tags))
    {
      g_free (activity->tags);
      activity->tags = g_strdup (tags);
      changed = TRUE;
    }

  return changed;
}

struct _set_olpc_activities_ctx
{
  SalutSelf *self;
  TpHandleRepoIface *room_repo;
  GHashTable *olpc_activities;
  GHashTable *room_to_act_id;
  GError **error;
};

static void
_set_olpc_activities_add (gpointer key, gpointer value, gpointer user_data)
{
  struct _set_olpc_activities_ctx *data = user_data;
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (data->self);
  SalutOLPCActivity *activity;
  gboolean need_update = FALSE;

  if (*(data->error) != NULL)
    {
      /* we already lost */
      return;
    }

  activity = g_hash_table_lookup (data->olpc_activities, key);
  if (activity == NULL)
    {
      gboolean is_private = TRUE;
      const gchar *color = NULL, *name = NULL, *type = NULL, *tags = NULL;

      /* add the activity service if it's not in data->olpc_activities */
      activity = salut_self_add_olpc_activity (data->self, value,
          GPOINTER_TO_UINT (key), data->error);

      if (activity == NULL)
        return;

      if (salut_contact_manager_merge_olpc_activity_properties (
          priv->contact_manager, GPOINTER_TO_UINT (key), &color, &name, &type,
          &tags, &is_private))
        {
          update_activity (activity, name, type, color, tags);
          activity->is_private = is_private;
        }
    }
  /* activity was already known */
  else if (tp_strdiff (value, activity->activity_id))
    {
      /* if the user is claiming that the activity ID of a room has changed,
       * believe them... */

      g_free (activity->activity_id);
      activity->activity_id = g_strdup (value);
      need_update = TRUE;
    }

  if (activity->is_private)
    /* activity is private, no need to announce or update it */
    return;

  if (!activity_is_announced (activity))
    {
      GError *e;

      if (!announce_activity (data->self, activity, &e))
        {
          DEBUG ("can't announce activity: %s", e->message);
          g_set_error (data->error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
              e->message);
          g_error_free (e);
          g_hash_table_remove (data->olpc_activities, key);
          return;
        }
    }
  else if (need_update)
    {
      GError *e;

      if (!ga_entry_group_service_set (activity->service,
            "activity-id", value, &e))
        {
          g_set_error (data->error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
              e->message);
          g_error_free (e);
          return;
        }
    }
}

static gboolean
_set_olpc_activities_delete (gpointer key, gpointer value, gpointer user_data)
{
  SalutOLPCActivity *activity = (SalutOLPCActivity *) value;
  struct _set_olpc_activities_ctx *data = user_data;
  gboolean remove;

  /* delete the activity service if it's not in data->room_to_act_id */
  remove = (g_hash_table_lookup (data->room_to_act_id, key) == NULL);

  if (remove)
    {
      revoke_invitations (data->self, activity);
    }

  return remove;
}

gboolean
salut_self_set_olpc_activities (SalutSelf *self,
                                GHashTable *room_to_act_id,
                                GError **error)
{
  GError *e = NULL;
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  struct _set_olpc_activities_ctx data = { self, priv->room_repo,
      priv->olpc_activities, room_to_act_id, &e };

  /* delete any which aren't in room_to_act_id. Can't fail */
  g_hash_table_foreach_remove (priv->olpc_activities,
      _set_olpc_activities_delete, &data);

  /* add any which aren't in olpc_activities */
  g_hash_table_foreach (room_to_act_id, _set_olpc_activities_add, &data);

  if (error != NULL)
    *error = e;
  return (e == NULL);
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

  ga_entry_group_service_freeze(priv->presence);

  ga_entry_group_service_set (priv->presence,
      "olpc-current-activity", self->olpc_cur_act, NULL);

  ga_entry_group_service_set (priv->presence,
      "olpc-current-activity-room", room_name, NULL);

  ret = ga_entry_group_service_thaw(priv->presence, &err);
  if (!ret)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR, err->message);
      g_error_free (err);
    }
  return ret;
}

gboolean
salut_self_merge_olpc_activity_properties (SalutSelf *self,
                                           TpHandle handle,
                                           const gchar **color,
                                           const gchar **name,
                                           const gchar **type,
                                           const gchar **tags,
                                           gboolean *is_private)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  SalutOLPCActivity *activity = g_hash_table_lookup (priv->olpc_activities,
      GUINT_TO_POINTER (handle));

  if (activity == NULL)
    return FALSE;

  if (activity->color != NULL && color != NULL)
    *color = activity->color;
  if (activity->name != NULL && name != NULL)
    *name = activity->name;
  if (activity->type != NULL && type != NULL)
    *type = activity->type;
  if (activity->tags != NULL && tags != NULL)
    *tags = activity->tags;
  *is_private = activity->is_private;
  return TRUE;
}

typedef struct
{
  SalutSelf *self;
  SalutOLPCActivity *activity;
} foreach_resend_invite_ctx;

static void
resend_invite_foreach (TpHandleSet *set,
                       TpHandle handle,
                       gpointer user_data)
{
  GError *error = NULL;
  foreach_resend_invite_ctx *ctx = (foreach_resend_invite_ctx *) user_data;

  if (!salut_muc_channel_send_invitation (ctx->activity->muc, handle,
        "OLPC activity properties update", &error))
    {
      DEBUG ("failed to re-invite contact %d to activity %s", handle,
          ctx->activity->activity_id);
    }
}

static void
resend_invite (SalutSelf *self,
               SalutOLPCActivity *activity)
{
  foreach_resend_invite_ctx ctx;

  /* Resend pending invitations so contacts will know about new properties */
  ctx.self = self;
  ctx.activity = activity;
  tp_handle_set_foreach (activity->invited, resend_invite_foreach, &ctx);
}

/* FIXME: we should share this code with salut-connection.c */
static GHashTable *
create_properties_table (SalutOLPCActivity *activity)
{
  GHashTable *properties;
  GValue *val;

  properties = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);

  if (activity->color != NULL)
    {
      val = tp_g_value_slice_new (G_TYPE_STRING);
      g_value_set_static_string (val, activity->color);
      g_hash_table_insert (properties, "color", val);
    }
  if (activity->name != NULL)
    {
      val = tp_g_value_slice_new (G_TYPE_STRING);
      g_value_set_static_string (val, activity->name);
      g_hash_table_insert (properties, "name", val);
    }
  if (activity->type != NULL)
    {
      val = tp_g_value_slice_new (G_TYPE_STRING);
      g_value_set_static_string (val, activity->type);
      g_hash_table_insert (properties, "type", val);
    }
  if (activity->tags != NULL)
    {
      val = tp_g_value_slice_new (G_TYPE_STRING);
      g_value_set_static_string (val, activity->tags);
      g_hash_table_insert (properties, "tags", val);
    }

  val = tp_g_value_slice_new (G_TYPE_BOOLEAN);
  g_value_set_boolean (val, activity->is_private);
  g_hash_table_insert (properties, "private", val);

  return properties;
}

static gboolean
send_olpc_activity_properties_changes_msg (SalutSelf *self,
                                           SalutOLPCActivity *activity,
                                           GError **error)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  GHashTable *properties;
  GValue *activity_id_val;
  GibberXmppStanza *stanza;
  GibberXmppNode *properties_node;
  gchar *muc_name;
  GibberMucConnection *muc_connection;
  gboolean result;
  GError *err = NULL;

  g_assert (activity->muc != NULL);

  g_object_get (activity->muc,
      "name", &muc_name,
      "muc-connection", &muc_connection,
      NULL);

  if (muc_connection->state != GIBBER_MUC_CONNECTION_CONNECTED)
    {
      DEBUG ("Muc connection not connected yet. Drop activity change message");
      g_object_unref (muc_connection);
      g_free (muc_name);
      return TRUE;
    }

  properties = create_properties_table (activity);

  /* add the activity id */
  activity_id_val = g_slice_new0 (GValue);
  g_value_init (activity_id_val, G_TYPE_STRING);
  g_value_set_static_string (activity_id_val, activity->activity_id);
  g_hash_table_insert (properties, "id", activity_id_val);

  stanza = gibber_xmpp_stanza_build (GIBBER_STANZA_TYPE_MESSAGE,
      GIBBER_STANZA_SUB_TYPE_GROUPCHAT,
      priv->connection->name, muc_name,
      GIBBER_NODE, "properties",
        GIBBER_NODE_XMLNS, GIBBER_TELEPATHY_NS_OLPC_ACTIVITY_PROPS,
      GIBBER_NODE_END, GIBBER_STANZA_END);

  properties_node = gibber_xmpp_node_get_child_ns (stanza->node, "properties",
      GIBBER_TELEPATHY_NS_OLPC_ACTIVITY_PROPS);

  salut_gibber_xmpp_node_add_children_from_properties (properties_node,
      properties, "property");

  result = gibber_muc_connection_send (muc_connection, stanza, &err);
  if (!result)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR, err->message);
      g_error_free (err);
    }

  g_object_unref (stanza);
  g_object_unref (muc_connection);
  g_free (muc_name);
  g_hash_table_destroy (properties);

  return result;
}

static gboolean
notify_activity_properties_changes (SalutSelf *self,
                                    SalutOLPCActivity *activity,
                                    GError **error)
{
  if (!send_olpc_activity_properties_changes_msg (self, activity, error))
    return FALSE;

  resend_invite (self, activity);

  return TRUE;
}

static gboolean
update_activity_privacy (SalutSelf *self,
                         SalutOLPCActivity *activity,
                         gboolean is_private,
                         gboolean *updated,
                         GError **error)
{
  if (activity->is_private == is_private)
    return TRUE;

  activity->is_private = is_private;
  if (updated != NULL)
    *updated = TRUE;

  if (!is_private)
    {
      /* activity becomes public */
      GError *err = NULL;

      DEBUG ("activity becomes public. Announce it");
      if (!announce_activity (self, activity, &err))
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR, err->message);
          g_error_free (err);
          return FALSE;
        }
      return TRUE;
    }
  else
    {
      return stop_announce_activity (self, activity, error);
    }
}

gboolean
salut_self_set_olpc_activity_properties (SalutSelf *self,
                                         TpHandle handle,
                                         const gchar *color,
                                         const gchar *name,
                                         const gchar *type,
                                         const gchar *tags,
                                         gboolean is_private,
                                         GError **error)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  SalutOLPCActivity *activity = g_hash_table_lookup (priv->olpc_activities,
      GUINT_TO_POINTER (handle));
  gboolean updated;

  if (activity == NULL)
    {
      /* User have to call org.laptop.Telepathy.BuddyInfo.SetActivities
       * to create the activity */
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "No activity associated with room having handle %d", handle);
      return FALSE;
    }

  updated = update_activity (activity, name, type, color, tags);

  if (!update_activity_privacy (self, activity, is_private, &updated, error))
    return FALSE;

  if (!updated)
    return TRUE;

  if (activity_is_announced (activity))
    {
      if (!send_olpc_activity_properties_changes_msg (self, activity, error))
        return FALSE;

      return update_activity_service (activity, error);
    }
  else
    {
      return notify_activity_properties_changes (self, activity, error);
    }

  return TRUE;
}

/* return TRUE if we have updated properties */
gboolean
salut_self_olpc_activity_properties_updated (SalutSelf *self,
                                             TpHandle handle,
                                             const gchar *color,
                                             const gchar *name,
                                             const gchar *type,
                                             const gchar *tags,
                                             gboolean is_private)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  SalutOLPCActivity *activity = g_hash_table_lookup (priv->olpc_activities,
      GUINT_TO_POINTER (handle));
  gboolean updated;

  if (activity == NULL)
    {
      /* We are not in this activity. Nothing to update */
      return FALSE;
    }

  updated = update_activity (activity, name, type, color, tags);

  if (!update_activity_privacy (self, activity, is_private, &updated, NULL))
    return FALSE;

  if (updated && activity_is_announced (activity))
    update_activity_service (activity, NULL);

  return updated;
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

  ga_entry_group_service_freeze(priv->presence);
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

          ga_entry_group_service_set_arbitrary (priv->presence, name,
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

          ga_entry_group_service_remove_key (priv->presence, name,
              NULL);
          g_free (name);

          i++;
        }
    }
  if (color != NULL)
    {
      g_free (self->olpc_color);
      self->olpc_color = g_strdup (color);

      ga_entry_group_service_set (priv->presence, "olpc-color",
          color, NULL);
    }
  if (jid != NULL)
    {
      g_free (self->jid);
      self->jid = g_strdup (jid);

      ga_entry_group_service_set (priv->presence, "jid",
          jid, NULL);
    }

  if (!ga_entry_group_service_thaw(priv->presence, &err))
    {
      g_set_error(error, TP_ERRORS, TP_ERROR_NETWORK_ERROR, err->message);
      g_error_free(err);
      return FALSE;
    }
  return TRUE;
}

typedef struct
{
  SalutSelfOLPCActivityFunc foreach;
  gpointer user_data;
} foreach_olpc_activity_ctx;

static void
foreach_olpc_activity (gpointer key, gpointer value, gpointer user_data)
{
  foreach_olpc_activity_ctx *ctx = user_data;
  SalutOLPCActivity *activity = value;

  DEBUG ("%s -> %u", activity->activity_id, GPOINTER_TO_UINT (key));
  (ctx->foreach) (activity->activity_id, GPOINTER_TO_UINT (key),
      ctx->user_data);
}

void
salut_self_foreach_olpc_activity (SalutSelf *self,
                                  SalutSelfOLPCActivityFunc foreach,
                                  gpointer user_data)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  foreach_olpc_activity_ctx ctx = { foreach, user_data };

  DEBUG ("called");

  g_hash_table_foreach (priv->olpc_activities, foreach_olpc_activity,
      &ctx);

  DEBUG ("end");
}

void
salut_self_olpc_augment_invitation (SalutSelf *self,
                                    TpHandle room,
                                    TpHandle contact,
                                    GibberXmppNode *invite_node)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  GibberXmppNode *properties_node;
  GHashTable *properties;
  SalutOLPCActivity *activity;
  GValue *activity_id_val;

  activity = g_hash_table_lookup (priv->olpc_activities,
      GUINT_TO_POINTER (room));
  if (activity == NULL)
    return;

  properties = create_properties_table (activity);

  properties_node = gibber_xmpp_node_add_child_ns (invite_node, "properties",
      GIBBER_TELEPATHY_NS_OLPC_ACTIVITY_PROPS);

  /* add the activity id */
  activity_id_val = g_slice_new0 (GValue);
  g_value_init (activity_id_val, G_TYPE_STRING);
  g_value_set_static_string (activity_id_val, activity->activity_id);
  g_hash_table_insert (properties, "id", activity_id_val);

  salut_gibber_xmpp_node_add_children_from_properties (properties_node,
      properties, "property");

  tp_handle_set_add (activity->invited, contact);

  g_hash_table_destroy (properties);
}

typedef struct
{
  GHashTable *olpc_activities;
  TpHandle contact_handle;
} remove_from_invited_ctx;

static void
remove_from_invited (const gchar *activity_id,
                     TpHandle room,
                     gpointer user_data)
{
  SalutOLPCActivity *activity;
  remove_from_invited_ctx *data = (remove_from_invited_ctx *) user_data;

  activity = g_hash_table_lookup (data->olpc_activities,
      GUINT_TO_POINTER (room));
  if (activity == NULL)
    return;

  if (tp_handle_set_remove (activity->invited, data->contact_handle))
    DEBUG ("contact %d joined activity %s. Remove it from the invited list",
        data->contact_handle, activity->activity_id);
}

/* when a buddy changes his activity list, check if we invited him
 * to this activity and remove him from the invited set */
void
contact_manager_contact_change_cb (SalutContactManager *mgr,
                                   SalutContact *contact,
                                   int changes,
                                   gpointer user_data)
{
  SalutSelf *self = SALUT_SELF (user_data);
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles(
      TP_BASE_CONNECTION (priv->connection), TP_HANDLE_TYPE_CONTACT);
  TpHandle handle;
  remove_from_invited_ctx data;

  if (!(changes & SALUT_CONTACT_OLPC_ACTIVITIES))
    return;

  handle = tp_handle_lookup (handle_repo, contact->name, NULL, NULL);

  data.olpc_activities = priv->olpc_activities;
  data.contact_handle = handle;
  salut_contact_foreach_olpc_activity (contact, remove_from_invited, &data);
}
#endif /* ENABLE_OLPC */
