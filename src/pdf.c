/* Nordstjernen — PDF documents rendered to an inline HTML page via poppler-glib.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "pdf.h"
#include "html.h"

#include <string.h>

#ifdef NS_HAVE_POPPLER
#include <cairo.h>
#include <poppler.h>
#endif

#define NS_PDF_TARGET_W   1240.0
#define NS_PDF_MIN_SCALE  0.5
#define NS_PDF_MAX_SCALE  4.0
#define NS_PDF_MAX_DIM    4000
#define NS_PDF_MAX_PAGES  300

static const char ns_pdf_style[] =
    "<style>"
    "html,body{margin:0;background:#1c1d1e}"
    ".doc{max-width:900px;margin:0 auto;padding:24px 16px}"
    ".page{background:#fff;margin:0 auto 24px;"
    "box-shadow:0 4px 12px rgba(0,0,0,.45);border:1px solid #444}"
    ".page:last-child{margin-bottom:0}"
    ".page img{display:block;width:100%;height:auto}"
    ".note{color:#bbb;font:14px/1.6 system-ui,sans-serif;"
    "text-align:center;padding:48px 16px}"
    ".note a{color:#6ab7ff}"
    "</style>";

static char *
ns_pdf_notice_html(const char *url, const char *message)
{
    char *esc_url = ns_html_escape_text(url ? url : "");
    char *esc_msg = ns_html_escape_text(message ? message : "");
    char *html = g_strdup_printf(
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<title>PDF</title>%s</head><body><div class=\"doc\">"
        "<p class=\"note\">%s<br><a href=\"%s\">%s</a></p>"
        "</div></body></html>",
        ns_pdf_style, esc_msg, esc_url, esc_url);
    g_free(esc_url);
    g_free(esc_msg);
    return html;
}

#ifdef NS_HAVE_POPPLER
static cairo_status_t
ns_pdf_png_write(void *closure, const unsigned char *data, unsigned int length)
{
    g_byte_array_append((GByteArray *)closure, data, length);
    return CAIRO_STATUS_SUCCESS;
}

static char *
ns_pdf_page_data_uri(PopplerPage *page)
{
    double pw = 0, ph = 0;
    poppler_page_get_size(page, &pw, &ph);
    if (pw <= 0 || ph <= 0)
        return NULL;
    double scale = NS_PDF_TARGET_W / pw;
    scale = CLAMP(scale, NS_PDF_MIN_SCALE, NS_PDF_MAX_SCALE);
    int w = (int)(pw * scale + 0.5);
    int h = (int)(ph * scale + 0.5);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > NS_PDF_MAX_DIM) w = NS_PDF_MAX_DIM;
    if (h > NS_PDF_MAX_DIM) h = NS_PDF_MAX_DIM;

    cairo_surface_t *surf =
        cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        return NULL;
    }
    cairo_t *cr = cairo_create(surf);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_scale(cr, scale, scale);
    poppler_page_render(page, cr);
    cairo_destroy(cr);
    cairo_surface_flush(surf);

    GByteArray *png = g_byte_array_new();
    cairo_status_t st =
        cairo_surface_write_to_png_stream(surf, ns_pdf_png_write, png);
    cairo_surface_destroy(surf);
    if (st != CAIRO_STATUS_SUCCESS) {
        g_byte_array_free(png, TRUE);
        return NULL;
    }
    char *b64 = g_base64_encode(png->data, png->len);
    g_byte_array_free(png, TRUE);
    char *uri = g_strconcat("data:image/png;base64,", b64, NULL);
    g_free(b64);
    return uri;
}
#endif

char *
ns_pdf_document_html(const guint8 *data, gsize len, const char *url)
{
#ifdef NS_HAVE_POPPLER
    if (!data || len == 0)
        return ns_pdf_notice_html(url, "This document could not be opened.");

    GBytes *bytes = g_bytes_new(data, len);
    GError *err = NULL;
    PopplerDocument *doc = poppler_document_new_from_bytes(bytes, NULL, &err);
    g_bytes_unref(bytes);
    if (!doc) {
        char *msg = g_strdup_printf("This PDF could not be displayed%s%s.",
                                    err ? ": " : "",
                                    err ? err->message : "");
        g_clear_error(&err);
        char *html = ns_pdf_notice_html(url, msg);
        g_free(msg);
        return html;
    }
    g_clear_error(&err);

    int n = poppler_document_get_n_pages(doc);
    if (n <= 0) {
        g_object_unref(doc);
        return ns_pdf_notice_html(url, "This PDF has no pages.");
    }

    char *name = url ? g_path_get_basename(url) : NULL;
    if (name) {
        char *query = strchr(name, '?');
        if (query) *query = '\0';
    }
    char *esc_name = ns_html_escape_text(name && *name ? name : "PDF");
    g_free(name);

    GString *out = g_string_new(NULL);
    g_string_append_printf(out,
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<title>%s</title>%s</head><body><div class=\"doc\">",
        esc_name, ns_pdf_style);
    g_free(esc_name);

    int limit = n < NS_PDF_MAX_PAGES ? n : NS_PDF_MAX_PAGES;
    for (int i = 0; i < limit; i++) {
        PopplerPage *page = poppler_document_get_page(doc, i);
        if (!page)
            continue;
        char *uri = ns_pdf_page_data_uri(page);
        g_object_unref(page);
        if (!uri)
            continue;
        g_string_append_printf(out,
            "<div class=\"page\"><img src=\"%s\" alt=\"Page %d\"></div>",
            uri, i + 1);
        g_free(uri);
    }
    if (limit < n)
        g_string_append_printf(out,
            "<p class=\"note\">Showing the first %d of %d pages.</p>",
            limit, n);

    g_string_append(out, "</div></body></html>");
    g_object_unref(doc);
    return g_string_free(out, FALSE);
#else
    (void)data; (void)len;
    return ns_pdf_notice_html(
        url, "Inline PDF viewing is not available in this build.");
#endif
}
