/* Nordstjernen — JPEG XL decode (stills and animations) via libjxl.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-or-later
 */

#include "image.h"

#include <string.h>
#include <jxl/decode.h>

enum {
    NS_JXL_MAX_DIM          = 16384,
    NS_JXL_MAX_PIXELS       = 64 * 1024 * 1024,
    NS_JXL_MAX_INPUT        = 64 * 1024 * 1024,
    NS_JXL_MAX_FRAMES       = 4096,
    NS_JXL_MAX_TOTAL_BYTES  = 512 * 1024 * 1024,
};

gboolean
ns_image_jxl_supports_bytes(const guchar *data, gsize len)
{
    if (!data || len < 2) return FALSE;
    JxlSignature sig = JxlSignatureCheck(data, len);
    return sig == JXL_SIG_CODESTREAM || sig == JXL_SIG_CONTAINER;
}

static void
ns_jxl_premultiply_to_bgra(guint8 *pix, gsize count)
{
    for (gsize i = 0; i < count; i++, pix += 4) {
        guint r = pix[0], g = pix[1], b = pix[2], a = pix[3];
        pix[0] = (guint8)((b * a + 127) / 255);
        pix[1] = (guint8)((g * a + 127) / 255);
        pix[2] = (guint8)((r * a + 127) / 255);
    }
}

static int
ns_jxl_ticks_to_ms(const JxlBasicInfo *info, uint32_t ticks)
{
    if (info->animation.tps_numerator == 0) return 100;
    guint64 ms = (guint64)ticks * 1000u * info->animation.tps_denominator /
                 info->animation.tps_numerator;
    return (int)MIN(ms, (guint64)G_MAXINT);
}

static GArray *
ns_jxl_decode_frames(const guchar *data, gsize len, guint max_frames,
                     gboolean animated_only, int *out_w, int *out_h)
{
    if (!ns_image_jxl_supports_bytes(data, len) || len > NS_JXL_MAX_INPUT)
        return NULL;

    JxlDecoder *dec = JxlDecoderCreate(NULL);
    if (!dec) return NULL;

    GArray *frames = g_array_new(FALSE, FALSE, sizeof(ns_image_pixel_frame));
    g_array_set_clear_func(frames, ns_image_pixel_frame_clear);

    JxlBasicInfo info;
    memset(&info, 0, sizeof info);
    JxlPixelFormat fmt = { 4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0 };
    ns_image_pixel_frame cur = {0};
    gsize frame_size = 0, stride = 0, total_bytes = 0;
    gboolean ok = FALSE;

    if (JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FRAME |
                                       JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS)
        goto out;
    JxlDecoderSetUnpremultiplyAlpha(dec, JXL_TRUE);
    if (JxlDecoderSetInput(dec, data, len) != JXL_DEC_SUCCESS) goto out;
    JxlDecoderCloseInput(dec);

    for (;;) {
        JxlDecoderStatus st = JxlDecoderProcessInput(dec);
        if (st == JXL_DEC_BASIC_INFO) {
            if (JxlDecoderGetBasicInfo(dec, &info) != JXL_DEC_SUCCESS) goto out;
            if (info.xsize == 0 || info.ysize == 0 ||
                info.xsize > NS_JXL_MAX_DIM || info.ysize > NS_JXL_MAX_DIM ||
                (guint64)info.xsize * info.ysize > NS_JXL_MAX_PIXELS)
                goto out;
            if (animated_only && !info.have_animation) goto out;
            JxlColorEncoding srgb;
            memset(&srgb, 0, sizeof srgb);
            srgb.color_space = JXL_COLOR_SPACE_RGB;
            srgb.white_point = JXL_WHITE_POINT_D65;
            srgb.primaries = JXL_PRIMARIES_SRGB;
            srgb.transfer_function = JXL_TRANSFER_FUNCTION_SRGB;
            srgb.rendering_intent = JXL_RENDERING_INTENT_PERCEPTUAL;
            JxlDecoderSetPreferredColorProfile(dec, &srgb);
            stride = (gsize)info.xsize * 4;
            frame_size = stride * info.ysize;
        } else if (st == JXL_DEC_FRAME) {
            JxlFrameHeader hdr;
            if (JxlDecoderGetFrameHeader(dec, &hdr) != JXL_DEC_SUCCESS) goto out;
            cur.delay_ms = info.have_animation
                ? ns_jxl_ticks_to_ms(&info, hdr.duration) : 0;
        } else if (st == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
            gsize need = 0;
            if (JxlDecoderImageOutBufferSize(dec, &fmt, &need) !=
                    JXL_DEC_SUCCESS || need != frame_size)
                goto out;
            if (total_bytes + frame_size > (gsize)NS_JXL_MAX_TOTAL_BYTES)
                break;
            g_free(cur.pixels);
            cur.pixels = g_try_malloc(frame_size);
            if (!cur.pixels) goto out;
            if (JxlDecoderSetImageOutBuffer(dec, &fmt, cur.pixels,
                                            frame_size) != JXL_DEC_SUCCESS)
                goto out;
        } else if (st == JXL_DEC_FULL_IMAGE) {
            if (!cur.pixels) goto out;
            ns_jxl_premultiply_to_bgra(cur.pixels,
                                       (gsize)info.xsize * info.ysize);
            cur.pixels_len = frame_size;
            cur.stride = stride;
            cur.format = NS_TEXTURE_BGRA_PREMULTIPLIED;
            cur.width = (int)info.xsize;
            cur.height = (int)info.ysize;
            g_array_append_val(frames, cur);
            total_bytes += frame_size;
            memset(&cur, 0, sizeof cur);
            if (frames->len >= max_frames) break;
        } else if (st == JXL_DEC_SUCCESS) {
            break;
        } else {
            goto out;
        }
    }
    ok = frames->len > 0;

out:
    g_free(cur.pixels);
    JxlDecoderDestroy(dec);
    if (!ok) {
        g_array_free(frames, TRUE);
        return NULL;
    }
    if (out_w) *out_w = (int)info.xsize;
    if (out_h) *out_h = (int)info.ysize;
    return frames;
}

