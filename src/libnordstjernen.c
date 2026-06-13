/* Nordstjernen — public C embedding API implementation.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "libnordstjernen.h"

#include <cairo.h>
#include <gio/gio.h>
#include <glib.h>
#include <stdint.h>
#include <string.h>

#include "anim.h"
#include "bcache.h"
#include "cache.h"
#include "history.h"
#include "config.h"
#include "css.h"
#include "dom.h"
#include "engine.h"
#include "font.h"
#include "forms.h"
#include "html.h"
#include "image.h"
#include "js.h"
#include "layout.h"
#include "net.h"
#include "paint.h"
#include "render.h"
#include "security.h"
#include "selection.h"
#include "webgl.h"

struct ns_browser {
    ns_node        *doc;
    ns_box         *layout;
    GHashTable     *styles;
    ns_js          *js;
    ns_anim        *anim;
    ns_image_cache *images;
    GHashTable     *css_cache;
    char           *base_url;
    char           *doc_charset;
    int             vw;
    double          vh;
    gboolean        images_fetched;
    GPtrArray      *img_sessions;
    GHashTable     *img_requested;
    gboolean        dirty;
    gboolean        relaying;
    char           *pending_nav;
    char           *refresh_url;
    gint64          refresh_due_us;
    char           *pending_post_body;
    gsize           pending_post_len;
    char           *pending_post_ct;
    gsize           caret_byte;
    gsize           sel_anchor_byte;
    ns_selection    selection;
    const ns_node  *hover_node;
    const ns_node  *press_node;
    int             press_x;
    int             press_y;
    int             press_mods;
    gboolean        press_active;
    char           *search_query;
    gboolean        search_case;
    const ns_box   *search_active;
    GString        *console_buf;
    guint64         layout_sig[2];
    int             layout_osc;
    gint64          last_layout_us;
    gint64          damp_until_us;
    gboolean        damp_logged;
    gint64          hover_relayout_us;
    gint64          relayout_cost_us;
    gboolean        hover_restyle_pending;
};

#define NS_LAYOUT_OSC_THRESHOLD 6
#define NS_LAYOUT_RAPID_US (100 * 1000)
#define NS_LAYOUT_DAMP_US (700 * 1000)

static guint64
layout_signature_walk(const ns_box *b, guint64 h)
{
    if (!b) return h;
    gint32 q[4] = {
        (gint32)(b->x * 4), (gint32)(b->y * 4),
        (gint32)(b->content_width * 4), (gint32)(b->content_height * 4),
    };
    const guchar *bytes = (const guchar *)q;
    h ^= (guint64)b->kind;
    h *= 0x100000001b3ULL;
    for (gsize i = 0; i < sizeof q; i++) {
        h ^= bytes[i];
        h *= 0x100000001b3ULL;
    }
    for (const ns_box *c = b->first_child; c; c = c->next_sibling)
        h = layout_signature_walk(c, h);
    if (b->inline_atomics)
        for (guint i = 0; i < b->inline_atomics->len; i++)
            h = layout_signature_walk(
                g_array_index(b->inline_atomics, ns_inline_atomic, i).box, h);
    return h;
}

static guint64
layout_signature(const ns_box *root)
{
    return layout_signature_walk(root, 0xcbf29ce484222325ULL);
}

static void
browser_damp_reset(ns_browser *b)
{
    b->layout_osc = 0;
    b->damp_until_us = 0;
    b->damp_logged = FALSE;
}

static void
browser_relayout(ns_browser *b)
{
    if (b->relaying) { b->dirty = TRUE; return; }
    b->relaying = TRUE;
    ns_css_set_viewport((double)b->vw, b->vh);
    if (b->js) ns_js_sync_window_metrics(b->js);
    b->search_active = NULL;
    ns_selection_clear(&b->selection);
    if (b->js && b->layout) ns_js_set_layout_root(b->js, NULL);
    if (b->layout) { ns_paint_3d_invalidate(); ns_box_free(b->layout); b->layout = NULL; }
    if (b->js && b->styles) ns_js_set_style_table(b->js, NULL);
    if (b->styles) { g_hash_table_destroy(b->styles); b->styles = NULL; }
    gint64 relayout_t0 = g_get_monotonic_time();
    b->styles = ns_engine_relayout(b->doc, b->base_url, b->vw, b->vh,
                                   b->images, b->anim, b->js,
                                   b->css_cache,
                                   b->js ? ns_js_focused_node(b->js) : NULL,
                                   b->hover_node,
                                   b->caret_byte, b->sel_anchor_byte,
                                   &b->layout);
    b->relayout_cost_us = g_get_monotonic_time() - relayout_t0;
    b->relaying = FALSE;
    b->images_fetched = FALSE;

    gint64 now = g_get_monotonic_time();
    gboolean rapid = now - b->last_layout_us < NS_LAYOUT_RAPID_US;
    b->last_layout_us = now;
    guint64 sig = layout_signature(b->layout);
    if (sig == b->layout_sig[0] || sig == b->layout_sig[1]) {
        if (rapid && b->layout_osc < G_MAXINT) b->layout_osc++;
    } else {
        browser_damp_reset(b);
    }
    b->layout_sig[1] = b->layout_sig[0];
    b->layout_sig[0] = sig;
}

static gboolean
browser_relayout_from_mutation(ns_browser *b)
{
    if (b->layout && b->layout_osc >= NS_LAYOUT_OSC_THRESHOLD) {
        gint64 now = g_get_monotonic_time();
        if (now < b->damp_until_us) {
            b->dirty = TRUE;
            return FALSE;
        }
        b->damp_until_us = now + NS_LAYOUT_DAMP_US;
        if (!b->damp_logged) {
            b->damp_logged = TRUE;
            g_message("nordstjernen: layout dampener engaged "
                      "(script reflow loop with no user input)");
        }
    }
    browser_relayout(b);
    return TRUE;
}

static void
walk_max_bottom(const ns_box *b, double *out)
{
    if (!b) return;
    double bottom = b->y + b->content_height;
    if (bottom > *out) *out = bottom;
    for (const ns_box *c = b->first_child; c; c = c->next_sibling)
        walk_max_bottom(c, out);
}

static gboolean
overflow_keyword_hidden(const char *kw)
{
    return kw && (g_ascii_strcasecmp(kw, "hidden") == 0 ||
                  g_ascii_strcasecmp(kw, "clip") == 0);
}

static gboolean
box_axis_overflow_hidden(const ns_box *b, ns_css_prop axis)
{
    if (!b || !b->style) return FALSE;
    const char *kw = ns_style_keyword(b->style, axis);
    if (!kw) kw = ns_style_keyword(b->style, NS_CSS_OVERFLOW);
    return overflow_keyword_hidden(kw);
}

static gboolean
root_axis_overflow_hidden(const ns_box *b, ns_css_prop axis)
{
    if (!b) return FALSE;
    if (b->dom && b->dom->name &&
        (strcmp(b->dom->name, "html") == 0 ||
         strcmp(b->dom->name, "body") == 0) &&
        box_axis_overflow_hidden(b, axis))
        return TRUE;
    for (const ns_box *c = b->first_child; c; c = c->next_sibling)
        if (root_axis_overflow_hidden(c, axis)) return TRUE;
    return FALSE;
}

static void
browser_image_arrived(gpointer user_data)
{
    ns_browser *b = user_data;
    if (b) b->dirty = TRUE;
}

static int
browser_images_outstanding(ns_browser *browser)
{
    if (!browser->img_sessions) return 0;
    int total = 0;
    for (guint i = 0; i < browser->img_sessions->len; ) {
        ns_engine_img_session *s = g_ptr_array_index(browser->img_sessions, i);
        int o = ns_engine_img_session_outstanding(s);
        if (o == 0) {
            ns_engine_img_session_close(s);
            g_ptr_array_remove_index_fast(browser->img_sessions, i);
            continue;
        }
        total += o;
        i++;
    }
    return total;
}

static void
browser_ensure_images(ns_browser *browser)
{
    if (browser->images_fetched) return;
    browser->images_fetched = TRUE;
    if (!browser->img_requested)
        browser->img_requested = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                       g_free, NULL);
    if (!browser->img_sessions)
        browser->img_sessions = g_ptr_array_new();
    ns_engine_img_session *s =
        ns_engine_fetch_images_start(browser->layout, browser->base_url,
                                     browser->images, browser->img_requested,
                                     browser_image_arrived, browser);
    if (s) g_ptr_array_add(browser->img_sessions, s);
}

static void
browser_wait_images(ns_browser *browser)
{
    browser_ensure_images(browser);
    gint64 deadline = g_get_monotonic_time() + (gint64)15 * G_USEC_PER_SEC;
    while (browser_images_outstanding(browser) > 0 &&
           g_get_monotonic_time() < deadline)
        g_main_context_iteration(NULL, TRUE);
    if (browser->dirty) {
        browser_relayout(browser);
        browser->dirty = FALSE;
    }
}

static void
browser_flush(gpointer user_data)
{
    ns_browser *b = user_data;
    if (!b || !b->js) return;
    if (!b->layout || b->dirty || ns_js_consume_mutated(b->js)) {
        if (browser_relayout_from_mutation(b))
            b->dirty = FALSE;
    }
}

static gboolean
settle_quit_cb(gpointer user_data)
{
    g_main_loop_quit(user_data);
    return G_SOURCE_CONTINUE;
}

typedef struct settle_ctx {
    ns_browser *b;
    GMainLoop  *loop;
    int         quiet_ticks;
} settle_ctx;

#define NS_SETTLE_QUIET_TICKS 3

static gboolean
browser_settle_quiet(ns_browser *b)
{
    if (b->dirty) return FALSE;
    if (b->js && ns_js_has_pending_work(b->js)) return FALSE;
    if (b->js && ns_js_has_pending_animation_frame(b->js)) return FALSE;
    if (b->images && ns_image_cache_has_pending(b->images)) return FALSE;
    if (g_main_context_pending(NULL)) return FALSE;
    return TRUE;
}

static gboolean
settle_tick_cb(gpointer user_data)
{
    settle_ctx *ctx = user_data;
    ns_browser *b = ctx->b;
    gint64 now = g_get_monotonic_time();
    if (b->images) ns_image_cache_tick(b->images, now);
    if (b->anim) ns_anim_tick(b->anim, now);
    if (b->anim && b->js) ns_js_dispatch_anim_events(b->js, b->anim);
    if (b->js) ns_js_run_animation_frame(b->js);
    if (b->dirty || (b->js && ns_js_consume_mutated(b->js))) {
        if (browser_relayout_from_mutation(b))
            b->dirty = FALSE;
    }
    if (browser_settle_quiet(b)) {
        if (++ctx->quiet_ticks >= NS_SETTLE_QUIET_TICKS) {
            g_main_loop_quit(ctx->loop);
            return G_SOURCE_CONTINUE;
        }
    } else {
        ctx->quiet_ticks = 0;
    }
    return G_SOURCE_CONTINUE;
}

static void
browser_settle(ns_browser *b, int settle_ms)
{
    if (settle_ms <= 0) return;
    if (browser_settle_quiet(b)) return;
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    settle_ctx ctx = { .b = b, .loop = loop };
    guint quit = g_timeout_add(settle_ms, settle_quit_cb, loop);
    guint tick = g_timeout_add(16, settle_tick_cb, &ctx);
    g_main_loop_run(loop);
    g_source_remove(tick);
    g_source_remove(quit);
    g_main_loop_unref(loop);
}

static void
browser_js_log(const char *line, gpointer ud)
{
    ns_browser *b = ud;
    if (!b || !line) return;
    if (!b->console_buf) b->console_buf = g_string_new(NULL);
    if (b->console_buf->len > 256u * 1024u)
        g_string_erase(b->console_buf, 0,
                       (gssize)(b->console_buf->len - 192u * 1024u));
    g_string_append(b->console_buf, line);
    g_string_append_c(b->console_buf, '\n');
}
static void browser_js_mutated(gpointer ud) { ns_browser *b = ud; if (b) b->dirty = TRUE; }

static gboolean
browser_allows_navigation_url(ns_browser *b, const char *url)
{
    if (!url || !g_str_has_prefix(url, "file:")) return TRUE;
    return b && b->base_url && g_str_has_prefix(b->base_url, "file:");
}

static char *
browser_resolve_navigation(ns_browser *b, const char *href)
{
    if (!b || !href) return NULL;
    char *abs = ns_url_resolve(b->base_url, href);
    if (!browser_allows_navigation_url(b, abs)) {
        g_free(abs);
        return NULL;
    }
    return abs;
}

static void browser_js_navigate(const char *url, gboolean reload, gpointer ud)
{
    (void)reload;
    ns_browser *b = ud;
    if (!b || !url || !*url) return;
    g_free(b->pending_nav);
    b->pending_nav = browser_resolve_navigation(b, url);
}

int
ns_browser_init(void)
{
    ns_config_init();
    ns_net_init();
    ns_net_set_allow_file_urls(TRUE);
    ns_cache_init();
    ns_bcache_init();
    ns_history_init();
    ns_font_init();
    return 0;
}

void
ns_browser_sandbox(const char *self_exe)
{
    ns_security_win32_mitigations_init(FALSE);
    ns_security_sandbox_init(self_exe);
    ns_security_seccomp_init();
}

void
ns_browser_shutdown(void)
{
    ns_font_shutdown();
    ns_bcache_shutdown();
    ns_history_shutdown();
    ns_cache_shutdown();
    ns_net_shutdown();
    ns_config_shutdown();
}

static char *
resolve_local_path(const char *url)
{
    if (!url || strstr(url, "://") ||
        g_str_has_prefix(url, "about:") || g_str_has_prefix(url, "data:") ||
        !g_file_test(url, G_FILE_TEST_EXISTS))
        return NULL;
    char *abs = g_canonicalize_filename(url, NULL);
    char *file_url = g_filename_to_uri(abs, NULL, NULL);
    g_free(abs);
    return file_url;
}

static const char *
browser_find_meta_refresh(const ns_node *n)
{
    if (!n) return NULL;
    if (ns_node_is_element_named(n, "meta")) {
        const char *equiv = ns_element_get_attr(n, "http-equiv");
        if (equiv && g_ascii_strcasecmp(equiv, "refresh") == 0) {
            const char *content = ns_element_get_attr(n, "content");
            if (content && *content) return content;
        }
    }
    for (const ns_node *c = n->first_child; c; c = c->next_sibling) {
        const char *found = browser_find_meta_refresh(c);
        if (found) return found;
    }
    return NULL;
}

static void
browser_arm_declarative_refresh(ns_browser *b, const char *header_value)
{
    double seconds = 0.0;
    char *target = NULL;
    gboolean armed = header_value &&
        ns_net_parse_refresh(header_value, &seconds, &target);
    if (!armed) {
        const char *meta = browser_find_meta_refresh(b->doc);
        armed = meta && ns_net_parse_refresh(meta, &seconds, &target);
    }
    if (!armed) return;
    if (target) {
        b->refresh_url = browser_resolve_navigation(b, target);
        g_free(target);
        if (!b->refresh_url) return;
    } else {
        b->refresh_url = g_strdup(b->base_url);
    }
    b->refresh_due_us = g_get_monotonic_time() + (gint64)(seconds * 1e6);
}

static gboolean
browser_content_type_starts(const char *content_type, const char *prefix)
{
    return content_type && prefix &&
        g_ascii_strncasecmp(content_type, prefix, strlen(prefix)) == 0;
}

static gboolean
browser_content_type_is_html(const char *content_type)
{
    return browser_content_type_starts(content_type, "text/html") ||
           browser_content_type_starts(content_type, "application/xhtml");
}

static char *
browser_text_document(const char *url, const char *text)
{
    char *esc_url = ns_html_escape_text(url && *url ? url : "text file");
    char *esc_text = ns_html_escape_text(text ? text : "");
    char *html = g_strconcat(
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>",
        esc_url,
        "</title><style>"
        "body{margin:0;background:#fff;color:#111}"
        "pre{margin:0;padding:12px;font:13px/1.45 ui-monospace,"
        "\"SF Mono\",Menlo,Consolas,monospace;white-space:pre-wrap;"
        "overflow-wrap:anywhere}"
        "</style></head><body><pre>",
        esc_text,
        "</pre></body></html>",
        NULL);
    g_free(esc_url);
    g_free(esc_text);
    return html;
}

static void
browser_prepare_document_response(ns_response *resp)
{
    if (!resp || !resp->body || !resp->content_type)
        return;
    const char *final_url = resp->final_url ? resp->final_url : "";
    char *html = NULL;
    if (browser_content_type_starts(resp->content_type, "image/")) {
        html = ns_html_image_document(final_url);
    } else if (browser_content_type_starts(resp->content_type, "text/") &&
               !browser_content_type_is_html(resp->content_type)) {
        char *decoded = ns_html_decode_body_full((const char *)resp->body->data,
                                                 resp->body->len,
                                                 resp->content_type, NULL);
        html = browser_text_document(final_url, decoded);
        g_free(decoded);
    }
    if (!html)
        return;
    g_byte_array_set_size(resp->body, 0);
    g_byte_array_append(resp->body, (const guint8 *)html, strlen(html));
    g_free(html);
    g_free(resp->content_type);
    resp->content_type = g_strdup("text/html; charset=utf-8");
}

static ns_browser *
browser_open_common(const char *url, int viewport_width, double viewport_height,
                    int settle_ms,
                    const void *body, size_t body_len, const char *content_type)
{
    if (!url || !*url) return NULL;

    char *file_url = resolve_local_path(url);
    const char *fetch_url = file_url ? file_url : url;

    GError *err = NULL;
    ns_response *resp = body
        ? ns_engine_post_blocking(fetch_url, NULL, body, body_len,
                                  content_type, &err)
        : ns_engine_fetch_blocking(fetch_url, NULL, &err);
    if (!resp || resp->error || !resp->body) {
        if (resp) ns_response_free(resp);
        g_clear_error(&err);
        g_free(file_url);
        return NULL;
    }
    g_clear_error(&err);

    char *base = g_strdup(resp->final_url ? resp->final_url : fetch_url);
    char *refresh_hdr = g_strdup(resp->refresh);
    g_free(file_url);
    browser_prepare_document_response(resp);

    char *doc_charset = NULL;
    char *decoded = ns_html_decode_body_full((const char *)resp->body->data,
                                             resp->body->len,
                                             resp->content_type,
                                             &doc_charset);
    ns_node *doc = ns_html_parse(decoded ? decoded : "",
                                 decoded ? (gssize)strlen(decoded) : 0);
    g_free(decoded);
    ns_response_free(resp);

    int vw = viewport_width > 0 ? viewport_width : 1000;
    double vh = viewport_height > 0.0
        ? viewport_height
        : (double)vw * 0.75;
    ns_css_set_viewport((double)vw, vh);
    const char *frag = strchr(url, '#');
    ns_css_set_target_fragment(frag && *(frag + 1) ? frag + 1 : NULL);

    ns_browser *b = g_new0(ns_browser, 1);
    b->doc = doc;
    b->doc_charset = doc_charset;
    b->base_url = base;
    b->vw = vw;
    b->vh = vh;
    b->css_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                         (GDestroyNotify)g_bytes_unref);
    b->images = ns_image_cache_new();
    b->styles = ns_engine_compute_cascade(doc, base, b->css_cache);

    b->anim = ns_anim_new();
    ns_engine_load_keyframes(b->anim, doc, base, b->css_cache);
    ns_engine_anim_observe(b->anim, b->styles, g_get_monotonic_time());

    b->js = ns_js_new(browser_js_log, b,
                      browser_js_mutated, b,
                      browser_js_navigate, b);
    if (b->js) {
        ns_js_set_style_table(b->js, b->styles);
        ns_js_set_image_cache(b->js, b->images);
        ns_js_set_layout_flush_cb(b->js, browser_flush, b);
        ns_js_run_scripts_in_doc(b->js, doc, base);
    }

    browser_arm_declarative_refresh(b, refresh_hdr);
    g_free(refresh_hdr);

    browser_settle(b, settle_ms);
    if (!b->layout || b->dirty)
        browser_relayout(b);
    return b;
}

ns_browser *
ns_browser_open(const char *url, int viewport_width, int settle_ms)
{
    return browser_open_common(url, viewport_width, 0.0, settle_ms,
                               NULL, 0, NULL);
}

ns_browser *
ns_browser_open_viewport(const char *url, int viewport_width,
                         double viewport_height, int settle_ms)
{
    return browser_open_common(url, viewport_width, viewport_height, settle_ms,
                               NULL, 0, NULL);
}

ns_browser *
ns_browser_open_post(const char *url, int viewport_width, int settle_ms,
                     const void *body, size_t body_len,
                     const char *content_type)
{
    return browser_open_common(url, viewport_width, 0.0, settle_ms,
                               body, body_len, content_type);
}

ns_browser *
ns_browser_open_post_viewport(const char *url, int viewport_width,
                              double viewport_height, int settle_ms,
                              const void *body, size_t body_len,
                              const char *content_type)
{
    return browser_open_common(url, viewport_width, viewport_height, settle_ms,
                               body, body_len, content_type);
}

char *
ns_browser_render_text(ns_browser *browser)
{
    if (!browser || !browser->layout) return NULL;
    GString *out = g_string_new(NULL);
    ns_engine_dump_text(browser->layout, out);
    char *text = malloc(out->len + 1);
    if (text) {
        memcpy(text, out->str, out->len);
        text[out->len] = '\0';
    }
    g_string_free(out, TRUE);
    return text;
}

int
ns_browser_render_image(ns_browser *browser, const char *path)
{
    if (!browser || !browser->layout || !path) return -1;

    browser_wait_images(browser);

    ns_paint_set_js(browser->js);
    ns_paint_set_anim(browser->anim);

    int rc;
    gsize len = strlen(path);
    if (len >= 4 && g_ascii_strcasecmp(path + len - 4, ".pdf") == 0)
        rc = ns_engine_write_pdf(browser->layout, path);
    else
        rc = ns_engine_write_png(browser->layout, path);

    ns_paint_set_anim(NULL);
    ns_paint_set_js(NULL);
    return rc;
}

int
ns_browser_tick(ns_browser *browser, int budget_ms)
{
    if (!browser) return -1;
    if (budget_ms < 0) budget_ms = 0;

    if (browser->refresh_due_us && !browser->pending_nav &&
        g_get_monotonic_time() >= browser->refresh_due_us) {
        browser->refresh_due_us = 0;
        browser->pending_nav = browser->refresh_url;
        browser->refresh_url = NULL;
    }

    gint64 deadline = g_get_monotonic_time() + (gint64)budget_ms * 1000;
    gboolean changed = FALSE;
    int guard = 0;
    if (browser->hover_restyle_pending) {
        gint64 now = g_get_monotonic_time();
        gint64 min_gap = browser->relayout_cost_us * 2;
        if (min_gap < 60000) min_gap = 60000;
        if (now - browser->hover_relayout_us >= min_gap) {
            browser->hover_restyle_pending = FALSE;
            browser->hover_relayout_us = now;
            browser_relayout(browser);
            browser->dirty = FALSE;
            changed = TRUE;
        }
    }
    for (;;) {
        gint64 now = g_get_monotonic_time();
        if (browser->images && ns_image_cache_tick(browser->images, now))
            changed = TRUE;
        if (browser->anim && ns_anim_tick(browser->anim, now))
            changed = TRUE;
        if (browser->anim && browser->js)
            ns_js_dispatch_anim_events(browser->js, browser->anim);
        if (browser->js && ns_js_run_animation_frame(browser->js))
            changed = TRUE;

        gboolean did_iter = FALSE;
        int it = 0;
        while (g_main_context_pending(NULL) && it++ < 64) {
            g_main_context_iteration(NULL, FALSE);
            did_iter = TRUE;
            changed = TRUE;
        }

        if (browser->dirty ||
            (browser->js && ns_js_consume_mutated(browser->js))) {
            if (browser_relayout_from_mutation(browser)) {
                changed = TRUE;
                browser->dirty = FALSE;
            }
        }

        if (!did_iter) break;
        if (++guard >= 4096) break;
        if (g_get_monotonic_time() >= deadline) break;
    }
    return changed ? 1 : 0;
}

int
ns_browser_animating(ns_browser *browser)
{
    if (!browser) return 0;
    if (browser->dirty) return 1;
    if (browser->hover_restyle_pending) return 1;
    if (browser->refresh_due_us || browser->refresh_url) return 1;
    if (browser_images_outstanding(browser) > 0) return 1;
    if (browser->js && ns_js_has_pending_animation_frame(browser->js))
        return 1;
    if (browser->js && ns_js_has_pending_work(browser->js))
        return 1;
    if (browser->anim && ns_anim_has_active(browser->anim))
        return 1;
    if (browser->images && ns_image_cache_animating(browser->images))
        return 1;
    return 0;
}

int
ns_browser_set_viewport(ns_browser *browser, int css_width, double css_height)
{
    if (!browser || !browser->doc || css_width <= 0) return -1;
    if (css_height <= 0.0) css_height = (double)css_width * 0.75;
    if (css_width == browser->vw && css_height == browser->vh) return 0;
    browser->vw = css_width;
    browser->vh = css_height;
    ns_css_set_viewport((double)browser->vw, browser->vh);
    if (browser->js) {
        ns_js_sync_window_metrics(browser->js);
        ns_js_dispatch_resize(browser->js);
    }
    browser_damp_reset(browser);
    browser_relayout(browser);
    return 0;
}

int
ns_browser_set_viewport_width(ns_browser *browser, int css_width)
{
    return ns_browser_set_viewport(browser, css_width,
                                   (double)css_width * 0.75);
}

int
ns_browser_page_size(ns_browser *browser, int *out_width, int *out_height)
{
    if (!browser || !browser->layout) return -1;
    gboolean hide_x = root_axis_overflow_hidden(browser->layout,
                                                NS_CSS_OVERFLOW_X);
    gboolean hide_y = root_axis_overflow_hidden(browser->layout,
                                                NS_CSS_OVERFLOW_Y);
    double w = hide_x ? browser->vw : browser->layout->content_width;
    if (!(w > 0)) w = browser->vw;
    double bottom = hide_y ? browser->vh : browser->layout->content_height;
    if (!hide_y)
        walk_max_bottom(browser->layout, &bottom);
    if (!(bottom > 0)) bottom = 0;
    if (out_width)  *out_width  = (int)w;
    int ypad = (!hide_y && bottom > browser->vh + 0.5) ? 32 : 0;
    if (out_height) *out_height = (int)bottom + ypad;
    return 0;
}

int
ns_browser_render_rgba(ns_browser *browser, int scroll_x, int scroll_y,
                       int width, int height, double scale,
                       unsigned char *out, int stride)
{
    if (!browser || !browser->layout || !out) return -1;
    if (width <= 0 || height <= 0 || stride < width * 4) return -1;
    if (!(scale > 0)) scale = 1.0;

    browser_ensure_images(browser);

    cairo_surface_t *surf =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        return -1;
    }
    cairo_t *cr = cairo_create(surf);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_clip(cr);
    cairo_scale(cr, scale, scale);
    cairo_translate(cr, -(double)scroll_x, -(double)scroll_y);

    ns_paint_set_js(browser->js);
    ns_paint_set_anim(browser->anim);
    ns_paint_set_search(browser->search_case, browser->search_active);
    const char *highlight = browser->search_query;
    if (ns_selection_has_range(&browser->selection))
        ns_paint_with_selection(cr, browser->layout, highlight,
                                &browser->selection);
    else
        ns_paint(cr, browser->layout, highlight);
    ns_paint_set_search(FALSE, NULL);
    ns_paint_set_anim(NULL);
    ns_paint_set_js(NULL);

    cairo_destroy(cr);
    cairo_surface_flush(surf);

    const unsigned char *src = cairo_image_surface_get_data(surf);
    int src_stride = cairo_image_surface_get_stride(surf);
    for (int y = 0; y < height; y++) {
        const unsigned char *srow = src + (size_t)y * src_stride;
        unsigned char *drow = out + (size_t)y * stride;
        for (int x = 0; x < width; x++) {
            uint32_t px;
            memcpy(&px, srow + x * 4, sizeof px);
            drow[x * 4 + 0] = (unsigned char)((px >> 16) & 0xFF);
            drow[x * 4 + 1] = (unsigned char)((px >> 8) & 0xFF);
            drow[x * 4 + 2] = (unsigned char)(px & 0xFF);
            drow[x * 4 + 3] = (unsigned char)((px >> 24) & 0xFF);
        }
    }
    cairo_surface_destroy(surf);
    return 0;
}

int
ns_browser_render_argb32(ns_browser *browser, int scroll_x, int scroll_y,
                         int width, int height, double scale,
                         unsigned char *out, int stride)
{
    if (!browser || !browser->layout || !out) return -1;
    if (width <= 0 || height <= 0 || stride < width * 4) return -1;
    if (!(scale > 0)) scale = 1.0;

    browser_ensure_images(browser);

    cairo_surface_t *surf =
        cairo_image_surface_create_for_data(out, CAIRO_FORMAT_ARGB32,
                                            width, height, stride);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        return -1;
    }
    cairo_t *cr = cairo_create(surf);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_clip(cr);
    cairo_scale(cr, scale, scale);
    cairo_translate(cr, -(double)scroll_x, -(double)scroll_y);

    ns_paint_set_js(browser->js);
    ns_paint_set_anim(browser->anim);
    ns_paint_set_search(browser->search_case, browser->search_active);
    const char *highlight = browser->search_query;
    if (ns_selection_has_range(&browser->selection))
        ns_paint_with_selection(cr, browser->layout, highlight,
                                &browser->selection);
    else
        ns_paint(cr, browser->layout, highlight);
    ns_paint_set_search(FALSE, NULL);
    ns_paint_set_anim(NULL);
    ns_paint_set_js(NULL);

    cairo_destroy(cr);
    cairo_surface_flush(surf);
    const char *dump_dir = g_getenv("NS_FRAME_DUMP");
    if (dump_dir) {
        static int frame_no;
        if (frame_no % 30 == 0) {
            char *path = g_strdup_printf("%s/frame-%05d.png",
                                         dump_dir, frame_no);
            cairo_surface_write_to_png(surf, path);
            g_free(path);
        }
        frame_no++;
    }
    cairo_surface_destroy(surf);
    return 0;
}

char *
ns_browser_link_at(ns_browser *browser, int x, int y)
{
    if (!browser || !browser->layout) return NULL;

    /* Touch taps are imprecise, so probe the exact point first and then a
     * small ring around it (CSS px) before giving up. */
    static const int kR = 6;
    static const int probe[][2] = {
        { 0, 0 },
        { 0, -kR }, { 0, kR }, { -kR, 0 }, { kR, 0 },
        { -kR, -kR }, { kR, -kR }, { -kR, kR }, { kR, kR },
    };
    for (int i = 0; i < (int)(sizeof probe / sizeof probe[0]); i++) {
        const char *href = ns_box_hit_link(browser->layout,
                                           (double)(x + probe[i][0]),
                                           (double)(y + probe[i][1]));
        if (href && *href) return browser_resolve_navigation(browser, href);
    }
    return NULL;
}

