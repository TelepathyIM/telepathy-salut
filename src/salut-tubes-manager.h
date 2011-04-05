/*
 * salut-tubes-manager.h - Header for SalutTubesManager
 * Copyright (C) 2006-2008 Collabora Ltd.
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

#ifndef __SALUT_TUBES_MANAGER_H__
#define __SALUT_TUBES_MANAGER_H__

#include <glib-object.h>

#include <telepathy-glib/base-connection.h>
#include "salut-connection.h"
#include "salut-contact-manager.h"
#include "salut-tubes-channel.h"

G_BEGIN_DECLS

typedef struct _SalutTubesManagerClass SalutTubesManagerClass;
typedef struct _SalutTubesManager SalutTubesManager;

struct _SalutTubesManagerClass {
  GObjectClass parent_class;
};

struct _SalutTubesManager {
  GObject parent;

  gpointer priv;
};

GType salut_tubes_manager_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_TUBES_MANAGER \
  (salut_tubes_manager_get_type ())
#define SALUT_TUBES_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_TUBES_MANAGER,\
                              SalutTubesManager))
#define SALUT_TUBES_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_TUBES_MANAGER,\
                           SalutTubesManagerClass))
#define SALUT_IS_TUBES_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_TUBES_MANAGER))
#define SALUT_IS_TUBES_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_TUBES_MANAGER))
#define SALUT_TUBES_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_TUBES_MANAGER,\
                              SalutTubesManagerClass))

SalutTubesManager * salut_tubes_manager_new (
    SalutConnection *conn,
    SalutContactManager *contact_manager);

G_END_DECLS

#endif /* #ifndef __SALUT_TUBES_MANAGER_H__ */

