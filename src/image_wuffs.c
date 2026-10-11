/* Nordstjernen — memory-safe PNG/GIF/BMP/JPEG decode via Wuffs.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-or-later
 */

#include "image.h"

#include <stdlib.h>
#include <string.h>


#include "wuffs-v0.4.c"

enum {
    NS_WUFFS_MAX_DIM    = 16384,
    NS_WUFFS_MAX_PIXELS = 64 * 1024 * 1024,
    NS_WUFFS_MAX_WORKBUF = 256 * 1024 * 1024,
};

typedef enum {
    NS_WUFFS_NONE = 0,
    NS_WUFFS_PNG,
    NS_WUFFS_GIF,
    NS_WUFFS_BMP,
    NS_WUFFS_JPEG,
    NS_WUFFS_WEBP,
} ns_wuffs_format;

static ns_wuffs_format
ns_wuffs_detect(const guchar *data, gsize len)
{
    if (!data || len < 4) return NS_WUFFS_NONE;
    if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47)
        return NS_WUFFS_PNG;
    if (len >= 6 &&
        data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8' &&
        (data[4] == '7' || data[4] == '9') && data[5] == 'a')
        return NS_WUFFS_GIF;
    if (data[0] == 'B' && data[1] == 'M')
        return NS_WUFFS_BMP;
    if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
        return NS_WUFFS_JPEG;
    if (len >= 12 &&
        data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
        data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P')
        return NS_WUFFS_WEBP;
    return NS_WUFFS_NONE;
}

gboolean
ns_image_png_is_animated(const guchar *data, gsize len)
{
    static const guchar sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    if (!data || len < 8 + 8 || memcmp(data, sig, sizeof sig) != 0) return FALSE;

    gsize pos = 8;
    while (pos + 8 <= len) {
        guint32 size = ((guint32)data[pos] << 24) | ((guint32)data[pos + 1] << 16) |
                       ((guint32)data[pos + 2] << 8) | (guint32)data[pos + 3];
        const guchar *tag = data + pos + 4;
        if (memcmp(tag, "IDAT", 4) == 0) return FALSE;
        if (memcmp(tag, "acTL", 4) == 0) return TRUE;
        if (memcmp(tag, "IEND", 4) == 0) return FALSE;
        if (size > len - pos - 8) return FALSE;
        pos += 12 + (gsize)size;
    }
    return FALSE;
}

static wuffs_base__image_decoder *
ns_wuffs_pick_decoder(const guchar *data, gsize len)
{
    switch (ns_wuffs_detect(data, len)) {
    case NS_WUFFS_PNG:  return wuffs_png__decoder__alloc_as__wuffs_base__image_decoder();
    case NS_WUFFS_GIF:  return wuffs_gif__decoder__alloc_as__wuffs_base__image_decoder();
    case NS_WUFFS_BMP:  return wuffs_bmp__decoder__alloc_as__wuffs_base__image_decoder();
    case NS_WUFFS_JPEG: return wuffs_jpeg__decoder__alloc_as__wuffs_base__image_decoder();
    case NS_WUFFS_WEBP: return wuffs_webp__decoder__alloc_as__wuffs_base__image_decoder();
    default:            return NULL;
    }
}

typedef struct {
    wuffs_base__image_decoder *dec;
    wuffs_base__io_buffer      src;
    wuffs_base__pixel_buffer   pb;
    uint8_t                   *pix;
    uint64_t                   pix_len;
    uint8_t                   *workbuf;
    uint64_t                   workbuf_len;
    uint32_t                   w, h;
} ns_wuffs_session;

static void
ns_wuffs_session_close(ns_wuffs_session *s)
{
    g_free(s->workbuf);
    g_free(s->pix);
    free(s->dec);
    s->workbuf = NULL;
    s->pix = NULL;
    s->dec = NULL;
}

