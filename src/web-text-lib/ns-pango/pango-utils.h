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

#ifndef __NS_PANGO_UTILS_H__
#define __NS_PANGO_UTILS_H__

#include <stdio.h>
#include <glib.h>
#include <ns-pango/pango-font.h>

G_BEGIN_DECLS

NS_PANGO_DEPRECATED
char **  ns_pango_split_file_list (const char *str);

NS_PANGO_DEPRECATED
char    *ns_pango_trim_string     (const char *str);
NS_PANGO_DEPRECATED
gint     ns_pango_read_line      (FILE        *stream,
			       GString     *str);
NS_PANGO_DEPRECATED
gboolean ns_pango_skip_space     (const char **pos);
NS_PANGO_DEPRECATED
gboolean ns_pango_scan_word      (const char **pos,
			       GString     *out);
NS_PANGO_DEPRECATED
gboolean ns_pango_scan_string    (const char **pos,
			       GString     *out);
NS_PANGO_DEPRECATED
gboolean ns_pango_scan_int       (const char **pos,
			       int         *out);

NS_PANGO_DEPRECATED
gboolean ns_pango_parse_enum     (GType       type,
			       const char *str,
			       int        *value,
			       gboolean    warn,
			       char      **possible_values);

/* Functions for parsing textual representations
 * of NsPangoFontDescription fields. They return TRUE if the input string
 * contains a valid value, which then has been assigned to the corresponding
 * field in the NsPangoFontDescription. If the warn parameter is TRUE,
 * a warning is printed (with g_warning) if the string does not
 * contain a valid value.
 */
NS_PANGO_AVAILABLE_IN_ALL
gboolean ns_pango_parse_style   (const char   *str,
			      NsPangoStyle   *style,
			      gboolean      warn);
NS_PANGO_AVAILABLE_IN_ALL
gboolean ns_pango_parse_variant (const char   *str,
			      NsPangoVariant *variant,
			      gboolean      warn);
NS_PANGO_AVAILABLE_IN_ALL
gboolean ns_pango_parse_weight  (const char   *str,
			      NsPangoWeight  *weight,
			      gboolean      warn);
NS_PANGO_AVAILABLE_IN_ALL
gboolean ns_pango_parse_stretch (const char   *str,
			      NsPangoStretch *stretch,
			      gboolean      warn);


/* Hint line position and thickness.
 */
NS_PANGO_AVAILABLE_IN_1_12
void ns_pango_quantize_line_geometry (int *thickness,
				   int *position);

/* A routine from fribidi that we either wrap or provide ourselves.
 */
NS_PANGO_AVAILABLE_IN_1_4
guint8 * ns_pango_log2vis_get_embedding_levels (const gchar    *text,
					     int             length,
					     NsPangoDirection *pbase_dir);

/* Unicode characters that are zero-width and should not be rendered
 * normally.
 */
NS_PANGO_AVAILABLE_IN_1_10
gboolean ns_pango_is_zero_width (gunichar ch) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_ALL
void     ns_pango_find_paragraph_boundary (const char *text,
                                        int         length,
                                        int        *paragraph_delimiter_index,
                                        int        *next_paragraph_start);

/* Pango version checking */

/* Encode a Pango version as an integer */
/**
 * NS_PANGO_VERSION_ENCODE:
 * @major: the major component of the version number
 * @minor: the minor component of the version number
 * @micro: the micro component of the version number
 *
 * This macro encodes the given Pango version into an integer.  The numbers
 * returned by %NS_PANGO_VERSION and ns_pango_version() are encoded using this macro.
 * Two encoded version numbers can be compared as integers.
 */
#define NS_PANGO_VERSION_ENCODE(major, minor, micro) (     \
	  ((major) * 10000)                             \
	+ ((minor) *   100)                             \
	+ ((micro) *     1))

/* Encoded version of Pango at compile-time */
/**
 * NS_PANGO_VERSION:
 *
 * The version of Pango available at compile-time, encoded using NS_PANGO_VERSION_ENCODE().
 */
/**
 * NS_PANGO_VERSION_STRING:
 *
 * A string literal containing the version of Pango available at compile-time.
 */
/**
 * NS_PANGO_VERSION_MAJOR:
 *
 * The major component of the version of Pango available at compile-time.
 */
/**
 * NS_PANGO_VERSION_MINOR:
 *
 * The minor component of the version of Pango available at compile-time.
 */
/**
 * NS_PANGO_VERSION_MICRO:
 *
 * The micro component of the version of Pango available at compile-time.
 */
#define NS_PANGO_VERSION NS_PANGO_VERSION_ENCODE(     \
	NS_PANGO_VERSION_MAJOR,                    \
	NS_PANGO_VERSION_MINOR,                    \
	NS_PANGO_VERSION_MICRO)

/* Check that compile-time Pango is as new as required */
/**
 * NS_PANGO_VERSION_CHECK:
 * @major: the major component of the version number
 * @minor: the minor component of the version number
 * @micro: the micro component of the version number
 *
 * Checks that the version of Pango available at compile-time is not older than
 * the provided version number.
 */
#define NS_PANGO_VERSION_CHECK(major,minor,micro)    \
	(NS_PANGO_VERSION >= NS_PANGO_VERSION_ENCODE(major,minor,micro))


/* Return encoded version of Pango at run-time */
NS_PANGO_AVAILABLE_IN_1_16
int ns_pango_version (void) G_GNUC_CONST;

/* Return run-time Pango version as an string */
NS_PANGO_AVAILABLE_IN_1_16
const char * ns_pango_version_string (void) G_GNUC_CONST;

/* Check that run-time Pango is as new as required */
NS_PANGO_AVAILABLE_IN_1_16
const char * ns_pango_version_check (int required_major,
                                  int required_minor,
                                  int required_micro) G_GNUC_CONST;

G_END_DECLS

#endif /* __NS_PANGO_UTILS_H__ */
