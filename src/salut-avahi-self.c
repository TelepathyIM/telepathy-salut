/*
 * salut-avahi-self.c - Source for SalutAvahiSelf
 * Copyright (C) 2008 Collabora Ltd.
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
#include <arpa/inet.h>
#include <string.h>
#include <avahi-gobject/ga-entry-group.h>

#define DEBUG_FLAG DEBUG_SELF
#include "debug.h"

#include "salut-avahi-self.h"

#include "sha1/sha1-util.h"

#ifdef ENABLE_OLPC
#define KEY_SEGMENT_SIZE 200
#endif

G_DEFINE_TYPE (SalutAvahiSelf, salut_avahi_self, SALUT_TYPE_SELF);

/* properties */
enum
{
  PROP_DISCOVERY_CLIENT = 1,
  LAST_PROPERTY
};

struct _SalutAvahiSelfPrivate
{
  SalutAvahiDiscoveryClient *discovery_client;
  GaEntryGroup *presence_group;
  GaEntryGroupService *presence;
  GaEntryGroup *avatar_group;

  gboolean dispose_has_run;
};

static void
salut_avahi_self_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  SalutAvahiSelf *self = SALUT_AVAHI_SELF (object);
  SalutAvahiSelfPrivate *priv = self->priv;

  switch (property_id) {
    case PROP_DISCOVERY_CLIENT:
      g_value_set_object (value, priv->discovery_client);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
salut_avahi_self_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  SalutAvahiSelf *self = SALUT_AVAHI_SELF (object);
  SalutAvahiSelfPrivate *priv = self->priv;

  switch (property_id) {
    case PROP_DISCOVERY_CLIENT:
      priv->discovery_client = g_value_get_object (value);
      g_object_ref (priv->discovery_client);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
salut_avahi_self_init (SalutAvahiSelf *self)
{
  SalutAvahiSelfPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_AVAHI_SELF, SalutAvahiSelfPrivate);

  self->priv = priv;

  priv->discovery_client = NULL;
}

static void
_avahi_presence_group_established (GaEntryGroup *group,
                                   GaEntryGroupState state,
                                   SalutAvahiSelf *self)
{
  salut_self_established (SALUT_SELF (self));
}

static void
_avahi_presence_group_failed (GaEntryGroup *group,
                              GaEntryGroupState state,
                              SalutAvahiSelf *self)
{
  DEBUG ("self presence group failed");
}

static AvahiStringList *
create_txt_record (SalutAvahiSelf *self,
                   int port)
{
  SalutSelf *_self = SALUT_SELF (self);
  AvahiStringList *ret;

  ret = avahi_string_list_new ("txtvers=1", NULL);

  /* Some silly clients still use this */
  ret = avahi_string_list_add_printf (ret, "port.p2pj=%d", port);

  if (_self->nickname != NULL)
    ret = avahi_string_list_add_printf (ret, "nick=%s", _self->nickname);
  if (_self->first_name != NULL)
    ret = avahi_string_list_add_printf (ret, "1st=%s", _self->first_name);
  if (_self->last_name != NULL)
    ret = avahi_string_list_add_printf (ret, "last=%s", _self->last_name);
  if (_self->email != NULL)
    ret = avahi_string_list_add_printf (ret, "email=%s", _self->email);
  if (_self->jid != NULL)
    ret = avahi_string_list_add_printf (ret, "jid=%s", _self->jid);

#ifdef ENABLE_OLPC
  if (_self->olpc_color)
    ret = avahi_string_list_add_printf (ret, "olpc-color=%s",
        _self->olpc_color);

  if (_self->olpc_key != NULL)
    {
      uint8_t *key = (uint8_t *) _self->olpc_key->data;
      size_t key_len = _self->olpc_key->len;
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

  ret = avahi_string_list_add_printf (ret, "status=%s",
      salut_presence_status_txt_names[_self->status]);

  if (_self->status_message != NULL)
    ret = avahi_string_list_add_printf (ret, "msg=%s", _self->status_message);

   return ret;
}

static gboolean
salut_avahi_self_set_caps (SalutSelf *_self,
                           GError **error)
{
  SalutAvahiSelf *self = SALUT_AVAHI_SELF (_self);
  SalutAvahiSelfPrivate *priv = self->priv;

  if (priv->presence == NULL)
    /* Service is not announced yet */
    return TRUE;

  ga_entry_group_service_freeze (priv->presence);

  if (_self->node == NULL)
      ga_entry_group_service_remove_key (priv->presence, "node", NULL);
  else
      ga_entry_group_service_set (priv->presence, "node", _self->node, NULL);

  if (_self->hash == NULL)
      ga_entry_group_service_remove_key (priv->presence, "hash", NULL);
  else
      ga_entry_group_service_set (priv->presence, "hash", _self->hash, NULL);

  if (_self->ver == NULL)
      ga_entry_group_service_remove_key (priv->presence, "ver", NULL);
  else
      ga_entry_group_service_set (priv->presence, "ver", _self->ver, NULL);

  return ga_entry_group_service_thaw (priv->presence, error);
}

static gboolean
salut_avahi_self_announce (SalutSelf *_self,
                           gint port,
                           GError **error)
{
  SalutAvahiSelf *self = SALUT_AVAHI_SELF (_self);
  SalutAvahiSelfPrivate *priv = self->priv;
  AvahiStringList *txt_record = NULL;
  const char *dnssd_name;

  priv->presence_group = ga_entry_group_new ();

  g_signal_connect (priv->presence_group, "state-changed::established",
      G_CALLBACK (_avahi_presence_group_established), self);
  g_signal_connect (priv->presence_group, "state-changed::collision",
      G_CALLBACK (_avahi_presence_group_failed), self);
  g_signal_connect (priv->presence_group, "state-changed::failure",
      G_CALLBACK(_avahi_presence_group_failed), self);

  if (!ga_entry_group_attach (priv->presence_group,
        priv->discovery_client->avahi_client, error))
    goto error;

  _self->name = g_strdup_printf ("%s@%s", _self->published_name,
      avahi_client_get_host_name (priv->discovery_client->avahi_client->avahi_client));

  txt_record = create_txt_record (self, port);

  dnssd_name = salut_avahi_discovery_client_get_dnssd_name (
      priv->discovery_client);

  priv->presence = ga_entry_group_add_service_strlist (priv->presence_group,
      _self->name, dnssd_name, port, error, txt_record);
  if (priv->presence == NULL)
    goto error;

  if (!salut_avahi_self_set_caps (_self, NULL))
    goto error;

  if (!ga_entry_group_commit (priv->presence_group, error))
    goto error;

  avahi_string_list_free (txt_record);
  return TRUE;

error:
  avahi_string_list_free (txt_record);
  return FALSE;
}

static gboolean
salut_avahi_self_set_presence (SalutSelf *self,
                               GError **error)
{
  SalutAvahiSelf *avahi_self = SALUT_AVAHI_SELF (self);
  SalutAvahiSelfPrivate *priv = avahi_self->priv;

  ga_entry_group_service_freeze (priv->presence);
  ga_entry_group_service_set (priv->presence, "status",
      salut_presence_status_txt_names[self->status], NULL);

  if (self->status_message)
    ga_entry_group_service_set (priv->presence, "msg",
        self->status_message, NULL);
  else
    ga_entry_group_service_remove_key (priv->presence, "msg", NULL);

  return ga_entry_group_service_thaw (priv->presence, error);
}

static gboolean
salut_avahi_self_set_alias (SalutSelf *_self,
                            GError **error)
{
  SalutAvahiSelf *self = SALUT_AVAHI_SELF (_self);
  SalutAvahiSelfPrivate *priv = self->priv;

  return ga_entry_group_service_set (priv->presence, "nick",
      _self->alias, error);
}

static void
salut_avahi_self_remove_avatar (SalutSelf *_self)
{
  SalutAvahiSelf *self = SALUT_AVAHI_SELF (_self);
  SalutAvahiSelfPrivate *priv = self->priv;

  ga_entry_group_service_remove_key (priv->presence, "phsh", NULL);
  if (priv->avatar_group != NULL)
    {
      g_object_unref (priv->avatar_group);
      priv->avatar_group = NULL;
    }
}

static gboolean
salut_avahi_self_publish_avatar (SalutAvahiSelf *self,
                                 guint8 *data,
                                 gsize size,
                                 GError **error)
{
  SalutAvahiSelfPrivate *priv = self->priv;
  SalutSelf *_self = SALUT_SELF (self);
  gchar *name;
  gboolean ret;
  gboolean is_new = FALSE;
  const gchar *dnssd_name;

  dnssd_name = salut_avahi_discovery_client_get_dnssd_name (
      priv->discovery_client);

  name = g_strdup_printf ("%s.%s.local", dnssd_name, _self->name);

  if (priv->avatar_group == NULL)
    {
      priv->avatar_group = ga_entry_group_new ();
      ga_entry_group_attach (priv->avatar_group,
          priv->discovery_client->avahi_client, NULL);
      is_new = TRUE;
    }

  ret = ga_entry_group_add_record (priv->avatar_group,
      is_new ? 0 : AVAHI_PUBLISH_UPDATE, name, 0xA, 120, data, size, error);

  g_free (name);

  if (is_new)
    ga_entry_group_commit (priv->avatar_group, error);

  return ret;
}

static gboolean
salut_avahi_self_set_avatar (SalutSelf *_self,
                             guint8 *data,
                             gsize size,
                             GError **error)
{
  SalutAvahiSelf *self = SALUT_AVAHI_SELF (_self);
  SalutAvahiSelfPrivate *priv = self->priv;

  if (!salut_avahi_self_publish_avatar (self, data, size, error))
    return FALSE;

  _self->avatar = g_memdup (data, size);
  _self->avatar_size = size;

  if (size > 0)
    _self->avatar_token = sha1_hex (data, size);

  return ga_entry_group_service_set (priv->presence, "phsh",
      _self->avatar_token, error);
}

#ifdef ENABLE_OLPC
static gboolean
salut_avahi_self_update_current_activity (SalutSelf *_self,
                                          const gchar *room_name,
                                          GError **error)
{
  SalutAvahiSelf *self = SALUT_AVAHI_SELF (_self);
  SalutAvahiSelfPrivate *priv = self->priv;

  ga_entry_group_service_freeze (priv->presence);

  ga_entry_group_service_set (priv->presence,
      "olpc-current-activity", _self->olpc_cur_act, NULL);

  ga_entry_group_service_set (priv->presence,
      "olpc-current-activity-room", room_name, NULL);

  return ga_entry_group_service_thaw (priv->presence, error);
}

static gboolean
salut_avahi_self_set_olpc_properties (SalutSelf *_self,
                                      const GArray *key,
                                      const gchar *color,
                                      const gchar *jid,
                                      GError **error)
{
  SalutAvahiSelf *self = SALUT_AVAHI_SELF (_self);
  SalutAvahiSelfPrivate *priv = self->priv;

  ga_entry_group_service_freeze (priv->presence);

  if (key != NULL)
    {
      size_t key_len = key->len;
      const guint8 *key_data = (const guint8 *) key->data;
      guint i;
      guint to_remove;

      if (_self->olpc_key == NULL)
        {
          to_remove = 0;
        }
      else
        {
          to_remove = (_self->olpc_key->len + KEY_SEGMENT_SIZE - 1) /
            KEY_SEGMENT_SIZE;
        }

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
      ga_entry_group_service_set (priv->presence, "olpc-color",
          color, NULL);
    }

  if (jid != NULL)
    {
      ga_entry_group_service_set (priv->presence, "jid",
          jid, NULL);
    }

  return ga_entry_group_service_thaw (priv->presence, error);
}
#endif

static void salut_avahi_self_dispose (GObject *object);

static void
salut_avahi_self_class_init (
    SalutAvahiSelfClass *salut_avahi_self_class) {
  GObjectClass *object_class = G_OBJECT_CLASS (salut_avahi_self_class);
  SalutSelfClass *self_class = SALUT_SELF_CLASS (
      salut_avahi_self_class);
  GParamSpec *param_spec;

  g_type_class_add_private (salut_avahi_self_class,
      sizeof (SalutAvahiSelfPrivate));

  object_class->dispose = salut_avahi_self_dispose;

  object_class->get_property = salut_avahi_self_get_property;
  object_class->set_property = salut_avahi_self_set_property;

  self_class->announce = salut_avahi_self_announce;
  self_class->set_presence = salut_avahi_self_set_presence;
  self_class->set_caps = salut_avahi_self_set_caps;
  self_class->set_alias = salut_avahi_self_set_alias;
  self_class->remove_avatar = salut_avahi_self_remove_avatar;
  self_class->set_avatar = salut_avahi_self_set_avatar;
#ifdef ENABLE_OLPC
  self_class->update_current_activity =
    salut_avahi_self_update_current_activity;
  self_class->set_olpc_properties = salut_avahi_self_set_olpc_properties;
#endif

  param_spec = g_param_spec_object (
      "discovery-client",
      "SalutAvahiDiscoveryClient object",
      "The Salut Avahi Discovery client associated with this self object",
      SALUT_TYPE_AVAHI_DISCOVERY_CLIENT,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_DISCOVERY_CLIENT,
      param_spec);
}

void
salut_avahi_self_dispose (GObject *object)
{
  SalutAvahiSelf *self = SALUT_AVAHI_SELF (object);
  SalutAvahiSelfPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->presence_group != NULL)
    {
      g_object_unref (priv->presence_group);
      priv->presence_group = NULL;
    }

  if (priv->avatar_group != NULL)
    {
      g_object_unref (priv->avatar_group);
      priv->avatar_group = NULL;
    }

  if (priv->discovery_client != NULL)
    {
      g_object_unref (priv->discovery_client);
      priv->discovery_client = NULL;
    }

  if (G_OBJECT_CLASS (salut_avahi_self_parent_class)->dispose)
    G_OBJECT_CLASS (salut_avahi_self_parent_class)->dispose (object);
}

SalutAvahiSelf *
salut_avahi_self_new (SalutConnection *connection,
                      SalutAvahiDiscoveryClient *discovery_client,
                      const gchar *nickname,
                      const gchar *first_name,
                      const gchar *last_name,
                      const gchar *jid,
                      const gchar *email,
                      const gchar *published_name,
                      const GArray *olpc_key,
                      const gchar *olpc_color)
{
  return g_object_new (SALUT_TYPE_AVAHI_SELF,
      "connection", connection,
      "discovery-client", discovery_client,
      "nickname", nickname,
      "first-name", first_name,
      "last-name", last_name,
      "jid", jid,
      "email", email,
      "published-name", published_name,
#ifdef ENABLE_OLPC
      "olpc-key", olpc_key,
      "olpc-color", olpc_color,
#endif
      NULL);
}
