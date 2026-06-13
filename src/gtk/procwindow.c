/* Nordstjernen — GTK tabbed process-per-tab browser shell (IPC renderer). */

#include "procwindow.h"
#include "procview.h"
#include "i18n.h"
#include "rproc_http.h"
#include "rproc_inproc.h"
#include "bookmarks.h"
#include "config.h"
#include "net.h"
#include "version.h"

#include <string.h>

#define NS_PROC_APP_ID "org.nordstjernen.Browser.Proc"

typedef struct {
    GtkApplication *app;
    GtkWidget      *window;
    GtkWidget      *notebook;
    GtkWidget      *address;
    GtkWidget      *back;
    GtkWidget      *forward;
    GtkWidget      *reload;
    GtkWidget      *spinner;
    GtkWidget      *status;
    GtkWidget      *bookmarks_button;
    char           *home_url;
    ns_bookmarks   *bookmarks;
    char           *session_path;
    guint           session_timer;
    GtkWidget      *task_mgr_win;
} ProcWindow;

static const char *
ns_brand_versioned(void)
{
    static char brand[128];
    if (!brand[0])
        g_snprintf(brand, sizeof brand, "%s %s",
                   ns_i18n("Nordstjernen"), NS_VERSION);
    return brand;
}

static void
procwindow_free(gpointer data)
{
    ProcWindow *pw = data;
    if (pw->session_timer)
        g_source_remove(pw->session_timer);
    g_free(pw->session_path);
    g_free(pw->home_url);
    if (pw->bookmarks)
        ns_bookmarks_free(pw->bookmarks);
    g_free(pw);
}

static void
install_icon_search_paths(void)
{
    GdkDisplay *display = gdk_display_get_default();
    if (!display)
        return;
    GtkIconTheme *theme = gtk_icon_theme_get_for_display(display);
    if (!theme)
        return;
    const char *exe = ns_app_self_exe();
    if (!exe)
        return;
    char *dir = g_path_get_dirname(exe);
    const char *rel[] = { "share/icons",      "../share/icons",
                          "data/icons",       "../data/icons",
                          "../../data/icons", "../../../data/icons",
                          "../../../../data/icons", NULL };
    for (int i = 0; rel[i]; i++) {
        char *p = g_build_filename(dir, rel[i], NULL);
        gtk_icon_theme_add_search_path(theme, p);
        g_free(p);
    }
    g_free(dir);
}

static void
install_status_css(void)
{
    GdkDisplay *display = gdk_display_get_default();
    if (!display)
        return;
    GtkCssProvider *p = gtk_css_provider_new();
    gtk_css_provider_load_from_string(
        p,
        ".ns-procstatus {"
        "  padding: 2px 8px;"
        "  border-top: 1px solid alpha(currentColor, 0.15);"
        "  font-size: smaller;"
        "}");
    gtk_style_context_add_provider_for_display(
        display, GTK_STYLE_PROVIDER(p),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);
}

static GtkWidget *
toolbar_button(const char *icon, const char *tooltip, GCallback cb,
               gpointer data)
{
    GtkWidget *b = gtk_button_new_from_icon_name(icon);
    gtk_button_set_has_frame(GTK_BUTTON(b), FALSE);
    gtk_widget_set_tooltip_text(b, tooltip);
    g_signal_connect(b, "clicked", cb, data);
    return b;
}

static NsProcView *
view_for_page(GtkWidget *page)
{
    return page ? g_object_get_data(G_OBJECT(page), "ns-proc-view") : NULL;
}

static NsProcView *
current_view(ProcWindow *pw)
{
    int idx = gtk_notebook_get_current_page(GTK_NOTEBOOK(pw->notebook));
    if (idx < 0)
        return NULL;
    return view_for_page(
        gtk_notebook_get_nth_page(GTK_NOTEBOOK(pw->notebook), idx));
}

static char *
normalize_url(const char *input)
{
    char *trimmed = g_strstrip(g_strdup(input ? input : ""));
    if (!*trimmed)
        return trimmed;
    if (g_str_has_prefix(trimmed, "about:") ||
        g_str_has_prefix(trimmed, "file:") ||
        g_str_has_prefix(trimmed, "data:") || strstr(trimmed, "://"))
        return trimmed;
    char *local = ns_url_from_local_path(trimmed);
    if (local) {
        g_free(trimmed);
        return local;
    }
    if (ns_address_is_search(trimmed)) {
        char *out = ns_search_url_for(trimmed);
        g_free(trimmed);
        return out;
    }
    char *out = g_strconcat("https://", trimmed, NULL);
    g_free(trimmed);
    return out;
}

static void
set_loading_ui(ProcWindow *pw, gboolean loading)
{
    gtk_widget_set_visible(pw->spinner, loading);
    gtk_spinner_set_spinning(GTK_SPINNER(pw->spinner), loading);
}

static char *
address_display_url(const char *url)
{
    if (!url || !*url) return g_strdup("");
    if (!strchr(url, '%')) return g_strdup(url);
    char *dec = g_uri_unescape_string(url, NULL);
    if (!dec) return g_strdup(url);
    if (!g_utf8_validate(dec, -1, NULL)) {
        g_free(dec);
        return g_strdup(url);
    }
    for (const char *p = dec; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (c < 0x20 || c == 0x7f ||
            (c >= 0x200e && c <= 0x200f) ||
            (c >= 0x202a && c <= 0x202e) ||
            (c >= 0x2066 && c <= 0x2069)) {
            g_free(dec);
            return g_strdup(url);
        }
    }
    return dec;
}

static void
set_address_text(ProcWindow *pw, const char *url)
{
    char *shown = address_display_url(url);
    gtk_editable_set_text(GTK_EDITABLE(pw->address), shown);
    g_free(shown);
}

static void
update_chrome(ProcWindow *pw)
{
    NsProcView *v = current_view(pw);
    if (!v) {
        gtk_editable_set_text(GTK_EDITABLE(pw->address), "");
        gtk_window_set_title(GTK_WINDOW(pw->window), ns_brand_versioned());
        gtk_widget_set_sensitive(pw->back, FALSE);
        gtk_widget_set_sensitive(pw->forward, FALSE);
        set_loading_ui(pw, FALSE);
        return;
    }
    set_loading_ui(pw, ns_proc_view_is_loading(v));
    const char *url = ns_proc_view_url(v);
    const char *title = ns_proc_view_title(v);
    set_address_text(pw, url);
    const char *brand = ns_brand_versioned();
    char *wt = g_strdup_printf("%s — %s",
                               title && *title ? title : brand, brand);
    gtk_window_set_title(GTK_WINDOW(pw->window), wt);
    g_free(wt);
    gtk_widget_set_sensitive(pw->back, ns_proc_view_can_back(v));
    gtk_widget_set_sensitive(pw->forward, ns_proc_view_can_forward(v));
}

