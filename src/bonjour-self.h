/*
 * bonjour-self.h - Header for SalutBonjourSelf
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

#ifndef __SALUT_BONJOUR_SELF_H__
#define __SALUT_BONJOUR_SELF_H__

#include <glib-object.h>

#include "self.h"
#include "connection.h"
#include "bonjour-discovery-client.h"

G_BEGIN_DECLS

typedef struct _SalutBonjourSelf SalutBonjourSelf;
typedef struct _SalutBonjourSelfClass SalutBonjourSelfClass;
typedef struct _SalutBonjourSelfPrivate SalutBonjourSelfPrivate;

struct _SalutBonjourSelfClass {
    SalutSelfClass parent_class;
};

struct _SalutBonjourSelf {
    SalutSelf parent;

    SalutBonjourSelfPrivate *priv;
};


GType salut_bonjour_self_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_BONJOUR_SELF \
  (salut_bonjour_self_get_type ())
#define SALUT_BONJOUR_SELF(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_BONJOUR_SELF, SalutBonjourSelf))
#define SALUT_BONJOUR_SELF_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_BONJOUR_SELF, SalutBonjourSelfClass))
#define SALUT_IS_BONJOUR_SELF(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_BONJOUR_SELF))
#define SALUT_IS_BONJOUR_SELF_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_BONJOUR_SELF))
#define SALUT_BONJOUR_SELF_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_BONJOUR_SELF, SalutBonjourSelfClass))

SalutBonjourSelf * salut_bonjour_self_new (SalutConnection *connection,
    SalutBonjourDiscoveryClient *discovery_client, const gchar *nickname,
    const gchar *first_name, const gchar *last_name, const gchar *jid,
    const gchar *email, const gchar *published_name);

#endif /* #ifndef __SALUT_BONJOUR_SELF_H__*/
