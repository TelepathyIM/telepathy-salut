/*
 * salut-avahi-errors.h - Header for Avahi error types
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

#ifndef __SALUT_AVAHI_ERRORS_H__
#define __SALUT_AVAHI_ERRORS_H__

#include <glib-object.h>

G_BEGIN_DECLS

#include <avahi-common/error.h>
GQuark salut_avahi_errors_quark (void);
#define SALUT_AVAHI_ERRORS salut_avahi_errors_quark ()

G_END_DECLS

#endif /* #ifndef __SALUT_AVAHI_ERRORS_H__*/
