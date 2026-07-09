/* Nordstjernen - probe a WebM/Matroska buffer's tracks via the vendored nestegg demuxer.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "webm_demux.h"

#include <string.h>

#include "nestegg/nestegg.h"

typedef struct { const unsigned char *data; size_t size; size_t pos; } ns_webm_mem;

static int64_t
ns_webm_read(void *buffer, size_t length, void *userdata)
{
    ns_webm_mem *m = userdata;
    size_t avail = m->size - m->pos;
    if (avail == 0) return 0;
    size_t want = length < avail ? length : avail;
    memcpy(buffer, m->data + m->pos, want);
    m->pos += want;
    return (int64_t)want;
}

static int
ns_webm_seek(int64_t offset, int whence, void *userdata)
{
    ns_webm_mem *m = userdata;
    int64_t base;
    if (whence == NESTEGG_SEEK_SET) base = 0;
    else if (whence == NESTEGG_SEEK_CUR) base = (int64_t)m->pos;
    else if (whence == NESTEGG_SEEK_END) base = (int64_t)m->size;
    else return -1;
    int64_t np = base + offset;
    if (np < 0 || np > (int64_t)m->size) return -1;
    m->pos = (size_t)np;
    return 0;
}

static int64_t
ns_webm_tell(void *userdata)
{
    ns_webm_mem *m = userdata;
    return (int64_t)m->pos;
}

static ns_webm_codec
ns_webm_map_codec(int id)
{
    switch (id) {
    case NESTEGG_CODEC_VP8:    return NS_WEBM_CODEC_VP8;
    case NESTEGG_CODEC_VP9:    return NS_WEBM_CODEC_VP9;
    case NESTEGG_CODEC_AV1:    return NS_WEBM_CODEC_AV1;
    case NESTEGG_CODEC_OPUS:   return NS_WEBM_CODEC_OPUS;
    case NESTEGG_CODEC_VORBIS: return NS_WEBM_CODEC_VORBIS;
    default:                   return NS_WEBM_CODEC_NONE;
    }
}

int
ns_webm_probe(const unsigned char *data, size_t len, ns_webm_info *out)
{
    if (!data || !out) return 0;
    ns_webm_info info;
    memset(&info, 0, sizeof info);

    ns_webm_mem mem = { data, len, 0 };
    nestegg_io io = { ns_webm_read, ns_webm_seek, ns_webm_tell, &mem };
    nestegg *ctx = NULL;
    if (nestegg_init(&ctx, io, NULL, -1) != 0 || !ctx) return 0;

    unsigned int tracks = 0;
    if (nestegg_track_count(ctx, &tracks) != 0) { nestegg_destroy(ctx); return 0; }

    for (unsigned int i = 0; i < tracks; i++) {
        int type = nestegg_track_type(ctx, i);
        ns_webm_codec c = ns_webm_map_codec(nestegg_track_codec_id(ctx, i));
        if (type == NESTEGG_TRACK_VIDEO && info.video == NS_WEBM_CODEC_NONE)
            info.video = c;
        else if (type == NESTEGG_TRACK_AUDIO && info.audio == NS_WEBM_CODEC_NONE)
            info.audio = c;
    }

    nestegg_destroy(ctx);
    *out = info;
    return info.video != NS_WEBM_CODEC_NONE || info.audio != NS_WEBM_CODEC_NONE;
}
