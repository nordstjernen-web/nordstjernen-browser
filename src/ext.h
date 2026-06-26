/* Nordstjernen — WebExtensions loader and content-script host.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_EXT_H
#define NS_EXT_H

#include <glib.h>
#include <quickjs.h>

G_BEGIN_DECLS

void   ns_ext_install(JSContext *ctx, JSValueConst global);
char  *ns_ext_content_scripts_for_url(const char *url, gboolean at_start);
guint  ns_ext_count(void);

G_END_DECLS

#endif
