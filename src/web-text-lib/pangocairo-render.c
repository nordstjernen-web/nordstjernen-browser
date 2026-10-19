/* Pango
 * pangocairo-render.c: Rendering routines to Cairo surfaces
 *
 * Copyright (C) 2004 Red Hat, Inc.
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

#include <math.h>

#include "pango-font-private.h"
#include "pangocairo-private.h"
#include "pango-glyph-item.h"
#include "pango-impl-utils.h"

typedef struct _PangoCairoRendererClass NsPangoCairoRendererClass;

#define NS_PANGO_CAIRO_RENDERER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NS_TYPE_PANGO_CAIRO_RENDERER, NsPangoCairoRendererClass))
#define NS_PANGO_IS_CAIRO_RENDERER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NS_TYPE_PANGO_CAIRO_RENDERER))
#define NS_PANGO_CAIRO_RENDERER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NS_TYPE_PANGO_CAIRO_RENDERER, NsPangoCairoRendererClass))

struct _PangoCairoRenderer
{
  NsPangoRenderer parent_instance;

  cairo_t *cr;
  gboolean do_path;
  gboolean has_show_text_glyphs;
  double x_offset, y_offset;

  /* house-keeping options */
  gboolean is_cached_renderer;
  gboolean cr_had_current_point;
};

struct _PangoCairoRendererClass
{
  NsPangoRendererClass parent_class;
};

G_DEFINE_TYPE (NsPangoCairoRenderer, ns_pango_cairo_renderer, NS_TYPE_PANGO_RENDERER)

static void
set_color (NsPangoCairoRenderer *crenderer,
	   NsPangoRenderPart     part)
{
  NsPangoColor *color = ns_pango_renderer_get_color ((NsPangoRenderer *) (crenderer), part);
  guint16 a = ns_pango_renderer_get_alpha ((NsPangoRenderer *) (crenderer), part);
  gdouble red, green, blue, alpha;

  if (!a && !color)
    return;

  if (color)
    {
      red = color->red / 65535.;
      green = color->green / 65535.;
      blue = color->blue / 65535.;
      alpha = 1.;
    }
  else
    {
      cairo_pattern_t *pattern = cairo_get_source (crenderer->cr);

      if (pattern && cairo_pattern_get_type (pattern) == CAIRO_PATTERN_TYPE_SOLID)
        cairo_pattern_get_rgba (pattern, &red, &green, &blue, &alpha);
      else
        {
          red = 0.;
          green = 0.;
          blue = 0.;
          alpha = 1.;
        }
    }

  if (a)
    alpha = a / 65535.;

  cairo_set_source_rgba (crenderer->cr, red, green, blue, alpha);
}

/* note: modifies crenderer->cr without doing cairo_save/restore() */
static void
_ns_pango_cairo_renderer_draw_frame (NsPangoCairoRenderer *crenderer,
				  double              x,
				  double              y,
				  double              width,
				  double              height,
				  double              line_width,
				  gboolean            invalid)
{
  cairo_t *cr = crenderer->cr;

  if (crenderer->do_path)
    {
      double d2 = line_width * .5, d = line_width;

      /* we draw an outer box in one winding direction and an inner one in the
       * opposite direction.  This works for both cairo windings rules.
       *
       * what we really want is cairo_stroke_to_path(), but that's not
       * implemented in cairo yet.
       */

      /* outer */
      cairo_rectangle (cr, x-d2, y-d2, width+d, height+d);

      /* inner */
      if (invalid)
        {
	  /* delicacies of computing the joint... this is REALLY slow */

	  double alpha, tan_alpha2, cos_alpha;
	  double sx, sy;

	  alpha = atan2 (height, width);

	  tan_alpha2 = tan (alpha * .5);
	  if (tan_alpha2 < 1e-5 || (sx = d2 / tan_alpha2, 2. * sx > width - d))
	    sx = (width - d) * .5;

	  cos_alpha = cos (alpha);
	  if (cos_alpha < 1e-5 || (sy = d2 / cos_alpha, 2. * sy > height - d))
	    sy = (height - d) * .5;

	  /* top triangle */
	  cairo_new_sub_path (cr);
	  cairo_line_to (cr, x+width-sx, y+d2);
	  cairo_line_to (cr, x+sx, y+d2);
	  cairo_line_to (cr, x+.5*width, y+.5*height-sy);
	  cairo_close_path (cr);

	  /* bottom triangle */
	  cairo_new_sub_path (cr);
	  cairo_line_to (cr, x+width-sx, y+height-d2);
	  cairo_line_to (cr, x+.5*width, y+.5*height+sy);
	  cairo_line_to (cr, x+sx, y+height-d2);
	  cairo_close_path (cr);


	  alpha = G_PI_2 - alpha;
	  tan_alpha2 = tan (alpha * .5);
	  if (tan_alpha2 < 1e-5 || (sy = d2 / tan_alpha2, 2. * sy > height - d))
	    sy = (height - d) * .5;

	  cos_alpha = cos (alpha);
	  if (cos_alpha < 1e-5 || (sx = d2 / cos_alpha, 2. * sx > width - d))
	    sx = (width - d) * .5;

	  /* left triangle */
	  cairo_new_sub_path (cr);
	  cairo_line_to (cr, x+d2, y+sy);
	  cairo_line_to (cr, x+d2, y+height-sy);
	  cairo_line_to (cr, x+.5*width-sx, y+.5*height);
	  cairo_close_path (cr);

	  /* right triangle */
	  cairo_new_sub_path (cr);
	  cairo_line_to (cr, x+width-d2, y+sy);
	  cairo_line_to (cr, x+.5*width+sx, y+.5*height);
	  cairo_line_to (cr, x+width-d2, y+height-sy);
	  cairo_close_path (cr);
	}
      else
	cairo_rectangle (cr, x+width-d2, y+d2, - (width-d), height-d);
    }
  else
    {
      cairo_rectangle (cr, x, y, width, height);

      if (invalid)
        {
	  /* draw an X */

	  cairo_new_sub_path (cr);
	  cairo_move_to (cr, x, y);
	  cairo_rel_line_to (cr, width, height);

	  cairo_new_sub_path (cr);
	  cairo_move_to (cr, x + width, y);
	  cairo_rel_line_to (cr, -width, height);

	  cairo_set_line_cap (cr, CAIRO_LINE_CAP_BUTT);
	}

      cairo_set_line_width (cr, line_width);
      cairo_set_line_join (cr, CAIRO_LINE_JOIN_MITER);
      cairo_set_miter_limit (cr, 2.);
      cairo_stroke (cr);
    }
}

