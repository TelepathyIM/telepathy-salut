/*
 * util.h - Headers for Salut utility functions
 * Copyright (C) 2006-2007 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
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

#ifndef __SALUT_UTIL_H__
#define __SALUT_UTIL_H__

#include <glib.h>
#include <glib-object.h>
#include <wocky/wocky.h>

/* Mapping a XMPP node with a GHashTable */
GHashTable *salut_wocky_node_extract_properties (WockyNode *node,
    const gchar *prop);
void salut_wocky_node_add_children_from_properties (WockyNode *node,
    GHashTable *properties, const gchar *prop);
gchar *salut_generate_id (void);

typedef enum
{
  SALUT_TUBE_TYPE_STREAM,
  SALUT_TUBE_TYPE_DBUS,
} SalutTubeType;

#endif /* __SALUT_UTIL_H__ */
