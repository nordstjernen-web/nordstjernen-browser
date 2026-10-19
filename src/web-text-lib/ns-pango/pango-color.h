/* Pango
 * pango-color.h: A color struct
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

#ifndef __NS_PANGO_COLOR_H__
#define __NS_PANGO_COLOR_H__

#include <ns-pango/pango-types.h>
#include <glib-object.h>

G_BEGIN_DECLS


typedef struct _PangoColor NsPangoColor;

/**
 * NsPangoColor:
 * @red: value of red component
 * @green: value of green component
 * @blue: value of blue component
 *
 * The `NsPangoColor` structure is used to
 * represent a color in an uncalibrated RGB color-space.
 */
struct _PangoColor
{
  guint16 red;
  guint16 green;
  guint16 blue;
};

#define NS_TYPE_PANGO_COLOR (ns_pango_color_get_type ())

NS_PANGO_AVAILABLE_IN_ALL
GType       ns_pango_color_get_type         (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_ALL
NsPangoColor *ns_pango_color_copy             (const NsPangoColor *src);

NS_PANGO_AVAILABLE_IN_ALL
void        ns_pango_color_free             (NsPangoColor       *color);

NS_PANGO_AVAILABLE_IN_ALL
gboolean    ns_pango_color_parse            (NsPangoColor       *color,
                                          const char       *spec);

NS_PANGO_AVAILABLE_IN_1_46
gboolean    ns_pango_color_parse_with_alpha (NsPangoColor       *color,
                                          guint16          *alpha,
                                          const char       *spec);

NS_PANGO_AVAILABLE_IN_1_16
char       *ns_pango_color_to_string        (const NsPangoColor *color);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (NsPangoColor, ns_pango_color_free)

G_END_DECLS

#endif /* __NS_PANGO_COLOR_H__ */