static void
_ns_pango_cairo_renderer_draw_box_glyph (NsPangoCairoRenderer *crenderer,
				      NsPangoGlyphInfo     *gi,
				      double              cx,
				      double              cy,
				      gboolean            invalid)
{
  cairo_save (crenderer->cr);

  _ns_pango_cairo_renderer_draw_frame (crenderer,
				    cx + 1.5,
				    cy + 1.5 - NS_PANGO_UNKNOWN_GLYPH_HEIGHT,
				    (double)gi->geometry.width / NS_PANGO_SCALE - 3.0,
				    NS_PANGO_UNKNOWN_GLYPH_HEIGHT - 3.0,
				    1.0,
				    invalid);

  cairo_restore (crenderer->cr);
}

static void
_ns_pango_cairo_renderer_draw_unknown_glyph (NsPangoCairoRenderer *crenderer,
					  NsPangoFont          *font,
					  NsPangoGlyphInfo     *gi,
					  double              cx,
					  double              cy)
{
  char buf[7];
  double x0, y0;
  int row, col;
  int rows, cols;
  double width, lsb;
  char hexbox_string[2] = { 0, 0 };
  NsPangoCairoFontHexBoxInfo *hbi;
  gunichar ch;
  gboolean invalid_input;
  const char *p;
  const char *name;

  cairo_save (crenderer->cr);

  ch = gi->glyph & ~NS_PANGO_GLYPH_UNKNOWN_FLAG;
  invalid_input = G_UNLIKELY (gi->glyph == NS_PANGO_GLYPH_INVALID_INPUT || ch > 0x10FFFF);

  hbi = _ns_pango_cairo_font_get_hex_box_info ((NsPangoCairoFont *)font);
  if (!hbi || !_ns_pango_cairo_font_install ((NsPangoFont *)(hbi->font), crenderer->cr))
    {
      _ns_pango_cairo_renderer_draw_box_glyph (crenderer, gi, cx, cy, invalid_input);
      goto done;
    }

  if (G_UNLIKELY (invalid_input))
    {
      rows = hbi->rows;
      cols = 1;
    }
  else if (ch == 0x2423 ||
           g_unichar_type (ch) == G_UNICODE_SPACE_SEPARATOR)
    {
      /* We never want to show a hex box or other drawing for
       * space. If we want space to be visible, we replace 0x20
       * by 0x2423 (visible space).
       *
       * Since we don't want to rely on glyph availability,
       * we render a centered dot ourselves.
       */
      double x = cx + 0.5 *((double)gi->geometry.width / NS_PANGO_SCALE);
      double y = cy + hbi->box_descent - 0.5 * hbi->box_height;

      cairo_new_sub_path (crenderer->cr);
      cairo_arc (crenderer->cr, x, y, 1.5 * hbi->line_width, 0, 2 * G_PI);
      cairo_close_path (crenderer->cr);
      cairo_fill (crenderer->cr);
      goto done;
    }
  else if (ch == '\t')
    {
      /* Since we don't want to rely on glyph availability,
       * we render an arrow like ↦ ourselves.
       */
      double y = cy + hbi->box_descent - 0.5 * hbi->box_height;
      double width = (double)gi->geometry.width / NS_PANGO_SCALE;
      double offset = 0.2 * width;
      double x = cx + offset;
      double al = width - 2 * offset; /* arrow length */
      double tl = MIN (hbi->digit_width, 0.75 * al); /* tip length */
      double tw2 = 2.5 * hbi->line_width; /* tip width / 2 */
      double lw2 = 0.5 * hbi->line_width; /* line width / 2 */

      cairo_move_to (crenderer->cr, x - lw2, y - tw2);
      cairo_line_to (crenderer->cr, x + lw2, y - tw2);
      cairo_line_to (crenderer->cr, x + lw2, y - lw2);
      cairo_line_to (crenderer->cr, x + al - tl, y - lw2);
      cairo_line_to (crenderer->cr, x + al - tl, y - tw2);
      cairo_line_to (crenderer->cr, x + al,  y);
      cairo_line_to (crenderer->cr, x + al - tl, y + tw2);
      cairo_line_to (crenderer->cr, x + al - tl, y + lw2);
      cairo_line_to (crenderer->cr, x + lw2, y + lw2);
      cairo_line_to (crenderer->cr, x + lw2, y + tw2);
      cairo_line_to (crenderer->cr, x - lw2, y + tw2);
      cairo_close_path (crenderer->cr);
      cairo_fill (crenderer->cr);
      goto done;
    }
  else if (ch == '\n' || ch == 0x2028 || ch == 0x2029)
    {
      /* Since we don't want to rely on glyph availability,
       * we render an arrow like ↵ ourselves.
       */
      double width = (double)gi->geometry.width / NS_PANGO_SCALE;
      double offset = 0.2 * width;
      double al = width - 2 * offset; /* arrow length */
      double tl = MIN (hbi->digit_width, 0.75 * al); /* tip length */
      double ah = al - 0.5 * tl; /* arrow height */
      double tw2 = 2.5 * hbi->line_width; /* tip width / 2 */
      double x = cx + offset;
      double y = cy - (hbi->box_height - al) / 2;
      double lw2 = 0.5 * hbi->line_width; /* line width / 2 */

      cairo_move_to (crenderer->cr, x, y);
      cairo_line_to (crenderer->cr, x + tl, y - tw2);
      cairo_line_to (crenderer->cr, x + tl, y - lw2);
      cairo_line_to (crenderer->cr, x + al - lw2, y - lw2);
      cairo_line_to (crenderer->cr, x + al - lw2, y - ah);
      cairo_line_to (crenderer->cr, x + al + lw2, y - ah);
      cairo_line_to (crenderer->cr, x + al + lw2, y + lw2);
      cairo_line_to (crenderer->cr, x + tl, y + lw2);
      cairo_line_to (crenderer->cr, x + tl, y + tw2);
      cairo_close_path (crenderer->cr);
      cairo_fill (crenderer->cr);
      goto done;
    }
  else if ((name = ns_pango_get_ignorable_size (ch, &rows, &cols)))
    {
      /* Nothing else to do, we render 'default ignorable' chars
       * as hex box with their nick.
       */
    }
  else
    {
      /* Everything else gets a traditional hex box. */
      rows = hbi->rows;
      cols = (ch > 0xffff ? 6 : 4) / rows;
      g_snprintf (buf, sizeof(buf), (ch > 0xffff) ? "%06X" : "%04X", ch);
      name = buf;
    }

  width = (3 * hbi->pad_x + cols * (hbi->digit_width + hbi->pad_x));
  lsb = ((double)gi->geometry.width / NS_PANGO_SCALE - width) * .5;
  lsb = floor (lsb / hbi->pad_x) * hbi->pad_x;

  _ns_pango_cairo_renderer_draw_frame (crenderer,
				    cx + lsb + 1.5 * hbi->pad_x,
				    cy + hbi->box_descent - hbi->box_height + hbi->pad_y * .5,
				    width - hbi->pad_x,
				    (hbi->box_height - hbi->pad_y),
				    hbi->line_width,
				    invalid_input);

  if (invalid_input)
    goto done;

  x0 = cx + lsb + hbi->pad_x * 3;
  y0 = cy + hbi->box_descent - hbi->pad_y * 2 - ((hbi->rows - rows) * hbi->digit_height / 2);

  for (row = 0, p = name; row < rows; row++)
    {
      double y = y0 - (rows - 1 - row) * (hbi->digit_height + hbi->pad_y);
      for (col = 0; col < cols; col++, p++)
	{
	  double x = x0 + col * (hbi->digit_width + hbi->pad_x);

          if (!p)
            goto done;

	  cairo_move_to (crenderer->cr, x, y);

          hexbox_string[0] = p[0];

	  if (crenderer->do_path)
	      cairo_text_path (crenderer->cr, hexbox_string);
	  else
	      cairo_show_text (crenderer->cr, hexbox_string);
	}
    }

done:
  cairo_restore (crenderer->cr);
}

