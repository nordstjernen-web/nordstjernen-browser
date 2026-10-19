/* Pango
 * pango-font.h: Font handling
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

#ifndef __NS_PANGO_FONTMAP_H__
#define __NS_PANGO_FONTMAP_H__

#include <ns-pango/pango-types.h>
#include <ns-pango/pango-font.h>
#include <ns-pango/pango-fontset.h>

G_BEGIN_DECLS

#define NS_TYPE_PANGO_FONT_MAP              (ns_pango_font_map_get_type ())
#define NS_PANGO_FONT_MAP(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_TYPE_PANGO_FONT_MAP, NsPangoFontMap))
#define NS_PANGO_IS_FONT_MAP(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_TYPE_PANGO_FONT_MAP))
#define NS_PANGO_FONT_MAP_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), NS_TYPE_PANGO_FONT_MAP, NsPangoFontMapClass))
#define NS_PANGO_IS_FONT_MAP_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), NS_TYPE_PANGO_FONT_MAP))
#define NS_PANGO_FONT_MAP_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), NS_TYPE_PANGO_FONT_MAP, NsPangoFontMapClass))

typedef struct _PangoFontMapClass NsPangoFontMapClass;

/**
 * NsPangoFontMap:
 *
 * A `NsPangoFontMap` represents the set of fonts available for a
 * particular rendering system.
 *
 * This is a virtual object with implementations being specific to
 * particular rendering systems.
 */
struct _PangoFontMap
{
  GObject parent_instance;
};

/**
 * NsPangoFontMapClass:
 * @parent_class: parent `GObjectClass`
 * @load_font: a function to load a font with a given description. See
 * ns_pango_font_map_load_font().
 * @list_families: A function to list available font families. See
 * ns_pango_font_map_list_families().
 * @load_fontset: a function to load a fontset with a given given description
 * suitable for a particular language. See ns_pango_font_map_load_fontset().
 * @shape_engine_type: the type of rendering-system-dependent engines that
 * can handle fonts of this fonts loaded with this fontmap.
 * @get_serial: a function to get the serial number of the fontmap.
 * See ns_pango_font_map_get_serial().
 * @changed: See ns_pango_font_map_changed()
 *
 * The `NsPangoFontMapClass` structure holds the virtual functions for
 * a particular `NsPangoFontMap` implementation.
 */
struct _PangoFontMapClass
{
  GObjectClass parent_class;

  /*< public >*/

  NsPangoFont *   (*load_font)     (NsPangoFontMap               *fontmap,
                                  NsPangoContext               *context,
                                  const NsPangoFontDescription *desc);
  void          (*list_families) (NsPangoFontMap               *fontmap,
                                  NsPangoFontFamily          ***families,
                                  int                        *n_families);
  NsPangoFontset *(*load_fontset)  (NsPangoFontMap               *fontmap,
                                  NsPangoContext               *context,
                                  const NsPangoFontDescription *desc,
                                  NsPangoLanguage              *language);

  const char     *shape_engine_type;

  guint         (*get_serial)    (NsPangoFontMap               *fontmap);
  void          (*changed)       (NsPangoFontMap               *fontmap);

  NsPangoFontFamily * (*get_family) (NsPangoFontMap               *fontmap,
                                   const char                 *name);

  NsPangoFontFace *   (*get_face)   (NsPangoFontMap               *fontmap,
                                   NsPangoFont                  *font);
};

NS_PANGO_AVAILABLE_IN_ALL
GType         ns_pango_font_map_get_type       (void) G_GNUC_CONST;
NS_PANGO_AVAILABLE_IN_1_22
NsPangoContext * ns_pango_font_map_create_context (NsPangoFontMap               *fontmap);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoFont *   ns_pango_font_map_load_font     (NsPangoFontMap                 *fontmap,
					    NsPangoContext                 *context,
					    const NsPangoFontDescription   *desc);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoFontset *ns_pango_font_map_load_fontset  (NsPangoFontMap                 *fontmap,
					    NsPangoContext                 *context,
					    const NsPangoFontDescription   *desc,
					    NsPangoLanguage                *language);
NS_PANGO_AVAILABLE_IN_ALL
void          ns_pango_font_map_list_families (NsPangoFontMap                 *fontmap,
					    NsPangoFontFamily            ***families,
					    int                          *n_families);
NS_PANGO_AVAILABLE_IN_1_32
guint         ns_pango_font_map_get_serial    (NsPangoFontMap                 *fontmap);
NS_PANGO_AVAILABLE_IN_1_34
void          ns_pango_font_map_changed       (NsPangoFontMap                 *fontmap);

NS_PANGO_AVAILABLE_IN_1_46
NsPangoFontFamily *ns_pango_font_map_get_family (NsPangoFontMap                 *fontmap,
                                            const char                   *name);

NS_PANGO_AVAILABLE_IN_1_52
NsPangoFont *   ns_pango_font_map_reload_font   (NsPangoFontMap                 *fontmap,
                                            NsPangoFont                    *font,
                                            double                        scale,
                                            NsPangoContext                 *context,
                                            const char                   *variations);

NS_PANGO_AVAILABLE_IN_1_56
gboolean      ns_pango_font_map_add_font_file (NsPangoFontMap                 *fontmap,
                                            const char                   *filename,
                                            GError                      **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(NsPangoFontMap, g_object_unref)

G_END_DECLS

#endif /* __NS_PANGO_FONTMAP_H__ */
