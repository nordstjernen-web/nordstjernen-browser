/* Nordstjernen — local phishing/malware blocklist and warning interstitial. */

#define _GNU_SOURCE
#include "safebrowsing.h"

#include <string.h>

#include <openssl/evp.h>

#ifdef G_OS_WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <stdlib.h>
#endif

static GHashTable *g_blocked;
static GHashTable *g_allow;
static gboolean    g_loaded;

static char *
host_sha256_hex(const char *host)
{
    if (!host || !*host) return NULL;
    char *low = g_ascii_strdown(host, -1);
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (EVP_Digest(low, strlen(low), digest, &len, EVP_sha256(), NULL) != 1) {
        g_free(low);
        return NULL;
    }
    g_free(low);
    GString *hex = g_string_sized_new(len * 2);
    for (unsigned int i = 0; i < len; i++)
        g_string_append_printf(hex, "%02x", digest[i]);
    return g_string_free(hex, FALSE);
}

static void
load_file(const char *path)
{
    char *contents = NULL;
    if (!path || !g_file_get_contents(path, &contents, NULL, NULL))
        return;
    char **lines = g_strsplit(contents, "\n", -1);
    for (int i = 0; lines && lines[i]; i++) {
        char *line = g_strstrip(lines[i]);
        if (!*line || *line == '#')
            continue;
        char *end = line;
        while (*end && !g_ascii_isspace(*end))
            end++;
        *end = '\0';
        if (strlen(line) >= 16)
            g_hash_table_add(g_blocked, g_ascii_strdown(line, -1));
    }
    g_strfreev(lines);
    g_free(contents);
}

static char *
self_exe_dir(void)
{
#ifdef G_OS_WIN32
    DWORD cap = MAX_PATH;
    wchar_t *buf = g_new(wchar_t, cap);
    DWORD n = GetModuleFileNameW(NULL, buf, cap);
    while (n >= cap && cap < 32768) {
        cap *= 2;
        buf = g_renew(wchar_t, buf, cap);
        n = GetModuleFileNameW(NULL, buf, cap);
    }
    char *utf8 = NULL;
    if (n > 0 && n < cap)
        utf8 = g_utf16_to_utf8((gunichar2 *)buf, -1, NULL, NULL, NULL);
    g_free(buf);
    if (!utf8) return NULL;
    char *dir = g_path_get_dirname(utf8);
    g_free(utf8);
    return dir;
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(NULL, &size);
    if (size == 0 || size > 32768) return NULL;
    char *raw = g_malloc(size);
    if (_NSGetExecutablePath(raw, &size) != 0) { g_free(raw); return NULL; }
    char *real = realpath(raw, NULL);
    char *dir = g_path_get_dirname(real ? real : raw);
    free(real);
    g_free(raw);
    return dir;
#else
    char *exe = g_file_read_link("/proc/self/exe", NULL);
    if (!exe) return NULL;
    char *dir = g_path_get_dirname(exe);
    g_free(exe);
    return dir;
#endif
}

static gboolean
load_bundled(void)
{
    static const char *const rel[] = {
        "../Resources/share/nordstjernen/safebrowsing.list",
        "../share/nordstjernen/safebrowsing.list",
        "share/nordstjernen/safebrowsing.list",
        "data/safebrowsing.list",
        "../data/safebrowsing.list",
        "../../data/safebrowsing.list",
        "../../../data/safebrowsing.list",
    };
    char *dir = self_exe_dir();
    if (dir) {
        for (gsize i = 0; i < G_N_ELEMENTS(rel); i++) {
            char *path = g_build_filename(dir, rel[i], NULL);
            gboolean exists = g_file_test(path, G_FILE_TEST_EXISTS);
            if (exists) {
                load_file(path);
                g_free(path);
                g_free(dir);
                return TRUE;
            }
            g_free(path);
        }
        g_free(dir);
    }
    if (g_file_test("data/safebrowsing.list", G_FILE_TEST_EXISTS)) {
        load_file("data/safebrowsing.list");
        return TRUE;
    }
    return FALSE;
}

static void
ensure_loaded(void)
{
    if (g_loaded)
        return;
    g_loaded = TRUE;
    g_blocked = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    g_allow = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    const char *override = g_getenv("NS_SAFEBROWSING_LIST");
    if (override && *override) {
        load_file(override);
        return;
    }
    char *user = g_build_filename(g_get_user_config_dir(), "nordstjernen",
                                  "safebrowsing.list", NULL);
    load_file(user);
    g_free(user);
    load_bundled();
}

