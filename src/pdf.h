/* Nordstjernen — embedded PDF rendering via poppler-glib.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_PDF_H
#define NS_PDF_H

#include <cairo.h>
#include <glib.h>

G_BEGIN_DECLS

typedef struct ns_pdf ns_pdf;

gboolean ns_pdf_available(void);

ns_pdf *ns_pdf_new_from_bytes(const guint8 *data, gsize len);
void    ns_pdf_free(ns_pdf *pdf);

int     ns_pdf_n_pages(const ns_pdf *pdf);

void    ns_pdf_paint(ns_pdf *pdf, cairo_t *cr, double viewport_w,
                     double *out_total_height);

G_END_DECLS

#endif
