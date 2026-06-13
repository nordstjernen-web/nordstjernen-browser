/* Nordstjernen — WebP decode via libwebp.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "image.h"

#include <string.h>
#include <webp/decode.h>

enum {
    NS_WEBP_MAX_DIM    = 16384,
    NS_WEBP_MAX_PIXELS = 64 * 1024 * 1024,
    NS_WEBP_MAX_INPUT  = 64 * 1024 * 1024,
};

static guint32
ns_webp_read_u24(const guchar *p)
{
    return (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16);
}

static guint32
ns_webp_read_u32(const guchar *p)
{
    return (guint32)p[0] | ((guint32)p[1] << 8) |
           ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

static void
ns_webp_write_u24(guchar *p, guint32 v)
{
    p[0] = (guchar)(v & 0xff);
    p[1] = (guchar)((v >> 8) & 0xff);
    p[2] = (guchar)((v >> 16) & 0xff);
}

static void
ns_webp_write_u32(guchar *p, guint32 v)
{
    p[0] = (guchar)(v & 0xff);
    p[1] = (guchar)((v >> 8) & 0xff);
    p[2] = (guchar)((v >> 16) & 0xff);
    p[3] = (guchar)((v >> 24) & 0xff);
}

gboolean
ns_image_webp_supports_bytes(const guchar *data, gsize len)
{
    return data && len >= 12 &&
           memcmp(data, "RIFF", 4) == 0 &&
           memcmp(data + 8, "WEBP", 4) == 0;
}

static void
ns_image_webp_premultiply(guint8 *pix, gsize size)
{
    for (gsize i = 0; i < size; i += 4) {
        guint a = pix[i + 3];
        if (a == 255) continue;
        pix[i]     = (guint8)((pix[i]     * a + 127) / 255);
        pix[i + 1] = (guint8)((pix[i + 1] * a + 127) / 255);
        pix[i + 2] = (guint8)((pix[i + 2] * a + 127) / 255);
    }
}

static guint8 *
ns_image_webp_decode_still_premultiplied(const guchar *data, gsize len,
                                         int *out_w, int *out_h,
                                         gsize *out_stride)
{
    if (!ns_image_webp_supports_bytes(data, len)) return NULL;
    if (len > NS_WEBP_MAX_INPUT) return NULL;
    int w = 0, h = 0;
    if (!WebPGetInfo(data, len, &w, &h)) return NULL;
    if (w <= 0 || h <= 0 || w > NS_WEBP_MAX_DIM || h > NS_WEBP_MAX_DIM ||
        (guint64)w * (guint64)h > (guint64)NS_WEBP_MAX_PIXELS) return NULL;
    gsize stride = (gsize)w * 4;
    gsize size = stride * (gsize)h;
    guint8 *pix = g_try_malloc(size);
    if (!pix) return NULL;
    if (!WebPDecodeBGRAInto(data, len, pix, size, (int)stride)) {
        g_free(pix);
        return NULL;
    }
    ns_image_webp_premultiply(pix, size);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    if (out_stride) *out_stride = stride;
    return pix;
}

static gboolean
ns_webp_append_chunk(GByteArray *out, const char fourcc[4],
                     const guchar *payload, guint32 size)
{
    if (!out || !fourcc || (!payload && size > 0)) return FALSE;
    guint8 head[8];
    memcpy(head, fourcc, 4);
    ns_webp_write_u32(head + 4, size);
    g_byte_array_append(out, head, sizeof head);
    if (size > 0) g_byte_array_append(out, payload, size);
    if (size & 1) {
        const guint8 pad = 0;
        g_byte_array_append(out, &pad, 1);
    }
    return TRUE;
}

static GByteArray *
ns_webp_build_still_frame(const char fourcc[4],
                          const guchar *bitstream, guint32 bitstream_size,
                          const guchar *alpha, guint32 alpha_size,
                          guint32 frame_w, guint32 frame_h)
{
    if (!fourcc || !bitstream || bitstream_size == 0 ||
        frame_w == 0 || frame_h == 0) return NULL;
    gboolean lossy = memcmp(fourcc, "VP8 ", 4) == 0;
    gboolean lossless = memcmp(fourcc, "VP8L", 4) == 0;
    if (!lossy && !lossless) return NULL;
    gboolean extended = lossy && alpha && alpha_size > 0;
    gsize cap = 12 + 8 + bitstream_size + (bitstream_size & 1);
    if (extended) cap += 18 + 8 + alpha_size + (alpha_size & 1);
    if (cap > G_MAXUINT32 + 8ull) return NULL;
    GByteArray *out = g_byte_array_sized_new((guint)cap);
    const guint8 header[12] = {
        'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P'
    };
    g_byte_array_append(out, header, sizeof header);
    if (extended) {
        guint8 vp8x[10] = {0};
        vp8x[0] = 0x10;
        ns_webp_write_u24(vp8x + 4, frame_w - 1);
        ns_webp_write_u24(vp8x + 7, frame_h - 1);
        if (!ns_webp_append_chunk(out, "VP8X", vp8x, sizeof vp8x) ||
            !ns_webp_append_chunk(out, "ALPH", alpha, alpha_size)) {
            g_byte_array_free(out, TRUE);
            return NULL;
        }
    }
    if (!ns_webp_append_chunk(out, fourcc, bitstream, bitstream_size)) {
        g_byte_array_free(out, TRUE);
        return NULL;
    }
    ns_webp_write_u32(out->data + 4, out->len - 8);
    return out;
}

static void
ns_webp_overwrite_bgra(guint8 *dst, const guint8 *src, guint32 w)
{
    memcpy(dst, src, (gsize)w * 4);
}

static void
ns_webp_blend_bgra(guint8 *dst, const guint8 *src, guint32 w)
{
    for (guint32 x = 0; x < w; x++) {
        guint s = src[(gsize)x * 4 + 3];
        if (s == 0) continue;
        if (s == 255) {
            memcpy(dst + (gsize)x * 4, src + (gsize)x * 4, 4);
            continue;
        }
        guint inv = 255 - s;
        dst[(gsize)x * 4 + 0] =
            (guint8)(src[(gsize)x * 4 + 0] +
                     (dst[(gsize)x * 4 + 0] * inv + 127) / 255);
        dst[(gsize)x * 4 + 1] =
            (guint8)(src[(gsize)x * 4 + 1] +
                     (dst[(gsize)x * 4 + 1] * inv + 127) / 255);
        dst[(gsize)x * 4 + 2] =
            (guint8)(src[(gsize)x * 4 + 2] +
                     (dst[(gsize)x * 4 + 2] * inv + 127) / 255);
        dst[(gsize)x * 4 + 3] =
            (guint8)(s + (dst[(gsize)x * 4 + 3] * inv + 127) / 255);
    }
}

static void
ns_webp_fill_bgra(guint8 *dst, guint32 w, guint32 h, guint32 stride,
                  const guint8 color[4])
{
    guint8 premul[4] = {
        (guint8)(((guint)color[0] * color[3] + 127) / 255),
        (guint8)(((guint)color[1] * color[3] + 127) / 255),
        (guint8)(((guint)color[2] * color[3] + 127) / 255),
        color[3],
    };
    for (guint32 y = 0; y < h; y++) {
        guint8 *row = dst + (gsize)y * stride;
        for (guint32 x = 0; x < w; x++)
            memcpy(row + (gsize)x * 4, premul, 4);
    }
}

static gboolean
ns_webp_chunk_bounds(gsize base, guint32 size, gsize end,
                     gsize *payload, gsize *next)
{
    if (base > end || end - base < 8) return FALSE;
    gsize p = base + 8;
    if ((gsize)size > end - p) return FALSE;
    gsize n = p + (gsize)size;
    if (size & 1) {
        if (n == end) return FALSE;
        n++;
    }
    if (n > end) return FALSE;
    if (payload) *payload = p;
    if (next) *next = n;
    return TRUE;
}

static guint8 *
ns_image_webp_decode_animation_first_frame(const guchar *data, gsize len,
                                           int *out_w, int *out_h,
                                           gsize *out_stride)
{
    if (!ns_image_webp_supports_bytes(data, len) || len > NS_WEBP_MAX_INPUT)
        return NULL;
    guint32 riff_size = ns_webp_read_u32(data + 4);
    if ((gsize)riff_size > len - 8 || riff_size < 4) return NULL;
    gsize end = (gsize)riff_size + 8;
    guint32 canvas_w = 0, canvas_h = 0;
    guint8 bgra[4] = {0, 0, 0, 0};
    gboolean animated = FALSE;
    gsize off = 12;
    while (off + 8 <= end) {
        guint32 size = ns_webp_read_u32(data + off + 4);
        gsize p = 0, next = 0;
        if (!ns_webp_chunk_bounds(off, size, end, &p, &next)) return NULL;
        if (memcmp(data + off, "VP8X", 4) == 0 && size >= 10) {
            animated = (data[p] & 0x02) != 0;
            canvas_w = ns_webp_read_u24(data + p + 4) + 1;
            canvas_h = ns_webp_read_u24(data + p + 7) + 1;
        } else if (memcmp(data + off, "ANIM", 4) == 0 && size >= 4) {
            memcpy(bgra, data + p, 4);
        } else if (memcmp(data + off, "ANMF", 4) == 0) {
            if (!animated || canvas_w == 0 || canvas_h == 0 || size < 16)
                return NULL;
            if (canvas_w > NS_WEBP_MAX_DIM || canvas_h > NS_WEBP_MAX_DIM ||
                (guint64)canvas_w * (guint64)canvas_h >
                (guint64)NS_WEBP_MAX_PIXELS) return NULL;
            guint32 frame_x = ns_webp_read_u24(data + p) * 2;
            guint32 frame_y = ns_webp_read_u24(data + p + 3) * 2;
            guint32 frame_w = ns_webp_read_u24(data + p + 6) + 1;
            guint32 frame_h = ns_webp_read_u24(data + p + 9) + 1;
            guint8 flags = data[p + 15];
            if (frame_w == 0 || frame_h == 0 ||
                frame_w > NS_WEBP_MAX_DIM || frame_h > NS_WEBP_MAX_DIM ||
                frame_x > canvas_w || frame_y > canvas_h ||
                frame_w > canvas_w - frame_x ||
                frame_h > canvas_h - frame_y) return NULL;
            const guchar *alpha = NULL;
            guint32 alpha_size = 0, bitstream_size = 0;
            const guchar *bitstream = NULL;
            char fourcc[4] = {0};
            gsize sub = p + 16;
            while (sub + 8 <= p + size) {
                guint32 sub_size = ns_webp_read_u32(data + sub + 4);
                gsize sub_payload = 0, sub_next = 0;
                if (!ns_webp_chunk_bounds(sub, sub_size, p + size,
                                          &sub_payload, &sub_next))
                    return NULL;
                if (memcmp(data + sub, "ALPH", 4) == 0 && !alpha) {
                    alpha = data + sub_payload;
                    alpha_size = sub_size;
                } else if ((memcmp(data + sub, "VP8 ", 4) == 0 ||
                            memcmp(data + sub, "VP8L", 4) == 0) &&
                           !bitstream) {
                    memcpy(fourcc, data + sub, 4);
                    bitstream = data + sub_payload;
                    bitstream_size = sub_size;
                }
                sub = sub_next;
            }
            if (sub != p + size) return NULL;
            GByteArray *still = ns_webp_build_still_frame(
                fourcc, bitstream, bitstream_size, alpha, alpha_size,
                frame_w, frame_h);
            if (!still) return NULL;
            int decoded_w = 0, decoded_h = 0;
            gsize frame_stride = 0;
            guint8 *frame = ns_image_webp_decode_still_premultiplied(
                still->data, still->len, &decoded_w, &decoded_h,
                &frame_stride);
            g_byte_array_free(still, TRUE);
            if (!frame) return NULL;
            if (decoded_w != (int)frame_w || decoded_h != (int)frame_h) {
                g_free(frame);
                return NULL;
            }
            gsize stride = (gsize)canvas_w * 4;
            gsize canvas_size = stride * (gsize)canvas_h;
            guint8 *canvas = g_try_malloc(canvas_size);
            if (!canvas) {
                g_free(frame);
                return NULL;
            }
            ns_webp_fill_bgra(canvas, canvas_w, canvas_h, stride, bgra);
            for (guint32 y = 0; y < frame_h; y++) {
                guint8 *dst = canvas + (gsize)(frame_y + y) * stride +
                              (gsize)frame_x * 4;
                const guint8 *src = frame + (gsize)y * frame_stride;
                if (flags & 0x02)
                    ns_webp_overwrite_bgra(dst, src, frame_w);
                else
                    ns_webp_blend_bgra(dst, src, frame_w);
            }
            g_free(frame);
            if (out_w) *out_w = (int)canvas_w;
            if (out_h) *out_h = (int)canvas_h;
            if (out_stride) *out_stride = stride;
            return canvas;
        }
        off = next;
    }
    if (off != end) return NULL;
    return NULL;
}

static guint8 *
ns_image_webp_decode_premultiplied(const guchar *data, gsize len,
                                   int *out_w, int *out_h, gsize *out_stride)
{
    guint8 *pix = ns_image_webp_decode_still_premultiplied(
        data, len, out_w, out_h, out_stride);
    if (pix) return pix;
    return ns_image_webp_decode_animation_first_frame(data, len,
                                                     out_w, out_h,
                                                     out_stride);
}

ns_texture *
ns_image_decode_webp(const guchar *data, gsize len, int *out_w, int *out_h)
{
    int w = 0, h = 0;
    gsize stride = 0;
    guint8 *pix = ns_image_webp_decode_premultiplied(data, len, &w, &h,
                                                     &stride);
    if (!pix) return NULL;
    GBytes *bytes = g_bytes_new_take(pix, stride * (gsize)h);
    ns_texture *tex = ns_texture_new(w, h, NS_TEXTURE_BGRA_PREMULTIPLIED,
                                     bytes, stride);
    g_bytes_unref(bytes);
    if (tex) {
        if (out_w) *out_w = w;
        if (out_h) *out_h = h;
    }
    return tex;
}

guint8 *
ns_image_webp_decode_to_bgra(const guchar *data, gsize len,
                             int *out_w, int *out_h,
                             gsize *out_stride, gsize *out_buf_len)
{
    int w = 0, h = 0;
    gsize stride = 0;
    guint8 *pix = ns_image_webp_decode_premultiplied(data, len, &w, &h,
                                                     &stride);
    if (!pix) return NULL;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    if (out_stride) *out_stride = stride;
    if (out_buf_len) *out_buf_len = stride * (gsize)h;
    return pix;
}
