/* Pango
 * pango-utils.c: Utilities for internal functions and modules
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

#include "config.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <locale.h>

#include "pango-font.h"
#include "pango-features.h"
#include "pango-impl-utils.h"
#include "pango-utils-internal.h"
#include "pango-utils-private.h"

#include <glib/gstdio.h>

/**
 * ns_pango_version:
 *
 * Returns the encoded version of Pango available at run-time.
 *
 * This is similar to the macro %NS_PANGO_VERSION except that the macro
 * returns the encoded version available at compile-time. A version
 * number can be encoded into an integer using NS_PANGO_VERSION_ENCODE().
 *
 * Returns: The encoded version of Pango library available at run time.
 *
 * Since: 1.16
 */
int
ns_pango_version (void)
{
  return NS_PANGO_VERSION;
}

/**
 * ns_pango_version_string:
 *
 * Returns the version of Pango available at run-time.
 *
 * This is similar to the macro %NS_PANGO_VERSION_STRING except that the
 * macro returns the version available at compile-time.
 *
 * Returns: (transfer none): A string containing the version of Pango library available
 *   at run time. The returned string is owned by Pango and should not
 *   be modified or freed.
 *
 * Since: 1.16
 */
const char *
ns_pango_version_string (void)
{
  return NS_PANGO_VERSION_STRING;
}


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

/**
 * ns_pango_find_base_dir:
 * @text: the text to process. Must be valid UTF-8
 * @length: length of @text in bytes (may be -1 if @text is nul-terminated)
 *
 * Searches a string the first character that has a strong
 * direction, according to the Unicode bidirectional algorithm.
 *
 * Returns: The direction corresponding to the first strong character.
 *   If no such character is found, then %NS_PANGO_DIRECTION_NEUTRAL is returned.
 *
 * Since: 1.4
 */
NsPangoDirection
ns_pango_find_base_dir (const gchar *text,
		     gint         length)
{
  NsPangoDirection dir = NS_PANGO_DIRECTION_NEUTRAL;
  const gchar *p;

  g_return_val_if_fail (text != NULL || length == 0, NS_PANGO_DIRECTION_NEUTRAL);

  p = text;
  while ((length < 0 || p < text + length) && *p)
    {
      gunichar wc = g_utf8_get_char (p);

      dir = ns_pango_unichar_direction (wc);

      if (dir != NS_PANGO_DIRECTION_NEUTRAL)
	break;

      p = g_utf8_next_char (p);
    }

  return dir;
}

#pragma GCC diagnostic pop

/**
 * ns_pango_is_zero_width:
 * @ch: a Unicode character
 *
 * Checks if a character that should not be normally rendered.
 *
 * This includes all Unicode characters with "ZERO WIDTH" in their name,
 * as well as *bidi* formatting characters, and a few other ones.
 *
 * This is totally different from [func@GLib.unichar_iszerowidth] and is at best misnamed.
 *
 * Returns: %TRUE if @ch is a zero-width character, %FALSE otherwise
 *
 * Since: 1.10
 */
gboolean
ns_pango_is_zero_width (gunichar ch)
{
/* Zero Width characters:
 *
 *  00AD  SOFT HYPHEN
 *  034F  COMBINING GRAPHEME JOINER
 *
 *  200B  ZERO WIDTH SPACE
 *  200C  ZERO WIDTH NON-JOINER
 *  200D  ZERO WIDTH JOINER
 *  200E  LEFT-TO-RIGHT MARK
 *  200F  RIGHT-TO-LEFT MARK
 *
 *  2028  LINE SEPARATOR
 *
 *  2060  WORD JOINER
 *  2061  FUNCTION APPLICATION
 *  2062  INVISIBLE TIMES
 *  2063  INVISIBLE SEPARATOR
 *
 *  2066  LEFT-TO-RIGHT ISOLATE
 *  2067  RIGHT-TO-LEFT ISOLATE
 *  2068  FIRST STRONG ISOLATE
 *  2069  POP DIRECTIONAL ISOLATE
 *
 *  202A  LEFT-TO-RIGHT EMBEDDING
 *  202B  RIGHT-TO-LEFT EMBEDDING
 *  202C  POP DIRECTIONAL FORMATTING
 *  202D  LEFT-TO-RIGHT OVERRIDE
 *  202E  RIGHT-TO-LEFT OVERRIDE
 *
 *  FEFF  ZERO WIDTH NO-BREAK SPACE
 */
  return ((ch & ~(gunichar)0x007F) == 0x2000 && (
		(ch >= 0x200B && ch <= 0x200F) ||
		(ch >= 0x202A && ch <= 0x202E) ||
		(ch >= 0x2060 && ch <= 0x2063) ||
                (ch >= 0x2066 && ch <= 0x2069) ||
		(ch == 0x2028)
	 )) || G_UNLIKELY (ch == 0x00AD
			|| ch == 0x034F
			|| ch == 0xFEFF);
}

