/* Nordstjernen — WebSocket client: libcurl-native when available, else a built-in RFC 6455 framer over a CONNECT_ONLY socket.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "ws.h"

#include <curl/curl.h>
#include <curl/websockets.h>

#include <string.h>

#include "net.h"
#include "security.h"

#define NS_WS_RECV_BUF       8192
#define NS_WS_MAX_MESSAGE    (8 * 1024 * 1024)
#define NS_WS_POLL_MSEC      10

typedef enum {
    NS_WS_OUT_TEXT,
    NS_WS_OUT_BINARY,
    NS_WS_OUT_CLOSE,
} ns_ws_out_kind;

typedef struct {
    ns_ws_out_kind kind;
    guint8        *data;
    gsize          len;
    int            close_code;
} ns_ws_out_msg;

struct ns_ws {
    volatile gint     refcount;

    char             *url;
    char             *origin;
    GPtrArray        *protocols;

    ns_ws_callbacks   cbs;
    gpointer          user_data;

    GMutex            lock;
    GCond             cond;
    GQueue            out_queue;
    volatile gint     state;
    volatile gint     exit_requested;
    volatile gint     detached;

    GThread          *thread;

    GByteArray       *recv_assembly;
    int               recv_assembly_kind;
    gboolean          recv_in_message;
};

static ns_ws *
ns_ws_ref(ns_ws *ws)
{
    if (ws) g_atomic_int_inc(&ws->refcount);
    return ws;
}

static void
ns_ws_destroy(ns_ws *ws)
{
    if (!ws) return;
    g_free(ws->url);
    g_free(ws->origin);
    if (ws->protocols) g_ptr_array_free(ws->protocols, TRUE);
    while (!g_queue_is_empty(&ws->out_queue)) {
        ns_ws_out_msg *m = g_queue_pop_head(&ws->out_queue);
        g_free(m->data);
        g_free(m);
    }
    if (ws->recv_assembly) g_byte_array_free(ws->recv_assembly, TRUE);
    g_mutex_clear(&ws->lock);
    g_cond_clear(&ws->cond);
    g_free(ws);
}

static void
ns_ws_unref(ns_ws *ws)
{
    if (!ws) return;
    if (g_atomic_int_dec_and_test(&ws->refcount))
        ns_ws_destroy(ws);
}

typedef struct {
    ns_ws  *ws;
    void  (*invoke)(struct ns_ws *ws, gpointer payload);
    gpointer payload;
    void   (*payload_free)(gpointer);
} ns_ws_dispatch;

static gboolean
ns_ws_dispatch_run(gpointer data)
{
    ns_ws_dispatch *d = data;
    ns_ws *ws = d->ws;
    if (!g_atomic_int_get(&ws->detached) && ws->cbs.busy &&
        ws->cbs.busy(ws->user_data)) {
        g_timeout_add(4, ns_ws_dispatch_run, d);
        return G_SOURCE_REMOVE;
    }
    if (!g_atomic_int_get(&ws->detached) && d->invoke)
        d->invoke(ws, d->payload);
    if (d->payload && d->payload_free) d->payload_free(d->payload);
    ns_ws_unref(ws);
    g_free(d);
    return G_SOURCE_REMOVE;
}

static void
ns_ws_post(ns_ws *ws,
           void (*invoke)(ns_ws *, gpointer),
           gpointer payload,
           void   (*payload_free)(gpointer))
{
    if (g_atomic_int_get(&ws->detached)) {
        if (payload && payload_free) payload_free(payload);
        return;
    }
    ns_ws_dispatch *d = g_new0(ns_ws_dispatch, 1);
    d->ws = ns_ws_ref(ws);
    d->invoke = invoke;
    d->payload = payload;
    d->payload_free = payload_free;
    g_idle_add(ns_ws_dispatch_run, d);
}

typedef struct {
    GByteArray *data;
    gboolean    is_text;
} ns_ws_msg_payload;

typedef struct {
    int   code;
    char *reason;
    gboolean clean;
} ns_ws_close_payload;

static void
ns_ws_msg_payload_free(gpointer p)
{
    ns_ws_msg_payload *m = p;
    if (m->data) g_byte_array_free(m->data, TRUE);
    g_free(m);
}

static void
ns_ws_close_payload_free(gpointer p)
{
    ns_ws_close_payload *c = p;
    g_free(c->reason);
    g_free(c);
}

static void
ns_ws_invoke_open(ns_ws *ws, gpointer payload)
{
    (void)payload;
    if (ws->cbs.on_open) ws->cbs.on_open(ws->user_data);
}

static void
ns_ws_invoke_msg(ns_ws *ws, gpointer payload)
{
    ns_ws_msg_payload *m = payload;
    if (m->is_text) {
        if (ws->cbs.on_text) {
            const char *text = m->data->len ? (const char *)m->data->data : "";
            ws->cbs.on_text(text, m->data->len, ws->user_data);
        }
    } else {
        if (ws->cbs.on_binary) {
            const guint8 *bytes = m->data->len
                ? m->data->data : (const guint8 *)"";
            ws->cbs.on_binary(bytes, m->data->len, ws->user_data);
        }
    }
}

static void
ns_ws_invoke_close(ns_ws *ws, gpointer payload)
{
    ns_ws_close_payload *c = payload;
    if (ws->cbs.on_close)
        ws->cbs.on_close(c->code, c->reason ? c->reason : "", c->clean, ws->user_data);
}

static void
ns_ws_invoke_error(ns_ws *ws, gpointer payload)
{
    const char *msg = payload;
    if (ws->cbs.on_error) ws->cbs.on_error(msg, ws->user_data);
}

static void
ns_ws_dispatch_open(ns_ws *ws)
{
    g_atomic_int_set(&ws->state, NS_WS_STATE_OPEN);
    ns_ws_post(ws, ns_ws_invoke_open, NULL, NULL);
}

static void
ns_ws_dispatch_message(ns_ws *ws, gboolean is_text,
                       const guint8 *data, gsize len)
{
    ns_ws_msg_payload *m = g_new0(ns_ws_msg_payload, 1);
    m->is_text = is_text;
    m->data = g_byte_array_sized_new(len);
    if (len) g_byte_array_append(m->data, data, len);
    ns_ws_post(ws, ns_ws_invoke_msg, m, ns_ws_msg_payload_free);
}

static void
ns_ws_dispatch_close(ns_ws *ws, int code, const char *reason, gboolean clean)
{
    g_atomic_int_set(&ws->state, NS_WS_STATE_CLOSED);
    ns_ws_close_payload *c = g_new0(ns_ws_close_payload, 1);
    c->code = code;
    c->reason = reason ? g_strdup(reason) : NULL;
    c->clean = clean;
    ns_ws_post(ws, ns_ws_invoke_close, c, ns_ws_close_payload_free);
}

static void
ns_ws_dispatch_error(ns_ws *ws, const char *msg)
{
    ns_ws_post(ws, ns_ws_invoke_error, g_strdup(msg ? msg : ""), g_free);
}

static gboolean
ns_ws_send_curl(ns_ws *ws, CURL *curl, const guint8 *data, gsize len,
                unsigned int flags)
{
    gsize off = 0;
    int stalls = 0;
    for (;;) {
        if (g_atomic_int_get(&ws->exit_requested)) return FALSE;
        gsize remain = len - off;
        size_t sent = 0;
        CURLcode rc = curl_ws_send(curl,
                                   remain ? (const void *)(data + off) : "",
                                   remain, &sent, 0, flags);
        if (rc == CURLE_AGAIN) {
            if (++stalls > 5000) return FALSE;
            g_usleep(2000);
            continue;
        }
        if (rc != CURLE_OK) return FALSE;
        off += sent;
        if (off >= len) return TRUE;
        if (sent == 0) {
            if (++stalls > 5000) return FALSE;
            g_usleep(2000);
        } else {
            stalls = 0;
        }
    }
}

static int
ns_ws_echo_close_code(int code)
{
    if (code == 1000 || code == 1001 || code == 1002 || code == 1003 ||
        (code >= 1007 && code <= 1014) || (code >= 3000 && code <= 4999))
        return code;
    return 0;
}

static gboolean
ns_ws_send_close_frame(CURL *curl, int code, const char *reason)
{
    guint8 buf[125];
    gsize  len = 0;
    if (code > 0) {
        buf[0] = (guint8)((code >> 8) & 0xff);
        buf[1] = (guint8)(code & 0xff);
        len = 2;
        if (reason && *reason) {
            gsize rlen = strlen(reason);
            if (rlen > sizeof buf - 2) rlen = sizeof buf - 2;
            memcpy(buf + 2, reason, rlen);
            len += rlen;
        }
    }
    size_t sent = 0;
    CURLcode rc = curl_ws_send(curl, len ? buf : (const void *)"", len,
                               &sent, 0, CURLWS_CLOSE);
    return rc == CURLE_OK;
}

static void
ns_ws_drain_outgoing(ns_ws *ws, CURL *curl, gboolean *want_close,
                     int *close_code, char **close_reason)
{
    for (;;) {
        g_mutex_lock(&ws->lock);
        ns_ws_out_msg *m = g_queue_pop_head(&ws->out_queue);
        g_mutex_unlock(&ws->lock);
        if (!m) return;
        switch (m->kind) {
        case NS_WS_OUT_TEXT:
            ns_ws_send_curl(ws, curl, m->data, m->len, CURLWS_TEXT);
            break;
        case NS_WS_OUT_BINARY:
            ns_ws_send_curl(ws, curl, m->data, m->len, CURLWS_BINARY);
            break;
        case NS_WS_OUT_CLOSE:
            *want_close = TRUE;
            *close_code = m->close_code;
            g_free(*close_reason);
            *close_reason = m->data ? g_strndup((const char *)m->data, m->len) : NULL;
            break;
        }
        g_free(m->data);
        g_free(m);
        if (*want_close) return;
    }
}

static void
ns_ws_handle_frame(ns_ws *ws, const guint8 *data, gsize len,
                   const struct curl_ws_frame *meta,
                   CURL *curl,
                   gboolean *peer_closed,
                   int *peer_code, char **peer_reason,
                   gboolean *bad_utf8, gboolean *too_big)
{
    int flags = meta->flags;

    if (flags & CURLWS_CLOSE) {
        int code = 1005;
        char *reason = NULL;
        if (data && len >= 2 && len <= 125) {
            code = (data[0] << 8) | data[1];
            if (len > 2)
                reason = g_strndup((const char *)data + 2, len - 2);
        } else if (data && len == 1) {
            code = 1002;
        }
        *peer_closed = TRUE;
        *peer_code = code;
        *peer_reason = reason;
        return;
    }

    if (flags & CURLWS_PING) {
        size_t sent = 0;
        size_t pong_len = (data && len > 0) ? (len > 125 ? 125 : len) : 0;
        curl_ws_send(curl, pong_len ? data : NULL, pong_len, &sent, 0,
                     CURLWS_PONG);
        return;
    }

    if (!(flags & (CURLWS_TEXT | CURLWS_BINARY | CURLWS_CONT))) return;

    if (!ws->recv_assembly) ws->recv_assembly = g_byte_array_new();
    if (meta->offset == 0 && !ws->recv_in_message &&
        (flags & (CURLWS_TEXT | CURLWS_BINARY))) {
        g_byte_array_set_size(ws->recv_assembly, 0);
        ws->recv_assembly_kind = (flags & CURLWS_BINARY)
            ? CURLWS_BINARY : CURLWS_TEXT;
        ws->recv_in_message = TRUE;
    } else if (!ws->recv_in_message) {
        return;
    }
    if (len) {
        if (ws->recv_assembly->len + len > NS_WS_MAX_MESSAGE) {
            *too_big = TRUE;
            g_byte_array_set_size(ws->recv_assembly, 0);
            ws->recv_in_message = FALSE;
            return;
        }
        g_byte_array_append(ws->recv_assembly, data, len);
    }

    if (meta->bytesleft == 0 && !(flags & CURLWS_CONT)) {
        gboolean is_text = (ws->recv_assembly_kind != CURLWS_BINARY);
        if (is_text && ws->recv_assembly->len > 0 &&
            !g_utf8_validate((const char *)ws->recv_assembly->data,
                             ws->recv_assembly->len, NULL)) {
            *bad_utf8 = TRUE;
        } else {
            ns_ws_dispatch_message(ws, is_text,
                                   ws->recv_assembly->data,
                                   ws->recv_assembly->len);
        }
        g_byte_array_set_size(ws->recv_assembly, 0);
        ws->recv_in_message = FALSE;
    }
}

static void
ns_ws_worker_wait(ns_ws *ws)
{
    g_mutex_lock(&ws->lock);
    if (g_queue_is_empty(&ws->out_queue) &&
        !g_atomic_int_get(&ws->exit_requested)) {
        gint64 deadline = g_get_monotonic_time() +
                          (gint64)NS_WS_POLL_MSEC * G_TIME_SPAN_MILLISECOND;
        g_cond_wait_until(&ws->cond, &ws->lock, deadline);
    }
    g_mutex_unlock(&ws->lock);
}

static int
ns_ws_handshake_progress(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                         curl_off_t ultotal, curl_off_t ulnow)
{
    (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
    const ns_ws *ws = clientp;
    return g_atomic_int_get(&ws->exit_requested) ? 1 : 0;
}

static gpointer
ns_ws_worker_curl(gpointer data)
{
    ns_ws *ws = data;
    CURL *curl = curl_easy_init();
    if (!curl) {
        ns_ws_dispatch_error(ws, "curl init failed");
        ns_ws_dispatch_close(ws, 1006, "init failed", FALSE);
        ns_ws_unref(ws);
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, ws->url);
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION,
                     (long)CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, NS_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    ns_net_apply_curl_tls(curl);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ns_ws_handshake_progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, ws);

    struct curl_slist *headers = NULL;
    if (ws->origin && *ws->origin) {
        char *h = g_strconcat("Origin: ", ws->origin, NULL);
        headers = curl_slist_append(headers, h);
        g_free(h);
    }
    if (ws->protocols && ws->protocols->len > 0) {
        GString *s = g_string_new("Sec-WebSocket-Protocol: ");
        for (guint i = 0; i < ws->protocols->len; i++) {
            if (i > 0) g_string_append(s, ", ");
            g_string_append(s, g_ptr_array_index(ws->protocols, i));
        }
        headers = curl_slist_append(headers, s->str);
        g_string_free(s, TRUE);
    }
    if (headers)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    char errbuf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

    CURLcode rc = curl_easy_perform(curl);

    if (rc != CURLE_OK || g_atomic_int_get(&ws->exit_requested)) {
        const char *msg = rc != CURLE_OK
            ? (errbuf[0] ? errbuf : curl_easy_strerror(rc))
            : "aborted";
        if (rc != CURLE_OK) ns_ws_dispatch_error(ws, msg);
        ns_ws_dispatch_close(ws, 1006, msg, FALSE);
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        ns_ws_unref(ws);
        return NULL;
    }

    {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        if (code != 0 && code != 101) {
            char *msg = g_strdup_printf(
                "WebSocket handshake failed (HTTP %ld)", code);
            ns_ws_dispatch_error(ws, msg);
            ns_ws_dispatch_close(ws, 1006, msg, FALSE);
            g_free(msg);
            if (headers) curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            ns_ws_unref(ws);
            return NULL;
        }
    }
    ns_ws_dispatch_open(ws);

    gboolean clean_close = FALSE;
    int close_code = 1006;
    char *close_reason = NULL;
    gboolean peer_closed = FALSE;
    int peer_code = 1005;
    char *peer_reason = NULL;
    gboolean want_close = FALSE;
    gboolean bad_utf8 = FALSE;
    gboolean too_big = FALSE;

    guint8 buf[NS_WS_RECV_BUF];

    while (!g_atomic_int_get(&ws->exit_requested)) {
        ns_ws_drain_outgoing(ws, curl, &want_close, &close_code, &close_reason);
        if (want_close) {
            g_atomic_int_set(&ws->state, NS_WS_STATE_CLOSING);
            ns_ws_send_close_frame(curl, close_code, close_reason);
            clean_close = TRUE;
            break;
        }

        size_t got = 0;
        const struct curl_ws_frame *meta = NULL;
        rc = curl_ws_recv(curl, buf, sizeof buf, &got, &meta);
        if (rc == CURLE_AGAIN) {
            ns_ws_worker_wait(ws);
            continue;
        }
        if (rc != CURLE_OK) {
            const char *msg = errbuf[0] ? errbuf : curl_easy_strerror(rc);
            ns_ws_dispatch_error(ws, msg);
            close_code = 1006;
            g_free(close_reason);
            close_reason = g_strdup(msg);
            break;
        }
        if (meta) {
            ns_ws_handle_frame(ws, buf, got, meta, curl,
                               &peer_closed, &peer_code, &peer_reason,
                               &bad_utf8, &too_big);
            if (too_big) {
                g_atomic_int_set(&ws->state, NS_WS_STATE_CLOSING);
                ns_ws_send_close_frame(curl, 1009, "message too big");
                clean_close = FALSE;
                close_code = 1009;
                g_free(close_reason);
                close_reason = g_strdup("message too big");
                break;
            }
            if (bad_utf8) {
                g_atomic_int_set(&ws->state, NS_WS_STATE_CLOSING);
                ns_ws_send_close_frame(curl, 1007, "invalid utf-8");
                clean_close = FALSE;
                close_code = 1007;
                g_free(close_reason);
                close_reason = g_strdup("invalid utf-8");
                break;
            }
            if (peer_closed) {
                g_atomic_int_set(&ws->state, NS_WS_STATE_CLOSING);
                ns_ws_send_close_frame(curl, ns_ws_echo_close_code(peer_code),
                                       NULL);
                clean_close = TRUE;
                close_code = peer_code;
                g_free(close_reason);
                close_reason = peer_reason ? g_strdup(peer_reason) : NULL;
                break;
            }
        }
    }

    ns_ws_dispatch_close(ws, close_code,
                         close_reason ? close_reason
                                      : (peer_reason ? peer_reason : ""),
                         clean_close);

    g_free(close_reason);
    g_free(peer_reason);
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    ns_ws_unref(ws);
    return NULL;
}

typedef struct {
    char    *connect_url;
    char    *host_hdr;
    char    *path;
    gboolean tls;
} ns_ws_target;

static void
ns_ws_target_clear(ns_ws_target *t)
{
    g_free(t->connect_url);
    g_free(t->host_hdr);
    g_free(t->path);
}

static gboolean
ns_ws_has_control_char(const char *s)
{
    for (; *s; s++)
        if ((guchar)*s < 0x20 || (guchar)*s == 0x7f)
            return TRUE;
    return FALSE;
}

static gboolean
ns_ws_parse_url(const char *url, ns_ws_target *out)
{
    gboolean tls;
    const char *p = url;
    if (g_ascii_strncasecmp(p, "wss://", 6) == 0)      { tls = TRUE;  p += 6; }
    else if (g_ascii_strncasecmp(p, "ws://", 5) == 0)  { tls = FALSE; p += 5; }
    else return FALSE;

    const char *host_start = p;
    while (*p && *p != '/' && *p != '?' && *p != '#') p++;
    if (p == host_start) return FALSE;
    char *hostport = g_strndup(host_start, (gsize)(p - host_start));

    const char *path = *p ? p : "/";
    const char *frag = strchr(path, '#');
    char *path_dup = frag ? g_strndup(path, (gsize)(frag - path)) : g_strdup(path);
    if (!*path_dup) { g_free(path_dup); path_dup = g_strdup("/"); }

    if (ns_ws_has_control_char(hostport) || ns_ws_has_control_char(path_dup)) {
        g_free(hostport);
        g_free(path_dup);
        return FALSE;
    }

    char *host_hdr = g_strdup(hostport);
    if (strchr(hostport, ']') == NULL) {
        char *colon = strrchr(host_hdr, ':');
        if (colon && ((tls && strcmp(colon, ":443") == 0) ||
                      (!tls && strcmp(colon, ":80") == 0)))
            *colon = '\0';
    }

    out->tls = tls;
    out->connect_url = g_strdup_printf("%s://%s", tls ? "https" : "http", hostport);
    out->host_hdr = host_hdr;
    out->path = path_dup;
    g_free(hostport);
    return TRUE;
}

static void
ns_ws_random_bytes(guint8 *buf, gsize len)
{
    if (ns_security_csprng_fill(buf, len)) return;
    for (gsize i = 0; i < len; i++)
        buf[i] = (guint8)g_random_int_range(0, 256);
}

static char *
ns_ws_accept_for_key(const char *key)
{
    GChecksum *c = g_checksum_new(G_CHECKSUM_SHA1);
    g_checksum_update(c, (const guchar *)key, (gssize)strlen(key));
    g_checksum_update(c, (const guchar *)"258EAFA5-E914-47DA-95CA-C5AB0DC85B11", 36);
    guint8 digest[20];
    gsize dl = sizeof digest;
    g_checksum_get_digest(c, digest, &dl);
    g_checksum_free(c);
    return g_base64_encode(digest, sizeof digest);
}

static gboolean
ns_ws_raw_send_all(ns_ws *ws, CURL *curl, const guint8 *data, gsize len)
{
    gsize off = 0;
    int stalls = 0;
    while (off < len) {
        if (g_atomic_int_get(&ws->exit_requested)) return FALSE;
        size_t sent = 0;
        CURLcode rc = curl_easy_send(curl, data + off, len - off, &sent);
        if (rc == CURLE_AGAIN) {
            if (++stalls > 5000) return FALSE;
            g_usleep(2000);
            continue;
        }
        if (rc != CURLE_OK) return FALSE;
        off += sent;
        if (sent == 0) { if (++stalls > 5000) return FALSE; g_usleep(2000); }
        else stalls = 0;
    }
    return TRUE;
}

static gboolean
ns_ws_raw_send_frame(ns_ws *ws, CURL *curl, int opcode,
                     const guint8 *data, gsize len)
{
    guint8 hdr[14];
    gsize hl = 0;
    hdr[0] = (guint8)(0x80 | (opcode & 0x0f));
    if (len < 126) {
        hdr[1] = (guint8)(0x80 | len);
        hl = 2;
    } else if (len <= 0xffff) {
        hdr[1] = 0x80 | 126;
        hdr[2] = (guint8)((len >> 8) & 0xff);
        hdr[3] = (guint8)(len & 0xff);
        hl = 4;
    } else {
        hdr[1] = 0x80 | 127;
        for (int i = 0; i < 8; i++)
            hdr[2 + i] = (guint8)((((guint64)len) >> (56 - 8 * i)) & 0xff);
        hl = 10;
    }
    guint8 mask[4];
    ns_ws_random_bytes(mask, 4);
    memcpy(hdr + hl, mask, 4);
    hl += 4;
    if (!ns_ws_raw_send_all(ws, curl, hdr, hl)) return FALSE;
    if (len) {
        guint8 *masked = g_malloc(len);
        for (gsize i = 0; i < len; i++) masked[i] = data[i] ^ mask[i & 3];
        gboolean ok = ns_ws_raw_send_all(ws, curl, masked, len);
        g_free(masked);
        return ok;
    }
    return TRUE;
}

static gboolean
ns_ws_raw_send_close(ns_ws *ws, CURL *curl, int code, const char *reason)
{
    guint8 buf[125];
    gsize len = 0;
    if (code > 0) {
        buf[0] = (guint8)((code >> 8) & 0xff);
        buf[1] = (guint8)(code & 0xff);
        len = 2;
        if (reason && *reason) {
            gsize rlen = strlen(reason);
            if (rlen > sizeof buf - 2) rlen = sizeof buf - 2;
            memcpy(buf + 2, reason, rlen);
            len += rlen;
        }
    }
    return ns_ws_raw_send_frame(ws, curl, 0x8, len ? buf : NULL, len);
}

static void
ns_ws_drain_outgoing_manual(ns_ws *ws, CURL *curl, gboolean *want_close,
                            int *close_code, char **close_reason)
{
    for (;;) {
        g_mutex_lock(&ws->lock);
        ns_ws_out_msg *m = g_queue_pop_head(&ws->out_queue);
        g_mutex_unlock(&ws->lock);
        if (!m) return;
        switch (m->kind) {
        case NS_WS_OUT_TEXT:
            ns_ws_raw_send_frame(ws, curl, 0x1, m->data, m->len);
            break;
        case NS_WS_OUT_BINARY:
            ns_ws_raw_send_frame(ws, curl, 0x2, m->data, m->len);
            break;
        case NS_WS_OUT_CLOSE:
            *want_close = TRUE;
            *close_code = m->close_code;
            g_free(*close_reason);
            *close_reason = m->data ? g_strndup((const char *)m->data, m->len) : NULL;
            break;
        }
        g_free(m->data);
        g_free(m);
        if (*want_close) return;
    }
}

static int
ns_ws_raw_take_frame(GByteArray *in, gboolean *fin, int *opcode,
                     guint8 **payload, gsize *plen)
{
    if (in->len < 2) return 0;
    const guint8 *d = in->data;
    if (d[1] & 0x80) return -1;
    guint64 len = d[1] & 0x7f;
    gsize pos = 2;
    if (len == 126) {
        if (in->len < 4) return 0;
        len = ((guint64)d[2] << 8) | d[3];
        pos = 4;
    } else if (len == 127) {
        if (in->len < 10) return 0;
        len = 0;
        for (int i = 0; i < 8; i++) len = (len << 8) | d[2 + i];
        pos = 10;
    }
    if (len > NS_WS_MAX_MESSAGE) return -1;
    int frame_op = d[0] & 0x0f;
    gboolean frame_fin = (d[0] & 0x80) != 0;
    if (frame_op >= 0x8 && (len > 125 || !frame_fin)) return -1;
    if (in->len < pos + len) return 0;

    guint8 *pl = len ? g_malloc(len) : NULL;
    if (len) memcpy(pl, d + pos, len);
    *fin = frame_fin;
    *opcode = frame_op;
    *payload = pl;
    *plen = len;
    g_byte_array_remove_range(in, 0, pos + len);
    return 1;
}

static gboolean
ns_ws_manual_handshake(ns_ws *ws, CURL *curl, const ns_ws_target *t,
                       GByteArray *leftover, char **err)
{
    guint8 rnd[16];
    ns_ws_random_bytes(rnd, sizeof rnd);
    char *key = g_base64_encode(rnd, sizeof rnd);

    GString *req = g_string_new(NULL);
    g_string_append_printf(req, "GET %s HTTP/1.1\r\n", t->path);
    g_string_append_printf(req, "Host: %s\r\n", t->host_hdr);
    g_string_append(req, "Upgrade: websocket\r\n");
    g_string_append(req, "Connection: Upgrade\r\n");
    g_string_append_printf(req, "Sec-WebSocket-Key: %s\r\n", key);
    g_string_append(req, "Sec-WebSocket-Version: 13\r\n");
    if (ws->origin && *ws->origin)
        g_string_append_printf(req, "Origin: %s\r\n", ws->origin);
    if (ws->protocols && ws->protocols->len > 0) {
        g_string_append(req, "Sec-WebSocket-Protocol: ");
        for (guint i = 0; i < ws->protocols->len; i++) {
            if (i > 0) g_string_append(req, ", ");
            g_string_append(req, g_ptr_array_index(ws->protocols, i));
        }
        g_string_append(req, "\r\n");
    }
    g_string_append_printf(req, "User-Agent: %s\r\n", NS_USER_AGENT);
    g_string_append(req, "\r\n");

    gboolean sent = ns_ws_raw_send_all(ws, curl, (const guint8 *)req->str, req->len);
    g_string_free(req, TRUE);
    if (!sent) {
        g_free(key);
        *err = g_strdup("WebSocket handshake send failed");
        return FALSE;
    }

    GByteArray *resp = g_byte_array_new();
    gint64 deadline = g_get_monotonic_time() + 15 * G_TIME_SPAN_SECOND;
    gssize hdr_end = -1;
    while (hdr_end < 0) {
        if (g_atomic_int_get(&ws->exit_requested) ||
            g_get_monotonic_time() > deadline) {
            g_byte_array_free(resp, TRUE);
            g_free(key);
            *err = g_strdup("WebSocket handshake timed out");
            return FALSE;
        }
        char tmp[2048];
        size_t got = 0;
        CURLcode rc = curl_easy_recv(curl, tmp, sizeof tmp, &got);
        if (rc == CURLE_AGAIN) { g_usleep(5000); continue; }
        if (rc != CURLE_OK || got == 0) {
            g_byte_array_free(resp, TRUE);
            g_free(key);
            *err = g_strdup("WebSocket handshake connection closed");
            return FALSE;
        }
        g_byte_array_append(resp, (guint8 *)tmp, got);
        for (gsize i = 0; i + 3 < resp->len; i++) {
            if (resp->data[i] == '\r' && resp->data[i + 1] == '\n' &&
                resp->data[i + 2] == '\r' && resp->data[i + 3] == '\n') {
                hdr_end = (gssize)i;
                break;
            }
        }
        if (resp->len > 65536) {
            g_byte_array_free(resp, TRUE);
            g_free(key);
            *err = g_strdup("WebSocket handshake response too large");
            return FALSE;
        }
    }

    char *headers = g_strndup((const char *)resp->data, (gsize)hdr_end);
    gsize body_off = (gsize)hdr_end + 4;
    if (resp->len > body_off)
        g_byte_array_append(leftover, resp->data + body_off, resp->len - body_off);
    g_byte_array_free(resp, TRUE);

    char *expected = ns_ws_accept_for_key(key);
    g_free(key);

    gboolean ok = FALSE;
    char *status = strstr(headers, " 101");
    char *eol = strchr(headers, '\n');
    if (status && eol && status < eol) {
        char *low = g_ascii_strdown(headers, -1);
        char *acc = strstr(low, "sec-websocket-accept:");
        if (acc) {
            acc += strlen("sec-websocket-accept:");
            while (*acc == ' ' || *acc == '\t') acc++;
            char *end = acc;
            while (*end && *end != '\r' && *end != '\n' && *end != ' ') end++;
            char *got_accept = g_strndup(acc, (gsize)(end - acc));
            char *exp_low = g_ascii_strdown(expected, -1);
            ok = strcmp(got_accept, exp_low) == 0;
            g_free(got_accept);
            g_free(exp_low);
        }
        g_free(low);
    }
    g_free(expected);
    g_free(headers);
    if (!ok) *err = g_strdup("WebSocket handshake rejected by server");
    return ok;
}

static gpointer
ns_ws_worker_manual(gpointer data)
{
    ns_ws *ws = data;
    ns_ws_target target = {0};
    if (!ns_ws_parse_url(ws->url, &target)) {
        ns_ws_dispatch_error(ws, "invalid WebSocket URL");
        ns_ws_dispatch_close(ws, 1006, "invalid URL", FALSE);
        ns_ws_unref(ws);
        return NULL;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        ns_ws_target_clear(&target);
        ns_ws_dispatch_error(ws, "curl init failed");
        ns_ws_dispatch_close(ws, 1006, "init failed", FALSE);
        ns_ws_unref(ws);
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, target.connect_url);
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, target.tls ? 2L : 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, NS_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    ns_net_apply_curl_tls(curl);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ns_ws_handshake_progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, ws);
    char errbuf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK || g_atomic_int_get(&ws->exit_requested)) {
        const char *msg = rc != CURLE_OK
            ? (errbuf[0] ? errbuf : curl_easy_strerror(rc)) : "aborted";
        if (rc != CURLE_OK) ns_ws_dispatch_error(ws, msg);
        ns_ws_dispatch_close(ws, 1006, msg, FALSE);
        curl_easy_cleanup(curl);
        ns_ws_target_clear(&target);
        ns_ws_unref(ws);
        return NULL;
    }

    GByteArray *inbuf = g_byte_array_new();
    char *herr = NULL;
    if (!ns_ws_manual_handshake(ws, curl, &target, inbuf, &herr)) {
        ns_ws_dispatch_error(ws, herr ? herr : "handshake failed");
        ns_ws_dispatch_close(ws, 1006, herr ? herr : "handshake failed", FALSE);
        g_free(herr);
        g_byte_array_free(inbuf, TRUE);
        curl_easy_cleanup(curl);
        ns_ws_target_clear(&target);
        ns_ws_unref(ws);
        return NULL;
    }
    ns_ws_target_clear(&target);
    ns_ws_dispatch_open(ws);

    GByteArray *msg = g_byte_array_new();
    gboolean in_msg = FALSE, msg_is_text = FALSE;
    gboolean clean_close = FALSE;
    int close_code = 1006;
    char *close_reason = NULL;
    gboolean want_close = FALSE;

    while (!g_atomic_int_get(&ws->exit_requested)) {
        ns_ws_drain_outgoing_manual(ws, curl, &want_close, &close_code, &close_reason);
        if (want_close) {
            g_atomic_int_set(&ws->state, NS_WS_STATE_CLOSING);
            ns_ws_raw_send_close(ws, curl, close_code, close_reason);
            clean_close = TRUE;
            break;
        }

        char tmp[NS_WS_RECV_BUF];
        size_t got = 0;
        rc = curl_easy_recv(curl, tmp, sizeof tmp, &got);
        if (rc == CURLE_AGAIN) {
            ns_ws_worker_wait(ws);
            continue;
        }
        if (rc != CURLE_OK || got == 0) {
            const char *m = (rc != CURLE_OK && errbuf[0]) ? errbuf
                : (rc != CURLE_OK ? curl_easy_strerror(rc) : "connection closed");
            ns_ws_dispatch_error(ws, m);
            close_code = 1006;
            g_free(close_reason);
            close_reason = g_strdup(m);
            break;
        }
        g_byte_array_append(inbuf, (guint8 *)tmp, got);

        gboolean fatal = FALSE;
        for (;;) {
            gboolean fin = FALSE;
            int opcode = 0;
            guint8 *pl = NULL;
            gsize plen = 0;
            int take = ns_ws_raw_take_frame(inbuf, &fin, &opcode, &pl, &plen);
            if (take == 0) break;
            if (take < 0) {
                g_atomic_int_set(&ws->state, NS_WS_STATE_CLOSING);
                ns_ws_raw_send_close(ws, curl, 1009, "message too big");
                close_code = 1009;
                g_free(close_reason);
                close_reason = g_strdup("message too big");
                fatal = TRUE;
                break;
            }
            if (opcode == 0x8) {
                int code = 1005;
                if (plen >= 2) code = (pl[0] << 8) | pl[1];
                char *reason = (plen > 2) ? g_strndup((char *)pl + 2, plen - 2) : NULL;
                g_atomic_int_set(&ws->state, NS_WS_STATE_CLOSING);
                ns_ws_raw_send_close(ws, curl, ns_ws_echo_close_code(code), NULL);
                clean_close = TRUE;
                close_code = code;
                g_free(close_reason);
                close_reason = reason;
                g_free(pl);
                fatal = TRUE;
                break;
            } else if (opcode == 0x9) {
                ns_ws_raw_send_frame(ws, curl, 0xA, pl, plen > 125 ? 125 : plen);
            } else if (opcode == 0xA) {
                /* pong: ignore */
            } else if (opcode == 0x1 || opcode == 0x2) {
                if (in_msg) { fatal = TRUE; close_code = 1002; g_free(pl); break; }
                in_msg = TRUE;
                msg_is_text = (opcode == 0x1);
                g_byte_array_set_size(msg, 0);
                if (plen) g_byte_array_append(msg, pl, plen);
            } else if (opcode == 0x0) {
                if (!in_msg) { fatal = TRUE; close_code = 1002; g_free(pl); break; }
                if (plen) {
                    if (msg->len + plen > NS_WS_MAX_MESSAGE) {
                        g_atomic_int_set(&ws->state, NS_WS_STATE_CLOSING);
                        ns_ws_raw_send_close(ws, curl, 1009, "message too big");
                        close_code = 1009;
                        g_free(close_reason);
                        close_reason = g_strdup("message too big");
                        fatal = TRUE;
                        g_free(pl);
                        break;
                    }
                    g_byte_array_append(msg, pl, plen);
                }
            }
            if ((opcode == 0x0 || opcode == 0x1 || opcode == 0x2) && fin && in_msg) {
                if (msg_is_text && msg->len > 0 &&
                    !g_utf8_validate((const char *)msg->data, msg->len, NULL)) {
                    g_atomic_int_set(&ws->state, NS_WS_STATE_CLOSING);
                    ns_ws_raw_send_close(ws, curl, 1007, "invalid utf-8");
                    close_code = 1007;
                    g_free(close_reason);
                    close_reason = g_strdup("invalid utf-8");
                    fatal = TRUE;
                } else {
                    ns_ws_dispatch_message(ws, msg_is_text, msg->data, msg->len);
                }
                in_msg = FALSE;
                g_byte_array_set_size(msg, 0);
            }
            g_free(pl);
            if (fatal) break;
        }
        if (fatal) break;
    }

    ns_ws_dispatch_close(ws, close_code, close_reason ? close_reason : "",
                         clean_close);
    g_free(close_reason);
    g_byte_array_free(inbuf, TRUE);
    g_byte_array_free(msg, TRUE);
    curl_easy_cleanup(curl);
    ns_ws_unref(ws);
    return NULL;
}

