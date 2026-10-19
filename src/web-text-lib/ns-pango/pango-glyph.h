/* Pango
 * pango-glyph.h: Glyph storage
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

#ifndef __NS_PANGO_GLYPH_H__
#define __NS_PANGO_GLYPH_H__

#include <ns-pango/pango-types.h>
#include <ns-pango/pango-item.h>
#include <ns-pango/pango-break.h>

G_BEGIN_DECLS

typedef struct _PangoGlyphGeometry NsPangoGlyphGeometry;
typedef struct _PangoGlyphVisAttr NsPangoGlyphVisAttr;
typedef struct _PangoGlyphInfo NsPangoGlyphInfo;
typedef struct _PangoGlyphString NsPangoGlyphString;

/* 1024ths of a device unit */
/**
 * NsPangoGlyphUnit:
 *
 * The `NsPangoGlyphUnit` type is used to store dimensions within
 * Pango.
 *
 * Dimensions are stored in 1/NS_PANGO_SCALE of a device unit.
 * (A device unit might be a pixel for screen display, or
 * a point on a printer.) NS_PANGO_SCALE is currently 1024, and
 * may change in the future (unlikely though), but you should not
 * depend on its exact value.
 *
 * The NS_PANGO_PIXELS() macro can be used to convert from glyph units
 * into device units with correct rounding.
 */
typedef gint32 NsPangoGlyphUnit;

/* Positioning information about a glyph
 */
/**
 * NsPangoGlyphGeometry:
 * @width: the logical width to use for the the character.
 * @x_offset: horizontal offset from nominal character position.
 * @y_offset: vertical offset from nominal character position.
 *
 * The `NsPangoGlyphGeometry` structure contains width and positioning
 * information for a single glyph.
 *
 * Note that @width is not guaranteed to be the same as the glyph
 * extents. Kerning and other positioning applied during shaping will
 * affect both the @width and the @x_offset for the glyphs in the
 * glyph string that results from shaping.
 *
 * The information in this struct is intended for rendering the glyphs,
 * as follows:
 *
 * 1. Assume the current point is (x, y)
 * 2. Render the current glyph at (x + x_offset, y + y_offset),
 * 3. Advance the current point to (x + width, y)
 * 4. Render the next glyph
 */
struct _PangoGlyphGeometry
{
  NsPangoGlyphUnit width;
  NsPangoGlyphUnit x_offset;
  NsPangoGlyphUnit y_offset;
};

/* Visual attributes of a glyph
 */
/**
 * NsPangoGlyphVisAttr:
 * @is_cluster_start: set for the first logical glyph in each cluster.
 * @is_color: set if the the font will render this glyph with color. Since 1.50
 *
 * A `NsPangoGlyphVisAttr` structure communicates information between
 * the shaping and rendering phases.
 *
 * Currently, it contains cluster start and color information.
 * More attributes may be added in the future.
 *
 * Clusters are stored in visual order, within the cluster, glyphs
 * are always ordered in logical order, since visual order is meaningless;
 * that is, in Arabic text, accent glyphs follow the glyphs for the
 * base character.
 */
struct _PangoGlyphVisAttr
{
  guint is_cluster_start : 1;
  guint is_color         : 1;
};

/* A single glyph
 */
/**
 * NsPangoGlyphInfo:
 * @glyph: the glyph itself.
 * @geometry: the positional information about the glyph.
 * @attr: the visual attributes of the glyph.
 *
 * A `NsPangoGlyphInfo` structure represents a single glyph with
 * positioning information and visual attributes.
 */
struct _PangoGlyphInfo
{
  NsPangoGlyph    glyph;
  NsPangoGlyphGeometry geometry;
  NsPangoGlyphVisAttr  attr;
};

/**
 * NsPangoGlyphString:
 * @num_glyphs: number of glyphs in this glyph string
 * @glyphs: (array length=num_glyphs): array of glyph information
 * @log_clusters: logical cluster info, indexed by the byte index
 *   within the text corresponding to the glyph string
 *
 * A `NsPangoGlyphString` is used to store strings of glyphs with geometry
 * and visual attribute information.
 *
 * The storage for the glyph information is owned by the structure
 * which simplifies memory management.
 */
struct _PangoGlyphString {
  int num_glyphs;

  NsPangoGlyphInfo *glyphs;
  int *log_clusters;

  /*< private >*/
  int space;
};

#define NS_TYPE_PANGO_GLYPH_STRING (ns_pango_glyph_string_get_type ())

