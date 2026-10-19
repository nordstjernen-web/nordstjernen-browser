/* Pango
 * pango-language.h: Language handling routines
 *
 * Copyright (C) 1999 Red Hat Software
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

#ifndef __NS_PANGO_LANGUAGE_H__
#define __NS_PANGO_LANGUAGE_H__

#include <glib.h>
#include <glib-object.h>

#include <ns-pango/pango-types.h>
#include <ns-pango/pango-version-macros.h>
#include <ns-pango/pango-script.h>

G_BEGIN_DECLS

#define NS_TYPE_PANGO_LANGUAGE (ns_pango_language_get_type ())

NS_PANGO_AVAILABLE_IN_ALL
GType                   ns_pango_language_get_type                 (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_1_16
NsPangoLanguage *         ns_pango_language_get_default              (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_1_48
NsPangoLanguage **        ns_pango_language_get_preferred            (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_ALL
NsPangoLanguage *         ns_pango_language_from_string              (const char     *language);

NS_PANGO_AVAILABLE_IN_ALL
const char *            ns_pango_language_to_string                (NsPangoLanguage  *language) G_GNUC_CONST;

/* For back compat.  Will have to keep indefinitely. */
#define ns_pango_language_to_string(language) ((const char *)language)

NS_PANGO_AVAILABLE_IN_ALL
const char *            ns_pango_language_get_sample_string        (NsPangoLanguage  *language) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_ALL
gboolean                ns_pango_language_matches                  (NsPangoLanguage  *language,
                                                                 const char     *range_list) G_GNUC_PURE;

NS_PANGO_AVAILABLE_IN_1_4
gboolean                ns_pango_language_includes_script          (NsPangoLanguage  *language,
                                                                 NsPangoScript     script) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_1_22
const NsPangoScript *     ns_pango_language_get_scripts              (NsPangoLanguage  *language,
                                                                 int            *num_scripts);

G_END_DECLS

#endif /* __NS_PANGO_LANGUAGE_H__ */
