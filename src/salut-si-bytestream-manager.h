/*
 * salut-bytestream-manager.h - Header for SalutBytestreamManager
 * Copyright (C) 2007 Collabora Ltd.
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

#ifndef __SALUT_BYTESTREAM_MANAGER_H__
#define __SALUT_BYTESTREAM_MANAGER_H__

#include <glib-object.h>
#include "salut-xmpp-connection-manager.h"
#include "salut-contact.h"

#include <gibber/gibber-linklocal-transport.h>
#include <gibber/gibber-bytestream-iface.h>

G_BEGIN_DECLS

typedef struct _SalutBytestreamManager SalutBytestreamManager;
typedef struct _SalutBytestreamManagerClass SalutBytestreamManagerClass;

struct _SalutBytestreamManagerClass {
    GObjectClass parent_class;
};

struct _SalutBytestreamManager {
    GObject parent;

    gpointer priv;
};


GType salut_bytestream_manager_get_type(void);

/* TYPE MACROS */
#define SALUT_TYPE_BYTESTREAM_MANAGER \
  (salut_bytestream_manager_get_type())
#define SALUT_BYTESTREAM_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_BYTESTREAM_MANAGER, \
                              SalutBytestreamManager))
#define SALUT_BYTESTREAM_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_BYTESTREAM_MANAGER, \
                           SalutBytestreamManagerClass))
#define SALUT_IS_BYTESTREAM_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_BYTESTREAM_MANAGER))
#define SALUT_IS_BYTESTREAM_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_BYTESTREAM_MANAGER))
#define SALUT_BYTESTREAM_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_BYTESTREAM_MANAGER, \
                              SalutBytestreamManagerClass))

typedef void (* SalutBytestreamManagerNegotiateReplyFunc) (
    GibberBytestreamIface *bytestream, const gchar *stream_id,
    GibberXmppStanza *stanza, gpointer user_data);

SalutBytestreamManager *
salut_bytestream_manager_new (SalutConnection *connection,
    const gchar *host_name_fqdn);

GibberXmppStanza *
salut_bytestream_manager_make_stream_init_iq (const gchar *from,
    const gchar *to, const gchar *stream_id, const gchar *profile);

gboolean
salut_bytestream_manager_negotiate_stream (SalutBytestreamManager *self,
    SalutContact *contact, GibberXmppStanza *stanza, const gchar *stream_id,
    SalutBytestreamManagerNegotiateReplyFunc func, gpointer user_data,
    GError **error);

#endif /* #ifndef __SALUT_BYTESTREAM_MANAGER_H__*/
