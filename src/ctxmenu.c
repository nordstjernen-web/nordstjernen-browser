/* Nordstjernen — right-click context menu over the rendered document.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include <gtk/gtk.h>

#include "ctxmenu.h"
#include "bookmarks.h"
#include "config.h"
#include "dom.h"
#include "js.h"
#include "layout.h"
#include "media.h"
#include "net.h"
#include "window.h"

static char *
ns_build_search_url(const char *query)
{
    if (!query || !*query) return NULL;
    char *escaped = g_uri_escape_string(query, NULL, FALSE);
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
ns_search_snippet_label(const char *text)
{
    char *flat = g_strdup(text ? text : "");
    for (char *p = flat; *p; p++) {
        if (*p == '\n' || *p == '\r' || *p == '\t') *p = ' ';
    }
    g_strstrip(flat);
    const char *end = flat;
    int chars = 0;
    while (*end && chars < 30) {
        end = g_utf8_next_char(end);
        chars++;
    }
    char *label = *end
        ? g_strdup_printf("Search the Web for \"%.*s…\"",
                          (int)(end - flat), flat)
        : g_strdup_printf("Search the Web for \"%s\"", flat);
    g_free(flat);
    return label;
}

static void
on_ctx_open_link(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    ns_window *w = ud;
    if (!w->context_menu_link) return;
    char *abs = ns_resolve_url(w, w->context_menu_link);
    if (!abs) return;
    ns_window_load_url(w, abs, NS_LOAD_USER);
    g_free(abs);
}

static void
on_ctx_open_link_new_tab(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    ns_window *w = ud;
    if (!w->context_menu_link) return;
    char *abs = ns_resolve_url(w, w->context_menu_link);
    if (!abs) return;
    GtkApplication *app = gtk_window_get_application(GTK_WINDOW(w->window));
    ns_window *nw = ns_browser_add_tab(w->window, app, abs);
    if (nw) ns_browser_set_active(w->window, nw);
    g_free(abs);
}

static void
on_ctx_open_link_new_window(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    ns_window *w = ud;
    if (!w->context_menu_link) return;
    char *abs = ns_resolve_url(w, w->context_menu_link);
    if (abs) {
        GtkApplication *app = gtk_window_get_application(GTK_WINDOW(w->window));
        ns_spawn_window(app, abs);
        g_free(abs);
    }
}

static void
on_ctx_copy_link(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    ns_window *w = ud;
    if (!w->context_menu_link) return;
    char *abs = ns_resolve_url(w, w->context_menu_link);
    GdkClipboard *cb = gtk_widget_get_clipboard(w->window);
    gdk_clipboard_set_text(cb, abs ? abs : w->context_menu_link);
    ns_window_set_status(w, "Copied %s", abs ? abs : w->context_menu_link);
    g_free(abs);
}

static void
on_ctx_bookmark_link(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    ns_window *w = ud;
    if (!w->context_menu_link || !ns_app_bookmarks()) return;
    char *abs = ns_resolve_url(w, w->context_menu_link);
    const char *url = abs ? abs : w->context_menu_link;
    if (ns_bookmarks_contains(ns_app_bookmarks(), url)) {
        ns_bookmarks_remove(ns_app_bookmarks(), url);
        ns_window_set_status(w, "Removed bookmark %s", url);
    } else {
        ns_bookmarks_add(ns_app_bookmarks(), url, url);
        ns_window_set_status(w, "Bookmarked %s", url);
    }
    g_free(abs);
}

static void
on_ctx_open_image(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    ns_window *w = ud;
    if (!w->context_menu_image) return;
    char *abs = ns_resolve_url(w, w->context_menu_image);
    if (!abs) return;
    ns_window_load_url(w, abs, NS_LOAD_USER);
    g_free(abs);
}

static void
on_ctx_open_image_new_tab(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    ns_window *w = ud;
    if (!w->context_menu_image) return;
    char *abs = ns_resolve_url(w, w->context_menu_image);
    if (!abs) return;
    GtkApplication *app = gtk_window_get_application(GTK_WINDOW(w->window));
    ns_window *nw = ns_browser_add_tab(w->window, app, abs);
    if (nw) ns_browser_set_active(w->window, nw);
    g_free(abs);
}

static void
on_ctx_copy_image_address(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    ns_window *w = ud;
    if (!w->context_menu_image) return;
    char *abs = ns_resolve_url(w, w->context_menu_image);
    GdkClipboard *cb = gtk_widget_get_clipboard(w->window);
    gdk_clipboard_set_text(cb, abs ? abs : w->context_menu_image);
    ns_window_set_status(w, "Copied %s", abs ? abs : w->context_menu_image);
    g_free(abs);
}

static void
on_ctx_open_media(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    ns_window *w = ud;
    if (!w->context_menu_media) return;
    if (ns_media_launch_external(GTK_WINDOW(w->window), w->context_menu_media,
                                 w->context_menu_media_is_video,
                                 w->context_menu_media_stream))
        ns_window_set_status(w, "Opening %s in external player…",
                             w->context_menu_media_is_video ? "video" : "audio");
}

static void
on_ctx_copy_media_address(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    ns_window *w = ud;
    if (!w->context_menu_media) return;
    GdkClipboard *cb = gtk_widget_get_clipboard(w->window);
    gdk_clipboard_set_text(cb, w->context_menu_media);
    ns_window_set_status(w, "Copied %s", w->context_menu_media);
}

static void
on_ctx_copy_selection(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    ns_window *w = ud;
    if (!w->context_menu_selection || !*w->context_menu_selection) return;
    GdkClipboard *cb = gtk_widget_get_clipboard(w->drawing_area
                                                ? w->drawing_area : w->window);
    gdk_clipboard_set_text(cb, w->context_menu_selection);
    ns_window_set_status(w, "Copied %d characters",
                         (int)g_utf8_strlen(w->context_menu_selection, -1));
}

static void
on_ctx_search_selection(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    ns_window *w = ud;
    if (!w->context_menu_selection || !*w->context_menu_selection) return;
    char *url = ns_build_search_url(w->context_menu_selection);
    if (!url) return;
    GtkApplication *app = gtk_window_get_application(GTK_WINDOW(w->window));
    ns_window *nw = ns_browser_add_tab(w->window, app, url);
    if (nw) ns_browser_set_active(w->window, nw);
    g_free(url);
}

static void
on_ctx_view_source(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    ns_window *w = ud;
    if (!w->last_body) return;
    w->mode = (w->mode == NS_VIEW_RAW) ? NS_VIEW_RENDER : NS_VIEW_RAW;
    ns_window_render(w);
}

static void
on_ctx_bookmark_page(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    ns_window *w = ud;
    const char *url = ns_window_current_url(w);
    if (!url || !ns_app_bookmarks()) return;
    if (ns_bookmarks_contains(ns_app_bookmarks(), url)) {
        ns_bookmarks_remove(ns_app_bookmarks(), url);
        ns_window_set_status(w, "Removed bookmark %s", url);
    } else {
        char *title = ns_window_current_title(w);
        ns_bookmarks_add(ns_app_bookmarks(), url, title ? title : url);
        ns_window_set_status(w, "Bookmarked %s", url);
        g_free(title);
    }
}

static void
on_ctx_home(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    ns_window *w = ud;
    if (ns_app_home_url() && *ns_app_home_url())
        ns_window_load_url(w, ns_app_home_url(), NS_LOAD_USER);
}

static void
on_ctx_copy_url(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    ns_window *w = ud;
    const char *url = ns_window_current_url(w);
    if (!url) return;
    GdkClipboard *cb = gtk_widget_get_clipboard(w->window);
    gdk_clipboard_set_text(cb, url);
    ns_window_set_status(w, "Copied %s", url);
}

void
ns_install_ctx_actions(ns_window *w)
{
    static const struct { const char *name; GCallback cb; } items[] = {
        { "ctx-open-link",            G_CALLBACK(on_ctx_open_link) },
        { "ctx-open-link-new-tab",    G_CALLBACK(on_ctx_open_link_new_tab) },
        { "ctx-open-link-new-window", G_CALLBACK(on_ctx_open_link_new_window) },
        { "ctx-copy-link",            G_CALLBACK(on_ctx_copy_link) },
        { "ctx-bookmark-link",        G_CALLBACK(on_ctx_bookmark_link) },
        { "ctx-open-image",           G_CALLBACK(on_ctx_open_image) },
        { "ctx-open-image-new-tab",   G_CALLBACK(on_ctx_open_image_new_tab) },
        { "ctx-copy-image-address",   G_CALLBACK(on_ctx_copy_image_address) },
        { "ctx-open-media",           G_CALLBACK(on_ctx_open_media) },
        { "ctx-copy-media-address",   G_CALLBACK(on_ctx_copy_media_address) },
        { "ctx-copy-selection",       G_CALLBACK(on_ctx_copy_selection) },
        { "ctx-search-selection",     G_CALLBACK(on_ctx_search_selection) },
        { "ctx-copy-url",             G_CALLBACK(on_ctx_copy_url) },
        { "ctx-view-source",          G_CALLBACK(on_ctx_view_source) },
        { "ctx-bookmark-page",        G_CALLBACK(on_ctx_bookmark_page) },
        { "ctx-home",                 G_CALLBACK(on_ctx_home) },
    };
    GActionMap *map = G_ACTION_MAP(w->window);
    for (gsize i = 0; i < G_N_ELEMENTS(items); i++) {
        g_action_map_remove_action(map, items[i].name);
        GSimpleAction *a = g_simple_action_new(items[i].name, NULL);
        g_signal_connect(a, "activate", items[i].cb, w);
        g_action_map_add_action(map, G_ACTION(a));
        g_object_unref(a);
    }
}

static const ns_box *
ns_box_find_image_ancestor(const ns_box *hit)
{
    for (const ns_box *b = hit; b; b = b->parent) {
        if (b->kind == NS_BOX_IMAGE && b->media && b->media->image_src
            && *b->media->image_src)
            return b;
    }
    return NULL;
}

void
ns_on_drawing_right_pressed(GtkGestureClick *gesture, int n_press,
                            double x, double y, gpointer user_data)
{
    (void)n_press;
    ns_window *w = user_data;
    if (!w->layout_tree) return;

    if (w->js) {
        const ns_box *target = ns_box_hit_test(w->layout_tree, x, y);
        if (target && target->dom) {
            double cy = y, cx = x;
            if (w->render_vadj) cy = y - gtk_adjustment_get_value(w->render_vadj);
            GtkWidget *sw = w->drawing_area
                ? gtk_widget_get_ancestor(w->drawing_area, GTK_TYPE_SCROLLED_WINDOW)
                : NULL;
            if (sw) {
                GtkAdjustment *h = gtk_scrolled_window_get_hadjustment(
                    GTK_SCROLLED_WINDOW(sw));
                if (h) cx = x - gtk_adjustment_get_value(h);
            }
            GdkModifierType st = gesture
                ? gtk_event_controller_get_current_event_state(
                      GTK_EVENT_CONTROLLER(gesture)) : 0;
            gboolean prevented = FALSE;
            ns_js_dispatch_mouse_event(w->js, target->dom, "contextmenu",
                                       cx, cy, x, y, 2, 2,
                                       (st & GDK_SHIFT_MASK)   != 0,
                                       (st & GDK_CONTROL_MASK) != 0,
                                       (st & GDK_ALT_MASK)     != 0,
                                       (st & GDK_META_MASK)    != 0,
                                       NULL, &prevented);
            if (w->js && ns_js_consume_mutated(w->js)) ns_window_js_mutated(w);
            if (prevented) {
                if (w->drawing_area) gtk_widget_queue_draw(w->drawing_area);
                return;
            }
        }
    }

    g_clear_pointer(&w->context_menu_link, g_free);
    g_clear_pointer(&w->context_menu_image, g_free);
    g_clear_pointer(&w->context_menu_selection, g_free);
    g_clear_pointer(&w->context_menu_media, g_free);

    const char *href = ns_box_hit_link(w->layout_tree, x, y);
    const ns_box *hit = ns_box_hit_test(w->layout_tree, x, y);
    if (!href && hit && hit->dom) {
        for (const ns_node *p = hit->dom; p; p = p->parent) {
            if (ns_node_is_element_named(p, "a")) {
                const char *h = ns_element_get_attr(p, "href");
                if (h && *h) { href = h; break; }
            }
        }
    }
    if (href) w->context_menu_link = g_strdup(href);

    const ns_box *img = ns_box_find_image_ancestor(hit);
    if (img) w->context_menu_image = g_strdup(img->media->image_src);

    {
        char *media_url = NULL;
        gboolean is_video = FALSE, stream = FALSE;
        if (ns_window_media_target(w, hit, &media_url, &is_video, &stream)) {
            w->context_menu_media = media_url;
            w->context_menu_media_is_video = is_video;
            w->context_menu_media_stream = stream;
        }
    }

    if (ns_selection_has_range(&w->selection)) {
        char *text = ns_selection_collect_text(w->layout_tree, &w->selection);
        if (text && *text) w->context_menu_selection = text;
        else g_free(text);
    }

    ns_window_update_nav_state(w);

    GMenu *menu = g_menu_new();

    if (w->context_menu_link) {
        GMenu *link_section = g_menu_new();
        g_menu_append(link_section, "Open Link",            "win.ctx-open-link");
        g_menu_append(link_section, "Open Link in New Tab", "win.ctx-open-link-new-tab");
        g_menu_append(link_section, "Copy Link Address",    "win.ctx-copy-link");
        g_menu_append_section(menu, NULL, G_MENU_MODEL(link_section));
        g_object_unref(link_section);
    }

    if (w->context_menu_image) {
        GMenu *img_section = g_menu_new();
        g_menu_append(img_section, "Open Image",            "win.ctx-open-image");
        g_menu_append(img_section, "Open Image in New Tab", "win.ctx-open-image-new-tab");
        g_menu_append(img_section, "Copy Image Address",    "win.ctx-copy-image-address");
        g_menu_append_section(menu, NULL, G_MENU_MODEL(img_section));
        g_object_unref(img_section);
    }

    if (w->context_menu_media) {
        GMenu *media_section = g_menu_new();
        g_menu_append(media_section,
                      w->context_menu_media_is_video
                          ? "Open Video in External Player"
                          : "Open Audio in External Player",
                      "win.ctx-open-media");
        g_menu_append(media_section,
                      w->context_menu_media_stream
                          ? "Copy Page Address"
                          : "Copy Media Address",
                      "win.ctx-copy-media-address");
        g_menu_append_section(menu, NULL, G_MENU_MODEL(media_section));
        g_object_unref(media_section);
    }

    if (w->context_menu_selection) {
        GMenu *sel_section = g_menu_new();
        g_menu_append(sel_section, "Copy", "win.ctx-copy-selection");
        char *search_label = ns_search_snippet_label(w->context_menu_selection);
        g_menu_append(sel_section, search_label, "win.ctx-search-selection");
        g_free(search_label);
        g_menu_append_section(menu, NULL, G_MENU_MODEL(sel_section));
        g_object_unref(sel_section);
    }

    GMenu *nav_section = g_menu_new();
    g_menu_append(nav_section, "Back",    "win.back");
    g_menu_append(nav_section, "Forward", "win.forward");
    g_menu_append(nav_section, "Reload",  "win.reload");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(nav_section));
    g_object_unref(nav_section);

    GMenu *page_section = g_menu_new();
    g_menu_append(page_section, "Copy Page URL", "win.ctx-copy-url");
    if (w->last_body)
        g_menu_append(page_section,
                      w->mode == NS_VIEW_RAW ? "Exit Source View" : "View Page Source",
                      "win.ctx-view-source");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(page_section));
    g_object_unref(page_section);

    GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
    g_object_unref(menu);
    gtk_widget_set_parent(popover, w->drawing_area);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    GdkRectangle rect = { (int)x, (int)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_popover_popup(GTK_POPOVER(popover));
    g_signal_connect_swapped(popover, "closed", G_CALLBACK(gtk_widget_unparent), popover);
}

void
ns_on_drawing_long_pressed(GtkGestureLongPress *gesture, double x, double y,
                           gpointer user_data)
{
    (void)gesture;
    ns_on_drawing_right_pressed(NULL, 1, x, y, user_data);
}
