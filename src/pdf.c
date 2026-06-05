/* Nordstjernen — embedded PDF rendering via poppler-glib.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "pdf.h"

#ifdef NS_HAVE_POPPLER
#include <poppler.h>
#endif

struct ns_pdf {
#ifdef NS_HAVE_POPPLER
    PopplerDocument *doc;
#else
    int unused;
#endif
};

gboolean
ns_pdf_available(void)
{
#ifdef NS_HAVE_POPPLER
    return TRUE;
#else
    return FALSE;
#endif
}

ns_pdf *
ns_pdf_new_from_bytes(const guint8 *data, gsize len)
{
#ifdef NS_HAVE_POPPLER
    if (!data || len == 0) return NULL;
    GBytes *bytes = g_bytes_new(data, len);
    GError *err = NULL;
    PopplerDocument *doc = poppler_document_new_from_bytes(bytes, NULL, &err);
    g_bytes_unref(bytes);
    if (!doc) {
        if (err) g_warning("ns_pdf: %s", err->message);
        g_clear_error(&err);
        return NULL;
    }
    g_clear_error(&err);
    ns_pdf *pdf = g_new0(ns_pdf, 1);
    pdf->doc = doc;
    return pdf;
#else
    (void)data; (void)len;
    return NULL;
#endif
}

void
ns_pdf_free(ns_pdf *pdf)
{
    if (!pdf) return;
#ifdef NS_HAVE_POPPLER
    if (pdf->doc) g_object_unref(pdf->doc);
#endif
    g_free(pdf);
}

int
ns_pdf_n_pages(const ns_pdf *pdf)
{
#ifdef NS_HAVE_POPPLER
    if (!pdf || !pdf->doc) return 0;
    return poppler_document_get_n_pages(pdf->doc);
#else
    (void)pdf;
    return 0;
#endif
}

#define NS_PDF_MARGIN     16.0
#define NS_PDF_PAGE_GAP   24.0
#define NS_PDF_SHADOW_OFF 4.0

void
ns_pdf_paint(ns_pdf *pdf, cairo_t *cr, double viewport_w,
             double *out_total_height)
{
    if (out_total_height) *out_total_height = 0;
    if (!cr) return;
#ifdef NS_HAVE_POPPLER
    if (!pdf || !pdf->doc) return;
    int n = poppler_document_get_n_pages(pdf->doc);
    if (n <= 0) return;
    double content_w = viewport_w - NS_PDF_MARGIN * 2;
    if (content_w < 100) content_w = 100;
    double y = NS_PDF_MARGIN;
    for (int i = 0; i < n; i++) {
        PopplerPage *page = poppler_document_get_page(pdf->doc, i);
        if (!page) continue;
        double pw, ph;
        poppler_page_get_size(page, &pw, &ph);
        double scale = pw > 0 ? content_w / pw : 1.0;
        double rendered_h = ph * scale;
        double x = NS_PDF_MARGIN;
        cairo_save(cr);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.18);
        cairo_rectangle(cr, x + NS_PDF_SHADOW_OFF, y + NS_PDF_SHADOW_OFF,
                        content_w, rendered_h);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_rectangle(cr, x, y, content_w, rendered_h);
        cairo_fill(cr);
        cairo_translate(cr, x, y);
        cairo_scale(cr, scale, scale);
        poppler_page_render(page, cr);
        cairo_restore(cr);
        cairo_save(cr);
        cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, 1.0);
        cairo_set_line_width(cr, 0.5);
        cairo_rectangle(cr, x, y, content_w, rendered_h);
        cairo_stroke(cr);
        cairo_restore(cr);
        y += rendered_h + NS_PDF_PAGE_GAP;
        g_object_unref(page);
    }
    y -= NS_PDF_PAGE_GAP;
    y += NS_PDF_MARGIN;
    if (out_total_height) *out_total_height = y;
#else
    (void)pdf; (void)viewport_w;
#endif
}