static void proc_window_add_tab(ProcWindow *pw, const char *url,
                                gboolean foreground);

static void
on_view_notify(NsProcView *v, NsProcEvent evt, const char *text,
               gpointer user_data)
{
    ProcWindow *pw = user_data;
    GtkWidget *page = ns_proc_view_widget(v);
    int idx = page ? gtk_notebook_page_num(GTK_NOTEBOOK(pw->notebook), page)
                   : -1;
    gboolean is_current = (v == current_view(pw));

    switch (evt) {
    case NS_PROC_EVT_TITLE: {
        if (idx >= 0) {
            GtkWidget *p =
                gtk_notebook_get_nth_page(GTK_NOTEBOOK(pw->notebook), idx);
            GtkWidget *label = g_object_get_data(G_OBJECT(p), "ns-tab-label");
            const char *t = text && *text ? text : ns_i18n("Untitled");
            char *clip = g_strndup(t, 40);
            if (label)
                gtk_label_set_text(GTK_LABEL(label), clip);
            g_free(clip);
        }
        if (is_current)
            update_chrome(pw);
        break;
    }
    case NS_PROC_EVT_URL:
        if (is_current)
            set_address_text(pw, text);
        break;
    case NS_PROC_EVT_STATUS:
        if (is_current)
            gtk_label_set_text(GTK_LABEL(pw->status), text ? text : "");
        break;
    case NS_PROC_EVT_HISTORY:
        if (is_current) {
            gtk_widget_set_sensitive(pw->back, ns_proc_view_can_back(v));
            gtk_widget_set_sensitive(pw->forward, ns_proc_view_can_forward(v));
        }
        break;
    case NS_PROC_EVT_NEWTAB:
        if (text && *text)
            proc_window_add_tab(pw, text, FALSE);
        break;
    case NS_PROC_EVT_LOADING:
        if (is_current)
            set_loading_ui(pw, text && *text == '1');
        break;
    }
}

static void
on_tab_close(GtkButton *button, gpointer user_data)
{
    ProcWindow *pw = g_object_get_data(G_OBJECT(button), "ns-pw");
    GtkWidget *page = user_data;
    int idx = gtk_notebook_page_num(GTK_NOTEBOOK(pw->notebook), page);
    if (idx >= 0)
        gtk_notebook_remove_page(GTK_NOTEBOOK(pw->notebook), idx);
    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(pw->notebook)) == 0)
        gtk_window_close(GTK_WINDOW(pw->window));
}

static void
proc_window_add_tab(ProcWindow *pw, const char *url, gboolean foreground)
{
    NsProcView *v = ns_proc_view_new();
    ns_proc_view_set_notify(v, on_view_notify, pw);
    GtkWidget *page = ns_proc_view_widget(v);
    g_object_set_data(G_OBJECT(page), "ns-proc-view", v);

    GtkWidget *tab = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *label = gtk_label_new(ns_i18n("New Tab"));
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(GTK_LABEL(label), 16);
    gtk_box_append(GTK_BOX(tab), label);
    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(close), FALSE);
    g_object_set_data(G_OBJECT(close), "ns-pw", pw);
    g_signal_connect(close, "clicked", G_CALLBACK(on_tab_close), page);
    gtk_box_append(GTK_BOX(tab), close);

    g_object_set_data(G_OBJECT(page), "ns-tab-label", label);
    int idx = gtk_notebook_append_page(GTK_NOTEBOOK(pw->notebook), page, tab);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(pw->notebook), page, TRUE);
    if (foreground)
        gtk_notebook_set_current_page(GTK_NOTEBOOK(pw->notebook), idx);

    char *resolved = normalize_url(url);
    ns_proc_view_load(v, resolved);
    g_free(resolved);
}

static void
on_address_activate(GtkEntry *entry, gpointer user_data)
{
    ProcWindow *pw = user_data;
    NsProcView *v = current_view(pw);
    char *resolved = normalize_url(gtk_editable_get_text(GTK_EDITABLE(entry)));
    if (!*resolved) {
        g_free(resolved);
        return;
    }
    if (!v) {
        proc_window_add_tab(pw, resolved, TRUE);
    } else {
        gtk_editable_set_text(GTK_EDITABLE(pw->address), resolved);
        ns_proc_view_load(v, resolved);
    }
    g_free(resolved);
}

static void
on_back_clicked(GtkButton *b, gpointer ud)
{
    (void)b;
    NsProcView *v = current_view(ud);
    if (v)
        ns_proc_view_back(v);
}

static void
on_forward_clicked(GtkButton *b, gpointer ud)
{
    (void)b;
    NsProcView *v = current_view(ud);
    if (v)
        ns_proc_view_forward(v);
}

static void
on_reload_clicked(GtkButton *b, gpointer ud)
{
    (void)b;
    NsProcView *v = current_view(ud);
    if (v)
        ns_proc_view_reload(v);
}

static void
on_home_clicked(GtkButton *b, gpointer ud)
{
    (void)b;
    ProcWindow *pw = ud;
    NsProcView *v = current_view(pw);
    if (v)
        ns_proc_view_load(v, pw->home_url ? pw->home_url : "about:start");
}

static void
on_logo_clicked(GtkButton *b, gpointer ud)
{
    (void)b;
    NsProcView *v = current_view(ud);
    if (v)
        ns_proc_view_load(v, "https://nordstjernen.org");
}

static void
on_go_clicked(GtkButton *b, gpointer ud)
{
    (void)b;
    ProcWindow *pw = ud;
    on_address_activate(GTK_ENTRY(pw->address), pw);
}

static void
on_newtab_clicked(GtkButton *b, gpointer ud)
{
    (void)b;
    proc_window_add_tab(ud, "about:start", TRUE);
}

static void
on_switch_page(GtkNotebook *nb, GtkWidget *page, guint num, gpointer ud)
{
    (void)nb;
    (void)page;
    (void)num;
    update_chrome(ud);
}

static void
act_back(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    NsProcView *v = current_view(ud);
    if (v)
        ns_proc_view_back(v);
}

static void
act_forward(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    NsProcView *v = current_view(ud);
    if (v)
        ns_proc_view_forward(v);
}

static void
act_reload(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    NsProcView *v = current_view(ud);
    if (v)
        ns_proc_view_reload(v);
}

static void
act_console(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    NsProcView *v = current_view(ud);
    if (v)
        ns_proc_view_toggle_console(v);
}

