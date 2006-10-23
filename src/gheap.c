/*
 * Header file for GHeap
 *
 * Copyright (C) 2006 Nokia Corporation. All rights reserved.
 *
 * Contact: Olli Salli (Nokia-M/Helsinki) <olli.salli@nokia.com>
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
#include "gheap.h"

#define DEFAULT_SIZE 64

struct _GHeap
{
  GPtrArray *data;
  GCompareFunc comparator;
};

GHeap *
g_heap_new (GCompareFunc comparator)
{
  GHeap *ret = g_slice_new (GHeap);
  g_assert (comparator != NULL);

  ret->data = g_ptr_array_sized_new (DEFAULT_SIZE);
  ret->comparator = comparator;

  return ret;
}

void
g_heap_destroy (GHeap * heap)
{
  g_return_if_fail (heap != NULL);

  g_ptr_array_free (heap->data, TRUE);
  g_slice_free (GHeap, heap);
}

void
g_heap_clear (GHeap *heap)
{
  g_return_if_fail (heap != NULL);

  g_ptr_array_free (heap->data, TRUE);
  heap->data = g_ptr_array_sized_new (DEFAULT_SIZE);
}

#define HEAP_INDEX(heap, index) (g_ptr_array_index ((heap)->data, (index)-1))

void
g_heap_add (GHeap *heap, gpointer element)
{
  guint m;

  g_return_if_fail (heap != NULL);

  g_ptr_array_add (heap->data, element);
  m = heap->data->len;
  while (m != 1)
    {
      gpointer parent = HEAP_INDEX (heap, m / 2);

      if (heap->comparator (element, parent) == -1)
        {
          HEAP_INDEX (heap, m / 2) = element;
          HEAP_INDEX (heap, m) = parent;
          m /= 2;
        }
      else
        break;
    }
}

gpointer
g_heap_peek_first (GHeap *heap)
{
  g_return_val_if_fail (heap != NULL, NULL);

  if (heap->data->len > 0)
    return HEAP_INDEX (heap, 1);
  else
    return NULL;
}

gpointer
g_heap_extract_first (GHeap * heap)
{
  gpointer ret;

  g_return_val_if_fail (heap != NULL, NULL);

  if (heap->data->len > 0)
    {
      guint m = heap->data->len;
      guint i = 1, j;
      ret = HEAP_INDEX (heap, 1);

      HEAP_INDEX (heap, 1) = HEAP_INDEX (heap, m);

      while (i * 2 <= m)
        {
          /* select the child which is supposed to come FIRST */
          if ((i * 2 + 1 <= m)
              && (heap->
                  comparator (HEAP_INDEX (heap, i * 2),
                              HEAP_INDEX (heap, i * 2 + 1)) == 1))
            j = i * 2 + 1;
          else
            j = i * 2;

          if (heap->comparator (HEAP_INDEX (heap, i), HEAP_INDEX (heap, j)) ==
              1)
            {
              gpointer tmp = HEAP_INDEX (heap, i);
              HEAP_INDEX (heap, i) = HEAP_INDEX (heap, j);
              HEAP_INDEX (heap, j) = tmp;
              i = j;
            }
          else
            break;
        }

      g_ptr_array_remove_index (heap->data, m - 1);
    }
  else
    ret = NULL;

  return ret;
}

guint
g_heap_size (GHeap *heap)
{
  g_return_val_if_fail (heap != NULL, 0);

  return heap->data->len;
}
