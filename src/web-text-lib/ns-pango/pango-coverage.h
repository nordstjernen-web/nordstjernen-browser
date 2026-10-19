/* Pango
 * pango-coverage.h: Coverage sets for fonts
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

#ifndef __NS_PANGO_COVERAGE_H__
#define __NS_PANGO_COVERAGE_H__

#include <glib-object.h>

#include <ns-pango/pango-version-macros.h>
#include <hb.h>

G_BEGIN_DECLS

/**
 * NsPangoCoverage:
 *
 * A `NsPangoCoverage` structure is a map from Unicode characters
 * to [enum@Pango.CoverageLevel] values.
 *
 * It is often necessary in Pango to determine if a particular
 * font can represent a particular character, and also how well
 * it can represent that character. The `NsPangoCoverage` is a data
 * structure that is used to represent that information. It is an
 * opaque structure with no public fields.
 */
typedef struct _PangoCoverage NsPangoCoverage;

/**
 * NsPangoCoverageLevel:
 * @NS_PANGO_COVERAGE_NONE: The character is not representable with
 *   the font.
 * @NS_PANGO_COVERAGE_FALLBACK: The character is represented in a
 *   way that may be comprehensible but is not the correct
 *   graphical form. For instance, a Hangul character represented
 *   as a a sequence of Jamos, or a Latin transliteration of a
 *   Cyrillic word.
 * @NS_PANGO_COVERAGE_APPROXIMATE: The character is represented as
 *   basically the correct graphical form, but with a stylistic
 *   variant inappropriate for the current script.
 * @NS_PANGO_COVERAGE_EXACT: The character is represented as the
 *   correct graphical form.
 *
 * `NsPangoCoverageLevel` is used to indicate how well a font can
 * represent a particular Unicode character for a particular script.
 *
 * Since 1.44, only %NS_PANGO_COVERAGE_NONE and %NS_PANGO_COVERAGE_EXACT
 * will be returned.
 */
typedef enum {
  NS_PANGO_COVERAGE_NONE,
  NS_PANGO_COVERAGE_FALLBACK,
  NS_PANGO_COVERAGE_APPROXIMATE,
  NS_PANGO_COVERAGE_EXACT
} NsPangoCoverageLevel;

NS_PANGO_AVAILABLE_IN_ALL
GType ns_pango_coverage_get_type (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_ALL
NsPangoCoverage *    ns_pango_coverage_new     (void);
NS_PANGO_DEPRECATED_IN_1_52_FOR(g_object_ref)
NsPangoCoverage *    ns_pango_coverage_ref     (NsPangoCoverage      *coverage);
NS_PANGO_DEPRECATED_IN_1_52_FOR(g_object_unref)
void               ns_pango_coverage_unref   (NsPangoCoverage      *coverage);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoCoverage *    ns_pango_coverage_copy    (NsPangoCoverage      *coverage);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoCoverageLevel ns_pango_coverage_get     (NsPangoCoverage      *coverage,
					   int                 index_);
NS_PANGO_AVAILABLE_IN_ALL
void               ns_pango_coverage_set     (NsPangoCoverage      *coverage,
					   int                 index_,
					   NsPangoCoverageLevel  level);
NS_PANGO_DEPRECATED_IN_1_44
void               ns_pango_coverage_max     (NsPangoCoverage      *coverage,
					   NsPangoCoverage      *other);

NS_PANGO_DEPRECATED_IN_1_44
void           ns_pango_coverage_to_bytes   (NsPangoCoverage  *coverage,
					  guchar        **bytes,
					  int            *n_bytes);
NS_PANGO_DEPRECATED_IN_1_44
NsPangoCoverage *ns_pango_coverage_from_bytes (guchar         *bytes,
					  int             n_bytes);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(NsPangoCoverage, g_object_unref)

G_END_DECLS

#endif /* __NS_PANGO_COVERAGE_H__ */
