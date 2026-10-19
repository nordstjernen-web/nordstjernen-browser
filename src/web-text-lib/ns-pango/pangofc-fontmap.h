/* Pango
 * pangofc-fontmap.h: Base fontmap type for fontconfig-based backends
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

#ifndef __NS_PANGO_FC_FONT_MAP_H__
#define __NS_PANGO_FC_FONT_MAP_H__

#include <ns-pango/pango.h>
#include <fontconfig/fontconfig.h>
#include <ns-pango/pangofc-decoder.h>
#include <ns-pango/pangofc-font.h>
#include <hb.h>

G_BEGIN_DECLS


/*
 * NsPangoFcFontMap
 */

#ifdef __GI_SCANNER__
#define NS_PANGO_FC_TYPE_FONT_MAP              (ns_pango_fc_font_map_get_type ())
#define NS_PANGO_FC_FONT_MAP(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_PANGO_FC_TYPE_FONT_MAP, NsPangoFcFontMap))
#define NS_PANGO_FC_IS_FONT_MAP(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_PANGO_FC_TYPE_FONT_MAP))
#else
#define NS_TYPE_PANGO_FC_FONT_MAP              (ns_pango_fc_font_map_get_type ())
#define NS_PANGO_FC_FONT_MAP(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_TYPE_PANGO_FC_FONT_MAP, NsPangoFcFontMap))
#define NS_PANGO_IS_FC_FONT_MAP(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_TYPE_PANGO_FC_FONT_MAP))
#endif

typedef struct _PangoFcFontMap        NsPangoFcFontMap;
typedef struct _PangoFcFontMapClass   NsPangoFcFontMapClass;
typedef struct _PangoFcFontMapPrivate NsPangoFcFontMapPrivate;

