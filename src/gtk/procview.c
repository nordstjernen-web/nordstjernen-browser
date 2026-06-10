/* Nordstjernen — GTK thin client over the out-of-process renderer (rproc). */

#include "procview.h"
#include "i18n.h"

#include "media.h"
#include "proc_limits.h"
#include "rproc_http.h"

#include <cairo.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

static int
pv_settle_ms(void)
{
    const char *e = g_getenv(NS_PROC_SETTLE_ENV);
    if (e && *e) {
        int v = atoi(e);
        if (v >= 0 && v <= 10000)
            return v;
    }
    return NS_PROC_SETTLE_MS;
}

typedef enum {
    REQ_LOAD, REQ_RENDER, REQ_LINK, REQ_CLICK, REQ_VIEWPORT, REQ_KEY,
    REQ_SELECT, REQ_HOVER, REQ_RELEASE, REQ_FIND, REQ_EXPORT, REQ_CONSOLE,
    REQ_EVAL,
    REQ_WEBGL, REQ_QUIT
} ReqType;
typedef enum { ACT_HOVER, ACT_NAVIGATE, ACT_NEWTAB, ACT_CONTEXT } LinkAct;

typedef struct {
    ReqType type;
    int     seq;
    char   *url;
    int     vw;
    int     vh;
    int     w, h, sx, sy;
    double  scale;
    int     x, y;
    int     mods;
    int     kind;
    int     keycode;
    char   *key;
    char   *code;
    LinkAct action;
    char   *query;
    int     find_dir;
    int     find_from_y;
    int     find_case;
    char   *export_dest;
} Req;

typedef enum {
    RES_PAGE, RES_FRAME, RES_LINK, RES_CLICK, RES_VIEWPORT, RES_KEY,
    RES_SELECT, RES_COPY, RES_HOVER, RES_RELEASE, RES_FIND, RES_EXPORT,
    RES_CONSOLE, RES_EVAL
} ResType;

typedef struct {
    NsProcView      *view;
    ResType          type;
    int              seq;
    gboolean         ok;
    int              pw, ph;
    char            *title;
    char            *url;
    char            *nav;
    char            *webgl;
    cairo_surface_t *surface;
    char            *href;
    char            *cursor;
    LinkAct          action;
    int              kind;
    gboolean         animating;
    gboolean         frame_unchanged;
    int              find_total, find_current, find_scroll_y;
    char            *media_url;
    int              media_is_video, media_stream;
} Res;

struct NsProcView {
    grefcount   rc;

    GtkWidget     *root;
    GtkWidget     *area;
    GtkIMContext  *im;
    GtkAdjustment *hadj;
    GtkAdjustment *vadj;
    gboolean    closed;

    GThread    *thread;
    GAsyncQueue *queue;
    ns_rproc_http *proc;
    GMutex      proc_lock;
    char       *renderer_path;

    NsProcNotify notify;
    gpointer     notify_ud;

    char       *current_url;
    char       *current_title;
    int         page_w, page_h;
    int         scroll_x, scroll_y;
    gboolean    opened;

    cairo_surface_t *frame;

    gboolean    render_inflight;
    gboolean    render_pending;
    int         render_restarts;

    gboolean    link_inflight;
    gboolean    link_pending;
    int         link_pending_x, link_pending_y;
    LinkAct     link_pending_action;

    gboolean    hover_inflight;
    gboolean    hover_pending;
    int         hover_pending_x, hover_pending_y;

    gboolean    has_selection;
    double      ctx_x, ctx_y;
    char       *ctx_link;
    GtkWidget  *ctx_popover;
    GSimpleActionGroup *ctx_actions;

    GtkWidget  *search_revealer;
    GtkWidget  *search_entry;
    GtkWidget  *search_label;
    int         find_seq;
    gboolean    find_case;

    GtkWidget    *console_revealer;
    GtkWidget    *console_entry;
    GtkWidget    *console_view;
    GtkTextBuffer *console_buffer;
    gboolean      console_open;
    guint         console_poll_id;

    GPtrArray  *history;
    int         hist_index;
    gboolean    pending_record;

    char       *deferred_url;
    gboolean    deferred_record;

    int         js_redirects;

    double      scale;
    gboolean    loading;
    gboolean    busy_cursor;

    int         load_seq, render_seq, link_seq, click_seq, viewport_seq;
    int         key_seq, select_seq, hover_seq;
    int         last_vp_w, last_vp_h;
    double      drag_start_x, drag_start_y;
    gboolean    drag_anchored;

    guint       anim_tick_id;
};

enum {
    NS_PV_ZOOM_MIN_PERMILLE = (int)(NS_PROC_ZOOM_MIN * 1000.0 + 0.5),
    NS_PV_ZOOM_MAX_PERMILLE = (int)(NS_PROC_ZOOM_MAX * 1000.0 + 0.5)
};

static NsProcView *pv_ref(NsProcView *v) { g_ref_count_inc(&v->rc); return v; }

static void
pv_free(NsProcView *v)
{
    if (v->queue) {
        Req *r;
        while ((r = g_async_queue_try_pop(v->queue))) {
            g_free(r->url);
            g_free(r->key);
            g_free(r->code);
            g_free(r->query);
            g_free(r->export_dest);
            g_free(r);
        }
        g_async_queue_unref(v->queue);
    }
    if (v->frame)
        cairo_surface_destroy(v->frame);
    if (v->ctx_popover)
        gtk_widget_unparent(v->ctx_popover);
    if (v->ctx_actions)
        g_object_unref(v->ctx_actions);
    g_free(v->ctx_link);
    if (v->history)
        g_ptr_array_unref(v->history);
    g_free(v->renderer_path);
    g_free(v->current_url);
    g_free(v->current_title);
    g_free(v->deferred_url);
    g_mutex_clear(&v->proc_lock);
    g_free(v);
}

static void pv_unref(NsProcView *v) { if (g_ref_count_dec(&v->rc)) pv_free(v); }

/* Atomically install a new renderer handle (worker thread only) and return the
   previous one for the caller to close outside the lock. The lock serialises
   the worker's reassignments against the main thread's close-time interrupt so
   it can never touch a freed handle. */
static ns_rproc_http *
pv_swap_proc(NsProcView *v, ns_rproc_http *newp)
{
    g_mutex_lock(&v->proc_lock);
    ns_rproc_http *old = v->proc;
    v->proc = newp;
    g_mutex_unlock(&v->proc_lock);
    return old;
}

char *
ns_proc_renderer_path(void)
{
    const char *env = g_getenv(NS_PROC_RENDERER_ENV);
    if (env && *env)
        return g_strdup(env);
#ifdef G_OS_WIN32
    const char *name = NS_PROC_RENDERER_NAME ".exe";
#else
    const char *name = NS_PROC_RENDERER_NAME;
#endif
    const char *exe = ns_app_self_exe();
    if (exe) {
        char *dir = g_path_get_dirname(exe);
        char *parent = g_build_filename("..", name, NULL);
        const char *rel[] = { name, parent, NULL };
        for (int i = 0; rel[i]; i++) {
            char *cand = g_build_filename(dir, rel[i], NULL);
            if (g_file_test(cand, G_FILE_TEST_IS_EXECUTABLE)) {
                g_free(parent);
                g_free(dir);
                return cand;
            }
            g_free(cand);
        }
        g_free(parent);
        g_free(dir);
    }
    return g_strdup(name);
}

static cairo_surface_t *
frame_to_surface(const unsigned char *px, int w, int h, int stride)
{
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(s);
        return NULL;
    }
    unsigned char *dst = cairo_image_surface_get_data(s);
    int dstride = cairo_image_surface_get_stride(s);
    size_t row = (size_t)w * 4u;
    for (int y = 0; y < h; y++)
        memcpy(dst + (size_t)y * dstride, px + (size_t)y * stride, row);
    cairo_surface_mark_dirty(s);
    return s;
}

static void
post_emit(NsProcView *v, NsProcEvent evt, const char *text)
{
    if (v->notify)
        v->notify(v, evt, text, v->notify_ud);
}

static void
clear_busy_cursor(NsProcView *v)
{
    if (!v->busy_cursor)
        return;
    v->busy_cursor = FALSE;
    if (v->area)
        gtk_widget_set_cursor_from_name(v->area, NULL);
}

static void
finish_loading(NsProcView *v)
{
    if (v->loading) {
        v->loading = FALSE;
        post_emit(v, NS_PROC_EVT_LOADING, "0");
    }
}

static gboolean on_result(gpointer data);

static void
post(Res *res)
{
    g_idle_add(on_result, res);
}

