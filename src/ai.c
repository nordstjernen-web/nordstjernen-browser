/* Nordstjernen — local on-CPU chat inference over llama.cpp.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "ai.h"
#include "config.h"

#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>

#ifndef NS_HAVE_AI

gboolean
ns_ai_available(void)
{
    return FALSE;
}

void
ns_ai_select_download(const char *model_id)
{
    (void)model_id;
}

char *
ns_ai_status_json(void)
{
    return g_strdup("{\"state\":\"disabled\",\"models\":[]}");
}

char *
ns_ai_chat(const char *user_msg)
{
    (void)user_msg;
    return g_strdup("This build of Nordstjernen was compiled without the "
                    "local AI feature.");
}

#else

#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>

#include "llama.h"
#include "ggml-backend.h"

#define NS_AI_N_CTX        2048
#define NS_AI_MAX_REPLY    320
#define NS_AI_SYSTEM_PROMPT \
    "You are the assistant built into the Nordstjernen web browser. " \
    "Answer concisely and helpfully. You can reach the live web with three " \
    "tools. If the user asks to see, show, or find a picture or image, reply " \
    "with ONLY one line: IMAGE: <search terms>. If answering needs current " \
    "facts, news, prices, or any web lookup, reply with ONLY one line: " \
    "SEARCH: <search terms>. If the user asks to open, go to, visit, or " \
    "navigate to a website or URL, reply with ONLY one line: GO: <url>. " \
    "Otherwise answer directly from your own knowledge. Never describe the " \
    "tools; either emit one tool line or give the answer."
#define NS_AI_ANSWER_PROMPT \
    "You are the assistant built into the Nordstjernen web browser. Answer " \
    "the user's request using the web search results provided. Be concise. " \
    "Cite sources inline as markdown links like [title](https://url). Do not " \
    "mention tools and do not emit IMAGE: or SEARCH: lines."

typedef struct {
    const char *id;
    const char *label;
    const char *file;
    const char *url;
    int         mb;
} ns_ai_model;

static const ns_ai_model k_models[] = {
    {
        "balanced", "Qwen2.5 1.5B", "qwen2.5-1.5b-instruct-q4_k_m.gguf",
        "https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/"
        "qwen2.5-1.5b-instruct-q4_k_m.gguf?download=true", 1010,
    },
    {
        "gemma", "Gemma 2 2B (Google)", "gemma-2-2b-it-Q4_K_M.gguf",
        "https://huggingface.co/bartowski/gemma-2-2b-it-GGUF/resolve/main/"
        "gemma-2-2b-it-Q4_K_M.gguf?download=true", 1710,
    },
    {
        "quality", "Qwen2.5 3B", "qwen2.5-3b-instruct-q4_k_m.gguf",
        "https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/main/"
        "qwen2.5-3b-instruct-q4_k_m.gguf?download=true", 1930,
    },
    {
        "large", "Llama 3.1 8B", "Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf",
        "https://huggingface.co/bartowski/Meta-Llama-3.1-8B-Instruct-GGUF/"
        "resolve/main/Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf?download=true", 4920,
    },
};

static struct llama_model       *g_model;
static struct llama_context     *g_ctx;
static const struct llama_vocab *g_vocab;
static char                     *g_loaded_path;
static int                       g_gpu_layers_used;
static char                     *g_gpu_device;
static GMutex                    g_model_lock;

static char    *g_active_id;
static gboolean g_downloading;
static char    *g_dl_id;
static char    *g_dl_error;
static gint64   g_dl_now;
static gint64   g_dl_total;
static GMutex   g_dl_lock;

static const ns_ai_model *
ns_ai_model_by_id(const char *id)
{
    if (!id) return NULL;
    for (gsize i = 0; i < G_N_ELEMENTS(k_models); i++)
        if (g_str_equal(k_models[i].id, id))
            return &k_models[i];
    return NULL;
}

static char *
ns_ai_models_dir(void)
{
    return g_build_filename(g_get_user_data_dir(), NS_APP_DIR_NAME, "models",
                            NULL);
}

static char *
ns_ai_model_path(const ns_ai_model *m)
{
    char *dir = ns_ai_models_dir();
    char *path = g_build_filename(dir, m->file, NULL);
    g_free(dir);
    return path;
}

static gboolean
ns_ai_model_installed(const ns_ai_model *m)
{
    char *path = ns_ai_model_path(m);
    gboolean ok = g_file_test(path, G_FILE_TEST_IS_REGULAR);
    g_free(path);
    return ok;
}

static const ns_ai_model *
ns_ai_first_installed(void)
{
    for (gsize i = 0; i < G_N_ELEMENTS(k_models); i++)
        if (ns_ai_model_installed(&k_models[i]))
            return &k_models[i];
    return NULL;
}

static const ns_ai_model *
ns_ai_active_model_locked(void)
{
    const ns_ai_model *m = ns_ai_model_by_id(g_active_id);
    if (m && ns_ai_model_installed(m))
        return m;
    return ns_ai_first_installed();
}

static char *
ns_ai_active_path(void)
{
    const char *env = g_getenv("NORDSTJERNEN_AI_MODEL");
    if (env && *env && g_file_test(env, G_FILE_TEST_IS_REGULAR))
        return g_strdup(env);

    g_mutex_lock(&g_dl_lock);
    const ns_ai_model *m = ns_ai_active_model_locked();
    char *path = m ? ns_ai_model_path(m) : NULL;
    g_mutex_unlock(&g_dl_lock);
    return path;
}

gboolean
ns_ai_available(void)
{
    char *path = ns_ai_active_path();
    gboolean ok = path != NULL;
    g_free(path);
    return ok;
}

static int
ns_ai_xfer_cb(void *ud, curl_off_t dltotal, curl_off_t dlnow,
              curl_off_t ultotal, curl_off_t ulnow)
{
    (void)ud; (void)ultotal; (void)ulnow;
    g_mutex_lock(&g_dl_lock);
    g_dl_now = (gint64)dlnow;
    g_dl_total = (gint64)dltotal;
    g_mutex_unlock(&g_dl_lock);
    return 0;
}

typedef struct {
    char *url;
    char *path;
} ns_ai_dl_job;

static gpointer
ns_ai_download_thread(gpointer data)
{
    ns_ai_dl_job *job = data;
    char *dir = ns_ai_models_dir();
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    char *part = g_strconcat(job->path, ".part", NULL);

    char *err = NULL;
    FILE *f = g_fopen(part, "wb");
    if (!f) {
        err = g_strdup("could not open destination file");
    } else {
        CURL *c = curl_easy_init();
        if (!c) {
            err = g_strdup("could not initialise libcurl");
        } else {
            curl_easy_setopt(c, CURLOPT_URL, job->url);
            curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, fwrite);
            curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
            curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
            curl_easy_setopt(c, CURLOPT_USERAGENT, "Nordstjernen-AI/1.0");
            curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, ns_ai_xfer_cb);
            curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
            CURLcode rc = curl_easy_perform(c);
            if (rc != CURLE_OK)
                err = g_strdup(curl_easy_strerror(rc));
            curl_easy_cleanup(c);
        }
        fclose(f);
    }

    if (!err) {
        if (g_rename(part, job->path) != 0)
            err = g_strdup("could not move downloaded model into place");
    } else {
        g_remove(part);
    }

    g_mutex_lock(&g_dl_lock);
    g_free(g_dl_error);
    g_dl_error = err;
    g_downloading = FALSE;
    g_free(g_dl_id);
    g_dl_id = NULL;
    g_mutex_unlock(&g_dl_lock);

    g_free(part);
    g_free(job->url);
    g_free(job->path);
    g_free(job);
    return NULL;
}

void
ns_ai_select_download(const char *model_id)
{
    const ns_ai_model *m = ns_ai_model_by_id(model_id);
    if (!m) return;

    ns_ai_dl_job *job = NULL;
    g_mutex_lock(&g_dl_lock);
    g_free(g_active_id);
    g_active_id = g_strdup(m->id);

    if (!ns_ai_model_installed(m) && !g_downloading) {
        const char *env = g_getenv("NORDSTJERNEN_AI_MODEL_URL");
        job = g_new0(ns_ai_dl_job, 1);
        job->url = g_strdup(env && *env ? env : m->url);
        job->path = ns_ai_model_path(m);
        g_downloading = TRUE;
        g_free(g_dl_id);
        g_dl_id = g_strdup(m->id);
        g_free(g_dl_error);
        g_dl_error = NULL;
        g_dl_now = 0;
        g_dl_total = 0;
    }
    g_mutex_unlock(&g_dl_lock);

    if (job) {
        GThread *t = g_thread_new("ns-ai-download", ns_ai_download_thread, job);
        if (t) g_thread_unref(t);
    }
}

static char *
ns_ai_json_escape(const char *s)
{
    GString *o = g_string_new(NULL);
    for (; s && *s; s++) {
        if (*s == '"' || *s == '\\') g_string_append_c(o, '\\');
        if ((guchar)*s < 0x20) { g_string_append_printf(o, "\\u%04x", *s); continue; }
        g_string_append_c(o, *s);
    }
    return g_string_free(o, FALSE);
}

typedef struct {
    char  *data;
    size_t len;
} ns_ai_http_buf;

static size_t
ns_ai_http_write(char *ptr, size_t size, size_t nmemb, void *ud)
{
    ns_ai_http_buf *b = ud;
    size_t add = size * nmemb;
    if (b->len + add > 8u * 1024u * 1024u)
        return 0;
    b->data = g_realloc(b->data, b->len + add + 1);
    memcpy(b->data + b->len, ptr, add);
    b->len += add;
    b->data[b->len] = '\0';
    return add;
}

static char *
ns_ai_http_get(const char *url)
{
    CURL *c = curl_easy_init();
    if (!c) return NULL;
    ns_ai_http_buf b = { 0 };
    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Accept: text/html,application/json,*/*");
    hdrs = curl_slist_append(hdrs, "Accept-Language: en-US,en;q=0.9");
    hdrs = curl_slist_append(hdrs, "Referer: https://duckduckgo.com/");
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, ns_ai_http_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_USERAGENT,
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, "
        "like Gecko) Chrome/124.0 Safari/537.36");
    CURLcode rc = curl_easy_perform(c);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) {
        g_free(b.data);
        return NULL;
    }
    return b.data;
}

