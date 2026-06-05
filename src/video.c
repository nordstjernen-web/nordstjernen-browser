/* Nordstjernen — video poster cache for the external-player handoff.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "video.h"

#include <gio/gio.h>
#include <string.h>

#include "image.h"
#include "net.h"
#include "tab_worker.h"

struct nd_video_cache {
    GHashTable *by_url;
    GPtrArray  *pending;
    nd_tab_worker *worker;
};

typedef struct nd_pending {
    nd_video          *video;
    nd_video_cache    *cache;
    nd_video_ready_cb  cb;
    gpointer           user_data;
    gboolean           dead;
} nd_pending;

static void
nd_video_free(gpointer p)
{
    nd_video *v = p;
    if (!v) return;
    g_free(v->url);
    nd_texture_unref(v->poster_texture);
    g_free(v);
}

nd_video_cache *
nd_video_cache_new(void)
{
    nd_video_cache *c = g_new0(nd_video_cache, 1);
    c->by_url = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, nd_video_free);
    c->pending = g_ptr_array_new();
    return c;
}

void
nd_video_cache_set_worker(nd_video_cache *cache, nd_tab_worker *worker)
{
    if (cache) cache->worker = worker;
}

void
nd_video_cache_free(nd_video_cache *cache)
{
    if (!cache) return;
    for (guint i = 0; i < cache->pending->len; i++) {
        nd_pending *p = g_ptr_array_index(cache->pending, i);
        p->dead = TRUE;
    }
    g_hash_table_destroy(cache->by_url);
    g_ptr_array_free(cache->pending, TRUE);
    g_free(cache);
}

static void
on_poster_decoded(nd_tab_image_result *decoded, gpointer user_data)
{
    nd_pending *pending = user_data;
    if (pending->dead) {
        nd_tab_image_result_free(decoded);
        g_free(pending);
        return;
    }
    if (decoded && decoded->pixels) {
        GBytes *bytes = g_bytes_new_take(decoded->pixels,
                                         decoded->pixels_len);
        decoded->pixels = NULL;
        nd_texture *tex = nd_texture_new(decoded->width, decoded->height,
                                         decoded->format, bytes,
                                         decoded->stride);
        g_bytes_unref(bytes);
        if (tex) pending->video->poster_texture = tex;
    }
    if (!pending->video->poster_texture && decoded && decoded->resp &&
        decoded->resp->body && decoded->resp->body->len > 0) {
        int w = 0, h = 0;
        nd_texture *tex = nd_image_decode_bytes(decoded->resp->body->data,
                                                decoded->resp->body->len,
                                                &w, &h);
        if (tex) {
            pending->video->poster_texture = tex;
            if (decoded->width <= 0) decoded->width = w;
            if (decoded->height <= 0) decoded->height = h;
        }
    }
    if (decoded && pending->video->poster_texture) {
        if (pending->video->natural_width <= 0)
            pending->video->natural_width = decoded->width;
        if (pending->video->natural_height <= 0)
            pending->video->natural_height = decoded->height;
    }
    nd_tab_image_result_free(decoded);
    if (pending->cb) pending->cb(pending->video, pending->user_data);
    g_ptr_array_remove_fast(pending->cache->pending, pending);
    g_free(pending);
}

static void
on_poster_fetched(GObject *src, GAsyncResult *result, gpointer user_data)
{
    (void)src;
    nd_pending *pending = user_data;
    GError *err = NULL;
    nd_response *resp = nd_net_fetch_finish(result, &err);
    if (pending->dead) {
        nd_response_free(resp);
        g_clear_error(&err);
        g_free(pending);
        return;
    }
    if (pending->cache->worker && resp &&
        nd_tab_worker_decode_image_response(pending->cache->worker, resp,
                                            on_poster_decoded, pending,
                                            g_free)) {
        g_clear_error(&err);
        return;
    }
    if (resp && !resp->error && resp->body && resp->body->len > 0) {
        int w = 0, h = 0;
        nd_texture *tex = nd_image_decode_bytes(resp->body->data,
                                                resp->body->len, &w, &h);
        if (tex) {
            pending->video->poster_texture = tex;
            if (pending->video->natural_width  <= 0) pending->video->natural_width  = w;
            if (pending->video->natural_height <= 0) pending->video->natural_height = h;
        }
    }
    g_clear_error(&err);
    nd_response_free(resp);
    if (pending->cb) pending->cb(pending->video, pending->user_data);
    g_ptr_array_remove_fast(pending->cache->pending, pending);
    g_free(pending);
}

nd_video *
nd_video_cache_get(nd_video_cache *cache,
                   const char *url,
                   const char *poster_url,
                   const char *top_url,
                   nd_video_ready_cb cb,
                   gpointer user_data)
{
    if (!cache || !url) return NULL;
    nd_video *cached = g_hash_table_lookup(cache->by_url, url);
    if (cached) return cached;

    nd_video *v = g_new0(nd_video, 1);
    v->url = g_strdup(url);
    v->loaded = TRUE;
    g_hash_table_insert(cache->by_url, g_strdup(url), v);

    if (poster_url && *poster_url) {
        nd_pending *pp = g_new0(nd_pending, 1);
        pp->video = v;
        pp->cache = cache;
        pp->cb = cb;
        pp->user_data = user_data;
        g_ptr_array_add(cache->pending, pp);
        nd_net_fetch_async(poster_url, top_url, NULL, on_poster_fetched, pp);
    }
    return v;
}
