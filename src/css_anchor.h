/* Nordstjernen — CSS Anchor Positioning value parsing, serialization and evaluation.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-or-later
 */

#ifndef NS_CSS_ANCHOR_H
#define NS_CSS_ANCHOR_H

#include <glib.h>

typedef gboolean (*ns_anchor_lookup_fn)(void *user, const char *name, double rect[4]);

typedef struct ns_anchor_eval_ctx {
    gboolean            vertical_axis;
    gboolean            far_edge;
    gboolean            allow_anchor;
    double              cb_x, cb_y, cb_w, cb_h;
    double              basis;
    double              font_px;
    double              root_font_px;
    double              viewport_w, viewport_h;
    ns_anchor_lookup_fn lookup;
    void               *user;
} ns_anchor_eval_ctx;

gboolean ns_css_text_has_anchor_fn(const char *text);
char    *ns_css_anchor_fn_canonical(const char *text, gboolean allow_anchor,
                                    gboolean allow_size);
gboolean ns_css_anchor_fn_eval(const char *canonical, const ns_anchor_eval_ctx *cx,
                               double *out_px);
char    *ns_css_anchor_fn_fallback(const char *canonical);

char    *ns_css_dashed_ident_list_canonical(const char *text,
                                            const char *const *keywords);
char    *ns_css_position_anchor_canonical(const char *text);
char    *ns_css_position_area_canonical(const char *text, gboolean computed);
gboolean ns_css_position_area_resolve(const char *value, int *col_lo, int *col_hi,
                                      int *row_lo, int *row_hi);
char    *ns_css_position_visibility_canonical(const char *text);
char    *ns_css_position_try_order_canonical(const char *text);
char    *ns_css_position_try_fallbacks_canonical(const char *text);

#endif
