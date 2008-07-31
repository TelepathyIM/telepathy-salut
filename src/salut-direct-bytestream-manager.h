/*
 * salut-direct-bytestream-manager.h - Header for SalutDirectBytestreamManager
 * Copyright (C) 2007, 2008 Collabora Ltd.
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

#ifndef __SALUT_DIRECT_BYTESTREAM_MANAGER_H__
#define __SALUT_DIRECT_BYTESTREAM_MANAGER_H__

#include <glib-object.h>
#include "salut-xmpp-connection-manager.h"
#include "salut-contact.h"
#include "tube-iface.h"

#include <gibber/gibber-linklocal-transport.h>
#include <gibber/gibber-bytestream-iface.h>

G_BEGIN_DECLS

typedef struct _SalutDirectBytestreamManager SalutDirectBytestreamManager;
typedef struct _SalutDirectBytestreamManagerClass SalutDirectBytestreamManagerClass;

struct _SalutDirectBytestreamManagerClass {
    GObjectClass parent_class;
};

struct _SalutDirectBytestreamManager {
    GObject parent;

    gpointer priv;
};


GType salut_direct_bytestream_manager_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_DIRECT_BYTESTREAM_MANAGER \
  (salut_direct_bytestream_manager_get_type ())
#define SALUT_DIRECT_BYTESTREAM_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_DIRECT_BYTESTREAM_MANAGER, \
                              SalutDirectBytestreamManager))
#define SALUT_DIRECT_BYTESTREAM_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_DIRECT_BYTESTREAM_MANAGER, \
                           SalutDirectBytestreamManagerClass))
#define SALUT_IS_DIRECT_BYTESTREAM_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_DIRECT_BYTESTREAM_MANAGER))
#define SALUT_IS_DIRECT_BYTESTREAM_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_DIRECT_BYTESTREAM_MANAGER))
#define SALUT_DIRECT_BYTESTREAM_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_DIRECT_BYTESTREAM_MANAGER, \
                              SalutDirectBytestreamManagerClass))

SalutDirectBytestreamManager *
salut_direct_bytestream_manager_new (SalutConnection *connection,
    const gchar *host_name_fqdn);

/* To be used on the CM-initiator side, to receive connections from the remote
 * CM
 *
 * return: port
 * */
int
salut_direct_new_listening_stream (SalutDirectBytestreamManager *self,
    SalutContact *contact, GibberXmppConnection *connection,
    SalutTubeIface *tube);

/* To be used on the CM-receptor side, to make a new connection */
GibberBytestreamIface *
salut_direct_bytestream_manager_new_stream (SalutDirectBytestreamManager *self,
    SalutContact *contact, int portnum);

#endif /* #ifndef __SALUT_DIRECT_BYTESTREAM_MANAGER_H__*/
