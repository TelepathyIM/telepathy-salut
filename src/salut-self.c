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

#include "salut-self.h"
#include "salut-self-signals-marshal.h"

#include "salut-avahi-entry-group.h"

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
  gchar *first_name;
  gchar *last_name;
  gchar *jid;
  gchar *email;

  SalutAvahiClient *client;
  SalutAvahiEntryGroup *presence_group;
  SalutAvahiEntryGroupService *presence;

  gboolean dispose_has_run;
};

#define SALUT_SELF_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_SELF, SalutSelfPrivate))

static void
salut_self_init (SalutSelf *obj)
{
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  obj->status = SALUT_PRESENCE_AVAILABLE;
  obj->status_message = g_strdup("Available");

  priv->first_name = NULL;
  priv->last_name = NULL;
  priv->jid = NULL;
  priv->email = NULL;

  priv->client = NULL;
  priv->presence_group = NULL;
  priv->presence = NULL;
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
  g_object_unref(priv->client); 
  priv->client = NULL;
  g_object_unref(priv->presence_group); 
  priv->presence_group = NULL;
  priv->presence = NULL;

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


SalutSelf *
salut_self_new(SalutAvahiClient *client,
               gchar *first_name, gchar *last_name, 
               gchar *jid, gchar *email) {
  SalutSelfPrivate *priv;

  SalutSelf *ret = g_object_new(SALUT_TYPE_SELF, NULL);
  priv = SALUT_SELF_GET_PRIVATE (ret);

  priv->client = client;
  g_object_ref(client);
  priv->first_name = g_strdup(first_name);
  priv->last_name = g_strdup(last_name);
  priv->jid  = g_strdup(jid);
  priv->email = g_strdup(email);

  return ret;
}


static 
AvahiStringList *create_txt_record(SalutSelf *self) {
  AvahiStringList *ret;
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);

   ret = avahi_string_list_new("textvers=1",
                               "port.p2pj=5298",
                               "vc=!",
                               NULL);
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

  self->name = g_strdup_printf("%s@%s", "sjoerd",
                       avahi_client_get_host_name(priv->client->avahi_client));
  txt_record = create_txt_record(self);

  if ((priv->presence = 
          salut_avahi_entry_group_add_service_strlist(priv->presence_group,
                                                      self->name, 
                                                      "_presence._tcp",
                                                      5298,
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


gboolean salut_self_set_presence(SalutSelf *self, 
                                 SalutPresenceId status,
                                 const gchar *message,
                                 GError **error) {
  SalutSelfPrivate *priv = SALUT_SELF_GET_PRIVATE (self);

  g_assert(status >= 0 && status < SALUT_PRESENCE_NR_PRESENCES);

  self->status = status;
  if (message) {
    g_free(self->status_message);
    self->status_message = g_strdup(message);
  }

  salut_avahi_entry_group_service_freeze(priv->presence);
  salut_avahi_entry_group_service_set(priv->presence, "status",
                               salut_presence_statuses[self->status].txt_name,
                               NULL);
  salut_avahi_entry_group_service_set(priv->presence, "msg",
                                      self->status_message, NULL);
  return salut_avahi_entry_group_service_thaw(priv->presence, error);
}
