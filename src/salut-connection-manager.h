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
#include <telepathy-glib/base-connection-manager.h>

G_BEGIN_DECLS

typedef struct _SalutConnectionManager SalutConnectionManager;
typedef struct _SalutConnectionManagerClass SalutConnectionManagerClass;

struct _SalutConnectionManagerClass {
  TpBaseConnectionManagerClass parent_class;
};

struct _SalutConnectionManager {
  TpBaseConnectionManager parent;
};

extern const TpCMProtocolSpec salut_protocols[];

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

G_END_DECLS

#endif /* #ifndef __SALUT_CONNECTION_MANAGER_H__*/
