/* Nordstjernen — decoded-image texture abstraction.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "texture.h"

#include <string.h>

#ifndef __ANDROID__

static GdkMemoryFormat
to_gdk_format(ns_texture_format format)
{
    return format == NS_TEXTURE_DEFAULT
               ? GDK_MEMORY_DEFAULT
               : GDK_MEMORY_B8G8R8A8_PREMULTIPLIED;
}

ns_texture *
ns_texture_new(int width, int height, ns_texture_format format,
               GBytes *bytes, gsize stride)
{
    return gdk_memory_texture_new(width, height, to_gdk_format(format),
                                  bytes, stride);
}

ns_texture *
ns_texture_ref(ns_texture *texture)
{
    return texture ? g_object_ref(texture) : NULL;
}

void
ns_texture_unref(ns_texture *texture)
{
    if (texture) g_object_unref(texture);
}

void
ns_texture_clear(ns_texture **texture)
{
    g_clear_object(texture);
}

int
ns_texture_get_width(ns_texture *texture)
{
    return gdk_texture_get_width(texture);
}

int
ns_texture_get_height(ns_texture *texture)
{
    return gdk_texture_get_height(texture);
}

void
ns_texture_download(ns_texture *texture, guchar *dst, gsize dst_stride)
{
    gdk_texture_download(texture, dst, dst_stride);
}

#else /* __ANDROID__ */

struct ns_texture {
    gint    ref_count;
    int     width;
    int     height;
    gsize   stride;
    guchar *bgra;
};

ns_texture *
ns_texture_new(int width, int height, ns_texture_format format,
               GBytes *bytes, gsize stride)
{
    (void)format;
    if (width <= 0 || height <= 0 || !bytes) return NULL;

    gsize src_len = 0;
    const guchar *src = g_bytes_get_data(bytes, &src_len);
    if (stride > G_MAXSIZE / (gsize)height) return NULL;
    gsize needed = stride * (gsize)height;
    if (!src || src_len < needed) return NULL;

    ns_texture *t = g_new0(ns_texture, 1);
    t->ref_count = 1;
    t->width = width;
    t->height = height;
    t->stride = stride;
    t->bgra = g_malloc(needed);
    memcpy(t->bgra, src, needed);
    return t;
}

ns_texture *
ns_texture_ref(ns_texture *texture)
{
    if (texture) g_atomic_int_inc(&texture->ref_count);
    return texture;
}

void
ns_texture_unref(ns_texture *texture)
{
    if (texture && g_atomic_int_dec_and_test(&texture->ref_count)) {
        g_free(texture->bgra);
        g_free(texture);
    }
}

void
ns_texture_clear(ns_texture **texture)
{
    if (texture && *texture) {
        ns_texture_unref(*texture);
        *texture = NULL;
    }
}

int
ns_texture_get_width(ns_texture *texture)
{
    return texture ? texture->width : 0;
}

int
ns_texture_get_height(ns_texture *texture)
{
    return texture ? texture->height : 0;
}

void
ns_texture_download(ns_texture *texture, guchar *dst, gsize dst_stride)
{
    if (!texture || !dst) return;
    gsize row = (gsize)texture->width * 4;
    if (row > dst_stride) row = dst_stride;
    if (row > texture->stride) row = texture->stride;
    for (int y = 0; y < texture->height; y++)
        memcpy(dst + (gsize)y * dst_stride,
               texture->bgra + (gsize)y * texture->stride, row);
}

#endif /* __ANDROID__ */
