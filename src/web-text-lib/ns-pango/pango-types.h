/* Pango
 * pango-types.h:
 *
 * Copyright (C) 1999 Red Hat Software
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

#ifndef __NS_PANGO_TYPES_H__
#define __NS_PANGO_TYPES_H__

#include <glib.h>
#include <glib-object.h>

#include <ns-pango/pango-version-macros.h>

G_BEGIN_DECLS

typedef struct _PangoLogAttr NsPangoLogAttr;

#ifndef __GI_SCANNER__
typedef struct _PangoEngineLang NsPangoEngineLang;
typedef struct _PangoEngineShape NsPangoEngineShape;
#endif

typedef struct _PangoFont    NsPangoFont;
typedef struct _PangoFontMap NsPangoFontMap;

typedef struct _PangoRectangle NsPangoRectangle;

typedef struct _PangoContext NsPangoContext;

typedef struct _PangoLanguage NsPangoLanguage;

/* A index of a glyph into a font. Rendering system dependent */
/**
 * NsPangoGlyph:
 *
 * A `NsPangoGlyph` represents a single glyph in the output form of a string.
 */
typedef guint32 NsPangoGlyph;



/**
 * NS_PANGO_SCALE:
 *
 * The scale between dimensions used for Pango distances and device units.
 *
 * The definition of device units is dependent on the output device; it will
 * typically be pixels for a screen, and points for a printer. %NS_PANGO_SCALE is
 * currently 1024, but this may be changed in the future.
 *
 * When setting font sizes, device units are always considered to be
 * points (as in "12 point font"), rather than pixels.
 */
/**
 * NS_PANGO_PIXELS:
 * @d: a dimension in Pango units.
 *
 * Converts a dimension to device units by rounding.
 *
 * Return value: rounded dimension in device units.
 */
/**
 * NS_PANGO_PIXELS_FLOOR:
 * @d: a dimension in Pango units.
 *
 * Converts a dimension to device units by flooring.
 *
 * Return value: floored dimension in device units.
 * Since: 1.14
 */
/**
 * NS_PANGO_PIXELS_CEIL:
 * @d: a dimension in Pango units.
 *
 * Converts a dimension to device units by ceiling.
 *
 * Return value: ceiled dimension in device units.
 * Since: 1.14
 */
#define NS_PANGO_SCALE 1024
#define NS_PANGO_PIXELS(d) (((int)(d) + 512) >> 10)
#define NS_PANGO_PIXELS_FLOOR(d) (((int)(d)) >> 10)
#define NS_PANGO_PIXELS_CEIL(d) (((int)(d) + 1023) >> 10)
/* The above expressions are just slightly wrong for floating point d;
 * For example we'd expect NS_PANGO_PIXELS(-512.5) => -1 but instead we get 0.
 * That's unlikely to matter for practical use and the expression is much
 * more compact and faster than alternatives that work exactly for both
 * integers and floating point.
 *
 * NS_PANGO_PIXELS also behaves differently for +512 and -512.
 */

/**
 * NS_PANGO_UNITS_FLOOR:
 * @d: a dimension in Pango units.
 *
 * Rounds a dimension down to whole device units, but does not
 * convert it to device units.
 *
 * Return value: rounded down dimension in Pango units.
 * Since: 1.50
 */
#define NS_PANGO_UNITS_FLOOR(d)                \
  ((d) & ~(NS_PANGO_SCALE - 1))

/**
 * NS_PANGO_UNITS_CEIL:
 * @d: a dimension in Pango units.
 *
 * Rounds a dimension up to whole device units, but does not
 * convert it to device units.
 *
 * Return value: rounded up dimension in Pango units.
 * Since: 1.50
 */
#define NS_PANGO_UNITS_CEIL(d)                 \
  (((d) + (NS_PANGO_SCALE - 1)) & ~(NS_PANGO_SCALE - 1))

/**
 * NS_PANGO_UNITS_ROUND:
 * @d: a dimension in Pango units.
 *
 * Rounds a dimension to whole device units, but does not
 * convert it to device units.
 *
 * Return value: rounded dimension in Pango units.
 * Since: 1.18
 */
#define NS_PANGO_UNITS_ROUND(d)				\
  (((d) + (NS_PANGO_SCALE >> 1)) & ~(NS_PANGO_SCALE - 1))


NS_PANGO_AVAILABLE_IN_1_16
int    ns_pango_units_from_double (double d) G_GNUC_CONST;
NS_PANGO_AVAILABLE_IN_1_16
double ns_pango_units_to_double (int i) G_GNUC_CONST;



/**
 * NsPangoRectangle:
 * @x: X coordinate of the left side of the rectangle.
 * @y: Y coordinate of the the top side of the rectangle.
 * @width: width of the rectangle.
 * @height: height of the rectangle.
 *
 * The `NsPangoRectangle` structure represents a rectangle.
 *
 * `NsPangoRectangle` is frequently used to represent the logical or ink
 * extents of a single glyph or section of text. (See, for instance,
 * [method@Pango.Font.get_glyph_extents].)
 */
struct _PangoRectangle
{
  int x;
  int y;
  int width;
  int height;
};

/* Macros to translate from extents rectangles to ascent/descent/lbearing/rbearing
 */
/**
 * NS_PANGO_ASCENT:
 * @rect: a `NsPangoRectangle`
 *
 * Extracts the *ascent* from a `NsPangoRectangle`
 * representing glyph extents.
 *
 * The ascent is the distance from the baseline to the
 * highest point of the character. This is positive if the
 * glyph ascends above the baseline.
 */
/**
 * NS_PANGO_DESCENT:
 * @rect: a `NsPangoRectangle`
 *
 * Extracts the *descent* from a `NsPangoRectangle`
 * representing glyph extents.
 *
 * The descent is the distance from the baseline to the
 * lowest point of the character. This is positive if the
 * glyph descends below the baseline.
 */
/**
 * NS_PANGO_LBEARING:
 * @rect: a `NsPangoRectangle`
 *
 * Extracts the *left bearing* from a `NsPangoRectangle`
 * representing glyph extents.
 *
 * The left bearing is the distance from the horizontal
 * origin to the farthest left point of the character.
 * This is positive for characters drawn completely to
 * the right of the glyph origin.
 */
/**
 * NS_PANGO_RBEARING:
 * @rect: a `NsPangoRectangle`
 *
 * Extracts the *right bearing* from a `NsPangoRectangle`
 * representing glyph extents.
 *
 * The right bearing is the distance from the horizontal
 * origin to the farthest right point of the character.
 * This is positive except for characters drawn completely
 * to the left of the horizontal origin.
 */
#define NS_PANGO_ASCENT(rect) (-(rect).y)
#define NS_PANGO_DESCENT(rect) ((rect).y + (rect).height)
#define NS_PANGO_LBEARING(rect) ((rect).x)
#define NS_PANGO_RBEARING(rect) ((rect).x + (rect).width)

NS_PANGO_AVAILABLE_IN_1_16
void ns_pango_extents_to_pixels (NsPangoRectangle *inclusive,
			      NsPangoRectangle *nearest);


#include <ns-pango/pango-gravity.h>
#include <ns-pango/pango-language.h>
#include <ns-pango/pango-matrix.h>
#include <ns-pango/pango-script.h>
#include <ns-pango/pango-bidi-type.h>


G_END_DECLS

#endif /* __NS_PANGO_TYPES_H__ */
