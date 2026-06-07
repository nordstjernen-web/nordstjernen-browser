/* Nordstjernen — JavaScript engine binding (QuickJS).
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_JS_H
#define NS_JS_H

#include <glib.h>

#include <cairo.h>

#include "csp.h"
#include "dom.h"

G_BEGIN_DECLS

#define NS_JS_EVAL_BUDGET_MAX_MS 60000

typedef struct ns_js ns_js;
typedef struct ns_js_drag_session ns_js_drag_session;

typedef void (*ns_js_log_cb)(const char *line, gpointer user_data);
typedef void (*ns_js_mutated_cb)(gpointer user_data);
typedef void (*ns_js_navigate_cb)(const char *url, gboolean reload, gpointer user_data);
typedef void (*ns_js_scroll_to_cb)(const ns_node *target, gpointer user_data);
typedef void (*ns_js_form_submit_cb)(const ns_node *form, const ns_node *submitter,
                                     gpointer user_data);
typedef void (*ns_js_soft_nav_cb)(const char *url, gboolean replace, gpointer user_data);
typedef void (*ns_js_repaint_cb)(gpointer user_data);
typedef void (*ns_js_layout_flush_cb)(gpointer user_data);
typedef gboolean (*ns_js_clipboard_write_cb)(const char *text, gpointer user_data);
typedef void (*ns_js_window_action_cb)(const char *action, gpointer user_data);

ns_js *ns_js_new(ns_js_log_cb      log_cb,  gpointer log_user_data,
                 ns_js_mutated_cb  mut_cb,  gpointer mut_user_data,
                 ns_js_navigate_cb nav_cb,  gpointer nav_user_data);

void   ns_js_set_csp(ns_js *js, const ns_csp *csp);

const char *ns_js_engine_name(void);

void   ns_js_set_scroll_to_cb(ns_js *js, ns_js_scroll_to_cb cb, gpointer user_data);
void   ns_js_set_form_submit_cb(ns_js *js, ns_js_form_submit_cb cb, gpointer user_data);
void   ns_js_set_soft_nav_cb(ns_js *js, ns_js_soft_nav_cb cb, gpointer user_data);
void   ns_js_set_repaint_cb(ns_js *js, ns_js_repaint_cb cb, gpointer user_data);
void   ns_js_set_layout_flush_cb(ns_js *js, ns_js_layout_flush_cb cb, gpointer user_data);
void   ns_js_set_clipboard_write_cb(ns_js *js, ns_js_clipboard_write_cb cb,
                                    gpointer user_data);
void   ns_js_set_window_action_cb(ns_js *js, ns_js_window_action_cb cb,
                                  gpointer user_data);
const char *ns_js_current_url(const ns_js *js);
const char *ns_js_storage_partition(const ns_js *js);
void   ns_js_dispatch_hashchange(ns_js *js,
                                 const char *old_url, const char *new_url);
void   ns_js_free(ns_js *js);

void     ns_js_halt(ns_js *js);
gboolean ns_js_is_halted(const ns_js *js);
gboolean ns_js_in_pump(const ns_js *js);

void     ns_js_run_scripts_in_doc(ns_js *js, ns_node *doc, const char *base_url);

gboolean ns_js_consume_mutated(ns_js *js);

char  *ns_js_eval_source(ns_js *js, const char *src, const char *origin);

gboolean ns_js_dispatch_event(ns_js *js, const ns_node *target, const char *type,
                              gboolean *default_prevented);
gboolean ns_js_dispatch_beforematch(ns_js *js, const ns_node *target);
gboolean ns_js_dispatch_submit_event(ns_js *js, const ns_node *form,
                                     const ns_node *submitter,
                                     gboolean *default_prevented);

void ns_js_dialog_close(ns_js *js, ns_node *dialog, const char *return_value);
gboolean ns_js_close_topmost_modal_dialog(ns_js *js);

void           ns_js_set_focus(ns_js *js, const ns_node *el);
void           ns_js_set_focused_node(ns_js *js, const ns_node *el);
const ns_node *ns_js_focused_node(const ns_js *js);
const ns_node *ns_js_sequential_focus_target(ns_js *js, gboolean backward);
gboolean       ns_node_is_focusable(const ns_node *el);
void           ns_js_refresh_top_layer(ns_js *js);

void ns_js_details_toggle_open(ns_js *js, ns_node *details, gboolean open);

gboolean ns_js_run_animation_frame(ns_js *js);

typedef struct ns_anim ns_anim;
void ns_js_dispatch_anim_events(ns_js *js, ns_anim *anim);

void     ns_js_set_style_table(ns_js *js, GHashTable *styles);

struct ns_box;
void     ns_js_set_layout_root(ns_js *js, const struct ns_box *root);
void     ns_js_fire_media_load_events(ns_js *js, const struct ns_box *layout);
void     ns_js_set_selection(ns_js *js, const char *text, gboolean has_range,
                             double x, double y, double w, double h);

cairo_surface_t *ns_js_canvas_surface(ns_js *js, const ns_node *n);

void ns_js_request_repaint(ns_js *js);

struct ns_image_cache;
struct ns_image;
void ns_js_set_image_cache(ns_js *js, struct ns_image_cache *cache);
const struct ns_image *ns_js_image_for_node(ns_js *js, const ns_node *el);

gboolean ns_js_dispatch_key_event(ns_js *js, const ns_node *target,
                                  const char *type,
                                  const char *key, const char *code, int key_code,
                                  gboolean shift, gboolean ctrl,
                                  gboolean alt,   gboolean meta,
                                  gboolean *default_prevented);

gboolean ns_js_dispatch_mouse_event(ns_js *js, const ns_node *target,
                                    const char *type,
                                    double client_x, double client_y,
                                    double page_x, double page_y,
                                    int button, int buttons,
                                    gboolean shift, gboolean ctrl,
                                    gboolean alt,   gboolean meta,
                                    const ns_node *related,
                                    gboolean *default_prevented);

ns_js_drag_session *ns_js_drag_session_new(ns_js *js);
void                ns_js_drag_session_free(ns_js_drag_session *session);
void                ns_js_drag_session_set_data(ns_js_drag_session *session,
                                                const char *type,
                                                const char *data);
gboolean            ns_js_dispatch_drag_event(ns_js *js, ns_js_drag_session *session,
                                              const ns_node *target,
                                              const char *type,
                                              double client_x, double client_y,
                                              double page_x, double page_y,
                                              int button, int buttons,
                                              gboolean shift, gboolean ctrl,
                                              gboolean alt, gboolean meta,
                                              const ns_node *related,
                                              gboolean *default_prevented);

gboolean ns_js_dispatch_wheel_event(ns_js *js, const ns_node *target,
                                    double client_x, double client_y,
                                    double page_x, double page_y,
                                    double delta_x, double delta_y,
                                    gboolean shift, gboolean ctrl,
                                    gboolean alt,   gboolean meta,
                                    gboolean *default_prevented);

G_END_DECLS

#endif
