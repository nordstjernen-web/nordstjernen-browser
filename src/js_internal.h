/* Nordstjernen — internal JS engine declarations shared between
 * js.c and js_canvas.c. Not a public API.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef ND_JS_INTERNAL_H
#define ND_JS_INTERNAL_H

#include <glib.h>
#include <cairo.h>
#include <pango/pango.h>
#include <quickjs.h>

#include "js.h"
#include "dom.h"
#include "image.h"
#include "layout.h"

typedef struct nd_worker_host nd_worker_host;

typedef struct nd_canvas_state {
    int w, h;
    cairo_surface_t *surf;
    cairo_t         *cr;
    double fill_r, fill_g, fill_b, fill_a;
    double stroke_r, stroke_g, stroke_b, stroke_a;
    double line_width;
    char  *font;
    cairo_pattern_t *fill_pattern;
    cairo_pattern_t *stroke_pattern;
    double shadow_r, shadow_g, shadow_b, shadow_a;
    double shadow_blur, shadow_ox, shadow_oy;
} nd_canvas_state;

typedef struct nd_path2d {
    cairo_surface_t *rs;
    cairo_t         *cr;
} nd_path2d;

typedef struct nd_image_bitmap {
    cairo_surface_t *surf;
    int w, h;
} nd_image_bitmap;

struct nd_js {
    JSRuntime    *rt;
    JSContext    *ctx;
    nd_js_log_cb  log_cb;
    gpointer      log_user_data;
    nd_js_mutated_cb mut_cb;
    gpointer      mut_user_data;
    nd_js_navigate_cb nav_cb;
    gpointer      nav_user_data;
    nd_js_scroll_to_cb scroll_to_cb;
    gpointer      scroll_to_user_data;
    nd_js_form_submit_cb form_submit_cb;
    gpointer      form_submit_user_data;
    nd_js_soft_nav_cb soft_nav_cb;
    gpointer      soft_nav_user_data;
    nd_js_repaint_cb repaint_cb;
    gpointer      repaint_user_data;
    nd_js_layout_flush_cb layout_flush_cb;
    gpointer      layout_flush_user_data;
    gboolean      in_layout_flush;
    nd_js_clipboard_write_cb clipboard_write_cb;
    gpointer      clipboard_write_user_data;
    JSValue       history_state;
    int           history_length;
    char         *current_url;
    nd_node       *current_doc;
    nd_node       *current_script;
    gboolean      mutated;
    GHashTable   *timers;
    GMainContext *main_context;
    GPtrArray    *workers;
    nd_worker_host *worker_host;
    int           next_timer_id;
    int           timer_nesting_level;
    GArray       *raf_pending;
    int           next_raf_id;
    gint64        raf_last_us;
    nd_node      *raf_frame_ctx;
    JSValue       pristine_promise;
    GHashTable   *style_table;
    const struct nd_box *layout_root;
    GHashTable   *box_lookup_cache;
    const void   *box_lookup_cache_root;
    const void   *box_lookup_pending_root;
    int           box_lookup_pending_count;
    const nd_node *focused_node;
    const nd_node *active_modal;
    const nd_node *focus_before_modal;
    GHashTable   *canvas_states;
    nd_image_cache *image_cache;
    GHashTable   *js_image_loads;
    GHashTable   *orphan_nodes;
    GPtrArray    *listeners;
    GHashTable   *pinned_wrappers_set;
    GPtrArray    *pending_fetches;
    GHashTable   *fetch_states_by_id;
    guint         next_fetch_id;
    GPtrArray    *pending_xhrs;
    GPtrArray    *pending_ws;
    GPtrArray    *filereader_idles;
    GHashTable   *local_storage;
    GHashTable   *session_storage;
    char         *local_storage_origin;
    char         *local_storage_path;
    gboolean      local_storage_dirty;
    guint         local_storage_flush_source;
    gboolean      local_storage_disabled;
    char         *cookie_value;
    GHashTable   *session_storage_buckets;
    GHashTable   *cookie_buckets;
    char         *partition_key;
    guint64       opaque_counter;
    char         *referrer;
    int           ready_state;
    gint64        eval_deadline_us;
    gint64        js_monitor_deadline_us;
    gboolean      halted;
    gboolean      in_pump;
    int           eval_depth;
    GPtrArray    *deferred_script_roots;
    GPtrArray    *pending_iframe_loads;
    GHashTable   *iframe_globals;
    int           iframe_load_depth;
    gint64        last_pump_us;
    gint64        last_orphan_sweep_us;
    int           dispatch_depth;
    GPtrArray    *mutation_observers;
    gboolean      mutation_drain_scheduled;
    GPtrArray    *intersection_observers;
    GPtrArray    *resize_observers;
    GArray       *doc_stack;
    JSValue       iframe_doc;
    int           iframe_doc_set;
    const nd_csp *csp;
    char         *selection_text;
    gboolean      selection_has_range;
    double        selection_x, selection_y, selection_w, selection_h;
    int           module_load_count;
    gsize         module_load_bytes;
    gint64        module_load_deadline_us;
    gboolean      module_load_capped;
    gint64        time_origin_us;
    double        time_origin_real_ms;
    GPtrArray    *perf_entries;
    GPtrArray    *perf_observers;
    gboolean      perf_drain_scheduled;
    GHashTable   *console_counts;
    GHashTable   *console_timers;
    GHashTable   *blob_urls;
    GHashTable   *ce_registry;
    GHashTable   *ce_pending;
    nd_node      *ce_upgrading;
    int           ce_in_attr_callback;
    int           ce_defer_upgrades;
    JSValue       nodelist_decorator;
    int           nodelist_decorator_set;
    JSValue       computed_style_proxy;
    int           computed_style_proxy_set;
    JSValue       url_helper;
    int           url_helper_set;
    JSValue       search_params_helper;
    int           search_params_helper_set;
    JSValue       form_data_helper;
    int           form_data_helper_set;
    JSValue       body_consumer_helper;
    int           body_consumer_helper_set;
    JSAtom        atom_capture;
    JSAtom        atom_once;
    JSAtom        atom_signal;
    JSAtom        atom_passive;
    JSAtom        atom_aborted;
    JSAtom        atom_immediate_stopped;
    JSAtom        atom_propagation_stopped;
    int           listener_atoms_set;
    guint64       dom_gen;
    struct {
        const void *root;
        char        kind;
        char       *key;
        guint64     gen;
        JSValue     value;
        int         set;
    } qcache[16];
    int           qcache_next;
};

static inline nd_js *
js_from_ctx(JSContext *ctx)
{
    return ctx ? (nd_js *)JS_GetContextOpaque(ctx) : NULL;
}

typedef void (*nd_ctx_drawfn)(cairo_t *cr, void *ud);

typedef struct nd_draw_rect_ud {
    double x, y, w, h, lw;
    JSContext *ctx;
    JSValueConst this_val;
    nd_canvas_state *st;
} nd_draw_rect_ud;

typedef struct nd_draw_path_ud {
    JSContext *ctx;
    JSValueConst this_val;
    nd_canvas_state *st;
    double lw;
    cairo_path_t *snapshot;
    cairo_fill_rule_t fill_rule;
} nd_draw_path_ud;

/* Helpers defined in js.c, used by js_canvas.c */
double nd_arg_d(JSContext *ctx, JSValueConst v);
void nd_bind_fn(JSContext *ctx, JSValueConst obj, const char *name, JSCFunction *fn, int argc);
const nd_box *nd_box_find_by_dom(const nd_box *root, const nd_node *target);
uint32_t nd_js_array_length(JSContext *ctx, JSValueConst arr);
const nd_image *nd_js_image_for_node(nd_js *js, const nd_node *el);
void nd_js_promise_reject(JSContext *ctx, JSValue resolvers[2], const char *message);
JSValue nd_make_element(JSContext *ctx, const nd_node *cnode);
const nd_node *nd_unwrap_element(JSValueConst val);