static gboolean
ns_pango_cairo_glyph_string_has_unknown_glyphs (NsPangoGlyphString *glyphs)
{
  int i;

  for (i = 0; i < glyphs->num_glyphs; i++)
    {
      if (glyphs->glyphs[i].glyph & NS_PANGO_GLYPH_UNKNOWN_FLAG)
        return TRUE;
    }

  return FALSE;
}

static gboolean
ns_pango_cairo_glyph_range_has_unknown_glyphs (NsPangoGlyphString *glyphs,
                                            int               glyph_start,
                                            int               glyph_end)
{
  int i;
  int dir = glyph_start < glyph_end ? 1 : -1;

  for (i = glyph_start; i != glyph_end; i += dir)
    {
      if (glyphs->glyphs[i].glyph & NS_PANGO_GLYPH_UNKNOWN_FLAG)
        return TRUE;
    }

  return FALSE;
}

static int
ns_pango_cairo_renderer_count_glyphs (NsPangoGlyphString *glyphs,
                                   int               glyph_start,
                                   int               glyph_end,
                                   gboolean          use_hex_box_scaled_font)
{
  int count = 0;
  int i;
  int dir = glyph_start < glyph_end ? 1 : -1;

  for (i = glyph_start; i != glyph_end; i += dir)
    {
      NsPangoGlyph glyph = glyphs->glyphs[i].glyph;

      if (glyph == NS_PANGO_GLYPH_EMPTY)
        continue;

      if (!use_hex_box_scaled_font && (glyph & NS_PANGO_GLYPH_UNKNOWN_FLAG))
        continue;

      count++;
    }

  return count;
}

static void
ns_pango_cairo_renderer_get_glyph_range (int  glyph_start,
                                      int  glyph_end,
                                      int *range_start,
                                      int *range_end)
{
  if (glyph_start < glyph_end)
    {
      *range_start = glyph_start;
      *range_end = glyph_end;
    }
  else
    {
      *range_start = glyph_end + 1;
      *range_end = glyph_start + 1;
    }
}

static gboolean
ns_pango_cairo_renderer_has_hex_box_scaled_font (NsPangoCairoRenderer *crenderer,
                                              NsPangoFont          *font,
                                              NsPangoGlyphString   *glyphs)
{
  return !crenderer->do_path &&
         crenderer->has_show_text_glyphs &&
         ns_pango_cairo_glyph_string_has_unknown_glyphs (glyphs) &&
         _ns_pango_cairo_font_get_hex_box_scaled_font ((NsPangoCairoFont *) font) != NULL;
}

#ifndef STACK_BUFFER_SIZE
#define STACK_BUFFER_SIZE (512 * sizeof (int))
#endif

#define STACK_ARRAY_LENGTH(T) (STACK_BUFFER_SIZE / sizeof(T))

