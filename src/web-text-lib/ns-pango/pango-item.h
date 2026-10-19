/* Pango
 * pango-item.h: Structure for storing run information
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __NS_PANGO_ITEM_H__
#define __NS_PANGO_ITEM_H__

#include <ns-pango/pango-types.h>
#include <ns-pango/pango-attributes.h>

G_BEGIN_DECLS

typedef struct _PangoAnalysis NsPangoAnalysis;
typedef struct _PangoItem NsPangoItem;

/**
 * NS_PANGO_ANALYSIS_FLAG_CENTERED_BASELINE:
 *
 * Whether the segment should be shifted to center around the baseline.
 *
 * This is mainly used in vertical writing directions.
 *
 * Since: 1.16
 */
#define NS_PANGO_ANALYSIS_FLAG_CENTERED_BASELINE (1 << 0)

/**
 * NS_PANGO_ANALYSIS_FLAG_IS_ELLIPSIS:
 *
 * Whether this run holds ellipsized text.
 *
 * Since: 1.36.7
 */
#define NS_PANGO_ANALYSIS_FLAG_IS_ELLIPSIS (1 << 1)

/**
 * NS_PANGO_ANALYSIS_FLAG_NEED_HYPHEN:
 *
 * Whether to add a hyphen at the end of the run during shaping.
 *
 * Since: 1.44
 */
#define NS_PANGO_ANALYSIS_FLAG_NEED_HYPHEN (1 << 2)

/**
 * NsPangoAnalysis:
 * @shape_engine: unused, reserved
 * @lang_engine: unused, reserved
 * @font: the font for this segment.
 * @level: the bidirectional level for this segment.
 * @gravity: the glyph orientation for this segment (A `NsPangoGravity`).
 * @flags: boolean flags for this segment (Since: 1.16).
 * @script: the detected script for this segment (A `NsPangoScript`) (Since: 1.18).
 * @language: the detected language for this segment.
 * @extra_attrs: extra attributes for this segment.
 *
 * The `NsPangoAnalysis` structure stores information about
 * the properties of a segment of text.
 */
struct _PangoAnalysis
{
#ifndef __GI_SCANNER__
  NsPangoEngineShape *shape_engine;
  NsPangoEngineLang  *lang_engine;
#else
  gpointer shape_engine;
  gpointer lang_engine;
#endif
  NsPangoFont *font;

  guint8 level;
  guint8 gravity;
  guint8 flags;

  guint8 script;
  NsPangoLanguage *language;

  GSList *extra_attrs;
};

/**
 * NsPangoItem:
 * @offset: byte offset of the start of this item in text.
 * @length: length of this item in bytes.
 * @num_chars: number of Unicode characters in the item.
 * @analysis: analysis results for the item.
 *
 * The `NsPangoItem` structure stores information about a segment of text.
 *
 * You typically obtain `NsPangoItems` by itemizing a piece of text
 * with [func@itemize].
 */
struct _PangoItem
{
  int offset;
  int length;
  int num_chars;
  NsPangoAnalysis analysis;
};

#define NS_TYPE_PANGO_ITEM (ns_pango_item_get_type ())

NS_PANGO_AVAILABLE_IN_ALL
GType                   ns_pango_item_get_type          (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_ALL
NsPangoItem *             ns_pango_item_new               (void);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoItem *             ns_pango_item_copy              (NsPangoItem         *item);
NS_PANGO_AVAILABLE_IN_ALL
void                    ns_pango_item_free              (NsPangoItem         *item);


NS_PANGO_AVAILABLE_IN_1_54
int                     ns_pango_item_get_char_offset   (NsPangoItem         *item);

NS_PANGO_AVAILABLE_IN_ALL
NsPangoItem *             ns_pango_item_split             (NsPangoItem         *orig,
                                                      int                split_index,
                                                      int                split_offset);

NS_PANGO_AVAILABLE_IN_1_44
void                    ns_pango_item_apply_attrs       (NsPangoItem         *item,
                                                      NsPangoAttrIterator *iter);

NS_PANGO_AVAILABLE_IN_ALL
GList *                 ns_pango_reorder_items          (GList             *items);

/* Itemization */

NS_PANGO_AVAILABLE_IN_ALL
GList *                 ns_pango_itemize                (NsPangoContext      *context,
                                                      const char        *text,
                                                      int                start_index,
                                                      int                length,
                                                      NsPangoAttrList     *attrs,
                                                      NsPangoAttrIterator *cached_iter);

NS_PANGO_AVAILABLE_IN_1_4
GList *                 ns_pango_itemize_with_base_dir  (NsPangoContext      *context,
                                                      NsPangoDirection     base_dir,
                                                      const char        *text,
                                                      int                start_index,
                                                      int                length,
                                                      NsPangoAttrList     *attrs,
                                                      NsPangoAttrIterator *cached_iter);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (NsPangoItem, ns_pango_item_free)

G_END_DECLS

#endif /* __NS_PANGO_ITEM_H__ */
