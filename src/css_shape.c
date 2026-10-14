/* Nordstjernen — CSS Shapes values: basic shapes, shape boxes and their serialization.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-or-later
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include "css_shape.h"
#include "css_syntax.h"

typedef struct {
    double num, pct, px;
    double rel[10];
    gboolean dim;
} sh_sum;

typedef struct {
    double v;
    char unit[8];
    gboolean calc;
    gboolean nonneg;
    sh_sum sum;
} sh_len;

enum { SH_NONE, SH_LEFT, SH_RIGHT, SH_TOP, SH_BOTTOM, SH_CENTER };

typedef struct {
    gboolean is_kw;
    int kw;
    sh_len len;
} sh_tok;

typedef struct {
    gboolean present;
    int n;
    sh_tok t[4];
} sh_pos;

typedef enum {
    SHAPE_NONE, SHAPE_CIRCLE, SHAPE_ELLIPSE, SHAPE_INSET, SHAPE_RECT, SHAPE_XYWH, SHAPE_POLYGON,
} sh_kind;

typedef struct {
    gboolean is_kw;
    char kw[16];
    sh_len len;
} sh_radius;

typedef struct {
    sh_kind kind;
    sh_radius r[2];
    int n_radii;
    sh_pos at;
    sh_len inset[4];
    int n_inset;
    sh_len round_h[4], round_v[4];
    int n_round_h, n_round_v;
    gboolean evenodd;
    gboolean poly_round;
    sh_len poly_radius;
    GArray *points;
} sh_shape;

typedef struct {
    GPtrArray *list;
    guint pos;
    guint end;
} sh_cur;

static const ns_css_component *
peek(sh_cur *c)
{
    while (c->pos < c->end) {
        const ns_css_component *t = g_ptr_array_index(c->list, c->pos);
        if (t->type != NS_CSS_COMPONENT_WHITESPACE) return t;
        c->pos++;
    }
    return NULL;
}

static gboolean
is_ident(const ns_css_component *t, const char *name)
{
    return t && t->type == NS_CSS_COMPONENT_IDENT && t->value &&
           g_ascii_strcasecmp(t->value, name) == 0;
}

#define SH_REL 10

static const char *const rel_units[SH_REL] = {
    "ch", "em", "ex", "lh", "rem", "rlh", "vh", "vmax", "vmin", "vw",
};

static double
abs_unit_px(const char *u)
{
    static const struct { const char *u; double px; } units[] = {
        { "px", 1 }, { "cm", 96 / 2.54 }, { "mm", 96 / 25.4 }, { "q", 96 / 101.6 },
        { "in", 96 }, { "pt", 96.0 / 72 }, { "pc", 16 },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(units); i++)
        if (g_ascii_strcasecmp(u, units[i].u) == 0) return units[i].px;
    return 0;
}

static int
rel_index(const char *u)
{
    for (int i = 0; i < SH_REL; i++)
        if (g_ascii_strcasecmp(u, rel_units[i]) == 0) return i;
    return -1;
}

static gboolean
unit_known(const char *u)
{
    return abs_unit_px(u) > 0 || rel_index(u) >= 0;
}

static double
rel_factor(int i, const ns_shape_ctx *cx)
{
    if (i < 0) return 0;
    const char *u = rel_units[i];
    if (strcmp(u, "em") == 0 || strcmp(u, "lh") == 0) return cx->font_px;
    if (strcmp(u, "rem") == 0 || strcmp(u, "rlh") == 0) return cx->root_px;
    if (strcmp(u, "ex") == 0) return cx->ex_px;
    if (strcmp(u, "ch") == 0) return cx->ch_px;
    if (strcmp(u, "vw") == 0) return cx->vw / 100.0;
    if (strcmp(u, "vh") == 0) return cx->vh / 100.0;
    if (strcmp(u, "vmin") == 0) return MIN(cx->vw, cx->vh) / 100.0;
    return MAX(cx->vw, cx->vh) / 100.0;
}

static void
sum_scale(sh_sum *s, double k)
{
    s->num *= k;
    s->pct *= k;
    s->px *= k;
    for (int i = 0; i < SH_REL; i++) s->rel[i] *= k;
}

static gboolean calc_sum(sh_cur *c, sh_sum *out, int depth);

static gboolean
calc_value(sh_cur *c, sh_sum *out, int depth)
{
    const ns_css_component *t = peek(c);
    memset(out, 0, sizeof *out);
    if (!t || depth > 16) return FALSE;
    if (t->type == NS_CSS_COMPONENT_NUMBER) {
        c->pos++;
        out->num = t->number;
        return TRUE;
    }
    if (t->type == NS_CSS_COMPONENT_PERCENTAGE) {
        c->pos++;
        out->pct = t->number;
        out->dim = TRUE;
        return TRUE;
    }
    if (t->type == NS_CSS_COMPONENT_DIMENSION) {
        double f = abs_unit_px(t->value);
        int ri = rel_index(t->value);
        if (f <= 0 && ri < 0) return FALSE;
        c->pos++;
        if (ri >= 0) out->rel[ri] = t->number;
        else out->px = t->number * f;
        out->dim = TRUE;
        return TRUE;
    }
    if ((t->type == NS_CSS_COMPONENT_BLOCK && t->delimiter == ')') ||
        (t->type == NS_CSS_COMPONENT_FUNCTION && t->value &&
         g_ascii_strcasecmp(t->value, "calc") == 0)) {
        c->pos++;
        sh_cur s = { t->children, 0, t->children ? t->children->len : 0 };
        return calc_sum(&s, out, depth + 1) && !peek(&s);
    }
    return FALSE;
}

static gboolean
calc_product(sh_cur *c, sh_sum *out, int depth)
{
    if (!calc_value(c, out, depth)) return FALSE;
    for (;;) {
        const ns_css_component *t = peek(c);
        if (!t || t->type != NS_CSS_COMPONENT_DELIM ||
            (t->delimiter != '*' && t->delimiter != '/'))
            return TRUE;
        gboolean div = t->delimiter == '/';
        c->pos++;
        sh_sum r;
        if (!calc_value(c, &r, depth)) return FALSE;
        if (div) {
            if (r.dim || r.num == 0) return FALSE;
            sum_scale(out, 1.0 / r.num);
        } else if (!r.dim) {
            sum_scale(out, r.num);
        } else if (!out->dim) {
            double k = out->num;
            *out = r;
            sum_scale(out, k);
        } else {
            return FALSE;
        }
    }
}

static gboolean
calc_sum(sh_cur *c, sh_sum *out, int depth)
{
    if (!calc_product(c, out, depth)) return FALSE;
    for (;;) {
        const ns_css_component *t = peek(c);
        if (!t || t->type != NS_CSS_COMPONENT_DELIM ||
            (t->delimiter != '+' && t->delimiter != '-'))
            return TRUE;
        double sign = t->delimiter == '-' ? -1 : 1;
        c->pos++;
        sh_sum r;
        if (!calc_product(c, &r, depth) || r.dim != out->dim) return FALSE;
        sum_scale(&r, sign);
        out->num += r.num;
        out->pct += r.pct;
        out->px += r.px;
        for (int i = 0; i < SH_REL; i++) out->rel[i] += r.rel[i];
    }
}

static gboolean
parse_len(sh_cur *c, sh_len *out, gboolean allow_negative)
{
    const ns_css_component *t = peek(c);
    memset(out, 0, sizeof *out);
    out->nonneg = !allow_negative;
    if (!t) return FALSE;
    if (t->type == NS_CSS_COMPONENT_PERCENTAGE) {
        c->pos++;
        out->v = t->number;
        strcpy(out->unit, "%");
    } else if (t->type == NS_CSS_COMPONENT_DIMENSION) {
        if (!unit_known(t->value) || strlen(t->value) >= sizeof out->unit) return FALSE;
        c->pos++;
        out->v = t->number;
        for (int i = 0; t->value[i]; i++) out->unit[i] = g_ascii_tolower(t->value[i]);
    } else if (t->type == NS_CSS_COMPONENT_NUMBER && t->number == 0) {
        c->pos++;
        strcpy(out->unit, "px");
    } else if (t->type == NS_CSS_COMPONENT_FUNCTION && t->value &&
               g_ascii_strcasecmp(t->value, "calc") == 0) {
        if (!calc_value(c, &out->sum, 0) || !out->sum.dim) return FALSE;
        out->calc = TRUE;
        return TRUE;
    } else {
        return FALSE;
    }
    return allow_negative || out->v >= 0;
}

static void
fmt_num(GString *s, double v)
{
    if (fabs(v) < 1e-9) v = 0;
    char buf[64];
    g_ascii_formatd(buf, sizeof buf, "%.6g", v);
    if (strchr(buf, 'e')) g_ascii_formatd(buf, sizeof buf, "%.6f", v);
    char *dot = strchr(buf, '.');
    if (dot && !strchr(buf, 'e')) {
        char *e = buf + strlen(buf) - 1;
        while (e > dot && *e == '0') *e-- = '\0';
        if (e == dot) *e = '\0';
    }
    g_string_append(s, buf);
}

static void
fmt_term(GString *s, double v, const char *unit, gboolean first)
{
    if (!first) {
        g_string_append(s, v < 0 ? " - " : " + ");
        v = fabs(v);
    }
    fmt_num(s, v);
    g_string_append(s, unit);
}

static void
fmt_px_pct(GString *s, double px, double pct, gboolean force_calc)
{
    if (!force_calc && pct == 0) {
        fmt_num(s, px);
        g_string_append(s, "px");
        return;
    }
    if (!force_calc && px == 0) {
        fmt_num(s, pct);
        g_string_append_c(s, '%');
        return;
    }
    g_string_append(s, "calc(");
    if (pct != 0 || px == 0) {
        fmt_term(s, pct, "%", TRUE);
        if (px != 0) fmt_term(s, px, "px", FALSE);
    } else {
        fmt_term(s, px, "px", TRUE);
    }
    g_string_append_c(s, ')');
}

static void
fmt_sum_specified(GString *s, const sh_sum *sum)
{
    gboolean first = TRUE;
    g_string_append(s, "calc(");
    if (sum->pct != 0) {
        fmt_term(s, sum->pct, "%", first);
        first = FALSE;
    }
    gboolean px_done = FALSE;
    for (int i = 0; i <= SH_REL; i++) {
        if (!px_done && (i == SH_REL || strcmp(rel_units[i], "px") > 0)) {
            px_done = TRUE;
            if (sum->px != 0) {
                fmt_term(s, sum->px, "px", first);
                first = FALSE;
            }
        }
        if (i < SH_REL && sum->rel[i] != 0) {
            fmt_term(s, sum->rel[i], rel_units[i], first);
            first = FALSE;
        }
    }
    if (first) g_string_append(s, "0px");
    g_string_append_c(s, ')');
}

static void
len_to_px_pct(const sh_len *l, const ns_shape_ctx *cx, double *px, double *pct)
{
    *px = 0;
    *pct = 0;
    if (l->calc) {
        *px = l->sum.px;
        *pct = l->sum.pct;
        for (int i = 0; i < SH_REL; i++)
            if (l->sum.rel[i] != 0) *px += l->sum.rel[i] * rel_factor(i, cx);
        if (l->nonneg && *pct == 0 && *px < 0) *px = 0;
        return;
    }
    if (strcmp(l->unit, "%") == 0) {
        *pct = l->v;
        return;
    }
    double f = abs_unit_px(l->unit);
    if (f <= 0) f = rel_factor(rel_index(l->unit), cx);
    *px = l->v * f;
}

static void
fmt_len(GString *s, const sh_len *l, const ns_shape_ctx *cx)
{
    if (cx) {
        double px, pct;
        len_to_px_pct(l, cx, &px, &pct);
        fmt_px_pct(s, px, pct, FALSE);
        return;
    }
    if (l->calc) {
        fmt_sum_specified(s, &l->sum);
        return;
    }
    fmt_num(s, l->v);
    g_string_append(s, l->unit);
}

static int
pos_kw(const ns_css_component *t)
{
    if (!t || t->type != NS_CSS_COMPONENT_IDENT) return SH_NONE;
    if (is_ident(t, "left")) return SH_LEFT;
    if (is_ident(t, "right")) return SH_RIGHT;
    if (is_ident(t, "top")) return SH_TOP;
    if (is_ident(t, "bottom")) return SH_BOTTOM;
    if (is_ident(t, "center")) return SH_CENTER;
    return SH_NONE;
}

static gboolean
horizontal(int k)
{
    return k == SH_LEFT || k == SH_RIGHT;
}

static gboolean
vertical(int k)
{
    return k == SH_TOP || k == SH_BOTTOM;
}

static gboolean
parse_position(sh_cur *c, sh_pos *p)
{
    memset(p, 0, sizeof *p);
    p->present = TRUE;
    while (p->n < 4) {
        const ns_css_component *t = peek(c);
        if (!t) break;
        int k = pos_kw(t);
        if (k != SH_NONE) {
            c->pos++;
            p->t[p->n].is_kw = TRUE;
            p->t[p->n].kw = k;
            p->n++;
            continue;
        }
        guint save = c->pos;
        if (!parse_len(c, &p->t[p->n].len, TRUE)) {
            c->pos = save;
            break;
        }
        p->n++;
    }
    sh_tok *t = p->t;
    switch (p->n) {
    case 1:
        return TRUE;
    case 2:
        if (t[0].is_kw && t[1].is_kw) {
            if ((horizontal(t[0].kw) && horizontal(t[1].kw)) ||
                (vertical(t[0].kw) && vertical(t[1].kw)))
                return FALSE;
            if (vertical(t[0].kw) || horizontal(t[1].kw)) {
                sh_tok tmp = t[0];
                t[0] = t[1];
                t[1] = tmp;
            }
            return TRUE;
        }
        if (t[0].is_kw && vertical(t[0].kw)) return FALSE;
        if (t[1].is_kw && horizontal(t[1].kw)) return FALSE;
        return TRUE;
    case 4:
        if (!t[0].is_kw || t[1].is_kw || !t[2].is_kw || t[3].is_kw) return FALSE;
        if (t[0].kw == SH_CENTER || t[2].kw == SH_CENTER) return FALSE;
        if ((horizontal(t[0].kw) && horizontal(t[2].kw)) ||
            (vertical(t[0].kw) && vertical(t[2].kw)))
            return FALSE;
        if (vertical(t[0].kw)) {
            sh_tok a = t[0], b = t[1];
            t[0] = t[2];
            t[1] = t[3];
            t[2] = a;
            t[3] = b;
        }
        return TRUE;
    default:
        return FALSE;
    }
}

static const char *
kw_name(int k)
{
    switch (k) {
    case SH_LEFT: return "left";
    case SH_RIGHT: return "right";
    case SH_TOP: return "top";
    case SH_BOTTOM: return "bottom";
    default: return "center";
    }
}

static void
fmt_axis_computed(GString *s, int kw, const sh_len *off, const ns_shape_ctx *cx)
{
    double px = 0, pct = 0;
    if (off) len_to_px_pct(off, cx, &px, &pct);
    if (kw == SH_CENTER) {
        g_string_append(s, "50%");
    } else if (kw == SH_RIGHT || kw == SH_BOTTOM) {
        if (!off) g_string_append(s, "100%");
        else if (px == 0) {
            fmt_num(s, 100 - pct);
            g_string_append_c(s, '%');
        } else {
            fmt_px_pct(s, -px, 100 - pct, TRUE);
        }
    } else if (!off) {
        g_string_append(s, "0%");
    } else {
        fmt_px_pct(s, px, pct, FALSE);
    }
}

static void
fmt_position(GString *s, const sh_pos *p, const ns_shape_ctx *cx)
{
    const sh_tok *t = p->t;
    if (cx) {
        if (p->n == 1) {
            if (t[0].is_kw && vertical(t[0].kw)) {
                fmt_axis_computed(s, SH_CENTER, NULL, cx);
                g_string_append_c(s, ' ');
                fmt_axis_computed(s, t[0].kw, NULL, cx);
            } else {
                fmt_axis_computed(s, t[0].is_kw ? t[0].kw : SH_LEFT,
                                  t[0].is_kw ? NULL : &t[0].len, cx);
                g_string_append(s, " 50%");
            }
        } else if (p->n == 2) {
            fmt_axis_computed(s, t[0].is_kw ? t[0].kw : SH_LEFT,
                              t[0].is_kw ? NULL : &t[0].len, cx);
            g_string_append_c(s, ' ');
            fmt_axis_computed(s, t[1].is_kw ? t[1].kw : SH_TOP,
                              t[1].is_kw ? NULL : &t[1].len, cx);
        } else {
            fmt_axis_computed(s, t[0].kw, &t[1].len, cx);
            g_string_append_c(s, ' ');
            fmt_axis_computed(s, t[2].kw, &t[3].len, cx);
        }
        return;
    }
    if (p->n == 1) {
        if (t[0].is_kw && vertical(t[0].kw)) {
            g_string_append(s, "center ");
            g_string_append(s, kw_name(t[0].kw));
        } else {
            if (t[0].is_kw) g_string_append(s, kw_name(t[0].kw));
            else fmt_len(s, &t[0].len, NULL);
            g_string_append(s, " center");
        }
        return;
    }
    for (int i = 0; i < p->n; i++) {
        if (i) g_string_append_c(s, ' ');
        if (t[i].is_kw) g_string_append(s, kw_name(t[i].kw));
        else fmt_len(s, &t[i].len, NULL);
    }
}

static gboolean
parse_radius(sh_cur *c, sh_radius *r)
{
    static const char *const kws[] = {
        "closest-side", "farthest-side", "closest-corner", "farthest-corner",
    };
    const ns_css_component *t = peek(c);
    memset(r, 0, sizeof *r);
    for (gsize i = 0; i < G_N_ELEMENTS(kws); i++) {
        if (!is_ident(t, kws[i])) continue;
        c->pos++;
        r->is_kw = TRUE;
        g_strlcpy(r->kw, kws[i], sizeof r->kw);
        return TRUE;
    }
    guint save = c->pos;
    if (parse_len(c, &r->len, FALSE)) return TRUE;
    c->pos = save;
    return FALSE;
}

static int
parse_len_list(sh_cur *c, sh_len *out, int max, gboolean allow_negative)
{
    int n = 0;
    while (n < max) {
        guint save = c->pos;
        if (!parse_len(c, &out[n], allow_negative)) {
            c->pos = save;
            break;
        }
        n++;
    }
    return n;
}

static gboolean
parse_round(sh_cur *c, sh_shape *sh)
{
    const ns_css_component *t = peek(c);
    if (!is_ident(t, "round")) return TRUE;
    c->pos++;
    sh->n_round_h = parse_len_list(c, sh->round_h, 4, FALSE);
    if (sh->n_round_h == 0) return FALSE;
    t = peek(c);
    if (t && t->type == NS_CSS_COMPONENT_DELIM && t->delimiter == '/') {
        c->pos++;
        sh->n_round_v = parse_len_list(c, sh->round_v, 4, FALSE);
        if (sh->n_round_v == 0) return FALSE;
    }
    return TRUE;
}

static gboolean
parse_rect_edge(sh_cur *c, sh_len *out)
{
    const ns_css_component *t = peek(c);
    if (is_ident(t, "auto")) {
        c->pos++;
        memset(out, 0, sizeof *out);
        strcpy(out->unit, "auto");
        return TRUE;
    }
    return parse_len(c, out, TRUE);
}

static gboolean
parse_shape_fn(const ns_css_component *f, sh_shape *sh)
{
    sh_cur c = { f->children, 0, f->children ? f->children->len : 0 };
    if (g_ascii_strcasecmp(f->value, "circle") == 0 ||
        g_ascii_strcasecmp(f->value, "ellipse") == 0) {
        gboolean ellipse = g_ascii_strcasecmp(f->value, "ellipse") == 0;
        sh->kind = ellipse ? SHAPE_ELLIPSE : SHAPE_CIRCLE;
        while (sh->n_radii < (ellipse ? 2 : 1) && parse_radius(&c, &sh->r[sh->n_radii]))
            sh->n_radii++;
        if (ellipse && sh->n_radii == 1) return FALSE;
        const ns_css_component *t = peek(&c);
        if (is_ident(t, "at")) {
            c.pos++;
            if (!parse_position(&c, &sh->at) || sh->at.n == 0) return FALSE;
        }
        return peek(&c) == NULL;
    }
    if (g_ascii_strcasecmp(f->value, "inset") == 0) {
        sh->kind = SHAPE_INSET;
        sh->n_inset = parse_len_list(&c, sh->inset, 4, TRUE);
        if (sh->n_inset == 0) return FALSE;
        return parse_round(&c, sh) && peek(&c) == NULL;
    }
    if (g_ascii_strcasecmp(f->value, "rect") == 0) {
        sh->kind = SHAPE_RECT;
        for (int i = 0; i < 4; i++)
            if (!parse_rect_edge(&c, &sh->inset[i])) return FALSE;
        sh->n_inset = 4;
        return parse_round(&c, sh) && peek(&c) == NULL;
    }
    if (g_ascii_strcasecmp(f->value, "xywh") == 0) {
        sh->kind = SHAPE_XYWH;
        for (int i = 0; i < 4; i++)
            if (!parse_len(&c, &sh->inset[i], i < 2)) return FALSE;
        sh->n_inset = 4;
        return parse_round(&c, sh) && peek(&c) == NULL;
    }
    if (g_ascii_strcasecmp(f->value, "polygon") == 0) {
        sh->kind = SHAPE_POLYGON;
        sh->points = g_array_new(FALSE, FALSE, sizeof(sh_len));
        const ns_css_component *t = peek(&c);
        gboolean prelude = FALSE;
        if (is_ident(t, "nonzero") || is_ident(t, "evenodd")) {
            sh->evenodd = is_ident(t, "evenodd");
            c.pos++;
            prelude = TRUE;
            t = peek(&c);
        }
        if (is_ident(t, "round")) {
            c.pos++;
            sh_len *r = &sh->poly_radius;
            if (!parse_len(&c, r, FALSE) || strcmp(r->unit, "%") == 0 ||
                (r->calc && r->sum.pct != 0))
                return FALSE;
            sh->poly_round = r->calc || r->v != 0;
            prelude = TRUE;
            t = peek(&c);
        }
        if (prelude) {
            if (!t || t->type != NS_CSS_COMPONENT_COMMA) return FALSE;
            c.pos++;
        }
        for (;;) {
            sh_len x, y;
            if (!parse_len(&c, &x, TRUE) || !parse_len(&c, &y, TRUE)) return FALSE;
            g_array_append_val(sh->points, x);
            g_array_append_val(sh->points, y);
            t = peek(&c);
            if (!t) break;
            if (t->type != NS_CSS_COMPONENT_COMMA) return FALSE;
            c.pos++;
        }
        return TRUE;
    }
    return FALSE;
}

static int
reduce_sides(char **v, int n)
{
    if (n == 4 && strcmp(v[3], v[1]) == 0) n = 3;
    if (n == 3 && strcmp(v[2], v[0]) == 0) n = 2;
    if (n == 2 && strcmp(v[1], v[0]) == 0) n = 1;
    return n;
}

static void
expand_sides(char **v, int n)
{
    if (n < 2) v[1] = v[0];
    if (n < 3) v[2] = v[0];
    if (n < 4) v[3] = v[1];
}

static char *
len_text(const sh_len *l, const ns_shape_ctx *cx)
{
    GString *s = g_string_new(NULL);
    if (strcmp(l->unit, "auto") == 0) g_string_append(s, "auto");
    else fmt_len(s, l, cx);
    return g_string_free(s, FALSE);
}

static char *
derived_text(double px, double pct)
{
    GString *s = g_string_new(NULL);
    if (px == 0) {
        fmt_num(s, pct);
        g_string_append_c(s, '%');
    } else {
        fmt_px_pct(s, px, pct, TRUE);
    }
    return g_string_free(s, FALSE);
}

static void
inset_computed_texts(const sh_shape *sh, const ns_shape_ctx *cx, char **out)
{
    double px[4], pct[4];
    for (int i = 0; i < 4; i++) {
        if (strcmp(sh->inset[i].unit, "auto") == 0) {
            px[i] = 0;
            pct[i] = sh->kind == SHAPE_RECT && (i == 1 || i == 2) ? 100 : 0;
        } else {
            len_to_px_pct(&sh->inset[i], cx, &px[i], &pct[i]);
        }
    }
    if (sh->kind == SHAPE_RECT) {
        out[0] = len_text(&sh->inset[0], cx);
        out[3] = len_text(&sh->inset[3], cx);
        if (strcmp(sh->inset[0].unit, "auto") == 0) { g_free(out[0]); out[0] = g_strdup("0%"); }
        if (strcmp(sh->inset[3].unit, "auto") == 0) { g_free(out[3]); out[3] = g_strdup("0%"); }
        out[1] = derived_text(-px[1], 100 - pct[1]);
        out[2] = derived_text(-px[2], 100 - pct[2]);
        return;
    }
    out[0] = len_text(&sh->inset[1], cx);
    out[3] = len_text(&sh->inset[0], cx);
    out[1] = derived_text(-px[0] - px[2], 100 - pct[0] - pct[2]);
    out[2] = derived_text(-px[1] - px[3], 100 - pct[1] - pct[3]);
}

static void
fmt_round(GString *s, const sh_shape *sh, const ns_shape_ctx *cx)
{
    if (!sh->n_round_h) return;
    char *h[4] = { 0 }, *v[4] = { 0 };
    for (int i = 0; i < sh->n_round_h; i++) h[i] = len_text(&sh->round_h[i], cx);
    for (int i = 0; i < sh->n_round_v; i++) v[i] = len_text(&sh->round_v[i], cx);
    char *he[4] = { h[0], h[1], h[2], h[3] }, *ve[4] = { v[0], v[1], v[2], v[3] };
    expand_sides(he, sh->n_round_h);
    gboolean same = TRUE;
    if (sh->n_round_v) {
        expand_sides(ve, sh->n_round_v);
        for (int i = 0; i < 4; i++)
            if (strcmp(he[i], ve[i]) != 0) same = FALSE;
    }
    int nh = reduce_sides(he, 4);
    g_string_append(s, " round");
    for (int i = 0; i < nh; i++) {
        g_string_append_c(s, ' ');
        g_string_append(s, he[i]);
    }
    if (!same) {
        int nv = reduce_sides(ve, 4);
        g_string_append(s, " /");
        for (int i = 0; i < nv; i++) {
            g_string_append_c(s, ' ');
            g_string_append(s, ve[i]);
        }
    }
    for (int i = 0; i < 4; i++) {
        g_free(h[i]);
        g_free(v[i]);
    }
}

static void
fmt_shape(GString *s, const sh_shape *sh, const ns_shape_ctx *cx)
{
    switch (sh->kind) {
    case SHAPE_CIRCLE:
    case SHAPE_ELLIPSE: {
        g_string_append(s, sh->kind == SHAPE_CIRCLE ? "circle(" : "ellipse(");
        gboolean all_closest = TRUE;
        for (int i = 0; i < sh->n_radii; i++)
            if (!sh->r[i].is_kw || strcmp(sh->r[i].kw, "closest-side") != 0)
                all_closest = FALSE;
        gboolean any = FALSE;
        if (sh->n_radii && !all_closest) {
            for (int i = 0; i < sh->n_radii; i++) {
                if (i) g_string_append_c(s, ' ');
                if (sh->r[i].is_kw) g_string_append(s, sh->r[i].kw);
                else fmt_len(s, &sh->r[i].len, cx);
            }
            any = TRUE;
        }
        if (sh->at.present) {
            if (any) g_string_append_c(s, ' ');
            g_string_append(s, "at ");
            fmt_position(s, &sh->at, cx);
        }
        g_string_append_c(s, ')');
        break;
    }
    case SHAPE_INSET:
    case SHAPE_RECT:
    case SHAPE_XYWH: {
        char *v[4] = { 0 };
        int n = sh->n_inset;
        if (cx && sh->kind != SHAPE_INSET) {
            inset_computed_texts(sh, cx, v);
            n = 4;
        } else {
            for (int i = 0; i < n; i++) v[i] = len_text(&sh->inset[i], cx);
        }
        gboolean as_inset = sh->kind == SHAPE_INSET || cx;
        if (as_inset) {
            char *e[4] = { v[0], v[1], v[2], v[3] };
            expand_sides(e, n);
            n = reduce_sides(e, 4);
            g_string_append(s, "inset(");
            for (int i = 0; i < n; i++) {
                if (i) g_string_append_c(s, ' ');
                g_string_append(s, e[i]);
            }
        } else {
            g_string_append(s, sh->kind == SHAPE_RECT ? "rect(" : "xywh(");
            for (int i = 0; i < n; i++) {
                if (i) g_string_append_c(s, ' ');
                g_string_append(s, v[i]);
            }
        }
        fmt_round(s, sh, cx);
        g_string_append_c(s, ')');
        for (int i = 0; i < 4; i++) g_free(v[i]);
        break;
    }
    case SHAPE_POLYGON: {
        g_string_append(s, "polygon(");
        double rpx = 0, rpct = 0;
        if (sh->poly_round && cx) len_to_px_pct(&sh->poly_radius, cx, &rpx, &rpct);
        gboolean round = sh->poly_round && (!cx || rpx != 0);
        if (sh->evenodd) g_string_append(s, round ? "evenodd " : "evenodd, ");
        if (round) {
            g_string_append(s, "round ");
            fmt_len(s, &sh->poly_radius, cx);
            g_string_append(s, ", ");
        }
        for (guint i = 0; i + 1 < sh->points->len; i += 2) {
            if (i) g_string_append(s, ", ");
            fmt_len(s, &g_array_index(sh->points, sh_len, i), cx);
            g_string_append_c(s, ' ');
            fmt_len(s, &g_array_index(sh->points, sh_len, i + 1), cx);
        }
        g_string_append_c(s, ')');
        break;
    }
    default:
        break;
    }
}

static const char *
box_keyword(const ns_css_component *t, gboolean geometry)
{
    static const char *const boxes[] = { "margin-box", "border-box", "padding-box",
                                         "content-box", "fill-box", "stroke-box",
                                         "view-box" };
    gsize n = geometry ? G_N_ELEMENTS(boxes) : 4;
    for (gsize i = 0; i < n; i++)
        if (is_ident(t, boxes[i])) return boxes[i];
    return NULL;
}

typedef struct {
    gboolean none;
    gboolean have_shape;
    const char *box;
    sh_shape sh;
} sh_value;

static gboolean
parse_value(const char *text, sh_value *v, gboolean geometry)
{
    memset(v, 0, sizeof *v);
    if (!text) return FALSE;
    gboolean valid = TRUE;
    GPtrArray *list = ns_css_component_values_parse(text, -1, &valid);
    sh_cur c = { list, 0, list->len };
    gboolean ok = valid;
    const ns_css_component *t = peek(&c);
    if (ok && is_ident(t, "none")) {
        c.pos++;
        v->none = TRUE;
        ok = peek(&c) == NULL;
    } else {
        while (ok && (t = peek(&c))) {
            const char *b = box_keyword(t, geometry);
            if (b && !v->box) {
                v->box = b;
                c.pos++;
            } else if (t->type == NS_CSS_COMPONENT_FUNCTION && t->value && !v->have_shape) {
                c.pos++;
                ok = parse_shape_fn(t, &v->sh);
                v->have_shape = TRUE;
            } else {
                ok = FALSE;
            }
        }
        ok = ok && (v->have_shape || v->box);
    }
    g_ptr_array_free(list, TRUE);
    return ok;
}

static void
value_clear(sh_value *v)
{
    if (v->sh.points) g_array_free(v->sh.points, TRUE);
    v->sh.points = NULL;
}

char *
ns_css_shape_outside_canonical(const char *text, const ns_shape_ctx *cx)
{
    sh_value v;
    char *result = NULL;
    if (parse_value(text, &v, FALSE)) {
        GString *s = g_string_new(NULL);
        if (v.none) g_string_append(s, "none");
        if (v.have_shape) fmt_shape(s, &v.sh, cx);
        if (v.box && !(v.have_shape && strcmp(v.box, "margin-box") == 0)) {
            if (s->len) g_string_append_c(s, ' ');
            g_string_append(s, v.box);
        }
        result = g_string_free(s, FALSE);
    }
    value_clear(&v);
    return result;
}

typedef struct {
    double px, pct;
} sh_cv;

typedef struct {
    gboolean is_kw;
    const char *kw;
    sh_cv v;
} sh_crad;

typedef struct {
    sh_kind kind;
    sh_crad r[2];
    int n_radii;
    gboolean has_at;
    sh_cv at[2];
    sh_cv inset[4];
    sh_cv rh[4], rv[4];
    gboolean evenodd;
    sh_cv poly_radius;
    GArray *pts;
} sh_cshape;

static sh_cv
len_cv(const sh_len *l, const ns_shape_ctx *cx)
{
    sh_cv r;
    len_to_px_pct(l, cx, &r.px, &r.pct);
    return r;
}

static sh_cv
axis_cv(int kw, const sh_len *off, const ns_shape_ctx *cx)
{
    sh_cv r = { 0, 0 };
    if (off) r = len_cv(off, cx);
    if (kw == SH_CENTER) {
        r.px = 0;
        r.pct = 50;
    } else if (kw == SH_RIGHT || kw == SH_BOTTOM) {
        r.px = -r.px;
        r.pct = 100 - r.pct;
    }
    return r;
}

static void
position_cv(const sh_pos *p, const ns_shape_ctx *cx, sh_cv *out)
{
    const sh_tok *t = p->t;
    if (p->n == 1) {
        if (t[0].is_kw && vertical(t[0].kw)) {
            out[0] = axis_cv(SH_CENTER, NULL, cx);
            out[1] = axis_cv(t[0].kw, NULL, cx);
        } else {
            out[0] = axis_cv(t[0].is_kw ? t[0].kw : SH_LEFT, t[0].is_kw ? NULL : &t[0].len, cx);
            out[1] = axis_cv(SH_CENTER, NULL, cx);
        }
    } else if (p->n == 2) {
        out[0] = axis_cv(t[0].is_kw ? t[0].kw : SH_LEFT, t[0].is_kw ? NULL : &t[0].len, cx);
        out[1] = axis_cv(t[1].is_kw ? t[1].kw : SH_TOP, t[1].is_kw ? NULL : &t[1].len, cx);
    } else {
        out[0] = axis_cv(t[0].kw, &t[1].len, cx);
        out[1] = axis_cv(t[2].kw, &t[3].len, cx);
    }
}

static void
expand_cv(sh_cv *v, int n)
{
    if (n < 1) v[0] = (sh_cv){ 0, 0 };
    if (n < 2) v[1] = v[0];
    if (n < 3) v[2] = v[0];
    if (n < 4) v[3] = v[1];
}

static void
to_cshape(const sh_shape *sh, const ns_shape_ctx *cx, sh_cshape *o)
{
    memset(o, 0, sizeof *o);
    o->kind = sh->kind == SHAPE_RECT || sh->kind == SHAPE_XYWH ? SHAPE_INSET : sh->kind;
    switch (sh->kind) {
    case SHAPE_CIRCLE:
    case SHAPE_ELLIPSE:
        o->n_radii = sh->kind == SHAPE_CIRCLE ? 1 : 2;
        for (int i = 0; i < o->n_radii; i++) {
            if (i >= sh->n_radii || sh->r[i].is_kw) {
                o->r[i].is_kw = TRUE;
                o->r[i].kw = i < sh->n_radii ? sh->r[i].kw : "closest-side";
            } else {
                o->r[i].v = len_cv(&sh->r[i].len, cx);
            }
        }
        o->has_at = sh->at.present;
        if (o->has_at) position_cv(&sh->at, cx, o->at);
        else o->at[0] = o->at[1] = (sh_cv){ 0, 50 };
        break;
    case SHAPE_INSET:
        for (int i = 0; i < sh->n_inset; i++) o->inset[i] = len_cv(&sh->inset[i], cx);
        expand_cv(o->inset, sh->n_inset);
        break;
    case SHAPE_RECT: {
        sh_cv e[4];
        for (int i = 0; i < 4; i++)
            e[i] = strcmp(sh->inset[i].unit, "auto") == 0
                ? (sh_cv){ 0, i == 1 || i == 2 ? 100 : 0 } : len_cv(&sh->inset[i], cx);
        o->inset[0] = e[0];
        o->inset[1] = (sh_cv){ -e[1].px, 100 - e[1].pct };
        o->inset[2] = (sh_cv){ -e[2].px, 100 - e[2].pct };
        o->inset[3] = e[3];
        break;
    }
    case SHAPE_XYWH: {
        sh_cv e[4];
        for (int i = 0; i < 4; i++) e[i] = len_cv(&sh->inset[i], cx);
        o->inset[0] = e[1];
        o->inset[1] = (sh_cv){ -e[0].px - e[2].px, 100 - e[0].pct - e[2].pct };
        o->inset[2] = (sh_cv){ -e[1].px - e[3].px, 100 - e[1].pct - e[3].pct };
        o->inset[3] = e[0];
        break;
    }
    case SHAPE_POLYGON:
        o->evenodd = sh->evenodd;
        if (sh->poly_round) o->poly_radius = len_cv(&sh->poly_radius, cx);
        o->pts = g_array_new(FALSE, FALSE, sizeof(sh_cv));
        for (guint i = 0; i < sh->points->len; i++) {
            sh_cv p = len_cv(&g_array_index(sh->points, sh_len, i), cx);
            g_array_append_val(o->pts, p);
        }
        break;
    default:
        break;
    }
    if (o->kind == SHAPE_INSET) {
        for (int i = 0; i < sh->n_round_h; i++) o->rh[i] = len_cv(&sh->round_h[i], cx);
        expand_cv(o->rh, sh->n_round_h);
        if (sh->n_round_v) {
            for (int i = 0; i < sh->n_round_v; i++) o->rv[i] = len_cv(&sh->round_v[i], cx);
            expand_cv(o->rv, sh->n_round_v);
        } else {
            memcpy(o->rv, o->rh, sizeof o->rv);
        }
    }
}

typedef struct {
    double wa, wb;
    gboolean add;
} sh_mix;

static sh_cv
cv_lerp(sh_cv a, sh_cv b, const sh_mix *m, gboolean nonneg)
{
    sh_cv r = { a.px * m->wa + b.px * m->wb, a.pct * m->wa + b.pct * m->wb };
    if (nonneg) {
        if (r.pct <= 0 && r.px < 0) r.px = 0;
        if (r.px <= 0 && r.pct < 0) r.pct = 0;
    }
    return r;
}

static char *
cv_text(sh_cv v)
{
    GString *s = g_string_new(NULL);
    fmt_px_pct(s, v.px, v.pct, FALSE);
    return g_string_free(s, FALSE);
}

static void
append_cv(GString *s, sh_cv v)
{
    fmt_px_pct(s, v.px, v.pct, FALSE);
}

static void
append_cv_sides(GString *s, const sh_cv *v)
{
    char *e[4];
    for (int i = 0; i < 4; i++) e[i] = cv_text(v[i]);
    int n = reduce_sides(e, 4);
    for (int i = 0; i < n; i++) {
        if (i) g_string_append_c(s, ' ');
        g_string_append(s, e[i]);
    }
    for (int i = 0; i < 4; i++) g_free(e[i]);
}

static gboolean
cv_sides_zero(const sh_cv *v)
{
    for (int i = 0; i < 4; i++)
        if (v[i].px != 0 || v[i].pct != 0) return FALSE;
    return TRUE;
}

static gboolean
cv_sides_equal(const sh_cv *a, const sh_cv *b)
{
    for (int i = 0; i < 4; i++)
        if (a[i].px != b[i].px || a[i].pct != b[i].pct) return FALSE;
    return TRUE;
}

static gboolean
cshape_lerp(const sh_cshape *a, const sh_cshape *b, const sh_mix *t, GString *s)
{
    if (a->kind != b->kind) return FALSE;
    switch (a->kind) {
    case SHAPE_CIRCLE:
    case SHAPE_ELLIPSE: {
        sh_crad r[2];
        gboolean all_closest = TRUE;
        for (int i = 0; i < a->n_radii; i++) {
            if (a->r[i].is_kw != b->r[i].is_kw) return FALSE;
            if (a->r[i].is_kw && (t->add || strcmp(a->r[i].kw, b->r[i].kw) != 0))
                return FALSE;
            r[i] = a->r[i];
            if (!r[i].is_kw) r[i].v = cv_lerp(a->r[i].v, b->r[i].v, t, TRUE);
            if (!r[i].is_kw || strcmp(r[i].kw, "closest-side") != 0) all_closest = FALSE;
        }
        g_string_append(s, a->kind == SHAPE_CIRCLE ? "circle(" : "ellipse(");
        if (!all_closest) {
            for (int i = 0; i < a->n_radii; i++) {
                if (i) g_string_append_c(s, ' ');
                if (r[i].is_kw) g_string_append(s, r[i].kw);
                else append_cv(s, r[i].v);
            }
        }
        if (a->has_at || b->has_at) {
            if (!all_closest) g_string_append_c(s, ' ');
            g_string_append(s, "at ");
            append_cv(s, cv_lerp(a->at[0], b->at[0], t, FALSE));
            g_string_append_c(s, ' ');
            append_cv(s, cv_lerp(a->at[1], b->at[1], t, FALSE));
        }
        g_string_append_c(s, ')');
        return TRUE;
    }
    case SHAPE_INSET: {
        sh_cv in[4], rh[4], rv[4];
        for (int i = 0; i < 4; i++) {
            in[i] = cv_lerp(a->inset[i], b->inset[i], t, FALSE);
            rh[i] = cv_lerp(a->rh[i], b->rh[i], t, TRUE);
            rv[i] = cv_lerp(a->rv[i], b->rv[i], t, TRUE);
        }
        g_string_append(s, "inset(");
        append_cv_sides(s, in);
        if (!cv_sides_zero(rh) || !cv_sides_zero(rv)) {
            g_string_append(s, " round ");
            append_cv_sides(s, rh);
            if (!cv_sides_equal(rh, rv)) {
                g_string_append(s, " / ");
                append_cv_sides(s, rv);
            }
        }
        g_string_append_c(s, ')');
        return TRUE;
    }
    case SHAPE_POLYGON:
        if (a->evenodd != b->evenodd || a->pts->len != b->pts->len) return FALSE;
    {
        g_string_append(s, "polygon(");
        sh_cv r = cv_lerp(a->poly_radius, b->poly_radius, t, TRUE);
        gboolean round = r.px != 0;
        if (a->evenodd) g_string_append(s, round ? "evenodd " : "evenodd, ");
        if (round) {
            g_string_append(s, "round ");
            append_cv(s, r);
            g_string_append(s, ", ");
        }
        for (guint i = 0; i + 1 < a->pts->len; i += 2) {
            if (i) g_string_append(s, ", ");
            append_cv(s, cv_lerp(g_array_index(a->pts, sh_cv, i),
                                 g_array_index(b->pts, sh_cv, i), t, FALSE));
            g_string_append_c(s, ' ');
            append_cv(s, cv_lerp(g_array_index(a->pts, sh_cv, i + 1),
                                 g_array_index(b->pts, sh_cv, i + 1), t, FALSE));
        }
        g_string_append_c(s, ')');
        return TRUE;
    }
    default:
        return FALSE;
    }
}

static char *
shape_mix(const char *from, const char *to, const sh_mix *t, const ns_shape_ctx *cx)
{
    sh_value a, b;
    char *result = NULL;
    gboolean ok_a = parse_value(from, &a, TRUE);
    gboolean ok = parse_value(to, &b, TRUE) && ok_a;
    const char *box_a = a.box ? a.box : "";
    const char *box_b = b.box ? b.box : "";
    if (ok && a.have_shape && b.have_shape && strcmp(box_a, box_b) == 0) {
        sh_cshape ca, cb;
        to_cshape(&a.sh, cx, &ca);
        to_cshape(&b.sh, cx, &cb);
        GString *s = g_string_new(NULL);
        if (cshape_lerp(&ca, &cb, t, s)) {
            if (*box_a) {
                g_string_append_c(s, ' ');
                g_string_append(s, box_a);
            }
            result = g_string_free(s, FALSE);
        } else {
            g_string_free(s, TRUE);
        }
        if (ca.pts) g_array_free(ca.pts, TRUE);
        if (cb.pts) g_array_free(cb.pts, TRUE);
    }
    value_clear(&a);
    value_clear(&b);
    return result;
}

char *
ns_css_shape_interpolate(const char *from, const char *to, double t, const ns_shape_ctx *cx)
{
    sh_mix m = { 1 - t, t, FALSE };
    return shape_mix(from, to, &m, cx);
}

char *
ns_css_shape_add(const char *underlying, const char *v, const ns_shape_ctx *cx)
{
    sh_mix m = { 1, 1, TRUE };
    return shape_mix(underlying, v, &m, cx);
}

char *
ns_css_shape_margin_canonical(const char *text, const ns_shape_ctx *cx)
{
    if (!text) return NULL;
    gboolean valid = TRUE;
    GPtrArray *list = ns_css_component_values_parse(text, -1, &valid);
    sh_cur c = { list, 0, list->len };
    sh_len l;
    char *result = NULL;
    if (valid && parse_len(&c, &l, FALSE) && !peek(&c)) {
        GString *s = g_string_new(NULL);
        fmt_len(s, &l, cx);
        result = g_string_free(s, FALSE);
    }
    g_ptr_array_free(list, TRUE);
    return result;
}

char *
ns_css_shape_image_threshold_canonical(const char *text, gboolean computed)
{
    if (!text) return NULL;
    gboolean valid = TRUE;
    GPtrArray *list = ns_css_component_values_parse(text, -1, &valid);
    sh_cur c = { list, 0, list->len };
    char *result = NULL;
    const ns_css_component *t = peek(&c);
    double v = 0;
    gboolean ok = FALSE, is_calc = FALSE;
    if (valid && t && t->type == NS_CSS_COMPONENT_NUMBER) {
        c.pos++;
        v = t->number;
        ok = TRUE;
    } else if (valid && t && t->type == NS_CSS_COMPONENT_FUNCTION && t->value &&
               g_ascii_strcasecmp(t->value, "calc") == 0) {
        sh_sum sum;
        ok = calc_value(&c, &sum, 0) && !sum.dim;
        v = sum.num;
        is_calc = TRUE;
    }
    if (ok && !peek(&c)) {
        GString *s = g_string_new(NULL);
        if (computed) {
            fmt_num(s, CLAMP(v, 0.0, 1.0));
        } else if (is_calc) {
            g_string_append(s, "calc(");
            fmt_num(s, v);
            g_string_append_c(s, ')');
        } else {
            fmt_num(s, v);
        }
        result = g_string_free(s, FALSE);
    }
    g_ptr_array_free(list, TRUE);
    return result;
}
