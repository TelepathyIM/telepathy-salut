/*
 * salut-xmpp-connection.h - Header for SalutXmppConnectionManager
 * Copyright (C) 2007 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the tubesplied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __SALUT_XMPP_CONNECTION_MANAGER_H__
#define __SALUT_XMPP_CONNECTION_MANAGER_H__

#include <glib-object.h>

#include "salut-connection.h"

G_BEGIN_DECLS

typedef struct _SalutXmppConnectionManager SalutXmppConnectionManager;
typedef struct _SalutXmppConnectionManagerClass \
          SalutXmppConnectionManagerClass;

struct _SalutXmppConnectionManagerClass
{
    GObjectClass parent_class;
};

struct _SalutXmppConnectionManager
{
    GObject parent;

    gpointer priv;
};


GType salut_xmpp_connection_manager_get_type(void);

/* TYPE MACROS */
#define SALUT_TYPE_XMPP_CONNECTION_MANAGER \
  (salut_xmpp_connection_manager_get_type())
#define SALUT_XMPP_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_XMPP_CONNECTION_MANAGER, \
                              SalutXmppConnectionManager))
#define SALUT_XMPP_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_XMPP_CONNECTION_MANAGER, \
                           SalutXmppConnectionManagerClass))
#define SALUT_IS_XMPP_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_XMPP_CONNECTION_MANAGER))
#define SALUT_IS_XMPP_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_XMPP_CONNECTION_MANAGER))
#define SALUT_XMPP_CONNECTION_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_XMPP_CONNECTION_MANAGER, \
                              SalutXmppConnectionManagerClass))

SalutXmppConnectionManager *
salut_xmpp_connection_manager_new (void);

int
salut_xmpp_connection_manager_listen (SalutXmppConnectionManager *manager);

#endif /* #ifndef __SALUT_XMPP_CONNECTION_MANAGER_H__*/