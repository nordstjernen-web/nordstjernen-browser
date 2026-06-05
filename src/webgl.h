/* Nordstjernen — minimalist WebGL: canvas.getContext mapped onto GLES via GTK.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef ND_WEBGL_H
#define ND_WEBGL_H

#include <glib.h>
#include <cairo.h>
#include <quickjs.h>

#include "dom.h"

G_BEGIN_DECLS

typedef struct nd_js nd_js;

JSValue nd_webgl_get_context(JSContext *ctx, nd_js *js, JSValueConst canvas_obj,
                             const nd_node *canvas, int version,
                             JSValueConst attrs);

cairo_surface_t *nd_webgl_canvas_surface(const nd_node *canvas);

cairo_surface_t *nd_js_drawimage_source_surface(JSContext *ctx,
                                                JSValueConst src,
                                                int *out_w, int *out_h);

G_END_DECLS

#endif