char *
ns_browser_cursor_at(ns_browser *browser, int x, int y)
{
    static const char *const known[] = {
        "default", "none", "context-menu", "help", "pointer", "progress",
        "wait", "cell", "crosshair", "text", "vertical-text", "alias",
        "copy", "move", "no-drop", "not-allowed", "grab", "grabbing",
        "all-scroll", "col-resize", "row-resize", "n-resize", "e-resize",
        "s-resize", "w-resize", "ne-resize", "nw-resize", "se-resize",
        "sw-resize", "ew-resize", "ns-resize", "nesw-resize", "nwse-resize",
        "zoom-in", "zoom-out",
    };
    if (!browser || !browser->layout || !browser->styles) return NULL;

    const ns_box *hit = ns_box_hit_test(browser->layout, (double)x, (double)y);
    const ns_node *node = hit ? hit->dom : NULL;
    const ns_node *inline_node =
        ns_box_hit_inline_dom(browser->layout, (double)x, (double)y);
    if (inline_node) node = inline_node;
    const ns_node *form_node =
        ns_box_hit_form_dom(browser->layout, (double)x, (double)y);
    if (form_node) node = form_node;

    const ns_style *style = NULL;
    for (const ns_node *n = node; n && !style; n = n->parent)
        style = g_hash_table_lookup(browser->styles, n);
    if (!style) return NULL;

    const ns_css_value *v = style->values[NS_CSS_CURSOR];
    char *match = NULL;
    if (v && v->kind == NS_CSS_V_KEYWORD && v->u.keyword) {
        char **tokens = g_strsplit_set(v->u.keyword, ", \t", -1);
        for (int i = 0; tokens && tokens[i]; i++) {
            if (!tokens[i][0]) continue;
            for (gsize k = 0; k < G_N_ELEMENTS(known); k++)
                if (g_ascii_strcasecmp(tokens[i], known[k]) == 0) {
                    g_free(match);
                    match = g_strdup(known[k]);
                }
        }
        g_strfreev(tokens);
    }
    if (match) return match;

    if (ns_box_hit_link(browser->layout, (double)x, (double)y)) return NULL;
    if (form_node)
        return ns_node_is_text_input(form_node) ? g_strdup("text") : NULL;
    for (const ns_node *n = node; n; n = n->parent)
        if (ns_node_is_contenteditable_host(n)) return g_strdup("text");
    if (ns_selection_text_at(browser->layout, (double)x, (double)y))
        return g_strdup("text");
    return NULL;
}

