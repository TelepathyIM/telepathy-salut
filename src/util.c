/*
 * util.c - Source for Gabble utility functions
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
#include <string.h>

#include "util.h"

static gboolean
g_strdiff (const gchar *left, const gchar *right)
{
  if ((NULL == left) != (NULL == right))
    return TRUE;

  else if (left == right)
    return FALSE;

  else
    return (0 != strcmp (left, right));
}

void
lm_message_node_steal_children (LmMessageNode *snatcher,
                                LmMessageNode *mum)
{
  LmMessageNode *baby;

  g_return_if_fail (snatcher->children == NULL);

  if (mum->children == NULL)
    return;

  snatcher->children = mum->children;
  mum->children = NULL;

  for (baby = snatcher->children;
       baby != NULL;
       baby = baby->next)
    baby->parent = snatcher;
}

gboolean
lm_message_node_has_namespace (LmMessageNode *node,
                               const gchar *ns,
                               const gchar *tag)
{
  gchar *attribute = NULL;
  const gchar *node_ns;
  gboolean ret;

  if (tag != NULL)
    attribute = g_strconcat ("xmlns:", tag, NULL);

  node_ns = lm_message_node_get_attribute (node,
      tag != NULL ? attribute : "xmlns");

  ret = !g_strdiff (node_ns, ns);

  g_free (attribute);

  return ret;
}

LmMessageNode *
lm_message_node_get_child_with_namespace (LmMessageNode *node,
                                          const gchar *name,
                                          const gchar *ns)
{
  LmMessageNode *tmp;

  for (tmp = node->children;
       tmp != NULL;
       tmp = tmp->next)
    {
      gchar *tag = NULL;
      gboolean found;

      if (g_strdiff (tmp->name, name))
        {
          const gchar *suffix;

          suffix = strchr (tmp->name, ':');

          if (suffix == NULL)
            continue;
          else
            suffix++;

          if (g_strdiff (suffix, name))
            continue;

          tag = g_strndup (tmp->name, suffix - tmp->name - 1);
        }

      found = lm_message_node_has_namespace (tmp, ns, tag);

      g_free (tag);

      if (found)
        return tmp;
    }

  return NULL;
}

/* extend a pointer by an offset, provided the offset is not 0 */
gpointer
mixin_offset_cast (gpointer instance, guint offset)
{
  g_return_val_if_fail (offset != 0, NULL);

  return ((guchar *) instance + offset);
}
