/* Nordstjernen — libcurl-backed async fetcher API.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_NET_H
#define NS_NET_H

#include <gio/gio.h>
#include <glib.h>

#include "version.h"

G_BEGIN_DECLS

#define NS_MAX_REDIRECTS 10
#define NS_DEFAULT_TIMEOUT_S 30
#define NS_MAX_TIMEOUT_S 60
#define NS_USER_AGENT \
    "Mozilla/5.0 (X11; Linux x86_64; rv:109.0) Gecko/20100101 " \
    "Nordstjernen/" NS_VERSION

typedef struct ns_response {
    long  status;
    char *final_url;
    char *content_type;
    char *content_disposition;
    char *csp_header;
    char *xframe_options;
    char *cors_allow_origin;
    char *refresh;
    char *raw_headers;
    GByteArray *body;
    char *error;
    char *tls_warning;
    int   redirect_count;
} ns_response;


void ns_response_free(ns_response *resp);

char *ns_build_error_page(const char *url, long status,
                          const char *transport_error);

void ns_net_init(void);
void ns_net_shutdown(void);

const char *ns_net_default_accept_language(void);
const char *ns_net_supported_encodings(void);

void ns_net_fetch_async(const char        *url,
                        const char        *top_url,
                        GCancellable      *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer            user_data);

void ns_net_post_async(const char         *url,
                       const char         *top_url,
                       const void         *body,
                       gsize               body_len,
                       const char         *content_type,
                       GCancellable       *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer            user_data);

void ns_net_request_async(const char         *url,
                          const char         *top_url,
                          const char         *method,
                          const void         *body,
                          gsize               body_len,
                          const char         *content_type,
                          const char *const  *extra_headers,
                          GCancellable       *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer            user_data);

ns_response *ns_net_fetch_finish(GAsyncResult *result, GError **error);

ns_response *ns_net_fetch_blocking(const char   *url,
                                   GCancellable *cancellable,
                                   GError      **error);

ns_response *ns_net_request_blocking(const char        *url,
                                     const char        *top_url,
                                     const char        *method,
                                     const void        *body,
                                     gsize              body_len,
                                     const char        *content_type,
                                     const char *const *extra_headers,
                                     GCancellable      *cancellable,
                                     GError           **error);

char    *ns_net_hsts_upgrade(const char *url);
gboolean ns_net_hsts_should_upgrade(const char *host);

char *ns_url_host_from(const char *url);
char *ns_url_origin_from(const char *url);
gboolean ns_url_same_origin(const char *a, const char *b);
gboolean ns_url_is_http_or_https(const char *url);

gboolean ns_data_url_decode(const char *url, GByteArray *out, guint64 budget,
                            char **out_content_type, gboolean *too_large);
gboolean ns_url_is_valid_absolute(const char *url);
char    *ns_url_resolve(const char *base, const char *href);

char    *ns_url_to_display(const char *url);

typedef struct ns_url_parts {
    char *href;
    char *protocol;
    char *origin;
    char *host;
    char *hostname;
    char *port;
    char *pathname;
    char *search;
    char *hash;
    char *username;
    char *password;
} ns_url_parts;

ns_url_parts *ns_url_parts_new(const char *url);
void          ns_url_parts_free(ns_url_parts *parts);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ns_url_parts, ns_url_parts_free)

char *ns_net_cookies_for_js(const char *url);
void  ns_net_cookie_store_from_js(const char *url, const char *cookie);

void  ns_net_set_proxy_override(const char *proxy_url);
void  ns_net_set_allow_file_urls(gboolean allow);
void  ns_net_set_log_fetches(gboolean on);
char *ns_net_proxy_mask(const char *proxy_url);
char *ns_net_effective_proxy_for(const char *url);

char *ns_multipart_boundary(void);
void  ns_multipart_quote_field(GString *out, const char *s);

void  ns_form_urlencoded_append(GString *out, const char *s);
void  ns_form_urlencoded_append_pair(GString *out, gboolean *first,
                                     const char *name, const char *value);

G_END_DECLS

#endif