static void
act_home(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    ProcWindow *pw = ud;
    NsProcView *v = current_view(pw);
    if (v)
        ns_proc_view_load(v, pw->home_url ? pw->home_url : "about:start");
}

static void
act_new_tab(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    proc_window_add_tab(ud, "about:start", TRUE);
}

static void
act_close_tab(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    ProcWindow *pw = ud;
    int idx = gtk_notebook_get_current_page(GTK_NOTEBOOK(pw->notebook));
    if (idx < 0)
        return;
    gtk_notebook_remove_page(GTK_NOTEBOOK(pw->notebook), idx);
    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(pw->notebook)) == 0)
        gtk_window_close(GTK_WINDOW(pw->window));
}

static void
act_focus_address(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    ProcWindow *pw = ud;
    gtk_widget_grab_focus(pw->address);
    gtk_editable_select_region(GTK_EDITABLE(pw->address), 0, -1);
}

static void
act_focus_page(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    NsProcView *v = current_view(ud);
    if (v)
        ns_proc_view_focus(v);
}

static void
act_zoom_in(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    NsProcView *v = current_view(ud);
    if (v)
        ns_proc_view_zoom_in(v);
}

static void
act_zoom_out(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    NsProcView *v = current_view(ud);
    if (v)
        ns_proc_view_zoom_out(v);
}

static void
act_zoom_reset(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    NsProcView *v = current_view(ud);
    if (v)
        ns_proc_view_zoom_reset(v);
}

static void
act_step_tab(ProcWindow *pw, int delta)
{
    GtkNotebook *nb = GTK_NOTEBOOK(pw->notebook);
    int n = gtk_notebook_get_n_pages(nb);
    if (n <= 1)
        return;
    int idx = gtk_notebook_get_current_page(nb);
    idx = ((idx + delta) % n + n) % n;
    gtk_notebook_set_current_page(nb, idx);
}

static void
act_next_tab(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    act_step_tab(ud, 1);
}

static void
act_prev_tab(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    act_step_tab(ud, -1);
}

static void
act_quit(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    ProcWindow *pw = ud;
    gtk_window_close(GTK_WINDOW(pw->window));
}

static void act_about(GSimpleAction *action, GVariant *parameter,
                      gpointer user_data);
static void act_settings(GSimpleAction *action, GVariant *parameter,
                         gpointer user_data);
static void on_bookmarks_clicked(GtkButton *button, gpointer user_data);

/* ---- Task manager: lists each tab's sandboxed renderer process ---- */

typedef struct {
    ProcWindow *pw;
    GtkWidget  *list;
    guint       timer;
} NsTaskMgr;

static GtkWidget *
task_mgr_header_label(const char *text, int width, gfloat xalign, gboolean expand)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), xalign);
    if (width > 0) gtk_label_set_width_chars(GTK_LABEL(l), width);
    if (expand) gtk_widget_set_hexpand(l, TRUE);
    gtk_widget_add_css_class(l, "heading");
    return l;
}

static void
task_mgr_refresh(NsTaskMgr *tm)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(tm->list)))
        gtk_list_box_remove(GTK_LIST_BOX(tm->list), child);

    int n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(tm->pw->notebook));
    for (int i = 0; i < n; i++) {
        NsProcView *v = view_for_page(
            gtk_notebook_get_nth_page(GTK_NOTEBOOK(tm->pw->notebook), i));
        if (!v) continue;

        int pid = ns_proc_view_renderer_pid(v);
        char state[32] = "starting";
        long rss = -1;
        if (pid > 0) {
            ns_rproc_http_proc_info(pid, state, sizeof state, &rss);
        } else if (ns_rproc_single_process_enabled()) {
            pid = ns_rproc_self_pid();
            ns_rproc_http_proc_info(pid, state, sizeof state, &rss);
            g_strlcpy(state, "in-process", sizeof state);
        }

        const char *title = ns_proc_view_title(v);
        const char *url = ns_proc_view_url(v);
        const char *name = (title && *title) ? title
                         : (url && *url)     ? url : ns_i18n("New Tab");

        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_set_margin_start(box, 10);
        gtk_widget_set_margin_end(box, 10);
        gtk_widget_set_margin_top(box, 5);
        gtk_widget_set_margin_bottom(box, 5);

        GtkWidget *l_name = gtk_label_new(name);
        gtk_label_set_xalign(GTK_LABEL(l_name), 0);
        gtk_label_set_ellipsize(GTK_LABEL(l_name), PANGO_ELLIPSIZE_END);
        gtk_widget_set_hexpand(l_name, TRUE);

        char pidbuf[24];
        if (pid > 0) g_snprintf(pidbuf, sizeof pidbuf, "%d", pid);
        else         g_strlcpy(pidbuf, "—", sizeof pidbuf);
        GtkWidget *l_pid = gtk_label_new(pidbuf);
        gtk_label_set_width_chars(GTK_LABEL(l_pid), 8);
        gtk_label_set_xalign(GTK_LABEL(l_pid), 1);

        GtkWidget *l_state = gtk_label_new(state);
        gtk_label_set_width_chars(GTK_LABEL(l_state), 11);
        gtk_label_set_xalign(GTK_LABEL(l_state), 0);

        char membuf[24];
        if (rss >= 0) g_snprintf(membuf, sizeof membuf, "%.1f MB", rss / 1024.0);
        else          g_strlcpy(membuf, "—", sizeof membuf);
        GtkWidget *l_mem = gtk_label_new(membuf);
        gtk_label_set_width_chars(GTK_LABEL(l_mem), 10);
        gtk_label_set_xalign(GTK_LABEL(l_mem), 1);

        gtk_box_append(GTK_BOX(box), l_name);
        gtk_box_append(GTK_BOX(box), l_pid);
        gtk_box_append(GTK_BOX(box), l_state);
        gtk_box_append(GTK_BOX(box), l_mem);

        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
        g_object_set_data(G_OBJECT(row), "ns-view", v);
        gtk_list_box_append(GTK_LIST_BOX(tm->list), row);
    }
}

static gboolean
task_mgr_tick(gpointer data)
{
    task_mgr_refresh(data);
    return G_SOURCE_CONTINUE;
}

static void
task_mgr_end_task(GtkButton *button, gpointer data)
{
    (void)button;
    NsTaskMgr *tm = data;
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(tm->list));
    NsProcView *v = row ? g_object_get_data(G_OBJECT(row), "ns-view") : NULL;
    if (v) ns_proc_view_end_task(v);
    task_mgr_refresh(tm);
}

static void
task_mgr_refresh_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    task_mgr_refresh(data);
}