static char *
ns_ai_json_first_string(const char *json, const char *key)
{
    if (!json) return NULL;
    char *needle = g_strdup_printf("\"%s\":\"", key);
    const char *p = strstr(json, needle);
    size_t nl = strlen(needle);
    g_free(needle);
    if (!p) return NULL;
    p += nl;
    GString *o = g_string_new(NULL);
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
            case '/':  g_string_append_c(o, '/'); break;
            case 'n':  g_string_append_c(o, '\n'); break;
            case 't':  g_string_append_c(o, '\t'); break;
            case 'u':
                if (p[1] && p[2] && p[3] && p[4]) {
                    char hex[5] = { p[1], p[2], p[3], p[4], 0 };
                    gunichar cp = (gunichar)strtol(hex, NULL, 16);
                    if (cp >= 0xD800 && cp <= 0xDBFF &&
                        p[5] == '\\' && p[6] == 'u' &&
                        p[7] && p[8] && p[9] && p[10]) {
                        char hex2[5] = { p[7], p[8], p[9], p[10], 0 };
                        gunichar lo = (gunichar)strtol(hex2, NULL, 16);
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        p += 6;
                    }
                    if (cp) {
                        char utf8[8];
                        int ln = g_unichar_to_utf8(cp, utf8);
                        g_string_append_len(o, utf8, ln);
                    }
                    p += 4;
                }
                break;
            default:   g_string_append_c(o, *p);
            }
            p++;
        } else {
            g_string_append_c(o, *p++);
        }
    }
    return g_string_free(o, FALSE);
}

