/* Nordstjernen — H.264 and AAC decoding through Android MediaCodec (NDK).
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-or-later
 */

#include "media_os.h"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define NS_MC_COLOR_I420  19
#define NS_MC_COLOR_NV12  21
#define NS_MC_FLAG_CONFIG 2
#define NS_MC_FLAG_EOS    4
#define NS_MC_PCM_FLOAT   4

struct ns_os_vdec {
    AMediaCodec *codec;
    int          nal_len_size;
    int          width;
    int          height;
    int          stride;
    int          slice_height;
    int          color_format;
    int          crop_left;
    int          crop_top;
    int          crop_right;
    int          crop_bottom;
    int          draining;
};

struct ns_os_adec {
    AMediaCodec *codec;
    int          rate;
    int          channels;
    int          pcm_float;
};

void
ns_os_media_preinit(void)
{
}

const char *
ns_os_media_backend(void)
{
    return "MediaCodec";
}

static int
mc_has(const char *mime, int *cache)
{
    if (*cache >= 0) return *cache;
    AMediaCodec *c = AMediaCodec_createDecoderByType(mime);
    *cache = c != NULL;
    if (c) AMediaCodec_delete(c);
    return *cache;
}

int
ns_os_h264_available(void)
{
    static int cache = -1;
    return mc_has("video/avc", &cache);
}

int
ns_os_aac_available(void)
{
    static int cache = -1;
    return mc_has("audio/mp4a-latm", &cache);
}

static int
avcc_split(const uint8_t *avcc, size_t len, int want_pps, uint8_t *out,
           size_t cap, size_t *out_len)
{
    size_t pos = 5, n = 0;
    for (int set = 0; set < 2; set++) {
        if (pos >= len) return 0;
        int count = set == 0 ? (avcc[pos] & 0x1f) : avcc[pos];
        pos++;
        for (int i = 0; i < count; i++) {
            if (pos + 2 > len) return 0;
            size_t sz = ((size_t)avcc[pos] << 8) | avcc[pos + 1];
            pos += 2;
            if (pos + sz > len) return 0;
            if (set == want_pps) {
                if (n + 4 + sz > cap) return 0;
                memcpy(out + n, "\0\0\0\1", 4);
                memcpy(out + n + 4, avcc + pos, sz);
                n += 4 + sz;
            }
            pos += sz;
        }
    }
    *out_len = n;
    return n > 0;
}

static void
vdec_read_format(ns_os_vdec *d)
{
    AMediaFormat *f = AMediaCodec_getOutputFormat(d->codec);
    if (!f) return;
    int32_t v;
    if (AMediaFormat_getInt32(f, "width", &v)) d->width = v;
    if (AMediaFormat_getInt32(f, "height", &v)) d->height = v;
    d->stride = AMediaFormat_getInt32(f, "stride", &v) && v > 0 ? v : d->width;
    d->slice_height = AMediaFormat_getInt32(f, "slice-height", &v) && v > 0
        ? v : d->height;
    if (AMediaFormat_getInt32(f, "color-format", &v)) d->color_format = v;
    d->crop_left = AMediaFormat_getInt32(f, "crop-left", &v) ? v : 0;
    d->crop_top = AMediaFormat_getInt32(f, "crop-top", &v) ? v : 0;
    d->crop_right = AMediaFormat_getInt32(f, "crop-right", &v) ? v : d->width - 1;
    d->crop_bottom = AMediaFormat_getInt32(f, "crop-bottom", &v) ? v : d->height - 1;
    AMediaFormat_delete(f);
}

ns_os_vdec *
ns_os_vdec_new_h264(const uint8_t *avcc, size_t avcc_len, int width, int height)
{
    uint8_t sps[512], pps[512];
    size_t sps_len = 0, pps_len = 0;
    if (!avcc || avcc_len < 7 || avcc[0] != 1 ||
        !avcc_split(avcc, avcc_len, 0, sps, sizeof sps, &sps_len) ||
        !avcc_split(avcc, avcc_len, 1, pps, sizeof pps, &pps_len))
        return NULL;
    ns_os_vdec *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->nal_len_size = (avcc[4] & 3) + 1;
    d->width = width;
    d->height = height;
    d->codec = AMediaCodec_createDecoderByType("video/avc");
    if (!d->codec) goto fail;
    AMediaFormat *f = AMediaFormat_new();
    AMediaFormat_setString(f, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_WIDTH, width);
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_HEIGHT, height);
    AMediaFormat_setBuffer(f, "csd-0", sps, sps_len);
    AMediaFormat_setBuffer(f, "csd-1", pps, pps_len);
    media_status_t st = AMediaCodec_configure(d->codec, f, NULL, NULL, 0);
    AMediaFormat_delete(f);
    if (st != AMEDIA_OK || AMediaCodec_start(d->codec) != AMEDIA_OK) goto fail;
    d->stride = width;
    d->slice_height = height;
    d->color_format = NS_MC_COLOR_I420;
    d->crop_right = width - 1;
    d->crop_bottom = height - 1;
    return d;
