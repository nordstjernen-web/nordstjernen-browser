/* Nordstjernen — serial per-tab worker for GTK-free page work.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef ND_TAB_WORKER_H
#define ND_TAB_WORKER_H

#include <gio/gio.h>
#include <glib.h>

#include "dom.h"
#include "net.h"
#include "texture.h"

typedef struct nd_css_stylesheet nd_css_stylesheet;

G_BEGIN_DECLS

typedef struct nd_tab_worker nd_tab_worker;

typedef struct nd_tab_load_result {
    nd_response *resp;
    char        *body;
    gsize        body_len;
    nd_node     *doc;
    gboolean     parsed;
} nd_tab_load_result;

typedef struct nd_tab_image_result {
    nd_response *resp;
    guint8      *pixels;
    GArray      *anim_frames;
    gsize        pixels_len;
    gsize        stride;
    nd_texture_format format;
    int          width;
    int          height;
} nd_tab_image_result;

typedef struct nd_tab_css_result {
    nd_response       *resp;
    nd_css_stylesheet *sheet;
} nd_tab_css_result;

typedef void (*nd_tab_load_ready_cb)(nd_tab_load_result *result,
                                     gpointer user_data);
typedef void (*nd_tab_image_ready_cb)(nd_tab_image_result *result,
                                      gpointer user_data);
typedef void (*nd_tab_css_ready_cb)(nd_tab_css_result *result,
                                    gpointer user_data);

nd_tab_worker *nd_tab_worker_new(const char *name);
void           nd_tab_worker_free(nd_tab_worker *worker);

gboolean nd_tab_worker_load_response(nd_tab_worker *worker,
                                     nd_response *resp,
                                     gboolean parse_html,
                                     nd_tab_load_ready_cb cb,
                                     gpointer user_data,
                                     GDestroyNotify user_data_destroy);

gboolean nd_tab_worker_decode_image_response(nd_tab_worker *worker,
                                             nd_response *resp,
                                             nd_tab_image_ready_cb cb,
                                             gpointer user_data,
                                             GDestroyNotify user_data_destroy);

gboolean nd_tab_worker_parse_css_response(nd_tab_worker *worker,
                                          nd_response *resp,
                                          const char *scope_id,
                                          nd_tab_css_ready_cb cb,
                                          gpointer user_data,
                                          GDestroyNotify user_data_destroy);

void nd_tab_load_result_free(nd_tab_load_result *result);
void nd_tab_image_result_free(nd_tab_image_result *result);
void nd_tab_css_result_free(nd_tab_css_result *result);

G_END_DECLS

#endif
