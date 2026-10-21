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
    SHAPE_PATH, SHAPE_SHAPE,
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
    GArray *path;
    sh_pos shape_from;
    GArray *scmds;
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
        if (px == 0 && (strcmp(l->unit, "%") == 0 || (l->calc && l->sum.pct != 0))) {
            fmt_num(s, pct);
            g_string_append_c(s, '%');
        } else {
            fmt_px_pct(s, px, pct, FALSE);
        }
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
        fmt_len(s, off, cx);
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

typedef struct {
    char cmd;
    int n;
    double v[7];
} sh_pcmd;

static int
path_arg_count(char up)
{
    switch (up) {
    case 'M': case 'L': case 'T': return 2;
    case 'H': case 'V': return 1;
    case 'C': return 6;
    case 'S': case 'Q': return 4;
    case 'A': return 7;
    default: return 0;
    }
}

static const char *
path_skip_sep(const char *p, gboolean comma)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f') p++;
    if (comma && *p == ',') {
        p++;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f') p++;
    }
    return p;
}

static gboolean
path_number(const char **pp, double *out)
{
    const char *p = *pp;
    const char *start = p;
    if (*p == '+' || *p == '-') p++;
    gboolean digits = FALSE;
    while (g_ascii_isdigit(*p)) { p++; digits = TRUE; }
    if (*p == '.') {
        p++;
        while (g_ascii_isdigit(*p)) { p++; digits = TRUE; }
    }
    if (!digits) return FALSE;
    if (*p == 'e' || *p == 'E') {
        const char *e = p + 1;
        if (*e == '+' || *e == '-') e++;
        if (g_ascii_isdigit(*e)) {
            while (g_ascii_isdigit(*e)) e++;
            p = e;
        }
    }
    char *tmp = g_strndup(start, p - start);
    *out = g_ascii_strtod(tmp, NULL);
    g_free(tmp);
    *pp = p;
    return TRUE;
}

static gboolean
path_flag(const char **pp, double *out)
{
    if (**pp != '0' && **pp != '1') return FALSE;
    *out = **pp - '0';
    (*pp)++;
    return TRUE;
}

static GArray *
path_parse(const char *d)
{
    GArray *cmds = g_array_new(FALSE, FALSE, sizeof(sh_pcmd));
    const char *p = path_skip_sep(d, FALSE);
    char prev = 0;
    while (*p) {
        char c = *p;
        if (g_ascii_isalpha(c)) {
            char up = g_ascii_toupper(c);
            if (!strchr("MLHVCSQTAZ", up)) goto fail;
            p++;
        } else {
            if (!prev || g_ascii_toupper(prev) == 'Z') goto fail;
            c = prev;
            if (c == 'M') c = 'L';
            else if (c == 'm') c = 'l';
        }
        if (cmds->len == 0 && g_ascii_toupper(c) != 'M') goto fail;
        sh_pcmd pc = { c, path_arg_count(g_ascii_toupper(c)), { 0 } };
        p = path_skip_sep(p, FALSE);
        for (int i = 0; i < pc.n; i++) {
            gboolean ok = g_ascii_toupper(c) == 'A' && (i == 3 || i == 4)
                ? path_flag(&p, &pc.v[i]) : path_number(&p, &pc.v[i]);
            if (!ok) goto fail;
            p = path_skip_sep(p, i + 1 < pc.n);
        }
        if (g_ascii_toupper(c) == 'Z') pc.cmd = c == 'z' ? 'z' : 'Z';
        g_array_append_val(cmds, pc);
        prev = c;
        if (pc.n && *p == ',') {
            p = path_skip_sep(p, TRUE);
            if (!*p || g_ascii_isalpha(*p)) goto fail;
        }
    }
    if (cmds->len == 0) goto fail;
    return cmds;
fail:
    g_array_free(cmds, TRUE);
    return NULL;
}

static void
path_absolutize(GArray *cmds)
{
    double cx = 0, cy = 0, sx = 0, sy = 0;
    for (guint i = 0; i < cmds->len; i++) {
        sh_pcmd *c = &g_array_index(cmds, sh_pcmd, i);
        gboolean rel = g_ascii_islower(c->cmd);
        char up = g_ascii_toupper(c->cmd);
        if (rel) {
            switch (up) {
            case 'H': c->v[0] += cx; break;
            case 'V': c->v[0] += cy; break;
            case 'A': c->v[5] += cx; c->v[6] += cy; break;
            default:
                for (int k = 0; k + 1 < c->n; k += 2) {
                    c->v[k] += cx;
                    c->v[k + 1] += cy;
                }
            }
        }
        c->cmd = up;
        switch (up) {
        case 'Z': cx = sx; cy = sy; break;
        case 'H': cx = c->v[0]; break;
        case 'V': cy = c->v[0]; break;
        default:
            cx = c->v[c->n - 2];
            cy = c->v[c->n - 1];
        }
        if (up == 'M') {
            sx = cx;
            sy = cy;
        }
    }
}

