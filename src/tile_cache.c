/* Nordstjernen - retained article paint tile cache. */

#include "tile_cache.h"

#include <math.h>
#include <string.h>

#include "paint.h"
#include "selection.h"

#define NS_TILE_CACHE_HEIGHT 1024
#define NS_TILE_CACHE_MAX_TILES 24
#define NS_TILE_CACHE_MAX_BYTES ((gsize)160 * 1024 * 1024)
#define NS_TILE_CACHE_MAX_WIDTH 8192

typedef struct ns_tile_cache_tile {
    cairo_surface_t *surface;
    gint64 last_used_us;
    gsize bytes;
} ns_tile_cache_tile;

struct ns_tile_cache {
    GHashTable *tiles;
    const ns_box *root;
    char *highlight_query;
    const void *state_token;
    guint state_flags;
    int width;
    gsize bytes;
    guint last_hits;
    guint last_misses;
};

static gpointer
ns_tile_cache_key(int tile)
{
    return GINT_TO_POINTER(tile + 1);
}

static void
ns_tile_cache_tile_free(gpointer data)
{
    ns_tile_cache_tile *tile = data;
    if (!tile) return;
    if (tile->surface) cairo_surface_destroy(tile->surface);
    g_free(tile);
}

ns_tile_cache *
ns_tile_cache_new(void)
{
    ns_tile_cache *cache = g_new0(ns_tile_cache, 1);
    cache->tiles = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                         NULL, ns_tile_cache_tile_free);
    return cache;
}

void
ns_tile_cache_clear(ns_tile_cache *cache)
{
    if (!cache) return;
    if (cache->tiles) g_hash_table_remove_all(cache->tiles);
    g_clear_pointer(&cache->highlight_query, g_free);
    cache->root = NULL;
    cache->state_token = NULL;
    cache->state_flags = 0;
    cache->width = 0;
    cache->bytes = 0;
    cache->last_hits = 0;
    cache->last_misses = 0;
}

void
ns_tile_cache_free(ns_tile_cache *cache)
{
    if (!cache) return;
    ns_tile_cache_clear(cache);
    g_hash_table_destroy(cache->tiles);
    g_free(cache);
}

void
ns_tile_cache_invalidate_range(ns_tile_cache *cache, double top, double bottom)
{
    if (!cache || !cache->tiles) return;
    if (bottom <= top) return;
    top -= 64;
    bottom += 64;
    int first = (int)floor(top / NS_TILE_CACHE_HEIGHT);
    int last = (int)floor((bottom - 1) / NS_TILE_CACHE_HEIGHT);
    if (first < 0) first = 0;
    if (last < first) last = first;
    for (int i = first; i <= last; i++) {
        gpointer key = ns_tile_cache_key(i);
        ns_tile_cache_tile *tile = g_hash_table_lookup(cache->tiles, key);
        if (!tile) continue;
        if (cache->bytes >= tile->bytes) cache->bytes -= tile->bytes;
        else cache->bytes = 0;
        g_hash_table_remove(cache->tiles, key);
    }
}

static gboolean
ns_tile_cache_key_changed(ns_tile_cache *cache,
                          const ns_box *root,
                          const char *highlight_query,
                          const void *state_token,
                          guint state_flags,
                          int width)
{
    if (cache->root != root) return TRUE;
    if (cache->width != width) return TRUE;
    if (cache->state_token != state_token) return TRUE;
    if (cache->state_flags != state_flags) return TRUE;
    if (g_strcmp0(cache->highlight_query, highlight_query) != 0) return TRUE;
    return FALSE;
}

static void
ns_tile_cache_set_key(ns_tile_cache *cache,
                      const ns_box *root,
                      const char *highlight_query,
                      const void *state_token,
                      guint state_flags,
                      int width)
{
    cache->root = root;
    cache->width = width;
    cache->state_token = state_token;
    cache->state_flags = state_flags;
    g_free(cache->highlight_query);
    cache->highlight_query = g_strdup(highlight_query);
}

static void
ns_tile_cache_direct_paint(cairo_t *cr,
                           const ns_box *root,
                           const char *highlight_query,
                           const struct ns_selection *sel,
                           double x,
                           double y,
                           double w,
                           double h)
{
    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);
    ns_paint_with_selection(cr, root, highlight_query, sel);
    cairo_restore(cr);
}

