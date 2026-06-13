/* Nordstjernen — renderer request dispatch over the HTTP/JSON IPC protocol,
   shared by nordstjernen-renderer and the single-process in-process host. */

#define _GNU_SOURCE
#include "renderer_serve.h"
#include "libnordstjernen.h"
#include "net.h"
#include "image.h"
#include "texture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ns_renderer_session {
    int            ctrl_w;
    unsigned char *fb;
    int            max_w;
    int            max_h;
    int            shm_mode;
    ns_browser    *cur;
    int            tick_budget_ms;
    int            frame_valid;
    long           frame_sx;
    long           frame_sy;
    int            frame_w;
    int            frame_h;
    double         frame_scale;
    char          *post_url;
    char          *post_body;
    size_t         post_len;
    char          *post_ct;
};

static void
session_stash_post(ns_renderer_session *s, const char *href)
{
    if (!s || !s->cur)
        return;
    size_t len = 0;
    char *ct = NULL;
    char *pb = ns_browser_take_post(s->cur, &len, &ct);
    if (!pb)
        return;
    free(s->post_url);
    free(s->post_body);
    free(s->post_ct);
    s->post_url = (href && *href) ? strdup(href) : NULL;
    s->post_body = pb;
    s->post_len = len;
    s->post_ct = ct;
}

static void
session_clear_post(ns_renderer_session *s)
{
    if (!s)
        return;
    free(s->post_url);
    free(s->post_body);
    free(s->post_ct);
    s->post_url = NULL;
    s->post_body = NULL;
    s->post_ct = NULL;
    s->post_len = 0;
}

