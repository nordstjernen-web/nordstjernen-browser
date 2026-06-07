/* Nordstjernen - retained article paint tile cache. */

#ifndef NS_TILE_CACHE_H
#define NS_TILE_CACHE_H

#include <cairo.h>
#include <glib.h>

#include "layout.h"

G_BEGIN_DECLS

struct ns_selection;

typedef struct ns_tile_cache ns_tile_cache;

typedef struct ns_tile_cache_stats {
    guint tiles;
    guint hits;
    guint misses;
    gsize bytes;
} ns_tile_cache_stats;

ns_tile_cache *ns_tile_cache_new(void);
void           ns_tile_cache_free(ns_tile_cache *cache);
void           ns_tile_cache_clear(ns_tile_cache *cache);
void           ns_tile_cache_invalidate_range(ns_tile_cache *cache,
                                              double top,
                                              double bottom);
void           ns_tile_cache_paint(ns_tile_cache *cache,
                                   cairo_t *cr,
                                   const ns_box *root,
                                   const char *highlight_query,
                                   const struct ns_selection *sel,
                                   const void *state_token,
                                   guint state_flags,
                                   int target_width,
                                   double clip_x,
                                   double clip_y,
                                   double clip_w,
                                   double clip_h);
guint          ns_tile_cache_prewarm(ns_tile_cache *cache,
                                     const ns_box *root,
                                     const char *highlight_query,
                                     const void *state_token,
                                     guint state_flags,
                                     int target_width,
                                     double clip_y,
                                     double clip_h,
                                     guint max_tiles);
void           ns_tile_cache_stats_get(const ns_tile_cache *cache,
                                       ns_tile_cache_stats *out);

G_END_DECLS

#endif