char *
ns_browser_select(ns_browser *browser, int kind, int x, int y)
{
    if (!browser || !browser->layout) return NULL;
    switch (kind) {
    case 0: ns_selection_anchor_at(&browser->selection, browser->layout,
                                   (double)x, (double)y); break;
    case 1: ns_selection_extend_to(&browser->selection, browser->layout,
                                   (double)x, (double)y); break;
    case 2: ns_selection_clear(&browser->selection); break;
    case 3: ns_selection_select_all(&browser->selection, browser->layout);
            break;
    case 4: return ns_selection_collect_text(browser->layout,
                                             &browser->selection);
    default: break;
    }
    return NULL;
}

static void
browser_hover_dispatch(ns_browser *b, const ns_node *target, int x, int y,
                       const char *ptr_type, const char *mouse_type,
                       const ns_node *related)
{
    if (!b->js || !target) return;
    ns_js_dispatch_mouse_event(b->js, target, ptr_type, (double)x, (double)y,
                               (double)x, (double)y, 0, 0,
                               FALSE, FALSE, FALSE, FALSE, related, NULL);
    ns_js_dispatch_mouse_event(b->js, target, mouse_type, (double)x, (double)y,
                               (double)x, (double)y, 0, 0,
                               FALSE, FALSE, FALSE, FALSE, related, NULL);
}

