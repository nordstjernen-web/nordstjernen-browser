/* Nordstjernen — value grammars for keyword-shaped CSS properties.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-or-later
 */

#ifndef NS_CSS_GRAMMAR_H
#define NS_CSS_GRAMMAR_H

#include <glib.h>

G_BEGIN_DECLS

char    *ns_css_grammar_canonical(const char *grammar, const char *text);
gboolean ns_css_grammar_has_keyword(const char *grammar, const char *keyword);

G_END_DECLS

#endif