static void
task_mgr_destroyed(GtkWidget *win, gpointer data)
{
    (void)win;
    NsTaskMgr *tm = data;
    if (tm->timer) g_source_remove(tm->timer);
    if (tm->pw->task_mgr_win) tm->pw->task_mgr_win = NULL;
    g_free(tm);
}

static void
act_task_manager(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    ProcWindow *pw = user_data;
    if (pw->task_mgr_win) {
        gtk_window_present(GTK_WINDOW(pw->task_mgr_win));
        return;
    }

    GtkWidget *win = gtk_window_new();
    char *tm_title = g_strdup_printf("%s — %s", ns_i18n("Task Manager"),
                                     ns_i18n("Nordstjernen"));
    gtk_window_set_title(GTK_WINDOW(win), tm_title);
    g_free(tm_title);
    gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(pw->window));
    gtk_window_set_default_size(GTK_WINDOW(win), 560, 380);

    NsTaskMgr *tm = g_new0(NsTaskMgr, 1);
    tm->pw = pw;

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(hdr, 10);
    gtk_widget_set_margin_end(hdr, 10);
    gtk_widget_set_margin_top(hdr, 8);
    gtk_widget_set_margin_bottom(hdr, 4);
    gtk_box_append(GTK_BOX(hdr), task_mgr_header_label(ns_i18n("Task"), 0, 0, TRUE));
    gtk_box_append(GTK_BOX(hdr), task_mgr_header_label(ns_i18n("Process ID"), 8, 1, FALSE));
    gtk_box_append(GTK_BOX(hdr), task_mgr_header_label(ns_i18n("State"), 11, 0, FALSE));
    gtk_box_append(GTK_BOX(hdr), task_mgr_header_label(ns_i18n("Memory"), 10, 1, FALSE));

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    tm->list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(tm->list), GTK_SELECTION_SINGLE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), tm->list);

    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(bar, 10);
    gtk_widget_set_margin_end(bar, 10);
    gtk_widget_set_margin_top(bar, 6);
    gtk_widget_set_margin_bottom(bar, 8);
    GtkWidget *spacer = gtk_label_new("");
    gtk_widget_set_hexpand(spacer, TRUE);
    GtkWidget *refresh_btn = gtk_button_new_with_label(ns_i18n("Refresh"));
    g_signal_connect(refresh_btn, "clicked",
                     G_CALLBACK(task_mgr_refresh_clicked), tm);
    GtkWidget *end_btn = gtk_button_new_with_label(ns_i18n("End task"));
    gtk_widget_add_css_class(end_btn, "destructive-action");
    g_signal_connect(end_btn, "clicked", G_CALLBACK(task_mgr_end_task), tm);
    gtk_box_append(GTK_BOX(bar), spacer);
    gtk_box_append(GTK_BOX(bar), refresh_btn);
    gtk_box_append(GTK_BOX(bar), end_btn);

    gtk_box_append(GTK_BOX(vbox), hdr);
    gtk_box_append(GTK_BOX(vbox), scroll);
    gtk_box_append(GTK_BOX(vbox), bar);
    gtk_window_set_child(GTK_WINDOW(win), vbox);

    pw->task_mgr_win = win;
    g_signal_connect(win, "destroy", G_CALLBACK(task_mgr_destroyed), tm);

    task_mgr_refresh(tm);
    tm->timer = g_timeout_add(1500, task_mgr_tick, tm);
    gtk_window_present(GTK_WINDOW(win));
}

static void
install_action(ProcWindow *pw, const char *name, GCallback cb,
               const char *const *accels)
{
    GSimpleAction *act = g_simple_action_new(name, NULL);
    g_signal_connect(act, "activate", cb, pw);
    g_action_map_add_action(G_ACTION_MAP(pw->window), G_ACTION(act));
    g_object_unref(act);
    if (accels) {
        char *full = g_strconcat("win.", name, NULL);
        gtk_application_set_accels_for_action(pw->app, full, accels);
        g_free(full);
    }
}

static void
install_shortcuts(ProcWindow *pw)
{
    install_action(pw, "back", G_CALLBACK(act_back),
                   (const char *[]){ "<Alt>Left", NULL });
    install_action(pw, "forward", G_CALLBACK(act_forward),
                   (const char *[]){ "<Alt>Right", NULL });
    install_action(pw, "reload", G_CALLBACK(act_reload),
                   (const char *[]){ "<Ctrl>r", "F5", NULL });
    install_action(pw, "console", G_CALLBACK(act_console),
                   (const char *[]){ NULL });
    install_action(pw, "home", G_CALLBACK(act_home),
                   (const char *[]){ "<Alt>Home", NULL });
    install_action(pw, "new-tab", G_CALLBACK(act_new_tab),
                   (const char *[]){ "<Ctrl>t", NULL });
    install_action(pw, "close-tab", G_CALLBACK(act_close_tab),
                   (const char *[]){ "<Ctrl>w", NULL });
    install_action(pw, "focus-address", G_CALLBACK(act_focus_address),
                   (const char *[]){ "<Ctrl>l", NULL });
    install_action(pw, "focus-page", G_CALLBACK(act_focus_page),
                   (const char *[]){ "Escape", NULL });
    install_action(pw, "zoom-in", G_CALLBACK(act_zoom_in),
                   (const char *[]){ "<Ctrl>plus", "<Ctrl>equal",
                                     "<Ctrl>KP_Add", NULL });
    install_action(pw, "zoom-out", G_CALLBACK(act_zoom_out),
                   (const char *[]){ "<Ctrl>minus", "<Ctrl>KP_Subtract",
                                     NULL });
    install_action(pw, "zoom-reset", G_CALLBACK(act_zoom_reset),
                   (const char *[]){ "<Ctrl>0", "<Ctrl>KP_0", NULL });
    install_action(pw, "next-tab", G_CALLBACK(act_next_tab),
                   (const char *[]){ "<Ctrl>Page_Down", "<Ctrl>Tab", NULL });
    install_action(pw, "prev-tab", G_CALLBACK(act_prev_tab),
                   (const char *[]){ "<Ctrl>Page_Up", "<Ctrl><Shift>Tab",
                                     NULL });
    install_action(pw, "task-manager", G_CALLBACK(act_task_manager),
                   (const char *[]){ "<Shift>Escape", NULL });
    install_action(pw, "about", G_CALLBACK(act_about), NULL);
    install_action(pw, "settings", G_CALLBACK(act_settings),
                   (const char *[]){ "<Ctrl>comma", NULL });
    install_action(pw, "quit", G_CALLBACK(act_quit),
                   (const char *[]){ "<Ctrl>q", NULL });
}

