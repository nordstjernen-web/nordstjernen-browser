/* Nordstjernen — bookmarks storage API.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_BOOKMARKS_H
#define NS_BOOKMARKS_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct ns_bookmark {
    char *url;
    char *title;
} ns_bookmark;

typedef struct ns_bookmarks ns_bookmarks;

ns_bookmarks *ns_bookmarks_load(void);
void          ns_bookmarks_free(ns_bookmarks *bm);

guint    ns_bookmarks_count(const ns_bookmarks *bm);
const ns_bookmark *ns_bookmarks_get(const ns_bookmarks *bm, guint i);

gboolean ns_bookmarks_contains(const ns_bookmarks *bm, const char *url);
void     ns_bookmarks_add(ns_bookmarks *bm, const char *url, const char *title);
void     ns_bookmarks_remove(ns_bookmarks *bm, const char *url);

G_END_DECLS

#endif