static void
ns_pango_cairo_renderer_show_text_glyphs_range (NsPangoRenderer        *renderer,
					     const char           *text,
					     int                   text_len,
					     NsPangoGlyphString     *glyphs,
					     int                   glyph_start,
					     int                   glyph_end,
					     cairo_text_cluster_t *clusters,
					     int                   num_clusters,
					     gboolean              backward,
					     NsPangoFont            *font,
					     int                   x,
					     int                   y,
					     gboolean              use_hex_box_scaled_font)
{
  NsPangoCairoRenderer *crenderer = (NsPangoCairoRenderer *) (renderer);

  int i, count;
  int x_position = 0;
  cairo_glyph_t *cairo_glyphs;
  cairo_glyph_t stack_glyphs[STACK_ARRAY_LENGTH (cairo_glyph_t)];
  double base_x = crenderer->x_offset + (double)x / NS_PANGO_SCALE;
  double base_y = crenderer->y_offset + (double)y / NS_PANGO_SCALE;
  NsPangoRenderComponent components = ns_pango_renderer_get_components (renderer);

  cairo_save (crenderer->cr);
  if (!crenderer->do_path)
    set_color (crenderer, NS_PANGO_RENDER_PART_FOREGROUND);

  for (i = 0; i < glyph_start; i++)
    x_position += glyphs->glyphs[i].geometry.width;

  if (use_hex_box_scaled_font)
    cairo_set_scaled_font (crenderer->cr,
                           _ns_pango_cairo_font_get_hex_box_scaled_font ((NsPangoCairoFont *) font));
  else if (!_ns_pango_cairo_font_install (font, crenderer->cr))
    {
      for (i = glyph_start; i < glyph_end; i++)
	{
	  NsPangoGlyphInfo *gi = &glyphs->glyphs[i];

	  if (gi->glyph != NS_PANGO_GLYPH_EMPTY)
	    {
	      double cx = base_x + (double)(x_position + gi->geometry.x_offset) / NS_PANGO_SCALE;
	      double cy = gi->geometry.y_offset == 0 ?
			  base_y :
			  base_y + (double)(gi->geometry.y_offset) / NS_PANGO_SCALE;

	      _ns_pango_cairo_renderer_draw_unknown_glyph (crenderer, font, gi, cx, cy);
	    }
	  x_position += gi->geometry.width;
	}

      goto done;
    }

  if (glyph_end - glyph_start > (int) G_N_ELEMENTS (stack_glyphs))
    cairo_glyphs = g_new (cairo_glyph_t, glyph_end - glyph_start);
  else
    cairo_glyphs = stack_glyphs;

  count = 0;
  for (i = glyph_start; i < glyph_end; i++)
    {
      NsPangoGlyphInfo *gi = &glyphs->glyphs[i];

      if ((components & (gi->attr.is_color ? NS_PANGO_RENDER_COMPONENT_COLOR_GLYPH : NS_PANGO_RENDER_COMPONENT_PLAIN_GLYPH)) != 0 &&
          gi->glyph != NS_PANGO_GLYPH_EMPTY)
	{
	  double cx = base_x + (double)(x_position + gi->geometry.x_offset) / NS_PANGO_SCALE;
	  double cy = gi->geometry.y_offset == 0 ?
		      base_y :
		      base_y + (double)(gi->geometry.y_offset) / NS_PANGO_SCALE;

	  if (gi->glyph & NS_PANGO_GLYPH_UNKNOWN_FLAG)
            {
              if (use_hex_box_scaled_font)
                {
                  cairo_glyphs[count].index = _ns_pango_cairo_font_encode_hex_box_glyph ((NsPangoCairoFont *) font,
                                                                                       gi->glyph);
                  cairo_glyphs[count].x = cx;
                  cairo_glyphs[count].y = cy;
                  count++;
                }
              else if (gi->glyph == (0x20 | NS_PANGO_GLYPH_UNKNOWN_FLAG))
                ; /* no hex boxes for space, please */
              else
		        _ns_pango_cairo_renderer_draw_unknown_glyph (crenderer, font, gi, cx, cy);
            }
	  else
	    {
	      cairo_glyphs[count].index = gi->glyph;
	      cairo_glyphs[count].x = cx;
	      cairo_glyphs[count].y = cy;
	      count++;
	    }
	}
      x_position += gi->geometry.width;
    }

  if (G_UNLIKELY (crenderer->do_path))
    cairo_glyph_path (crenderer->cr, cairo_glyphs, count);
  else
    if (G_UNLIKELY (clusters))
      cairo_show_text_glyphs (crenderer->cr,
			      text, text_len,
			      cairo_glyphs, count,
			      clusters, num_clusters,
			      backward ? CAIRO_TEXT_CLUSTER_FLAG_BACKWARD : 0);
    else
      cairo_show_glyphs (crenderer->cr, cairo_glyphs, count);

  if (cairo_glyphs != stack_glyphs)
    g_free (cairo_glyphs);

done:
  cairo_restore (crenderer->cr);
}

static void
ns_pango_cairo_renderer_show_text_glyphs (NsPangoRenderer        *renderer,
				       const char           *text,
				       int                   text_len,
				       NsPangoGlyphString     *glyphs,
				       cairo_text_cluster_t *clusters,
				       int                   num_clusters,
				       gboolean              backward,
				       NsPangoFont            *font,
				       int                   x,
				       int                   y)
{
  ns_pango_cairo_renderer_show_text_glyphs_range (renderer,
					       text,
					       text_len,
					       glyphs,
					       0,
					       glyphs->num_glyphs,
					       clusters,
					       num_clusters,
					       backward,
					       font,
					       x,
					       y,
					       FALSE);
}

static void
ns_pango_cairo_renderer_show_text_glyphs_segment (NsPangoRenderer        *renderer,
					       const char           *text,
					       NsPangoGlyphString     *glyphs,
					       int                   text_start,
					       int                   text_end,
					       int                   glyph_start,
					       int                   glyph_end,
					       cairo_text_cluster_t *clusters,
					       int                   num_clusters,
					       gboolean              backward,
					       NsPangoFont            *font,
					       int                   x,
					       int                   y,
					       gboolean              use_hex_box_scaled_font)
{
  int range_start, range_end;

  ns_pango_cairo_renderer_get_glyph_range (glyph_start, glyph_end,
					&range_start, &range_end);

  ns_pango_cairo_renderer_show_text_glyphs_range (renderer,
					       text + text_start,
					       text_end - text_start,
					       glyphs,
					       range_start,
					       range_end,
					       clusters,
					       num_clusters,
					       backward,
					       font,
					       x,
					       y,
					       use_hex_box_scaled_font);
}

static void
ns_pango_cairo_renderer_draw_glyphs (NsPangoRenderer     *renderer,
				  NsPangoFont         *font,
				  NsPangoGlyphString  *glyphs,
				  int                x,
				  int                y)
{
  ns_pango_cairo_renderer_show_text_glyphs (renderer,
					 NULL, 0,
					 glyphs,
					 NULL, 0,
					 FALSE,
					 font,
					 x, y);
}