static ProcWindow *
proc_window_new(GtkApplication *app, const char *home_url)
{
    ProcWindow *pw = g_new0(ProcWindow, 1);
    pw->app = app;
    pw->home_url = g_strdup(home_url && *home_url ? home_url : "about:start");
    pw->bookmarks = ns_bookmarks_load();
    pw->window = gtk_application_window_new(app);
    g_object_set_data_full(G_OBJECT(pw->window), "ns-procwindow", pw,
                           (GDestroyNotify)procwindow_free);
    gtk_window_set_title(GTK_WINDOW(pw->window), ns_brand_versioned());
    gtk_window_set_default_size(GTK_WINDOW(pw->window), 1024, 768);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_margin_top(toolbar, 4);
    gtk_widget_set_margin_bottom(toolbar, 4);
    gtk_widget_set_margin_start(toolbar, 4);
    gtk_widget_set_margin_end(toolbar, 4);

    pw->back = toolbar_button("nordstjernen-back", ns_i18n("Back"),
                              G_CALLBACK(on_back_clicked), pw);
    pw->forward = toolbar_button("nordstjernen-forward", ns_i18n("Forward"),
                                 G_CALLBACK(on_forward_clicked), pw);
    pw->reload = toolbar_button("nordstjernen-reload", ns_i18n("Reload"),
                                G_CALLBACK(on_reload_clicked), pw);
    GtkWidget *home = toolbar_button("nordstjernen-home", ns_i18n("Home"),
                                     G_CALLBACK(on_home_clicked), pw);

    pw->spinner = gtk_spinner_new();
    gtk_widget_set_tooltip_text(pw->spinner, ns_i18n("Loading"));
    gtk_widget_set_valign(pw->spinner, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(pw->spinner, FALSE);

    pw->address = gtk_entry_new();
    gtk_widget_set_hexpand(pw->address, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(pw->address),
                                   ns_i18n("Enter a URL and press Enter"));
    g_signal_connect(pw->address, "activate",
                     G_CALLBACK(on_address_activate), pw);

    GtkWidget *go = toolbar_button("nordstjernen-go", ns_i18n("Go"),
                                   G_CALLBACK(on_go_clicked), pw);
    GtkWidget *newtab = toolbar_button("tab-new-symbolic", ns_i18n("New tab"),
                                       G_CALLBACK(on_newtab_clicked), pw);
    pw->bookmarks_button = toolbar_button("user-bookmarks-symbolic",
                                          ns_i18n("Bookmarks"),
                                          G_CALLBACK(on_bookmarks_clicked), pw);

    GMenu *appmenu = g_menu_new();
    g_menu_append(appmenu, ns_i18n("New Tab"), "win.new-tab");
    g_menu_append(appmenu, ns_i18n("Reload"), "win.reload");
    g_menu_append(appmenu, ns_i18n("JavaScript Console"), "win.console");
    g_menu_append(appmenu, ns_i18n("Task Manager"), "win.task-manager");
    g_menu_append(appmenu, ns_i18n("Settings"), "win.settings");
    GMenu *appmenu_about = g_menu_new();
    g_menu_append(appmenu_about, ns_i18n("About Nordstjernen"), "win.about");
    g_menu_append_section(appmenu, NULL, G_MENU_MODEL(appmenu_about));
    g_object_unref(appmenu_about);
    GtkWidget *menu_button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_button),
                                  "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button),
                                   G_MENU_MODEL(appmenu));
    gtk_widget_set_tooltip_text(menu_button, ns_i18n("Menu"));
    g_object_unref(appmenu);

    GtkWidget *logo = gtk_image_new_from_icon_name("nordstjernen");
    gtk_image_set_pixel_size(GTK_IMAGE(logo), 24);
    GtkWidget *logo_button = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(logo_button), logo);
    gtk_button_set_has_frame(GTK_BUTTON(logo_button), FALSE);
    gtk_widget_set_tooltip_text(logo_button, ns_i18n("Visit nordstjernen.org"));
    g_signal_connect(logo_button, "clicked", G_CALLBACK(on_logo_clicked), pw);

    gtk_box_append(GTK_BOX(toolbar), pw->back);
    gtk_box_append(GTK_BOX(toolbar), pw->forward);
    gtk_box_append(GTK_BOX(toolbar), pw->reload);
    gtk_box_append(GTK_BOX(toolbar), home);
    gtk_box_append(GTK_BOX(toolbar), pw->spinner);
    gtk_box_append(GTK_BOX(toolbar), pw->address);
    gtk_box_append(GTK_BOX(toolbar), go);
    gtk_box_append(GTK_BOX(toolbar), newtab);
    gtk_box_append(GTK_BOX(toolbar), pw->bookmarks_button);
    gtk_box_append(GTK_BOX(toolbar), menu_button);
    gtk_box_append(GTK_BOX(toolbar), logo_button);
    gtk_box_append(GTK_BOX(vbox), toolbar);

    pw->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(pw->notebook), TRUE);
    gtk_widget_set_hexpand(pw->notebook, TRUE);
    gtk_widget_set_vexpand(pw->notebook, TRUE);
    g_signal_connect(pw->notebook, "switch-page",
                     G_CALLBACK(on_switch_page), pw);
    gtk_box_append(GTK_BOX(vbox), pw->notebook);

    pw->status = gtk_label_new("");
    gtk_widget_set_halign(pw->status, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(pw->status), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(pw->status, "ns-procstatus");
    gtk_box_append(GTK_BOX(vbox), pw->status);

    gtk_window_set_child(GTK_WINDOW(pw->window), vbox);
    install_shortcuts(pw);
    return pw;
}

#define NS_ABOUT_LICENSE_CORE \
    "Nordstjernen is distributed under the Nordstjernen Source License " \
    "v1.0 (NSL-1.0), © 2026 Andreas Røsdal. See License.md for the full " \
    "terms.\n" \
    "\n" \
    "This program includes third-party open-source software. Full " \
    "license texts are in THIRD-PARTY-LICENSES.md.\n" \
    "\n" \
    "Statically linked:\n" \
    "  • lexbor — Apache License 2.0, © 2018–2025 Alexander Borisov\n" \
    "  • Wuffs — Apache License 2.0, © 2017 The Wuffs Authors\n" \
    "  • quickjs-ng — MIT License, © Fabrice Bellard, Charlie Gordon and " \
    "the quickjs-ng contributors\n" \
    "  • WebAssembly Micro Runtime — Apache License 2.0 with LLVM " \
    "exceptions\n" \
    "\n" \
    "Dynamically linked:\n" \
    "  • GTK 4, GLib, Pango, gdk-pixbuf — GNU LGPL 2.1 or later\n" \
    "  • Cairo — GNU LGPL 2.1 or MPL 1.1\n" \
    "  • libcurl — curl license (MIT-style), © 1996–2026 Daniel Stenberg " \
    "and contributors\n" \
    "  • OpenSSL (libcrypto) — Apache License 2.0, © The OpenSSL Project\n" \
    "  • uchardet — MPL 1.1 / LGPL 2.1+ / GPL 2.0+\n" \
    "  • libwebp — BSD 3-Clause, © Google Inc.\n" \
    "  • libpsl — MIT License, © Tim Rühsen\n" \
    "  • SQLite — public domain\n" \
    "  • libepoxy — MIT License, © Intel Corporation\n" \
    "  • zlib — zlib License, © Jean-loup Gailly and Mark Adler\n" \
    "  • libseccomp (Linux) — GNU LGPL 2.1\n" \
    "  • librsvg (Windows/macOS bundles) — GNU LGPL 2.1 or later\n"