int
ns_browser_hover(ns_browser *browser, int x, int y)
{
    if (!browser || !browser->layout) return -1;

    const ns_box *hit = ns_box_hit_test(browser->layout, (double)x, (double)y);
    const ns_node *node = hit ? hit->dom : NULL;
    const ns_node *inline_node =
        ns_box_hit_inline_dom(browser->layout, (double)x, (double)y);
    if (inline_node) node = inline_node;
    const ns_node *form_node =
        ns_box_hit_form_dom(browser->layout, (double)x, (double)y);
    if (form_node) node = form_node;

    const ns_node *prev = browser->hover_node;
    gboolean changed = node != prev;
    browser->hover_node = node;

    gboolean dirty = FALSE;
    if (browser->js) {
        if (changed) {
            browser_hover_dispatch(browser, prev, x, y, "pointerout",
                                   "mouseout", node);
            browser_hover_dispatch(browser, prev, x, y, "pointerleave",
                                   "mouseleave", node);
            browser_hover_dispatch(browser, node, x, y, "pointerover",
                                   "mouseover", prev);
            browser_hover_dispatch(browser, node, x, y, "pointerenter",
                                   "mouseenter", prev);
        }
        browser_hover_dispatch(browser, node, x, y, "pointermove",
                               "mousemove", NULL);
        if (ns_js_consume_mutated(browser->js)) dirty = TRUE;
    }

    gboolean hover_restyle = changed && ns_render_page_uses_hover() &&
                             !ns_selection_has_range(&browser->selection);
    if (dirty) {
        browser_relayout(browser);
        browser->dirty = FALSE;
        return 1;
    }
    if (hover_restyle) {
        gint64 now = g_get_monotonic_time();
        gint64 min_gap = browser->relayout_cost_us * 2;
        if (min_gap < 60000) min_gap = 60000;
        if (now - browser->hover_relayout_us >= min_gap) {
            browser->hover_relayout_us = now;
            browser->hover_restyle_pending = FALSE;
            browser_relayout(browser);
            browser->dirty = FALSE;
            return 1;
        }
        browser->hover_restyle_pending = TRUE;
    }
    return 0;
}