static void
fmt_path(GString *s, const GArray *cmds, gboolean evenodd)
{
    g_string_append(s, "path(");
    if (evenodd) g_string_append(s, "evenodd, ");
    g_string_append_c(s, '"');
    for (guint i = 0; i < cmds->len; i++) {
        const sh_pcmd *c = &g_array_index(cmds, sh_pcmd, i);
        if (i) g_string_append_c(s, ' ');
        g_string_append_c(s, g_ascii_toupper(c->cmd) == 'Z' ? 'Z' : c->cmd);
        for (int k = 0; k < c->n; k++) {
            g_string_append_c(s, ' ');
            fmt_num(s, c->v[k]);
        }
    }
    g_string_append(s, "\")");
}

static gboolean
parse_path_fn(sh_cur *c, sh_shape *sh)
{
    const ns_css_component *t = peek(c);
    if (is_ident(t, "nonzero") || is_ident(t, "evenodd")) {
        sh->evenodd = is_ident(t, "evenodd");
        c->pos++;
        t = peek(c);
        if (!t || t->type != NS_CSS_COMPONENT_COMMA) return FALSE;
        c->pos++;
        t = peek(c);
    }
    if (!t || t->type != NS_CSS_COMPONENT_STRING || !t->value) return FALSE;
    c->pos++;
    sh->path = path_parse(t->value);
    return sh->path && peek(c) == NULL;
}

enum { SC_MOVE, SC_LINE, SC_HLINE, SC_VLINE, SC_CURVE, SC_SMOOTH, SC_ARC, SC_CLOSE };
enum { AN_NONE, AN_START, AN_END, AN_ORIGIN };

typedef struct {
    gboolean is_pos;
    sh_pos pos;
    sh_len x, y;
    int anchor;
} sh_pt;

typedef struct {
    int kind;
    gboolean by;
    sh_pt end;
    const char *hv_kw;
    double hv_kw_pct;
    sh_len hv;
    int n_ctrl;
    sh_pt ctrl[2];
    int n_r;
    sh_len r[2];
    gboolean cw, large;
    double rot_v;
    char rot_unit[6];
} sh_scmd;

static const char *const sc_names[] = {
    "move", "line", "hline", "vline", "curve", "smooth", "arc", "close",
};

static const char *const anchor_names[] = { "", "start", "end", "origin" };

static gboolean
comma_or_end(sh_cur *c)
{
    const ns_css_component *t = peek(c);
    return !t || t->type == NS_CSS_COMPONENT_COMMA;
}

static gboolean
parse_pair(sh_cur *c, sh_pt *p)
{
    memset(p, 0, sizeof *p);
    return parse_len(c, &p->x, TRUE) && parse_len(c, &p->y, TRUE);
}

static gboolean
parse_end_pos(sh_cur *c, sh_pt *p)
{
    memset(p, 0, sizeof *p);
    p->is_pos = TRUE;
    return parse_position(c, &p->pos) && p->pos.n > 0;
}

static gboolean
parse_control(sh_cur *c, sh_pt *p)
{
    if (!parse_end_pos(c, p)) return FALSE;
    const sh_tok *t = p->pos.t;
    if (p->pos.n == 2 && !t[0].is_kw && !t[1].is_kw) {
        sh_len x = t[0].len, y = t[1].len;
        memset(p, 0, sizeof *p);
        p->x = x;
        p->y = y;
        const ns_css_component *k = peek(c);
        if (is_ident(k, "from")) {
            c->pos++;
            k = peek(c);
            if (is_ident(k, "start")) p->anchor = AN_START;
            else if (is_ident(k, "end")) p->anchor = AN_END;
            else if (is_ident(k, "origin")) p->anchor = AN_ORIGIN;
            else return FALSE;
            c->pos++;
        }
    }
    return TRUE;
}

static gboolean
parse_angle_deg(const ns_css_component *t, double *deg)
{
    if (!t) return FALSE;
    if (t->type == NS_CSS_COMPONENT_NUMBER && t->number == 0) {
        *deg = 0;
        return TRUE;
    }
    if (t->type != NS_CSS_COMPONENT_DIMENSION || !t->value) return FALSE;
    const char *u = t->value;
    if (g_ascii_strcasecmp(u, "deg") == 0) *deg = t->number;
    else if (g_ascii_strcasecmp(u, "rad") == 0) *deg = t->number * 180.0 / G_PI;
    else if (g_ascii_strcasecmp(u, "grad") == 0) *deg = t->number * 0.9;
    else if (g_ascii_strcasecmp(u, "turn") == 0) *deg = t->number * 360.0;
    else return FALSE;
    return TRUE;
}