#ifdef NS_HAVE_AI
#define NS_ABOUT_LICENSE_AI \
    "\n" \
    "Local AI inference (optional feature):\n" \
    "  • llama.cpp and ggml — MIT License, © 2023–2024 The ggml authors\n" \
    "\n" \
    "AI models are downloaded at your request and are not bundled. Your " \
    "use of a downloaded model is governed by that model's license:\n" \
    "  • Built with Llama. Meta Llama 3.1 8B Instruct — Llama 3.1 " \
    "Community License Agreement, © Meta Platforms, Inc. " \
    "(https://www.llama.com/llama3_1/license/)\n" \
    "  • Qwen2.5 0.5B and 1.5B Instruct — Apache License 2.0, © Alibaba " \
    "Cloud\n" \
    "  • Qwen2.5 3B Instruct — Qwen Research License Agreement " \
    "(research / non-commercial use only), © Alibaba Cloud\n"
#else
#define NS_ABOUT_LICENSE_AI ""
#endif

#define NS_ABOUT_LICENSE_TEXT NS_ABOUT_LICENSE_CORE NS_ABOUT_LICENSE_AI

static void
act_about(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    ProcWindow *pw = user_data;
    const char *authors[] = { "Andreas Røsdal", NULL };
    const char *comments =
        "Northstar — the legendary web browser.\n"
        "A clean-room browser in C with GTK 4 and libcurl, "
        "rendering each tab in a sandboxed process."
#ifdef NS_HAVE_AI
        "\n\nBuilt with Llama. The optional local AI assistant can run "
        "Meta Llama 3.1 and Alibaba Qwen2.5 models that you choose to "
        "download."
#endif
        ;
    gtk_show_about_dialog(
        GTK_WINDOW(pw->window),
        "program-name", ns_i18n("Nordstjernen"),
        "version", NS_VERSION,
        "comments", ns_i18n(comments),
        "website", "https://nordstjernen.org",
        "website-label", "nordstjernen.org",
        "authors", authors,
        "copyright", "© 2026 Andreas Røsdal",
        "license-type", GTK_LICENSE_CUSTOM,
        "license", NS_ABOUT_LICENSE_TEXT,
        "wrap-license", TRUE,
        "logo-icon-name", "nordstjernen",
        NULL);
}

static const struct { const char *name; const char *url; } k_search_engines[] = {
    { "DuckDuckGo Lite", "https://lite.duckduckgo.com/lite/?q=%s" },
    { "DuckDuckGo",      "https://duckduckgo.com/?q=%s" },
    { "Baidu",           "https://www.baidu.com/s?wd=%s" },
    { "Google",          "https://www.google.com/search?q=%s" },
    { "Bing",            "https://www.bing.com/search?q=%s" },
    { "Yandex",          "https://yandex.com/search/?text=%s" },
    { "Yahoo",           "https://search.yahoo.com/search?p=%s" },
    { "Yahoo! Japan",    "https://search.yahoo.co.jp/search?p=%s" },
    { "Sogou",           "https://www.sogou.com/web?query=%s" },
    { "Naver",           "https://search.naver.com/search.naver?query=%s" },
    { "Startpage",       "https://www.startpage.com/sp/search?query=%s" },
    { "Brave Search",    "https://search.brave.com/search?q=%s" },
    { "Ecosia",          "https://www.ecosia.org/search?q=%s" },
};

typedef struct {
    ProcWindow *pw;
    GtkWidget  *window;
    GtkWidget  *home;
    GtkWidget  *search;
    GtkWidget  *search_dd;
    GtkWidget  *font_size;
    GtkWidget  *images;
    GtkWidget  *webgl;
    GtkWidget  *storage;
    GtkWidget  *dnt;
    GtkWidget  *cache;
} SettingsDlg;

static void
settings_set_str(char **field, const char *value)
{
    g_free(*field);
    *field = g_strdup(value ? value : "");
}

static void
on_search_engine_selected(GObject *dd, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    SettingsDlg *s = user_data;
    guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(dd));
    if (sel < G_N_ELEMENTS(k_search_engines)) {
        gtk_editable_set_text(GTK_EDITABLE(s->search),
                              k_search_engines[sel].url);
        gtk_widget_set_sensitive(s->search, FALSE);
    } else {
        gtk_widget_set_sensitive(s->search, TRUE);
        gtk_widget_grab_focus(s->search);
    }
}

static void
on_settings_save(GtkButton *button, gpointer user_data)
{
    (void)button;
    SettingsDlg *s = user_data;
    ns_config *cfg = ns_config_mut();
    if (cfg) {
        settings_set_str(&cfg->home_url,
            gtk_editable_get_text(GTK_EDITABLE(s->home)));
        settings_set_str(&cfg->search_engine,
            gtk_editable_get_text(GTK_EDITABLE(s->search)));
        cfg->default_font_size_px =
            gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->font_size));
        cfg->images_enabled = gtk_switch_get_active(GTK_SWITCH(s->images));
        cfg->webgl_enabled = gtk_switch_get_active(GTK_SWITCH(s->webgl));
        cfg->local_storage_enabled =
            gtk_switch_get_active(GTK_SWITCH(s->storage));
        cfg->do_not_track = gtk_switch_get_active(GTK_SWITCH(s->dnt));
        cfg->cache_enabled = gtk_switch_get_active(GTK_SWITCH(s->cache));
        ns_config_save(NULL);
        g_free(s->pw->home_url);
        s->pw->home_url = g_strdup(cfg->home_url);
    }
    gtk_window_destroy(GTK_WINDOW(s->window));
}

