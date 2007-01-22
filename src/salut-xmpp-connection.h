/*
 * salut-xmpp-connection.h - Header for SalutXmppConnection
 * Copyright (C) 2006 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd@luon.net>
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

#ifndef __SALUT_XMPP_CONNECTION_H__
#define __SALUT_XMPP_CONNECTION_H__

#include <glib-object.h>

#include "salut-transport.h"
#include "salut-xmpp-stanza.h"

G_BEGIN_DECLS

typedef struct _SalutXmppConnection SalutXmppConnection;
typedef struct _SalutXmppConnectionClass SalutXmppConnectionClass;

struct _SalutXmppConnectionClass {
    GObjectClass parent_class;
};

struct _SalutXmppConnection {
    GObject parent;
    SalutTransport *transport;
    gboolean stream_open;
};

GType salut_xmpp_connection_get_type(void);

/* TYPE MACROS */
#define SALUT_TYPE_XMPP_CONNECTION \
  (salut_xmpp_connection_get_type())
#define SALUT_XMPP_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_XMPP_CONNECTION, SalutXmppConnection))
#define SALUT_XMPP_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_XMPP_CONNECTION, SalutXmppConnectionClass))
#define SALUT_IS_XMPP_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_XMPP_CONNECTION))
#define SALUT_IS_XMPP_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_XMPP_CONNECTION))
#define SALUT_XMPP_CONNECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_XMPP_CONNECTION, SalutXmppConnectionClass))



SalutXmppConnection *salut_xmpp_connection_new(SalutTransport *transport); 
void salut_xmpp_connection_open(SalutXmppConnection *connection,
                                const gchar *to, const gchar *from,
                                const gchar *version);
void salut_xmpp_connection_close(SalutXmppConnection *connection);
gboolean salut_xmpp_connection_send(SalutXmppConnection *connection, 
                                    SalutXmppStanza *stanza, 
                                    GError **error);

G_END_DECLS

#endif /* #ifndef __SALUT_XMPP_CONNECTION_H__*/