static gboolean
ns_ws_curl_native(void)
{
    const curl_version_info_data *v = curl_version_info(CURLVERSION_NOW);
    if (!v || !v->protocols) return FALSE;
    for (const char *const *p = v->protocols; *p; p++) {
        if (g_ascii_strcasecmp(*p, "ws") == 0) return TRUE;
        if (g_ascii_strcasecmp(*p, "wss") == 0) return TRUE;
    }
    return FALSE;
}

static gpointer
ns_ws_worker(gpointer data)
{
    if (ns_ws_curl_native())
        return ns_ws_worker_curl(data);
    return ns_ws_worker_manual(data);
}

gboolean
ns_ws_available(void)
{
    return TRUE;
}

ns_ws *
ns_ws_new(const char        *url,
          const char        *origin,
          const char *const *protocols,
          const ns_ws_callbacks *cbs,
          gpointer           user_data)
{
    g_return_val_if_fail(url != NULL, NULL);
    if (!ns_ws_available()) return NULL;

    ns_ws *ws = g_new0(ns_ws, 1);
    ws->refcount = 1;
    ws->url    = g_strdup(url);
    ws->origin = origin ? g_strdup(origin) : NULL;
    if (protocols && protocols[0]) {
        ws->protocols = g_ptr_array_new_with_free_func(g_free);
        for (int i = 0; protocols[i]; i++)
            g_ptr_array_add(ws->protocols, g_strdup(protocols[i]));
    }
    if (cbs) ws->cbs = *cbs;
    ws->user_data = user_data;
    g_mutex_init(&ws->lock);
    g_cond_init(&ws->cond);
    g_queue_init(&ws->out_queue);
    g_atomic_int_set(&ws->state, NS_WS_STATE_CONNECTING);

    ns_ws_ref(ws);
    ws->thread = g_thread_new("nd-ws", ns_ws_worker, ws);
    return ws;
}

