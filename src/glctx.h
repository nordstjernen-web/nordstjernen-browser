/* Nordstjernen — toolkit-independent offscreen GLES context for WebGL.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-only
 */

#ifndef NS_GLCTX_H
#define NS_GLCTX_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct ns_gl_context ns_gl_context;

ns_gl_context *ns_gl_context_create(void);
gboolean       ns_gl_context_make_current(ns_gl_context *c);
void           ns_gl_context_release(ns_gl_context *c);
void           ns_gl_context_destroy(ns_gl_context *c);

G_END_DECLS

#endif
