/* Pango
 * pangocairo-private.h: private symbols for the Cairo backend
 *
 * Copyright (C) 2000,2004 Red Hat, Inc.
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

#ifndef __NS_PANGOCAIRO_PRIVATE_H__
#define __NS_PANGOCAIRO_PRIVATE_H__

#include <ns-pango/pangocairo.h>
#include <ns-pango/pango-renderer.h>

G_BEGIN_DECLS


#define NS_PANGO_CAIRO_FONT_MAP_GET_IFACE(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), NS_TYPE_PANGO_CAIRO_FONT_MAP, NsPangoCairoFontMapIface))

typedef struct _PangoCairoFontMapIface NsPangoCairoFontMapIface;

struct _PangoCairoFontMapIface
{
  GTypeInterface g_iface;

  void           (*set_resolution) (NsPangoCairoFontMap *fontmap,
				    double             dpi);
  double         (*get_resolution) (NsPangoCairoFontMap *fontmap);

  cairo_font_type_t (*get_font_type) (NsPangoCairoFontMap *fontmap);
};


#define NS_PANGO_CAIRO_FONT_GET_IFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), NS_TYPE_PANGO_CAIRO_FONT, NsPangoCairoFontIface))

typedef struct _PangoCairoFontIface                  NsPangoCairoFontIface;
typedef struct _PangoCairoFontPrivate                NsPangoCairoFontPrivate;
typedef struct _PangoCairoFontHexBoxInfo             NsPangoCairoFontHexBoxInfo;
typedef struct _PangoCairoFontPrivateScaledFontData  NsPangoCairoFontPrivateScaledFontData;
typedef struct _PangoCairoFontGlyphExtentsCacheEntry NsPangoCairoFontGlyphExtentsCacheEntry;

struct _PangoCairoFontHexBoxInfo
{
  NsPangoCairoFont *font;
  int rows;
  double digit_width;
  double digit_height;
  double pad_x;
  double pad_y;
  double line_width;
  double box_descent;
  double box_height;
};

struct _PangoCairoFontPrivateScaledFontData
{
  cairo_matrix_t font_matrix;
  cairo_matrix_t ctm;
  cairo_font_options_t *options;
};

struct _PangoCairoFontPrivate
{
  NsPangoCairoFont *cfont;

  NsPangoCairoFontPrivateScaledFontData *data;

  cairo_scaled_font_t *scaled_font;
  cairo_scaled_font_t *hex_box_scaled_font;
  NsPangoCairoFontHexBoxInfo *hbi;
  GHashTable *hex_box_glyphs;
  GArray *hex_box_pango_glyphs;
  unsigned int hex_box_glyph_base;

  gboolean is_hinted;
  NsPangoGravity gravity;

  NsPangoRectangle font_extents;
  NsPangoCairoFontGlyphExtentsCacheEntry *glyph_extents_cache;

  GSList *metrics_by_lang;
};

struct _PangoCairoFontIface
{
  GTypeInterface g_iface;

  cairo_font_face_t *(*create_font_face) (NsPangoCairoFont *cfont);
  NsPangoFontMetrics *(*create_base_metrics_for_context) (NsPangoCairoFont *cfont,
							NsPangoContext   *context);

  gssize cf_priv_offset;
};

gboolean _ns_pango_cairo_font_install (NsPangoFont *font,
				    cairo_t   *cr);
cairo_scaled_font_t *_ns_pango_cairo_font_get_hex_box_scaled_font (NsPangoCairoFont *cfont);
unsigned long _ns_pango_cairo_font_encode_hex_box_glyph (NsPangoCairoFont *cfont,
						      NsPangoGlyph      glyph);
NsPangoFontMetrics * _ns_pango_cairo_font_get_metrics (NsPangoFont     *font,
						  NsPangoLanguage *language);
NsPangoCairoFontHexBoxInfo *_ns_pango_cairo_font_get_hex_box_info (NsPangoCairoFont *cfont);

void _ns_pango_cairo_font_private_initialize (NsPangoCairoFontPrivate      *cf_priv,
					   NsPangoCairoFont             *font,
					   NsPangoGravity                gravity,
					   const cairo_font_options_t *font_options,
					   const NsPangoMatrix          *ns_pango_ctm,
					   const cairo_matrix_t       *font_matrix);
void _ns_pango_cairo_font_private_finalize (NsPangoCairoFontPrivate *cf_priv);
cairo_scaled_font_t *_ns_pango_cairo_font_private_get_scaled_font (NsPangoCairoFontPrivate *cf_priv);
void  ns_pango_cairo_font_private_get_font_options (NsPangoCairoFontPrivate *cf_private,
                                                 cairo_font_options_t  *options);

gboolean _ns_pango_cairo_font_private_is_metrics_hinted (NsPangoCairoFontPrivate *cf_priv);
void _ns_pango_cairo_font_private_get_glyph_extents (NsPangoCairoFontPrivate *cf_priv,
						  NsPangoGlyph             glyph,
						  NsPangoRectangle        *ink_rect,
						  NsPangoRectangle        *logical_rect);

#define NS_TYPE_PANGO_CAIRO_RENDERER            (ns_pango_cairo_renderer_get_type())
#define NS_PANGO_CAIRO_RENDERER(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_TYPE_PANGO_CAIRO_RENDERER, NsPangoCairoRenderer))
#define NS_PANGO_IS_CAIRO_RENDERER(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_TYPE_PANGO_CAIRO_RENDERER))

typedef struct _PangoCairoRenderer NsPangoCairoRenderer;

_PANGO_EXTERN
GType ns_pango_cairo_renderer_get_type    (void) G_GNUC_CONST;


const cairo_font_options_t *_ns_pango_cairo_context_get_merged_font_options (NsPangoContext *context);

void                        ns_pango_cairo_font_get_font_options (NsPangoCairoFont       *cfont,
                                                               cairo_font_options_t *options);
NsPangoFont *                 ns_pango_cairo_font_map_reload_font  (NsPangoFontMap   *fontmap,
                                                               NsPangoFont      *font,
                                                               double          scale,
                                                               NsPangoContext   *context,
                                                               const char     *variations);

G_END_DECLS

#endif /* __NS_PANGOCAIRO_PRIVATE_H__ */