/**
 * ns_pango_units_from_double:
 * @d: double floating-point value
 *
 * Converts a floating-point number to Pango units.
 *
 * The conversion is done by multiplying @d by %NS_PANGO_SCALE and
 * rounding the result to nearest integer.
 *
 * Returns: the value in Pango units.
 *
 * Since: 1.16
 */
int
ns_pango_units_from_double (double d)
{
  return (int)floor (d * NS_PANGO_SCALE + 0.5);
}

/**
 * ns_pango_extents_to_pixels:
 * @inclusive: (inout) (optional): rectangle to round to pixels inclusively
 * @nearest: (inout) (optional): rectangle to round to nearest pixels
 *
 * Converts extents from Pango units to device units.
 *
 * The conversion is done by dividing by the %NS_PANGO_SCALE factor and
 * performing rounding.
 *
 * The @inclusive rectangle is converted by flooring the x/y coordinates
 * and extending width/height, such that the final rectangle completely
 * includes the original rectangle.
 *
 * The @nearest rectangle is converted by rounding the coordinates
 * of the rectangle to the nearest device unit (pixel).
 *
 * The rule to which argument to use is: if you want the resulting device-space
 * rectangle to completely contain the original rectangle, pass it in as
 * @inclusive. If you want two touching-but-not-overlapping rectangles stay
 * touching-but-not-overlapping after rounding to device units, pass them in
 * as @nearest.
 *
 * Since: 1.16
 */
void
ns_pango_extents_to_pixels (NsPangoRectangle *inclusive,
			 NsPangoRectangle *nearest)
{
  if (inclusive)
    {
      int orig_x = inclusive->x;
      int orig_y = inclusive->y;

      inclusive->x = NS_PANGO_PIXELS_FLOOR (inclusive->x);
      inclusive->y = NS_PANGO_PIXELS_FLOOR (inclusive->y);

      inclusive->width  = NS_PANGO_PIXELS_CEIL (orig_x + inclusive->width ) - inclusive->x;
      inclusive->height = NS_PANGO_PIXELS_CEIL (orig_y + inclusive->height) - inclusive->y;
    }

  if (nearest)
    {
      int orig_x = nearest->x;
      int orig_y = nearest->y;

      nearest->x = NS_PANGO_PIXELS (nearest->x);
      nearest->y = NS_PANGO_PIXELS (nearest->y);

      nearest->width  = NS_PANGO_PIXELS (orig_x + nearest->width ) - nearest->x;
      nearest->height = NS_PANGO_PIXELS (orig_y + nearest->height) - nearest->y;
    }
}





/*********************************************************
 * Some internal functions for handling NS_PANGO_ATTR_SHAPE *
 ********************************************************/

void
_ns_pango_shape_shape (const char       *text,
		    unsigned int      n_chars,
		    NsPangoRectangle   *shape_ink G_GNUC_UNUSED,
		    NsPangoRectangle   *shape_logical,
		    NsPangoGlyphString *glyphs)
{
  unsigned int i;
  const char *p;

  ns_pango_glyph_string_set_size (glyphs, n_chars);

  for (i=0, p = text; i < n_chars; i++, p = g_utf8_next_char (p))
    {
      glyphs->glyphs[i].glyph = NS_PANGO_GLYPH_EMPTY;
      glyphs->glyphs[i].geometry.x_offset = 0;
      glyphs->glyphs[i].geometry.y_offset = 0;
      glyphs->glyphs[i].geometry.width = shape_logical->width;
      glyphs->glyphs[i].attr.is_cluster_start = 1;

      glyphs->log_clusters[i] = p - text;
    }
}

