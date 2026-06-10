/* Nordstjernen — HTTP/JSON renderer server (IPC experiment, pixels in body). */

#define _GNU_SOURCE
#include "ipc_http.h"
#include "libnordstjernen.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#endif

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

int
main(int argc, char **argv)
{
    if (argc < 3)
        return 2;
    int max_w = atoi(argv[1]);
    int max_h = atoi(argv[2]);
    if (max_w <= 0 || max_h <= 0)
        return 2;

#ifdef __linux__
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() == 1)
        return 0;
#endif

    int shm_mode = argc > 3 && strcmp(argv[3], "shm") == 0;
    size_t fb_size = (size_t)max_w * (size_t)max_h * 4u;
    unsigned char *fb;

#ifdef _WIN32
    HANDLE proc = GetCurrentProcess();
    HANDLE ipc_in = NULL, ipc_out = NULL;
    if (!DuplicateHandle(proc, GetStdHandle(STD_INPUT_HANDLE), proc, &ipc_in, 0,
                         FALSE, DUPLICATE_SAME_ACCESS) ||
        !DuplicateHandle(proc, GetStdHandle(STD_OUTPUT_HANDLE), proc, &ipc_out,
                         0, FALSE, DUPLICATE_SAME_ACCESS))
        return 2;
    HANDLE nul_in = CreateFileA("NUL", GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                OPEN_EXISTING, 0, NULL);
    HANDLE nul_out = CreateFileA("NUL", GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                 OPEN_EXISTING, 0, NULL);
    if (nul_in != INVALID_HANDLE_VALUE)
        SetStdHandle(STD_INPUT_HANDLE, nul_in);
    if (nul_out != INVALID_HANDLE_VALUE)
        SetStdHandle(STD_OUTPUT_HANDLE, nul_out);
    FILE *redir_in = freopen("NUL", "r", stdin);
    FILE *redir_out = freopen("NUL", "w", stdout);
    (void)redir_in;
    (void)redir_out;
    int ctrl_r = _open_osfhandle((intptr_t)ipc_in, _O_BINARY);
    int ctrl_w = _open_osfhandle((intptr_t)ipc_out, _O_BINARY);
    if (ctrl_r < 0 || ctrl_w < 0)
        return 2;
    HANDLE shm_handle = NULL;
    if (shm_mode) {
        if (argc < 5)
            return 2;
        shm_handle = (HANDLE)(uintptr_t)_strtoui64(argv[4], NULL, 10);
        fb = MapViewOfFile(shm_handle, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                           fb_size);
        if (!fb)
            return 2;
    } else {
        fb = malloc(fb_size);
        if (!fb)
            return 2;
    }
#else
    signal(SIGPIPE, SIG_IGN);
    int ctrl_r = 3, ctrl_w = 3;
    if (shm_mode) {
        int fd = http_recv_fd(ctrl_r);
        if (fd < 0)
            return 2;
        fb = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (fb == MAP_FAILED)
            return 2;
    } else {
        fb = malloc(fb_size);
        if (!fb)
            return 2;
    }
