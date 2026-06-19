/* Nordstjernen — experimental WebGPU (navigator.gpu) over wgpu-native.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_WEBGPU_H
#define NS_WEBGPU_H

#include <glib.h>
#include <cairo.h>
#include <quickjs.h>

#include "dom.h"

G_BEGIN_DECLS

typedef struct ns_js ns_js;

void ns_webgpu_install(JSContext *ctx, ns_js *js, JSValueConst navigator);

JSValue ns_webgpu_get_context(JSContext *ctx, ns_js *js, JSValueConst canvas_obj,
                              const ns_node *canvas);

cairo_surface_t *ns_webgpu_canvas_surface(const ns_node *canvas);

G_END_DECLS

#endif
