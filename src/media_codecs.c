/* Nordstjernen - native open-media decoder availability for the mobile build.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "media_codecs.h"

#if defined(NS_HAVE_DAV1D)
#include <dav1d/dav1d.h>
#endif
#if defined(NS_HAVE_VPX)
#include <vpx/vpx_codec.h>
#endif
#if defined(NS_HAVE_OPUS)
#include <opus.h>
#endif
#if defined(NS_HAVE_VORBIS)
#include <vorbis/codec.h>
#endif

gboolean
ns_media_codec_have(ns_media_codec codec)
{
    switch (codec) {
    case NS_MEDIA_CODEC_AV1:
#if defined(NS_HAVE_DAV1D)
        return TRUE;
#else
        return FALSE;
#endif
    case NS_MEDIA_CODEC_VP8:
    case NS_MEDIA_CODEC_VP9:
#if defined(NS_HAVE_VPX)
        return TRUE;
#else
        return FALSE;
#endif
    case NS_MEDIA_CODEC_OPUS:
#if defined(NS_HAVE_OPUS)
        return TRUE;
#else
        return FALSE;
#endif
    case NS_MEDIA_CODEC_VORBIS:
#if defined(NS_HAVE_VORBIS)
        return TRUE;
#else
        return FALSE;
#endif
    }
    return FALSE;
}

const char *
ns_media_codec_backend(ns_media_codec codec)
{
    switch (codec) {
    case NS_MEDIA_CODEC_AV1:
#if defined(NS_HAVE_DAV1D)
        return dav1d_version();
#else
        return NULL;
#endif
    case NS_MEDIA_CODEC_VP8:
    case NS_MEDIA_CODEC_VP9:
#if defined(NS_HAVE_VPX)
        return vpx_codec_version_str();
#else
        return NULL;
#endif
    case NS_MEDIA_CODEC_OPUS:
#if defined(NS_HAVE_OPUS)
        return opus_get_version_string();
#else
        return NULL;
#endif
    case NS_MEDIA_CODEC_VORBIS:
#if defined(NS_HAVE_VORBIS)
        return vorbis_version_string();
#else
        return NULL;
#endif
    }
    return NULL;
}
