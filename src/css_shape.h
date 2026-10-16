/* Nordstjernen — CSS Shapes value parsing and serialization. */

#ifndef NS_CSS_SHAPE_H
#define NS_CSS_SHAPE_H

#include <glib.h>
#include "css_syntax.h"

G_BEGIN_DECLS

typedef struct ns_shape_ctx {
    double font_px;
    double root_px;
    double ex_px;
    double ch_px;
    double vw;
    double vh;
} ns_shape_ctx;

char *ns_css_shape_outside_canonical(const char *text, const ns_shape_ctx *cx);
char *ns_css_shape_interpolate(const char *from, const char *to, double t,
                                const ns_shape_ctx *cx);
char *ns_css_shape_add(const char *underlying, const char *v, const ns_shape_ctx *cx);
char *ns_css_clip_path_canonical(const char *text, const ns_shape_ctx *cx, gboolean *is_shape);
gboolean ns_css_shape_eval_math(const ns_css_component *t, const ns_shape_ctx *cx, double *num,
                                double *pct, double *px, gboolean *dim);
char *ns_css_shape_margin_canonical(const char *text, const ns_shape_ctx *cx);
char *ns_css_shape_image_threshold_canonical(const char *text, gboolean computed);

G_END_DECLS

#endif