static char *
ns_ai_html_text(const char *start, const char *end)
{
    GString *o = g_string_new(NULL);
    for (const char *p = start; p < end; p++) {
        if (*p == '<') {
            while (p < end && *p != '>') p++;
            continue;
        }
        if (*p == '&') {
            if (g_str_has_prefix(p, "&amp;"))  { g_string_append_c(o, '&'); p += 4; continue; }
            if (g_str_has_prefix(p, "&lt;"))   { g_string_append_c(o, '<'); p += 3; continue; }
            if (g_str_has_prefix(p, "&gt;"))   { g_string_append_c(o, '>'); p += 3; continue; }
            if (g_str_has_prefix(p, "&quot;")) { g_string_append_c(o, '"'); p += 5; continue; }
            if (g_str_has_prefix(p, "&#x27;")) { g_string_append_c(o, '\''); p += 5; continue; }
            if (g_str_has_prefix(p, "&#39;"))  { g_string_append_c(o, '\''); p += 4; continue; }
            if (g_str_has_prefix(p, "&nbsp;")) { g_string_append_c(o, ' '); p += 5; continue; }
        }
        g_string_append_c(o, *p);
    }
    return g_string_free(o, FALSE);
}

static char *
ns_ai_ddg_decode_url(const char *href)
{
    const char *u = strstr(href, "uddg=");
    if (u) {
        u += 5;
        const char *end = strchr(u, '&');
        char *enc = end ? g_strndup(u, (gsize)(end - u)) : g_strdup(u);
        char *dec = g_uri_unescape_string(enc, NULL);
        g_free(enc);
        return dec ? dec : g_strdup(href);
    }
    if (g_str_has_prefix(href, "//"))
        return g_strconcat("https:", href, NULL);
    return g_strdup(href);
}

static char *
ns_ai_image_search(const char *query, char **page_out)
{
    if (page_out) *page_out = NULL;
    char *eq = g_uri_escape_string(query, NULL, TRUE);
    char *url = g_strdup_printf(
        "https://en.wikipedia.org/w/api.php?action=query&format=json"
        "&generator=search&gsrsearch=%s&gsrlimit=1&prop=pageimages"
        "&piprop=thumbnail&pithumbsize=640", eq);
    char *json = ns_ai_http_get(url);
    g_free(url);
    g_free(eq);
    if (!json) return NULL;

    char *img = ns_ai_json_first_string(json, "source");
    char *title = ns_ai_json_first_string(json, "title");
    g_free(json);

    if (img && page_out && title) {
        char *t = g_uri_escape_string(title, NULL, TRUE);
        *page_out = g_strdup_printf("https://en.wikipedia.org/wiki/%s", t);
        g_free(t);
    }
    g_free(title);
    return img;
}

static char *
ns_ai_fetch_data_uri(const char *url)
{
    CURL *c = curl_easy_init();
    if (!c) return NULL;
    ns_ai_http_buf b = { 0 };
    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Accept: image/*,*/*");
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, ns_ai_http_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_USERAGENT,
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, "
        "like Gecko) Chrome/124.0 Safari/537.36");
    CURLcode rc = curl_easy_perform(c);
    char *ctype = NULL;
    if (rc == CURLE_OK) {
        char *info = NULL;
        curl_easy_getinfo(c, CURLINFO_CONTENT_TYPE, &info);
        if (info) ctype = g_strdup(info);
    }
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK || b.len == 0) {
        g_free(b.data);
        g_free(ctype);
        return NULL;
    }
    if (!ctype || !g_str_has_prefix(ctype, "image/")) {
        g_free(ctype);
        ctype = g_strdup("image/jpeg");
    } else {
        char *semi = strchr(ctype, ';');
        if (semi) *semi = '\0';
        g_strstrip(ctype);
    }
    char *b64 = g_base64_encode((const guchar *)b.data, b.len);
    g_free(b.data);
    char *uri = g_strdup_printf("data:%s;base64,%s", ctype, b64);
    g_free(b64);
    g_free(ctype);
    return uri;
}