char *
ns_browser_console_drain(ns_browser *browser)
{
    if (!browser || !browser->console_buf || browser->console_buf->len == 0)
        return NULL;
    char *out = g_strdup(browser->console_buf->str);
    g_string_truncate(browser->console_buf, 0);
    return out;
}

char *
ns_browser_eval(ns_browser *browser, const char *src)
{
    if (!browser || !browser->js || !src) return NULL;
    browser_damp_reset(browser);
    char *res = ns_js_eval_source(browser->js, src, "devtools-console");
    if (ns_js_run_animation_frame(browser->js)) browser->dirty = TRUE;
    if (ns_js_consume_mutated(browser->js)) browser->dirty = TRUE;
    if (browser->dirty) {
        browser_relayout(browser);
        browser->dirty = FALSE;
    }
    return res;
}

char *
ns_browser_media_at(ns_browser *browser, int x, int y, int *out_is_video,
                    int *out_stream)
{
    if (out_is_video) *out_is_video = 0;
    if (out_stream) *out_stream = 0;
    if (!browser || !browser->layout) return NULL;

    const ns_box *hit = ns_box_hit_test(browser->layout, (double)x, (double)y);
    const ns_box *media = NULL;
    for (const ns_box *b = hit; b; b = b->parent) {
        if (b->dom && (ns_node_is_element_named(b->dom, "video") ||
                       ns_node_is_element_named(b->dom, "audio"))) {
            media = b;
            break;
        }
    }
    if (!media || !media->dom) return NULL;

    gboolean is_video = ns_node_is_element_named(media->dom, "video");
    const char *msrc = NULL;
    if (media->media)
        msrc = is_video ? media->media->video_src
                        : media->media->video_audio_src;
    char *abs = msrc ? ns_url_resolve(browser->base_url, msrc) : NULL;
    gboolean stream = !abs || g_str_has_prefix(abs, "blob:") ||
                      g_str_has_prefix(abs, "data:");
    if (stream) {
        g_free(abs);
        abs = browser->base_url ? g_strdup(browser->base_url) : NULL;
    }
    if (!abs) return NULL;
    if (g_str_has_prefix(abs, "file://") &&
        (!browser->base_url || !g_str_has_prefix(browser->base_url, "file://"))) {
        g_free(abs);
        return NULL;
    }
    if (out_is_video) *out_is_video = is_video ? 1 : 0;
    if (out_stream) *out_stream = stream ? 1 : 0;
    return abs;
}