static gboolean
parse_shape_command(sh_cur *c, sh_scmd *cmd)
{
    const ns_css_component *t = peek(c);
    memset(cmd, 0, sizeof *cmd);
    int kind = -1;
    for (int i = 0; i < (int)G_N_ELEMENTS(sc_names); i++)
        if (is_ident(t, sc_names[i])) kind = i;
    if (kind < 0) return FALSE;
    c->pos++;
    cmd->kind = kind;
    if (kind == SC_CLOSE) return comma_or_end(c);
    t = peek(c);
    if (is_ident(t, "by")) cmd->by = TRUE;
    else if (!is_ident(t, "to")) return FALSE;
    c->pos++;
    if (kind == SC_HLINE || kind == SC_VLINE) {
        t = peek(c);
        if (!cmd->by && t && t->type == NS_CSS_COMPONENT_IDENT && t->value) {
            static const struct { const char *kw; double pct; gboolean h; } kws[] = {
                { "left", 0, TRUE }, { "center", 50, TRUE }, { "right", 100, TRUE },
                { "x-start", 0, TRUE }, { "x-end", 100, TRUE },
                { "top", 0, FALSE }, { "center", 50, FALSE }, { "bottom", 100, FALSE },
                { "y-start", 0, FALSE }, { "y-end", 100, FALSE },
            };
            for (gsize i = 0; i < G_N_ELEMENTS(kws); i++) {
                if (kws[i].h != (kind == SC_HLINE) || !is_ident(t, kws[i].kw)) continue;
                cmd->hv_kw = kws[i].kw;
                cmd->hv_kw_pct = kws[i].pct;
            }
            if (!cmd->hv_kw) return FALSE;
            c->pos++;
            return comma_or_end(c);
        }
        return parse_len(c, &cmd->hv, TRUE) && comma_or_end(c);
    }
    if (!(cmd->by ? parse_pair(c, &cmd->end) : parse_end_pos(c, &cmd->end))) return FALSE;
    if (kind == SC_MOVE || kind == SC_LINE) return comma_or_end(c);
    t = peek(c);
    if (kind == SC_CURVE || kind == SC_SMOOTH) {
        if (!is_ident(t, "with")) return kind == SC_SMOOTH && comma_or_end(c);
        c->pos++;
        if (!parse_control(c, &cmd->ctrl[0])) return FALSE;
        cmd->n_ctrl = 1;
        t = peek(c);
        if (kind == SC_CURVE && t && t->type == NS_CSS_COMPONENT_DELIM && t->delimiter == '/') {
            c->pos++;
            if (!parse_control(c, &cmd->ctrl[1])) return FALSE;
            cmd->n_ctrl = 2;
        }
        return comma_or_end(c);
    }
    if (!is_ident(t, "of")) return FALSE;
    c->pos++;
    cmd->n_r = parse_len_list(c, cmd->r, 2, TRUE);
    if (cmd->n_r == 0) return FALSE;
    gboolean seen_sweep = FALSE, seen_size = FALSE, seen_rot = FALSE;
    while ((t = peek(c)) && t->type != NS_CSS_COMPONENT_COMMA) {
        if (!seen_sweep && (is_ident(t, "cw") || is_ident(t, "ccw"))) {
            cmd->cw = is_ident(t, "cw");
            seen_sweep = TRUE;
            c->pos++;
        } else if (!seen_size && (is_ident(t, "large") || is_ident(t, "small"))) {
            cmd->large = is_ident(t, "large");
            seen_size = TRUE;
            c->pos++;
        } else if (!seen_rot && is_ident(t, "rotate")) {
            c->pos++;
            t = peek(c);
            double deg;
            if (!parse_angle_deg(t, &deg)) return FALSE;
            c->pos++;
            cmd->rot_v = t->number;
            g_strlcpy(cmd->rot_unit, t->type == NS_CSS_COMPONENT_DIMENSION ? t->value : "deg",
                      sizeof cmd->rot_unit);
            for (char *u = cmd->rot_unit; *u; u++) *u = g_ascii_tolower(*u);
            seen_rot = TRUE;
        } else {
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean
parse_shape_shape(sh_cur *c, sh_shape *sh)
{
    const ns_css_component *t = peek(c);
    if (is_ident(t, "nonzero") || is_ident(t, "evenodd")) {
        sh->evenodd = is_ident(t, "evenodd");
        c->pos++;
        t = peek(c);
    }
    if (!is_ident(t, "from")) return FALSE;
    c->pos++;
    sh_pt from;
    if (!parse_end_pos(c, &from)) return FALSE;
    sh->shape_from = from.pos;
    t = peek(c);
    if (!t || t->type != NS_CSS_COMPONENT_COMMA) return FALSE;
    sh->scmds = g_array_new(FALSE, FALSE, sizeof(sh_scmd));
    while ((t = peek(c))) {
        if (t->type != NS_CSS_COMPONENT_COMMA) return FALSE;
        c->pos++;
        sh_scmd cmd;
        if (!parse_shape_command(c, &cmd)) return FALSE;
        g_array_append_val(sh->scmds, cmd);
    }
    return sh->scmds->len > 0;
}

static void
fmt_pt(GString *s, const sh_pt *p, gboolean by, const ns_shape_ctx *cx)
{
    if (p->is_pos) {
        fmt_position(s, &p->pos, cx);
        return;
    }
    fmt_len(s, &p->x, cx);
    g_string_append_c(s, ' ');
    fmt_len(s, &p->y, cx);
    int def = by ? AN_START : AN_ORIGIN;
    if (p->anchor != AN_NONE && p->anchor != def) {
        g_string_append(s, " from ");
        g_string_append(s, anchor_names[p->anchor]);
    }
}

static void
fmt_shape_shape(GString *s, const sh_shape *sh, const ns_shape_ctx *cx)
{
    g_string_append(s, "shape(");
    if (sh->evenodd) g_string_append(s, "evenodd ");
    g_string_append(s, "from ");
    fmt_position(s, &sh->shape_from, cx);
    for (guint i = 0; i < sh->scmds->len; i++) {
        const sh_scmd *m = &g_array_index(sh->scmds, sh_scmd, i);
        g_string_append(s, ", ");
        g_string_append(s, sc_names[m->kind]);
        if (m->kind == SC_CLOSE) continue;
        g_string_append(s, m->by ? " by " : " to ");
        if (m->kind == SC_HLINE || m->kind == SC_VLINE) {
            if (m->hv_kw && !cx) {
                g_string_append(s, m->hv_kw);
            } else if (m->hv_kw) {
                fmt_num(s, m->hv_kw_pct);
                g_string_append_c(s, '%');
            } else {
                fmt_len(s, &m->hv, cx);
            }
            continue;
        }
        fmt_pt(s, &m->end, m->by, cx);
        if (m->n_ctrl) {
            g_string_append(s, " with ");
            fmt_pt(s, &m->ctrl[0], m->by, cx);
            if (m->n_ctrl == 2) {
                g_string_append(s, " / ");
                fmt_pt(s, &m->ctrl[1], m->by, cx);
            }
        }
        if (m->kind == SC_ARC) {
            g_string_append(s, " of ");
            fmt_len(s, &m->r[0], cx);
            if (m->n_r == 2) {
                g_string_append_c(s, ' ');
                fmt_len(s, &m->r[1], cx);
            }
            if (m->cw) g_string_append(s, " cw");
            if (m->large) g_string_append(s, " large");
            if (m->rot_v != 0) {
                g_string_append(s, " rotate ");
                if (cx) {
                    double deg = 0;
                    ns_css_component tmp = { .type = NS_CSS_COMPONENT_DIMENSION,
                                             .number = m->rot_v, .value = (char *)m->rot_unit };
                    parse_angle_deg(&tmp, &deg);
                    fmt_num(s, deg);
                    g_string_append(s, "deg");
                } else {
                    fmt_num(s, m->rot_v);
                    g_string_append(s, m->rot_unit);
                }
            }
        }
    }
    g_string_append_c(s, ')');
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
    if (g_ascii_strcasecmp(f->value, "path") == 0) {
        sh->kind = SHAPE_PATH;
        return parse_path_fn(&c, sh);
    }
    if (g_ascii_strcasecmp(f->value, "shape") == 0) {
        sh->kind = SHAPE_SHAPE;
        return parse_shape_shape(&c, sh);
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
    case SHAPE_SHAPE:
        fmt_shape_shape(s, sh, cx);
        break;
    case SHAPE_PATH: {
        GArray *cmds = g_array_copy(sh->path);
        if (cx) path_absolutize(cmds);
        fmt_path(s, cmds, sh->evenodd);
        g_array_free(cmds, TRUE);
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
    if (v->sh.path) g_array_free(v->sh.path, TRUE);
    if (v->sh.scmds) g_array_free(v->sh.scmds, TRUE);
    v->sh.scmds = NULL;
    v->sh.points = NULL;
    v->sh.path = NULL;
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
    gboolean is_pct;
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
    GArray *path;
    GArray *ccmds;
    sh_cv from[2];
} sh_cshape;

static sh_cv
len_cv(const sh_len *l, const ns_shape_ctx *cx)
{
    sh_cv r;
    len_to_px_pct(l, cx, &r.px, &r.pct);
    r.is_pct = r.px == 0 && (strcmp(l->unit, "%") == 0 || (l->calc && l->sum.pct != 0));
    return r;
}

static sh_cv
axis_cv(int kw, const sh_len *off, const ns_shape_ctx *cx)
{
    sh_cv r = { 0, 0, TRUE };
    if (off) r = len_cv(off, cx);
    if (kw == SH_CENTER) {
        r.px = 0;
        r.pct = 50;
        r.is_pct = TRUE;
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
    if (n < 1) v[0] = (sh_cv){ 0, 0, FALSE };
    if (n < 2) v[1] = v[0];
    if (n < 3) v[2] = v[0];
    if (n < 4) v[3] = v[1];
}

typedef struct {
    int kind;
    gboolean by;
    sh_cv end[2];
    sh_cv hv;
    int n_ctrl;
    sh_cv ctrl[2][2];
    int anchor[2];
    int n_r;
    sh_cv r[2];
    double cw, large, rot;
} sh_ccmd;

static void
pt_cv(const sh_pt *p, const ns_shape_ctx *cx, sh_cv *out)
{
    if (p->is_pos) {
        position_cv(&p->pos, cx, out);
        return;
    }
    out[0] = len_cv(&p->x, cx);
    out[1] = len_cv(&p->y, cx);
}

static GArray *
shape_ccmds(const sh_shape *sh, const ns_shape_ctx *cx, sh_cv *from)
{
    GArray *out = g_array_new(FALSE, FALSE, sizeof(sh_ccmd));
    position_cv(&sh->shape_from, cx, from);
    for (guint i = 0; i < sh->scmds->len; i++) {
        const sh_scmd *m = &g_array_index(sh->scmds, sh_scmd, i);
        sh_ccmd o;
        memset(&o, 0, sizeof o);
        o.kind = m->kind;
        o.by = m->by;
        if (m->kind == SC_HLINE || m->kind == SC_VLINE) {
            o.hv = m->hv_kw ? (sh_cv){ 0, m->hv_kw_pct, TRUE } : len_cv(&m->hv, cx);
        } else if (m->kind != SC_CLOSE) {
            pt_cv(&m->end, cx, o.end);
        }
        o.n_ctrl = m->n_ctrl;
        for (int k = 0; k < m->n_ctrl; k++) {
            pt_cv(&m->ctrl[k], cx, o.ctrl[k]);
            o.anchor[k] = m->ctrl[k].anchor != AN_NONE ? m->ctrl[k].anchor
                          : m->by ? AN_START : AN_ORIGIN;
        }
        o.n_r = m->n_r;
        for (int k = 0; k < m->n_r; k++) o.r[k] = len_cv(&m->r[k], cx);
        o.cw = m->cw;
        o.large = m->large;
        if (m->rot_v != 0) {
            ns_css_component tmp = { .type = NS_CSS_COMPONENT_DIMENSION,
                                     .number = m->rot_v, .value = (char *)m->rot_unit };
            parse_angle_deg(&tmp, &o.rot);
        }
        g_array_append_val(out, o);
    }
    return out;
}

static GArray *
path_ccmds(const GArray *path, sh_cv *from)
{
    GArray *out = g_array_new(FALSE, FALSE, sizeof(sh_ccmd));
    for (guint i = 0; i < path->len; i++) {
        const sh_pcmd *p = &g_array_index(path, sh_pcmd, i);
        char up = g_ascii_toupper(p->cmd);
        gboolean by = g_ascii_islower(p->cmd) && up != 'Z';
        if (i == 0) {
            from[0] = (sh_cv){ p->v[0], 0, FALSE };
            from[1] = (sh_cv){ p->v[1], 0, FALSE };
            continue;
        }
        sh_ccmd o;
        memset(&o, 0, sizeof o);
        o.by = by;
        int anchor = by ? AN_START : AN_ORIGIN;
        switch (up) {
        case 'M': case 'L':
            o.kind = up == 'M' ? SC_MOVE : SC_LINE;
            o.end[0] = (sh_cv){ p->v[0], 0, FALSE };
            o.end[1] = (sh_cv){ p->v[1], 0, FALSE };
            break;
        case 'H': case 'V':
            o.kind = up == 'H' ? SC_HLINE : SC_VLINE;
            o.hv = (sh_cv){ p->v[0], 0, FALSE };
            break;
        case 'C': case 'S': case 'Q': case 'T': {
            o.kind = up == 'C' || up == 'Q' ? SC_CURVE : SC_SMOOTH;
            o.n_ctrl = up == 'C' ? 2 : up == 'T' ? 0 : 1;
            for (int k = 0; k < o.n_ctrl; k++) {
                o.ctrl[k][0] = (sh_cv){ p->v[2 * k], 0, FALSE };
                o.ctrl[k][1] = (sh_cv){ p->v[2 * k + 1], 0, FALSE };
                o.anchor[k] = anchor;
            }
            o.end[0] = (sh_cv){ p->v[p->n - 2], 0, FALSE };
            o.end[1] = (sh_cv){ p->v[p->n - 1], 0, FALSE };
            break;
        }
        case 'A':
            o.kind = SC_ARC;
            o.n_r = 2;
            o.r[0] = (sh_cv){ p->v[0], 0, FALSE };
            o.r[1] = (sh_cv){ p->v[1], 0, FALSE };
            o.rot = p->v[2];
            o.large = p->v[3];
            o.cw = p->v[4];
            o.end[0] = (sh_cv){ p->v[5], 0, FALSE };
            o.end[1] = (sh_cv){ p->v[6], 0, FALSE };
            break;
        default:
            o.kind = SC_CLOSE;
        }
        g_array_append_val(out, o);
    }
    return out;
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
        else o->at[0] = o->at[1] = (sh_cv){ 0, 50, TRUE };
        break;
    case SHAPE_INSET:
        for (int i = 0; i < sh->n_inset; i++) o->inset[i] = len_cv(&sh->inset[i], cx);
        expand_cv(o->inset, sh->n_inset);
        break;
    case SHAPE_RECT: {
        sh_cv e[4];
        for (int i = 0; i < 4; i++)
            e[i] = strcmp(sh->inset[i].unit, "auto") == 0
                ? (sh_cv){ 0, i == 1 || i == 2 ? 100 : 0, TRUE } : len_cv(&sh->inset[i], cx);
        o->inset[0] = e[0];
        o->inset[1] = (sh_cv){ -e[1].px, 100 - e[1].pct, e[1].px == 0 };
        o->inset[2] = (sh_cv){ -e[2].px, 100 - e[2].pct, e[2].px == 0 };
        o->inset[3] = e[3];
        break;
    }
    case SHAPE_XYWH: {
        sh_cv e[4];
        for (int i = 0; i < 4; i++) e[i] = len_cv(&sh->inset[i], cx);
        o->inset[0] = e[1];
        o->inset[1] = (sh_cv){ -e[0].px - e[2].px, 100 - e[0].pct - e[2].pct,
                               e[0].px + e[2].px == 0 };
        o->inset[2] = (sh_cv){ -e[1].px - e[3].px, 100 - e[1].pct - e[3].pct,
                               e[1].px + e[3].px == 0 };
        o->inset[3] = e[0];
        break;
    }
    case SHAPE_PATH:
        o->evenodd = sh->evenodd;
        o->ccmds = path_ccmds(sh->path, o->from);
        o->path = g_array_copy(sh->path);
        path_absolutize(o->path);
        break;
    case SHAPE_SHAPE:
        o->evenodd = sh->evenodd;
        o->ccmds = shape_ccmds(sh, cx, o->from);
        break;
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
    sh_cv r = { a.px * m->wa + b.px * m->wb, a.pct * m->wa + b.pct * m->wb,
                a.is_pct && b.is_pct };
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
    if (v.is_pct && v.px == 0) {
        fmt_num(s, v.pct);
        g_string_append_c(s, '%');
    } else {
        fmt_px_pct(s, v.px, v.pct, FALSE);
    }
    return g_string_free(s, FALSE);
}

static void
append_cv(GString *s, sh_cv v)
{
    if (v.is_pct && v.px == 0) {
        fmt_num(s, v.pct);
        g_string_append_c(s, '%');
        return;
    }
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

static void
append_cv_pair(GString *s, const sh_cv *v)
{
    append_cv(s, v[0]);
    g_string_append_c(s, ' ');
    append_cv(s, v[1]);
}

static gboolean
ccmds_mix(const sh_cv *fa, const GArray *a, const sh_cv *fb, const GArray *b,
          gboolean evenodd, const sh_mix *t, GString *s)
{
    if (a->len != b->len) return FALSE;
    for (guint i = 0; i < a->len; i++) {
        const sh_ccmd *x = &g_array_index(a, sh_ccmd, i);
        const sh_ccmd *y = &g_array_index(b, sh_ccmd, i);
        if (x->kind != y->kind || x->by != y->by || x->n_ctrl != y->n_ctrl ||
            x->n_r != y->n_r)
            return FALSE;
        for (int k = 0; k < x->n_ctrl; k++)
            if (x->anchor[k] != y->anchor[k]) return FALSE;
    }
    g_string_append(s, "shape(");
    if (evenodd) g_string_append(s, "evenodd ");
    g_string_append(s, "from ");
    sh_cv f[2] = { cv_lerp(fa[0], fb[0], t, FALSE), cv_lerp(fa[1], fb[1], t, FALSE) };
    append_cv_pair(s, f);
    for (guint i = 0; i < a->len; i++) {
        const sh_ccmd *x = &g_array_index(a, sh_ccmd, i);
        const sh_ccmd *y = &g_array_index(b, sh_ccmd, i);
        g_string_append(s, ", ");
        g_string_append(s, sc_names[x->kind]);
        if (x->kind == SC_CLOSE) continue;
        g_string_append(s, x->by ? " by " : " to ");
        if (x->kind == SC_HLINE || x->kind == SC_VLINE) {
            append_cv(s, cv_lerp(x->hv, y->hv, t, FALSE));
            continue;
        }
        sh_cv e[2] = { cv_lerp(x->end[0], y->end[0], t, FALSE),
                       cv_lerp(x->end[1], y->end[1], t, FALSE) };
        append_cv_pair(s, e);
        for (int k = 0; k < x->n_ctrl; k++) {
            g_string_append(s, k ? " / " : " with ");
            sh_cv cp[2] = { cv_lerp(x->ctrl[k][0], y->ctrl[k][0], t, FALSE),
                            cv_lerp(x->ctrl[k][1], y->ctrl[k][1], t, FALSE) };
            append_cv_pair(s, cp);
            if (x->anchor[k] != (x->by ? AN_START : AN_ORIGIN)) {
                g_string_append(s, " from ");
                g_string_append(s, anchor_names[x->anchor[k]]);
            }
        }
        if (x->kind == SC_ARC) {
            g_string_append(s, " of ");
            append_cv(s, cv_lerp(x->r[0], y->r[0], t, FALSE));
            if (x->n_r == 2) {
                g_string_append_c(s, ' ');
                append_cv(s, cv_lerp(x->r[1], y->r[1], t, FALSE));
            }
            double rot = x->rot * t->wa + y->rot * t->wb;
            if (rot != 0) {
                g_string_append(s, " rotate ");
                fmt_num(s, rot);
                g_string_append(s, "deg");
            }
            double cw = x->cw * t->wa + y->cw * t->wb;
            double large = x->large * t->wa + y->large * t->wb;
            g_string_append(s, cw != 0 ? " cw" : " ccw");
            g_string_append(s, large != 0 ? " large" : " small");
        }
    }
    g_string_append_c(s, ')');
    return TRUE;
}

static gboolean
cshape_lerp(const sh_cshape *a, const sh_cshape *b, const sh_mix *t, GString *s)
{
    if ((a->kind == SHAPE_SHAPE || b->kind == SHAPE_SHAPE) && a->ccmds && b->ccmds) {
        if (a->evenodd != b->evenodd) return FALSE;
        return ccmds_mix(a->from, a->ccmds, b->from, b->ccmds, a->evenodd, t, s);
    }
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
    case SHAPE_PATH: {
        if (a->evenodd != b->evenodd || a->path->len != b->path->len) return FALSE;
        GArray *mix = g_array_copy(a->path);
        for (guint i = 0; i < mix->len; i++) {
            sh_pcmd *m = &g_array_index(mix, sh_pcmd, i);
            const sh_pcmd *y = &g_array_index(b->path, sh_pcmd, i);
            if (m->cmd != y->cmd ||
                (m->cmd == 'A' && (m->v[3] != y->v[3] || m->v[4] != y->v[4]))) {
                g_array_free(mix, TRUE);
                return FALSE;
            }
            for (int k = 0; k < m->n; k++) {
                if (m->cmd == 'A' && (k == 3 || k == 4)) continue;
                m->v[k] = m->v[k] * t->wa + y->v[k] * t->wb;
            }
        }
        fmt_path(s, mix, a->evenodd);
        g_array_free(mix, TRUE);
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
            if (!t->add && t->wb == 0 && a.sh.kind == SHAPE_SHAPE) {
                g_string_truncate(s, 0);
                fmt_shape(s, &a.sh, cx);
            }
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
        if (ca.path) g_array_free(ca.path, TRUE);
        if (cb.path) g_array_free(cb.path, TRUE);
        if (ca.ccmds) g_array_free(ca.ccmds, TRUE);
        if (cb.ccmds) g_array_free(cb.ccmds, TRUE);
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

char *
ns_css_clip_path_canonical(const char *text, const ns_shape_ctx *cx, gboolean *is_shape)
{
    static const char *const fns[] = {
        "circle", "ellipse", "inset", "rect", "xywh", "polygon", "path", "shape",
    };
    *is_shape = FALSE;
    if (!text) return NULL;
    gboolean valid = TRUE;
    GPtrArray *list = ns_css_component_values_parse(text, -1, &valid);
    for (guint i = 0; i < list->len && !*is_shape; i++) {
        const ns_css_component *t = g_ptr_array_index(list, i);
        if (box_keyword(t, TRUE)) *is_shape = TRUE;
        for (gsize k = 0; t->type == NS_CSS_COMPONENT_FUNCTION && t->value &&
                          k < G_N_ELEMENTS(fns); k++)
            if (g_ascii_strcasecmp(t->value, fns[k]) == 0) *is_shape = TRUE;
    }
    g_ptr_array_free(list, TRUE);
    if (!*is_shape) return NULL;
    sh_value v;
    char *result = NULL;
    if (parse_value(text, &v, TRUE) && !v.none) {
        GString *s = g_string_new(NULL);
        if (v.have_shape) fmt_shape(s, &v.sh, cx);
        if (v.box && !(v.have_shape && strcmp(v.box, "border-box") == 0)) {
            if (s->len) g_string_append_c(s, ' ');
            g_string_append(s, v.box);
        }
        result = g_string_free(s, FALSE);
    }
    value_clear(&v);
    return result;
}

gboolean
ns_css_shape_eval_math(const ns_css_component *t, const ns_shape_ctx *cx, double *num,
                       double *pct, double *px, gboolean *dim)
{
    GPtrArray *one = g_ptr_array_new();
    g_ptr_array_add(one, (gpointer)t);
    sh_cur c = { one, 0, 1 };
    sh_sum sum;
    gboolean ok = calc_value(&c, &sum, 0) && !peek(&c);
    g_ptr_array_free(one, TRUE);
    if (!ok) return FALSE;
    *num = sum.num;
    *pct = sum.pct;
    *px = sum.px;
    for (int i = 0; i < SH_REL; i++)
        if (sum.rel[i] != 0) *px += sum.rel[i] * rel_factor(i, cx);
    *dim = sum.dim;
    return TRUE;
}

static double
cv_resolve(sh_cv v, double basis)
{
    return v.px + v.pct * basis / 100.0;
}

static int
clip_box_index(const char *box)
{
    if (!box) return NS_CLIP_BOX_BORDER;
    if (strcmp(box, "margin-box") == 0) return NS_CLIP_BOX_MARGIN;
    if (strcmp(box, "padding-box") == 0) return NS_CLIP_BOX_PADDING;
    if (strcmp(box, "content-box") == 0) return NS_CLIP_BOX_CONTENT;
    return NS_CLIP_BOX_BORDER;
}

static void
clip_scale_radii(ns_clip_shape *o)
{
    double f = 1.0;
    double sums[4] = {
        o->rh[0] + o->rh[1], o->rv[1] + o->rv[2],
        o->rh[2] + o->rh[3], o->rv[3] + o->rv[0],
    };
    double sides[4] = { o->w, o->h, o->w, o->h };
    for (int i = 0; i < 4; i++)
        if (sums[i] > 0 && sides[i] / sums[i] < f) f = sides[i] / sums[i];
    for (int i = 0; i < 4; i++) {
        o->rh[i] *= f;
        o->rv[i] *= f;
    }
}

static double
clip_side_radius(const sh_crad *r, double center, double lo, double hi, double basis)
{
    double closest = MIN(fabs(center - lo), fabs(hi - center));
    double farthest = MAX(fabs(center - lo), fabs(hi - center));
    if (!r->is_kw) return MAX(0.0, cv_resolve(r->v, basis));
    return strcmp(r->kw, "farthest-side") == 0 ? farthest : closest;
}

static gboolean
clip_shape_from_cshape(const sh_cshape *c, const double *ref, ns_clip_shape *o)
{
    double x = ref[0], y = ref[1], w = ref[2], h = ref[3];
    switch (c->kind) {
    case SHAPE_INSET: {
        double top = cv_resolve(c->inset[0], h), right = cv_resolve(c->inset[1], w);
        double bottom = cv_resolve(c->inset[2], h), left = cv_resolve(c->inset[3], w);
        if (top + bottom > h && top + bottom > 0) {
            double f = h / (top + bottom);
            top *= f;
            bottom *= f;
        }
        if (left + right > w && left + right > 0) {
            double f = w / (left + right);
            left *= f;
            right *= f;
        }
        o->kind = NS_CLIP_SHAPE_INSET;
        o->x = x + left;
        o->y = y + top;
        o->w = MAX(0.0, w - left - right);
        o->h = MAX(0.0, h - top - bottom);
        for (int i = 0; i < 4; i++) {
            o->rh[i] = MAX(0.0, cv_resolve(c->rh[i], w));
            o->rv[i] = MAX(0.0, cv_resolve(c->rv[i], h));
        }
        clip_scale_radii(o);
        return TRUE;
    }
    case SHAPE_CIRCLE:
    case SHAPE_ELLIPSE:
        o->kind = NS_CLIP_SHAPE_ELLIPSE;
        o->cx = x + cv_resolve(c->at[0], w);
        o->cy = y + cv_resolve(c->at[1], h);
        if (c->kind == SHAPE_CIRCLE && c->r[0].is_kw) {
            double rx = clip_side_radius(&c->r[0], o->cx, x, x + w, w);
            double ry = clip_side_radius(&c->r[0], o->cy, y, y + h, h);
            o->rx = o->ry = strcmp(c->r[0].kw, "farthest-side") == 0 ? MAX(rx, ry)
                                                                      : MIN(rx, ry);
        } else if (c->kind == SHAPE_CIRCLE) {
            o->rx = o->ry = MAX(0.0, cv_resolve(c->r[0].v, hypot(w, h) / G_SQRT2));
        } else {
            o->rx = clip_side_radius(&c->r[0], o->cx, x, x + w, w);
            o->ry = clip_side_radius(&c->r[1], o->cy, y, y + h, h);
        }
        return TRUE;
    case SHAPE_POLYGON:
        if (!c->pts || c->pts->len < 2) return FALSE;
        o->kind = NS_CLIP_SHAPE_POLYGON;
        o->evenodd = c->evenodd;
        o->points = g_array_sized_new(FALSE, FALSE, sizeof(double), c->pts->len);
        for (guint i = 0; i + 1 < c->pts->len; i += 2) {
            double px = x + cv_resolve(g_array_index(c->pts, sh_cv, i), w);
            double py = y + cv_resolve(g_array_index(c->pts, sh_cv, i + 1), h);
            g_array_append_val(o->points, px);
            g_array_append_val(o->points, py);
        }
        return TRUE;
    default:
        return FALSE;
    }
}

gboolean
ns_css_clip_shape_resolve(const char *text, const double boxes[NS_CLIP_BOX_COUNT][4],
                          ns_clip_shape *out)
{
    memset(out, 0, sizeof *out);
    sh_value v;
    gboolean ok = parse_value(text, &v, TRUE) && !v.none;
    if (ok) {
        int box = clip_box_index(v.box);
        const double *ref = boxes[box];
        out->box = box;
        if (!v.have_shape) {
            out->kind = NS_CLIP_SHAPE_BOX;
            out->x = ref[0];
            out->y = ref[1];
            out->w = ref[2];
            out->h = ref[3];
        } else {
            static const ns_shape_ctx px_only = { 0 };
            sh_cshape c;
            to_cshape(&v.sh, &px_only, &c);
            ok = clip_shape_from_cshape(&c, ref, out);
            if (c.pts) g_array_free(c.pts, TRUE);
            if (c.path) g_array_free(c.path, TRUE);
            if (c.ccmds) g_array_free(c.ccmds, TRUE);
        }
    }
    value_clear(&v);
    return ok;
}

void
ns_clip_shape_clear(ns_clip_shape *s)
{
    if (s->points) g_array_free(s->points, TRUE);
    s->points = NULL;
}
