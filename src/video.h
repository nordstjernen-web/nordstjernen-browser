/* Nordstjernen — video poster cache for the external-player handoff.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_VIDEO_H
#define NS_VIDEO_H

#include <glib.h>

#include "texture.h"

G_BEGIN_DECLS

typedef struct ns_video {
    char        *url;
    int          natural_width;
    int          natural_height;
    ns_texture  *poster_texture;
    gboolean     loaded;
    gboolean     failed;
} ns_video;

typedef struct ns_video_cache ns_video_cache;
typedef struct ns_tab_worker  ns_tab_worker;
typedef void (*ns_video_ready_cb)(ns_video *v, gpointer user_data);

ns_video_cache *ns_video_cache_new(void);
void            ns_video_cache_free(ns_video_cache *cache);
void            ns_video_cache_set_worker(ns_video_cache *cache,
                                          ns_tab_worker *worker);

ns_video *ns_video_cache_get(ns_video_cache *cache,
                             const char *url,
                             const char *poster_url,
                             const char *top_url,
                             ns_video_ready_cb cb,
                             gpointer user_data);

G_END_DECLS

#endif
