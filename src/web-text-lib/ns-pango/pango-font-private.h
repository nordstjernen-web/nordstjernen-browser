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

#ifndef __NS_PANGO_FONT_PRIVATE_H__
#define __NS_PANGO_FONT_PRIVATE_H__

#include <ns-pango/pango-font.h>
#include <ns-pango/pango-coverage.h>
#include <ns-pango/pango-types.h>

#include <glib-object.h>

G_BEGIN_DECLS

NS_PANGO_AVAILABLE_IN_ALL
NsPangoFontMetrics *ns_pango_font_metrics_new (void);

typedef struct {
  NsPangoLanguage ** (* get_languages) (NsPangoFont *font);

  gboolean         (* is_hinted) (NsPangoFont *font);

  void             (* get_scale_factors) (NsPangoFont *font,
                                          double    *x_scale,
                                          double    *y_scale);

  gboolean         (* has_char) (NsPangoFont *font,
                                 gunichar   wc);
  NsPangoFontFace *  (* get_face) (NsPangoFont *font);
  void             (* get_matrix) (NsPangoFont   *font,
                                   NsPangoMatrix *matrix);
  int              (* get_absolute_size) (NsPangoFont *font);
  NsPangoVariant     (* get_variant) (NsPangoFont *font);
} NsPangoFontClassPrivate;

gboolean ns_pango_font_is_hinted         (NsPangoFont *font);
void     ns_pango_font_get_scale_factors (NsPangoFont *font,
                                       double    *x_scale,
                                       double    *y_scale);
void     ns_pango_font_get_matrix        (NsPangoFont   *font,
                                       NsPangoMatrix *matrix);

static inline int ns_pango_font_get_absolute_size (NsPangoFont *font)
{
  GTypeClass *klass = (GTypeClass *) NS_PANGO_FONT_GET_CLASS (font);
  NsPangoFontClassPrivate *priv = (NsPangoFontClassPrivate *) g_type_class_get_private (klass, NS_TYPE_PANGO_FONT);
  return priv->get_absolute_size (font);
}

static inline NsPangoVariant
ns_pango_font_get_variant (NsPangoFont *font)
{
  GTypeClass *klass = (GTypeClass *) NS_PANGO_FONT_GET_CLASS (font);
  NsPangoFontClassPrivate *priv = (NsPangoFontClassPrivate *) g_type_class_get_private (klass, NS_TYPE_PANGO_FONT);
  if (priv->get_variant)
    return priv->get_variant (font);
  else
    {
      NsPangoFontDescription *desc;
      NsPangoVariant variant;

      desc = ns_pango_font_describe (font);
      variant = ns_pango_font_description_get_variant (desc);
      ns_pango_font_description_free (desc);

      return variant;
    }
}

G_END_DECLS

#endif /* __NS_PANGO_FONT_PRIVATE_H__ */
