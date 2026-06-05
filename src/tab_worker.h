/* Nordstjernen — serial per-tab worker for GTK-free page work.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_TAB_WORKER_H
#define NS_TAB_WORKER_H

#include <gio/gio.h>
#include <glib.h>

#include "dom.h"
#include "net.h"
#include "texture.h"

typedef struct ns_css_stylesheet ns_css_stylesheet;

G_BEGIN_DECLS

typedef struct ns_tab_worker ns_tab_worker;

typedef struct ns_tab_load_result {
    ns_response *resp;
    char        *body;
    gsize        body_len;
    ns_node     *doc;
    gboolean     parsed;
} ns_tab_load_result;

typedef struct ns_tab_image_result {
    ns_response *resp;
    guint8      *pixels;
    GArray      *anim_frames;
    gsize        pixels_len;
    gsize        stride;
    ns_texture_format format;
    int          width;
    int          height;
} ns_tab_image_result;

typedef struct ns_tab_css_result {
    ns_response       *resp;
    ns_css_stylesheet *sheet;
} ns_tab_css_result;

typedef void (*ns_tab_load_ready_cb)(ns_tab_load_result *result,
                                     gpointer user_data);
typedef void (*ns_tab_image_ready_cb)(ns_tab_image_result *result,
                                      gpointer user_data);
typedef void (*ns_tab_css_ready_cb)(ns_tab_css_result *result,
                                    gpointer user_data);

ns_tab_worker *ns_tab_worker_new(const char *name);
void           ns_tab_worker_free(ns_tab_worker *worker);

gboolean ns_tab_worker_load_response(ns_tab_worker *worker,
                                     ns_response *resp,
                                     gboolean parse_html,
                                     ns_tab_load_ready_cb cb,
                                     gpointer user_data,
                                     GDestroyNotify user_data_destroy);

gboolean ns_tab_worker_decode_image_response(ns_tab_worker *worker,
                                             ns_response *resp,
                                             ns_tab_image_ready_cb cb,
                                             gpointer user_data,
                                             GDestroyNotify user_data_destroy);

gboolean ns_tab_worker_parse_css_response(ns_tab_worker *worker,
                                          ns_response *resp,
                                          const char *scope_id,
                                          ns_tab_css_ready_cb cb,
                                          gpointer user_data,
                                          GDestroyNotify user_data_destroy);

void ns_tab_load_result_free(ns_tab_load_result *result);
void ns_tab_image_result_free(ns_tab_image_result *result);
void ns_tab_css_result_free(ns_tab_css_result *result);

G_END_DECLS

#endif