/* Canvas API implemented in js_canvas.c */
void
nd_path2d_finalizer(JSRuntime *rt, JSValue val);
void
nd_image_bitmap_finalizer(JSRuntime *rt, JSValue val);
void
nd_canvas_state_free(gpointer data);
JSValue
nd_image_bitmap_close(JSContext *ctx, JSValueConst this_val,
                      int argc, JSValueConst *argv);
JSValue
nd_image_bitmap_make(JSContext *ctx, cairo_surface_t *surf, int w, int h);
cairo_surface_t *
nd_image_bitmap_from_imagedata(JSContext *ctx, JSValueConst src,
                               int *out_w, int *out_h);
cairo_surface_t *
nd_image_bitmap_crop(cairo_surface_t *src, int sw, int sh,
                     int sx, int sy, int rw, int rh);
JSValue
nd_window_create_image_bitmap(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv);
JSValue
nd_offscreen_transferToImageBitmap(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv);
void
nd_dommatrix_read(JSContext *ctx, JSValueConst v, double *a, double *b,
                  double *c, double *d, double *e, double *f);
void
nd_dommatrix_write(JSContext *ctx, JSValueConst obj, double a, double b,
                   double c, double d, double e, double f);
JSValue
nd_dommatrix_multiply(JSContext *ctx, JSValueConst this_val,
                      int argc, JSValueConst *argv);
