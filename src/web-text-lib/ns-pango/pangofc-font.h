/* Pango
 * pangofc-font.h: Base fontmap type for fontconfig-based backends
 *
 * Copyright (C) 2003 Red Hat Software
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

#ifndef __NS_PANGO_FC_FONT_H__
#define __NS_PANGO_FC_FONT_H__

#include <ns-pango/pango-glyph.h>
#include <ns-pango/pango-font.h>
#include <ns-pango/pango-glyph.h>

/* FreeType has undefined macros in its header */
#ifdef NS_PANGO_COMPILATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#endif

#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>

#ifdef NS_PANGO_COMPILATION
#pragma GCC diagnostic pop
#endif

G_BEGIN_DECLS

#ifdef __GI_SCANNER__
#define NS_PANGO_FC_TYPE_FONT              (ns_pango_fc_font_get_type ())
#define NS_PANGO_FC_FONT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_PANGO_FC_TYPE_FONT, NsPangoFcFont))
#define NS_PANGO_FC_IS_FONT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_PANGO_FC_TYPE_FONT))
#else
#define NS_TYPE_PANGO_FC_FONT              (ns_pango_fc_font_get_type ())
#define NS_PANGO_FC_FONT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_TYPE_PANGO_FC_FONT, NsPangoFcFont))
#define NS_PANGO_IS_FC_FONT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_TYPE_PANGO_FC_FONT))
#endif

typedef struct _PangoFcFont      NsPangoFcFont;
typedef struct _PangoFcFontClass NsPangoFcFontClass;

#ifndef NS_PANGO_DISABLE_DEPRECATED

/**
 * NsPangoFcFont:
 *
 * `NsPangoFcFont` is a base class for font implementations
 * using the Fontconfig and FreeType libraries.
 *
 * It is used in onjunction with [class@NsPangoFc.FontMap].
 * When deriving from this class, you need to implement all
 * of its virtual functions other than shutdown() along with
 * the get_glyph_extents() virtual function from `NsPangoFont`.
 */
struct _PangoFcFont
{
  NsPangoFont parent_instance;

  FcPattern *font_pattern;          /* fully resolved pattern */
  NsPangoFontMap *fontmap;            /* associated map (no strong reference is held,
                                     * but a g_object_add_weak_pointer() guards it) */
  gpointer priv;                    /* used internally */
  NsPangoMatrix matrix;               /* unused */
  NsPangoFontDescription *description;

  GSList *metrics_by_lang;

  guint is_hinted : 1;
  guint is_transformed : 1;
};

#endif /* NS_PANGO_DISABLE_DEPRECATED */

NS_PANGO_AVAILABLE_IN_ALL
GType      ns_pango_fc_font_get_type (void) G_GNUC_CONST;

NS_PANGO_DEPRECATED_IN_1_44
gboolean   ns_pango_fc_font_has_char          (NsPangoFcFont      *font,
                                            gunichar          wc);
NS_PANGO_AVAILABLE_IN_1_4
guint      ns_pango_fc_font_get_glyph         (NsPangoFcFont      *font,
                                            gunichar          wc);

NS_PANGO_DEPRECATED_IN_1_50_FOR(ns_pango_font_get_language)
NsPangoLanguage **
           ns_pango_fc_font_get_languages     (NsPangoFcFont      *font);

NS_PANGO_AVAILABLE_IN_1_48
FcPattern *ns_pango_fc_font_get_pattern       (NsPangoFcFont      *font);

NS_PANGO_DEPRECATED_FOR(NS_PANGO_GET_UNKNOWN_GLYPH)
NsPangoGlyph ns_pango_fc_font_get_unknown_glyph (NsPangoFcFont      *font,
                                            gunichar          wc);
NS_PANGO_DEPRECATED_IN_1_32
void       ns_pango_fc_font_kern_glyphs       (NsPangoFcFont      *font,
                                            NsPangoGlyphString *glyphs);

NS_PANGO_DEPRECATED_IN_1_44_FOR(ns_pango_font_get_hb_font)
FT_Face    ns_pango_fc_font_lock_face         (NsPangoFcFont      *font);
NS_PANGO_DEPRECATED_IN_1_44_FOR(ns_pango_font_get_hb_font)
void       ns_pango_fc_font_unlock_face       (NsPangoFcFont      *font);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (NsPangoFcFont, g_object_unref)

G_END_DECLS
#endif /* __NS_PANGO_FC_FONT_H__ */
