/* Nordstjernen - probe a WebM/Matroska buffer's tracks via the vendored nestegg demuxer.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_WEBM_DEMUX_H
#define NS_WEBM_DEMUX_H

#include <stddef.h>

typedef enum {
    NS_WEBM_CODEC_NONE = 0,
    NS_WEBM_CODEC_VP8,
    NS_WEBM_CODEC_VP9,
    NS_WEBM_CODEC_AV1,
    NS_WEBM_CODEC_OPUS,
    NS_WEBM_CODEC_VORBIS,
} ns_webm_codec;

typedef struct {
    ns_webm_codec video;
    ns_webm_codec audio;
} ns_webm_info;

int ns_webm_probe(const unsigned char *data, size_t len, ns_webm_info *out);

#endif
