/* Pango
 * pango-fontset-simple.h: Font set handling
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

#ifndef __NS_PANGO_FONTSET_SIMPLE_H__
#define __NS_PANGO_FONTSET_SIMPLE_H__

#include <ns-pango/pango-coverage.h>
#include <ns-pango/pango-types.h>
#include <ns-pango/pango-fontset.h>

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * NsPangoFontsetSimple:
 *
 * `NsPangoFontsetSimple` is a implementation of the abstract
 * `NsPangoFontset` base class as an array of fonts.
 *
 * When creating a `NsPangoFontsetSimple`, you have to provide
 * the array of fonts that make up the fontset.
 */
#define NS_TYPE_PANGO_FONTSET_SIMPLE       (ns_pango_fontset_simple_get_type ())
#define NS_PANGO_FONTSET_SIMPLE(object)    (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_TYPE_PANGO_FONTSET_SIMPLE, NsPangoFontsetSimple))
#define NS_PANGO_IS_FONTSET_SIMPLE(object) (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_TYPE_PANGO_FONTSET_SIMPLE))

typedef struct _PangoFontsetSimple  NsPangoFontsetSimple;
typedef struct _PangoFontsetSimpleClass  NsPangoFontsetSimpleClass;


NS_PANGO_AVAILABLE_IN_ALL
GType                   ns_pango_fontset_simple_get_type (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_ALL
NsPangoFontsetSimple *    ns_pango_fontset_simple_new    (NsPangoLanguage      *language);
NS_PANGO_AVAILABLE_IN_ALL
void                    ns_pango_fontset_simple_append (NsPangoFontsetSimple *fontset,
                                                     NsPangoFont          *font);
NS_PANGO_AVAILABLE_IN_ALL
int                     ns_pango_fontset_simple_size   (NsPangoFontsetSimple *fontset);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (NsPangoFontsetSimple, g_object_unref)

G_END_DECLS

#endif /* __NS_PANGO_FONTSET_SIMPLE_H__ */
