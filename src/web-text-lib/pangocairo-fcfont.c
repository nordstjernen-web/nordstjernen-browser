/* Pango
 * pangocairofc-font.c: Cairo font handling, fontconfig backend
 *
 * Copyright (C) 2000-2005 Red Hat Software
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

#include "config.h"

/* FreeType has undefined macros in its header */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include <cairo-ft.h>
#pragma GCC diagnostic pop

#include "pangofc-fontmap-private.h"
#include "pangocairo-private.h"
#include "pangocairo-fc-private.h"
#include "pangofc-private.h"
#include "pango-impl-utils.h"

#include <hb-ot.h>
#include <freetype/ftmm.h>

#define NS_TYPE_PANGO_CAIRO_FC_FONT           (ns_pango_cairo_fc_font_get_type ())
#define NS_PANGO_CAIRO_FC_FONT(object)        (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_TYPE_PANGO_CAIRO_FC_FONT, NsPangoCairoFcFont))
#define NS_PANGO_CAIRO_FC_FONT_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), NS_TYPE_PANGO_CAIRO_FC_FONT, NsPangoCairoFcFontClass))
#define NS_PANGO_CAIRO_IS_FONT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), NS_TYPE_PANGO_CAIRO_FC_FONT))
#define NS_PANGO_CAIRO_FC_FONT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), NS_TYPE_PANGO_CAIRO_FC_FONT, NsPangoCairoFcFontClass))

typedef struct _PangoCairoFcFont      NsPangoCairoFcFont;
typedef struct _PangoCairoFcFontClass NsPangoCairoFcFontClass;

struct _PangoCairoFcFont
{
  NsPangoFcFont font;
  NsPangoCairoFontPrivate cf_priv;
};

struct _PangoCairoFcFontClass
{
  NsPangoFcFontClass  parent_class;
};

_PANGO_EXTERN
GType ns_pango_cairo_fc_font_get_type (void);

/********************************
 *    Method implementations    *
 ********************************/

static cairo_font_face_t *
ns_pango_cairo_fc_font_create_font_face (NsPangoCairoFont *cfont)
{
  NsPangoFcFont *fcfont = (NsPangoFcFont *) (cfont);

  return cairo_ft_font_face_create_for_pattern (fcfont->font_pattern);
}

static NsPangoFontMetrics *
ns_pango_cairo_fc_font_create_base_metrics_for_context (NsPangoCairoFont *cfont,
						     NsPangoContext   *context)
{
  NsPangoCairoFcFont *cffont = (NsPangoCairoFcFont *) cfont;
  NsPangoFcFont *fcfont = (NsPangoFcFont *) cfont;
  NsPangoFontMetrics *metrics;

  metrics = ns_pango_fc_font_create_base_metrics_for_context (fcfont, context);

  if (_ns_pango_cairo_font_private_is_metrics_hinted (&cffont->cf_priv))
    {
      metrics->ascent = NS_PANGO_PIXELS_CEIL (metrics->ascent) * NS_PANGO_SCALE;
      metrics->descent = NS_PANGO_PIXELS_CEIL (metrics->descent) * NS_PANGO_SCALE;
      metrics->height = NS_PANGO_PIXELS_CEIL (metrics->height) * NS_PANGO_SCALE;
      metrics->underline_position = NS_PANGO_PIXELS_CEIL (metrics->underline_position) * NS_PANGO_SCALE;
      metrics->underline_thickness = NS_PANGO_PIXELS_CEIL (metrics->underline_thickness) * NS_PANGO_SCALE;
      metrics->strikethrough_position = NS_PANGO_PIXELS_CEIL (metrics->strikethrough_position) * NS_PANGO_SCALE;
      metrics->strikethrough_thickness = NS_PANGO_PIXELS_CEIL (metrics->strikethrough_thickness) * NS_PANGO_SCALE;
    }

  return metrics;
}

static void
cairo_font_iface_init (NsPangoCairoFontIface *iface)
{
  iface->create_font_face = ns_pango_cairo_fc_font_create_font_face;
  iface->create_base_metrics_for_context = ns_pango_cairo_fc_font_create_base_metrics_for_context;
  iface->cf_priv_offset = G_STRUCT_OFFSET (NsPangoCairoFcFont, cf_priv);
}

G_DEFINE_TYPE_WITH_CODE (NsPangoCairoFcFont, ns_pango_cairo_fc_font, NS_TYPE_PANGO_FC_FONT,
                         G_IMPLEMENT_INTERFACE (NS_TYPE_PANGO_CAIRO_FONT, cairo_font_iface_init))

static void
ns_pango_cairo_fc_font_finalize (GObject *object)
{
  NsPangoCairoFcFont *cffont = (NsPangoCairoFcFont *) object;

  _ns_pango_cairo_font_private_finalize (&cffont->cf_priv);

  G_OBJECT_CLASS (ns_pango_cairo_fc_font_parent_class)->finalize (object);
}

/* we want get_glyph_extents extremely fast, so we use a small wrapper here
 * to avoid having to lookup the interface data like we do for get_metrics
 * in _ns_pango_cairo_font_get_metrics(). */
static void
ns_pango_cairo_fc_font_get_glyph_extents (NsPangoFont      *font,
				       NsPangoGlyph      glyph,
				       NsPangoRectangle *ink_rect,
				       NsPangoRectangle *logical_rect)
{
  NsPangoCairoFcFont *cffont = (NsPangoCairoFcFont *) (font);

  _ns_pango_cairo_font_private_get_glyph_extents (&cffont->cf_priv,
					       glyph,
					       ink_rect,
					       logical_rect);
}

