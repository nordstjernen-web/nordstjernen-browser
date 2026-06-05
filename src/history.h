/* Nordstjernen — SQLite-backed browsing history API.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef ND_HISTORY_H
#define ND_HISTORY_H

#include <glib.h>

G_BEGIN_DECLS

void   nd_history_init(void);
void   nd_history_shutdown(void);

void   nd_history_record(const char *url, const char *title);
void   nd_history_clear(void);

typedef struct nd_history_suggestion {
    char *url;
    char *title;
} nd_history_suggestion;

void   nd_history_suggestion_free(gpointer s);
GPtrArray *nd_history_suggest(const char *prefix, int limit);

char  *nd_history_html_page(void);

G_END_DECLS

#endif
