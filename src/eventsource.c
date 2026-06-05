/* Nordstjernen — minimal EventSource / Server-Sent Events client (libcurl). */

#include "eventsource.h"
#include "net.h"

#include <curl/curl.h>
#include <string.h>

struct nd_es {
    char     *url;
    char     *origin;
    char     *last_event_id;
    gint64    reconnect_ms;
    GThread  *thread;
    GMutex    lock;
    volatile gint exit_requested;
    volatile gint detached;
    int       refcount;
    nd_es_callbacks cbs;
    gpointer  user_data;
};

static nd_es *
nd_es_ref(nd_es *es)
{
    g_atomic_int_inc(&es->refcount);
    return es;
}

static void
nd_es_destroy(nd_es *es)
{
    g_free(es->url);
    g_free(es->origin);
    g_free(es->last_event_id);
    g_mutex_clear(&es->lock);
    g_free(es);
}

static void
nd_es_unref(nd_es *es)
{
    if (g_atomic_int_dec_and_test(&es->refcount))
        nd_es_destroy(es);
}

typedef struct {
    nd_es   *es;
    void   (*invoke)(nd_es *, gpointer);
    gpointer payload;
    void   (*payload_free)(gpointer);
} nd_es_dispatch;

static gboolean
nd_es_dispatch_run(gpointer data)
{
    nd_es_dispatch *d = data;
    nd_es *es = d->es;
    if (!g_atomic_int_get(&es->detached) && es->cbs.busy &&
        es->cbs.busy(es->user_data)) {
        g_timeout_add(4, nd_es_dispatch_run, d);
        return G_SOURCE_REMOVE;
    }
    if (!g_atomic_int_get(&es->detached) && d->invoke)
        d->invoke(es, d->payload);
    if (d->payload && d->payload_free) d->payload_free(d->payload);
    nd_es_unref(es);
    g_free(d);
    return G_SOURCE_REMOVE;
}

static void
nd_es_post(nd_es *es, void (*invoke)(nd_es *, gpointer), gpointer payload,
           void (*payload_free)(gpointer))
{
    if (g_atomic_int_get(&es->detached)) {
        if (payload && payload_free) payload_free(payload);
        return;
    }
    nd_es_dispatch *d = g_new0(nd_es_dispatch, 1);
    d->es = nd_es_ref(es);
    d->invoke = invoke;
    d->payload = payload;
    d->payload_free = payload_free;
    g_idle_add(nd_es_dispatch_run, d);
}

typedef struct {
    char *event;
    char *data;
    char *last_id;
} nd_es_msg;

static void
nd_es_msg_free(gpointer p)
{
    nd_es_msg *m = p;
    g_free(m->event);
    g_free(m->data);
    g_free(m->last_id);
    g_free(m);
}

static void
nd_es_invoke_open(nd_es *es, gpointer payload)
{
    (void)payload;
    if (es->cbs.on_open) es->cbs.on_open(es->user_data);
}

static void
nd_es_invoke_message(nd_es *es, gpointer payload)
{
    nd_es_msg *m = payload;
    if (es->cbs.on_message)
        es->cbs.on_message(m->event, m->data, m->last_id, es->user_data);
}

static void
nd_es_invoke_error(nd_es *es, gpointer payload)
{
    if (es->cbs.on_error) es->cbs.on_error(GPOINTER_TO_INT(payload), es->user_data);
}

static void
nd_es_emit_error(nd_es *es, gboolean fatal)
{
    nd_es_post(es, nd_es_invoke_error, GINT_TO_POINTER(fatal ? 1 : 0), NULL);
}

typedef struct {
    nd_es      *es;
    GByteArray *line;
    gboolean    last_was_cr;
    GString    *data;
    char       *event_type;
    long        status;
    gboolean    opened;
    gboolean    fatal;
    gboolean    is_event_stream;
} nd_es_parse;