guint8 *
ns_image_jxl_decode_to_bgra(const guchar *data, gsize len,
                            int *out_w, int *out_h,
                            gsize *out_stride, gsize *out_buf_len)
{
    int w = 0, h = 0;
    GArray *frames = ns_jxl_decode_frames(data, len, 1, FALSE, &w, &h);
    if (!frames) return NULL;
    ns_image_pixel_frame *f = &g_array_index(frames, ns_image_pixel_frame, 0);
    guint8 *pix = f->pixels;
    gsize stride = f->stride, buf_len = f->pixels_len;
    f->pixels = NULL;
    g_array_free(frames, TRUE);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    if (out_stride) *out_stride = stride;
    if (out_buf_len) *out_buf_len = buf_len;
    return pix;
}

ns_texture *
ns_image_decode_jxl(const guchar *data, gsize len, int *out_w, int *out_h)
{
    int w = 0, h = 0;
    gsize stride = 0, buf_len = 0;
    guint8 *pix = ns_image_jxl_decode_to_bgra(data, len, &w, &h,
                                              &stride, &buf_len);
    if (!pix) return NULL;
    GBytes *bytes = g_bytes_new_take(pix, buf_len);
    ns_texture *tex = ns_texture_new(w, h, NS_TEXTURE_BGRA_PREMULTIPLIED,
                                     bytes, stride);
    g_bytes_unref(bytes);
    if (tex) {
        if (out_w) *out_w = w;
        if (out_h) *out_h = h;
    }
    return tex;
}

GArray *
ns_image_decode_jxl_anim_to_pixels(const guchar *data, gsize len,
                                   int *out_w, int *out_h)
{
    GArray *frames = ns_jxl_decode_frames(data, len, NS_JXL_MAX_FRAMES,
                                          TRUE, out_w, out_h);
    if (frames && frames->len < 2) {
        g_array_free(frames, TRUE);
        return NULL;
    }
    return frames;
}