JSValue
nd_dommatrix_multiplySelf(JSContext *ctx, JSValueConst this_val,
                          int argc, JSValueConst *argv);
JSValue
nd_dommatrix_translate(JSContext *ctx, JSValueConst this_val,
                       int argc, JSValueConst *argv);
JSValue
nd_dommatrix_scale(JSContext *ctx, JSValueConst this_val,
                   int argc, JSValueConst *argv);
JSValue
nd_dommatrix_rotate(JSContext *ctx, JSValueConst this_val,
                    int argc, JSValueConst *argv);
JSValue
nd_dommatrix_inverse(JSContext *ctx, JSValueConst this_val,
                     int argc, JSValueConst *argv);
JSValue
nd_dommatrix_invertSelf(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv);
void
nd_obj_double(JSContext *ctx, JSValueConst obj, const char *key, double *out);
JSValue
nd_dommatrix_transformPoint(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv);
JSValue
nd_dommatrix_toString(JSContext *ctx, JSValueConst this_val,
                      int argc, JSValueConst *argv);
JSValue
nd_dommatrix_make(JSContext *ctx, double a, double b, double c, double d,
                  double e, double f, gboolean readonly);
JSValue
nd_dommatrix_ctor_impl(JSContext *ctx, int argc, JSValueConst *argv,
                       gboolean readonly);
JSValue
nd_window_dommatrix_ctor(JSContext *ctx, JSValueConst this_val,
                         int argc, JSValueConst *argv);
JSValue
nd_window_dommatrix_readonly_ctor(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv);
JSValue
nd_window_offscreen_canvas_ctor(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv);
int
nd_canvas_dim_from_attr(const nd_node *el, const char *name, int defv);
gboolean
nd_canvas_parse_color(const char *s, double *r, double *g, double *b, double *a);
nd_canvas_state *
nd_canvas_state_for(nd_js *js, const nd_node *el);
nd_canvas_state *
nd_ctx_state(JSContext *ctx, JSValueConst this_val);
cairo_pattern_t *
nd_ctx_build_pattern(JSContext *ctx, JSValueConst obj);
double
nd_ctx_global_alpha(JSContext *ctx, JSValueConst this_val);
cairo_operator_t
nd_ctx_parse_composite(const char *s);
void
nd_ctx_apply_composite(JSContext *ctx, JSValueConst this_val, cairo_t *cr);
gboolean
nd_ctx_image_smoothing(JSContext *ctx, JSValueConst this_val);
void
nd_ctx_sync_styles(JSContext *ctx, JSValueConst this_val, nd_canvas_state *st);
gboolean
nd_ctx_has_shadow(const nd_canvas_state *st);
void
nd_box_blur_argb(uint8_t *data, int w, int h, int stride, int radius);
void
nd_ctx_with_shadow(JSContext *ctx, JSValueConst this_val, nd_canvas_state *st,
                   nd_ctx_drawfn draw, void *ud);