static gboolean
ns_ws_enqueue(ns_ws *ws, ns_ws_out_kind kind,
              const guint8 *data, gsize len, int close_code)
{
    if (!ws) return FALSE;
    int s = g_atomic_int_get(&ws->state);
    if (s == NS_WS_STATE_CLOSED) return FALSE;
    if (kind != NS_WS_OUT_CLOSE && s == NS_WS_STATE_CLOSING) return FALSE;
    if (kind != NS_WS_OUT_CLOSE && len > NS_WS_MAX_MESSAGE) return FALSE;

    ns_ws_out_msg *m = g_new0(ns_ws_out_msg, 1);
    m->kind = kind;
    m->close_code = close_code;
    if (data && len > 0) {
        m->data = g_memdup2(data, len);
        m->len = len;
    }
    g_mutex_lock(&ws->lock);
    g_queue_push_tail(&ws->out_queue, m);
    g_cond_signal(&ws->cond);
    g_mutex_unlock(&ws->lock);
    return TRUE;
}

gboolean
ns_ws_send_text(ns_ws *ws, const char *text, gsize len)
{
    return ns_ws_enqueue(ws, NS_WS_OUT_TEXT,
                         (const guint8 *)text, len, 0);
}

gboolean
ns_ws_send_binary(ns_ws *ws, const guint8 *data, gsize len)
{
    return ns_ws_enqueue(ws, NS_WS_OUT_BINARY, data, len, 0);
}

void
ns_ws_close(ns_ws *ws, int code, const char *reason)
{
    if (!ws) return;
    int s = g_atomic_int_get(&ws->state);
    if (s == NS_WS_STATE_CLOSING || s == NS_WS_STATE_CLOSED) return;
    if (code <= 0) code = 1000;
    gsize rlen = reason ? strlen(reason) : 0;
    ns_ws_enqueue(ws, NS_WS_OUT_CLOSE, (const guint8 *)reason, rlen, code);
    g_atomic_int_set(&ws->state, NS_WS_STATE_CLOSING);
}

int
ns_ws_state_get(ns_ws *ws)
{
    if (!ws) return NS_WS_STATE_CLOSED;
    return g_atomic_int_get(&ws->state);
}

void
ns_ws_free(ns_ws *ws)
{
    if (!ws) return;
    g_atomic_int_set(&ws->detached, 1);
    g_atomic_int_set(&ws->exit_requested, 1);
    g_mutex_lock(&ws->lock);
    g_cond_signal(&ws->cond);
    g_mutex_unlock(&ws->lock);
    if (ws->thread) {
        g_thread_join(ws->thread);
        ws->thread = NULL;
    }
    ns_ws_unref(ws);
}