static void
nd_es_dispatch_event(nd_es_parse *p)
{
    if (!p->data->len && !p->event_type) return;
    if (!p->data->len) {
        g_free(p->event_type);
        p->event_type = NULL;
        return;
    }
    gsize n = p->data->len;
    if (n && p->data->str[n - 1] == '\n')
        g_string_truncate(p->data, n - 1);
    nd_es_msg *m = g_new0(nd_es_msg, 1);
    m->event = p->event_type ? p->event_type : g_strdup("message");
    m->data = g_strdup(p->data->str);
    g_mutex_lock(&p->es->lock);
    m->last_id = g_strdup(p->es->last_event_id ? p->es->last_event_id : "");
    g_mutex_unlock(&p->es->lock);
    p->event_type = NULL;
    g_string_truncate(p->data, 0);
    nd_es_post(p->es, nd_es_invoke_message, m, nd_es_msg_free);
}

static void
nd_es_process_line(nd_es_parse *p, const char *line)
{
    if (!*line) { nd_es_dispatch_event(p); return; }
    if (line[0] == ':') return;
    const char *colon = strchr(line, ':');
    char *field, *value;
    if (colon) {
        field = g_strndup(line, (gsize)(colon - line));
        const char *v = colon + 1;
        if (*v == ' ') v++;
        value = g_strdup(v);
    } else {
        field = g_strdup(line);
        value = g_strdup("");
    }
    if (!strcmp(field, "data")) {
        g_string_append(p->data, value);
        g_string_append_c(p->data, '\n');
    } else if (!strcmp(field, "event")) {
        g_free(p->event_type);
        p->event_type = *value ? g_strdup(value) : NULL;
    } else if (!strcmp(field, "id")) {
        g_mutex_lock(&p->es->lock);
        g_free(p->es->last_event_id);
        p->es->last_event_id = g_strdup(value);
        g_mutex_unlock(&p->es->lock);
    } else if (!strcmp(field, "retry")) {
        gboolean digits = *value != '\0';
        for (const char *c = value; *c; c++)
            if (*c < '0' || *c > '9') { digits = FALSE; break; }
        if (digits) {
            gint64 ms = g_ascii_strtoll(value, NULL, 10);
            if (ms > 0) p->es->reconnect_ms = ms;
        }
    }
    g_free(field);
    g_free(value);
}

static void
nd_es_feed(nd_es_parse *p, const char *buf, gsize len)
{
    for (gsize i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\n' && p->last_was_cr) { p->last_was_cr = FALSE; continue; }
        p->last_was_cr = FALSE;
        if (c == '\r' || c == '\n') {
            if (c == '\r') p->last_was_cr = TRUE;
            g_byte_array_append(p->line, (const guint8 *)"", 1);
            p->line->data[p->line->len - 1] = '\0';
            nd_es_process_line(p, (const char *)p->line->data);
            g_byte_array_set_size(p->line, 0);
        } else {
            g_byte_array_append(p->line, (const guint8 *)&c, 1);
        }
    }
}

static size_t
nd_es_header_cb(char *buffer, size_t size, size_t nitems, void *userdata)
{
    nd_es_parse *p = userdata;
    size_t total = size * nitems;
    if (total >= 5 && !g_ascii_strncasecmp(buffer, "HTTP/", 5)) {
        long code = 0;
        const char *sp = memchr(buffer, ' ', total);
        if (sp) code = strtol(sp + 1, NULL, 10);
        p->status = code;
        p->is_event_stream = FALSE;
    } else if (total >= 13 &&
               !g_ascii_strncasecmp(buffer, "Content-Type:", 13)) {
        if (g_strstr_len(buffer, total, "text/event-stream"))
            p->is_event_stream = TRUE;
    } else if (total <= 2) {
        if (!p->opened) {
            if (p->status == 200 && p->is_event_stream) {
                p->opened = TRUE;
                nd_es_post(p->es, nd_es_invoke_open, NULL, NULL);
            } else if (p->status >= 200) {
                p->fatal = TRUE;
            }
        }
    }
    return g_atomic_int_get(&p->es->exit_requested) ? 0 : total;
}