void
nd_ctx_set_fill_source(JSContext *ctx, JSValueConst this_val, nd_canvas_state *st);
void
nd_ctx_set_stroke_source(JSContext *ctx, JSValueConst this_val, nd_canvas_state *st);
void
nd_draw_fillrect(cairo_t *cr, void *vud);
void
nd_draw_strokerect(cairo_t *cr, void *vud);
JSValue
nd_ctx_fillRect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue
nd_ctx_strokeRect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue
nd_ctx_clearRect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue
nd_ctx_beginPath(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue
nd_ctx_closePath(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue
nd_ctx_moveTo(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue
nd_ctx_lineTo(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue
nd_ctx_arc(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue
nd_ctx_rect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
gboolean
nd_value_is_path2d(JSValueConst v);
void
nd_replay_path2d(cairo_t *target, JSValueConst path_v);
cairo_fill_rule_t
nd_parse_fill_rule(const char *s);
cairo_path_t *
nd_ctx_prepare_path_and_rule(JSContext *ctx, cairo_t *cr,
                             int argc, JSValueConst *argv);
void
nd_ctx_restore_path(cairo_t *cr, cairo_path_t *saved);
void
nd_draw_fillpath(cairo_t *cr, void *vud);
void
nd_draw_strokepath(cairo_t *cr, void *vud);
JSValue
nd_ctx_fill(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue
nd_ctx_stroke(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue
nd_ctx_save(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue
nd_ctx_restore(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue
nd_ctx_translate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue
nd_ctx_scale(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue
nd_ctx_rotate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
PangoFontDescription *
nd_canvas_font_desc(const char *css_font);
gboolean
nd_ctx_direction_is_rtl(JSContext *ctx, JSValueConst this_val);
void
nd_ctx_paint_text(JSContext *ctx, JSValueConst this_val,
                  nd_canvas_state *st, const char *text,
                  double x, double y, double max_width,
                  gboolean stroke);
JSValue
nd_ctx_fillText(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue
nd_ctx_measureText(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue
nd_ctx_quadraticCurveTo(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv);
JSValue
nd_ctx_bezierCurveTo(JSContext *ctx, JSValueConst this_val,
                     int argc, JSValueConst *argv);
JSValue
nd_ctx_arcTo(JSContext *ctx, JSValueConst this_val,
             int argc, JSValueConst *argv);
JSValue
nd_ctx_ellipse(JSContext *ctx, JSValueConst this_val,
               int argc, JSValueConst *argv);
JSValue
nd_ctx_clip(JSContext *ctx, JSValueConst this_val,
            int argc, JSValueConst *argv);
gboolean
nd_matrix_from_obj(JSContext *ctx, JSValueConst v, cairo_matrix_t *m);
JSValue
nd_ctx_setTransform(JSContext *ctx, JSValueConst this_val,
                    int argc, JSValueConst *argv);
JSValue
nd_ctx_transform(JSContext *ctx, JSValueConst this_val,
                 int argc, JSValueConst *argv);
JSValue
nd_ctx_resetTransform(JSContext *ctx, JSValueConst this_val,
                      int argc, JSValueConst *argv);
JSValue
nd_ctx_setLineDash(JSContext *ctx, JSValueConst this_val,
                   int argc, JSValueConst *argv);
JSValue
nd_ctx_getLineDash(JSContext *ctx, JSValueConst this_val,
                   int argc, JSValueConst *argv);
JSValue
nd_ctx_gradient_addColorStop(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv);
cairo_surface_t *
nd_ctx_drawimage_source(JSContext *ctx, JSValueConst src, int *out_w, int *out_h);
JSValue
nd_ctx_drawImage(JSContext *ctx, JSValueConst this_val,
                 int argc, JSValueConst *argv);
JSValue
nd_ctx_createPattern(JSContext *ctx, JSValueConst this_val,
                     int argc, JSValueConst *argv);
JSValue
nd_ctx_createLinearGradient(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv);
JSValue
nd_ctx_createRadialGradient(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv);
JSValue
nd_image_data_make(JSContext *ctx, int w, int h, const uint8_t *rgba);
JSValue
nd_ctx_createImageData(JSContext *ctx, JSValueConst this_val,
                       int argc, JSValueConst *argv);
JSValue
nd_ctx_getImageData(JSContext *ctx, JSValueConst this_val,
                    int argc, JSValueConst *argv);
JSValue
nd_ctx_putImageData(JSContext *ctx, JSValueConst this_val,
                    int argc, JSValueConst *argv);
JSValue
nd_ctx_strokeText(JSContext *ctx, JSValueConst this_val,
                  int argc, JSValueConst *argv);
void
nd_round_rect_subpath(cairo_t *cr, double x, double y, double w, double h,
                      double rtl, double rtr, double rbr, double rbl);
void
nd_extract_radii(JSContext *ctx, JSValueConst v,
                 double *rtl, double *rtr, double *rbr, double *rbl);
JSValue
nd_ctx_roundRect(JSContext *ctx, JSValueConst this_val,
                 int argc, JSValueConst *argv);
JSValue
nd_ctx_reset(JSContext *ctx, JSValueConst this_val,
             int argc, JSValueConst *argv);
JSValue
nd_ctx_getTransform(JSContext *ctx, JSValueConst this_val,
                    int argc, JSValueConst *argv);
JSValue
nd_ctx_isPointInPath(JSContext *ctx, JSValueConst this_val,
                     int argc, JSValueConst *argv);
JSValue
nd_ctx_isPointInStroke(JSContext *ctx, JSValueConst this_val,
                       int argc, JSValueConst *argv);
JSValue
nd_path2d_get_cr(JSContext *ctx, JSValueConst this_val, cairo_t **out);
JSValue
nd_path2d_moveTo(JSContext *ctx, JSValueConst this_val,
                 int argc, JSValueConst *argv);
JSValue
nd_path2d_lineTo(JSContext *ctx, JSValueConst this_val,
                 int argc, JSValueConst *argv);
JSValue
nd_path2d_closePath(JSContext *ctx, JSValueConst this_val,
                    int argc, JSValueConst *argv);
JSValue
nd_path2d_bezierCurveTo(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv);
JSValue
nd_path2d_quadraticCurveTo(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv);
JSValue
nd_path2d_arc(JSContext *ctx, JSValueConst this_val,
              int argc, JSValueConst *argv);
JSValue
nd_path2d_arcTo(JSContext *ctx, JSValueConst this_val,
                int argc, JSValueConst *argv);
JSValue
nd_path2d_ellipse(JSContext *ctx, JSValueConst this_val,
                  int argc, JSValueConst *argv);
JSValue
nd_path2d_rect(JSContext *ctx, JSValueConst this_val,
               int argc, JSValueConst *argv);
JSValue
nd_path2d_roundRect(JSContext *ctx, JSValueConst this_val,
                    int argc, JSValueConst *argv);
JSValue
nd_path2d_addPath(JSContext *ctx, JSValueConst this_val,
                  int argc, JSValueConst *argv);
void
nd_path2d_attach_methods(JSContext *ctx, JSValueConst obj);
const char *
nd_svg_skip_ws(const char *p);
gboolean
nd_svg_read_number(const char **pp, double *out);
void
nd_path2d_arc_svg(cairo_t *cr, double x1, double y1,
                  double rx, double ry, double phi_deg,
                  gboolean large_arc, gboolean sweep,
                  double x2, double y2);
void
nd_path2d_parse_svg(cairo_t *cr, const char *d);
JSValue
nd_path2d_ctor(JSContext *ctx, JSValueConst this_val,
               int argc, JSValueConst *argv);
JSValue
nd_ctx_get_attrs(JSContext *ctx, JSValueConst this_val,
                 int argc, JSValueConst *argv);
JSValue
nd_ctx_is_context_lost(JSContext *ctx, JSValueConst this_val,
                       int argc, JSValueConst *argv);
JSValue
nd_ctx_draw_focus_if_needed(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv);
JSValue
nd_element_getContext(JSContext *ctx, JSValueConst this_val,
                      int argc, JSValueConst *argv);
cairo_status_t
nd_canvas_png_write(void *closure, const unsigned char *data, unsigned int length);
JSValue
nd_offscreen_convertToBlob(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv);

void nd_canvas_register_image_bitmap_class(JSRuntime *rt);
void nd_canvas_register_path2d_class(JSRuntime *rt);

#endif
