/* Nordstjernen — synchronous fetch/cascade/layout/capture pipeline shared by drivers.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-or-later
 */

#ifndef NS_ENGINE_H
#define NS_ENGINE_H

#include <glib.h>

#include "anim.h"
#include "dom.h"
#include "image.h"
#include "js.h"
#include "layout.h"
#include "net.h"

G_BEGIN_DECLS

struct ns_print_setup;

ns_response *ns_engine_fetch_blocking(const char *url, const char *top_url,
                                      GError **error);

ns_response *ns_engine_navigate_blocking(const char *url, const char *top_url,
                                         gboolean user_activated,
                                         GError **error);

gboolean ns_engine_in_blocking_fetch(void);

ns_response *ns_engine_navigate_post_blocking(
    const char *url, const char *top_url,
    const void *body, gsize body_len,
    const char *content_type, gboolean user_activated, GError **error);

/* out_docs, when not NULL, gets the document of each sheet in out. */
void ns_engine_collect_stylesheets(ns_node *doc, const char *base_url,
                                   GPtrArray *out, GPtrArray *out_docs,
                                   GHashTable *css_cache);

char *ns_engine_linked_css_text(const char *url);
gboolean ns_engine_linked_css_known(const char *url);

/* Resource timing of the stylesheets the engine fetched for a page, held
 * until that page's JavaScript takes them for its performance timeline. */
typedef struct ns_engine_resource_timing {
    char               *top_url;
    char               *url;
    const char         *initiator;   /* "link" or "css" */
    gint64              start_us, end_us;
    struct ns_response *resp;
    gboolean            render_blocking;  /* a <head> sheet, or imported by one */
    gboolean            in_frame;         /* for a frame's document */
} ns_engine_resource_timing;

GPtrArray *ns_engine_take_resource_timings(const char *top_url);

void ns_engine_speculative_preload(ns_node *doc, const char *base_url,
                                   gboolean include_images);
void ns_engine_preload_script(const char *url, const char *top_url);

GHashTable *ns_engine_compute_cascade(ns_node *doc, const char *base_url,
                                      GHashTable *css_cache, ns_anim *anim);

GHashTable *ns_engine_relayout(ns_node *doc, const char *base_url,
                               int viewport_width, double viewport_height,
                               ns_image_cache *images, ns_anim *anim,
                               ns_js *js, GHashTable *css_cache,
                               const ns_node *focused, const ns_node *hover,
                               gsize caret_byte,
                               gsize sel_anchor_byte, ns_box **out_layout);

void ns_engine_layout_perf(guint64 *relayouts, double *total_ms);
void ns_engine_blocking_perf(guint64 *waits, double *total_ms);
double ns_engine_encode_ms(void);

void ns_engine_load_keyframes(ns_anim *anim, ns_node *doc, const char *base_url,
                              GHashTable *css_cache);

void ns_engine_anim_observe(ns_anim *anim, GHashTable *styles, gint64 now_us);

void ns_engine_fetch_images(ns_box *root, const char *base_url,
                            ns_image_cache *cache);

typedef struct ns_engine_img_session ns_engine_img_session;

ns_engine_img_session *ns_engine_fetch_images_start(
    ns_box *root, const char *base_url, ns_image_cache *cache,
    GHashTable *requested, double scroll_y, double viewport_h,
    gboolean *deferred_any,
    void (*arrived_cb)(gpointer user_data), gpointer user_data);
int  ns_engine_img_session_outstanding(const ns_engine_img_session *s);
void ns_engine_img_session_close(ns_engine_img_session *s);

int ns_engine_write_png(const ns_box *root, const char *path);
int ns_engine_write_pdf(const ns_box *root, const char *path);
int ns_engine_write_pdf_paged(const ns_box *root, const char *path,
                              const struct ns_print_setup *setup);

/* Renders each sheet into its own cairo recording surface, in CSS pixels.
   The caller owns the array and must destroy every surface in it. */
GPtrArray *ns_engine_print_recordings(const ns_box *root,
                                      const struct ns_print_setup *setup);

void ns_engine_dump_text(const ns_box *root, GString *out);
void ns_engine_dump_layout(const ns_box *root, int indent, GString *out);

char *ns_engine_suffix_before_ext(const char *path, const char *suffix);

G_END_DECLS

#endif