NS_PANGO_AVAILABLE_IN_ALL
GType                   ns_pango_glyph_string_get_type             (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_ALL
NsPangoGlyphString *      ns_pango_glyph_string_new                  (void);
NS_PANGO_AVAILABLE_IN_ALL
void                    ns_pango_glyph_string_set_size             (NsPangoGlyphString    *string,
                                                                 int                  new_len);

NS_PANGO_AVAILABLE_IN_ALL
NsPangoGlyphString *      ns_pango_glyph_string_copy                 (NsPangoGlyphString    *string);
NS_PANGO_AVAILABLE_IN_ALL
void                    ns_pango_glyph_string_free                 (NsPangoGlyphString    *string);

NS_PANGO_AVAILABLE_IN_ALL
void                    ns_pango_glyph_string_extents              (NsPangoGlyphString    *glyphs,
                                                                 NsPangoFont           *font,
                                                                 NsPangoRectangle      *ink_rect,
                                                                 NsPangoRectangle      *logical_rect);
NS_PANGO_AVAILABLE_IN_1_14
int                     ns_pango_glyph_string_get_width            (NsPangoGlyphString    *glyphs);

NS_PANGO_AVAILABLE_IN_ALL
void                    ns_pango_glyph_string_extents_range        (NsPangoGlyphString    *glyphs,
                                                                 int                  start,
                                                                 int                  end,
                                                                 NsPangoFont           *font,
                                                                 NsPangoRectangle      *ink_rect,
                                                                 NsPangoRectangle      *logical_rect);

NS_PANGO_AVAILABLE_IN_ALL
void                    ns_pango_glyph_string_get_logical_widths   (NsPangoGlyphString    *glyphs,
                                                                 const char          *text,
                                                                 int                  length,
                                                                 int                  embedding_level,
                                                                 int                 *logical_widths);

NS_PANGO_AVAILABLE_IN_ALL
void                    ns_pango_glyph_string_index_to_x           (NsPangoGlyphString    *glyphs,
                                                                 const char          *text,
                                                                 int                  length,
                                                                 NsPangoAnalysis       *analysis,
                                                                 int                  index_,
                                                                 gboolean             trailing,
                                                                 int                 *x_pos);
NS_PANGO_AVAILABLE_IN_ALL
void                    ns_pango_glyph_string_x_to_index           (NsPangoGlyphString    *glyphs,
                                                                 const char          *text,
                                                                 int                  length,
                                                                 NsPangoAnalysis       *analysis,
                                                                 int                  x_pos,
                                                                 int                 *index_,
                                                                 int                 *trailing);

NS_PANGO_AVAILABLE_IN_1_50
void                    ns_pango_glyph_string_index_to_x_full      (NsPangoGlyphString    *glyphs,
                                                                 const char          *text,
                                                                 int                  length,
                                                                 NsPangoAnalysis       *analysis,
                                                                 NsPangoLogAttr        *attrs,
                                                                 int                  index_,
                                                                 gboolean             trailing,
                                                                 int                 *x_pos);

/* Shaping */

/**
 * NsPangoShapeFlags:
 * @NS_PANGO_SHAPE_NONE: Default value
 * @NS_PANGO_SHAPE_ROUND_POSITIONS: Round glyph positions and widths to whole device units
 *   This option should be set if the target renderer can't do subpixel positioning of glyphs
 *
 * Flags influencing the shaping process.
 *
 * `NsPangoShapeFlags` can be passed to [func@Pango.shape_with_flags].
 *
 * Since: 1.44
 */
typedef enum {
  NS_PANGO_SHAPE_NONE            = 0,
  NS_PANGO_SHAPE_ROUND_POSITIONS = 1 << 0,
} NsPangoShapeFlags;

NS_PANGO_AVAILABLE_IN_ALL
void                    ns_pango_shape                             (const char          *text,
                                                                 int                  length,
                                                                 const NsPangoAnalysis *analysis,
                                                                 NsPangoGlyphString    *glyphs);

NS_PANGO_AVAILABLE_IN_1_32
void                    ns_pango_shape_full                        (const char          *item_text,
                                                                 int                  item_length,
                                                                 const char          *paragraph_text,
                                                                 int                  paragraph_length,
                                                                 const NsPangoAnalysis *analysis,
                                                                 NsPangoGlyphString    *glyphs);

NS_PANGO_AVAILABLE_IN_1_44
void                    ns_pango_shape_with_flags                  (const char          *item_text,
                                                                 int                  item_length,
                                                                 const char          *paragraph_text,
                                                                 int                  paragraph_length,
                                                                 const NsPangoAnalysis *analysis,
                                                                 NsPangoGlyphString    *glyphs,
                                                                 NsPangoShapeFlags      flags);


NS_PANGO_AVAILABLE_IN_1_50
void                    ns_pango_shape_item                        (NsPangoItem           *item,
                                                                 const char          *paragraph_text,
                                                                 int                  paragraph_length,
                                                                 NsPangoLogAttr        *log_attrs,
                                                                 NsPangoGlyphString    *glyphs,
                                                                 NsPangoShapeFlags      flags);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (NsPangoGlyphString, ns_pango_glyph_string_free)

G_END_DECLS

#endif /* __NS_PANGO_GLYPH_H__ */
