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

#ifndef __NS_PANGO_FONTMAP_PRIVATE_H__
#define __NS_PANGO_FONTMAP_PRIVATE_H__

#include <ns-pango/pango-font-private.h>
#include <ns-pango/pango-fontset.h>
#include <ns-pango/pango-fontmap.h>

G_BEGIN_DECLS

typedef struct {
  NsPangoFont * (* reload_font) (NsPangoFontMap *fontmap,
                               NsPangoFont    *font,
                               double        scale,
                               NsPangoContext *context,
                               const char   *variations);

  gboolean (* add_font_file)  (NsPangoFontMap  *fontmap,
                               const char    *filename,
                               GError       **error);

} NsPangoFontMapClassPrivate;

NS_PANGO_DEPRECATED_IN_1_38
const char   *ns_pango_font_map_get_shape_engine_type (NsPangoFontMap *fontmap);

G_END_DECLS

#endif /* __NS_PANGO_FONTMAP_PRIVATE_H__ */
