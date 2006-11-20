/*
 * util.h - Headers for Gabble utility functions
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *   @author Robert McQueen <robert.mcqueen@collabora.co.uk>
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

#include <glib.h>
#include <loudmouth/loudmouth.h>


#ifndef __SALUT_UTIL_H__
#define __SALUT_UTIL_H__

void lm_message_node_steal_children (LmMessageNode *snatcher, LmMessageNode *mum);
gboolean lm_message_node_has_namespace (LmMessageNode *node, const gchar *ns, const gchar *tag);
LmMessageNode *lm_message_node_get_child_with_namespace (LmMessageNode *node, const gchar *name, const gchar *ns);

#endif /* __SALUT_UTIL_H__ */