static char *
ns_ai_web_search(const char *query, char **sources_out, char **display_out)
{
    if (sources_out) *sources_out = NULL;
    if (display_out) *display_out = NULL;
    char *eq = g_uri_escape_string(query, NULL, TRUE);
    char *u = g_strdup_printf("https://html.duckduckgo.com/html/?q=%s", eq);
    g_free(eq);
    char *html = ns_ai_http_get(u);
    g_free(u);
    if (!html) return NULL;

    GString *ctx = g_string_new(NULL);
    GString *src = g_string_new(NULL);
    GString *disp = g_string_new(NULL);
    const char *p = html;
    int n = 0;
    while (n < 4) {
        p = strstr(p, "result__a");
        if (!p) break;
        const char *href = strstr(p, "href=\"");
        if (!href) break;
        href += 6;
        const char *hend = strchr(href, '"');
        if (!hend) break;
        char *raw = g_strndup(href, (gsize)(hend - href));
        char *url = ns_ai_ddg_decode_url(raw);
        g_free(raw);

        char *title = NULL;
        const char *gt = strchr(hend, '>');
        if (gt) {
            const char *lt = strstr(gt + 1, "</a>");
            if (lt) title = g_strstrip(ns_ai_html_text(gt + 1, lt));
        }

        char *snippet = NULL;
        const char *sn = strstr(hend, "result__snippet");
        const char *nextres = strstr(hend, "result__a");
        if (sn && (!nextres || sn < nextres)) {
            const char *sgt = strchr(sn, '>');
            if (sgt) {
                const char *slt = strstr(sgt + 1, "</a>");
                if (slt) snippet = g_strstrip(ns_ai_html_text(sgt + 1, slt));
            }
        }

        n++;
        const char *tt = (title && *title) ? title : (url ? url : "");
        g_string_append_printf(ctx, "[%d] %s\n%s\n%.280s\n\n",
            n, title ? title : "", url ? url : "", snippet ? snippet : "");
        g_string_append_printf(src, "[%d] [%s](%s)\n", n, tt, url ? url : "");
        g_string_append_printf(disp, "[%s](%s)\n%.240s\n\n",
            tt, url ? url : "", snippet ? snippet : "");
        g_free(title);
        g_free(snippet);
        g_free(url);
        p = hend;
    }
    g_free(html);

    if (n == 0) {
        g_string_free(ctx, TRUE);
        g_string_free(src, TRUE);
        g_string_free(disp, TRUE);
        return NULL;
    }
    if (sources_out) *sources_out = g_string_free(src, FALSE);
    else g_string_free(src, TRUE);
    if (display_out) *display_out = g_string_free(disp, FALSE);
    else g_string_free(disp, TRUE);
    return g_string_free(ctx, FALSE);
}

static gboolean
ns_ai_parse_tool(const char *reply, char **kind, char **arg)
{
    for (const char *p = reply; p && *p; ) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        const char *s = p;
        size_t l = len;
        while (l && (*s == ' ' || *s == '\t' || *s == '*' || *s == '`')) { s++; l--; }
        if (l >= 6 && g_ascii_strncasecmp(s, "IMAGE:", 6) == 0) {
            *kind = g_strdup("image");
            *arg = g_strstrip(g_strndup(s + 6, l - 6));
            return TRUE;
        }
        if (l >= 7 && g_ascii_strncasecmp(s, "SEARCH:", 7) == 0) {
            *kind = g_strdup("search");
            *arg = g_strstrip(g_strndup(s + 7, l - 7));
            return TRUE;
        }
        if ((l >= 3 && g_ascii_strncasecmp(s, "GO:", 3) == 0)) {
            *kind = g_strdup("go");
            *arg = g_strstrip(g_strndup(s + 3, l - 3));
            return TRUE;
        }
        if (l >= 9 && g_ascii_strncasecmp(s, "NAVIGATE:", 9) == 0) {
            *kind = g_strdup("go");
            *arg = g_strstrip(g_strndup(s + 9, l - 9));
            return TRUE;
        }
        if (!eol) break;
        p = eol + 1;
    }
    return FALSE;
}

static char *
ns_ai_normalize_url(const char *raw)
{
    if (!raw || !*raw) return NULL;
    char *s = g_strstrip(g_strdup(raw));
    if (!*s) { g_free(s); return NULL; }
    if (strstr(s, "://"))
        return s;
    char *full = g_strconcat("https://", s, NULL);
    g_free(s);
    return full;
}