static gboolean
ns_wuffs_session_open(ns_wuffs_session *s, const guchar *data, gsize len)
{
    memset(s, 0, sizeof *s);
    s->dec = ns_wuffs_pick_decoder(data, len);
    if (!s->dec) return FALSE;

    s->src = wuffs_base__make_io_buffer(
        wuffs_base__make_slice_u8((uint8_t *)data, len),
        wuffs_base__make_io_buffer_meta(len, 0, 0, true));

    wuffs_base__image_config ic = {0};
    wuffs_base__status st =
        wuffs_base__image_decoder__decode_image_config(s->dec, &ic, &s->src);
    if (!wuffs_base__status__is_ok(&st) ||
        !wuffs_base__image_config__is_valid(&ic))
        goto fail;

    s->w = wuffs_base__pixel_config__width(&ic.pixcfg);
    s->h = wuffs_base__pixel_config__height(&ic.pixcfg);
    if (s->w == 0 || s->h == 0 ||
        s->w > NS_WUFFS_MAX_DIM || s->h > NS_WUFFS_MAX_DIM ||
        (uint64_t)s->w * (uint64_t)s->h > (uint64_t)NS_WUFFS_MAX_PIXELS)
        goto fail;

    wuffs_base__pixel_config__set(&ic.pixcfg,
        WUFFS_BASE__PIXEL_FORMAT__BGRA_PREMUL,
        WUFFS_BASE__PIXEL_SUBSAMPLING__NONE, s->w, s->h);

    s->pix_len = wuffs_base__pixel_config__pixbuf_len(&ic.pixcfg);
    if (s->pix_len == 0 || s->pix_len > (uint64_t)NS_WUFFS_MAX_PIXELS * 4u)
        goto fail;

    s->pix = g_try_malloc0((gsize)s->pix_len);
    if (!s->pix) goto fail;

    st = wuffs_base__pixel_buffer__set_from_slice(
        &s->pb, &ic.pixcfg,
        wuffs_base__make_slice_u8(s->pix, (size_t)s->pix_len));
    if (!wuffs_base__status__is_ok(&st)) goto fail;

    s->workbuf_len = wuffs_base__image_decoder__workbuf_len(s->dec).max_incl;
    if (s->workbuf_len) {
        if (s->workbuf_len > (uint64_t)NS_WUFFS_MAX_WORKBUF) goto fail;
        s->workbuf = g_try_malloc((gsize)s->workbuf_len);
        if (!s->workbuf) goto fail;
    }
    return TRUE;

fail:
    ns_wuffs_session_close(s);
    return FALSE;
}

gboolean
ns_image_wuffs_supports_bytes(const guchar *data, gsize len)
{
    return ns_wuffs_detect(data, len) != NS_WUFFS_NONE;
}

guint8 *
ns_image_wuffs_decode_to_bgra(const guchar *data, gsize len,
                              int *out_w, int *out_h,
                              gsize *out_stride, gsize *out_buf_len)
{
    ns_wuffs_format kind = ns_wuffs_detect(data, len);
    ns_wuffs_session s;
    if (!ns_wuffs_session_open(&s, data, len)) return NULL;

    wuffs_base__status st = wuffs_base__image_decoder__decode_frame(
        s.dec, &s.pb, &s.src, WUFFS_BASE__PIXEL_BLEND__SRC,
        wuffs_base__make_slice_u8(s.workbuf, (size_t)s.workbuf_len),
        NULL);

    uint8_t *pix = s.pix;
    uint64_t pix_len64 = s.pix_len;
    uint32_t w = s.w, h = s.h;
    wuffs_base__table_u8 tab = wuffs_base__pixel_buffer__plane(&s.pb, 0);
    s.pix = NULL;
    ns_wuffs_session_close(&s);

    gboolean frame_ok = wuffs_base__status__is_ok(&st) ||
                        st.repr == wuffs_base__error__too_much_data;
    if (!frame_ok && kind != NS_WUFFS_GIF) {
        g_free(pix);
        return NULL;
    }

    if (tab.ptr == NULL || tab.stride == 0 ||
        (uint64_t)tab.stride < (uint64_t)w * 4 ||
        (uint64_t)tab.stride * (uint64_t)h > pix_len64) {
        g_free(pix);
        return NULL;
    }

    if (out_w) *out_w = (int)w;
    if (out_h) *out_h = (int)h;
    if (out_stride) *out_stride = (gsize)tab.stride;
    if (out_buf_len) *out_buf_len = (gsize)pix_len64;
    return pix;
}

ns_texture *
ns_image_decode_wuffs(const guchar *data, gsize len, int *out_w, int *out_h)
{
    int w = 0, h = 0;
    gsize stride = 0, buf_len = 0;
    guint8 *pix = ns_image_wuffs_decode_to_bgra(data, len, &w, &h,
                                                &stride, &buf_len);
    if (!pix) return NULL;

    GBytes *bytes = g_bytes_new_take(pix, buf_len);
    ns_texture *tex = ns_texture_new(
        w, h, NS_TEXTURE_BGRA_PREMULTIPLIED, bytes, stride);
    g_bytes_unref(bytes);

    if (tex) {
        if (out_w) *out_w = w;
        if (out_h) *out_h = h;
    }
    return tex;
}

