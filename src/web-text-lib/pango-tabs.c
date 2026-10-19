/* Pango
 * pango-tabs.c: Tab-related stuff
 *
 * Copyright (C) 2000 Red Hat Software
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"
#include "pango-tabs.h"
#include "pango-impl-utils.h"
#include <stdlib.h>
#include <string.h>

typedef struct _PangoTab NsPangoTab;

struct _PangoTab
{
  int location;
  NsPangoTabAlign alignment;
  gunichar decimal_point;
};

/**
 * NsPangoTabArray:
 *
 * A `NsPangoTabArray` contains an array of tab stops.
 *
 * `NsPangoTabArray` can be used to set tab stops in a `NsPangoLayout`.
 * Each tab stop has an alignment, a position, and optionally
 * a character to use as decimal point.
 */
struct _PangoTabArray
{
  gint size;
  gint allocated;
  gboolean positions_in_pixels;
  NsPangoTab *tabs;
};

static void
init_tabs (NsPangoTabArray *array, gint start, gint end)
{
  while (start < end)
    {
      array->tabs[start].location = 0;
      array->tabs[start].alignment = NS_PANGO_TAB_LEFT;
      array->tabs[start].decimal_point = 0;
      ++start;
    }
}

/**
 * ns_pango_tab_array_new:
 * @initial_size: Initial number of tab stops to allocate, can be 0
 * @positions_in_pixels: whether positions are in pixel units
 *
 * Creates an array of @initial_size tab stops.
 *
 * Tab stops are specified in pixel units if @positions_in_pixels is %TRUE,
 * otherwise in Pango units. All stops are initially at position 0.
 *
 * Return value: the newly allocated `NsPangoTabArray`, which should
 *   be freed with [method@Pango.TabArray.free].
 */
NsPangoTabArray*
ns_pango_tab_array_new (gint     initial_size,
                     gboolean positions_in_pixels)
{
  NsPangoTabArray *array;

  g_return_val_if_fail (initial_size >= 0, NULL);

  /* alloc enough to treat array->tabs as an array of length
   * size, though it's declared as an array of length 1.
   * If we allowed tab array resizing we'd need to drop this
   * optimization.
   */
  array = g_slice_new (NsPangoTabArray);
  array->size = initial_size; array->allocated = initial_size;

  if (array->allocated > 0)
    {
      array->tabs = g_new (NsPangoTab, array->allocated);
      init_tabs (array, 0, array->allocated);
    }
  else
    array->tabs = NULL;

  array->positions_in_pixels = positions_in_pixels;

  return array;
}

/**
 * ns_pango_tab_array_copy:
 * @src: `NsPangoTabArray` to copy
 *
 * Copies a `NsPangoTabArray`.
 *
 * Return value: the newly allocated `NsPangoTabArray`, which should
 *   be freed with [method@Pango.TabArray.free].
 */
NsPangoTabArray*
ns_pango_tab_array_copy (NsPangoTabArray *src)
{
  NsPangoTabArray *copy;

  g_return_val_if_fail (src != NULL, NULL);

  copy = ns_pango_tab_array_new (src->size, src->positions_in_pixels);

  if (copy->tabs)
    memcpy (copy->tabs, src->tabs, sizeof(NsPangoTab) * src->size);

  return copy;
}

/**
 * ns_pango_tab_array_free:
 * @tab_array: a `NsPangoTabArray`
 *
 * Frees a tab array and associated resources.
 */
void
ns_pango_tab_array_free (NsPangoTabArray *tab_array)
{
  g_return_if_fail (tab_array != NULL);

  g_free (tab_array->tabs);

  g_slice_free (NsPangoTabArray, tab_array);
}

/**
 * ns_pango_tab_array_get_size:
 * @tab_array: a `NsPangoTabArray`
 *
 * Gets the number of tab stops in @tab_array.
 *
 * Return value: the number of tab stops in the array.
 */
gint
ns_pango_tab_array_get_size (NsPangoTabArray *tab_array)
{
  g_return_val_if_fail (tab_array != NULL, 0);

  return tab_array->size;
}

/**
 * ns_pango_tab_array_resize:
 * @tab_array: a `NsPangoTabArray`
 * @new_size: new size of the array
 *
 * Resizes a tab array.
 *
 * You must subsequently initialize any tabs
 * that were added as a result of growing the array.
 */
