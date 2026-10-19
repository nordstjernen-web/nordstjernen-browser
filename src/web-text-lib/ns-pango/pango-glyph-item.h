/* Pango
 * pango-glyph-item.h: Pair of NsPangoItem and a glyph string
 *
 * Copyright (C) 2002 Red Hat Software
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

#ifndef __NS_PANGO_GLYPH_ITEM_H__
#define __NS_PANGO_GLYPH_ITEM_H__

#include <ns-pango/pango-attributes.h>
#include <ns-pango/pango-break.h>
#include <ns-pango/pango-item.h>
#include <ns-pango/pango-glyph.h>

G_BEGIN_DECLS

/**
 * NsPangoGlyphItem:
 * @item: corresponding `NsPangoItem`
 * @glyphs: corresponding `NsPangoGlyphString`
 * @y_offset: shift of the baseline, relative to the baseline
 *   of the containing line. Positive values shift upwards
 * @start_x_offset: horizontal displacement to apply before the
 *   glyph item. Positive values shift right
 * @end_x_offset: horizontal displacement to apply after th
 *   glyph item. Positive values shift right
 *
 * A `NsPangoGlyphItem` is a pair of a `NsPangoItem` and the glyphs
 * resulting from shaping the items text.
 *
 * As an example of the usage of `NsPangoGlyphItem`, the results
 * of shaping text with `NsPangoLayout` is a list of `NsPangoLayoutLine`,
 * each of which contains a list of `NsPangoGlyphItem`.
 */
typedef struct _PangoGlyphItem NsPangoGlyphItem;

struct _PangoGlyphItem
{
  NsPangoItem *item;
  NsPangoGlyphString *glyphs;
  int y_offset;
  int start_x_offset;
  int end_x_offset;
};

#define NS_TYPE_PANGO_GLYPH_ITEM (ns_pango_glyph_item_get_type ())

NS_PANGO_AVAILABLE_IN_ALL
GType ns_pango_glyph_item_get_type (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_1_2
NsPangoGlyphItem *ns_pango_glyph_item_split        (NsPangoGlyphItem *orig,
					       const char     *text,
					       int             split_index);
NS_PANGO_AVAILABLE_IN_1_20
NsPangoGlyphItem *ns_pango_glyph_item_copy         (NsPangoGlyphItem *orig);
NS_PANGO_AVAILABLE_IN_1_6
void            ns_pango_glyph_item_free         (NsPangoGlyphItem *glyph_item);
NS_PANGO_AVAILABLE_IN_1_2
GSList *        ns_pango_glyph_item_apply_attrs  (NsPangoGlyphItem *glyph_item,
					       const char     *text,
					       NsPangoAttrList  *list);
NS_PANGO_AVAILABLE_IN_1_6
void            ns_pango_glyph_item_letter_space (NsPangoGlyphItem *glyph_item,
					       const char     *text,
					       NsPangoLogAttr   *log_attrs,
					       int             letter_spacing);
NS_PANGO_AVAILABLE_IN_1_58
void            ns_pango_glyph_item_word_space   (NsPangoGlyphItem *glyph_item,
					       const char     *text,
					       int             word_spacing);
NS_PANGO_AVAILABLE_IN_1_26
void 	  ns_pango_glyph_item_get_logical_widths (NsPangoGlyphItem *glyph_item,
					       const char     *text,
					       int            *logical_widths);


/**
 * NsPangoGlyphItemIter:
 *
 * A `NsPangoGlyphItemIter` is an iterator over the clusters in a
 * `NsPangoGlyphItem`.
 *
 * The *forward direction* of the iterator is the logical direction of text.
 * That is, with increasing @start_index and @start_char values. If @glyph_item
 * is right-to-left (that is, if `glyph_item->item->analysis.level` is odd),
 * then @start_glyph decreases as the iterator moves forward.  Moreover,
 * in right-to-left cases, @start_glyph is greater than @end_glyph.
 *
 * An iterator should be initialized using either
 * ns_pango_glyph_item_iter_init_start() or
 * ns_pango_glyph_item_iter_init_end(), for forward and backward iteration
 * respectively, and walked over using any desired mixture of
 * ns_pango_glyph_item_iter_next_cluster() and
 * ns_pango_glyph_item_iter_prev_cluster().
 *
 * A common idiom for doing a forward iteration over the clusters is:
 *
 * ```
 * NsPangoGlyphItemIter cluster_iter;
 * gboolean have_cluster;
 *
 * for (have_cluster = ns_pango_glyph_item_iter_init_start (&cluster_iter,
 *                                                       glyph_item, text);
 *      have_cluster;
 *      have_cluster = ns_pango_glyph_item_iter_next_cluster (&cluster_iter))
 * {
 *   ...
 * }
 * ```
 *
 * Note that @text is the start of the text for layout, which is then
 * indexed by `glyph_item->item->offset` to get to the text of @glyph_item.
 * The @start_index and @end_index values can directly index into @text. The
 * @start_glyph, @end_glyph, @start_char, and @end_char values however are
 * zero-based for the @glyph_item.  For each cluster, the item pointed at by
 * the start variables is included in the cluster while the one pointed at by
 * end variables is not.
 *
 * None of the members of a `NsPangoGlyphItemIter` should be modified manually.
 *
 * Since: 1.22
 */
typedef struct _PangoGlyphItemIter NsPangoGlyphItemIter;

struct _PangoGlyphItemIter
{
  NsPangoGlyphItem *glyph_item;
  const gchar *text;

  int start_glyph;
  int start_index;
  int start_char;

  int end_glyph;
  int end_index;
  int end_char;
};

#define NS_TYPE_PANGO_GLYPH_ITEM_ITER (ns_pango_glyph_item_iter_get_type ())

NS_PANGO_AVAILABLE_IN_1_22
GType               ns_pango_glyph_item_iter_get_type (void) G_GNUC_CONST;
NS_PANGO_AVAILABLE_IN_1_22
NsPangoGlyphItemIter *ns_pango_glyph_item_iter_copy (NsPangoGlyphItemIter *orig);
NS_PANGO_AVAILABLE_IN_1_22
void                ns_pango_glyph_item_iter_free (NsPangoGlyphItemIter *iter);

NS_PANGO_AVAILABLE_IN_1_22
gboolean ns_pango_glyph_item_iter_init_start   (NsPangoGlyphItemIter *iter,
					     NsPangoGlyphItem     *glyph_item,
					     const char         *text);
NS_PANGO_AVAILABLE_IN_1_22
gboolean ns_pango_glyph_item_iter_init_end     (NsPangoGlyphItemIter *iter,
					     NsPangoGlyphItem     *glyph_item,
					     const char         *text);
NS_PANGO_AVAILABLE_IN_1_22
gboolean ns_pango_glyph_item_iter_next_cluster (NsPangoGlyphItemIter *iter);
NS_PANGO_AVAILABLE_IN_1_22
gboolean ns_pango_glyph_item_iter_prev_cluster (NsPangoGlyphItemIter *iter);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (NsPangoGlyphItemIter, ns_pango_glyph_item_iter_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (NsPangoGlyphItem, ns_pango_glyph_item_free)

G_END_DECLS

#endif /* __NS_PANGO_GLYPH_ITEM_H__ */
