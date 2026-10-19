/* Pango
 * pango-layout.h: High-level layout driver
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

#ifndef __NS_PANGO_LAYOUT_H__
#define __NS_PANGO_LAYOUT_H__

#include <ns-pango/pango-attributes.h>
#include <ns-pango/pango-context.h>
#include <ns-pango/pango-glyph-item.h>
#include <ns-pango/pango-tabs.h>

G_BEGIN_DECLS

typedef struct _PangoLayout      NsPangoLayout;
typedef struct _PangoLayoutClass NsPangoLayoutClass;
typedef struct _PangoLayoutLine  NsPangoLayoutLine;

/**
 * NsPangoLayoutRun:
 *
 * A `NsPangoLayoutRun` represents a single run within a `NsPangoLayoutLine`.
 *
 * It is simply an alternate name for [struct@Pango.GlyphItem].
 * See the [struct@Pango.GlyphItem] docs for details on the fields.
 */
typedef NsPangoGlyphItem NsPangoLayoutRun;

/**
 * NsPangoAlignment:
 * @NS_PANGO_ALIGN_LEFT: Put all available space on the right
 * @NS_PANGO_ALIGN_CENTER: Center the line within the available space
 * @NS_PANGO_ALIGN_RIGHT: Put all available space on the left
 *
 * `NsPangoAlignment` describes how to align the lines of a `NsPangoLayout`
 * within the available space.
 *
 * If the `NsPangoLayout` is set to justify using [method@Pango.Layout.set_justify],
 * this only affects partial lines.
 *
 * See [method@Pango.Layout.set_auto_dir] for how text direction affects
 * the interpretation of `NsPangoAlignment` values.
 */
typedef enum {
  NS_PANGO_ALIGN_LEFT,
  NS_PANGO_ALIGN_CENTER,
  NS_PANGO_ALIGN_RIGHT
} NsPangoAlignment;

/**
 * NsPangoWrapMode:
 * @NS_PANGO_WRAP_WORD: wrap lines at word boundaries.
 * @NS_PANGO_WRAP_CHAR: wrap lines at character boundaries.
 * @NS_PANGO_WRAP_WORD_CHAR: wrap lines at word boundaries, but fall back to
 *   character boundaries if there is not enough space for a full word.
 *
 * `NsPangoWrapMode` describes how to wrap the lines of a `NsPangoLayout`
 * to the desired width.
 *
 * For @NS_PANGO_WRAP_WORD, Pango uses break opportunities that are determined
 * by the Unicode line breaking algorithm. For @NS_PANGO_WRAP_CHAR, Pango allows
 * breaking at grapheme boundaries that are determined by the Unicode text
 * segmentation algorithm.
 */

/**
 * NS_PANGO_WRAP_NONE:
 *
 * do not wrap.
 *
 * Since: 1.56
 */
typedef enum {
  NS_PANGO_WRAP_WORD,
  NS_PANGO_WRAP_CHAR,
  NS_PANGO_WRAP_WORD_CHAR,
  NS_PANGO_WRAP_NONE NS_PANGO_AVAILABLE_ENUMERATOR_IN_1_56
} NsPangoWrapMode;

/**
 * NsPangoEllipsizeMode:
 * @NS_PANGO_ELLIPSIZE_NONE: No ellipsization
 * @NS_PANGO_ELLIPSIZE_START: Omit characters at the start of the text
 * @NS_PANGO_ELLIPSIZE_MIDDLE: Omit characters in the middle of the text
 * @NS_PANGO_ELLIPSIZE_END: Omit characters at the end of the text
 *
 * `NsPangoEllipsizeMode` describes what sort of ellipsization
 * should be applied to text.
 *
 * In the ellipsization process characters are removed from the
 * text in order to make it fit to a given width and replaced
 * with an ellipsis.
 */