static gpointer
worker_main(gpointer data)
{
    NsProcView *v = data;
    for (;;) {
        Req *req = g_async_queue_pop(v->queue);
        if (req->type == REQ_QUIT) {
            g_free(req->url);
            g_free(req);
            break;
        }
        if (!v->proc)
            pv_swap_proc(v, ns_rproc_http_spawn_shm(v->renderer_path,
                                     NS_PROC_MAX_WIDTH, NS_PROC_MAX_HEIGHT));

        if (req->type == REQ_LOAD) {
            Res *res = g_new0(Res, 1);
            res->view = pv_ref(v);
            res->type = RES_PAGE;
            res->seq = req->seq;
            ns_rproc_http_page pg;
            int settle = pv_settle_ms();
            int rc = v->proc ? ns_rproc_http_open(v->proc, req->url, req->vw,
                                             req->vh, settle, &pg)
                             : -1;
            if (rc != 0 && v->proc) {
                ns_rproc_http_close(pv_swap_proc(v, NULL));
                pv_swap_proc(v, ns_rproc_http_spawn_shm(v->renderer_path,
                                         NS_PROC_MAX_WIDTH, NS_PROC_MAX_HEIGHT));
                rc = v->proc ? ns_rproc_http_open(v->proc, req->url, req->vw,
                                             req->vh, settle, &pg)
                             : -1;
            }
            if (rc == 0 && pg.ok) {
                res->ok = TRUE;
                res->pw = pg.page_width;
                res->ph = pg.page_height;
                res->title = g_strdup(pg.title ? pg.title : "");
                res->url = g_strdup(pg.url ? pg.url : req->url);
                res->nav = pg.nav ? g_strdup(pg.nav) : NULL;
            }
            if (rc == 0)
                ns_rproc_http_page_clear(&pg);
            post(res);
        } else if (req->type == REQ_RENDER) {
            Res *res = g_new0(Res, 1);
            res->view = pv_ref(v);
            res->type = RES_FRAME;
            res->seq = req->seq;
            ns_rproc_http_frame fr;
            gboolean rendered = v->proc &&
                ns_rproc_http_render(v->proc, req->w, req->h, req->sx, req->sy,
                                req->scale, &fr) == 0 && fr.ok;
            if (rendered) {
                res->ok = TRUE;
                res->animating = fr.animating ? TRUE : FALSE;
                res->frame_unchanged = fr.unchanged ? TRUE : FALSE;
                if (!fr.unchanged)
                    res->surface = frame_to_surface(fr.pixels, fr.width,
                                                    fr.height, fr.stride);
                if (fr.nav) {
                    res->nav = g_strdup(fr.nav);
                    free(fr.nav);
                }
                if (fr.webgl) {
                    res->webgl = g_strdup(fr.webgl);
                    free(fr.webgl);
                }
            } else if (v->proc) {
                ns_rproc_http_close(pv_swap_proc(v, NULL));
            }
            post(res);
        } else if (req->type == REQ_LINK) {
            Res *res = g_new0(Res, 1);
            res->view = pv_ref(v);
            res->type = RES_LINK;
            res->seq = req->seq;
            res->action = req->action;
            if (v->proc && req->action == ACT_HOVER)
                res->href = ns_rproc_http_link_cursor_at(v->proc, req->x,
                                                         req->y, &res->cursor);
            else if (v->proc)
                res->href = ns_rproc_http_link_at(v->proc, req->x, req->y);
            post(res);
        } else if (req->type == REQ_CLICK) {
            Res *res = g_new0(Res, 1);
            res->view = pv_ref(v);
            res->type = RES_CLICK;
            res->seq = req->seq;
            res->href = v->proc
                ? ns_rproc_http_click(v->proc, req->x, req->y, req->mods)
                : NULL;
            if (v->proc && (!res->href || !*res->href))
                res->media_url = ns_rproc_http_media_at(v->proc, req->x, req->y,
                                                   &res->media_is_video,
                                                   &res->media_stream);
            post(res);
        } else if (req->type == REQ_VIEWPORT) {
            Res *res = g_new0(Res, 1);
            res->view = pv_ref(v);
            res->type = RES_VIEWPORT;
            res->seq = req->seq;
            ns_rproc_http_page pg;
            if (v->proc &&
                ns_rproc_http_set_viewport(v->proc, req->vw, req->vh, &pg) == 0) {
                res->ok = pg.ok;
                res->pw = pg.page_width;
                res->ph = pg.page_height;
                ns_rproc_http_page_clear(&pg);
            }
            post(res);
        } else if (req->type == REQ_KEY) {
            Res *res = g_new0(Res, 1);
            res->view = pv_ref(v);
            res->type = RES_KEY;
            res->seq = req->seq;
            res->kind = req->kind;
            res->href = v->proc
                ? ns_rproc_http_key(v->proc, req->kind, req->key, req->code,
                               req->keycode, req->mods)
                : NULL;
            post(res);
        } else if (req->type == REQ_SELECT) {
            Res *res = g_new0(Res, 1);
            res->view = pv_ref(v);
            res->type = (req->kind == 4) ? RES_COPY : RES_SELECT;
            res->seq = req->seq;
            res->href = v->proc
                ? ns_rproc_http_select(v->proc, req->kind, req->x, req->y)
                : NULL;
            post(res);
        } else if (req->type == REQ_HOVER) {
            Res *res = g_new0(Res, 1);
            res->view = pv_ref(v);
            res->type = RES_HOVER;
            res->seq = req->seq;
            res->ok = v->proc && ns_rproc_http_hover(v->proc, req->x, req->y) == 1;
            post(res);
        } else if (req->type == REQ_RELEASE) {
            Res *res = g_new0(Res, 1);
            res->view = pv_ref(v);
            res->type = RES_RELEASE;
            res->seq = req->seq;
            res->ok = v->proc && ns_rproc_http_release(v->proc) == 1;
            post(res);
        } else if (req->type == REQ_FIND) {
            Res *res = g_new0(Res, 1);
            res->view = pv_ref(v);
            res->type = RES_FIND;
            res->seq = req->seq;
            if (v->proc)
                ns_rproc_http_find(v->proc, req->query, req->find_case,
                              req->find_dir, req->find_from_y,
                              &res->find_total, &res->find_current,
                              &res->find_scroll_y);
            post(res);
        } else if (req->type == REQ_EXPORT) {
            Res *res = g_new0(Res, 1);
            res->view = pv_ref(v);
            res->type = RES_EXPORT;
            res->seq = req->seq;
            gboolean ok = FALSE;
            if (v->proc && req->url && req->export_dest &&
                ns_rproc_http_export(v->proc, req->url) == 0) {
                GFile *src = g_file_new_for_path(req->url);
                GFile *dst = g_file_new_for_path(req->export_dest);
                ok = g_file_copy(src, dst, G_FILE_COPY_OVERWRITE, NULL,
                                 NULL, NULL, NULL);
                g_object_unref(src);
                g_object_unref(dst);
            }
            if (req->url)
                g_unlink(req->url);
            res->ok = ok;
            res->url = g_strdup(req->export_dest ? req->export_dest : "");
            post(res);
        } else if (req->type == REQ_CONSOLE) {
            Res *res = g_new0(Res, 1);
            res->view = pv_ref(v);
            res->type = RES_CONSOLE;
            res->seq = req->seq;
            res->href = v->proc ? ns_rproc_http_console_poll(v->proc) : NULL;
            post(res);
        } else if (req->type == REQ_EVAL) {
            Res *res = g_new0(Res, 1);
            res->view = pv_ref(v);
            res->type = RES_EVAL;
            res->seq = req->seq;
            res->href = v->proc ? ns_rproc_http_eval(v->proc, req->query) : NULL;
            post(res);
        } else if (req->type == REQ_WEBGL) {
            if (v->proc)
                ns_rproc_http_resolve_webgl(v->proc, req->url, req->mods);
        }
        g_free(req->url);
        g_free(req->key);
        g_free(req->code);
        g_free(req->query);
        g_free(req->export_dest);
        g_free(req);
    }
    if (v->proc)
        ns_rproc_http_close(pv_swap_proc(v, NULL));
    pv_unref(v);
    return NULL;
}

static void
push_req(NsProcView *v, Req *req)
{
    g_async_queue_push(v->queue, req);
}

static int
viewport_w(NsProcView *v)
{
    int w = v->area ? gtk_widget_get_width(v->area) : 0;
    return w > 0 ? w : 1;
}

static int
viewport_h(NsProcView *v)
{
    int h = v->area ? gtk_widget_get_height(v->area) : 0;
    return h > 0 ? h : 1;
}

static double
cur_scale(NsProcView *v)
{
    return v->scale > 0.0 ? v->scale : 1.0;
}

static void request_render(NsProcView *v);

static void
configure_adjustments(NsProcView *v)
{
    double s = cur_scale(v);
    double cw = viewport_w(v) / s;
    double ch = viewport_h(v) / s;
    gtk_adjustment_configure(v->hadj, v->scroll_x, 0,
                             v->page_w > 0 ? v->page_w : cw, 60, cw, cw);
    gtk_adjustment_configure(v->vadj, v->scroll_y, 0,
                             v->page_h > 0 ? v->page_h : ch, 60, ch, ch);
    v->scroll_x = (int)gtk_adjustment_get_value(v->hadj);
    v->scroll_y = (int)gtk_adjustment_get_value(v->vadj);
}

static void
on_adj_changed(GtkAdjustment *adj, gpointer data)
{
    (void)adj;
    NsProcView *v = data;
    if (v->closed)
        return;
    v->scroll_x = (int)gtk_adjustment_get_value(v->hadj);
    v->scroll_y = (int)gtk_adjustment_get_value(v->vadj);
    if (v->opened)
        request_render(v);
}

static void start_render(NsProcView *v);

static void
request_render(NsProcView *v)
{
    if (!v->opened)
        return;
    if (v->render_inflight) {
        v->render_pending = TRUE;
        return;
    }
    start_render(v);
}

static void
start_render(NsProcView *v)
{
    if (!v->opened)
        return;
    v->render_inflight = TRUE;
    Req *req = g_new0(Req, 1);
    req->type = REQ_RENDER;
    req->seq = ++v->render_seq;
    req->w = viewport_w(v);
    req->h = viewport_h(v);
    req->sx = v->scroll_x;
    req->sy = v->scroll_y;
    req->scale = cur_scale(v);
    push_req(v, req);
}

static gboolean
anim_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
    (void)widget;
    (void)clock;
    NsProcView *v = data;
    if (v->closed || !v->opened) {
        v->anim_tick_id = 0;
        return G_SOURCE_REMOVE;
    }
    request_render(v);
    return G_SOURCE_CONTINUE;
}

static void
arm_anim(NsProcView *v)
{
    if (v->anim_tick_id || v->closed || !v->area)
        return;
    v->anim_tick_id = gtk_widget_add_tick_callback(v->area, anim_tick, v, NULL);
}

static void
disarm_anim(NsProcView *v)
{
    if (v->anim_tick_id && v->area)
        gtk_widget_remove_tick_callback(v->area, v->anim_tick_id);
    v->anim_tick_id = 0;
}

