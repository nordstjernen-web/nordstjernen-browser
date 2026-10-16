/* Nordstjernen — MP4 playback over the in-tree demuxer and the OS H.264 decoder, for builds without libav.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-or-later
 */

#include "video_mp4.h"

#include <stdlib.h>
#include <string.h>

#include "debuglog.h"
#include "media_os.h"
#include "mp4.h"

struct ns_mp4_player {
    guint8     *data;
    gsize       len;
    gsize       cap;
    ns_mp4      mp4;
    int         vtrack;
    ns_os_vdec *dec;
    gsize       next;
    gboolean    draining;
    double      skip_before;
};

static ns_mp4_track *
player_track(ns_mp4_player *p)
{
    return &p->mp4.tracks[p->vtrack];
}

static double
track_end(const ns_mp4_track *t)
{
    double end = 0.0;
    for (gsize i = 0; i < t->count; i++) {
        double e = ns_mp4_sample_time(t, &t->samples[i]) +
                   (t->timescale ? (double)t->samples[i].duration / t->timescale : 0.0);
        if (e > end) end = e;
    }
    return end;
}

ns_mp4_player *
ns_mp4_player_new(const guint8 *bytes, gsize len, int *out_w, int *out_h,
                  double *out_duration, double *out_end, int *out_has_audio)
{
    if (!ns_mp4_bytes_are_mp4(bytes, len) || !ns_os_h264_available())
        return NULL;
    ns_mp4_player *p = g_new0(ns_mp4_player, 1);
    p->data = g_try_malloc(len);
    if (!p->data) {
        g_free(p);
        return NULL;
    }
    memcpy(p->data, bytes, len);
    p->len = p->cap = len;
    p->skip_before = -1.0;
    ns_mp4_init(&p->mp4);
    ns_mp4_parse(&p->mp4, p->data, p->len);
    ns_mp4_track *v = ns_mp4_track_of(&p->mp4, 'v');
    if (!v || (strcmp(v->codec, "avc1") != 0 && strcmp(v->codec, "avc3") != 0) ||
        v->width <= 0 || v->height <= 0 || v->width > 16384 || v->height > 16384) {
        ns_mp4_player_free(p);
        return NULL;
    }
    p->vtrack = (int)(v - p->mp4.tracks);
    p->dec = ns_os_vdec_new_h264(v->config, v->config_len, v->width, v->height);
    if (!p->dec) {
        ns_mp4_player_free(p);
        return NULL;
    }
    ns_debug_log_emit(NS_DLOG_INFO, "media", "H.264 %dx%d decoding through %s",
                      v->width, v->height, ns_os_media_backend());
    double end = track_end(v);
    double dur = ns_mp4_duration(&p->mp4);
    *out_w = v->width;
    *out_h = v->height;
    *out_duration = dur > 0.0 ? dur : end;
    *out_end = end;
    *out_has_audio = ns_mp4_track_of(&p->mp4, 'a') != NULL;
    return p;
}

void
ns_mp4_player_free(ns_mp4_player *p)
{
    if (!p) return;
    if (p->dec) ns_os_vdec_free(p->dec);
    ns_mp4_clear(&p->mp4);
    g_free(p->data);
    g_free(p);
}

static ns_texture *
frame_texture(ns_os_frame *f)
{
    guint8 *bgra = NULL;
    int stride = 0;
    if (!ns_os_frame_to_bgra(f, &bgra, &stride)) return NULL;
    GBytes *gb = g_bytes_new_with_free_func(bgra, (gsize)stride * (gsize)f->height,
                                            free, bgra);
    ns_texture *tex = ns_texture_new(f->width, f->height,
                                     NS_TEXTURE_BGRA_PREMULTIPLIED, gb,
                                     (gsize)stride);
    g_bytes_unref(gb);
    return tex;
}

