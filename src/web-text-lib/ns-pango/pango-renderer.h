/* Pango
 * pango-renderer.h: Base class for rendering
 *
 * Copyright (C) 2004, Red Hat, Inc.
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
#ifndef __PANGO_RENDERER_H_
#define __PANGO_RENDERER_H_

#include <ns-pango/pango-layout.h>

G_BEGIN_DECLS

#define NS_TYPE_PANGO_RENDERER            (ns_pango_renderer_get_type())
#define NS_PANGO_RENDERER(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_TYPE_PANGO_RENDERER, NsPangoRenderer))
#define NS_PANGO_IS_RENDERER(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_TYPE_PANGO_RENDERER))
#define NS_PANGO_RENDERER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NS_TYPE_PANGO_RENDERER, NsPangoRendererClass))
#define NS_PANGO_IS_RENDERER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NS_TYPE_PANGO_RENDERER))
#define NS_PANGO_RENDERER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NS_TYPE_PANGO_RENDERER, NsPangoRendererClass))

typedef struct _PangoRenderer        NsPangoRenderer;
typedef struct _PangoRendererClass   NsPangoRendererClass;
typedef struct _PangoRendererPrivate NsPangoRendererPrivate;

/**
 * NsPangoRenderPart:
 * @NS_PANGO_RENDER_PART_FOREGROUND: the text itself
 * @NS_PANGO_RENDER_PART_BACKGROUND: the area behind the text
 * @NS_PANGO_RENDER_PART_UNDERLINE: underlines
 * @NS_PANGO_RENDER_PART_STRIKETHROUGH: strikethrough lines
 * @NS_PANGO_RENDER_PART_OVERLINE: overlines
 *
 * `NsPangoRenderPart` defines different items to render for such
 * purposes as setting colors.
 *
 * Since: 1.8
 **/
/* When extending, note N_RENDER_PARTS #define in pango-renderer.c */
typedef enum
{
  NS_PANGO_RENDER_PART_FOREGROUND,
  NS_PANGO_RENDER_PART_BACKGROUND,
  NS_PANGO_RENDER_PART_UNDERLINE,
  NS_PANGO_RENDER_PART_STRIKETHROUGH,
  NS_PANGO_RENDER_PART_OVERLINE,
} NsPangoRenderPart;

/**
 * NsPangoRenderer:
 * @matrix: (nullable): the current transformation matrix for
 *   the Renderer; may be %NULL, which should be treated the
 *   same as the identity matrix.
 *
 * `NsPangoRenderer` is a base class for objects that can render text
 * provided as `NsPangoGlyphString` or `NsPangoLayout`.
 *
 * By subclassing `NsPangoRenderer` and overriding operations such as
 * @draw_glyphs and @draw_rectangle, renderers for particular font
 * backends and destinations can be created.
 *
 * Since: 1.8
 */
struct _PangoRenderer
{
  /*< private >*/
  GObject parent_instance;

  NsPangoUnderline underline;
  gboolean strikethrough;
  int active_count;

  /*< public >*/
  NsPangoMatrix *matrix;          /* May be NULL */

  /*< private >*/
  NsPangoRendererPrivate *priv;
};

/**
 * NsPangoRendererClass:
 * @draw_glyphs: draws a `NsPangoGlyphString`
 * @draw_rectangle: draws a rectangle
 * @draw_error_underline: draws a squiggly line that approximately
 * covers the given rectangle in the style of an underline used to
 * indicate a spelling error.
 * @draw_shape: draw content for a glyph shaped with `NsPangoAttrShape`
 *   @x, @y are the coordinates of the left edge of the baseline,
 *   in user coordinates.
 * @draw_trapezoid: draws a trapezoidal filled area
 * @draw_glyph: draws a single glyph
 * @part_changed: do renderer specific processing when rendering
 *  attributes change
 * @begin: Do renderer-specific initialization before drawing
 * @end: Do renderer-specific cleanup after drawing
 * @prepare_run: updates the renderer for a new run
 * @draw_glyph_item: draws a `NsPangoGlyphItem`
 *
 * Class structure for `NsPangoRenderer`.
 *
 * The following vfuncs take user space coordinates in Pango units
 * and have default implementations:
 * - draw_glyphs
 * - draw_rectangle
 * - draw_error_underline
 * - draw_shape
 * - draw_glyph_item
 *
 * The default draw_shape implementation draws nothing.
 *
 * The following vfuncs take device space coordinates as doubles
 * and must be implemented:
 * - draw_trapezoid
 * - draw_glyph
 *
 * The following vfuncs should look at the components value and
 * skip color or plain glyphs accordingly:
 * - draw_glyphs
 * - draw_glyph_item
 * - draw_glyph
 *
 * Since: 1.8
 */