int
ns_browser_find(ns_browser *browser, const char *query, int case_sensitive,
                int direction, int from_y, int *out_total, int *out_current,
                int *out_y)
{
    if (out_total) *out_total = 0;
    if (out_current) *out_current = 0;
    if (out_y) *out_y = 0;
    if (!browser || !browser->layout) return -1;

    gboolean cs = case_sensitive != 0;
    if (!query || !*query) {
        g_clear_pointer(&browser->search_query, g_free);
        browser->search_active = NULL;
        browser->search_case = cs;
        return 0;
    }

    if (!browser->search_query || strcmp(browser->search_query, query) != 0 ||
        browser->search_case != cs) {
        g_free(browser->search_query);
        browser->search_query = g_strdup(query);
        browser->search_active = NULL;
    }
    browser->search_case = cs;

    guint total = ns_box_count_matches(browser->layout, query, cs);
    if (out_total) *out_total = (int)total;
    if (total == 0) {
        browser->search_active = NULL;
        return 0;
    }

    double cur_y = browser->search_active ? browser->search_active->y
                                          : (double)from_y;
    const ns_box *target = NULL;
    if (direction == 2) {
        target = ns_box_first_match_above(browser->layout, query, cur_y, cs);
        if (!target)
            target = ns_box_first_match_above(browser->layout, query,
                                              G_MAXDOUBLE, cs);
    } else if (direction == 1) {
        target = ns_box_first_match_below(browser->layout, query, cur_y + 2,
                                          cs);
        if (!target)
            target = ns_box_first_match_below(browser->layout, query, -1, cs);
    } else {
        target = ns_box_first_match_below(browser->layout, query,
                                          (double)from_y - 1, cs);
        if (!target)
            target = ns_box_first_match_below(browser->layout, query, -1, cs);
    }

    browser->search_active = target;
    if (target) {
        if (out_y) *out_y = (int)target->y;
        if (out_current)
            *out_current = (int)ns_box_match_ordinal(browser->layout, query,
                                                     target, cs);
    }
    return 0;
}

char *
ns_browser_take_post(ns_browser *browser, size_t *out_len, char **out_ct)
{
    if (out_len) *out_len = 0;
    if (out_ct) *out_ct = NULL;
    if (!browser || !browser->pending_post_body) return NULL;
    char *body = browser->pending_post_body;
    browser->pending_post_body = NULL;
    if (out_len) *out_len = browser->pending_post_len;
    browser->pending_post_len = 0;
    if (out_ct) *out_ct = browser->pending_post_ct;
    else        g_free(browser->pending_post_ct);
    browser->pending_post_ct = NULL;
    return body;
}

static void
browser_submit_form(ns_browser *b, const ns_node *clicked)
{
    if (!clicked || !b->doc) return;
    gboolean from_text = ns_node_is_text_input(clicked);
    gboolean from_form = ns_node_is_element_named(clicked, "form");
    if (!from_text && !from_form && !ns_form_is_submit_trigger(clicked))
        return;
    const ns_node *form = from_form ? clicked : ns_form_owner(clicked, b->doc);
    if (!form) return;

    if (!ns_element_get_attr(form, "novalidate") &&
        !ns_element_get_attr(clicked, "formnovalidate")) {
        const ns_node *bad = ns_form_first_invalid(form, b->doc, b->doc);
        if (bad) {
            if (b->js) ns_js_dispatch_event(b->js, bad, "invalid", NULL);
            return;
        }
    }

    if (b->js) {
        gboolean prevented = FALSE;
        ns_js_dispatch_submit_event(b->js, form, clicked, &prevented);
        if (ns_js_consume_mutated(b->js)) b->dirty = TRUE;
        if (prevented) return;
    }

    const char *method = ns_element_get_attr(form, "method");
    const char *formmethod = !from_text ? ns_element_get_attr(clicked,
                                                              "formmethod")
                                        : NULL;
    if (formmethod && *formmethod) method = formmethod;
    gboolean is_post = method && g_ascii_strcasecmp(method, "post") == 0;

    const char *action = ns_element_get_attr(form, "action");
    const char *formaction = !from_text ? ns_element_get_attr(clicked,
                                                              "formaction")
                                        : NULL;
    if (formaction && *formaction) action = formaction;
    char *abs_action = (action && *action) ? ns_url_resolve(b->base_url, action)
                                           : g_strdup(b->base_url);
    if (!abs_action) return;
    if (!browser_allows_navigation_url(b, abs_action)) {
        g_free(abs_action);
        return;
    }

    const char *accept_charset = ns_element_get_attr(form, "accept-charset");
    ns_form_set_submission_charset(
        (accept_charset && *accept_charset) ? accept_charset
                                            : b->doc_charset);

    if (is_post) {
        GString *body = g_string_new(NULL);
        gboolean first = TRUE;
        ns_form_collect_inputs(form, b->doc, b->doc, body, &first, clicked);
        ns_form_set_submission_charset(NULL);
        g_free(b->pending_post_body);
        g_free(b->pending_post_ct);
        b->pending_post_len = body->len;
        b->pending_post_body = g_string_free(body, FALSE);
        b->pending_post_ct = g_strdup("application/x-www-form-urlencoded");
        g_free(b->pending_nav);
        b->pending_nav = abs_action;
        return;
    }

    GString *query = g_string_new(NULL);
    gboolean first = TRUE;
    ns_form_collect_inputs(form, b->doc, b->doc, query, &first, clicked);
    ns_form_set_submission_charset(NULL);

    char *frag = strchr(abs_action, '#');
    if (frag) *frag = '\0';
    char *full;
    if (query->len == 0) {
        full = g_strdup(abs_action);
    } else {
        full = strchr(abs_action, '?')
            ? g_strdup_printf("%s&%s", abs_action, query->str)
            : g_strdup_printf("%s?%s", abs_action, query->str);
    }
    g_string_free(query, TRUE);
    g_free(abs_action);
    g_free(b->pending_nav);
    b->pending_nav = full;
}

static const ns_node *
browser_hit_node(ns_browser *browser, int x, int y)
{
    const ns_box *hit = ns_box_hit_test(browser->layout, (double)x, (double)y);
    const ns_node *node = hit ? hit->dom : NULL;
    const ns_node *inline_node =
        ns_box_hit_inline_dom(browser->layout, (double)x, (double)y);
    if (inline_node)
        node = inline_node;
    const ns_node *form_node =
        ns_box_hit_form_dom(browser->layout, (double)x, (double)y);
    if (form_node)
        node = form_node;
    return node;
}

char *
ns_browser_press(ns_browser *browser, int x, int y, int mods)
{
    if (!browser || !browser->layout) return NULL;
    browser_damp_reset(browser);
    g_clear_pointer(&browser->pending_nav, g_free);
    ns_selection_clear(&browser->selection);

    const ns_node *node = browser_hit_node(browser, x, y);
    browser->press_node = node;
    browser->press_x = x;
    browser->press_y = y;
    browser->press_mods = mods;
    browser->press_active = node != NULL;

    ns_css_set_active_node(node);
    if (node && ns_render_page_uses_active()) {
        browser_relayout(browser);
        browser->dirty = FALSE;
    }

    if (browser->js && node) {
        gboolean sh = (mods & 1) != 0, ct = (mods & 2) != 0;
        gboolean al = (mods & 4) != 0, me = (mods & 8) != 0;
        ns_js_dispatch_mouse_event(browser->js, node, "pointerdown",
                                   (double)x, (double)y, (double)x, (double)y,
                                   0, 1, sh, ct, al, me, NULL, NULL);
        ns_js_dispatch_mouse_event(browser->js, node, "mousedown",
                                   (double)x, (double)y, (double)x, (double)y,
                                   0, 1, sh, ct, al, me, NULL, NULL);
        if (ns_js_consume_mutated(browser->js)) browser->dirty = TRUE;
    }

    if (browser->js) {
        const ns_node *focus = NULL;
        for (const ns_node *a = node; a; a = a->parent)
            if (ns_node_is_focusable(a)) { focus = a; break; }
        ns_js_set_focus(browser->js, focus);
        const char *val = focus ? ns_node_editable_value(focus) : NULL;
        browser->caret_byte = val ? strlen(val) : 0;
        browser->sel_anchor_byte = browser->caret_byte;
        if (ns_js_consume_mutated(browser->js)) browser->dirty = TRUE;
    }

    if (browser->js) {
        if (ns_js_run_animation_frame(browser->js)) browser->dirty = TRUE;
        if (ns_js_consume_mutated(browser->js)) browser->dirty = TRUE;
    }
    if (browser->dirty) {
        browser_relayout(browser);
        browser->dirty = FALSE;
    }

    char *nav = browser->pending_nav;
    browser->pending_nav = NULL;
    return nav;
}