void
ns_safebrowsing_allow_host(const char *host)
{
    if (!host || !*host)
        return;
    ensure_loaded();
    g_hash_table_add(g_allow, g_ascii_strdown(host, -1));
}

gboolean
ns_safebrowsing_blocked(const char *host)
{
    if (!host || !*host)
        return FALSE;
    ensure_loaded();
    if (g_hash_table_size(g_blocked) == 0)
        return FALSE;
    char *low = g_ascii_strdown(host, -1);
    for (gsize ln = strlen(low); ln > 0 && low[ln - 1] == '.'; ln--)
        low[ln - 1] = '\0';
    if (g_hash_table_contains(g_allow, low)) {
        g_free(low);
        return FALSE;
    }
    gboolean hit = FALSE;
    for (const char *h = low; h && strchr(h, '.'); h = strchr(h, '.') + 1) {
        char *hex = host_sha256_hex(h);
        if (hex && g_hash_table_contains(g_blocked, hex))
            hit = TRUE;
        g_free(hex);
        if (hit)
            break;
    }
    g_free(low);
    return hit;
}

char *
ns_safebrowsing_interstitial(const char *url, const char *host)
{
    char *esc_url = g_markup_escape_text(url && *url ? url : "", -1);
    char *esc_host = g_markup_escape_text(host && *host ? host : "", -1);
    char *continue_href = g_strconcat(NS_UNSAFE_CONTINUE_SCHEME,
                                      url ? url : "", NULL);
    char *esc_continue = g_markup_escape_text(continue_href, -1);
    g_free(continue_href);

    char *html = g_strdup_printf(
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<title>Security warning — Nordstjernen</title><style>"
        "body{font-family:system-ui,-apple-system,\"Segoe UI\",Helvetica,"
        "Arial,sans-serif;background:#7a1212;color:#1b1b22;margin:0;padding:0;"
        "min-height:100vh;display:flex;align-items:center;justify-content:center}"
        ".card{background:#fff;border:1px solid #e3e3e8;border-top:6px solid "
        "#c0271c;border-radius:10px;box-shadow:0 6px 30px rgba(0,0,0,0.25);"
        "padding:36px 40px;max-width:640px;margin:32px 16px;line-height:1.5}"
        ".icon{font-size:52px;line-height:1;margin-bottom:14px}"
        "h1{font-size:23px;margin:0 0 12px 0;color:#a01b12}"
        "p.summary{font-size:16px;color:#33333d;margin:0 0 16px 0}"
        ".host{font-family:ui-monospace,\"SF Mono\",Menlo,Consolas,monospace;"
        "background:#fbeeec;border:1px solid #f0cfcb;border-radius:6px;"
        "padding:8px 12px;font-size:14px;color:#a01b12;font-weight:600;"
        "overflow-wrap:anywhere;margin:0 0 18px 0}"
        ".actions{display:flex;gap:10px;flex-wrap:wrap}"
        ".btn{display:inline-block;padding:10px 18px;border-radius:6px;"
        "text-decoration:none;font-size:14px;font-weight:600;"
        "border:1px solid transparent;font-family:inherit}"
        ".btn.primary{background:#2f7d36;color:#fff;border-color:#2f7d36}"
        ".btn.primary:hover{background:#286b2e}"
        ".btn.danger{background:#fff;color:#a01b12;border-color:#e0b6b2}"
        ".btn.danger:hover{background:#fbeeec}"
        ".tips{margin-top:24px;padding-top:18px;border-top:1px solid #ececf0;"
        "color:#555;font-size:13px}"
        "</style></head><body><div class=\"card\">"
        "<div class=\"icon\">\xe2\x9a\xa0\xef\xb8\x8f</div>"
        "<h1>Deceptive site ahead</h1>"
        "<p class=\"summary\">Nordstjernen blocked this page because the site "
        "below is on your local list of known phishing or malware hosts. "
        "Attackers there may try to trick you into revealing passwords or "
        "payment details, or to install harmful software.</p>"
        "<p class=\"host\">%s</p>"
        "<div class=\"actions\">"
        "<a class=\"btn primary\" href=\"about:start\">Back to safety</a>"
        "<a class=\"btn danger\" href=\"%s\">Continue anyway "
        "(not recommended)</a>"
        "</div>"
        "<div class=\"tips\">This check runs entirely on your device against a "
        "local list — nothing about the page you visited was sent anywhere. "
        "The blocked address was <span style=\"font-family:ui-monospace,"
        "monospace;overflow-wrap:anywhere\">%s</span>.</div>"
        "</div></body></html>",
        esc_host, esc_continue, esc_url);

    g_free(esc_url);
    g_free(esc_host);
    g_free(esc_continue);
    return html;
}
