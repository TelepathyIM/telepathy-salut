/*
 * handle-set.c - a set which refs a handle when inserted
 *
 * Copyright (C) 2005,2006 Collabora Ltd.
 * Copyright (C) 2005,2006 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */
#include <glib.h>
#include "gintset.h"
#include "handle-repository.h"

#include "handle-set.h"

struct _HandleSet
{
  HandleRepo *repo;
  GIntSet *intset;
  TpHandleType type;
};

/**
 * handle_set_new:
 * @repo: #HandleRepo that holds the handles to be reffed by this set
 *
 * Creates a new #HandleSet
 *
 * Returns: A new #HandleSet
 */
HandleSet *
handle_set_new (HandleRepo *repo, TpHandleType type)
{
  HandleSet *set = g_new(HandleSet, 1);
  set->intset = g_intset_new();
  set->repo = repo;
  set->type = type;

  return set;
}

static void
freer (HandleSet *set, Handle handle, gpointer userdata)
{
  handle_set_remove (set, handle);
}

/**
 * handle_set_destroy:
 * @set:#HandleSet to destroy
 *
 * Delete a #HandleSet and unreference any handles that it holds
 */
void
handle_set_destroy (HandleSet *set)
{
  handle_set_foreach (set, freer, NULL);
  g_intset_destroy (set->intset);
  g_free (set);
}

/**
 * handle_set_peek:
 * @set:#HandleSet to peek at
 *
 * Get the underlying GIntSet used by this HandleSet
 */
GIntSet *
handle_set_peek (HandleSet *set)
{
  return set->intset;
}

/**
 * handle_set_add:
 * @set: #HandleSet to add this handle to
 * @handle: handle to add
 *
 * Add a handle to a #HandleSet,and reference it in the attched
 * #HandleRepo
 *
 */
void
handle_set_add (HandleSet *set, Handle handle)
{
  g_return_if_fail (set != NULL);
  g_return_if_fail (handle != 0);

  if (!g_intset_is_member(set->intset, handle))
    {
      g_return_if_fail (handle_ref (set->repo, set->type, handle));

      g_intset_add (set->intset, handle);
    }
}

/**
 * handle_set_remove:
 * @set: #HandleSet to remove this handle from
 * @handle: handle to remove
 * @type: type of handle
 *
 * Remove a handle to a #HandleSet,and unreference it in the attched
 * #HandleRepo
 *
 * Returns: FALSE if the (handle,type) pair was invalid, or was not in this set
 */

gboolean
handle_set_remove (HandleSet *set, Handle handle)
{
  g_return_val_if_fail (set != NULL, FALSE);
  g_return_val_if_fail (handle != 0, FALSE);

  if (g_intset_is_member(set->intset, handle))
    {
      g_return_val_if_fail (handle_unref (set->repo, set->type, handle), FALSE);

      g_intset_remove (set->intset, handle);
      return TRUE;
    }

  return FALSE;
}

/**
 * handle_set_is_member:
 * @set: A #HandleSet
 * @handle: handle to check
 * @type: type of handle
 *
 * Check if the (handle,type) pair is in this set
 *
 * Returns: TRUE if the (handle,type) pair is in this repo
 *
 */
gboolean
handle_set_is_member (HandleSet *set, Handle handle)
{
  return g_intset_is_member(set->intset, handle);
}

typedef struct __foreach_data
{
  HandleSet *set;
  HandleFunc func;
  gpointer userdata;
} _foreach_data;

static void
foreach_helper(guint i, gpointer userdata)
{
  _foreach_data *data = (_foreach_data*) userdata;

  data->func(data->set, i, data->userdata);
}

void
handle_set_foreach (HandleSet *set, HandleFunc func, gpointer userdata)
{
  _foreach_data data = {set, func, userdata};
  data.set = set;
  data.func = func;
  data.userdata = userdata;
  g_intset_foreach (set->intset, foreach_helper, &data);
}

int
handle_set_size (HandleSet *set)
{
  return g_intset_size (set->intset);
}

GArray *handle_set_to_array (HandleSet *set)
{
  g_return_val_if_fail (set != NULL, NULL);

  return g_intset_to_array (set->intset);
}

static void
ref_one (guint handle, gpointer data)
{
  HandleSet *set = (HandleSet *) data;
  handle_ref (set->repo, set->type, handle);
}

/**
 * handle_set_update:
 * @set: a #HandleSet to update
 * @add: a #GIntSet of handles to add
 *
 * Add a set of handles to a handle set, referencing those which are not
 * already members. The GIntSet returned must be freed with g_intset_destroy.
 *
 * Returns: the handles which were added
 */
GIntSet *
handle_set_update (HandleSet *set, const GIntSet *add)
{
  GIntSet *ret, *tmp;

  g_return_val_if_fail (set != NULL, NULL);
  g_return_val_if_fail (add != NULL, NULL);

  /* reference each of ADD - CURRENT */
  ret = g_intset_difference (add, set->intset);
  g_intset_foreach (ret, ref_one, set);

  /* update CURRENT to be the union of CURRENT and ADD */
  tmp = g_intset_union (add, set->intset);
  g_intset_destroy (set->intset);
  set->intset = tmp;

  return ret;
}

static void
unref_one (guint handle, gpointer data)
{
  HandleSet *set = (HandleSet *) data;
  handle_unref (set->repo, set->type, handle);
}

/**
 * handle_set_difference_update:
 * @set: a #HandleSet to update
 * @remove: a #GIntSet of handles to remove
 *
 * Remove a set of handles from a handle set, dereferencing those which are
 * members. The GIntSet returned must be freed with g_intset_destroy.
 *
 * Returns: the handles which were removed
 */
GIntSet *
handle_set_difference_update (HandleSet *set, const GIntSet *remove)
{
  GIntSet *ret, *tmp;

  g_return_val_if_fail (set != NULL, NULL);
  g_return_val_if_fail (remove != NULL, NULL);

  /* dereference each of REMOVE n CURRENT */
  ret = g_intset_intersection (remove, set->intset);
  g_intset_foreach (ret, unref_one, set);

  /* update CURRENT to be CURRENT - REMOVE */
  tmp = g_intset_difference (set->intset, remove);
  g_intset_destroy (set->intset);
  set->intset = tmp;

  return ret;
}