char *
ns_browser_release_click(ns_browser *browser, int *out_changed)
{
    if (out_changed) *out_changed = 0;
    if (!browser) {
        if (out_changed) *out_changed = -1;
        return NULL;
    }
    browser_damp_reset(browser);
    g_clear_pointer(&browser->pending_nav, g_free);

    const ns_node *node = browser->press_active ? browser->press_node : NULL;
    int x = browser->press_x;
    int y = browser->press_y;
    int mods = browser->press_mods;
    browser->press_node = NULL;
    browser->press_active = FALSE;

    gboolean prevented = FALSE;
    if (browser->js && node) {
        gboolean sh = (mods & 1) != 0, ct = (mods & 2) != 0;
        gboolean al = (mods & 4) != 0, me = (mods & 8) != 0;
        ns_js_dispatch_mouse_event(browser->js, node, "pointerup",
                                   (double)x, (double)y, (double)x, (double)y,
                                   0, 0, sh, ct, al, me, NULL, NULL);
        ns_js_dispatch_mouse_event(browser->js, node, "mouseup",
                                   (double)x, (double)y, (double)x, (double)y,
                                   0, 0, sh, ct, al, me, NULL, NULL);
        ns_js_dispatch_mouse_event(browser->js, node, "click",
                                   (double)x, (double)y, (double)x, (double)y,
                                   0, 0, sh, ct, al, me, NULL, &prevented);
        if (ns_js_consume_mutated(browser->js)) browser->dirty = TRUE;
    }

    if (!prevented && browser->js && node &&
        ns_js_click_activate(browser->js, node))
        browser->dirty = TRUE;

    if (!prevented && !browser->pending_nav && node &&
        ns_form_is_submit_trigger(node)) {
        browser_submit_form(browser, node);
    } else if (!prevented && !browser->pending_nav) {
        const char *href = NULL;
        for (const ns_node *a = node; a && !href; a = a->parent) {
            if (ns_node_is_element_named(a, "a")) {
                const char *h = ns_element_get_attr(a, "href");
                if (h && *h) href = h;
            }
        }
        if (!href)
            href = ns_box_hit_link(browser->layout, (double)x, (double)y);
        if (href && *href)
            browser->pending_nav = browser_resolve_navigation(browser, href);
    }

    const ns_node *prev = ns_css_set_active_node(NULL);
    if (prev && ns_render_page_uses_active())
        browser->dirty = TRUE;

    if (browser->js) {
        if (ns_js_run_animation_frame(browser->js)) browser->dirty = TRUE;
        if (ns_js_consume_mutated(browser->js)) browser->dirty = TRUE;
    }
    if (browser->dirty) {
        browser_relayout(browser);
        browser->dirty = FALSE;
        if (out_changed) *out_changed = 1;
    }

    char *nav = browser->pending_nav;
    browser->pending_nav = NULL;
    return nav;
}

int
ns_browser_release(ns_browser *browser)
{
    int changed = 0;
    char *nav = ns_browser_release_click(browser, &changed);
    free(nav);
    return changed;
}

char *
ns_browser_click(ns_browser *browser, int x, int y, int mods)
{
    char *nav = ns_browser_press(browser, x, y, mods);
    if (nav && *nav) {
        if (browser) {
            browser->press_node = NULL;
            browser->press_active = FALSE;
            ns_css_set_active_node(NULL);
        }
        return nav;
    }
    free(nav);
    return ns_browser_release_click(browser, NULL);
}

static void
browser_input_replace(ns_browser *b, ns_node *node, gsize del_start,
                      gsize del_end, const char *insert)
{
    const char *cur = ns_node_editable_value(node);
    if (!cur) return;
    gsize cur_len = strlen(cur);
    if (del_start > cur_len) del_start = cur_len;
    if (del_end > cur_len) del_end = cur_len;
    if (del_end < del_start) del_end = del_start;
    gsize ins_len = insert ? strlen(insert) : 0;

    GString *s = g_string_sized_new(cur_len - (del_end - del_start) + ins_len);
    g_string_append_len(s, cur, (gssize)del_start);
    if (ins_len) g_string_append_len(s, insert, (gssize)ins_len);
    g_string_append_len(s, cur + del_end, (gssize)(cur_len - del_end));

    if (b->js) {
        gboolean prevented = FALSE;
        ns_js_dispatch_event(b->js, node, "beforeinput", &prevented);
        if (prevented || ns_js_focused_node(b->js) != node) {
            g_string_free(s, TRUE);
            return;
        }
    }
    ns_node_set_editable_value(node, s->str);
    b->caret_byte = del_start + ins_len;
    b->sel_anchor_byte = b->caret_byte;
    g_string_free(s, TRUE);
    if (b->js) {
        ns_js_dispatch_event(b->js, node, "input", NULL);
        (void)ns_js_consume_mutated(b->js);
    }
}

static gboolean
browser_edit_key(ns_browser *b, ns_node *node, const char *key, int mods)
{
    gboolean shift = (mods & 1) != 0, ctrl = (mods & 2) != 0;
    if (mods & (4 | 8)) return FALSE;
    const char *cur = ns_node_editable_value(node);
    if (!cur || !key || !*key) return FALSE;
    gsize cur_len = strlen(cur);
    if (b->caret_byte > cur_len) b->caret_byte = cur_len;
    if (b->sel_anchor_byte > cur_len) b->sel_anchor_byte = cur_len;
    gsize sel_lo = b->sel_anchor_byte < b->caret_byte ? b->sel_anchor_byte
                                                      : b->caret_byte;
    gsize sel_hi = b->sel_anchor_byte < b->caret_byte ? b->caret_byte
                                                      : b->sel_anchor_byte;
    gboolean has_sel = sel_lo != sel_hi;
    gboolean multiline = (node->name && strcmp(node->name, "textarea") == 0) ||
                         ns_node_is_contenteditable_host(node);

    if (ctrl) {
        if ((key[0] == 'a' || key[0] == 'A') && !key[1]) {
            b->sel_anchor_byte = 0;
            b->caret_byte = cur_len;
            return TRUE;
        }
        return FALSE;
    }
    if (strcmp(key, "Backspace") == 0) {
        if (has_sel) browser_input_replace(b, node, sel_lo, sel_hi, NULL);
        else if (b->caret_byte > 0) {
            const char *prev = g_utf8_prev_char(cur + b->caret_byte);
            browser_input_replace(b, node, (gsize)(prev - cur), b->caret_byte,
                                  NULL);
        }
        return TRUE;
    }
    if (strcmp(key, "Delete") == 0) {
        if (has_sel) browser_input_replace(b, node, sel_lo, sel_hi, NULL);
        else if (b->caret_byte < cur_len) {
            const char *nxt = g_utf8_next_char(cur + b->caret_byte);
            browser_input_replace(b, node, b->caret_byte, (gsize)(nxt - cur),
                                  NULL);
        }
        return TRUE;
    }
    if (strcmp(key, "ArrowLeft") == 0) {
        if (has_sel && !shift) b->caret_byte = sel_lo;
        else if (b->caret_byte > 0)
            b->caret_byte = (gsize)(g_utf8_prev_char(cur + b->caret_byte) - cur);
        if (!shift) b->sel_anchor_byte = b->caret_byte;
        return TRUE;
    }
    if (strcmp(key, "ArrowRight") == 0) {
        if (has_sel && !shift) b->caret_byte = sel_hi;
        else if (b->caret_byte < cur_len)
            b->caret_byte = (gsize)(g_utf8_next_char(cur + b->caret_byte) - cur);
        if (!shift) b->sel_anchor_byte = b->caret_byte;
        return TRUE;
    }
    if (strcmp(key, "Home") == 0) {
        b->caret_byte = 0;
        if (!shift) b->sel_anchor_byte = 0;
        return TRUE;
    }
    if (strcmp(key, "End") == 0) {
        b->caret_byte = cur_len;
        if (!shift) b->sel_anchor_byte = cur_len;
        return TRUE;
    }
    if (strcmp(key, "Enter") == 0) {
        if (multiline) {
            browser_input_replace(b, node, sel_lo, sel_hi, "\n");
            return TRUE;
        }
        browser_submit_form(b, node);
        return TRUE;
    }
    if (g_utf8_strlen(key, -1) == 1 &&
        !g_unichar_iscntrl(g_utf8_get_char(key))) {
        browser_input_replace(b, node, sel_lo, sel_hi, key);
        return TRUE;
    }
    return FALSE;
}

