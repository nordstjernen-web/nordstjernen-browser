/* Nordstjernen — CSS filter function lists: computed values, interpolation and composition. */

#ifndef NS_CSS_FILTER_H
#define NS_CSS_FILTER_H

#include <glib.h>
#include "css_shape.h"

G_BEGIN_DECLS

char *ns_css_filter_computed(const char *text, const ns_shape_ctx *cx, const guint8 *current);
char *ns_css_filter_interpolate(const char *from, const char *to, double t,
                                const ns_shape_ctx *cx);
char *ns_css_filter_composite(const char *underlying, const char *v, gboolean accumulate,
                              const ns_shape_ctx *cx);

G_END_DECLS

#endif
