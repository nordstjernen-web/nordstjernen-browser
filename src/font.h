/* Nordstjernen — @font-face web font loader.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_FONT_H
#define NS_FONT_H

#include <glib.h>

G_BEGIN_DECLS

typedef void (*ns_font_loaded_cb)(const char *family, gpointer user_data);

void     ns_font_init(void);
void     ns_font_shutdown(void);

gboolean ns_font_available(void);


gboolean ns_font_family_loaded(const char *family);

void     ns_font_request(const char *family, const char *src_url,
                         const char *base_url);

G_END_DECLS

#endif
