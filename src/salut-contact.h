/*
 * salut-contact.h - Header for SalutContact
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

#ifndef __SALUT_CONTACT_H__
#define __SALUT_CONTACT_H__

#include "config.h"

#include <netinet/in.h>
#include <glib-object.h>

#include <telepathy-glib/handle-repo.h>

#include "salut-presence.h"
#include "salut-connection.h"
#ifdef ENABLE_OLPC
#include "salut-olpc-activity.h"
#endif

G_BEGIN_DECLS

#define  SALUT_CONTACT_ALIAS_CHANGED  0x1
#define  SALUT_CONTACT_STATUS_CHANGED 0x2
#define  SALUT_CONTACT_AVATAR_CHANGED 0x4
#ifdef ENABLE_OLPC
#define  SALUT_CONTACT_OLPC_PROPERTIES 0x8
#define  SALUT_CONTACT_OLPC_CURRENT_ACTIVITY 0x10
#define  SALUT_CONTACT_OLPC_ACTIVITIES 0x20
#endif /* ENABLE_OLPC */

typedef struct _SalutContact SalutContact;
typedef struct _SalutContactClass SalutContactClass;

struct _SalutContactClass {
    GObjectClass parent_class;

    /* public abstract methods */
    GArray * (*get_addresses) (SalutContact *contact);
    gboolean (*has_address) (SalutContact *contact,
        struct sockaddr *address, guint size);

    /* private abstract methods */
    void (*retrieve_avatar) (SalutContact *contact);
};

struct _SalutContact {
    GObject parent;
    gchar *name;
    SalutPresenceId status;
    gchar *avatar_token;
    gchar *status_message;
    gchar *jid;
    TpHandle handle;
#ifdef ENABLE_OLPC
    GArray *olpc_key;
    gchar *olpc_color;
    gchar *olpc_cur_act;
    TpHandle olpc_cur_act_room;
    gchar *olpc_ip4;
    gchar *olpc_ip6;
#endif /* ENABLE_OLPC */

    /* private */
    SalutConnection *connection;
};

GType salut_contact_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_CONTACT \
  (salut_contact_get_type ())
#define SALUT_CONTACT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_CONTACT, SalutContact))
#define SALUT_CONTACT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_CONTACT, SalutContactClass))
#define SALUT_IS_CONTACT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_CONTACT))
#define SALUT_IS_CONTACT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_CONTACT))
#define SALUT_CONTACT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_CONTACT, SalutContactClass))

typedef struct {
  struct sockaddr_storage address;
} salut_contact_address_t;

/* Returns an array of addresses on which the contact can be found */
GArray * salut_contact_get_addresses (SalutContact *contact);

gboolean salut_contact_has_address (SalutContact *contact,
                                    struct sockaddr *address,
                                    guint size);
const gchar * salut_contact_get_alias (SalutContact *contact);

typedef void (*salut_contact_get_avatar_callback)(SalutContact *contact,
                                                  guint8 *avatar,
                                                  gsize size,
                                                  gpointer user_data);

void salut_contact_get_avatar (SalutContact *contact,
                               salut_contact_get_avatar_callback callback,
                               gpointer user_data1);

#ifdef ENABLE_OLPC
typedef void (*SalutContactOLPCActivityFunc)
    (SalutOlpcActivity *activity, gpointer user_data);

void salut_contact_foreach_olpc_activity (SalutContact *self,
    SalutContactOLPCActivityFunc foreach, gpointer user_data);

gboolean salut_contact_joined_activity (SalutContact *self,
    SalutOlpcActivity *activity);

void salut_contact_left_activity (SalutContact *self,
    SalutOlpcActivity *activity);
#endif

/* restricted methods */
void salut_contact_change_alias (SalutContact *self, const gchar *alias);
void salut_contact_change_status (SalutContact *self, SalutPresenceId);
void salut_contact_change_status_message (SalutContact *self,
  const gchar *message);
void salut_contact_change_avatar_token (SalutContact *self,
  const gchar *avatar_token);
void salut_contact_change_jid (SalutContact *self, gchar *jid);

#ifdef ENABLE_OLPC
void salut_contact_change_olpc_color (SalutContact *self,
  const gchar *olpc_color);
void salut_contact_change_olpc_key (SalutContact *self, GArray *olpc_key);
void salut_contact_change_ipv4_addr (SalutContact *self,
  const gchar *ipv4_addr);
void salut_contact_change_ipv6_addr (SalutContact *self,
  const gchar *ipv4_addr);
void salut_contact_change_current_activity (SalutContact *self,
  const gchar *current_activity_id, const gchar *current_activity_room);
#endif

void salut_contact_avatar_request_flush (SalutContact *self, guint8 *data,
    gsize size);

void salut_contact_found (SalutContact *self);
void salut_contact_lost (SalutContact *self);

void salut_contact_freeze (SalutContact *self);
void salut_contact_thaw (SalutContact *self);

G_END_DECLS

#endif /* #ifndef __SALUT_CONTACT_H__*/