static char *
ns_ai_try_image_query(const char *msg)
{
    static const char *const cues[] = {
        "image of ", "images of ", "picture of ", "pictures of ",
        "photo of ", "photos of ", "pic of ", "pics of ",
        "image for ", "picture for ", "photo for ",
    };
    char *low = g_ascii_strdown(msg, -1);
    char *subject = NULL;
    for (gsize i = 0; i < G_N_ELEMENTS(cues); i++) {
        char *hit = strstr(low, cues[i]);
        if (hit) {
            subject = g_strdup(msg + (hit - low) + strlen(cues[i]));
            break;
        }
    }
    g_free(low);
    if (subject) {
        subject = g_strstrip(subject);
        size_t n = strlen(subject);
        while (n && strchr(".!?,;\"'", subject[n - 1])) subject[--n] = '\0';
        if (!*subject) { g_free(subject); subject = NULL; }
    }
    return subject;
}

static char *
ns_ai_image_reply(const char *query)
{
    char *page = NULL;
    char *img = ns_ai_image_search(query, &page);
    char *reply;
    if (img) {
        char *alt = g_strdup(query);
        g_strdelimit(alt, "[]()", ' ');
        char *data_uri = ns_ai_fetch_data_uri(img);
        const char *src = data_uri ? data_uri : img;
        reply = g_strdup_printf(
            "Here's an image of %s:\n\n![%s](%s)\n\n[Image source](%s)",
            query, alt, src, page ? page : img);
        g_free(data_uri);
        g_free(alt);
    } else {
        reply = g_strdup_printf("I couldn't find an image for \"%s\".", query);
    }
    g_free(img);
    g_free(page);
    return reply;
}

static char *
ns_ai_strip_lead_article(char *s)
{
    static const char *const arts[] = { "the ", "a ", "an " };
    for (gsize i = 0; i < G_N_ELEMENTS(arts); i++) {
        if (g_ascii_strncasecmp(s, arts[i], strlen(arts[i])) == 0) {
            memmove(s, s + strlen(arts[i]), strlen(s + strlen(arts[i])) + 1);
            break;
        }
    }
    return s;
}

static char *
ns_ai_try_factual_query(const char *msg)
{
    static const char *const cues[] = {
        "who is ", "who was ", "who are ", "who were ",
        "what is ", "what was ", "what are ", "what were ",
        "tell me about ", "tell me more about ",
    };
    char *low = g_ascii_strdown(msg, -1);
    char *subject = NULL;
    for (gsize i = 0; i < G_N_ELEMENTS(cues); i++) {
        if (g_str_has_prefix(low, cues[i])) {
            subject = g_strdup(msg + strlen(cues[i]));
            break;
        }
    }
    g_free(low);
    if (subject) {
        subject = g_strstrip(subject);
        size_t n = strlen(subject);
        while (n && strchr(".!?,;\"'", subject[n - 1])) subject[--n] = '\0';
        subject = g_strstrip(ns_ai_strip_lead_article(subject));
        if (strlen(subject) < 2) { g_free(subject); subject = NULL; }
    }
    return subject;
}

static gboolean
ns_ai_text_contains_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !*needle) return FALSE;
    char *h = g_utf8_strdown(haystack, -1);
    char *n = g_utf8_strdown(needle, -1);
    gboolean found = h && n && strstr(h, n) != NULL;
    g_free(h);
    g_free(n);
    return found;
}

static gboolean
ns_ai_wiki_relevant(const char *query, const char *title, const char *extract)
{
    char **words = g_strsplit_set(query, " \t\n\r", -1);
    char *key = NULL;
    glong keylen = 0;
    for (char **w = words; *w; w++) {
        char *s = g_strstrip(*w);
        glong l = g_utf8_strlen(s, -1);
        if (l > keylen) { keylen = l; key = s; }
    }
    gboolean relevant = TRUE;
    if (key && keylen >= 5)
        relevant = ns_ai_text_contains_ci(title, key) ||
                   ns_ai_text_contains_ci(extract, key);
    g_strfreev(words);
    return relevant;
}

static char *
ns_ai_wiki_summary(const char *query)
{
    char *eq = g_uri_escape_string(query, NULL, TRUE);
    char *url = g_strdup_printf(
        "https://en.wikipedia.org/w/api.php?action=query&format=json"
        "&generator=search&gsrsearch=%s&gsrlimit=1&prop=extracts%%7Cinfo"
        "&inprop=url&exintro&explaintext&exchars=700", eq);
    char *json = ns_ai_http_get(url);
    g_free(url);
    g_free(eq);
    if (!json) return NULL;

    char *extract = ns_ai_json_first_string(json, "extract");
    char *title = ns_ai_json_first_string(json, "title");
    char *page = ns_ai_json_first_string(json, "fullurl");
    g_free(json);

    char *reply = NULL;
    if (extract && *g_strstrip(extract) &&
        ns_ai_wiki_relevant(query, title, extract)) {
        reply = g_strdup_printf("%s\n\n[Read more on Wikipedia](%s)",
            extract, page ? page : "https://en.wikipedia.org");
    }
    g_free(extract);
    g_free(title);
    g_free(page);
    return reply;
}