static int
clamp(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static void
reply_str(int fd, const char *key, const char *val)
{
    char *e = json_escape(val ? val : "");
    char *json = NULL;
    int n = asprintf(&json, "{\"%s\":\"%s\"}", key, e ? e : "");
    if (n > 0)
        http_write_response(fd, 200, "application/json", NULL, json,
                            (size_t)n);
    free(json);
    free(e);
}

static void
reply_str2(int fd, const char *key1, const char *val1,
           const char *key2, const char *val2)
{
    char *e1 = json_escape(val1 ? val1 : "");
    char *e2 = json_escape(val2 ? val2 : "");
    char *json = NULL;
    int n = asprintf(&json, "{\"%s\":\"%s\",\"%s\":\"%s\"}",
                     key1, e1 ? e1 : "", key2, e2 ? e2 : "");
    if (n > 0)
        http_write_response(fd, 200, "application/json", NULL, json,
                            (size_t)n);
    free(json);
    free(e1);
    free(e2);
}

static void
reply_href_changed(int fd, const char *href, int changed)
{
    char *e = json_escape(href ? href : "");
    char *json = NULL;
    int n = asprintf(&json, "{\"href\":\"%s\",\"changed\":%d}",
                     e ? e : "", changed ? 1 : 0);
    if (n > 0)
        http_write_response(fd, 200, "application/json", NULL, json,
                            (size_t)n);
    free(json);
    free(e);
}

static unsigned char *
favicon_bgra(ns_browser *b, int *out_w, int *out_h)
{
    char *url = ns_browser_favicon_url(b);
    if (!url || !*url) {
        free(url);
        return NULL;
    }
    ns_response *resp = ns_net_fetch_blocking(url, NULL, NULL);
    free(url);
    if (!resp || resp->status >= 400 || !resp->body || resp->body->len == 0) {
        if (resp)
            ns_response_free(resp);
        return NULL;
    }
    int w = 0, h = 0;
    ns_texture *tex =
        ns_image_decode_bytes(resp->body->data, resp->body->len, &w, &h);
    ns_response_free(resp);
    if (!tex)
        return NULL;
    if (w < 1 || h < 1 || w > 512 || h > 512) {
        ns_texture_unref(tex);
        return NULL;
    }
    gsize stride = (gsize)w * 4u;
    unsigned char *px = malloc(stride * (gsize)h);
    if (!px) {
        ns_texture_unref(tex);
        return NULL;
    }
    ns_texture_download(tex, px, stride);
    ns_texture_unref(tex);
    *out_w = w;
    *out_h = h;
    return px;
}

ns_renderer_session *
ns_renderer_session_new(int ctrl_w, unsigned char *fb, int max_w, int max_h,
                        int shm_mode)
{
    if (!fb || max_w <= 0 || max_h <= 0)
        return NULL;
    ns_renderer_session *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->ctrl_w = ctrl_w;
    s->fb = fb;
    s->max_w = max_w;
    s->max_h = max_h;
    s->shm_mode = shm_mode;
    s->tick_budget_ms = 16;
    s->frame_scale = 1.0;
    return s;
}

void
ns_renderer_session_free(ns_renderer_session *s)
{
    if (!s)
        return;
    session_clear_post(s);
    if (s->cur)
        ns_browser_close(s->cur);
    free(s);
}

int
ns_renderer_session_handle(ns_renderer_session *s, const http_head *head,
                           const char *body)
{
    int ctrl_w = s->ctrl_w;

    if (strcmp(head->path, "/quit") == 0) {
        http_write_response(ctrl_w, 200, "text/plain", NULL, NULL, 0);
        return 1;
    }

    if (strcmp(head->path, "/webgl") == 0) {
        char *origin = json_get_str(body, "origin");
        long allow = 0;
        json_get_long(body, "allow", &allow);
        if (s->cur && origin)
            ns_browser_resolve_webgl(s->cur, origin, (int)allow);
        s->frame_valid = 0;
        http_write_response(ctrl_w, 200, "text/plain", NULL, NULL, 0);
        free(origin);
        return 0;
    }

    if (strcmp(head->path, "/open") == 0) {
        char *url = json_get_str(body, "url");
        long w = 0, h = 0, settle = 0;
        json_get_long(body, "width", &w);
        json_get_long(body, "height", &h);
        json_get_long(body, "settle_ms", &settle);
        int vw = clamp((int)w, 1, s->max_w);
        int vh = clamp((int)h, 1, s->max_h);
        if (s->cur) {
            ns_browser_close(s->cur);
            s->cur = NULL;
        }
        s->frame_valid = 0;
        if (url && s->post_body && s->post_url &&
            strcmp(url, s->post_url) == 0)
            s->cur = ns_browser_open_post_viewport(url, vw, vh, (int)settle,
                                                   s->post_body, s->post_len,
                                                   s->post_ct);
        else
            s->cur = url ? ns_browser_open_viewport(url, vw, vh, (int)settle)
                         : NULL;
        session_clear_post(s);
        int pw = 0, ph = 0, ok = s->cur != NULL;
        char *title = NULL, *final_url = NULL, *nav = NULL;
        char *te = NULL, *ue = NULL, *ne = NULL;
        if (s->cur) {
            ns_browser_page_size(s->cur, &pw, &ph);
            title = ns_browser_title(s->cur);
            final_url = ns_browser_url(s->cur);
            nav = ns_browser_take_pending_nav(s->cur);
        }
        te = json_escape(title ? title : "");
        ue = json_escape(final_url ? final_url : (url ? url : ""));
        ne = json_escape(nav ? nav : "");
        char *json = NULL;
        int n = asprintf(&json,
                         "{\"ok\":%d,\"page_width\":%d,\"page_height\":%d,"
                         "\"title\":\"%s\",\"url\":\"%s\",\"nav\":\"%s\"}",
                         ok, pw, ph, te ? te : "", ue ? ue : "",
                         ne ? ne : "");
        if (n >= 0)
            http_write_response(ctrl_w, 200, "application/json", NULL, json,
                                (size_t)n);
        free(json);
        free(te);
        free(ue);
        free(ne);
        free(title);
        free(final_url);
        free(nav);
        free(url);
        return 0;
    }

    if (strcmp(head->path, "/render") == 0) {
        long w = 0, h = 0, sx = 0, sy = 0;
        double scale = 1.0;
        json_get_long(body, "width", &w);
        json_get_long(body, "height", &h);
        json_get_long(body, "scroll_x", &sx);
        json_get_long(body, "scroll_y", &sy);
        json_get_double(body, "scale", &scale);
        int vw = clamp((int)w, 1, s->max_w);
        int vh = clamp((int)h, 1, s->max_h);
        int stride = vw * 4;
        if (!s->cur) {
            http_write_response(ctrl_w, 200, "application/octet-stream",
                                "X-W: 0\r\nX-H: 0\r\nX-Stride: 0\r\n"
                                "X-Anim: 0\r\n", NULL, 0);
            return 0;
        }
        int ticked = ns_browser_tick(s->cur, s->tick_budget_ms);
        int unchanged = s->frame_valid && ticked == 0 &&
                        sx == s->frame_sx && sy == s->frame_sy &&
                        vw == s->frame_w && vh == s->frame_h &&
                        scale == s->frame_scale;
        if (!unchanged) {
            ns_browser_render_argb32(s->cur, (int)sx, (int)sy, vw, vh, scale,
                                     s->fb, stride);
            s->frame_valid = 1;
            s->frame_sx = sx;
            s->frame_sy = sy;
            s->frame_w = vw;
            s->frame_h = vh;
            s->frame_scale = scale;
        }
        char *nav = ns_browser_take_pending_nav(s->cur);
        if (nav)
            for (char *p = nav; *p; p++)
                if (*p == '\r' || *p == '\n') *p = ' ';
        char *webgl = ns_browser_take_pending_webgl(s->cur);
        if (webgl)
            for (char *p = webgl; *p; p++)
                if (*p == '\r' || *p == '\n') *p = ' ';
        char *download = ns_browser_take_pending_download(s->cur);
        if (download)
            for (char *p = download; *p; p++)
                if (*p == '\r' || *p == '\n') *p = ' ';
        char hdrs[4608];
        int hn = snprintf(hdrs, sizeof hdrs,
                 "X-W: %d\r\nX-H: %d\r\nX-Stride: %d\r\nX-Anim: %d\r\n%s",
                 vw, vh, stride, ns_browser_animating(s->cur) ? 1 : 0,
                 unchanged ? "X-Unchanged: 1\r\n" : "");
        if (nav && *nav && hn > 0 && (size_t)hn < sizeof hdrs)
            hn += snprintf(hdrs + hn, sizeof hdrs - (size_t)hn,
                           "X-Nav: %.2000s\r\n", nav);
        if (webgl && *webgl && hn > 0 && (size_t)hn < sizeof hdrs)
            hn += snprintf(hdrs + hn, sizeof hdrs - (size_t)hn,
                     "X-WebGL: %.2000s\r\n", webgl);
        if (download && *download && hn > 0 && (size_t)hn < sizeof hdrs)
            snprintf(hdrs + hn, sizeof hdrs - (size_t)hn,
                     "X-Download: %.3000s\r\n", download);
        free(nav);
        free(webgl);
        free(download);
        if (s->shm_mode || unchanged)
            http_write_response(ctrl_w, 200, "application/octet-stream",
                                hdrs, NULL, 0);
        else
            http_write_response(ctrl_w, 200, "application/octet-stream",
                                hdrs, s->fb, (size_t)stride * (size_t)vh);
        return 0;
    }

    if (strcmp(head->path, "/favicon") == 0) {
        int fw = 0, fh = 0;
        unsigned char *px = s->cur ? favicon_bgra(s->cur, &fw, &fh) : NULL;
        if (px) {
            char hdrs[96];
            int hn = snprintf(hdrs, sizeof hdrs,
                              "X-W: %d\r\nX-H: %d\r\nX-Stride: %d\r\n",
                              fw, fh, fw * 4);
            http_write_response(ctrl_w, 200, "application/octet-stream",
                                hn > 0 ? hdrs : NULL, px,
                                (size_t)fw * (size_t)fh * 4u);
            free(px);
        } else {
            http_write_response(ctrl_w, 200, "application/octet-stream",
                                "X-W: 0\r\nX-H: 0\r\nX-Stride: 0\r\n", NULL, 0);
        }
        return 0;
    }

    if (strcmp(head->path, "/link") == 0 ||
        strcmp(head->path, "/click") == 0 ||
        strcmp(head->path, "/select") == 0) {
        long x = 0, y = 0, mods = 0, kind = 0;
        json_get_long(body, "x", &x);
        json_get_long(body, "y", &y);
        json_get_long(body, "mods", &mods);
        json_get_long(body, "kind", &kind);
        char *href = NULL;
        if (s->cur && strcmp(head->path, "/link") == 0) {
            href = ns_browser_link_at(s->cur, (int)x, (int)y);
            char *cursor = ns_browser_cursor_at(s->cur, (int)x, (int)y);
            reply_str2(ctrl_w, "href", href, "cursor", cursor);
            free(cursor);
            free(href);
            return 0;
        }
        s->frame_valid = 0;
        if (s->cur && strcmp(head->path, "/click") == 0) {
            href = ns_browser_press(s->cur, (int)x, (int)y, (int)mods);
            session_stash_post(s, href);
        } else if (s->cur)
            href = ns_browser_select(s->cur, (int)kind, (int)x, (int)y);
        reply_str(ctrl_w, "href", href);
        free(href);
        return 0;
    }

    if (strcmp(head->path, "/key") == 0) {
        long kind = 0, keycode = 0, mods = 0;
        json_get_long(body, "kind", &kind);
        json_get_long(body, "keycode", &keycode);
        json_get_long(body, "mods", &mods);
        char *key = json_get_str(body, "key");
        char *code = json_get_str(body, "code");
        s->frame_valid = 0;
        int prevented = 0;
        char *href = s->cur ? ns_browser_key_full(s->cur, (int)kind,
                                                  key ? key : "",
                                                  code ? code : "",
                                                  (int)keycode, (int)mods,
                                                  &prevented)
                            : NULL;
        session_stash_post(s, href);
        char *e = json_escape(href ? href : "");
        char *json = NULL;
        int n = asprintf(&json, "{\"href\":\"%s\",\"prevented\":%d}",
                         e ? e : "", prevented ? 1 : 0);
        if (n > 0)
            http_write_response(ctrl_w, 200, "application/json", NULL, json,
                                (size_t)n);
        free(json);
        free(e);
        free(href);
        free(key);
        free(code);
        return 0;
    }

    if (strcmp(head->path, "/release") == 0) {
        s->frame_valid = 0;
        int changed = 0;
        char *href = s->cur ? ns_browser_release_click(s->cur, &changed) : NULL;
        session_stash_post(s, href);
        reply_href_changed(ctrl_w, href, changed > 0);
        free(href);
        return 0;
    }

    if (strcmp(head->path, "/hover") == 0) {
        long x = 0, y = 0;
        json_get_long(body, "x", &x);
        json_get_long(body, "y", &y);
        int changed = s->cur ? ns_browser_hover(s->cur, (int)x, (int)y) : 0;
        if (changed > 0)
            s->frame_valid = 0;
        char *href = s->cur ? ns_browser_link_at(s->cur, (int)x, (int)y)
                            : NULL;
        char *cursor = s->cur ? ns_browser_cursor_at(s->cur, (int)x, (int)y)
                              : NULL;
        char *he = json_escape(href ? href : "");
        char *ce = json_escape(cursor ? cursor : "");
        char *json = NULL;
        int n = asprintf(&json,
                         "{\"changed\":%d,\"href\":\"%s\",\"cursor\":\"%s\"}",
                         changed > 0 ? 1 : 0, he ? he : "", ce ? ce : "");
        if (n > 0)
            http_write_response(ctrl_w, 200, "application/json", NULL,
                                json, (size_t)n);
        free(json);
        free(he);
        free(ce);
        free(href);
        free(cursor);
        return 0;
    }

    if (strcmp(head->path, "/find") == 0) {
        long cs = 0, dir = 0, from_y = 0;
        json_get_long(body, "case_sensitive", &cs);
        json_get_long(body, "direction", &dir);
        json_get_long(body, "from_y", &from_y);
        char *q = json_get_str(body, "query");
        int total = 0, current = 0, scroll_y = 0;
        s->frame_valid = 0;
        if (s->cur)
            ns_browser_find(s->cur, q ? q : "", (int)cs, (int)dir,
                            (int)from_y, &total, &current, &scroll_y);
        char json[96];
        int n = snprintf(json, sizeof json,
                         "{\"total\":%d,\"current\":%d,\"scroll_y\":%d}",
                         total, current, scroll_y);
        http_write_response(ctrl_w, 200, "application/json", NULL, json,
                            (size_t)n);
        free(q);
        return 0;
    }

    if (strcmp(head->path, "/viewport") == 0) {
        long w = 0, h = 0;
        json_get_long(body, "width", &w);
        json_get_long(body, "height", &h);
        int vw = clamp((int)w, 1, s->max_w);
        int vh = clamp((int)h, 1, s->max_h);
        int pw = 0, ph = 0, ok = 0;
        s->frame_valid = 0;
        if (s->cur && ns_browser_set_viewport(s->cur, vw, vh) == 0) {
            ns_browser_page_size(s->cur, &pw, &ph);
            ok = 1;
        }
        char json[96];
        int n = snprintf(json, sizeof json,
                         "{\"ok\":%d,\"page_width\":%d,\"page_height\":%d}",
                         ok, pw, ph);
        http_write_response(ctrl_w, 200, "application/json", NULL, json,
                            (size_t)n);
        return 0;
    }

    if (strcmp(head->path, "/eval") == 0) {
        char *src = json_get_str(body, "src");
        s->frame_valid = 0;
        char *res = s->cur ? ns_browser_eval(s->cur, src ? src : "") : NULL;
        reply_str(ctrl_w, "text", res);
        free(res);
        free(src);
        return 0;
    }

    if (strcmp(head->path, "/console") == 0) {
        char *log = s->cur ? ns_browser_console_drain(s->cur) : NULL;
        reply_str(ctrl_w, "text", log);
        free(log);
        return 0;
    }

    if (strcmp(head->path, "/media") == 0) {
        long x = 0, y = 0;
        json_get_long(body, "x", &x);
        json_get_long(body, "y", &y);
        int is_video = 0, stream = 0;
        char *url = s->cur ? ns_browser_media_at(s->cur, (int)x, (int)y,
                                                 &is_video, &stream)
                           : NULL;
        char *e = json_escape(url ? url : "");
        char *json = NULL;
        int n = asprintf(&json,
                         "{\"url\":\"%s\",\"is_video\":%d,\"stream\":%d}",
                         e ? e : "", is_video, stream);
        if (n > 0)
            http_write_response(ctrl_w, 200, "application/json", NULL,
                                json, (size_t)n);
        free(json);
        free(e);
        free(url);
        return 0;
    }

    if (strcmp(head->path, "/export") == 0) {
        char *path = json_get_str(body, "path");
        s->frame_valid = 0;
        int rc = (s->cur && path) ? ns_browser_render_image(s->cur, path)
                                  : -1;
        char json[24];
        int n = snprintf(json, sizeof json, "{\"ok\":%d}", rc);
        http_write_response(ctrl_w, 200, "application/json", NULL, json,
                            (size_t)n);
        free(path);
        return 0;
    }

    http_write_response(ctrl_w, 404, "text/plain", NULL, NULL, 0);
    return 0;
}