static void
on_settings_close(GtkButton *button, gpointer user_data)
{
    (void)button;
    SettingsDlg *s = user_data;
    gtk_window_destroy(GTK_WINDOW(s->window));
}

static GtkWidget *
settings_add_switch(GtkGrid *grid, int row, const char *label, gboolean on)
{
    GtkWidget *l = gtk_label_new(label);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    GtkWidget *sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw), on);
    gtk_widget_set_halign(sw, GTK_ALIGN_START);
    gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
    gtk_grid_attach(grid, l, 0, row, 1, 1);
    gtk_grid_attach(grid, sw, 1, row, 1, 1);
    return sw;
}

static void
act_settings(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    ProcWindow *pw = user_data;
    const ns_config *cfg = ns_config_get();

    SettingsDlg *s = g_new0(SettingsDlg, 1);
    s->pw = pw;
    s->window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(s->window), ns_i18n("Settings"));
    gtk_window_set_transient_for(GTK_WINDOW(s->window), GTK_WINDOW(pw->window));
    gtk_window_set_modal(GTK_WINDOW(s->window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(s->window), 460, -1);
    g_object_set_data_full(G_OBJECT(s->window), "ns-settings", s, g_free);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 14);

    GtkWidget *home_l = gtk_label_new(ns_i18n("Home page"));
    gtk_widget_set_halign(home_l, GTK_ALIGN_START);
    s->home = gtk_entry_new();
    gtk_widget_set_hexpand(s->home, TRUE);
    gtk_editable_set_text(GTK_EDITABLE(s->home),
                          cfg && cfg->home_url ? cfg->home_url : "");
    gtk_grid_attach(GTK_GRID(grid), home_l, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), s->home, 1, 0, 1, 1);

    const char *cur_engine =
        cfg && cfg->search_engine ? cfg->search_engine : "";
    guint match = G_N_ELEMENTS(k_search_engines);
    for (guint i = 0; i < G_N_ELEMENTS(k_search_engines); i++)
        if (g_strcmp0(cur_engine, k_search_engines[i].url) == 0) {
            match = i;
            break;
        }

    GtkWidget *search_l = gtk_label_new(ns_i18n("Search engine"));
    gtk_widget_set_halign(search_l, GTK_ALIGN_START);
    const char *dd_names[G_N_ELEMENTS(k_search_engines) + 2];
    for (guint i = 0; i < G_N_ELEMENTS(k_search_engines); i++)
        dd_names[i] = k_search_engines[i].name;
    dd_names[G_N_ELEMENTS(k_search_engines)] = ns_i18n("Custom\xe2\x80\xa6");
    dd_names[G_N_ELEMENTS(k_search_engines) + 1] = NULL;
    s->search_dd = gtk_drop_down_new_from_strings(dd_names);
    gtk_widget_set_hexpand(s->search_dd, TRUE);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(s->search_dd), match);
    gtk_grid_attach(GTK_GRID(grid), search_l, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), s->search_dd, 1, 1, 1, 1);

    GtkWidget *custom_l = gtk_label_new(ns_i18n("Custom URL"));
    gtk_widget_set_halign(custom_l, GTK_ALIGN_START);
    s->search = gtk_entry_new();
    gtk_widget_set_hexpand(s->search, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(s->search),
                                   "https://example.com/search?q=%s");
    gtk_editable_set_text(GTK_EDITABLE(s->search), cur_engine);
    gtk_widget_set_sensitive(s->search,
                             match >= G_N_ELEMENTS(k_search_engines));
    gtk_grid_attach(GTK_GRID(grid), custom_l, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), s->search, 1, 2, 1, 1);
    g_signal_connect(s->search_dd, "notify::selected",
                     G_CALLBACK(on_search_engine_selected), s);

    GtkWidget *font_l = gtk_label_new(ns_i18n("Default font size"));
    gtk_widget_set_halign(font_l, GTK_ALIGN_START);
    s->font_size = gtk_spin_button_new_with_range(8, 32, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->font_size),
                              cfg ? cfg->default_font_size_px : 16);
    gtk_widget_set_halign(s->font_size, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), font_l, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), s->font_size, 1, 3, 1, 1);

    s->images  = settings_add_switch(GTK_GRID(grid), 4, ns_i18n("Load images"),
                                     cfg ? cfg->images_enabled : TRUE);
    s->webgl   = settings_add_switch(GTK_GRID(grid), 5, ns_i18n("Enable WebGL"),
                                     cfg ? cfg->webgl_enabled : FALSE);
    s->storage = settings_add_switch(GTK_GRID(grid), 6, ns_i18n("Enable local storage"),
                                     cfg ? cfg->local_storage_enabled : TRUE);
    s->dnt     = settings_add_switch(GTK_GRID(grid), 7, ns_i18n("Send Do Not Track"),
                                     cfg ? cfg->do_not_track : FALSE);
    s->cache   = settings_add_switch(GTK_GRID(grid), 8, ns_i18n("Enable cache"),
                                     cfg ? cfg->cache_enabled : TRUE);

    gtk_box_append(GTK_BOX(box), grid);

    GtkWidget *note = gtk_label_new(
        ns_i18n("Changes apply to newly opened pages."));
    gtk_widget_add_css_class(note, "dim-label");
    gtk_widget_set_halign(note, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), note);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget *cancel = gtk_button_new_with_label(ns_i18n("Cancel"));
    GtkWidget *save = gtk_button_new_with_label(ns_i18n("Save"));
    gtk_widget_add_css_class(save, "suggested-action");
    g_signal_connect(cancel, "clicked", G_CALLBACK(on_settings_close), s);
    g_signal_connect(save, "clicked", G_CALLBACK(on_settings_save), s);
    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(buttons), save);
    gtk_box_append(GTK_BOX(box), buttons);

    gtk_window_set_child(GTK_WINDOW(s->window), box);
    gtk_window_present(GTK_WINDOW(s->window));
}

static void
on_bookmark_activate(GtkButton *button, gpointer user_data)
{
    ProcWindow *pw = user_data;
    const char *url = g_object_get_data(G_OBJECT(button), "ns-bm-url");
    NsProcView *v = current_view(pw);
    if (url && v)
        ns_proc_view_load(v, url);
    GtkWidget *pop = gtk_widget_get_ancestor(GTK_WIDGET(button),
                                             GTK_TYPE_POPOVER);
    if (pop)
        gtk_popover_popdown(GTK_POPOVER(pop));
}

static void
on_bookmark_remove(GtkButton *button, gpointer user_data)
{
    ProcWindow *pw = user_data;
    const char *url = g_object_get_data(G_OBJECT(button), "ns-bm-url");
    if (url && pw->bookmarks) {
        ns_bookmarks_remove(pw->bookmarks, url);
        GtkWidget *row = gtk_widget_get_parent(GTK_WIDGET(button));
        GtkWidget *list = row ? gtk_widget_get_parent(row) : NULL;
        if (list && row)
            gtk_box_remove(GTK_BOX(list), row);
    }
}

