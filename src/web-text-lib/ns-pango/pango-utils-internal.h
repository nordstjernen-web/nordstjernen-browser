/* Pango
 * pango-utils.c: Utilities for internal functions and modules
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

#ifndef __NS_PANGO_UTILS_INTERNAL_H__
#define __NS_PANGO_UTILS_INTERNAL_H__

#include <stdio.h>
#include <glib.h>
#include <ns-pango/pango-font.h>

G_BEGIN_DECLS

gboolean _ns_pango_scan_int                (const char **pos,
	         	                 int         *out);

gboolean _ns_pango_parse_enum              (GType       type,
                                         const char *str,
                                         int        *value,
                                         gboolean    warn,
                                         char      **possible_values);
gboolean ns_pango_parse_flags              (GType       type,
                                         const char *str,
                                         int        *value,
                                         char      **possible_values);

char    *_ns_pango_trim_string             (const char *str);

gboolean ns_pango_parse_width (const char *str,
                            NsPangoWidth *width,
                            gboolean    warn);

G_END_DECLS

#endif /* __NS_PANGO_UTILS_H__ */
