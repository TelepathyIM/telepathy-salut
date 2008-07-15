/*
 * salut-ft-manager.h - Header for SalutFtManager
 * Copyright (C) 2007 Marco Barisione <marco@barisione.org>
 * Copyright (C) 2006 Collabora Ltd.
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

#ifndef __SALUT_FT_MANAGER_H__
#define __SALUT_FT_MANAGER_H__

#include <glib-object.h>

#include <salut-connection.h>
#include <salut-im-manager.h>

G_BEGIN_DECLS

typedef struct _SalutFtManager SalutFtManager;
typedef struct _SalutFtManagerClass SalutFtManagerClass;

struct _SalutFtManagerClass {
    GObjectClass parent_class;
};

struct _SalutFtManager {
    GObject parent;
};

GType salut_ft_manager_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_FT_MANAGER \
  (salut_ft_manager_get_type ())
#define SALUT_FT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_FT_MANAGER, SalutFtManager))
#define SALUT_FT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_FT_MANAGER, SalutFtManagerClass))
#define SALUT_IS_FT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_FT_MANAGER))
#define SALUT_IS_FT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_FT_MANAGER))
#define SALUT_FT_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_FT_MANAGER, SalutFtManagerClass))

SalutFtManager *salut_ft_manager_new (SalutConnection *connection,
    SalutContactManager *contact_manager,
    SalutXmppConnectionManager *xmpp_connection_manager);

G_END_DECLS

#endif /* #ifndef __SALUT_FT_MANAGER_H__*/