fail:
    ns_os_vdec_free(d);
    return NULL;
}

int
ns_os_vdec_send(ns_os_vdec *d, const uint8_t *data, size_t len, int64_t pts_us)
{
    if (!d || !d->codec || !data || !len) return 0;
    ssize_t idx = AMediaCodec_dequeueInputBuffer(d->codec, 10000);
    if (idx < 0) return -1;
    size_t cap = 0;
    uint8_t *buf = AMediaCodec_getInputBuffer(d->codec, (size_t)idx, &cap);
    uint8_t *annexb = NULL;
    size_t alen = 0;
    if (!buf || !ns_os_avcc_to_annexb(data, len, d->nal_len_size, &annexb, &alen) ||
        alen > cap) {
        free(annexb);
        AMediaCodec_queueInputBuffer(d->codec, (size_t)idx, 0, 0, (uint64_t)pts_us, 0);
        return 0;
    }
    memcpy(buf, annexb, alen);
    free(annexb);
    AMediaCodec_queueInputBuffer(d->codec, (size_t)idx, 0, alen, (uint64_t)pts_us, 0);
    d->draining = 0;
    return 1;
}

static int
vdec_copy(ns_os_vdec *d, const uint8_t *buf, size_t size, ns_os_frame *out)
{
    int w = d->crop_right - d->crop_left + 1;
    int h = d->crop_bottom - d->crop_top + 1;
    if (w <= 0 || h <= 0 || d->stride <= 0 || d->slice_height <= 0) return 0;
    size_t luma = (size_t)d->stride * (size_t)d->slice_height;
    if (size < luma + luma / 2) return 0;
    const uint8_t *y = buf + (size_t)d->crop_top * (size_t)d->stride + (size_t)d->crop_left;
    if (d->color_format == NS_MC_COLOR_NV12) {
        const uint8_t *uv = buf + luma + (size_t)(d->crop_top / 2) * (size_t)d->stride +
                            (size_t)(d->crop_left & ~1);
        return ns_os_frame_copy_nv12(out, y, d->stride, uv, d->stride, w, h);
    }
    if (d->color_format == NS_MC_COLOR_I420) {
        int cs = d->stride / 2;
        size_t chroma = (size_t)cs * (size_t)(d->slice_height / 2);
        size_t coff = (size_t)(d->crop_top / 2) * (size_t)cs + (size_t)(d->crop_left / 2);
        return ns_os_frame_copy_i420(out, y, d->stride, buf + luma + coff, cs,
                                     buf + luma + chroma + coff, cs, w, h);
    }
    return 0;
}

ns_os_status
ns_os_vdec_receive(ns_os_vdec *d, ns_os_frame *out)
{
    memset(out, 0, sizeof *out);
    if (!d || !d->codec) return NS_OS_ERROR;
    for (int guard = 0; guard < 4; guard++) {
        AMediaCodecBufferInfo info;
        ssize_t idx = AMediaCodec_dequeueOutputBuffer(d->codec, &info,
                                                      d->draining ? 20000 : 0);
        if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            vdec_read_format(d);
            continue;
        }
        if (idx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) continue;
        if (idx < 0) return NS_OS_AGAIN;
        size_t size = 0;
        uint8_t *buf = AMediaCodec_getOutputBuffer(d->codec, (size_t)idx, &size);
        int ok = 0;
        if (buf && info.size > 0 && !(info.flags & NS_MC_FLAG_CONFIG))
            ok = vdec_copy(d, buf + info.offset, (size_t)info.size, out);
        AMediaCodec_releaseOutputBuffer(d->codec, (size_t)idx, false);
        if (info.flags & NS_MC_FLAG_EOS && !ok) return NS_OS_AGAIN;
        if (!ok) {
            if (info.size > 0 && !(info.flags & NS_MC_FLAG_CONFIG)) return NS_OS_ERROR;
            continue;
        }
        out->pts_us = info.presentationTimeUs;
        return NS_OS_FRAME;
    }
    return NS_OS_AGAIN;
}

void
ns_os_vdec_drain(ns_os_vdec *d)
{
    if (!d || !d->codec) return;
    ssize_t idx = AMediaCodec_dequeueInputBuffer(d->codec, 20000);
    if (idx >= 0)
        AMediaCodec_queueInputBuffer(d->codec, (size_t)idx, 0, 0, 0,
                                     NS_MC_FLAG_EOS);
    d->draining = 1;
}

void
ns_os_vdec_flush(ns_os_vdec *d)
{
    if (!d || !d->codec) return;
    AMediaCodec_flush(d->codec);
    d->draining = 0;
}

void
ns_os_vdec_free(ns_os_vdec *d)
{
    if (!d) return;
    if (d->codec) {
        AMediaCodec_stop(d->codec);
        AMediaCodec_delete(d->codec);
    }
    free(d);
}

