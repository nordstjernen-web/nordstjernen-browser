/* Nordstjernen — CSS Anchor Positioning values: anchor(), anchor-size(), position-area and the anchor properties.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-or-later
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include "css_anchor.h"
#include "css_syntax.h"

typedef enum { AN_NUM, AN_ANCHOR, AN_SIZE, AN_SUM, AN_PRODUCT, AN_MIN, AN_MAX, AN_CLAMP } an_kind;
typedef enum { AT_NUMBER, AT_LENGTH } an_type;

typedef struct an_node {
    an_kind kind;
    double v;
    char unit[8];
    char *name;
    char *side;
    struct an_node *side_pct;
    gboolean side_math;
    char *size;
    struct an_node *fallback;
    GPtrArray *kids;
    gboolean neg;
    gboolean inv;
} an_node;

typedef struct {
    GPtrArray *list;
    guint pos;
    guint end;
} an_cursor;

static void
an_free(an_node *n)
{
    if (!n) return;
    g_free(n->name);
    g_free(n->side);
    g_free(n->size);
    an_free(n->side_pct);
    an_free(n->fallback);
    if (n->kids) g_ptr_array_free(n->kids, TRUE);
    g_free(n);
}

static an_node *
an_new(an_kind kind)
{
    an_node *n = g_new0(an_node, 1);
    n->kind = kind;
    if (kind == AN_SUM || kind == AN_PRODUCT || kind == AN_MIN || kind == AN_MAX ||
        kind == AN_CLAMP)
        n->kids = g_ptr_array_new_with_free_func((GDestroyNotify)an_free);
    return n;
}

static const ns_css_component *
cur_peek(an_cursor *c)
{
    while (c->pos < c->end) {
        const ns_css_component *t = g_ptr_array_index(c->list, c->pos);
        if (t->type != NS_CSS_COMPONENT_WHITESPACE) return t;
        c->pos++;
    }
    return NULL;
}

static gboolean
cur_ws_before(an_cursor *c)
{
    return c->pos > 0 && c->pos <= c->end &&
           ((const ns_css_component *)g_ptr_array_index(c->list, c->pos - 1))->type ==
               NS_CSS_COMPONENT_WHITESPACE;
}

static gboolean
cur_ws_after(an_cursor *c)
{
    return c->pos + 1 < c->end &&
           ((const ns_css_component *)g_ptr_array_index(c->list, c->pos + 1))->type ==
               NS_CSS_COMPONENT_WHITESPACE;
}

static gboolean
name_is(const ns_css_component *t, const char *want)
{
    return t && t->value && g_ascii_strcasecmp(t->value, want) == 0;
}

static gboolean
is_dashed_ident(const ns_css_component *t)
{
    return t && t->type == NS_CSS_COMPONENT_IDENT && t->value &&
           t->value[0] == '-' && t->value[1] == '-' && t->value[2];
}

typedef struct {
    gboolean allow_anchor;
    gboolean allow_size;
    int depth;
} an_ctx;

static an_node *parse_sum(an_cursor *c, an_ctx *cx, an_type *type);

static gboolean
unit_is_length(const char *u)
{
    static const char *const units[] = {
        "px", "cm", "mm", "q", "in", "pt", "pc", "em", "rem", "ex", "rex", "ch",
        "rch", "cap", "rcap", "ic", "ric", "lh", "rlh", "vw", "vh", "vi", "vb",
        "vmin", "vmax", "svw", "svh", "svi", "svb", "svmin", "svmax", "lvw",
        "lvh", "lvi", "lvb", "lvmin", "lvmax", "dvw", "dvh", "dvi", "dvb",
        "dvmin", "dvmax", "cqw", "cqh", "cqi", "cqb", "cqmin", "cqmax",
    };
    for (gsize i = 0; i < G_N_ELEMENTS(units); i++)
        if (g_ascii_strcasecmp(u, units[i]) == 0) return TRUE;
    return FALSE;
}

static an_cursor
sub_cursor(const ns_css_component *f)
{
    an_cursor c = { f->children, 0, f->children ? f->children->len : 0 };
    return c;
}

static gboolean
split_args(const ns_css_component *f, GArray *bounds)
{
    guint start = 0;
    guint n = f->children ? f->children->len : 0;
    for (guint i = 0; i <= n; i++) {
        gboolean at_end = i == n;
        const ns_css_component *t = at_end ? NULL : g_ptr_array_index(f->children, i);
        if (at_end || t->type == NS_CSS_COMPONENT_COMMA) {
            guint pair[2] = { start, i };
            g_array_append_vals(bounds, pair, 2);
            start = i + 1;
        }
    }
    return TRUE;
}

static gboolean
range_empty(GPtrArray *list, guint a, guint b)
{
    for (guint i = a; i < b; i++)
        if (((const ns_css_component *)g_ptr_array_index(list, i))->type !=
            NS_CSS_COMPONENT_WHITESPACE)
            return FALSE;
    return TRUE;
}

static an_node *
parse_fallback(GPtrArray *list, guint a, guint b, an_ctx *cx)
{
    an_cursor c = { list, a, b };
    an_type type;
    an_node *n = parse_sum(&c, cx, &type);
    if (!n || cur_peek(&c) || type != AT_LENGTH) {
        if (n && type == AT_NUMBER && n->kind == AN_NUM && n->v == 0 && !cur_peek(&c)) {
            strcpy(n->unit, "px");
            return n;
        }
        an_free(n);
        return NULL;
    }
    return n;
}

static an_node *
parse_anchor_fn(const ns_css_component *f, an_ctx *cx, gboolean size_fn)
{
    GArray *b = g_array_new(FALSE, FALSE, sizeof(guint));
    split_args(f, b);
    guint nargs = b->len / 2;
    an_node *n = an_new(size_fn ? AN_SIZE : AN_ANCHOR);
    gboolean ok = nargs >= 1 && nargs <= 2;
    guint a0 = ok ? g_array_index(b, guint, 0) : 0;
    guint a1 = ok ? g_array_index(b, guint, 1) : 0;
    if (ok && nargs == 2) {
        guint f0 = g_array_index(b, guint, 2), f1 = g_array_index(b, guint, 3);
        if (range_empty(f->children, f0, f1)) ok = FALSE;
        else {
            n->fallback = parse_fallback(f->children, f0, f1, cx);
            if (!n->fallback) ok = FALSE;
        }
    }
    if (ok) {
        an_cursor c = { f->children, a0, a1 };
        int seen = 0;
        const ns_css_component *t;
        while (ok && (t = cur_peek(&c))) {
            if (is_dashed_ident(t) && !n->name) {
                n->name = g_strdup(t->value);
                c.pos++;
            } else if (t->type == NS_CSS_COMPONENT_IDENT && !size_fn && !n->side && !n->side_pct) {
                static const char *const sides[] = {
                    "inside", "outside", "top", "left", "right", "bottom", "start",
                    "end", "self-start", "self-end", "center",
                };
                gboolean known = FALSE;
                for (gsize i = 0; i < G_N_ELEMENTS(sides); i++)
                    if (name_is(t, sides[i])) known = TRUE;
                if (!known) { ok = FALSE; break; }
                n->side = g_ascii_strdown(t->value, -1);
                c.pos++;
            } else if (t->type == NS_CSS_COMPONENT_IDENT && size_fn && !n->size) {
                static const char *const sizes[] = {
                    "width", "height", "block", "inline", "self-block", "self-inline",
                };
                gboolean known = FALSE;
                for (gsize i = 0; i < G_N_ELEMENTS(sizes); i++)
                    if (name_is(t, sizes[i])) known = TRUE;
                if (!known) { ok = FALSE; break; }
                n->size = g_ascii_strdown(t->value, -1);
                c.pos++;
            } else if (!size_fn && !n->side && !n->side_pct &&
                       (t->type == NS_CSS_COMPONENT_PERCENTAGE ||
                        t->type == NS_CSS_COMPONENT_FUNCTION)) {
                an_cursor pc = { f->children, c.pos, c.pos + 1 };
                an_ctx sub = { FALSE, FALSE, cx->depth + 1 };
                an_type pt;
                an_node *p = parse_sum(&pc, &sub, &pt);
                if (!p || pt != AT_LENGTH || p->kind == AN_ANCHOR || p->kind == AN_SIZE ||
                    (p->kind == AN_NUM && strcmp(p->unit, "%") != 0)) {
                    an_free(p);
                    ok = FALSE;
                    break;
                }
                n->side_pct = p;
                n->side_math = t->type == NS_CSS_COMPONENT_FUNCTION;
                c.pos++;
            } else if (size_fn && nargs == 1 && seen == 0 && !n->name && !n->size &&
                       t->type != NS_CSS_COMPONENT_IDENT) {
                n->fallback = parse_fallback(f->children, a0, a1, cx);
                ok = n->fallback != NULL;
                c.pos = a1;
            } else {
                ok = FALSE;
            }
            seen++;
        }
        if (ok && !size_fn && !n->side && !n->side_pct) ok = FALSE;
        if (ok && nargs == 2 && seen == 0) ok = FALSE;
    }
    g_array_free(b, TRUE);
    if (!ok) {
        an_free(n);
        return NULL;
    }
    return n;
}

static an_node *
parse_value(an_cursor *c, an_ctx *cx, an_type *type)
{
    if (cx->depth > 32) return NULL;
    const ns_css_component *t = cur_peek(c);
    if (!t) return NULL;
    if (t->type == NS_CSS_COMPONENT_NUMBER || t->type == NS_CSS_COMPONENT_PERCENTAGE ||
        t->type == NS_CSS_COMPONENT_DIMENSION) {
        c->pos++;
        an_node *n = an_new(AN_NUM);
        n->v = t->number;
        if (t->type == NS_CSS_COMPONENT_PERCENTAGE) {
            strcpy(n->unit, "%");
            *type = AT_LENGTH;
        } else if (t->type == NS_CSS_COMPONENT_DIMENSION) {
            if (!unit_is_length(t->value) || strlen(t->value) >= sizeof n->unit) {
                an_free(n);
                return NULL;
            }
            for (int i = 0; t->value[i]; i++) n->unit[i] = g_ascii_tolower(t->value[i]);
            *type = AT_LENGTH;
        } else {
            *type = AT_NUMBER;
        }
        return n;
    }
    if (t->type == NS_CSS_COMPONENT_BLOCK && t->delimiter == ')') {
        c->pos++;
        an_cursor sc = sub_cursor(t);
        an_ctx sub = *cx;
        sub.depth++;
        an_node *n = parse_sum(&sc, &sub, type);
        if (n && cur_peek(&sc)) {
            an_free(n);
            return NULL;
        }
        return n;
    }
    if (t->type != NS_CSS_COMPONENT_FUNCTION) return NULL;
    c->pos++;
    an_ctx sub = *cx;
    sub.depth++;
    if (name_is(t, "anchor") || name_is(t, "anchor-size")) {
        gboolean size_fn = name_is(t, "anchor-size");
        if (size_fn ? !cx->allow_size : !cx->allow_anchor) return NULL;
        *type = AT_LENGTH;
        return parse_anchor_fn(t, &sub, size_fn);
    }
    if (name_is(t, "calc")) {
        an_cursor sc = sub_cursor(t);
        an_node *n = parse_sum(&sc, &sub, type);
        if (n && cur_peek(&sc)) {
            an_free(n);
            return NULL;
        }
        return n;
    }
    an_kind kind = name_is(t, "min") ? AN_MIN : name_is(t, "max") ? AN_MAX
                 : name_is(t, "clamp") ? AN_CLAMP : AN_NUM;
    if (kind == AN_NUM) return NULL;
    GArray *b = g_array_new(FALSE, FALSE, sizeof(guint));
    split_args(t, b);
    an_node *n = an_new(kind);
    gboolean ok = b->len / 2 >= 1 && (kind != AN_CLAMP || b->len / 2 == 3);
    an_type first = AT_LENGTH;
    for (guint i = 0; ok && i < b->len / 2; i++) {
        an_cursor ac = { t->children, g_array_index(b, guint, 2 * i),
                         g_array_index(b, guint, 2 * i + 1) };
        an_type at;
        an_node *k = parse_sum(&ac, &sub, &at);
        if (!k || cur_peek(&ac)) {
            an_free(k);
            ok = FALSE;
            break;
        }
        if (i == 0) first = at;
        else if (at != first) ok = FALSE;
        g_ptr_array_add(n->kids, k);
    }
    g_array_free(b, TRUE);
    if (!ok) {
        an_free(n);
        return NULL;
    }
    *type = first;
    return n;
}

static an_node *
an_drop(an_node *first, an_node *group, an_node *rhs)
{
    an_free(rhs);
    an_free(group ? group : first);
    return NULL;
}

static an_node *
parse_product(an_cursor *c, an_ctx *cx, an_type *type)
{
    an_node *first = parse_value(c, cx, type);
    if (!first) return NULL;
    an_node *prod = NULL;
    for (;;) {
        const ns_css_component *t = cur_peek(c);
        if (!t || t->type != NS_CSS_COMPONENT_DELIM ||
            (t->delimiter != '*' && t->delimiter != '/'))
            break;
        gboolean div = t->delimiter == '/';
        c->pos++;
        an_type rt;
        an_node *rhs = parse_value(c, cx, &rt);
        if (!rhs) return an_drop(first, prod, NULL);
        if (div) {
            if (rt != AT_NUMBER) return an_drop(first, prod, rhs);
            rhs->inv = TRUE;
        } else if (rt == AT_LENGTH) {
            if (*type == AT_LENGTH) return an_drop(first, prod, rhs);
            *type = AT_LENGTH;
        }
        if (!prod) {
            prod = an_new(AN_PRODUCT);
            g_ptr_array_add(prod->kids, first);
        }
        g_ptr_array_add(prod->kids, rhs);
    }
    return prod ? prod : first;
}

static an_node *
parse_sum(an_cursor *c, an_ctx *cx, an_type *type)
{
    an_node *first = parse_product(c, cx, type);
    if (!first) return NULL;
    an_node *sum = NULL;
    for (;;) {
        const ns_css_component *t = cur_peek(c);
        if (!t || t->type != NS_CSS_COMPONENT_DELIM ||
            (t->delimiter != '+' && t->delimiter != '-'))
            break;
        if (!cur_ws_before(c) || !cur_ws_after(c)) return an_drop(first, sum, NULL);
        gboolean minus = t->delimiter == '-';
        c->pos++;
        an_type rt;
        an_node *rhs = parse_product(c, cx, &rt);
        if (!rhs || rt != *type) return an_drop(first, sum, rhs);
        rhs->neg = minus;
        if (!sum) {
            sum = an_new(AN_SUM);
            g_ptr_array_add(sum->kids, first);
        }
        g_ptr_array_add(sum->kids, rhs);
    }
    return sum ? sum : first;
}

static gboolean
contains_anchor(const an_node *n)
{
    if (!n) return FALSE;
    if (n->kind == AN_ANCHOR || n->kind == AN_SIZE) return TRUE;
    for (guint i = 0; n->kids && i < n->kids->len; i++)
        if (contains_anchor(g_ptr_array_index(n->kids, i))) return TRUE;
    return FALSE;
}

static an_node *simplify(an_node *n);

static void
take_kids(an_node *dst, an_node *src, gboolean neg, gboolean inv)
{
    for (guint i = 0; i < src->kids->len; i++) {
        an_node *k = src->kids->pdata[i];
        src->kids->pdata[i] = NULL;
        if (neg) k->neg = !k->neg;
        if (inv) k->inv = !k->inv;
        g_ptr_array_add(dst->kids, k);
    }
}

static int
sort_rank(const an_node *n)
{
    if (n->kind != AN_NUM) return 3;
    if (!n->unit[0]) return 0;
    if (strcmp(n->unit, "%") == 0) return 1;
    return 2;
}

static gint
kid_cmp(gconstpointer a, gconstpointer b)
{
    const an_node *x = *(const an_node *const *)a, *y = *(const an_node *const *)b;
    int rx = sort_rank(x), ry = sort_rank(y);
    if (rx != ry) return rx - ry;
    if (rx == 2) return strcmp(x->unit, y->unit);
    return 0;
}

static void
stable_sort(GPtrArray *kids)
{
    for (guint i = 1; i < kids->len; i++) {
        gpointer cur = kids->pdata[i];
        guint j = i;
        while (j > 0 && kid_cmp(&kids->pdata[j - 1], &cur) > 0) {
            kids->pdata[j] = kids->pdata[j - 1];
            j--;
        }
        kids->pdata[j] = cur;
    }
}

static an_node *
simplify(an_node *n)
{
    if (!n) return NULL;
    if (n->kind == AN_ANCHOR || n->kind == AN_SIZE) {
        n->fallback = simplify(n->fallback);
        n->side_pct = simplify(n->side_pct);
        return n;
    }
    if (n->kind == AN_NUM) {
        if (n->neg) {
            n->v = -n->v;
            n->neg = FALSE;
        }
        if (n->inv && !n->unit[0] && n->v != 0) {
            n->v = 1.0 / n->v;
            n->inv = FALSE;
        }
        return n;
    }
    for (guint i = 0; i < n->kids->len; i++)
        n->kids->pdata[i] = simplify(n->kids->pdata[i]);
    if (n->kind == AN_SUM) {
        an_node *flat = an_new(AN_SUM);
        flat->neg = n->neg;
        flat->inv = n->inv;
        for (guint i = 0; i < n->kids->len; i++) {
            an_node *k = n->kids->pdata[i];
            if (k->kind == AN_SUM && !k->inv) {
                take_kids(flat, k, k->neg, FALSE);
                continue;
            }
            n->kids->pdata[i] = NULL;
            g_ptr_array_add(flat->kids, k);
        }
        an_free(n);
        n = flat;
        for (guint i = 0; i < n->kids->len; i++) {
            an_node *k = n->kids->pdata[i];
            if (k->kind == AN_NUM && k->neg) {
                k->v = -k->v;
                k->neg = FALSE;
            }
        }
        for (guint i = 0; i < n->kids->len; i++) {
            an_node *a = n->kids->pdata[i];
            if (a->kind != AN_NUM) continue;
            for (guint j = i + 1; j < n->kids->len;) {
                an_node *b = n->kids->pdata[j];
                if (b->kind == AN_NUM && strcmp(a->unit, b->unit) == 0) {
                    a->v += b->v;
                    g_ptr_array_remove_index(n->kids, j);
                } else {
                    j++;
                }
            }
        }
        stable_sort(n->kids);
        if (n->kids->len == 1 && !n->neg && !n->inv) {
            an_node *only = n->kids->pdata[0];
            n->kids->pdata[0] = NULL;
            an_free(n);
            return only;
        }
        return n;
    }
    if (n->kind == AN_PRODUCT) {
        an_node *flat = an_new(AN_PRODUCT);
        flat->neg = n->neg;
        flat->inv = n->inv;
        double coeff = 1.0;
        gboolean has_coeff = FALSE;
        for (guint i = 0; i < n->kids->len; i++) {
            an_node *k = n->kids->pdata[i];
            if (k->kind == AN_PRODUCT && !k->neg) {
                take_kids(flat, k, FALSE, k->inv);
                continue;
            }
            n->kids->pdata[i] = NULL;
            g_ptr_array_add(flat->kids, k);
        }
        an_free(n);
        n = flat;
        for (guint i = 0; i < n->kids->len;) {
            an_node *k = n->kids->pdata[i];
            if (k->kind == AN_NUM && !k->unit[0]) {
                double v = k->v;
                if (k->inv) v = v != 0 ? 1.0 / v : INFINITY;
                coeff *= v;
                has_coeff = TRUE;
                g_ptr_array_remove_index(n->kids, i);
                continue;
            }
            i++;
        }
        if (n->kids->len == 1) {
            an_node *only = n->kids->pdata[0];
            if (only->kind == AN_NUM && !only->inv) {
                n->kids->pdata[0] = NULL;
                an_free(n);
                only->v *= coeff;
                return only;
            }
            if (only->kind == AN_SUM) {
                gboolean all_num = TRUE;
                for (guint i = 0; i < only->kids->len; i++)
                    if (((an_node *)only->kids->pdata[i])->kind != AN_NUM) all_num = FALSE;
                if (all_num) {
                    n->kids->pdata[0] = NULL;
                    an_free(n);
                    for (guint i = 0; i < only->kids->len; i++)
                        ((an_node *)only->kids->pdata[i])->v *= coeff;
                    return only;
                }
            }
            if (!has_coeff || coeff == 1.0) {
                n->kids->pdata[0] = NULL;
                an_free(n);
                return only;
            }
        }
        if (has_coeff) {
            an_node *num = an_new(AN_NUM);
            num->v = coeff;
            g_ptr_array_insert(n->kids, 0, num);
        }
        stable_sort(n->kids);
        return n;
    }
    return n;
}

static void
fmt_number(GString *s, double v)
{
    if (isnan(v)) {
        g_string_append(s, "NaN");
        return;
    }
    if (isinf(v)) {
        g_string_append(s, v > 0 ? "infinity" : "-infinity");
        return;
    }
    char buf[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(buf, sizeof buf, "%.6f", v);
    char *dot = strchr(buf, '.');
    if (dot) {
        char *e = buf + strlen(buf) - 1;
        while (e > dot && *e == '0') *e-- = '\0';
        if (e == dot) *e = '\0';
    }
    if (strcmp(buf, "-0") == 0) strcpy(buf, "0");
    g_string_append(s, buf);
}

static void serialize(GString *s, const an_node *n, gboolean top);

static void
serialize_inner(GString *s, const an_node *n)
{
    switch (n->kind) {
    case AN_NUM:
        fmt_number(s, n->v);
        g_string_append(s, n->unit);
        break;
    case AN_ANCHOR:
    case AN_SIZE: {
        g_string_append(s, n->kind == AN_ANCHOR ? "anchor(" : "anchor-size(");
        gboolean any = FALSE;
        if (n->name) {
            g_string_append(s, n->name);
            any = TRUE;
        }
        if (n->side || n->size) {
            if (any) g_string_append_c(s, ' ');
            g_string_append(s, n->side ? n->side : n->size);
            any = TRUE;
        } else if (n->side_pct) {
            if (any) g_string_append_c(s, ' ');
            gboolean wrap = n->side_math && n->side_pct->kind != AN_MIN &&
                            n->side_pct->kind != AN_MAX && n->side_pct->kind != AN_CLAMP;
            if (wrap) g_string_append(s, "calc(");
            serialize_inner(s, n->side_pct);
            if (wrap) g_string_append_c(s, ')');
            any = TRUE;
        }
        if (n->fallback) {
            if (any) g_string_append(s, ", ");
            serialize(s, n->fallback, TRUE);
        }
        g_string_append_c(s, ')');
        break;
    }
    case AN_SUM:
        for (guint i = 0; i < n->kids->len; i++) {
            const an_node *k = n->kids->pdata[i];
            if (i > 0) {
                if (k->kind == AN_NUM && k->v < 0) {
                    g_string_append(s, " - ");
                    an_node tmp = *k;
                    tmp.v = -k->v;
                    serialize_inner(s, &tmp);
                    continue;
                }
                g_string_append(s, k->neg ? " - " : " + ");
            } else if (k->neg && k->kind != AN_NUM) {
                g_string_append(s, "-1 * ");
            }
            if (k->kind == AN_SUM) g_string_append_c(s, '(');
            serialize_inner(s, k);
            if (k->kind == AN_SUM) g_string_append_c(s, ')');
        }
        break;
    case AN_PRODUCT:
        for (guint i = 0; i < n->kids->len; i++) {
            const an_node *k = n->kids->pdata[i];
            if (i > 0) g_string_append(s, k->inv ? " / " : " * ");
            if (k->kind == AN_SUM || k->kind == AN_PRODUCT) g_string_append_c(s, '(');
            serialize_inner(s, k);
            if (k->kind == AN_SUM || k->kind == AN_PRODUCT) g_string_append_c(s, ')');
        }
        break;
    case AN_MIN:
    case AN_MAX:
    case AN_CLAMP:
        g_string_append(s, n->kind == AN_MIN ? "min(" : n->kind == AN_MAX ? "max(" : "clamp(");
        for (guint i = 0; i < n->kids->len; i++) {
            if (i) g_string_append(s, ", ");
            serialize_inner(s, n->kids->pdata[i]);
        }
        g_string_append_c(s, ')');
        break;
    }
}

static void
serialize(GString *s, const an_node *n, gboolean top)
{
    gboolean wrap = top && (n->kind == AN_SUM || n->kind == AN_PRODUCT ||
                            (n->kind == AN_NUM && n->neg));
    if (wrap) g_string_append(s, "calc(");
    serialize_inner(s, n);
    if (wrap) g_string_append_c(s, ')');
}

static an_node *
parse_top(const char *text, gboolean allow_anchor, gboolean allow_size)
{
    gboolean valid = TRUE;
    GPtrArray *list = ns_css_component_values_parse(text, -1, &valid);
    an_node *n = NULL;
    if (valid) {
        an_cursor c = { list, 0, list->len };
        an_ctx cx = { allow_anchor, allow_size, 0 };
        an_type type;
        n = parse_sum(&c, &cx, &type);
        if (n && (cur_peek(&c) || type != AT_LENGTH || !contains_anchor(n))) {
            an_free(n);
            n = NULL;
        }
    }
    g_ptr_array_free(list, TRUE);
    return n ? simplify(n) : NULL;
}

char *
ns_css_anchor_fn_canonical(const char *text, gboolean allow_anchor,
                           gboolean allow_size)
{
    if (!ns_css_text_has_anchor_fn(text)) return NULL;
    an_node *n = parse_top(text, allow_anchor, allow_size);
    if (!n) return NULL;
    GString *s = g_string_new(NULL);
    serialize(s, n, TRUE);
    an_free(n);
    return g_string_free(s, FALSE);
}

static double
unit_px(const char *u, const ns_anchor_eval_ctx *cx)
{
    static const struct { const char *u; double px; } abs_units[] = {
        { "px", 1 }, { "cm", 96 / 2.54 }, { "mm", 96 / 25.4 }, { "q", 96 / 101.6 },
        { "in", 96 }, { "pt", 96.0 / 72 }, { "pc", 16 },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(abs_units); i++)
        if (strcmp(u, abs_units[i].u) == 0) return abs_units[i].px;
    if (strcmp(u, "em") == 0 || strcmp(u, "lh") == 0 || strcmp(u, "cap") == 0)
        return cx->font_px;
    if (strcmp(u, "rem") == 0 || strcmp(u, "rlh") == 0) return cx->root_font_px;
    if (strcmp(u, "ex") == 0 || strcmp(u, "ch") == 0) return cx->font_px / 2;
    if (strcmp(u, "vw") == 0 || strcmp(u, "svw") == 0 || strcmp(u, "lvw") == 0 ||
        strcmp(u, "dvw") == 0 || strcmp(u, "vi") == 0)
        return cx->viewport_w / 100.0;
    if (strcmp(u, "vh") == 0 || strcmp(u, "svh") == 0 || strcmp(u, "lvh") == 0 ||
        strcmp(u, "dvh") == 0 || strcmp(u, "vb") == 0)
        return cx->viewport_h / 100.0;
    if (strcmp(u, "vmin") == 0) return MIN(cx->viewport_w, cx->viewport_h) / 100.0;
    if (strcmp(u, "vmax") == 0) return MAX(cx->viewport_w, cx->viewport_h) / 100.0;
    return 0;
}

static gboolean eval_node(const an_node *n, const ns_anchor_eval_ctx *cx, double *out);

static gboolean
eval_anchor(const an_node *n, const ns_anchor_eval_ctx *cx, double *out)
{
    double r[4];
    gboolean found = cx->lookup && (n->kind == AN_SIZE || cx->allow_anchor) &&
                     cx->lookup(cx->user, n->name, r);
    if (found && n->kind == AN_SIZE) {
        const char *sz = n->size;
        gboolean horizontal = sz ? (strcmp(sz, "width") == 0 || strcmp(sz, "inline") == 0 ||
                                    strcmp(sz, "self-inline") == 0)
                                 : !cx->vertical_axis;
        *out = horizontal ? r[2] : r[3];
        return TRUE;
    }
    if (found) {
        gboolean vertical = cx->vertical_axis;
        double start = vertical ? r[1] : r[0];
        double size = vertical ? r[3] : r[2];
        double coord;
        const char *side = n->side;
        gboolean ok = TRUE;
        if (n->side_pct) {
            double pct = 0;
            ns_anchor_eval_ctx pcx = *cx;
            pcx.basis = 100.0;
            ok = eval_node(n->side_pct, &pcx, &pct);
            coord = start + size * pct / 100.0;
        } else if (strcmp(side, "center") == 0) {
            coord = start + size / 2;
        } else if (strcmp(side, "start") == 0 || strcmp(side, "self-start") == 0) {
            coord = start;
        } else if (strcmp(side, "end") == 0 || strcmp(side, "self-end") == 0) {
            coord = start + size;
        } else if (strcmp(side, "inside") == 0) {
            coord = cx->far_edge ? start + size : start;
        } else if (strcmp(side, "outside") == 0) {
            coord = cx->far_edge ? start : start + size;
        } else if (strcmp(side, vertical ? "top" : "left") == 0) {
            coord = start;
        } else if (strcmp(side, vertical ? "bottom" : "right") == 0) {
            coord = start + size;
        } else {
            ok = FALSE;
            coord = 0;
        }
        if (ok) {
            double cb_start = vertical ? cx->cb_y : cx->cb_x;
            double cb_size = vertical ? cx->cb_h : cx->cb_w;
            *out = cx->far_edge ? cb_start + cb_size - coord : coord - cb_start;
            return TRUE;
        }
    }
    if (n->fallback) return eval_node(n->fallback, cx, out);
    return FALSE;
}

static gboolean
eval_node(const an_node *n, const ns_anchor_eval_ctx *cx, double *out)
{
    double v = 0;
    switch (n->kind) {
    case AN_NUM:
        if (!n->unit[0]) v = n->v;
        else if (strcmp(n->unit, "%") == 0) v = n->v * cx->basis / 100.0;
        else v = n->v * unit_px(n->unit, cx);
        break;
    case AN_ANCHOR:
    case AN_SIZE:
        if (!eval_anchor(n, cx, &v)) return FALSE;
        break;
    case AN_SUM:
        for (guint i = 0; i < n->kids->len; i++) {
            const an_node *k = n->kids->pdata[i];
            double kv;
            if (!eval_node(k, cx, &kv)) return FALSE;
            v += k->neg && k->kind != AN_NUM ? -kv : kv;
        }
        break;
    case AN_PRODUCT:
        v = 1;
        for (guint i = 0; i < n->kids->len; i++) {
            const an_node *k = n->kids->pdata[i];
            double kv;
            if (!eval_node(k, cx, &kv)) return FALSE;
            v = k->inv ? v / kv : v * kv;
        }
        break;
    case AN_MIN:
    case AN_MAX:
        for (guint i = 0; i < n->kids->len; i++) {
            double kv;
            if (!eval_node(n->kids->pdata[i], cx, &kv)) return FALSE;
            if (i == 0 || (n->kind == AN_MIN ? kv < v : kv > v)) v = kv;
        }
        break;
    case AN_CLAMP: {
        double lo, mid, hi;
        if (!eval_node(n->kids->pdata[0], cx, &lo) ||
            !eval_node(n->kids->pdata[1], cx, &mid) ||
            !eval_node(n->kids->pdata[2], cx, &hi))
            return FALSE;
        v = MAX(lo, MIN(mid, hi));
        break;
    }
    }
    if (n->neg && n->kind == AN_NUM) v = -v;
    *out = v;
    return TRUE;
}

gboolean
ns_css_anchor_fn_eval(const char *canonical, const ns_anchor_eval_ctx *cx,
                      double *out_px)
{
    an_node *n = parse_top(canonical, TRUE, TRUE);
    if (!n) return FALSE;
    gboolean ok = eval_node(n, cx, out_px);
    an_free(n);
    return ok;
}

static an_node *
strip_anchors(an_node *n)
{
    if (!n) return NULL;
    if (n->kind == AN_ANCHOR || n->kind == AN_SIZE) {
        an_node *fb = n->fallback;
        n->fallback = NULL;
        gboolean neg = n->neg, inv = n->inv;
        an_free(n);
        if (!fb) return NULL;
        fb = strip_anchors(fb);
        if (fb) {
            fb->neg ^= neg;
            fb->inv ^= inv;
        }
        return fb;
    }
    for (guint i = 0; n->kids && i < n->kids->len; i++) {
        an_node *k = strip_anchors(n->kids->pdata[i]);
        n->kids->pdata[i] = k;
        if (!k) {
            an_free(n);
            return NULL;
        }
    }
    return n;
}

static const char *
flip_word(const char *w, const char *const (*pairs)[2], gsize n)
{
    for (gsize i = 0; w && i < n; i++) {
        if (strcmp(w, pairs[i][0]) == 0) return pairs[i][1];
        if (strcmp(w, pairs[i][1]) == 0) return pairs[i][0];
    }
    return NULL;
}

static void
flip_node(an_node *n, const char *const (*pairs)[2], gsize n_pairs, gboolean invert)
{
    if (!n) return;
    if (n->kind == AN_ANCHOR || n->kind == AN_SIZE) {
        char **slot = n->kind == AN_ANCHOR ? &n->side : &n->size;
        const char *repl = flip_word(*slot, pairs, n_pairs);
        if (repl) {
            g_free(*slot);
            *slot = g_strdup(repl);
        }
        if (invert && n->side_pct && n->side_pct->kind == AN_NUM)
            n->side_pct->v = 100.0 - n->side_pct->v;
        flip_node(n->fallback, pairs, n_pairs, invert);
        return;
    }
    for (guint i = 0; n->kids && i < n->kids->len; i++)
        flip_node(n->kids->pdata[i], pairs, n_pairs, invert);
}

char *
ns_css_anchor_fn_flip(const char *canonical, const char *const (*pairs)[2],
                      gsize n_pairs, gboolean invert_percent)
{
    an_node *n = parse_top(canonical, TRUE, TRUE);
    if (!n) return NULL;
    flip_node(n, pairs, n_pairs, invert_percent);
    GString *s = g_string_new(NULL);
    serialize(s, n, TRUE);
    an_free(n);
    return g_string_free(s, FALSE);
}

char *
ns_css_anchor_fn_fallback(const char *canonical)
{
    an_node *n = parse_top(canonical, TRUE, TRUE);
    n = strip_anchors(n);
    if (!n) return NULL;
    n = simplify(n);
    GString *s = g_string_new(NULL);
    serialize(s, n, TRUE);
    an_free(n);
    return g_string_free(s, FALSE);
}

gboolean
ns_css_text_has_anchor_fn(const char *text)
{
    if (!text) return FALSE;
    for (const char *p = text; (p = strstr(p, "anchor")); p += 6) {
        if (p > text && (g_ascii_isalnum(p[-1]) || p[-1] == '-')) continue;
        if (p[6] == '(' || strncmp(p + 6, "-size(", 6) == 0) return TRUE;
    }
    return FALSE;
}

static gboolean
word_is_dashed(const char *w)
{
    if (w[0] != '-' || w[1] != '-' || !w[2]) return FALSE;
    for (const char *p = w + 2; *p; p++)
        if (!g_ascii_isalnum(*p) && *p != '-' && *p != '_' && (guchar)*p < 0x80)
            return FALSE;
    return TRUE;
}

char *
ns_css_dashed_ident_list_canonical(const char *text, const char *const *keywords)
{
    char *t = g_strstrip(g_strdup(text));
    for (int i = 0; keywords && keywords[i]; i++)
        if (g_ascii_strcasecmp(t, keywords[i]) == 0) {
            char *r = g_ascii_strdown(t, -1);
            g_free(t);
            return r;
        }
    char **items = g_strsplit(t, ",", -1);
    GString *out = g_string_new(NULL);
    gboolean ok = items[0] != NULL;
    for (int i = 0; ok && items[i]; i++) {
        char *w = g_strstrip(items[i]);
        if (!word_is_dashed(w)) ok = FALSE;
        else {
            if (i) g_string_append(out, ", ");
            g_string_append(out, w);
        }
    }
    g_strfreev(items);
    g_free(t);
    if (!ok) {
        g_string_free(out, TRUE);
        return NULL;
    }
    return g_string_free(out, FALSE);
}

char *
ns_css_position_anchor_canonical(const char *text)
{
    char *t = g_strstrip(g_strdup(text));
    char *r = NULL;
    if (g_ascii_strcasecmp(t, "normal") == 0 || g_ascii_strcasecmp(t, "none") == 0 ||
        g_ascii_strcasecmp(t, "auto") == 0)
        r = g_ascii_strdown(t, -1);
    else if (word_is_dashed(t))
        r = g_strdup(t);
    g_free(t);
    return r;
}

typedef enum {
    PA_X, PA_Y, PA_BLOCK, PA_INLINE, PA_SELF_BLOCK, PA_SELF_INLINE, PA_AMBIG,
    PA_AMBIG_SELF, PA_GENERIC
} pa_cat;

static const struct { const char *kw; pa_cat cat; } g_pa_keywords[] = {
    { "left", PA_X }, { "right", PA_X }, { "span-left", PA_X }, { "span-right", PA_X },
    { "x-start", PA_X }, { "x-end", PA_X }, { "span-x-start", PA_X }, { "span-x-end", PA_X },
    { "self-x-start", PA_X }, { "self-x-end", PA_X }, { "span-self-x-start", PA_X },
    { "span-self-x-end", PA_X },
    { "top", PA_Y }, { "bottom", PA_Y }, { "span-top", PA_Y }, { "span-bottom", PA_Y },
    { "y-start", PA_Y }, { "y-end", PA_Y }, { "span-y-start", PA_Y }, { "span-y-end", PA_Y },
    { "self-y-start", PA_Y }, { "self-y-end", PA_Y }, { "span-self-y-start", PA_Y },
    { "span-self-y-end", PA_Y },
    { "block-start", PA_BLOCK }, { "block-end", PA_BLOCK },
    { "span-block-start", PA_BLOCK }, { "span-block-end", PA_BLOCK },
    { "inline-start", PA_INLINE }, { "inline-end", PA_INLINE },
    { "span-inline-start", PA_INLINE }, { "span-inline-end", PA_INLINE },
    { "self-block-start", PA_SELF_BLOCK }, { "self-block-end", PA_SELF_BLOCK },
    { "span-self-block-start", PA_SELF_BLOCK }, { "span-self-block-end", PA_SELF_BLOCK },
    { "self-inline-start", PA_SELF_INLINE }, { "self-inline-end", PA_SELF_INLINE },
    { "span-self-inline-start", PA_SELF_INLINE }, { "span-self-inline-end", PA_SELF_INLINE },
    { "start", PA_AMBIG }, { "end", PA_AMBIG }, { "span-start", PA_AMBIG },
    { "span-end", PA_AMBIG },
    { "self-start", PA_AMBIG_SELF }, { "self-end", PA_AMBIG_SELF },
    { "span-self-start", PA_AMBIG_SELF }, { "span-self-end", PA_AMBIG_SELF },
    { "center", PA_GENERIC }, { "span-all", PA_GENERIC },
};

static int
pa_lookup(const char *w)
{
    for (gsize i = 0; i < G_N_ELEMENTS(g_pa_keywords); i++)
        if (g_ascii_strcasecmp(w, g_pa_keywords[i].kw) == 0) return (int)i;
    return -1;
}

static gboolean
pa_first_axis(pa_cat c)
{
    return c == PA_X || c == PA_BLOCK || c == PA_SELF_BLOCK;
}

static const char *
pa_short(const char *kw)
{
    static const struct { const char *l, *s; } map[] = {
        { "block-start", "start" }, { "block-end", "end" },
        { "span-block-start", "span-start" }, { "span-block-end", "span-end" },
        { "inline-start", "start" }, { "inline-end", "end" },
        { "span-inline-start", "span-start" }, { "span-inline-end", "span-end" },
        { "self-block-start", "self-start" }, { "self-block-end", "self-end" },
        { "span-self-block-start", "span-self-start" },
        { "span-self-block-end", "span-self-end" },
        { "self-inline-start", "self-start" }, { "self-inline-end", "self-end" },
        { "span-self-inline-start", "span-self-start" },
        { "span-self-inline-end", "span-self-end" },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(map); i++)
        if (strcmp(kw, map[i].l) == 0) return map[i].s;
    return kw;
}

static char *
pa_ambig_long(const char *kw, gboolean block)
{
    gboolean span = g_str_has_prefix(kw, "span-");
    const char *rest = span ? kw + 5 : kw;
    gboolean is_self = g_str_has_prefix(rest, "self-");
    if (is_self) rest += 5;
    return g_strdup_printf("%s%s%s-%s", span ? "span-" : "", is_self ? "self-" : "",
                           block ? "block" : "inline", rest);
}

char *
ns_css_position_area_canonical(const char *text, gboolean computed)
{
    char **w = g_strsplit_set(g_strstrip(g_strdup(text)), " \t\n", -1);
    GPtrArray *words = g_ptr_array_new();
    for (int i = 0; w[i]; i++)
        if (*w[i]) g_ptr_array_add(words, w[i]);
    char *result = NULL;
    guint n = words->len;
    if (n == 1 && g_ascii_strcasecmp(words->pdata[0], "none") == 0) {
        result = g_strdup("none");
        goto out;
    }
    if (n < 1 || n > 2) goto out;
    int a = pa_lookup(words->pdata[0]);
    int b = n == 2 ? pa_lookup(words->pdata[1]) : -1;
    if (a < 0 || (n == 2 && b < 0)) goto out;
    const char *ka = g_pa_keywords[a].kw;
    pa_cat ca = g_pa_keywords[a].cat;
    if (n == 1) {
        result = g_strdup(ka);
        goto out;
    }
    const char *kb = g_pa_keywords[b].kw;
    pa_cat cb = g_pa_keywords[b].cat;
    if (strcmp(ka, kb) == 0) {
        if (ca == PA_GENERIC || ca == PA_AMBIG || ca == PA_AMBIG_SELF)
            result = g_strdup(ka);
        goto out;
    }
    if (ca == PA_GENERIC && cb == PA_GENERIC) {
        result = g_strdup_printf("%s %s", ka, kb);
        goto out;
    }
    if ((ca == PA_AMBIG || ca == PA_AMBIG_SELF || cb == PA_AMBIG || cb == PA_AMBIG_SELF)) {
        pa_cat amb = (ca == PA_AMBIG || ca == PA_AMBIG_SELF) ? ca : cb;
        pa_cat other = amb == ca ? cb : ca;
        if (other != PA_GENERIC && other != amb) goto out;
        if (computed && other == PA_GENERIC && strcmp(amb == ca ? kb : ka, "span-all") == 0) {
            gboolean first = amb == ca;
            result = pa_ambig_long(first ? ka : kb, first);
            goto out;
        }
        result = g_strdup_printf("%s %s", ka, kb);
        goto out;
    }
    pa_cat specific = ca == PA_GENERIC ? cb : ca;
    if (ca != PA_GENERIC && cb != PA_GENERIC) {
        static const pa_cat pairs[][2] = {
            { PA_X, PA_Y }, { PA_BLOCK, PA_INLINE }, { PA_SELF_BLOCK, PA_SELF_INLINE },
        };
        gboolean ok = FALSE;
        for (gsize i = 0; i < G_N_ELEMENTS(pairs); i++)
            if ((ca == pairs[i][0] && cb == pairs[i][1]) ||
                (ca == pairs[i][1] && cb == pairs[i][0]))
                ok = TRUE;
        if (!ok) goto out;
        const char *first = pa_first_axis(ca) ? ka : kb;
        const char *second = pa_first_axis(ca) ? kb : ka;
        if (computed && specific != PA_X) {
            const char *s1 = pa_short(first), *s2 = pa_short(second);
            result = strcmp(s1, s2) == 0 ? g_strdup(s1) : g_strdup_printf("%s %s", s1, s2);
        } else {
            result = g_strdup_printf("%s %s", first, second);
        }
        goto out;
    }
    const char *kw = ca == PA_GENERIC ? kb : ka;
    const char *gen = ca == PA_GENERIC ? ka : kb;
    if (strcmp(gen, "span-all") == 0) {
        result = g_strdup(kw);
        goto out;
    }
    const char *shown = computed && specific != PA_X && specific != PA_Y ? pa_short(kw) : kw;
    if (pa_first_axis(specific))
        result = g_strdup_printf("%s center", shown);
    else
        result = g_strdup_printf("center %s", shown);
out:
    g_ptr_array_free(words, TRUE);
    g_strfreev(w);
    return result;
}

static void
pa_span(const char *kw, gboolean vertical, int *lo, int *hi)
{
    const char *k = kw;
    gboolean span = g_str_has_prefix(k, "span-");
    if (span) k += 5;
    if (g_str_has_prefix(k, "self-")) k += 5;
    if (g_str_has_prefix(k, "x-") || g_str_has_prefix(k, "y-")) k += 2;
    else if (g_str_has_prefix(k, "block-") || g_str_has_prefix(k, "inline-")) k = strchr(k, '-') + 1;
    gboolean start = strcmp(k, "start") == 0 || strcmp(k, vertical ? "top" : "left") == 0;
    gboolean end = strcmp(k, "end") == 0 || strcmp(k, vertical ? "bottom" : "right") == 0;
    if (strcmp(kw, "center") == 0) { *lo = *hi = 1; return; }
    if (strcmp(kw, "span-all") == 0 || strcmp(k, "all") == 0) { *lo = 0; *hi = 2; return; }
    if (start) { *lo = 0; *hi = span ? 1 : 0; return; }
    if (end) { *lo = span ? 1 : 2; *hi = 2; return; }
    *lo = 0;
    *hi = 2;
}

gboolean
ns_css_position_area_resolve(const char *value, int *col_lo, int *col_hi,
                             int *row_lo, int *row_hi)
{
    if (!value || g_ascii_strcasecmp(value, "none") == 0) return FALSE;
    char **w = g_strsplit(value, " ", -1);
    int n = (int)g_strv_length(w);
    if (n < 1 || n > 2) {
        g_strfreev(w);
        return FALSE;
    }
    int ia = pa_lookup(w[0]);
    int ib = n == 2 ? pa_lookup(w[1]) : -1;
    if (ia < 0 || (n == 2 && ib < 0)) {
        g_strfreev(w);
        return FALSE;
    }
    const char *x = NULL, *y = NULL;
    pa_cat ca = g_pa_keywords[ia].cat;
    if (n == 1) {
        switch (ca) {
        case PA_X: case PA_INLINE: case PA_SELF_INLINE: x = w[0]; y = "span-all"; break;
        case PA_Y: case PA_BLOCK: case PA_SELF_BLOCK: y = w[0]; x = "span-all"; break;
        default: x = y = w[0]; break;
        }
    } else {
        pa_cat cb = g_pa_keywords[ib].cat;
        gboolean a_vertical = ca == PA_Y || ca == PA_BLOCK || ca == PA_SELF_BLOCK;
        gboolean b_horizontal = cb == PA_X || cb == PA_INLINE || cb == PA_SELF_INLINE;
        gboolean a_horizontal = ca == PA_X || ca == PA_INLINE || ca == PA_SELF_INLINE;
        gboolean b_vertical = cb == PA_Y || cb == PA_BLOCK || cb == PA_SELF_BLOCK;
        if (a_horizontal || b_vertical) { x = w[0]; y = w[1]; }
        else if (a_vertical || b_horizontal) { y = w[0]; x = w[1]; }
        else { y = w[0]; x = w[1]; }
    }
    pa_span(x, FALSE, col_lo, col_hi);
    pa_span(y, TRUE, row_lo, row_hi);
    g_strfreev(w);
    return TRUE;
}

char *
ns_css_position_visibility_canonical(const char *text)
{
    char *t = g_strstrip(g_strdup(text));
    if (g_ascii_strcasecmp(t, "always") == 0) {
        g_free(t);
        return g_strdup("always");
    }
    static const char *const order[] = { "anchors-valid", "anchor-valid",
                                         "anchors-visible", "anchor-visible",
                                         "no-overflow" };
    gboolean seen[G_N_ELEMENTS(order)] = { 0 };
    char **w = g_strsplit_set(t, " \t\n", -1);
    gboolean ok = TRUE;
    int count = 0;
    for (int i = 0; ok && w[i]; i++) {
        if (!*w[i]) continue;
        int found = -1;
        for (gsize k = 0; k < G_N_ELEMENTS(order); k++)
            if (g_ascii_strcasecmp(w[i], order[k]) == 0) found = (int)k;
        if (found < 0) ok = FALSE;
        int group = found == 0 || found == 1 ? 0 : found == 2 || found == 3 ? 1 : 2;
        for (gsize k = 0; ok && k < G_N_ELEMENTS(order); k++) {
            int g = k == 0 || k == 1 ? 0 : k == 2 || k == 3 ? 1 : 2;
            if (seen[k] && g == group) ok = FALSE;
        }
        if (ok) seen[found] = TRUE;
        count++;
    }
    g_strfreev(w);
    g_free(t);
    if (!ok || count == 0) return NULL;
    GString *out = g_string_new(NULL);
    for (gsize k = 0; k < G_N_ELEMENTS(order); k++)
        if (seen[k]) {
            if (out->len) g_string_append_c(out, ' ');
            g_string_append(out, order[k]);
        }
    return g_string_free(out, FALSE);
}

char *
ns_css_position_try_order_canonical(const char *text)
{
    static const char *const kws[] = {
        "normal", "most-width", "most-height", "most-block-size", "most-inline-size",
    };
    char *t = g_strstrip(g_strdup(text));
    char *r = NULL;
    for (gsize i = 0; i < G_N_ELEMENTS(kws); i++)
        if (g_ascii_strcasecmp(t, kws[i]) == 0) r = g_strdup(kws[i]);
    g_free(t);
    return r;
}

static gboolean
try_tactic_canonical(char **words, int n, GString *out)
{
    static const char *const tactics[] = { "flip-block", "flip-inline", "flip-start",
                                           "flip-x", "flip-y" };
    gboolean used[G_N_ELEMENTS(tactics)] = { 0 };
    int ident = -1;
    for (int i = 0; i < n; i++) {
        int found = -1;
        for (gsize k = 0; k < G_N_ELEMENTS(tactics); k++)
            if (g_ascii_strcasecmp(words[i], tactics[k]) == 0) found = (int)k;
        if (found >= 0) {
            if (used[found]) return FALSE;
            used[found] = TRUE;
        } else if (word_is_dashed(words[i]) && ident < 0 && (i == 0 || i == n - 1)) {
            ident = i;
        } else {
            return FALSE;
        }
    }
    if (ident >= 0) g_string_append(out, words[ident]);
    for (int i = 0; i < n; i++) {
        if (i == ident) continue;
        if (out->len) g_string_append_c(out, ' ');
        g_string_append(out, words[i]);
    }
    return TRUE;
}

char *
ns_css_position_try_fallbacks_canonical(const char *text)
{
    char *t = g_strstrip(g_strdup(text));
    if (g_ascii_strcasecmp(t, "none") == 0) {
        g_free(t);
        return g_strdup("none");
    }
    char **items = g_strsplit(t, ",", -1);
    GString *out = g_string_new(NULL);
    gboolean ok = items[0] != NULL;
    for (int i = 0; ok && items[i]; i++) {
        char *item = g_strstrip(items[i]);
        if (!*item) { ok = FALSE; break; }
        if (i) g_string_append(out, ", ");
        char **w = g_strsplit_set(item, " \t\n", -1);
        GPtrArray *words = g_ptr_array_new();
        for (int k = 0; w[k]; k++)
            if (*w[k]) g_ptr_array_add(words, w[k]);
        gboolean area = words->len > 0 && pa_lookup(words->pdata[0]) >= 0 &&
                        !g_str_has_prefix(words->pdata[0], "flip-");
        if (area) {
            char *canon = ns_css_position_area_canonical(item, FALSE);
            if (canon) g_string_append(out, canon);
            else ok = FALSE;
            g_free(canon);
        } else {
            GString *one = g_string_new(NULL);
            ok = words->len > 0 && try_tactic_canonical((char **)words->pdata,
                                                        (int)words->len, one);
            g_string_append(out, one->str);
            g_string_free(one, TRUE);
        }
        g_ptr_array_free(words, TRUE);
        g_strfreev(w);
    }
    g_strfreev(items);
    g_free(t);
    if (!ok) {
        g_string_free(out, TRUE);
        return NULL;
    }
    return g_string_free(out, FALSE);
}
