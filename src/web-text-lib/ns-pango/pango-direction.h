/* Pango
 * pango-direction.h: Unicode text direction
 *
 * Copyright (C) 2018 Matthias Clasen
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

#ifndef __NS_PANGO_DIRECTION_H__
#define __NS_PANGO_DIRECTION_H__

#include <glib.h>

G_BEGIN_DECLS

/**
 * NsPangoDirection:
 * @NS_PANGO_DIRECTION_LTR: A strong left-to-right direction
 * @NS_PANGO_DIRECTION_RTL: A strong right-to-left direction
 * @NS_PANGO_DIRECTION_TTB_LTR: Deprecated value; treated the
 *   same as `NS_PANGO_DIRECTION_RTL`.
 * @NS_PANGO_DIRECTION_TTB_RTL: Deprecated value; treated the
 *   same as `NS_PANGO_DIRECTION_LTR`
 * @NS_PANGO_DIRECTION_WEAK_LTR: A weak left-to-right direction
 * @NS_PANGO_DIRECTION_WEAK_RTL: A weak right-to-left direction
 * @NS_PANGO_DIRECTION_NEUTRAL: No direction specified
 *
 * `NsPangoDirection` represents a direction in the Unicode bidirectional
 * algorithm.
 *
 * Not every value in this enumeration makes sense for every usage of
 * `NsPangoDirection`; for example, the return value of [func@unichar_direction]
 * and [func@find_base_dir] cannot be `NS_PANGO_DIRECTION_WEAK_LTR` or
 * `NS_PANGO_DIRECTION_WEAK_RTL`, since every character is either neutral
 * or has a strong direction; on the other hand `NS_PANGO_DIRECTION_NEUTRAL`
 * doesn't make sense to pass to [func@itemize_with_base_dir].
 *
 * The `NS_PANGO_DIRECTION_TTB_LTR`, `NS_PANGO_DIRECTION_TTB_RTL` values come from
 * an earlier interpretation of this enumeration as the writing direction
 * of a block of text and are no longer used. See `NsPangoGravity` for how
 * vertical text is handled in Pango.
 *
 * If you are interested in text direction, you should really use fribidi
 * directly. `NsPangoDirection` is only retained because it is used in some
 * public apis.
 */
typedef enum {
  NS_PANGO_DIRECTION_LTR,
  NS_PANGO_DIRECTION_RTL,
  NS_PANGO_DIRECTION_TTB_LTR,
  NS_PANGO_DIRECTION_TTB_RTL,
  NS_PANGO_DIRECTION_WEAK_LTR,
  NS_PANGO_DIRECTION_WEAK_RTL,
  NS_PANGO_DIRECTION_NEUTRAL
} NsPangoDirection;

G_END_DECLS

#endif /* __NS_PANGO_DIRECTION_H__ */