static FT_Face
ns_pango_cairo_fc_font_lock_face (NsPangoFcFont *font)
{
  NsPangoCairoFcFont *cffont = (NsPangoCairoFcFont *) (font);
  cairo_scaled_font_t *scaled_font = _ns_pango_cairo_font_private_get_scaled_font (&cffont->cf_priv);

  if (G_UNLIKELY (!scaled_font))
    return NULL;

  return cairo_ft_scaled_font_lock_face (scaled_font);
}

static void
ns_pango_cairo_fc_font_unlock_face (NsPangoFcFont *font)
{
  NsPangoCairoFcFont *cffont = (NsPangoCairoFcFont *) (font);
  cairo_scaled_font_t *scaled_font = _ns_pango_cairo_font_private_get_scaled_font (&cffont->cf_priv);

  if (G_UNLIKELY (!scaled_font))
    return;

  cairo_ft_scaled_font_unlock_face (scaled_font);
}

static void
ns_pango_cairo_fc_font_class_init (NsPangoCairoFcFontClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  NsPangoFontClass *font_class = NS_PANGO_FONT_CLASS (class);
  NsPangoFcFontClass *fc_font_class = NS_PANGO_FC_FONT_CLASS (class);

  object_class->finalize = ns_pango_cairo_fc_font_finalize;

  font_class->get_glyph_extents = ns_pango_cairo_fc_font_get_glyph_extents;
  font_class->get_metrics = _ns_pango_cairo_font_get_metrics;

  fc_font_class->lock_face = ns_pango_cairo_fc_font_lock_face;
  fc_font_class->unlock_face = ns_pango_cairo_fc_font_unlock_face;
}

static void
ns_pango_cairo_fc_font_init (NsPangoCairoFcFont *cffont G_GNUC_UNUSED)
{
}

/********************
 *    Private API   *
 ********************/

static double
get_font_size (const FcPattern *pattern)
{
  double size;
  double dpi;

  if (FcPatternGetDouble (pattern, FC_PIXEL_SIZE, 0, &size) == FcResultMatch)
    return size;

  /* Just in case FC_PIXEL_SIZE got unset between ns_pango_fc_make_pattern()
   * and here.  That would be very weird.
   */

  if (FcPatternGetDouble (pattern, FC_DPI, 0, &dpi) != FcResultMatch)
    dpi = 72;

  if (FcPatternGetDouble (pattern, FC_SIZE, 0, &size) == FcResultMatch)
    return size * dpi / 72.;

  /* Whatever */
  return 18.;
}

static gpointer
get_gravity_class (void)
{
  static GEnumClass *class = NULL; /* MT-safe */

  if (g_once_init_enter (&class))
    g_once_init_leave(&class, (gpointer)g_type_class_ref (NS_TYPE_PANGO_GRAVITY));

  return class;
}

static NsPangoGravity
get_gravity (const FcPattern *pattern)
{
  char *s;

  if (FcPatternGetString (pattern, NS_PANGO_FC_GRAVITY, 0, (FcChar8 **)(void *)&s) == FcResultMatch)
    {
      GEnumValue *value = g_enum_get_value_by_nick (get_gravity_class (), s);
      return value->value;
    }

  return NS_PANGO_GRAVITY_SOUTH;
}

NsPangoFcFont *
_ns_pango_cairo_fc_font_new (NsPangoCairoFcFontMap *cffontmap,
			  NsPangoFcFontKey      *key)
{
  NsPangoCairoFcFont *cffont;
  const FcPattern *pattern = ns_pango_fc_font_key_get_pattern (key);
  cairo_matrix_t font_matrix;
  FcMatrix fc_matrix, *fc_matrix_val;
  double size;
  int i;
  cairo_font_options_t *options;

  g_return_val_if_fail (NS_PANGO_IS_CAIRO_FC_FONT_MAP (cffontmap), NULL);
  g_return_val_if_fail (pattern != NULL, NULL);

  cffont = g_object_new (NS_TYPE_PANGO_CAIRO_FC_FONT,
			 "pattern", pattern,
			 "fontmap", cffontmap,
			 NULL);

  size = get_font_size (pattern) /
	 ns_pango_matrix_get_font_scale_factor (ns_pango_fc_font_key_get_matrix (key));

  FcMatrixInit (&fc_matrix);
  for (i = 0; FcPatternGetMatrix (pattern, FC_MATRIX, i, &fc_matrix_val) == FcResultMatch; i++)
    FcMatrixMultiply (&fc_matrix, &fc_matrix, fc_matrix_val);

  cairo_matrix_init (&font_matrix,
		     fc_matrix.xx,
		     - fc_matrix.yx,
		     - fc_matrix.xy,
		     fc_matrix.yy,
		     0., 0.);

  cairo_matrix_scale (&font_matrix, size, size);

  options = ns_pango_fc_font_key_get_context_key (key);

  _ns_pango_cairo_font_private_initialize (&cffont->cf_priv,
					(NsPangoCairoFont *) cffont,
					get_gravity (pattern),
					options,
					ns_pango_fc_font_key_get_matrix (key),
					&font_matrix);

  ((NsPangoFcFont *)(cffont))->is_hinted = _ns_pango_cairo_font_private_is_metrics_hinted (&cffont->cf_priv);

  return (NsPangoFcFont *) cffont;
}
