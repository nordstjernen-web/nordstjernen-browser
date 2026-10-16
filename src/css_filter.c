/* Nordstjernen — CSS filter function lists: computed values, interpolation and composition.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-or-later
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include "css_filter.h"
#include "css_color.h"
#include "css_syntax.h"

enum {
    F_BLUR, F_BRIGHTNESS, F_CONTRAST, F_DROP_SHADOW, F_GRAYSCALE, F_HUE_ROTATE,
    F_INVERT, F_OPACITY, F_SATURATE, F_SEPIA, F_URL,
};

static const char *const filter_names[] = {
    "blur", "brightness", "contrast", "drop-shadow", "grayscale", "hue-rotate",
    "invert", "opacity", "saturate", "sepia", "url",
};

typedef struct {
    int fn;
    double v;
    double x, y, blur;
    double rgba[4];
    char *url;
} filter_fn;

static void
fn_clear(gpointer p)
{
    g_free(((filter_fn *)p)->url);
}

static GArray *
fn_list_new(void)
{
    GArray *a = g_array_new(FALSE, TRUE, sizeof(filter_fn));
    g_array_set_clear_func(a, fn_clear);
    return a;
}

static gboolean
is_unit_amount(int fn)
{
    return fn == F_GRAYSCALE || fn == F_INVERT || fn == F_OPACITY || fn == F_SEPIA;
}

static double
identity_amount(int fn)
{
    return fn == F_BRIGHTNESS || fn == F_CONTRAST || fn == F_OPACITY || fn == F_SATURATE ? 1 : 0;
}

static const ns_css_component *
next_token(GPtrArray *list, guint *i)
{
    while (list && *i < list->len) {
        const ns_css_component *t = g_ptr_array_index(list, *i);
        if (t->type != NS_CSS_COMPONENT_WHITESPACE) return t;
        (*i)++;
    }
    return NULL;
}

static gboolean
token_angle(const ns_css_component *t, double *deg)
{
    if (t->type == NS_CSS_COMPONENT_NUMBER && t->number == 0) {
        *deg = 0;
        return TRUE;
    }
    if (t->type != NS_CSS_COMPONENT_DIMENSION || !t->value) return FALSE;
    if (g_ascii_strcasecmp(t->value, "deg") == 0) *deg = t->number;
    else if (g_ascii_strcasecmp(t->value, "rad") == 0) *deg = t->number * 180.0 / G_PI;
    else if (g_ascii_strcasecmp(t->value, "grad") == 0) *deg = t->number * 0.9;
    else if (g_ascii_strcasecmp(t->value, "turn") == 0) *deg = t->number * 360.0;
    else return FALSE;
    return TRUE;
}

static gboolean
token_length(const ns_css_component *t, const ns_shape_ctx *cx, double *px)
{
    double num, pct;
    gboolean dim;
    if (t->type == NS_CSS_COMPONENT_NUMBER && t->number == 0) {
        *px = 0;
        return TRUE;
    }
    if (!ns_css_shape_eval_math(t, cx, &num, &pct, px, &dim)) return FALSE;
    return dim && pct == 0;
}

static gboolean
token_amount(const ns_css_component *t, const ns_shape_ctx *cx, double *v)
{
    double num, pct, px;
    gboolean dim;
    if (!ns_css_shape_eval_math(t, cx, &num, &pct, &px, &dim) || px != 0) return FALSE;
    *v = num + pct / 100.0;
    return TRUE;
}

static gboolean
parse_drop_shadow(const char *text, const ns_css_component *f, const ns_shape_ctx *cx,
                  const guint8 *current, filter_fn *out)
{
    double len[3] = { 0, 0, 0 };
    int n_len = 0;
    char *color = NULL;
    gboolean lengths_closed = FALSE, ok = TRUE;
    guint i = 0;
    const ns_css_component *t;
    while (ok && (t = next_token(f->children, &i))) {
        double px;
        if (token_length(t, cx, &px)) {
            ok = n_len < 3 && !lengths_closed;
            if (ok) len[n_len++] = px;
        } else {
            ok = !color;
            if (ok) color = g_strndup(text + t->start, t->end - t->start);
            if (n_len > 0) lengths_closed = TRUE;
        }
        i++;
    }
    if (ok && (n_len < 2 || len[2] < 0)) ok = FALSE;
    guint8 rgba[4] = { 0, 0, 0, 255 };
    if (current) memcpy(rgba, current, 4);
    if (ok && color) ok = ns_css_color_resolve(color, current, rgba);
    g_free(color);
    if (!ok) return FALSE;
    out->x = len[0];
    out->y = len[1];
    out->blur = len[2];
    for (int k = 0; k < 4; k++) out->rgba[k] = rgba[k];
    return TRUE;
}

static gboolean
parse_fn(const char *text, const ns_css_component *f, const ns_shape_ctx *cx,
         const guint8 *current, filter_fn *out)
{
    memset(out, 0, sizeof *out);
    out->fn = -1;
    for (int i = 0; i < (int)G_N_ELEMENTS(filter_names); i++)
        if (g_ascii_strcasecmp(f->value, filter_names[i]) == 0) out->fn = i;
    if (out->fn < 0) return FALSE;
    if (out->fn == F_URL) {
        out->url = g_strndup(text + f->start, f->end - f->start);
        return TRUE;
    }
    if (out->fn == F_DROP_SHADOW) return parse_drop_shadow(text, f, cx, current, out);
    guint i = 0;
    const ns_css_component *t = next_token(f->children, &i);
    if (!t) {
        out->v = out->fn == F_BLUR || out->fn == F_HUE_ROTATE ? 0 : 1;
        return TRUE;
    }
    gboolean ok;
    if (out->fn == F_BLUR) ok = token_length(t, cx, &out->v);
    else if (out->fn == F_HUE_ROTATE) ok = token_angle(t, &out->v);
    else ok = token_amount(t, cx, &out->v);
    i++;
    if (!ok || next_token(f->children, &i)) return FALSE;
    if (out->fn != F_HUE_ROTATE && out->v < 0) out->v = 0;
    if (is_unit_amount(out->fn) && out->v > 1) out->v = 1;
    return TRUE;
}

static GArray *
parse_list(const char *text, const ns_shape_ctx *cx, const guint8 *current, gboolean *none)
{
    *none = FALSE;
    if (!text) return NULL;
    gboolean valid = TRUE;
    GPtrArray *list = ns_css_component_values_parse(text, -1, &valid);
    GArray *out = fn_list_new();
    guint i = 0;
    const ns_css_component *t = next_token(list, &i);
    if (valid && t && t->type == NS_CSS_COMPONENT_IDENT && t->value &&
        g_ascii_strcasecmp(t->value, "none") == 0) {
        i++;
        *none = TRUE;
        valid = next_token(list, &i) == NULL;
    } else {
        while (valid && (t = next_token(list, &i))) {
            filter_fn fn;
            valid = t->type == NS_CSS_COMPONENT_FUNCTION && t->value &&
                    parse_fn(text, t, cx, current, &fn);
            if (valid) g_array_append_val(out, fn);
            i++;
        }
        valid = valid && out->len > 0;
    }
    g_ptr_array_free(list, TRUE);
    if (!valid) {
        g_array_free(out, TRUE);
        return NULL;
    }
    return out;
}

static void
fmt_num(GString *s, double v)
{
    if (fabs(v) < 1e-9) v = 0;
    char buf[64];
    g_ascii_formatd(buf, sizeof buf, "%.6g", v);
    g_string_append(s, buf);
}

static void
fmt_color(GString *s, const double *rgba)
{
    int c[3];
    for (int k = 0; k < 3; k++) c[k] = (int)CLAMP(lround(rgba[k]), 0, 255);
    double a = CLAMP(rgba[3], 0, 255) / 255.0;
    if (a >= 1) {
        g_string_append_printf(s, "rgb(%d, %d, %d)", c[0], c[1], c[2]);
        return;
    }
    g_string_append_printf(s, "rgba(%d, %d, %d, ", c[0], c[1], c[2]);
    char buf[32];
    g_ascii_formatd(buf, sizeof buf, "%.3g", round(a * 1000) / 1000);
    g_string_append(s, buf);
    g_string_append_c(s, ')');
}

static void
fmt_url(GString *s, const char *raw)
{
    const char *open = strchr(raw, '(');
    const char *close = strrchr(raw, ')');
    if (!open || !close || close < open) {
        g_string_append(s, raw);
        return;
    }
    char *inner = g_strstrip(g_strndup(open + 1, close - open - 1));
    if (inner[0] == '"' || inner[0] == '\'') {
        gsize n = strlen(inner);
        if (n >= 2) memmove(inner, inner + 1, n - 2), inner[n - 2] = '\0';
    }
    g_string_append(s, "url(\"");
    for (const char *p = inner; *p; p++) {
        if (*p == '"' || *p == '\\') g_string_append_c(s, '\\');
        g_string_append_c(s, *p);
    }
    g_string_append(s, "\")");
    g_free(inner);
}

static void
fmt_list(GString *s, const GArray *fns)
{
    for (guint i = 0; i < fns->len; i++) {
        const filter_fn *f = &g_array_index(fns, filter_fn, i);
        if (i) g_string_append_c(s, ' ');
        if (f->fn == F_URL) {
            fmt_url(s, f->url);
            continue;
        }
        g_string_append(s, filter_names[f->fn]);
        g_string_append_c(s, '(');
        switch (f->fn) {
        case F_BLUR:
            fmt_num(s, MAX(f->v, 0));
            g_string_append(s, "px");
            break;
        case F_HUE_ROTATE:
            fmt_num(s, f->v);
            g_string_append(s, "deg");
            break;
        case F_DROP_SHADOW:
            fmt_color(s, f->rgba);
            g_string_append_c(s, ' ');
            fmt_num(s, f->x);
            g_string_append(s, "px ");
            fmt_num(s, f->y);
            g_string_append(s, "px ");
            fmt_num(s, MAX(f->blur, 0));
            g_string_append(s, "px");
            break;
        default:
            fmt_num(s, is_unit_amount(f->fn) ? CLAMP(f->v, 0, 1) : MAX(f->v, 0));
        }
        g_string_append_c(s, ')');
    }
}

char *
ns_css_filter_computed(const char *text, const ns_shape_ctx *cx, const guint8 *current)
{
    gboolean none;
    GArray *fns = parse_list(text, cx, current, &none);
    if (!fns) return NULL;
    GString *s = g_string_new(NULL);
    if (none) g_string_append(s, "none");
    else fmt_list(s, fns);
    g_array_free(fns, TRUE);
    return g_string_free(s, FALSE);
}

static filter_fn
identity_of(const filter_fn *f)
{
    filter_fn id;
    memset(&id, 0, sizeof id);
    id.fn = f->fn;
    id.v = identity_amount(f->fn);
    return id;
}

static void
mix_fn(const filter_fn *a, const filter_fn *b, double wa, double wb, filter_fn *o)
{
    memset(o, 0, sizeof *o);
    o->fn = a->fn;
    o->v = a->v * wa + b->v * wb;
    o->x = a->x * wa + b->x * wb;
    o->y = a->y * wa + b->y * wb;
    o->blur = a->blur * wa + b->blur * wb;
    double alpha = a->rgba[3] * wa + b->rgba[3] * wb;
    for (int k = 0; k < 3; k++) {
        double premul = a->rgba[k] * a->rgba[3] * wa + b->rgba[k] * b->rgba[3] * wb;
        o->rgba[k] = alpha > 0 ? premul / alpha : 0;
    }
    o->rgba[3] = alpha;
}

static gboolean
lists_pair(const GArray *a, gboolean a_none, const GArray *b, gboolean b_none,
           GArray *pa, GArray *pb)
{
    if (a_none && b_none) return FALSE;
    guint n = MAX(a->len, b->len);
    for (guint i = 0; i < n; i++) {
        const filter_fn *x = i < a->len ? &g_array_index(a, filter_fn, i) : NULL;
        const filter_fn *y = i < b->len ? &g_array_index(b, filter_fn, i) : NULL;
        if ((x && x->fn == F_URL) || (y && y->fn == F_URL)) return FALSE;
        if (x && y && x->fn != y->fn) return FALSE;
        filter_fn fx, fy;
        if (x && y) {
            fx = *x;
            fy = *y;
        } else if (x) {
            fx = *x;
            fy = identity_of(x);
        } else if (y) {
            fx = identity_of(y);
            fy = *y;
        } else {
            break;
        }
        fx.url = fy.url = NULL;
        g_array_append_val(pa, fx);
        g_array_append_val(pb, fy);
    }
    return TRUE;
}

static char *
mix_lists(const char *from, const char *to, double wa, double wb, const ns_shape_ctx *cx)
{
    gboolean an, bn;
    GArray *a = parse_list(from, cx, NULL, &an);
    GArray *b = parse_list(to, cx, NULL, &bn);
    char *result = NULL;
    if (a && b) {
        GArray *pa = fn_list_new(), *pb = fn_list_new();
        if (lists_pair(a, an, b, bn, pa, pb)) {
            GArray *mix = fn_list_new();
            for (guint i = 0; i < pa->len; i++) {
                filter_fn o;
                mix_fn(&g_array_index(pa, filter_fn, i), &g_array_index(pb, filter_fn, i),
                       wa, wb, &o);
                g_array_append_val(mix, o);
            }
            GString *s = g_string_new(NULL);
            fmt_list(s, mix);
            result = g_string_free(s, FALSE);
            g_array_free(mix, TRUE);
        }
        g_array_free(pa, TRUE);
        g_array_free(pb, TRUE);
    }
    if (a) g_array_free(a, TRUE);
    if (b) g_array_free(b, TRUE);
    return result;
}

char *
ns_css_filter_interpolate(const char *from, const char *to, double t, const ns_shape_ctx *cx)
{
    return mix_lists(from, to, 1 - t, t, cx);
}

static char *
add_lists(const char *underlying, const char *v, const ns_shape_ctx *cx)
{
    gboolean un, vn;
    GArray *u = parse_list(underlying, cx, NULL, &un);
    GArray *a = parse_list(v, cx, NULL, &vn);
    char *result = NULL;
    if (u && a) {
        GString *s = g_string_new(NULL);
        if (un && vn) {
            g_string_append(s, "none");
        } else {
            GArray *all = fn_list_new();
            for (guint i = 0; i < u->len; i++) {
                filter_fn f = g_array_index(u, filter_fn, i);
                f.url = g_strdup(f.url);
                g_array_append_val(all, f);
            }
            for (guint i = 0; i < a->len; i++) {
                filter_fn f = g_array_index(a, filter_fn, i);
                f.url = g_strdup(f.url);
                g_array_append_val(all, f);
            }
            fmt_list(s, all);
            g_array_free(all, TRUE);
        }
        result = g_string_free(s, FALSE);
    }
    if (u) g_array_free(u, TRUE);
    if (a) g_array_free(a, TRUE);
    return result;
}

static char *
accumulate_lists(const char *underlying, const char *v, const ns_shape_ctx *cx)
{
    gboolean un, vn;
    GArray *u = parse_list(underlying, cx, NULL, &un);
    GArray *a = parse_list(v, cx, NULL, &vn);
    char *result = NULL;
    if (u && a) {
        GArray *pu = fn_list_new(), *pa = fn_list_new();
        if (lists_pair(u, un, a, vn, pu, pa)) {
            GArray *sum = fn_list_new();
            for (guint i = 0; i < pu->len; i++) {
                const filter_fn *x = &g_array_index(pu, filter_fn, i);
                const filter_fn *y = &g_array_index(pa, filter_fn, i);
                filter_fn o;
                mix_fn(x, y, 1, 1, &o);
                o.v -= identity_amount(x->fn);
                for (int k = 0; k < 4; k++) o.rgba[k] = MIN(x->rgba[k] + y->rgba[k], 255);
                g_array_append_val(sum, o);
            }
            GString *s = g_string_new(NULL);
            fmt_list(s, sum);
            result = g_string_free(s, FALSE);
            g_array_free(sum, TRUE);
        }
        g_array_free(pu, TRUE);
        g_array_free(pa, TRUE);
    }
    if (u) g_array_free(u, TRUE);
    if (a) g_array_free(a, TRUE);
    return result;
}

char *
ns_css_filter_composite(const char *underlying, const char *v, gboolean accumulate,
                        const ns_shape_ctx *cx)
{
    return accumulate ? accumulate_lists(underlying, v, cx) : add_lists(underlying, v, cx);
}
