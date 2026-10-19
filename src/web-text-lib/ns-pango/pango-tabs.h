/* Pango
 * pango-tabs.h: Tab-related stuff
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __NS_PANGO_TABS_H__
#define __NS_PANGO_TABS_H__

#include <ns-pango/pango-types.h>

G_BEGIN_DECLS

typedef struct _PangoTabArray NsPangoTabArray;

/**
 * NsPangoTabAlign:
 * @NS_PANGO_TAB_LEFT: the text appears to the right of the tab stop position
 * @NS_PANGO_TAB_RIGHT: the text appears to the left of the tab stop position
 *   until the available space is filled. Since: 1.50
 * @NS_PANGO_TAB_CENTER: the text is centered at the tab stop position
 *   until the available space is filled. Since: 1.50
 * @NS_PANGO_TAB_DECIMAL: text before the first occurrence of the decimal point
 *   character appears to the left of the tab stop position (until the available
 *   space is filled), the rest to the right. Since: 1.50
 *
 * `NsPangoTabAlign` specifies where the text appears relative to the tab stop
 * position.
 */
typedef enum
{
  NS_PANGO_TAB_LEFT,
  NS_PANGO_TAB_RIGHT,
  NS_PANGO_TAB_CENTER,
  NS_PANGO_TAB_DECIMAL
} NsPangoTabAlign;

#define NS_TYPE_PANGO_TAB_ARRAY (ns_pango_tab_array_get_type ())

NS_PANGO_AVAILABLE_IN_ALL
NsPangoTabArray  *ns_pango_tab_array_new                 (gint           initial_size,
						     gboolean       positions_in_pixels);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoTabArray  *ns_pango_tab_array_new_with_positions  (gint           size,
						     gboolean       positions_in_pixels,
						     NsPangoTabAlign  first_alignment,
						     gint           first_position,
						     ...);
NS_PANGO_AVAILABLE_IN_ALL
GType           ns_pango_tab_array_get_type            (void) G_GNUC_CONST;
NS_PANGO_AVAILABLE_IN_ALL
NsPangoTabArray  *ns_pango_tab_array_copy                (NsPangoTabArray *src);
NS_PANGO_AVAILABLE_IN_ALL
void            ns_pango_tab_array_free                (NsPangoTabArray *tab_array);
NS_PANGO_AVAILABLE_IN_ALL
gint            ns_pango_tab_array_get_size            (NsPangoTabArray *tab_array);
NS_PANGO_AVAILABLE_IN_ALL
void            ns_pango_tab_array_resize              (NsPangoTabArray *tab_array,
						     gint           new_size);
NS_PANGO_AVAILABLE_IN_ALL
void            ns_pango_tab_array_set_tab             (NsPangoTabArray *tab_array,
						     gint           tab_index,
						     NsPangoTabAlign  alignment,
						     gint           location);
NS_PANGO_AVAILABLE_IN_ALL
void            ns_pango_tab_array_get_tab             (NsPangoTabArray *tab_array,
						     gint           tab_index,
						     NsPangoTabAlign *alignment,
						     gint          *location);
NS_PANGO_AVAILABLE_IN_ALL
void            ns_pango_tab_array_get_tabs            (NsPangoTabArray *tab_array,
						     NsPangoTabAlign **alignments,
						     gint          **locations);

NS_PANGO_AVAILABLE_IN_ALL
gboolean        ns_pango_tab_array_get_positions_in_pixels (NsPangoTabArray *tab_array);

NS_PANGO_AVAILABLE_IN_1_50
void            ns_pango_tab_array_set_positions_in_pixels (NsPangoTabArray *tab_array,
                                                         gboolean       positions_in_pixels);

NS_PANGO_AVAILABLE_IN_1_50
char *          ns_pango_tab_array_to_string           (NsPangoTabArray *tab_array);
NS_PANGO_AVAILABLE_IN_1_50
NsPangoTabArray * ns_pango_tab_array_from_string         (const char    *text);

NS_PANGO_AVAILABLE_IN_1_50
void            ns_pango_tab_array_set_decimal_point   (NsPangoTabArray *tab_array,
                                                     int            tab_index,
                                                     gunichar       decimal_point);
NS_PANGO_AVAILABLE_IN_1_50
gunichar        ns_pango_tab_array_get_decimal_point   (NsPangoTabArray *tab_array,
                                                     int            tab_index);

NS_PANGO_AVAILABLE_IN_1_50
void            ns_pango_tab_array_sort                (NsPangoTabArray *tab_array);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(NsPangoTabArray, ns_pango_tab_array_free)

G_END_DECLS

#endif /* __NS_PANGO_TABS_H__ */
