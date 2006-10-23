/*
 * salut-connection-manager.h - Header for SalutConnectionManager
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#ifndef __SALUT_CONNECTION_MANAGER_H__
#define __SALUT_CONNECTION_MANAGER_H__

#include <glib-object.h>

#define SALUT_CONN_MGR_BUS_NAME        "org.freedesktop.Telepathy.ConnectionManager.salut"
#define SALUT_CONN_MGR_OBJECT_PATH     "/org/freedesktop/Telepathy/ConnectionManager/salut"


G_BEGIN_DECLS

typedef struct _SalutConnectionManager SalutConnectionManager;
typedef struct _SalutConnectionManagerClass SalutConnectionManagerClass;

struct _SalutConnectionManagerClass {
    GObjectClass parent_class;
};

struct _SalutConnectionManager {
    GObject parent;
};


typedef struct {
    const gchar *name;          /* name as passed over dbus */
    const gchar *dtype;         /* D-Bus type string */
    const GType gtype;          /* glib type string */
    guint flags;                /* combination of TP_CONN_MGR_PARAM_FLAG_foo */
    const gpointer def;         /* default - gchar * or GINT_TO_POINTER */
    const gsize offset;         /* internal use only */
} SalutParamSpec;

typedef struct {
    const gchar *name;
    const SalutParamSpec *parameters;       /* terminated by a NULL name */
} SalutProtocolSpec;

const SalutProtocolSpec *salut_protocols; /* terminated by a NULL name */

GType salut_connection_manager_get_type(void);

/* TYPE MACROS */
#define SALUT_TYPE_CONNECTION_MANAGER \
  (salut_connection_manager_get_type())
#define SALUT_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_CONNECTION_MANAGER, SalutConnectionManager))
#define SALUT_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_CONNECTION_MANAGER, SalutConnectionManagerClass))
#define SALUT_IS_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_CONNECTION_MANAGER))
#define SALUT_IS_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_CONNECTION_MANAGER))
#define SALUT_CONNECTION_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_CONNECTION_MANAGER, SalutConnectionManagerClass))

void _salut_connection_manager_register(SalutConnectionManager *self);

gboolean 
salut_connection_manager_get_parameters (SalutConnectionManager *self, 
                                          const gchar * proto, 
                                          GPtrArray ** ret, 
                                          GError **error);
gboolean 
salut_connection_manager_list_protocols (SalutConnectionManager *self, 
                                          gchar *** ret, 
                                          GError **error);
gboolean 
salut_connection_manager_request_connection (SalutConnectionManager *self, 
                                              const gchar * proto, 
                                              GHashTable * parameters, 
                                              gchar ** ret, 
                                              gchar ** ret1, 
                                              GError **error);


G_END_DECLS

#endif /* #ifndef __SALUT_CONNECTION_MANAGER_H__*/