typedef enum {
  NS_PANGO_ELLIPSIZE_NONE,
  NS_PANGO_ELLIPSIZE_START,
  NS_PANGO_ELLIPSIZE_MIDDLE,
  NS_PANGO_ELLIPSIZE_END
} NsPangoEllipsizeMode;

/**
 * NsPangoLayoutLine:
 * @layout: (nullable): the layout this line belongs to, might be %NULL
 * @start_index: start of line as byte index into layout->text
 * @length: length of line in bytes
 * @runs: (nullable) (element-type Pango.LayoutRun): list of runs in the
 *   line, from left to right
 * @is_paragraph_start: #TRUE if this is the first line of the paragraph
 * @resolved_dir: #Resolved NsPangoDirection of line
 *
 * A `NsPangoLayoutLine` represents one of the lines resulting from laying
 * out a paragraph via `NsPangoLayout`.
 *
 * `NsPangoLayoutLine` structures are obtained by calling
 * [method@Pango.Layout.get_line] and are only valid until the text,
 * attributes, or settings of the parent `NsPangoLayout` are modified.
 */
struct _PangoLayoutLine
{
  NsPangoLayout *layout;
  gint         start_index;     /* start of line as byte index into layout->text */
  gint         length;		/* length of line in bytes */
  GSList      *runs;
  guint        is_paragraph_start : 1;  /* TRUE if this is the first line of the paragraph */
  guint        resolved_dir : 3;  /* Resolved NsPangoDirection of line */
};

#define NS_TYPE_PANGO_LAYOUT              (ns_pango_layout_get_type ())
#define NS_PANGO_LAYOUT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_TYPE_PANGO_LAYOUT, NsPangoLayout))
#define NS_PANGO_LAYOUT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), NS_TYPE_PANGO_LAYOUT, NsPangoLayoutClass))
#define NS_PANGO_IS_LAYOUT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_TYPE_PANGO_LAYOUT))
#define NS_PANGO_IS_LAYOUT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), NS_TYPE_PANGO_LAYOUT))
#define NS_PANGO_LAYOUT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), NS_TYPE_PANGO_LAYOUT, NsPangoLayoutClass))

/* The NsPangoLayout and NsPangoLayoutClass structs are private; if you
 * need to create a subclass of these, file a bug.
 */

NS_PANGO_AVAILABLE_IN_ALL
GType        ns_pango_layout_get_type       (void) G_GNUC_CONST;
NS_PANGO_AVAILABLE_IN_ALL
NsPangoLayout *ns_pango_layout_new            (NsPangoContext   *context);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoLayout *ns_pango_layout_copy           (NsPangoLayout    *src);

NS_PANGO_AVAILABLE_IN_ALL
NsPangoContext  *ns_pango_layout_get_context    (NsPangoLayout    *layout);

NS_PANGO_AVAILABLE_IN_ALL
void           ns_pango_layout_set_attributes (NsPangoLayout    *layout,
					    NsPangoAttrList  *attrs);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoAttrList *ns_pango_layout_get_attributes (NsPangoLayout    *layout);

NS_PANGO_AVAILABLE_IN_ALL
void           ns_pango_layout_set_text       (NsPangoLayout    *layout,
					    const char     *text,
					    int             length);
NS_PANGO_AVAILABLE_IN_ALL
const char    *ns_pango_layout_get_text       (NsPangoLayout    *layout);

NS_PANGO_AVAILABLE_IN_1_30
gint           ns_pango_layout_get_character_count (NsPangoLayout *layout);

NS_PANGO_AVAILABLE_IN_ALL
void           ns_pango_layout_set_markup     (NsPangoLayout    *layout,
					    const char     *markup,
					    int             length);

NS_PANGO_AVAILABLE_IN_ALL
void           ns_pango_layout_set_markup_with_accel (NsPangoLayout    *layout,
						   const char     *markup,
						   int             length,
						   gunichar        accel_marker,
						   gunichar       *accel_char);

NS_PANGO_AVAILABLE_IN_ALL
void           ns_pango_layout_set_font_description (NsPangoLayout                *layout,
						  const NsPangoFontDescription *desc);

