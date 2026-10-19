/* Pango
 * pangofc-private.h: Private routines and declarations for generic
 *  fontconfig operation
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

#ifndef __NS_PANGOFC_PRIVATE_H__
#define __NS_PANGOFC_PRIVATE_H__

#include <pangofc-fontmap-private.h>

G_BEGIN_DECLS


#ifndef FC_WEIGHT_DEMILIGHT
#define FC_WEIGHT_DEMILIGHT 55
#define FC_WEIGHT_SEMILIGHT FC_WEIGHT_DEMILIGHT
#endif


typedef struct _PangoFcMetricsInfo  NsPangoFcMetricsInfo;

struct _PangoFcMetricsInfo
{
  const char       *sample_str;
  NsPangoFontMetrics *metrics;
};


#define NS_PANGO_SCALE_26_6 (NS_PANGO_SCALE / (1<<6))
#define NS_PANGO_PIXELS_26_6(d)				\
  (((d) >= 0) ?						\
   ((d) + NS_PANGO_SCALE_26_6 / 2) / NS_PANGO_SCALE_26_6 :	\
   ((d) - NS_PANGO_SCALE_26_6 / 2) / NS_PANGO_SCALE_26_6)
#define NS_PANGO_UNITS_26_6(d)    ((d) * NS_PANGO_SCALE_26_6)
#define NS_PANGO_UNITS_TO_26_6(d) ((d) / NS_PANGO_SCALE_26_6)

void _ns_pango_fc_font_shutdown (NsPangoFcFont *fcfont);

void           _ns_pango_fc_font_map_remove          (NsPangoFcFontMap *fcfontmap,
						   NsPangoFcFont    *fcfont);

NsPangoCoverage *_ns_pango_fc_font_map_get_coverage    (NsPangoFcFontMap *fcfontmap,
						   NsPangoFcFont    *fcfont);
NsPangoCoverage  *_ns_pango_fc_font_map_fc_to_coverage (FcCharSet      *charset);

NsPangoFcDecoder *_ns_pango_fc_font_get_decoder       (NsPangoFcFont    *font);
void            _ns_pango_fc_font_set_decoder       (NsPangoFcFont    *font,
						  NsPangoFcDecoder *decoder);

NsPangoFcFontKey *_ns_pango_fc_font_get_font_key      (NsPangoFcFont    *fcfont);
void            _ns_pango_fc_font_set_font_key      (NsPangoFcFont    *fcfont,
						  NsPangoFcFontKey *key);

_PANGO_EXTERN
void            ns_pango_fc_font_get_raw_extents    (NsPangoFcFont    *font,
						  NsPangoGlyph      glyph,
						  NsPangoRectangle *ink_rect,
						  NsPangoRectangle *logical_rect);

_PANGO_EXTERN
NsPangoFontMetrics *ns_pango_fc_font_create_base_metrics_for_context (NsPangoFcFont   *font,
								 NsPangoContext  *context);

NsPangoLanguage **_ns_pango_fc_font_map_get_languages (NsPangoFcFontMap *fcfontmap,
                                                  NsPangoFcFont    *fcfont);

G_END_DECLS

#endif /* __NS_PANGOFC_PRIVATE_H__ */