struct _PangoRendererClass
{
  /*< private >*/
  GObjectClass parent_class;

  /* vtable - not signals */
  /*< public >*/

  void (*draw_glyphs)          (NsPangoRenderer    *renderer,
                                NsPangoFont        *font,
                                NsPangoGlyphString *glyphs,
                                int               x,
                                int               y);
  void (*draw_rectangle)       (NsPangoRenderer    *renderer,
                                NsPangoRenderPart   part,
                                int               x,
                                int               y,
                                int               width,
                                int               height);
  void (*draw_error_underline) (NsPangoRenderer    *renderer,
                                int               x,
                                int               y,
                                int               width,
                                int               height);
  void (*draw_shape)           (NsPangoRenderer    *renderer,
                                NsPangoAttrShape   *attr,
                                int               x,
                                int               y);

  void (*draw_trapezoid)       (NsPangoRenderer    *renderer,
                                NsPangoRenderPart   part,
                                double            y1_,
                                double            x11,
                                double            x21,
                                double            y2,
                                double            x12,
                                double            x22);
  void (*draw_glyph)           (NsPangoRenderer    *renderer,
                                NsPangoFont        *font,
                                NsPangoGlyph        glyph,
                                double            x,
                                double            y);

  void (*part_changed)         (NsPangoRenderer    *renderer,
                                NsPangoRenderPart   part);

  void (*begin)                (NsPangoRenderer    *renderer);
  void (*end)                  (NsPangoRenderer    *renderer);

  void (*prepare_run)          (NsPangoRenderer    *renderer,
                                NsPangoLayoutRun   *run);

  void (*draw_glyph_item)      (NsPangoRenderer    *renderer,
                                const char       *text,
                                NsPangoGlyphItem   *glyph_item,
                                int               x,
                                int               y);

  /*< private >*/

  /* Padding for future expansion */
  void (*_ns_pango_reserved2) (void);
  void (*_ns_pango_reserved3) (void);
  void (*_ns_pango_reserved4) (void);
};

