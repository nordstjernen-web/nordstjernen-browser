/* Nordstjernen — HTML parser API (lexbor).
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_HTML_H
#define NS_HTML_H

#include <glib.h>

#include "dom.h"

G_BEGIN_DECLS

ns_node *ns_html_parse(const char *input, gssize len);

ns_node *ns_html_parse_fragment_in(const char *context_tag,
                                   const char *input, gssize len);

gboolean ns_html_is_void(const char *tag);

gboolean ns_html_is_raw_text(const char *tag);

void ns_html_escape_append(GString *out, const char *s, gboolean escape_quotes);

char *ns_html_escape_text(const char *s);

char *ns_html_decode_body(const char *body, gsize len);

char *ns_html_image_document(const char *url);

const char *ns_html_engine_name(void);

const char *ns_html_engine_version(void);

G_END_DECLS

#endif