static void
on_add_bookmark(GtkButton *button, gpointer user_data)
{
    (void)button;
    ProcWindow *pw = user_data;
    NsProcView *v = current_view(pw);
    if (!v || !pw->bookmarks)
        return;
    const char *url = ns_proc_view_url(v);
    const char *title = ns_proc_view_title(v);
    if (url && *url && !ns_bookmarks_contains(pw->bookmarks, url)) {
        ns_bookmarks_add(pw->bookmarks, url, title);
        gtk_label_set_text(GTK_LABEL(pw->status), ns_i18n("Bookmark added"));
    }
}

static GtkWidget *
build_bookmarks_popover(ProcWindow *pw)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);
    gtk_widget_set_size_request(box, 320, -1);

    GtkWidget *add = gtk_button_new_with_label(ns_i18n("Bookmark this page"));
    g_signal_connect(add, "clicked", G_CALLBACK(on_add_bookmark), pw);
    gtk_box_append(GTK_BOX(box), add);
    gtk_box_append(GTK_BOX(box),
                   gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 280);
    GtkWidget *list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    guint n = pw->bookmarks ? ns_bookmarks_count(pw->bookmarks) : 0;
    if (n == 0) {
        GtkWidget *empty = gtk_label_new(ns_i18n("No bookmarks yet"));
        gtk_widget_add_css_class(empty, "dim-label");
        gtk_box_append(GTK_BOX(list), empty);
    }
    for (guint i = 0; i < n; i++) {
        const ns_bookmark *bm = ns_bookmarks_get(pw->bookmarks, i);
        if (!bm || !bm->url) continue;
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        GtkWidget *open = gtk_button_new_with_label(
            (bm->title && *bm->title) ? bm->title : bm->url);
        gtk_button_set_has_frame(GTK_BUTTON(open), FALSE);
        gtk_widget_set_hexpand(open, TRUE);
        gtk_widget_set_halign(open, GTK_ALIGN_START);
        gtk_widget_set_tooltip_text(open, bm->url);
        g_object_set_data_full(G_OBJECT(open), "ns-bm-url",
                               g_strdup(bm->url), g_free);
        g_signal_connect(open, "clicked", G_CALLBACK(on_bookmark_activate), pw);
        GtkWidget *del = gtk_button_new_from_icon_name("user-trash-symbolic");
        gtk_button_set_has_frame(GTK_BUTTON(del), FALSE);
        g_object_set_data_full(G_OBJECT(del), "ns-bm-url",
                               g_strdup(bm->url), g_free);
        g_signal_connect(del, "clicked", G_CALLBACK(on_bookmark_remove), pw);
        gtk_box_append(GTK_BOX(row), open);
        gtk_box_append(GTK_BOX(row), del);
        gtk_box_append(GTK_BOX(list), row);
    }
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(box), scroll);

    GtkWidget *pop = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(pop), box);
    return pop;
}

static void
on_bookmarks_clicked(GtkButton *button, gpointer user_data)
{
    ProcWindow *pw = user_data;
    GtkWidget *pop = build_bookmarks_popover(pw);
    gtk_widget_set_parent(pop, GTK_WIDGET(button));
    gtk_popover_set_position(GTK_POPOVER(pop), GTK_POS_BOTTOM);
    g_signal_connect(pop, "closed", G_CALLBACK(gtk_widget_unparent), NULL);
    gtk_popover_popup(GTK_POPOVER(pop));
}

typedef struct {
    char    *url;
    char    *session_path;
    gboolean recover;
} ProcAppCtx;

static gboolean
session_url_recoverable(const char *u)
{
    return u && (g_str_has_prefix(u, "http://") ||
                 g_str_has_prefix(u, "https://") ||
                 g_str_has_prefix(u, "ftp://") ||
                 g_str_has_prefix(u, "file://"));
}

static gboolean
write_session_cb(gpointer data)
{
    ProcWindow *pw = data;
    if (!pw->session_path)
        return G_SOURCE_REMOVE;
    GString *s = g_string_new(NULL);
    int n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(pw->notebook));
    for (int i = 0; i < n; i++) {
        NsProcView *v = view_for_page(
            gtk_notebook_get_nth_page(GTK_NOTEBOOK(pw->notebook), i));
        const char *u = v ? ns_proc_view_url(v) : NULL;
        if (session_url_recoverable(u)) {
            g_string_append(s, u);
            g_string_append_c(s, '\n');
        }
    }
    g_file_set_contents(pw->session_path, s->str, (gssize)s->len, NULL);
    g_string_free(s, TRUE);
    return G_SOURCE_CONTINUE;
}

static void
on_proc_activate(GtkApplication *app, gpointer user_data)
{
    ProcAppCtx *ctx = user_data;
    install_icon_search_paths();
    install_status_css();
    ProcWindow *pw = proc_window_new(app, "about:start");
    pw->session_path = g_strdup(ctx->session_path);

    gboolean opened = FALSE;
    if (ctx->recover && ctx->session_path) {
        char *contents = NULL;
        if (g_file_get_contents(ctx->session_path, &contents, NULL, NULL)) {
            char **lines = g_strsplit(contents, "\n", -1);
            for (int i = 0; lines && lines[i]; i++) {
                if (session_url_recoverable(lines[i])) {
                    proc_window_add_tab(pw, lines[i], !opened);
                    opened = TRUE;
                }
            }
            g_strfreev(lines);
        }
        g_free(contents);
        if (opened)
            gtk_label_set_text(GTK_LABEL(pw->status),
                               ns_i18n("Recovered the previous session after "
                                       "an unexpected exit"));
    }
    if (!opened)
        proc_window_add_tab(pw, ctx->url ? ctx->url : "about:start", TRUE);

    if (pw->session_path)
        pw->session_timer = g_timeout_add_seconds(4, write_session_cb, pw);

    gtk_window_present(GTK_WINDOW(pw->window));
}

int
ns_procapp_run(const char *startup_url, const char *session_path,
               gboolean recover)
{
    ProcAppCtx ctx = {
        .url = g_strdup(startup_url),
        .session_path = g_strdup(session_path),
        .recover = recover,
    };
    GtkApplication *app =
        gtk_application_new(NS_PROC_APP_ID, G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(on_proc_activate), &ctx);
    int status = g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);
    g_free(ctx.url);
    g_free(ctx.session_path);
    return status;
}
