/* Nordstjernen — external media player launching (frontend-agnostic).
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "media.h"

#include <gio/gio.h>
#include <string.h>

#ifdef G_OS_WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#endif

gboolean
ns_media_url_is_safe(const char *u, gboolean allow_local)
{
    if (!u || !*u || u[0] == '-' || strlen(u) > 8192) return FALSE;
    for (const char *c = u; *c; c++)
        if ((guchar)*c < 0x20) return FALSE;
    static const char *const remote[] = {
        "http://", "https://", "ftp://", "rtsp://", "rtmp://", NULL,
    };
    for (int i = 0; remote[i]; i++)
        if (g_str_has_prefix(u, remote[i])) return TRUE;
    if (!allow_local) return FALSE;
    return g_str_has_prefix(u, "file://") || u[0] == '/';
}

gboolean
ns_media_is_video_page(const char *url)
{
    if (!url) return FALSE;
    const char *p = NULL;
    if (g_str_has_prefix(url, "https://")) p = url + 8;
    else if (g_str_has_prefix(url, "http://")) p = url + 7;
    else return FALSE;

    gsize hlen = strcspn(p, "/?#");
    char *raw = g_strndup(p, hlen);
    char *host = g_ascii_strdown(raw, -1);
    g_free(raw);
    const char *h = host;
    if (g_str_has_prefix(h, "www.")) h += 4;
    else if (g_str_has_prefix(h, "m.")) h += 2;
    const char *path = p + hlen;

    gboolean match = FALSE;
    if (strcmp(h, "youtu.be") == 0) {
        match = path[0] == '/' && path[1] && path[1] != '?' && path[1] != '#';
    } else if (strcmp(h, "youtube.com") == 0 ||
               strcmp(h, "music.youtube.com") == 0) {
        if (g_str_has_prefix(path, "/watch") && strstr(url, "v="))
            match = TRUE;
        else if (g_str_has_prefix(path, "/shorts/"))
            match = TRUE;
        else if (g_str_has_prefix(path, "/embed/"))
            match = TRUE;
        else if (g_str_has_prefix(path, "/live/"))
            match = TRUE;
    }
    g_free(host);
    return match;
}

static gboolean
ns_media_spawnv(char **argv)
{
    GError *err = NULL;
    gboolean ok = g_spawn_async(NULL, argv, NULL,
                                G_SPAWN_SEARCH_PATH |
                                G_SPAWN_STDOUT_TO_DEV_NULL |
                                G_SPAWN_STDERR_TO_DEV_NULL,
                                NULL, NULL, NULL, &err);
    if (!ok) {
        g_warning("ns_media: failed to launch %s: %s",
                  argv[0], err ? err->message : "unknown error");
        g_clear_error(&err);
    }
    return ok;
}

#ifndef G_OS_WIN32

#ifndef __APPLE__
static const char *const ns_media_mime_types[] = {
    "video/mp4", "video/webm", "video/x-matroska", "audio/mpeg", NULL,
};

static char *
ns_media_find_player(void)
{
    static const char *const players[] = {
        "mpv", "vlc", "celluloid", "totem", "mplayer", "ffplay", NULL,
    };
    for (int i = 0; players[i]; i++) {
        char *path = g_find_program_in_path(players[i]);
        if (path) return path;
    }
    return NULL;
}

static gboolean
ns_media_ytdlp_available(void)
{
    char *p = g_find_program_in_path("yt-dlp");
    if (!p) p = g_find_program_in_path("youtube-dl");
    if (p) { g_free(p); return TRUE; }
    return FALSE;
}

static GAppInfo *
ns_media_default_handler(const char *url)
{
    char *base = g_path_get_basename(url);
    gboolean uncertain = TRUE;
    char *ctype = base ? g_content_type_guess(base, NULL, 0, &uncertain) : NULL;
    GAppInfo *app = NULL;
    if (ctype && !uncertain)
        app = g_app_info_get_default_for_type(ctype, FALSE);
    g_free(ctype);
    g_free(base);
    for (int i = 0; !app && ns_media_mime_types[i]; i++)
        app = g_app_info_get_default_for_type(ns_media_mime_types[i], FALSE);
    return app;
}

static gboolean
ns_media_player_available(const char *url, gboolean stream)
{
    char *path = ns_media_find_player();
    if (path) { g_free(path); return TRUE; }
    if (stream) return FALSE;
    GAppInfo *app = ns_media_default_handler(url);
    if (app) { g_object_unref(app); return TRUE; }
    return FALSE;
}

static gboolean
ns_media_run_player(const char *url)
{
    char *player = ns_media_find_player();
    if (player) {
        char *argv[] = { player, (char *)url, NULL };
        gboolean ok = ns_media_spawnv(argv);
        g_free(player);
        if (ok) return TRUE;
    }
    GAppInfo *app = ns_media_default_handler(url);
    if (!app) return FALSE;
    char *uri = strstr(url, "://") ? g_strdup(url)
                                   : g_filename_to_uri(url, NULL, NULL);
    if (!uri) { g_object_unref(app); return FALSE; }
    GList *uris = g_list_append(NULL, uri);
    gboolean ok = g_app_info_launch_uris(app, uris, NULL, NULL);
    g_list_free(uris);
    g_free(uri);
    g_object_unref(app);
    return ok;
}
#endif

#if defined(__linux__)
static int ns_media_broker_fd = -1;

static gboolean
read_all(int fd, void *buf, size_t n)
{
    guint8 *p = buf;
    while (n) {
        ssize_t r = read(fd, p, n);
        if (r > 0) { p += r; n -= (size_t)r; continue; }
        if (r < 0 && errno == EINTR) continue;
        return FALSE;
    }
    return TRUE;
}

static G_GNUC_NORETURN void
ns_media_broker_loop(int fd)
{
    signal(SIGCHLD, SIG_IGN);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    for (;;) {
        guint32 len;
        if (!read_all(fd, &len, sizeof len) || len == 0 || len > 65536)
            _exit(0);
        char *buf = g_malloc(len + 1);
        if (!read_all(fd, buf, len)) { g_free(buf); _exit(0); }
        buf[len] = '\0';
        if (ns_media_url_is_safe(buf, TRUE))
            ns_media_run_player(buf);
        g_free(buf);
    }
}

void
ns_media_broker_start(void)
{
    if (ns_media_broker_fd >= 0) return;
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return;
    pid_t pid = fork();
    if (pid < 0) { close(sv[0]); close(sv[1]); return; }
    if (pid == 0) {
        close(sv[0]);
        ns_media_broker_loop(sv[1]);
    }
    close(sv[1]);
    ns_media_broker_fd = sv[0];
    fcntl(ns_media_broker_fd, F_SETFD, FD_CLOEXEC);
}

static gboolean
write_all(int fd, const void *buf, size_t n)
{
    const guint8 *p = buf;
    while (n) {
        ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
        if (w > 0) { p += w; n -= (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        return FALSE;
    }
    return TRUE;
}

static gboolean
ns_media_broker_send(const char *url)
{
    if (ns_media_broker_fd < 0) return FALSE;
    guint32 len = (guint32)strlen(url);
    if (len == 0) return FALSE;
    return write_all(ns_media_broker_fd, &len, sizeof len) &&
           write_all(ns_media_broker_fd, url, len);
}
#else
void
ns_media_broker_start(void)
{
}
#endif

#else
void
ns_media_broker_start(void)
{
}
#endif

#if defined(G_OS_WIN32)
static char *
ns_media_find_windows_player(void)
{
    static const char *const path_players[] = {
        "mpv.exe", "mpv", "vlc.exe", "vlc", NULL,
    };
    static const char *const fixed_players[] = {
        "C:\\Program Files\\VideoLAN\\VLC\\vlc.exe",
        "C:\\Program Files (x86)\\VideoLAN\\VLC\\vlc.exe",
        NULL,
    };
    for (int i = 0; path_players[i]; i++) {
        char *path = g_find_program_in_path(path_players[i]);
        if (path) return path;
    }
    for (int i = 0; fixed_players[i]; i++)
        if (g_file_test(fixed_players[i], G_FILE_TEST_IS_EXECUTABLE))
            return g_strdup(fixed_players[i]);
    return NULL;
}

static gboolean
ns_media_run_windows_player(const char *url)
{
    char *player = ns_media_find_windows_player();
    if (!player) return FALSE;
    char *argv[] = { player, (char *)url, NULL };
    gboolean ok = ns_media_spawnv(argv);
    g_free(player);
    return ok;
}
#endif

ns_media_status
ns_media_try_launch(const char *url, gboolean stream,
                    char **suggest_app, char **suggest_url)
{
    if (suggest_app) *suggest_app = NULL;
    if (suggest_url) *suggest_url = NULL;
    if (!url || !*url) return NS_MEDIA_FAILED;

#if defined(G_OS_WIN32) || defined(__APPLE__)
    (void)stream;
#endif

#if defined(G_OS_WIN32)
    if (!ns_media_url_is_safe(url, FALSE)) return NS_MEDIA_UNSAFE;
    if (ns_media_run_windows_player(url)) return NS_MEDIA_LAUNCHED;
    gunichar2 *wurl = g_utf8_to_utf16(url, -1, NULL, NULL, NULL);
    if (wurl) {
        HINSTANCE rc = ShellExecuteW(NULL, L"open", (LPCWSTR)wurl,
                                     NULL, NULL, SW_SHOWNORMAL);
        g_free(wurl);
        if ((INT_PTR)rc > 32) return NS_MEDIA_LAUNCHED;
    }
    if (suggest_app) *suggest_app = g_strdup("VLC");
    if (suggest_url) *suggest_url = g_strdup("https://videolan.org");
    return NS_MEDIA_NO_PLAYER;
#elif defined(__APPLE__)
    if (!ns_media_url_is_safe(url, FALSE)) return NS_MEDIA_UNSAFE;
    static const char *const apps[] = {
        "IINA", "VLC", "mpv", "QuickTime Player", NULL,
    };
    for (int i = 0; apps[i]; i++) {
        char *bundle = g_strdup_printf("/Applications/%s.app", apps[i]);
        gboolean present = g_file_test(bundle, G_FILE_TEST_EXISTS);
        g_free(bundle);
        if (!present) continue;
        char *argv[] = {
            (char *)"open", (char *)"-a", (char *)apps[i], (char *)url, NULL,
        };
        if (ns_media_spawnv(argv)) return NS_MEDIA_LAUNCHED;
    }
    char *argv[] = { (char *)"open", (char *)url, NULL };
    if (ns_media_spawnv(argv)) return NS_MEDIA_LAUNCHED;
    if (suggest_app) *suggest_app = g_strdup("IINA");
    if (suggest_url) *suggest_url = g_strdup("https://iina.io");
    return NS_MEDIA_NO_PLAYER;
#else
    if (!ns_media_url_is_safe(url, stream ? FALSE : TRUE))
        return NS_MEDIA_UNSAFE;
    if (!ns_media_player_available(url, stream)) {
        if (suggest_app) *suggest_app = g_strdup("mpv");
        if (suggest_url) *suggest_url = g_strdup("https://mpv.io");
        return NS_MEDIA_NO_PLAYER;
    }
    if (stream && !ns_media_ytdlp_available()) {
        if (suggest_app) *suggest_app = g_strdup("yt-dlp");
        if (suggest_url)
            *suggest_url = g_strdup("https://github.com/yt-dlp/yt-dlp");
        return NS_MEDIA_NEED_YTDLP;
    }
#if defined(__linux__)
    if (ns_media_broker_send(url)) return NS_MEDIA_LAUNCHED;
#endif
    return ns_media_run_player(url) ? NS_MEDIA_LAUNCHED : NS_MEDIA_FAILED;
#endif
}