static void
ns_pango_cairo_renderer_draw_glyph_item (NsPangoRenderer     *renderer,
				      const char        *text,
				      NsPangoGlyphItem    *glyph_item,
				      int                x,
				      int                y)
{
  NsPangoCairoRenderer *crenderer = (NsPangoCairoRenderer *) (renderer);
  NsPangoFont          *font      = glyph_item->item->analysis.font;
  NsPangoGlyphString   *glyphs    = glyph_item->glyphs;
  NsPangoItem          *item      = glyph_item->item;
  gboolean            backward  = (item->analysis.level & 1) != 0;

  NsPangoGlyphItemIter   iter;
  cairo_text_cluster_t *cairo_clusters;
  cairo_text_cluster_t stack_clusters[STACK_ARRAY_LENGTH (cairo_text_cluster_t)];
  gboolean              use_hex_box_scaled_font;
  int num_clusters;

  if (!crenderer->has_show_text_glyphs || crenderer->do_path)
    {
      ns_pango_cairo_renderer_show_text_glyphs (renderer,
					     NULL, 0,
					     glyphs,
					     NULL, 0,
					     FALSE,
					     font,
					     x, y);
      return;
    }

  use_hex_box_scaled_font = ns_pango_cairo_renderer_has_hex_box_scaled_font (crenderer,
									  font,
									  glyphs);

  if (glyphs->num_glyphs > (int) G_N_ELEMENTS (stack_clusters))
    cairo_clusters = g_new (cairo_text_cluster_t, glyphs->num_glyphs);
  else
    cairo_clusters = stack_clusters;

  {
    gboolean have_segment = FALSE;
    gboolean segment_use_hex_box = FALSE;
    int segment_start_index = 0;
    int segment_end_index = 0;
    int segment_start_glyph = 0;
    int segment_end_glyph = 0;

    num_clusters = 0;
    if (ns_pango_glyph_item_iter_init_start (&iter, glyph_item, text))
      {
	do {
	  gboolean cluster_use_hex_box;
	  int num_bytes, num_glyphs;

	  num_bytes = iter.end_index - iter.start_index;
	  cluster_use_hex_box = use_hex_box_scaled_font &&
				ns_pango_cairo_glyph_range_has_unknown_glyphs (glyphs,
									    iter.start_glyph,
									    iter.end_glyph);
	  num_glyphs = ns_pango_cairo_renderer_count_glyphs (glyphs,
							   iter.start_glyph,
							   iter.end_glyph,
							   cluster_use_hex_box);

	  if (num_bytes < 1)
	    g_warning ("ns_pango_cairo_renderer_draw_glyph_item: bad cluster has num_bytes %d", num_bytes);
	  if (backward ? iter.start_glyph - iter.end_glyph < 1 : iter.end_glyph - iter.start_glyph < 1)
	    g_warning ("ns_pango_cairo_renderer_draw_glyph_item: bad cluster has num_glyphs %d",
		       backward ? iter.start_glyph - iter.end_glyph : iter.end_glyph - iter.start_glyph);

	  if (have_segment && cluster_use_hex_box != segment_use_hex_box)
	    {
	      ns_pango_cairo_renderer_show_text_glyphs_segment (renderer,
							     text,
							     glyphs,
							     segment_start_index,
							     segment_end_index,
							     segment_start_glyph,
							     segment_end_glyph,
							     cairo_clusters,
							     num_clusters,
							     backward,
							     font,
							     x, y,
							     segment_use_hex_box);
	      have_segment = FALSE;
	      num_clusters = 0;
	    }

	  if (!have_segment)
	    {
	      segment_use_hex_box = cluster_use_hex_box;
	      segment_start_index = iter.start_index;
	      segment_start_glyph = iter.start_glyph;
	      have_segment = TRUE;
	    }

	  segment_end_index = iter.end_index;
	  segment_end_glyph = iter.end_glyph;
	  cairo_clusters[num_clusters].num_bytes = num_bytes;
	  cairo_clusters[num_clusters].num_glyphs = num_glyphs;
	  num_clusters++;
	} while (ns_pango_glyph_item_iter_next_cluster (&iter));
      }

    if (have_segment)
      ns_pango_cairo_renderer_show_text_glyphs_segment (renderer,
						     text,
						     glyphs,
						     segment_start_index,
						     segment_end_index,
						     segment_start_glyph,
						     segment_end_glyph,
						     cairo_clusters,
						     num_clusters,
						     backward,
						     font,
						     x, y,
						     segment_use_hex_box);
  }

  if (cairo_clusters != stack_clusters)
    g_free (cairo_clusters);
}

static void
ns_pango_cairo_renderer_draw_rectangle (NsPangoRenderer     *renderer,
				     NsPangoRenderPart    part,
				     int                x,
				     int                y,
				     int                width,
				     int                height)
{
  NsPangoCairoRenderer *crenderer = (NsPangoCairoRenderer *) (renderer);

  if (!crenderer->do_path)
    {
      cairo_save (crenderer->cr);

      set_color (crenderer, part);
    }

  cairo_rectangle (crenderer->cr,
		   crenderer->x_offset + (double)x / NS_PANGO_SCALE,
		   crenderer->y_offset + (double)y / NS_PANGO_SCALE,
		   (double)width / NS_PANGO_SCALE, (double)height / NS_PANGO_SCALE);

  if (!crenderer->do_path)
    {
      cairo_fill (crenderer->cr);

      cairo_restore (crenderer->cr);
    }
}

static void
ns_pango_cairo_renderer_draw_trapezoid (NsPangoRenderer     *renderer,
				     NsPangoRenderPart    part,
				     double             y1_,
				     double             x11,
				     double             x21,
				     double             y2,
				     double             x12,
				     double             x22)
{
  NsPangoCairoRenderer *crenderer = (NsPangoCairoRenderer *) (renderer);
  cairo_t *cr;
  double x, y;

  cr = crenderer->cr;

  cairo_save (cr);

  if (!crenderer->do_path)
    set_color (crenderer, part);

  x = crenderer->x_offset,
  y = crenderer->y_offset;
  cairo_user_to_device_distance (cr, &x, &y);
  cairo_identity_matrix (cr);
  cairo_translate (cr, x, y);

  cairo_move_to (cr, x11, y1_);
  cairo_line_to (cr, x21, y1_);
  cairo_line_to (cr, x22, y2);
  cairo_line_to (cr, x12, y2);
  cairo_close_path (cr);

  if (!crenderer->do_path)
    cairo_fill (cr);

  cairo_restore (cr);
}

/* Draws an error underline that looks like one of:
 *              H       E                H
 *     /\      /\      /\        /\      /\               -
 *   A/  \    /  \    /  \     A/  \    /  \              |
 *    \   \  /    \  /   /D     \   \  /    \             |
 *     \   \/  C   \/   /        \   \/   C  \            | height = HEIGHT_SQUARES * square
 *      \      /\  F   /          \  F   /\   \           |
 *       \    /  \    /            \    /  \   \G         |
 *        \  /    \  /              \  /    \  /          |
 *         \/      \/                \/      \/           -
 *         B                         B
 *         |---|
 *       unit_width = (HEIGHT_SQUARES - 1) * square
 *
 * The x, y, width, height passed in give the desired bounding box;
 * x/width are adjusted to make the underline a integer number of units
 * wide.
 */
#define HEIGHT_SQUARES 2.5

