/* Nordstjernen — curl-free network helpers shared by the engine and the
 * thin browser shells: Accept-Language, search-URL building, proxy masking. */

#include "net.h"
#include "config.h"

#include <string.h>

static char *
build_accept_language_from_locales(void)
{
    const char *const *langs = g_get_language_names();
    if (!langs || !langs[0]) return NULL;
    GString *out = g_string_new(NULL);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal,
                                             g_free, NULL);
    int n = 0;
    for (int i = 0; langs[i] && n < 6; i++) {
        char *tag = g_strdup(langs[i]);
        char *dot = strchr(tag, '.');
        if (dot) *dot = '\0';
        char *at = strchr(tag, '@');
        if (at) *at = '\0';
        for (char *p = tag; *p; p++) if (*p == '_') *p = '-';
        if (!*tag ||
            g_ascii_strcasecmp(tag, "C") == 0 ||
            g_ascii_strcasecmp(tag, "POSIX") == 0) {
            g_free(tag); continue;
        }
        char *lower = g_ascii_strdown(tag, -1);
        if (g_hash_table_contains(seen, lower)) {
            g_free(tag); g_free(lower); continue;
        }
        g_hash_table_insert(seen, lower, NULL);
        if (n == 0) {
            g_string_append(out, tag);
        } else {
            double q = 1.0 - (double)n * 0.1;
            if (q < 0.1) q = 0.1;
            g_string_append_printf(out, ",%s;q=%.1f", tag, q);
        }
        n++;
        g_free(tag);
    }
    g_hash_table_destroy(seen);
    if (n == 0) {
        g_string_free(out, TRUE);
        return NULL;
    }
    return g_string_free(out, FALSE);
}

const char *
ns_net_default_accept_language(void)
{
    static char *cached;
    static gboolean tried;
    if (!tried) {
        tried = TRUE;
        cached = build_accept_language_from_locales();
        if (!cached) cached = g_strdup("en-US,en;q=0.9");
    }
    return cached;
}

gboolean
ns_address_is_search(const char *s)
{
    if (!s || !*s) return FALSE;
    if (g_str_has_prefix(s, "about:") || g_str_has_prefix(s, "file:") ||
        g_str_has_prefix(s, "data:") || strstr(s, "://"))
        return FALSE;
    for (const char *p = s; *p; p++)
        if (*p == ' ' || *p == '\t')
            return TRUE;
    if (g_str_has_prefix(s, "localhost") &&
        (s[9] == '\0' || s[9] == ':' || s[9] == '/'))
        return FALSE;
    if (strchr(s, '.') || strchr(s, ':'))
        return FALSE;
    return TRUE;
}

char *
ns_search_url_for(const char *query)
{
    const ns_config *cfg = ns_config_get();
    const char *engine = (cfg && cfg->search_engine && *cfg->search_engine)
        ? cfg->search_engine : "https://lite.duckduckgo.com/lite/?q=%s";
    char *enc = g_uri_escape_string(query ? query : "", NULL, TRUE);
    const char *pct = strstr(engine, "%s");
    char *out;
    if (pct) {
        char *prefix = g_strndup(engine, (gsize)(pct - engine));
        out = g_strconcat(prefix, enc, pct + 2, NULL);
        g_free(prefix);
    } else {
        out = g_strconcat(engine, enc, NULL);
    }
    g_free(enc);
    return out;
}

char *
ns_net_proxy_mask(const char *proxy_url)
{
    if (!proxy_url || !*proxy_url) return g_strdup("");
    const char *scheme_sep = strstr(proxy_url, "://");
    const char *cursor = scheme_sep ? scheme_sep + 3 : proxy_url;
    const char *at = strchr(cursor, '@');
    if (!at) return g_strdup(proxy_url);
    const char *colon = memchr(cursor, ':', (gsize)(at - cursor));
    if (!colon) return g_strdup(proxy_url);
    GString *s = g_string_new(NULL);
    g_string_append_len(s, proxy_url, (gssize)(colon - proxy_url));
    g_string_append(s, ":***");
    g_string_append(s, at);
    return g_string_free(s, FALSE);
}