NS_PANGO_AVAILABLE_IN_1_8
const NsPangoFontDescription *ns_pango_layout_get_font_description (NsPangoLayout *layout);

NS_PANGO_AVAILABLE_IN_ALL
void           ns_pango_layout_set_width            (NsPangoLayout                *layout,
						  int                         width);
NS_PANGO_AVAILABLE_IN_ALL
int            ns_pango_layout_get_width            (NsPangoLayout                *layout);
NS_PANGO_AVAILABLE_IN_1_20
void           ns_pango_layout_set_height           (NsPangoLayout                *layout,
						  int                         height);
NS_PANGO_AVAILABLE_IN_1_20
int            ns_pango_layout_get_height           (NsPangoLayout                *layout);
NS_PANGO_AVAILABLE_IN_ALL
void           ns_pango_layout_set_wrap             (NsPangoLayout                *layout,
						  NsPangoWrapMode               wrap);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoWrapMode  ns_pango_layout_get_wrap             (NsPangoLayout                *layout);
NS_PANGO_AVAILABLE_IN_1_16
gboolean       ns_pango_layout_is_wrapped           (NsPangoLayout                *layout);
NS_PANGO_AVAILABLE_IN_ALL
void           ns_pango_layout_set_indent           (NsPangoLayout                *layout,
						  int                         indent);
NS_PANGO_AVAILABLE_IN_ALL
int            ns_pango_layout_get_indent           (NsPangoLayout                *layout);
NS_PANGO_AVAILABLE_IN_ALL
void           ns_pango_layout_set_spacing          (NsPangoLayout                *layout,
						  int                         spacing);
NS_PANGO_AVAILABLE_IN_ALL
int            ns_pango_layout_get_spacing          (NsPangoLayout                *layout);
NS_PANGO_AVAILABLE_IN_1_44
void           ns_pango_layout_set_line_spacing     (NsPangoLayout                *layout,
                                                  float                       factor);
NS_PANGO_AVAILABLE_IN_1_44
float          ns_pango_layout_get_line_spacing     (NsPangoLayout                *layout);
NS_PANGO_AVAILABLE_IN_ALL
void           ns_pango_layout_set_justify          (NsPangoLayout                *layout,
                                                  gboolean                    justify);
NS_PANGO_AVAILABLE_IN_ALL
gboolean       ns_pango_layout_get_justify          (NsPangoLayout                *layout);
NS_PANGO_AVAILABLE_IN_1_50
void           ns_pango_layout_set_justify_last_line (NsPangoLayout                *layout,
                                                   gboolean                    justify);
NS_PANGO_AVAILABLE_IN_1_50
gboolean       ns_pango_layout_get_justify_last_line (NsPangoLayout                *layout);
NS_PANGO_AVAILABLE_IN_1_4
void           ns_pango_layout_set_auto_dir         (NsPangoLayout                *layout,
						  gboolean                    auto_dir);
NS_PANGO_AVAILABLE_IN_1_4
gboolean       ns_pango_layout_get_auto_dir         (NsPangoLayout                *layout);
NS_PANGO_AVAILABLE_IN_ALL
void           ns_pango_layout_set_alignment        (NsPangoLayout                *layout,
						  NsPangoAlignment              alignment);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoAlignment ns_pango_layout_get_alignment        (NsPangoLayout                *layout);

NS_PANGO_AVAILABLE_IN_ALL
void           ns_pango_layout_set_tabs             (NsPangoLayout                *layout,
						  NsPangoTabArray              *tabs);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoTabArray* ns_pango_layout_get_tabs             (NsPangoLayout                *layout);

NS_PANGO_AVAILABLE_IN_ALL
void           ns_pango_layout_set_single_paragraph_mode (NsPangoLayout                *layout,
						       gboolean                    setting);
NS_PANGO_AVAILABLE_IN_ALL
gboolean       ns_pango_layout_get_single_paragraph_mode (NsPangoLayout                *layout);

