/*
 * gibber-xmpp-connection-listener.h - Header for GibberXmppConnectionListener
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

#ifndef __GIBBER_XMPP_CONNECTION_LISTENER_H__
#define __GIBBER_XMPP_CONNECTION_LISTENER_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _GibberXmppConnectionListener GibberXmppConnectionListener;
typedef struct _GibberXmppConnectionListenerClass
    GibberXmppConnectionListenerClass;

struct _GibberXmppConnectionListenerClass {
  GObjectClass parent_class;
};

struct _GibberXmppConnectionListener {
  GObject parent;

  gpointer priv;
};

GType gibber_xmpp_connection_listener_get_type (void);

/* TYPE MACROS */
#define GIBBER_TYPE_XMPP_CONNECTION_LISTENER \
  (gibber_xmpp_connection_listener_get_type ())
#define GIBBER_XMPP_CONNECTION_LISTENER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_XMPP_CONNECTION_LISTENER,\
                              GibberXmppConnectionListener))
#define GIBBER_XMPP_CONNECTION_LISTENER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_XMPP_CONNECTION_LISTENER,\
                           GibberXmppConnectionListenerClass))
#define GIBBER_IS_XMPP_CONNECTION_LISTENER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_XMPP_CONNECTION_LISTENER))
#define GIBBER_IS_XMPP_CONNECTION_LISTENER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_XMPP_CONNECTION_LISTENER))
#define GIBBER_XMPP_CONNECTION_LISTENER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_XMPP_CONNECTION_LISTENER,\
                              GibberXmppConnectionListenerClass))

GibberXmppConnectionListener *
gibber_xmpp_connection_listener_new (void);

gboolean gibber_xmpp_connection_listener_listen (
    GibberXmppConnectionListener *listener, int port, GError **error);

int gibber_xmpp_connection_listener_get_port (
    GibberXmppConnectionListener *listener);


G_END_DECLS

#endif /* #ifndef __GIBBER_XMPP_CONNECTION_LISTENER_H__ */
