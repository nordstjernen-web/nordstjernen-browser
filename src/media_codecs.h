/* Nordstjernen - query which native open-media decoders are compiled in.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_MEDIA_CODECS_H
#define NS_MEDIA_CODECS_H

#include <glib.h>

typedef enum {
    NS_MEDIA_CODEC_AV1,
    NS_MEDIA_CODEC_VP8,
    NS_MEDIA_CODEC_VP9,
    NS_MEDIA_CODEC_OPUS,
    NS_MEDIA_CODEC_VORBIS,
} ns_media_codec;

gboolean ns_media_codec_have(ns_media_codec codec);
const char *ns_media_codec_backend(ns_media_codec codec);

#endif