static int
browser_key_char_code(const char *key)
{
    if (!key || !*key || !g_utf8_validate(key, -1, NULL))
        return 0;
    const char *next = g_utf8_next_char(key);
    if (!next || *next)
        return 0;
    gunichar ch = g_utf8_get_char(key);
    return g_unichar_iscntrl(ch) ? 0 : (int)ch;
}

char *
ns_browser_key_full(ns_browser *browser, int kind, const char *key,
                    const char *code, int keycode, int mods,
                    int *out_prevented)
{
    if (out_prevented) *out_prevented = 0;
    if (!browser || !browser->js) return NULL;
    browser_damp_reset(browser);
    g_clear_pointer(&browser->pending_nav, g_free);

    const ns_node *target = ns_js_focused_node(browser->js);
    if (!target && browser->doc)
        target = ns_node_find_first_element(browser->doc, "body");
    if (!target) return NULL;

    if (kind == 2) {
        const ns_node *f = ns_js_focused_node(browser->js);
        if (f && ns_node_editable_value(f) && key && *key &&
            g_utf8_validate(key, -1, NULL)) {
            const char *cur = ns_node_editable_value(f);
            gsize cur_len = cur ? strlen(cur) : 0;
            if (browser->caret_byte > cur_len)
                browser->caret_byte = cur_len;
            if (browser->sel_anchor_byte > cur_len)
                browser->sel_anchor_byte = cur_len;
            gsize lo = browser->sel_anchor_byte < browser->caret_byte
                       ? browser->sel_anchor_byte : browser->caret_byte;
            gsize hi = browser->sel_anchor_byte < browser->caret_byte
                       ? browser->caret_byte : browser->sel_anchor_byte;
            browser_input_replace(browser, (ns_node *)f, lo, hi, key);
            browser->dirty = TRUE;
        }
    } else {
        gboolean prevented = FALSE;
        ns_js_dispatch_key_event_full(browser->js, target,
                                      kind == 1 ? "keyup" : "keydown",
                                      key ? key : "", code ? code : "",
                                      keycode, 0,
                                      (mods & 1) != 0, (mods & 2) != 0,
                                      (mods & 4) != 0, (mods & 8) != 0,
                                      &prevented);
        if (out_prevented && prevented)
            *out_prevented = 1;
        if (ns_js_consume_mutated(browser->js)) browser->dirty = TRUE;

        int char_code = browser_key_char_code(key);
        if (kind == 3 && !prevented && char_code > 0 &&
            (mods & (2 | 4 | 8)) == 0) {
            gboolean press_prevented = FALSE;
            ns_js_dispatch_key_event_full(browser->js, target, "keypress",
                                          key ? key : "", code ? code : "",
                                          keycode, char_code,
                                          (mods & 1) != 0, FALSE, FALSE, FALSE,
                                          &press_prevented);
            if (out_prevented && press_prevented)
                *out_prevented = 1;
            if (ns_js_consume_mutated(browser->js)) browser->dirty = TRUE;
        }

        if (!prevented && kind == 0) {
            const ns_node *f = ns_js_focused_node(browser->js);
            if (f && ns_node_editable_value(f) &&
                browser_edit_key(browser, (ns_node *)f, key, mods))
                browser->dirty = TRUE;
        }
    }

    if (ns_js_run_animation_frame(browser->js)) browser->dirty = TRUE;
    if (ns_js_consume_mutated(browser->js)) browser->dirty = TRUE;
    if (browser->dirty) {
        browser_relayout(browser);
        browser->dirty = FALSE;
    }

    char *nav = browser->pending_nav;
    browser->pending_nav = NULL;
    return nav;
}

char *
ns_browser_key(ns_browser *browser, int kind, const char *key,
               const char *code, int keycode, int mods)
{
    return ns_browser_key_full(browser, kind, key, code, keycode, mods, NULL);
}

char *
ns_browser_title(ns_browser *browser)
{
    if (!browser || !browser->doc) return NULL;
    ns_node *title = ns_node_find_first_element(browser->doc, "title");
    if (!title) return NULL;
    char *raw = ns_node_collect_text(title);
    if (!raw) return NULL;

    GString *out = g_string_new(NULL);
    gboolean prev_ws = TRUE;
    for (const char *p = raw; *p; p++) {
        gboolean ws = (*p == ' ' || *p == '\t' || *p == '\n' ||
                       *p == '\r' || *p == '\f');
        if (ws) {
            if (!prev_ws) g_string_append_c(out, ' ');
            prev_ws = TRUE;
        } else {
            g_string_append_c(out, *p);
            prev_ws = FALSE;
        }
    }
    if (out->len > 0 && out->str[out->len - 1] == ' ')
        g_string_set_size(out, out->len - 1);
    g_free(raw);

    if (out->len == 0) { g_string_free(out, TRUE); return NULL; }
    char *result = strdup(out->str);
    g_string_free(out, TRUE);
    return result;
}

char *
ns_browser_url(ns_browser *browser)
{
    if (!browser || !browser->base_url) return NULL;
    return strdup(browser->base_url);
}

char *
ns_browser_take_pending_nav(ns_browser *browser)
{
    if (!browser || !browser->pending_nav) return NULL;
    char *out = strdup(browser->pending_nav);
    g_clear_pointer(&browser->pending_nav, g_free);
    return out;
}

char *
ns_browser_take_pending_webgl(ns_browser *browser)
{
    (void)browser;
    return ns_webgl_take_pending_origin();
}

void
ns_browser_resolve_webgl(ns_browser *browser, const char *origin, int allow)
{
    (void)browser;
    ns_webgl_set_decision(origin, allow);
}

static void
collect_links(const ns_node *node, const char *base, GString *out,
              GHashTable *seen)
{
    for (const ns_node *c = node->first_child; c; c = c->next_sibling) {
        if (ns_node_is_element_named(c, "a")) {
            const char *href = ns_element_get_attr(c, "href");
            if (href && *href && href[0] != '#' &&
                !g_str_has_prefix(href, "javascript:")) {
                char *abs = ns_url_resolve(base, href);
                if (abs && *abs && !g_hash_table_contains(seen, abs)) {
                    g_hash_table_add(seen, g_strdup(abs));
                    if (out->len) g_string_append_c(out, '\n');
                    g_string_append(out, abs);
                }
                g_free(abs);
            }
        }
        collect_links(c, base, out, seen);
    }
}

char *
ns_browser_links(ns_browser *browser)
{
    if (!browser || !browser->doc) return NULL;
    GString *out = g_string_new(NULL);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal,
                                             g_free, NULL);
    collect_links(browser->doc, browser->base_url, out, seen);
    g_hash_table_destroy(seen);
    if (out->len == 0) { g_string_free(out, TRUE); return NULL; }
    char *result = strdup(out->str);
    g_string_free(out, TRUE);
    return result;
}

void
ns_browser_close(ns_browser *browser)
{
    if (!browser) return;
    if (browser->img_sessions) {
        for (guint i = 0; i < browser->img_sessions->len; i++)
            ns_engine_img_session_close(
                g_ptr_array_index(browser->img_sessions, i));
        g_ptr_array_free(browser->img_sessions, TRUE);
    }
    if (browser->img_requested) g_hash_table_destroy(browser->img_requested);
    ns_css_set_active_node(NULL);
    ns_paint_set_anim(NULL);
    if (browser->js) {
        ns_js_set_layout_root(browser->js, NULL);
        ns_js_set_style_table(browser->js, NULL);
    }
    if (browser->anim) ns_anim_free(browser->anim);
    if (browser->layout) { ns_paint_3d_invalidate(); ns_box_free(browser->layout); }
    if (browser->styles) g_hash_table_destroy(browser->styles);
    if (browser->css_cache) g_hash_table_destroy(browser->css_cache);
    if (browser->js) ns_js_free(browser->js);
    if (browser->doc) ns_node_free(browser->doc);
    if (browser->images) ns_image_cache_free(browser->images);
    g_free(browser->base_url);
    g_free(browser->doc_charset);
    g_free(browser->pending_nav);
    g_free(browser->refresh_url);
    g_free(browser->pending_post_body);
    g_free(browser->pending_post_ct);
    g_free(browser->search_query);
    if (browser->console_buf) g_string_free(browser->console_buf, TRUE);
    g_free(browser);
}