GArray *
ns_image_decode_wuffs_anim_to_pixels(const guchar *data, gsize len,
                                     int *out_w, int *out_h)
{
    if (!data || len < 6) return NULL;
    ns_wuffs_format kind = ns_wuffs_detect(data, len);
    if (kind != NS_WUFFS_GIF &&
        !(kind == NS_WUFFS_PNG && ns_image_png_is_animated(data, len)))
        return NULL;

    ns_wuffs_session s;
    if (!ns_wuffs_session_open(&s, data, len)) return NULL;
    uint32_t h = s.h;
    uint8_t *pix = s.pix;
    wuffs_base__status st;

    GArray *frames = g_array_new(FALSE, FALSE, sizeof(ns_image_pixel_frame));
    g_array_set_clear_func(frames, ns_image_pixel_frame_clear);
    enum { NS_ANIM_MAX_FRAMES = 1024 };
    const gsize NS_ANIM_MAX_TOTAL_BYTES = (gsize)512 * 1024 * 1024;
    gsize anim_bytes_total = 0;

    uint8_t prev_disposal = WUFFS_BASE__ANIMATION_DISPOSAL__NONE;
    wuffs_base__rect_ie_u32 prev_dirty = {0, 0, 0, 0};
    uint8_t *backup = NULL;
    gsize    pix_bytes_total = (gsize)s.pix_len;

    while (frames->len < NS_ANIM_MAX_FRAMES) {
        wuffs_base__frame_config fc = {0};
        st = wuffs_base__image_decoder__decode_frame_config(s.dec, &fc, &s.src);
        if (!wuffs_base__status__is_ok(&st)) break;

        wuffs_base__table_u8 tab = wuffs_base__pixel_buffer__plane(&s.pb, 0);
        if (!tab.ptr || tab.stride == 0) break;

        if (prev_disposal == WUFFS_BASE__ANIMATION_DISPOSAL__RESTORE_BACKGROUND) {
            for (uint32_t y = prev_dirty.min_incl_y; y < prev_dirty.max_excl_y; y++) {
                if ((gsize)y * (gsize)tab.stride >= (gsize)tab.stride * (gsize)h)
                    break;
                uint8_t *row = pix + (gsize)y * (gsize)tab.stride;
                gsize x0 = (gsize)prev_dirty.min_incl_x * 4;
                gsize x1 = (gsize)prev_dirty.max_excl_x * 4;
                if (x1 > tab.stride) x1 = tab.stride;
                if (x0 < x1) memset(row + x0, 0, x1 - x0);
            }
        } else if (prev_disposal == WUFFS_BASE__ANIMATION_DISPOSAL__RESTORE_PREVIOUS
                   && backup) {
            memcpy(pix, backup, pix_bytes_total);
        }

        uint8_t cur_disposal = wuffs_base__frame_config__disposal(&fc);
        if (cur_disposal == WUFFS_BASE__ANIMATION_DISPOSAL__RESTORE_PREVIOUS) {
            if (!backup) backup = g_try_malloc(pix_bytes_total);
            if (backup) memcpy(backup, pix, pix_bytes_total);
        }

        st = wuffs_base__image_decoder__decode_frame(
            s.dec, &s.pb, &s.src, WUFFS_BASE__PIXEL_BLEND__SRC_OVER,
            wuffs_base__make_slice_u8(s.workbuf, (size_t)s.workbuf_len),
            NULL);
        if (!wuffs_base__status__is_ok(&st)) break;

        prev_disposal = cur_disposal;
        prev_dirty    = wuffs_base__frame_config__bounds(&fc);

        gsize frame_bytes = (gsize)tab.stride * (gsize)h;
        if (frame_bytes > pix_bytes_total) break;
        anim_bytes_total += frame_bytes;
        if (anim_bytes_total > NS_ANIM_MAX_TOTAL_BYTES) break;
        uint8_t *copy = g_try_malloc(frame_bytes);
        if (!copy) break;
        memcpy(copy, pix, frame_bytes);

        uint64_t flicks = wuffs_base__frame_config__duration(&fc);
        int delay_ms = (int)(flicks / 705600);
        if (delay_ms <= 0) delay_ms = 100;

        ns_image_pixel_frame f = {
            .pixels = copy,
            .pixels_len = frame_bytes,
            .stride = (gsize)tab.stride,
            .format = NS_TEXTURE_BGRA_PREMULTIPLIED,
            .width = (int)s.w,
            .height = (int)h,
            .delay_ms = delay_ms,
        };
        g_array_append_val(frames, f);
    }

    g_free(backup);
    ns_wuffs_session_close(&s);

    if (frames->len == 0) { g_array_free(frames, TRUE); return NULL; }
    if (out_w) *out_w = (int)s.w;
    if (out_h) *out_h = (int)h;
    return frames;
}
