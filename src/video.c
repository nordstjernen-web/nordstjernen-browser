/* Nordstjernen — inline video playback and poster cache.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include <stdio.h>
#include "video.h"

#include <gio/gio.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>

#include "camera.h"
#include "dom.h"
#include "image.h"
#include "layout.h"
#include "net.h"
#include "video_decode.h"

#define NS_VIDEO_MAX_BYTES (256u * 1024u * 1024u)

struct ns_video_cache {
    GHashTable       *by_url;
    GHashTable       *requested;
    GPtrArray        *pending;
    char             *base_url;
    ns_video_js_cb    js_cb;
    gpointer          js_user;
    ns_video_audio_cb audio_cb;
    gpointer          audio_user;
    guint             next_token;
};

typedef struct ns_pending {
    ns_video       *video;
    ns_video_cache *cache;
    gboolean        dead;
} ns_pending;

static void
ns_video_free(gpointer p)
{
    ns_video *v = p;
    if (!v) return;
    g_free(v->url);
    g_free(v->token);
    ns_texture_unref(v->poster_texture);
    ns_texture_unref(v->frame_texture);
    ns_video_player_free(v->player);
    g_free(v);
}

ns_video_cache *
ns_video_cache_new(void)
{
    ns_video_cache *c = g_new0(ns_video_cache, 1);
    c->by_url = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, ns_video_free);
    c->requested = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    c->pending = g_ptr_array_new();
    return c;
}

void
ns_video_cache_set_base(ns_video_cache *cache, const char *base_url)
{
    if (!cache) return;
    g_free(cache->base_url);
    cache->base_url = g_strdup(base_url);
}

void
ns_video_cache_set_js_cb(ns_video_cache *cache, ns_video_js_cb cb, gpointer user_data)
{
    if (!cache) return;
    cache->js_cb = cb;
    cache->js_user = user_data;
}

void
ns_video_cache_set_audio_cb(ns_video_cache *cache, ns_video_audio_cb cb,
                            gpointer user_data)
{
    if (!cache) return;
    cache->audio_cb = cb;
    cache->audio_user = user_data;
}

static void
ns_video_emit_audio(ns_video_cache *cache, const char *fmt, ...) G_GNUC_PRINTF(2, 3);

static void
ns_video_emit_audio(ns_video_cache *cache, const char *fmt, ...)
{
    if (!cache || !cache->audio_cb) return;
    va_list ap;
    va_start(ap, fmt);
    char *cmd = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    cache->audio_cb(cmd, cache->audio_user);
    g_free(cmd);
}

static gboolean
ns_video_url_audio_safe(const char *url)
{
    if (!url || !*url) return FALSE;
    for (const unsigned char *p = (const unsigned char *)url; *p; p++)
        if (*p < 0x20 || *p == 0x7f) return FALSE;
    return TRUE;
}

static void
ns_video_audio_start(ns_video_cache *cache, ns_video *v)
{
    if (!cache || !cache->audio_cb || !v || v->muted || !v->has_audio) return;
    if (!ns_video_url_audio_safe(v->url)) return;
    if (!v->token)
        v->token = g_strdup_printf("nv%u", ++cache->next_token);
    if (!v->audio_opened) {
        ns_video_emit_audio(cache, "open %s %s", v->token, v->url);
        if (v->loop)
            ns_video_emit_audio(cache, "loop %s 1", v->token);
        v->audio_opened = TRUE;
    }
    ns_video_emit_audio(cache, "play %s", v->token);
}

static void
ns_video_audio_pause(ns_video_cache *cache, ns_video *v)
{
    if (!cache || !cache->audio_cb || !v || !v->audio_opened) return;
    ns_video_emit_audio(cache, "pause %s", v->token);
}

static void
ns_video_audio_stop(ns_video_cache *cache, ns_video *v)
{
    if (!cache || !cache->audio_cb || !v || !v->audio_opened) return;
    ns_video_emit_audio(cache, "stop %s", v->token);
    v->audio_opened = FALSE;
}

static void
ns_video_audio_resync(ns_video_cache *cache, ns_video *v)
{
    if (!cache || !cache->audio_cb || !v || !v->audio_opened) return;
    ns_video_emit_audio(cache, "seek %s 0", v->token);
}

void
ns_video_cache_free(ns_video_cache *cache)
{
    if (!cache) return;
    if (cache->audio_cb) {
        GHashTableIter it;
        gpointer key, val;
        g_hash_table_iter_init(&it, cache->by_url);
        while (g_hash_table_iter_next(&it, &key, &val))
            ns_video_audio_stop(cache, val);
    }
    for (guint i = 0; i < cache->pending->len; i++) {
        ns_pending *p = g_ptr_array_index(cache->pending, i);
        p->dead = TRUE;
    }
    g_hash_table_destroy(cache->by_url);
    g_hash_table_destroy(cache->requested);
    g_ptr_array_free(cache->pending, TRUE);
    g_free(cache->base_url);
    g_free(cache);
}

static void
ns_video_emit_js(ns_video_cache *cache, ns_video *v, const char *kind, double value)
{
    if (cache && cache->js_cb && v && v->dom_node)
        cache->js_cb(v->dom_node, kind, value, cache->js_user);
}

void
ns_video_play(ns_video *v, gint64 now_us)
{
    if (!v || !v->player) return;
    if (v->playing) return;
    if (v->ended) { v->cur_time = 0.0; v->ended = FALSE; }
    v->base_us = now_us - (gint64)(v->cur_time * 1e6);
    v->playing = TRUE;
}

void
ns_video_pause(ns_video *v, gint64 now_us)
{
    if (!v || !v->player || !v->playing) return;
    v->cur_time = (double)(now_us - v->base_us) / 1e6;
    v->playing = FALSE;
}

gboolean
ns_video_toggle(ns_video *v, gint64 now_us)
{
    if (!v || !v->player) return FALSE;
    if (v->playing) ns_video_pause(v, now_us);
    else ns_video_play(v, now_us);
    return TRUE;
}

gboolean
ns_video_cache_seek_node(ns_video_cache *cache, const void *dom_node,
                         double seconds, gint64 now_us)
{
    if (!cache || !dom_node) return FALSE;
    ns_video *v = NULL;
    GHashTableIter it;
    gpointer key, val;
    g_hash_table_iter_init(&it, cache->by_url);
    while (g_hash_table_iter_next(&it, &key, &val)) {
        ns_video *cand = val;
        if (cand->dom_node == dom_node) { v = cand; break; }
    }
    if (!v || !v->player || v->is_camera) return FALSE;

    double t = seconds;
    if (t < 0.0) t = 0.0;
    if (v->duration > 0.0 && t > v->duration) t = v->duration;
    v->cur_time = t;
    v->prev_tick_time = t;
    v->last_emit_time = t;
    v->ended = v->duration > 0.0 && t >= v->duration && !v->loop;
    if (v->playing)
        v->base_us = now_us - (gint64)(t * 1e6);

    gboolean ended = FALSE;
    ns_texture *frame = ns_video_player_frame_at(v->player, t, v->loop, &ended);
    if (frame) {
        ns_texture_unref(v->frame_texture);
        v->frame_texture = ns_texture_ref(frame);
    }
    if (v->audio_opened)
        ns_video_emit_audio(cache, "seek %s %.3f", v->token, t);
    ns_video_emit_js(cache, v, "pos", t);
    return TRUE;
}

gboolean
ns_video_cache_toggle(ns_video_cache *cache, ns_video *v, gint64 now_us)
{
    if (!v || !v->player) return FALSE;
    gboolean was_playing = v->playing;
    ns_video_toggle(v, now_us);
    ns_video_emit_js(cache, v, was_playing ? "pause" : "play", v->cur_time);
    if (was_playing) ns_video_audio_pause(cache, v);
    else ns_video_audio_start(cache, v);
    return TRUE;
}

static void
ns_video_build_player(ns_pending *pending, ns_response *resp)
{
    ns_video *v = pending->video;
    if (!resp || resp->error || !resp->body || resp->body->len == 0 ||
        resp->body->len > NS_VIDEO_MAX_BYTES)
        return;

    ns_video_player *player = ns_video_player_new(resp->body->data,
                                                  resp->body->len);
    if (!player) return;

    v->player = player;
    v->duration = ns_video_player_duration(player);
    v->has_audio = ns_video_player_has_audio(player);
    if (v->natural_width <= 0)  v->natural_width  = ns_video_player_width(player);
    if (v->natural_height <= 0) v->natural_height = ns_video_player_height(player);

    gboolean ended = FALSE;
    ns_texture *frame = ns_video_player_frame_at(player, 0.0, v->loop, &ended);
    if (frame) v->frame_texture = ns_texture_ref(frame);

    if (v->autoplay) {
        ns_video_play(v, g_get_monotonic_time());
        ns_video_audio_start(pending->cache, v);
    }
}

static void
on_video_fetched(GObject *src, GAsyncResult *result, gpointer user_data)
{
    (void)src;
    ns_pending *pending = user_data;
    GError *err = NULL;
    ns_response *resp = ns_net_fetch_finish(result, &err);
    if (pending->dead) {
        ns_response_free(resp);
        g_clear_error(&err);
        g_free(pending);
        return;
    }
    ns_video_build_player(pending, resp);
    g_clear_error(&err);
    ns_response_free(resp);
    pending->video->loaded = TRUE;
    g_ptr_array_remove_fast(pending->cache->pending, pending);
    g_free(pending);
}

static void
on_poster_fetched(GObject *src, GAsyncResult *result, gpointer user_data)
{
    (void)src;
    ns_pending *pending = user_data;
    GError *err = NULL;
    ns_response *resp = ns_net_fetch_finish(result, &err);
    if (pending->dead) {
        ns_response_free(resp);
        g_clear_error(&err);
        g_free(pending);
        return;
    }
    if (resp && !resp->error && resp->body && resp->body->len > 0) {
        int w = 0, h = 0;
        ns_texture *tex = ns_image_decode_bytes(resp->body->data,
                                                resp->body->len, &w, &h);
        if (tex) {
            pending->video->poster_texture = tex;
            if (pending->video->natural_width  <= 0) pending->video->natural_width  = w;
            if (pending->video->natural_height <= 0) pending->video->natural_height = h;
        }
    }
    g_clear_error(&err);
    ns_response_free(resp);
    g_ptr_array_remove_fast(pending->cache->pending, pending);
    g_free(pending);
}

static gboolean
attr_present(const ns_node *n, const char *name)
{
    const char *v = n ? ns_element_get_attr(n, name) : NULL;
    return v != NULL;
}

static gboolean url_is_inline_video(const char *url);

gboolean
ns_video_url_is_inline(const char *url)
{
    return url_is_inline_video(url);
}

static gboolean
url_is_inline_video(const char *url)
{
    if (!url) return FALSE;
    if (g_str_has_prefix(url, "blob:")) return TRUE;
    gsize n = strcspn(url, "?#");
    static const char *const exts[] = {
        ".mpg", ".mpeg", ".m1v", ".mpeg1", ".mpg1",
#ifdef NS_HAVE_LIBAV
        ".mp4", ".m4v", ".webm",
#endif
        NULL,
    };
    for (int i = 0; exts[i]; i++) {
        gsize el = strlen(exts[i]);
        if (n >= el && g_ascii_strncasecmp(url + n - el, exts[i], el) == 0)
            return TRUE;
    }
    return FALSE;
}

static void
ns_video_cache_start(ns_video_cache *cache, const ns_node *dom, ns_box *box,
                     const char *abs_url, const char *poster_abs)
{
    ns_video *v = g_hash_table_lookup(cache->by_url, abs_url);
    if (v) { if (box) box->media->video = v; return; }

    v = g_new0(ns_video, 1);
    v->url = g_strdup(abs_url);
    v->dom_node = dom;
    v->autoplay = attr_present(dom, "autoplay");
    v->loop = attr_present(dom, "loop");
    v->controls = attr_present(dom, "controls");
    v->muted = attr_present(dom, "muted");
    v->cur_time = 0.0;
    g_hash_table_insert(cache->by_url, g_strdup(abs_url), v);
    if (box) box->media->video = v;

    ns_pending *pv = g_new0(ns_pending, 1);
    pv->video = v;
    pv->cache = cache;
    g_ptr_array_add(cache->pending, pv);
    ns_net_fetch_async(abs_url, cache->base_url, NULL, on_video_fetched, pv);

    if (poster_abs && *poster_abs) {
        ns_pending *pp = g_new0(ns_pending, 1);
        pp->video = v;
        pp->cache = cache;
        g_ptr_array_add(cache->pending, pp);
        ns_net_fetch_async(poster_abs, cache->base_url, NULL, on_poster_fetched, pp);
    }
}

static void
ns_video_discover_dom(ns_video_cache *cache, const ns_node *node)
{
    if (!node) return;
    if (node->kind == NS_NODE_ELEMENT && node->name &&
        strcmp(node->name, "video") == 0) {
        const char *src = ns_element_get_attr(node, "src");
        if (!src || !*src) src = ns_element_get_attr(node, NS_MEDIA_SRC_ATTR);
        if (src && *src) {
            char *abs = g_str_has_prefix(src, "blob:")
                ? g_strdup(src)
                : ns_url_resolve(cache->base_url, src);
            if (abs && url_is_inline_video(abs) &&
                (g_str_has_prefix(abs, "http://") ||
                 g_str_has_prefix(abs, "https://") ||
                 g_str_has_prefix(abs, "file://") ||
                 g_str_has_prefix(abs, "blob:")) &&
                !g_hash_table_contains(cache->requested, abs)) {
                g_hash_table_add(cache->requested, g_strdup(abs));
                const char *poster_raw = ns_element_get_attr(node, "poster");
                char *poster = (poster_raw && *poster_raw)
                    ? ns_url_resolve(cache->base_url, poster_raw) : NULL;
                ns_video_cache_start(cache, node, NULL, abs, poster);
                g_free(poster);
            }
            g_free(abs);
        }
    }
    for (const ns_node *c = node->first_child; c; c = c->next_sibling)
        ns_video_discover_dom(cache, c);
}

void
ns_video_cache_discover(ns_video_cache *cache, const ns_box *root,
                        const ns_node *doc, gint64 now_us)
{
    (void)now_us;
    if (!cache || !root) return;

    GPtrArray *vids = g_ptr_array_new();
    ns_layout_collect_videos(root, vids);
    for (guint i = 0; i < vids->len; i++) {
        ns_box *box = g_ptr_array_index(vids, i);
        if (!box->media || !box->dom) continue;
        if (box->media->video) continue;
        const char *stream = ns_element_get_attr(box->dom, NS_MEDIA_STREAM_ATTR);
        if (stream && g_strcmp0(stream, "camera") == 0) {
            char *ckey = g_strdup_printf("camera:%p", (const void *)box->dom);
            ns_video *cv = g_hash_table_lookup(cache->by_url, ckey);
            if (!cv) {
                cv = g_new0(ns_video, 1);
                cv->is_camera = TRUE;
                cv->dom_node = box->dom;
                cv->playing = TRUE;
                g_hash_table_insert(cache->by_url, g_strdup(ckey), cv);
            }
            box->media->video = cv;
            g_free(ckey);
            continue;
        }
        const char *src = box->media->video_src;
        if (!src || !*src) continue;
        char *abs = g_str_has_prefix(src, "blob:") ? g_strdup(src)
                                                   : ns_url_resolve(cache->base_url, src);
        if (!abs) continue;
        if ((!g_str_has_prefix(abs, "http://") && !g_str_has_prefix(abs, "https://") &&
             !g_str_has_prefix(abs, "file://") && !g_str_has_prefix(abs, "blob:")) ||
            !url_is_inline_video(abs)) {
            g_free(abs);
            continue;
        }
        char *poster = NULL;
        if (box->media->video_poster)
            poster = ns_url_resolve(cache->base_url, box->media->video_poster);

        ns_video *existing = g_hash_table_lookup(cache->by_url, abs);
        if (existing) {
            box->media->video = existing;
        } else if (!g_hash_table_contains(cache->requested, abs)) {
            g_hash_table_add(cache->requested, g_strdup(abs));
            ns_video_cache_start(cache, box->dom, box, abs, poster);
        }
        g_free(poster);
        g_free(abs);
    }
    g_ptr_array_free(vids, TRUE);

    if (doc)
        ns_video_discover_dom(cache, doc);
}

gboolean
ns_video_cache_tick(ns_video_cache *cache, gint64 now_us)
{
    if (!cache) return FALSE;
    gboolean changed = FALSE;
    GHashTableIter it;
    gpointer key, val;
    g_hash_table_iter_init(&it, cache->by_url);
    while (g_hash_table_iter_next(&it, &key, &val)) {
        ns_video *v = val;
        if (v->is_camera) {
            ns_camera *cam = ns_camera_active();
            if (cam) {
                ns_texture *frame = ns_camera_next_frame(cam);
                if (frame) {
                    ns_texture_unref(v->frame_texture);
                    v->frame_texture = frame;
                    v->natural_width  = ns_texture_get_width(frame);
                    v->natural_height = ns_texture_get_height(frame);
                    changed = TRUE;
                }
            }
            continue;
        }
        if (!v->player) continue;

        if (!v->meta_sent && v->duration > 0.0) {
            v->meta_sent = TRUE;
            ns_video_emit_js(cache, v, "meta", v->duration);
            if (v->natural_width > 0)
                ns_video_emit_js(cache, v, "vwidth", (double)v->natural_width);
            if (v->natural_height > 0)
                ns_video_emit_js(cache, v, "vheight", (double)v->natural_height);
        }
        if (!v->playing) continue;

        double elapsed = (double)(now_us - v->base_us) / 1e6;
        double t = elapsed;
        if (v->loop && v->duration > 0.0)
            t = fmod(elapsed, v->duration);

        if (v->loop && t + 1e-3 < v->prev_tick_time)
            ns_video_audio_resync(cache, v);
        v->prev_tick_time = t;

        gboolean ended = FALSE;
        ns_texture *frame = ns_video_player_frame_at(v->player, t, v->loop, &ended);
        if (frame) {
            ns_texture_unref(v->frame_texture);
            v->frame_texture = ns_texture_ref(frame);
            changed = TRUE;
        }
        v->cur_time = t;
        if (t - v->last_emit_time >= 0.20 || ended) {
            v->last_emit_time = t;
            ns_video_emit_js(cache, v, "pos", v->cur_time);
        }
        if (ended) {
            v->playing = FALSE;
            v->ended = TRUE;
            v->cur_time = v->duration;
            ns_video_audio_stop(cache, v);
            ns_video_emit_js(cache, v, "ended", v->duration);
        }
    }
    return changed;
}

gboolean
ns_video_cache_has_pending(const ns_video_cache *cache)
{
    return cache && cache->pending->len > 0;
}

gboolean
ns_video_cache_animating(const ns_video_cache *cache)
{
    if (!cache) return FALSE;
    if (cache->pending->len > 0) return TRUE;
    GHashTableIter it;
    gpointer key, val;
    g_hash_table_iter_init(&it, cache->by_url);
    while (g_hash_table_iter_next(&it, &key, &val)) {
        ns_video *v = val;
        if (v->is_camera && ns_camera_active()) return TRUE;
        if (v->player && v->playing) return TRUE;
    }
    return FALSE;
}
