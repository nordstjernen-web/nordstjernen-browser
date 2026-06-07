/* Nordstjernen — per-window construction and lifecycle.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_WINDOW_H
#define NS_WINDOW_H

#include <gtk/gtk.h>

#include "bookmarks.h"
#include "csp.h"
#include "css.h"
#include "image.h"
#include "js.h"
#include "layout.h"
#include "pdf.h"
#include "selection.h"
#include "video.h"

G_BEGIN_DECLS

typedef struct ns_tab_worker ns_tab_worker;

typedef enum ns_view_mode {
    NS_VIEW_RENDER,
    NS_VIEW_RAW,
    NS_VIEW_DOM,
    NS_VIEW_LAYOUT,
} ns_view_mode;

typedef enum ns_load_stage {
    NS_STAGE_IDLE,
    NS_STAGE_CONNECTING,
    NS_STAGE_FETCHING,
    NS_STAGE_PARSING,
    NS_STAGE_STYLING,
    NS_STAGE_SCRIPTING,
    NS_STAGE_RENDERING,
    NS_STAGE_DONE,
} ns_load_stage;

typedef struct ns_window {
    guint         id;
    GtkWidget    *window;
    GtkWidget    *page_root;
    GtkWidget    *tab_button;
    GtkWidget    *tab_icon;
    GtkWidget    *tab_label;
    gboolean      favicon_loaded;
    GtkWidget    *url_entry;
    GtkWidget    *suggest_popover;
    GtkWidget    *suggest_list;
    GListStore   *suggest_model;
    gboolean      suggest_suppress;
    gboolean      url_focused;
    GtkWidget    *back_button;
    GtkWidget    *forward_button;
    GtkWidget    *home_button;
    GtkWidget    *reload_button;
    GtkWidget    *about_button;
    GtkWidget    *console_button;
    GtkWidget    *bookmarks_button;
    GtkWidget    *settings_button;
    GtkWidget    *go_button;
    GtkWidget    *stop_button;
    GtkWidget    *spinner;
    GtkWidget    *spinner_anim;
    GtkWidget    *stage_stack;
    ns_load_stage stage;
    guint         stage_done_source;
    GtkWidget    *logo_image;
    guint         logo_anim_source;
    int           logo_anim_index;
    GtkWidget    *content_stack;
    GtkWidget    *status_bar;
    GtkWidget    *text_view;
    GtkWidget    *drawing_area;
    guint         raf_tick_id;
    GtkAdjustment *render_vadj;
    ns_box       *layout_tree;
    GHashTable   *style_table;
    ns_node      *parsed_doc;
    ns_node      *focused_input;
    char         *focused_input_initial;
    gsize         caret_byte;
    gsize         sel_anchor_byte;
    GtkIMContext *im_context;
    guint         caret_blink_source;
    guint         refresh_source;
    guint         image_retry_source;
    guint         scroll_image_source;
    gboolean      caret_blink_on;
    GCancellable *current_fetch;
    guint         fetch_gen;
    ns_view_mode  mode;

    GPtrArray    *history;
    int           cursor;

    char         *last_body;
    gsize         last_body_len;
    char         *last_content_type;
    gboolean      dom_mutated;
    char         *pending_fragment;
    char         *lazy_url;
    ns_csp       *csp;

    double        zoom;
    double        pinch_base_zoom;

    GtkWidget    *search_revealer;
    GtkWidget    *search_entry;
    GtkWidget    *search_count_label;
    GtkWidget    *search_case_toggle;
    gboolean      search_case_sensitive;
    const ns_box *search_active_box;
    char         *search_query;

    ns_image_cache *images;
    ns_video_cache *videos;
    ns_tab_worker  *worker;
    ns_js          *js;
    struct ns_anim *anim;

    ns_pdf       *pdf;

    ns_selection  selection;
    char         *context_menu_link;
    char         *context_menu_image;
    char         *context_menu_selection;
    char         *context_menu_media;
    gboolean      context_menu_media_is_video;
    gboolean      context_menu_media_stream;
    double        drag_start_x;
    double        drag_start_y;
    double        cursor_x;
    double        cursor_y;
    const ns_node *hover_node;
    const ns_node *html_drag_source;
    const ns_node *html_drag_over;
    ns_js_drag_session *html_drag_session;
    gboolean      html_drag_active;
    gboolean      html_drag_can_drop;

    GPtrArray    *external_stylesheets;
    GHashTable   *external_css_seen;
    GHashTable   *external_css_loaded;
    GCancellable *css_cancellable;
    int          css_inflight;
    gboolean     busy;
    gboolean     first_paint_done;
    gint64       last_render_us;
    guint        js_relayout_idle_id;
    gint64       js_relayout_deadline_us;
    gint64       last_wheel_us;
    gboolean     layout_dirty;
    double       last_viewport_w;

    struct {
        GtkWidget     *window;
        GtkWidget     *notebook;
        GtkTextBuffer *buffer;
        GtkWidget     *entry;
        GtkTextBuffer *profile_buffer;
        GtkWidget     *profile_start_btn;
        GtkWidget     *profile_samples_spin;
        GtkWidget     *profile_interval_spin;
        GtkWidget     *profile_progress_label;
        gboolean       profile_running;
        GtkTextBuffer *dlog_buffer;
        guint          dlog_sub_id;
    } console;
} ns_window;

void ns_window_build_toolbar     (ns_window *w, GtkWidget *header,
                                  const char *home_url);
void ns_window_update_logo_loading(ns_window *w, gboolean loading);
void ns_window_set_stage(ns_window *w, ns_load_stage stage);
GArray *ns_logo_anim_frames(void);
void ns_window_build_search_bar  (ns_window *w, GtkWidget *vbox);
void ns_window_build_content     (ns_window *w, GtkWidget *vbox);

typedef enum ns_load_source {
    NS_LOAD_USER,
    NS_LOAD_HISTORY,
} ns_load_source;

ns_window *ns_window_for_id       (guint id);
void       ns_window_ensure_js    (ns_window *w);
void       ns_window_js_mutated   (gpointer user_data);
void       ns_window_ensure_layout(ns_window *w, double viewport_width);
const char *ns_window_current_url (ns_window *w);
char       *ns_window_current_title(ns_window *w);
void        ns_window_set_status  (ns_window *w, const char *fmt, ...)
                                   G_GNUC_PRINTF(2, 3);
double      ns_layout_viewport    (void);
void        ns_window_load_url    (ns_window *w, const char *raw_url,
                                   ns_load_source src);
char       *ns_resolve_url        (const ns_window *w, const char *href);
gboolean    ns_window_media_target(ns_window *w, const struct ns_box *hit,
                                   char **out_url, gboolean *out_is_video,
                                   gboolean *out_stream);
void        ns_window_render      (ns_window *w);
void        ns_window_update_nav_state(ns_window *w);
void        ns_spawn_window       (GtkApplication *app, const char *url);
ns_window  *ns_browser_add_tab    (GtkWidget *toplevel, GtkApplication *app,
                                   const char *url);
void        ns_browser_set_active (GtkWidget *toplevel, ns_window *w);
ns_bookmarks *ns_app_bookmarks    (void);
const char   *ns_app_home_url     (void);
void          ns_app_set_home_url (const char *url);

void on_back_clicked        (GtkButton *b, gpointer ud);
void on_forward_clicked     (GtkButton *b, gpointer ud);
void on_home_clicked        (GtkButton *b, gpointer ud);
void on_reload_clicked      (GtkButton *b, gpointer ud);
void on_entry_activate      (GtkEntry  *e, gpointer ud);
void on_go_clicked          (GtkButton *b, gpointer ud);
void on_stop_clicked        (GtkButton *b, gpointer ud);
gboolean on_url_entry_key_pressed(GtkEventControllerKey *ctrl, guint keyval,
                                  guint keycode, GdkModifierType state,
                                  gpointer ud);
void ns_window_setup_url_suggestions(ns_window *w);
void on_drawing_motion      (GtkEventControllerMotion *c, double x, double y, gpointer ud);
void on_drawing_leave       (GtkEventControllerMotion *c, gpointer ud);
gboolean ns_on_drawing_scroll(GtkEventControllerScroll *c, double dx, double dy, gpointer ud);
void ns_draw_render         (GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer ud);
void ns_on_drawing_pressed  (GtkGestureClick *g, int n, double x, double y, gpointer ud);
void ns_on_drawing_released (GtkGestureClick *g, int n, double x, double y, gpointer ud);
void ns_on_drawing_pressed_middle(GtkGestureClick *g, int n, double x, double y, gpointer ud);
void ns_on_drawing_side_pressed(GtkGestureClick *g, int n, double x, double y, gpointer ud);
void ns_on_drawing_zoom_begin(GtkGesture *g, GdkEventSequence *seq, gpointer ud);
void ns_on_drawing_zoom_end(GtkGesture *g, GdkEventSequence *seq, gpointer ud);
void ns_on_drawing_drag_begin (GtkGestureDrag *g, double x, double y, gpointer ud);
void ns_on_drawing_drag_update(GtkGestureDrag *g, double dx, double dy, gpointer ud);
void ns_on_drawing_drag_end   (GtkGestureDrag *g, double dx, double dy, gpointer ud);
gboolean ns_on_drawing_key_pressed (GtkEventControllerKey *c, guint kv, guint kc, GdkModifierType st, gpointer ud);
void     ns_on_drawing_key_released(GtkEventControllerKey *c, guint kv, guint kc, GdkModifierType st, gpointer ud);
gboolean ns_window_raf_tick        (GtkWidget *widget, GdkFrameClock *clock, gpointer ud);
void     ns_window_render_vadjustment_changed(GtkAdjustment *adj, gpointer ud);

G_END_DECLS

#endif