static void start_link(NsProcView *v, int x, int y, LinkAct action);
static void show_context_menu(NsProcView *v, const char *href);
static void build_search_bar(NsProcView *v);
static void console_append(NsProcView *v, const char *text);
static void console_set_open(NsProcView *v, gboolean open);

static void
request_link(NsProcView *v, int x, int y, LinkAct action)
{
    if (!v->opened)
        return;
    if (v->link_inflight) {
        if (action != ACT_HOVER || v->link_pending_action == ACT_HOVER) {
            v->link_pending_x = x;
            v->link_pending_y = y;
            v->link_pending_action = action;
        }
        v->link_pending = TRUE;
        return;
    }
    start_link(v, x, y, action);
}

static void
start_link(NsProcView *v, int x, int y, LinkAct action)
{
    if (!v->opened)
        return;
    v->link_inflight = TRUE;
    Req *req = g_new0(Req, 1);
    req->type = REQ_LINK;
    req->seq = ++v->link_seq;
    req->x = x;
    req->y = y;
    req->action = action;
    push_req(v, req);
}

static void start_hover(NsProcView *v, int x, int y);

static void
request_hover(NsProcView *v, int x, int y)
{
    if (!v->opened)
        return;
    if (v->hover_inflight) {
        v->hover_pending_x = x;
        v->hover_pending_y = y;
        v->hover_pending = TRUE;
        return;
    }
    start_hover(v, x, y);
}

static void
start_hover(NsProcView *v, int x, int y)
{
    if (!v->opened)
        return;
    v->hover_inflight = TRUE;
    Req *req = g_new0(Req, 1);
    req->type = REQ_HOVER;
    req->seq = ++v->hover_seq;
    req->x = x;
    req->y = y;
    push_req(v, req);
}

static const char *
keyval_js_key(guint keyval, char *buf, size_t bufsz)
{
    gunichar uc = gdk_keyval_to_unicode(keyval);
    if (uc >= 0x20 && uc != 0x7f) {
        int len = g_unichar_to_utf8(uc, buf);
        if (len >= (int)bufsz) len = (int)bufsz - 1;
        buf[len] = '\0';
        return buf;
    }
    switch (keyval) {
    case GDK_KEY_Up:         return "ArrowUp";
    case GDK_KEY_Down:       return "ArrowDown";
    case GDK_KEY_Left:       return "ArrowLeft";
    case GDK_KEY_Right:      return "ArrowRight";
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:   return "Enter";
    case GDK_KEY_Escape:     return "Escape";
    case GDK_KEY_BackSpace:  return "Backspace";
    case GDK_KEY_Tab:
    case GDK_KEY_ISO_Left_Tab: return "Tab";
    case GDK_KEY_Delete:     return "Delete";
    case GDK_KEY_Insert:     return "Insert";
    case GDK_KEY_Home:       return "Home";
    case GDK_KEY_End:        return "End";
    case GDK_KEY_Page_Up:    return "PageUp";
    case GDK_KEY_Page_Down:  return "PageDown";
    case GDK_KEY_Shift_L:
    case GDK_KEY_Shift_R:    return "Shift";
    case GDK_KEY_Control_L:
    case GDK_KEY_Control_R:  return "Control";
    case GDK_KEY_Alt_L:
    case GDK_KEY_Alt_R:      return "Alt";
    default: { const char *n = gdk_keyval_name(keyval); return n ? n : ""; }
    }
}

static const char *
keyval_js_code(guint keyval, char *buf, size_t bufsz)
{
    if (keyval >= GDK_KEY_a && keyval <= GDK_KEY_z) {
        g_snprintf(buf, bufsz, "Key%c", 'A' + (int)(keyval - GDK_KEY_a));
        return buf;
    }
    if (keyval >= GDK_KEY_A && keyval <= GDK_KEY_Z) {
        g_snprintf(buf, bufsz, "Key%c", 'A' + (int)(keyval - GDK_KEY_A));
        return buf;
    }
    if (keyval >= GDK_KEY_0 && keyval <= GDK_KEY_9) {
        g_snprintf(buf, bufsz, "Digit%c", '0' + (int)(keyval - GDK_KEY_0));
        return buf;
    }
    switch (keyval) {
    case GDK_KEY_Up:         return "ArrowUp";
    case GDK_KEY_Down:       return "ArrowDown";
    case GDK_KEY_Left:       return "ArrowLeft";
    case GDK_KEY_Right:      return "ArrowRight";
    case GDK_KEY_Return:     return "Enter";
    case GDK_KEY_KP_Enter:   return "NumpadEnter";
    case GDK_KEY_Escape:     return "Escape";
    case GDK_KEY_BackSpace:  return "Backspace";
    case GDK_KEY_Tab:
    case GDK_KEY_ISO_Left_Tab: return "Tab";
    case GDK_KEY_Delete:     return "Delete";
    case GDK_KEY_Home:       return "Home";
    case GDK_KEY_End:        return "End";
    case GDK_KEY_Page_Up:    return "PageUp";
    case GDK_KEY_Page_Down:  return "PageDown";
    case GDK_KEY_space:      return "Space";
    default:                 return "";
    }
}

static int
keyval_js_keycode(guint keyval)
{
    if (keyval >= GDK_KEY_a && keyval <= GDK_KEY_z)
        return 65 + (int)(keyval - GDK_KEY_a);
    if (keyval >= GDK_KEY_A && keyval <= GDK_KEY_Z)
        return 65 + (int)(keyval - GDK_KEY_A);
    if (keyval >= GDK_KEY_0 && keyval <= GDK_KEY_9)
        return 48 + (int)(keyval - GDK_KEY_0);
    switch (keyval) {
    case GDK_KEY_BackSpace:  return 8;
    case GDK_KEY_Tab:        return 9;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:   return 13;
    case GDK_KEY_Escape:     return 27;
    case GDK_KEY_space:      return 32;
    case GDK_KEY_Page_Up:    return 33;
    case GDK_KEY_Page_Down:  return 34;
    case GDK_KEY_End:        return 35;
    case GDK_KEY_Home:       return 36;
    case GDK_KEY_Left:       return 37;
    case GDK_KEY_Up:         return 38;
    case GDK_KEY_Right:      return 39;
    case GDK_KEY_Down:       return 40;
    case GDK_KEY_Delete:     return 46;
    default:                 return 0;
    }
}

static void
start_key(NsProcView *v, int kind, guint keyval, GdkModifierType state)
{
    if (!v->opened)
        return;
    char keybuf[8] = {0}, codebuf[16] = {0};
    Req *req = g_new0(Req, 1);
    req->type = REQ_KEY;
    req->seq = kind == 0 ? ++v->key_seq : v->key_seq;
    req->kind = kind;
    req->keycode = keyval_js_keycode(keyval);
    req->mods = ((state & GDK_SHIFT_MASK)   ? 1 : 0) |
                ((state & GDK_CONTROL_MASK) ? 2 : 0) |
                ((state & GDK_ALT_MASK)     ? 4 : 0) |
                ((state & GDK_META_MASK)    ? 8 : 0);
    req->key = g_strdup(keyval_js_key(keyval, keybuf, sizeof keybuf));
    req->code = g_strdup(keyval_js_code(keyval, codebuf, sizeof codebuf));
    push_req(v, req);
}

static void
start_key_text(NsProcView *v, int kind, const char *text)
{
    if (!v->opened || !text || !*text)
        return;
    Req *req = g_new0(Req, 1);
    req->type = REQ_KEY;
    req->seq = ++v->key_seq;
    req->kind = kind;
    req->keycode = 0;
    req->mods = 0;
    req->key = g_strdup(text);
    req->code = g_strdup("");
    push_req(v, req);
}

static void
on_im_commit(GtkIMContext *im, const char *text, gpointer data)
{
    (void)im;
    NsProcView *v = data;
    if (!v || !v->opened || !text || !*text) return;
    start_key_text(v, 2, text);
}

static void
start_release(NsProcView *v)
{
    if (!v->opened)
        return;
    Req *req = g_new0(Req, 1);
    req->type = REQ_RELEASE;
    push_req(v, req);
}

static void
start_select(NsProcView *v, int kind, int x, int y)
{
    if (!v->opened)
        return;
    Req *req = g_new0(Req, 1);
    req->type = REQ_SELECT;
    req->seq = ++v->select_seq;
    req->kind = kind;
    req->x = x;
    req->y = y;
    push_req(v, req);
}

static void
start_click(NsProcView *v, int x, int y, int mods)
{
    if (!v->opened)
        return;
    Req *req = g_new0(Req, 1);
    req->type = REQ_CLICK;
    req->seq = ++v->click_seq;
    req->x = x;
    req->y = y;
    req->mods = mods;
    push_req(v, req);
}

static void
start_viewport(NsProcView *v, int width, int height)
{
    if (!v->opened)
        return;
    Req *req = g_new0(Req, 1);
    req->type = REQ_VIEWPORT;
    req->seq = ++v->viewport_seq;
    req->vw = width;
    req->vh = height;
    push_req(v, req);
}

static gboolean
maybe_update_viewport(NsProcView *v)
{
    if (!v->opened)
        return FALSE;
    int w = viewport_w(v);
    int h = viewport_h(v);
    if (w <= 1 || h <= 1)
        return FALSE;
    if (abs(w - v->last_vp_w) < 16 && abs(h - v->last_vp_h) < 16)
        return FALSE;
    v->last_vp_w = w;
    v->last_vp_h = h;
    start_viewport(v, w, h);
    return TRUE;
}

static void
push_history(NsProcView *v, const char *url)
{
    if (!url || !*url)
        return;
    if (v->hist_index >= 0 &&
        g_strcmp0(g_ptr_array_index(v->history, v->hist_index), url) == 0)
        return;
    while ((int)v->history->len > v->hist_index + 1)
        g_ptr_array_remove_index(v->history, v->history->len - 1);
    g_ptr_array_add(v->history, g_strdup(url));
    v->hist_index = (int)v->history->len - 1;
    post_emit(v, NS_PROC_EVT_HISTORY, NULL);
}

