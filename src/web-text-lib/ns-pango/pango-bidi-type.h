/* Pango
 * pango-bidi-type.h: Bidirectional Character Types
 *
 * Copyright (C) 2008 Jürg Billeter <j@bitron.ch>
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

#ifndef __NS_PANGO_BIDI_TYPE_H__
#define __NS_PANGO_BIDI_TYPE_H__

#include <glib.h>

#include <ns-pango/pango-version-macros.h>
#include <ns-pango/pango-direction.h>

G_BEGIN_DECLS

#ifndef NS_PANGO_DISABLE_DEPRECATED
/**
 * NsPangoBidiType:
 * @NS_PANGO_BIDI_TYPE_L: Left-to-Right
 * @NS_PANGO_BIDI_TYPE_LRE: Left-to-Right Embedding
 * @NS_PANGO_BIDI_TYPE_LRO: Left-to-Right Override
 * @NS_PANGO_BIDI_TYPE_R: Right-to-Left
 * @NS_PANGO_BIDI_TYPE_AL: Right-to-Left Arabic
 * @NS_PANGO_BIDI_TYPE_RLE: Right-to-Left Embedding
 * @NS_PANGO_BIDI_TYPE_RLO: Right-to-Left Override
 * @NS_PANGO_BIDI_TYPE_PDF: Pop Directional Format
 * @NS_PANGO_BIDI_TYPE_EN: European Number
 * @NS_PANGO_BIDI_TYPE_ES: European Number Separator
 * @NS_PANGO_BIDI_TYPE_ET: European Number Terminator
 * @NS_PANGO_BIDI_TYPE_AN: Arabic Number
 * @NS_PANGO_BIDI_TYPE_CS: Common Number Separator
 * @NS_PANGO_BIDI_TYPE_NSM: Nonspacing Mark
 * @NS_PANGO_BIDI_TYPE_BN: Boundary Neutral
 * @NS_PANGO_BIDI_TYPE_B: Paragraph Separator
 * @NS_PANGO_BIDI_TYPE_S: Segment Separator
 * @NS_PANGO_BIDI_TYPE_WS: Whitespace
 * @NS_PANGO_BIDI_TYPE_ON: Other Neutrals
 * @NS_PANGO_BIDI_TYPE_LRI: Left-to-Right isolate. Since 1.48.6
 * @NS_PANGO_BIDI_TYPE_RLI: Right-to-Left isolate. Since 1.48.6
 * @NS_PANGO_BIDI_TYPE_FSI: First strong isolate. Since 1.48.6
 * @NS_PANGO_BIDI_TYPE_PDI: Pop directional isolate. Since 1.48.6
 *
 * `NsPangoBidiType` represents the bidirectional character
 * type of a Unicode character.
 *
 * The values in this enumeration are specified by the
 * [Unicode bidirectional algorithm](http://www.unicode.org/reports/tr9/).
 *
 * Since: 1.22
 * Deprecated: 1.44: Use fribidi for this information
 **/
typedef enum {
  /* Strong types */
  NS_PANGO_BIDI_TYPE_L,
  NS_PANGO_BIDI_TYPE_LRE,
  NS_PANGO_BIDI_TYPE_LRO,
  NS_PANGO_BIDI_TYPE_R,
  NS_PANGO_BIDI_TYPE_AL,
  NS_PANGO_BIDI_TYPE_RLE,
  NS_PANGO_BIDI_TYPE_RLO,

  /* Weak types */
  NS_PANGO_BIDI_TYPE_PDF,
  NS_PANGO_BIDI_TYPE_EN,
  NS_PANGO_BIDI_TYPE_ES,
  NS_PANGO_BIDI_TYPE_ET,
  NS_PANGO_BIDI_TYPE_AN,
  NS_PANGO_BIDI_TYPE_CS,
  NS_PANGO_BIDI_TYPE_NSM,
  NS_PANGO_BIDI_TYPE_BN,

  /* Neutral types */
  NS_PANGO_BIDI_TYPE_B,
  NS_PANGO_BIDI_TYPE_S,
  NS_PANGO_BIDI_TYPE_WS,
  NS_PANGO_BIDI_TYPE_ON,

  /* Explicit formatting */
  NS_PANGO_BIDI_TYPE_LRI,
  NS_PANGO_BIDI_TYPE_RLI,
  NS_PANGO_BIDI_TYPE_FSI,
  NS_PANGO_BIDI_TYPE_PDI
} NsPangoBidiType;

NS_PANGO_DEPRECATED_IN_1_44
NsPangoBidiType ns_pango_bidi_type_for_unichar (gunichar ch) G_GNUC_CONST;

NS_PANGO_DEPRECATED_IN_1_44
NsPangoDirection ns_pango_unichar_direction      (gunichar     ch) G_GNUC_CONST;
NS_PANGO_DEPRECATED_IN_1_44
NsPangoDirection ns_pango_find_base_dir          (const gchar *text,
					     gint         length);

NS_PANGO_DEPRECATED_IN_1_30_FOR(g_unichar_get_mirror_char)
gboolean       ns_pango_get_mirror_char        (gunichar     ch,
					     gunichar    *mirrored_ch);
#endif

G_END_DECLS

#endif /* __NS_PANGO_BIDI_TYPE_H__ */
