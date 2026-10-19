/* Pango
 * pangocairo-fc.h: Private header file for Cairo/fontconfig combination
 *
 * Copyright (C) 2005 Red Hat, Inc.
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

#ifndef __NS_PANGOCAIRO_FC_H__
#define __NS_PANGOCAIRO_FC_H__

#include <ns-pango/pangofc-fontmap.h>
#include <ns-pango/pangocairo.h>

G_BEGIN_DECLS

#ifdef __GI_SCANNER__
#define NS_PANGO_CAIRO_TYPE_FC_FONT_MAP    (ns_pango_cairo_fc_font_map_get_type())
#define NS_PANGO_CAIRO_FC_FONT_MAP(obj)    (G_TYPE_CHECK_INSTANCE_CAST ((obj), NS_PANGO_CAIRO_TYPE_FC_FONT_MAP, NsPangoCairoFcFontMap))
#define NS_PANGO_CAIRO_IS_FC_FONT_MAP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NS_PANGO_CAIRO_TYPE_FC_FONT_MAP))
#else
#define NS_TYPE_PANGO_CAIRO_FC_FONT_MAP       (ns_pango_cairo_fc_font_map_get_type ())
#define NS_PANGO_CAIRO_FC_FONT_MAP(object)    (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_TYPE_PANGO_CAIRO_FC_FONT_MAP, NsPangoCairoFcFontMap))
#define NS_PANGO_IS_CAIRO_FC_FONT_MAP(object) (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_TYPE_PANGO_CAIRO_FC_FONT_MAP))
#endif

typedef struct _PangoCairoFcFontMap NsPangoCairoFcFontMap;

NS_PANGO_AVAILABLE_IN_ALL
GType ns_pango_cairo_fc_font_map_get_type (void) G_GNUC_CONST;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (NsPangoCairoFcFontMap, g_object_unref)

G_END_DECLS

#endif /* __NS_PANGOCAIRO_FC_H__ */