ns_os_adec *
ns_os_adec_new_aac(const uint8_t *asc, size_t asc_len, int rate, int channels)
{
    if (!asc || !asc_len) return NULL;
    int cfg_rate = 0, cfg_ch = 0;
    if (ns_os_aac_config(asc, asc_len, &cfg_rate, &cfg_ch, NULL)) {
        if (rate <= 0) rate = cfg_rate;
        if (channels <= 0) channels = cfg_ch;
    }
    if (rate <= 0 || channels <= 0 || channels > 8) return NULL;
    ns_os_adec *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->rate = rate;
    d->channels = channels;
    d->codec = AMediaCodec_createDecoderByType("audio/mp4a-latm");
    if (!d->codec) goto fail;
    AMediaFormat *f = AMediaFormat_new();
    AMediaFormat_setString(f, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE, rate);
    AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, channels);
    AMediaFormat_setInt32(f, "is-adts", 0);
    AMediaFormat_setBuffer(f, "csd-0", asc, asc_len);
    media_status_t st = AMediaCodec_configure(d->codec, f, NULL, NULL, 0);
    AMediaFormat_delete(f);
    if (st != AMEDIA_OK || AMediaCodec_start(d->codec) != AMEDIA_OK) goto fail;
    return d;
fail:
    ns_os_adec_free(d);
    return NULL;
}

static void
adec_read_format(ns_os_adec *d)
{
    AMediaFormat *f = AMediaCodec_getOutputFormat(d->codec);
    if (!f) return;
    int32_t v;
    if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE, &v) && v > 0) d->rate = v;
    if (AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &v) && v > 0) d->channels = v;
    d->pcm_float = AMediaFormat_getInt32(f, "pcm-encoding", &v) && v == NS_MC_PCM_FLOAT;
    AMediaFormat_delete(f);
}

static int
adec_append(ns_os_adec *d, const uint8_t *buf, size_t size, float **pcm,
            size_t *frames, size_t *cap)
{
    size_t bps = d->pcm_float ? 4 : 2;
    size_t n = size / (bps * (size_t)d->channels);
    size_t need = (*frames + n) * (size_t)d->channels;
    if (need > *cap) {
        size_t ncap = *cap ? *cap * 2 : 8192;
        while (ncap < need) ncap *= 2;
        float *grown = realloc(*pcm, ncap * sizeof(float));
        if (!grown) return 0;
        *pcm = grown;
        *cap = ncap;
    }
    float *dst = *pcm + *frames * (size_t)d->channels;
    size_t count = n * (size_t)d->channels;
    if (d->pcm_float) {
        memcpy(dst, buf, count * sizeof(float));
    } else {
        for (size_t i = 0; i < count; i++) {
            int16_t v;
            memcpy(&v, buf + i * 2, sizeof v);
            dst[i] = (float)v / 32768.0f;
        }
    }
    *frames += n;
    return 1;
}

int
ns_os_adec_decode(ns_os_adec *d, const uint8_t *data, size_t len,
                  float **out_pcm, size_t *out_frames, int *out_rate,
                  int *out_channels)
{
    *out_pcm = NULL;
    *out_frames = 0;
    if (!d || !d->codec) return 0;
    ssize_t in = AMediaCodec_dequeueInputBuffer(d->codec, 10000);
    if (in >= 0) {
        size_t cap = 0;
        uint8_t *buf = AMediaCodec_getInputBuffer(d->codec, (size_t)in, &cap);
        size_t n = data && buf && len <= cap ? len : 0;
        if (n) memcpy(buf, data, n);
        AMediaCodec_queueInputBuffer(d->codec, (size_t)in, 0, n, 0,
                                     data ? 0 : NS_MC_FLAG_EOS);
    }
    float *pcm = NULL;
    size_t frames = 0, cap = 0;
    int64_t wait = 5000;
    for (int guard = 0; guard < 16; guard++) {
        AMediaCodecBufferInfo info;
        ssize_t idx = AMediaCodec_dequeueOutputBuffer(d->codec, &info, wait);
        if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            adec_read_format(d);
            continue;
        }
        if (idx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) continue;
        if (idx < 0) break;
        size_t size = 0;
        uint8_t *buf = AMediaCodec_getOutputBuffer(d->codec, (size_t)idx, &size);
        if (buf && info.size > 0)
            adec_append(d, buf + info.offset, (size_t)info.size, &pcm, &frames, &cap);
        AMediaCodec_releaseOutputBuffer(d->codec, (size_t)idx, false);
        wait = 0;
    }
    if (!frames) {
        free(pcm);
        return 0;
    }
    *out_pcm = pcm;
    *out_frames = frames;
    *out_rate = d->rate;
    *out_channels = d->channels;
    return 1;
}

void
ns_os_adec_flush(ns_os_adec *d)
{
    if (d && d->codec) AMediaCodec_flush(d->codec);
}

void
ns_os_adec_free(ns_os_adec *d)
{
    if (!d) return;
    if (d->codec) {
        AMediaCodec_stop(d->codec);
        AMediaCodec_delete(d->codec);
    }
    free(d);
}
