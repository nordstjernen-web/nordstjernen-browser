/* Nordstjernen — decoded-image texture abstraction.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-only
 */

#ifndef NS_TEXTURE_H
#define NS_TEXTURE_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    NS_TEXTURE_BGRA_PREMULTIPLIED = 0,
    NS_TEXTURE_DEFAULT = 1,
} ns_texture_format;

typedef struct ns_texture ns_texture;

ns_texture *ns_texture_new(int width, int height, ns_texture_format format,
                           GBytes *bytes, gsize stride);

ns_texture *ns_texture_ref(ns_texture *texture);
void        ns_texture_unref(ns_texture *texture);
void        ns_texture_clear(ns_texture **texture);

int  ns_texture_get_width(ns_texture *texture);
int  ns_texture_get_height(ns_texture *texture);

void ns_texture_download(ns_texture *texture, guchar *dst, gsize dst_stride);

void *ns_texture_get_user_data(ns_texture *texture);
void  ns_texture_set_user_data(ns_texture *texture, void *data,
                               GDestroyNotify destroy);

G_END_DECLS

#endif