static char *
ns_ai_try_search_query(const char *msg)
{
    static const char *const cues[] = {
        "search the web for ", "search the web ", "search for ", "search ",
        "look up ", "google ", "web search for ", "web search ",
        "find information about ", "find info about ",
    };
    char *low = g_ascii_strdown(msg, -1);
    char *subject = NULL;
    for (gsize i = 0; i < G_N_ELEMENTS(cues); i++) {
        if (g_str_has_prefix(low, cues[i])) {
            subject = g_strdup(msg + strlen(cues[i]));
            break;
        }
    }
    g_free(low);
    if (subject) {
        subject = g_strstrip(subject);
        size_t n = strlen(subject);
        while (n && strchr(".!?,;\"'", subject[n - 1])) subject[--n] = '\0';
        if (strlen(subject) < 2) { g_free(subject); subject = NULL; }
    }
    return subject;
}

static char *
ns_ai_search_reply(const char *query)
{
    char *display = NULL;
    char *ctx = ns_ai_web_search(query, NULL, &display);
    char *reply;
    if (display && *display) {
        reply = g_strdup_printf("Here's what I found for \"%s\":\n\n%s",
                                query, display);
    } else {
        reply = g_strdup_printf(
            "I couldn't search the web for \"%s\" right now.", query);
    }
    g_free(ctx);
    g_free(display);
    return reply;
}

static char *
ns_ai_try_navigate(const char *msg)
{
    static const char *const verbs[] = {
        "go to ", "goto ", "open ", "navigate to ", "visit ",
        "take me to ", "browse to ", "bring up ",
    };
    char *trimmed = g_strstrip(g_strdup(msg));
    char *low = g_ascii_strdown(trimmed, -1);
    const char *rest = NULL;
    for (gsize i = 0; i < G_N_ELEMENTS(verbs); i++) {
        if (g_str_has_prefix(low, verbs[i])) {
            rest = trimmed + strlen(verbs[i]);
            break;
        }
    }

    char *result = NULL;
    if (rest) {
        char *r = g_strstrip(g_strdup(rest));
        size_t n = strlen(r);
        while (n && strchr(".!?,; ", r[n - 1])) r[--n] = '\0';
        if (*r && !strchr(r, ' ') && (strstr(r, "://") || strchr(r, '.'))) {
            char *url = ns_ai_normalize_url(r);
            if (url) {
                result = g_strdup_printf("@@NAVIGATE@@%s", url);
                g_free(url);
            }
        }
        g_free(r);
    }
    g_free(low);
    g_free(trimmed);
    return result;
}

char *
ns_ai_status_json(void)
{
    g_mutex_lock(&g_model_lock);
    int gpu_layers = g_gpu_layers_used;
    char *gpu_device = g_strdup(g_gpu_device);
    g_mutex_unlock(&g_model_lock);

    g_mutex_lock(&g_dl_lock);
    gboolean downloading = g_downloading;
    char *dl_id = g_strdup(g_dl_id);
    char *err = g_strdup(g_dl_error);
    gint64 now = g_dl_now, total = g_dl_total;
    const ns_ai_model *active = ns_ai_active_model_locked();
    char *active_id = g_strdup(active ? active->id : NULL);
    g_mutex_unlock(&g_dl_lock);

    GString *models = g_string_new("[");
    for (gsize i = 0; i < G_N_ELEMENTS(k_models); i++) {
        const ns_ai_model *m = &k_models[i];
        g_string_append_printf(models,
            "%s{\"id\":\"%s\",\"label\":\"%s\",\"mb\":%d,\"installed\":%s}",
            i ? "," : "", m->id, m->label, m->mb,
            ns_ai_model_installed(m) ? "true" : "false");
    }
    g_string_append_c(models, ']');

    const char *state = downloading ? "downloading"
                      : active_id   ? "ready"
                      : err         ? "error" : "idle";

    GString *out = g_string_new(NULL);
    g_string_append_printf(out, "{\"state\":\"%s\",\"models\":%s",
                           state, models->str);
    if (active_id)
        g_string_append_printf(out, ",\"active\":\"%s\"", active_id);
    if (gpu_device && gpu_layers != 0) {
        char *esc = ns_ai_json_escape(gpu_device);
        g_string_append_printf(out,
            ",\"gpu\":\"%s\",\"gpu_layers\":%d", esc, gpu_layers);
        g_free(esc);
    }
    if (downloading) {
        int pct = total > 0 ? (int)((now * 100) / total) : 0;
        g_string_append_printf(out,
            ",\"downloading\":\"%s\",\"percent\":%d,"
            "\"received\":%" G_GINT64_FORMAT ",\"total\":%" G_GINT64_FORMAT,
            dl_id ? dl_id : "", pct, now, total);
    }
    if (err && !downloading && !active_id) {
        char *esc = ns_ai_json_escape(err);
        g_string_append_printf(out, ",\"message\":\"%s\"", esc);
        g_free(esc);
    }
    g_string_append_c(out, '}');

    g_string_free(models, TRUE);
    g_free(dl_id);
    g_free(err);
    g_free(active_id);
    g_free(gpu_device);
    return g_string_free(out, FALSE);
}

static void
ns_ai_log_sink(enum ggml_log_level level, const char *text, void *ud)
{
    (void)ud;
    if (level >= GGML_LOG_LEVEL_ERROR && text)
        g_printerr("%s", text);
}

static void
ns_ai_unload_locked(void)
{
    if (g_ctx) { llama_free(g_ctx); g_ctx = NULL; }
    if (g_model) { llama_model_free(g_model); g_model = NULL; }
    g_vocab = NULL;
    g_free(g_loaded_path);
    g_loaded_path = NULL;
    g_gpu_layers_used = 0;
    g_free(g_gpu_device);
    g_gpu_device = NULL;
}