void
_ns_pango_shape_get_extents (gint              n_chars,
			  NsPangoRectangle   *shape_ink,
			  NsPangoRectangle   *shape_logical,
			  NsPangoRectangle   *ink_rect,
			  NsPangoRectangle   *logical_rect)
{
  if (n_chars > 0)
    {
      if (ink_rect)
	{
	  ink_rect->x = MIN (shape_ink->x, shape_ink->x + shape_logical->width * (n_chars - 1));
	  ink_rect->width = MAX (shape_ink->width, shape_ink->width + shape_logical->width * (n_chars - 1));
	  ink_rect->y = shape_ink->y;
	  ink_rect->height = shape_ink->height;
	}
      if (logical_rect)
	{
	  logical_rect->x = MIN (shape_logical->x, shape_logical->x + shape_logical->width * (n_chars - 1));
	  logical_rect->width = MAX (shape_logical->width, shape_logical->width + shape_logical->width * (n_chars - 1));
	  logical_rect->y = shape_logical->y;
	  logical_rect->height = shape_logical->height;
	}
    }
  else
    {
      if (ink_rect)
	{
	  ink_rect->x = 0;
	  ink_rect->y = 0;
	  ink_rect->width = 0;
	  ink_rect->height = 0;
	}

      if (logical_rect)
	{
	  logical_rect->x = 0;
	  logical_rect->y = 0;
	  logical_rect->width = 0;
	  logical_rect->height = 0;
	}
    }
}

/**
 * ns_pango_find_paragraph_boundary:
 * @text: UTF-8 text
 * @length: length of @text in bytes, or -1 if nul-terminated
 * @paragraph_delimiter_index: (out) (optional): return location
 *  for index of delimiter
 * @next_paragraph_start: (out) (optional): return location for
 *  start of next paragraph
 *
 * Locates a paragraph boundary in @text.
 *
 * A boundary is caused by delimiter characters, such as
 * a newline, carriage return, carriage return-newline pair,
 * or Unicode paragraph separator character.
 *
 * The index of the run of delimiters is returned in
 * @paragraph_delimiter_index. The index of the start of the
 * next paragraph (index after all delimiters) is stored n
 * @next_paragraph_start.
 *
 * If no delimiters are found, both @paragraph_delimiter_index
 * and @next_paragraph_start are filled with the length of @text
 * (an index one off the end).
 */
void
ns_pango_find_paragraph_boundary (const char *text,
                               int         length,
                               int        *paragraph_delimiter_index,
                               int        *next_paragraph_start)
{
  const char *p = text;
  const char *end;
  const char *start = NULL;
  const char *delimiter = NULL;

  /* Only one character has type G_UNICODE_PARAGRAPH_SEPARATOR in
   * Unicode 5.0; update the following code if that changes.
   */

  /* prev_sep is the first byte of the previous separator.  Since
   * the valid separators are \r, \n, and PARAGRAPH_SEPARATOR, the
   * first byte is enough to identify it.
   */
  char prev_sep;

#define PARAGRAPH_SEPARATOR_STRING "\xE2\x80\xA9"

  if (length < 0)
    length = strlen (text);

  end = text + length;

  if (paragraph_delimiter_index)
    *paragraph_delimiter_index = length;

  if (next_paragraph_start)
    *next_paragraph_start = length;

  if (length == 0)
    return;

  prev_sep = 0;
  while (p < end)
    {
      if (prev_sep == '\n' ||
          prev_sep == PARAGRAPH_SEPARATOR_STRING[0])
        {
          g_assert (delimiter);
          start = p;
          break;
        }
      else if (prev_sep == '\r')
        {
          /* don't break between \r and \n */
          if (*p != '\n')
            {
              g_assert (delimiter);
              start = p;
              break;
            }
        }

      if (*p == '\n' ||
           *p == '\r' ||
           !strncmp(p, PARAGRAPH_SEPARATOR_STRING, strlen (PARAGRAPH_SEPARATOR_STRING)))
        {
          if (delimiter == NULL)
            delimiter = p;
          prev_sep = *p;
        }
      else
        prev_sep = 0;

      p = g_utf8_next_char (p);
    }

  if (delimiter && paragraph_delimiter_index)
    *paragraph_delimiter_index = delimiter - text;

  if (start && next_paragraph_start)
    *next_paragraph_start = start - text;
}
