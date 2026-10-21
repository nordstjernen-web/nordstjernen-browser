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

enum {
    NS_CLIP_BOX_MARGIN,
    NS_CLIP_BOX_BORDER,
    NS_CLIP_BOX_PADDING,
    NS_CLIP_BOX_CONTENT,
    NS_CLIP_BOX_COUNT,
};

typedef enum {
    NS_CLIP_SHAPE_BOX,
    NS_CLIP_SHAPE_INSET,
    NS_CLIP_SHAPE_ELLIPSE,
    NS_CLIP_SHAPE_POLYGON,
} ns_clip_shape_kind;

typedef struct {
    ns_clip_shape_kind kind;
    int box;
    double x, y, w, h;
    double rh[4], rv[4];
    double cx, cy, rx, ry;
    gboolean evenodd;
    GArray *points;
} ns_clip_shape;

gboolean ns_css_clip_shape_resolve(const char *text,
                                   const double boxes[NS_CLIP_BOX_COUNT][4],
                                   ns_clip_shape *out);
void ns_clip_shape_clear(ns_clip_shape *s);
char *ns_css_shape_image_threshold_canonical(const char *text, gboolean computed);

G_END_DECLS

#endif