static gboolean
ns_ai_device_is_software(const char *name)
{
    if (!name) return FALSE;
    char *lower = g_ascii_strdown(name, -1);
    gboolean soft = strstr(lower, "llvmpipe") || strstr(lower, "lavapipe") ||
                    strstr(lower, "swiftshader") || strstr(lower, "software") ||
                    strstr(lower, "cpu");
    g_free(lower);
    return soft;
}

static ggml_backend_dev_t
ns_ai_pick_gpu(void)
{
    size_t n = ggml_backend_dev_count();
    for (size_t i = 0; i < n; i++) {
        ggml_backend_dev_t d = ggml_backend_dev_get(i);
        enum ggml_backend_dev_type t = ggml_backend_dev_type(d);
        if (t != GGML_BACKEND_DEVICE_TYPE_GPU &&
            t != GGML_BACKEND_DEVICE_TYPE_IGPU)
            continue;
        if (ns_ai_device_is_software(ggml_backend_dev_name(d)) ||
            ns_ai_device_is_software(ggml_backend_dev_description(d)))
            continue;
        return d;
    }
    return NULL;
}

static int
ns_ai_plan_gpu_offload(const char *path, int n_layers, char **device_out)
{
    *device_out = NULL;

    const char *env = g_getenv("NORDSTJERNEN_AI_GPU_LAYERS");
    gboolean forced = env && *env;
    int want = forced ? atoi(env) : 0;
    if (forced && want == 0)
        return 0;

    if (!llama_supports_gpu_offload())
        return 0;

    ggml_backend_dev_t dev = ns_ai_pick_gpu();
    if (!dev)
        return 0;

    if (!forced) {
        size_t freem = 0, totalm = 0;
        ggml_backend_dev_memory(dev, &freem, &totalm);
        GStatBuf st;
        if (freem == 0 || n_layers <= 0 || g_stat(path, &st) != 0)
            return 0;
        double per_layer = (double)st.st_size / (double)n_layers;
        double budget = (double)freem * 0.85;
        want = per_layer > 0 ? (int)(budget / per_layer) : 0;
        if (want < 1)
            return 0;
        if (want > n_layers)
            want = n_layers;
    }

    const char *desc = ggml_backend_dev_description(dev);
    *device_out = g_strdup(desc ? desc : ggml_backend_dev_name(dev));
    return want;
}

static struct llama_model *
ns_ai_load_model(const char *path, int n_gpu_layers)
{
    struct llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = n_gpu_layers;
    return llama_model_load_from_file(path, mparams);
}

static gboolean
ns_ai_ensure_loaded_locked(void)
{
    char *path = ns_ai_active_path();
    if (!path) return FALSE;

    if (g_loaded_path && g_str_equal(g_loaded_path, path)) {
        g_free(path);
        return TRUE;
    }
    ns_ai_unload_locked();

    llama_log_set(ns_ai_log_sink, NULL);
    llama_backend_init();

    g_model = ns_ai_load_model(path, 0);
    if (!g_model) { g_free(path); return FALSE; }

    char *device = NULL;
    int want = ns_ai_plan_gpu_offload(path, llama_model_n_layer(g_model),
                                      &device);
    if (want != 0) {
        llama_model_free(g_model);
        g_model = ns_ai_load_model(path, want);
        if (g_model) {
            g_gpu_layers_used = want;
            g_gpu_device = device;
        } else {
            g_free(device);
            g_model = ns_ai_load_model(path, 0);
        }
    }
    if (!g_model) { g_free(path); return FALSE; }

    struct llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx           = NS_AI_N_CTX;
    cparams.n_batch         = NS_AI_N_CTX;
    cparams.n_threads       = (int32_t)g_get_num_processors();
    cparams.n_threads_batch = (int32_t)g_get_num_processors();
    g_ctx = llama_init_from_model(g_model, cparams);
    if (!g_ctx && g_gpu_layers_used != 0) {
        llama_model_free(g_model);
        g_gpu_layers_used = 0;
        g_free(g_gpu_device);
        g_gpu_device = NULL;
        g_model = ns_ai_load_model(path, 0);
        if (g_model)
            g_ctx = llama_init_from_model(g_model, cparams);
    }
    if (!g_ctx) {
        if (g_model) { llama_model_free(g_model); g_model = NULL; }
        g_free(path);
        return FALSE;
    }

    g_vocab = llama_model_get_vocab(g_model);
    g_loaded_path = path;
    return TRUE;
}

static char *
ns_ai_build_prompt(const char *system_prompt, const char *user_msg)
{
    const char *tmpl = llama_model_chat_template(g_model, NULL);
    struct llama_chat_message msgs[2] = {
        { "system", system_prompt },
        { "user",   user_msg },
    };

    if (tmpl) {
        int need = llama_chat_apply_template(tmpl, msgs, 2, true, NULL, 0);
        if (need > 0) {
            char *buf = g_malloc((gsize)need + 1);
            int n = llama_chat_apply_template(tmpl, msgs, 2, true, buf, need + 1);
            if (n > 0) {
                buf[n] = '\0';
                return buf;
            }
            g_free(buf);
        }
    }

    return g_strdup_printf(
        "<|im_start|>system\n%s<|im_end|>\n"
        "<|im_start|>user\n%s<|im_end|>\n"
        "<|im_start|>assistant\n",
        system_prompt, user_msg);
}