static size_t
nd_es_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    nd_es_parse *p = userdata;
    if (g_atomic_int_get(&p->es->exit_requested) || p->fatal) return 0;
    nd_es_feed(p, ptr, size * nmemb);
    return size * nmemb;
}

static int
nd_es_progress(void *clientp, curl_off_t dt, curl_off_t dn, curl_off_t ut,
               curl_off_t un)
{
    (void)dt; (void)dn; (void)ut; (void)un;
    const nd_es *es = clientp;
    return g_atomic_int_get(&es->exit_requested) ? 1 : 0;
}

static gboolean
nd_es_connect_once(nd_es *es, gboolean *opened_out)
{
    CURL *curl = curl_easy_init();
    if (!curl) return FALSE;

    nd_es_parse p = {0};
    p.es = es;
    p.line = g_byte_array_new();
    p.data = g_string_new(NULL);
    p.status = 0;

    curl_easy_setopt(curl, CURLOPT_URL, es->url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, ND_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nd_es_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &p);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, nd_es_header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &p);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, nd_es_progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, es);

    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Accept: text/event-stream");
    hdrs = curl_slist_append(hdrs, "Cache-Control: no-cache");
    if (es->origin && *es->origin) {
        char *h = g_strconcat("Origin: ", es->origin, NULL);
        hdrs = curl_slist_append(hdrs, h);
        g_free(h);
    }
    g_mutex_lock(&es->lock);
    if (es->last_event_id && *es->last_event_id) {
        char *h = g_strconcat("Last-Event-ID: ", es->last_event_id, NULL);
        hdrs = curl_slist_append(hdrs, h);
        g_free(h);
    }
    g_mutex_unlock(&es->lock);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);

    curl_easy_perform(curl);

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    *opened_out = p.opened;
    gboolean fatal = p.fatal;
    g_byte_array_free(p.line, TRUE);
    g_string_free(p.data, TRUE);
    g_free(p.event_type);
    return !fatal;
}

static gpointer
nd_es_worker(gpointer data)
{
    nd_es *es = data;
    while (!g_atomic_int_get(&es->exit_requested)) {
        gboolean opened = FALSE;
        gboolean retriable = nd_es_connect_once(es, &opened);
        if (g_atomic_int_get(&es->exit_requested)) break;
        if (!retriable) {
            nd_es_emit_error(es, TRUE);
            break;
        }
        nd_es_emit_error(es, FALSE);
        gint64 wait_ms = es->reconnect_ms;
        if (wait_ms <= 0) wait_ms = 3000;
        gint64 waited = 0;
        while (waited < wait_ms && !g_atomic_int_get(&es->exit_requested)) {
            g_usleep(50 * 1000);
            waited += 50;
        }
    }
    nd_es_unref(es);
    return NULL;
}

nd_es *
nd_es_new(const char *url, const char *origin, const char *last_event_id,
          const nd_es_callbacks *cbs, gpointer user_data)
{
    nd_es *es = g_new0(nd_es, 1);
    es->url = g_strdup(url);
    es->origin = g_strdup(origin);
    es->last_event_id = g_strdup(last_event_id ? last_event_id : "");
    es->reconnect_ms = 3000;
    es->refcount = 1;
    es->cbs = *cbs;
    es->user_data = user_data;
    g_mutex_init(&es->lock);
    nd_es_ref(es);
    es->thread = g_thread_new("nd-eventsource", nd_es_worker, es);
    return es;
}

void
nd_es_close(nd_es *es)
{
    if (!es) return;
    g_atomic_int_set(&es->exit_requested, 1);
}

void
nd_es_free(nd_es *es)
{
    if (!es) return;
    g_atomic_int_set(&es->detached, 1);
    g_atomic_int_set(&es->exit_requested, 1);
    if (es->thread) {
        g_thread_join(es->thread);
        es->thread = NULL;
    }
    nd_es_unref(es);
}