NS_PANGO_AVAILABLE_IN_1_6
void               ns_pango_layout_set_ellipsize (NsPangoLayout        *layout,
					       NsPangoEllipsizeMode  ellipsize);
NS_PANGO_AVAILABLE_IN_1_6
NsPangoEllipsizeMode ns_pango_layout_get_ellipsize (NsPangoLayout        *layout);
NS_PANGO_AVAILABLE_IN_1_16
gboolean           ns_pango_layout_is_ellipsized (NsPangoLayout        *layout);

NS_PANGO_AVAILABLE_IN_1_16
int      ns_pango_layout_get_unknown_glyphs_count (NsPangoLayout    *layout);

NS_PANGO_AVAILABLE_IN_1_46
NsPangoDirection ns_pango_layout_get_direction (NsPangoLayout *layout,
                                           int          index);

NS_PANGO_AVAILABLE_IN_ALL
void     ns_pango_layout_context_changed (NsPangoLayout    *layout);
NS_PANGO_AVAILABLE_IN_1_32
guint    ns_pango_layout_get_serial      (NsPangoLayout    *layout);

NS_PANGO_AVAILABLE_IN_ALL
void     ns_pango_layout_get_log_attrs (NsPangoLayout    *layout,
				     NsPangoLogAttr  **attrs,
				     gint           *n_attrs);

NS_PANGO_AVAILABLE_IN_1_30
const NsPangoLogAttr *ns_pango_layout_get_log_attrs_readonly (NsPangoLayout *layout,
							 gint        *n_attrs);

NS_PANGO_AVAILABLE_IN_ALL
void     ns_pango_layout_index_to_pos         (NsPangoLayout    *layout,
					    int             index_,
					    NsPangoRectangle *pos);
NS_PANGO_AVAILABLE_IN_ALL
void     ns_pango_layout_index_to_line_x      (NsPangoLayout    *layout,
					    int             index_,
					    gboolean        trailing,
					    int            *line,
					    int            *x_pos);
NS_PANGO_AVAILABLE_IN_ALL
void     ns_pango_layout_get_cursor_pos       (NsPangoLayout    *layout,
					    int             index_,
					    NsPangoRectangle *strong_pos,
					    NsPangoRectangle *weak_pos);

NS_PANGO_AVAILABLE_IN_1_50
void     ns_pango_layout_get_caret_pos        (NsPangoLayout    *layout,
                                            int             index_,
                                            NsPangoRectangle *strong_pos,
                                            NsPangoRectangle *weak_pos);

NS_PANGO_AVAILABLE_IN_ALL
void     ns_pango_layout_move_cursor_visually (NsPangoLayout    *layout,
					    gboolean        strong,
					    int             old_index,
					    int             old_trailing,
					    int             direction,
					    int            *new_index,
					    int            *new_trailing);
NS_PANGO_AVAILABLE_IN_ALL
gboolean ns_pango_layout_xy_to_index          (NsPangoLayout    *layout,
					    int             x,
					    int             y,
					    int            *index_,
					    int            *trailing);
NS_PANGO_AVAILABLE_IN_ALL
void     ns_pango_layout_get_extents          (NsPangoLayout    *layout,
					    NsPangoRectangle *ink_rect,
					    NsPangoRectangle *logical_rect);
NS_PANGO_AVAILABLE_IN_ALL
void     ns_pango_layout_get_pixel_extents    (NsPangoLayout    *layout,
					    NsPangoRectangle *ink_rect,
					    NsPangoRectangle *logical_rect);
NS_PANGO_AVAILABLE_IN_ALL
void     ns_pango_layout_get_size             (NsPangoLayout    *layout,
					    int            *width,
					    int            *height);
NS_PANGO_AVAILABLE_IN_ALL
void     ns_pango_layout_get_pixel_size       (NsPangoLayout    *layout,
					    int            *width,
					    int            *height);