static void
do_load(NsProcView *v, const char *url, gboolean record)
{
    if (!url || !*url)
        return;
    v->pending_record = record;
    int seq = ++v->load_seq;
    ++v->render_seq;
    ++v->link_seq;
    ++v->click_seq;
    ++v->viewport_seq;
    ++v->key_seq;
    ++v->select_seq;
    ++v->hover_seq;
    v->render_pending = FALSE;
    v->render_inflight = FALSE;
    v->link_inflight = FALSE;
    v->link_pending = FALSE;
    v->link_pending_action = ACT_HOVER;
    v->hover_inflight = FALSE;
    v->hover_pending = FALSE;
    v->has_selection = FALSE;
    ++v->find_seq;
    if (v->search_revealer)
        gtk_revealer_set_reveal_child(GTK_REVEALER(v->search_revealer), FALSE);
    if (v->search_label)
        gtk_label_set_text(GTK_LABEL(v->search_label), "");
    v->opened = FALSE;
    disarm_anim(v);
    if (v->frame) {
        cairo_surface_destroy(v->frame);
        v->frame = NULL;
    }
    gtk_widget_queue_draw(v->area);
    if (!v->loading) {
        v->loading = TRUE;
        post_emit(v, NS_PROC_EVT_LOADING, "1");
    }
    v->busy_cursor = TRUE;
    if (v->area)
        gtk_widget_set_cursor_from_name(v->area, "wait");
    post_emit(v, NS_PROC_EVT_STATUS, ns_i18n("Loading…"));

    int vw = gtk_widget_get_width(v->area);
    int vh = gtk_widget_get_height(v->area);
    if (vw <= 1 || vh <= 1) {
        g_free(v->deferred_url);
        v->deferred_url = g_strdup(url);
        v->deferred_record = record;
        return;
    }
    v->last_vp_w = vw;
    v->last_vp_h = vh;

    Req *req = g_new0(Req, 1);
    req->type = REQ_LOAD;
    req->seq = seq;
    req->url = g_strdup(url);
    req->vw = vw;
    req->vh = vh;
    push_req(v, req);
}

void
ns_proc_view_load(NsProcView *v, const char *url)
{
    v->render_restarts = 0;
    do_load(v, url, TRUE);
}

gboolean ns_proc_view_can_back(NsProcView *v) { return v->hist_index > 0; }

gboolean
ns_proc_view_can_forward(NsProcView *v)
{
    return v->hist_index >= 0 && v->hist_index < (int)v->history->len - 1;
}

void
ns_proc_view_back(NsProcView *v)
{
    if (!ns_proc_view_can_back(v))
        return;
    v->hist_index--;
    v->render_restarts = 0;
    post_emit(v, NS_PROC_EVT_HISTORY, NULL);
    do_load(v, g_ptr_array_index(v->history, v->hist_index), FALSE);
}

void
ns_proc_view_forward(NsProcView *v)
{
    if (!ns_proc_view_can_forward(v))
        return;
    v->hist_index++;
    v->render_restarts = 0;
    post_emit(v, NS_PROC_EVT_HISTORY, NULL);
    do_load(v, g_ptr_array_index(v->history, v->hist_index), FALSE);
}

void
ns_proc_view_reload(NsProcView *v)
{
    v->render_restarts = 0;
    if (v->hist_index >= 0 && v->hist_index < (int)v->history->len)
        do_load(v, g_ptr_array_index(v->history, v->hist_index), FALSE);
    else if (v->current_url)
        do_load(v, v->current_url, FALSE);
}

void
ns_proc_view_toggle_console(NsProcView *v)
{
    if (v->opened)
        console_set_open(v, !v->console_open);
}

const char *ns_proc_view_url(NsProcView *v) { return v->current_url; }
const char *ns_proc_view_title(NsProcView *v) { return v->current_title; }
gboolean ns_proc_view_is_loading(NsProcView *v) { return v->loading; }

int
ns_proc_view_renderer_pid(NsProcView *v)
{
    if (!v) return -1;
    g_mutex_lock(&v->proc_lock);
    int pid = v->proc ? ns_rproc_http_pid(v->proc) : -1;
    g_mutex_unlock(&v->proc_lock);
    return pid;
}

void
ns_proc_view_end_task(NsProcView *v)
{
    if (!v) return;
    g_mutex_lock(&v->proc_lock);
    if (v->proc) {
        ns_rproc_http_interrupt(v->proc);
        ns_rproc_http_terminate(v->proc);
    }
    g_mutex_unlock(&v->proc_lock);
}
double ns_proc_view_zoom(NsProcView *v) { return cur_scale(v); }

void ns_proc_view_focus(NsProcView *v)
{
    if (v->area)
        gtk_widget_grab_focus(v->area);
}

static void
set_zoom(NsProcView *v, double scale)
{
    int permille = (int)(scale * 1000.0 + 0.5);
    if (permille < NS_PV_ZOOM_MIN_PERMILLE)
        permille = NS_PV_ZOOM_MIN_PERMILLE;
    if (permille > NS_PV_ZOOM_MAX_PERMILLE)
        permille = NS_PV_ZOOM_MAX_PERMILLE;
    double clamped = permille / 1000.0;
    if (clamped == cur_scale(v))
        return;
    v->scale = clamped;
    char status[32];
    g_snprintf(status, sizeof status, "%s %d%%", ns_i18n("Zoom"),
               permille / 10);
    post_emit(v, NS_PROC_EVT_STATUS, status);
    if (v->opened) {
        configure_adjustments(v);
        request_render(v);
    }
}

void ns_proc_view_zoom_in(NsProcView *v)  { set_zoom(v, cur_scale(v) * NS_PROC_ZOOM_STEP); }
void ns_proc_view_zoom_out(NsProcView *v) { set_zoom(v, cur_scale(v) / NS_PROC_ZOOM_STEP); }
void ns_proc_view_zoom_reset(NsProcView *v) { set_zoom(v, 1.0); }

typedef struct {
    NsProcView *v;
    char       *origin;
} PvWebglPrompt;

static void
pv_webgl_prompt_free(PvWebglPrompt *p)
{
    pv_unref(p->v);
    g_free(p->origin);
    g_free(p);
}

static void
pv_webgl_resolve(NsProcView *v, const char *origin, gboolean allow)
{
    Req *req = g_new0(Req, 1);
    req->type = REQ_WEBGL;
    req->url = g_strdup(origin);
    req->mods = allow ? 1 : 0;
    push_req(v, req);
    if (allow && v->current_url && !v->closed)
        do_load(v, v->current_url, FALSE);
}

static GtkWindow *
pv_window(NsProcView *v)
{
    GtkWidget *root = v->area ? gtk_widget_get_ancestor(v->area, GTK_TYPE_WINDOW)
                              : NULL;
    return GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL;
}

static void
pv_webgl_confirm_done(GObject *src, GAsyncResult *res, gpointer ud)
{
    PvWebglPrompt *p = ud;
    GError *err = NULL;
    int idx = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(src), res, &err);
    if (err) { idx = 0; g_error_free(err); }
    if (!p->v->closed)
        pv_webgl_resolve(p->v, p->origin, idx == 1);
    pv_webgl_prompt_free(p);
}

static void
pv_webgl_first_done(GObject *src, GAsyncResult *res, gpointer ud)
{
    PvWebglPrompt *p = ud;
    GError *err = NULL;
    int idx = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(src), res, &err);
    if (err) { idx = 0; g_error_free(err); }
    if (idx != 1 || p->v->closed) {
        if (!p->v->closed)
            pv_webgl_resolve(p->v, p->origin, FALSE);
        pv_webgl_prompt_free(p);
        return;
    }
    char *detail = g_strdup_printf(
        "Give %s near-direct access to your GPU driver? This stays enabled "
        "for this origin for the rest of the session.", p->origin);
    const char *buttons[] = { ns_i18n("Cancel"), ns_i18n("Enable WebGL"),
                              NULL };
    GtkAlertDialog *dlg = gtk_alert_dialog_new("%s", ns_i18n("Are you sure?"));
    gtk_alert_dialog_set_detail(dlg, detail);
    gtk_alert_dialog_set_buttons(dlg, buttons);
    gtk_alert_dialog_set_cancel_button(dlg, 0);
    gtk_alert_dialog_set_modal(dlg, TRUE);
    gtk_alert_dialog_choose(dlg, pv_window(p->v), NULL,
                            pv_webgl_confirm_done, p);
    g_object_unref(dlg);
    g_free(detail);
}

static void
pv_webgl_prompt(NsProcView *v, const char *origin)
{
    PvWebglPrompt *p = g_new0(PvWebglPrompt, 1);
    p->v = pv_ref(v);
    p->origin = g_strdup(origin);
    char *primary = g_strdup_printf("Enable WebGL for %s?", origin);
    char *detail = g_strdup_printf(
        "This page wants to use WebGL (hardware-accelerated 3D graphics) on "
        "%s.\n\nWebGL hands the page near-direct access to your GPU driver — "
        "only allow it on sites you trust. Allowing keeps WebGL enabled for "
        "this site for the rest of the session and reloads the page.", origin);
    const char *buttons[] = { ns_i18n("Block"),
                              ns_i18n("Allow and trust this site"), NULL };
    GtkAlertDialog *dlg = gtk_alert_dialog_new("%s", primary);
    gtk_alert_dialog_set_detail(dlg, detail);
    gtk_alert_dialog_set_buttons(dlg, buttons);
    gtk_alert_dialog_set_cancel_button(dlg, 0);
    gtk_alert_dialog_set_default_button(dlg, 0);
    gtk_alert_dialog_set_modal(dlg, TRUE);
    gtk_alert_dialog_choose(dlg, pv_window(v), NULL, pv_webgl_first_done, p);
    g_object_unref(dlg);
    g_free(primary);
    g_free(detail);
}

