/* Pango
 * pangocairo.h:
 *
 * Copyright (C) 1999, 2004 Red Hat, Inc.
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

#ifndef __NS_PANGOCAIRO_H__
#define __NS_PANGOCAIRO_H__

#include <ns-pango/pango.h>
#include <cairo.h>

G_BEGIN_DECLS

/**
 * NsPangoCairoFont:
 *
 * `NsPangoCairoFont` is an interface exported by fonts for
 * use with Cairo.
 *
 * The actual type of the font will depend on the particular
 * font technology Cairo was compiled to use.
 *
 * Since: 1.18
 **/
typedef struct _PangoCairoFont      NsPangoCairoFont;

/* This is a hack because NsPangoCairo is hijacking the Pango namespace, but
 * consumers of the NsPangoCairo API expect these symbols to live under the
 * NsPangoCairo namespace.
 */
#ifdef __GI_SCANNER__
#define NS_PANGO_CAIRO_TYPE_FONT           (ns_pango_cairo_font_get_type())
#define NS_PANGO_CAIRO_FONT(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), NS_PANGO_CAIRO_TYPE_FONT, NsPangoCairoFont))
#define NS_PANGO_CAIRO_IS_FONT(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NS_PANGO_CAIRO_TYPE_FONT))
#else
#define NS_TYPE_PANGO_CAIRO_FONT           (ns_pango_cairo_font_get_type ())
#define NS_PANGO_CAIRO_FONT(object)        (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_TYPE_PANGO_CAIRO_FONT, NsPangoCairoFont))
#define NS_PANGO_IS_CAIRO_FONT(object)     (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_TYPE_PANGO_CAIRO_FONT))
#endif

/**
 * NsPangoCairoFontMap:
 *
 * `NsPangoCairoFontMap` is an interface exported by font maps for
 * use with Cairo.
 *
 * The actual type of the font map will depend on the particular
 * font technology Cairo was compiled to use.
 *
 * Since: 1.10
 **/
typedef struct _PangoCairoFontMap        NsPangoCairoFontMap;

#ifdef __GI_SCANNER__
#define NS_PANGO_CAIRO_TYPE_FONT_MAP       (ns_pango_cairo_font_map_get_type())
#define NS_PANGO_CAIRO_FONT_MAP(obj)       (G_TYPE_CHECK_INSTANCE_CAST ((obj), NS_PANGO_CAIRO_TYPE_FONT_MAP, NsPangoCairoFontMap))
#define NS_PANGO_CAIRO_IS_FONT_MAP(obj)    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NS_PANGO_CAIRO_TYPE_FONT_MAP))
#else
#define NS_TYPE_PANGO_CAIRO_FONT_MAP       (ns_pango_cairo_font_map_get_type ())
#define NS_PANGO_CAIRO_FONT_MAP(object)    (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_TYPE_PANGO_CAIRO_FONT_MAP, NsPangoCairoFontMap))
#define NS_PANGO_IS_CAIRO_FONT_MAP(object) (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_TYPE_PANGO_CAIRO_FONT_MAP))
#endif

/**
 * NsPangoCairoShapeRendererFunc:
 * @cr: a Cairo context with current point set to where the shape should
 * be rendered
 * @attr: the %NS_PANGO_ATTR_SHAPE to render
 * @do_path: whether only the shape path should be appended to current
 * path of @cr and no filling/stroking done.  This will be set
 * to %TRUE when called from ns_pango_cairo_layout_path() and
 * ns_pango_cairo_layout_line_path() rendering functions.
 * @data: (closure): user data passed to ns_pango_cairo_context_set_shape_renderer()
 *
 * Function type for rendering attributes of type %NS_PANGO_ATTR_SHAPE
 * with Pango's Cairo renderer.
 */
typedef void (* NsPangoCairoShapeRendererFunc) (cairo_t        *cr,
					      NsPangoAttrShape *attr,
					      gboolean        do_path,
					      gpointer        data);

/*
 * NsPangoCairoFontMap
 */