void
ns_pango_tab_array_resize (NsPangoTabArray *tab_array,
                        gint           new_size)
{
  if (new_size > tab_array->allocated)
    {
      gint current_end = tab_array->allocated;

      /* Ratchet allocated size up above the index. */
      if (tab_array->allocated == 0)
        tab_array->allocated = 2;

      while (new_size > tab_array->allocated)
        tab_array->allocated = tab_array->allocated * 2;

      tab_array->tabs = g_renew (NsPangoTab, tab_array->tabs,
                                 tab_array->allocated);

      init_tabs (tab_array, current_end, tab_array->allocated);
    }

  tab_array->size = new_size;
}

/**
 * ns_pango_tab_array_set_tab:
 * @tab_array: a `NsPangoTabArray`
 * @tab_index: the index of a tab stop
 * @alignment: tab alignment
 * @location: tab location in Pango units
 *
 * Sets the alignment and location of a tab stop.
 */
void
ns_pango_tab_array_set_tab (NsPangoTabArray *tab_array,
                         gint           tab_index,
                         NsPangoTabAlign  alignment,
                         gint           location)
{
  g_return_if_fail (tab_array != NULL);
  g_return_if_fail (tab_index >= 0);
  g_return_if_fail (location >= 0);

  if (tab_index >= tab_array->size)
    ns_pango_tab_array_resize (tab_array, tab_index + 1);

  tab_array->tabs[tab_index].alignment = alignment;
  tab_array->tabs[tab_index].location = location;
}

/**
 * ns_pango_tab_array_get_tab:
 * @tab_array: a `NsPangoTabArray`
 * @tab_index: tab stop index
 * @alignment: (out) (optional): location to store alignment
 * @location: (out) (optional): location to store tab position
 *
 * Gets the alignment and position of a tab stop.
 */
void
ns_pango_tab_array_get_tab (NsPangoTabArray *tab_array,
                         gint           tab_index,
                         NsPangoTabAlign *alignment,
                         gint          *location)
{
  g_return_if_fail (tab_array != NULL);
  g_return_if_fail (tab_index < tab_array->size);
  g_return_if_fail (tab_index >= 0);

  if (alignment)
    *alignment = tab_array->tabs[tab_index].alignment;

  if (location)
    *location = tab_array->tabs[tab_index].location;
}

/**
 * ns_pango_tab_array_get_positions_in_pixels:
 * @tab_array: a `NsPangoTabArray`
 *
 * Returns %TRUE if the tab positions are in pixels,
 * %FALSE if they are in Pango units.
 *
 * Return value: whether positions are in pixels.
 */
gboolean
ns_pango_tab_array_get_positions_in_pixels (NsPangoTabArray *tab_array)
{
  g_return_val_if_fail (tab_array != NULL, FALSE);

  return tab_array->positions_in_pixels;
}

/**
 * ns_pango_tab_array_get_decimal_point:
 * @tab_array: a `NsPangoTabArray`
 * @tab_index: the index of a tab stop
 *
 * Gets the Unicode character to use as decimal point.
 *
 * This is only relevant for tabs with %NS_PANGO_TAB_DECIMAL alignment,
 * which align content at the first occurrence of the decimal point
 * character.
 *
 * The default value of 0 means that Pango will use the
 * decimal point according to the current locale.
 *
 * Since: 1.50
 */
gunichar
ns_pango_tab_array_get_decimal_point (NsPangoTabArray *tab_array,
                                   int            tab_index)
{
  g_return_val_if_fail (tab_array != NULL, 0);
  g_return_val_if_fail (tab_index < tab_array->size, 0);
  g_return_val_if_fail (tab_index >= 0, 0);

  return tab_array->tabs[tab_index].decimal_point;
}

static int
compare_tabs (const void *p1, const void *p2)
{
  const NsPangoTab *t1 = p1;
  const NsPangoTab *t2 = p2;

  return t1->location - t2->location;
}

/**
 * ns_pango_tab_array_sort:
 * @tab_array: a `NsPangoTabArray`
 *
 * Utility function to ensure that the tab stops are in increasing order.
 *
 * Since: 1.50
 */
void
ns_pango_tab_array_sort (NsPangoTabArray *tab_array)
{
  g_return_if_fail (tab_array != NULL);

  qsort (tab_array->tabs, tab_array->size, sizeof (NsPangoTab), compare_tabs);
}