static gboolean
on_result(gpointer data)
{
    Res *res = data;
    NsProcView *v = res->view;

    if (v->closed)
        goto done;

    if (res->type == RES_PAGE) {
        if (res->seq != v->load_seq)
            goto done;
        if (!res->ok) {
            post_emit(v, NS_PROC_EVT_STATUS, ns_i18n("Failed to load page"));
            finish_loading(v);
            clear_busy_cursor(v);
            goto done;
        }
        if (res->nav && *res->nav) {
            if (v->js_redirects < NS_PROC_MAX_JS_REDIRECTS) {
                v->js_redirects++;
                do_load(v, res->nav, v->pending_record);
                goto done;
            }
            post_emit(v, NS_PROC_EVT_STATUS,
                      ns_i18n("Stopped after too many redirects"));
        }
        v->js_redirects = 0;
        g_free(v->current_url);
        v->current_url = g_strdup(res->url);
        g_free(v->current_title);
        v->current_title = g_strdup(res->title);
        v->page_w = res->pw;
        v->page_h = res->ph;
        v->scroll_x = 0;
        v->scroll_y = 0;
        v->opened = TRUE;
        configure_adjustments(v);
        if (v->pending_record)
            push_history(v, v->current_url);
        post_emit(v, NS_PROC_EVT_URL, v->current_url);
        post_emit(v, NS_PROC_EVT_TITLE, v->current_title);
        post_emit(v, NS_PROC_EVT_STATUS, ns_i18n("Done"));
        finish_loading(v);
        request_render(v);
    } else if (res->type == RES_FRAME) {
        gboolean current = res->seq == v->render_seq;
        if (current && res->ok) {
            if (res->animating)
                arm_anim(v);
            else
                disarm_anim(v);
        }
        if (current && res->ok && res->surface) {
            if (v->frame)
                cairo_surface_destroy(v->frame);
            v->frame = res->surface;
            res->surface = NULL;
            v->render_restarts = 0;
            gtk_widget_queue_draw(v->area);
            clear_busy_cursor(v);
        } else if (current && res->ok && res->frame_unchanged) {
            v->render_restarts = 0;
        }
        if (current && res->ok && res->nav && *res->nav &&
            v->js_redirects < NS_PROC_MAX_JS_REDIRECTS) {
            v->js_redirects++;
            do_load(v, res->nav, FALSE);
        }
        if (res->ok && res->webgl && *res->webgl)
            pv_webgl_prompt(v, res->webgl);
        v->render_inflight = FALSE;
        if (v->render_pending) {
            v->render_pending = FALSE;
            start_render(v);
        } else if (current && !res->ok && v->current_url) {
            if (v->render_restarts < NS_PROC_MAX_RESTARTS) {
                v->render_restarts++;
                post_emit(v, NS_PROC_EVT_STATUS, ns_i18n("Renderer restarted"));
                do_load(v, v->current_url, FALSE);
            } else {
                post_emit(v, NS_PROC_EVT_STATUS,
                          ns_i18n("This tab's renderer keeps failing — "
                                  "reload to retry"));
                finish_loading(v);
                clear_busy_cursor(v);
            }
        }
    } else if (res->type == RES_VIEWPORT) {
        if (res->seq != v->viewport_seq)
            goto done;
        if (res->ok) {
            v->page_w = res->pw;
            v->page_h = res->ph;
            configure_adjustments(v);
            request_render(v);
        }
    } else if (res->type == RES_SELECT) {
        if (res->seq == v->select_seq)
            request_render(v);
    } else if (res->type == RES_COPY) {
        if (res->href && *res->href && v->area) {
            gdk_clipboard_set_text(gtk_widget_get_clipboard(v->area),
                                   res->href);
            post_emit(v, NS_PROC_EVT_STATUS, ns_i18n("Copied selection"));
        }
    } else if (res->type == RES_KEY) {
        if (res->kind != 0)
            goto done;
        if (res->seq != v->key_seq)
            goto done;
        if (res->href && *res->href) {
            post_emit(v, NS_PROC_EVT_STATUS, res->href);
            ns_proc_view_load(v, res->href);
        } else {
            request_render(v);
        }
    } else if (res->type == RES_CLICK) {
        if (res->seq != v->click_seq)
            goto done;
        if (res->href && *res->href) {
            post_emit(v, NS_PROC_EVT_STATUS, res->href);
            ns_proc_view_load(v, res->href);
        } else if (res->media_url && *res->media_url) {
            char *app = NULL, *app_url = NULL;
            ns_media_status st = ns_media_try_launch(res->media_url,
                                                     res->media_stream != 0,
                                                     &app, &app_url);
            const char *kind = res->media_is_video ? "video" : "audio";
            if (st == NS_MEDIA_LAUNCHED) {
                char *msg = g_strdup_printf("Opening %s in external player…",
                                            kind);
                post_emit(v, NS_PROC_EVT_STATUS, msg);
                g_free(msg);
            } else if (st == NS_MEDIA_NEED_YTDLP) {
                post_emit(v, NS_PROC_EVT_STATUS,
                          ns_i18n("Install yt-dlp to play this stream "
                                  "externally"));
            } else if (st == NS_MEDIA_NO_PLAYER) {
                post_emit(v, NS_PROC_EVT_STATUS,
                          ns_i18n("No external media player found "
                                  "(install mpv or vlc)"));
            } else {
                post_emit(v, NS_PROC_EVT_STATUS,
                          ns_i18n("Cannot play this media externally"));
            }
            g_free(app);
            g_free(app_url);
        } else {
            request_render(v);
        }
    } else if (res->type == RES_LINK) {
        if (res->seq != v->link_seq)
            goto done;
        v->link_inflight = FALSE;
        gboolean navigated = FALSE;
        GtkWidget *area = v->area;
        if (res->action == ACT_CONTEXT) {
            show_context_menu(v, res->href);
            if (v->link_pending) {
                v->link_pending = FALSE;
                LinkAct a = v->link_pending_action;
                v->link_pending_action = ACT_HOVER;
                start_link(v, v->link_pending_x, v->link_pending_y, a);
            }
            goto done;
        }
        if (res->href && *res->href) {
            post_emit(v, NS_PROC_EVT_STATUS, res->href);
            if (!v->busy_cursor)
                gtk_widget_set_cursor_from_name(
                    area, res->cursor ? res->cursor : "pointer");
            if (res->action == ACT_NAVIGATE) {
                navigated = TRUE;
                ns_proc_view_load(v, res->href);
            } else if (res->action == ACT_NEWTAB) {
                post_emit(v, NS_PROC_EVT_NEWTAB, res->href);
            }
        } else if (!v->busy_cursor) {
            gtk_widget_set_cursor_from_name(area, res->cursor);
        }
        if (!navigated && v->link_pending) {
            v->link_pending = FALSE;
            LinkAct a = v->link_pending_action;
            v->link_pending_action = ACT_HOVER;
            start_link(v, v->link_pending_x, v->link_pending_y, a);
        }
    } else if (res->type == RES_HOVER) {
        if (res->seq != v->hover_seq)
            goto done;
        v->hover_inflight = FALSE;
        if (res->ok)
            request_render(v);
        if (v->hover_pending) {
            v->hover_pending = FALSE;
            start_hover(v, v->hover_pending_x, v->hover_pending_y);
        }
    } else if (res->type == RES_RELEASE) {
        if (res->ok)
            request_render(v);
    } else if (res->type == RES_FIND) {
        if (res->seq != v->find_seq)
            goto done;
        if (v->search_label) {
            const char *q = v->search_entry
                ? gtk_editable_get_text(GTK_EDITABLE(v->search_entry)) : NULL;
            if (res->find_total > 0) {
                char buf[64];
                g_snprintf(buf, sizeof buf, "%d/%d", res->find_current,
                           res->find_total);
                gtk_label_set_text(GTK_LABEL(v->search_label), buf);
            } else {
                gtk_label_set_text(GTK_LABEL(v->search_label),
                                   (q && *q) ? ns_i18n("No results") : "");
            }
        }
        if (res->find_total > 0) {
            double target = res->find_scroll_y > 40 ? res->find_scroll_y - 40
                                                    : 0;
            gtk_adjustment_set_value(v->vadj, target);
        }
        request_render(v);
    } else if (res->type == RES_EXPORT) {
        if (res->ok && res->url)
            post_emit(v, NS_PROC_EVT_STATUS, res->url);
        else
            post_emit(v, NS_PROC_EVT_STATUS, ns_i18n("Could not save page"));
    } else if (res->type == RES_CONSOLE) {
        if (res->href && *res->href)
            console_append(v, res->href);
    } else if (res->type == RES_EVAL) {
        if (res->href && *res->href) {
            console_append(v, res->href);
            console_append(v, "\n");
        } else {
            console_append(v, "undefined\n");
        }
        request_render(v);
    }

done:
    if (res->surface)
        cairo_surface_destroy(res->surface);
    g_free(res->title);
    g_free(res->url);
    g_free(res->nav);
    g_free(res->webgl);
    free(res->href);
    free(res->cursor);
    free(res->media_url);
    pv_unref(res->view);
    g_free(res);
    return G_SOURCE_REMOVE;
}

static void
on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
        gpointer data)
{
    (void)area;
    NsProcView *v = data;
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);
    if (v->frame) {
        cairo_set_source_surface(cr, v->frame, 0, 0);
        cairo_paint(cr);
    }
}

static void
on_resize(GtkDrawingArea *area, int width, int height, gpointer data)
{
    (void)area;
    NsProcView *v = data;
    if (v->deferred_url && width > 1 && height > 1) {
        char *u = v->deferred_url;
        gboolean rec = v->deferred_record;
        v->deferred_url = NULL;
        do_load(v, u, rec);
        g_free(u);
        return;
    }
    if (v->opened) {
        if (maybe_update_viewport(v))
            return;
        configure_adjustments(v);
        request_render(v);
    }
}