NS_PANGO_AVAILABLE_IN_1_10
GType         ns_pango_cairo_font_map_get_type          (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_1_10
NsPangoFontMap *ns_pango_cairo_font_map_new               (void);
NS_PANGO_AVAILABLE_IN_1_18
NsPangoFontMap *ns_pango_cairo_font_map_new_for_font_type (cairo_font_type_t fonttype);
NS_PANGO_AVAILABLE_IN_1_10
NsPangoFontMap *ns_pango_cairo_font_map_get_default       (void);
NS_PANGO_AVAILABLE_IN_1_22
void          ns_pango_cairo_font_map_set_default       (NsPangoCairoFontMap *fontmap);
NS_PANGO_AVAILABLE_IN_1_18
cairo_font_type_t ns_pango_cairo_font_map_get_font_type (NsPangoCairoFontMap *fontmap);

NS_PANGO_AVAILABLE_IN_1_10
void          ns_pango_cairo_font_map_set_resolution (NsPangoCairoFontMap *fontmap,
						   double             dpi);
NS_PANGO_AVAILABLE_IN_1_10
double        ns_pango_cairo_font_map_get_resolution (NsPangoCairoFontMap *fontmap);
#ifndef NS_PANGO_DISABLE_DEPRECATED
NS_PANGO_DEPRECATED_IN_1_22_FOR(ns_pango_font_map_create_context)
NsPangoContext *ns_pango_cairo_font_map_create_context (NsPangoCairoFontMap *fontmap);
#endif

/*
 * NsPangoCairoFont
 */
NS_PANGO_AVAILABLE_IN_1_18
GType         ns_pango_cairo_font_get_type               (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_1_18
cairo_scaled_font_t *ns_pango_cairo_font_get_scaled_font (NsPangoCairoFont *font);

/* Update a Pango context for the current state of a cairo context
 */
NS_PANGO_AVAILABLE_IN_1_10
void         ns_pango_cairo_update_context (cairo_t      *cr,
					 NsPangoContext *context);

NS_PANGO_AVAILABLE_IN_1_10
void                        ns_pango_cairo_context_set_font_options (NsPangoContext               *context,
								  const cairo_font_options_t *options);
NS_PANGO_AVAILABLE_IN_1_10
const cairo_font_options_t *ns_pango_cairo_context_get_font_options (NsPangoContext               *context);

NS_PANGO_AVAILABLE_IN_1_10
void               ns_pango_cairo_context_set_resolution     (NsPangoContext       *context,
							   double              dpi);
NS_PANGO_AVAILABLE_IN_1_10
double             ns_pango_cairo_context_get_resolution     (NsPangoContext       *context);

NS_PANGO_AVAILABLE_IN_1_18
void                        ns_pango_cairo_context_set_shape_renderer (NsPangoContext                *context,
								    NsPangoCairoShapeRendererFunc  func,
								    gpointer                     data,
								    GDestroyNotify               dnotify);
NS_PANGO_AVAILABLE_IN_1_18
NsPangoCairoShapeRendererFunc ns_pango_cairo_context_get_shape_renderer (NsPangoContext                *context,
								    gpointer                    *data);

/* Convenience
 */
NS_PANGO_AVAILABLE_IN_1_22
NsPangoContext *ns_pango_cairo_create_context (cairo_t   *cr);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoLayout *ns_pango_cairo_create_layout (cairo_t     *cr);
NS_PANGO_AVAILABLE_IN_1_10
void         ns_pango_cairo_update_layout (cairo_t     *cr,
					NsPangoLayout *layout);

/*
 * Rendering
 */
NS_PANGO_AVAILABLE_IN_1_10
void ns_pango_cairo_show_glyph_string (cairo_t          *cr,
				    NsPangoFont        *font,
				    NsPangoGlyphString *glyphs);
NS_PANGO_AVAILABLE_IN_1_22
void ns_pango_cairo_show_glyph_item   (cairo_t          *cr,
				    const char       *text,
				    NsPangoGlyphItem   *glyph_item);
NS_PANGO_AVAILABLE_IN_1_10
void ns_pango_cairo_show_layout_line  (cairo_t          *cr,
				    NsPangoLayoutLine  *line);
NS_PANGO_AVAILABLE_IN_1_10
void ns_pango_cairo_show_layout       (cairo_t          *cr,
				    NsPangoLayout      *layout);

NS_PANGO_AVAILABLE_IN_1_14
void ns_pango_cairo_show_error_underline (cairo_t       *cr,
				       double         x,
				       double         y,
				       double         width,
				       double         height);

/*
 * Rendering to a path
 */
NS_PANGO_AVAILABLE_IN_1_10
void ns_pango_cairo_glyph_string_path (cairo_t          *cr,
				    NsPangoFont        *font,
				    NsPangoGlyphString *glyphs);
NS_PANGO_AVAILABLE_IN_1_10
void ns_pango_cairo_layout_line_path  (cairo_t          *cr,
				    NsPangoLayoutLine  *line);
NS_PANGO_AVAILABLE_IN_1_10
void ns_pango_cairo_layout_path       (cairo_t          *cr,
				    NsPangoLayout      *layout);
NS_PANGO_AVAILABLE_IN_1_58
void ns_pango_cairo_layout_path_for_components (cairo_t              *cr,
                                             NsPangoLayout          *layout,
                                             NsPangoRenderComponent  components);

NS_PANGO_AVAILABLE_IN_1_14
void ns_pango_cairo_error_underline_path (cairo_t       *cr,
				       double         x,
				       double         y,
				       double         width,
				       double         height);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (NsPangoCairoFont, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (NsPangoCairoFontMap, g_object_unref)

G_END_DECLS

#endif /* __NS_PANGOCAIRO_H__ */
