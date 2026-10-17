/* Nordstjernen — paginates a laid-out page onto sheets of paper.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-or-later
 */

#ifndef NS_PRINT_H
#define NS_PRINT_H

#include <cairo.h>
#include <glib.h>

#include "css.h"
#include "layout.h"

G_BEGIN_DECLS

struct ns_browser;
struct ns_renderer_session;
struct ns_rproc_http;

typedef struct ns_print_setup {
    double width;
    double height;
    double margin_top;
    double margin_right;
    double margin_bottom;
    double margin_left;
} ns_print_setup;

void ns_print_setup_default(ns_print_setup *setup);
void ns_print_setup_apply_page_rule(ns_print_setup *setup,
                                    const ns_css_page_rule *rule);

GArray *ns_print_page_offsets(const ns_box *root, double page_content_height);

double ns_print_page_bottom(const GArray *offsets, guint i,
                            double page_content_height);

void ns_print_draw_page(cairo_t *cr, const ns_box *root,
                        const ns_print_setup *setup, double scale,
                        double page_top, double page_bottom);

GPtrArray *ns_browser_print_pages(struct ns_browser *browser,
                                  ns_print_setup *out_setup);

GPtrArray *ns_renderer_session_print(struct ns_renderer_session *s,
                                     ns_print_setup *out_setup);

#define NS_PRINT_RASTER_SCALE 3.0

typedef GPtrArray *(*ns_rproc_inproc_print_fn)(void *conn,
                                               ns_print_setup *out_setup);
void ns_rproc_http_set_inproc_print(ns_rproc_inproc_print_fn print);
GPtrArray *ns_rproc_http_print(struct ns_rproc_http *r,
                               ns_print_setup *out_setup, double *out_scale);

G_END_DECLS

#endif