static gboolean
on_scroll(GtkEventControllerScroll *ctrl, double dx, double dy, gpointer data)
{
    NsProcView *v = data;
    if (!v->opened)
        return FALSE;
    GdkModifierType mods =
        gtk_event_controller_get_current_event_state(
            GTK_EVENT_CONTROLLER(ctrl));
    if (mods & GDK_CONTROL_MASK) {
        if (dy < 0)
            ns_proc_view_zoom_in(v);
        else if (dy > 0)
            ns_proc_view_zoom_out(v);
        return TRUE;
    }
    gtk_adjustment_set_value(v->hadj,
                             gtk_adjustment_get_value(v->hadj) + dx * 60.0);
    gtk_adjustment_set_value(v->vadj,
                             gtk_adjustment_get_value(v->vadj) + dy * 60.0);
    return TRUE;
}

typedef struct { NsProcView *view; gboolean pdf; } ExportCtx;

static void
on_save_dialog_done(GObject *src, GAsyncResult *res, gpointer ud)
{
    ExportCtx *c = ud;
    NsProcView *v = c->view;
    GError *err = NULL;
    GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, &err);
    if (file) {
        char *dest = g_file_get_path(file);
        if (dest && v->opened) {
            static int export_counter = 0;
            char *base = g_strdup_printf(
                "nordstjernen-export-%" G_GINT64_FORMAT "-%d.%s",
                g_get_monotonic_time(), ++export_counter,
                c->pdf ? "pdf" : "png");
            Req *req = g_new0(Req, 1);
            req->type = REQ_EXPORT;
            req->url = g_build_filename(g_get_user_runtime_dir(), base, NULL);
            req->export_dest = g_strdup(dest);
            push_req(v, req);
            g_free(base);
        }
        g_free(dest);
        g_object_unref(file);
    }
    g_clear_error(&err);
    pv_unref(v);
    g_free(c);
}

static void
view_save(NsProcView *v, gboolean pdf)
{
    if (!v->opened || !v->area)
        return;
    GtkRoot *root = gtk_widget_get_root(v->area);
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog,
        pdf ? ns_i18n("Save page as PDF")
                                      : ns_i18n("Save page as PNG"));
    const char *t = (v->current_title && *v->current_title)
        ? v->current_title : "page";
    char *name = g_strdup_printf("%s.%s", t, pdf ? "pdf" : "png");
    g_strdelimit(name, "/", '_');
    gtk_file_dialog_set_initial_name(dialog, name);
    ExportCtx *c = g_new0(ExportCtx, 1);
    c->view = pv_ref(v);
    c->pdf = pdf;
    gtk_file_dialog_save(dialog, GTK_WINDOW(root), NULL,
                         on_save_dialog_done, c);
    g_object_unref(dialog);
    g_free(name);
}

static void
ctx_set_clipboard(NsProcView *v, const char *text, const char *status)
{
    if (!text || !*text || !v->area)
        return;
    gdk_clipboard_set_text(gtk_widget_get_clipboard(v->area), text);
    post_emit(v, NS_PROC_EVT_STATUS, status);
}

static void
on_ctx_back(GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a; (void)p; ns_proc_view_back(ud); }

static void
on_ctx_forward(GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a; (void)p; ns_proc_view_forward(ud); }

static void
on_ctx_reload(GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a; (void)p; ns_proc_view_reload(ud); }

static void
on_ctx_copy_url(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    NsProcView *v = ud;
    ctx_set_clipboard(v, v->current_url, ns_i18n("Copied page address"));
}

static void
on_ctx_open_newtab(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    NsProcView *v = ud;
    if (v->ctx_link && *v->ctx_link)
        post_emit(v, NS_PROC_EVT_NEWTAB, v->ctx_link);
}

static void
on_ctx_open_link(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    NsProcView *v = ud;
    if (v->ctx_link && *v->ctx_link)
        ns_proc_view_load(v, v->ctx_link);
}

static void
on_ctx_copy_link(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    NsProcView *v = ud;
    ctx_set_clipboard(v, v->ctx_link, ns_i18n("Copied link address"));
}

static void
on_ctx_copy_sel(GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a; (void)p; start_select(ud, 4, 0, 0); }

static void
on_ctx_select_all(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    NsProcView *v = ud;
    v->has_selection = TRUE;
    start_select(v, 3, 0, 0);
}

static void
on_ctx_save_pdf(GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a; (void)p; view_save(ud, TRUE); }

static void
on_ctx_save_png(GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a; (void)p; view_save(ud, FALSE); }

static void
ctx_action_enable(NsProcView *v, const char *name, gboolean on)
{
    GAction *act = g_action_map_lookup_action(G_ACTION_MAP(v->ctx_actions),
                                              name);
    if (act)
        g_simple_action_set_enabled(G_SIMPLE_ACTION(act), on);
}

static void
ctx_install_actions(NsProcView *v)
{
    static const GActionEntry entries[] = {
        { "back",        on_ctx_back,        NULL, NULL, NULL, {0} },
        { "forward",     on_ctx_forward,     NULL, NULL, NULL, {0} },
        { "reload",      on_ctx_reload,      NULL, NULL, NULL, {0} },
        { "copy-url",    on_ctx_copy_url,    NULL, NULL, NULL, {0} },
        { "open-link",   on_ctx_open_link,   NULL, NULL, NULL, {0} },
        { "open-newtab", on_ctx_open_newtab, NULL, NULL, NULL, {0} },
        { "copy-link",   on_ctx_copy_link,   NULL, NULL, NULL, {0} },
        { "copy-sel",    on_ctx_copy_sel,    NULL, NULL, NULL, {0} },
        { "select-all",  on_ctx_select_all,  NULL, NULL, NULL, {0} },
        { "save-pdf",    on_ctx_save_pdf,    NULL, NULL, NULL, {0} },
        { "save-png",    on_ctx_save_png,    NULL, NULL, NULL, {0} },
    };
    v->ctx_actions = g_simple_action_group_new();
    g_action_map_add_action_entries(G_ACTION_MAP(v->ctx_actions), entries,
                                    G_N_ELEMENTS(entries), v);
    gtk_widget_insert_action_group(v->area, "ctx",
                                   G_ACTION_GROUP(v->ctx_actions));
}

static void
show_context_menu(NsProcView *v, const char *href)
{
    g_free(v->ctx_link);
    v->ctx_link = (href && *href) ? g_strdup(href) : NULL;

    ctx_action_enable(v, "back", ns_proc_view_can_back(v));
    ctx_action_enable(v, "forward", ns_proc_view_can_forward(v));
    ctx_action_enable(v, "open-link", v->ctx_link != NULL);
    ctx_action_enable(v, "open-newtab", v->ctx_link != NULL);
    ctx_action_enable(v, "copy-link", v->ctx_link != NULL);
    ctx_action_enable(v, "copy-sel", v->has_selection);

    GMenu *menu = g_menu_new();
    if (v->ctx_link) {
        GMenu *s = g_menu_new();
        g_menu_append(s, ns_i18n("Open Link"), "ctx.open-link");
        g_menu_append(s, ns_i18n("Open Link in New Tab"), "ctx.open-newtab");
        g_menu_append(s, ns_i18n("Copy Link Address"), "ctx.copy-link");
        g_menu_append_section(menu, NULL, G_MENU_MODEL(s));
        g_object_unref(s);
    }
    if (v->has_selection) {
        GMenu *s = g_menu_new();
        g_menu_append(s, ns_i18n("Copy"), "ctx.copy-sel");
        g_menu_append_section(menu, NULL, G_MENU_MODEL(s));
        g_object_unref(s);
    }
    GMenu *nav = g_menu_new();
    g_menu_append(nav, ns_i18n("Back"), "ctx.back");
    g_menu_append(nav, ns_i18n("Forward"), "ctx.forward");
    g_menu_append(nav, ns_i18n("Reload"), "ctx.reload");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(nav));
    g_object_unref(nav);
    GMenu *page = g_menu_new();
    g_menu_append(page, ns_i18n("Select All"), "ctx.select-all");
    g_menu_append(page, ns_i18n("Copy Page Address"), "ctx.copy-url");
    g_menu_append(page, ns_i18n("Save Page as PDF…"), "ctx.save-pdf");
    g_menu_append(page, ns_i18n("Save Page as Image…"), "ctx.save-png");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(page));
    g_object_unref(page);

    if (v->ctx_popover)
        gtk_widget_unparent(v->ctx_popover);
    v->ctx_popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
    g_object_unref(menu);
    gtk_widget_set_parent(v->ctx_popover, v->area);
    gtk_popover_set_has_arrow(GTK_POPOVER(v->ctx_popover), FALSE);
    gtk_popover_set_pointing_to(GTK_POPOVER(v->ctx_popover),
        &(GdkRectangle){ (int)v->ctx_x, (int)v->ctx_y, 1, 1 });
    gtk_popover_popup(GTK_POPOVER(v->ctx_popover));
}

static void
on_secondary_pressed(GtkGestureClick *gesture, int n_press, double x, double y,
                     gpointer data)
{
    (void)n_press;
    NsProcView *v = data;
    if (!v->opened)
        return;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    v->ctx_x = x;
    v->ctx_y = y;
    double s = cur_scale(v);
    int px = v->scroll_x + (int)(x / s);
    int py = v->scroll_y + (int)(y / s);
    request_link(v, px, py, ACT_CONTEXT);
}