NS_PANGO_AVAILABLE_IN_ALL
GType ns_pango_fc_font_map_get_type (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_1_4
void           ns_pango_fc_font_map_cache_clear    (NsPangoFcFontMap *fcfontmap);

NS_PANGO_AVAILABLE_IN_1_38
void
ns_pango_fc_font_map_config_changed (NsPangoFcFontMap *fcfontmap);

NS_PANGO_AVAILABLE_IN_1_38
void
ns_pango_fc_font_map_set_config (NsPangoFcFontMap *fcfontmap,
			      FcConfig       *fcconfig);
NS_PANGO_AVAILABLE_IN_1_38
FcConfig *
ns_pango_fc_font_map_get_config (NsPangoFcFontMap *fcfontmap);

/**
 * NsPangoFcDecoderFindFunc:
 * @pattern: a fully resolved `FcPattern` specifying the font on the system
 * @user_data: user data passed to
 *   [method@NsPangoFc.FontMap.add_decoder_find_func]
 *
 * Callback function passed to [method@NsPangoFc.FontMap.add_decoder_find_func].
 *
 * Return value: a new reference to a custom decoder for this pattern,
 *  or %NULL if the default decoder handling should be used.
 **/
typedef NsPangoFcDecoder * (*NsPangoFcDecoderFindFunc) (FcPattern *pattern,
						    gpointer   user_data);

NS_PANGO_AVAILABLE_IN_1_6
void ns_pango_fc_font_map_add_decoder_find_func (NsPangoFcFontMap        *fcfontmap,
					      NsPangoFcDecoderFindFunc findfunc,
					      gpointer               user_data,
					      GDestroyNotify         dnotify);
NS_PANGO_AVAILABLE_IN_1_26
NsPangoFcDecoder *ns_pango_fc_font_map_find_decoder (NsPangoFcFontMap *fcfontmap,
					        FcPattern      *pattern);

NS_PANGO_AVAILABLE_IN_1_4
NsPangoFontDescription *ns_pango_fc_font_description_from_pattern (FcPattern *pattern,
							      gboolean   include_size);

#ifndef NS_PANGO_DISABLE_DEPRECATED
NS_PANGO_DEPRECATED_IN_1_22_FOR(ns_pango_font_map_create_context)
NsPangoContext * ns_pango_fc_font_map_create_context (NsPangoFcFontMap *fcfontmap);
#endif
NS_PANGO_AVAILABLE_IN_1_4
void           ns_pango_fc_font_map_shutdown       (NsPangoFcFontMap *fcfontmap);


NS_PANGO_AVAILABLE_IN_1_44
hb_face_t * ns_pango_fc_font_map_get_hb_face (NsPangoFcFontMap *fcfontmap,
                                           NsPangoFcFont    *fcfont);

/**
 * NsPangoFcSubstituteFunc:
 * @pattern: the FcPattern to tweak.
 * @data: user data.
 *
 * Function type for doing final config tweaking on prepared `FcPattern`s.
 */
typedef void (*NsPangoFcSubstituteFunc) (FcPattern *pattern,
				       gpointer   data);

/**
 * ns_pango_fc_font_map_set_default_substitute:
 * @fontmap: a `NsPangoFcFontMap`
 * @func: function to call to to do final config tweaking on `FcPattern` objects
 * @data: data to pass to @func
 * @notify: function to call when @data is no longer used
 *
 * Sets a function that will be called to do final configuration
 * substitution on a `FcPattern` before it is used to load
 * the font.
 *
 * This function can be used to do things like set
 * hinting and antialiasing options.
 *
 * Since: 1.48
 */
NS_PANGO_AVAILABLE_IN_1_48
void ns_pango_fc_font_map_set_default_substitute (NsPangoFcFontMap        *fontmap,
					       NsPangoFcSubstituteFunc func,
					       gpointer              data,
					       GDestroyNotify        notify);

/**
 * ns_pango_fc_font_map_substitute_changed:
 * @fontmap: a `NsPangoFcFontMap`
 *
 * Call this function any time the results of the default
 * substitution function set with
 * [method@NsPangoFc.FontMap.set_default_substitute] change.
 *
 * That is, if your substitution function will return different
 * results for the same input pattern, you must call this function.
 *
 * Since: 1.48
 */
NS_PANGO_AVAILABLE_IN_1_48
void ns_pango_fc_font_map_substitute_changed (NsPangoFcFontMap *fontmap);

/**
 * NS_PANGO_FC_GRAVITY:
 *
 * Fontconfig property that Pango sets on any
 * fontconfig pattern it passes to fontconfig
 * if a `NsPangoGravity` other than %NS_PANGO_GRAVITY_SOUTH
 * is desired.
 *
 * The property will have a `NsPangoGravity` value as a string,
 * like "east". This can be used to write fontconfig configuration
 * rules to choose different fonts for horizontal and vertical
 * writing directions.
 *
 * Since: 1.20
 */
#define NS_PANGO_FC_GRAVITY "pangogravity"

/**
 * NS_PANGO_FC_VERSION:
 *
 * Fontconfig property that Pango sets on any
 * fontconfig pattern it passes to fontconfig.
 *
 * The property will have an integer value equal to what
 * [func@Pango.version] returns. This can be used to write
 * fontconfig configuration rules that only affect certain
 * pango versions (or only pango-using applications, or only
 * non-pango-using applications).
 *
 * Since: 1.20
 */
#define NS_PANGO_FC_VERSION "pangoversion"

/**
 * NS_PANGO_FC_PRGNAME:
 *
 * Fontconfig property that Pango sets on any
 * fontconfig pattern it passes to fontconfig.
 *
 * The property will have a string equal to what
 * g_get_prgname() returns. This can be used to write
 * fontconfig configuration rules that only affect
 * certain applications.
 *
 * This is equivalent to FC_PRGNAME in versions of
 * fontconfig that have that.
 *
 * Since: 1.24
 *
 * Deprecated: 1.56: Use FC_PRGNAME
 */
#define NS_PANGO_FC_PRGNAME "prgname"

/**
 * NS_PANGO_FC_FONT_FEATURES:
 *
 * Fontconfig property that Pango reads from font
 * patterns to populate list of OpenType features
 * to be enabled for the font by default.
 *
 * The property will have a number of string elements,
 * each of which is the OpenType feature tag of one feature
 * to enable.
 *
 * This is equivalent to FC_FONT_FEATURES in versions of
 * fontconfig that have that.
 *
 * Since: 1.34
 *
 * Deprecated: 1.56: Use FC_FONT_FEATURES
 */
#define NS_PANGO_FC_FONT_FEATURES "fontfeatures"

/**
 * NS_PANGO_FC_FONT_VARIATIONS:
 *
 * Fontconfig property that Pango reads from font
 * patterns to populate list of OpenType font variations
 * to be used for a font.
 *
 * The property will have a string elements, each of which
 * a comma-separated list of OpenType axis setting of the
 * form AXIS=VALUE.
 *
 * This is equivalent to FC_FONT_VARIATIONS in versions of
 * fontconfig that have that.
 *
 * Deprecated: 1.56: Use FC_FONT_VARIATIONS
 */
#define NS_PANGO_FC_FONT_VARIATIONS "fontvariations"

G_DEFINE_AUTOPTR_CLEANUP_FUNC (NsPangoFcFontMap, g_object_unref)

G_END_DECLS

#endif /* __NS_PANGO_FC_FONT_MAP_H__ */