NS_PANGO_AVAILABLE_IN_1_8
GType ns_pango_renderer_get_type            (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_1_8
void ns_pango_renderer_draw_layout          (NsPangoRenderer    *renderer,
                                          NsPangoLayout      *layout,
                                          int               x,
                                          int               y);
NS_PANGO_AVAILABLE_IN_1_8
void ns_pango_renderer_draw_layout_line     (NsPangoRenderer    *renderer,
                                          NsPangoLayoutLine  *line,
                                          int               x,
                                          int               y);
NS_PANGO_AVAILABLE_IN_1_8
void ns_pango_renderer_draw_glyphs          (NsPangoRenderer    *renderer,
                                          NsPangoFont        *font,
                                          NsPangoGlyphString *glyphs,
                                          int               x,
                                          int               y);
NS_PANGO_AVAILABLE_IN_1_22
void ns_pango_renderer_draw_glyph_item      (NsPangoRenderer    *renderer,
                                          const char       *text,
                                          NsPangoGlyphItem   *glyph_item,
                                          int               x,
                                          int               y);
NS_PANGO_AVAILABLE_IN_1_8
void ns_pango_renderer_draw_rectangle       (NsPangoRenderer    *renderer,
                                          NsPangoRenderPart   part,
                                          int               x,
                                          int               y,
                                          int               width,
                                          int               height);
NS_PANGO_AVAILABLE_IN_1_8
void ns_pango_renderer_draw_error_underline (NsPangoRenderer    *renderer,
                                          int               x,
                                          int               y,
                                          int               width,
                                          int               height);
NS_PANGO_AVAILABLE_IN_1_8
void ns_pango_renderer_draw_trapezoid       (NsPangoRenderer    *renderer,
                                          NsPangoRenderPart   part,
                                          double            y1_,
                                          double            x11,
                                          double            x21,
                                          double            y2,
                                          double            x12,
                                          double            x22);
NS_PANGO_AVAILABLE_IN_1_8
void ns_pango_renderer_draw_glyph           (NsPangoRenderer    *renderer,
                                          NsPangoFont        *font,
                                          NsPangoGlyph        glyph,
                                          double            x,
                                          double            y);

NS_PANGO_AVAILABLE_IN_1_8
void ns_pango_renderer_activate             (NsPangoRenderer    *renderer);
NS_PANGO_AVAILABLE_IN_1_8
void ns_pango_renderer_deactivate           (NsPangoRenderer    *renderer);

NS_PANGO_AVAILABLE_IN_1_8
void ns_pango_renderer_part_changed         (NsPangoRenderer   *renderer,
                                          NsPangoRenderPart  part);

NS_PANGO_AVAILABLE_IN_1_8
void        ns_pango_renderer_set_color     (NsPangoRenderer    *renderer,
                                          NsPangoRenderPart   part,
                                          const NsPangoColor *color);
NS_PANGO_AVAILABLE_IN_1_8
NsPangoColor *ns_pango_renderer_get_color     (NsPangoRenderer    *renderer,
                                          NsPangoRenderPart   part);

NS_PANGO_AVAILABLE_IN_1_38
void        ns_pango_renderer_set_alpha     (NsPangoRenderer    *renderer,
                                          NsPangoRenderPart   part,
                                          guint16           alpha);
NS_PANGO_AVAILABLE_IN_1_38
guint16     ns_pango_renderer_get_alpha     (NsPangoRenderer    *renderer,
                                          NsPangoRenderPart   part);

NS_PANGO_AVAILABLE_IN_1_8
void               ns_pango_renderer_set_matrix      (NsPangoRenderer     *renderer,
                                                   const NsPangoMatrix *matrix);
NS_PANGO_AVAILABLE_IN_1_8
const NsPangoMatrix *ns_pango_renderer_get_matrix      (NsPangoRenderer     *renderer);

NS_PANGO_AVAILABLE_IN_1_20
NsPangoLayout       *ns_pango_renderer_get_layout      (NsPangoRenderer     *renderer);
NS_PANGO_AVAILABLE_IN_1_20
NsPangoLayoutLine   *ns_pango_renderer_get_layout_line (NsPangoRenderer     *renderer);


/**
 * NsPangoRenderComponent:
 * @NS_PANGO_RENDER_COMPONENT_NONE: No components
 * @NS_PANGO_RENDER_COMPONENT_PLAIN_GLYPH: The plain glyphs of the layout
 * @NS_PANGO_RENDER_COMPONENT_COLOR_GLYPH: The color glyphs of the layout
 * @NS_PANGO_RENDER_COMPONENT_BACKGROUND: Background of the layout
 * @NS_PANGO_RENDER_COMPONENT_UNDERLINE: Underlines of the layout
 * @NS_PANGO_RENDER_COMPONENT_STRIKETHROUGH: Strikethrough lines of the layout
 * @NS_PANGO_RENDER_COMPONENT_OVERLINE: Overlines of the layout
 *
 * Flags that specify which components of a layout to include
 * in renderer output.
 *
 * This is more or less parallel to the [enum@Pango.RenderPart] enum,
 * but allows separating plain and color glyphs, and specifying more
 * than one component.
 *
 * Since: 1.58
 */
typedef enum
{
  NS_PANGO_RENDER_COMPONENT_NONE          = 0,
  NS_PANGO_RENDER_COMPONENT_PLAIN_GLYPH   = 1 << 1,
  NS_PANGO_RENDER_COMPONENT_COLOR_GLYPH   = 1 << 2,
  NS_PANGO_RENDER_COMPONENT_BACKGROUND    = 1 << 3,
  NS_PANGO_RENDER_COMPONENT_UNDERLINE     = 1 << 3,
  NS_PANGO_RENDER_COMPONENT_STRIKETHROUGH = 1 << 4,
  NS_PANGO_RENDER_COMPONENT_OVERLINE      = 1 << 5,
} NsPangoRenderComponent;

#define NS_PANGO_RENDER_COMPONENT_ALL (NS_PANGO_RENDER_COMPONENT_PLAIN_GLYPH | \
                                    NS_PANGO_RENDER_COMPONENT_COLOR_GLYPH | \
                                    NS_PANGO_RENDER_COMPONENT_BACKGROUND | \
                                    NS_PANGO_RENDER_COMPONENT_UNDERLINE | \
                                    NS_PANGO_RENDER_COMPONENT_STRIKETHROUGH | \
                                    NS_PANGO_RENDER_COMPONENT_OVERLINE)

NS_PANGO_AVAILABLE_IN_1_58
void                  ns_pango_renderer_set_components  (NsPangoRenderer        *renderer,
                                                      NsPangoRenderComponent  components);
NS_PANGO_AVAILABLE_IN_1_58
NsPangoRenderComponent  ns_pango_renderer_get_components (NsPangoRenderer         *renderer);


G_DEFINE_AUTOPTR_CLEANUP_FUNC (NsPangoRenderer, g_object_unref)

G_END_DECLS

#endif /* __PANGO_RENDERER_H_ */