static ns_tile_cache_tile *
ns_tile_cache_render_tile(const ns_box *root,
                          const char *highlight_query,
                          const struct ns_selection *sel,
                          int width,
                          int tile_index)
{
    if (width <= 0 || width > NS_TILE_CACHE_MAX_WIDTH) return NULL;
    cairo_surface_t *surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                   width, NS_TILE_CACHE_HEIGHT);
    if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface) cairo_surface_destroy(surface);
        return NULL;
    }
    cairo_t *tile_cr = cairo_create(surface);
    if (!tile_cr || cairo_status(tile_cr) != CAIRO_STATUS_SUCCESS) {
        if (tile_cr) cairo_destroy(tile_cr);
        cairo_surface_destroy(surface);
        return NULL;
    }
    double tile_y = (double)tile_index * NS_TILE_CACHE_HEIGHT;
    cairo_translate(tile_cr, 0, -tile_y);
    cairo_rectangle(tile_cr, 0, tile_y, width, NS_TILE_CACHE_HEIGHT);
    cairo_clip(tile_cr);
    ns_paint_with_selection(tile_cr, root, highlight_query, sel);
    cairo_destroy(tile_cr);
    cairo_surface_flush(surface);

    ns_tile_cache_tile *tile = g_new0(ns_tile_cache_tile, 1);
    tile->surface = surface;
    int stride = cairo_image_surface_get_stride(surface);
    tile->bytes = (gsize)stride * NS_TILE_CACHE_HEIGHT;
    return tile;
}

static void
ns_tile_cache_evict(ns_tile_cache *cache)
{
    while (g_hash_table_size(cache->tiles) > NS_TILE_CACHE_MAX_TILES ||
           cache->bytes > NS_TILE_CACHE_MAX_BYTES) {
        gpointer best_key = NULL;
        ns_tile_cache_tile *best = NULL;
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, cache->tiles);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            ns_tile_cache_tile *tile = value;
            if (!best || tile->last_used_us < best->last_used_us) {
                best = tile;
                best_key = key;
            }
        }
        if (!best_key || !best) break;
        if (cache->bytes >= best->bytes) cache->bytes -= best->bytes;
        else cache->bytes = 0;
        g_hash_table_remove(cache->tiles, best_key);
    }
}

void
ns_tile_cache_paint(ns_tile_cache *cache,
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
                    double clip_h)
{
    if (!root || !cr || clip_w <= 0 || clip_h <= 0) return;
    double x0 = floor(clip_x);
    double y0 = floor(clip_y);
    double x1 = ceil(clip_x + clip_w);
    double y1 = ceil(clip_y + clip_h);
    if (x1 <= x0 || y1 <= y0) return;
    if (!cache || target_width <= 0 || target_width > NS_TILE_CACHE_MAX_WIDTH) {
        ns_tile_cache_direct_paint(cr, root, highlight_query, sel,
                                   x0, y0, x1 - x0, y1 - y0);
        return;
    }
    if (ns_tile_cache_key_changed(cache, root, highlight_query,
                                  state_token, state_flags, target_width)) {
        ns_tile_cache_clear(cache);
        ns_tile_cache_set_key(cache, root, highlight_query,
                              state_token, state_flags, target_width);
    }

    cache->last_hits = 0;
    cache->last_misses = 0;
    int first = (int)floor(y0 / NS_TILE_CACHE_HEIGHT);
    int last = (int)floor((y1 - 1) / NS_TILE_CACHE_HEIGHT);
    if (first < 0) first = 0;
    if (last < first) last = first;

    cairo_save(cr);
    cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
    cairo_clip(cr);

    gint64 now_us = g_get_monotonic_time();
    for (int i = first; i <= last; i++) {
        gpointer key = ns_tile_cache_key(i);
        ns_tile_cache_tile *tile = g_hash_table_lookup(cache->tiles, key);
        if (tile) {
            cache->last_hits++;
        } else {
            cache->last_misses++;
            tile = ns_tile_cache_render_tile(root, highlight_query, sel,
                                             target_width, i);
            if (tile) {
                cache->bytes += tile->bytes;
                g_hash_table_insert(cache->tiles, key, tile);
            }
        }
        if (tile) {
            tile->last_used_us = now_us++;
            cairo_set_source_surface(cr, tile->surface, 0,
                                     (double)i * NS_TILE_CACHE_HEIGHT);
            cairo_paint(cr);
        } else {
            ns_tile_cache_direct_paint(cr, root, highlight_query, sel,
                                       0, (double)i * NS_TILE_CACHE_HEIGHT,
                                       target_width, NS_TILE_CACHE_HEIGHT);
        }
    }
    cairo_restore(cr);
    ns_tile_cache_evict(cache);
}

void
ns_tile_cache_stats_get(const ns_tile_cache *cache, ns_tile_cache_stats *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!cache) return;
    out->tiles = g_hash_table_size(cache->tiles);
    out->hits = cache->last_hits;
    out->misses = cache->last_misses;
    out->bytes = cache->bytes;
}
