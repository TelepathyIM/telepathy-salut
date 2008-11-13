/*
 * salut-si-bytestream-manager.h - Header for SalutSiBytestreamManager
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

#ifndef __SALUT_SI_BYTESTREAM_MANAGER_H__
#define __SALUT_SI_BYTESTREAM_MANAGER_H__

#include <glib-object.h>
#include "salut-xmpp-connection-manager.h"
#include "salut-contact.h"

#include <gibber/gibber-linklocal-transport.h>
#include <gibber/gibber-bytestream-iface.h>

G_BEGIN_DECLS

typedef struct _SalutSiBytestreamManager SalutSiBytestreamManager;
typedef struct _SalutSiBytestreamManagerClass SalutSiBytestreamManagerClass;

struct _SalutSiBytestreamManagerClass {
    GObjectClass parent_class;
};

struct _SalutSiBytestreamManager {
    GObject parent;

    gpointer priv;
};


GType salut_si_bytestream_manager_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_SI_BYTESTREAM_MANAGER \
  (salut_si_bytestream_manager_get_type ())
#define SALUT_SI_BYTESTREAM_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_SI_BYTESTREAM_MANAGER, \
                              SalutSiBytestreamManager))
#define SALUT_SI_BYTESTREAM_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_SI_BYTESTREAM_MANAGER, \
                           SalutSiBytestreamManagerClass))
#define SALUT_IS_SI_BYTESTREAM_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_SI_BYTESTREAM_MANAGER))
#define SALUT_IS_SI_BYTESTREAM_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_SI_BYTESTREAM_MANAGER))
#define SALUT_SI_BYTESTREAM_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_SI_BYTESTREAM_MANAGER, \
                              SalutSiBytestreamManagerClass))

typedef void (* SalutSiBytestreamManagerNegotiateReplyFunc) (
    GibberBytestreamIface *bytestream, const gchar *stream_id,
    GibberXmppStanza *stanza, gpointer user_data);

SalutSiBytestreamManager *
salut_si_bytestream_manager_new (SalutConnection *connection,
    const gchar *host_name_fqdn);

GibberXmppStanza *
salut_si_bytestream_manager_make_stream_init_iq (const gchar *from,
    const gchar *to, const gchar *stream_id, const gchar *profile);

gboolean
salut_si_bytestream_manager_negotiate_stream (SalutSiBytestreamManager *self,
    SalutContact *contact, GibberXmppStanza *stanza, const gchar *stream_id,
    SalutSiBytestreamManagerNegotiateReplyFunc func, gpointer user_data,
    GError **error);

#endif /* #ifndef __SALUT_SI_BYTESTREAM_MANAGER_H__*/
