/* Pango
 * pango-gravity.h: Gravity routines
 *
 * Copyright (C) 2006, 2007 Red Hat Software
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

#ifndef __NS_PANGO_GRAVITY_H__
#define __NS_PANGO_GRAVITY_H__

#include <glib.h>

G_BEGIN_DECLS

/**
 * NsPangoGravity:
 * @NS_PANGO_GRAVITY_SOUTH: Glyphs stand upright (default) <img align="right" valign="center" src="m-south.png">
 * @NS_PANGO_GRAVITY_EAST: Glyphs are rotated 90 degrees counter-clockwise. <img align="right" valign="center" src="m-east.png">
 * @NS_PANGO_GRAVITY_NORTH: Glyphs are upside-down. <img align="right" valign="cener" src="m-north.png">
 * @NS_PANGO_GRAVITY_WEST: Glyphs are rotated 90 degrees clockwise. <img align="right" valign="center" src="m-west.png">
 * @NS_PANGO_GRAVITY_AUTO: Gravity is resolved from the context matrix
 *
 * `NsPangoGravity` represents the orientation of glyphs in a segment
 * of text.
 *
 * This is useful when rendering vertical text layouts. In those situations,
 * the layout is rotated using a non-identity [struct@Pango.Matrix], and then
 * glyph orientation is controlled using `NsPangoGravity`.
 *
 * Not every value in this enumeration makes sense for every usage of
 * `NsPangoGravity`; for example, %NS_PANGO_GRAVITY_AUTO only can be passed to
 * [method@Pango.Context.set_base_gravity] and can only be returned by
 * [method@Pango.Context.get_base_gravity].
 *
 * See also: [enum@Pango.GravityHint]
 *
 * Since: 1.16
 */
typedef enum {
  NS_PANGO_GRAVITY_SOUTH,
  NS_PANGO_GRAVITY_EAST,
  NS_PANGO_GRAVITY_NORTH,
  NS_PANGO_GRAVITY_WEST,
  NS_PANGO_GRAVITY_AUTO
} NsPangoGravity;

/**
 * NsPangoGravityHint:
 * @NS_PANGO_GRAVITY_HINT_NATURAL: scripts will take their natural gravity based
 *   on the base gravity and the script.  This is the default.
 * @NS_PANGO_GRAVITY_HINT_STRONG: always use the base gravity set, regardless of
 *   the script.
 * @NS_PANGO_GRAVITY_HINT_LINE: for scripts not in their natural direction (eg.
 *   Latin in East gravity), choose per-script gravity such that every script
 *   respects the line progression. This means, Latin and Arabic will take
 *   opposite gravities and both flow top-to-bottom for example.
 *
 * `NsPangoGravityHint` defines how horizontal scripts should behave in a
 * vertical context.
 *
 * That is, English excerpts in a vertical paragraph for example.
 *
 * See also [enum@Pango.Gravity]
 *
 * Since: 1.16
 */
typedef enum {
  NS_PANGO_GRAVITY_HINT_NATURAL,
  NS_PANGO_GRAVITY_HINT_STRONG,
  NS_PANGO_GRAVITY_HINT_LINE
} NsPangoGravityHint;

/**
 * NS_PANGO_GRAVITY_IS_VERTICAL:
 * @gravity: the `NsPangoGravity` to check
 *
 * Whether a `NsPangoGravity` represents vertical writing directions.
 *
 * Returns: %TRUE if @gravity is %NS_PANGO_GRAVITY_EAST or %NS_PANGO_GRAVITY_WEST,
 *   %FALSE otherwise.
 *
 * Since: 1.16
 */
#define NS_PANGO_GRAVITY_IS_VERTICAL(gravity) \
	((gravity) == NS_PANGO_GRAVITY_EAST || (gravity) == NS_PANGO_GRAVITY_WEST)

/**
 * NS_PANGO_GRAVITY_IS_IMPROPER:
 * @gravity: the `NsPangoGravity` to check
 *
 * Whether a `NsPangoGravity` represents a gravity that results in reversal
 * of text direction.
 *
 * Returns: %TRUE if @gravity is %NS_PANGO_GRAVITY_WEST or %NS_PANGO_GRAVITY_NORTH,
 *   %FALSE otherwise.
 *
 * Since: 1.32
 */
#define NS_PANGO_GRAVITY_IS_IMPROPER(gravity) \
	((gravity) == NS_PANGO_GRAVITY_WEST || (gravity) == NS_PANGO_GRAVITY_NORTH)

#include <ns-pango/pango-matrix.h>
#include <ns-pango/pango-script.h>

NS_PANGO_AVAILABLE_IN_1_16
double       ns_pango_gravity_to_rotation    (NsPangoGravity       gravity) G_GNUC_CONST;
NS_PANGO_AVAILABLE_IN_1_16
NsPangoGravity ns_pango_gravity_get_for_matrix (const NsPangoMatrix *matrix) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_1_16
NsPangoGravity ns_pango_gravity_get_for_script (NsPangoScript        script,
					   NsPangoGravity       base_gravity,
					   NsPangoGravityHint   hint) G_GNUC_CONST;
NS_PANGO_AVAILABLE_IN_1_26
NsPangoGravity ns_pango_gravity_get_for_script_and_width
					  (NsPangoScript        script,
					   gboolean           wide,
					   NsPangoGravity       base_gravity,
					   NsPangoGravityHint   hint) G_GNUC_CONST;


G_END_DECLS

#endif /* __NS_PANGO_GRAVITY_H__ */
