/* SalutConnection Requests (requestotron) implementation
 *
 * Copyright (C) 2008 Collabora Ltd.
 * Copyright (C) 2008 Nokia Corporation
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

#ifndef SALUT_CONN_REQUESTS_H
#define SALUT_CONN_REQUESTS_H

#include <glib.h>

#include "salut-connection.h"

G_BEGIN_DECLS

void salut_conn_requests_get_dbus_property (GObject *object,
    GQuark interface, GQuark name, GValue *value, gpointer unused);

void salut_conn_requests_init (SalutConnection *self);
void salut_conn_requests_dispose (SalutConnection *self);
void salut_conn_requests_iface_init (gpointer, gpointer);
void salut_conn_requests_conn_iface_init (gpointer, gpointer);

G_END_DECLS

#endif