gboolean
ns_mp4_player_next(ns_mp4_player *p, ns_texture **out_tex, double *out_pts)
{
    int refused = 0;
    for (;;) {
        ns_os_frame f;
        ns_os_status st = ns_os_vdec_receive(p->dec, &f);
        if (st == NS_OS_FRAME) {
            double t = (double)f.pts_us / 1e6;
            if (p->skip_before >= 0.0 && t < p->skip_before - 0.05) {
                ns_os_frame_clear(&f);
                continue;
            }
            p->skip_before = -1.0;
            *out_tex = frame_texture(&f);
            *out_pts = t;
            ns_os_frame_clear(&f);
            return TRUE;
        }
        if (st == NS_OS_ERROR) return FALSE;
        ns_mp4_track *v = player_track(p);
        if (p->next < v->count) {
            ns_mp4_sample *s = &v->samples[p->next];
            if (s->offset + s->size <= p->len) {
                int r = ns_os_vdec_send(p->dec, p->data + s->offset, s->size,
                                        (gint64)(ns_mp4_sample_time(v, s) * 1e6));
                if (r < 0) {
                    if (++refused > 64) return FALSE;
                    continue;
                }
                refused = 0;
                p->next++;
                p->draining = FALSE;
                continue;
            }
        }
        if (p->draining) return FALSE;
        ns_os_vdec_drain(p->dec);
        p->draining = TRUE;
    }
}

gboolean
ns_mp4_player_seek(ns_mp4_player *p, double seconds)
{
    ns_mp4_track *v = player_track(p);
    gsize best = v->count;
    for (gsize i = 0; i < v->count; i++) {
        if (!v->samples[i].key) continue;
        if (ns_mp4_sample_time(v, &v->samples[i]) <= seconds + 1e-3 || best == v->count)
            best = i;
        else
            break;
    }
    if (best == v->count) return FALSE;
    ns_os_vdec_flush(p->dec);
    p->next = best;
    p->draining = FALSE;
    p->skip_before = seconds > 0.0 ? seconds : -1.0;
    return TRUE;
}

void
ns_mp4_player_skip_before(ns_mp4_player *p, double seconds)
{
    p->skip_before = seconds;
}

gboolean
ns_mp4_player_extend(ns_mp4_player *p, const guint8 *bytes, gsize len,
                     double *out_end)
{
    if (len < p->len) return FALSE;
    if (len == p->len) return TRUE;
    gsize head = MIN(p->len, (gsize)4096);
    if (memcmp(bytes, p->data, head) != 0) return FALSE;
    if (len > p->cap) {
        gsize cap = p->cap ? p->cap : len;
        while (cap < len) cap = cap > G_MAXSIZE / 2 ? len : cap * 2;
        guint8 *grown = g_try_realloc(p->data, cap);
        if (!grown) return FALSE;
        p->data = grown;
        p->cap = cap;
    }
    memcpy(p->data + p->len, bytes + p->len, len - p->len);
    p->len = len;
    ns_mp4_parse(&p->mp4, p->data, p->len);
    if (p->draining) {
        ns_os_vdec_flush(p->dec);
        p->draining = FALSE;
    }
    if (out_end) *out_end = track_end(player_track(p));
    return TRUE;
}

gboolean
ns_mp4_probe_chunk_range(const guint8 *init, gsize init_len,
                         const guint8 *chunk, gsize chunk_len,
                         double *out_start, double *out_end)
{
    if (!chunk || !chunk_len) return FALSE;
    guint8 *joined = g_try_malloc(init_len + chunk_len);
    if (!joined) return FALSE;
    if (init_len) memcpy(joined, init, init_len);
    memcpy(joined + init_len, chunk, chunk_len);
    ns_mp4 m;
    ns_mp4_init(&m);
    ns_mp4_parse(&m, joined, init_len + chunk_len);
    gboolean ok = ns_mp4_time_range(&m, out_start, out_end);
    ns_mp4_clear(&m);
    g_free(joined);
    return ok;
}
