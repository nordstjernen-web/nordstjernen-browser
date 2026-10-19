/* Pango
 * pango-markup.h: Parser for attributed text
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __NS_PANGO_MARKUP_H__
#define __NS_PANGO_MARKUP_H__

#include <ns-pango/pango-attributes.h>

G_BEGIN_DECLS


NS_PANGO_AVAILABLE_IN_1_32
GMarkupParseContext * ns_pango_markup_parser_new    (gunichar               accel_marker);

NS_PANGO_AVAILABLE_IN_1_32
gboolean              ns_pango_markup_parser_finish (GMarkupParseContext   *context,
                                                  NsPangoAttrList        **attr_list,
                                                  char                 **text,
                                                  gunichar              *accel_char,
                                                  GError               **error);

NS_PANGO_AVAILABLE_IN_ALL
gboolean               ns_pango_parse_markup        (const char            *markup_text,
                                                  int                    length,
                                                  gunichar               accel_marker,
                                                  NsPangoAttrList        **attr_list,
                                                  char                 **text,
                                                  gunichar              *accel_char,
                                                  GError               **error);


G_END_DECLS

#endif /* __NS_PANGO_MARKUP_H__ */