static void
draw_error_underline (cairo_t *cr,
		      double   x,
		      double   y,
		      double   width,
		      double   height)
{
  double square = height / HEIGHT_SQUARES;
  double unit_width = (HEIGHT_SQUARES - 1) * square;
  double double_width = 2 * unit_width;
  int width_units = (width + unit_width / 2) / unit_width;
  double y_top, y_bottom;
  double x_left, x_middle, x_right;
  int i;

  x += (width - width_units * unit_width) / 2;

  y_top = y;
  y_bottom = y + height;

  /* Bottom of squiggle */
  x_middle = x + unit_width;
  x_right  = x + double_width;
  cairo_move_to (cr, x - square / 2, y_top + square / 2); /* A */
  for (i = 0; i < width_units-2; i += 2)
    {
      cairo_line_to (cr, x_middle, y_bottom); /* B */
      cairo_line_to (cr, x_right, y_top + square); /* C */

      x_middle += double_width;
      x_right  += double_width;
    }
  cairo_line_to (cr, x_middle, y_bottom); /* B */

  if (i + 1 == width_units)
    cairo_line_to (cr, x_middle + square / 2, y_bottom - square / 2); /* G */
  else if (i + 2 == width_units) {
    cairo_line_to (cr, x_right + square / 2, y_top + square / 2); /* D */
    cairo_line_to (cr, x_right, y_top); /* E */
  }

  /* Top of squiggle */
  x_left = x_middle - unit_width;
  for (; i >= 0; i -= 2)
    {
      cairo_line_to (cr, x_middle, y_bottom - square); /* F */
      cairo_line_to (cr, x_left, y_top);   /* H */

      x_left   -= double_width;
      x_middle -= double_width;
    }
}

static void
ns_pango_cairo_renderer_draw_error_underline (NsPangoRenderer *renderer,
					   int            x,
					   int            y,
					   int            width,
					   int            height)
{
  NsPangoCairoRenderer *crenderer = (NsPangoCairoRenderer *) (renderer);
  cairo_t *cr = crenderer->cr;

  if (!crenderer->do_path)
    {
      cairo_save (cr);

      set_color (crenderer, NS_PANGO_RENDER_PART_UNDERLINE);

      cairo_new_path (cr);
    }

  draw_error_underline (cr,
			crenderer->x_offset + (double)x / NS_PANGO_SCALE,
			crenderer->y_offset + (double)y / NS_PANGO_SCALE,
			(double)width / NS_PANGO_SCALE, (double)height / NS_PANGO_SCALE);

  if (!crenderer->do_path)
    {
      cairo_fill (cr);

      cairo_restore (cr);
    }
}

static void
ns_pango_cairo_renderer_draw_shape (NsPangoRenderer  *renderer,
				 NsPangoAttrShape *attr,
				 int             x,
				 int             y)
{
  NsPangoCairoRenderer *crenderer = (NsPangoCairoRenderer *) (renderer);
  cairo_t *cr = crenderer->cr;
  NsPangoLayout *layout;
  NsPangoCairoShapeRendererFunc shape_renderer;
  gpointer                    shape_renderer_data;
  double base_x, base_y;

  layout = ns_pango_renderer_get_layout (renderer);

  if (!layout)
  	return;

  shape_renderer = ns_pango_cairo_context_get_shape_renderer (ns_pango_layout_get_context (layout),
							   &shape_renderer_data);

  if (!shape_renderer)
    return;

  base_x = crenderer->x_offset + (double)x / NS_PANGO_SCALE;
  base_y = crenderer->y_offset + (double)y / NS_PANGO_SCALE;

  cairo_save (cr);
  if (!crenderer->do_path)
    set_color (crenderer, NS_PANGO_RENDER_PART_FOREGROUND);

  cairo_move_to (cr, base_x, base_y);

  shape_renderer (cr, attr, crenderer->do_path, shape_renderer_data);

  cairo_restore (cr);
}

static void
ns_pango_cairo_renderer_init (NsPangoCairoRenderer *renderer G_GNUC_UNUSED)
{
}

static void
ns_pango_cairo_renderer_class_init (NsPangoCairoRendererClass *klass)
{
  NsPangoRendererClass *renderer_class = NS_PANGO_RENDERER_CLASS (klass);

  renderer_class->draw_glyphs = ns_pango_cairo_renderer_draw_glyphs;
  renderer_class->draw_glyph_item = ns_pango_cairo_renderer_draw_glyph_item;
  renderer_class->draw_rectangle = ns_pango_cairo_renderer_draw_rectangle;
  renderer_class->draw_trapezoid = ns_pango_cairo_renderer_draw_trapezoid;
  renderer_class->draw_error_underline = ns_pango_cairo_renderer_draw_error_underline;
  renderer_class->draw_shape = ns_pango_cairo_renderer_draw_shape;
}

static NsPangoCairoRenderer *cached_renderer = NULL; /* MT-safe */
G_LOCK_DEFINE_STATIC (cached_renderer);

static NsPangoCairoRenderer *
acquire_renderer (void)
{
  NsPangoCairoRenderer *renderer;

  if (G_LIKELY (G_TRYLOCK (cached_renderer)))
    {
      if (G_UNLIKELY (!cached_renderer))
        {
	  cached_renderer = g_object_new (NS_TYPE_PANGO_CAIRO_RENDERER, NULL);
	  cached_renderer->is_cached_renderer = TRUE;
	}

      renderer = cached_renderer;
    }
  else
    {
      renderer = g_object_new (NS_TYPE_PANGO_CAIRO_RENDERER, NULL);
    }

  ns_pango_renderer_set_components (NS_PANGO_RENDERER (renderer), NS_PANGO_RENDER_COMPONENT_ALL);

  return renderer;
}

static void
release_renderer (NsPangoCairoRenderer *renderer)
{
  if (G_LIKELY (renderer->is_cached_renderer))
    {
      renderer->cr = NULL;
      renderer->do_path = FALSE;
      renderer->has_show_text_glyphs = FALSE;
      renderer->x_offset = 0.;
      renderer->y_offset = 0.;

      G_UNLOCK (cached_renderer);
    }
  else
    g_object_unref (renderer);
}

static void
save_current_point (NsPangoCairoRenderer *renderer)
{
  renderer->cr_had_current_point = cairo_has_current_point (renderer->cr);
  cairo_get_current_point (renderer->cr, &renderer->x_offset, &renderer->y_offset);

  /* abuse save_current_point() to cache cairo_has_show_text_glyphs() result */
  renderer->has_show_text_glyphs = cairo_surface_has_show_text_glyphs (cairo_get_target (renderer->cr));
}

static void
restore_current_point (NsPangoCairoRenderer *renderer)
{
  if (renderer->cr_had_current_point)
    /* XXX should do cairo_set_current_point() when we have that function */
    cairo_move_to (renderer->cr, renderer->x_offset, renderer->y_offset);
  else
    cairo_new_sub_path (renderer->cr);
}


/* convenience wrappers using the default renderer */


