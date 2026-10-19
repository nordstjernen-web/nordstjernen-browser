/* Pango
 * pango-fontset.h: Font set handling
 *
 * Copyright (C) 2001 Red Hat Software
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

#ifndef __NS_PANGO_FONTSET_H__
#define __NS_PANGO_FONTSET_H__

#include <ns-pango/pango-coverage.h>
#include <ns-pango/pango-types.h>

#include <glib-object.h>

G_BEGIN_DECLS

/*
 * NsPangoFontset
 */

#define NS_TYPE_PANGO_FONTSET              (ns_pango_fontset_get_type ())
#define NS_PANGO_FONTSET(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_TYPE_PANGO_FONTSET, NsPangoFontset))
#define NS_PANGO_IS_FONTSET(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_TYPE_PANGO_FONTSET))
#define NS_PANGO_FONTSET_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), NS_TYPE_PANGO_FONTSET, NsPangoFontsetClass))
#define NS_PANGO_IS_FONTSET_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), NS_TYPE_PANGO_FONTSET))
#define NS_PANGO_FONTSET_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), NS_TYPE_PANGO_FONTSET, NsPangoFontsetClass))


NS_PANGO_AVAILABLE_IN_ALL
GType ns_pango_fontset_get_type (void) G_GNUC_CONST;

typedef struct _PangoFontset        NsPangoFontset;
typedef struct _PangoFontsetClass   NsPangoFontsetClass;

/**
 * NsPangoFontsetForeachFunc:
 * @fontset: a `NsPangoFontset`
 * @font: a font from @fontset
 * @user_data: callback data
 *
 * Callback used when enumerating fonts in a fontset.
 *
 * See [method@Pango.Fontset.foreach].
 *
 * Returns: if %TRUE, stop iteration and return immediately.
 *
 * Since: 1.4
 */
typedef gboolean (*NsPangoFontsetForeachFunc) (NsPangoFontset  *fontset,
                                             NsPangoFont     *font,
                                             gpointer       user_data);

/**
 * NsPangoFontset:
 *
 * A `NsPangoFontset` represents a set of `NsPangoFont` to use when rendering text.
 *
 * A `NsPangoFontset` is the result of resolving a `NsPangoFontDescription`
 * against a particular `NsPangoContext`. It has operations for finding the
 * component font for a particular Unicode character, and for finding a
 * composite set of metrics for the entire fontset.
 */
struct _PangoFontset
{
  GObject parent_instance;
};

/**
 * NsPangoFontsetClass:
 * @parent_class: parent `GObjectClass`
 * @get_font: a function to get the font in the fontset that contains the
 *   best glyph for the given Unicode character; see [method@Pango.Fontset.get_font]
 * @get_metrics: a function to get overall metric information for the fonts
 *   in the fontset; see [method@Pango.Fontset.get_metrics]
 * @get_language: a function to get the language of the fontset.
 * @foreach: a function to loop over the fonts in the fontset. See
 *   [method@Pango.Fontset.foreach]
 *
 * The `NsPangoFontsetClass` structure holds the virtual functions for
 * a particular `NsPangoFontset` implementation.
 */
struct _PangoFontsetClass
{
  GObjectClass parent_class;

  /*< public >*/

  NsPangoFont *       (*get_font)     (NsPangoFontset     *fontset,
                                     guint             wc);

  NsPangoFontMetrics *(*get_metrics)  (NsPangoFontset     *fontset);
  NsPangoLanguage *   (*get_language) (NsPangoFontset     *fontset);
  void              (*foreach)      (NsPangoFontset           *fontset,
                                     NsPangoFontsetForeachFunc func,
                                     gpointer                data);

  /*< private >*/

  /* Padding for future expansion */
  void (*_ns_pango_reserved1) (void);
  void (*_ns_pango_reserved2) (void);
  void (*_ns_pango_reserved3) (void);
  void (*_ns_pango_reserved4) (void);
};

NS_PANGO_AVAILABLE_IN_ALL
NsPangoFont *             ns_pango_fontset_get_font          (NsPangoFontset                   *fontset,
                                                         guint                           wc);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoFontMetrics *      ns_pango_fontset_get_metrics       (NsPangoFontset                   *fontset);
NS_PANGO_AVAILABLE_IN_1_4
void                    ns_pango_fontset_foreach           (NsPangoFontset                   *fontset,
                                                         NsPangoFontsetForeachFunc         func,
                                                         gpointer                        data);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (NsPangoFontset, g_object_unref)

G_END_DECLS

#endif /* __NS_PANGO_FONTSET_H__ */