#endif

    if (ns_browser_init() != 0) {
        free(fb);
        return 2;
    }
    ns_browser_sandbox(argv[0]);

    http_set_bufsize(ctrl_r, (int)fb_size + 65536);
    http_set_bufsize(ctrl_w, (int)fb_size + 65536);

    http_conn c;
    http_conn_init(&c, ctrl_r);

    ns_browser *cur = NULL;
    int tick_budget_ms = 16;
    int frame_valid = 0;
    long frame_sx = 0, frame_sy = 0;
    int frame_w = 0, frame_h = 0;
    double frame_scale = 1.0;

    for (;;) {
        http_head head;
        if (http_read_head(&c, &head) != 0)
            break;

        char *body = NULL;
        if (head.content_length > 0) {
            body = malloc((size_t)head.content_length + 1);
            if (!body || http_read_body(&c, head.content_length, body) != 0) {
                free(body);
                break;
            }
            body[head.content_length] = '\0';
        }

        if (strcmp(head.path, "/quit") == 0) {
            http_write_response(ctrl_w, 200, "text/plain", NULL, NULL, 0);
            free(body);
            break;
        }

        if (strcmp(head.path, "/webgl") == 0) {
            char *origin = json_get_str(body, "origin");
            long allow = 0;
            json_get_long(body, "allow", &allow);
            if (cur && origin)
                ns_browser_resolve_webgl(cur, origin, (int)allow);
            frame_valid = 0;
            http_write_response(ctrl_w, 200, "text/plain", NULL, NULL, 0);
            free(origin);
            free(body);
            continue;
        }

        if (strcmp(head.path, "/open") == 0) {
            char *url = json_get_str(body, "url");
            long w = 0, h = 0, settle = 0;
            json_get_long(body, "width", &w);
            json_get_long(body, "height", &h);
            json_get_long(body, "settle_ms", &settle);
            int vw = clamp((int)w, 1, max_w);
            int vh = clamp((int)h, 1, max_h);
            if (cur) {
                ns_browser_close(cur);
                cur = NULL;
            }
            frame_valid = 0;
            cur = url ? ns_browser_open_viewport(url, vw, vh, (int)settle)
                      : NULL;
            int pw = 0, ph = 0, ok = cur != NULL;
            char *title = NULL, *final_url = NULL, *nav = NULL;
            char *te = NULL, *ue = NULL, *ne = NULL;
            if (cur) {
                ns_browser_page_size(cur, &pw, &ph);
                title = ns_browser_title(cur);
                final_url = ns_browser_url(cur);
                nav = ns_browser_take_pending_nav(cur);
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
            free(body);
            continue;
        }

        if (strcmp(head.path, "/render") == 0) {
            long w = 0, h = 0, sx = 0, sy = 0;
            double scale = 1.0;
            json_get_long(body, "width", &w);
            json_get_long(body, "height", &h);
            json_get_long(body, "scroll_x", &sx);
            json_get_long(body, "scroll_y", &sy);
            json_get_double(body, "scale", &scale);
            int vw = clamp((int)w, 1, max_w);
            int vh = clamp((int)h, 1, max_h);
            int stride = vw * 4;
            if (!cur) {
                http_write_response(ctrl_w, 200, "application/octet-stream",
                                    "X-W: 0\r\nX-H: 0\r\nX-Stride: 0\r\n"
                                    "X-Anim: 0\r\n", NULL, 0);
                free(body);
                continue;
            }
            int ticked = ns_browser_tick(cur, tick_budget_ms);
            int unchanged = frame_valid && ticked == 0 &&
                            sx == frame_sx && sy == frame_sy &&
                            vw == frame_w && vh == frame_h &&
                            scale == frame_scale;
            if (!unchanged) {
                ns_browser_render_argb32(cur, (int)sx, (int)sy, vw, vh, scale,
                                         fb, stride);
                frame_valid = 1;
                frame_sx = sx;
                frame_sy = sy;
                frame_w = vw;
                frame_h = vh;
                frame_scale = scale;
            }
            char *nav = ns_browser_take_pending_nav(cur);
            if (nav)
                for (char *p = nav; *p; p++)
                    if (*p == '\r' || *p == '\n') *p = ' ';
            char *webgl = ns_browser_take_pending_webgl(cur);
            if (webgl)
                for (char *p = webgl; *p; p++)
                    if (*p == '\r' || *p == '\n') *p = ' ';
            char hdrs[4608];
            int hn = snprintf(hdrs, sizeof hdrs,
                     "X-W: %d\r\nX-H: %d\r\nX-Stride: %d\r\nX-Anim: %d\r\n%s",
                     vw, vh, stride, ns_browser_animating(cur) ? 1 : 0,
                     unchanged ? "X-Unchanged: 1\r\n" : "");
            if (nav && *nav && hn > 0 && (size_t)hn < sizeof hdrs)
                hn += snprintf(hdrs + hn, sizeof hdrs - (size_t)hn,
                               "X-Nav: %.2000s\r\n", nav);
            if (webgl && *webgl && hn > 0 && (size_t)hn < sizeof hdrs)
                snprintf(hdrs + hn, sizeof hdrs - (size_t)hn,
                         "X-WebGL: %.2000s\r\n", webgl);
            free(nav);
            free(webgl);
            if (shm_mode || unchanged)
                http_write_response(ctrl_w, 200, "application/octet-stream",
                                    hdrs, NULL, 0);
            else
                http_write_response(ctrl_w, 200, "application/octet-stream",
                                    hdrs, fb, (size_t)stride * (size_t)vh);
            free(body);
            continue;
        }

        if (strcmp(head.path, "/link") == 0 ||
            strcmp(head.path, "/click") == 0 ||
            strcmp(head.path, "/select") == 0) {
            long x = 0, y = 0, mods = 0, kind = 0;
            json_get_long(body, "x", &x);
            json_get_long(body, "y", &y);
            json_get_long(body, "mods", &mods);
            json_get_long(body, "kind", &kind);
            char *href = NULL;
            if (cur && strcmp(head.path, "/link") == 0) {
                href = ns_browser_link_at(cur, (int)x, (int)y);
                char *cursor = ns_browser_cursor_at(cur, (int)x, (int)y);
                reply_str2(ctrl_w, "href", href, "cursor", cursor);
                free(cursor);
                free(href);
                free(body);
                continue;
            }
            frame_valid = 0;
            if (cur && strcmp(head.path, "/click") == 0)
                href = ns_browser_click(cur, (int)x, (int)y, (int)mods);
            else if (cur)
                href = ns_browser_select(cur, (int)kind, (int)x, (int)y);
            reply_str(ctrl_w, "href", href);
            free(href);
            free(body);
            continue;
        }

        if (strcmp(head.path, "/key") == 0) {
            long kind = 0, keycode = 0, mods = 0;
            json_get_long(body, "kind", &kind);
            json_get_long(body, "keycode", &keycode);
            json_get_long(body, "mods", &mods);
            char *key = json_get_str(body, "key");
            char *code = json_get_str(body, "code");
            frame_valid = 0;
            char *href = cur ? ns_browser_key(cur, (int)kind, key ? key : "",
                                              code ? code : "", (int)keycode,
                                              (int)mods)
                             : NULL;
            reply_str(ctrl_w, "href", href);
            free(href);
            free(key);
            free(code);
            free(body);
            continue;
        }

        if (strcmp(head.path, "/release") == 0) {
            frame_valid = 0;
            int changed = cur ? ns_browser_release(cur) : 0;
            char json[32];
            int n = snprintf(json, sizeof json, "{\"changed\":%d}",
                             changed > 0 ? 1 : 0);
            http_write_response(ctrl_w, 200, "application/json", NULL, json,
                                (size_t)n);
            free(body);
            continue;
        }

        if (strcmp(head.path, "/hover") == 0) {
            long x = 0, y = 0;
            json_get_long(body, "x", &x);
            json_get_long(body, "y", &y);
            int changed = cur ? ns_browser_hover(cur, (int)x, (int)y) : 0;
            if (changed > 0)
                frame_valid = 0;
            char json[32];
            int n = snprintf(json, sizeof json, "{\"changed\":%d}",
                             changed > 0 ? 1 : 0);
            http_write_response(ctrl_w, 200, "application/json", NULL, json,
                                (size_t)n);
            free(body);
            continue;
        }

        if (strcmp(head.path, "/find") == 0) {
            long cs = 0, dir = 0, from_y = 0;
            json_get_long(body, "case_sensitive", &cs);
            json_get_long(body, "direction", &dir);
            json_get_long(body, "from_y", &from_y);
            char *q = json_get_str(body, "query");
            int total = 0, current = 0, scroll_y = 0;
            frame_valid = 0;
            if (cur)
                ns_browser_find(cur, q ? q : "", (int)cs, (int)dir, (int)from_y,
                                &total, &current, &scroll_y);
            char json[96];
            int n = snprintf(json, sizeof json,
                             "{\"total\":%d,\"current\":%d,\"scroll_y\":%d}",
                             total, current, scroll_y);
            http_write_response(ctrl_w, 200, "application/json", NULL, json,
                                (size_t)n);
            free(q);
            free(body);
            continue;
        }

        if (strcmp(head.path, "/viewport") == 0) {
            long w = 0, h = 0;
            json_get_long(body, "width", &w);
            json_get_long(body, "height", &h);
            int vw = clamp((int)w, 1, max_w);
            int vh = clamp((int)h, 1, max_h);
            int pw = 0, ph = 0, ok = 0;
            frame_valid = 0;
            if (cur && ns_browser_set_viewport(cur, vw, vh) == 0) {
                ns_browser_page_size(cur, &pw, &ph);
                ok = 1;
            }
            char json[96];
            int n = snprintf(json, sizeof json,
                             "{\"ok\":%d,\"page_width\":%d,\"page_height\":%d}",
                             ok, pw, ph);
            http_write_response(ctrl_w, 200, "application/json", NULL, json,
                                (size_t)n);
            free(body);
            continue;
        }

        if (strcmp(head.path, "/eval") == 0) {
            char *src = json_get_str(body, "src");
            frame_valid = 0;
            char *res = cur ? ns_browser_eval(cur, src ? src : "") : NULL;
            reply_str(ctrl_w, "text", res);
            free(res);
            free(src);
            free(body);
            continue;
        }

        if (strcmp(head.path, "/console") == 0) {
            char *log = cur ? ns_browser_console_drain(cur) : NULL;
            reply_str(ctrl_w, "text", log);
            free(log);
            free(body);
            continue;
        }

        if (strcmp(head.path, "/media") == 0) {
            long x = 0, y = 0;
            json_get_long(body, "x", &x);
            json_get_long(body, "y", &y);
            int is_video = 0, stream = 0;
            char *url = cur ? ns_browser_media_at(cur, (int)x, (int)y,
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
            free(body);
            continue;
        }

        if (strcmp(head.path, "/export") == 0) {
            char *path = json_get_str(body, "path");
            frame_valid = 0;
            int rc = (cur && path) ? ns_browser_render_image(cur, path) : -1;
            char json[24];
            int n = snprintf(json, sizeof json, "{\"ok\":%d}", rc);
            http_write_response(ctrl_w, 200, "application/json", NULL, json,
                                (size_t)n);
            free(path);
            free(body);
            continue;
        }

        http_write_response(ctrl_w, 404, "text/plain", NULL, NULL, 0);
        free(body);
    }

    if (cur)
        ns_browser_close(cur);
    ns_browser_shutdown();
    if (!shm_mode)
        free(fb);
#ifdef _WIN32
    else
        UnmapViewOfFile(fb);
#else
    else
        munmap(fb, fb_size);
#endif
    return 0;
}