static void
_ns_pango_cairo_do_glyph_string (cairo_t          *cr,
			      NsPangoFont        *font,
			      NsPangoGlyphString *glyphs,
			      gboolean          do_path)
{
  NsPangoCairoRenderer *crenderer = acquire_renderer ();
  NsPangoRenderer *renderer = (NsPangoRenderer *) crenderer;

  crenderer->cr = cr;
  crenderer->do_path = do_path;
  save_current_point (crenderer);

  if (!do_path)
    {
      /* unset all part colors, since when drawing just a glyph string,
       * prepare_run() isn't called.
       */

      ns_pango_renderer_activate (renderer);

      ns_pango_renderer_set_color (renderer, NS_PANGO_RENDER_PART_FOREGROUND, NULL);
      ns_pango_renderer_set_color (renderer, NS_PANGO_RENDER_PART_BACKGROUND, NULL);
      ns_pango_renderer_set_color (renderer, NS_PANGO_RENDER_PART_UNDERLINE, NULL);
      ns_pango_renderer_set_color (renderer, NS_PANGO_RENDER_PART_STRIKETHROUGH, NULL);
      ns_pango_renderer_set_color (renderer, NS_PANGO_RENDER_PART_OVERLINE, NULL);
    }

  ns_pango_renderer_draw_glyphs (renderer, font, glyphs, 0, 0);

  if (!do_path)
    {
      ns_pango_renderer_deactivate (renderer);
    }

  restore_current_point (crenderer);

  release_renderer (crenderer);
}

static void
_ns_pango_cairo_do_glyph_item (cairo_t          *cr,
			    const char       *text,
			    NsPangoGlyphItem   *glyph_item,
			    gboolean          do_path)
{
  NsPangoCairoRenderer *crenderer = acquire_renderer ();
  NsPangoRenderer *renderer = (NsPangoRenderer *) crenderer;

  crenderer->cr = cr;
  crenderer->do_path = do_path;
  save_current_point (crenderer);

  if (!do_path)
    {
      /* unset all part colors, since when drawing just a glyph string,
       * prepare_run() isn't called.
       */

      ns_pango_renderer_activate (renderer);

      ns_pango_renderer_set_color (renderer, NS_PANGO_RENDER_PART_FOREGROUND, NULL);
      ns_pango_renderer_set_color (renderer, NS_PANGO_RENDER_PART_BACKGROUND, NULL);
      ns_pango_renderer_set_color (renderer, NS_PANGO_RENDER_PART_UNDERLINE, NULL);
      ns_pango_renderer_set_color (renderer, NS_PANGO_RENDER_PART_STRIKETHROUGH, NULL);
      ns_pango_renderer_set_color (renderer, NS_PANGO_RENDER_PART_OVERLINE, NULL);
    }

  ns_pango_renderer_draw_glyph_item (renderer, text, glyph_item, 0, 0);

  if (!do_path)
    {
      ns_pango_renderer_deactivate (renderer);
    }

  restore_current_point (crenderer);

  release_renderer (crenderer);
}

static void
_ns_pango_cairo_do_layout_line (cairo_t          *cr,
			     NsPangoLayoutLine  *line,
			     gboolean          do_path)
{
  NsPangoCairoRenderer *crenderer = acquire_renderer ();
  NsPangoRenderer *renderer = (NsPangoRenderer *) crenderer;

  crenderer->cr = cr;
  crenderer->do_path = do_path;
  save_current_point (crenderer);

  ns_pango_renderer_draw_layout_line (renderer, line, 0, 0);

  restore_current_point (crenderer);

  release_renderer (crenderer);
}

static void
_ns_pango_cairo_do_layout (cairo_t              *cr,
                        NsPangoLayout          *layout,
                        gboolean              do_path,
                        NsPangoRenderComponent  components)
{
  NsPangoCairoRenderer *crenderer = acquire_renderer ();
  NsPangoRenderer *renderer = (NsPangoRenderer *) crenderer;

  ns_pango_renderer_set_components (renderer, components);

  crenderer->cr = cr;
  crenderer->do_path = do_path;
  save_current_point (crenderer);

  ns_pango_renderer_draw_layout (renderer, layout, 0, 0);

  restore_current_point (crenderer);

  release_renderer (crenderer);
}

static void
_ns_pango_cairo_do_error_underline (cairo_t *cr,
				 double   x,
				 double   y,
				 double   width,
				 double   height,
				 gboolean do_path)
{
  /* We don't use a renderer here, for a simple reason:
   * the only renderer we can get is the default renderer, that
   * is all implemented here, so we shortcircuit and make our
   * life way easier.
   */

  if (!do_path)
    cairo_new_path (cr);

  draw_error_underline (cr, x, y, width, height);

  if (!do_path)
    cairo_fill (cr);
}


/* public wrapper of above to show or append path */


/**
 * ns_pango_cairo_show_glyph_string:
 * @cr: a Cairo context
 * @font: a `NsPangoFont` from a `NsPangoCairoFontMap`
 * @glyphs: a `NsPangoGlyphString`
 *
 * Draws the glyphs in @glyphs in the specified cairo context.
 *
 * The origin of the glyphs (the left edge of the baseline) will
 * be drawn at the current point of the cairo context.
 *
 * Since: 1.10
 */
void
ns_pango_cairo_show_glyph_string (cairo_t          *cr,
			       NsPangoFont        *font,
			       NsPangoGlyphString *glyphs)
{
  g_return_if_fail (cr != NULL);
  g_return_if_fail (glyphs != NULL);

  _ns_pango_cairo_do_glyph_string (cr, font, glyphs, FALSE);
}


/**
 * ns_pango_cairo_show_glyph_item:
 * @cr: a Cairo context
 * @text: the UTF-8 text that @glyph_item refers to
 * @glyph_item: a `NsPangoGlyphItem`
 *
 * Draws the glyphs in @glyph_item in the specified cairo context,
 *
 * embedding the text associated with the glyphs in the output if the
 * output format supports it (PDF for example), otherwise it acts
 * similar to [func@show_glyph_string].
 *
 * The origin of the glyphs (the left edge of the baseline) will
 * be drawn at the current point of the cairo context.
 *
 * Note that @text is the start of the text for layout, which is then
 * indexed by `glyph_item->item->offset`.
 *
 * Since: 1.22
 */
void
ns_pango_cairo_show_glyph_item (cairo_t          *cr,
			     const char       *text,
			     NsPangoGlyphItem   *glyph_item)
{
  g_return_if_fail (cr != NULL);
  g_return_if_fail (text != NULL);
  g_return_if_fail (glyph_item != NULL);

  _ns_pango_cairo_do_glyph_item (cr, text, glyph_item, FALSE);
}

