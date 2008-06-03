/*
 * salut-avahi-self.h - Header for SalutAvahiSelf
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

#include "config.h"

#ifndef __SALUT_AVAHI_SELF_H__
#define __SALUT_AVAHI_SELF_H__

#include <glib-object.h>

#include "salut-self.h"
#include "salut-connection.h"
#include "salut-avahi-discovery-client.h"

G_BEGIN_DECLS

typedef struct _SalutAvahiSelf SalutAvahiSelf;
typedef struct _SalutAvahiSelfClass SalutAvahiSelfClass;

struct _SalutAvahiSelfClass {
    SalutSelfClass parent_class;
};

struct _SalutAvahiSelf {
    SalutSelf parent;

    gpointer priv;
};


GType salut_avahi_self_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_AVAHI_SELF \
  (salut_avahi_self_get_type ())
#define SALUT_AVAHI_SELF(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_AVAHI_SELF, SalutAvahiSelf))
#define SALUT_AVAHI_SELF_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_AVAHI_SELF, SalutAvahiSelfClass))
#define SALUT_IS_AVAHI_SELF(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_AVAHI_SELF))
#define SALUT_IS_AVAHI_SELF_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_AVAHI_SELF))
#define SALUT_AVAHI_SELF_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_AVAHI_SELF, SalutAvahiSelfClass))

SalutAvahiSelf * salut_avahi_self_new (SalutConnection *connection,
    SalutAvahiDiscoveryClient *discovery_client, const gchar *nickname,
    const gchar *first_name, const gchar *last_name, const gchar *jid,
    const gchar *email, const gchar *published_name, const GArray *olpc_key,
    const gchar *olpc_color);

#endif /* #ifndef __SALUT_AVAHI_SELF_H__*/
