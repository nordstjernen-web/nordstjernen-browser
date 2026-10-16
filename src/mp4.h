/* Nordstjernen — minimal MP4 / fragmented-MP4 demuxer for builds without libav.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-or-later
 */

#ifndef NS_MP4_H
#define NS_MP4_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t offset;
    uint32_t size;
    int64_t  dts;
    int64_t  pts;
    uint32_t duration;
    int      key;
} ns_mp4_sample;

typedef struct {
    uint32_t       id;
    char           kind;
    char           codec[5];
    uint32_t       timescale;
    int64_t        pts_shift;
    uint8_t       *config;
    size_t         config_len;
    int            width;
    int            height;
    int            rate;
    int            channels;
    ns_mp4_sample *samples;
    size_t         count;
    size_t         cap;
    int64_t        next_dts;
    uint32_t       trex_duration;
    uint32_t       trex_size;
    uint32_t       trex_flags;
} ns_mp4_track;

typedef struct {
    ns_mp4_track *tracks;
    int           ntracks;
    uint32_t      movie_timescale;
    uint64_t      movie_duration;
    uint64_t      fragment_duration;
    size_t        pos;
    int           have_moov;
} ns_mp4;

int  ns_mp4_bytes_are_mp4(const uint8_t *data, size_t len);
void ns_mp4_init(ns_mp4 *m);
int  ns_mp4_parse(ns_mp4 *m, const uint8_t *data, size_t len);
void ns_mp4_clear(ns_mp4 *m);
ns_mp4_track *ns_mp4_track_of(ns_mp4 *m, char kind);
double ns_mp4_duration(const ns_mp4 *m);
double ns_mp4_sample_time(const ns_mp4_track *t, const ns_mp4_sample *s);
int  ns_mp4_time_range(ns_mp4 *m, double *out_start, double *out_end);

#endif