/**
 * ns_pango_cairo_show_layout_line:
 * @cr: a Cairo context
 * @line: a `NsPangoLayoutLine`
 *
 * Draws a `NsPangoLayoutLine` in the specified cairo context.
 *
 * The origin of the glyphs (the left edge of the line) will
 * be drawn at the current point of the cairo context.
 *
 * Since: 1.10
 */
void
ns_pango_cairo_show_layout_line (cairo_t          *cr,
			      NsPangoLayoutLine  *line)
{
  g_return_if_fail (cr != NULL);
  g_return_if_fail (line != NULL);

  _ns_pango_cairo_do_layout_line (cr, line, FALSE);
}

/**
 * ns_pango_cairo_show_layout:
 * @cr: a Cairo context
 * @layout: a Pango layout
 *
 * Draws a `NsPangoLayout` in the specified cairo context.
 *
 * The top-left corner of the `NsPangoLayout` will be drawn
 * at the current point of the cairo context.
 *
 * Since: 1.10
 */
void
ns_pango_cairo_show_layout (cairo_t     *cr,
			 NsPangoLayout *layout)
{
  g_return_if_fail (cr != NULL);
  g_return_if_fail (NS_PANGO_IS_LAYOUT (layout));

  _ns_pango_cairo_do_layout (cr, layout, FALSE, NS_PANGO_RENDER_COMPONENT_ALL);
}

/**
 * ns_pango_cairo_show_error_underline:
 * @cr: a Cairo context
 * @x: The X coordinate of one corner of the rectangle
 * @y: The Y coordinate of one corner of the rectangle
 * @width: Non-negative width of the rectangle
 * @height: Non-negative height of the rectangle
 *
 * Draw a squiggly line in the specified cairo context that approximately
 * covers the given rectangle in the style of an underline used to indicate a
 * spelling error.
 *
 * The width of the underline is rounded to an integer
 * number of up/down segments and the resulting rectangle is centered in the
 * original rectangle.
 *
 * Since: 1.14
 */
void
ns_pango_cairo_show_error_underline (cairo_t *cr,
				  double  x,
				  double  y,
				  double  width,
				  double  height)
{
  g_return_if_fail (cr != NULL);
  g_return_if_fail ((width >= 0) && (height >= 0));

  _ns_pango_cairo_do_error_underline (cr, x, y, width, height, FALSE);
}

/**
 * ns_pango_cairo_glyph_string_path:
 * @cr: a Cairo context
 * @font: a `NsPangoFont` from a `NsPangoCairoFontMap`
 * @glyphs: a `NsPangoGlyphString`
 *
 * Adds the glyphs in @glyphs to the current path in the specified
 * cairo context.
 *
 * The origin of the glyphs (the left edge of the baseline)
 * will be at the current point of the cairo context.
 *
 * Since: 1.10
 */
void
ns_pango_cairo_glyph_string_path (cairo_t          *cr,
			       NsPangoFont        *font,
			       NsPangoGlyphString *glyphs)
{
  g_return_if_fail (cr != NULL);
  g_return_if_fail (glyphs != NULL);

  _ns_pango_cairo_do_glyph_string (cr, font, glyphs, TRUE);
}

/**
 * ns_pango_cairo_layout_line_path:
 * @cr: a Cairo context
 * @line: a `NsPangoLayoutLine`
 *
 * Adds the text in `NsPangoLayoutLine` to the current path in the
 * specified cairo context.
 *
 * The origin of the glyphs (the left edge of the line) will be
 * at the current point of the cairo context.
 *
 * Since: 1.10
 */
void
ns_pango_cairo_layout_line_path (cairo_t          *cr,
			      NsPangoLayoutLine  *line)
{
  g_return_if_fail (cr != NULL);
  g_return_if_fail (line != NULL);

  _ns_pango_cairo_do_layout_line (cr, line, TRUE);
}

/**
 * ns_pango_cairo_layout_path:
 * @cr: a Cairo context
 * @layout: a Pango layout
 *
 * Adds the text in a `NsPangoLayout` to the current path in the
 * specified cairo context.
 *
 * The top-left corner of the `NsPangoLayout` will be at the
 * current point of the cairo context.
 *
 * Since: 1.10
 */
void
ns_pango_cairo_layout_path (cairo_t     *cr,
			 NsPangoLayout *layout)
{
  g_return_if_fail (cr != NULL);
  g_return_if_fail (NS_PANGO_IS_LAYOUT (layout));

  _ns_pango_cairo_do_layout (cr, layout, TRUE, NS_PANGO_RENDER_COMPONENT_ALL);
}

/**
 * ns_pango_cairo_layout_path_for_components:
 * @cr: a Cairo context
 * @layout: a Pango layout
 * @components: the components to include
 *
 * Adds components of a `NsPangoLayout` to the current path
 * in the specified cairo context.
 *
 * The top-left corner of the `NsPangoLayout` will be at the
 * current point of the cairo context.
 *
 * Since: 1.58
 */
void
ns_pango_cairo_layout_path_for_components (cairo_t              *cr,
                                        NsPangoLayout          *layout,
                                        NsPangoRenderComponent  components)
{
  g_return_if_fail (cr != NULL);
  g_return_if_fail (NS_PANGO_IS_LAYOUT (layout));

  _ns_pango_cairo_do_layout (cr, layout, TRUE, components);
}
/**
 * ns_pango_cairo_error_underline_path:
 * @cr: a Cairo context
 * @x: The X coordinate of one corner of the rectangle
 * @y: The Y coordinate of one corner of the rectangle
 * @width: Non-negative width of the rectangle
 * @height: Non-negative height of the rectangle
 *
 * Add a squiggly line to the current path in the specified cairo context that
 * approximately covers the given rectangle in the style of an underline used
 * to indicate a spelling error.
 *
 * The width of the underline is rounded to an integer number of up/down
 * segments and the resulting rectangle is centered in the original rectangle.
 *
 * Since: 1.14
 */
void
ns_pango_cairo_error_underline_path (cairo_t *cr,
				  double   x,
				  double   y,
				  double   width,
				  double   height)
{
  g_return_if_fail (cr != NULL);
  g_return_if_fail ((width >= 0) && (height >= 0));

  _ns_pango_cairo_do_error_underline (cr, x, y, width, height, TRUE);
}
