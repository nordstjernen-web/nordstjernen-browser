/* Nordstjernen — inline MPEG-1 video decoding (pl_mpeg).
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "video_decode.h"

#include <stdio.h>
#include <string.h>

#include "pl_mpeg.h"

struct ns_video_player {
    plm_t      *plm;
    int         width;
    int         height;
    double      duration;
    ns_texture *cur_tex;
    double      cur_time;
    ns_texture *pending_tex;
    double      pending_time;
};

gboolean
ns_video_decode_probe(const guint8 *bytes, gsize len)
{
    if (!bytes || len < 8) return FALSE;
    return bytes[0] == 0x00 && bytes[1] == 0x00 && bytes[2] == 0x01 &&
           (bytes[3] == 0xBA || bytes[3] == 0xB3);
}

ns_video_player *
ns_video_player_new(const guint8 *bytes, gsize len)
{
    if (!ns_video_decode_probe(bytes, len)) return NULL;

    guint8 *copy = g_malloc(len);
    memcpy(copy, bytes, len);
    plm_t *plm = plm_create_with_memory(copy, len, TRUE);
    if (!plm) { g_free(copy); return NULL; }

    plm_set_audio_enabled(plm, FALSE);
    plm_set_loop(plm, FALSE);

    int w = plm_get_width(plm);
    int h = plm_get_height(plm);
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
        plm_destroy(plm);
        return NULL;
    }

    ns_video_player *player = g_new0(ns_video_player, 1);
    player->plm = plm;
    player->width = w;
    player->height = h;
    player->duration = plm_get_duration(plm);
    player->cur_time = -1.0;
    player->pending_time = -1.0;
    return player;
}

void
ns_video_player_free(ns_video_player *player)
{
    if (!player) return;
    ns_texture_unref(player->cur_tex);
    ns_texture_unref(player->pending_tex);
    if (player->plm) plm_destroy(player->plm);
    g_free(player);
}

int
ns_video_player_width(const ns_video_player *player)
{
    return player ? player->width : 0;
}

int
ns_video_player_height(const ns_video_player *player)
{
    return player ? player->height : 0;
}

double
ns_video_player_duration(const ns_video_player *player)
{
    return player ? player->duration : 0.0;
}

ns_texture *
ns_video_player_current(ns_video_player *player)
{
    return player ? player->cur_tex : NULL;
}

static ns_texture *
ns_video_frame_to_texture(ns_video_player *player, plm_frame_t *frame)
{
    int w = player->width, h = player->height;
    gsize stride = (gsize)w * 4;
    gsize buf_len = stride * (gsize)h;
    guint8 *bgra = g_malloc(buf_len);
    memset(bgra, 0xFF, buf_len);
    plm_frame_to_bgra(frame, bgra, (int)stride);

    GBytes *bytes = g_bytes_new_take(bgra, buf_len);
    ns_texture *tex = ns_texture_new(w, h, NS_TEXTURE_BGRA_PREMULTIPLIED,
                                     bytes, stride);
    g_bytes_unref(bytes);
    return tex;
}

ns_texture *
ns_video_player_frame_at(ns_video_player *player, double seconds,
                         gboolean loop, gboolean *out_ended)
{
    if (out_ended) *out_ended = FALSE;
    if (!player || !player->plm) return NULL;
    if (seconds < 0) seconds = 0;

    if (seconds + 1e-4 < player->cur_time) {
        plm_rewind(player->plm);
        player->cur_time = -1.0;
        ns_texture_clear(&player->pending_tex);
        player->pending_time = -1.0;
    }

    gboolean changed = FALSE;
    for (;;) {
        if (player->pending_time >= 0.0) {
            if (player->pending_time <= seconds || player->cur_time < 0.0) {
                ns_texture_unref(player->cur_tex);
                player->cur_tex = player->pending_tex;
                player->cur_time = player->pending_time;
                player->pending_tex = NULL;
                player->pending_time = -1.0;
                changed = TRUE;
                continue;
            }
            break;
        }

        plm_frame_t *frame = plm_decode_video(player->plm);
        if (!frame) {
            if (!loop && plm_has_ended(player->plm) && out_ended)
                *out_ended = TRUE;
            break;
        }
        player->pending_tex = ns_video_frame_to_texture(player, frame);
        player->pending_time = frame->time;
    }

    return changed ? player->cur_tex : NULL;
}