NS_PANGO_AVAILABLE_IN_1_22
int      ns_pango_layout_get_baseline         (NsPangoLayout    *layout);

NS_PANGO_AVAILABLE_IN_ALL
int              ns_pango_layout_get_line_count       (NsPangoLayout    *layout);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoLayoutLine *ns_pango_layout_get_line             (NsPangoLayout    *layout,
						    int             line);
NS_PANGO_AVAILABLE_IN_1_16
NsPangoLayoutLine *ns_pango_layout_get_line_readonly    (NsPangoLayout    *layout,
						    int             line);
NS_PANGO_AVAILABLE_IN_ALL
GSList *         ns_pango_layout_get_lines            (NsPangoLayout    *layout);
NS_PANGO_AVAILABLE_IN_1_16
GSList *         ns_pango_layout_get_lines_readonly   (NsPangoLayout    *layout);

#define NS_TYPE_PANGO_LAYOUT_LINE (ns_pango_layout_line_get_type ())

NS_PANGO_AVAILABLE_IN_ALL
GType    ns_pango_layout_line_get_type     (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_1_10
NsPangoLayoutLine *ns_pango_layout_line_ref   (NsPangoLayoutLine *line);
NS_PANGO_AVAILABLE_IN_ALL
void             ns_pango_layout_line_unref (NsPangoLayoutLine *line);

NS_PANGO_AVAILABLE_IN_1_50
int      ns_pango_layout_line_get_start_index (NsPangoLayoutLine *line);
NS_PANGO_AVAILABLE_IN_1_50
int      ns_pango_layout_line_get_length      (NsPangoLayoutLine *line);
NS_PANGO_AVAILABLE_IN_1_50
gboolean ns_pango_layout_line_is_paragraph_start (NsPangoLayoutLine *line);
NS_PANGO_AVAILABLE_IN_1_50
NsPangoDirection ns_pango_layout_line_get_resolved_direction (NsPangoLayoutLine *line);

NS_PANGO_AVAILABLE_IN_ALL
gboolean ns_pango_layout_line_x_to_index   (NsPangoLayoutLine  *line,
					 int               x_pos,
					 int              *index_,
					 int              *trailing);
NS_PANGO_AVAILABLE_IN_ALL
void     ns_pango_layout_line_index_to_x   (NsPangoLayoutLine  *line,
					 int               index_,
					 gboolean          trailing,
					 int              *x_pos);
NS_PANGO_AVAILABLE_IN_ALL
void     ns_pango_layout_line_get_x_ranges (NsPangoLayoutLine  *line,
					 int               start_index,
					 int               end_index,
					 int             **ranges,
					 int              *n_ranges);
NS_PANGO_AVAILABLE_IN_ALL
void     ns_pango_layout_line_get_extents  (NsPangoLayoutLine  *line,
					 NsPangoRectangle   *ink_rect,
					 NsPangoRectangle   *logical_rect);
NS_PANGO_AVAILABLE_IN_1_44
void     ns_pango_layout_line_get_height   (NsPangoLayoutLine  *line,
					 int              *height);

NS_PANGO_AVAILABLE_IN_ALL
void     ns_pango_layout_line_get_pixel_extents (NsPangoLayoutLine *layout_line,
					      NsPangoRectangle  *ink_rect,
					      NsPangoRectangle  *logical_rect);

typedef struct _PangoLayoutIter NsPangoLayoutIter;

#define NS_TYPE_PANGO_LAYOUT_ITER         (ns_pango_layout_iter_get_type ())

NS_PANGO_AVAILABLE_IN_ALL
GType            ns_pango_layout_iter_get_type (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_ALL
NsPangoLayoutIter *ns_pango_layout_get_iter  (NsPangoLayout     *layout);
NS_PANGO_AVAILABLE_IN_1_20
NsPangoLayoutIter *ns_pango_layout_iter_copy (NsPangoLayoutIter *iter);
NS_PANGO_AVAILABLE_IN_ALL
void             ns_pango_layout_iter_free (NsPangoLayoutIter *iter);

NS_PANGO_AVAILABLE_IN_ALL
int              ns_pango_layout_iter_get_index  (NsPangoLayoutIter *iter);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoLayoutRun  *ns_pango_layout_iter_get_run    (NsPangoLayoutIter *iter);
NS_PANGO_AVAILABLE_IN_1_16
NsPangoLayoutRun  *ns_pango_layout_iter_get_run_readonly   (NsPangoLayoutIter *iter);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoLayoutLine *ns_pango_layout_iter_get_line   (NsPangoLayoutIter *iter);
NS_PANGO_AVAILABLE_IN_1_16
NsPangoLayoutLine *ns_pango_layout_iter_get_line_readonly  (NsPangoLayoutIter *iter);
NS_PANGO_AVAILABLE_IN_ALL
gboolean         ns_pango_layout_iter_at_last_line (NsPangoLayoutIter *iter);
NS_PANGO_AVAILABLE_IN_1_20
NsPangoLayout     *ns_pango_layout_iter_get_layout (NsPangoLayoutIter *iter);

NS_PANGO_AVAILABLE_IN_ALL
gboolean ns_pango_layout_iter_next_char    (NsPangoLayoutIter *iter);
NS_PANGO_AVAILABLE_IN_ALL
gboolean ns_pango_layout_iter_next_cluster (NsPangoLayoutIter *iter);
NS_PANGO_AVAILABLE_IN_ALL
gboolean ns_pango_layout_iter_next_run     (NsPangoLayoutIter *iter);
NS_PANGO_AVAILABLE_IN_ALL
gboolean ns_pango_layout_iter_next_line    (NsPangoLayoutIter *iter);

NS_PANGO_AVAILABLE_IN_ALL
void ns_pango_layout_iter_get_char_extents    (NsPangoLayoutIter *iter,
					    NsPangoRectangle  *logical_rect);
NS_PANGO_AVAILABLE_IN_ALL
void ns_pango_layout_iter_get_cluster_extents (NsPangoLayoutIter *iter,
					    NsPangoRectangle  *ink_rect,
					    NsPangoRectangle  *logical_rect);
NS_PANGO_AVAILABLE_IN_ALL
void ns_pango_layout_iter_get_run_extents     (NsPangoLayoutIter *iter,
					    NsPangoRectangle  *ink_rect,
					    NsPangoRectangle  *logical_rect);
NS_PANGO_AVAILABLE_IN_ALL
void ns_pango_layout_iter_get_line_extents    (NsPangoLayoutIter *iter,
					    NsPangoRectangle  *ink_rect,
					    NsPangoRectangle  *logical_rect);
/* All the yranges meet, unlike the logical_rect's (i.e. the yranges
 * assign between-line spacing to the nearest line)
 */
NS_PANGO_AVAILABLE_IN_ALL
void ns_pango_layout_iter_get_line_yrange     (NsPangoLayoutIter *iter,
					    int             *y0_,
					    int             *y1_);
NS_PANGO_AVAILABLE_IN_ALL
void ns_pango_layout_iter_get_layout_extents  (NsPangoLayoutIter *iter,
					    NsPangoRectangle  *ink_rect,
					    NsPangoRectangle  *logical_rect);
NS_PANGO_AVAILABLE_IN_ALL
int  ns_pango_layout_iter_get_baseline        (NsPangoLayoutIter *iter);
NS_PANGO_AVAILABLE_IN_1_50
int  ns_pango_layout_iter_get_run_baseline    (NsPangoLayoutIter *iter);


G_DEFINE_AUTOPTR_CLEANUP_FUNC(NsPangoLayout, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(NsPangoLayoutIter, ns_pango_layout_iter_free)

G_DEFINE_AUTOPTR_CLEANUP_FUNC (NsPangoLayoutLine, ns_pango_layout_line_unref)

G_END_DECLS

#endif /* __NS_PANGO_LAYOUT_H__ */

