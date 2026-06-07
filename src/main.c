/* Nordstjernen — GTK 4 application shell.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include <gtk/gtk.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <cairo-pdf.h>
#include <librsvg/rsvg.h>
#include <math.h>

#ifdef G_OS_WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <glib-unix.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <stdlib.h>
#endif

#include "quickjs.h"
#include "anim.h"
#include "bcache.h"
#include "bookmarks.h"
#include "cache.h"
#include "config.h"
#include "history.h"
#include "console.h"
#include "css.h"
#include "ctxmenu.h"
#include "dialogs.h"
#include "debuglog.h"
#include "env.h"
#include "export.h"
#include "find.h"
#include "forms.h"
#include "headless.h"
#include "html.h"
#include "image.h"
#include "video.h"
#include "media.h"
#include "js.h"
#include "layout.h"
#include "mobile.h"
#include "net.h"
#include "paint.h"
#include "profiler.h"
#include "render.h"
#include "security.h"
#include "font.h"
#include "pdf.h"
#include "selection.h"
#include "tab_worker.h"
#include "version.h"
#include "watchdog.h"
#include "window.h"

#define NS_APP_ID     "com.nordstjernen.Browser"
#define NS_TITLE      "Nordstjernen"
#define NS_IMAGE_START_GAP_US (300 * G_TIME_SPAN_MILLISECOND)

static char         *g_startup_url_override;
static char         *g_screenshot_path;
static int           g_screenshot_delay_ms;
static int           g_screenshot_every_ms;
static char         *g_self_exe;
static char         *g_home_url;
static char         *g_watchdog_session_path;
static ns_bookmarks *g_bookmarks;
static GFileMonitor *g_bookmarks_monitor;

ns_bookmarks *ns_app_bookmarks(void) { return g_bookmarks; }
const char   *ns_app_home_url(void)  { return g_home_url; }
void ns_app_set_home_url(const char *url)
{
    g_free(g_home_url);
    g_home_url = g_strdup(url ? url : "");
}

static gboolean
ns_profile_enabled(void)
{
    static gint cached = -1;
    if (G_UNLIKELY(cached < 0))
        cached = g_getenv("NS_PROFILE") ? 1 : 0;
    return cached == 1;
}

double
ns_layout_viewport(void)
{
    const ns_config *c = ns_config_get();
    return c && c->layout_viewport_px > 0 ? (double)c->layout_viewport_px : 1000.0;
}

static void ns_window_sync_selection_to_js(ns_window *w);
static void ns_window_record_final_url(ns_window *w, const ns_response *resp);
static void ns_window_set_busy(ns_window *w, gboolean busy);
static void ns_window_clear_cache(ns_window *w);
static void ns_install_icon_search_paths(void);
static void ns_window_open(GtkApplication *app, const char *startup_url);
static void ns_browser_close_tab(ns_window *w);
static void ns_window_update_tab_label(ns_window *w);
static void ns_setup_bookmarks_watch(GtkApplication *app);
static void ns_window_kick_image_loads(ns_window *w);
static void ns_window_maybe_kick_visible_image_loads(ns_window *w,
                                                     gboolean force);
static void ns_window_kick_video_loads(ns_window *w);
static void ns_window_kick_favicon(ns_window *w);
static gboolean ns_window_scroll_image_load_cb(gpointer data);
static void        ns_window_js_log(const char *line, gpointer user_data);
static void        ns_window_js_soft_nav(const char *url, gboolean replace,
                                         gpointer user_data);
static void ns_window_install_actions(ns_window *w);
static void ns_window_kick_stylesheet_loads(ns_window *w);
static void ns_window_js_flush_layout(gpointer user_data);
static gboolean mixed_content_blocked(ns_window *w, const char *abs_url,
                                      const char *kind);
static gboolean csp_blocked(ns_window *w, ns_csp_kind kind, const char *abs_url,
                            const char *kind_word);
static gboolean ns_window_subresource_blocked(ns_window *w, const char *abs_url,
                                              ns_csp_kind csp_kind,
                                              const char *kind_word);
static void ns_window_apply_page_title(ns_window *w);
static void ns_window_apply_meta_refresh(ns_window *w, const ns_response *resp);
static gboolean ns_input_is_text_like(const ns_node *n);
static void ns_window_set_focused_input(ns_window *w, ns_node *target);
static void ns_window_open_select_popover(ns_window *w, ns_node *select_node,
                                          double x, double y);
static void ns_window_open_file_chooser(ns_window *w, ns_node *input);
static void ns_window_maybe_reset_form(ns_window *w, const ns_node *clicked);
static void ns_window_maybe_submit_form(ns_window *w, const ns_node *clicked);
static void ns_window_clear_html_drag(ns_window *w);
static int ns_dom_buttons_bit(int dom_button);
static void ns_on_fetch_done(GObject *src, GAsyncResult *result, gpointer user_data);

typedef struct ns_fetch_ctx {
    guint wid;
    guint gen;
} ns_fetch_ctx;

static gpointer
ns_fetch_ctx_new(ns_window *w)
{
    ns_fetch_ctx *c = g_new0(ns_fetch_ctx, 1);
    c->wid = w->id;
    c->gen = ++w->fetch_gen;
    return c;
}

static void     ns_window_mark_alive(ns_window *w);
static void     ns_window_mark_dead(ns_window *w);


void
ns_window_set_status(ns_window *w, const char *fmt, ...)
{
    if (!w || !w->status_bar) return;
    if (!fmt || !*fmt) {
        gtk_label_set_text(GTK_LABEL(w->status_bar), "");
        gtk_widget_set_visible(w->status_bar, FALSE);
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    char *text = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    gtk_label_set_text(GTK_LABEL(w->status_bar), text);
    gtk_widget_set_visible(w->status_bar, TRUE);
    g_free(text);
}

static void
ns_window_set_body_text(ns_window *w, const char *text, gssize len)
{
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->text_view));
    gtk_text_buffer_set_text(buf, text ? text : "", (int)len);
}

static gboolean
ns_image_retryable_failure(const ns_image *img)
{
    if (!img || !img->failed || img->attempts >= 3) return FALSE;
    if (img->http_status > 0 &&
        img->http_status != 408 &&
        img->http_status != 425 &&
        img->http_status != 429 &&
        img->http_status != 500 &&
        img->http_status != 502 &&
        img->http_status != 503 &&
        img->http_status != 504)
        return FALSE;
    return TRUE;
}

static guint
ns_image_retry_delay_ms(const ns_image *img)
{
    if (img && img->http_status == 429)
        return img->attempts <= 1 ? 15000 : 45000;
    return img && img->attempts <= 1 ? 2000 : 10000;
}

static gboolean
ns_window_image_retry_cb(gpointer user_data)
{
    ns_window *w = user_data;
    if (!w) return G_SOURCE_REMOVE;
    w->image_retry_source = 0;
    if (w->mode == NS_VIEW_RENDER) {
        ns_window_kick_image_loads(w);
        if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
    }
    return G_SOURCE_REMOVE;
}

static void
ns_window_schedule_image_retry(ns_window *w, const ns_image *img)
{
    if (!w || w->image_retry_source || !ns_image_retryable_failure(img))
        return;
    w->image_retry_source =
        g_timeout_add(ns_image_retry_delay_ms(img), ns_window_image_retry_cb, w);
}

static void
ns_window_schedule_image_kick(ns_window *w, guint delay_ms)
{
    if (!w || w->scroll_image_source) return;
    w->scroll_image_source =
        g_timeout_add(delay_ms, ns_window_scroll_image_load_cb, w);
}

static void
ns_window_sync_selection_to_js(ns_window *w)
{
    if (!w || !w->js) return;
    gboolean has = w->layout_tree && ns_selection_has_range(&w->selection);
    char *text = NULL;
    double x = 0, y = 0, sw = 0, sh = 0;
    if (has) {
        text = ns_selection_collect_text(w->layout_tree, &w->selection);
        ns_selection_bounds(w->layout_tree, &w->selection, &x, &y, &sw, &sh);
    }
    ns_js_set_selection(w->js, text ? text : "", has, x, y, sw, sh);
    g_free(text);
}

static void
ns_window_clear_html_drag(ns_window *w)
{
    if (!w) return;
    if (w->html_drag_session)
        ns_js_drag_session_free(w->html_drag_session);
    w->html_drag_session = NULL;
    w->html_drag_source = NULL;
    w->html_drag_over = NULL;
    w->html_drag_active = FALSE;
    w->html_drag_can_drop = FALSE;
}

static void
ns_window_drop_layout(ns_window *w)
{
    if (w->layout_tree) {
        if (w->js) ns_js_set_layout_root(w->js, NULL);
        ns_box_free(w->layout_tree);
        w->layout_tree = NULL;
        ns_selection_clear(&w->selection);
        w->search_active_box = NULL;
        w->hover_node = NULL;
    }
    if (w->style_table) {
        if (w->js) ns_js_set_style_table(w->js, NULL);
        g_hash_table_destroy(w->style_table);
        w->style_table = NULL;
    }
}

static void
ns_window_clear_cache(ns_window *w)
{
    g_clear_handle_id(&w->refresh_source, g_source_remove);
    g_clear_handle_id(&w->image_retry_source, g_source_remove);
    g_clear_handle_id(&w->scroll_image_source, g_source_remove);
    g_clear_pointer(&w->last_body, g_free);
    w->last_body_len = 0;
    g_clear_pointer(&w->last_content_type, g_free);
    if (w->csp) { if (w->js) ns_js_set_csp(w->js, NULL); ns_csp_free(w->csp); w->csp = NULL; }
    if (w->pdf) { ns_pdf_free(w->pdf); w->pdf = NULL; }
    ns_window_clear_html_drag(w);
    ns_window_drop_layout(w);
    w->focused_input = NULL;
    g_clear_pointer(&w->focused_input_initial, g_free);
    g_clear_handle_id(&w->caret_blink_source, g_source_remove);
    if (w->parsed_doc) { ns_node_free(w->parsed_doc); w->parsed_doc = NULL; }
    if (w->js)         { ns_js_free(w->js);           w->js         = NULL; }
    if (w->css_cancellable) {
        g_cancellable_cancel(w->css_cancellable);
        g_clear_object(&w->css_cancellable);
    }
    if (w->external_css_loaded)
        g_hash_table_remove_all(w->external_css_loaded);
    if (w->external_stylesheets) {
        for (guint i = 0; i < w->external_stylesheets->len; i++)
            ns_css_stylesheet_free(g_ptr_array_index(w->external_stylesheets, i));
        g_ptr_array_set_size(w->external_stylesheets, 0);
    }
    if (w->external_css_seen)
        g_hash_table_remove_all(w->external_css_seen);
    w->css_inflight = 0;
    w->first_paint_done = FALSE;
    w->layout_dirty = TRUE;
    w->layout_dirty_reason = NULL;
    w->favicon_loaded = FALSE;
    g_clear_handle_id(&w->js_relayout_idle_id, g_source_remove);
    w->js_relayout_deadline_us = 0;
    w->last_wheel_us = 0;
}

static void
ns_adjustment_scroll_to(GtkAdjustment *adj, double y)
{
    double upper = gtk_adjustment_get_upper(adj);
    double page  = gtk_adjustment_get_page_size(adj);
    if (y > upper - page) y = upper - page;
    if (y < 0) y = 0;
    gtk_adjustment_set_value(adj, y);
}

static void
ns_window_scroll_to_fragment(ns_window *w)
{
    if (!w->pending_fragment || !w->render_vadj) return;
    if (!*w->pending_fragment ||
        g_ascii_strcasecmp(w->pending_fragment, "top") == 0) {
        ns_adjustment_scroll_to(w->render_vadj, 0);
        g_free(w->pending_fragment);
        w->pending_fragment = NULL;
        return;
    }
    if (!w->layout_tree) return;
    const ns_box *target =
        ns_box_find_by_id_or_name(w->layout_tree, w->pending_fragment);
    if (!target) return;
    ns_adjustment_scroll_to(w->render_vadj, target->y);
    g_free(w->pending_fragment);
    w->pending_fragment = NULL;
}

typedef enum ns_fragment_reveal_kind {
    NS_FRAGMENT_REVEAL_HIDDEN,
    NS_FRAGMENT_REVEAL_DETAILS,
} ns_fragment_reveal_kind;

typedef struct ns_fragment_reveal {
    ns_node *node;
    ns_fragment_reveal_kind kind;
} ns_fragment_reveal;

static void
ns_fragment_reveal_add(GArray *items, ns_node *node,
                       ns_fragment_reveal_kind kind)
{
    ns_fragment_reveal item = { node, kind };
    g_array_append_val(items, item);
}

static gboolean
ns_window_reveal_pending_fragment(ns_window *w, gboolean fire_event)
{
    if (!w || !w->parsed_doc || !w->pending_fragment ||
        !*w->pending_fragment ||
        g_ascii_strcasecmp(w->pending_fragment, "top") == 0)
        return FALSE;
    ns_node *target =
        ns_node_find_fragment_target(w->parsed_doc, w->pending_fragment);
    if (!target) return FALSE;
    GArray *items = g_array_new(FALSE, FALSE, sizeof(ns_fragment_reveal));
    for (ns_node *cur = target; cur; cur = cur->parent) {
        if (ns_element_hidden_until_found(cur))
            ns_fragment_reveal_add(items, cur, NS_FRAGMENT_REVEAL_HIDDEN);
        if (cur->parent && ns_details_fragment_needs_open(cur->parent, cur))
            ns_fragment_reveal_add(items, cur->parent,
                                   NS_FRAGMENT_REVEAL_DETAILS);
        if (cur == w->parsed_doc) break;
    }
    gboolean changed = FALSE;
    for (guint i = 0; i < items->len; i++) {
        ns_fragment_reveal item = g_array_index(items, ns_fragment_reveal, i);
        ns_node *el = item.node;
        if (ns_node_root(el) != w->parsed_doc) break;
        if (item.kind == NS_FRAGMENT_REVEAL_HIDDEN) {
            if (!ns_element_hidden_until_found(el)) continue;
            if (fire_event && w->js) {
                ns_js_dispatch_beforematch(w->js, el);
                if (ns_js_consume_mutated(w->js)) changed = TRUE;
            }
            if (ns_node_root(el) != w->parsed_doc) break;
            if (!ns_element_hidden_until_found(el)) continue;
            ns_element_remove_attr(el, "hidden");
            changed = TRUE;
        } else if (!ns_element_get_attr(el, "open")) {
            ns_element_set_attr(el, "open", "");
            if (fire_event && w->js)
                ns_js_details_toggle_open(w->js, el, TRUE);
            changed = TRUE;
        }
    }
    g_array_free(items, TRUE);
    if (changed) {
        ns_window_drop_layout(w);
        w->layout_dirty = TRUE;
    }
    return changed;
}

static void
ns_window_js_log(const char *line, gpointer user_data)
{
    ns_window *w = user_data;
    if (!w || !line) return;
    const char *nl = strchr(line, '\n');
    if (nl) {
        char *first = g_strndup(line, (gsize)(nl - line));
        ns_window_set_status(w, "JS: %s", first);
        g_free(first);
    } else {
        ns_window_set_status(w, "JS: %s", line);
    }
    ns_window_console_append(w, line);
}

static guint ns_window_relayout_max_delay(ns_window *w, guint max_delay);
static guint ns_window_scroll_quiet_ms(ns_window *w);
static guint ns_window_adaptive_relayout_delay(ns_window *w, guint min_delay,
                                               guint max_delay);
static void ns_window_schedule_relayout(ns_window *w, guint delay,
                                        const char *reason);
static void ns_window_profile_relayout_timer(ns_window *w, const char *action,
                                             const char *reason, guint delay,
                                             gint64 now);

static gboolean
ns_window_js_relayout_now(gpointer user_data)
{
    ns_window *w = user_data;
    if (!w) return G_SOURCE_REMOVE;
    w->js_relayout_idle_id = 0;
    w->js_relayout_deadline_us = 0;
    gint64 now = g_get_monotonic_time();
    if (w->last_wheel_us > 0) {
        guint quiet_ms = ns_window_scroll_quiet_ms(w);
        gint64 quiet_until = w->last_wheel_us + (gint64)quiet_ms * 1000;
        if (quiet_until > now) {
            guint delay = (guint)((quiet_until - now + 999) / 1000);
            guint min_delay = w->last_render_us > 250000 ? 250 : delay;
            guint max_delay = ns_window_relayout_max_delay(w, 1500);
            guint adaptive = ns_window_adaptive_relayout_delay(
                w, min_delay, max_delay);
            if (delay < adaptive) delay = adaptive;
            ns_window_profile_relayout_timer(w, "defer-scroll",
                                             w->layout_dirty_reason,
                                             delay, now);
            ns_window_schedule_relayout(w, delay,
                w->layout_dirty_reason ? w->layout_dirty_reason : "scroll");
            return G_SOURCE_REMOVE;
        }
    }
    ns_window_drop_layout(w);
    w->layout_dirty = TRUE;
    if (!w->layout_dirty_reason) w->layout_dirty_reason = "scheduled";
    ns_window_profile_relayout_timer(w, "run", w->layout_dirty_reason, 0, now);
    if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
    ns_window_apply_page_title(w);
    return G_SOURCE_REMOVE;
}

static guint
ns_window_adaptive_relayout_delay(ns_window *w, guint min_delay,
                                  guint max_delay)
{
    guint delay = min_delay;
    double render_ms = w ? w->last_render_us / 1000.0 : 0;
    if (render_ms > 5) {
        guint adaptive = (guint)(render_ms * 3);
        if (adaptive > delay) delay = adaptive;
    }
    if (delay > max_delay) delay = max_delay;
    return delay;
}

static guint
ns_window_relayout_max_delay(ns_window *w, guint max_delay)
{
    if (!w) return max_delay;
    if (w->last_render_us > 1000000 && max_delay < 3500) return 3500;
    if (w->last_render_us > 250000 && max_delay < 2500) return 2500;
    return max_delay;
}

static guint
ns_window_scroll_quiet_ms(ns_window *w)
{
    if (!w) return 350;
    if (w->last_render_us > 1000000) return 1200;
    if (w->last_render_us > 250000) return 700;
    return 350;
}

static void
ns_window_profile_relayout_timer(ns_window *w, const char *action,
                                 const char *reason, guint delay,
                                 gint64 now)
{
    if (!ns_profile_enabled()) return;
    double render_ms = w ? w->last_render_us / 1000.0 : 0;
    double wheel_ms = -1.0;
    guint pending = w ? w->js_relayout_idle_id : 0;
    gboolean dirty = w ? w->layout_dirty : FALSE;
    if (w && w->last_wheel_us > 0)
        wheel_ms = (now - w->last_wheel_us) / 1000.0;
    g_printerr("[profile] relayout %s reason=%s delay=%ums "
               "render=%.1fms since-wheel=%.1fms pending=%u dirty=%d\n",
               action ? action : "?",
               reason ? reason : (w && w->layout_dirty_reason
                   ? w->layout_dirty_reason : "?"),
               delay, render_ms, wheel_ms, pending, dirty);
}

static void
ns_window_schedule_relayout(ns_window *w, guint delay, const char *reason)
{
    if (!w) return;
    if (reason) w->layout_dirty_reason = reason;
    gint64 now = g_get_monotonic_time();
    if (w->last_wheel_us > 0) {
        gint64 quiet_until =
            w->last_wheel_us + (gint64)ns_window_scroll_quiet_ms(w) * 1000;
        if (quiet_until > now) {
            guint quiet_delay = (guint)((quiet_until - now + 999) / 1000);
            if (delay < quiet_delay) delay = quiet_delay;
        }
    }
    gint64 deadline = now + (gint64)delay * 1000;
    if (w->js_relayout_idle_id) {
        if (w->js_relayout_deadline_us &&
            w->js_relayout_deadline_us <= deadline) {
            guint keep_delay = 0;
            if (w->js_relayout_deadline_us > now)
                keep_delay =
                    (guint)((w->js_relayout_deadline_us - now + 999) / 1000);
            ns_window_profile_relayout_timer(w, "keep",
                                             w->layout_dirty_reason,
                                             keep_delay, now);
            return;
        }
        ns_window_profile_relayout_timer(w, "replace",
                                         w->layout_dirty_reason, delay, now);
        g_clear_handle_id(&w->js_relayout_idle_id, g_source_remove);
    }
    w->js_relayout_deadline_us = deadline;
    w->js_relayout_idle_id =
        g_timeout_add(delay, ns_window_js_relayout_now, w);
    ns_window_profile_relayout_timer(w, "schedule",
                                     w->layout_dirty_reason, delay, now);
}

static void
ns_window_postpone_relayout_after_wheel(ns_window *w)
{
    if (!w || !w->js_relayout_idle_id) return;
    guint delay = ns_window_adaptive_relayout_delay(
        w, 350, ns_window_relayout_max_delay(w, 1500));
    gint64 now = g_get_monotonic_time();
    gint64 deadline = now + (gint64)delay * 1000;
    if (w->js_relayout_deadline_us &&
        w->js_relayout_deadline_us >= deadline) {
        ns_window_profile_relayout_timer(w, "wheel-keep",
                                         w->layout_dirty_reason,
                                         delay, now);
        return;
    }
    ns_window_profile_relayout_timer(w, "wheel-postpone",
                                     w->layout_dirty_reason, delay, now);
    g_clear_handle_id(&w->js_relayout_idle_id, g_source_remove);
    w->js_relayout_deadline_us = deadline;
    w->js_relayout_idle_id =
        g_timeout_add(delay, ns_window_js_relayout_now, w);
}

void
ns_window_js_mutated(gpointer user_data)
{
    ns_window *w = user_data;
    if (!w) return;
    w->dom_mutated = TRUE;
    ns_window_schedule_relayout(w,
        ns_window_adaptive_relayout_delay(
            w, 16, ns_window_relayout_max_delay(w, 1500)), "js");
}

static void
ns_window_js_scroll_to(const ns_node *target, gpointer user_data)
{
    ns_window *w = user_data;
    if (!w || !target || !w->layout_tree || !w->render_vadj) return;
    const char *id = ns_element_get_attr(target, "id");
    if (!id || !*id) return;
    const ns_box *box = ns_box_find_by_id(w->layout_tree, id);
    if (!box) return;
    ns_adjustment_scroll_to(w->render_vadj, box->y);
}

static void
ns_window_js_repaint(gpointer user_data)
{
    ns_window *w = user_data;
    if (!w || !w->drawing_area) return;
    gtk_widget_queue_draw(w->drawing_area);
}

static gboolean
ns_window_js_clipboard_write(const char *text, gpointer user_data)
{
    ns_window *w = user_data;
    if (!w || !w->window) return FALSE;
    GdkClipboard *cb = gtk_widget_get_clipboard(GTK_WIDGET(w->window));
    if (!cb) return FALSE;
    gdk_clipboard_set_text(cb, text ? text : "");
    return TRUE;
}

static void
ns_window_js_window_action(const char *action, gpointer user_data)
{
    ns_window *w = user_data;
    if (!w || !w->window || !action) return;
    if (g_strcmp0(action, "focus") == 0) {
        if (GTK_IS_WINDOW(w->window))
            gtk_window_present(GTK_WINDOW(w->window));
        return;
    }
    if (g_strcmp0(action, "print") != 0 && g_strcmp0(action, "stop") != 0)
        return;
    if (g_action_group_has_action(G_ACTION_GROUP(w->window), action))
        g_action_group_activate_action(G_ACTION_GROUP(w->window), action, NULL);
}

static void
ns_window_js_form_submit(const ns_node *form, const ns_node *submitter,
                         gpointer user_data)
{
    ns_window *w = user_data;
    if (!w || !form) return;
    ns_window_maybe_submit_form(w, submitter ? submitter : form);
}

static void
ns_window_js_navigate(const char *url, gboolean reload, gpointer user_data)
{
    ns_window *w = user_data;
    if (!w || !url) return;
    char *resolved = NULL;
    if (!reload && url[0] && !strstr(url, "://") &&
        !g_str_has_prefix(url, "about:") &&
        !g_str_has_prefix(url, "data:") &&
        !g_str_has_prefix(url, "mailto:")) {
        resolved = ns_resolve_url(w, url);
    }
    const char *target = resolved ? resolved : url;
    if (reload) {
        ns_window_load_url(w, target, NS_LOAD_HISTORY);
    } else {
        ns_window_load_url(w, target, NS_LOAD_USER);
    }
    g_free(resolved);
}

#define NS_HISTORY_MAX 512

static void
ns_window_history_append(ns_window *w, const char *url)
{
    if (!w->history) return;
    while ((int)w->history->len > w->cursor + 1) {
        g_free(g_ptr_array_index(w->history, w->history->len - 1));
        g_ptr_array_set_size(w->history, w->history->len - 1);
    }
    g_ptr_array_add(w->history, g_strdup(url));
    while (w->history->len > NS_HISTORY_MAX) {
        g_free(g_ptr_array_index(w->history, 0));
        g_ptr_array_remove_index(w->history, 0);
    }
    w->cursor = (int)w->history->len - 1;
}

static void
ns_window_js_soft_nav(const char *url, gboolean replace, gpointer user_data)
{
    ns_window *w = user_data;
    if (!w || !url) return;
    if (w->url_entry) {
        char *disp = ns_url_to_display(url);
        gtk_editable_set_text(GTK_EDITABLE(w->url_entry), disp ? disp : url);
        g_free(disp);
    }
    if (!w->history) return;
    if (replace) {
        if (w->cursor >= 0 && w->cursor < (int)w->history->len) {
            g_free(g_ptr_array_index(w->history, w->cursor));
            w->history->pdata[w->cursor] = g_strdup(url);
        } else {
            g_ptr_array_add(w->history, g_strdup(url));
            w->cursor = (int)w->history->len - 1;
        }
    } else {
        ns_window_history_append(w, url);
    }
    ns_window_update_nav_state(w);
}

static void
ns_window_maybe_reset_form(ns_window *w, const ns_node *clicked)
{
    if (!clicked) return;
    if (ns_element_effectively_disabled(clicked)) return;
    if (ns_element_effectively_inert(clicked)) return;
    if (!ns_form_is_reset_trigger(clicked)) return;
    const ns_node *doc = w && w->parsed_doc ? w->parsed_doc : ns_node_root(clicked);
    ns_node *form = (ns_node *)ns_form_owner(clicked, doc);
    if (!form) return;
    if (w->js) {
        gboolean prevented = FALSE;
        ns_js_dispatch_event(w->js, form, "reset", &prevented);
        if (ns_js_consume_mutated(w->js)) ns_window_js_mutated(w);
        if (prevented) return;
    }
    ns_form_reset_owned_controls(form, (ns_node *)(doc ? doc : form),
                                 doc ? doc : form);
    ns_window_js_mutated(w);
}

static void
ns_window_maybe_submit_form(ns_window *w, const ns_node *clicked)
{
    if (!clicked) return;
    if (ns_element_effectively_disabled(clicked)) return;
    if (ns_element_effectively_inert(clicked)) return;
    if (ns_form_is_reset_trigger(clicked)) {
        ns_window_maybe_reset_form(w, clicked);
        return;
    }
    gboolean from_text_input = ns_input_is_text_like(clicked);
    gboolean from_js = ns_node_is_element_named(clicked, "form");
    if (!from_text_input && !from_js && !ns_form_is_submit_trigger(clicked)) return;
    const ns_node *doc = w && w->parsed_doc ? w->parsed_doc : ns_node_root(clicked);
    const ns_node *form = from_js ? clicked : ns_form_owner(clicked, doc);
    if (!form) return;

    if (!ns_element_get_attr(form, "novalidate") &&
        (!clicked || !ns_element_get_attr(clicked, "formnovalidate"))) {
        const ns_node *bad = ns_form_first_invalid(form, doc ? doc : form,
                                                   doc ? doc : form);
        if (bad) {
            if (ns_input_is_text_like(bad)) {
                ns_window_set_focused_input(w, (ns_node *)bad);
                if (w->drawing_area) gtk_widget_grab_focus(w->drawing_area);
            }
            if (w->js)
                ns_js_dispatch_event(w->js, bad, "invalid", NULL);
            const char *name = ns_element_get_attr(bad, "name");
            ns_window_set_status(w, "Please fill out the %s field correctly",
                                 name && *name ? name : "highlighted");
            return;
        }
    }

    if (w->js) {
        gboolean prevented = FALSE;
        ns_js_dispatch_submit_event(w->js, form, clicked, &prevented);
        if (ns_js_consume_mutated(w->js)) ns_window_js_mutated(w);
        if (prevented) return;
    }

    const char *method = ns_element_get_attr(form, "method");
    const char *formmethod = (!from_text_input && clicked) ?
        ns_element_get_attr(clicked, "formmethod") : NULL;
    if (formmethod && *formmethod) method = formmethod;

    if (method && g_ascii_strcasecmp(method, "dialog") == 0) {
        const ns_node *dialog = form;
        while (dialog && !ns_node_is_element_named(dialog, "dialog"))
            dialog = dialog->parent;
        if (dialog && w->js) {
            const char *rv = NULL;
            if (!from_text_input && clicked && ns_form_is_submit_trigger(clicked))
                rv = ns_element_get_attr(clicked, "value");
            ns_js_dialog_close(w->js, (ns_node *)dialog, rv);
            if (ns_js_consume_mutated(w->js)) ns_window_js_mutated(w);
        }
        return;
    }
    gboolean is_post = method && g_ascii_strcasecmp(method, "post") == 0;

    const char *enctype = ns_element_get_attr(form, "enctype");
    const char *formenctype = (!from_text_input && clicked) ?
        ns_element_get_attr(clicked, "formenctype") : NULL;
    if (formenctype && *formenctype) enctype = formenctype;
    const ns_node *root = doc ? doc : form;
    gboolean has_files = ns_form_has_file_upload(form, root, root);
    gboolean use_multipart = is_post &&
        (has_files ||
         (enctype && g_ascii_strcasecmp(enctype, "multipart/form-data") == 0));

    GString *body = NULL;
    char *content_type = NULL;
    if (use_multipart) {
        char *boundary = ns_multipart_boundary();
        body = g_string_new(NULL);
        ns_form_collect_multipart(form, root, root, body, boundary, clicked);
        g_string_append_printf(body, "--%s--\r\n", boundary);
        content_type = g_strdup_printf("multipart/form-data; boundary=%s",
                                       boundary);
        g_free(boundary);
    } else {
        body = g_string_new(NULL);
        gboolean first = TRUE;
        ns_form_collect_inputs(form, root, root, body, &first, clicked);
        content_type = g_strdup("application/x-www-form-urlencoded");
    }

    const char *action = ns_element_get_attr(form, "action");
    const char *formaction = (!from_text_input && clicked) ?
        ns_element_get_attr(clicked, "formaction") : NULL;
    if (formaction && *formaction) action = formaction;
    char *abs_action;
    if (!action || !*action) abs_action = g_strdup(ns_window_current_url(w));
    else                      abs_action = ns_resolve_url(w, action);
    if (!abs_action) {
        g_string_free(body, TRUE);
        g_free(content_type);
        return;
    }

    if (is_post) {
        if (w->current_fetch) {
            g_cancellable_cancel(w->current_fetch);
            g_clear_object(&w->current_fetch);
        }
        ns_window_history_append(w, abs_action);
        char *disp = ns_url_to_display(abs_action);
        gtk_editable_set_text(GTK_EDITABLE(w->url_entry),
                              disp ? disp : abs_action);
        g_free(disp);
        w->current_fetch = g_cancellable_new();
        ns_window_set_busy(w, TRUE);
        ns_window_update_nav_state(w);
        ns_window_set_status(w, "POST %s …", abs_action);
        const char *nav_headers[] = { "X-ND-Navigate: 1", NULL };
        ns_net_request_async(abs_action, ns_window_current_url(w),
                             "POST", body->str, body->len,
                             content_type, nav_headers,
                             w->current_fetch, ns_on_fetch_done,
                             ns_fetch_ctx_new(w));
        g_free(abs_action);
        g_string_free(body, TRUE);
        g_free(content_type);
        return;
    }
    GString *query = body;
    g_free(content_type);

    if (query->len == 0) {
        ns_window_load_url(w, abs_action, NS_LOAD_USER);
        g_free(abs_action);
        g_string_free(query, TRUE);
        return;
    }

    char *frag = strchr(abs_action, '#');
    if (frag) *frag = '\0';
    char *sep = strchr(abs_action, '?');
    char *full = sep ? g_strdup_printf("%s&%s", abs_action, query->str)
                     : g_strdup_printf("%s?%s", abs_action, query->str);
    g_free(abs_action);
    g_string_free(query, TRUE);
    ns_window_load_url(w, full, NS_LOAD_USER);
    g_free(full);
}

static void
ns_window_set_title_if_active(ns_window *w, const char *full)
{
    ns_window *active = w->window
        ? g_object_get_data(G_OBJECT(w->window), "nd-window") : NULL;
    if (active == w)
        gtk_window_set_title(GTK_WINDOW(w->window), full);
}

static void
ns_window_apply_page_title(ns_window *w)
{
    ns_window_update_tab_label(w);
    if (!w->parsed_doc) {
        ns_window_set_title_if_active(w, NS_TITLE);
        return;
    }
    ns_node *title = ns_node_find_first_element(w->parsed_doc, "title");
    if (!title) {
        ns_window_set_title_if_active(w, NS_TITLE);
        return;
    }
    char *raw = ns_node_collect_text(title);
    if (!raw || !*raw) { g_free(raw); ns_window_set_title_if_active(w, NS_TITLE); return; }
    GString *trimmed = g_string_new(NULL);
    gboolean prev_ws = TRUE;
    for (const char *p = raw; *p; p++) {
        char c = *p;
        gboolean ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f');
        if (ws) { if (!prev_ws) g_string_append_c(trimmed, ' '); prev_ws = TRUE; }
        else    { g_string_append_c(trimmed, c); prev_ws = FALSE; }
    }
    if (trimmed->len > 0 && trimmed->str[trimmed->len - 1] == ' ')
        g_string_set_size(trimmed, trimmed->len - 1);
    g_free(raw);

    if (trimmed->len > 0) {
        char *full = g_strdup_printf("%s — %s", trimmed->str, NS_TITLE);
        ns_window_set_title_if_active(w, full);
        g_free(full);
        if (w->drawing_area) {
            char *aria = g_strdup_printf("Web page: %s", trimmed->str);
            gtk_accessible_update_property(GTK_ACCESSIBLE(w->drawing_area),
                GTK_ACCESSIBLE_PROPERTY_LABEL, aria, -1);
            g_free(aria);
        }
    } else {
        ns_window_set_title_if_active(w, NS_TITLE);
    }
    if (trimmed->len > 0 && w->history && w->cursor >= 0 &&
        w->cursor < (int)w->history->len) {
        const char *cur = g_ptr_array_index(w->history, w->cursor);
        if (cur) ns_history_record(cur, trimmed->str);
    }
    g_string_free(trimmed, TRUE);
}

gboolean
ns_window_raf_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer ud)
{
    (void)widget; (void)clock;
    ns_window *w = ud;
    if (!w) return G_SOURCE_CONTINUE;
    gboolean redraw = FALSE;
    gint64 now_us = g_get_monotonic_time();
    if (w->images && ns_image_cache_tick(w->images, now_us))
        redraw = TRUE;
    if (w->anim && ns_anim_tick(w->anim, now_us))
        redraw = TRUE;
    if (w->anim && w->js)
        ns_js_dispatch_anim_events(w->js, w->anim);
    if (w->js && ns_js_run_animation_frame(w->js)) {
        if (ns_js_consume_mutated(w->js)) {
            ns_window_js_mutated(w);
            return G_SOURCE_CONTINUE;
        }
        redraw = TRUE;
    }
    if (redraw && w->drawing_area)
        gtk_widget_queue_draw(w->drawing_area);
    return G_SOURCE_CONTINUE;
}

static char *
ns_window_render_resolve(const char *href, gpointer ud)
{
    return ns_resolve_url((const ns_window *)ud, href);
}

static gboolean
ns_rel_has_token(const char *rel, const char *token)
{
    if (!rel || !token || !*token) return FALSE;
    gchar **parts = g_strsplit_set(rel, " \t\r\n\f", -1);
    gboolean found = FALSE;
    for (gchar **p = parts; *p; p++) {
        if (**p && g_ascii_strcasecmp(*p, token) == 0) {
            found = TRUE;
            break;
        }
    }
    g_strfreev(parts);
    return found;
}

static gboolean
ns_rel_is_stylesheet(const char *rel)
{
    return ns_rel_has_token(rel, "stylesheet") &&
           !ns_rel_has_token(rel, "alternate");
}

static gboolean
ns_window_render_font_allowed(const char *abs_url, gpointer ud)
{
    return !ns_window_subresource_blocked((ns_window *)ud, abs_url,
                                          NS_CSP_FONT, "font");
}

static void
ns_window_mark_layout_dirty(ns_window *w)
{
    if (!w) return;
    if (w->layout_tree) {
        if (w->js) ns_js_set_layout_root(w->js, NULL);
        ns_box_free(w->layout_tree);
        w->layout_tree = NULL;
        ns_selection_clear(&w->selection);
        w->search_active_box = NULL;
        w->hover_node = NULL;
    }
    if (w->style_table) {
        if (w->js) ns_js_set_style_table(w->js, NULL);
        g_hash_table_destroy(w->style_table);
        w->style_table = NULL;
    }
    w->layout_dirty = TRUE;
    w->layout_dirty_reason = "style";
}

static void
ns_window_append_stylesheet_expanded(ns_window *w, GPtrArray *out,
                                     ns_css_stylesheet *sh,
                                     const char *base_url,
                                     GHashTable *seen,
                                     int depth)
{
    if (!out || !sh) return;
    if (depth < NS_CSS_IMPORT_MAX_DEPTH && sh->imports) {
        for (guint i = 0; i < sh->imports->len; i++) {
            ns_css_import *im = &g_array_index(sh->imports, ns_css_import, i);
            if (!im->url || !*im->url) continue;
            if (im->media && *im->media &&
                !ns_css_media_query_matches(im->media))
                continue;
            char *abs = ns_url_resolve(base_url, im->url);
            if (!abs) continue;
            if (seen && g_hash_table_contains(seen, abs)) {
                g_free(abs);
                continue;
            }
            if (w && ns_window_subresource_blocked(w, abs, NS_CSP_STYLE,
                                                   "stylesheet")) {
                g_free(abs);
                continue;
            }
            if (seen) g_hash_table_add(seen, g_strdup(abs));
            GError *err = NULL;
            ns_response *resp = ns_net_request_blocking(
                abs, w ? ns_window_current_url(w) : NULL, "GET",
                NULL, 0, NULL, NULL, w ? w->css_cancellable : NULL, &err);
            if (err) {
                if (w && !g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                    char *line = g_strdup_printf("[error] stylesheet: %s — %s",
                                                 abs, err->message);
                    ns_window_console_append(w, line);
                    g_free(line);
                }
                g_clear_error(&err);
            } else if (resp && !resp->error && resp->status < 400 &&
                       resp->body && resp->body->len > 0) {
                ns_css_stylesheet *child = ns_css_stylesheet_parse(
                    (const char *)resp->body->data, (gssize)resp->body->len);
                if (child) {
                    if (im->layer_name)
                        ns_css_stylesheet_force_layer(child, im->layer_name);
                    ns_window_append_stylesheet_expanded(w, out, child, abs,
                                                         seen, depth + 1);
                }
            } else if (w && resp) {
                char *line = resp->error
                    ? g_strdup_printf("[error] stylesheet: %s — %s",
                                      abs, resp->error)
                    : g_strdup_printf("[error] stylesheet: %s — HTTP %ld",
                                      abs, resp->status);
                ns_window_console_append(w, line);
                g_free(line);
            }
            if (resp) ns_response_free(resp);
            g_free(abs);
        }
    }
    ns_css_stylesheet_resolve_urls(sh, base_url);
    g_ptr_array_add(out, sh);
}

static void
ns_window_store_external_stylesheet_group(ns_window *w, const char *url,
                                          guint first_index)
{
    if (!w || !url || !w->external_stylesheets ||
        first_index >= w->external_stylesheets->len)
        return;
    if (!w->external_css_loaded)
        w->external_css_loaded =
            g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                  (GDestroyNotify)g_ptr_array_unref);
    GPtrArray *group = g_ptr_array_new();
    for (guint i = first_index; i < w->external_stylesheets->len; i++)
        g_ptr_array_add(group, g_ptr_array_index(w->external_stylesheets, i));
    g_hash_table_replace(w->external_css_loaded, g_strdup(url), group);
}

static void
ns_window_append_owned_stylesheet_range(GPtrArray *page_sheets,
                                        GPtrArray *owned_sheets,
                                        guint first_index)
{
    if (!page_sheets || !owned_sheets || first_index >= page_sheets->len)
        return;
    for (guint i = first_index; i < page_sheets->len; i++)
        g_ptr_array_add(owned_sheets, g_ptr_array_index(page_sheets, i));
}

static const char *
ns_window_node_base_url(ns_window *w, const ns_node *n)
{
    for (const ns_node *a = n; a; a = a->parent) {
        if (!ns_node_is_element_named(a, "iframe")) continue;
        const char *furl = ns_element_get_attr(a, "data-nd-frame-url");
        if (furl && *furl) return furl;
    }
    return ns_window_current_url(w);
}

static char *
ns_window_resolve_for_node(ns_window *w, const ns_node *n, const char *href)
{
    const char *base = ns_window_node_base_url(w, n);
    if (base != ns_window_current_url(w))
        return ns_url_resolve(base, href);
    return ns_resolve_url(w, href);
}

typedef struct {
    ns_window  *w;
    GPtrArray  *page_sheets;
    GPtrArray  *owned_sheets;
    GString    *run;
    const char *run_base;
} ns_sheet_collect_ctx;

static void
ns_window_sheet_run_flush(ns_sheet_collect_ctx *cc)
{
    if (!cc->run || cc->run->len == 0) return;
    ns_css_stylesheet *sh =
        ns_css_merged_styles_cached(cc->run->str, (gssize)cc->run->len);
    if (sh) {
        GHashTable *seen =
            g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        guint first = cc->page_sheets->len;
        ns_window_append_stylesheet_expanded(cc->w, cc->page_sheets, sh,
                                             cc->run_base, seen, 0);
        ns_window_append_owned_stylesheet_range(cc->page_sheets,
                                                cc->owned_sheets, first);
        g_hash_table_destroy(seen);
    }
    g_string_set_size(cc->run, 0);
    cc->run_base = NULL;
}

static void
ns_window_collect_page_stylesheets_walk(ns_node *n, ns_sheet_collect_ctx *cc)
{
    if (!n || ns_node_is_element_named(n, "noscript")) return;
    ns_window *w = cc->w;
    if (ns_node_is_element_named(n, "iframe"))
        ns_window_sheet_run_flush(cc);
    if (ns_node_is_element_named(n, "style")) {
        char *css = ns_css_style_element_text(n);
        if (css) {
            const char *base = ns_window_node_base_url(w, n);
            if (cc->run_base && cc->run_base != base)
                ns_window_sheet_run_flush(cc);
            if (strstr(css, "@import")) {
                ns_window_sheet_run_flush(cc);
                ns_css_stylesheet *sh =
                    ns_css_stylesheet_from_style_element_cached(n);
                if (sh) {
                    GHashTable *seen =
                        g_hash_table_new_full(g_str_hash, g_str_equal,
                                              g_free, NULL);
                    guint first = cc->page_sheets->len;
                    ns_window_append_stylesheet_expanded(w, cc->page_sheets,
                                                         sh, base, seen, 0);
                    ns_window_append_owned_stylesheet_range(cc->page_sheets,
                                                            cc->owned_sheets,
                                                            first);
                    g_hash_table_destroy(seen);
                }
            } else {
                g_string_append(cc->run, css);
                g_string_append_c(cc->run, '\n');
                cc->run_base = base;
            }
            g_free(css);
        }
    } else if (ns_node_is_element_named(n, "link")) {
        const char *rel = ns_element_get_attr(n, "rel");
        const char *href = ns_element_get_attr(n, "href");
        const char *media = ns_element_get_attr(n, "media");
        if (href && *href && ns_rel_is_stylesheet(rel) &&
            (!media || !*media || ns_css_media_query_matches(media))) {
            ns_window_sheet_run_flush(cc);
            char *abs = ns_window_resolve_for_node(w, n, href);
            GPtrArray *group = abs && w->external_css_loaded
                ? g_hash_table_lookup(w->external_css_loaded, abs)
                : NULL;
            if (group)
                for (guint i = 0; i < group->len; i++)
                    g_ptr_array_add(cc->page_sheets,
                                    g_ptr_array_index(group, i));
            g_free(abs);
        }
    }
    for (ns_node *c = n->first_child; c; c = c->next_sibling)
        ns_window_collect_page_stylesheets_walk(c, cc);
}

static void
ns_window_collect_page_stylesheets(ns_window *w, ns_node *doc,
                                   GPtrArray *page_sheets,
                                   GPtrArray *owned_sheets)
{
    if (!w || !doc) return;
    ns_sheet_collect_ctx cc = {
        .w = w, .page_sheets = page_sheets, .owned_sheets = owned_sheets,
        .run = g_string_new(NULL), .run_base = NULL,
    };
    ns_window_collect_page_stylesheets_walk(doc, &cc);
    ns_window_sheet_run_flush(&cc);
    g_string_free(cc.run, TRUE);
}

void
ns_window_ensure_layout(ns_window *w, double viewport_width)
{
    if (!w->last_body) return;
    if (w->layout_tree && w->parsed_doc && !w->layout_dirty &&
        fabs(viewport_width - w->last_viewport_w) < 16.0)
        return;

    gboolean profile = ns_profile_enabled();
    const char *reason = w->layout_dirty_reason;
    if (!reason) reason = w->layout_tree ? "viewport" : "initial";
    gint64 t_start = g_get_monotonic_time();
    ns_css_style_element_cache_begin();
    if (w->layout_tree) { if (w->js) ns_js_set_layout_root(w->js, NULL); ns_box_free(w->layout_tree); w->layout_tree = NULL; ns_selection_clear(&w->selection); w->search_active_box = NULL; }
    if (w->style_table) { if (w->js) ns_js_set_style_table(w->js, NULL); g_hash_table_destroy(w->style_table); w->style_table = NULL; }
    gint64 t_after_free = g_get_monotonic_time();

    gboolean parsed_fresh = FALSE;
    if (!w->parsed_doc) {
        w->parsed_doc = ns_html_parse(w->last_body,
                                      (gssize)w->last_body_len);
        parsed_fresh = TRUE;
    }
    gint64 t_after_parse = g_get_monotonic_time();

    GPtrArray *page_sheets = g_ptr_array_new();
    GPtrArray *owned_sheets = g_ptr_array_new();
    ns_window_collect_page_stylesheets(w, w->parsed_doc, page_sheets,
                                       owned_sheets);
    guint sheet_count = page_sheets->len;

    double viewport_height = viewport_width * 0.75;
    if (w->drawing_area) {
        GtkWidget *sw = gtk_widget_get_ancestor(w->drawing_area,
                                                GTK_TYPE_SCROLLED_WINDOW);
        double vis_h = 0;
        if (sw) {
            GtkAdjustment *va =
                gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw));
            double page = va ? gtk_adjustment_get_page_size(va) : 0;
            if (page > 100) vis_h = page;
            else { int swh = gtk_widget_get_height(sw); if (swh > 100) vis_h = swh; }
        }
        if (vis_h <= 100) {
            int alloc_h = gtk_widget_get_height(w->drawing_area);
            if (alloc_h > 100) vis_h = (double)alloc_h;
        }
        if (vis_h > 100) viewport_height = vis_h;
    }

    gint64 t_before_layout = g_get_monotonic_time();
    ns_render_ctx rc = {
        .doc             = w->parsed_doc,
        .sheets          = (const ns_css_stylesheet *const *)page_sheets->pdata,
        .n_sheets        = page_sheets->len,
        .viewport_width  = viewport_width,
        .viewport_height = viewport_height,
        .zoom            = w->zoom,
        .images          = w->images,
        .base_url        = ns_window_current_url(w),
        .anim            = w->anim,
        .js              = w->js,
        .focused_input   = w->focused_input,
        .caret_byte      = w->caret_byte,
        .sel_anchor_byte = w->sel_anchor_byte,
        .resolve_url     = ns_window_render_resolve,
        .font_allowed    = ns_window_render_font_allowed,
        .cb_ud           = w,
    };
    ns_render_profile rp;
    w->style_table = ns_render_relayout_profile(
        &rc, &w->layout_tree, profile ? &rp : NULL);
    gint64 t_after_layout = g_get_monotonic_time();

    for (guint i = 0; i < owned_sheets->len; i++)
        ns_css_stylesheet_free(g_ptr_array_index(owned_sheets, i));
    g_ptr_array_free(owned_sheets, TRUE);
    g_ptr_array_free(page_sheets, TRUE);
    ns_window_apply_page_title(w);
    ns_window_kick_image_loads(w);
    ns_window_kick_video_loads(w);
    ns_window_kick_stylesheet_loads(w);
    gint64 t_after_kicks = g_get_monotonic_time();
    if (w->drawing_area && w->layout_tree) {
        double ext_w = 0, ext_h = 0;
        ns_box_content_extent(w->layout_tree, &ext_w, &ext_h);
        int h = (int)(ext_h + 32);
        int min_w = (int)(ext_w + 0.5);
        if (min_w <= (int)viewport_width) min_w = -1;
        gtk_widget_set_size_request(w->drawing_area, min_w, h);
        if (g_getenv("NS_LAYOUT_DEBUG"))
            g_warning("layout: vp=%.0f ext=%.0fx%.0f root=%.0fx%.0f",
                      viewport_width, ext_w, ext_h,
                      w->layout_tree->content_width,
                      w->layout_tree->content_height);
    }
    gint64 t_end = g_get_monotonic_time();
    w->last_render_us = t_end - t_start;
    w->layout_dirty = FALSE;
    w->layout_dirty_reason = NULL;
    w->last_viewport_w = viewport_width;
    {
        guint style_count = w->style_table ? g_hash_table_size(w->style_table) : 0;
        ns_debug_log_emit(NS_DLOG_RENDER, "layout",
            "reason=%s vp=%.0f styles=%u sheets=%u parse=%.1fms%s "
            "render=%.1fms total=%.1fms",
            reason, viewport_width, style_count, (guint)sheet_count,
            (t_after_parse  - t_after_free)   / 1000.0,
            parsed_fresh ? "" : "(cached)",
            (t_after_layout- t_before_layout) / 1000.0,
            (t_end         - t_start)         / 1000.0);
        if (profile) {
            g_printerr("[profile] render reason=%s vp=%.0f styles=%u sheets=%u "
                       "free=%.1fms parse=%.1fms%s sheetscan=%.1fms "
                       "css1=%.1fms style1=%.1fms layout1=%.1fms "
                       "containers=%u container=%.1fms css2=%.1fms "
                       "style2=%.1fms layout2=%.1fms render=%.1fms "
                       "subres=%.1fms size=%.1fms total=%.1fms\n",
                       reason, viewport_width, style_count, (guint)sheet_count,
                       (t_after_free   - t_start)        / 1000.0,
                       (t_after_parse  - t_after_free)   / 1000.0,
                       parsed_fresh ? "" : "(cached)",
                       (t_before_layout - t_after_parse)  / 1000.0,
                       rp.css1_us / 1000.0,
                       rp.style1_us / 1000.0,
                       rp.layout1_us / 1000.0,
                       rp.containers,
                       rp.container_us / 1000.0,
                       rp.css2_us / 1000.0,
                       rp.style2_us / 1000.0,
                       rp.layout2_us / 1000.0,
                       (t_after_layout- t_before_layout) / 1000.0,
                       (t_after_kicks - t_after_layout)   / 1000.0,
                       (t_end - t_after_kicks)            / 1000.0,
                       (t_end         - t_start)         / 1000.0);
        }
    }
}

static void
ns_window_js_flush_layout(gpointer user_data)
{
    ns_window *w = user_data;
    if (!w || !w->js) return;
    gboolean mutated = ns_js_consume_mutated(w->js);
    gboolean dirty = !w->layout_tree || mutated;
    if (!dirty) return;
    if (mutated && w->layout_tree) {
        w->layout_dirty = TRUE;
        if (w->last_render_us > 250000) {
            ns_window_schedule_relayout(w,
                ns_window_adaptive_relayout_delay(
                    w, 250, ns_window_relayout_max_delay(w, 2500)),
                "js-flush");
            return;
        }
        w->layout_dirty_reason = "js-flush";
    }
    double vw = w->last_viewport_w > 0 ? w->last_viewport_w : ns_layout_viewport();
    ns_window_ensure_layout(w, vw);
    if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
}

void
ns_window_ensure_js(ns_window *w)
{
    if (!w || w->js) {
        if (w && w->js) ns_js_set_image_cache(w->js, w->images);
        return;
    }
    w->js = ns_js_new(ns_window_js_log, w,
                      ns_window_js_mutated, w,
                      ns_window_js_navigate, w);
    if (!w->js) return;
    ns_js_set_scroll_to_cb(w->js, ns_window_js_scroll_to, w);
    ns_js_set_form_submit_cb(w->js, ns_window_js_form_submit, w);
    ns_js_set_soft_nav_cb(w->js, ns_window_js_soft_nav, w);
    ns_js_set_repaint_cb(w->js, ns_window_js_repaint, w);
    ns_js_set_layout_flush_cb(w->js, ns_window_js_flush_layout, w);
    ns_js_set_clipboard_write_cb(w->js, ns_window_js_clipboard_write, w);
    ns_js_set_window_action_cb(w->js, ns_window_js_window_action, w);
    ns_js_set_image_cache(w->js, w->images);
}

static gboolean
is_html_content_type(const char *ct)
{
    if (!ct) return FALSE;
    return g_ascii_strncasecmp(ct, "text/html", 9) == 0 ||
           g_ascii_strncasecmp(ct, "application/xhtml+xml", 21) == 0 ||
           g_ascii_strncasecmp(ct, "application/xml", 15) == 0 ||
           g_ascii_strncasecmp(ct, "text/xml", 8) == 0;
}

static char *
to_utf8_or_pass(const char *body, gsize len)
{
    return ns_html_decode_body(body, len);
}

void
ns_window_render(ns_window *w)
{
    if (w->pdf) {
        gtk_stack_set_visible_child_name(GTK_STACK(w->content_stack), "render");
        if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
        return;
    }
    if (!w->last_body) {
        ns_window_set_body_text(w, "", 0);
        return;
    }

    gboolean is_html = is_html_content_type(w->last_content_type);

    if (w->mode == NS_VIEW_RENDER && is_html) {
        gtk_stack_set_visible_child_name(GTK_STACK(w->content_stack), "render");
        gtk_widget_queue_draw(w->drawing_area);
        return;
    }

    gtk_stack_set_visible_child_name(GTK_STACK(w->content_stack), "text");

    if (w->mode == NS_VIEW_DOM && is_html) {
        ns_node *doc = ns_html_parse(w->last_body,
                                     (gssize)w->last_body_len);
        GString *dump = ns_node_dump(doc);
        ns_window_set_body_text(w, dump->str, (gssize)dump->len);
        g_string_free(dump, TRUE);
        ns_node_free(doc);
        return;
    }

    if (w->mode == NS_VIEW_LAYOUT && is_html) {
        ns_window_ensure_layout(w, ns_layout_viewport());
        GString *dump = ns_box_dump(w->layout_tree);
        ns_window_set_body_text(w, dump->str, (gssize)dump->len);
        g_string_free(dump, TRUE);
        return;
    }

    char *utf8 = to_utf8_or_pass(w->last_body, w->last_body_len);
    ns_window_set_body_text(w, utf8, -1);
    g_free(utf8);
}

static void
ns_window_follow_href(ns_window *w, const char *href, const char *target,
                      GdkModifierType mods)
{
    if (!href || !*href) return;
    if (g_str_has_prefix(href, "javascript:")) {
        const char *code = href + strlen("javascript:");
        if (!ns_csp_javascript_url_allowed(w->csp)) {
            g_warning("CSP blocked: javascript: URL navigation");
            return;
        }
        if (w->js && *code) {
            char *result = ns_js_eval_source(w->js, code, "href");
            g_free(result);
            if (ns_js_consume_mutated(w->js)) ns_window_js_mutated(w);
        }
        return;
    }
    if (g_str_has_prefix(href, "#")) {
        const char *frag = href + 1;
        const char *cur = ns_window_current_url(w);
        char *base = cur ? g_strdup(cur) : g_strdup("");
        char *hash = strchr(base, '#');
        if (hash) *hash = '\0';
        char *old_url = g_strdup(cur ? cur : "");
        char *new_url = g_strconcat(base, "#", frag, NULL);
        g_free(base);
        g_free(w->pending_fragment);
        w->pending_fragment = g_strdup(frag);
        ns_css_set_target_fragment(*frag ? frag : NULL);
        ns_window_js_soft_nav(new_url, FALSE, w);
        ns_window_reveal_pending_fragment(w, TRUE);
        w->layout_dirty = TRUE;
        ns_window_ensure_layout(w, ns_layout_viewport());
        ns_window_scroll_to_fragment(w);
        if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
        if (w->js && strcmp(old_url, new_url) != 0)
            ns_js_dispatch_hashchange(w->js, old_url, new_url);
        g_free(old_url);
        g_free(new_url);
        return;
    }
    if (g_str_has_prefix(href, "mailto:")) return;
    char *abs_url = ns_resolve_url(w, href);
    if (!abs_url) return;
    gboolean new_win = (mods & GDK_CONTROL_MASK) != 0 ||
                       (target && strcmp(target, "_blank") == 0);
    if (new_win) {
        GtkApplication *app = gtk_window_get_application(GTK_WINDOW(w->window));
        ns_spawn_window(app, abs_url);
    } else {
        ns_window_load_url(w, abs_url, NS_LOAD_USER);
    }
    g_free(abs_url);
}

static gboolean
ns_is_labelable(const ns_node *n)
{
    if (!n || n->kind != NS_NODE_ELEMENT || !n->name) return FALSE;
    if (strcmp(n->name, "select")   == 0 ||
        strcmp(n->name, "textarea") == 0 ||
        strcmp(n->name, "button")   == 0 ||
        strcmp(n->name, "meter")    == 0 ||
        strcmp(n->name, "progress") == 0 ||
        strcmp(n->name, "output")   == 0)
        return TRUE;
    if (strcmp(n->name, "input") == 0) {
        const char *type = ns_element_get_attr(n, "type");
        return !(type && g_ascii_strcasecmp(type, "hidden") == 0);
    }
    return FALSE;
}

static ns_node *
ns_first_labelable_descendant(const ns_node *n)
{
    for (const ns_node *c = n->first_child; c; c = c->next_sibling) {
        if (ns_is_labelable(c)) return (ns_node *)c;
        ns_node *deep = ns_first_labelable_descendant(c);
        if (deep) return deep;
    }
    return NULL;
}

gboolean
ns_window_media_target(ns_window *w, const ns_box *hit,
                       char **out_url, gboolean *out_is_video,
                       gboolean *out_stream)
{
    if (!hit || !hit->dom) return FALSE;
    gboolean is_video = ns_node_is_element_named(hit->dom, "video");
    gboolean is_audio = ns_node_is_element_named(hit->dom, "audio");
    if (!is_video && !is_audio) return FALSE;
    const char *msrc = NULL;
    if (hit->media)
        msrc = is_video ? hit->media->video_src : hit->media->video_audio_src;
    char *abs = msrc ? ns_resolve_url(w, msrc) : NULL;
    gboolean stream = !abs || g_str_has_prefix(abs, "blob:") ||
                      g_str_has_prefix(abs, "data:");
    if (stream) {
        g_free(abs);
        const char *page = ns_window_current_url(w);
        abs = page ? g_strdup(page) : NULL;
    }
    if (!abs) return FALSE;
    if (g_str_has_prefix(abs, "file://")) {
        const char *page = ns_window_current_url(w);
        if (!page || !g_str_has_prefix(page, "file://")) {
            g_free(abs);
            return FALSE;
        }
    }
    *out_url = abs;
    *out_is_video = is_video;
    *out_stream = stream;
    return TRUE;
}

static const ns_node *
ns_window_event_target_at(ns_window *w, double x, double y)
{
    if (!w->layout_tree) return NULL;
    const ns_box *hit = ns_box_hit_test(w->layout_tree, x, y);
    return hit ? hit->dom : NULL;
}

static void
ns_window_client_xy(ns_window *w, double x, double y, double *cx, double *cy)
{
    double sx = 0, sy = 0;
    if (w->render_vadj) sy = gtk_adjustment_get_value(w->render_vadj);
    GtkWidget *sw = w->drawing_area
        ? gtk_widget_get_ancestor(w->drawing_area, GTK_TYPE_SCROLLED_WINDOW)
        : NULL;
    if (sw) {
        GtkAdjustment *h =
            gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(sw));
        if (h) sx = gtk_adjustment_get_value(h);
    }
    *cx = x - sx;
    *cy = y - sy;
}

static gboolean
ns_window_dispatch_mouse(ns_window *w, const ns_node *node, const char *type,
                         double x, double y, int button, int buttons,
                         GdkModifierType state, const ns_node *related)
{
    if (!w->js || !node) return FALSE;
    double cx, cy;
    ns_window_client_xy(w, x, y, &cx, &cy);
    gboolean prevented = FALSE;
    ns_js_dispatch_mouse_event(w->js, node, type, cx, cy, x, y, button, buttons,
                               (state & GDK_SHIFT_MASK)   != 0,
                               (state & GDK_CONTROL_MASK) != 0,
                               (state & GDK_ALT_MASK)     != 0,
                               (state & GDK_META_MASK)    != 0,
                               related, &prevented);
    if (w->js && ns_js_consume_mutated(w->js)) ns_window_js_mutated(w);
    return prevented;
}

static void
ns_window_emit_pointer_and_mouse(ns_window *w, const ns_node *node,
                                 const char *pointer_type, const char *mouse_type,
                                 double x, double y, int button, int buttons,
                                 GdkModifierType state, const ns_node *related)
{
    if (!w->js || !node) return;
    ns_window_dispatch_mouse(w, node, pointer_type, x, y, button, buttons,
                             state, related);
    if (!w->js) return;
    ns_window_dispatch_mouse(w, node, mouse_type, x, y, button, buttons,
                             state, related);
}

static gboolean
ns_element_attr_is_true(const ns_node *n, const char *name)
{
    const char *v = ns_element_get_attr(n, name);
    return v && g_ascii_strcasecmp(v, "true") == 0;
}

static gboolean
ns_element_attr_is_false(const ns_node *n, const char *name)
{
    const char *v = ns_element_get_attr(n, name);
    return v && g_ascii_strcasecmp(v, "false") == 0;
}

static const ns_node *
ns_window_html_drag_source_at(ns_window *w, double x, double y)
{
    const ns_node *hit = ns_window_event_target_at(w, x, y);
    for (const ns_node *p = hit; p; p = p->parent) {
        if (p->kind != NS_NODE_ELEMENT || !p->name) continue;
        if (ns_element_effectively_inert(p) ||
            ns_element_effectively_disabled(p))
            return NULL;
        if (ns_element_attr_is_true(p, "draggable"))
            return p;
        if (ns_element_attr_is_false(p, "draggable"))
            continue;
        if (strcmp(p->name, "a") == 0) {
            const char *href = ns_element_get_attr(p, "href");
            if (href && *href) return p;
        }
        if (strcmp(p->name, "img") == 0) {
            const char *src = ns_element_get_attr(p, "src");
            if (src && *src) return p;
        }
    }
    return NULL;
}

static const ns_node *
ns_window_html_drag_target_at(ns_window *w, double x, double y)
{
    const ns_node *hit = ns_window_event_target_at(w, x, y);
    if (hit) return hit;
    if (!w->parsed_doc) return NULL;
    ns_node *body = ns_node_find_first_element(w->parsed_doc, "body");
    return body ? body : w->parsed_doc;
}

static void
ns_window_seed_html_drag_data(ns_window *w, const ns_node *source)
{
    if (!w || !source || !w->html_drag_session) return;
    const char *raw = NULL;
    if (ns_node_is_element_named(source, "a"))
        raw = ns_element_get_attr(source, "href");
    else if (ns_node_is_element_named(source, "img"))
        raw = ns_element_get_attr(source, "src");
    if (!raw || !*raw) return;
    char *abs = ns_resolve_url(w, raw);
    if (!abs) return;
    ns_js_drag_session_set_data(w->html_drag_session, "text/plain", abs);
    ns_js_drag_session_set_data(w->html_drag_session, "text/uri-list", abs);
    g_free(abs);
}

static gboolean
ns_window_dispatch_drag(ns_window *w, const ns_node *target, const char *type,
                        double x, double y, int buttons,
                        GdkModifierType state, const ns_node *related)
{
    if (!w || !w->js || !w->html_drag_session || !target) return FALSE;
    double cx, cy;
    ns_window_client_xy(w, x, y, &cx, &cy);
    gboolean prevented = FALSE;
    ns_js_dispatch_drag_event(w->js, w->html_drag_session, target, type,
                              cx, cy, x, y, 0, buttons,
                              (state & GDK_SHIFT_MASK)   != 0,
                              (state & GDK_CONTROL_MASK) != 0,
                              (state & GDK_ALT_MASK)     != 0,
                              (state & GDK_META_MASK)    != 0,
                              related, &prevented);
    if (w->js && ns_js_consume_mutated(w->js)) ns_window_js_mutated(w);
    return prevented;
}

static gboolean
ns_window_begin_html_drag(ns_window *w, GtkGestureDrag *gesture,
                          double x, double y)
{
    ns_window_clear_html_drag(w);
    if (!w || !w->js || !w->layout_tree) return FALSE;
    const ns_node *source = ns_window_html_drag_source_at(w, x, y);
    if (!source) return FALSE;
    w->html_drag_session = ns_js_drag_session_new(w->js);
    if (!w->html_drag_session) return FALSE;
    w->html_drag_source = source;
    ns_window_seed_html_drag_data(w, source);
    GdkModifierType state = gtk_event_controller_get_current_event_state(
        GTK_EVENT_CONTROLLER(gesture));
    gboolean prevented = ns_window_dispatch_drag(w, source, "dragstart",
                                                 x, y, ns_dom_buttons_bit(0),
                                                 state, NULL);
    if (prevented) {
        ns_window_clear_html_drag(w);
        return FALSE;
    }
    w->html_drag_active = TRUE;
    w->html_drag_over = NULL;
    w->html_drag_can_drop = FALSE;
    return TRUE;
}

static gboolean
ns_window_update_html_drag(ns_window *w, GtkGestureDrag *gesture,
                           double x, double y)
{
    if (!w || !w->html_drag_active || !w->html_drag_session) return FALSE;
    GdkModifierType state = gtk_event_controller_get_current_event_state(
        GTK_EVENT_CONTROLLER(gesture));
    const ns_node *target = ns_window_html_drag_target_at(w, x, y);
    if (target != w->html_drag_over) {
        const ns_node *old = w->html_drag_over;
        w->html_drag_can_drop = FALSE;
        if (old)
            ns_window_dispatch_drag(w, old, "dragleave", x, y,
                                    ns_dom_buttons_bit(0), state, target);
        w->html_drag_over = target;
        if (target) {
            gboolean enter_prevented =
                ns_window_dispatch_drag(w, target, "dragenter", x, y,
                                        ns_dom_buttons_bit(0), state, old);
            if (enter_prevented) w->html_drag_can_drop = TRUE;
        }
    }
    if (target) {
        gboolean over_prevented =
            ns_window_dispatch_drag(w, target, "dragover", x, y,
                                    ns_dom_buttons_bit(0), state, NULL);
        if (over_prevented) w->html_drag_can_drop = TRUE;
    }
    return TRUE;
}

static gboolean
ns_window_end_html_drag(ns_window *w, GtkGestureDrag *gesture,
                        double x, double y)
{
    if (!w || !w->html_drag_active || !w->html_drag_session) return FALSE;
    GdkModifierType state = gtk_event_controller_get_current_event_state(
        GTK_EVENT_CONTROLLER(gesture));
    const ns_node *target = ns_window_html_drag_target_at(w, x, y);
    if (target && target != w->html_drag_over) {
        ns_window_update_html_drag(w, gesture, x, y);
        target = ns_window_html_drag_target_at(w, x, y);
    }
    if (target && w->html_drag_can_drop)
        ns_window_dispatch_drag(w, target, "drop", x, y, 0, state, NULL);
    if (w->html_drag_source)
        ns_window_dispatch_drag(w, w->html_drag_source, "dragend",
                                x, y, 0, state, target);
    ns_window_clear_html_drag(w);
    return TRUE;
}

static int
ns_dom_buttons_bit(int dom_button)
{
    if (dom_button == 1) return 4;
    if (dom_button == 2) return 2;
    return 1;
}

void
ns_on_drawing_pressed(GtkGestureClick *gesture, int n_press,
                      double x, double y, gpointer user_data)
{
    ns_window *w = user_data;
    GdkModifierType pstate = gtk_event_controller_get_current_event_state(
        GTK_EVENT_CONTROLLER(gesture));
    if (w->js && w->layout_tree) {
        const ns_node *down = ns_window_event_target_at(w, x, y);
        if (down) {
            ns_window_emit_pointer_and_mouse(w, down, "pointerdown", "mousedown",
                                             x, y, 0, ns_dom_buttons_bit(0),
                                             pstate, NULL);
            if (n_press == 2 && w->js && w->layout_tree) {
                const ns_node *dbl = ns_window_event_target_at(w, x, y);
                if (dbl)
                    ns_window_dispatch_mouse(w, dbl, "dblclick", x, y, 0, 0,
                                             pstate, NULL);
            }
        }
    }
    if (ns_selection_has_range(&w->selection)) {
        ns_selection_clear(&w->selection);
        ns_window_sync_selection_to_js(w);
        if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
    }
    if (!w->layout_tree) return;
    const ns_link_range *link = ns_box_hit_link_range(w->layout_tree, x, y);
    if (!link) {
        const ns_node *form_target = ns_box_hit_form_dom(w->layout_tree, x, y);
        const ns_box *hit = ns_box_hit_test(w->layout_tree, x, y);
        if (form_target) {
            if (ns_element_effectively_disabled(form_target) ||
                ns_element_effectively_inert(form_target)) {
                ns_window_set_focused_input(w, NULL);
                return;
            }
            gboolean prevented = ns_window_dispatch_mouse(w, form_target, "click",
                                                          x, y, 0, 0, pstate, NULL);
            if (!prevented) {
                if (ns_input_is_text_like(form_target)) {
                    ns_window_set_focused_input(w, (ns_node *)form_target);
                    gtk_widget_grab_focus(w->drawing_area);
                } else if (form_target->kind == NS_NODE_ELEMENT &&
                           form_target->name &&
                           strcmp(form_target->name, "input") == 0) {
                    const char *type = ns_element_get_attr(form_target, "type");
                    if (type && g_ascii_strcasecmp(type, "checkbox") == 0) {
                        if (ns_element_get_attr(form_target, "checked"))
                            ns_element_remove_attr((ns_node *)form_target, "checked");
                        else
                            ns_element_set_attr((ns_node *)form_target, "checked", "");
                        if (w->js) {
                            ns_js_dispatch_event(w->js, form_target, "input",  NULL);
                            ns_js_dispatch_event(w->js, form_target, "change", NULL);
                        }
                        ns_window_js_mutated(w);
                    } else if (type && g_ascii_strcasecmp(type, "radio") == 0) {
                        ns_clear_radio_group_for(w->parsed_doc, form_target);
                        ns_element_set_attr((ns_node *)form_target, "checked", "");
                        if (w->js) {
                            ns_js_dispatch_event(w->js, form_target, "input",  NULL);
                            ns_js_dispatch_event(w->js, form_target, "change", NULL);
                        }
                        ns_window_js_mutated(w);
                    } else if (type && g_ascii_strcasecmp(type, "file") == 0) {
                        ns_window_set_focused_input(w, NULL);
                        ns_window_open_file_chooser(w, (ns_node *)form_target);
                    } else {
                        ns_window_set_focused_input(w, NULL);
                        ns_window_maybe_submit_form(w, form_target);
                    }
                } else {
                    ns_window_set_focused_input(w, NULL);
                    ns_window_maybe_submit_form(w, form_target);
                }
            }
            return;
        }
        if (hit && hit->dom) {
            if (ns_element_effectively_inert(hit->dom)) {
                ns_window_set_focused_input(w, NULL);
                return;
            }
            gboolean prevented = ns_window_dispatch_mouse(w, hit->dom, "click",
                                                          x, y, 0, 0, pstate, NULL);
            if (!prevented) {
                char *media_abs = NULL;
                gboolean is_video_el = FALSE, stream = FALSE;
                if (ns_window_media_target(w, hit, &media_abs,
                                           &is_video_el, &stream)) {
                    if (ns_media_launch_external(GTK_WINDOW(w->window),
                                                 media_abs, is_video_el, stream))
                        ns_window_set_status(w, "Opening %s in external player…",
                                             is_video_el ? "video" : "audio");
                    g_free(media_abs);
                    return;
                }
                if (ns_node_is_element_named(hit->dom, "img")) {
                    const char *usemap = ns_element_get_attr(hit->dom, "usemap");
                    if (usemap && *usemap && w->parsed_doc) {
                        double cx0 = hit->x + hit->margin.left +
                                     hit->border.left + hit->padding.left;
                        double cy0 = hit->y + hit->margin.top +
                                     hit->border.top + hit->padding.top;
                        const char *atarget = NULL;
                        char *ahref = ns_image_map_resolve(w->parsed_doc, usemap,
                                                           x - cx0, y - cy0,
                                                           hit->content_width,
                                                           hit->content_height,
                                                           &atarget);
                        if (ahref) {
                            GdkEvent *ev = gtk_event_controller_get_current_event(
                                GTK_EVENT_CONTROLLER(gesture));
                            GdkModifierType mods =
                                ev ? gdk_event_get_modifier_state(ev) : 0;
                            ns_window_follow_href(w, ahref, atarget, mods);
                            g_free(ahref);
                            return;
                        }
                    }
                }
                const ns_node *cur = hit->dom;
                gboolean handled = FALSE;
                while (cur && !handled) {
                    if (ns_node_is_element_named(cur, "a")) {
                        const char *href = ns_element_get_attr(cur, "href");
                        if (href && *href) {
                            GdkEvent *event = gtk_event_controller_get_current_event(
                                GTK_EVENT_CONTROLLER(gesture));
                            GdkModifierType mods = event ?
                                gdk_event_get_modifier_state(event) : 0;
                            const char *target = ns_element_get_attr(cur, "target");
                            char *ismap_href = NULL;
                            if (ns_node_is_element_named(hit->dom, "img") &&
                                ns_element_get_attr(hit->dom, "ismap")) {
                                double cx0 = hit->x + hit->margin.left +
                                             hit->border.left + hit->padding.left;
                                double cy0 = hit->y + hit->margin.top +
                                             hit->border.top + hit->padding.top;
                                int ix = (int)(x - cx0); if (ix < 0) ix = 0;
                                int iy = (int)(y - cy0); if (iy < 0) iy = 0;
                                ismap_href = g_strdup_printf("%s?%d,%d", href, ix, iy);
                            }
                            ns_window_follow_href(w, ismap_href ? ismap_href : href,
                                                  target, mods);
                            g_free(ismap_href);
                            handled = TRUE;
                            break;
                        }
                    }
                    if (ns_node_is_element_named(cur, "label")) {
                        ns_node *target = NULL;
                        const char *for_id = ns_element_get_attr(cur, "for");
                        if (for_id && *for_id && w->parsed_doc)
                            target = ns_node_find_by_id(w->parsed_doc, for_id);
                        if (!target)
                            target = ns_first_labelable_descendant(cur);
                        if (target && target != cur && ns_is_labelable(target)) {
                            cur = target;
                            continue;
                        }
                    }
                    if (ns_input_is_text_like(cur)) {
                        ns_window_set_focused_input(w, (ns_node *)cur);
                        gtk_widget_grab_focus(w->drawing_area);
                        handled = TRUE;
                        break;
                    }
                    if (ns_node_is_contenteditable_host(cur)) {
                        ns_window_set_focused_input(w, (ns_node *)cur);
                        gtk_widget_grab_focus(w->drawing_area);
                        handled = TRUE;
                        break;
                    }
                    if (cur->kind == NS_NODE_ELEMENT && cur->name) {
                        if (strcmp(cur->name, "select") == 0) {
                            ns_window_open_select_popover(w, (ns_node *)cur, x, y);
                            handled = TRUE;
                            break;
                        }
                        if (strcmp(cur->name, "summary") == 0 &&
                            cur->parent && cur->parent->kind == NS_NODE_ELEMENT &&
                            cur->parent->name &&
                            strcmp(cur->parent->name, "details") == 0) {
                            ns_node *details = cur->parent;
                            gboolean now_open;
                            if (ns_element_get_attr(details, "open")) {
                                ns_element_remove_attr(details, "open");
                                now_open = FALSE;
                            } else {
                                ns_element_set_attr(details, "open", "");
                                now_open = TRUE;
                            }
                            if (w->js)
                                ns_js_details_toggle_open(w->js, details, now_open);
                            ns_window_js_mutated(w);
                            return;
                        }
                        if (strcmp(cur->name, "input") == 0) {
                            const char *type = ns_element_get_attr(cur, "type");
                            if (type && g_ascii_strcasecmp(type, "checkbox") == 0) {
                                if (ns_element_get_attr(cur, "checked"))
                                    ns_element_remove_attr((ns_node *)cur, "checked");
                                else
                                    ns_element_set_attr((ns_node *)cur, "checked", "");
                                if (w->js) {
                                    ns_js_dispatch_event(w->js, cur, "input",  NULL);
                                    ns_js_dispatch_event(w->js, cur, "change", NULL);
                                }
                                ns_window_js_mutated(w);
                                handled = TRUE;
                                break;
                            }
                            if (type && g_ascii_strcasecmp(type, "radio") == 0) {
                                ns_clear_radio_group_for(w->parsed_doc, cur);
                                ns_element_set_attr((ns_node *)cur, "checked", "");
                                if (w->js) {
                                    ns_js_dispatch_event(w->js, cur, "input",  NULL);
                                    ns_js_dispatch_event(w->js, cur, "change", NULL);
                                }
                                ns_window_js_mutated(w);
                                handled = TRUE;
                                break;
                            }
                            if (type && g_ascii_strcasecmp(type, "file") == 0) {
                                ns_window_set_focused_input(w, NULL);
                                ns_window_open_file_chooser(w, (ns_node *)cur);
                                handled = TRUE;
                                break;
                            }
                        }
                    }
                    cur = cur->parent;
                }
                if (!handled && hit->dom) {
                    const ns_node *probe[1] = { hit->dom };
                    GQueue q = G_QUEUE_INIT;
                    g_queue_push_tail(&q, (gpointer)probe[0]);
                    ns_node *picked = NULL;
                    while (!g_queue_is_empty(&q) && !picked) {
                        const ns_node *n = g_queue_pop_head(&q);
                        if (ns_input_is_text_like(n)) { picked = (ns_node *)n; break; }
                        for (const ns_node *d = n->first_child; d; d = d->next_sibling)
                            g_queue_push_tail(&q, (gpointer)d);
                    }
                    g_queue_clear(&q);
                    if (picked) {
                        ns_window_set_focused_input(w, picked);
                        gtk_widget_grab_focus(w->drawing_area);
                        handled = TRUE;
                    }
                }
                if (!handled) {
                    ns_window_set_focused_input(w, NULL);
                    ns_window_maybe_submit_form(w, hit->dom);
                }
            }
        }
        return;
    }
    if (link->dom && ns_element_effectively_inert(link->dom)) {
        ns_window_set_focused_input(w, NULL);
        return;
    }
    GdkEvent *event = gtk_event_controller_get_current_event(
        GTK_EVENT_CONTROLLER(gesture));
    GdkModifierType mods = event ? gdk_event_get_modifier_state(event) : 0;
    if (w->js && link->dom) {
        if (ns_window_dispatch_mouse(w, link->dom, "click", x, y, 0, 0, mods, NULL))
            return;
    }
    ns_window_follow_href(w, link->href, link->target, mods);
}

void
ns_on_drawing_released(GtkGestureClick *gesture, int n_press,
                       double x, double y, gpointer user_data)
{
    (void)n_press;
    ns_window *w = user_data;
    if (!w->js || !w->layout_tree) return;
    const ns_node *up = ns_window_event_target_at(w, x, y);
    if (!up) return;
    GdkModifierType state = gtk_event_controller_get_current_event_state(
        GTK_EVENT_CONTROLLER(gesture));
    ns_window_emit_pointer_and_mouse(w, up, "pointerup", "mouseup",
                                     x, y, 0, 0, state, NULL);
}

static const ns_node *
ns_window_key_target(ns_window *w)
{
    if (!w->parsed_doc) return NULL;
    ns_node *body = ns_node_find_first_element(w->parsed_doc, "body");
    return body ? body : w->parsed_doc;
}

static gboolean
ns_input_is_text_like(const ns_node *n)
{
    return ns_node_is_text_input(n);
}

static gboolean
ns_node_in_tree(const ns_node *root, const ns_node *needle)
{
    if (!root) return FALSE;
    if (root == needle) return TRUE;
    for (const ns_node *c = root->first_child; c; c = c->next_sibling)
        if (ns_node_in_tree(c, needle)) return TRUE;
    return FALSE;
}

static gboolean
ns_window_focused_input_live(ns_window *w)
{
    if (!w->focused_input) return FALSE;
    if (ns_element_effectively_inert(w->focused_input) ||
        ns_element_effectively_disabled(w->focused_input)) {
        ns_window_set_focused_input(w, NULL);
        return FALSE;
    }
    if (w->parsed_doc && !ns_node_in_tree(w->parsed_doc, w->focused_input)) {
        w->focused_input = NULL;
        return FALSE;
    }
    return TRUE;
}

static const char *
ns_input_current_value(const ns_node *n)
{
    return ns_node_editable_value(n);
}

static void
ns_input_set_value(ns_node *n, const char *value)
{
    ns_node_set_editable_value(n, value);
}

typedef struct ns_refresh_ctx {
    ns_window *w;
    char *url;
} ns_refresh_ctx;

static void
ns_refresh_ctx_free(gpointer data)
{
    ns_refresh_ctx *ctx = data;
    if (!ctx) return;
    g_free(ctx->url);
    g_free(ctx);
}

static gboolean
ns_window_refresh_fire(gpointer data)
{
    ns_refresh_ctx *ctx = data;
    if (ctx->w) {
        ctx->w->refresh_source = 0;
        if (ctx->url)
            ns_window_load_url(ctx->w, ctx->url, NS_LOAD_USER);
    }
    return G_SOURCE_REMOVE;
}

static inline gboolean
ns_refresh_is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static gboolean
ns_parse_refresh(const char *s, guint *out_delay, char **out_raw_url)
{
    *out_delay = 0;
    *out_raw_url = NULL;
    if (!s) return FALSE;
    const char *p = s;
    while (ns_refresh_is_ws(*p)) p++;
    const char *digits = p;
    while (g_ascii_isdigit(*p)) p++;
    if (p == digits && *p != '.') return FALSE;
    guint delay = 0;
    for (const char *d = digits; d < p; d++) {
        delay = delay * 10 + (guint)(*d - '0');
        if (delay > 600) { delay = 600; break; }
    }
    while (g_ascii_isdigit(*p) || *p == '.') p++;
    while (ns_refresh_is_ws(*p)) p++;
    if (*p == ';' || *p == ',') {
        p++;
        while (ns_refresh_is_ws(*p)) p++;
    }
    if (*p) {
        if ((p[0] == 'u' || p[0] == 'U') &&
            (p[1] == 'r' || p[1] == 'R') &&
            (p[2] == 'l' || p[2] == 'L')) {
            const char *q = p + 3;
            while (ns_refresh_is_ws(*q)) q++;
            if (*q == '=') {
                q++;
                while (ns_refresh_is_ws(*q)) q++;
                p = q;
            }
        }
        char quote = 0;
        if (*p == '"' || *p == '\'') { quote = *p; p++; }
        const char *start = p;
        const char *end;
        if (quote) {
            end = p;
            while (*end && *end != quote) end++;
        } else {
            end = start + strlen(start);
            while (end > start && ns_refresh_is_ws(end[-1])) end--;
        }
        if (end > start)
            *out_raw_url = g_strndup(start, (gsize)(end - start));
    }
    *out_delay = delay;
    return TRUE;
}

static const char *
ns_window_meta_refresh_content(ns_window *w)
{
    if (!w->parsed_doc) return NULL;
    ns_node *head = ns_node_find_first_element(w->parsed_doc, "head");
    if (!head) return NULL;
    for (const ns_node *c = head->first_child; c; c = c->next_sibling) {
        if (c->kind != NS_NODE_ELEMENT || !c->name) continue;
        if (strcmp(c->name, "meta") != 0) continue;
        const char *equiv = ns_element_get_attr(c, "http-equiv");
        if (!equiv || g_ascii_strcasecmp(equiv, "refresh") != 0) continue;
        const char *content = ns_element_get_attr(c, "content");
        if (content && *content) return content;
    }
    return NULL;
}

static void
ns_window_apply_meta_refresh(ns_window *w, const ns_response *resp)
{
    const char *content = (resp && resp->refresh && *resp->refresh)
                              ? resp->refresh
                              : ns_window_meta_refresh_content(w);
    if (!content) return;

    guint delay = 0;
    char *raw = NULL;
    if (!ns_parse_refresh(content, &delay, &raw)) return;

    char *url = raw ? ns_resolve_url(w, raw)
                    : g_strdup(ns_window_current_url(w));
    g_free(raw);
    if (!url) return;

    g_clear_handle_id(&w->refresh_source, g_source_remove);
    ns_refresh_ctx *ctx = g_new0(ns_refresh_ctx, 1);
    ctx->w = w;
    ctx->url = url;
    w->refresh_source = g_timeout_add_seconds_full(G_PRIORITY_DEFAULT, delay,
                                                   ns_window_refresh_fire, ctx,
                                                   ns_refresh_ctx_free);
}

static gboolean
ns_window_caret_blink_tick(gpointer user_data)
{
    ns_window *w = user_data;
    if (!w->focused_input) {
        w->caret_blink_source = 0;
        ns_paint_set_caret_visible(TRUE);
        return G_SOURCE_REMOVE;
    }
    w->caret_blink_on = !w->caret_blink_on;
    ns_paint_set_caret_visible(w->caret_blink_on);
    if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
    return G_SOURCE_CONTINUE;
}

static void
ns_window_reset_caret_blink(ns_window *w)
{
    w->caret_blink_on = TRUE;
    ns_paint_set_caret_visible(TRUE);
    g_clear_handle_id(&w->caret_blink_source, g_source_remove);
    if (w->focused_input)
        w->caret_blink_source = g_timeout_add(530, ns_window_caret_blink_tick, w);
}

static void ns_on_im_commit(GtkIMContext *im, const char *str, gpointer user_data);

static void
ns_window_ensure_im_context(ns_window *w)
{
    if (w->im_context || !w->drawing_area) return;
    w->im_context = gtk_im_multicontext_new();
    gtk_im_context_set_client_widget(w->im_context, w->drawing_area);
    g_signal_connect(w->im_context, "commit", G_CALLBACK(ns_on_im_commit), w);
}

static void
ns_window_input_replace(ns_window *w, gsize del_start, gsize del_end,
                        const char *insert, gsize insert_len)
{
    if (!ns_window_focused_input_live(w)) return;
    ns_node *target = w->focused_input;
    const char *cur = ns_input_current_value(target);
    gsize cur_len = strlen(cur);
    if (del_start > cur_len) del_start = cur_len;
    if (del_end   > cur_len) del_end   = cur_len;
    if (del_end < del_start) del_end = del_start;
    if (insert && insert_len && ns_form_control_length_limits_apply(target)) {
        const char *ml = ns_element_get_attr(target, "maxlength");
        if (ml && *ml) {
            long maxl = atol(ml);
            if (maxl >= 0) {
                glong kept = g_utf8_strlen(cur, (gssize)del_start) +
                             g_utf8_strlen(cur + del_end,
                                           (gssize)(cur_len - del_end));
                glong room = maxl - kept;
                if (room < 0) room = 0;
                if (g_utf8_strlen(insert, (gssize)insert_len) > room) {
                    const char *p = insert;
                    for (glong i = 0; i < room; i++) p = g_utf8_next_char(p);
                    insert_len = (gsize)(p - insert);
                    if (insert_len == 0) return;
                }
            }
        }
    }
    GString *s = g_string_sized_new(cur_len - (del_end - del_start) + insert_len);
    g_string_append_len(s, cur, (gssize)del_start);
    if (insert && insert_len) g_string_append_len(s, insert, (gssize)insert_len);
    g_string_append_len(s, cur + del_end, (gssize)(cur_len - del_end));
    if (w->js) {
        gboolean prevented = FALSE;
        ns_js_dispatch_event(w->js, target, "beforeinput", &prevented);
        if (prevented) { g_string_free(s, TRUE); return; }
        if (w->focused_input != target || !ns_window_focused_input_live(w)) {
            g_string_free(s, TRUE);
            return;
        }
    }
    ns_input_set_value(target, s->str);
    w->caret_byte = del_start + insert_len;
    w->sel_anchor_byte = w->caret_byte;
    g_string_free(s, TRUE);
    if (w->js) {
        ns_js_dispatch_event(w->js, target, "input", NULL);
        (void)ns_js_consume_mutated(w->js);
    }
    ns_window_reset_caret_blink(w);
    g_clear_handle_id(&w->js_relayout_idle_id, g_source_remove);
    ns_window_js_relayout_now(w);
}

static void
ns_on_im_commit(GtkIMContext *im, const char *str, gpointer user_data)
{
    (void)im;
    ns_window *w = user_data;
    if (!w->focused_input || !str || !*str) return;
    ns_window_input_replace(w, w->caret_byte, w->caret_byte, str, strlen(str));
}

static void
ns_on_paste_text(GObject *source, GAsyncResult *result, gpointer user_data)
{
    ns_window *w = ns_window_for_id(GPOINTER_TO_UINT(user_data));
    GError *err = NULL;
    char *text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), result, &err);
    if (text && w && ns_window_focused_input_live(w)) {
        gboolean is_multiline =
            (w->focused_input->name &&
             strcmp(w->focused_input->name, "textarea") == 0) ||
            ns_node_is_contenteditable_host(w->focused_input);
        if (!is_multiline) {
            for (char *p = text; *p; p++)
                if (*p == '\n' || *p == '\r') *p = ' ';
        }
        ns_window_input_replace(w, w->caret_byte, w->caret_byte, text, strlen(text));
    }
    g_clear_error(&err);
    g_free(text);
}

static void
ns_window_input_paste(ns_window *w)
{
    if (!w->focused_input || !w->drawing_area) return;
    GdkClipboard *cb = gtk_widget_get_clipboard(w->drawing_area);
    gdk_clipboard_read_text_async(cb, NULL, ns_on_paste_text,
                                  GUINT_TO_POINTER(w->id));
}

static void
ns_window_set_focused_input(ns_window *w, ns_node *target)
{
    if (w->focused_input == target) return;
    if (target && (ns_element_effectively_disabled(target) ||
                   ns_element_effectively_inert(target)))
        target = NULL;
    if (w->layout_tree) { if (w->js) ns_js_set_layout_root(w->js, NULL); ns_box_free(w->layout_tree); w->layout_tree = NULL; ns_selection_clear(&w->selection); w->search_active_box = NULL; }
    w->layout_dirty = TRUE;
    if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
    if (w->focused_input) {
        ns_node *old = w->focused_input;
        gboolean old_live = !w->parsed_doc || ns_node_in_tree(w->parsed_doc, old);
        if (w->im_context) {
            gtk_im_context_reset(w->im_context);
            gtk_im_context_focus_out(w->im_context);
        }
        if (w->js && old_live) {
            const char *cur = ns_input_current_value(old);
            if (!ns_node_is_contenteditable_host(old) &&
                w->focused_input_initial &&
                (!cur || strcmp(cur, w->focused_input_initial) != 0))
                ns_js_dispatch_event(w->js, old, "change", NULL);
            ns_js_dispatch_event(w->js, old, "blur",     NULL);
            ns_js_dispatch_event(w->js, old, "focusout", NULL);
        }
        g_free(w->focused_input_initial);
        w->focused_input_initial = NULL;
    }
    w->focused_input = target;
    w->caret_byte = 0;
    g_clear_handle_id(&w->caret_blink_source, g_source_remove);
    ns_paint_set_caret_visible(TRUE);
    if (target) {
        ns_window_ensure_im_context(w);
        ns_node_flatten_editable(target);
        w->focused_input_initial = g_strdup(ns_input_current_value(target));
        w->caret_byte = w->focused_input_initial ? strlen(w->focused_input_initial) : 0;
        w->sel_anchor_byte = w->caret_byte;
        w->caret_blink_on = TRUE;
        w->caret_blink_source = g_timeout_add(530, ns_window_caret_blink_tick, w);
        if (w->im_context) gtk_im_context_focus_in(w->im_context);
        if (w->js) {
            ns_js_dispatch_event(w->js, target, "focus",   NULL);
            ns_js_dispatch_event(w->js, target, "focusin", NULL);
        }
    }
    if (w->js) ns_js_set_focused_node(w->js, target);
}

static gboolean
ns_window_handle_input_key(ns_window *w, guint keyval, GdkModifierType state)
{
    if (!ns_window_focused_input_live(w)) return FALSE;
    ns_node *target = w->focused_input;
    const char *cur = ns_input_current_value(target);
    gsize cur_len = strlen(cur);
    if (w->caret_byte > cur_len) w->caret_byte = cur_len;
    if (w->sel_anchor_byte > cur_len) w->sel_anchor_byte = cur_len;

    gboolean ctrl  = (state & GDK_CONTROL_MASK) != 0;
    gboolean alt   = (state & GDK_ALT_MASK)     != 0;
    gboolean meta  = (state & GDK_META_MASK)    != 0;
    gboolean shift = (state & GDK_SHIFT_MASK)   != 0;
    gboolean is_textarea = target->name && strcmp(target->name, "textarea") == 0;
    gboolean is_multiline = is_textarea || ns_node_is_contenteditable_host(target);

    gsize sel_lo = w->sel_anchor_byte < w->caret_byte
                   ? w->sel_anchor_byte : w->caret_byte;
    gsize sel_hi = w->sel_anchor_byte < w->caret_byte
                   ? w->caret_byte : w->sel_anchor_byte;
    gboolean has_sel = sel_lo != sel_hi;

    if (alt || meta) return FALSE;

    if (ctrl) {
        if (keyval == GDK_KEY_v || keyval == GDK_KEY_V) {
            ns_window_input_paste(w);
            return TRUE;
        }
        if (keyval == GDK_KEY_c || keyval == GDK_KEY_C ||
            keyval == GDK_KEY_x || keyval == GDK_KEY_X) {
            if (has_sel && w->drawing_area) {
                char *sub = g_strndup(cur + sel_lo, sel_hi - sel_lo);
                GdkClipboard *cb = gtk_widget_get_clipboard(w->drawing_area);
                gdk_clipboard_set_text(cb, sub);
                g_free(sub);
                if (keyval == GDK_KEY_x || keyval == GDK_KEY_X)
                    ns_window_input_replace(w, sel_lo, sel_hi, NULL, 0);
            }
            return TRUE;
        }
        if (keyval == GDK_KEY_a || keyval == GDK_KEY_A) {
            w->sel_anchor_byte = 0;
            w->caret_byte = cur_len;
            ns_window_reset_caret_blink(w);
            ns_window_js_mutated(w);
            return TRUE;
        }
        if (keyval == GDK_KEY_Left) {
            w->caret_byte = 0;
            if (!shift) w->sel_anchor_byte = w->caret_byte;
            ns_window_reset_caret_blink(w);
            ns_window_js_mutated(w);
            return TRUE;
        }
        if (keyval == GDK_KEY_Right) {
            w->caret_byte = cur_len;
            if (!shift) w->sel_anchor_byte = w->caret_byte;
            ns_window_reset_caret_blink(w);
            ns_window_js_mutated(w);
            return TRUE;
        }
        return FALSE;
    }

    if (keyval == GDK_KEY_Escape) {
        ns_window_set_focused_input(w, NULL);
        if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
        return TRUE;
    }
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        if (is_multiline) {
            ns_window_input_replace(w, sel_lo, sel_hi, "\n", 1);
            return TRUE;
        }
        ns_node *submit_target = target;
        ns_window_set_focused_input(w, NULL);
        ns_window_maybe_submit_form(w, submit_target);
        return TRUE;
    }
    if (keyval == GDK_KEY_BackSpace) {
        if (has_sel) {
            ns_window_input_replace(w, sel_lo, sel_hi, NULL, 0);
            return TRUE;
        }
        if (w->caret_byte == 0) return TRUE;
        const char *prev = g_utf8_prev_char(cur + w->caret_byte);
        ns_window_input_replace(w, (gsize)(prev - cur), w->caret_byte, NULL, 0);
        return TRUE;
    }
    if (keyval == GDK_KEY_Delete) {
        if (has_sel) {
            ns_window_input_replace(w, sel_lo, sel_hi, NULL, 0);
            return TRUE;
        }
        if (w->caret_byte >= cur_len) return TRUE;
        const char *nxt = g_utf8_next_char(cur + w->caret_byte);
        ns_window_input_replace(w, w->caret_byte, (gsize)(nxt - cur), NULL, 0);
        return TRUE;
    }
    if (keyval == GDK_KEY_Left) {
        if (has_sel && !shift) {
            w->caret_byte = sel_lo;
        } else if (w->caret_byte > 0) {
            const char *prev = g_utf8_prev_char(cur + w->caret_byte);
            w->caret_byte = (gsize)(prev - cur);
        }
        if (!shift) w->sel_anchor_byte = w->caret_byte;
        ns_window_reset_caret_blink(w);
        ns_window_js_mutated(w);
        return TRUE;
    }
    if (keyval == GDK_KEY_Right) {
        if (has_sel && !shift) {
            w->caret_byte = sel_hi;
        } else if (w->caret_byte < cur_len) {
            const char *nxt = g_utf8_next_char(cur + w->caret_byte);
            w->caret_byte = (gsize)(nxt - cur);
        }
        if (!shift) w->sel_anchor_byte = w->caret_byte;
        ns_window_reset_caret_blink(w);
        ns_window_js_mutated(w);
        return TRUE;
    }
    if (keyval == GDK_KEY_Home) {
        w->caret_byte = 0;
        if (!shift) w->sel_anchor_byte = w->caret_byte;
        ns_window_reset_caret_blink(w);
        ns_window_js_mutated(w);
        return TRUE;
    }
    if (keyval == GDK_KEY_End) {
        w->caret_byte = cur_len;
        if (!shift) w->sel_anchor_byte = w->caret_byte;
        ns_window_reset_caret_blink(w);
        ns_window_js_mutated(w);
        return TRUE;
    }
    if (keyval == GDK_KEY_Up || keyval == GDK_KEY_Down) {
        const char *itype = target->name &&
            strcmp(target->name, "input") == 0
            ? ns_element_get_attr(target, "type") : NULL;
        if (itype && g_ascii_strcasecmp(itype, "number") == 0) {
            const char *sv = ns_element_get_attr(target, "step");
            double step = sv && *sv ? g_ascii_strtod(sv, NULL) : 1.0;
            if (!(step > 0)) step = 1.0;
            double val = *cur ? g_ascii_strtod(cur, NULL) : 0.0;
            val += (keyval == GDK_KEY_Up) ? step : -step;
            const char *mn = ns_element_get_attr(target, "min");
            const char *mx = ns_element_get_attr(target, "max");
            if (mn && *mn) { double m = g_ascii_strtod(mn, NULL); if (val < m) val = m; }
            if (mx && *mx) { double m = g_ascii_strtod(mx, NULL); if (val > m) val = m; }
            char buf[32];
            g_snprintf(buf, sizeof buf, "%g", val);
            ns_input_set_value(target, buf);
            w->caret_byte = strlen(buf);
            w->sel_anchor_byte = w->caret_byte;
            if (w->js) {
                ns_js_dispatch_event(w->js, target, "input",  NULL);
                ns_js_dispatch_event(w->js, target, "change", NULL);
                (void)ns_js_consume_mutated(w->js);
            }
            ns_window_reset_caret_blink(w);
            g_clear_handle_id(&w->js_relayout_idle_id, g_source_remove);
            ns_window_js_relayout_now(w);
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean
ns_keyval_is_action(guint keyval)
{
    switch (keyval) {
    case GDK_KEY_Return: case GDK_KEY_KP_Enter:
    case GDK_KEY_Escape:
    case GDK_KEY_Tab:    case GDK_KEY_ISO_Left_Tab:
    case GDK_KEY_Up:     case GDK_KEY_Down:
    case GDK_KEY_Left:   case GDK_KEY_Right:
    case GDK_KEY_Home:   case GDK_KEY_End:
    case GDK_KEY_Page_Up: case GDK_KEY_Page_Down:
    case GDK_KEY_BackSpace: case GDK_KEY_Delete:
        return TRUE;
    default:
        return FALSE;
    }
}

static int
ns_keyval_to_js_keycode(guint keyval)
{
    if (keyval >= GDK_KEY_a && keyval <= GDK_KEY_z)
        return (int)(keyval - GDK_KEY_a) + 65;
    if (keyval >= GDK_KEY_A && keyval <= GDK_KEY_Z)
        return (int)(keyval - GDK_KEY_A) + 65;
    if (keyval >= GDK_KEY_0 && keyval <= GDK_KEY_9)
        return (int)(keyval - GDK_KEY_0) + 48;
    if (keyval >= GDK_KEY_KP_0 && keyval <= GDK_KEY_KP_9)
        return (int)(keyval - GDK_KEY_KP_0) + 96;
    if (keyval >= GDK_KEY_F1 && keyval <= GDK_KEY_F12)
        return (int)(keyval - GDK_KEY_F1) + 112;

    switch (keyval) {
    case GDK_KEY_BackSpace:    return 8;
    case GDK_KEY_Tab:
    case GDK_KEY_ISO_Left_Tab: return 9;
    case GDK_KEY_Clear:
    case GDK_KEY_KP_Begin:     return 12;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:     return 13;
    case GDK_KEY_Shift_L:
    case GDK_KEY_Shift_R:      return 16;
    case GDK_KEY_Control_L:
    case GDK_KEY_Control_R:    return 17;
    case GDK_KEY_Alt_L:
    case GDK_KEY_Alt_R:        return 18;
    case GDK_KEY_Pause:        return 19;
    case GDK_KEY_Caps_Lock:    return 20;
    case GDK_KEY_Escape:       return 27;
    case GDK_KEY_space:
    case GDK_KEY_KP_Space:     return 32;
    case GDK_KEY_Page_Up:
    case GDK_KEY_KP_Page_Up:   return 33;
    case GDK_KEY_Page_Down:
    case GDK_KEY_KP_Page_Down: return 34;
    case GDK_KEY_End:
    case GDK_KEY_KP_End:       return 35;
    case GDK_KEY_Home:
    case GDK_KEY_KP_Home:      return 36;
    case GDK_KEY_Left:
    case GDK_KEY_KP_Left:      return 37;
    case GDK_KEY_Up:
    case GDK_KEY_KP_Up:        return 38;
    case GDK_KEY_Right:
    case GDK_KEY_KP_Right:     return 39;
    case GDK_KEY_Down:
    case GDK_KEY_KP_Down:      return 40;
    case GDK_KEY_Insert:
    case GDK_KEY_KP_Insert:    return 45;
    case GDK_KEY_Delete:
    case GDK_KEY_KP_Delete:    return 46;
    case GDK_KEY_Meta_L:
    case GDK_KEY_Super_L:      return 91;
    case GDK_KEY_Meta_R:
    case GDK_KEY_Super_R:      return 92;
    case GDK_KEY_Menu:         return 93;
    case GDK_KEY_KP_Multiply:  return 106;
    case GDK_KEY_KP_Add:       return 107;
    case GDK_KEY_KP_Subtract:  return 109;
    case GDK_KEY_KP_Decimal:   return 110;
    case GDK_KEY_KP_Divide:    return 111;
    case GDK_KEY_Num_Lock:     return 144;
    case GDK_KEY_Scroll_Lock:  return 145;
    case GDK_KEY_parenright:   return 48;
    case GDK_KEY_exclam:       return 49;
    case GDK_KEY_at:           return 50;
    case GDK_KEY_numbersign:   return 51;
    case GDK_KEY_dollar:       return 52;
    case GDK_KEY_percent:      return 53;
    case GDK_KEY_asciicircum:  return 54;
    case GDK_KEY_ampersand:    return 55;
    case GDK_KEY_asterisk:     return 56;
    case GDK_KEY_parenleft:    return 57;
    case GDK_KEY_semicolon:
    case GDK_KEY_colon:        return 186;
    case GDK_KEY_equal:
    case GDK_KEY_plus:         return 187;
    case GDK_KEY_comma:
    case GDK_KEY_less:         return 188;
    case GDK_KEY_minus:
    case GDK_KEY_underscore:   return 189;
    case GDK_KEY_period:
    case GDK_KEY_greater:      return 190;
    case GDK_KEY_slash:
    case GDK_KEY_question:     return 191;
    case GDK_KEY_grave:
    case GDK_KEY_asciitilde:   return 192;
    case GDK_KEY_bracketleft:
    case GDK_KEY_braceleft:    return 219;
    case GDK_KEY_backslash:
    case GDK_KEY_bar:          return 220;
    case GDK_KEY_bracketright:
    case GDK_KEY_braceright:   return 221;
    case GDK_KEY_apostrophe:
    case GDK_KEY_quotedbl:     return 222;
    default:                   return 0;
    }
}

static const char *
ns_keyval_to_js_code(guint keyval, char *buf, gsize buflen)
{
    if (keyval >= GDK_KEY_a && keyval <= GDK_KEY_z) {
        g_snprintf(buf, buflen, "Key%c", 'A' + (int)(keyval - GDK_KEY_a));
        return buf;
    }
    if (keyval >= GDK_KEY_A && keyval <= GDK_KEY_Z) {
        g_snprintf(buf, buflen, "Key%c", 'A' + (int)(keyval - GDK_KEY_A));
        return buf;
    }
    if (keyval >= GDK_KEY_0 && keyval <= GDK_KEY_9) {
        g_snprintf(buf, buflen, "Digit%c", '0' + (int)(keyval - GDK_KEY_0));
        return buf;
    }
    if (keyval >= GDK_KEY_KP_0 && keyval <= GDK_KEY_KP_9) {
        g_snprintf(buf, buflen, "Numpad%c", '0' + (int)(keyval - GDK_KEY_KP_0));
        return buf;
    }
    if (keyval >= GDK_KEY_F1 && keyval <= GDK_KEY_F12) {
        g_snprintf(buf, buflen, "F%d", (int)(keyval - GDK_KEY_F1) + 1);
        return buf;
    }

    switch (keyval) {
    case GDK_KEY_BackSpace:    return "Backspace";
    case GDK_KEY_Tab:
    case GDK_KEY_ISO_Left_Tab: return "Tab";
    case GDK_KEY_Return:       return "Enter";
    case GDK_KEY_KP_Enter:     return "NumpadEnter";
    case GDK_KEY_Shift_L:      return "ShiftLeft";
    case GDK_KEY_Shift_R:      return "ShiftRight";
    case GDK_KEY_Control_L:    return "ControlLeft";
    case GDK_KEY_Control_R:    return "ControlRight";
    case GDK_KEY_Alt_L:        return "AltLeft";
    case GDK_KEY_Alt_R:        return "AltRight";
    case GDK_KEY_Caps_Lock:    return "CapsLock";
    case GDK_KEY_Escape:       return "Escape";
    case GDK_KEY_space:        return "Space";
    case GDK_KEY_Page_Up:      return "PageUp";
    case GDK_KEY_Page_Down:    return "PageDown";
    case GDK_KEY_End:          return "End";
    case GDK_KEY_Home:         return "Home";
    case GDK_KEY_Left:         return "ArrowLeft";
    case GDK_KEY_Up:           return "ArrowUp";
    case GDK_KEY_Right:        return "ArrowRight";
    case GDK_KEY_Down:         return "ArrowDown";
    case GDK_KEY_Insert:       return "Insert";
    case GDK_KEY_Delete:       return "Delete";
    case GDK_KEY_Meta_L:
    case GDK_KEY_Super_L:      return "MetaLeft";
    case GDK_KEY_Meta_R:
    case GDK_KEY_Super_R:      return "MetaRight";
    case GDK_KEY_Menu:         return "ContextMenu";
    case GDK_KEY_KP_Begin:     return "Numpad5";
    case GDK_KEY_KP_Multiply:  return "NumpadMultiply";
    case GDK_KEY_KP_Add:       return "NumpadAdd";
    case GDK_KEY_KP_Subtract:  return "NumpadSubtract";
    case GDK_KEY_KP_Decimal:   return "NumpadDecimal";
    case GDK_KEY_KP_Divide:    return "NumpadDivide";
    case GDK_KEY_KP_Home:      return "Numpad7";
    case GDK_KEY_KP_Up:        return "Numpad8";
    case GDK_KEY_KP_Page_Up:   return "Numpad9";
    case GDK_KEY_KP_Left:      return "Numpad4";
    case GDK_KEY_KP_Right:     return "Numpad6";
    case GDK_KEY_KP_End:       return "Numpad1";
    case GDK_KEY_KP_Down:      return "Numpad2";
    case GDK_KEY_KP_Page_Down: return "Numpad3";
    case GDK_KEY_KP_Insert:    return "Numpad0";
    case GDK_KEY_KP_Delete:    return "NumpadDecimal";
    case GDK_KEY_Num_Lock:     return "NumLock";
    case GDK_KEY_Scroll_Lock:  return "ScrollLock";
    case GDK_KEY_semicolon:
    case GDK_KEY_colon:        return "Semicolon";
    case GDK_KEY_equal:
    case GDK_KEY_plus:         return "Equal";
    case GDK_KEY_comma:
    case GDK_KEY_less:         return "Comma";
    case GDK_KEY_minus:
    case GDK_KEY_underscore:   return "Minus";
    case GDK_KEY_period:
    case GDK_KEY_greater:      return "Period";
    case GDK_KEY_slash:
    case GDK_KEY_question:     return "Slash";
    case GDK_KEY_grave:
    case GDK_KEY_asciitilde:   return "Backquote";
    case GDK_KEY_bracketleft:
    case GDK_KEY_braceleft:    return "BracketLeft";
    case GDK_KEY_backslash:
    case GDK_KEY_bar:          return "Backslash";
    case GDK_KEY_bracketright:
    case GDK_KEY_braceright:   return "BracketRight";
    case GDK_KEY_apostrophe:
    case GDK_KEY_quotedbl:     return "Quote";
    default:                   return NULL;
    }
}

static gboolean
ns_im_has_preedit(GtkIMContext *im)
{
    if (!im) return FALSE;
    char *str = NULL;
    gtk_im_context_get_preedit_string(im, &str, NULL, NULL);
    gboolean has = str && *str;
    g_free(str);
    return has;
}

static gboolean
ns_dispatch_key_event_common(ns_window *w, const char *type, guint keyval,
                             GdkModifierType state, GdkEvent *event)
{
    if (strcmp(type, "keydown") == 0 && w->focused_input) {
        ns_window_ensure_im_context(w);
        gboolean action_key = ns_keyval_is_action(keyval);
        gboolean composing  = ns_im_has_preedit(w->im_context);
        if (!(action_key && !composing) && w->im_context && event &&
            gtk_im_context_filter_keypress(w->im_context, event))
            return TRUE;
        if (ns_window_handle_input_key(w, keyval, state))
            return TRUE;
    }
    if (!w->js) return FALSE;
    const ns_node *target = w->focused_input ? w->focused_input
                                             : ns_window_key_target(w);
    if (!target) return FALSE;
    const char *name = gdk_keyval_name(keyval);
    char key_buf[8] = {0};
    gunichar uc = gdk_keyval_to_unicode(keyval);
    const char *key;
    if (uc >= 0x20 && uc != 0x7f) {
        int len = g_unichar_to_utf8(uc, key_buf);
        key_buf[len] = 0;
        key = key_buf;
    } else {
        key = name ? name : "";
    }
    if (name) {
        if (strcmp(name, "Up") == 0)         key = "ArrowUp";
        else if (strcmp(name, "Down") == 0)  key = "ArrowDown";
        else if (strcmp(name, "Left") == 0)  key = "ArrowLeft";
        else if (strcmp(name, "Right") == 0) key = "ArrowRight";
        else if (strcmp(name, "Return") == 0 ||
                 strcmp(name, "KP_Enter") == 0) key = "Enter";
        else if (strcmp(name, "Escape") == 0) key = "Escape";
        else if (strcmp(name, "BackSpace") == 0) key = "Backspace";
        else if (strcmp(name, "Tab") == 0)    key = "Tab";
        else if (strcmp(name, "Page_Up") == 0)   key = "PageUp";
        else if (strcmp(name, "Page_Down") == 0) key = "PageDown";
        else if (strcmp(name, "Home") == 0)   key = "Home";
        else if (strcmp(name, "End") == 0)    key = "End";
        else if (strcmp(name, "Delete") == 0) key = "Delete";
        else if (strcmp(name, "Insert") == 0) key = "Insert";
    }
    char code_buf[16];
    const char *code = ns_keyval_to_js_code(keyval, code_buf, sizeof code_buf);
    if (!code) code = name ? name : "";
    gboolean prevented = FALSE;
    ns_js_dispatch_key_event(w->js, target, type, key, code,
                             ns_keyval_to_js_keycode(keyval),
                             (state & GDK_SHIFT_MASK)   != 0,
                             (state & GDK_CONTROL_MASK) != 0,
                             (state & GDK_ALT_MASK)     != 0,
                             (state & GDK_META_MASK)    != 0,
                             &prevented);
    if (ns_js_consume_mutated(w->js)) ns_window_js_mutated(w);
    return prevented;
}

gboolean
ns_on_drawing_key_pressed(GtkEventControllerKey *c, guint keyval, guint keycode,
                          GdkModifierType state, gpointer user_data)
{
    (void)keycode;
    ns_window *w = user_data;
    if (keyval == GDK_KEY_Escape && w->js &&
        ns_js_close_topmost_modal_dialog(w->js)) {
        if (ns_js_consume_mutated(w->js)) ns_window_js_mutated(w);
        if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
        return TRUE;
    }
    if ((keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab) &&
        !(state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) && w->js) {
        gboolean backward = (state & GDK_SHIFT_MASK) != 0 ||
                            keyval == GDK_KEY_ISO_Left_Tab;
        const ns_node *target = ns_js_sequential_focus_target(w->js, backward);
        if (target) {
            if (ns_node_is_editable(target))
                ns_window_set_focused_input(w, (ns_node *)target);
            else {
                ns_window_set_focused_input(w, NULL);
                ns_js_set_focus(w->js, target);
            }
            if (ns_js_consume_mutated(w->js)) ns_window_js_mutated(w);
            if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
            return TRUE;
        }
    }
    if ((state & GDK_CONTROL_MASK) && !w->focused_input) {
        if (keyval == GDK_KEY_c || keyval == GDK_KEY_C) {
            char *text = ns_selection_collect_text(w->layout_tree, &w->selection);
            if (text && *text) {
                GdkClipboard *cb = gtk_widget_get_clipboard(w->drawing_area);
                gdk_clipboard_set_text(cb, text);
                ns_window_set_status(w, "Copied %d characters",
                                     (int)g_utf8_strlen(text, -1));
                g_free(text);
                return TRUE;
            }
            g_free(text);
        } else if (keyval == GDK_KEY_a || keyval == GDK_KEY_A) {
            if (w->layout_tree &&
                ns_selection_select_all(&w->selection, w->layout_tree)) {
                ns_window_sync_selection_to_js(w);
                if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
                return TRUE;
            }
        }
    }
    GdkEvent *event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(c));
    gboolean prevented = ns_dispatch_key_event_common(user_data, "keydown", keyval, state, event);
    if (prevented) return TRUE;
    if (!w->focused_input && w->render_vadj) {
        double page = gtk_adjustment_get_page_size(w->render_vadj);
        double step = gtk_adjustment_get_step_increment(w->render_vadj);
        if (step <= 0) step = 40;
        double cur = gtk_adjustment_get_value(w->render_vadj);
        double lo  = gtk_adjustment_get_lower(w->render_vadj);
        double hi  = gtk_adjustment_get_upper(w->render_vadj);
        double max_value = hi - page;
        if (max_value < lo) max_value = lo;
        double target = cur;
        switch (keyval) {
        case GDK_KEY_Page_Down: target = cur + page * 0.9; break;
        case GDK_KEY_Page_Up:   target = cur - page * 0.9; break;
        case GDK_KEY_space:
            if (!(state & GDK_SHIFT_MASK)) target = cur + page * 0.9;
            else                            target = cur - page * 0.9;
            break;
        case GDK_KEY_End:
            if (!(state & (GDK_CONTROL_MASK))) target = max_value;
            else                                target = max_value;
            break;
        case GDK_KEY_Home:
            target = lo;
            break;
        case GDK_KEY_Up:
            if (!(state & GDK_CONTROL_MASK)) target = cur - step * 3;
            break;
        case GDK_KEY_Down:
            if (!(state & GDK_CONTROL_MASK)) target = cur + step * 3;
            break;
        default: return FALSE;
        }
        if (target < lo) target = lo;
        if (target > max_value) target = max_value;
        if (target != cur) {
            gtk_adjustment_set_value(w->render_vadj, target);
            return TRUE;
        }
    }
    return FALSE;
}

void
ns_on_drawing_key_released(GtkEventControllerKey *c, guint keyval, guint keycode,
                           GdkModifierType state, gpointer user_data)
{
    (void)keycode;
    GdkEvent *event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(c));
    ns_dispatch_key_event_common(user_data, "keyup", keyval, state, event);
}

static void ns_window_after_zoom(ns_window *w);
static void on_win_zoom_in(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void on_win_zoom_out(GSimpleAction *action, GVariant *parameter, gpointer user_data);

static gboolean
ns_window_should_defer_draw_layout(ns_window *w, double viewport_width)
{
    if (!w || !w->layout_tree || !w->layout_dirty ||
        !w->js_relayout_idle_id)
        return FALSE;
    if (fabs(viewport_width - w->last_viewport_w) >= 16.0)
        return FALSE;
    gint64 now = g_get_monotonic_time();
    guint delay = 0;
    if (w->js_relayout_deadline_us > now)
        delay = (guint)((w->js_relayout_deadline_us - now + 999) / 1000);
    ns_window_profile_relayout_timer(w, "defer-draw",
                                     w->layout_dirty_reason, delay, now);
    return TRUE;
}

gboolean
ns_on_drawing_scroll(GtkEventControllerScroll *c, double dx, double dy,
                     gpointer user_data)
{
    ns_window *w = user_data;
    if (!w->layout_tree || !w->drawing_area) return FALSE;
    w->last_wheel_us = g_get_monotonic_time();
    ns_window_postpone_relayout_after_wheel(w);

    GdkModifierType state =
        gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(c));

    if (w->js) {
        const ns_node *wt = ns_window_event_target_at(w, w->cursor_x, w->cursor_y);
        if (wt && ns_js_has_event_handler(w->js, wt, "wheel")) {
            double cx, cy;
            ns_window_client_xy(w, w->cursor_x, w->cursor_y, &cx, &cy);
            gboolean prevented = FALSE;
            ns_js_dispatch_wheel_event(w->js, wt, cx, cy,
                                       w->cursor_x, w->cursor_y,
                                       dx * 40.0, dy * 40.0,
                                       (state & GDK_SHIFT_MASK)   != 0,
                                       (state & GDK_CONTROL_MASK) != 0,
                                       (state & GDK_ALT_MASK)     != 0,
                                       (state & GDK_META_MASK)    != 0,
                                       &prevented);
            if (w->js && ns_js_consume_mutated(w->js)) ns_window_js_mutated(w);
            if (prevented) return TRUE;
            if (!w->layout_tree) return TRUE;
        }
    }

    if (state & GDK_CONTROL_MASK) {
        if (dy < 0)      on_win_zoom_in(NULL, NULL, w);
        else if (dy > 0) on_win_zoom_out(NULL, NULL, w);
        return TRUE;
    }

    if ((state & GDK_SHIFT_MASK) && dx == 0.0) {
        dx = dy;
        dy = 0.0;
    }

    ns_box *target = ns_box_hit_scrollable(w->layout_tree,
                                           w->cursor_x, w->cursor_y);
    if (!target) return FALSE;
    double step = 40.0;
    double new_x = target->scroll_x + dx * step;
    double new_y = target->scroll_y + dy * step;
    if (new_x < 0) new_x = 0;
    if (new_x > target->scroll_max_x) new_x = target->scroll_max_x;
    if (new_y < 0) new_y = 0;
    if (new_y > target->scroll_max_y) new_y = target->scroll_max_y;
    gboolean changed_x = (new_x != target->scroll_x);
    gboolean changed_y = (new_y != target->scroll_y);
    if (!changed_x && !changed_y) return FALSE;
    target->scroll_x = new_x;
    target->scroll_y = new_y;
    gtk_widget_queue_draw(w->drawing_area);
    return TRUE;
}

void
ns_on_drawing_pressed_middle(GtkGestureClick *gesture, int n_press,
                             double x, double y, gpointer user_data)
{
    (void)gesture; (void)n_press;
    ns_window *w = user_data;
    if (!w->layout_tree) return;
    const char *href = ns_box_hit_link(w->layout_tree, x, y);
    if (!href) return;
    GtkApplication *app = gtk_window_get_application(GTK_WINDOW(w->window));
    char *abs_url = ns_resolve_url(w, href);
    if (abs_url) {
        ns_spawn_window(app, abs_url);
        g_free(abs_url);
    }
}

void
ns_on_drawing_side_pressed(GtkGestureClick *gesture, int n_press,
                           double x, double y, gpointer user_data)
{
    (void)n_press; (void)x; (void)y;
    ns_window *w = user_data;
    guint button = gtk_gesture_single_get_current_button(
        GTK_GESTURE_SINGLE(gesture));
    if (button == 8) {
        if (w->cursor <= 0) return;
        w->cursor--;
        ns_window_load_url(w, g_ptr_array_index(w->history, w->cursor),
                           NS_LOAD_HISTORY);
    } else if (button == 9) {
        if (w->cursor < 0 || w->cursor + 1 >= (int)w->history->len) return;
        w->cursor++;
        ns_window_load_url(w, g_ptr_array_index(w->history, w->cursor),
                           NS_LOAD_HISTORY);
    }
}

void
ns_on_drawing_zoom_begin(GtkGesture *gesture, GdkEventSequence *seq,
                         gpointer user_data)
{
    (void)gesture; (void)seq;
    ns_window *w = user_data;
    w->pinch_base_zoom = w->zoom;
}

void
ns_on_drawing_zoom_end(GtkGesture *gesture, GdkEventSequence *seq,
                       gpointer user_data)
{
    (void)seq;
    ns_window *w = user_data;
    double base = w->pinch_base_zoom > 0 ? w->pinch_base_zoom : w->zoom;
    double target = base * gtk_gesture_zoom_get_scale_delta(
        GTK_GESTURE_ZOOM(gesture));
    if (target > 5.0) target = 5.0;
    if (target < 0.4) target = 0.4;
    if (fabs(target - w->zoom) < 0.001) return;
    w->zoom = target;
    ns_window_after_zoom(w);
}

void
ns_on_drawing_drag_begin(GtkGestureDrag *gesture, double x, double y,
                         gpointer user_data)
{
    ns_window *w = user_data;
    w->drag_start_x = x;
    w->drag_start_y = y;
    if (!w->layout_tree) return;
    if (ns_window_begin_html_drag(w, gesture, x, y)) {
        ns_selection_clear(&w->selection);
        ns_window_sync_selection_to_js(w);
        if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
        return;
    }
    if (ns_box_hit_link_range(w->layout_tree, x, y)) {
        ns_selection_clear(&w->selection);
        ns_window_sync_selection_to_js(w);
        if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
        return;
    }
    ns_selection_anchor_at(&w->selection, w->layout_tree, x, y);
    ns_window_sync_selection_to_js(w);
    if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
}

void
ns_on_drawing_drag_update(GtkGestureDrag *gesture, double dx, double dy,
                          gpointer user_data)
{
    ns_window *w = user_data;
    if (ns_window_update_html_drag(w, gesture,
                                   w->drag_start_x + dx,
                                   w->drag_start_y + dy))
        return;
    if (!w->layout_tree || !w->selection.active) return;
    ns_selection_extend_to(&w->selection, w->layout_tree,
                           w->drag_start_x + dx, w->drag_start_y + dy);
    ns_window_sync_selection_to_js(w);
    if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
}

void
ns_on_drawing_drag_end(GtkGestureDrag *gesture, double dx, double dy,
                       gpointer user_data)
{
    ns_window *w = user_data;
    if (ns_window_end_html_drag(w, gesture,
                                w->drag_start_x + dx,
                                w->drag_start_y + dy))
        return;
    if (!w->layout_tree || !w->selection.active) return;
    if (fabs(dx) < 2 && fabs(dy) < 2) {
        ns_selection_clear(&w->selection);
    } else {
        ns_selection_extend_to(&w->selection, w->layout_tree,
                               w->drag_start_x + dx, w->drag_start_y + dy);
    }
    ns_window_sync_selection_to_js(w);
    if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
}

void
ns_draw_render(GtkDrawingArea *area, cairo_t *cr,
               int width, int height, gpointer user_data)
{
    (void)area;
    (void)height;
    ns_window *w = user_data;
    cairo_set_source_rgb(cr, 0.94, 0.94, 0.95);
    cairo_paint(cr);
    if (w->pdf) {
        double total_h = 0;
        ns_pdf_paint(w->pdf, cr, (double)width, &total_h);
        int h_req = (int)(total_h + 0.5);
        if (h_req > height) gtk_widget_set_size_request(w->drawing_area, -1, h_req);
        return;
    }
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    if (!w->last_body || !is_html_content_type(w->last_content_type))
        return;
    if (!w->first_paint_done && w->css_inflight > 0)
        return;
    double vw = (double)width;
    GtkWidget *sw = gtk_widget_get_ancestor(w->drawing_area,
                                            GTK_TYPE_SCROLLED_WINDOW);
    if (sw) {
        GtkAdjustment *ha =
            gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(sw));
        double page = ha ? gtk_adjustment_get_page_size(ha) : 0;
        if (page > 0) vw = page;
        else { int sww = gtk_widget_get_width(sw); if (sww > 0) vw = (double)sww; }
    }
    if (!ns_window_should_defer_draw_layout(w, vw))
        ns_window_ensure_layout(w, vw);
    if (!w->layout_tree) return;
    ns_window_maybe_kick_visible_image_loads(w, FALSE);
    ns_paint_set_js(w->js);
    ns_paint_set_anim(w->anim);
    gboolean profile = ns_profile_enabled();
    if (profile) ns_paint_stats_reset();
    gint64 t_paint = profile ? g_get_monotonic_time() : 0;
    double clip_x = 0;
    double clip_y = 0;
    double clip_w = width;
    double clip_h = height;
    if (sw) {
        GtkAdjustment *ha =
            gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(sw));
        GtkAdjustment *va =
            gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw));
        if (ha) {
            double page = gtk_adjustment_get_page_size(ha);
            if (page > 0) {
                clip_x = gtk_adjustment_get_value(ha);
                clip_w = page;
            }
        }
        if (va) {
            double page = gtk_adjustment_get_page_size(va);
            if (page > 0) {
                clip_y = gtk_adjustment_get_value(va);
                clip_h = page;
            }
        }
    }
    double overscan_y = clip_h * 2.5;
    if (overscan_y < 2000) overscan_y = 2000;
    if (overscan_y > 5000) overscan_y = 5000;
    double overscan_x = 128;
    clip_x -= overscan_x;
    clip_w += overscan_x * 2.0;
    clip_y -= overscan_y;
    clip_h += overscan_y * 2.0;
    double ix0 = floor(clip_x);
    double iy0 = floor(clip_y);
    double ix1 = ceil(clip_x + clip_w);
    double iy1 = ceil(clip_y + clip_h);
    ns_paint_set_cull_margin(800.0);
    cairo_save(cr);
    cairo_rectangle(cr, ix0, iy0, ix1 - ix0, iy1 - iy0);
    cairo_clip(cr);
    ns_paint_with_selection(cr, w->layout_tree, w->search_query, &w->selection);
    cairo_restore(cr);
    ns_paint_set_cull_margin(400.0);
    gint64 paint_us = g_get_monotonic_time() - t_paint;
    ns_paint_stats ps = {0};
    if (profile) ns_paint_stats_get(&ps);
    if (profile) {
        ns_debug_log_emit(NS_DLOG_RENDER, "paint",
                          "vp=%d total=%.1fms boxes=%u culled=%u%s",
                          width, paint_us / 1000.0,
                          ps.boxes_seen, ps.culled_bounds + ps.offscreen,
                          w->first_paint_done ? "" : " (first)");
    } else {
        ns_debug_log_emit(NS_DLOG_RENDER, "paint",
                          "vp=%d total=%.1fms%s",
                          width, paint_us / 1000.0,
                          w->first_paint_done ? "" : " (first)");
    }
    if (profile)
        g_printerr("[profile] paint vp=%d total=%.1fms boxes=%u "
                   "hidden=%u skipped=%u culled=%u offscreen=%u "
                   "blocks=%u inlines=%u images=%u videos=%u canvases=%u "
                   "groups=%u clips=%u sorted=%u/%u\n",
                   width, paint_us / 1000.0, ps.boxes_seen,
                   ps.hidden, ps.skipped_top, ps.culled_bounds, ps.offscreen,
                   ps.blocks, ps.inlines, ps.images, ps.videos, ps.canvases,
                   ps.grouped, ps.overflow_clips,
                   ps.sorted_parents, ps.sorted_children);
    w->first_paint_done = TRUE;
}

char *
ns_resolve_url(const ns_window *w, const char *href)
{
    if (!href || !*href) return NULL;
    if (w->cursor < 0 || w->cursor >= (int)w->history->len)
        return ns_url_resolve(NULL, href);
    const char *base = g_ptr_array_index(w->history, w->cursor);
    g_autofree char *resolved_base = NULL;
    if (w->parsed_doc) {
        ns_node *base_el = ns_node_find_first_element(w->parsed_doc, "base");
        if (base_el) {
            const char *b = ns_element_get_attr(base_el, "href");
            if (b && *b) {
                resolved_base = ns_url_resolve(base, b);
                if (resolved_base && ns_url_is_http_or_https(resolved_base))
                    base = resolved_base;
            }
        }
    }
    return ns_url_resolve(base, href);
}

static gboolean
ns_window_image_ready_needs_relayout(ns_window *w, ns_image *img)
{
    if (!img) return TRUE;
    if (!w->layout_tree) return FALSE;
    GPtrArray *imgs = g_ptr_array_new();
    ns_layout_collect_images(w->layout_tree, imgs);
    gboolean needs = FALSE;
    for (guint i = 0; i < imgs->len && !needs; i++) {
        ns_box *box = g_ptr_array_index(imgs, i);
        if (!box->media) continue;
        gboolean fixed = box->media->size_independent_of_image ||
                         box->media->declared_image_size ||
                         box->media->placeholder_image_size;
        if (box->media->image == (void *)img && !fixed)
            needs = TRUE;
    }
    g_ptr_array_free(imgs, TRUE);
    return needs;
}

static void
on_image_ready(ns_image *img, gpointer user_data)
{
    ns_window *w = user_data;
    if (!w) return;
    if (img && img->failed && img->url) {
        char *line = g_strdup_printf("[error] image: %s — %s",
                                     img->url,
                                     img->error ? img->error : "failed");
        ns_window_console_append(w, line);
        ns_debug_log_emit(NS_DLOG_ERROR, "image", "%s: %s",
                          img->url, img->error ? img->error : "failed");
        g_free(line);
    } else if (img && img->url) {
        ns_debug_log_emit(NS_DLOG_RENDER, "image",
                          "ready %dx%d %s",
                          img->natural_width, img->natural_height, img->url);
    }
    if (img && img->failed)
        ns_window_schedule_image_retry(w, img);
    if (img && img->failed && img->http_status == 429) {
        gint64 wait_us = img->attempts <= 1 ? 15 * G_USEC_PER_SEC
                                            : 45 * G_USEC_PER_SEC;
        gint64 until_us = g_get_monotonic_time() + wait_us;
        if (w->image_backoff_until_us < until_us)
            w->image_backoff_until_us = until_us;
    }
    if (w->mode != NS_VIEW_RENDER || !w->drawing_area) return;
    if (ns_window_image_ready_needs_relayout(w, img)) {
        ns_window_schedule_relayout(w,
            ns_window_adaptive_relayout_delay(
                w, 250, ns_window_relayout_max_delay(w, 1500)), "image");
    } else {
        gtk_widget_queue_draw(w->drawing_area);
    }
    ns_window_schedule_image_kick(w, 25);
}

static void
on_video_ready(ns_video *v, gpointer user_data)
{
    (void)v;
    ns_window *w = user_data;
    if (w->mode == NS_VIEW_RENDER && w->drawing_area)
        gtk_widget_queue_draw(w->drawing_area);
}

typedef struct ns_css_fetch {
    guint      w_id;
    guint      gen;
    char      *url;
    char      *integrity;
    char      *scope_id;
} ns_css_fetch;

static void
ns_css_fetch_free(ns_css_fetch *fetch)
{
    if (!fetch) return;
    g_free(fetch->url);
    g_free(fetch->integrity);
    g_free(fetch->scope_id);
    g_free(fetch);
}

static void
ns_window_css_fetch_done(ns_window *w)
{
    if (!w) return;
    if (g_getenv("NS_CSS_DEBUG"))
        g_printerr("[cssdbg] done inflight=%u->%u gen=%u first=%d\n",
                   w->css_inflight,
                   w->css_inflight > 0 ? w->css_inflight - 1 : 0,
                   w->fetch_gen, w->first_paint_done);
    if (w->css_inflight > 0) w->css_inflight--;
    if (w->css_inflight == 0 && !w->first_paint_done)
        w->first_paint_done = TRUE;
    if (w->css_inflight == 0 && w->drawing_area)
        gtk_widget_queue_draw(w->drawing_area);
}

static void
on_external_css_parsed(ns_tab_css_result *parsed, gpointer user_data)
{
    ns_css_fetch *fetch = user_data;
    ns_window *w = ns_window_for_id(fetch->w_id);
    if (!w || fetch->gen != w->fetch_gen) {
        ns_tab_css_result_free(parsed);
        ns_css_fetch_free(fetch);
        return;
    }
    if (parsed && parsed->sheet && w->external_stylesheets) {
        GHashTable *seen =
            g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        g_hash_table_add(seen, g_strdup(fetch->url));
        guint first = w->external_stylesheets->len;
        ns_window_append_stylesheet_expanded(w, w->external_stylesheets,
                                             parsed->sheet, fetch->url,
                                             seen, 0);
        parsed->sheet = NULL;
        ns_window_store_external_stylesheet_group(w, fetch->url, first);
        g_hash_table_destroy(seen);
        ns_window_mark_layout_dirty(w);
    }
    ns_tab_css_result_free(parsed);
    ns_window_css_fetch_done(w);
    ns_css_fetch_free(fetch);
}

static void
on_external_css_loaded(GObject *src, GAsyncResult *result, gpointer user_data)
{
    (void)src;
    ns_css_fetch *fetch = user_data;
    GError *err = NULL;
    ns_response *resp = ns_net_fetch_finish(result, &err);
    ns_window *w = ns_window_for_id(fetch->w_id);
    if (!w || fetch->gen != w->fetch_gen) {
        if (g_getenv("NS_CSS_DEBUG"))
            g_printerr("[cssdbg] orphan css cb gen=%u cur=%u inflight=%u %s\n",
                       fetch->gen, w ? w->fetch_gen : 0,
                       w ? w->css_inflight : 0, fetch->url);
        g_clear_error(&err);
        ns_response_free(resp);
        ns_css_fetch_free(fetch);
        return;
    }
    if (err) {
        if (!g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            ns_window_set_status(w, "CSS fetch failed: %s", err->message);
            char *line = g_strdup_printf("[error] stylesheet: %s — %s",
                                         fetch->url, err->message);
            ns_window_console_append(w, line);
            g_free(line);
        }
        g_clear_error(&err);
        ns_response_free(resp);
        ns_window_css_fetch_done(w);
        ns_css_fetch_free(fetch);
        return;
    }
    if (!resp) {
        ns_window_css_fetch_done(w);
        ns_css_fetch_free(fetch);
        return;
    }
    if (resp->error) {
        char *line = g_strdup_printf("[error] stylesheet: %s — %s",
                                     fetch->url, resp->error);
        ns_window_console_append(w, line);
        g_free(line);
    } else if (resp->status >= 400) {
        char *line = g_strdup_printf("[error] stylesheet: %s — HTTP %ld",
                                     fetch->url, resp->status);
        ns_window_console_append(w, line);
        g_free(line);
    }
    else if (resp->body && resp->body->len > 0 && w && w->external_stylesheets &&
             !ns_security_sri_check(fetch->integrity, resp->body->data, resp->body->len)) {
        char *line = g_strdup_printf(
            "SRI mismatch: stylesheet %s (integrity=\"%s\")",
            fetch->url, fetch->integrity);
        ns_window_console_append(w, line);
        g_free(line);
    } else if (resp->body && resp->body->len > 0 && w && w->external_stylesheets) {
        if (w->worker &&
            ns_tab_worker_parse_css_response(w->worker, resp, fetch->scope_id,
                                             on_external_css_parsed, fetch,
                                             (GDestroyNotify)ns_css_fetch_free))
            return;
        ns_tab_css_result *parsed = g_new0(ns_tab_css_result, 1);
        parsed->resp = resp;
        char *scoped = fetch->scope_id
            ? ns_css_scope_css((const char *)resp->body->data,
                               (gssize)resp->body->len, fetch->scope_id)
            : NULL;
        parsed->sheet = scoped
            ? ns_css_stylesheet_parse(scoped, (gssize)strlen(scoped))
            : ns_css_stylesheet_parse((const char *)resp->body->data,
                                      (gssize)resp->body->len);
        g_free(scoped);
        on_external_css_parsed(parsed, fetch);
        return;
    }
    ns_response_free(resp);
    ns_window_css_fetch_done(w);
    ns_css_fetch_free(fetch);
}

static char *
extract_attr_value(const char *tag, const char *end, const char *name)
{
    gsize nlen = strlen(name);
    const char *p = tag;
    while (p + nlen < end) {
        if (g_ascii_strncasecmp(p, name, nlen) == 0) {
            const char *after = p + nlen;
            while (after < end && (*after == ' ' || *after == '\t' ||
                                    *after == '\r' || *after == '\n'))
                after++;
            if (after < end && *after == '=') {
                after++;
                while (after < end && (*after == ' ' || *after == '\t' ||
                                        *after == '\r' || *after == '\n'))
                    after++;
                if (after >= end) return NULL;
                char quote = 0;
                if (*after == '"' || *after == '\'') { quote = *after; after++; }
                const char *start = after;
                while (after < end) {
                    if (quote && *after == quote) break;
                    if (!quote && (*after == ' ' || *after == '\t' ||
                                   *after == '>' || *after == '/' ||
                                   *after == '\r' || *after == '\n')) break;
                    after++;
                }
                return g_strndup(start, (gsize)(after - start));
            }
        }
        p++;
    }
    return NULL;
}

static void
ns_window_preload_stylesheets(ns_window *w, const char *html, gsize len)
{
    if (!html || len == 0) return;
    if (!w->external_stylesheets) {
        w->external_stylesheets = g_ptr_array_new();
        w->external_css_seen = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                      g_free, NULL);
    }
    if (!w->external_css_loaded)
        w->external_css_loaded =
            g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                  (GDestroyNotify)g_ptr_array_unref);
    if (!w->css_cancellable) w->css_cancellable = g_cancellable_new();

    const char *p = html;
    const char *end = html + len;
    while (p < end) {
        const char *lt = memchr(p, '<', (gsize)(end - p));
        if (!lt) break;
        if (lt + 5 < end && g_ascii_strncasecmp(lt, "<link", 5) == 0 &&
            (lt[5] == ' ' || lt[5] == '\t' || lt[5] == '\r' ||
             lt[5] == '\n' || lt[5] == '/' || lt[5] == '>')) {
            const char *gt = memchr(lt, '>', (gsize)(end - lt));
            if (!gt) break;
            char *rel  = extract_attr_value(lt + 5, gt, "rel");
            char *href = extract_attr_value(lt + 5, gt, "href");
            char *integrity = extract_attr_value(lt + 5, gt, "integrity");
            if (href && *href && ns_rel_is_stylesheet(rel)) {
                char *abs = ns_resolve_url(w, href);
                if (abs && !ns_window_subresource_blocked(
                                w, abs, NS_CSP_STYLE, "stylesheet") &&
                    !g_hash_table_contains(w->external_css_seen, abs)) {
                    g_hash_table_add(w->external_css_seen, g_strdup(abs));
                    ns_css_fetch *fetch = g_new0(ns_css_fetch, 1);
                    fetch->w_id = w->id;
                    fetch->gen = w->fetch_gen;
                    fetch->url = abs;
                    fetch->integrity = integrity ? g_strdup(integrity) : NULL;
                    w->css_inflight++;
                    ns_net_fetch_async(abs, ns_window_current_url(w),
                                       w->css_cancellable,
                                       on_external_css_loaded, fetch);
                } else {
                    g_free(abs);
                }
            }
            g_free(rel);
            g_free(href);
            g_free(integrity);
            p = gt + 1;
            continue;
        }
        p = lt + 1;
    }
}

static void
ns_window_kick_stylesheet_loads(ns_window *w)
{
    if (!w->parsed_doc) return;
    if (!w->external_stylesheets) {
        w->external_stylesheets = g_ptr_array_new();
        w->external_css_seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    }
    if (!w->external_css_loaded)
        w->external_css_loaded =
            g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                  (GDestroyNotify)g_ptr_array_unref);
    if (!w->css_cancellable) w->css_cancellable = g_cancellable_new();

    GQueue queue = G_QUEUE_INIT;
    g_queue_push_tail(&queue, w->parsed_doc);
    while (!g_queue_is_empty(&queue)) {
        ns_node *n = g_queue_pop_head(&queue);
        if (ns_node_is_element_named(n, "link")) {
            const char *rel = ns_element_get_attr(n, "rel");
            const char *href = ns_element_get_attr(n, "href");
            const char *media = ns_element_get_attr(n, "media");
            if (href && *href && ns_rel_is_stylesheet(rel) &&
                (!media || !*media || ns_css_media_query_matches(media))) {
                char *abs = ns_window_resolve_for_node(w, n, href);
                if (abs && ns_window_subresource_blocked(w, abs, NS_CSP_STYLE, "stylesheet")) {
                    g_free(abs);
                    continue;
                }
                if (abs && !g_hash_table_contains(w->external_css_seen, abs)) {
                    g_hash_table_add(w->external_css_seen, g_strdup(abs));
                    const char *integrity = ns_element_get_attr(n, "integrity");
                    ns_css_fetch *fetch = g_new0(ns_css_fetch, 1);
                    fetch->w_id = w->id;
                    fetch->gen = w->fetch_gen;
                    fetch->url = abs;
                    fetch->integrity = integrity ? g_strdup(integrity) : NULL;
                    fetch->scope_id = ns_css_assign_iframe_scope(n);
                    w->css_inflight++;
                    ns_net_fetch_async(abs, ns_window_current_url(w), w->css_cancellable,
                                       on_external_css_loaded, fetch);
                    continue;
                }
                g_free(abs);
            }
        }
        for (ns_node *c = n->first_child; c; c = c->next_sibling)
            g_queue_push_tail(&queue, c);
    }
}

static gboolean
mixed_content_blocked(ns_window *w, const char *abs_url, const char *kind)
{
    const char *page = ns_window_current_url(w);
    if (!page || !g_str_has_prefix(page, "https://")) return FALSE;
    if (!g_str_has_prefix(abs_url, "http://")) return FALSE;
    g_warning("mixed-content blocked: %s %s on https page", kind, abs_url);
    return TRUE;
}

static gboolean
ns_window_subresource_blocked(ns_window *w, const char *abs_url,
                              ns_csp_kind csp_kind, const char *kind_word)
{
    return mixed_content_blocked(w, abs_url, kind_word) ||
           csp_blocked(w, csp_kind, abs_url, kind_word);
}

static gboolean
csp_blocked(ns_window *w, ns_csp_kind kind, const char *abs_url,
            const char *kind_word)
{
    if (!w->csp) return FALSE;
    if (abs_url && g_str_has_prefix(abs_url, "nd-inline-svg:")) return FALSE;
    if (ns_csp_allows(w->csp, kind, abs_url, ns_window_current_url(w)))
        return FALSE;
    g_warning("CSP blocked: %s %s", kind_word, abs_url);
    return TRUE;
}

static const char *
ns_link_rel_icon_kind(const char *rel)
{
    if (!rel) return NULL;
    gchar **toks = g_strsplit_set(rel, " \t\n\r\f", -1);
    const char *match = NULL;
    for (gchar **p = toks; *p; p++) {
        if (!**p) continue;
        if (g_ascii_strcasecmp(*p, "icon") == 0 ||
            g_ascii_strcasecmp(*p, "shortcut") == 0 ||
            g_ascii_strcasecmp(*p, "apple-touch-icon") == 0 ||
            g_ascii_strcasecmp(*p, "apple-touch-icon-precomposed") == 0) {
            match = "icon";
            break;
        }
    }
    g_strfreev(toks);
    return match;
}

static char *
ns_window_pick_favicon_href(ns_window *w)
{
    if (!w->parsed_doc) return NULL;
    ns_node *head = ns_node_find_first_element(w->parsed_doc, "head");
    if (!head) return NULL;
    char *fallback = NULL;
    for (const ns_node *c = head->first_child; c; c = c->next_sibling) {
        if (c->kind != NS_NODE_ELEMENT || !c->name) continue;
        if (g_ascii_strcasecmp(c->name, "link") != 0) continue;
        const char *rel = ns_element_get_attr(c, "rel");
        const char *href = ns_element_get_attr(c, "href");
        if (!rel || !href || !*href) continue;
        if (!ns_link_rel_icon_kind(rel)) continue;
        return g_strdup(href);
    }
    return fallback;
}

static char *
ns_window_default_favicon_url(ns_window *w)
{
    const char *page = ns_window_current_url(w);
    if (!page) return NULL;
    if (!ns_url_is_http_or_https(page))
        return NULL;
    char *origin = ns_url_origin_from(page);
    if (!origin || !*origin) { g_free(origin); return NULL; }
    char *url = g_strdup_printf("%s/favicon.ico", origin);
    g_free(origin);
    return url;
}

static void
on_favicon_ready(ns_image *img, gpointer user_data)
{
    ns_window *w = user_data;
    if (!w || !w->tab_icon) return;
    if (!img || !img->loaded || !img->texture) return;
    if (img->natural_width <= 0 || img->natural_height <= 0) return;
    gtk_image_set_from_paintable(GTK_IMAGE(w->tab_icon),
                                 GDK_PAINTABLE(img->texture));
    gtk_image_set_pixel_size(GTK_IMAGE(w->tab_icon), 14);
    w->favicon_loaded = TRUE;
}

static void
ns_window_kick_favicon(ns_window *w)
{
    if (!w || !w->images || !w->tab_icon) return;
    if (w->favicon_loaded) return;
    const ns_config *cfg = ns_config_get();
    if (cfg && !cfg->images_enabled) return;
    const char *page = ns_window_current_url(w);
    if (!page) return;
    if (g_str_has_prefix(page, "about:") || g_str_has_prefix(page, "file:") ||
        g_str_has_prefix(page, "data:"))
        return;

    char *href = ns_window_pick_favicon_href(w);
    char *abs = href ? ns_resolve_url(w, href) : NULL;
    g_free(href);
    if (!abs) abs = ns_window_default_favicon_url(w);
    if (!abs) return;
    if (!ns_url_is_http_or_https(abs)) {
        g_free(abs);
        return;
    }
    if (ns_window_subresource_blocked(w, abs, NS_CSP_IMG, "favicon")) {
        g_free(abs);
        return;
    }
    ns_image_cache_get(w->images, abs, page, on_favicon_ready, w);
    g_free(abs);
}

static double
ns_window_visible_page_height(ns_window *w)
{
    double page = 0;
    if (w && w->render_vadj)
        page = gtk_adjustment_get_page_size(w->render_vadj);
    if (page <= 100 && w && w->drawing_area) {
        int h = gtk_widget_get_height(w->drawing_area);
        if (h > 100) page = h;
    }
    if (page <= 100 && w && w->window) {
        int h = gtk_widget_get_height(w->window);
        if (h > 100) page = h;
    }
    return page > 100 ? page : 900.0;
}

static gboolean
ns_box_in_fixed_layer(const ns_box *box)
{
    for (const ns_box *p = box; p; p = p->parent) {
        const ns_css_value *pos = p->style
            ? p->style->values[NS_CSS_POSITION]
            : NULL;
        if (ns_css_keyword_is(pos, "fixed") ||
            ns_css_keyword_is(pos, "sticky"))
            return TRUE;
    }
    return FALSE;
}

static double
ns_window_image_prefetch_margin(ns_window *w)
{
    double page = ns_window_visible_page_height(w);
    double margin = page * 3.0;
    if (margin < 2400) margin = 2400;
    if (margin > 6000) margin = 6000;
    return margin;
}

static double
ns_window_image_visible_margin(ns_window *w)
{
    double page = ns_window_visible_page_height(w);
    double margin = page * 1.5;
    if (margin < 1200) margin = 1200;
    if (margin > 2600) margin = 2600;
    return margin;
}

static gboolean
ns_window_image_box_in_margin(const ns_window *w, const ns_box *box,
                              double margin)
{
    if (!w || !box || !w->render_vadj) return TRUE;
    if (ns_box_in_fixed_layer(box)) return TRUE;
    double top = gtk_adjustment_get_value(w->render_vadj);
    double page = ns_window_visible_page_height((ns_window *)w);
    double box_h = box->content_height + box->padding.top + box->padding.bottom +
                   box->border.top + box->border.bottom;
    if (box_h < 1) box_h = 1;
    double bottom = top + page;
    return box->y + box_h >= top - margin &&
           box->y <= bottom + margin;
}

typedef struct ns_image_priority_ctx {
    double top;
    double bottom;
} ns_image_priority_ctx;

static double
ns_window_image_box_distance(const ns_image_priority_ctx *ctx,
                             const ns_box *box)
{
    if (!ctx || !box) return G_MAXDOUBLE;
    if (ns_box_in_fixed_layer(box)) return -1.0;
    double box_h = box->content_height + box->padding.top + box->padding.bottom +
                   box->border.top + box->border.bottom;
    if (box_h < 1) box_h = 1;
    double by0 = box->y;
    double by1 = box->y + box_h;
    if (by1 >= ctx->top && by0 <= ctx->bottom) return 0.0;
    if (by1 < ctx->top) return ctx->top - by1;
    return by0 - ctx->bottom;
}

static gint
ns_window_image_priority_cmp(gconstpointer a, gconstpointer b,
                             gpointer user_data)
{
    const ns_box *ba = *(const ns_box * const *)a;
    const ns_box *bb = *(const ns_box * const *)b;
    const ns_image_priority_ctx *ctx = user_data;
    double da = ns_window_image_box_distance(ctx, ba);
    double db = ns_window_image_box_distance(ctx, bb);
    if (da < db) return -1;
    if (da > db) return 1;
    double ya = ba ? ba->y : G_MAXDOUBLE;
    double yb = bb ? bb->y : G_MAXDOUBLE;
    if (ya < yb) return -1;
    if (ya > yb) return 1;
    return 0;
}

static void
ns_window_sort_images_by_view(ns_window *w, GPtrArray *imgs)
{
    if (!w || !imgs || imgs->len < 2 || !w->render_vadj) return;
    double top = gtk_adjustment_get_value(w->render_vadj);
    double page = ns_window_visible_page_height(w);
    ns_image_priority_ctx ctx = { top, top + page };
    g_ptr_array_sort_with_data(imgs, ns_window_image_priority_cmp, &ctx);
}

static gboolean
ns_image_inflight(const ns_image *img)
{
    return img && !img->loaded && !img->failed;
}

static guint
ns_window_image_inflight_count(GPtrArray *imgs)
{
    guint n = 0;
    for (guint i = 0; i < imgs->len; i++) {
        ns_box *box = g_ptr_array_index(imgs, i);
        if (!box || !box->media) continue;
        if (ns_image_inflight(box->media->image)) n++;
        if (ns_image_inflight(box->media->bg_image)) n++;
        if (box->media->bg_layer_images) {
            for (guint li = 0; li < box->media->bg_layer_images->len; li++)
                if (ns_image_inflight(g_ptr_array_index(
                        box->media->bg_layer_images, li)))
                    n++;
        }
    }
    return n;
}

static guint
ns_window_image_inflight_limit(ns_window *w)
{
    (void)w;
    return 4;
}

static guint
ns_window_image_next_delay_ms(ns_window *w, gint64 now_us)
{
    gint64 wait_us = 0;
    if (w->image_backoff_until_us > now_us) {
        wait_us = w->image_backoff_until_us - now_us;
    } else if (w->last_image_request_us > 0) {
        gint64 since_us = now_us - w->last_image_request_us;
        if (since_us < NS_IMAGE_START_GAP_US)
            wait_us = NS_IMAGE_START_GAP_US - since_us;
    }
    if (wait_us <= 0) return 0;
    guint delay = (guint)((wait_us + 999) / 1000);
    if (delay < 40) delay = 40;
    if (delay > 15000) delay = 15000;
    return delay;
}

static gboolean
ns_window_image_slot_available(guint inflight, guint limit, guint started)
{
    return inflight < limit && started == 0;
}

static ns_image *
ns_window_request_image(ns_window *w, const char *src, const char *kind,
                        guint *inflight, guint limit,
                        guint *started, gboolean *deferred)
{
    if (!src || g_str_has_prefix(src, "nd-inline-svg:")) return NULL;
    char *abs = ns_resolve_url(w, src);
    if (!abs) return NULL;
    gint64 now_us = g_get_monotonic_time();
    ns_image *cached = ns_image_cache_peek(w->images, abs);
    if (cached && !ns_image_should_retry(cached, now_us)) {
        g_free(abs);
        return cached;
    }
    if (!ns_window_image_slot_available(*inflight, limit, *started) ||
        ns_window_image_next_delay_ms(w, now_us) > 0) {
        if (deferred) *deferred = TRUE;
        g_free(abs);
        return NULL;
    }
    if (ns_window_subresource_blocked(w, abs, NS_CSP_IMG, kind)) {
        g_free(abs);
        return NULL;
    }
    gboolean was_inflight = ns_image_inflight(cached);
    int attempts_before = cached ? cached->attempts : 0;
    ns_image *img = ns_image_cache_get(w->images, abs,
        ns_window_current_url(w), on_image_ready, w);
    if (ns_image_inflight(img) && !was_inflight &&
        (!cached || img->attempts > attempts_before)) {
        (*inflight)++;
        (*started)++;
        w->last_image_request_us = now_us;
    }
    g_free(abs);
    return img;
}

static void
ns_window_kick_image_loads_with_margin(ns_window *w, double margin)
{
    if (!w->layout_tree || !w->images) return;
    gint64 now_us = g_get_monotonic_time();
    GPtrArray *imgs = g_ptr_array_new();
    ns_layout_collect_images(w->layout_tree, imgs);
    ns_window_sort_images_by_view(w, imgs);
    guint inflight = ns_window_image_inflight_count(imgs);
    guint limit = ns_window_image_inflight_limit(w);
    guint started = 0;
    gboolean deferred = FALSE;
    for (guint i = 0; i < imgs->len; i++) {
        ns_box *box = g_ptr_array_index(imgs, i);
        if (!box->media) continue;
        if (!ns_window_image_box_in_margin(w, box, margin)) continue;
        if (ns_image_should_retry(box->media->image, now_us))
            box->media->image = NULL;
        if (ns_image_should_retry(box->media->bg_image, now_us))
            box->media->bg_image = NULL;
        if (box->media->image_src && !box->media->image) {
            box->media->image = ns_window_request_image(w,
                box->media->image_src, "image",
                &inflight, limit, &started, &deferred);
            if (deferred) break;
        }
        if (box->media->bg_image_src && !box->media->bg_image) {
            box->media->bg_image = ns_window_request_image(w,
                box->media->bg_image_src, "image",
                &inflight, limit, &started, &deferred);
            if (deferred) break;
        }
        if (box->media->bg_layer_srcs) {
            for (guint li = 0; li < box->media->bg_layer_srcs->len; li++) {
                const char *src =
                    g_ptr_array_index(box->media->bg_layer_srcs, li);
                if (!src) continue;
                ns_image *cur =
                    g_ptr_array_index(box->media->bg_layer_images, li);
                if (ns_image_should_retry(cur, now_us)) cur = NULL;
                if (cur) continue;
                ns_image *img = ns_window_request_image(w, src, "image",
                    &inflight, limit, &started, &deferred);
                if (deferred) break;
                if (!img) continue;
                box->media->bg_layer_images->pdata[li] = img;
            }
            if (deferred) break;
        }
    }
    g_ptr_array_free(imgs, TRUE);
    if (started > 0 || deferred) {
        guint delay = ns_window_image_next_delay_ms(w, g_get_monotonic_time());
        if (delay == 0) delay = 250;
        ns_window_schedule_image_kick(w, delay);
    }
}

static void
ns_window_kick_image_loads(ns_window *w)
{
    ns_window_kick_image_loads_with_margin(w,
        ns_window_image_prefetch_margin(w));
}

static void
ns_window_kick_visible_image_loads(ns_window *w)
{
    ns_window_kick_image_loads_with_margin(w,
        ns_window_image_visible_margin(w));
}

static void
ns_window_maybe_kick_visible_image_loads(ns_window *w, gboolean force)
{
    if (!w || !w->layout_tree || !w->images) return;
    gint64 now_us = g_get_monotonic_time();
    double top = w->render_vadj ? gtk_adjustment_get_value(w->render_vadj) : 0;
    double page = ns_window_visible_page_height(w);
    double moved = fabs(top - w->last_visible_image_top);
    if (!force && w->last_visible_image_kick_us > 0 &&
        now_us - w->last_visible_image_kick_us < 80 * G_TIME_SPAN_MILLISECOND &&
        moved < page * 0.2)
        return;
    w->last_visible_image_kick_us = now_us;
    w->last_visible_image_top = top;
    ns_window_kick_visible_image_loads(w);
}

static gboolean
ns_window_scroll_image_load_cb(gpointer data)
{
    ns_window *w = data;
    if (!w) return G_SOURCE_REMOVE;
    w->scroll_image_source = 0;
    if (w->mode == NS_VIEW_RENDER) {
        ns_window_maybe_kick_visible_image_loads(w, TRUE);
        ns_window_kick_image_loads(w);
    }
    return G_SOURCE_REMOVE;
}

void
ns_window_render_vadjustment_changed(GtkAdjustment *adj, gpointer ud)
{
    (void)adj;
    ns_window *w = ud;
    if (!w || w->mode != NS_VIEW_RENDER) return;
    if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
    ns_window_maybe_kick_visible_image_loads(w, FALSE);
    ns_window_schedule_image_kick(w, 40);
}

static void
ns_window_kick_video_loads(ns_window *w)
{
    if (!w->layout_tree || !w->videos) return;
    GPtrArray *vids = g_ptr_array_new();
    ns_layout_collect_videos(w->layout_tree, vids);
    for (guint i = 0; i < vids->len; i++) {
        ns_box *box = g_ptr_array_index(vids, i);
        if (!box->media || !box->media->video_src) continue;
        ns_box_media *m = box->media;
        char *abs = ns_resolve_url(w, m->video_src);
        if (!abs) continue;
        if (ns_window_subresource_blocked(w, abs, NS_CSP_MEDIA, "video")) {
            g_free(abs);
            continue;
        }
        char *poster_abs = NULL;
        if (m->video_poster) poster_abs = ns_resolve_url(w, m->video_poster);
        if (poster_abs &&
            ns_window_subresource_blocked(w, poster_abs, NS_CSP_IMG, "video-poster")) {
            g_free(poster_abs);
            poster_abs = NULL;
        }
        m->video = ns_video_cache_get(w->videos, abs, poster_abs,
                                      ns_window_current_url(w),
                                      on_video_ready, w);
        g_free(abs);
        g_free(poster_abs);
    }
    g_ptr_array_free(vids, TRUE);
}

static gboolean
looks_like_host(const char *s, size_t len)
{
    if (len == 0) return FALSE;
    gboolean has_dot = FALSE;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c == ' ' || c == '\t') return FALSE;
        if (c == '.') has_dot = TRUE;
        if (c == '/' || c == '?' || c == '#') break;
    }
    return has_dot;
}

static char *
ns_normalize_url(const char *raw)
{
    if (!raw)
        return NULL;

    while (*raw == ' ' || *raw == '\t' || *raw == '\r' || *raw == '\n')
        raw++;
    size_t len = strlen(raw);
    while (len > 0 && (raw[len - 1] == ' ' || raw[len - 1] == '\t' ||
                       raw[len - 1] == '\r' || raw[len - 1] == '\n'))
        len--;
    if (len == 0)
        return NULL;

    {
        size_t out_len = 0;
        for (size_t i = 0; i < len; i++) {
            char c = raw[i];
            if (c == '\t' || c == '\r' || c == '\n') continue;
            out_len++;
        }
        if (out_len != len) {
            char *out = g_malloc(out_len + 1);
            size_t j = 0;
            for (size_t i = 0; i < len; i++) {
                char c = raw[i];
                if (c == '\t' || c == '\r' || c == '\n') continue;
                out[j++] = c;
            }
            out[j] = '\0';
            char *resolved = ns_normalize_url(out);
            g_free(out);
            return resolved;
        }
    }

    if ((len >= 6 && g_ascii_strncasecmp(raw, "about:", 6) == 0))
        return g_strndup(raw, len);

    gboolean has_scheme = FALSE;
    for (size_t i = 0; i < len; i++) {
        if (raw[i] == ':' && i + 2 < len && raw[i + 1] == '/' && raw[i + 2] == '/') {
            has_scheme = TRUE;
            break;
        }
        if (!g_ascii_isalnum(raw[i]) && raw[i] != '+' && raw[i] != '-' && raw[i] != '.')
            break;
    }

    if (has_scheme)
        return g_strndup(raw, len);

#ifdef G_OS_WIN32
    if (len >= 3 && g_ascii_isalpha(raw[0]) && raw[1] == ':' &&
        (raw[2] == '\\' || raw[2] == '/')) {
        GString *p = g_string_new("file:///");
        g_string_append_c(p, g_ascii_toupper(raw[0]));
        g_string_append_c(p, ':');
        for (size_t i = 2; i < len; i++) {
            char c = raw[i] == '\\' ? '/' : raw[i];
            if (c == ' ') g_string_append(p, "%20");
            else          g_string_append_c(p, c);
        }
        return g_string_free(p, FALSE);
    }
    if (len >= 3 && raw[0] == '\\' && raw[1] == '\\') {
        GString *p = g_string_new("file://");
        for (size_t i = 2; i < len; i++) {
            char c = raw[i] == '\\' ? '/' : raw[i];
            if (c == ' ') g_string_append(p, "%20");
            else          g_string_append_c(p, c);
        }
        return g_string_free(p, FALSE);
    }
#endif

    if (looks_like_host(raw, len) || raw[0] == '/') {
        char *bare = g_strndup(raw, len);
        char *full = g_strconcat("https://", bare, NULL);
        g_free(bare);
        return full;
    }

    char *query = g_strndup(raw, len);
    char *escaped = g_uri_escape_string(query, NULL, FALSE);
    g_free(query);
    const ns_config *cfg = ns_config_get();
    const char *tmpl = cfg && cfg->search_engine && *cfg->search_engine
                       ? cfg->search_engine
                       : "https://www.google.com/search?q=%s";
    const char *pct = strstr(tmpl, "%s");
    char *full;
    if (pct) {
        char *prefix = g_strndup(tmpl, (gsize)(pct - tmpl));
        full = g_strconcat(prefix, escaped, pct + 2, NULL);
        g_free(prefix);
    } else {
        full = g_strconcat(tmpl, escaped, NULL);
    }
    g_free(escaped);
    return full;
}

static char *
ns_download_extract_disposition_name(const char *disp)
{
    if (!disp || !*disp) return NULL;
    const char *p = strstr(disp, "filename*=");
    if (p) {
        p += 10;
        const char *q = strchr(p, '\'');
        if (q) {
            q = strchr(q + 1, '\'');
            if (q) p = q + 1;
        }
        gsize len = strcspn(p, ";");
        while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t')) len--;
        if (len > 0) {
            char *raw = g_strndup(p, len);
            char *decoded = g_uri_unescape_string(raw, NULL);
            g_free(raw);
            if (decoded && *decoded) return decoded;
            g_free(decoded);
        }
    }
    p = strstr(disp, "filename=");
    if (p) {
        p += 9;
        while (*p == ' ' || *p == '\t') p++;
        const char *end;
        if (*p == '"') {
            p++;
            end = strchr(p, '"');
            if (!end) end = p + strlen(p);
        } else {
            end = p + strcspn(p, ";");
            while (end > p && (end[-1] == ' ' || end[-1] == '\t')) end--;
        }
        if (end > p) return g_strndup(p, (gsize)(end - p));
    }
    return NULL;
}

static char *
ns_download_suggest_filename(const char *url, const char *disp)
{
    char *name = ns_download_extract_disposition_name(disp);
    if (name && *name) {
        for (char *s = name; *s; s++)
            if (*s == '/' || *s == '\\') *s = '_';
        return name;
    }
    g_free(name);
    if (!url) return g_strdup("download");
    const char *q = strchr(url, '?');
    gsize end_off = q ? (gsize)(q - url) : strlen(url);
    const char *frag = memchr(url, '#', end_off);
    if (frag) end_off = (gsize)(frag - url);
    gsize slash = end_off;
    while (slash > 0 && url[slash - 1] != '/') slash--;
    if (slash < end_off) {
        char *raw = g_strndup(url + slash, end_off - slash);
        char *decoded = g_uri_unescape_string(raw, NULL);
        g_free(raw);
        if (decoded && *decoded) return decoded;
        g_free(decoded);
    }
    return g_strdup("download");
}

static gboolean
ns_should_download(const char *content_type, const char *content_disposition)
{
    if (content_disposition) {
        char *lc = g_ascii_strdown(content_disposition, -1);
        gboolean attach = strstr(lc, "attachment") != NULL;
        g_free(lc);
        if (attach) return TRUE;
    }
    if (!content_type) return FALSE;
    if (g_ascii_strncasecmp(content_type, "application/octet-stream", 24) == 0) return TRUE;
    if (g_ascii_strncasecmp(content_type, "application/pdf", 15) == 0 &&
        !ns_pdf_available()) return TRUE;
    if (g_ascii_strncasecmp(content_type, "application/zip", 15) == 0) return TRUE;
    if (g_ascii_strncasecmp(content_type, "application/x-tar", 17) == 0) return TRUE;
    if (g_ascii_strncasecmp(content_type, "application/gzip", 16) == 0) return TRUE;
    if (g_ascii_strncasecmp(content_type, "application/x-gzip", 18) == 0) return TRUE;
    if (g_ascii_strncasecmp(content_type, "application/x-bzip2", 19) == 0) return TRUE;
    if (g_ascii_strncasecmp(content_type, "application/x-xz", 16) == 0) return TRUE;
    if (g_ascii_strncasecmp(content_type, "application/x-7z-compressed", 27) == 0) return TRUE;
    if (g_ascii_strncasecmp(content_type, "application/x-rar-compressed", 28) == 0) return TRUE;
    if (g_ascii_strncasecmp(content_type, "application/x-debian-package", 28) == 0) return TRUE;
    if (g_ascii_strncasecmp(content_type, "application/vnd.android.package-archive", 39) == 0) return TRUE;
    if (g_ascii_strncasecmp(content_type, "application/x-msdownload", 24) == 0) return TRUE;
    return FALSE;
}

typedef struct ns_download_pending {
    guint      w_id;
    GBytes    *bytes;
    char      *url;
} ns_download_pending;

static void
ns_download_pending_free(ns_download_pending *p)
{
    if (!p) return;
    if (p->bytes) g_bytes_unref(p->bytes);
    g_free(p->url);
    g_free(p);
}

static void
ns_download_save_done(GObject *src, GAsyncResult *res, gpointer user_data)
{
    ns_download_pending *p = user_data;
    GError *err = NULL;
    GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, &err);
    ns_window *w = ns_window_for_id(p->w_id);
    if (!file) {
        if (w) {
            if (err && !g_error_matches(err, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED))
                ns_window_set_status(w, "Download cancelled: %s", err->message);
            else
                ns_window_set_status(w, "Download cancelled");
        }
        g_clear_error(&err);
        ns_download_pending_free(p);
        return;
    }
    g_clear_error(&err);
    char *path = g_file_get_path(file);
    gsize sz = 0;
    gconstpointer data = g_bytes_get_data(p->bytes, &sz);
    GError *werr = NULL;
    gboolean ok = g_file_replace_contents(file, data, sz, NULL, FALSE,
                                          G_FILE_CREATE_REPLACE_DESTINATION,
                                          NULL, NULL, &werr);
    if (w) {
        if (ok) ns_window_set_status(w, "Saved %s (%" G_GSIZE_FORMAT " bytes)",
                                     path ? path : "(file)", sz);
        else    ns_window_set_status(w, "Save failed: %s",
                                     werr ? werr->message : "unknown");
    }
    g_clear_error(&werr);
    g_free(path);
    g_object_unref(file);
    ns_download_pending_free(p);
}

static void
ns_window_record_final_url(ns_window *w, const ns_response *resp)
{
    if (!w || !resp || !resp->final_url) return;
    if (!ns_url_is_http_or_https(resp->final_url))
        return;
    if (w->url_entry) {
        char *disp = ns_url_to_display(resp->final_url);
        const char *show = disp ? disp : resp->final_url;
        const char *cur = gtk_editable_get_text(GTK_EDITABLE(w->url_entry));
        if (!cur || strcmp(cur, show) != 0)
            gtk_editable_set_text(GTK_EDITABLE(w->url_entry), show);
        g_free(disp);
    }
    if (w->history && w->cursor >= 0 && w->cursor < (int)w->history->len) {
        char *cur = g_ptr_array_index(w->history, w->cursor);
        if (!cur || strcmp(cur, resp->final_url) != 0) {
            g_free(cur);
            w->history->pdata[w->cursor] = g_strdup(resp->final_url);
        }
    }
    ns_history_record(resp->final_url, NULL);
}

static void
ns_window_offer_download(ns_window *w, const ns_response *resp)
{
    if (!resp || !resp->body || resp->body->len == 0) return;
    ns_download_pending *p = g_new0(ns_download_pending, 1);
    p->w_id = w->id;
    p->bytes = g_bytes_new(resp->body->data, resp->body->len);
    p->url = g_strdup(resp->final_url ? resp->final_url : "");

    char *suggested = ns_download_suggest_filename(resp->final_url,
                                                   resp->content_disposition);
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Save file");
    if (suggested) gtk_file_dialog_set_initial_name(dialog, suggested);
    g_free(suggested);
    gtk_file_dialog_save(dialog, GTK_WINDOW(w->window), NULL,
                         ns_download_save_done, p);
    g_object_unref(dialog);
    ns_window_set_status(w, "Downloading %s ...",
                         resp->final_url ? resp->final_url : "");
}

static ns_tab_load_result *
ns_prepare_load_sync(ns_response *resp, gboolean parse_html)
{
    ns_tab_load_result *prepared = g_new0(ns_tab_load_result, 1);
    prepared->resp = resp;
    if (resp && resp->body && resp->body->len > 0) {
        prepared->body = ns_html_decode_body((const char *)resp->body->data,
                                             resp->body->len);
        if (!prepared->body) prepared->body = g_strdup("");
        prepared->body_len = strlen(prepared->body);
        if (parse_html) {
            prepared->doc = ns_html_parse(prepared->body,
                                          (gssize)prepared->body_len);
            prepared->parsed = prepared->doc != NULL;
        }
    }
    return prepared;
}

static void
ns_on_load_prepared(ns_tab_load_result *prepared, gpointer user_data)
{
    ns_fetch_ctx *ctx = user_data;
    guint wid = ctx ? ctx->wid : 0;
    guint gen = ctx ? ctx->gen : 0;
    g_free(ctx);
    ns_window *w = ns_window_for_id(wid);
    if (!w || gen != w->fetch_gen || !prepared || !prepared->resp) {
        ns_tab_load_result_free(prepared);
        return;
    }

    ns_response *resp = prepared->resp;
    if (prepared->body) {
        w->last_body = prepared->body;
        prepared->body = NULL;
        w->last_body_len = prepared->body_len;
        prepared->body_len = 0;
        w->dom_mutated = FALSE;
    }
    if (prepared->doc) {
        w->parsed_doc = prepared->doc;
        prepared->doc = NULL;
    }
    w->last_content_type = g_strdup(resp->content_type ? resp->content_type : "");
    if (w->csp) { ns_csp_free(w->csp); w->csp = NULL; }
    if (resp->csp_header && *resp->csp_header)
        w->csp = ns_csp_parse(resp->csp_header);

    gboolean is_html = is_html_content_type(w->last_content_type);
    if (is_html && w->last_body)
        ns_window_preload_stylesheets(w, w->last_body, w->last_body_len);

    if (is_html)
        w->mode = NS_VIEW_RENDER;
    else
        w->mode = NS_VIEW_RAW;

    ns_css_set_target_fragment(
        w->pending_fragment && *w->pending_fragment
            ? w->pending_fragment : NULL);
    ns_window_reveal_pending_fragment(w, FALSE);
    ns_window_set_stage(w, NS_STAGE_RENDERING);
    ns_window_render(w);
    if (is_html) {
        ns_window_ensure_layout(w, ns_layout_viewport());
        ns_window_apply_page_title(w);
        ns_window_kick_favicon(w);
    } else {
        ns_window_set_title_if_active(w, NS_TITLE);
        ns_window_update_tab_label(w);
    }
    if (w->pending_fragment && w->render_vadj) {
        ns_window_scroll_to_fragment(w);
    } else if (w->render_vadj) {
        gtk_adjustment_set_value(w->render_vadj, 0);
    }

    if (w->parsed_doc) {
        ns_window_apply_meta_refresh(w, resp);
        if (w->js) {
            const char *prev_url = ns_js_current_url(w->js);
            const char *new_url  = ns_window_current_url(w);
            if (prev_url && *prev_url && new_url && *new_url) {
                g_autofree char *prev_origin = ns_url_origin_from(prev_url);
                g_autofree char *new_origin  = ns_url_origin_from(new_url);
                if (prev_origin && new_origin &&
                    strcmp(prev_origin, new_origin) != 0) {
                    ns_js_free(w->js);
                    w->js = NULL;
                }
            }
        }
        ns_window_ensure_js(w);
        if (w->js) {
            ns_js_set_csp(w->js, w->csp);
            ns_js_set_image_cache(w->js, w->images);
            if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
            for (int i = 0; i < 64 && g_main_context_iteration(NULL, FALSE); i++) { }
            if (!ns_window_for_id(wid)) {
                ns_tab_load_result_free(prepared);
                return;
            }
            ns_window_set_stage(w, NS_STAGE_SCRIPTING);
            ns_js_run_scripts_in_doc(w->js, w->parsed_doc,
                                     ns_window_current_url(w));
            if (!ns_window_for_id(wid)) {
                ns_tab_load_result_free(prepared);
                return;
            }
            if (ns_js_consume_mutated(w->js))
                ns_window_js_mutated(w);
        }
    }

    ns_window_set_status(w, "%ld  %s  (%s, %" G_GSIZE_FORMAT " bytes)",
                         resp->status,
                         resp->final_url ? resp->final_url : "",
                         resp->content_type ? resp->content_type : "?",
                         (gsize)w->last_body_len);
    ns_tab_load_result_free(prepared);
    ns_window_set_busy(w, FALSE);
    ns_window_set_stage(w, NS_STAGE_DONE);
}

static void
ns_on_fetch_done(GObject *src, GAsyncResult *result, gpointer user_data)
{
    (void)src;
    ns_fetch_ctx *ctx = user_data;
    guint wid = ctx ? ctx->wid : 0;
    guint gen = ctx ? ctx->gen : 0;
    ns_window *w = ns_window_for_id(wid);
    GError *err = NULL;
    ns_response *resp = ns_net_fetch_finish(result, &err);

    if (!w) {
        ns_response_free(resp);
        g_clear_error(&err);
        g_free(ctx);
        return;
    }

    if (gen != w->fetch_gen) {
        ns_response_free(resp);
        g_clear_error(&err);
        g_free(ctx);
        return;
    }

    g_clear_object(&w->current_fetch);

    if (!resp) {
        if (err && g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            ns_window_set_status(w, "Cancelled");
            if (err) g_clear_error(&err);
            ns_window_set_busy(w, FALSE);
            g_free(ctx);
            return;
        }
        const char *emsg = err ? err->message : "unknown error";
        ns_window_set_status(w, "Error: %s", emsg);
        char *line = g_strdup_printf("[error] page fetch failed: %s", emsg);
        ns_window_console_append(w, line);
        g_free(line);
        const char *fetch_url = w->history && w->cursor >= 0 &&
            w->cursor < (int)w->history->len
            ? g_ptr_array_index(w->history, w->cursor) : NULL;
        char *html = ns_build_error_page(fetch_url, 0, emsg);
        ns_window_clear_cache(w);
        w->last_body = html;
        w->last_body_len = strlen(html);
        w->last_content_type = g_strdup("text/html; charset=utf-8");
        w->dom_mutated = FALSE;
        w->mode = NS_VIEW_RENDER;
        ns_window_render(w);
        ns_window_ensure_layout(w, ns_layout_viewport());
        ns_window_set_title_if_active(w, "Error — " NS_TITLE);
        if (err) g_clear_error(&err);
        ns_window_set_busy(w, FALSE);
        g_free(ctx);
        return;
    }

    ns_window_record_final_url(w, resp);
    ns_debug_log_emit(NS_DLOG_NET, "fetch",
                      "status=%ld %s len=%zu type=%s",
                      resp->status, resp->final_url ? resp->final_url : "(no-url)",
                      (size_t)(resp->body ? resp->body->len : 0),
                      resp->content_type ? resp->content_type : "?");

    if (resp->error) {
        char *line = g_strdup_printf("[error] page transport error: %s",
                                     resp->error);
        ns_window_console_append(w, line);
        g_free(line);
        ns_window_set_status(w, "Transport error: %s", resp->error);
        ns_window_clear_cache(w);
        char *html = ns_build_error_page(
            resp->final_url ? resp->final_url : "",
            resp->status, resp->error);
        w->last_body = html;
        w->last_body_len = strlen(html);
        w->last_content_type = g_strdup("text/html; charset=utf-8");
        w->dom_mutated = FALSE;
        w->mode = NS_VIEW_RENDER;
        ns_window_render(w);
        ns_window_ensure_layout(w, ns_layout_viewport());
        ns_window_set_title_if_active(w, "Error — " NS_TITLE);
        ns_response_free(resp);
        ns_window_set_busy(w, FALSE);
        g_free(ctx);
        return;
    }

    if (resp->status < 400 &&
        ns_should_download(resp->content_type, resp->content_disposition)) {
        ns_window_offer_download(w, resp);
        if (w->history && w->cursor >= 0 && (int)w->history->len > w->cursor + 1) {
            g_free(g_ptr_array_index(w->history, w->history->len - 1));
            g_ptr_array_set_size(w->history, w->history->len - 1);
        } else if (w->history && (int)w->history->len > 0 &&
                   w->cursor == (int)w->history->len - 1) {
            const char *cur = g_ptr_array_index(w->history, w->cursor);
            if (cur && resp->final_url && strcmp(cur, resp->final_url) == 0) {
                g_free(g_ptr_array_index(w->history, w->cursor));
                g_ptr_array_set_size(w->history, w->history->len - 1);
                w->cursor--;
                if (w->cursor >= 0 && w->cursor < (int)w->history->len) {
                    const char *prev = g_ptr_array_index(w->history, w->cursor);
                    if (prev && w->url_entry) {
                        char *disp = ns_url_to_display(prev);
                        gtk_editable_set_text(GTK_EDITABLE(w->url_entry),
                                              disp ? disp : prev);
                        g_free(disp);
                    }
                }
                ns_window_update_nav_state(w);
            }
        }
        ns_response_free(resp);
        ns_window_set_busy(w, FALSE);
        g_free(ctx);
        return;
    }

    if (resp->tls_warning) {
        ns_window_set_status(w, "%s", resp->tls_warning);
        char *line = g_strdup_printf("[warn] TLS: %s", resp->tls_warning);
        ns_window_console_append(w, line);
        g_free(line);
    } else if (resp->status >= 400) {
        ns_window_set_status(w, "%ld %s", resp->status,
                             resp->final_url ? resp->final_url : "");
        char *line = g_strdup_printf("[error] HTTP %ld: %s",
                                     resp->status,
                                     resp->final_url ? resp->final_url : "");
        ns_window_console_append(w, line);
        g_free(line);

        gboolean body_is_html =
            resp->content_type &&
            (g_ascii_strncasecmp(resp->content_type, "text/html", 9) == 0 ||
             g_ascii_strncasecmp(resp->content_type, "application/xhtml", 17) == 0);
        gboolean body_useful = resp->body && resp->body->len > 64 && body_is_html;
        if (!body_useful) {
            char *html = ns_build_error_page(
                resp->final_url ? resp->final_url : "",
                resp->status, NULL);
            ns_window_clear_cache(w);
            w->last_body = html;
            w->last_body_len = strlen(html);
            w->last_content_type = g_strdup("text/html; charset=utf-8");
            w->dom_mutated = FALSE;
            w->mode = NS_VIEW_RENDER;
            ns_window_render(w);
            ns_window_ensure_layout(w, ns_layout_viewport());
            ns_window_set_title_if_active(w, "Error — " NS_TITLE);
            ns_response_free(resp);
            ns_window_set_busy(w, FALSE);
            g_free(ctx);
            return;
        }
    }

    ns_window_clear_cache(w);

    if (ns_pdf_available() && resp->content_type &&
        g_ascii_strncasecmp(resp->content_type, "application/pdf", 15) == 0 &&
        resp->body && resp->body->len > 0) {
        w->pdf = ns_pdf_new_from_bytes(resp->body->data, resp->body->len);
        if (w->pdf) {
            w->last_content_type = g_strdup("application/pdf");
            w->mode = NS_VIEW_RENDER;
            ns_window_render(w);
            char *title = g_path_get_basename(resp->final_url
                                              ? resp->final_url : "document.pdf");
            char *q = strchr(title, '?');
            if (q) *q = '\0';
            char *full_title = g_strdup_printf("%s — %s",
                title && *title ? title : "PDF", NS_TITLE);
            ns_window_set_title_if_active(w, full_title);
            g_free(full_title);
            g_free(title);
            ns_window_update_tab_label(w);
            if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
            ns_window_set_status(w, "%ld  %s  (PDF, %" G_GSIZE_FORMAT " bytes, %d pages)",
                                 resp->status,
                                 resp->final_url ? resp->final_url : "",
                                 (gsize)resp->body->len,
                                 ns_pdf_n_pages(w->pdf));
            ns_response_free(resp);
            ns_window_set_busy(w, FALSE);
            g_free(ctx);
            return;
        }
    }

    if (resp->content_type &&
        g_ascii_strncasecmp(resp->content_type, "image/", 6) == 0 &&
        resp->body && resp->body->len > 0) {
        char *html = ns_html_image_document(resp->final_url);
        g_byte_array_set_size(resp->body, 0);
        g_byte_array_append(resp->body, (const guint8 *)html, strlen(html));
        g_free(html);
        g_free(resp->content_type);
        resp->content_type = g_strdup("text/html; charset=utf-8");
    }

    ns_window_set_stage(w, NS_STAGE_PARSING);
    gboolean parse_html = is_html_content_type(resp->content_type);
    if (w->worker &&
        ns_tab_worker_load_response(w->worker, resp, parse_html,
                                    ns_on_load_prepared, ctx, g_free))
        return;
    ns_tab_load_result *prepared = ns_prepare_load_sync(resp, parse_html);
    ns_on_load_prepared(prepared, ctx);
}

typedef struct {
    guint          wid;
    char          *url;
    ns_load_source src;
} ns_deferred_nav;

static gboolean
ns_window_load_url_deferred(gpointer data)
{
    ns_deferred_nav *d = data;
    ns_window *w = ns_window_for_id(d->wid);
    if (w) ns_window_load_url(w, d->url, d->src);
    g_free(d->url);
    g_free(d);
    return G_SOURCE_REMOVE;
}

void
ns_window_load_url(ns_window *w, const char *raw_url, ns_load_source src)
{
    if (w->js && ns_js_in_pump(w->js)) {
        ns_deferred_nav *d = g_new0(ns_deferred_nav, 1);
        d->wid = w->id;
        d->url = g_strdup(raw_url);
        d->src = src;
        g_idle_add(ns_window_load_url_deferred, d);
        return;
    }
    g_clear_pointer(&w->lazy_url, g_free);
    char *url = ns_normalize_url(raw_url);
    if (!url) {
        ns_window_set_status(w, "Empty URL");
        return;
    }
    char *upgraded = ns_net_hsts_upgrade(url);
    if (upgraded) { g_free(url); url = upgraded; }

    char *mobile = ns_mobile_rewrite_url(url);
    if (mobile) { g_free(url); url = mobile; }

    g_free(w->pending_fragment);
    w->pending_fragment = NULL;
    char *hash = strchr(url, '#');
    if (hash) {
        w->pending_fragment = g_strdup(hash + 1);
        *hash = '\0';
        const char *cur = ns_window_current_url(w);
        if (cur && strcmp(cur, url) == 0) {
            char *old_url = g_strdup(cur);
            char *new_url = g_strconcat(url, "#",
                                        w->pending_fragment ? w->pending_fragment : "",
                                        NULL);
            g_free(url);
            ns_css_set_target_fragment(
                w->pending_fragment && *w->pending_fragment
                    ? w->pending_fragment : NULL);
            if (src != NS_LOAD_HISTORY)
                ns_window_js_soft_nav(new_url, FALSE, w);
            ns_window_reveal_pending_fragment(w, src != NS_LOAD_HISTORY);
            w->layout_dirty = TRUE;
            ns_window_ensure_layout(w, ns_layout_viewport());
            ns_window_scroll_to_fragment(w);
            if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
            if (w->js && strcmp(old_url, new_url) != 0)
                ns_js_dispatch_hashchange(w->js, old_url, new_url);
            g_free(old_url);
            g_free(new_url);
            return;
        }
    }

    if (w->current_fetch) {
        g_cancellable_cancel(w->current_fetch);
        g_clear_object(&w->current_fetch);
    }

    if (src == NS_LOAD_USER) {

        while ((int)w->history->len > w->cursor + 1) {
            g_free(g_ptr_array_index(w->history, w->history->len - 1));
            g_ptr_array_set_size(w->history, w->history->len - 1);
        }

        gboolean is_dup = FALSE;
        if (w->cursor >= 0 && w->cursor < (int)w->history->len) {
            const char *cur = g_ptr_array_index(w->history, w->cursor);
            if (cur && strcmp(cur, url) == 0) is_dup = TRUE;
        }
        if (!is_dup)
            ns_window_history_append(w, url);
    }

    char *disp = ns_url_to_display(url);
    gtk_editable_set_text(GTK_EDITABLE(w->url_entry), disp ? disp : url);
    g_free(disp);

    w->current_fetch = g_cancellable_new();
    ns_window_set_busy(w, TRUE);
    ns_window_update_nav_state(w);
    ns_window_set_status(w, "Loading %s …", url);
    ns_debug_log_emit(NS_DLOG_NET, "navigate", "%s", url);
    ns_net_fetch_async(url, NULL, w->current_fetch, ns_on_fetch_done,
                       ns_fetch_ctx_new(w));
    g_free(url);
}

static void
ns_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
}

static void
ns_hourglass_glass_path(cairo_t *cr, double s)
{
    double l = s * 0.22, r = s * 0.78, t = s * 0.19, b = s * 0.81;
    double cx = s * 0.5, mid = s * 0.5, neck = s * 0.045;
    cairo_new_sub_path(cr);
    cairo_move_to(cr, l, t);
    cairo_curve_to(cr, l, mid - s * 0.11, cx - neck, mid - s * 0.07,
                   cx - neck, mid);
    cairo_curve_to(cr, cx - neck, mid + s * 0.07, l, mid + s * 0.11, l, b);
    cairo_line_to(cr, r, b);
    cairo_curve_to(cr, r, mid + s * 0.11, cx + neck, mid + s * 0.07,
                   cx + neck, mid);
    cairo_curve_to(cr, cx + neck, mid - s * 0.07, r, mid - s * 0.11, r, t);
    cairo_close_path(cr);
}

static void
ns_hourglass_cap(cairo_t *cr, double s, double y)
{
    cairo_pattern_t *wood = cairo_pattern_create_linear(0, y, 0, y + s * 0.10);
    cairo_pattern_add_color_stop_rgb(wood, 0.0, 0.58, 0.37, 0.20);
    cairo_pattern_add_color_stop_rgb(wood, 0.5, 0.45, 0.27, 0.13);
    cairo_pattern_add_color_stop_rgb(wood, 1.0, 0.32, 0.18, 0.08);
    ns_rounded_rect(cr, s * 0.14, y, s * 0.72, s * 0.10, s * 0.045);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9);
    cairo_set_line_width(cr, s * 0.06);
    cairo_stroke_preserve(cr);
    cairo_set_source(cr, wood);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.10, 0.06, 0.03, 0.55);
    cairo_set_line_width(cr, s * 0.02);
    cairo_stroke(cr);
    cairo_pattern_destroy(wood);
}

static void
ns_draw_hourglass(cairo_t *cr, double s)
{
    double cx = s * 0.5, mid = s * 0.5, t = s * 0.19, b = s * 0.81;

    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    ns_hourglass_glass_path(cr, s);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9);
    cairo_set_line_width(cr, s * 0.13);
    cairo_stroke_preserve(cr);
    cairo_set_source_rgba(cr, 0.88, 0.94, 1.0, 0.42);
    cairo_fill(cr);

    cairo_save(cr);
    ns_hourglass_glass_path(cr, s);
    cairo_clip(cr);

    cairo_pattern_t *sand = cairo_pattern_create_linear(0, t, 0, b);
    cairo_pattern_add_color_stop_rgb(sand, 0.0, 0.98, 0.80, 0.38);
    cairo_pattern_add_color_stop_rgb(sand, 1.0, 0.85, 0.56, 0.14);
    cairo_set_source(cr, sand);

    cairo_move_to(cr, 0, s * 0.31);
    cairo_curve_to(cr, s * 0.35, s * 0.37, s * 0.65, s * 0.37, s, s * 0.31);
    cairo_line_to(cr, s, mid);
    cairo_line_to(cr, 0, mid);
    cairo_close_path(cr);
    cairo_fill(cr);

    cairo_move_to(cr, 0, b);
    cairo_line_to(cr, 0, s * 0.75);
    cairo_curve_to(cr, s * 0.30, s * 0.73, s * 0.42, s * 0.63, cx, s * 0.61);
    cairo_curve_to(cr, s * 0.58, s * 0.63, s * 0.70, s * 0.73, s, s * 0.75);
    cairo_line_to(cr, s, b);
    cairo_close_path(cr);
    cairo_fill(cr);

    cairo_set_line_width(cr, s * 0.035);
    cairo_move_to(cr, cx, mid);
    cairo_line_to(cr, cx, s * 0.78);
    cairo_stroke(cr);
    cairo_pattern_destroy(sand);
    cairo_restore(cr);

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.55);
    cairo_set_line_width(cr, s * 0.035);
    cairo_move_to(cr, s * 0.28, s * 0.25);
    cairo_curve_to(cr, s * 0.28, s * 0.34, s * 0.38, s * 0.40, s * 0.44,
                   s * 0.45);
    cairo_stroke(cr);

    ns_hourglass_glass_path(cr, s);
    cairo_set_source_rgb(cr, 0.16, 0.18, 0.22);
    cairo_set_line_width(cr, s * 0.04);
    cairo_stroke(cr);

    ns_hourglass_cap(cr, s, s * 0.09);
    ns_hourglass_cap(cr, s, s * 0.81);
}

static GdkCursor *
ns_busy_cursor(void)
{
    static GdkCursor *cursor = NULL;
    if (cursor) return cursor;

    const int size = 32;
    cairo_surface_t *surf =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t *cr = cairo_create(surf);
    ns_draw_hourglass(cr, size);
    cairo_destroy(cr);
    cairo_surface_flush(surf);

    int stride = cairo_image_surface_get_stride(surf);
    GBytes *bytes = g_bytes_new(cairo_image_surface_get_data(surf),
                                (gsize)stride * size);
    GdkTexture *tex = gdk_memory_texture_new(
        size, size, GDK_MEMORY_B8G8R8A8_PREMULTIPLIED, bytes, stride);
    g_bytes_unref(bytes);
    cairo_surface_destroy(surf);

    GdkCursor *fallback = gdk_cursor_new_from_name("wait", NULL);
    cursor = gdk_cursor_new_from_texture(tex, size / 2, size / 2, fallback);
    g_clear_object(&fallback);
    g_object_unref(tex);
    return cursor;
}

static void
ns_window_set_busy(ns_window *w, gboolean busy)
{
    if (!w) return;
    w->busy = busy;
    if (w->go_button && GTK_IS_WIDGET(w->go_button))
        gtk_widget_set_sensitive(w->go_button, !busy);
    if (w->home_button && GTK_IS_WIDGET(w->home_button))
        gtk_widget_set_sensitive(w->home_button, !busy);
    gboolean can_stop = busy || (w->js && !ns_js_is_halted(w->js));
    if (w->stop_button && GTK_IS_WIDGET(w->stop_button)) {
        gtk_widget_set_sensitive(w->stop_button, can_stop);
        gtk_widget_set_tooltip_text(w->stop_button,
            busy ? "Stop loading" : "Stop scripts");
    }
    ns_window_set_stage(w, busy ? NS_STAGE_FETCHING : NS_STAGE_IDLE);
    ns_window_update_logo_loading(w, busy);
    GdkCursor *busy_cursor = busy ? ns_busy_cursor() : NULL;
    if (w->window && GTK_IS_WIDGET(w->window))
        gtk_widget_set_cursor(w->window, busy_cursor);
    if (w->drawing_area && GTK_IS_WIDGET(w->drawing_area))
        gtk_widget_set_cursor(w->drawing_area, busy_cursor);
    if (busy) {
        gtk_widget_set_sensitive(w->back_button, FALSE);
        gtk_widget_set_sensitive(w->forward_button, FALSE);
    } else {
        ns_window_update_nav_state(w);
    }
}

void
ns_window_update_nav_state(ns_window *w)
{
    if (!w || !w->window || !GTK_IS_WINDOW(w->window)) return;
    gboolean can_back    = w->cursor > 0;
    gboolean can_forward = w->cursor >= 0 && w->cursor + 1 < (int)w->history->len;
    if (w->back_button && GTK_IS_WIDGET(w->back_button))
        gtk_widget_set_sensitive(w->back_button, can_back);
    if (w->forward_button && GTK_IS_WIDGET(w->forward_button))
        gtk_widget_set_sensitive(w->forward_button, can_forward);
    GAction *ab = g_action_map_lookup_action(G_ACTION_MAP(w->window), "back");
    GAction *af = g_action_map_lookup_action(G_ACTION_MAP(w->window), "forward");
    if (G_IS_SIMPLE_ACTION(ab))
        g_simple_action_set_enabled(G_SIMPLE_ACTION(ab), can_back);
    if (G_IS_SIMPLE_ACTION(af))
        g_simple_action_set_enabled(G_SIMPLE_ACTION(af), can_forward);
}

#define NS_TYPE_SUGGEST (ns_suggest_get_type())
G_DECLARE_FINAL_TYPE(NsSuggest, ns_suggest, NS, SUGGEST, GObject)
struct _NsSuggest { GObject parent_instance; char *url; char *title; };
G_DEFINE_FINAL_TYPE(NsSuggest, ns_suggest, G_TYPE_OBJECT)

static void
ns_suggest_finalize(GObject *obj)
{
    NsSuggest *s = NS_SUGGEST(obj);
    g_free(s->url);
    g_free(s->title);
    G_OBJECT_CLASS(ns_suggest_parent_class)->finalize(obj);
}

static void
ns_suggest_class_init(NsSuggestClass *klass)
{
    G_OBJECT_CLASS(klass)->finalize = ns_suggest_finalize;
}

static void
ns_suggest_init(NsSuggest *s) { (void)s; }

static NsSuggest *
ns_suggest_new(const char *url, const char *title)
{
    NsSuggest *s = g_object_new(NS_TYPE_SUGGEST, NULL);
    s->url   = g_strdup(url);
    s->title = g_strdup(title ? title : "");
    return s;
}

static const char k_suggest_css_common[] =
    ".nd-suggest listview { background: transparent; }"
    ".nd-suggest listview > row {"
    " border-radius: 7px; margin: 1px 0; }"
    ".nd-suggest listview > row:hover {"
    " background-color: alpha(@theme_fg_color, 0.07); }"
    ".nd-suggest listview > row:selected {"
    " background-color: @theme_selected_bg_color; }"
    ".nd-suggest listview > row:selected .nd-suggest-title,"
    ".nd-suggest listview > row:selected .nd-suggest-url {"
    " color: @theme_selected_fg_color; }"
    ".nd-suggest-row { padding: 7px 12px; }"
    ".nd-suggest-title { font-weight: 600; }"
    ".nd-suggest-url {"
    " font-size: 0.85em; color: alpha(@theme_fg_color, 0.55); }";

static const char k_suggest_css_composited[] =
    ".nd-suggest > contents {"
    " padding: 6px; border-radius: 12px;"
    " background-color: @theme_base_color;"
    " box-shadow: 0 3px 10px rgba(0,0,0,0.18);"
    " border: 1px solid alpha(@theme_fg_color, 0.10); }";

static const char k_suggest_css_flat[] =
    ".nd-suggest, .nd-suggest > contents { box-shadow: none; }"
    ".nd-suggest > contents {"
    " padding: 5px; border-radius: 0; margin: 0;"
    " background-color: @theme_base_color;"
    " border: 1px solid alpha(@theme_fg_color, 0.35); }";

static void
ns_install_suggest_css(void)
{
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    GdkDisplay *display = gdk_display_get_default();
    if (!display) return;
    char *css = g_strconcat(k_suggest_css_common,
        gdk_display_is_composited(display) ? k_suggest_css_composited
                                           : k_suggest_css_flat,
        NULL);
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(display,
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
    g_free(css);
}

static void
ns_window_hide_suggestions(ns_window *w)
{
    if (!w) return;
    g_clear_handle_id(&w->suggest_hide_source, g_source_remove);
    if (w->suggest_popover)
        gtk_popover_popdown(GTK_POPOVER(w->suggest_popover));
}

static gboolean
ns_window_hide_suggestions_cb(gpointer data)
{
    ns_window *w = data;
    if (!w) return G_SOURCE_REMOVE;
    w->suggest_hide_source = 0;
    ns_window_hide_suggestions(w);
    return G_SOURCE_REMOVE;
}

static void
ns_window_hide_suggestions_later(ns_window *w)
{
    if (!w || w->suggest_hide_source) return;
    w->suggest_hide_source =
        g_timeout_add(250, ns_window_hide_suggestions_cb, w);
}

static void
on_url_focus_enter(GtkEventControllerFocus *ctrl, gpointer user_data)
{
    (void)ctrl;
    ns_window *w = user_data;
    g_clear_handle_id(&w->suggest_hide_source, g_source_remove);
    w->url_focused = TRUE;
}

static void
on_url_focus_leave(GtkEventControllerFocus *ctrl, gpointer user_data)
{
    (void)ctrl;
    ns_window *w = user_data;
    w->url_focused = FALSE;
    ns_window_hide_suggestions_later(w);
}

static void
ns_suggest_navigate(ns_window *w, const char *url)
{
    if (!url || !*url) return;
    char *u = g_strdup(url);
    w->suggest_suppress = TRUE;
    ns_window_hide_suggestions(w);
    gtk_editable_set_text(GTK_EDITABLE(w->url_entry), u);
    w->suggest_suppress = FALSE;
    if (w->drawing_area) gtk_widget_grab_focus(w->drawing_area);
    ns_window_load_url(w, u, NS_LOAD_USER);
    g_free(u);
}

static void
on_suggest_row_activated(GtkListView *list, guint position, gpointer user_data)
{
    (void)list;
    ns_window *w = user_data;
    if (!w->suggest_model) return;
    NsSuggest *s = g_list_model_get_item(G_LIST_MODEL(w->suggest_model), position);
    if (s) {
        char *u = g_strdup(s->url);
        g_object_unref(s);
        ns_suggest_navigate(w, u);
        g_free(u);
    }
}

static void
on_suggest_item_pressed(GtkGestureClick *gesture, int n_press,
                        double x, double y, gpointer user_data)
{
    (void)n_press; (void)x; (void)y;
    ns_window *w = user_data;
    GtkWidget *box =
        gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    GtkListItem *item = box ? g_object_get_data(G_OBJECT(box), "nd-item") : NULL;
    GObject *obj = item ? gtk_list_item_get_item(item) : NULL;
    if (!NS_IS_SUGGEST(obj)) return;
    NsSuggest *s = NS_SUGGEST(obj);
    char *u = g_strdup(s->url);
    ns_suggest_navigate(w, u);
    g_free(u);
}

static void
suggest_move_selection(ns_window *w, int delta)
{
    if (!w->suggest_list || !w->suggest_model) return;
    guint n = g_list_model_get_n_items(G_LIST_MODEL(w->suggest_model));
    if (n == 0) return;
    GtkSelectionModel *sm = gtk_list_view_get_model(GTK_LIST_VIEW(w->suggest_list));
    if (!GTK_IS_SINGLE_SELECTION(sm)) return;
    guint cur = gtk_single_selection_get_selected(GTK_SINGLE_SELECTION(sm));
    guint next;
    if (cur == GTK_INVALID_LIST_POSITION) {
        next = (delta > 0) ? 0 : n - 1;
    } else {
        int ni = (int)cur + delta;
        if (ni < 0) ni = 0;
        if (ni >= (int)n) ni = (int)n - 1;
        next = (guint)ni;
    }
    gtk_single_selection_set_selected(GTK_SINGLE_SELECTION(sm), next);
}

static guint
suggest_selected_position(ns_window *w)
{
    if (!w->suggest_list) return GTK_INVALID_LIST_POSITION;
    GtkSelectionModel *sm = gtk_list_view_get_model(GTK_LIST_VIEW(w->suggest_list));
    if (!GTK_IS_SINGLE_SELECTION(sm)) return GTK_INVALID_LIST_POSITION;
    return gtk_single_selection_get_selected(GTK_SINGLE_SELECTION(sm));
}

static void
on_url_entry_changed(GtkEditable *editable, gpointer user_data)
{
    ns_window *w = user_data;
    if (!w->suggest_popover || !w->suggest_model || w->suggest_suppress) return;
    const char *text = w->url_focused
        ? gtk_editable_get_text(editable) : NULL;
    GPtrArray *matches = (text && *text) ? ns_history_suggest(text, 8) : NULL;
    g_list_store_remove_all(w->suggest_model);
    if (!matches || matches->len == 0) {
        ns_window_hide_suggestions(w);
        if (matches) g_ptr_array_free(matches, TRUE);
        return;
    }
    for (guint i = 0; i < matches->len; i++) {
        ns_history_suggestion *m = g_ptr_array_index(matches, i);
        NsSuggest *s = ns_suggest_new(m->url, m->title);
        g_list_store_append(w->suggest_model, s);
        g_object_unref(s);
    }
    guint rows = matches->len;
    g_ptr_array_free(matches, TRUE);
    GtkWidget *scroller = gtk_widget_get_parent(w->suggest_list);
    if (GTK_IS_SCROLLED_WINDOW(scroller))
        gtk_scrolled_window_set_min_content_height(
            GTK_SCROLLED_WINDOW(scroller), (int)rows * 50);
    int entry_w = gtk_widget_get_width(w->url_entry);
    if (entry_w > 120)
        gtk_widget_set_size_request(w->suggest_popover, entry_w, -1);
    gtk_popover_popup(GTK_POPOVER(w->suggest_popover));
}

static void
suggest_item_setup(GtkSignalListItemFactory *factory, GObject *obj, gpointer u)
{
    (void)factory;
    ns_window *w = u;
    GtkListItem *item = GTK_LIST_ITEM(obj);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_add_css_class(box, "nd-suggest-row");
    g_object_set_data(G_OBJECT(box), "nd-item", item);
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_suggest_item_pressed), w);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(click));
    GtkWidget *title = gtk_label_new(NULL);
    gtk_widget_add_css_class(title, "nd-suggest-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    GtkWidget *url = gtk_label_new(NULL);
    gtk_widget_add_css_class(url, "nd-suggest-url");
    gtk_label_set_xalign(GTK_LABEL(url), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(url), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(box), title);
    gtk_box_append(GTK_BOX(box), url);
    g_object_set_data(G_OBJECT(box), "nd-title", title);
    g_object_set_data(G_OBJECT(box), "nd-url", url);
    gtk_list_item_set_child(item, box);
}

static void
suggest_item_bind(GtkSignalListItemFactory *factory, GObject *obj, gpointer u)
{
    (void)factory; (void)u;
    GtkListItem *item = GTK_LIST_ITEM(obj);
    GtkWidget *box = gtk_list_item_get_child(item);
    NsSuggest *s = NS_SUGGEST(gtk_list_item_get_item(item));
    if (!box || !s) return;
    GtkWidget *title = g_object_get_data(G_OBJECT(box), "nd-title");
    GtkWidget *url   = g_object_get_data(G_OBJECT(box), "nd-url");
    char *disp = ns_url_to_display(s->url);
    const char *disp_url = disp ? disp : s->url;
    gboolean have_title = s->title && *s->title;
    gtk_label_set_text(GTK_LABEL(title), have_title ? s->title : disp_url);
    if (have_title) {
        gtk_label_set_text(GTK_LABEL(url), disp_url);
        gtk_widget_set_visible(url, TRUE);
    } else {
        gtk_widget_set_visible(url, FALSE);
    }
    g_free(disp);
}

void
ns_window_setup_url_suggestions(ns_window *w)
{
    ns_install_suggest_css();
    w->suggest_model = g_list_store_new(NS_TYPE_SUGGEST);
    GtkSingleSelection *sel =
        gtk_single_selection_new(G_LIST_MODEL(w->suggest_model));
    gtk_single_selection_set_autoselect(sel, FALSE);
    gtk_single_selection_set_can_unselect(sel, TRUE);

    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(suggest_item_setup), w);
    g_signal_connect(factory, "bind",  G_CALLBACK(suggest_item_bind),  NULL);

    w->suggest_list = gtk_list_view_new(GTK_SELECTION_MODEL(sel), factory);
    gtk_list_view_set_single_click_activate(GTK_LIST_VIEW(w->suggest_list), TRUE);
    g_signal_connect(w->suggest_list, "activate",
                     G_CALLBACK(on_suggest_row_activated), w);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroller), 320);
    gtk_scrolled_window_set_propagate_natural_height(
        GTK_SCROLLED_WINDOW(scroller), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), w->suggest_list);

    w->suggest_popover = gtk_popover_new();
    gtk_widget_add_css_class(w->suggest_popover, "nd-suggest");
    gtk_popover_set_has_arrow(GTK_POPOVER(w->suggest_popover), FALSE);
    gtk_popover_set_autohide(GTK_POPOVER(w->suggest_popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(w->suggest_popover), GTK_POS_BOTTOM);
    gtk_widget_set_halign(w->suggest_popover, GTK_ALIGN_START);
    gtk_popover_set_child(GTK_POPOVER(w->suggest_popover), scroller);
    gtk_widget_set_parent(w->suggest_popover, w->url_entry);

    g_signal_connect(w->url_entry, "changed",
                     G_CALLBACK(on_url_entry_changed), w);
    GtkEventController *focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter", G_CALLBACK(on_url_focus_enter), w);
    g_signal_connect(focus, "leave", G_CALLBACK(on_url_focus_leave), w);
    gtk_widget_add_controller(w->url_entry, focus);
}

void
on_go_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    ns_window *w = user_data;
    ns_window_hide_suggestions(w);
    const char *text = gtk_editable_get_text(GTK_EDITABLE(w->url_entry));
    ns_window_load_url(w, text, NS_LOAD_USER);
}

void
on_stop_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    ns_window *w = user_data;
    g_action_group_activate_action(G_ACTION_GROUP(w->window), "stop", NULL);
}

void
on_entry_activate(GtkEntry *entry, gpointer user_data)
{
    (void)entry;
    ns_window *w = user_data;
    if (w->suggest_popover && gtk_widget_get_visible(w->suggest_popover) &&
        w->suggest_model) {
        guint pos = suggest_selected_position(w);
        if (pos != GTK_INVALID_LIST_POSITION) {
            NsSuggest *s =
                g_list_model_get_item(G_LIST_MODEL(w->suggest_model), pos);
            if (s) {
                char *u = g_strdup(s->url);
                g_object_unref(s);
                ns_suggest_navigate(w, u);
                g_free(u);
                return;
            }
        }
    }
    ns_window_hide_suggestions(w);
    const char *text = gtk_editable_get_text(GTK_EDITABLE(w->url_entry));
    if (w->drawing_area) gtk_widget_grab_focus(w->drawing_area);
    ns_window_load_url(w, text, NS_LOAD_USER);
}

void
on_back_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    ns_window *w = user_data;
    if (w->cursor <= 0) return;
    w->cursor--;
    const char *url = g_ptr_array_index(w->history, w->cursor);
    ns_window_load_url(w, url, NS_LOAD_HISTORY);
}

void
on_forward_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    ns_window *w = user_data;
    if (w->cursor < 0 || w->cursor + 1 >= (int)w->history->len) return;
    w->cursor++;
    const char *url = g_ptr_array_index(w->history, w->cursor);
    ns_window_load_url(w, url, NS_LOAD_HISTORY);
}

void
on_home_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    ns_window *w = user_data;
    ns_window_load_url(w, g_home_url, NS_LOAD_USER);
}

void
on_reload_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    ns_window *w = user_data;
    if (w->cursor < 0 || w->cursor >= (int)w->history->len) return;
    const char *cur = g_ptr_array_index(w->history, w->cursor);
    ns_window_load_url(w, cur, NS_LOAD_HISTORY);
}

const char *
ns_window_current_url(ns_window *w)
{
    if (w->cursor < 0 || w->cursor >= (int)w->history->len) return NULL;
    return g_ptr_array_index(w->history, w->cursor);
}

char *
ns_window_current_title(ns_window *w)
{
    if (!w->parsed_doc) return NULL;
    ns_node *title = ns_node_find_first_element(w->parsed_doc, "title");
    if (!title) return NULL;
    return ns_node_collect_text(title);
}

static gboolean
is_button_like(const ns_node *n)
{
    if (!n || n->kind != NS_NODE_ELEMENT || !n->name) return FALSE;
    if (strcmp(n->name, "button") == 0) return TRUE;
    if (strcmp(n->name, "input") != 0) return FALSE;
    const char *type = ns_element_get_attr(n, "type");
    if (!type) return FALSE;
    return g_ascii_strcasecmp(type, "submit") == 0 ||
           g_ascii_strcasecmp(type, "button") == 0 ||
           g_ascii_strcasecmp(type, "reset")  == 0 ||
           g_ascii_strcasecmp(type, "checkbox") == 0 ||
           g_ascii_strcasecmp(type, "radio") == 0;
}

static gboolean ns_node_reachable_from(const ns_node *root, const ns_node *target);

typedef struct ns_select_pick_ctx {
    ns_window *w;
    ns_node *select_node;
    ns_node *option;
    GtkWidget *popover;
} ns_select_pick_ctx;

static void
ns_select_pick(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    ns_select_pick_ctx *ctx = user_data;
    if (!ctx || !ctx->select_node || !ctx->option) return;
    if (!ns_node_reachable_from(ctx->w->parsed_doc, ctx->select_node) ||
        !ns_node_reachable_from(ctx->w->parsed_doc, ctx->option)) {
        if (ctx->popover) gtk_popover_popdown(GTK_POPOVER(ctx->popover));
        return;
    }
    GQueue queue = G_QUEUE_INIT;
    g_queue_push_tail(&queue, ctx->select_node);
    while (!g_queue_is_empty(&queue)) {
        ns_node *n = g_queue_pop_head(&queue);
        if (ns_node_is_element_named(n, "option"))
            ns_element_remove_attr(n, "selected");
        for (ns_node *c = n->first_child; c; c = c->next_sibling)
            g_queue_push_tail(&queue, c);
    }
    ns_element_set_attr(ctx->option, "selected", "");
    ns_window_js_mutated(ctx->w);
    if (ctx->w->js) {
        ns_js_dispatch_event(ctx->w->js, ctx->select_node, "input",  NULL);
        ns_js_dispatch_event(ctx->w->js, ctx->select_node, "change", NULL);
    }
    if (ctx->popover) gtk_popover_popdown(GTK_POPOVER(ctx->popover));
}

static void
ns_select_pick_free(gpointer data, GClosure *closure)
{
    (void)closure;
    g_free(data);
}

static void
ns_window_open_select_popover(ns_window *w, ns_node *select_node, double x, double y)
{
    GtkWidget *popover = gtk_popover_new();
    gtk_widget_set_parent(popover, w->drawing_area);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    GdkRectangle rect = { (int)x, (int)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_set_size_request(scrolled, 240, 320);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), list);
    gtk_popover_set_child(GTK_POPOVER(popover), scrolled);

    GQueue queue = G_QUEUE_INIT;
    g_queue_push_tail(&queue, select_node);
    while (!g_queue_is_empty(&queue)) {
        ns_node *n = g_queue_pop_head(&queue);
        if (ns_node_is_element_named(n, "option")) {
            char *label = ns_node_collect_text(n);
            GtkWidget *btn = gtk_button_new_with_label(label ? label : "");
            gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
            gtk_widget_set_halign(btn, GTK_ALIGN_START);
            ns_select_pick_ctx *ctx = g_new0(ns_select_pick_ctx, 1);
            ctx->w = w;
            ctx->select_node = select_node;
            ctx->option = n;
            ctx->popover = popover;
            g_signal_connect_data(btn, "clicked", G_CALLBACK(ns_select_pick),
                                  ctx, ns_select_pick_free, 0);
            gtk_box_append(GTK_BOX(list), btn);
            g_free(label);
            continue;
        }
        for (ns_node *c = n->first_child; c; c = c->next_sibling)
            g_queue_push_tail(&queue, c);
    }
    gtk_popover_popup(GTK_POPOVER(popover));
    g_signal_connect_swapped(popover, "closed",
                             G_CALLBACK(gtk_widget_unparent), popover);
}

typedef struct ns_file_chooser_ctx {
    guint    w_id;
    ns_node *input;
} ns_file_chooser_ctx;

static gboolean
ns_node_reachable_depth(const ns_node *root, const ns_node *target, int depth)
{
    if (!root || depth >= 512) return FALSE;
    if (root == target) return TRUE;
    for (const ns_node *c = root->first_child; c; c = c->next_sibling)
        if (ns_node_reachable_depth(c, target, depth + 1)) return TRUE;
    return FALSE;
}

static gboolean
ns_node_reachable_from(const ns_node *root, const ns_node *target)
{
    if (!root || !target) return FALSE;
    return ns_node_reachable_depth(root, target, 0);
}

static void
ns_on_file_chooser_response(GObject *source, GAsyncResult *result,
                            gpointer user_data)
{
    ns_file_chooser_ctx *ctx = user_data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *err = NULL;
    GFile *file = gtk_file_dialog_open_finish(dialog, result, &err);
    ns_window *w = ctx ? ns_window_for_id(ctx->w_id) : NULL;
    if (w && ctx->input && !ns_node_reachable_from(w->parsed_doc, ctx->input))
        ctx->input = NULL;
    if (file && w && ctx->input) {
        char *path = g_file_get_path(file);
        if (path) {
            ns_element_set_attr(ctx->input, "data-nd-file-path", path);
            const char *base = strrchr(path, '/');
#ifdef G_OS_WIN32
            const char *base_w = strrchr(path, '\\');
            if (!base || (base_w && base_w > base)) base = base_w;
#endif
            ns_element_set_attr(ctx->input, "value", base ? base + 1 : path);
            g_free(path);
            if (w->js) {
                ns_js_dispatch_event(w->js, ctx->input, "input",  NULL);
                ns_js_dispatch_event(w->js, ctx->input, "change", NULL);
            }
            ns_window_js_mutated(w);
        }
    }
    if (err) g_error_free(err);
    if (file) g_object_unref(file);
    g_free(ctx);
}

static void
ns_window_open_file_chooser(ns_window *w, ns_node *input)
{
    if (!w || !input) return;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Choose file to upload");

    const char *accept = ns_element_get_attr(input, "accept");
    if (accept && *accept) {
        GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
        GtkFileFilter *f = gtk_file_filter_new();
        gtk_file_filter_set_name(f, accept);
        char **parts = g_strsplit(accept, ",", -1);
        for (int i = 0; parts[i]; i++) {
            char *p = g_strstrip(parts[i]);
            if (!*p) continue;
            if (*p == '.') {
                char *pat = g_strdup_printf("*%s", p);
                gtk_file_filter_add_pattern(f, pat);
                g_free(pat);
            } else if (strchr(p, '/')) {
                gtk_file_filter_add_mime_type(f, p);
            }
        }
        g_strfreev(parts);
        g_list_store_append(filters, f);
        gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
        g_object_unref(f);
        g_object_unref(filters);
    }

    ns_file_chooser_ctx *ctx = g_new0(ns_file_chooser_ctx, 1);
    ctx->w_id = w->id;
    ctx->input = input;
    GtkWindow *parent = w->window ? GTK_WINDOW(w->window) : NULL;
    gtk_file_dialog_open(dialog, parent, NULL,
                         ns_on_file_chooser_response, ctx);
    g_object_unref(dialog);
}

static const ns_node *
find_form_role_ancestor(const ns_node *n, gboolean *is_text, gboolean *is_button)
{
    *is_text = FALSE;
    *is_button = FALSE;
    for (const ns_node *p = n; p; p = p->parent) {
        if (ns_input_is_text_like(p) ||
            ns_node_is_contenteditable_host(p)) { *is_text = TRUE; return p; }
        if (is_button_like(p))  { *is_button = TRUE; return p; }
    }
    return NULL;
}

static const char *
ns_supported_cursor_name(const char *name)
{
    static const char *known[] = {
        "default", "context-menu", "help", "pointer", "progress", "wait",
        "cell", "crosshair", "text", "vertical-text", "alias", "copy",
        "move", "no-drop", "not-allowed", "grab", "grabbing",
        "e-resize", "n-resize", "ne-resize", "nw-resize", "s-resize",
        "se-resize", "sw-resize", "w-resize", "ew-resize", "ns-resize",
        "nesw-resize", "nwse-resize", "col-resize", "row-resize",
        "all-scroll", "zoom-in", "zoom-out",
    };
    if (name && *name)
        for (guint i = 0; i < G_N_ELEMENTS(known); i++)
            if (strcmp(name, known[i]) == 0) return name;
    return "default";
}

void
on_drawing_motion(GtkEventControllerMotion *ctrl, double x, double y, gpointer user_data)
{
    ns_window *w = user_data;
    w->cursor_x = x;
    w->cursor_y = y;
    if (w->busy) return;
    if (!w->layout_tree) return;
    const char *href = ns_box_hit_link(w->layout_tree, x, y);
    const ns_box *hit = NULL;
    if (!href) {
        hit = ns_box_hit_test(w->layout_tree, x, y);
        if (hit && hit->dom) {
            for (const ns_node *p = hit->dom; p; p = p->parent) {
                if (ns_node_is_element_named(p, "a")) {
                    const char *h = ns_element_get_attr(p, "href");
                    if (h && *h) { href = h; break; }
                }
            }
        }
    }
    const char *cursor_name = "default";
    gboolean media_hover = FALSE;
    if (href) {
        cursor_name = "pointer";
    } else {
        const ns_node *form_target = ns_box_hit_form_dom(w->layout_tree, x, y);
        if (!hit) hit = ns_box_hit_test(w->layout_tree, x, y);
        if (form_target) {
            if (ns_input_is_text_like(form_target))
                cursor_name = "text";
            else
                cursor_name = "pointer";
        } else if (hit && hit->dom &&
                   (ns_node_is_element_named(hit->dom, "video") ||
                    ns_node_is_element_named(hit->dom, "audio"))) {
            cursor_name = "pointer";
            media_hover = TRUE;
        } else if (hit && hit->dom) {
            gboolean t, btn;
            find_form_role_ancestor(hit->dom, &t, &btn);
            if (t)        cursor_name = "text";
            else if (btn) cursor_name = "pointer";
            else {
                for (const ns_box *bp = hit; bp; bp = bp->parent) {
                    if (bp->style && bp->style->values[NS_CSS_CURSOR] &&
                        bp->style->values[NS_CSS_CURSOR]->kind == NS_CSS_V_KEYWORD) {
                        cursor_name = bp->style->values[NS_CSS_CURSOR]->u.keyword;
                        break;
                    }
                }
                if (g_strcmp0(cursor_name, "default") == 0) {
                    for (const ns_node *p = hit->dom; p; p = p->parent) {
                        if (p->kind == NS_NODE_ELEMENT && p->name &&
                            (strcmp(p->name, "summary") == 0 ||
                             strcmp(p->name, "label") == 0 ||
                             strcmp(p->name, "select") == 0 ||
                             strcmp(p->name, "details") == 0)) {
                            cursor_name = "pointer";
                            break;
                        }
                    }
                }
            }
        }
    }
    cursor_name = ns_supported_cursor_name(cursor_name);
    GdkCursor *cur = gdk_cursor_new_from_name(cursor_name, NULL);
    if (!cur && strcmp(cursor_name, "default") != 0)
        cur = gdk_cursor_new_from_name("default", NULL);
    gtk_widget_set_cursor(w->drawing_area, cur);
    if (cur) g_object_unref(cur);
    if (href)
        ns_window_set_status(w, "%s", href);
    else if (media_hover)
        ns_window_set_status(w, "Click to play in external player");
    else
        ns_window_set_status(w, NULL);

    if (w->js && w->layout_tree) {
        GdkModifierType st = gtk_event_controller_get_current_event_state(
            GTK_EVENT_CONTROLLER(ctrl));
        const ns_node *now = ns_window_event_target_at(w, x, y);
        const ns_node *prev = w->hover_node;
        if (now != prev) {
            w->hover_node = now;
            if (prev)
                ns_window_emit_pointer_and_mouse(w, prev, "pointerout", "mouseout",
                                                 x, y, 0, 0, st, now);
            if (w->js && w->layout_tree && prev)
                ns_window_emit_pointer_and_mouse(w, prev, "pointerleave",
                                                 "mouseleave", x, y, 0, 0, st, now);
            if (w->js && w->layout_tree && now)
                ns_window_emit_pointer_and_mouse(w, now, "pointerover", "mouseover",
                                                 x, y, 0, 0, st, prev);
            if (w->js && w->layout_tree && now)
                ns_window_emit_pointer_and_mouse(w, now, "pointerenter",
                                                 "mouseenter", x, y, 0, 0, st, prev);
        }
        if (w->js && w->layout_tree && now)
            ns_window_emit_pointer_and_mouse(w, now, "pointermove", "mousemove",
                                             x, y, 0, 0, st, NULL);
    }
}

void
on_drawing_leave(GtkEventControllerMotion *ctrl, gpointer user_data)
{
    (void)ctrl;
    ns_window *w = user_data;
    ns_window_set_status(w, NULL);
}

static void
on_window_destroy(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    ns_window *w = user_data;
    ns_window_mark_dead(w);
    if (w->raf_tick_id && w->drawing_area) {
        gtk_widget_remove_tick_callback(w->drawing_area, w->raf_tick_id);
        w->raf_tick_id = 0;
    }
    g_clear_handle_id(&w->caret_blink_source, g_source_remove);
    g_clear_handle_id(&w->refresh_source, g_source_remove);
    g_clear_handle_id(&w->scroll_image_source, g_source_remove);
    g_clear_handle_id(&w->suggest_hide_source, g_source_remove);
    g_clear_handle_id(&w->logo_anim_source, g_source_remove);
    g_clear_handle_id(&w->stage_done_source, g_source_remove);
    w->logo_image = NULL;
    if (w->suggest_popover) {
        gtk_widget_unparent(w->suggest_popover);
        w->suggest_popover = NULL;
    }
    if (w->im_context) {
        gtk_im_context_set_client_widget(w->im_context, NULL);
        g_clear_object(&w->im_context);
    }
    if (w->current_fetch) {
        g_cancellable_cancel(w->current_fetch);
        g_clear_object(&w->current_fetch);
    }
    ns_window_clear_cache(w);
    g_free(w->pending_fragment);
    g_free(w->lazy_url);
    g_free(w->search_query);
    g_free(w->context_menu_link);
    g_free(w->context_menu_image);
    g_free(w->context_menu_selection);
    g_free(w->context_menu_media);
    if (w->history) {
        for (guint i = 0; i < w->history->len; i++)
            g_free(g_ptr_array_index(w->history, i));
        g_ptr_array_free(w->history, TRUE);
    }
    if (w->images) ns_image_cache_free(w->images);
    if (w->videos) ns_video_cache_free(w->videos);
    if (w->worker) ns_tab_worker_free(w->worker);
    if (w->anim)   ns_anim_free(w->anim);
    if (w->external_stylesheets) g_ptr_array_free(w->external_stylesheets, TRUE);
    if (w->external_css_seen)    g_hash_table_destroy(w->external_css_seen);
    if (w->external_css_loaded)  g_hash_table_destroy(w->external_css_loaded);
    ns_window_console_close(w);
    g_free(w);
}

static void
on_app_new_window(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    GtkApplication *app = user_data;
    ns_spawn_window(app, NULL);
}

static void
on_app_new_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    GtkApplication *app = user_data;
    GtkWindow *active = gtk_application_get_active_window(app);
    if (!active) {
        ns_window_open(app, NULL);
        return;
    }
    ns_window *nw = ns_browser_add_tab(GTK_WIDGET(active), app, g_home_url);
    if (nw) {
        ns_browser_set_active(GTK_WIDGET(active), nw);
        gtk_widget_grab_focus(nw->url_entry);
    }
}

void
ns_spawn_window(GtkApplication *app, const char *url)
{
    ns_window_open(app, url);
}

static void
ns_media_open_uri_cb(GtkWindow *parent, const char *uri)
{
    if (!parent || !uri) return;
    GtkApplication *app = gtk_window_get_application(parent);
    ns_browser_add_tab(GTK_WIDGET(parent), app, uri);
}

static void
on_tab_button_clicked(GtkButton *b, gpointer user_data)
{
    (void)b;
    ns_window *w = user_data;
    ns_browser_set_active(w->window, w);
    if (w->lazy_url) {
        char *url = g_steal_pointer(&w->lazy_url);
        ns_window_load_url(w, url, NS_LOAD_USER);
        g_free(url);
    }
}

static void
on_tab_close_clicked(GtkButton *b, gpointer user_data)
{
    (void)b;
    ns_window *w = user_data;
    ns_browser_close_tab(w);
}

static void
on_new_tab_clicked(GtkButton *b, gpointer user_data)
{
    (void)b;
    GtkWidget *toplevel = user_data;
    GtkApplication *app = gtk_window_get_application(GTK_WINDOW(toplevel));
    ns_window *nw = ns_browser_add_tab(toplevel, app, g_home_url);
    if (nw) ns_browser_set_active(toplevel, nw);
}

static void
on_toplevel_destroy(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    GPtrArray *tabs = user_data;
    if (!tabs) return;
    for (guint i = 0; i < tabs->len; i++) {
        ns_window *w = g_ptr_array_index(tabs, i);
        on_window_destroy(NULL, w);
    }
    g_ptr_array_free(tabs, TRUE);
}

void
ns_browser_set_active(GtkWidget *toplevel, ns_window *w)
{
    if (!toplevel || !w) return;
    GtkStack *stack = g_object_get_data(G_OBJECT(toplevel), "nd-stack");
    if (stack && w->page_root)
        gtk_stack_set_visible_child(stack, w->page_root);
    g_object_set_data(G_OBJECT(toplevel), "nd-window", w);
    ns_window_install_actions(w);
    ns_install_ctx_actions(w);
    GtkBox *strip = g_object_get_data(G_OBJECT(toplevel), "nd-tab-strip");
    if (strip) {
        GPtrArray *tabs = g_object_get_data(G_OBJECT(toplevel), "nd-tabs");
        if (tabs) {
            for (guint i = 0; i < tabs->len; i++) {
                ns_window *t = g_ptr_array_index(tabs, i);
                if (!t->tab_button) continue;
                if (t == w) gtk_widget_add_css_class(t->tab_button, "suggested-action");
                else        gtk_widget_remove_css_class(t->tab_button, "suggested-action");
            }
        }
    }
    ns_window_apply_page_title(w);
}

static void
ns_window_update_tab_label(ns_window *w)
{
    if (!w || !w->tab_label) return;
    char *title = ns_window_current_title(w);
    const char *show = (title && *title) ? title : ns_window_current_url(w);
    if (!show || !*show) show = "New Tab";
    char *valid = g_utf8_make_valid(show, -1);
    char short_label[256];
    g_utf8_strncpy(short_label, valid, 40);
    gtk_label_set_text(GTK_LABEL(w->tab_label), short_label);
    g_free(valid);
    g_free(title);

    if (w->tab_icon && !w->favicon_loaded) {
        const char *url = ns_window_current_url(w);
        const char *icon = "web-browser-symbolic";
        if (url && g_str_has_prefix(url, "about:"))     icon = "nordstjernen";
        else if (url && g_str_has_prefix(url, "file:")) icon = "folder-symbolic";
        gtk_image_set_from_icon_name(GTK_IMAGE(w->tab_icon), icon);
    }
}

static GHashTable *g_live_windows;
static guint       g_next_window_id;

static void
ns_window_mark_alive(ns_window *w)
{
    if (!g_live_windows) g_live_windows = g_hash_table_new(NULL, NULL);
    if (!w->id) w->id = ++g_next_window_id;
    g_hash_table_insert(g_live_windows, GUINT_TO_POINTER(w->id), w);
}

static void
ns_window_mark_dead(ns_window *w)
{
    if (g_live_windows) g_hash_table_remove(g_live_windows, GUINT_TO_POINTER(w->id));
}

ns_window *
ns_window_for_id(guint id)
{
    if (!g_live_windows || !id) return NULL;
    return g_hash_table_lookup(g_live_windows, GUINT_TO_POINTER(id));
}

static ns_window *
ns_browser_construct_tab(GtkWidget *toplevel)
{
    if (!toplevel) return NULL;
    GtkStack *stack = g_object_get_data(G_OBJECT(toplevel), "nd-stack");
    GtkBox   *strip = g_object_get_data(G_OBJECT(toplevel), "nd-tab-strip");
    GPtrArray *tabs = g_object_get_data(G_OBJECT(toplevel), "nd-tabs");
    if (!stack || !strip || !tabs) return NULL;

    ns_window *w = g_new0(ns_window, 1);
    ns_window_mark_alive(w);
    w->window  = toplevel;
    w->history = g_ptr_array_new();
    w->cursor  = -1;
    char worker_name[32];
    g_snprintf(worker_name, sizeof worker_name, "nd-tab-%u", w->id);
    w->worker = ns_tab_worker_new(worker_name);
    w->images  = ns_image_cache_new();
    w->videos  = ns_video_cache_new();
    ns_image_cache_set_worker(w->images, w->worker);
    ns_video_cache_set_worker(w->videos, w->worker);
    w->anim    = ns_anim_new();
    w->zoom    = 1.0;

    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    w->page_root = page;

    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class(toolbar, "toolbar");
    gtk_widget_add_css_class(toolbar, "nd-toolbar");
    ns_window_build_toolbar(w, toolbar, g_home_url);
    gtk_box_append(GTK_BOX(page), toolbar);

    ns_window_build_search_bar(w, page);
    ns_window_build_content(w, page);

    char page_name[32];
    g_snprintf(page_name, sizeof page_name, "tab-%p", (void *)w);
    gtk_stack_add_named(stack, page, page_name);

    GtkWidget *tab_button = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(tab_button, "nd-tab");

    GtkWidget *tab_activate = gtk_button_new();
    gtk_widget_add_css_class(tab_activate, "flat");
    GtkWidget *tab_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *tab_icon = gtk_image_new_from_icon_name("web-browser-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(tab_icon), 14);
    gtk_box_append(GTK_BOX(tab_box), tab_icon);
    GtkWidget *tab_label = gtk_label_new("New Tab");
    gtk_label_set_ellipsize(GTK_LABEL(tab_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(tab_label), 24);
    gtk_box_append(GTK_BOX(tab_box), tab_label);
    gtk_button_set_child(GTK_BUTTON(tab_activate), tab_box);
    g_signal_connect(tab_activate, "clicked", G_CALLBACK(on_tab_button_clicked), w);
    gtk_box_append(GTK_BOX(tab_button), tab_activate);

    GtkWidget *close_button = gtk_button_new_from_icon_name("window-close");
    gtk_widget_add_css_class(close_button, "flat");
    gtk_widget_add_css_class(close_button, "nd-tab-close");
    gtk_widget_set_tooltip_text(close_button, "Close tab");
    g_signal_connect(close_button, "clicked", G_CALLBACK(on_tab_close_clicked), w);
    gtk_box_append(GTK_BOX(tab_button), close_button);
    w->tab_button = tab_button;
    w->tab_icon   = tab_icon;
    w->tab_label  = tab_label;

    GtkWidget *new_tab_btn = g_object_get_data(G_OBJECT(toplevel), "nd-new-tab-button");
    if (new_tab_btn) {
        g_object_ref(new_tab_btn);
        gtk_box_remove(GTK_BOX(strip), new_tab_btn);
    }
    gtk_box_append(GTK_BOX(strip), tab_button);
    if (new_tab_btn) {
        gtk_box_append(GTK_BOX(strip), new_tab_btn);
        g_object_unref(new_tab_btn);
    }

    g_ptr_array_add(tabs, w);
    return w;
}

ns_window *
ns_browser_add_tab(GtkWidget *toplevel, GtkApplication *app, const char *url)
{
    (void)app;
    ns_window *w = ns_browser_construct_tab(toplevel);
    if (!w) return NULL;
    const char *target = (url && *url) ? url : g_home_url;
    if (target && *target) ns_window_load_url(w, target, NS_LOAD_USER);
    return w;
}

static ns_window *
ns_browser_add_tab_unloaded(GtkWidget *toplevel, const char *url)
{
    ns_window *w = ns_browser_construct_tab(toplevel);
    if (!w) return NULL;
    if (url && *url) {
        w->lazy_url = g_strdup(url);
        gtk_editable_set_text(GTK_EDITABLE(w->url_entry), url);
        if (w->tab_label) {
            char short_label[256];
            g_utf8_strncpy(short_label, url, 40);
            gtk_label_set_text(GTK_LABEL(w->tab_label), short_label);
        }
    }
    return w;
}

static void ns_push_closed_url(const char *url);

static gboolean
ns_browser_close_tab_deferred(gpointer data)
{
    ns_window *w = ns_window_for_id(GPOINTER_TO_UINT(data));
    if (w) ns_browser_close_tab(w);
    return G_SOURCE_REMOVE;
}

static void
ns_browser_close_tab(ns_window *w)
{
    if (!w || !w->window) return;
    if (w->js && ns_js_in_pump(w->js)) {
        g_idle_add(ns_browser_close_tab_deferred, GUINT_TO_POINTER(w->id));
        return;
    }
    GtkWidget *toplevel = w->window;
    ns_push_closed_url(ns_window_current_url(w));
    GPtrArray *tabs = g_object_get_data(G_OBJECT(toplevel), "nd-tabs");
    GtkStack *stack = g_object_get_data(G_OBJECT(toplevel), "nd-stack");
    GtkBox   *strip = g_object_get_data(G_OBJECT(toplevel), "nd-tab-strip");
    if (!tabs) return;

    if (tabs->len <= 1) {
        gtk_window_destroy(GTK_WINDOW(toplevel));
        return;
    }

    guint idx = 0;
    gboolean found = g_ptr_array_find(tabs, w, &idx);
    g_ptr_array_remove(tabs, w);
    if (strip && w->tab_button) gtk_box_remove(GTK_BOX(strip), w->tab_button);
    GtkWidget *page_root = w->page_root;

    ns_window *next = NULL;
    if (found && tabs->len > 0)
        next = g_ptr_array_index(tabs, idx < tabs->len ? idx : tabs->len - 1);
    else if (tabs->len > 0)
        next = g_ptr_array_index(tabs, 0);

    on_window_destroy(NULL, w);
    if (stack && page_root) gtk_stack_remove(stack, page_root);

    if (next) ns_browser_set_active(toplevel, next);
}

static void
ns_app_menu_popup(GtkWidget *toplevel)
{
    GtkWidget *anchor = g_object_get_data(G_OBJECT(toplevel), "nd-stack");
    if (!anchor) return;

    GMenu *menu = g_menu_new();

    GMenu *tab_section = g_menu_new();
    g_menu_append(tab_section, "New Tab",    "app.new-tab");
    g_menu_append(tab_section, "New Window", "app.new-window");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(tab_section));
    g_object_unref(tab_section);

    GMenu *page_section = g_menu_new();
    g_menu_append(page_section, "Find in Page", "win.find");
    g_menu_append(page_section, "Print",        "win.print");
    g_menu_append(page_section, "Save as PDF",  "win.save-pdf");
    g_menu_append(page_section, "Save Page",    "win.save-html");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(page_section));
    g_object_unref(page_section);

    GMenu *view_section = g_menu_new();
    g_menu_append(view_section, "Zoom In",     "win.zoom-in");
    g_menu_append(view_section, "Zoom Out",    "win.zoom-out");
    g_menu_append(view_section, "Reset Zoom",  "win.zoom-reset");
    g_menu_append(view_section, "Full Screen", "win.fullscreen");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(view_section));
    g_object_unref(view_section);

    GMenu *dev_section = g_menu_new();
    g_menu_append(dev_section, "View Page Source",  "win.ctx-view-source");
    g_menu_append(dev_section, "Developer Console", "win.open-console");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(dev_section));
    g_object_unref(dev_section);

    GMenu *quit_section = g_menu_new();
    g_menu_append(quit_section, "Close Tab", "win.close");
    g_menu_append(quit_section, "Exit",      "app.quit");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(quit_section));
    g_object_unref(quit_section);

    GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
    g_object_unref(menu);
    gtk_widget_set_parent(popover, anchor);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_widget_set_halign(popover, GTK_ALIGN_START);
    gtk_widget_set_valign(popover, GTK_ALIGN_START);
    GdkRectangle rect = { 0, 0, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_popover_popup(GTK_POPOVER(popover));
    g_signal_connect_swapped(popover, "closed", G_CALLBACK(gtk_widget_unparent), popover);
}

static gboolean
ns_on_menu_key_pressed(GtkEventControllerKey *c, guint keyval, guint keycode,
                       GdkModifierType state, gpointer user_data)
{
    (void)c; (void)keycode;
    GtkWidget *toplevel = user_data;
    gboolean alt_only = (keyval == GDK_KEY_Alt_L || keyval == GDK_KEY_Alt_R) &&
                        !(state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_SUPER_MASK));
    g_object_set_data(G_OBJECT(toplevel), "nd-alt-armed",
                      GINT_TO_POINTER(alt_only ? 1 : 0));
    return FALSE;
}

static gboolean
ns_on_menu_key_released(GtkEventControllerKey *c, guint keyval, guint keycode,
                        GdkModifierType state, gpointer user_data)
{
    (void)c; (void)keycode; (void)state;
    GtkWidget *toplevel = user_data;
    if (keyval != GDK_KEY_Alt_L && keyval != GDK_KEY_Alt_R) return FALSE;
    gboolean armed = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(toplevel), "nd-alt-armed")) != 0;
    g_object_set_data(G_OBJECT(toplevel), "nd-alt-armed", GINT_TO_POINTER(0));
    if (!armed) return FALSE;
    ns_app_menu_popup(toplevel);
    return TRUE;
}

static gboolean
ns_toplevel_close_retry(gpointer data)
{
    GtkWindow *win = data;
    gtk_window_close(win);
    g_object_unref(win);
    return G_SOURCE_REMOVE;
}

static gboolean
on_toplevel_close_request(GtkWindow *toplevel, gpointer user_data)
{
    (void)user_data;
    GPtrArray *tabs = g_object_get_data(G_OBJECT(toplevel), "nd-tabs");
    if (!tabs) return FALSE;
    for (guint i = 0; i < tabs->len; i++) {
        ns_window *w = g_ptr_array_index(tabs, i);
        if (w && w->js && ns_js_in_pump(w->js)) {
            g_timeout_add(50, ns_toplevel_close_retry, g_object_ref(toplevel));
            return TRUE;
        }
    }
    return FALSE;
}

static GtkWidget *
ns_browser_open_shell(GtkApplication *app)
{
    GtkWidget *toplevel = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(toplevel), NS_TITLE);
    gtk_window_set_icon_name(GTK_WINDOW(toplevel), "nordstjernen");
    const ns_config *cfg = ns_config_get();
    int win_w = cfg && cfg->window_width_px  > 0 ? cfg->window_width_px  : 1280;
    int win_h = cfg && cfg->window_height_px > 0 ? cfg->window_height_px :  800;
    gtk_window_set_default_size(GTK_WINDOW(toplevel), win_w, win_h);

    GPtrArray *tabs = g_ptr_array_new();
    g_object_set_data(G_OBJECT(toplevel), "nd-tabs", tabs);
    g_signal_connect(toplevel, "destroy", G_CALLBACK(on_toplevel_destroy), tabs);
    g_signal_connect(toplevel, "close-request",
                     G_CALLBACK(on_toplevel_close_request), NULL);

    GtkEventController *menukey = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(menukey, GTK_PHASE_CAPTURE);
    g_signal_connect(menukey, "key-pressed", G_CALLBACK(ns_on_menu_key_pressed), toplevel);
    g_signal_connect(menukey, "key-released", G_CALLBACK(ns_on_menu_key_released), toplevel);
    gtk_widget_add_controller(toplevel, menukey);

    GtkWidget *titlebar = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(titlebar), TRUE);
    gtk_widget_add_css_class(titlebar, "nd-titlebar");
    gtk_window_set_titlebar(GTK_WINDOW(toplevel), titlebar);

    GtkWidget *tab_strip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_margin_start(tab_strip, 20);
    GtkWidget *strip_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(strip_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(strip_scroll), tab_strip);
    gtk_widget_set_hexpand(strip_scroll, TRUE);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(titlebar), strip_scroll);
    g_object_set_data(G_OBJECT(toplevel), "nd-tab-strip", tab_strip);

    GtkWidget *new_tab_button = gtk_button_new_from_icon_name("tab-new");
    gtk_widget_add_css_class(new_tab_button, "flat");
    gtk_widget_set_tooltip_text(new_tab_button, "New tab (Ctrl+T)");
    g_signal_connect(new_tab_button, "clicked", G_CALLBACK(on_new_tab_clicked), toplevel);
    gtk_box_append(GTK_BOX(tab_strip), new_tab_button);
    g_object_set_data(G_OBJECT(toplevel), "nd-new-tab-button", new_tab_button);

    GtkWidget *stack = gtk_stack_new();
    gtk_widget_set_hexpand(stack, TRUE);
    gtk_widget_set_vexpand(stack, TRUE);
    gtk_window_set_child(GTK_WINDOW(toplevel), stack);
    g_object_set_data(G_OBJECT(toplevel), "nd-stack", stack);

    return toplevel;
}

static void
ns_window_open(GtkApplication *app, const char *startup_url)
{
    GtkWidget *toplevel = ns_browser_open_shell(app);

    const char *url = startup_url;
    if (!url || !*url) url = g_home_url;
    ns_window *first = ns_browser_add_tab(toplevel, app, url);

    gtk_window_maximize(GTK_WINDOW(toplevel));
    gtk_window_present(GTK_WINDOW(toplevel));

    if (first) {
        ns_browser_set_active(toplevel, first);
        gtk_widget_grab_focus(first->url_entry);
    }
}

static gboolean
ns_gtk_prefers_dark(void)
{
    GtkSettings *settings = gtk_settings_get_default();
    if (!settings) return FALSE;
    gboolean prefer_dark = FALSE;
    g_object_get(settings, "gtk-application-prefer-dark-theme", &prefer_dark, NULL);
    if (prefer_dark) return TRUE;
    char *theme = NULL;
    g_object_get(settings, "gtk-theme-name", &theme, NULL);
    gboolean dark = FALSE;
    if (theme) {
        char *lower = g_ascii_strdown(theme, -1);
        if (strstr(lower, "dark")) dark = TRUE;
        g_free(lower);
        g_free(theme);
    }
    return dark;
}

static void
ns_apply_user_prefs_to_css(void)
{
    const ns_config *c = ns_config_get();
    ns_color_scheme_pref cs = c ? c->color_scheme : NS_COLOR_SCHEME_PREF_AUTO;
    ns_css_color_scheme scheme = NS_CSS_COLOR_SCHEME_LIGHT;
    switch (cs) {
    case NS_COLOR_SCHEME_PREF_LIGHT: scheme = NS_CSS_COLOR_SCHEME_LIGHT; break;
    case NS_COLOR_SCHEME_PREF_DARK:  scheme = NS_CSS_COLOR_SCHEME_DARK;  break;
    case NS_COLOR_SCHEME_PREF_AUTO:
    default:
        scheme = ns_gtk_prefers_dark() ? NS_CSS_COLOR_SCHEME_DARK
                                       : NS_CSS_COLOR_SCHEME_LIGHT;
        break;
    }
    ns_css_set_color_scheme(scheme);

    ns_reduced_motion_pref rm = c ? c->reduced_motion : NS_REDUCED_MOTION_PREF_AUTO;
    ns_css_reduced_motion m = NS_CSS_REDUCED_MOTION_NO_PREFERENCE;
    switch (rm) {
    case NS_REDUCED_MOTION_PREF_REDUCE:        m = NS_CSS_REDUCED_MOTION_REDUCE; break;
    case NS_REDUCED_MOTION_PREF_NO_PREFERENCE: m = NS_CSS_REDUCED_MOTION_NO_PREFERENCE; break;
    case NS_REDUCED_MOTION_PREF_AUTO:
    default: {
        GtkSettings *settings = gtk_settings_get_default();
        gboolean enable_anim = TRUE;
        if (settings)
            g_object_get(settings, "gtk-enable-animations", &enable_anim, NULL);
        m = enable_anim ? NS_CSS_REDUCED_MOTION_NO_PREFERENCE
                        : NS_CSS_REDUCED_MOTION_REDUCE;
        break;
    }
    }
    ns_css_set_reduced_motion(m);
}

static void
on_gtk_theme_changed(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    (void)obj; (void)pspec; (void)user_data;
    ns_apply_user_prefs_to_css();
}

static gboolean
ns_screenshot_timer_cb(gpointer user_data)
{
    GtkApplication *app = user_data;
    static int seq = 0;
    GtkWindow *win = gtk_application_get_active_window(app);
    ns_window *w = win ? g_object_get_data(G_OBJECT(win), "nd-window") : NULL;
    if (!w || !g_screenshot_path) return G_SOURCE_REMOVE;
    char *path;
    if (g_screenshot_every_ms > 0) {
        char *dot = strrchr(g_screenshot_path, '.');
        if (dot)
            path = g_strdup_printf("%.*s-%03d%s",
                                   (int)(dot - g_screenshot_path),
                                   g_screenshot_path, seq, dot);
        else
            path = g_strdup_printf("%s-%03d.png", g_screenshot_path, seq);
    } else {
        path = g_strdup(g_screenshot_path);
    }
    seq++;
    if (ns_window_screenshot_to(w, path))
        g_printerr("[screenshot] wrote %s\n", path);
    else
        g_printerr("[screenshot] failed to write %s\n", path);
    g_free(path);
    if (g_screenshot_every_ms > 0 && seq < 60)
        return G_SOURCE_CONTINUE;
    return G_SOURCE_REMOVE;
}

#define NS_SESSION_WRITE_SECS 3

#ifndef G_OS_WIN32
static gboolean
ns_on_quit_signal(gpointer user_data)
{
    g_application_quit(G_APPLICATION(user_data));
    return G_SOURCE_REMOVE;
}
#endif

static gboolean
ns_session_url_restorable(const char *url)
{
    return url && (g_str_has_prefix(url, "http://") ||
                   g_str_has_prefix(url, "https://") ||
                   g_str_has_prefix(url, "file://"));
}

static gboolean
ns_session_write_cb(gpointer user_data)
{
    static char *last_written;
    GtkApplication *app = user_data;
    GString *s = g_string_new(NULL);
    for (GList *l = gtk_application_get_windows(app); l; l = l->next) {
        GPtrArray *tabs = g_object_get_data(G_OBJECT(l->data), "nd-tabs");
        if (!tabs) continue;
        for (guint i = 0; i < tabs->len; i++) {
            ns_window *t = g_ptr_array_index(tabs, i);
            const char *url = t->lazy_url ? t->lazy_url : ns_window_current_url(t);
            if (ns_session_url_restorable(url))
                g_string_append_printf(s, "%s\n", url);
        }
    }
    if (g_strcmp0(s->str, last_written) != 0) {
        g_file_set_contents(g_watchdog_session_path, s->str, s->len, NULL);
        g_free(last_written);
        last_written = g_strdup(s->str);
    }
    g_string_free(s, TRUE);
    return G_SOURCE_CONTINUE;
}

static void
ns_session_recovery_notice(GtkWindow *parent, guint n)
{
    GtkAlertDialog *dlg =
        gtk_alert_dialog_new("Nordstjernen recovered after an unexpected exit");
    char *detail = g_strdup_printf("Restored %u page%s from your previous session. "
                                   "Click a tab to reload it.",
                                   n, n == 1 ? "" : "s");
    gtk_alert_dialog_set_detail(dlg, detail);
    gtk_alert_dialog_set_modal(dlg, FALSE);
    gtk_alert_dialog_show(dlg, parent);
    g_free(detail);
    g_object_unref(dlg);
}

static gboolean
ns_session_try_restore(GtkApplication *app)
{
    char *contents = NULL;
    if (!g_watchdog_session_path ||
        !g_file_get_contents(g_watchdog_session_path, &contents, NULL, NULL))
        return FALSE;

    char **lines = g_strsplit(contents, "\n", -1);
    g_free(contents);
    GPtrArray *urls = g_ptr_array_new();
    for (int i = 0; lines[i]; i++) {
        char *u = g_strstrip(lines[i]);
        if (ns_session_url_restorable(u))
            g_ptr_array_add(urls, u);
    }
    if (urls->len == 0) {
        g_ptr_array_free(urls, TRUE);
        g_strfreev(lines);
        return FALSE;
    }

    GtkWidget *toplevel = ns_browser_open_shell(app);
    ns_window *first = NULL;
    for (guint i = 0; i < urls->len; i++) {
        ns_window *w = ns_browser_add_tab_unloaded(toplevel,
                                                   g_ptr_array_index(urls, i));
        if (w && !first) first = w;
    }
    gtk_window_maximize(GTK_WINDOW(toplevel));
    gtk_window_present(GTK_WINDOW(toplevel));
    if (first) ns_browser_set_active(toplevel, first);
    ns_session_recovery_notice(GTK_WINDOW(toplevel), urls->len);

    g_ptr_array_free(urls, TRUE);
    g_strfreev(lines);
    return TRUE;
}

static void ns_probe_gl_or_fallback(void);

static void
on_activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;
    ns_probe_gl_or_fallback();
    ns_install_icon_search_paths();
    ns_apply_user_prefs_to_css();
    GtkSettings *settings = gtk_settings_get_default();
    if (settings) {
        g_signal_connect(settings, "notify::gtk-application-prefer-dark-theme",
                         G_CALLBACK(on_gtk_theme_changed), NULL);
        g_signal_connect(settings, "notify::gtk-theme-name",
                         G_CALLBACK(on_gtk_theme_changed), NULL);
        g_signal_connect(settings, "notify::gtk-enable-animations",
                         G_CALLBACK(on_gtk_theme_changed), NULL);
    }
    static gboolean recovery_consumed;
    gboolean restored = FALSE;
    if (!recovery_consumed && ns_watchdog_child_is_recovery()) {
        recovery_consumed = TRUE;
        restored = ns_session_try_restore(app);
    }
    if (!restored) {
        const char *startup_url = g_startup_url_override;
        if (!startup_url || !*startup_url) startup_url = g_getenv("NS_STARTUP_URL");
        ns_window_open(app, startup_url);
    }
    if (g_screenshot_path) {
        int first = g_screenshot_every_ms > 0 ? g_screenshot_every_ms
                                              : g_screenshot_delay_ms;
        if (first > 0)
            g_timeout_add(first, ns_screenshot_timer_cb, app);
    }
}

static int
ns_on_command_line(GApplication *app, GApplicationCommandLine *cmdline, gpointer user_data)
{
    (void)user_data;
    gchar **argv;
    gint argc = 0;
    argv = g_application_command_line_get_arguments(cmdline, &argc);
    g_free(g_startup_url_override);
    g_startup_url_override = NULL;
    for (int i = 1; i < argc; i++) {
        if (!argv[i]) continue;
        if (g_str_has_prefix(argv[i], "--screenshot=")) {
            g_free(g_screenshot_path);
            g_screenshot_path = g_strdup(argv[i] + 13);
            if (g_screenshot_delay_ms <= 0) g_screenshot_delay_ms = 3500;
        } else if (g_str_has_prefix(argv[i], "--screenshot-delay=")) {
            g_screenshot_delay_ms = (int)g_ascii_strtoll(argv[i] + 19, NULL, 10);
        } else if (g_str_has_prefix(argv[i], "--screenshot-every=")) {
            g_screenshot_every_ms = (int)g_ascii_strtoll(argv[i] + 19, NULL, 10);
        } else if (argv[i][0] != '-' && !g_startup_url_override) {
            g_startup_url_override = g_strdup(argv[i]);
        }
    }
    g_application_activate(app);
    g_strfreev(argv);
    return 0;
}

static void
on_win_focus_url(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    ns_window *w = user_data;
    gtk_widget_grab_focus(w->url_entry);
    gtk_editable_select_region(GTK_EDITABLE(w->url_entry), 0, -1);
}

static void
on_win_reload(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    ns_window *w = user_data;
    if (w->cursor < 0 || w->cursor >= (int)w->history->len) return;
    const char *cur = g_ptr_array_index(w->history, w->cursor);
    ns_window_load_url(w, cur, NS_LOAD_HISTORY);
}

static void
on_win_stop(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    ns_window *w = user_data;
    if (w->search_revealer &&
        gtk_revealer_get_reveal_child(GTK_REVEALER(w->search_revealer))) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(w->search_revealer), FALSE);
        g_free(w->search_query);
        w->search_query = NULL;
        gtk_widget_queue_draw(w->drawing_area);
        return;
    }
    if (w->window && GTK_IS_WINDOW(w->window) &&
        gtk_window_is_fullscreen(GTK_WINDOW(w->window))) {
        gtk_window_unfullscreen(GTK_WINDOW(w->window));
        ns_window_set_status(w, "Exited full screen");
        return;
    }
    gboolean did_something = FALSE;
    gboolean invalidated_load = FALSE;
    if (w->current_fetch) {
        g_cancellable_cancel(w->current_fetch);
        g_clear_object(&w->current_fetch);
        w->fetch_gen++;
        invalidated_load = TRUE;
        did_something = TRUE;
    }
    if (w->busy && !invalidated_load) {
        w->fetch_gen++;
        did_something = TRUE;
    }
    if (w->css_cancellable) {
        g_cancellable_cancel(w->css_cancellable);
        if (!invalidated_load) {
            w->fetch_gen++;
            invalidated_load = TRUE;
        }
        w->css_inflight = 0;
        did_something = TRUE;
    }
    if (w->js && !ns_js_is_halted(w->js)) {
        ns_js_halt(w->js);
        did_something = TRUE;
    }
    if (did_something) {
        ns_window_set_status(w, "Stopped");
        ns_window_set_busy(w, FALSE);
        if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
    }
}

static gboolean
ns_window_after_zoom_deferred(gpointer data)
{
    ns_window *w = ns_window_for_id(GPOINTER_TO_UINT(data));
    if (w) ns_window_after_zoom(w);
    return G_SOURCE_REMOVE;
}

static void
ns_window_after_zoom(ns_window *w)
{
    if (w->js && ns_js_in_pump(w->js)) {
        g_idle_add(ns_window_after_zoom_deferred, GUINT_TO_POINTER(w->id));
        return;
    }
    if (w->layout_tree) { if (w->js) ns_js_set_layout_root(w->js, NULL); ns_box_free(w->layout_tree); w->layout_tree = NULL; ns_selection_clear(&w->selection); w->search_active_box = NULL; }
    if (w->style_table) { if (w->js) ns_js_set_style_table(w->js, NULL); g_hash_table_destroy(w->style_table); w->style_table = NULL; }
    if (w->parsed_doc)  { ns_node_free(w->parsed_doc);  w->parsed_doc  = NULL; }
    w->layout_dirty = TRUE;
    w->focused_input = NULL;
    g_free(w->focused_input_initial);
    w->focused_input_initial = NULL;
    if (w->js)          { ns_js_free(w->js);            w->js          = NULL; }
    if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
}

static void
on_win_zoom_in(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    ns_window *w = user_data;
    w->zoom *= 1.1;
    if (w->zoom > 5.0) w->zoom = 5.0;
    ns_window_after_zoom(w);
}

static void
on_win_zoom_out(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    ns_window *w = user_data;
    w->zoom /= 1.1;
    if (w->zoom < 0.4) w->zoom = 0.4;
    ns_window_after_zoom(w);
}

static void
on_win_zoom_reset(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    ns_window *w = user_data;
    w->zoom = 1.0;
    ns_window_after_zoom(w);
}

static void
on_win_fullscreen(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    ns_window *w = user_data;
    if (!w->window || !GTK_IS_WINDOW(w->window)) return;
    if (gtk_window_is_fullscreen(GTK_WINDOW(w->window))) {
        gtk_window_unfullscreen(GTK_WINDOW(w->window));
        ns_window_set_status(w, "Exited full screen");
    } else {
        gtk_window_fullscreen(GTK_WINDOW(w->window));
        ns_window_set_status(w, "Full screen — press F11 or Esc to exit");
    }
}

gboolean
on_url_entry_key_pressed(GtkEventControllerKey *ctrl, guint keyval,
                         guint keycode, GdkModifierType state,
                         gpointer user_data)
{
    (void)ctrl; (void)keycode; (void)state;
    ns_window *w = user_data;
    gboolean suggesting = w->suggest_popover &&
                          gtk_widget_get_visible(w->suggest_popover);
    if (suggesting && (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down)) {
        suggest_move_selection(w, +1);
        return TRUE;
    }
    if (suggesting && (keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up)) {
        suggest_move_selection(w, -1);
        return TRUE;
    }
    if (keyval != GDK_KEY_Escape) return FALSE;
    if (suggesting) {
        ns_window_hide_suggestions(w);
        return TRUE;
    }
    const char *cur = ns_window_current_url(w);
    w->suggest_suppress = TRUE;
    if (cur) {
        char *disp = ns_url_to_display(cur);
        gtk_editable_set_text(GTK_EDITABLE(w->url_entry), disp ? disp : cur);
        g_free(disp);
    } else {
        gtk_editable_set_text(GTK_EDITABLE(w->url_entry), "");
    }
    w->suggest_suppress = FALSE;
    if (w->drawing_area) gtk_widget_grab_focus(w->drawing_area);
    return TRUE;
}

static void
on_win_close(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    ns_window *w = user_data;
    ns_browser_close_tab(w);
}

static void
on_win_back(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    ns_window *w = user_data;
    if (w->cursor <= 0) return;
    w->cursor--;
    ns_window_load_url(w, g_ptr_array_index(w->history, w->cursor), NS_LOAD_HISTORY);
}

static void
on_win_forward(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    ns_window *w = user_data;
    if (w->cursor < 0 || w->cursor + 1 >= (int)w->history->len) return;
    w->cursor++;
    ns_window_load_url(w, g_ptr_array_index(w->history, w->cursor), NS_LOAD_HISTORY);
}

static void
on_app_quit(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    GtkApplication *app = user_data;
    g_application_quit(G_APPLICATION(app));
}

static GPtrArray *g_closed_tab_urls;

static void
ns_push_closed_url(const char *url)
{
    if (!url || !*url || g_str_has_prefix(url, "about:")) return;
    if (!g_closed_tab_urls)
        g_closed_tab_urls = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(g_closed_tab_urls, g_strdup(url));
    while (g_closed_tab_urls->len > 32)
        g_ptr_array_remove_index(g_closed_tab_urls, 0);
}

static char *
ns_pop_closed_url(void)
{
    if (!g_closed_tab_urls || g_closed_tab_urls->len == 0) return NULL;
    guint last = g_closed_tab_urls->len - 1;
    char *url = g_strdup(g_ptr_array_index(g_closed_tab_urls, last));
    g_ptr_array_remove_index(g_closed_tab_urls, last);
    return url;
}

static void
ns_browser_switch_to_index(GtkWidget *toplevel, int index)
{
    GPtrArray *tabs = g_object_get_data(G_OBJECT(toplevel), "nd-tabs");
    if (!tabs || tabs->len == 0) return;
    if (index < 0) index = 0;
    if (index >= (int)tabs->len) index = (int)tabs->len - 1;
    ns_browser_set_active(toplevel, g_ptr_array_index(tabs, index));
}

static void
ns_browser_switch_relative(GtkWidget *toplevel, int delta)
{
    GPtrArray *tabs = g_object_get_data(G_OBJECT(toplevel), "nd-tabs");
    ns_window *cur = g_object_get_data(G_OBJECT(toplevel), "nd-window");
    if (!tabs || tabs->len == 0 || !cur) return;
    guint idx = 0;
    if (!g_ptr_array_find(tabs, cur, &idx)) return;
    int n = (int)tabs->len;
    int next = (((int)idx + delta) % n + n) % n;
    ns_browser_set_active(toplevel, g_ptr_array_index(tabs, next));
}

static void
on_win_hard_reload(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    ns_window *w = user_data;
    if (w->cursor < 0 || w->cursor >= (int)w->history->len) return;
    const char *cur = g_ptr_array_index(w->history, w->cursor);
    ns_window_load_url(w, cur, NS_LOAD_USER);
}

static void
on_win_home(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    ns_window *w = user_data;
    if (g_home_url && *g_home_url)
        ns_window_load_url(w, g_home_url, NS_LOAD_USER);
}

static void
on_win_next_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    ns_window *w = user_data;
    ns_browser_switch_relative(w->window, +1);
}

static void
on_win_prev_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    ns_window *w = user_data;
    ns_browser_switch_relative(w->window, -1);
}

static void
on_win_select_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    ns_window *w = user_data;
    if (!parameter) return;
    gint32 n = g_variant_get_int32(parameter);
    GPtrArray *tabs = g_object_get_data(G_OBJECT(w->window), "nd-tabs");
    if (!tabs) return;
    if (n >= 9) ns_browser_switch_to_index(w->window, (int)tabs->len - 1);
    else        ns_browser_switch_to_index(w->window, n - 1);
}

static void
on_win_reopen_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    ns_window *w = user_data;
    char *url = ns_pop_closed_url();
    if (!url) return;
    GtkApplication *app = gtk_window_get_application(GTK_WINDOW(w->window));
    ns_window *nw = ns_browser_add_tab(w->window, app, url);
    if (nw) ns_browser_set_active(w->window, nw);
    g_free(url);
}

static void
on_win_close_window(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    ns_window *w = user_data;
    if (w->window && GTK_IS_WINDOW(w->window))
        gtk_window_destroy(GTK_WINDOW(w->window));
}

static void
ns_window_install_actions(ns_window *w)
{
    static const struct {
        const char *name;
        GCallback   cb;
    } actions[] = {
        { "focus-url", G_CALLBACK(on_win_focus_url) },
        { "reload",    G_CALLBACK(on_win_reload)    },
        { "hard-reload", G_CALLBACK(on_win_hard_reload) },
        { "stop",      G_CALLBACK(on_win_stop)      },
        { "close",     G_CALLBACK(on_win_close)     },
        { "close-window", G_CALLBACK(on_win_close_window) },
        { "back",      G_CALLBACK(on_win_back)      },
        { "forward",   G_CALLBACK(on_win_forward)   },
        { "home",      G_CALLBACK(on_win_home)      },
        { "find",      G_CALLBACK(on_win_find)      },
        { "find-next", G_CALLBACK(on_win_find_next) },
        { "find-prev", G_CALLBACK(on_win_find_prev) },
        { "next-tab",  G_CALLBACK(on_win_next_tab)  },
        { "prev-tab",  G_CALLBACK(on_win_prev_tab)  },
        { "reopen-tab", G_CALLBACK(on_win_reopen_tab) },
        { "zoom-in",   G_CALLBACK(on_win_zoom_in)   },
        { "zoom-out",  G_CALLBACK(on_win_zoom_out)  },
        { "zoom-reset",G_CALLBACK(on_win_zoom_reset)},
        { "print",     G_CALLBACK(on_win_print)     },
        { "save-pdf",  G_CALLBACK(on_win_save_pdf)  },
        { "save-html", G_CALLBACK(on_win_save_html) },
        { "screenshot", G_CALLBACK(on_win_screenshot) },
        { "open-console", G_CALLBACK(on_win_open_console) },
        { "fullscreen", G_CALLBACK(on_win_fullscreen) },
    };
    GActionMap *map = G_ACTION_MAP(w->window);
    for (gsize i = 0; i < G_N_ELEMENTS(actions); i++) {
        g_action_map_remove_action(map, actions[i].name);
        GSimpleAction *a = g_simple_action_new(actions[i].name, NULL);
        g_signal_connect(a, "activate", actions[i].cb, w);
        g_action_map_add_action(map, G_ACTION(a));
        g_object_unref(a);
    }

    g_action_map_remove_action(map, "select-tab");
    GSimpleAction *select_tab = g_simple_action_new("select-tab",
                                                    G_VARIANT_TYPE_INT32);
    g_signal_connect(select_tab, "activate", G_CALLBACK(on_win_select_tab), w);
    g_action_map_add_action(map, G_ACTION(select_tab));
    g_object_unref(select_tab);
}

static void
ns_install_css(void)
{
    GdkDisplay *display = gdk_display_get_default();
    if (!display) return;
    static const char css[] =
        "headerbar.nd-titlebar { min-height: 24px; padding: 0 2px; }\n"
        "headerbar.nd-titlebar button { min-height: 20px; min-width: 20px; padding: 0 4px; }\n"
        "headerbar.nd-titlebar windowcontrols button { min-height: 20px; min-width: 20px; }\n"
        "button.nd-tab { min-height: 18px; padding: 0 6px; }\n"
        "button.nd-tab-close { min-height: 16px; min-width: 16px; padding: 0; }\n"
        "box.nd-toolbar { padding: 4px 6px; border-bottom: 1px solid alpha(currentColor, 0.12); }\n"
        "box.nd-toolbar > button { min-height: 28px; min-width: 28px; padding: 0 6px; }\n"
        "box.nd-toolbar > entry { min-height: 28px; margin: 0 4px; }\n"
        "window.nd-about box.nd-about-content { padding: 24px 36px; }\n"
        "window.nd-about label.nd-about-name {\n"
        "  font-size: 20pt; font-weight: 600; margin-top: 8px;\n"
        "}\n"
        "window.nd-about label.nd-about-version {\n"
        "  font-size: 11pt; color: alpha(currentColor, 0.7);\n"
        "}\n"
        "window.nd-about label.nd-about-tagline {\n"
        "  font-style: italic; color: alpha(currentColor, 0.65);\n"
        "  margin-top: 12px;\n"
        "}\n"
        "window.nd-about label.nd-about-copy {\n"
        "  font-size: 9pt; color: alpha(currentColor, 0.55);\n"
        "  margin-top: 16px;\n"
        "}\n";
    GtkCssProvider *p = gtk_css_provider_new();
    gtk_css_provider_load_from_string(p, css);
    gtk_style_context_add_provider_for_display(
        display, GTK_STYLE_PROVIDER(p),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);
}

static void
ns_install_icon_search_paths(void)
{
    GdkDisplay *display = gdk_display_get_default();
    if (!display) return;
    GtkIconTheme *theme = gtk_icon_theme_get_for_display(display);
    if (!theme) return;
    if (!g_self_exe) return;
    char *exe_dir = g_path_get_dirname(g_self_exe);
    if (!exe_dir) return;
    const char *try_rel[] = {
        "share/icons",
        "../share/icons",
        "../Resources/share/icons",
        "data/icons",
        "../data/icons",
        "../../data/icons",
        NULL,
    };
    for (int i = 0; try_rel[i]; i++) {
        char *p = g_build_filename(exe_dir, try_rel[i], NULL);
        gtk_icon_theme_add_search_path(theme, p);
        g_free(p);
    }
    g_free(exe_dir);
}

static void
ns_install_actions(GtkApplication *app)
{
    ns_install_icon_search_paths();
    ns_install_css();

    GSimpleAction *new_window = g_simple_action_new("new-window", NULL);
    g_signal_connect(new_window, "activate", G_CALLBACK(on_app_new_window), app);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(new_window));
    g_object_unref(new_window);

    GSimpleAction *new_tab = g_simple_action_new("new-tab", NULL);
    g_signal_connect(new_tab, "activate", G_CALLBACK(on_app_new_tab), app);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(new_tab));
    g_object_unref(new_tab);

    GSimpleAction *quit = g_simple_action_new("quit", NULL);
    g_signal_connect(quit, "activate", G_CALLBACK(on_app_quit), app);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(quit));
    g_object_unref(quit);

    ns_setup_bookmarks_watch(app);

    const struct {
        const char *action;
        const char *accels[4];
    } binds[] = {
        { "app.new-window", { "<Primary>n", NULL, NULL } },
        { "app.new-tab",    { "<Primary>t", NULL, NULL } },
        { "app.quit",       { "<Primary>q", NULL, NULL } },
        { "win.focus-url",  { "<Primary>l", "<Alt>d", "F6", NULL } },
        { "win.reload",     { "<Primary>r", "F5", NULL } },
        { "win.hard-reload",{ "<Primary><Shift>r", "<Primary>F5", NULL } },
        { "win.stop",       { "Escape", NULL, NULL } },
        { "win.close",      { "<Primary>w", "<Primary>F4", NULL } },
        { "win.close-window", { "<Primary><Shift>w", NULL, NULL } },
        { "win.back",       { "<Alt>Left", NULL, NULL } },
        { "win.forward",    { "<Alt>Right", NULL, NULL } },
        { "win.home",       { "<Alt>Home", NULL, NULL } },
        { "win.find",       { "<Primary>f", NULL, NULL } },
        { "win.find-next",  { "F3", "<Primary>g", NULL } },
        { "win.find-prev",  { "<Shift>F3", "<Primary><Shift>g", NULL } },
        { "win.next-tab",   { "<Primary>Page_Down", "<Primary>Tab", NULL } },
        { "win.prev-tab",   { "<Primary>Page_Up", "<Primary><Shift>Tab", NULL } },
        { "win.reopen-tab", { "<Primary><Shift>t", NULL, NULL } },
        { "win.zoom-in",    { "<Primary>plus", "<Primary>equal", NULL } },
        { "win.zoom-out",   { "<Primary>minus", NULL, NULL } },
        { "win.zoom-reset", { "<Primary>0", NULL, NULL } },
        { "win.print",      { "<Primary>p", NULL, NULL } },
        { "win.save-pdf",   { "<Primary><Shift>s", NULL, NULL } },
        { "win.save-html",  { "<Primary>s", NULL, NULL } },
        { "win.screenshot", { "<Primary><Shift>p", NULL, NULL } },
        { "win.open-console", { "<Primary><Shift>j", "<Primary><Shift>i" } },
        { "win.fullscreen", { "F11", NULL, NULL } },
        { "win.ctx-bookmark-page", { "<Primary>d", NULL, NULL } },
        { "win.ctx-view-source",   { "<Primary>u", NULL, NULL } },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(binds); i++)
        gtk_application_set_accels_for_action(app, binds[i].action, binds[i].accels);

    for (int n = 1; n <= 9; n++) {
        char action_name[32];
        char accel[16];
        g_snprintf(action_name, sizeof action_name, "win.select-tab(%d)", n);
        g_snprintf(accel, sizeof accel, "<Primary>%d", n);
        const char *accels[] = { accel, NULL };
        gtk_application_set_accels_for_action(app, action_name, accels);
    }
}

static void
on_bookmarks_file_changed(GFileMonitor *mon, GFile *file, GFile *other,
                          GFileMonitorEvent event, gpointer user_data)
{
    (void)mon; (void)file; (void)other;
    GtkApplication *app = user_data;
    if (event != G_FILE_MONITOR_EVENT_CHANGED &&
        event != G_FILE_MONITOR_EVENT_CREATED &&
        event != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT)
        return;
    (void)app;
    ns_bookmarks_free(g_bookmarks);
    g_bookmarks = ns_bookmarks_load();
}

static void
ns_setup_bookmarks_watch(GtkApplication *app)
{
    char *path = g_build_filename(g_get_user_config_dir(),
                                  NS_APP_DIR_NAME, "bookmarks.txt", NULL);
    GFile *file = g_file_new_for_path(path);
    g_free(path);
    GError *err = NULL;
    g_bookmarks_monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, &err);
    g_object_unref(file);
    if (g_bookmarks_monitor) {
        g_signal_connect(g_bookmarks_monitor, "changed",
                         G_CALLBACK(on_bookmarks_file_changed), app);
    } else if (err) {
        g_clear_error(&err);
    }
}

static void
init_self_exe(const char *argv0)
{
#ifdef __linux__
    char *resolved = g_file_read_link("/proc/self/exe", NULL);
    if (resolved) { g_self_exe = resolved; return; }
#endif
#ifdef __APPLE__
    {
        uint32_t size = 0;
        _NSGetExecutablePath(NULL, &size);
        if (size > 0 && size <= 32768) {
            char *raw = g_malloc(size);
            if (_NSGetExecutablePath(raw, &size) == 0) {
                char *real = realpath(raw, NULL);
                if (real) {
                    g_self_exe = g_strdup(real);
                    free(real);
                } else {
                    g_self_exe = g_strdup(raw);
                }
            }
            g_free(raw);
            if (g_self_exe) return;
        }
    }
#endif
#ifdef G_OS_WIN32
    {
        DWORD cap = MAX_PATH;
        wchar_t *buf = g_new(wchar_t, cap);
        DWORD n = GetModuleFileNameW(NULL, buf, cap);
        while (n >= cap && cap < 32768) {
            cap *= 2;
            wchar_t *bigger = g_renew(wchar_t, buf, cap);
            buf = bigger;
            n = GetModuleFileNameW(NULL, buf, cap);
        }
        if (n > 0 && n < cap)
            g_self_exe = g_utf16_to_utf8((gunichar2 *)buf, -1, NULL, NULL, NULL);
        g_free(buf);
        if (g_self_exe) return;
    }
#endif
    if (argv0) {
        if (g_path_is_absolute(argv0))
            g_self_exe = g_strdup(argv0);
        else
            g_self_exe = g_find_program_in_path(argv0);
        if (!g_self_exe) g_self_exe = g_strdup(argv0);
    }
    if (!g_self_exe)
        g_debug("init_self_exe: could not resolve own binary path; "
                "new-window will run in-process");
}

const char *ns_app_self_exe(void);

const char *
ns_app_self_exe(void)
{
    return g_self_exe;
}

static GLogWriterOutput
ns_log_writer(GLogLevelFlags log_level,
              const GLogField *fields, gsize n_fields,
              gpointer user_data)
{
    (void)user_data;
    const char *captured = NULL;
    const char *captured_domain = NULL;
    for (gsize i = 0; i < n_fields; i++) {
        if (g_strcmp0(fields[i].key, "MESSAGE") == 0 && fields[i].value) {
            const char *m = fields[i].value;
            captured = m;
            if (strstr(m, "win32 session dbus binary not found"))
                return G_LOG_WRITER_HANDLED;
            if (strstr(m, "but sizes must be >= 0"))
                return G_LOG_WRITER_HANDLED;
            if (strstr(m, "Baselines must be inside the widget size"))
                return G_LOG_WRITER_HANDLED;
            if (strstr(m, "without a current allocation"))
                return G_LOG_WRITER_HANDLED;
            if (strstr(m, "No IM module matching GTK_IM_MODULE="))
                return G_LOG_WRITER_HANDLED;
        }
        if (g_strcmp0(fields[i].key, "GLIB_DOMAIN") == 0 && fields[i].value)
            captured_domain = fields[i].value;
    }
    if (captured) {
        ns_dlog_level lvl = NS_DLOG_INFO;
        if (log_level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL))
            lvl = NS_DLOG_ERROR;
        else if (log_level & G_LOG_LEVEL_WARNING)
            lvl = NS_DLOG_WARN;
        ns_debug_log_emit(lvl, captured_domain ? captured_domain : "glib",
                          "%s", captured);
    }
    return g_log_writer_default(log_level, fields, n_fields, user_data);
}

#ifdef G_OS_WIN32

__declspec(dllimport) HRESULT __stdcall
SetCurrentProcessExplicitAppUserModelID(PCWSTR AppID);

static void
ns_win32_set_app_id(void)
{
    (void)SetCurrentProcessExplicitAppUserModelID(L"Nordstjernen.Browser");
}

static void
ns_win32_anchor_gtk_data(void)
{
    if (!g_self_exe) return;
    char *dir = g_path_get_dirname(g_self_exe);
    if (!dir) return;
    char *share_dir = g_build_filename(dir, "share", NULL);
    if (g_file_test(share_dir, G_FILE_TEST_IS_DIR)) {
        if (!g_getenv("GTK_DATA_PREFIX")) g_setenv("GTK_DATA_PREFIX", dir, TRUE);
        if (!g_getenv("GTK_EXE_PREFIX"))  g_setenv("GTK_EXE_PREFIX",  dir, TRUE);
        if (!g_getenv("XDG_DATA_DIRS"))   g_setenv("XDG_DATA_DIRS", share_dir, TRUE);
    }
    g_free(share_dir);
    if (!g_getenv("GDK_PIXBUF_MODULE_FILE")) {
        char *loaders = g_build_filename(dir,
            "lib", "gdk-pixbuf-2.0", "2.10.0", "loaders.cache", NULL);
        if (g_file_test(loaders, G_FILE_TEST_EXISTS))
            g_setenv("GDK_PIXBUF_MODULE_FILE", loaders, TRUE);
        g_free(loaders);
    }
    {
        char *ca = g_build_filename(dir,
            "etc", "ssl", "certs", "ca-bundle.crt", NULL);
        if (g_file_test(ca, G_FILE_TEST_EXISTS)) {
            if (!g_getenv("CURL_CA_BUNDLE")) g_setenv("CURL_CA_BUNDLE", ca, TRUE);
            if (!g_getenv("SSL_CERT_FILE"))  g_setenv("SSL_CERT_FILE",  ca, TRUE);
        }
        g_free(ca);
    }
    g_free(dir);
}

static gboolean
ns_win32_args_need_console(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!argv[i]) continue;
        if (g_strcmp0(argv[i], "--headless")     == 0 ||
            g_strcmp0(argv[i], "--print-config") == 0 ||
            g_str_has_prefix(argv[i], "--dump=") ||
            g_str_has_prefix(argv[i], "--url=")  ||
            g_str_has_prefix(argv[i], "--viewport=") ||
            g_str_has_prefix(argv[i], "--eval=") ||
            g_str_has_prefix(argv[i], "--inspect=") ||
            g_str_has_prefix(argv[i], "--inspect-at=") ||
            g_str_has_prefix(argv[i], "--settle-ms="))
            return TRUE;
    }
    return FALSE;
}

static gboolean
ns_win32_fd_is_bound(int fd)
{
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    return h != NULL && h != INVALID_HANDLE_VALUE;
}

static void
ns_win32_attach_parent_console(void)
{
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    FILE *fp;
    if (!ns_win32_fd_is_bound(_fileno(stdout)))
        (void)freopen_s(&fp, "CONOUT$", "w", stdout);
    if (!ns_win32_fd_is_bound(_fileno(stderr)))
        (void)freopen_s(&fp, "CONOUT$", "w", stderr);
    if (!ns_win32_fd_is_bound(_fileno(stdin)))
        (void)freopen_s(&fp, "CONIN$",  "r", stdin);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}
#endif

#ifdef __APPLE__
static void
ns_macos_anchor_gtk_data(void)
{
    if (!g_self_exe) return;
    char *macos_dir = g_path_get_dirname(g_self_exe);
    if (!macos_dir) return;
    char *contents = g_path_get_dirname(macos_dir);
    g_free(macos_dir);
    if (!contents) return;
    char *res = g_build_filename(contents, "Resources", NULL);
    g_free(contents);

    char *schemas = g_build_filename(res, "share", "glib-2.0", "schemas", NULL);
    if (g_file_test(schemas, G_FILE_TEST_IS_DIR) &&
        !g_getenv("GSETTINGS_SCHEMA_DIR"))
        g_setenv("GSETTINGS_SCHEMA_DIR", schemas, TRUE);
    g_free(schemas);

    char *loaders = g_build_filename(res, "lib", "gdk-pixbuf-2.0",
                                     "2.10.0", "loaders", NULL);
    char *cache = g_build_filename(res, "lib", "gdk-pixbuf-2.0",
                                   "2.10.0", "loaders.cache", NULL);
    if (g_file_test(cache, G_FILE_TEST_EXISTS)) {
        if (!g_getenv("GDK_PIXBUF_MODULE_FILE"))
            g_setenv("GDK_PIXBUF_MODULE_FILE", cache, TRUE);
        if (!g_getenv("GDK_PIXBUF_MODULEDIR"))
            g_setenv("GDK_PIXBUF_MODULEDIR", loaders, TRUE);
    }
    g_free(cache);
    g_free(loaders);

    g_free(res);
}
#endif

void ns_main_on_font_loaded(const char *family, gpointer user_data);

void
ns_main_on_font_loaded(const char *family, gpointer user_data)
{
    (void)family;
    GtkApplication *app = user_data;
    if (!app) return;
    GList *windows = gtk_application_get_windows(app);
    for (GList *l = windows; l; l = l->next) {
        GtkWindow *win = l->data;
        ns_window *w = g_object_get_data(G_OBJECT(win), "nd-window");
        if (!w) continue;
        w->layout_dirty = TRUE;
        if (w->layout_tree) {
            if (w->js) ns_js_set_layout_root(w->js, NULL);
            ns_box_free(w->layout_tree);
            w->layout_tree = NULL;
            ns_selection_clear(&w->selection);
            w->search_active_box = NULL;
        }
        if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
    }
}

#ifdef __linux__
static gboolean
ns_linux_has_gpu_render_node(void)
{
    GDir *dir = g_dir_open("/dev/dri", 0, NULL);
    if (!dir) return FALSE;
    const char *name;
    gboolean found = FALSE;
    while ((name = g_dir_read_name(dir))) {
        if (g_str_has_prefix(name, "renderD") || g_str_has_prefix(name, "card")) {
            found = TRUE;
            break;
        }
    }
    g_dir_close(dir);
    return found;
}
#endif

static void
ns_apply_gsk_renderer_auto(void)
{
#ifdef __linux__
    if (!ns_linux_has_gpu_render_node()) {
        g_setenv("GSK_RENDERER", "cairo", TRUE);
        g_message("no GPU detected; using the software (cairo) renderer");
    }
#endif
}

static void
ns_probe_gl_or_fallback(void)
{
#ifdef __linux__
    static gboolean probed;
    if (probed) return;
    probed = TRUE;
    if (g_getenv("GSK_RENDERER")) return;
    GdkDisplay *display = gdk_display_get_default();
    if (!display) return;
    GError *err = NULL;
    GdkGLContext *gl = gdk_display_create_gl_context(display, &err);
    if (gl && gdk_gl_context_realize(gl, &err)) {
        g_object_unref(gl);
        return;
    }
    g_clear_object(&gl);
    g_message("OpenGL init failed (%s); using the software (cairo) renderer",
              err && err->message ? err->message : "no usable driver");
    g_clear_error(&err);
    g_setenv("GSK_RENDERER", "cairo", TRUE);
#endif
}

static void
ns_apply_gsk_renderer(const char *pref)
{
    if (g_getenv("GSK_RENDERER")) return;
    if (!pref || !*pref ||
        g_ascii_strcasecmp(pref, "auto")    == 0 ||
        g_ascii_strcasecmp(pref, "default") == 0 ||
        g_ascii_strcasecmp(pref, "system")  == 0) {
        ns_apply_gsk_renderer_auto();
        return;
    }
    static const char *const known[] = {
        "gl", "ngl", "opengl", "vulkan", "cairo", "help",
    };
    for (gsize i = 0; i < G_N_ELEMENTS(known); i++) {
        if (g_ascii_strcasecmp(pref, known[i]) == 0) {
            g_setenv("GSK_RENDERER", known[i], TRUE);
            return;
        }
    }
    g_warning("ignoring unknown gsk_renderer '%s' "
              "(expected one of: auto, gl, ngl, vulkan, cairo)", pref);
}

int
main(int argc, char **argv)
{
#ifdef G_OS_WIN32
    if (ns_win32_args_need_console(argc, argv))
        ns_win32_attach_parent_console();
#endif
    if (!ns_security_refuse_root()) return 77;
    init_self_exe(argc > 0 ? argv[0] : NULL);
    ns_config_init();
    if (ns_watchdog_should_supervise(argc, argv,
                                     ns_config_get()->watchdog_enabled)) {
        int rc = ns_watchdog_run_supervisor(g_self_exe, argc, argv);
        ns_config_shutdown();
        return rc;
    }
    ns_security_win32_mitigations_init();
    for (int i = 1; i < argc; i++) {
        const char *p = NULL;
        if (g_str_has_prefix(argv[i], "--dump=")) {
            const char *v = argv[i] + 7;
            if      (g_str_has_prefix(v, "png:")) p = v + 4;
            else if (g_str_has_prefix(v, "pdf:")) p = v + 4;
        } else if (g_str_has_prefix(argv[i], "--screenshot=")) {
            p = argv[i] + 13;
        }
        if (!p || !*p || *p == '-') continue;
        char *dir = g_path_get_dirname(p);
        if (dir && *dir) {
            g_mkdir_with_parents(dir, 0700);
            ns_security_add_writable_dir(dir);
        }
        g_free(dir);
    }
    ns_media_broker_start();
    ns_media_set_open_uri_handler(ns_media_open_uri_cb);
    ns_security_sandbox_init(g_self_exe);
    ns_security_seccomp_init();
    ns_debug_log_init();
    g_log_set_writer_func(ns_log_writer, NULL, NULL);
#ifdef G_OS_WIN32
    ns_win32_set_app_id();
    ns_win32_anchor_gtk_data();
#endif
#ifdef __APPLE__
    ns_macos_anchor_gtk_data();
#endif

    gboolean headless = FALSE;
    gboolean dump_set = FALSE;
    const char *proxy_override = NULL;
    const char *gsk_renderer_override = NULL;
    ns_headless_opts hopts = {
        .url = NULL,
        .dump = NS_DUMP_TEXT,
        .out_path = NULL,
        .viewport_width = 1000,
        .settle_ms = 200,
        .time_ms = 1000,
    };
    for (int i = 1; i < argc; i++) {
        if (g_str_has_prefix(argv[i], "--proxy=")) {
            proxy_override = argv[i] + 8;
            ns_net_set_proxy_override(proxy_override);
            continue;
        }
        if (g_str_has_prefix(argv[i], "--gsk-renderer=")) {
            gsk_renderer_override = argv[i] + 15;
            continue;
        }
        if (g_strcmp0(argv[i], "--print-config") == 0) {
            char *dump = ns_config_dump();
            fputs(dump, stdout);
            g_free(dump);
            ns_config_shutdown();
            return 0;
        }
        if (g_strcmp0(argv[i], "--headless") == 0) {
            headless = TRUE;
        } else if (g_str_has_prefix(argv[i], "--dump=")) {
            const char *v = argv[i] + 7;
            dump_set = TRUE;
            if      (g_strcmp0(v, "text")   == 0) hopts.dump = NS_DUMP_TEXT;
            else if (g_strcmp0(v, "dom")    == 0) hopts.dump = NS_DUMP_DOM;
            else if (g_strcmp0(v, "layout") == 0) hopts.dump = NS_DUMP_LAYOUT;
            else if (g_str_has_prefix(v, "png:"))   { hopts.dump = NS_DUMP_PNG; hopts.out_path = v + 4; }
            else if (g_str_has_prefix(v, "pdf:"))   { hopts.dump = NS_DUMP_PDF; hopts.out_path = v + 4; }
        } else if (g_str_has_prefix(argv[i], "--viewport=")) {
            char *end = NULL;
            gint64 n = g_ascii_strtoll(argv[i] + 11, &end, 10);
            if (end != argv[i] + 11 && *end == '\0' && n > 0 && n < 100000)
                hopts.viewport_width = (int)n;
        } else if (g_str_has_prefix(argv[i], "--viewport-height=")) {
            char *end = NULL;
            gint64 n = g_ascii_strtoll(argv[i] + 18, &end, 10);
            if (end != argv[i] + 18 && *end == '\0' && n > 0 && n < 100000)
                hopts.viewport_height = (int)n;
        } else if (g_str_has_prefix(argv[i], "--settle-ms=")) {
            char *end = NULL;
            gint64 n = g_ascii_strtoll(argv[i] + 12, &end, 10);
            if (end != argv[i] + 12 && *end == '\0' && n >= 0 && n < 600000)
                hopts.settle_ms = (int)n;
        } else if (g_str_has_prefix(argv[i], "--time-ms=")) {
            char *end = NULL;
            gint64 n = g_ascii_strtoll(argv[i] + 10, &end, 10);
            if (end != argv[i] + 10 && *end == '\0' && n >= 0 && n < 600000)
                hopts.time_ms = (int)n;
        } else if (g_str_has_prefix(argv[i], "--act=")) {
            hopts.actions = argv[i] + 6;
        } else if (g_str_has_prefix(argv[i], "--eval=")) {
            hopts.eval = argv[i] + 7;
        } else if (g_str_has_prefix(argv[i], "--inspect=")) {
            hopts.inspect = argv[i] + 10;
        } else if (g_str_has_prefix(argv[i], "--inspect-at=")) {
            hopts.inspect_at = argv[i] + 13;
        } else if (g_strcmp0(argv[i], "--debug") == 0) {
            hopts.debug_levels = ns_headless_debug_mask("all");
        } else if (g_str_has_prefix(argv[i], "--debug=")) {
            hopts.debug_levels = ns_headless_debug_mask(argv[i] + 8);
        } else if (g_str_has_prefix(argv[i], "--url=")) {
            hopts.url = argv[i] + 6;
        } else if (argv[i][0] != '-' && !hopts.url) {
            hopts.url = argv[i];
        }
    }
    if (!dump_set && (hopts.inspect || hopts.inspect_at))
        hopts.dump = NS_DUMP_NONE;
    if (headless) {
        ns_net_init();
        ns_net_set_allow_file_urls(TRUE);
        if (hopts.debug_levels & (1u << NS_DLOG_NET))
            ns_net_set_log_fetches(TRUE);
        ns_cache_init();
        ns_bcache_init();
        ns_history_init();
        ns_font_init();
        char *file_url = NULL;
        if (hopts.url && !strstr(hopts.url, "://") &&
            !g_str_has_prefix(hopts.url, "about:") &&
            !g_str_has_prefix(hopts.url, "data:") &&
            g_file_test(hopts.url, G_FILE_TEST_EXISTS)) {
            char *abs = g_canonicalize_filename(hopts.url, NULL);
            file_url = g_filename_to_uri(abs, NULL, NULL);
            g_free(abs);
            if (file_url) hopts.url = file_url;
        }
        int rc = ns_headless_run(&hopts);
        g_free(file_url);
        ns_bcache_shutdown();
        ns_history_shutdown();
        ns_cache_shutdown();
        ns_net_shutdown();
        ns_config_shutdown();
        return rc;
    }

    {
        const ns_config *cfg = ns_config_get();
        g_home_url = g_strdup(cfg && cfg->home_url ? cfg->home_url : "");
        ns_apply_gsk_renderer(gsk_renderer_override ? gsk_renderer_override
                              : (cfg ? cfg->gsk_renderer : NULL));
    }
    ns_net_init();
    ns_cache_init();
    ns_bcache_init();
    ns_history_init();
    ns_font_init();
    g_bookmarks = ns_bookmarks_load();

    GApplicationFlags app_flags = G_APPLICATION_HANDLES_COMMAND_LINE |
                                  G_APPLICATION_NON_UNIQUE;
    GtkApplication *app = gtk_application_new(NS_APP_ID, app_flags);
    ns_font_set_loaded_cb(ns_main_on_font_loaded, app);
    g_signal_connect(app, "startup",      G_CALLBACK(ns_install_actions), NULL);
    g_signal_connect(app, "activate",     G_CALLBACK(on_activate), NULL);
    g_signal_connect(app, "command-line", G_CALLBACK(ns_on_command_line), NULL);
    if (ns_watchdog_is_child(argc, argv)) {
        ns_watchdog_child_guard_parent_death();
        ns_watchdog_child_arm_hang_monitor(ns_config_get()->js_eval_budget_ms);
#ifndef G_OS_WIN32
        g_unix_signal_add(SIGTERM, ns_on_quit_signal, app);
        g_unix_signal_add(SIGINT, ns_on_quit_signal, app);
#endif
    }
    g_watchdog_session_path = g_strdup(ns_watchdog_child_session_arg(argc, argv));
    if (g_watchdog_session_path && *g_watchdog_session_path)
        g_timeout_add_seconds(NS_SESSION_WRITE_SECS, ns_session_write_cb, app);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    g_clear_object(&g_bookmarks_monitor);
    ns_bookmarks_free(g_bookmarks);
    g_bookmarks = NULL;
    g_free(g_startup_url_override);
    g_free(g_self_exe);
    g_self_exe = NULL;
    g_free(g_home_url);
    g_home_url = NULL;
    g_free(g_watchdog_session_path);
    g_watchdog_session_path = NULL;
    ns_font_shutdown();
    ns_bcache_shutdown();
    ns_history_shutdown();
    ns_cache_shutdown();
    ns_net_shutdown();
    ns_config_shutdown();
    return status;
}