static void
request_find(NsProcView *v, int direction)
{
    if (!v->opened || !v->search_entry)
        return;
    const char *q = gtk_editable_get_text(GTK_EDITABLE(v->search_entry));
    Req *req = g_new0(Req, 1);
    req->type = REQ_FIND;
    req->seq = ++v->find_seq;
    req->query = g_strdup(q ? q : "");
    req->find_dir = direction;
    req->find_from_y = v->scroll_y;
    req->find_case = v->find_case;
    push_req(v, req);
}

static void
search_open(NsProcView *v)
{
    if (!v->search_revealer)
        return;
    gtk_revealer_set_reveal_child(GTK_REVEALER(v->search_revealer), TRUE);
    gtk_widget_grab_focus(v->search_entry);
    gtk_editable_select_region(GTK_EDITABLE(v->search_entry), 0, -1);
    const char *q = gtk_editable_get_text(GTK_EDITABLE(v->search_entry));
    if (q && *q)
        request_find(v, 0);
}

static void
search_close(NsProcView *v)
{
    if (!v->search_revealer)
        return;
    gtk_revealer_set_reveal_child(GTK_REVEALER(v->search_revealer), FALSE);
    gtk_label_set_text(GTK_LABEL(v->search_label), "");
    Req *req = g_new0(Req, 1);
    req->type = REQ_FIND;
    req->seq = ++v->find_seq;
    req->query = g_strdup("");
    req->find_from_y = v->scroll_y;
    push_req(v, req);
    gtk_widget_grab_focus(v->area);
}

static void
on_search_changed(GtkSearchEntry *e, gpointer data)
{ (void)e; request_find(data, 0); }

static void
on_search_next(GtkWidget *w, gpointer data)
{ (void)w; request_find(data, 1); }

static void
on_search_prev(GtkWidget *w, gpointer data)
{ (void)w; request_find(data, 2); }

static void
on_search_stop(GtkSearchEntry *e, gpointer data)
{ (void)e; search_close(data); }

static void
on_search_close_clicked(GtkButton *b, gpointer data)
{ (void)b; search_close(data); }

static void
on_pressed(GtkGestureClick *gesture, int n_press, double x, double y,
           gpointer data)
{
    (void)n_press;
    NsProcView *v = data;
    if (!v->opened)
        return;
    GdkModifierType mods =
        gtk_event_controller_get_current_event_state(
            GTK_EVENT_CONTROLLER(gesture));
    guint button = gtk_gesture_single_get_current_button(
        GTK_GESTURE_SINGLE(gesture));
    gtk_widget_grab_focus(v->area);
    double s = cur_scale(v);
    int px = v->scroll_x + (int)(x / s);
    int py = v->scroll_y + (int)(y / s);
    if (button == GDK_BUTTON_MIDDLE || (mods & GDK_CONTROL_MASK)) {
        request_link(v, px, py, ACT_NEWTAB);
        return;
    }
    int kmods = ((mods & GDK_SHIFT_MASK)   ? 1 : 0) |
                ((mods & GDK_CONTROL_MASK) ? 2 : 0) |
                ((mods & GDK_ALT_MASK)     ? 4 : 0) |
                ((mods & GDK_META_MASK)    ? 8 : 0);
    v->has_selection = FALSE;
    start_click(v, px, py, kmods);
}

static void
on_released(GtkGestureClick *gesture, int n_press, double x, double y,
            gpointer data)
{
    (void)gesture; (void)n_press; (void)x; (void)y;
    NsProcView *v = data;
    if (v->opened)
        start_release(v);
}

static void
on_motion(GtkEventControllerMotion *ctrl, double x, double y, gpointer data)
{
    (void)ctrl;
    NsProcView *v = data;
    if (v->opened) {
        double s = cur_scale(v);
        int px = v->scroll_x + (int)(x / s);
        int py = v->scroll_y + (int)(y / s);
        request_link(v, px, py, ACT_HOVER);
        request_hover(v, px, py);
    }
}

static void
on_drag_begin(GtkGestureDrag *g, double sx, double sy, gpointer data)
{
    (void)g;
    NsProcView *v = data;
    v->drag_start_x = sx;
    v->drag_start_y = sy;
    v->drag_anchored = FALSE;
}

static void
on_drag_update(GtkGestureDrag *g, double ox, double oy, gpointer data)
{
    (void)g;
    NsProcView *v = data;
    if (!v->opened)
        return;
    double s = cur_scale(v);
    if (!v->drag_anchored) {
        start_select(v, 0, v->scroll_x + (int)(v->drag_start_x / s),
                     v->scroll_y + (int)(v->drag_start_y / s));
        v->drag_anchored = TRUE;
    }
    start_select(v, 1, v->scroll_x + (int)((v->drag_start_x + ox) / s),
                 v->scroll_y + (int)((v->drag_start_y + oy) / s));
    v->has_selection = TRUE;
}

static gboolean
on_key(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
       GdkModifierType state, gpointer data)
{
    (void)ctrl;
    (void)keycode;
    NsProcView *v = data;
    if (!v->opened)
        return FALSE;
    if (keyval == GDK_KEY_F12) {
        console_set_open(v, !v->console_open);
        return TRUE;
    }
    if ((state & GDK_CONTROL_MASK) && (state & GDK_SHIFT_MASK) &&
        (keyval == GDK_KEY_j || keyval == GDK_KEY_J)) {
        console_set_open(v, !v->console_open);
        return TRUE;
    }
    gunichar uc = gdk_keyval_to_unicode(keyval);
    if (uc && uc != ' ' && !g_unichar_iscntrl(uc) &&
        !(state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_META_MASK))) {
        start_key(v, 3, keyval, state);
        return FALSE;
    }
    start_key(v, 0, keyval, state);
    if (state & GDK_CONTROL_MASK) {
        switch (keyval) {
        case GDK_KEY_c:
        case GDK_KEY_C:          start_select(v, 4, 0, 0); return TRUE;
        case GDK_KEY_a:
        case GDK_KEY_A:          start_select(v, 3, 0, 0); return TRUE;
        case GDK_KEY_plus:
        case GDK_KEY_equal:
        case GDK_KEY_KP_Add:      ns_proc_view_zoom_in(v); return TRUE;
        case GDK_KEY_minus:
        case GDK_KEY_KP_Subtract: ns_proc_view_zoom_out(v); return TRUE;
        case GDK_KEY_0:
        case GDK_KEY_KP_0:        ns_proc_view_zoom_reset(v); return TRUE;
        case GDK_KEY_f:
        case GDK_KEY_F:           search_open(v); return TRUE;
        case GDK_KEY_g:
        case GDK_KEY_G:
            request_find(v, (state & GDK_SHIFT_MASK) ? 2 : 1);
            return TRUE;
        case GDK_KEY_p:
        case GDK_KEY_P:           view_save(v, TRUE); return TRUE;
        default: break;
        }
    }
    double line = 60.0;
    double page = viewport_h(v) / cur_scale(v) - line;
    if (page < line) page = line;
    double vy = gtk_adjustment_get_value(v->vadj);
    double vx = gtk_adjustment_get_value(v->hadj);
    switch (keyval) {
    case GDK_KEY_Down:       gtk_adjustment_set_value(v->vadj, vy + line); break;
    case GDK_KEY_Up:         gtk_adjustment_set_value(v->vadj, vy - line); break;
    case GDK_KEY_Right:      gtk_adjustment_set_value(v->hadj, vx + line); break;
    case GDK_KEY_Left:       gtk_adjustment_set_value(v->hadj, vx - line); break;
    case GDK_KEY_Page_Down:
    case GDK_KEY_space:      gtk_adjustment_set_value(v->vadj, vy + page); break;
    case GDK_KEY_Page_Up:    gtk_adjustment_set_value(v->vadj, vy - page); break;
    case GDK_KEY_Home:       gtk_adjustment_set_value(v->vadj, 0); break;
    case GDK_KEY_End:
        gtk_adjustment_set_value(v->vadj,
                                 gtk_adjustment_get_upper(v->vadj)); break;
    default:                 return FALSE;
    }
    return TRUE;
}

static void
on_key_released(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
                GdkModifierType state, gpointer data)
{
    (void)ctrl;
    (void)keycode;
    NsProcView *v = data;
    if (v->opened)
        start_key(v, 1, keyval, state);
}

static void
on_area_destroy(GtkWidget *widget, gpointer data)
{
    (void)widget;
    NsProcView *v = data;
    v->closed = TRUE;
    g_clear_object(&v->im);
    disarm_anim(v);
    if (v->console_poll_id) {
        g_source_remove(v->console_poll_id);
        v->console_poll_id = 0;
    }
    if (v->ctx_popover) {
        gtk_widget_unparent(v->ctx_popover);
        v->ctx_popover = NULL;
    }
    v->area = NULL;
    Req *req = g_new0(Req, 1);
    req->type = REQ_QUIT;
    push_req(v, req);
    /* Unblock the worker if it is mid-request to a wedged renderer, so the join
     * below can't stall the main loop for up to the 30 s IPC read timeout (which
     * would also trip the watchdog heartbeat and restart the whole shell). */
    g_mutex_lock(&v->proc_lock);
    if (v->proc)
        ns_rproc_http_interrupt(v->proc);
    g_mutex_unlock(&v->proc_lock);
    if (v->thread) {
        g_thread_join(v->thread);
        v->thread = NULL;
    }
    pv_unref(v);
}

static void
console_append(NsProcView *v, const char *text)
{
    if (!v->console_buffer || !text || !*text)
        return;
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(v->console_buffer, &end);
    gtk_text_buffer_insert(v->console_buffer, &end, text, -1);
    if (v->console_view) {
        gtk_text_buffer_get_end_iter(v->console_buffer, &end);
        GtkTextMark *m = gtk_text_buffer_create_mark(v->console_buffer, NULL,
                                                     &end, FALSE);
        gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(v->console_view), m);
        gtk_text_buffer_delete_mark(v->console_buffer, m);
    }
}

