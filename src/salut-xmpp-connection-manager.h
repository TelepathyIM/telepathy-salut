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

#include <gibber/gibber-xmpp-stanza.h>
#include <gibber/gibber-xmpp-connection.h>

#include "salut-connection.h"
#include "salut-contact.h"
#include "salut-contact-manager.h"

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

typedef enum
{
  SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_DONE,
  SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_PENDING,
  SALUT_XMPP_CONNECTION_MANAGER_REQUEST_CONNECTION_RESULT_FAILURE,
} SalutXmppConnectionManagerRequestConnectionResult;

typedef gboolean (* SalutXmppConnectionManagerStanzaFilterFunc) (
    SalutXmppConnectionManager *manager, GibberXmppConnection *conn,
    GibberXmppStanza *stanza, SalutContact *contact, gpointer user_data);

typedef void (* SalutXmppConnectionManagerStanzaCallbackFunc) (
    SalutXmppConnectionManager *mgr, GibberXmppConnection *conn,
    GibberXmppStanza *stanza, SalutContact *contact, gpointer user_data);

SalutXmppConnectionManager *
salut_xmpp_connection_manager_new (SalutConnection *connection,
    SalutContactManager *contact_manager);

int
salut_xmpp_connection_manager_listen (SalutXmppConnectionManager *manager,
    GError **error);

SalutXmppConnectionManagerRequestConnectionResult
salut_xmpp_connection_request_connection (SalutXmppConnectionManager *manager,
    SalutContact *contact, GibberXmppConnection **conn);

gboolean
salut_xmpp_connection_manager_add_stanza_filter (
    SalutXmppConnectionManager *manager,
    GibberXmppConnection *conn,
    SalutXmppConnectionManagerStanzaFilterFunc filter,
    SalutXmppConnectionManagerStanzaCallbackFunc callback,
    gpointer user_data);

void
salut_xmpp_connection_manager_remove_stanza_filter (
    SalutXmppConnectionManager *manager,
    GibberXmppConnection *conn,
    SalutXmppConnectionManagerStanzaFilterFunc filter,
    SalutXmppConnectionManagerStanzaCallbackFunc callback,
    gpointer user_data);

#endif /* #ifndef __SALUT_XMPP_CONNECTION_MANAGER_H__*/