static char *
ns_ai_run_locked(const char *system_prompt, const char *user_msg)
{
    char *prompt = ns_ai_build_prompt(system_prompt, user_msg);

    int n_max = (int)strlen(prompt) + 8;
    llama_token *toks = g_malloc(sizeof(llama_token) * (gsize)n_max);
    int n_prompt = llama_tokenize(g_vocab, prompt, (int)strlen(prompt),
                                  toks, n_max, true, true);
    g_free(prompt);
    if (n_prompt <= 0) {
        g_free(toks);
        return NULL;
    }

    llama_memory_clear(llama_get_memory(g_ctx), true);

    struct llama_sampler_chain_params sp = llama_sampler_chain_default_params();
    struct llama_sampler *smpl = llama_sampler_chain_init(sp);
    llama_sampler_chain_add(smpl, llama_sampler_init_top_k(40));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_p(0.95f, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.7f));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    GString *out = g_string_new(NULL);
    struct llama_batch batch = llama_batch_get_one(toks, n_prompt);
    int n_decoded = 0;
    gboolean failed = FALSE;

    while (n_decoded < NS_AI_MAX_REPLY) {
        if (llama_decode(g_ctx, batch) != 0) {
            failed = out->len == 0;
            break;
        }
        llama_token id = llama_sampler_sample(smpl, g_ctx, -1);
        if (llama_vocab_is_eog(g_vocab, id))
            break;

        char piece[256];
        int pn = llama_token_to_piece(g_vocab, id, piece, sizeof piece, 0, false);
        if (pn > 0)
            g_string_append_len(out, piece, pn);

        batch = llama_batch_get_one(&id, 1);
        n_decoded++;
    }

    llama_sampler_free(smpl);
    g_free(toks);

    if (failed) {
        g_string_free(out, TRUE);
        return NULL;
    }
    return g_string_free(out, FALSE);
}

static char *
ns_ai_run_tools_locked(const char *user_msg)
{
    char *first = ns_ai_run_locked(NS_AI_SYSTEM_PROMPT, user_msg);
    if (!first) return NULL;

    char *kind = NULL, *arg = NULL;
    if (!ns_ai_parse_tool(first, &kind, &arg))
        return first;
    g_free(first);

    const char *query = (arg && *arg) ? arg : user_msg;
    char *reply = NULL;

    if (g_str_equal(kind, "go")) {
        char *url = ns_ai_normalize_url(query);
        reply = url ? g_strdup_printf("@@NAVIGATE@@%s", url)
                    : g_strdup("I couldn't work out which page to open.");
        g_free(url);
    } else if (g_str_equal(kind, "image")) {
        reply = ns_ai_image_reply(query);
    } else {
        char *sources = NULL;
        char *ctx = ns_ai_web_search(query, &sources, NULL);
        if (ctx) {
            char *augmented = g_strdup_printf(
                "Web search results for \"%s\":\n\n%s"
                "Using only these results, answer this request: %s",
                query, ctx, user_msg);
            char *answer = ns_ai_run_locked(NS_AI_ANSWER_PROMPT, augmented);
            g_free(augmented);
            if (answer && *g_strstrip(answer))
                reply = sources
                      ? g_strdup_printf("%s\n\nSources:\n%s", answer, sources)
                      : g_strdup(answer);
            else
                reply = g_strdup_printf("Here's what I found:\n\n%s",
                                        sources ? sources : ctx);
            g_free(answer);
        } else {
            reply = g_strdup_printf(
                "I couldn't search the web for \"%s\" right now.", query);
        }
        g_free(ctx);
        g_free(sources);
    }

    g_free(kind);
    g_free(arg);
    return reply;
}

char *
ns_ai_chat(const char *user_msg)
{
    if (!user_msg || !*user_msg)
        return g_strdup("Please type a message.");

    char *nav = ns_ai_try_navigate(user_msg);
    if (nav)
        return nav;

    char *imgq = ns_ai_try_image_query(user_msg);
    if (imgq) {
        char *reply = ns_ai_image_reply(imgq);
        g_free(imgq);
        return reply;
    }

    char *searchq = ns_ai_try_search_query(user_msg);
    if (searchq) {
        char *reply = ns_ai_search_reply(searchq);
        g_free(searchq);
        return reply;
    }

    char *factq = ns_ai_try_factual_query(user_msg);
    if (factq) {
        char *reply = ns_ai_wiki_summary(factq);
        g_free(factq);
        if (reply)
            return reply;
    }

    g_mutex_lock(&g_model_lock);
    char *reply = NULL;
    if (ns_ai_ensure_loaded_locked())
        reply = ns_ai_run_tools_locked(user_msg);
    g_mutex_unlock(&g_model_lock);

    if (!reply)
        reply = g_strdup("The local AI model is not ready yet. Pick and "
                         "download a model from the start page first.");
    return reply;
}

#endif
