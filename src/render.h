/* Nordstjernen — shared style/layout pipeline used by GUI and headless.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_RENDER_H
#define NS_RENDER_H

#include <glib.h>

#include "anim.h"
#include "css.h"
#include "dom.h"
#include "js.h"
#include "layout.h"

G_BEGIN_DECLS

struct ns_image_cache;

typedef struct ns_render_ctx {
    ns_node                        *doc;
    const ns_css_stylesheet *const *sheets;
    guint                           n_sheets;
    double                          viewport_width;
    double                          viewport_height;
    double                          zoom;
    struct ns_image_cache          *images;
    const char                     *base_url;
    ns_anim                        *anim;
    ns_js                          *js;
    const ns_node                  *focused_input;
    gsize                           caret_byte;
    gsize                           sel_anchor_byte;
    char     *(*resolve_url)(const char *href, gpointer ud);
    gboolean  (*font_allowed)(const char *abs_url, gpointer ud);
    gpointer                        cb_ud;
} ns_render_ctx;

GHashTable *ns_render_relayout(const ns_render_ctx *c, ns_box **out_layout);

G_END_DECLS

#endif
