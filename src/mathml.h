/* Nordstjernen — minimalist MathML presentation layout and paint.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_MATHML_H
#define NS_MATHML_H

#include <cairo.h>
#include <glib.h>

#include "dom.h"

G_BEGIN_DECLS

void ns_math_measure(const ns_node *math, double font_px,
                     double *out_w, double *out_ascent, double *out_descent);

void ns_math_paint(cairo_t *cr, const ns_node *math, double x, double y,
                   double font_px, double r, double g, double b, double a);

G_END_DECLS

#endif