static gboolean
console_poll_cb(gpointer data)
{
    NsProcView *v = data;
    if (!v->console_open || !v->opened)
        return G_SOURCE_CONTINUE;
    Req *req = g_new0(Req, 1);
    req->type = REQ_CONSOLE;
    push_req(v, req);
    return G_SOURCE_CONTINUE;
}

static void
on_console_eval(GtkEntry *entry, gpointer data)
{
    NsProcView *v = data;
    const char *src = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (!src || !*src || !v->opened)
        return;
    char *echo = g_strdup_printf("> %s\n", src);
    console_append(v, echo);
    g_free(echo);
    Req *req = g_new0(Req, 1);
    req->type = REQ_EVAL;
    req->query = g_strdup(src);
    push_req(v, req);
    gtk_editable_set_text(GTK_EDITABLE(entry), "");
}

static void
console_set_open(NsProcView *v, gboolean open)
{
    if (!v->console_revealer)
        return;
    v->console_open = open;
    if (open)
        gtk_widget_set_visible(v->console_revealer, TRUE);
    gtk_revealer_set_reveal_child(GTK_REVEALER(v->console_revealer), open);
    if (!open)
        gtk_widget_set_visible(v->console_revealer, FALSE);
    if (open) {
        if (!v->console_poll_id)
            v->console_poll_id = g_timeout_add(NS_PROC_CONSOLE_POLL_MS, console_poll_cb, v);
        gtk_widget_grab_focus(v->console_entry);
    } else {
        if (v->console_poll_id) {
            g_source_remove(v->console_poll_id);
            v->console_poll_id = 0;
        }
        gtk_widget_grab_focus(v->area);
    }
}

static void
on_console_clear(GtkButton *b, gpointer data)
{
    (void)b;
    NsProcView *v = data;
    if (v->console_buffer)
        gtk_text_buffer_set_text(v->console_buffer, "", 0);
}

static void
on_console_close(GtkButton *b, gpointer data)
{ (void)b; console_set_open(data, FALSE); }

static void
build_console_panel(NsProcView *v)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(header, "toolbar");
    GtkWidget *title = gtk_label_new(ns_i18n("Console"));
    gtk_widget_set_hexpand(title, TRUE);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_widget_set_margin_start(title, 6);
    GtkWidget *clear = gtk_button_new_from_icon_name("edit-clear-symbolic");
    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_set_tooltip_text(clear, ns_i18n("Clear console"));
    gtk_widget_set_tooltip_text(close, ns_i18n("Close console (F12)"));
    g_signal_connect(clear, "clicked", G_CALLBACK(on_console_clear), v);
    g_signal_connect(close, "clicked", G_CALLBACK(on_console_close), v);
    gtk_box_append(GTK_BOX(header), title);
    gtk_box_append(GTK_BOX(header), clear);
    gtk_box_append(GTK_BOX(header), close);

    v->console_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(v->console_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(v->console_view), TRUE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(v->console_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(v->console_view),
                                GTK_WRAP_WORD_CHAR);
    v->console_buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(v->console_view));
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), v->console_view);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_size_request(scroll, -1, 180);

    v->console_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(v->console_entry),
                                   ns_i18n("Evaluate JavaScript and press Enter"));
    g_signal_connect(v->console_entry, "activate",
                     G_CALLBACK(on_console_eval), v);

    gtk_box_append(GTK_BOX(box), header);
    gtk_box_append(GTK_BOX(box), scroll);
    gtk_box_append(GTK_BOX(box), v->console_entry);

    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_frame_set_child(GTK_FRAME(frame), box);

    v->console_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(v->console_revealer),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    gtk_revealer_set_child(GTK_REVEALER(v->console_revealer), frame);
    gtk_widget_set_visible(v->console_revealer, FALSE);
}

static void
build_search_bar(NsProcView *v)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(box, "toolbar");
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_end(box, 6);

    v->search_entry = gtk_search_entry_new();
    gtk_widget_set_size_request(v->search_entry, 220, -1);
    g_signal_connect(v->search_entry, "search-changed",
                     G_CALLBACK(on_search_changed), v);
    g_signal_connect(v->search_entry, "activate",
                     G_CALLBACK(on_search_next), v);
    g_signal_connect(v->search_entry, "next-match",
                     G_CALLBACK(on_search_next), v);
    g_signal_connect(v->search_entry, "previous-match",
                     G_CALLBACK(on_search_prev), v);
    g_signal_connect(v->search_entry, "stop-search",
                     G_CALLBACK(on_search_stop), v);

    v->search_label = gtk_label_new("");
    gtk_widget_set_size_request(v->search_label, 56, -1);

    GtkWidget *prev = gtk_button_new_from_icon_name("go-up-symbolic");
    GtkWidget *next = gtk_button_new_from_icon_name("go-down-symbolic");
    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_set_tooltip_text(prev, ns_i18n("Previous match (Shift+Enter)"));
    gtk_widget_set_tooltip_text(next, ns_i18n("Next match (Enter)"));
    gtk_widget_set_tooltip_text(close, ns_i18n("Close (Esc)"));
    g_signal_connect(prev, "clicked", G_CALLBACK(on_search_prev), v);
    g_signal_connect(next, "clicked", G_CALLBACK(on_search_next), v);
    g_signal_connect(close, "clicked", G_CALLBACK(on_search_close_clicked), v);

    gtk_box_append(GTK_BOX(box), v->search_entry);
    gtk_box_append(GTK_BOX(box), v->search_label);
    gtk_box_append(GTK_BOX(box), prev);
    gtk_box_append(GTK_BOX(box), next);
    gtk_box_append(GTK_BOX(box), close);

    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_frame_set_child(GTK_FRAME(frame), box);

    v->search_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(v->search_revealer),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_child(GTK_REVEALER(v->search_revealer), frame);
    gtk_widget_set_halign(v->search_revealer, GTK_ALIGN_END);
    gtk_widget_set_valign(v->search_revealer, GTK_ALIGN_START);
    gtk_overlay_add_overlay(GTK_OVERLAY(v->root), v->search_revealer);
}

NsProcView *
ns_proc_view_new(void)
{
    NsProcView *v = g_new0(NsProcView, 1);
    g_ref_count_init(&v->rc);
    g_mutex_init(&v->proc_lock);
    v->renderer_path = ns_proc_renderer_path();
    v->queue = g_async_queue_new();
    v->history = g_ptr_array_new_with_free_func(g_free);
    v->hist_index = -1;
    v->link_pending_action = ACT_HOVER;
    v->pending_record = TRUE;
    v->scale = 1.0;

    v->hadj = gtk_adjustment_new(0, 0, 1, 60, 60, 1);
    v->vadj = gtk_adjustment_new(0, 0, 1, 60, 60, 1);
    g_signal_connect(v->hadj, "value-changed", G_CALLBACK(on_adj_changed), v);
    g_signal_connect(v->vadj, "value-changed", G_CALLBACK(on_adj_changed), v);

    v->area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(v->area, TRUE);
    gtk_widget_set_vexpand(v->area, TRUE);
    gtk_widget_set_focusable(v->area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(v->area), on_draw, v, NULL);
    g_signal_connect(v->area, "resize", G_CALLBACK(on_resize), v);

    GtkWidget *grid = gtk_grid_new();
    GtkWidget *vscroll =
        gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, v->vadj);
    GtkWidget *hscroll =
        gtk_scrollbar_new(GTK_ORIENTATION_HORIZONTAL, v->hadj);
    gtk_grid_attach(GTK_GRID(grid), v->area, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), vscroll, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), hscroll, 0, 1, 1, 1);

    v->root = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(v->root), grid);
    build_search_bar(v);

    GtkWidget *overlay = v->root;
    gtk_widget_set_vexpand(overlay, TRUE);
    v->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(v->root), overlay);
    build_console_panel(v);
    gtk_box_append(GTK_BOX(v->root), v->console_revealer);

    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_pressed), v);
    g_signal_connect(click, "released", G_CALLBACK(on_released), v);
    gtk_widget_add_controller(v->area, GTK_EVENT_CONTROLLER(click));

    GtkGesture *drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), v);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), v);
    gtk_widget_add_controller(v->area, GTK_EVENT_CONTROLLER(drag));

    GtkGesture *middle = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(middle), GDK_BUTTON_MIDDLE);
    g_signal_connect(middle, "pressed", G_CALLBACK(on_pressed), v);
    gtk_widget_add_controller(v->area, GTK_EVENT_CONTROLLER(middle));

    ctx_install_actions(v);
    GtkGesture *secondary = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(secondary),
                                  GDK_BUTTON_SECONDARY);
    g_signal_connect(secondary, "pressed", G_CALLBACK(on_secondary_pressed), v);
    gtk_widget_add_controller(v->area, GTK_EVENT_CONTROLLER(secondary));

    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), v);
    gtk_widget_add_controller(v->area, motion);

    GtkEventController *scroll =
        gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), v);
    gtk_widget_add_controller(v->area, scroll);

    GtkEventController *key = gtk_event_controller_key_new();
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_key), v);
    g_signal_connect(key, "key-released", G_CALLBACK(on_key_released), v);
    v->im = gtk_im_multicontext_new();
    gtk_im_context_set_client_widget(v->im, v->area);
    g_signal_connect(v->im, "commit", G_CALLBACK(on_im_commit), v);
    gtk_event_controller_key_set_im_context(GTK_EVENT_CONTROLLER_KEY(key),
                                            v->im);
    gtk_widget_add_controller(v->area, key);

    g_signal_connect(v->area, "destroy", G_CALLBACK(on_area_destroy), v);

    v->thread = g_thread_new("ns-proc-view", worker_main, pv_ref(v));
    return v;
}

GtkWidget *ns_proc_view_widget(NsProcView *v) { return v->root; }

void
ns_proc_view_set_notify(NsProcView *v, NsProcNotify cb, gpointer ud)
{
    v->notify = cb;
    v->notify_ud = ud;
}
