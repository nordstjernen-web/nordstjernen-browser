/* Nordstjernen — block layout.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "layout.h"

#include <math.h>
#include "ns_pango.h"
#include <stdlib.h>
#include <string.h>

#include "css.h"
#include "html.h"
#include "image.h"
#include "svg.h"
#include "mathml.h"
#include "net.h"
#include "paint.h"

#define length_or ns_css_length_or

static void hit_enter_box(const ns_box *b, double *x, double *y);

static double
length_resolve(const ns_css_value *v, double basis, double fallback)
{
    if (!v) return fallback;
    if (ns_css_calc_is_math_fn(v))
        return ns_css_calc_math_fn_px(v, basis);
    if (v->kind == NS_CSS_V_CALC)
        return v->u.calc.pct / 100.0 * basis + v->u.calc.px;
    if (v->kind != NS_CSS_V_LENGTH) return fallback;
    if (v->u.length.unit == NS_CSS_UNIT_PX ||
        v->u.length.unit == NS_CSS_UNIT_NUMBER) return v->u.length.v;
    if (v->u.length.unit == NS_CSS_UNIT_EM ||
        v->u.length.unit == NS_CSS_UNIT_REM) return v->u.length.v * 16.0;
    if (v->u.length.unit == NS_CSS_UNIT_PERCENT)
        return v->u.length.v * basis / 100.0;
    if (v->u.length.unit == NS_CSS_UNIT_VW)
        return v->u.length.v * ns_css_viewport_w() / 100.0;
    if (v->u.length.unit == NS_CSS_UNIT_VH)
        return v->u.length.v * ns_css_viewport_h() / 100.0;
    if (v->u.length.unit == NS_CSS_UNIT_VMIN) {
        double m = MIN(ns_css_viewport_w(), ns_css_viewport_h());
        return v->u.length.v * m / 100.0;
    }
    if (v->u.length.unit == NS_CSS_UNIT_VMAX) {
        double m = MAX(ns_css_viewport_w(), ns_css_viewport_h());
        return v->u.length.v * m / 100.0;
    }
    if (v->u.length.unit == NS_CSS_UNIT_CQW) {
        double cw = ns_css_container_w();
        return v->u.length.v * (cw > 0 ? cw : ns_css_viewport_w()) / 100.0;
    }
    if (v->u.length.unit == NS_CSS_UNIT_CQH) {
        double ch = ns_css_container_h();
        return v->u.length.v * (ch > 0 ? ch : ns_css_viewport_h()) / 100.0;
    }
    if (v->u.length.unit == NS_CSS_UNIT_CQMIN ||
        v->u.length.unit == NS_CSS_UNIT_CQMAX) {
        double cw = ns_css_container_w(); if (cw <= 0) cw = ns_css_viewport_w();
        double ch = ns_css_container_h(); if (ch <= 0) ch = ns_css_viewport_h();
        double m = v->u.length.unit == NS_CSS_UNIT_CQMIN ? MIN(cw, ch) : MAX(cw, ch);
        return v->u.length.v * m / 100.0;
    }
    return fallback;
}

static double
length_resolve_nonnegative(const ns_css_value *v, double basis,
                           double fallback)
{
    double resolved = length_resolve(v, basis, fallback);
    return resolved < 0 ? 0 : resolved;
}

double
ns_text_indent_px(const ns_style *s, double basis)
{
    const ns_css_value *v = s ? s->values[NS_CSS_TEXT_INDENT] : NULL;
    return v ? length_resolve(v, basis, 0) : 0;
}

static gboolean
length_is_auto(const ns_css_value *v)
{
    return v && v->kind == NS_CSS_V_KEYWORD && v->u.keyword &&
           strcmp(v->u.keyword, "auto") == 0;
}

static const char *
abs_self_alignment(const ns_box *abox, ns_css_prop prop)
{
    const char *k = abox->style ? ns_style_keyword(abox->style, prop) : NULL;
    if (!k || strcmp(k, "auto") == 0) k = "normal";
    if (g_str_has_prefix(k, "safe ")) k += 5;
    else if (g_str_has_prefix(k, "unsafe ")) k += 7;
    if (strcmp(k, "normal") == 0) {
        gboolean replaced = abox->kind == NS_BOX_IMAGE ||
                            abox->kind == NS_BOX_VIDEO || abox->kind == NS_BOX_SVG;
        k = replaced ? "start" : "stretch";
    }
    return k;
}

static double
aspect_ratio_number(const ns_css_value *v, gboolean *with_auto)
{
    if (with_auto) *with_auto = FALSE;
    if (!v || v->kind != NS_CSS_V_SIZE || v->u.size.w_unit != NS_CSS_UNIT_NUMBER ||
        v->u.size.h_unit != NS_CSS_UNIT_NUMBER)
        return -1;
    if (with_auto) *with_auto = v->u.size.w_auto;
    return v->u.size.w > 0 && v->u.size.h > 0 ? v->u.size.w / v->u.size.h : -1;
}

static gboolean
value_is_percent(const ns_css_value *v)
{
    if (!v) return FALSE;
    if (v->kind == NS_CSS_V_LENGTH && v->u.length.unit == NS_CSS_UNIT_PERCENT)
        return TRUE;
    if (v->kind == NS_CSS_V_CALC && v->u.calc.pct != 0.0)
        return TRUE;
    if (ns_css_calc_is_math_fn(v))
        for (int i = 0; i < v->u.calc.n_args; i++)
            if (v->u.calc.args[i].pct != 0.0) return TRUE;
    return FALSE;
}

static gboolean
box_is_doc_root(const ns_box *b)
{
    return b && b->dom && b->dom->name &&
           (strcmp(b->dom->name, "html") == 0 ||
            strcmp(b->dom->name, "body") == 0);
}

static double containing_block_definite_height(const ns_box *box);
static gboolean style_is_absolute_or_fixed(const ns_style *s);
static gboolean height_keyword_stretches(const ns_css_value *v);
static gboolean size_keyword_is_intrinsic(const ns_css_value *v);
static double intrinsic_keyword_width(ns_box *box, const char *kw,
                                      const ns_style *mi, double avail);

static double
resolve_used_height(const ns_box *box, const ns_css_value *hv,
                    double width_basis, double fallback)
{
    if (!hv) return fallback;
    if (value_is_percent(hv)) {
        double vh;
        if (box_is_doc_root(box)) {
            vh = ns_css_viewport_h();
        } else if (box && box->style &&
                   ns_css_keyword_is(box->style->values[NS_CSS_POSITION],
                                     "fixed")) {
            vh = ns_css_viewport_h();
        } else {
            vh = containing_block_definite_height(box);
            if (vh < 0) return fallback;
        }
        if (hv->kind == NS_CSS_V_CALC)
            return hv->u.calc.pct / 100.0 * vh + hv->u.calc.px;
        return hv->u.length.v * vh / 100.0;
    }
    return length_resolve(hv, width_basis, fallback);
}

static double
clamp_height_minmax_px(const ns_style *s, double h)
{
    if (!s || h < 0) return h;
    const ns_css_value *mx = s->values[NS_CSS_MAX_HEIGHT];
    if (mx && mx->kind == NS_CSS_V_LENGTH &&
        mx->u.length.unit != NS_CSS_UNIT_PERCENT) {
        double m = length_resolve(mx, 0, -1);
        if (m >= 0 && h > m) h = m;
    }
    const ns_css_value *mn = s->values[NS_CSS_MIN_HEIGHT];
    if (mn && mn->kind == NS_CSS_V_LENGTH &&
        mn->u.length.unit != NS_CSS_UNIT_PERCENT) {
        double m = length_resolve(mn, 0, -1);
        if (m >= 0 && h < m) h = m;
    }
    return h;
}

static double
specified_height_to_content(const ns_box *b, double h)
{
    if (h < 0 || !b->style ||
        !ns_css_keyword_is(b->style->values[NS_CSS_BOX_SIZING], "border-box"))
        return h;
    h -= b->padding.top + b->padding.bottom + b->border.top + b->border.bottom;
    return h < 0 ? 0 : h;
}

static double
containing_block_definite_height(const ns_box *box)
{
    if (box && box->cb_height_override > 0) return box->cb_height_override;
    const ns_box *p = box ? box->parent : NULL;
    while (p && !p->style) p = p->parent;
    if (!p) return -1;
    if (p->definite_height > 0) return p->definite_height;
    const ns_css_value *h = p->style->values[NS_CSS_HEIGHT];
    if (height_keyword_stretches(h)) {
        double base = containing_block_definite_height(p);
        if (base < 0) return -1;
        double inner = base - p->margin.top - p->margin.bottom
                     - p->border.top - p->border.bottom
                     - p->padding.top - p->padding.bottom;
        return clamp_height_minmax_px(p->style, inner > 0 ? inner : 0);
    }
    if (h && h->kind == NS_CSS_V_KEYWORD) h = NULL;
    if (!h) {
        const ns_css_value *top = p->style->values[NS_CSS_TOP];
        const ns_css_value *bottom = p->style->values[NS_CSS_BOTTOM];
        if (style_is_absolute_or_fixed(p->style) &&
            top && !length_is_auto(top) &&
            bottom && !length_is_auto(bottom) &&
            p->content_height > 0)
            return p->content_height;
        double ratio = aspect_ratio_number(p->style->values[NS_CSS_ASPECT_RATIO], NULL);
        if (ratio > 0 && p->content_width > 0)
            return p->content_width / ratio;
        return -1;
    }
    if (value_is_percent(h)) {
        double base = box_is_doc_root(p) ? ns_css_viewport_h()
                                         : containing_block_definite_height(p);
        if (base < 0) return -1;
        double ch = h->kind == NS_CSS_V_CALC
            ? h->u.calc.pct / 100.0 * base + h->u.calc.px
            : h->u.length.v * base / 100.0;
        return specified_height_to_content(p, clamp_height_minmax_px(p->style, ch));
    }
    if (p->content_height > 0) return p->content_height;
    return specified_height_to_content(
        p, clamp_height_minmax_px(p->style, length_resolve(h, 0, -1)));
}

static double
resolve_height_with_basis(const ns_css_value *hv, double width_basis,
                          double height_basis, double fallback)
{
    if (!hv) return fallback;
    if (hv->kind == NS_CSS_V_CALC) {
        if (ns_css_calc_is_math_fn(hv) && height_basis >= 0)
            return ns_css_calc_math_fn_px(hv, height_basis);
        if (hv->u.calc.pct != 0.0 && height_basis >= 0)
            return hv->u.calc.pct / 100.0 * height_basis + hv->u.calc.px;
        return length_resolve(hv, width_basis, fallback);
    }
    if (hv->kind == NS_CSS_V_LENGTH &&
        hv->u.length.unit == NS_CSS_UNIT_PERCENT) {
        if (height_basis >= 0)
            return hv->u.length.v * height_basis / 100.0;
        return fallback;
    }
    return length_resolve(hv, width_basis, fallback);
}

#define is_keyword ns_css_keyword_is
#define keyword_is ns_css_keyword_is

static gboolean
display_is_atomic_inline_container(ns_display d)
{
    return ns_display_is_atomic_inline(d) && d.inner != NS_DISPLAY_INNER_TABLE;
}

static gboolean
style_is_block(const ns_style *s)
{
    return ns_display_generates_own_box(ns_css_display_of(s));
}

static gboolean
style_is_block_level(const ns_style *s)
{
    return ns_display_is_block_level(ns_css_display_of(s));
}

static gboolean
style_display_is_table(const ns_style *s)
{
    return ns_display_is_table_wrapper(ns_css_display_of(s));
}

static gboolean
style_display_is_table_row(const ns_style *s)
{
    return ns_display_is(ns_css_display_of(s), NS_DISPLAY_INTERNAL_TABLE_ROW);
}

static gboolean
style_display_is_table_cell(const ns_style *s)
{
    return ns_display_is(ns_css_display_of(s), NS_DISPLAY_INTERNAL_TABLE_CELL);
}

static gboolean
style_display_is_table_caption(const ns_style *s)
{
    return ns_display_is(ns_css_display_of(s),
                         NS_DISPLAY_INTERNAL_TABLE_CAPTION);
}

static gboolean
style_is_flex_container(const ns_style *s)
{
    return ns_display_is_flex_container(ns_css_display_of(s));
}

static gboolean
style_is_grid_container(const ns_style *s)
{
    return ns_display_is_grid_container(ns_css_display_of(s));
}

static const char *
keyword_or(const ns_style *s, ns_css_prop p, const char *fallback)
{
    if (!s || !s->values[p]) return fallback;
    const ns_css_value *v = s->values[p];
    if (v->kind != NS_CSS_V_KEYWORD || !v->u.keyword) return fallback;
    return ns_css_alignment_base(v->u.keyword);
}

static const char *
flex_direction_of(const ns_style *s)
{
    const char *dir = keyword_or(s, NS_CSS_FLEX_DIRECTION, "row");
    int writing_mode = ns_css_writing_mode(s);
    if (!writing_mode) return dir;
    gboolean reverse = strstr(dir, "-reverse") != NULL;
    if (strncmp(dir, "row", 3) == 0) {
        gboolean rtl = keyword_is(s->values[NS_CSS_DIRECTION], "rtl");
        return reverse != rtl ? "column-reverse" : "column";
    }
    gboolean right_to_left = writing_mode == 1;
    return reverse != right_to_left ? "row-reverse" : "row";
}

static gboolean
self_start_is_far_side(const ns_style *s, gboolean horizontal_axis)
{
    int writing_mode = ns_css_writing_mode(s);
    gboolean rtl = s && keyword_is(s->values[NS_CSS_DIRECTION], "rtl");
    if (horizontal_axis)
        return writing_mode == 1 ? TRUE : writing_mode == 2 ? FALSE : rtl;
    if (!writing_mode) return FALSE;
    gboolean upward = keyword_is(s->values[NS_CSS_WRITING_MODE], "sideways-lr");
    return upward != rtl;
}

static double
number_or(const ns_css_value *v, double fallback)
{
    if (!v) return fallback;
    if (v->kind == NS_CSS_V_LENGTH) return v->u.length.v;
    return fallback;
}

static int
box_css_order(const ns_box *b)
{
    if (!b || !b->style) return 0;
    return (int)number_or(b->style->values[NS_CSS_ORDER], 0);
}

static void
reorder_children_by_order(ns_box *box)
{
    int n = 0;
    gboolean any = FALSE;
    for (ns_box *c = box->first_child; c; c = c->next_sibling) {
        n++;
        if (box_css_order(c) != 0) any = TRUE;
    }
    if (n < 2 || !any) return;
    ns_box **arr = g_new(ns_box *, n);
    int i = 0;
    for (ns_box *c = box->first_child; c; c = c->next_sibling) arr[i++] = c;
    for (int a = 1; a < n; a++) {
        ns_box *key = arr[a];
        int ko = box_css_order(key);
        int b = a - 1;
        while (b >= 0 && box_css_order(arr[b]) > ko) {
            arr[b + 1] = arr[b];
            b--;
        }
        arr[b + 1] = key;
    }
    for (int k = 0; k < n; k++)
        arr[k]->next_sibling = (k + 1 < n) ? arr[k + 1] : NULL;
    box->first_child = arr[0];
    box->last_child  = arr[n - 1];
    g_free(arr);
}

static gboolean
style_is_multicol(const ns_style *s)
{
    if (!s) return FALSE;
    const ns_css_value *cc = s->values[NS_CSS_COLUMN_COUNT];
    if (cc && cc->kind == NS_CSS_V_LENGTH && cc->u.length.v >= 2) return TRUE;
    const ns_css_value *cw = s->values[NS_CSS_COLUMN_WIDTH];
    return cw && cw->kind == NS_CSS_V_LENGTH && cw->u.length.v > 0;
}

static gboolean
style_is_absolute_or_fixed(const ns_style *s)
{
    const ns_css_value *v = s ? s->values[NS_CSS_POSITION] : NULL;
    return keyword_is(v, "absolute") || keyword_is(v, "fixed");
}

static gboolean
style_is_none(const ns_style *s)
{
    return ns_display_is_none(ns_css_display_of(s));
}

static gboolean
style_is_contents(const ns_style *s)
{
    return ns_display_is_contents(ns_css_display_of(s));
}

static gboolean
style_content_visibility_hidden(const ns_style *s)
{
    return s && s->values[NS_CSS_CONTENT_VISIBILITY] &&
           is_keyword(s->values[NS_CSS_CONTENT_VISIBILITY], "hidden");
}

static gboolean
style_contains_inline_size(const ns_style *s)
{
    const ns_css_value *ct = s ? s->values[NS_CSS_CONTAINER_TYPE] : NULL;
    if (!ct || ct->kind != NS_CSS_V_KEYWORD || !ct->u.keyword) return FALSE;
    return g_ascii_strcasecmp(ct->u.keyword, "size") == 0 ||
           g_ascii_strcasecmp(ct->u.keyword, "inline-size") == 0;
}

static gboolean
text_is_ws_only(const char *text)
{
    if (!text) return TRUE;
    for (const char *p = text; *p; p++)
        if (!g_ascii_isspace((unsigned char)*p)) return FALSE;
    return TRUE;
}


static double
border_side_width(const ns_style *s, ns_css_prop width_prop,
                  ns_css_prop style_prop)
{
    const ns_css_value *st = s->values[style_prop];
    if (st && st->kind == NS_CSS_V_KEYWORD && st->u.keyword &&
        (strcmp(st->u.keyword, "none") == 0 ||
         strcmp(st->u.keyword, "hidden") == 0))
        return 0;
    return length_or(s->values[width_prop], 0);
}

static void
edges_from_style(const ns_style *s, double basis,
                 ns_edges *margin, ns_edges *padding, ns_edges *border)
{
    if (!s) {
        memset(margin, 0, sizeof(*margin));
        memset(padding, 0, sizeof(*padding));
        memset(border, 0, sizeof(*border));
        return;
    }
    margin->top    = length_resolve(s->values[NS_CSS_MARGIN_TOP],    basis, 0);
    margin->right  = length_resolve(s->values[NS_CSS_MARGIN_RIGHT],  basis, 0);
    margin->bottom = length_resolve(s->values[NS_CSS_MARGIN_BOTTOM], basis, 0);
    margin->left   = length_resolve(s->values[NS_CSS_MARGIN_LEFT],   basis, 0);
    padding->top    = length_resolve_nonnegative(
        s->values[NS_CSS_PADDING_TOP], basis, 0);
    padding->right  = length_resolve_nonnegative(
        s->values[NS_CSS_PADDING_RIGHT], basis, 0);
    padding->bottom = length_resolve_nonnegative(
        s->values[NS_CSS_PADDING_BOTTOM], basis, 0);
    padding->left   = length_resolve_nonnegative(
        s->values[NS_CSS_PADDING_LEFT], basis, 0);
    border->top    = border_side_width(s, NS_CSS_BORDER_TOP_WIDTH,
                                       NS_CSS_BORDER_TOP_STYLE);
    border->right  = border_side_width(s, NS_CSS_BORDER_RIGHT_WIDTH,
                                       NS_CSS_BORDER_RIGHT_STYLE);
    border->bottom = border_side_width(s, NS_CSS_BORDER_BOTTOM_WIDTH,
                                       NS_CSS_BORDER_BOTTOM_STYLE);
    border->left   = border_side_width(s, NS_CSS_BORDER_LEFT_WIDTH,
                                       NS_CSS_BORDER_LEFT_STYLE);
}

static ns_box *g_box_pool[16384];
static int g_box_pool_n;

static ns_box *
box_new(ns_box_kind kind)
{
    ns_box *b;
    if (g_box_pool_n > 0) {
        b = g_box_pool[--g_box_pool_n];
        memset(b, 0, sizeof(*b));
    } else {
        b = g_new0(ns_box, 1);
    }
    b->kind = kind;
    b->colspan = 1;
    b->rowspan = 1;
    return b;
}

static void
link_clear(gpointer data)
{
    ns_link_range *r = data;
    g_free(r->href);
    g_free(r->target);
}

static GArray *inline_links_ensure(ns_box *b);

static ns_box_media *
ns_box_media_ensure(ns_box *b)
{
    if (!b->media) b->media = g_new0(ns_box_media, 1);
    return b->media;
}

static ns_box *
inline_merge_prefix(ns_box *prefix, ns_box *suffix)
{
    if (!prefix) return suffix;
    if (!suffix) return prefix;
    gsize plen = prefix->text ? strlen(prefix->text) : 0;
    gsize slen = suffix->text ? strlen(suffix->text) : 0;
    if (plen > G_MAXSIZE - slen - 1) { ns_box_free(suffix); return prefix; }
    char *combined = g_malloc(plen + slen + 1);
    if (plen) memcpy(combined, prefix->text, plen);
    if (slen) memcpy(combined + plen, suffix->text, slen);
    combined[plen + slen] = '\0';
    g_free(suffix->text);
    suffix->text = combined;

    if (suffix->attrs) {
        for (guint i = 0; i < suffix->attrs->len; i++) {
            ns_inline_attr *a = &g_array_index(suffix->attrs, ns_inline_attr, i);
            a->start += plen;
        }
    }
    if (suffix->links) {
        for (guint i = 0; i < suffix->links->len; i++) {
            ns_link_range *l = &g_array_index(suffix->links, ns_link_range, i);
            l->start += plen;
        }
    }
    if (prefix->attrs) {
        for (guint i = 0; i < prefix->attrs->len; i++) {
            ns_inline_attr a = g_array_index(prefix->attrs, ns_inline_attr, i);
            g_array_append_val(suffix->attrs, a);
        }
    }
    if (suffix->inline_atomics) {
        for (guint i = 0; i < suffix->inline_atomics->len; i++)
            g_array_index(suffix->inline_atomics, ns_inline_atomic, i).byte_off += plen;
    }
    if (prefix->inline_atomics) {
        if (!suffix->inline_atomics)
            suffix->inline_atomics = g_array_new(FALSE, FALSE, sizeof(ns_inline_atomic));
        for (guint i = 0; i < prefix->inline_atomics->len; i++) {
            ns_inline_atomic ia = g_array_index(prefix->inline_atomics, ns_inline_atomic, i);
            if (ia.box && ia.box->parent == prefix) ia.box->parent = suffix;
            g_array_append_val(suffix->inline_atomics, ia);
        }
        g_array_free(prefix->inline_atomics, TRUE);
        prefix->inline_atomics = NULL;
    }
    if (prefix->links) {
        GArray *dst = inline_links_ensure(suffix);
        for (guint i = 0; i < prefix->links->len; i++) {
            ns_link_range src = g_array_index(prefix->links, ns_link_range, i);
            ns_link_range dup = src;
            dup.href   = src.href   ? g_strdup(src.href)   : NULL;
            dup.target = src.target ? g_strdup(src.target) : NULL;
            g_array_append_val(dst, dup);
        }
    }
    ns_box_free(prefix);
    return suffix;
}

static ns_box *
box_new_inline(void)
{
    ns_box *b = box_new(NS_BOX_INLINE);
    b->attrs = g_array_new(FALSE, FALSE, sizeof(ns_inline_attr));
    return b;
}

static GArray *
inline_links_ensure(ns_box *b)
{
    if (!b->links) {
        b->links = g_array_new(FALSE, FALSE, sizeof(ns_link_range));
        g_array_set_clear_func(b->links, link_clear);
    }
    return b->links;
}

static void
box_append_child(ns_box *parent, ns_box *child)
{
    if (parent->last_child && parent->last_child->kind == NS_BOX_INLINE &&
        child->kind == NS_BOX_INLINE && !parent->last_child->style &&
        !child->style) {
        ns_box *prefix = parent->last_child;
        ns_box *previous = NULL;
        for (ns_box *c = parent->first_child; c && c != prefix;
             c = c->next_sibling)
            previous = c;
        ns_box *merged = inline_merge_prefix(prefix, child);
        merged->parent = parent;
        if (previous) previous->next_sibling = merged;
        else parent->first_child = merged;
        parent->last_child = merged;
        return;
    }
    child->parent = parent;
    if (!parent->first_child) parent->first_child = child;
    else                       parent->last_child->next_sibling = child;
    parent->last_child = child;
}

void
ns_box_free(ns_box *box)
{
    if (!box) return;
    GPtrArray *stack = g_ptr_array_new();
    g_ptr_array_add(stack, box);
    while (stack->len > 0) {
        ns_box *cur = g_ptr_array_index(stack, stack->len - 1);
        g_ptr_array_set_size(stack, stack->len - 1);
        for (ns_box *c = cur->first_child; c; ) {
            ns_box *next = c->next_sibling;
            g_ptr_array_add(stack, c);
            c = next;
        }
        if (cur->paint_layout) ns_paint_drop_box_cache(cur);
        if (cur->links) g_array_free(cur->links, TRUE);
        if (cur->attrs) g_array_free(cur->attrs, TRUE);
        if (cur->table_col_hints) g_array_free(cur->table_col_hints, TRUE);
        if (cur->grid_col_tracks) g_array_free(cur->grid_col_tracks, TRUE);
        if (cur->grid_row_tracks) g_array_free(cur->grid_row_tracks, TRUE);
        if (cur->inline_atomics) {
            for (guint i = 0; i < cur->inline_atomics->len; i++) {
                ns_inline_atomic *ia = &g_array_index(cur->inline_atomics,
                                                      ns_inline_atomic, i);
                if (ia->box) g_ptr_array_add(stack, ia->box);
            }
            g_array_free(cur->inline_atomics, TRUE);
        }
        g_free(cur->text);
        if (cur->media) {
            g_free(cur->media->image_src);
            g_free(cur->media->bg_image_src);
            g_free(cur->media->marker_image_src);
            g_free(cur->media->border_image_src);
            if (cur->media->bg_layer_srcs)
                g_ptr_array_free(cur->media->bg_layer_srcs, TRUE);
            if (cur->media->bg_layer_images)
                g_ptr_array_free(cur->media->bg_layer_images, TRUE);
            g_free(cur->media->video_src);
            g_free(cur->media->video_poster);
            g_free(cur->media->video_audio_src);
            g_free(cur->media);
        }
        if (g_box_pool_n < (int)G_N_ELEMENTS(g_box_pool))
            g_box_pool[g_box_pool_n++] = cur;
        else
            g_free(cur);
    }
    g_ptr_array_free(stack, TRUE);
}

static gboolean box_clips_children(const ns_box *b);
static gboolean box_first_baseline(const ns_box *b, double *out);

static gboolean
box_clips_for_page_height(const ns_box *b)
{
    if (!b->parent) return FALSE;
    if (b->dom && b->dom->name &&
        (strcmp(b->dom->name, "html") == 0 ||
         strcmp(b->dom->name, "body") == 0))
        return FALSE;
    return box_clips_children(b);
}

static void
box_walk_max_bottom(const ns_box *b, double *out)
{
    if (!b) return;
    double bottom = b->y + b->content_height;
    if (bottom > *out) *out = bottom;
    if (box_clips_for_page_height(b)) return;
    for (const ns_box *c = b->first_child; c; c = c->next_sibling)
        box_walk_max_bottom(c, out);
}

double
ns_box_max_bottom(const ns_box *root, double seed)
{
    double out = seed;
    box_walk_max_bottom(root, &out);
    return out;
}

static gboolean
is_replaced_block_tag(const char *name)
{
    return name && (strcmp(name, "img") == 0 ||
                    strcmp(name, "picture") == 0 ||
                    strcmp(name, "svg") == 0 ||
                    strcmp(name, "canvas") == 0 ||
                    strcmp(name, "audio") == 0 ||
                    strcmp(name, "video") == 0 ||
                    strcmp(name, "math") == 0 ||
                    strcmp(name, "table") == 0);
}

static gboolean
is_inline_level_replaced(const ns_node *n, GHashTable *styles)
{
    if (!n || n->kind != NS_NODE_ELEMENT || !n->name) return FALSE;
    if (!(strcmp(n->name, "img") == 0 || strcmp(n->name, "svg") == 0 ||
          strcmp(n->name, "picture") == 0 || strcmp(n->name, "audio") == 0 ||
          strcmp(n->name, "video") == 0 || strcmp(n->name, "math") == 0 ||
          strcmp(n->name, "canvas") == 0))
        return FALSE;
    const ns_style *s = styles ? g_hash_table_lookup(styles, n) : NULL;
    return !ns_display_is_block_level(ns_css_display_of(s));
}

static gboolean
node_has_media_metadata(const ns_node *n)
{
    return n && n->kind == NS_NODE_ELEMENT &&
           (ns_element_get_attr(n, NS_MEDIA_SRC_ATTR) != NULL ||
            ns_element_get_attr(n, NS_MEDIA_STREAM_ATTR) != NULL);
}

#define NS_LAYOUT_MAX_DEPTH 512
#define NS_TABLE_MAX_COLS 4096

static gboolean tag_is_non_rendering(const char *name);
static gboolean node_is_non_rendering(const ns_node *n);

static gboolean
node_is_frame_fallback(const ns_node *n)
{
    if (!n || n->kind == NS_NODE_DOCUMENT) return FALSE;
    const ns_node *p = n->parent;
    return p && p->kind == NS_NODE_ELEMENT && p->name &&
           (strcmp(p->name, "iframe") == 0 || strcmp(p->name, "frame") == 0);
}

static GHashTable *g_contains_block_media_cache;

static gboolean
contains_block_media_depth(const ns_node *n, GHashTable *styles, int depth)
{
    if (!n || depth >= NS_LAYOUT_MAX_DEPTH || n->kind != NS_NODE_ELEMENT)
        return FALSE;
    for (const ns_node *c = n->first_child; c; c = c->next_sibling) {
        if (c->kind != NS_NODE_ELEMENT || !c->name) continue;
        if (node_is_non_rendering(c)) continue;
        if (node_has_media_metadata(c) ||
            (is_replaced_block_tag(c->name) &&
             !is_inline_level_replaced(c, styles)) ||
            strcmp(c->name, "iframe") == 0)
            return TRUE;
        if (styles) {
            const ns_style *cs = g_hash_table_lookup(styles, c);
            if (cs) {
                if (style_is_none(cs) || style_is_absolute_or_fixed(cs))
                    continue;
                if (style_is_block_level(cs)) return TRUE;
                if (ns_display_is_atomic_inline(ns_css_display_of(cs)))
                    continue;
            }
        }
        if (contains_block_media_depth(c, styles, depth + 1)) return TRUE;
    }
    return FALSE;
}

static gboolean
contains_block_media(const ns_node *n, GHashTable *styles)
{
    if (!n) return FALSE;
    if (g_contains_block_media_cache) {
        gpointer hit = g_hash_table_lookup(g_contains_block_media_cache, n);
        if (hit) return GPOINTER_TO_INT(hit) == 2;
    }
    gboolean has_media = contains_block_media_depth(n, styles, 0);
    if (g_contains_block_media_cache)
        g_hash_table_insert(g_contains_block_media_cache,
                            (gpointer)n, GINT_TO_POINTER(has_media ? 2 : 1));
    return has_media;
}

static gboolean is_inline_dom(const ns_node *n, GHashTable *styles);

static gboolean
abs_joins_inline_run(const ns_node *n, const ns_style *s, GHashTable *styles)
{
    if (!s || !s->specified_inline || !n->parent) return FALSE;
    const ns_style *parent = g_hash_table_lookup(styles, n->parent);
    if (parent && (style_is_flex_container(parent) ||
                   style_is_grid_container(parent)))
        return FALSE;
    for (const ns_node *p = n->prev_sibling; p; p = p->prev_sibling) {
        if (p->kind == NS_NODE_TEXT) {
            for (const char *t = p->text; t && *t; t++)
                if (!g_ascii_isspace(*t)) return TRUE;
            continue;
        }
        if (p->kind != NS_NODE_ELEMENT) continue;
        const ns_style *ps = g_hash_table_lookup(styles, p);
        if (ps && (style_is_none(ps) || style_is_absolute_or_fixed(ps)))
            continue;
        return is_inline_dom(p, styles);
    }
    return FALSE;
}

static int clear_kind_of(const ns_style *s);

static gboolean
is_inline_dom(const ns_node *n, GHashTable *styles)
{
    if (!n) return FALSE;
    if (n->kind == NS_NODE_TEXT) return TRUE;
    if (n->kind != NS_NODE_ELEMENT) return FALSE;
    if (n->name && strcmp(n->name, "slot") == 0) return FALSE;
    if (node_has_media_metadata(n)) return FALSE;
    for (const ns_node *sc = n->first_child; sc; sc = sc->next_sibling)
        if (sc->kind == NS_NODE_ELEMENT &&
            ns_element_get_attr(sc, NS_SHADOW_ATTR))
            return FALSE;
    if (is_replaced_block_tag(n->name)) {
        if (strcmp(n->name, "table") == 0) return FALSE;
        const ns_style *rs = g_hash_table_lookup(styles, n);
        if (rs && style_is_none(rs)) return FALSE;
        if (rs && style_is_absolute_or_fixed(rs))
            return abs_joins_inline_run(n, rs, styles);
        return is_inline_level_replaced(n, styles);
    }
    const ns_style *s = g_hash_table_lookup(styles, n);
    if (!s) return n->name && strchr(n->name, '-') != NULL;
    if (style_is_none(s)) return FALSE;
    if (style_is_absolute_or_fixed(s)) return abs_joins_inline_run(n, s, styles);
    ns_display d = ns_css_display_of(s);
    if (ns_display_is_table_internal(d)) return FALSE;
    if (ns_display_is_flex_container(d) || ns_display_is_grid_container(d)) {
        if (d.outer == NS_DISPLAY_OUTER_INLINE) return TRUE;
    }
    if (n->name && strcmp(n->name, "br") == 0 && clear_kind_of(s))
        return FALSE;
    if (!style_is_block(s) && contains_block_media(n, styles)) return FALSE;
    if (display_is_atomic_inline_container(d)) return TRUE;
    return !style_is_block(s);
}

static gboolean
node_leaves_inline_run_open(const ns_node *n, GHashTable *styles)
{
    if (n->kind == NS_NODE_COMMENT) return TRUE;
    if (n->kind != NS_NODE_ELEMENT) return FALSE;
    const ns_style *s = g_hash_table_lookup(styles, n);
    return s && style_is_none(s);
}

static gboolean
continues_inline_run(const ns_node *n, GHashTable *styles)
{
    return is_inline_dom(n, styles) || node_leaves_inline_run_open(n, styles);
}

static ns_display_internal
node_table_internal(const ns_node *n, GHashTable *styles)
{
    if (!n || n->kind != NS_NODE_ELEMENT) return NS_DISPLAY_INTERNAL_NONE;
    ns_display d = ns_css_display_of(
        styles ? g_hash_table_lookup(styles, n) : NULL);
    return ns_display_is_table_internal(d) ? d.internal
                                           : NS_DISPLAY_INTERNAL_NONE;
}

static gboolean
node_is_row_group(const ns_node *n, GHashTable *styles)
{
    ns_display_internal k = node_table_internal(n, styles);
    return k == NS_DISPLAY_INTERNAL_TABLE_ROW_GROUP ||
           k == NS_DISPLAY_INTERNAL_TABLE_HEADER_GROUP ||
           k == NS_DISPLAY_INTERNAL_TABLE_FOOTER_GROUP;
}

static gboolean
is_table_row(const ns_node *n, GHashTable *styles)
{
    if (!n || n->kind != NS_NODE_ELEMENT) return FALSE;
    if (n->name && strcmp(n->name, "tr") == 0) return TRUE;
    return style_display_is_table_row(g_hash_table_lookup(styles, n));
}

static gboolean
is_table_box(const ns_node *n, GHashTable *styles)
{
    if (!n || n->kind != NS_NODE_ELEMENT) return FALSE;
    if (n->name && strcmp(n->name, "table") == 0) return TRUE;
    return style_display_is_table(g_hash_table_lookup(styles, n));
}

static gboolean
is_cell_element(const ns_node *n, GHashTable *styles)
{
    if (ns_node_is_element_named(n, "td") ||
        ns_node_is_element_named(n, "th"))
        return TRUE;
    if (!n || n->kind != NS_NODE_ELEMENT) return FALSE;
    return style_display_is_table_cell(g_hash_table_lookup(styles, n));
}

static gboolean
is_table_caption(const ns_node *n, GHashTable *styles)
{
    if (ns_node_is_element_named(n, "caption")) return TRUE;
    if (!n || n->kind != NS_NODE_ELEMENT) return FALSE;
    return style_display_is_table_caption(g_hash_table_lookup(styles, n));
}

static gboolean
node_is_table_internal(const ns_node *n, GHashTable *styles)
{
    return node_table_internal(n, styles) != NS_DISPLAY_INTERNAL_NONE ||
           is_cell_element(n, styles) || is_table_row(n, styles) ||
           is_table_caption(n, styles);
}

static void
collect_rows_recurse(const ns_node *n, GHashTable *styles, GPtrArray *out, int depth)
{
    if (!n || depth >= NS_LAYOUT_MAX_DEPTH) return;
    if (n->kind == NS_NODE_ELEMENT && n->name) {
        if (is_table_row(n, styles)) {
            g_ptr_array_add(out, (gpointer)n);
            return;
        }
        if (n != NULL && (strcmp(n->name, "table") == 0 ||
                          style_display_is_table_caption(g_hash_table_lookup(styles, n)) ||
                          style_display_is_table(g_hash_table_lookup(styles, n))))
            return;
    }
    for (const ns_node *c = n->first_child; c; c = c->next_sibling)
        collect_rows_recurse(c, styles, out, depth + 1);
}

typedef enum { ROW_GROUP_BODY, ROW_GROUP_HEADER, ROW_GROUP_FOOTER } row_group_kind;

static row_group_kind
node_row_group(const ns_node *n, GHashTable *styles)
{
    if (ns_node_is_element_named(n, "thead")) return ROW_GROUP_HEADER;
    if (ns_node_is_element_named(n, "tfoot")) return ROW_GROUP_FOOTER;
    const ns_style *s = styles ? g_hash_table_lookup(styles, n) : NULL;
    ns_display d = ns_css_display_of(s);
    if (ns_display_is(d, NS_DISPLAY_INTERNAL_TABLE_HEADER_GROUP))
        return ROW_GROUP_HEADER;
    if (ns_display_is(d, NS_DISPLAY_INTERNAL_TABLE_FOOTER_GROUP))
        return ROW_GROUP_FOOTER;
    return ROW_GROUP_BODY;
}

static void
collect_rows(const ns_node *table, GHashTable *styles, GPtrArray *out)
{
    if (!table) return;
    GPtrArray *header = g_ptr_array_new();
    GPtrArray *body   = g_ptr_array_new();
    GPtrArray *footer = g_ptr_array_new();
    for (const ns_node *c = table->first_child; c; c = c->next_sibling) {
        row_group_kind g = node_row_group(c, styles);
        GPtrArray *dst = g == ROW_GROUP_HEADER ? header
                       : g == ROW_GROUP_FOOTER ? footer
                                               : body;
        collect_rows_recurse(c, styles, dst, 0);
    }
    for (guint i = 0; i < header->len; i++)
        g_ptr_array_add(out, g_ptr_array_index(header, i));
    for (guint i = 0; i < body->len; i++)
        g_ptr_array_add(out, g_ptr_array_index(body, i));
    for (guint i = 0; i < footer->len; i++)
        g_ptr_array_add(out, g_ptr_array_index(footer, i));
    g_ptr_array_free(header, TRUE);
    g_ptr_array_free(body, TRUE);
    g_ptr_array_free(footer, TRUE);
}

static ns_box *build_block(const ns_node *n, GHashTable *styles);
static void layout_box(ns_box *box, double parent_content_width,
                       const ns_style *inherited_style);
static ns_box *build_inline_run(const ns_node *first, const ns_node *last_excl, GHashTable *styles);
static void translate_subtree(ns_box *box, double dx, double dy);
static ns_box *build_inline_run_no_abs_placeholders(const ns_node *first, const ns_node *last_excl, GHashTable *styles);
static gboolean inline_atomic_needs_layout(const ns_box *ab);
static double pango_layout_line_top(NsPangoLayout *layout, int line_index);
static ns_box *build_pseudo_inline_for(const ns_style *ps, const ns_node *host);
static ns_box *build_pseudo_block_for(const ns_style *ps, const ns_node *host);
static void register_abs_pseudo(const ns_node *host, const ns_style *ps);
static ns_box *build_blockified_inline_item(const ns_node *n, GHashTable *styles,
                                            ns_box **pending_before);
static void layout_block(ns_box *box, double parent_content_width, const ns_style *inherited_style);
static void append_display_contents_children(ns_box *block, const ns_node *n,
                                             GHashTable *styles,
                                             gboolean blockify_children,
                                             ns_box **pending_before);

static gboolean
button_has_replaced_child_depth(const ns_node *n, int depth)
{
    if (depth >= NS_LAYOUT_MAX_DEPTH) return FALSE;
    for (const ns_node *c = n->first_child; c; c = c->next_sibling) {
        if (c->kind == NS_NODE_ELEMENT && c->name &&
            (strcmp(c->name, "svg") == 0 || strcmp(c->name, "img") == 0 ||
             strcmp(c->name, "picture") == 0 || strcmp(c->name, "audio") == 0 ||
             strcmp(c->name, "video") == 0))
            return TRUE;
        if (c->kind == NS_NODE_ELEMENT &&
            button_has_replaced_child_depth(c, depth + 1))
            return TRUE;
    }
    return FALSE;
}

static gboolean
button_has_replaced_child(const ns_node *n)
{
    return button_has_replaced_child_depth(n, 0);
}

static gboolean
style_has_atomic_inline_box(const ns_style *s)
{
    if (!s) return FALSE;
    const ns_css_value *w = s->values[NS_CSS_WIDTH];
    const ns_css_value *h = s->values[NS_CSS_HEIGHT];
    gboolean has_w = w && (w->kind == NS_CSS_V_LENGTH || w->kind == NS_CSS_V_CALC);
    gboolean has_h = h && (h->kind == NS_CSS_V_LENGTH || h->kind == NS_CSS_V_CALC);
    if (has_w || has_h) return TRUE;
    if (s->values[NS_CSS_BACKGROUND_IMAGE] &&
        (s->values[NS_CSS_BACKGROUND_IMAGE]->kind == NS_CSS_V_URL ||
         s->values[NS_CSS_BACKGROUND_IMAGE]->kind == NS_CSS_V_GRADIENT))
        return TRUE;
    if (s->values[NS_CSS_BORDER_RADIUS] ||
        s->values[NS_CSS_BORDER_TOP_LEFT_RADIUS] ||
        s->values[NS_CSS_BORDER_TOP_RIGHT_RADIUS] ||
        s->values[NS_CSS_BORDER_BOTTOM_RIGHT_RADIUS] ||
        s->values[NS_CSS_BORDER_BOTTOM_LEFT_RADIUS])
        return TRUE;
    const ns_css_prop widths[4] = {
        NS_CSS_BORDER_TOP_WIDTH,
        NS_CSS_BORDER_RIGHT_WIDTH,
        NS_CSS_BORDER_BOTTOM_WIDTH,
        NS_CSS_BORDER_LEFT_WIDTH,
    };
    const ns_css_prop styles_p[4] = {
        NS_CSS_BORDER_TOP_STYLE,
        NS_CSS_BORDER_RIGHT_STYLE,
        NS_CSS_BORDER_BOTTOM_STYLE,
        NS_CSS_BORDER_LEFT_STYLE,
    };
    for (int i = 0; i < 4; i++) {
        if (length_or(s->values[widths[i]], 0) <= 0) continue;
        const ns_css_value *st = s->values[styles_p[i]];
        if (!keyword_is(st, "none") && !keyword_is(st, "hidden"))
            return TRUE;
    }
    const ns_css_value *bg = s->values[NS_CSS_BACKGROUND_COLOR];
    return bg && bg->kind == NS_CSS_V_COLOR && bg->u.color.a > 0;
}

static gboolean
is_atomic_inline(const ns_node *n, GHashTable *styles)
{
    if (!n || n->kind != NS_NODE_ELEMENT || !n->name) return FALSE;
    const char *nm = n->name;
    if (strcmp(nm, "button") == 0) {
        const ns_style *bs = styles ? g_hash_table_lookup(styles, n) : NULL;
        if (display_is_atomic_inline_container(ns_css_display_of(bs)) ||
            style_has_atomic_inline_box(bs))
            return TRUE;
        if (button_has_replaced_child(n)) return TRUE;
        const ns_css_value *bw = bs ? bs->values[NS_CSS_WIDTH]  : NULL;
        const ns_css_value *bh = bs ? bs->values[NS_CSS_HEIGHT] : NULL;
        return (bw && (bw->kind == NS_CSS_V_LENGTH || bw->kind == NS_CSS_V_CALC)) ||
               (bh && (bh->kind == NS_CSS_V_LENGTH || bh->kind == NS_CSS_V_CALC));
    }
    if (strcmp(nm, "a") == 0 || strcmp(nm, "label") == 0 ||
        strcmp(nm, "summary") == 0) {
        const ns_style *s = styles ? g_hash_table_lookup(styles, n) : NULL;
        ns_display d = ns_css_display_of(s);
        if (!(display_is_atomic_inline_container(d) ||
              ns_display_is_flex_container(d) ||
              ns_display_is_grid_container(d)))
            return FALSE;
        return button_has_replaced_child(n) || style_has_atomic_inline_box(s);
    }
    if (strcmp(nm, "img") == 0 || strcmp(nm, "svg") == 0 ||
        strcmp(nm, "audio") == 0 || strcmp(nm, "video") == 0 ||
        strcmp(nm, "math") == 0 || strcmp(nm, "canvas") == 0 ||
        strcmp(nm, "picture") == 0)
        return is_inline_level_replaced(n, styles);
    if (strcmp(nm, "input") == 0 || strcmp(nm, "textarea") == 0 ||
        strcmp(nm, "select") == 0 ||
        strcmp(nm, "progress") == 0 || strcmp(nm, "meter") == 0) {
        const ns_style *s = styles ? g_hash_table_lookup(styles, n) : NULL;
        gboolean atomic_display =
            display_is_atomic_inline_container(ns_css_display_of(s));
        if (atomic_display && strcmp(nm, "input") == 0) {
            const char *type = ns_element_get_attr(n, "type");
            if (type && (g_ascii_strcasecmp(type, "radio") == 0 ||
                         g_ascii_strcasecmp(type, "checkbox") == 0)) {
                const ns_css_value *ap = s ? s->values[NS_CSS_APPEARANCE] : NULL;
                const ns_css_value *w = s ? s->values[NS_CSS_WIDTH]  : NULL;
                const ns_css_value *h = s ? s->values[NS_CSS_HEIGHT] : NULL;
                gboolean styled =
                    keyword_is(ap, "none") ||
                    (w && (w->kind == NS_CSS_V_LENGTH || w->kind == NS_CSS_V_CALC)) ||
                    (h && (h->kind == NS_CSS_V_LENGTH || h->kind == NS_CSS_V_CALC));
                if (!styled) return FALSE;
            }
        }
        return atomic_display;
    }
    if (strcmp(nm, "br") == 0 || strcmp(nm, "wbr") == 0)
        return FALSE;
    const ns_style *s = styles ? g_hash_table_lookup(styles, n) : NULL;
    if (!s) return FALSE;
    return display_is_atomic_inline_container(ns_css_display_of(s));
}
static const ns_node *g_focused_input_for_layout;
static const ns_node *g_open_select_for_layout;
static gboolean       g_focused_is_contenteditable_for_layout;
static gsize          g_focused_caret_byte_for_layout;
static gsize          g_focused_sel_anchor_byte_for_layout;
static gboolean       g_datalist_open_for_layout;

void
ns_layout_set_open_select(const ns_node *select)
{
    g_open_select_for_layout = select;
}

void
ns_layout_set_datalist_open(gboolean open)
{
    g_datalist_open_for_layout = open;
}

char *
ns_vertical_stack_text(const char *text)
{
    if (!text) return g_strdup("");
    GString *out = g_string_new(NULL);
    for (const char *p = text; *p; ) {
        const char *next = g_utf8_next_char(p);
        if (out->len) g_string_append_c(out, '\n');
        g_string_append_len(out, p, (gssize)(next - p));
        p = next;
    }
    return g_string_free(out, FALSE);
}
static struct ns_image_cache *g_image_cache_for_layout;
static const char    *g_base_url_for_layout;
static GHashTable    *g_counters_for_layout;
static gboolean       g_svg_defs_computed_for_layout;
static ns_box *ns_layout_build_(const ns_node *doc, GHashTable *styles, double viewport_width);

typedef struct ns_subgrid_cols {
    int n;
    double x[NS_CSS_TRACKS_MAX + 1];
    double sizes[NS_CSS_TRACKS_MAX];
    double gap;
} ns_subgrid_cols;
static const ns_subgrid_cols *g_pending_subgrid_cols;

typedef struct ns_subgrid_rows {
    int n;
    double y[NS_CSS_TRACKS_MAX + 1];
    double sizes[NS_CSS_TRACKS_MAX];
    double gap;
} ns_subgrid_rows;
static const ns_subgrid_rows *g_pending_subgrid_rows;

static gboolean
style_columns_are_subgrid(const ns_style *s)
{
    const ns_css_value *v = s ? s->values[NS_CSS_GRID_TEMPLATE_COLUMNS] : NULL;
    return v && v->kind == NS_CSS_V_TRACKS && v->u.tracks.subgrid;
}

static gboolean
style_rows_are_subgrid(const ns_style *s)
{
    const ns_css_value *v = s ? s->values[NS_CSS_GRID_TEMPLATE_ROWS] : NULL;
    return v && v->kind == NS_CSS_V_TRACKS && v->u.tracks.subgrid;
}

typedef struct ns_table_col_hint {
    const ns_style *style;
    int span;
} ns_table_col_hint;

typedef struct ns_abs_entry {
    const ns_node *dom;
    const ns_style *pseudo;
    gboolean       fixed;
} ns_abs_entry;

typedef struct ns_abs_static {
    ns_box *run;
    double  rel_x;
    double  rel_y;
} ns_abs_static;

enum {
    NS_LAYOUT_DATA_IMAGE_BUDGET = 64ULL * 1024ULL * 1024ULL,
};

static GArray      *g_abs_pending;
static gboolean     g_abs_force_build;
static const ns_node *g_form_control_inline;
static GHashTable  *g_abs_ph_set;
static GHashTable  *g_abs_static;
static GHashTable  *g_abs_seen;
static const ns_node *g_inline_skip_node;

static void *
collect_peek_image(const char *src)
{
    if (!src || !g_image_cache_for_layout) return NULL;
    char *abs = g_base_url_for_layout
        ? ns_url_resolve(g_base_url_for_layout, src)
        : NULL;
    void *img = ns_image_cache_peek(g_image_cache_for_layout,
                                    abs ? abs : src);
    g_free(abs);
    return img;
}

static void
collect_box_bg_image(ns_box *box, const ns_style *s)
{
    const ns_css_value *mi = s ? s->values[NS_CSS_LIST_STYLE_IMAGE] : NULL;
    if (mi && mi->kind == NS_CSS_V_URL && mi->u.url &&
        box->dom && ns_node_is_element_named(box->dom, "li")) {
        ns_box_media *m = ns_box_media_ensure(box);
        m->marker_image_src = g_strdup(mi->u.url);
        m->marker_image = collect_peek_image(m->marker_image_src);
    }
    const ns_css_value *bi = ns_css_border_image_source(s);
    if (bi && bi->kind == NS_CSS_V_URL && bi->u.url) {
        ns_box_media *m = ns_box_media_ensure(box);
        m->border_image_src = g_strdup(bi->u.url);
        m->border_image = collect_peek_image(m->border_image_src);
    }
    const ns_css_value *bg = s ? s->values[NS_CSS_BACKGROUND_IMAGE] : NULL;
    gboolean any_url = FALSE;
    for (const ns_css_value *l = bg; l; l = l->next_layer)
        if (l->kind == NS_CSS_V_URL && l->u.url) { any_url = TRUE; break; }
    const ns_css_value *mask =
        (s && s->values[NS_CSS_MASK_IMAGE] &&
         s->values[NS_CSS_MASK_IMAGE]->kind == NS_CSS_V_URL &&
         s->values[NS_CSS_MASK_IMAGE]->u.url)
            ? s->values[NS_CSS_MASK_IMAGE]
            : NULL;
    if (!any_url && !mask) return;
    ns_box_media *m = ns_box_media_ensure(box);
    if (!any_url) {
        m->bg_image_src = g_strdup(mask->u.url);
        m->bg_image = collect_peek_image(m->bg_image_src);
        return;
    }
    if (bg->next_layer) {
        m->bg_layer_srcs = g_ptr_array_new_with_free_func(g_free);
        m->bg_layer_images = g_ptr_array_new();
        for (const ns_css_value *l = bg; l; l = l->next_layer) {
            char *src = (l->kind == NS_CSS_V_URL && l->u.url)
                        ? g_strdup(l->u.url) : NULL;
            g_ptr_array_add(m->bg_layer_srcs, src);
            g_ptr_array_add(m->bg_layer_images, collect_peek_image(src));
        }
    }
    for (const ns_css_value *l = bg; l; l = l->next_layer) {
        if (l->kind != NS_CSS_V_URL || !l->u.url) continue;
        m->bg_image_src = g_strdup(l->u.url);
        m->bg_image = collect_peek_image(m->bg_image_src);
        break;
    }
}

static ns_box *
build_cell(const ns_node *n, GHashTable *styles)
{
    ns_box *cell = box_new(NS_BOX_TABLE_CELL);
    cell->dom = n;
    cell->style = g_hash_table_lookup(styles, n);
    collect_box_bg_image(cell, cell->style);
    const char *cs_attr = ns_element_get_attr(n, "colspan");
    if (cs_attr) cell->colspan = ns_parse_int(cs_attr, 1, 1, 100);
    const char *rs_attr = ns_element_get_attr(n, "rowspan");
    if (rs_attr) cell->rowspan = ns_parse_int(rs_attr, 1, 1, 100);
    const ns_node *c = n->first_child;
    while (c) {
        if (is_inline_dom(c, styles)) {
            const ns_node *start = c;
            while (c && continues_inline_run(c, styles)) c = c->next_sibling;
            ns_box *run = build_inline_run(start, c, styles);
            if (run->text && run->text[0] != '\0')
                box_append_child(cell, run);
            else
                ns_box_free(run);
        } else {
            ns_box *child = build_block(c, styles);
            if (child) box_append_child(cell, child);
            if (c) c = c->next_sibling;
        }
    }
    return cell;
}

static void
append_table_col_hint(ns_box *table, const ns_node *n, GHashTable *styles,
                      int span, const ns_style *fallback)
{
    if (span < 1) span = 1;
    if (!table->table_col_hints)
        table->table_col_hints = g_array_new(FALSE, FALSE,
                                             sizeof(ns_table_col_hint));
    const ns_style *s = styles ? g_hash_table_lookup(styles, n) : NULL;
    if (!s) s = fallback;
    ns_table_col_hint hint = { .style = s, .span = span };
    g_array_append_val(table->table_col_hints, hint);
}

static void
collect_table_col_hints(ns_box *table, const ns_node *n, GHashTable *styles)
{
    if (!table || !n) return;
    for (const ns_node *c = n->first_child; c; c = c->next_sibling) {
        if (c->kind != NS_NODE_ELEMENT || !c->name) continue;
        if (strcmp(c->name, "col") == 0) {
            int span = ns_parse_int(ns_element_get_attr(c, "span"), 1, 1, 1000);
            append_table_col_hint(table, c, styles, span, NULL);
            continue;
        }
        if (strcmp(c->name, "colgroup") != 0) continue;
        const ns_style *group_style = styles ? g_hash_table_lookup(styles, c) : NULL;
        gboolean saw_col = FALSE;
        for (const ns_node *col = c->first_child; col; col = col->next_sibling) {
            if (!ns_node_is_element_named(col, "col")) continue;
            int span = ns_parse_int(ns_element_get_attr(col, "span"), 1, 1, 1000);
            append_table_col_hint(table, col, styles, span, group_style);
            saw_col = TRUE;
        }
        if (!saw_col) {
            int span = ns_parse_int(ns_element_get_attr(c, "span"), 1, 1, 1000);
            append_table_col_hint(table, c, styles, span, NULL);
        }
    }
}

static ns_box *
build_table_caption(const ns_node *n, GHashTable *styles)
{
    ns_box *caption = box_new(NS_BOX_TABLE_CAPTION);
    caption->dom = n;
    caption->style = g_hash_table_lookup(styles, n);
    collect_box_bg_image(caption, caption->style);
    append_display_contents_children(caption, n, styles, FALSE, NULL);
    return caption;
}

static ns_box *
build_anonymous_table_cell(const ns_node *n, GHashTable *styles)
{
    ns_box *cell = box_new(NS_BOX_TABLE_CELL);
    gboolean any = FALSE;
    const ns_node *c = n ? n->first_child : NULL;
    while (c) {
        if (c->kind == NS_NODE_ELEMENT && c->name &&
            tag_is_non_rendering(c->name)) {
            c = c->next_sibling;
            continue;
        }
        if (is_table_caption(c, styles)) {
            c = c->next_sibling;
            continue;
        }
        if (c->kind == NS_NODE_TEXT && text_is_ws_only(c->text)) {
            c = c->next_sibling;
            continue;
        }
        if (c->kind == NS_NODE_ELEMENT) {
            const ns_style *cs = g_hash_table_lookup(styles, c);
            if (cs && style_is_none(cs)) {
                c = c->next_sibling;
                continue;
            }
            if (style_is_contents(cs)) {
                append_display_contents_children(cell, c, styles, FALSE, NULL);
                if (cell->last_child) any = TRUE;
                c = c->next_sibling;
                continue;
            }
        }
        if (is_inline_dom(c, styles)) {
            const ns_node *start = c;
            c = c->next_sibling;
            while (c && !is_table_caption(c, styles)) {
                if (c->kind == NS_NODE_ELEMENT && c->name &&
                    tag_is_non_rendering(c->name)) {
                    c = c->next_sibling;
                    continue;
                }
                if (c->kind == NS_NODE_ELEMENT) {
                    const ns_style *cs = g_hash_table_lookup(styles, c);
                    if (style_is_contents(cs)) break;
                }
                if (!continues_inline_run(c, styles)) break;
                c = c->next_sibling;
            }
            ns_box *run = build_inline_run(start, c, styles);
            if (run && run->text && run->text[0]) {
                box_append_child(cell, run);
                any = TRUE;
            } else if (run) {
                ns_box_free(run);
            }
        } else {
            ns_box *child = build_block(c, styles);
            if (child) {
                box_append_child(cell, child);
                any = TRUE;
            }
            if (c) c = c->next_sibling;
        }
    }
    if (!any) {
        ns_box_free(cell);
        return NULL;
    }
    return cell;
}

static ns_box *
build_table_row(const ns_node *tr, GHashTable *styles)
{
    ns_box *row = box_new(NS_BOX_TABLE_ROW);
    row->dom = tr;
    row->style = g_hash_table_lookup(styles, tr);
    for (const ns_node *c = tr->first_child; c; c = c->next_sibling)
        if (is_cell_element(c, styles))
            box_append_child(row, build_cell(c, styles));
    return row;
}

static ns_box *
build_row_from_cell_run(const ns_node *start, const ns_node *end,
                        const ns_node *dom, GHashTable *styles)
{
    ns_box *row = box_new(NS_BOX_TABLE_ROW);
    row->dom = dom;
    for (const ns_node *c = start; c != end; c = c->next_sibling) {
        if (!is_cell_element(c, styles)) continue;
        const ns_style *cs = g_hash_table_lookup(styles, c);
        if (cs && (style_is_none(cs) || style_is_absolute_or_fixed(cs)))
            continue;
        box_append_child(row, build_cell(c, styles));
    }
    if (row->first_child) return row;
    ns_box_free(row);
    return NULL;
}

static ns_box *
build_table(const ns_node *n, GHashTable *styles)
{
    ns_box *table = box_new(NS_BOX_TABLE);
    table->dom = n;
    table->style = g_hash_table_lookup(styles, n);
    collect_table_col_hints(table, n, styles);
    for (const ns_node *c = n->first_child; c; c = c->next_sibling) {
        if (!is_table_caption(c, styles)) continue;
        ns_box *caption = build_table_caption(c, styles);
        box_append_child(table, caption);
    }
    GPtrArray *rows = g_ptr_array_new();
    collect_rows(n, styles, rows);
    gboolean has_direct_cells = FALSE;
    if (rows->len == 0)
        for (const ns_node *c = n->first_child; c; c = c->next_sibling)
            if (is_cell_element(c, styles)) { has_direct_cells = TRUE; break; }
    if (rows->len == 0 && has_direct_cells) {
        ns_box *row = build_row_from_cell_run(n->first_child, NULL, n, styles);
        if (row) box_append_child(table, row);
    } else if (rows->len == 0) {
        ns_box *cell = build_anonymous_table_cell(n, styles);
        if (cell) {
            ns_box *row = box_new(NS_BOX_TABLE_ROW);
            row->dom = n;
            box_append_child(row, cell);
            box_append_child(table, row);
        }
    } else {
        for (guint i = 0; i < rows->len; i++)
            box_append_child(table,
                             build_table_row(g_ptr_array_index(rows, i),
                                             styles));
    }
    g_ptr_array_free(rows, TRUE);
    return table;
}

static ns_box *
build_anonymous_table(const ns_node *start, const ns_node *end,
                      GHashTable *styles)
{
    ns_box *table = box_new(NS_BOX_TABLE);
    for (const ns_node *c = start; c != end; c = c->next_sibling)
        if (is_table_caption(c, styles))
            box_append_child(table, build_table_caption(c, styles));

    const ns_node *cells = NULL;
    for (const ns_node *c = start; c != end; c = c->next_sibling) {
        if (is_cell_element(c, styles)) {
            if (!cells) cells = c;
            continue;
        }
        if (cells) {
            ns_box *row = build_row_from_cell_run(cells, c, NULL, styles);
            if (row) box_append_child(table, row);
            cells = NULL;
        }
        if (is_table_row(c, styles))
            box_append_child(table, build_table_row(c, styles));
        else if (node_is_row_group(c, styles))
            for (const ns_node *r = c->first_child; r; r = r->next_sibling)
                if (is_table_row(r, styles))
                    box_append_child(table, build_table_row(r, styles));
    }
    if (cells) {
        ns_box *row = build_row_from_cell_run(cells, end, NULL, styles);
        if (row) box_append_child(table, row);
    }

    if (table->first_child) return table;
    ns_box_free(table);
    return NULL;
}

typedef struct collector_ctx {
    GHashTable *styles;
    const char *active_href;
    const char *active_target;
    const ns_node *active_link_node;
    GString    *out;
    GArray     *links;
    GArray     *attrs;
    int  bold_depth;
    int  italic_depth;
    int  mono_depth;
    int  underline_depth;
    int  overline_depth;
    int  strike_depth;
    int  q_depth;
    gsize bold_start;
    gsize italic_start;
    gsize mono_start;
    gsize underline_start;
    gsize overline_start;
    gsize strike_start;
    const char *text_transform;
    GArray     *atomics;
    gboolean    abs_placeholders;
} collector_ctx;

typedef struct ns_atomic_raw {
    gsize start;
    ns_box *box;
} ns_atomic_raw;

static void
append_inline_spacer(collector_ctx *ctx, double width)
{
    if (!(width > 0)) return;
    gsize start = ctx->out->len;
    g_string_append(ctx->out, "\xef\xbf\xbc");
    ns_inline_attr spacer = {
        .kind = NS_INLINE_SPACER,
        .start = start,
        .len = 3,
        .box_w = width,
    };
    g_array_append_val(ctx->attrs, spacer);
}

static int
control_pad_spaces(const ns_style *s, ns_css_prop prop)
{
    if (!s) return 0;
    double fs = length_or(s->values[NS_CSS_FONT_SIZE], 16);
    if (!(fs > 0)) fs = 16;
    double pad = length_resolve(s->values[prop], fs * 20.0, 0);
    double space = fs * 0.25;
    if (!(space > 0) || pad <= space) return 0;
    int n = (int)((pad - space) / space + 0.5);
    if (n < 0) n = 0;
    if (n > 12) n = 12;
    return n;
}

static int
text_input_leading_spaces(const ns_style *s)
{
    return control_pad_spaces(s, NS_CSS_PADDING_LEFT);
}

static GHashTable *g_input_columns_for_layout;

static int
text_input_size_attr(const ns_node *n)
{
    const char *size_str = ns_element_get_attr(n, "size");
    return size_str ? ns_parse_int(size_str, 20, 4, 80) : 20;
}

static int
text_input_columns(const ns_node *n)
{
    int size = text_input_size_attr(n);
    if (!g_input_columns_for_layout) return size;
    gpointer fitted = g_hash_table_lookup(g_input_columns_for_layout, n);
    return fitted ? GPOINTER_TO_INT(fitted) : size;
}

static gboolean
text_input_align_rtl(const ns_style *s, const ns_node *n)
{
    for (const ns_node *p = n; p; p = p->parent) {
        if (p->kind != NS_NODE_ELEMENT) continue;
        const char *dir = ns_element_get_attr(p, "dir");
        if (!dir) continue;
        if (g_ascii_strcasecmp(dir, "rtl") == 0) return TRUE;
        if (g_ascii_strcasecmp(dir, "ltr") == 0) return FALSE;
    }
    return keyword_is(s ? s->values[NS_CSS_DIRECTION] : NULL, "rtl");
}

static glong
text_input_align_offset(const ns_style *s, const ns_node *n, glong span)
{
    if (span <= 0) return 0;
    const ns_css_value *ta = s ? s->values[NS_CSS_TEXT_ALIGN] : NULL;
    gboolean rtl = text_input_align_rtl(s, n);
    if (keyword_is(ta, "center"))
        return span / 2;
    if (keyword_is(ta, "right") ||
        (keyword_is(ta, "end") && !rtl) ||
        (keyword_is(ta, "start") && rtl) ||
        (!ta && rtl))
        return span;
    return 0;
}

static glong
text_input_align_skip_cps(const ns_style *s, const ns_node *n,
                          glong cps, glong size)
{
    if (cps <= size) return 0;
    return text_input_align_offset(s, n, cps - size);
}

static glong
text_input_focused_skip_cps(const char *value, gsize caret_byte,
                            glong cps, glong size)
{
    if (!value || cps <= size) return 0;
    glong max_skip = cps - size;
    glong caret_cp = g_utf8_pointer_to_offset(value, value + caret_byte);
    glong skip = caret_cp > size ? caret_cp - size : 0;
    if (skip > max_skip) skip = max_skip;
    if (skip < 0) skip = 0;
    return skip;
}

static const char *
text_input_advance_cps(const char *p, glong cps)
{
    for (glong i = 0; p && *p && i < cps; i++)
        p = g_utf8_next_char(p);
    return p ? p : "";
}

static gboolean
name_in(const char *name, const char *const *set)
{
    if (!name) return FALSE;
    for (; *set; set++) if (strcmp(name, *set) == 0) return TRUE;
    return FALSE;
}

static gboolean
tag_is_bold(const char *name)
{
    static const char *const set[] = { "b", "strong", NULL };
    return name_in(name, set);
}

static gboolean
tag_is_italic(const char *name)
{
    static const char *const set[] = { "i", "em", "cite", "dfn", NULL };
    return name_in(name, set);
}

static gboolean
tag_is_monospace(const char *name)
{
    static const char *const set[] = { "code", "tt", "kbd", "samp", "pre", NULL };
    return name_in(name, set);
}

static gboolean
tag_is_underline(const char *name)
{
    static const char *const set[] = { "u", "ins", NULL };
    return name_in(name, set);
}

static gboolean
tag_is_strike(const char *name)
{
    static const char *const set[] = { "s", "del", "strike", NULL };
    return name_in(name, set);
}

static gboolean
tag_is_non_rendering(const char *name)
{
    static const char *const set[] = {
        "area", "base", "head", "link", "meta", "noscript", "param",
        "script", "source", "style", "template", "title", "track", NULL,
    };
    return name_in(name, set);
}

static gboolean
node_is_non_rendering(const ns_node *n)
{
    if (!n || !tag_is_non_rendering(n->name)) return FALSE;
    if (n->name && g_ascii_strcasecmp(n->name, "noscript") == 0) {
        const ns_node *root = ns_node_root(n);
        if (root && (root->flags & NS_NODE_SCRIPTING_DISABLED))
            return FALSE;
    }
    return TRUE;
}

static void
emit_attr(GArray *attrs, ns_inline_attr_kind k, gsize start, gsize end)
{
    if (end <= start) return;
    ns_inline_attr a = { .kind = k, .start = start, .len = end - start };
    g_array_append_val(attrs, a);
}

static void
emit_attr_styled(GArray *attrs, ns_inline_attr_kind k, gsize start, gsize end,
                 const ns_style *style)
{
    if (end <= start) return;
    ns_inline_attr a = { .kind = k, .start = start, .len = end - start,
                         .style = style };
    g_array_append_val(attrs, a);
}

static gboolean
inline_run_at_line_start(const GString *out)
{
    if (!out || out->len == 0) return TRUE;
    gsize i = out->len;
    while (i > 0) {
        if ((guchar)out->str[i - 1] == ' ') { i--; continue; }
        if (i >= 2 && (guchar)out->str[i - 2] == 0xc2 &&
            (guchar)out->str[i - 1] == 0xa0) { i -= 2; continue; }
        break;
    }
    if (i == 0) return TRUE;
    if (i >= 3 && (guchar)out->str[i - 3] == 0xe2 &&
        (guchar)out->str[i - 2] == 0x80 &&
        ((guchar)out->str[i - 1] == 0xa8 || (guchar)out->str[i - 1] == 0xa9))
        return TRUE;
    if ((guchar)out->str[i - 1] == '\n') return TRUE;
    return FALSE;
}

static double
control_dim_px_basis(const ns_css_value *v, double font_size, double basis)
{
    if (!v) return 0;
    if (v->kind == NS_CSS_V_CALC) {
        double out = v->u.calc.px;
        if (basis > 0) out += v->u.calc.pct * basis / 100.0;
        return out > 0 ? out : 0;
    }
    if (v->kind != NS_CSS_V_LENGTH) return 0;
    switch (v->u.length.unit) {
    case NS_CSS_UNIT_PX:
    case NS_CSS_UNIT_NUMBER:
        return v->u.length.v;
    case NS_CSS_UNIT_EM:
        return v->u.length.v * font_size;
    case NS_CSS_UNIT_REM:
        return v->u.length.v * 16.0;
    case NS_CSS_UNIT_PERCENT:
        return basis > 0 ? v->u.length.v * basis / 100.0 : 0;
    case NS_CSS_UNIT_VW:
        return v->u.length.v * ns_css_viewport_w() / 100.0;
    case NS_CSS_UNIT_VH:
        return v->u.length.v * ns_css_viewport_h() / 100.0;
    case NS_CSS_UNIT_VMIN:
        return v->u.length.v * MIN(ns_css_viewport_w(), ns_css_viewport_h()) / 100.0;
    case NS_CSS_UNIT_VMAX:
        return v->u.length.v * MAX(ns_css_viewport_w(), ns_css_viewport_h()) / 100.0;
    case NS_CSS_UNIT_CQW:
        return v->u.length.v * (ns_css_container_w() > 0 ? ns_css_container_w() : ns_css_viewport_w()) / 100.0;
    case NS_CSS_UNIT_CQH:
        return v->u.length.v * (ns_css_container_h() > 0 ? ns_css_container_h() : ns_css_viewport_h()) / 100.0;
    case NS_CSS_UNIT_CQMIN: {
        double cw = ns_css_container_w() > 0 ? ns_css_container_w() : ns_css_viewport_w();
        double ch = ns_css_container_h() > 0 ? ns_css_container_h() : ns_css_viewport_h();
        return v->u.length.v * MIN(cw, ch) / 100.0;
    }
    case NS_CSS_UNIT_CQMAX: {
        double cw = ns_css_container_w() > 0 ? ns_css_container_w() : ns_css_viewport_w();
        double ch = ns_css_container_h() > 0 ? ns_css_container_h() : ns_css_viewport_h();
        return v->u.length.v * MAX(cw, ch) / 100.0;
    }
    case NS_CSS_UNIT_EX:
    case NS_CSS_UNIT_CH:
        return v->u.length.v * font_size * 0.5;
    case NS_CSS_UNIT_CAP:
        return v->u.length.v * font_size * 0.7;
    case NS_CSS_UNIT_IC:
        return v->u.length.v * font_size;
    case NS_CSS_UNIT_LH:
        return v->u.length.v * font_size * 1.5;
    case NS_CSS_UNIT_RLH:
        return v->u.length.v * 24.0;
    case NS_CSS_UNIT_REX:
    case NS_CSS_UNIT_RCH:
        return v->u.length.v * 8.0;
    case NS_CSS_UNIT_RCAP:
        return v->u.length.v * 11.2;
    case NS_CSS_UNIT_RIC:
        return v->u.length.v * 16.0;
    }
    return 0;
}

static double
control_dim_px_clamped(const ns_style *s, ns_css_prop value_prop,
                       ns_css_prop min_prop, ns_css_prop max_prop,
                       double font_size, double basis)
{
    if (!s) return 0;
    double out = control_dim_px_basis(s->values[value_prop], font_size, basis);
    double mn = control_dim_px_basis(s->values[min_prop], font_size, basis);
    double mx = control_dim_px_basis(s->values[max_prop], font_size, basis);
    if (mn > 0 && out > 0 && out < mn) out = mn;
    if (mx > 0 && out > mx) out = mx;
    return out;
}

static gboolean
control_is_border_box(const ns_node *dom, const ns_style *s)
{
    if (s && keyword_is(s->values[NS_CSS_BOX_SIZING], "border-box"))
        return TRUE;
    if (s && keyword_is(s->values[NS_CSS_BOX_SIZING], "content-box"))
        return FALSE;
    if (!dom || !dom->name) return FALSE;
    if (strcmp(dom->name, "button") == 0 || strcmp(dom->name, "select") == 0)
        return TRUE;
    if (strcmp(dom->name, "input") != 0) return FALSE;
    const char *type = ns_element_get_attr(dom, "type");
    if (!type || !*type) return FALSE;
    return g_ascii_strcasecmp(type, "submit") == 0 ||
           g_ascii_strcasecmp(type, "reset") == 0 ||
           g_ascii_strcasecmp(type, "button") == 0 ||
           g_ascii_strcasecmp(type, "checkbox") == 0 ||
           g_ascii_strcasecmp(type, "radio") == 0 ||
           g_ascii_strcasecmp(type, "color") == 0 ||
           g_ascii_strcasecmp(type, "search") == 0;
}

double
ns_control_css_extra_w(const ns_node *dom, const ns_style *s)
{
    if (!s || control_is_border_box(dom, s)) return 0;
    ns_edges m, p, b;
    edges_from_style(s, 0, &m, &p, &b);
    return p.left + p.right + b.left + b.right;
}

double
ns_control_css_extra_h(const ns_node *dom, const ns_style *s)
{
    if (!s || control_is_border_box(dom, s)) return 0;
    ns_edges m, p, b;
    edges_from_style(s, 0, &m, &p, &b);
    return p.top + p.bottom + b.top + b.bottom;
}

static double
inline_attr_control_width(const ns_inline_attr *r, const ns_box *box)
{
    if (!r || !r->style) return r && r->box_w > 0 ? r->box_w : 0;
    double fs = length_or(r->style->values[NS_CSS_FONT_SIZE], 16);
    double w = control_dim_px_clamped(r->style, NS_CSS_WIDTH,
                                      NS_CSS_MIN_WIDTH, NS_CSS_MAX_WIDTH,
                                      fs, box ? box->content_width : 0);
    if (w > 0) w += ns_control_css_extra_w(r->dom, r->style);
    return w > 0 ? w : r->box_w;
}

static gboolean
style_has_visible_control_box(const ns_style *s)
{
    if (!s) return FALSE;
    const ns_css_value *bg = s->values[NS_CSS_BACKGROUND_COLOR];
    if (bg && bg->kind == NS_CSS_V_COLOR && bg->u.color.a > 0)
        return TRUE;
    if (s->values[NS_CSS_BOX_SHADOW] &&
        s->values[NS_CSS_BOX_SHADOW]->kind == NS_CSS_V_SHADOW &&
        s->values[NS_CSS_BOX_SHADOW]->u.shadow.n > 0)
        return TRUE;
    if (s->values[NS_CSS_BACKGROUND_IMAGE] &&
        (s->values[NS_CSS_BACKGROUND_IMAGE]->kind == NS_CSS_V_URL ||
         s->values[NS_CSS_BACKGROUND_IMAGE]->kind == NS_CSS_V_GRADIENT))
        return TRUE;
    if (length_or(s->values[NS_CSS_BORDER_RADIUS], 0) > 0 ||
        length_or(s->values[NS_CSS_BORDER_TOP_LEFT_RADIUS], 0) > 0 ||
        length_or(s->values[NS_CSS_BORDER_TOP_RIGHT_RADIUS], 0) > 0 ||
        length_or(s->values[NS_CSS_BORDER_BOTTOM_RIGHT_RADIUS], 0) > 0 ||
        length_or(s->values[NS_CSS_BORDER_BOTTOM_LEFT_RADIUS], 0) > 0)
        return TRUE;
    const ns_css_prop widths[4] = {
        NS_CSS_BORDER_TOP_WIDTH,
        NS_CSS_BORDER_RIGHT_WIDTH,
        NS_CSS_BORDER_BOTTOM_WIDTH,
        NS_CSS_BORDER_LEFT_WIDTH,
    };
    const ns_css_prop styles_p[4] = {
        NS_CSS_BORDER_TOP_STYLE,
        NS_CSS_BORDER_RIGHT_STYLE,
        NS_CSS_BORDER_BOTTOM_STYLE,
        NS_CSS_BORDER_LEFT_STYLE,
    };
    for (int i = 0; i < 4; i++) {
        if (length_or(s->values[widths[i]], 0) <= 0) continue;
        const ns_css_value *st = s->values[styles_p[i]];
        if (!keyword_is(st, "none") && !keyword_is(st, "hidden"))
            return TRUE;
    }
    return FALSE;
}

static gboolean
control_style_strips_chrome(const ns_style *s)
{
    static const ns_css_prop styles_p[4] = {
        NS_CSS_BORDER_TOP_STYLE, NS_CSS_BORDER_RIGHT_STYLE,
        NS_CSS_BORDER_BOTTOM_STYLE, NS_CSS_BORDER_LEFT_STYLE,
    };
    static const ns_css_prop widths_p[4] = {
        NS_CSS_BORDER_TOP_WIDTH, NS_CSS_BORDER_RIGHT_WIDTH,
        NS_CSS_BORDER_BOTTOM_WIDTH, NS_CSS_BORDER_LEFT_WIDTH,
    };
    if (!s) return FALSE;
    (void)widths_p;
    for (int i = 0; i < 4; i++) {
        const ns_css_value *st = s->values[styles_p[i]];
        if (!keyword_is(st, "none") && !keyword_is(st, "hidden"))
            return FALSE;
    }
    return TRUE;
}

static gboolean
control_prefers_css_chrome(ns_inline_attr_kind k, const ns_node *dom,
                           const ns_style *s)
{
    if (!dom || !s) return FALSE;
    if (keyword_is(s->values[NS_CSS_APPEARANCE], "none"))
        return TRUE;
    if (control_style_strips_chrome(s)) return TRUE;
    if (style_has_visible_control_box(s)) return TRUE;
    if (!ns_element_get_attr(dom, "class")) return FALSE;
    ns_display d = ns_css_display_of(s);
    if (ns_display_is_block_level(d) || display_is_atomic_inline_container(d))
        return k == NS_INLINE_INPUT_FIELD ||
               k == NS_INLINE_INPUT_FIELD_FOCUSED ||
               k == NS_INLINE_BUTTON;
    return FALSE;
}

static void
emit_form_attr_sized(GArray *attrs, ns_inline_attr_kind k, gsize start, gsize end,
                     const ns_node *dom, GHashTable *styles)
{
    if (end <= start) return;
    double bw = 0, bh = 0;
    const ns_style *s = NULL;
    double fs = 16;
    const char *bg_image_src = NULL;
    void *bg_image = NULL;
    if (dom && styles) {
        s = g_hash_table_lookup(styles, dom);
        if (s) {
            fs = length_or(s->values[NS_CSS_FONT_SIZE], 16);
            bw = control_dim_px_clamped(s, NS_CSS_WIDTH,
                                        NS_CSS_MIN_WIDTH, NS_CSS_MAX_WIDTH,
                                        fs, 0);
            bh = control_dim_px_clamped(s, NS_CSS_HEIGHT,
                                        NS_CSS_MIN_HEIGHT, NS_CSS_MAX_HEIGHT,
                                        fs, 0);
            if (bw > 0) bw += ns_control_css_extra_w(dom, s);
            if (bh > 0) bh += ns_control_css_extra_h(dom, s);
            const ns_css_value *bg = s->values[NS_CSS_BACKGROUND_IMAGE];
            if (bg && bg->kind == NS_CSS_V_URL && bg->u.url) {
                bg_image_src = bg->u.url;
                if (g_image_cache_for_layout) {
                    char *abs = g_base_url_for_layout
                        ? ns_url_resolve(g_base_url_for_layout, bg_image_src)
                        : NULL;
                    bg_image = ns_image_cache_peek(g_image_cache_for_layout,
                                                   abs ? abs : bg_image_src);
                    g_free(abs);
                }
            }
        }
    }
    if (dom && dom->name && strcmp(dom->name, "textarea") == 0) {
        if (bw <= 0) {
            const char *cols = ns_element_get_attr(dom, "cols");
            int c = cols ? atoi(cols) : 20;
            if (c <= 0) c = 20;
            bw = c * (fs * 0.5) + 8.0;
        }
        if (bh <= 0) {
            const char *rows = ns_element_get_attr(dom, "rows");
            int r = rows ? atoi(rows) : 2;
            if (r <= 0) r = 2;
            double line_h = fs * 1.3;
            bh = r * line_h + 6.0;
        }
    }
    ns_inline_attr a = {
        .kind = k, .start = start, .len = end - start, .dom = dom,
        .box_w = bw, .box_h = bh,
        .native_chrome = !control_prefers_css_chrome(k, dom, s),
        .style = s,
        .bg_image_src = bg_image_src,
        .bg_image = bg_image,
    };
    g_array_append_val(attrs, a);
}

static void
emit_control_text_style(GArray *attrs, const ns_style *s,
                        gsize field_start, gsize field_end,
                        gsize val_start, gsize val_end,
                        gboolean is_placeholder, gboolean disabled)
{
    if (field_end > field_start) {
        double ifs = s ? length_or(s->values[NS_CSS_FONT_SIZE], 0) : 0;
        if (ifs > 0) {
            ns_inline_attr a = {
                .kind = NS_INLINE_FONT_SIZE,
                .start = field_start, .len = field_end - field_start,
                .font_size_px = ifs,
            };
            g_array_append_val(attrs, a);
        }
        const ns_css_value *ffam = s ? s->values[NS_CSS_FONT_FAMILY] : NULL;
        if (ffam && ffam->kind == NS_CSS_V_KEYWORD && ffam->u.keyword) {
            ns_inline_attr a = {
                .kind = NS_INLINE_FONT_FAMILY,
                .start = field_start, .len = field_end - field_start,
                .family = ffam->u.keyword,
            };
            g_array_append_val(attrs, a);
        }
    }
    if (val_end <= val_start) return;
    const ns_style *phs = (s && is_placeholder) ? s->placeholder : NULL;
    guint8 cr = 0, cg = 0, cb = 0, ca = 255;
    gboolean have_color = FALSE;
    if (is_placeholder) {
        const ns_css_value *pc = phs ? phs->values[NS_CSS_COLOR] : NULL;
        if (pc && pc->kind == NS_CSS_V_COLOR) {
            cr = pc->u.color.r; cg = pc->u.color.g;
            cb = pc->u.color.b; ca = pc->u.color.a;
        } else {
            cr = cg = cb = 0x75;
        }
        have_color = TRUE;
    } else {
        const ns_css_value *cv = s ? s->values[NS_CSS_COLOR] : NULL;
        if (cv && cv->kind == NS_CSS_V_COLOR) {
            cr = cv->u.color.r; cg = cv->u.color.g;
            cb = cv->u.color.b; ca = cv->u.color.a;
            have_color = TRUE;
        }
    }
    if (disabled) { cr = cg = cb = 0x82; ca = 255; have_color = TRUE; }
    if (have_color) {
        ns_inline_attr a = {
            .kind = NS_INLINE_COLOR,
            .start = val_start, .len = val_end - val_start,
            .r = cr, .g = cg, .b = cb, .a = ca,
        };
        g_array_append_val(attrs, a);
    }
    if (phs) {
        if (keyword_is(phs->values[NS_CSS_FONT_STYLE], "italic") ||
            keyword_is(phs->values[NS_CSS_FONT_STYLE], "oblique"))
            emit_attr(attrs, NS_INLINE_ITALIC, val_start, val_end);
        int phw = ns_css_font_weight_number(phs->values[NS_CSS_FONT_WEIGHT], -1);
        if (phw > 0) {
            ns_inline_attr a = {
                .kind = NS_INLINE_FONT_WEIGHT,
                .start = val_start, .len = val_end - val_start,
                .font_weight = phw,
            };
            g_array_append_val(attrs, a);
        }
    }
}

static void
emit_font_size_attr(GArray *attrs, gsize start, gsize end, double font_size_px)
{
    if (end <= start) return;
    ns_inline_attr a = { .kind = NS_INLINE_FONT_SIZE, .start = start,
                         .len = end - start, .font_size_px = font_size_px };
    g_array_append_val(attrs, a);
}

static void
emit_font_weight_attr(GArray *attrs, gsize start, gsize end, int font_weight)
{
    if (end <= start || font_weight <= 0) return;
    ns_inline_attr a = { .kind = NS_INLINE_FONT_WEIGHT, .start = start,
                         .len = end - start, .font_weight = font_weight };
    g_array_append_val(attrs, a);
}

static void
emit_font_stretch_attr(GArray *attrs, gsize start, gsize end, int font_stretch)
{
    if (end <= start) return;
    ns_inline_attr a = { .kind = NS_INLINE_FONT_STRETCH, .start = start,
                         .len = end - start, .font_stretch = font_stretch };
    g_array_append_val(attrs, a);
}

static int
font_kerning_int_from_style(const ns_style *s)
{
    const char *kw = s ? ns_style_keyword(s, NS_CSS_FONT_KERNING) : NULL;
    if (!kw) return -1;
    if (strcmp(kw, "none") == 0) return 0;
    if (strcmp(kw, "normal") == 0 || strcmp(kw, "auto") == 0) return 1;
    return -1;
}

static const char *
font_ligatures_from_style(const ns_style *s)
{
    const ns_css_value *v = s ? s->values[NS_CSS_FONT_VARIANT_LIGATURES] : NULL;
    return v && v->kind == NS_CSS_V_KEYWORD ? v->u.keyword : NULL;
}

static const char *
font_feature_settings_from_style(const ns_style *s)
{
    const ns_css_value *v = s ? s->values[NS_CSS_FONT_FEATURE_SETTINGS] : NULL;
    return v && v->kind == NS_CSS_V_KEYWORD ? v->u.keyword : NULL;
}

static const char *
font_variation_settings_from_style(const ns_style *s)
{
    const ns_css_value *v = s ? s->values[NS_CSS_FONT_VARIATION_SETTINGS] : NULL;
    return v && v->kind == NS_CSS_V_KEYWORD ? v->u.keyword : NULL;
}

static void
emit_font_features_attr(GArray *attrs, gsize start, gsize end,
                        int font_kerning, const char *font_ligatures,
                        const char *font_features)
{
    if (end <= start ||
        (font_kerning < 0 && !font_ligatures && !font_features))
        return;
    ns_inline_attr a = { .kind = NS_INLINE_FONT_FEATURES, .start = start,
                         .len = end - start, .font_kerning = font_kerning,
                         .font_ligatures = font_ligatures,
                         .font_features = font_features };
    g_array_append_val(attrs, a);
}

static void
emit_font_variations_attr(GArray *attrs, gsize start, gsize end,
                          const char *font_variations)
{
    if (end <= start || !font_variations) return;
    ns_inline_attr a = { .kind = NS_INLINE_FONT_VARIATIONS, .start = start,
                         .len = end - start,
                         .font_variations = font_variations };
    g_array_append_val(attrs, a);
}

static void
emit_color_attr(GArray *attrs, gsize start, gsize end,
                guint8 r, guint8 g, guint8 b, guint8 a8)
{
    if (end <= start) return;
    ns_inline_attr a = { .kind = NS_INLINE_COLOR, .start = start,
                         .len = end - start, .r = r, .g = g, .b = b, .a = a8 };
    g_array_append_val(attrs, a);
}

static void
emit_bg_color_attr(GArray *attrs, gsize start, gsize end,
                   guint8 r, guint8 g, guint8 b, guint8 a8)
{
    if (end <= start) return;
    ns_inline_attr a = { .kind = NS_INLINE_BG_COLOR, .start = start,
                         .len = end - start, .r = r, .g = g, .b = b, .a = a8 };
    g_array_append_val(attrs, a);
}

static void
emit_font_family_attr(GArray *attrs, gsize start, gsize end, const char *family)
{
    if (end <= start || !family) return;
    ns_inline_attr a = { .kind = NS_INLINE_FONT_FAMILY, .start = start,
                         .len = end - start, .family = family };
    g_array_append_val(attrs, a);
}

static void
counter_apply_decl(GHashTable *counters, const char *decl, gboolean increment)
{
    if (!decl || !*decl) return;
    const char *p = decl;
    while (*p) {
        while (*p && g_ascii_isspace(*p)) p++;
        if (!*p) break;
        const char *name_s = p;
        while (*p && !g_ascii_isspace(*p)) p++;
        gsize nlen = (gsize)(p - name_s);
        if (nlen == 0) break;
        char *name = g_strndup(name_s, nlen);
        if (strcmp(name, "none") == 0) { g_free(name); break; }
        while (*p && g_ascii_isspace(*p)) p++;
        int val = increment ? 1 : 0;
        if (*p == '-' || g_ascii_isdigit(*p)) {
            char *end = NULL;
            long v = strtol(p, &end, 10);
            if (end != p) { val = (int)v; p = end; }
        }
        gint cur = increment
            ? GPOINTER_TO_INT(g_hash_table_lookup(counters, name))
            : 0;
        g_hash_table_insert(counters, g_strdup(name),
                            GINT_TO_POINTER(cur + val));
        g_free(name);
    }
}

static gchar *
counter_format(gint v, const char *style)
{
    if (!style || strcmp(style, "decimal") == 0)
        return g_strdup_printf("%d", v);
    if (strcmp(style, "decimal-leading-zero") == 0)
        return g_strdup_printf("%02d", v);
    if (strcmp(style, "lower-roman") == 0 || strcmp(style, "upper-roman") == 0) {
        if (v < 1 || v > 3999) return g_strdup_printf("%d", v);
        static const struct { int n; const char *s; } R[] = {
            {1000,"m"},{900,"cm"},{500,"d"},{400,"cd"},{100,"c"},{90,"xc"},
            {50,"l"},{40,"xl"},{10,"x"},{9,"ix"},{5,"v"},{4,"iv"},{1,"i"}
        };
        GString *s = g_string_new(NULL);
        for (gsize i = 0; i < G_N_ELEMENTS(R); i++)
            while (v >= R[i].n) { g_string_append(s, R[i].s); v -= R[i].n; }
        if (style[0] == 'u') {
            char *up = g_ascii_strup(s->str, -1);
            g_string_free(s, TRUE);
            return up;
        }
        return g_string_free(s, FALSE);
    }
    if (strcmp(style, "lower-alpha") == 0 || strcmp(style, "lower-latin") == 0 ||
        strcmp(style, "upper-alpha") == 0 || strcmp(style, "upper-latin") == 0) {
        if (v < 1) return g_strdup_printf("%d", v);
        char base = (style[0] == 'u') ? 'A' : 'a';
        GString *s = g_string_new(NULL);
        char buf[16]; int n = 0;
        while (v > 0 && n < (int)sizeof(buf)) { v--; buf[n++] = base + (v % 26); v /= 26; }
        while (n > 0) g_string_append_c(s, buf[--n]);
        return g_string_free(s, FALSE);
    }
    if (strcmp(style, "lower-greek") == 0) {
        static const gunichar greek[24] = {
            0x3B1, 0x3B2, 0x3B3, 0x3B4, 0x3B5, 0x3B6, 0x3B7, 0x3B8,
            0x3B9, 0x3BA, 0x3BB, 0x3BC, 0x3BD, 0x3BE, 0x3BF, 0x3C0,
            0x3C1, 0x3C3, 0x3C4, 0x3C5, 0x3C6, 0x3C7, 0x3C8, 0x3C9,
        };
        if (v < 1) return g_strdup_printf("%d", v);
        gunichar bg[16];
        int n = 0, w = v;
        while (w > 0 && n < 16) { w--; bg[n++] = greek[w % 24]; w /= 24; }
        GString *s = g_string_new(NULL);
        while (n > 0) g_string_append_unichar(s, bg[--n]);
        return g_string_free(s, FALSE);
    }
    return g_strdup_printf("%d", v);
}

static void
counter_apply_style(GHashTable *current, const ns_style *s)
{
    if (!s) return;
    if (s->values[NS_CSS_COUNTER_RESET] &&
        s->values[NS_CSS_COUNTER_RESET]->kind == NS_CSS_V_KEYWORD)
        counter_apply_decl(current,
            s->values[NS_CSS_COUNTER_RESET]->u.keyword, FALSE);
    if (s->values[NS_CSS_COUNTER_INCREMENT] &&
        s->values[NS_CSS_COUNTER_INCREMENT]->kind == NS_CSS_V_KEYWORD)
        counter_apply_decl(current,
            s->values[NS_CSS_COUNTER_INCREMENT]->u.keyword, TRUE);
}

static void
counter_walk(const ns_node *n, GHashTable *styles,
             GHashTable *current, GHashTable *snapshots, int depth)
{
    if (!n || depth >= NS_LAYOUT_MAX_DEPTH) return;
    const ns_style *s = NULL;
    if (n->kind == NS_NODE_ELEMENT && styles) {
        s = g_hash_table_lookup(styles, n);
        counter_apply_style(current, s);
        if (s) counter_apply_style(current, s->before);
        gboolean need = FALSE;
        if (s && s->before && s->before->values[NS_CSS_CONTENT]) {
            const ns_css_value *cv = s->before->values[NS_CSS_CONTENT];
            if (cv->kind == NS_CSS_V_KEYWORD && cv->u.keyword &&
                strstr(cv->u.keyword, "counter")) need = TRUE;
        }
        if (s && s->after && s->after->values[NS_CSS_CONTENT]) {
            const ns_css_value *cv = s->after->values[NS_CSS_CONTENT];
            if (cv->kind == NS_CSS_V_KEYWORD && cv->u.keyword &&
                strstr(cv->u.keyword, "counter")) need = TRUE;
        }
        if (need) {
            GHashTable *snap = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                     g_free, NULL);
            GHashTableIter it;
            gpointer k, v;
            g_hash_table_iter_init(&it, current);
            while (g_hash_table_iter_next(&it, &k, &v))
                g_hash_table_insert(snap, g_strdup((const char *)k), v);
            g_hash_table_insert(snapshots, (gpointer)n, snap);
        }
    }
    for (const ns_node *c = n->first_child; c; c = c->next_sibling)
        counter_walk(c, styles, current, snapshots, depth + 1);
    if (s) counter_apply_style(current, s->after);
}

static void
counter_snapshot_destroy(gpointer p) { g_hash_table_destroy(p); }

static GHashTable *
build_counter_snapshots(const ns_node *root, GHashTable *styles)
{
    GHashTable *snapshots = g_hash_table_new_full(
        g_direct_hash, g_direct_equal, NULL, counter_snapshot_destroy);
    GHashTable *current = g_hash_table_new_full(
        g_str_hash, g_str_equal, g_free, NULL);
    counter_walk(root, styles, current, snapshots, 0);
    g_hash_table_destroy(current);
    return snapshots;
}

static char *
substitute_one_counter(const char *body, const ns_node *host, gboolean is_counters)
{
    const char *p = body;
    while (*p && g_ascii_isspace(*p)) p++;
    const char *name_s = p;
    while (*p && *p != ',' && *p != ')' && !g_ascii_isspace(*p)) p++;
    gsize nlen = (gsize)(p - name_s);
    if (nlen == 0) return g_strdup("");
    char *name = g_strndup(name_s, nlen);
    char *sep = NULL;
    char *style = NULL;
    while (*p == ',' || g_ascii_isspace(*p)) {
        if (*p == ',') p++;
        while (*p && g_ascii_isspace(*p)) p++;
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            const char *vs = p;
            while (*p && *p != q) p++;
            char *v = g_strndup(vs, p - vs);
            if (*p) p++;
            if (!sep) sep = v;
            else { g_free(v); }
        } else if (g_ascii_isalpha(*p)) {
            const char *vs = p;
            while (*p && *p != ',' && *p != ')' && !g_ascii_isspace(*p)) p++;
            if (!style) style = g_strndup(vs, p - vs);
        } else {
            break;
        }
    }
    if (is_counters && !sep) sep = g_strdup("");
    GHashTable *snap = g_counters_for_layout
        ? g_hash_table_lookup(g_counters_for_layout, host) : NULL;
    int v = snap ? GPOINTER_TO_INT(g_hash_table_lookup(snap, name)) : 0;
    char *formatted = counter_format(v, style);
    g_free(name); g_free(sep); g_free(style);
    return formatted;
}

static char *
resolve_pseudo_content(const char *raw, const ns_node *host)
{
    if (!raw || !*raw) return NULL;
    if (strcmp(raw, "none") == 0 || strcmp(raw, "normal") == 0) return NULL;
    gboolean has_func = strchr(raw, '(') != NULL;
    gboolean has_string = strchr(raw, '"') || strchr(raw, '\'');
    if (!has_func && !has_string) return g_strdup(raw);
    GString *out = g_string_new(NULL);
    const char *p = raw;
    while (*p) {
        while (*p && g_ascii_isspace(*p)) p++;
        if (!*p || *p == '/') break;
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            const char *start = p;
            while (*p && *p != q) {
                if (*p == '\\' && p[1]) { p += 2; continue; }
                p++;
            }
            char *raw_str = g_strndup(start, p - start);
            for (const char *r = raw_str; *r; )
                ns_css_append_unescaped(out, &r);
            g_free(raw_str);
            if (*p == q) p++;
        } else if (g_str_has_prefix(p, "attr(")) {
            p += 5;
            while (*p == ' ') p++;
            const char *start = p;
            while (*p && *p != ')' && *p != ',' && *p != ' ') p++;
            if (host && p != start) {
                char *attr_name = g_strndup(start, p - start);
                const char *val = ns_element_get_attr(host, attr_name);
                if (val) g_string_append(out, val);
                g_free(attr_name);
            }
            while (*p && *p != ')') p++;
            if (*p == ')') p++;
        } else if (g_str_has_prefix(p, "counter(") ||
                   g_str_has_prefix(p, "counters(")) {
            gboolean is_counters = (p[7] == 's');
            p += is_counters ? 9 : 8;
            const char *body_s = p;
            int depth = 1;
            while (*p && depth > 0) {
                if (*p == '(') depth++;
                else if (*p == ')') { depth--; if (depth == 0) break; }
                p++;
            }
            char *body = g_strndup(body_s, p - body_s);
            if (*p == ')') p++;
            char *sub = substitute_one_counter(body, host, is_counters);
            if (sub) g_string_append(out, sub);
            g_free(sub);
            g_free(body);
        } else {
            const char *id = p;
            while (*p && (g_ascii_isalnum((guchar)*p) || *p == '-')) p++;
            if (*p == '(') {
                int depth = 0;
                for (; *p; p++) {
                    if (*p == '(') depth++;
                    else if (*p == ')') { depth--; if (depth == 0) { p++; break; } }
                }
            } else {
                while (*p && !g_ascii_isspace(*p) && *p != '"' && *p != '\'') p++;
                if (p > id)
                    g_string_append_len(out, id, p - id);
                else
                    p++;
            }
        }
    }
    return g_string_free(out, FALSE);
}

static char *
apply_text_transform(const char *src, const char *tt)
{
    if (!src || !tt) return NULL;
    if (strcmp(tt, "uppercase") == 0)
        return g_utf8_strup(src, -1);
    if (strcmp(tt, "lowercase") == 0)
        return g_utf8_strdown(src, -1);
    if (strcmp(tt, "capitalize") == 0) {
        GString *out = g_string_new(NULL);
        gboolean at_word_start = TRUE;
        for (const char *p = src; p && *p; ) {
            gunichar c = g_utf8_get_char(p);
            const char *next = g_utf8_next_char(p);
            if (g_unichar_isspace(c) || c == '-' || c == '/') {
                g_string_append_len(out, p, next - p);
                at_word_start = TRUE;
            } else {
                if (at_word_start) {
                    gunichar uc = g_unichar_totitle(c);
                    char buf[8];
                    gint nb = g_unichar_to_utf8(uc, buf);
                    g_string_append_len(out, buf, nb);
                    at_word_start = FALSE;
                } else {
                    g_string_append_len(out, p, next - p);
                }
            }
            p = next;
        }
        return g_string_free(out, FALSE);
    }
    return NULL;
}

typedef struct ns_progress_state {
    gboolean determinate;
    double max;
    double value;
    double frac;
} ns_progress_state;

typedef struct ns_meter_state {
    double min;
    double max;
    double value;
    double low;
    double high;
    double optimum;
    double frac;
    int quality;
} ns_meter_state;

static gboolean
parse_float_attr(const ns_node *n, const char *attr, double *out)
{
    return ns_html_parse_float(ns_element_get_attr(n, attr), out);
}

static ns_progress_state
progress_state_for(const ns_node *n)
{
    ns_progress_state st = {0};
    double parsed;
    st.max = parse_float_attr(n, "max", &parsed) && parsed > 0 ? parsed : 1.0;
    st.determinate = ns_element_get_attr(n, "value") != NULL;
    if (st.determinate) {
        st.value = parse_float_attr(n, "value", &parsed) && parsed > 0 ? parsed : 0.0;
        if (st.value > st.max) st.value = st.max;
        st.frac = st.max > 0 ? st.value / st.max : 0;
    } else {
        st.value = 0;
        st.frac = -1;
    }
    return st;
}

static ns_meter_state
meter_state_for(const ns_node *n)
{
    ns_meter_state st = {0};
    double parsed;
    st.min = parse_float_attr(n, "min", &parsed) ? parsed : 0.0;
    double candidate_max = parse_float_attr(n, "max", &parsed) ? parsed : 1.0;
    st.max = candidate_max >= st.min ? candidate_max : st.min;
    double candidate_value = parse_float_attr(n, "value", &parsed) ? parsed : 0.0;
    st.value = candidate_value;
    if (st.value < st.min) st.value = st.min;
    if (st.value > st.max) st.value = st.max;
    double candidate_low = parse_float_attr(n, "low", &parsed) ? parsed : st.min;
    st.low = candidate_low;
    if (st.low < st.min) st.low = st.min;
    if (st.low > st.max) st.low = st.max;
    double candidate_high = parse_float_attr(n, "high", &parsed) ? parsed : st.max;
    st.high = candidate_high;
    if (st.high < st.low) st.high = st.low;
    if (st.high > st.max) st.high = st.max;
    double midpoint = st.min + (st.max - st.min) / 2.0;
    double candidate_optimum = parse_float_attr(n, "optimum", &parsed) ? parsed : midpoint;
    st.optimum = candidate_optimum;
    if (st.optimum < st.min) st.optimum = st.min;
    if (st.optimum > st.max) st.optimum = st.max;
    st.frac = st.max > st.min ? (st.value - st.min) / (st.max - st.min) : 1.0;
    if (st.optimum >= st.low && st.optimum <= st.high)
        st.quality = (st.value >= st.low && st.value <= st.high) ? 0 : 1;
    else if (st.optimum < st.low)
        st.quality = st.value < st.low ? 0 : (st.value <= st.high ? 1 : 2);
    else
        st.quality = st.value > st.high ? 0 : (st.value >= st.low ? 1 : 2);
    return st;
}

static char *
quotes_string_for(const ns_style *s, int depth, gboolean closing)
{
    if (depth < 0) depth = 0;
    const ns_css_value *qv = s ? s->values[NS_CSS_QUOTES] : NULL;
    const char *spec = (qv && qv->kind == NS_CSS_V_KEYWORD) ? qv->u.keyword
                                                            : NULL;
    if (spec && strcmp(spec, "none") == 0) return g_strdup("");
    if (spec && strchr(spec, '"') == NULL && strchr(spec, '\'') == NULL)
        spec = NULL;
    if (spec) {
        GPtrArray *parts = g_ptr_array_new_with_free_func(g_free);
        for (const char *p = spec; *p; ) {
            if (*p == '"' || *p == '\'') {
                char term = *p++;
                GString *part = g_string_new(NULL);
                while (*p && *p != term)
                    ns_css_append_unescaped(part, &p);
                g_ptr_array_add(parts, g_string_free(part, FALSE));
                if (*p) p++;
            } else {
                p++;
            }
        }
        guint pairs = parts->len / 2;
        char *result = NULL;
        if (pairs > 0) {
            guint pair = (guint)depth < pairs ? (guint)depth : pairs - 1;
            result = g_strdup(g_ptr_array_index(parts,
                                                pair * 2 + (closing ? 1 : 0)));
        }
        g_ptr_array_free(parts, TRUE);
        if (result) return result;
    }
    if (depth % 2 == 0)
        return g_strdup(closing ? "\xe2\x80\x9d" : "\xe2\x80\x9c");
    return g_strdup(closing ? "\xe2\x80\x99" : "\xe2\x80\x98");
}

static void
append_pseudo_content(GString *out, const ns_css_value *cv,
                      const ns_node *host, const ns_style *host_style)
{
    if (!cv || cv->kind != NS_CSS_V_KEYWORD || !cv->u.keyword) return;
    char *resolved = resolve_pseudo_content(cv->u.keyword, host);
    if (!resolved) return;
    char *quote = NULL;
    const char *txt = resolved;
    if (strcmp(resolved, "open-quote") == 0)
        txt = quote = quotes_string_for(host_style, 0, FALSE);
    else if (strcmp(resolved, "close-quote") == 0)
        txt = quote = quotes_string_for(host_style, 0, TRUE);
    else if (strcmp(resolved, "no-open-quote") == 0 ||
             strcmp(resolved, "no-close-quote") == 0) { g_free(resolved); return; }
    g_string_append(out, txt);
    g_free(quote);
    g_free(resolved);
}

static void
emit_open_select_option(collector_ctx *ctx, const ns_node *option)
{
    if (!option) return;
    g_string_append(ctx->out, "\xe2\x80\xa8");
    gsize start = ctx->out->len;
    g_string_append(ctx->out, "\xc2\xa0\xc2\xa0");
    char *t = ns_option_label_dup(option);
    if (t && *t) g_string_append(ctx->out, t);
    g_free(t);
    g_string_append(ctx->out, "\xc2\xa0\xc2\xa0");
    emit_form_attr_sized(ctx->attrs, NS_INLINE_INPUT_FIELD,
                         start, ctx->out->len, option, ctx->styles);
}

static void
emit_listbox_option(collector_ctx *ctx, const ns_node *option, gboolean first)
{
    if (!option) return;
    if (!first) g_string_append(ctx->out, "\xe2\x80\xa8");
    gsize start = ctx->out->len;
    g_string_append(ctx->out, "\xc2\xa0\xc2\xa0");
    char *t = ns_option_label_dup(option);
    if (t && *t) g_string_append(ctx->out, t);
    g_free(t);
    g_string_append(ctx->out, "\xc2\xa0\xc2\xa0");
    emit_form_attr_sized(ctx->attrs, NS_INLINE_INPUT_FIELD,
                         start, ctx->out->len, option, ctx->styles);
}

static const ns_node *
find_datalist_by_id(const ns_node *node, const char *id, int depth)
{
    if (!node || !id || depth >= NS_LAYOUT_MAX_DEPTH) return NULL;
    if (node->kind == NS_NODE_ELEMENT && node->name &&
        strcmp(node->name, "datalist") == 0) {
        const char *did = ns_element_get_attr(node, "id");
        if (did && strcmp(did, id) == 0) return node;
    }
    for (const ns_node *c = node->first_child; c; c = c->next_sibling) {
        const ns_node *m = find_datalist_by_id(c, id, depth + 1);
        if (m) return m;
    }
    return NULL;
}

static char *
datalist_option_value(const ns_node *option)
{
    const char *v = ns_element_get_attr(option, "value");
    if (v && *v) return g_strdup(v);
    return ns_option_label_dup(option);
}

static void
emit_datalist_suggestions(collector_ctx *ctx, const ns_node *input)
{
    const char *list_id = ns_element_get_attr(input, "list");
    if (!list_id || !*list_id) return;
    const ns_node *root = input;
    while (root->parent) root = root->parent;
    const ns_node *dl = find_datalist_by_id(root, list_id, 0);
    if (!dl) return;

    const char *cur = ns_input_used_value(input);
    char *needle = (cur && *cur) ? g_utf8_casefold(cur, -1) : NULL;
    int shown = 0;
    for (const ns_node *o = dl->first_child; o && shown < 8; o = o->next_sibling) {
        if (!ns_node_is_element_named(o, "option")) continue;
        char *val = datalist_option_value(o);
        if (!val || !*val) { g_free(val); continue; }
        gboolean match = TRUE;
        if (needle) {
            char *vl = g_utf8_casefold(val, -1);
            match = strstr(vl, needle) != NULL &&
                    g_ascii_strcasecmp(vl, needle) != 0;
            g_free(vl);
        }
        if (match) {
            g_string_append(ctx->out, "\xe2\x80\xa8");
            gsize start = ctx->out->len;
            g_string_append(ctx->out, "\xc2\xa0\xc2\xa0");
            g_string_append(ctx->out, val);
            g_string_append(ctx->out, "\xc2\xa0\xc2\xa0");
            emit_form_attr_sized(ctx->attrs, NS_INLINE_INPUT_FIELD,
                                 start, ctx->out->len, o, ctx->styles);
            shown++;
        }
        g_free(val);
    }
    g_free(needle);
}

static void
collect_walk(const ns_node *n, collector_ctx *ctx, int depth)
{
    if (!n || depth >= NS_LAYOUT_MAX_DEPTH) return;
    if (node_is_frame_fallback(n)) return;
    if (n->kind == NS_NODE_TEXT) {
        if (!n->text) return;
        gsize start = ctx->out->len;
        if (n->parent && ns_node_is_contenteditable_host(n->parent)) {
            gboolean focused = g_focused_is_contenteditable_for_layout &&
                               n->parent == g_focused_input_for_layout;
            const char *val = n->text;
            gsize vlen = strlen(val);
            gsize cb = g_focused_caret_byte_for_layout;
            gsize ab = g_focused_sel_anchor_byte_for_layout;
            if (cb > vlen) cb = vlen;
            if (ab > vlen) ab = vlen;
            gsize caret_pos = start, anchor_pos = start;
            for (gsize i = 0; i <= vlen; i++) {
                if (i == cb) caret_pos = ctx->out->len;
                if (i == ab) anchor_pos = ctx->out->len;
                if (i == vlen) break;
                char ch = val[i];
                if (ch == '\n') g_string_append(ctx->out, "\xe2\x80\xa8");
                else if (ch != '\r') g_string_append_c(ctx->out, ch);
            }
            if (ctx->active_href) {
                ns_link_range r = {
                    .start = start, .len = ctx->out->len - start,
                    .href  = g_strdup(ctx->active_href),
                    .target = ctx->active_target ? g_strdup(ctx->active_target) : NULL,
                    .dom   = ctx->active_link_node,
                };
                g_array_append_val(ctx->links, r);
            }
            if (focused) {
                g_string_append(ctx->out, "\xc2\xa0");
                if (anchor_pos != caret_pos) {
                    gsize s0 = anchor_pos < caret_pos ? anchor_pos : caret_pos;
                    gsize s1 = anchor_pos < caret_pos ? caret_pos : anchor_pos;
                    emit_attr(ctx->attrs, NS_INLINE_SELECTION, s0, s1);
                }
                emit_attr(ctx->attrs, NS_INLINE_CARET, caret_pos, caret_pos + 1);
            }
            return;
        }
        char *xformed = ctx->text_transform
                        ? apply_text_transform(n->text, ctx->text_transform)
                        : NULL;
        g_string_append(ctx->out, xformed ? xformed : n->text);
        g_free(xformed);
        if (ctx->active_href) {
            ns_link_range r = {
                .start = start,
                .len   = ctx->out->len - start,
                .href  = g_strdup(ctx->active_href),
                .target = ctx->active_target ? g_strdup(ctx->active_target) : NULL,
                .dom   = ctx->active_link_node,
            };
            g_array_append_val(ctx->links, r);
        }
        return;
    }
    if (n->kind != NS_NODE_ELEMENT) return;
    if (node_is_non_rendering(n)) return;
    if (n == g_inline_skip_node) return;
    const ns_style *s = g_hash_table_lookup(ctx->styles, n);
    if (s && style_is_none(s)) return;
    if (s && style_is_absolute_or_fixed(s)) {
        const ns_css_value *pv = s->values[NS_CSS_POSITION];
        gboolean fixed = pv && pv->kind == NS_CSS_V_KEYWORD && pv->u.keyword &&
                         strcmp(pv->u.keyword, "fixed") == 0;
        if (g_abs_pending &&
            (!g_abs_seen || g_hash_table_add(g_abs_seen, (gpointer)n))) {
            ns_abs_entry e;
            e.dom = n;
            e.pseudo = NULL;
            e.fixed = fixed;
            g_array_append_val(g_abs_pending, e);
        }
        if (ctx->atomics && !fixed && ctx->abs_placeholders && g_abs_ph_set) {
            ns_box *ph = box_new(NS_BOX_BLOCK);
            ns_atomic_raw rec = { .start = ctx->out->len, .box = ph };
            g_string_append(ctx->out, "\xef\xbf\xbc");
            g_array_append_val(ctx->atomics, rec);
            g_hash_table_insert(g_abs_ph_set, ph, (gpointer)n);
        }
        return;
    }

    if (ctx->atomics && n != g_form_control_inline &&
        is_atomic_inline(n, ctx->styles)) {
        ns_box *sub = build_block(n, ctx->styles);
        if (sub) {
            ns_atomic_raw rec = { .start = ctx->out->len, .box = sub };
            g_string_append(ctx->out, "\xef\xbf\xbc");
            g_array_append_val(ctx->atomics, rec);
        }
        return;
    }

    if (!n->name) {
        for (const ns_node *c = n->first_child; c; c = c->next_sibling)
            collect_walk(c, ctx, depth + 1);
        return;
    }
    if (strcmp(n->name, "br") == 0) {
        g_string_append(ctx->out, "\xe2\x80\xa8");
        return;
    }
    if (strcmp(n->name, "wbr") == 0) {
        g_string_append(ctx->out, "\xe2\x80\x8b");
        return;
    }
    if (strcmp(n->name, "progress") == 0 || strcmp(n->name, "meter") == 0) {
        gboolean is_meter = strcmp(n->name, "meter") == 0;
        gsize start = ctx->out->len;
        for (int i = 0; i < 12; i++) g_string_append(ctx->out, "\xc2\xa0");
        double frac = 0;
        guint8 r = 0, g = 0, b = 0, alpha = 0;
        if (is_meter) {
            ns_meter_state st = meter_state_for(n);
            frac = st.frac;
            if (st.quality == 0) { r = 0x2e; g = 0x9d; b = 0x54; }
            else if (st.quality == 1) { r = 0xd0; g = 0xa4; b = 0x1f; }
            else { r = 0xc4; g = 0x43; b = 0x3c; }
            alpha = 255;
        } else {
            ns_progress_state st = progress_state_for(n);
            frac = st.frac;
        }
        ns_inline_attr attr = {
            .kind = is_meter ? NS_INLINE_METER : NS_INLINE_PROGRESS,
            .start = start, .len = ctx->out->len - start,
            .font_size_px = frac,
            .r = r, .g = g, .b = b, .a = alpha,
            .dom = n,
        };
        g_array_append_val(ctx->attrs, attr);
        return;
    }
    if (strcmp(n->name, "input") == 0) {
        const char *type = ns_element_get_attr(n, "type");
        gboolean is_password = type && g_ascii_strcasecmp(type, "password") == 0;
        gboolean is_text = !type || !*type ||
                           is_password ||
                           g_ascii_strcasecmp(type, "text") == 0 ||
                           g_ascii_strcasecmp(type, "search") == 0 ||
                           g_ascii_strcasecmp(type, "email") == 0 ||
                           g_ascii_strcasecmp(type, "url") == 0 ||
                           g_ascii_strcasecmp(type, "tel") == 0 ||
                           g_ascii_strcasecmp(type, "number") == 0;
        if (is_text) {
            if (!inline_run_at_line_start(ctx->out))
                g_string_append(ctx->out, "\xc2\xa0\xc2\xa0\xc2\xa0");
            gsize start = ctx->out->len;
            g_string_append(ctx->out, "\xc2\xa0");
            int leading = text_input_leading_spaces(s);
            for (int i = 0; i < leading; i++)
                g_string_append(ctx->out, "\xc2\xa0");
            const char *real_value = ns_input_used_value(n);
            const char *v = real_value;
            gboolean is_placeholder = FALSE;
            if (!v || !*v) {
                v = ns_element_get_attr(n, "placeholder");
                is_placeholder = (v && *v);
            }
            gboolean focused = (n == g_focused_input_for_layout);
            gsize val_start = ctx->out->len;
            gsize disp_end = val_start;
            gsize caret_pos = val_start;
            gsize anchor_pos = val_start;
            gsize caret_byte = g_focused_caret_byte_for_layout;
            gsize anchor_byte = g_focused_sel_anchor_byte_for_layout;
            if (real_value && caret_byte > strlen(real_value))
                caret_byte = strlen(real_value);
            if (real_value && anchor_byte > strlen(real_value))
                anchor_byte = strlen(real_value);
            else if (!real_value) anchor_byte = 0;
            int size = text_input_columns(n);
            glong displayed_chars = 0;
            if (v && *v && is_password && !is_placeholder) {
                glong cps = g_utf8_strlen(v, -1);
                glong skip_cps = focused
                    ? text_input_focused_skip_cps(real_value, caret_byte,
                                                  cps, size)
                    : text_input_align_skip_cps(s, n, cps, size);
                glong shown_cps = cps - skip_cps;
                if (shown_cps > size) shown_cps = size;
                for (glong i = 0; i < shown_cps; i++)
                    g_string_append(ctx->out, "\xe2\x80\xa2");
                displayed_chars = shown_cps;
                disp_end = ctx->out->len;
                if (focused) {
                    glong cp_before = real_value
                        ? g_utf8_pointer_to_offset(real_value, real_value + caret_byte)
                        : 0;
                    glong cp_in_shown = cp_before > skip_cps
                                        ? cp_before - skip_cps : 0;
                    if (cp_in_shown > shown_cps) cp_in_shown = shown_cps;
                    caret_pos = val_start + (gsize)cp_in_shown * 3;
                }
            } else if (v && *v) {
                const char *display_v = v;
                gsize skip_bytes = 0;
                gsize display_bytes = strlen(v);
                glong value_cps = g_utf8_strlen(v, -1);
                if (value_cps > size) {
                    glong skip_cps = focused && real_value && v == real_value
                        ? text_input_focused_skip_cps(real_value, caret_byte,
                                                      value_cps, size)
                        : text_input_align_skip_cps(s, n, value_cps, size);
                    const char *p = text_input_advance_cps(v, skip_cps);
                    const char *q = text_input_advance_cps(p, size);
                    display_v = p;
                    display_bytes = (gsize)(q - p);
                    if (real_value && v == real_value) {
                        skip_bytes = (gsize)(p - real_value);
                    }
                }
                g_string_append_len(ctx->out, display_v, (gssize)display_bytes);
                displayed_chars = g_utf8_strlen(display_v, (gssize)display_bytes);
                disp_end = ctx->out->len;
                if (focused && real_value && *real_value) {
                    gsize caret_in_shown = caret_byte > skip_bytes
                                           ? caret_byte - skip_bytes : 0;
                    if (caret_in_shown > display_bytes) caret_in_shown = display_bytes;
                    caret_pos = val_start + caret_in_shown;
                    gsize anchor_in_shown = anchor_byte > skip_bytes
                                            ? anchor_byte - skip_bytes : 0;
                    if (anchor_in_shown > display_bytes) anchor_in_shown = display_bytes;
                    anchor_pos = val_start + anchor_in_shown;
                } else if (focused) {
                    caret_pos = val_start;
                    anchor_pos = val_start;
                }
            } else {
                if (focused) { caret_pos = val_start; anchor_pos = val_start; }
            }
            glong pad = (glong)size - displayed_chars;
            if (pad < 0) pad = 0;
            glong left_pad = 0, right_pad = pad;
            if (pad > 0) {
                left_pad = text_input_align_offset(s, n, pad);
                right_pad = pad - left_pad;
            }
            if (left_pad > 0) {
                GString *lp = g_string_new(NULL);
                for (glong i = 0; i < left_pad; i++)
                    g_string_append(lp, "\xc2\xa0");
                g_string_insert_len(ctx->out, val_start, lp->str, lp->len);
                gsize shift = (gsize)lp->len;
                g_string_free(lp, TRUE);
                caret_pos += shift;
                anchor_pos += shift;
                disp_end += shift;
                val_start += shift;
            }
            for (glong i = 0; i < right_pad; i++)
                g_string_append(ctx->out, "\xc2\xa0");
            g_string_append(ctx->out, "\xc2\xa0");
            ns_inline_attr_kind kind = focused
                                       ? NS_INLINE_INPUT_FIELD_FOCUSED
                                       : NS_INLINE_INPUT_FIELD;
            emit_form_attr_sized(ctx->attrs, kind, start, ctx->out->len, n, ctx->styles);
            emit_control_text_style(ctx->attrs, s, start, ctx->out->len,
                                    val_start, disp_end, is_placeholder,
                                    ns_element_get_attr(n, "disabled") != NULL);
            if (!is_placeholder && disp_end > val_start &&
                ns_element_get_attr(n, "disabled") == NULL &&
                ns_node_spellcheck_used(n)) {
                ns_inline_attr sc = { .kind = NS_INLINE_SPELLCHECK,
                                      .start = val_start,
                                      .len = disp_end - val_start, .dom = n };
                g_array_append_val(ctx->attrs, sc);
            }
            if (focused) {
                if (anchor_pos != caret_pos) {
                    gsize s0 = anchor_pos < caret_pos ? anchor_pos : caret_pos;
                    gsize s1 = anchor_pos < caret_pos ? caret_pos : anchor_pos;
                    emit_attr(ctx->attrs, NS_INLINE_SELECTION, s0, s1);
                }
                emit_attr(ctx->attrs, NS_INLINE_CARET, caret_pos, caret_pos + 1);
            }
            if (focused && g_datalist_open_for_layout)
                emit_datalist_suggestions(ctx, n);
        } else if (type && (g_ascii_strcasecmp(type, "submit") == 0 ||
                            g_ascii_strcasecmp(type, "button") == 0 ||
                            g_ascii_strcasecmp(type, "reset") == 0)) {
            const char *v = ns_element_get_attr(n, "value");
            gsize start = ctx->out->len;
            if (!v)
                v = g_ascii_strcasecmp(type, "submit") == 0 ? "Submit"
                  : g_ascii_strcasecmp(type, "reset")  == 0 ? "Reset"
                                                            : "Button";
            int lead  = control_pad_spaces(s, NS_CSS_PADDING_LEFT);
            int trail = control_pad_spaces(s, NS_CSS_PADDING_RIGHT);
            g_string_append(ctx->out, "\xc2\xa0");
            for (int i = 0; i < lead; i++)
                g_string_append(ctx->out, "\xc2\xa0");
            g_string_append(ctx->out, v);
            for (int i = 0; i < trail; i++)
                g_string_append(ctx->out, "\xc2\xa0");
            g_string_append(ctx->out, "\xc2\xa0");
            emit_form_attr_sized(ctx->attrs, NS_INLINE_BUTTON, start, ctx->out->len, n, ctx->styles);
            g_string_append_c(ctx->out, ' ');
        } else if (type && g_ascii_strcasecmp(type, "checkbox") == 0) {
            gboolean checked = ns_input_is_checked(n);
            gsize start = ctx->out->len;
            g_string_append(ctx->out, checked ? "\xe2\x98\x91" : "\xe2\x98\x90");
            emit_form_attr_sized(ctx->attrs,
                checked ? NS_INLINE_CHECKBOX_CHECKED : NS_INLINE_CHECKBOX,
                start, ctx->out->len, n, ctx->styles);
            g_string_append_c(ctx->out, ' ');
        } else if (type && g_ascii_strcasecmp(type, "radio") == 0) {
            gboolean checked = ns_input_is_checked(n);
            gsize start = ctx->out->len;
            g_string_append(ctx->out, checked ? "\xe2\x97\x89" : "\xe2\x97\x8b");
            emit_form_attr_sized(ctx->attrs,
                checked ? NS_INLINE_RADIO_CHECKED : NS_INLINE_RADIO,
                start, ctx->out->len, n, ctx->styles);
            g_string_append_c(ctx->out, ' ');
        } else if (type && g_ascii_strcasecmp(type, "file") == 0) {
            gsize start = ctx->out->len;
            g_string_append(ctx->out, "\xc2\xa0" "Choose File" "\xc2\xa0");
            emit_form_attr_sized(ctx->attrs, NS_INLINE_BUTTON, start, ctx->out->len, n, ctx->styles);
            const char *fpath = ns_element_get_attr(n, "data-nd-file-path");
            if (fpath && *fpath) {
                const char *base = strrchr(fpath, '/');
#ifdef G_OS_WIN32
                const char *base_w = strrchr(fpath, '\\');
                if (!base || (base_w && base_w > base)) base = base_w;
#endif
                const char *show = base ? base + 1 : fpath;
                g_string_append_c(ctx->out, ' ');
                g_string_append(ctx->out, show);
            } else {
                g_string_append(ctx->out, " (no file chosen)");
            }
        } else if (type && g_ascii_strcasecmp(type, "color") == 0) {
            const char *v = ns_element_get_attr(n, "value");
            const char *hex = v && *v ? v : "#000000";
            gsize start = ctx->out->len;
            g_string_append(ctx->out, "\xc2\xa0");
            gsize swatch_start = ctx->out->len;
            g_string_append(ctx->out, "\xe2\x96\xa0");
            gsize swatch_end = ctx->out->len;
            g_string_append(ctx->out, "\xc2\xa0");
            g_string_append(ctx->out, hex);
            g_string_append(ctx->out, "\xc2\xa0");
            emit_form_attr_sized(ctx->attrs, NS_INLINE_INPUT_FIELD, start, ctx->out->len, n, ctx->styles);
            guint8 r8 = 0, g8 = 0, b8 = 0;
            if (hex[0] == '#' && strlen(hex) >= 7) {
                unsigned int rv, gv, bv;
                if (sscanf(hex + 1, "%2x%2x%2x", &rv, &gv, &bv) == 3) {
                    r8 = (guint8)rv; g8 = (guint8)gv; b8 = (guint8)bv;
                }
            }
            emit_color_attr(ctx->attrs, swatch_start, swatch_end, r8, g8, b8, 255);
        } else if (type && (g_ascii_strcasecmp(type, "range") == 0)) {
            const char *v = ns_element_get_attr(n, "value");
            const char *mn = ns_element_get_attr(n, "min");
            const char *mx = ns_element_get_attr(n, "max");
            double vv = v && *v ? g_ascii_strtod(v, NULL) : 50;
            double mnv = mn && *mn ? g_ascii_strtod(mn, NULL) : 0;
            double mxv = mx && *mx ? g_ascii_strtod(mx, NULL) : 100;
            if (mxv <= mnv) mxv = mnv + 1;
            double frac = (vv - mnv) / (mxv - mnv);
            if (frac < 0) frac = 0;
            if (frac > 1) frac = 1;
            int knob_at = (int)(frac * 10 + 0.5);
            gsize start = ctx->out->len;
            g_string_append(ctx->out, "\xc2\xa0");
            for (int i = 0; i <= 10; i++) {
                if (i == knob_at)
                    g_string_append(ctx->out, "\xe2\x97\x8f");
                else
                    g_string_append(ctx->out, "\xe2\x94\x80");
            }
            g_string_append_printf(ctx->out, " %g\xc2\xa0", vv);
            emit_form_attr_sized(ctx->attrs, NS_INLINE_INPUT_FIELD, start, ctx->out->len, n, ctx->styles);
        } else if (type && (g_ascii_strcasecmp(type, "date") == 0 ||
                            g_ascii_strcasecmp(type, "datetime-local") == 0 ||
                            g_ascii_strcasecmp(type, "time") == 0 ||
                            g_ascii_strcasecmp(type, "month") == 0 ||
                            g_ascii_strcasecmp(type, "week") == 0)) {
            const char *v = ns_element_get_attr(n, "value");
            gsize start = ctx->out->len;
            g_string_append(ctx->out, "\xc2\xa0");
            if (v && *v) g_string_append(ctx->out, v);
            else         g_string_append(ctx->out, "____-__-__");
            g_string_append(ctx->out, "\xc2\xa0");
            emit_form_attr_sized(ctx->attrs, NS_INLINE_INPUT_FIELD, start, ctx->out->len, n, ctx->styles);
        }
        return;
    }
    if (strcmp(n->name, "button") == 0) {
        char *label = ns_node_collect_text(n);
        if (!label || !*label) {
            g_free(label);
            const ns_style *bs = ctx->styles
                ? g_hash_table_lookup(ctx->styles, n) : NULL;
            GString *pseudo = g_string_new(NULL);
            if (bs && bs->before)
                append_pseudo_content(pseudo, bs->before->values[NS_CSS_CONTENT],
                                      n, bs->before);
            if (bs && bs->after)
                append_pseudo_content(pseudo, bs->after->values[NS_CSS_CONTENT],
                                      n, bs->after);
            if (pseudo->len > 0) {
                label = g_string_free(pseudo, FALSE);
            } else {
                g_string_free(pseudo, TRUE);
                const char *aria = ns_element_get_attr(n, "aria-label");
                const char *title = ns_element_get_attr(n, "title");
                const char *value = ns_element_get_attr(n, "value");
                if (aria && *aria) label = g_strdup(aria);
                else if (title && *title) label = g_strdup(title);
                else if (value && *value) label = g_strdup(value);
                else label = g_strdup("");
            }
        }
        if (!*label) {
            g_free(label);
            return;
        }
        gsize start = ctx->out->len;
        g_string_append(ctx->out, "\xc2\xa0");
        g_string_append(ctx->out, label);
        g_string_append(ctx->out, "\xc2\xa0");
        emit_form_attr_sized(ctx->attrs, NS_INLINE_BUTTON, start, ctx->out->len, n, ctx->styles);
        g_free(label);
        return;
    }
    if (strcmp(n->name, "select") == 0) {
        gboolean multi = ns_element_get_attr(n, "multiple") != NULL;
        const char *size_attr = ns_element_get_attr(n, "size");
        int size_n = size_attr ? ns_parse_int(size_attr, 0, 0, 1000) : 0;
        gboolean listbox = multi || size_n > 1;
        if (listbox) {
            int shown = 0;
            int cap = size_n > 0 ? size_n : 6;
            gboolean first = TRUE;
            for (const ns_node *c = n->first_child; c && shown < cap; c = c->next_sibling) {
                if (c->kind != NS_NODE_ELEMENT || !c->name) continue;
                if (strcmp(c->name, "optgroup") == 0) {
                    const char *gl = ns_element_get_attr(c, "label");
                    if (gl && *gl) {
                        if (!first) g_string_append(ctx->out, "\xe2\x80\xa8");
                        g_string_append_printf(ctx->out, "\xc2\xa0%s\xc2\xa0", gl);
                        first = FALSE;
                        shown++;
                    }
                    for (const ns_node *opt = c->first_child;
                         opt && shown < cap; opt = opt->next_sibling) {
                        if (!ns_node_is_element_named(opt, "option")) continue;
                        emit_listbox_option(ctx, opt, first);
                        first = FALSE;
                        shown++;
                    }
                } else if (strcmp(c->name, "option") == 0) {
                    emit_listbox_option(ctx, c, first);
                    first = FALSE;
                    shown++;
                }
            }
            if (shown == 0) {
                gsize start = ctx->out->len;
                g_string_append(ctx->out, "\xc2\xa0\xc2\xa0");
                emit_form_attr_sized(ctx->attrs, NS_INLINE_INPUT_FIELD,
                                     start, ctx->out->len, n, ctx->styles);
            }
            return;
        }
        const ns_node *chosen = ns_select_chosen_option(n);
        char *label = chosen ? ns_node_collect_text(chosen) : g_strdup("");
        if (!label) label = g_strdup("");
        gsize start = ctx->out->len;
        g_string_append(ctx->out, "\xc2\xa0");
        if (*label) g_string_append(ctx->out, label);
        g_string_append(ctx->out, n == g_open_select_for_layout
                                  ? " \xe2\x96\xb4\xc2\xa0" : " \xe2\x96\xbe\xc2\xa0");
        emit_form_attr_sized(ctx->attrs, NS_INLINE_INPUT_FIELD,
                             start, ctx->out->len, n, ctx->styles);
        g_free(label);
        if (n == g_open_select_for_layout) {
            for (const ns_node *c = n->first_child; c; c = c->next_sibling) {
                if (c->kind != NS_NODE_ELEMENT || !c->name) continue;
                const ns_node *opts[2] = { NULL, NULL };
                const ns_node *grp = NULL;
                if (strcmp(c->name, "option") == 0) {
                    opts[0] = c;
                } else if (strcmp(c->name, "optgroup") == 0) {
                    grp = c;
                } else {
                    continue;
                }
                if (grp) {
                    const char *gl = ns_element_get_attr(grp, "label");
                    if (gl && *gl) {
                        g_string_append(ctx->out, "\xe2\x80\xa8\xc2\xa0");
                        g_string_append(ctx->out, gl);
                        g_string_append(ctx->out, "\xc2\xa0");
                    }
                    for (const ns_node *o = grp->first_child; o; o = o->next_sibling)
                        if (ns_node_is_element_named(o, "option"))
                            emit_open_select_option(ctx, o);
                } else {
                    emit_open_select_option(ctx, opts[0]);
                }
            }
        }
        return;
    }
    if (strcmp(n->name, "option") == 0 || strcmp(n->name, "optgroup") == 0)
        return;
    if (strcmp(n->name, "textarea") == 0) {
        gsize start = ctx->out->len;
        gboolean focused = (n == g_focused_input_for_layout);
        g_string_append(ctx->out, "\xc2\xa0");
        gsize val_start = ctx->out->len;
        char *raw = ns_textarea_value_dup(n);
        gsize value_byte_len = strlen(raw);
        gboolean any = value_byte_len > 0;
        gsize caret_byte = g_focused_caret_byte_for_layout;
        if (caret_byte > value_byte_len) caret_byte = value_byte_len;
        gsize anchor_byte = g_focused_sel_anchor_byte_for_layout;
        if (anchor_byte > value_byte_len) anchor_byte = value_byte_len;
        gsize caret_pos = val_start;
        gsize anchor_pos = val_start;
        for (gsize i = 0; i <= value_byte_len; i++) {
            if (i == caret_byte) caret_pos = ctx->out->len;
            if (i == anchor_byte) anchor_pos = ctx->out->len;
            if (i == value_byte_len) break;
            char ch = raw[i];
            if (ch == '\n')
                g_string_append(ctx->out, "\xe2\x80\xa8");
            else if (ch != '\r')
                g_string_append_c(ctx->out, ch);
        }
        g_free(raw);
        gsize disp_end = ctx->out->len;
        gboolean is_placeholder = FALSE;
        gboolean ta_sized = ns_element_get_attr(n, "rows") ||
                            ns_element_get_attr(n, "cols");
        if (!any) {
            const char *ph = ns_element_get_attr(n, "placeholder");
            if (ph && *ph && !focused) {
                for (const char *p = ph; *p; p++) {
                    if (*p == '\n') g_string_append(ctx->out, "\xe2\x80\xa8");
                    else if (*p != '\r') g_string_append_c(ctx->out, *p);
                }
                disp_end = ctx->out->len;
                is_placeholder = TRUE;
                caret_pos = 0;
            } else if (ta_sized) {
                const char *rows_attr = ns_element_get_attr(n, "rows");
                int row_lines = rows_attr ? atoi(rows_attr) : 2;
                if (row_lines < 1) row_lines = 1;
                for (int r = 0; r < row_lines; r++) {
                    if (r) g_string_append(ctx->out, "\xe2\x80\xa8");
                    g_string_append(ctx->out, "\xc2\xa0");
                }
                if (focused) { caret_pos = val_start; anchor_pos = val_start; }
                else         { caret_pos = 0; }
            } else {
                for (int i = 0; i < 40; i++) g_string_append(ctx->out, "\xc2\xa0");
                if (focused) { caret_pos = val_start; anchor_pos = val_start; }
                else         { caret_pos = 0; }
            }
        }
        g_string_append(ctx->out, "\xc2\xa0");
        ns_inline_attr_kind ta_kind = focused
                                       ? NS_INLINE_INPUT_FIELD_FOCUSED
                                       : NS_INLINE_INPUT_FIELD;
        emit_form_attr_sized(ctx->attrs, ta_kind, start, ctx->out->len,
                             n, ctx->styles);
        emit_control_text_style(ctx->attrs, s, start, ctx->out->len,
                                val_start, disp_end, is_placeholder,
                                ns_element_get_attr(n, "disabled") != NULL);
        if (!is_placeholder && disp_end > val_start &&
            ns_element_get_attr(n, "disabled") == NULL &&
            ns_node_spellcheck_used(n)) {
            ns_inline_attr sc = { .kind = NS_INLINE_SPELLCHECK,
                                  .start = val_start,
                                  .len = disp_end - val_start, .dom = n };
            g_array_append_val(ctx->attrs, sc);
        }
        if (focused) {
            if (anchor_pos != caret_pos) {
                gsize s0 = anchor_pos < caret_pos ? anchor_pos : caret_pos;
                gsize s1 = anchor_pos < caret_pos ? caret_pos : anchor_pos;
                emit_attr(ctx->attrs, NS_INLINE_SELECTION, s0, s1);
            }
            emit_attr(ctx->attrs, NS_INLINE_CARET, caret_pos, caret_pos + 1);
        }
        return;
    }

    const char *prev_href   = ctx->active_href;
    const char *prev_target = ctx->active_target;
    const ns_node *prev_link_node = ctx->active_link_node;
    if (strcmp(n->name, "a") == 0) {
        const char *h = ns_element_get_attr(n, "href");
        if (h && *h) {
            ctx->active_href   = h;
            ctx->active_target = ns_element_get_attr(n, "target");
            ctx->active_link_node = n;
        }
    }
    double ml = length_or(s ? s->values[NS_CSS_MARGIN_LEFT]  : NULL, 0);
    double mr = length_or(s ? s->values[NS_CSS_MARGIN_RIGHT] : NULL, 0);
    double pl = length_or(s ? s->values[NS_CSS_PADDING_LEFT]  : NULL, 0);
    double pr = length_or(s ? s->values[NS_CSS_PADDING_RIGHT] : NULL, 0);
    append_inline_spacer(ctx, ml);
    gsize elem_start = ctx->out->len;
    append_inline_spacer(ctx, pl);
    gboolean bold   = tag_is_bold(n->name);
    gboolean italic = tag_is_italic(n->name);
    gboolean mono   = tag_is_monospace(n->name);
    gboolean uline  = tag_is_underline(n->name);
    gboolean oline  = FALSE;
    gboolean strike = tag_is_strike(n->name);
    const ns_css_value *fw = s ? s->values[NS_CSS_FONT_WEIGHT] : NULL;
    int font_weight_self = ns_css_font_weight_number(fw, -1);
    gboolean font_weight_active = font_weight_self > 0;
    if (font_weight_active) {
        bold = FALSE;
    } else if (fw && fw->kind == NS_CSS_V_KEYWORD && fw->u.keyword) {
        const char *kw = fw->u.keyword;
        if (strcmp(kw, "bold") == 0 || strcmp(kw, "bolder") == 0) bold = TRUE;
    }
    if (s && s->values[NS_CSS_FONT_STYLE] &&
        s->values[NS_CSS_FONT_STYLE]->kind == NS_CSS_V_KEYWORD &&
        (strcmp(s->values[NS_CSS_FONT_STYLE]->u.keyword, "italic") == 0 ||
         strcmp(s->values[NS_CSS_FONT_STYLE]->u.keyword, "oblique") == 0))
        italic = TRUE;
    if (s && s->values[NS_CSS_TEXT_DECORATION] &&
        s->values[NS_CSS_TEXT_DECORATION]->kind == NS_CSS_V_KEYWORD) {
        const char *kw = s->values[NS_CSS_TEXT_DECORATION]->u.keyword;
        if (strstr(kw, "underline")) uline = TRUE;
        if (strstr(kw, "overline")) oline = TRUE;
        if (strstr(kw, "line-through")) strike = TRUE;
        if (strstr(kw, "none")) { uline = FALSE; oline = FALSE; strike = FALSE; }
    }
    const ns_css_value *tdc = s ? s->values[NS_CSS_TEXT_DECORATION_COLOR] : NULL;
    if (tdc && tdc->kind == NS_CSS_V_COLOR && tdc->u.color.a == 0)
        { uline = FALSE; oline = FALSE; strike = FALSE; }
    const ns_style *decor_style = (uline || oline || strike) ? s : NULL;
    if (bold && ctx->bold_depth++ == 0) ctx->bold_start = ctx->out->len;
    if (italic && ctx->italic_depth++ == 0) ctx->italic_start = ctx->out->len;
    if (mono && ctx->mono_depth++ == 0) ctx->mono_start = ctx->out->len;
    if (uline && ctx->underline_depth++ == 0) ctx->underline_start = ctx->out->len;
    if (oline && ctx->overline_depth++ == 0) ctx->overline_start = ctx->out->len;
    if (strike && ctx->strike_depth++ == 0) ctx->strike_start = ctx->out->len;

    gsize weight_start = ctx->out->len;
    const ns_css_value *fst = s ? s->values[NS_CSS_FONT_STRETCH] : NULL;
    int font_stretch_self = ns_css_font_stretch_rank(fst);
    gboolean font_stretch_active = fst != NULL;
    gsize stretch_start = ctx->out->len;
    int font_kerning_self = font_kerning_int_from_style(s);
    const char *font_ligatures_self = font_ligatures_from_style(s);
    const char *font_features_self = font_feature_settings_from_style(s);
    gboolean font_features_active =
        font_kerning_self >= 0 || font_ligatures_self != NULL ||
        font_features_self != NULL;
    gsize features_start = ctx->out->len;
    const char *font_variations_self = font_variation_settings_from_style(s);
    gsize variations_start = ctx->out->len;

    gboolean is_q = strcmp(n->name, "q") == 0;
    if (is_q) {
        char *open_q = quotes_string_for(s, ctx->q_depth, FALSE);
        g_string_append(ctx->out, open_q);
        g_free(open_q);
        ctx->q_depth++;
    }

    gboolean bidi_override = FALSE;
    gboolean bidi_isolate = FALSE;
    gboolean bidi_plaintext = FALSE;
    if (s) {
        const char *ub = ns_style_keyword(s, NS_CSS_UNICODE_BIDI);
        if (ub) {
            if (strcmp(ub, "bidi-override") == 0)
                bidi_override = TRUE;
            else if (strcmp(ub, "isolate-override") == 0)
                bidi_override = bidi_isolate = TRUE;
            else if (strcmp(ub, "isolate") == 0)
                bidi_isolate = TRUE;
            else if (strcmp(ub, "plaintext") == 0)
                bidi_plaintext = TRUE;
        }
    }
    if (bidi_override || bidi_isolate || bidi_plaintext) {
        const char *bd_dir = ns_element_get_attr(n, "dir");
        if (!bd_dir || !*bd_dir) bd_dir = ns_style_keyword(s, NS_CSS_DIRECTION);
        gboolean rtl = bd_dir && g_ascii_strcasecmp(bd_dir, "rtl") == 0;
        gboolean dir_auto = (bd_dir && g_ascii_strcasecmp(bd_dir, "auto") == 0) ||
                            (strcmp(n->name, "bdi") == 0 &&
                             !ns_element_get_attr(n, "dir"));
        if (bidi_isolate || bidi_plaintext)
            g_string_append(ctx->out,
                (bidi_plaintext || dir_auto) ? "\xe2\x81\xa8"   /* FSI */
                : rtl                        ? "\xe2\x81\xa7"   /* RLI */
                                             : "\xe2\x81\xa6"); /* LRI */
        if (bidi_override)
            g_string_append(ctx->out, rtl ? "\xe2\x80\xae"   /* RLO */
                                           : "\xe2\x80\xad"); /* LRO */
    }

    gboolean sup = strcmp(n->name, "sup") == 0;
    gboolean sub = strcmp(n->name, "sub") == 0;
    gsize rise_start = ctx->out->len;
    gboolean small_caps = s && keyword_is(s->values[NS_CSS_FONT_VARIANT],
                                          "small-caps");
    gsize sc_start = ctx->out->len;

    double font_size_self = 0;
    if (s && s->values[NS_CSS_FONT_SIZE]) {
        const ns_css_value *fv = s->values[NS_CSS_FONT_SIZE];
        if (fv->kind == NS_CSS_V_LENGTH && fv->u.length.unit == NS_CSS_UNIT_PX)
            font_size_self = fv->u.length.v;
    }
    gsize fs_start = ctx->out->len;
    gboolean fs_active = font_size_self > 0;

    gsize color_start = ctx->out->len;
    gboolean color_active = FALSE;
    guint8 cr = 0, cg = 0, cb = 0, ca = 0;
    if (s && s->values[NS_CSS_COLOR] &&
        s->values[NS_CSS_COLOR]->kind == NS_CSS_V_COLOR) {
        cr = s->values[NS_CSS_COLOR]->u.color.r;
        cg = s->values[NS_CSS_COLOR]->u.color.g;
        cb = s->values[NS_CSS_COLOR]->u.color.b;
        ca = s->values[NS_CSS_COLOR]->u.color.a;
        color_active = TRUE;
    }
    {
        const char *vis = s ? ns_style_keyword(s, NS_CSS_VISIBILITY) : NULL;
        if (vis && (strcmp(vis, "hidden") == 0 || strcmp(vis, "collapse") == 0)) {
            ca = 0;
            color_active = TRUE;
        }
    }

    gsize bg_start = ctx->out->len;
    gboolean bg_active = FALSE;
    guint8 bgr = 0, bgg = 0, bgb = 0, bga = 0;
    if (s && s->values[NS_CSS_BACKGROUND_COLOR] &&
        s->values[NS_CSS_BACKGROUND_COLOR]->kind == NS_CSS_V_COLOR) {
        bgr = s->values[NS_CSS_BACKGROUND_COLOR]->u.color.r;
        bgg = s->values[NS_CSS_BACKGROUND_COLOR]->u.color.g;
        bgb = s->values[NS_CSS_BACKGROUND_COLOR]->u.color.b;
        bga = s->values[NS_CSS_BACKGROUND_COLOR]->u.color.a;
        if (bga > 0) bg_active = TRUE;
    }

    gsize family_start = ctx->out->len;
    const char *family_str = NULL;
    if (s && s->values[NS_CSS_FONT_FAMILY] &&
        s->values[NS_CSS_FONT_FAMILY]->kind == NS_CSS_V_KEYWORD)
        family_str = s->values[NS_CSS_FONT_FAMILY]->u.keyword;

    const char *prev_text_transform = ctx->text_transform;
    if (s && s->values[NS_CSS_TEXT_TRANSFORM] &&
        s->values[NS_CSS_TEXT_TRANSFORM]->kind == NS_CSS_V_KEYWORD) {
        const char *kw = s->values[NS_CSS_TEXT_TRANSFORM]->u.keyword;
        if (strcmp(kw, "none") == 0)
            ctx->text_transform = NULL;
        else if (strcmp(kw, "uppercase") == 0 ||
                 strcmp(kw, "lowercase") == 0 ||
                 strcmp(kw, "capitalize") == 0)
            ctx->text_transform = kw;
    }

    if (s && s->before && s->before->values[NS_CSS_CONTENT])
        append_pseudo_content(ctx->out, s->before->values[NS_CSS_CONTENT], n,
                              s->before);

    for (const ns_node *c = n->first_child; c; c = c->next_sibling)
        collect_walk(c, ctx, depth + 1);

    if (s && s->after && s->after->values[NS_CSS_CONTENT])
        append_pseudo_content(ctx->out, s->after->values[NS_CSS_CONTENT], n,
                              s->after);

    ctx->text_transform = prev_text_transform;

    if (fs_active && ctx->out->len > fs_start)
        emit_font_size_attr(ctx->attrs, fs_start, ctx->out->len, font_size_self);
    if (color_active && ctx->out->len > color_start)
        emit_color_attr(ctx->attrs, color_start, ctx->out->len, cr, cg, cb, ca);
    if (bg_active && ctx->out->len > bg_start)
        emit_bg_color_attr(ctx->attrs, bg_start, ctx->out->len, bgr, bgg, bgb, bga);
    if (family_str && ctx->out->len > family_start)
        emit_font_family_attr(ctx->attrs, family_start, ctx->out->len, family_str);
    if (font_weight_active && ctx->out->len > weight_start)
        emit_font_weight_attr(ctx->attrs, weight_start, ctx->out->len, font_weight_self);
    if (font_stretch_active && ctx->out->len > stretch_start)
        emit_font_stretch_attr(ctx->attrs, stretch_start, ctx->out->len,
                               font_stretch_self);
    if (font_features_active && ctx->out->len > features_start)
        emit_font_features_attr(ctx->attrs, features_start, ctx->out->len,
                                font_kerning_self, font_ligatures_self,
                                font_features_self);
    if (font_variations_self && ctx->out->len > variations_start)
        emit_font_variations_attr(ctx->attrs, variations_start, ctx->out->len,
                                  font_variations_self);

    if (bold && --ctx->bold_depth == 0)
        emit_attr(ctx->attrs, NS_INLINE_BOLD, ctx->bold_start, ctx->out->len);
    if (italic && --ctx->italic_depth == 0)
        emit_attr(ctx->attrs, NS_INLINE_ITALIC, ctx->italic_start, ctx->out->len);
    if (mono && --ctx->mono_depth == 0)
        emit_attr(ctx->attrs, NS_INLINE_MONOSPACE, ctx->mono_start, ctx->out->len);
    if (uline && --ctx->underline_depth == 0)
        emit_attr_styled(ctx->attrs, NS_INLINE_UNDERLINE, ctx->underline_start,
                         ctx->out->len, decor_style);
    if (oline && --ctx->overline_depth == 0)
        emit_attr_styled(ctx->attrs, NS_INLINE_OVERLINE, ctx->overline_start,
                         ctx->out->len, decor_style);
    if (strike && --ctx->strike_depth == 0)
        emit_attr_styled(ctx->attrs, NS_INLINE_STRIKETHROUGH, ctx->strike_start,
                         ctx->out->len, decor_style);
    if (sup && ctx->out->len > rise_start)
        emit_attr(ctx->attrs, NS_INLINE_SUPERSCRIPT, rise_start, ctx->out->len);
    if (sub && ctx->out->len > rise_start)
        emit_attr(ctx->attrs, NS_INLINE_SUBSCRIPT, rise_start, ctx->out->len);
    if (small_caps && ctx->out->len > sc_start)
        emit_attr(ctx->attrs, NS_INLINE_SMALL_CAPS, sc_start, ctx->out->len);
    if (bidi_override)
        g_string_append(ctx->out, "\xe2\x80\xac");   /* PDF */
    if (bidi_isolate || bidi_plaintext)
        g_string_append(ctx->out, "\xe2\x81\xa9");   /* PDI */
    if (is_q) {
        ctx->q_depth--;
        char *close_q = quotes_string_for(s, ctx->q_depth, TRUE);
        g_string_append(ctx->out, close_q);
        g_free(close_q);
    }
    append_inline_spacer(ctx, pr);
    if (ctx->out->len > elem_start) {
        ns_inline_attr elem = {
            .kind = NS_INLINE_ELEMENT,
            .start = elem_start,
            .len = ctx->out->len - elem_start,
            .dom = n,
        };
        g_array_append_val(ctx->attrs, elem);
    }
    append_inline_spacer(ctx, mr);
    ctx->active_href   = prev_href;
    ctx->active_target = prev_target;
    ctx->active_link_node = prev_link_node;
}

enum { NS_WS_COLLAPSE = 0, NS_WS_PRESERVE = 1, NS_WS_PRE_LINE = 2 };

static int
white_space_mode(const ns_node *node, GHashTable *styles)
{
    for (const ns_node *p = node; p; p = p->parent) {
        if (p->kind != NS_NODE_ELEMENT) continue;
        const ns_style *ps = g_hash_table_lookup(styles, p);
        if (ps) {
            const ns_css_value *ws = ps->values[NS_CSS_WHITE_SPACE];
            if (ws && ws->kind == NS_CSS_V_KEYWORD && ws->u.keyword) {
                const char *kw = ws->u.keyword;
                if (strcmp(kw, "pre-line") == 0)
                    return NS_WS_PRE_LINE;
                if (strcmp(kw, "pre") == 0 ||
                    strcmp(kw, "pre-wrap") == 0 ||
                    strcmp(kw, "break-spaces") == 0)
                    return NS_WS_PRESERVE;
                if (strcmp(kw, "normal") == 0 || strcmp(kw, "nowrap") == 0)
                    return NS_WS_COLLAPSE;
            }
        }
        if (p->name && (strcmp(p->name, "pre") == 0 ||
                        strcmp(p->name, "textarea") == 0))
            return NS_WS_PRESERVE;
    }
    return NS_WS_COLLAPSE;
}

static ns_box *
build_inline_run_impl(const ns_node *first, const ns_node *last_excl,
                      GHashTable *styles, gboolean abs_placeholders)
{
    GString *buf = g_string_new(NULL);
    GArray  *raw_links = g_array_new(FALSE, FALSE, sizeof(ns_link_range));
    GArray  *raw_attrs = g_array_new(FALSE, FALSE, sizeof(ns_inline_attr));
    GArray  *raw_atomics = g_array_new(FALSE, FALSE, sizeof(ns_atomic_raw));
    g_array_set_clear_func(raw_links, link_clear);
    collector_ctx ctx = {
        .styles = styles, .out = buf, .links = raw_links, .attrs = raw_attrs,
        .atomics = raw_atomics, .abs_placeholders = abs_placeholders,
    };
    if (first && first->parent) {
        const ns_style *ps = g_hash_table_lookup(styles, first->parent);
        if (ps && ps->values[NS_CSS_TEXT_TRANSFORM] &&
            ps->values[NS_CSS_TEXT_TRANSFORM]->kind == NS_CSS_V_KEYWORD) {
            const char *kw = ps->values[NS_CSS_TEXT_TRANSFORM]->u.keyword;
            if (kw && strcmp(kw, "none") != 0) ctx.text_transform = kw;
        }
    }
    if (first && first->parent && first->parent->kind == NS_NODE_ELEMENT &&
        first == first->parent->first_child) {
        const ns_style *li_style = g_hash_table_lookup(styles, first->parent);
        if (ns_paint_li_is_inside(li_style)) {
            char marker[64];
            if (ns_paint_li_marker_text(first->parent, li_style,
                                        marker, sizeof marker))
                g_string_append(buf, marker);
        }
    }
    for (const ns_node *n = first; n && n != last_excl; n = n->next_sibling)
        collect_walk(n, &ctx, 0);

    int ws_mode = first ? white_space_mode(first, styles) : NS_WS_COLLAPSE;
    gboolean preformatted = ws_mode == NS_WS_PRESERVE;

    GString *collapsed = g_string_new(NULL);
    gsize   *map = g_new(gsize, buf->len + 1);
    gboolean prev_ws = ws_mode != NS_WS_PRESERVE;
    for (gsize i = 0; i < buf->len; i++) {
        char c = buf->str[i];
        if (ws_mode == NS_WS_PRESERVE) {
            map[i] = collapsed->len;
            g_string_append_c(collapsed, c);
            continue;
        }
        if (ws_mode == NS_WS_PRE_LINE && c == '\n') {
            map[i] = collapsed->len;
            g_string_append_c(collapsed, '\n');
            prev_ws = TRUE;
            continue;
        }
        gboolean ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f');
        if (ws) {
            if (!prev_ws) {
                map[i] = collapsed->len;
                g_string_append_c(collapsed, ' ');
            } else {
                map[i] = collapsed->len;
            }
            prev_ws = TRUE;
        } else {
            map[i] = collapsed->len;
            g_string_append_c(collapsed, c);
            prev_ws = FALSE;
        }
    }
    map[buf->len] = collapsed->len;

    ns_box *box = box_new_inline();
    if (!preformatted && collapsed->len > 0 &&
        collapsed->str[collapsed->len - 1] == ' ')
        g_string_set_size(collapsed, collapsed->len - 1);

    for (guint i = 0; i < raw_links->len; i++) {
        ns_link_range *r = &g_array_index(raw_links, ns_link_range, i);
        if (r->start > buf->len) r->start = buf->len;
        gsize end = r->start + r->len;
        if (end > buf->len) end = buf->len;
        gsize ns = map[r->start];
        gsize ne = map[end];
        if (ne > collapsed->len) ne = collapsed->len;
        if (ne <= ns) continue;
        ns_link_range out = {
            .start = ns,
            .len = ne - ns,
            .href = g_strdup(r->href),
            .target = r->target ? g_strdup(r->target) : NULL,
            .dom = r->dom,
        };
        g_array_append_val(inline_links_ensure(box), out);
    }

    for (guint i = 0; i < raw_attrs->len; i++) {
        ns_inline_attr *a = &g_array_index(raw_attrs, ns_inline_attr, i);
        if (a->start > buf->len) a->start = buf->len;
        gsize end = a->start + a->len;
        if (end > buf->len) end = buf->len;
        gsize ns = map[a->start];
        gsize ne = map[end];
        if (ne > collapsed->len) ne = collapsed->len;
        if (ne <= ns) continue;
        ns_inline_attr out = *a;
        out.start = ns;
        out.len = ne - ns;
        g_array_append_val(box->attrs, out);
        if (out.bg_image_src) {
            ns_box_media *m = ns_box_media_ensure(box);
            if (!m->bg_image_src) {
                m->bg_image_src = g_strdup(out.bg_image_src);
                m->bg_image = out.bg_image;
            }
        }
    }

    if (first && first->parent && collapsed->len > 0) {
        const ns_style *ps = g_hash_table_lookup(styles, first->parent);
        if (ps && ps->values[NS_CSS_TEXT_DECORATION] &&
            ps->values[NS_CSS_TEXT_DECORATION]->kind == NS_CSS_V_KEYWORD &&
            ps->values[NS_CSS_TEXT_DECORATION]->u.keyword) {
            const char *kw = ps->values[NS_CSS_TEXT_DECORATION]->u.keyword;
            const ns_css_value *pc = ps->values[NS_CSS_TEXT_DECORATION_COLOR];
            gboolean invisible = pc && pc->kind == NS_CSS_V_COLOR &&
                                 pc->u.color.a == 0;
            if (!strstr(kw, "none") && !invisible) {
                if (strstr(kw, "underline")) {
                    ns_inline_attr a = {
                        .kind = NS_INLINE_UNDERLINE, .style = ps,
                        .start = 0, .len = collapsed->len
                    };
                    g_array_append_val(box->attrs, a);
                }
                if (strstr(kw, "overline")) {
                    ns_inline_attr a = {
                        .kind = NS_INLINE_OVERLINE, .style = ps,
                        .start = 0, .len = collapsed->len
                    };
                    g_array_append_val(box->attrs, a);
                }
                if (strstr(kw, "line-through")) {
                    ns_inline_attr a = {
                        .kind = NS_INLINE_STRIKETHROUGH, .style = ps,
                        .start = 0, .len = collapsed->len
                    };
                    g_array_append_val(box->attrs, a);
                }
            }
        }
    }

    if (first && first->parent && collapsed->len > 0 &&
        first == first->parent->first_child) {
        const ns_style *ps = g_hash_table_lookup(styles, first->parent);
        if (ps && ps->first_letter) {
            const ns_style *fl = ps->first_letter;
            const char *txt = collapsed->str;
            gsize i = 0;
            while (i < collapsed->len &&
                   (txt[i] == ' ' || txt[i] == '\t' || txt[i] == '\n'))
                i++;
            while (i < collapsed->len) {
                gunichar ch = g_utf8_get_char(txt + i);
                if (g_unichar_isalnum(ch)) break;
                i = (gsize)(g_utf8_next_char(txt + i) - txt);
            }
            if (i < collapsed->len) {
                gsize fl_start = i;
                gsize fl_end = (gsize)(g_utf8_next_char(txt + i) - txt);
                if (fl_end > collapsed->len) fl_end = collapsed->len;
                gsize fl_len = fl_end - fl_start;
                if (fl->values[NS_CSS_FONT_SIZE] &&
                    fl->values[NS_CSS_FONT_SIZE]->kind == NS_CSS_V_LENGTH &&
                    fl->values[NS_CSS_FONT_SIZE]->u.length.unit == NS_CSS_UNIT_PX) {
                    ns_inline_attr a = {
                        .kind = NS_INLINE_FONT_SIZE,
                        .start = fl_start, .len = fl_len,
                        .font_size_px = fl->values[NS_CSS_FONT_SIZE]->u.length.v,
                    };
                    g_array_append_val(box->attrs, a);
                }
                if (fl->values[NS_CSS_COLOR] &&
                    fl->values[NS_CSS_COLOR]->kind == NS_CSS_V_COLOR) {
                    ns_inline_attr a = {
                        .kind = NS_INLINE_COLOR,
                        .start = fl_start, .len = fl_len,
                        .r = fl->values[NS_CSS_COLOR]->u.color.r,
                        .g = fl->values[NS_CSS_COLOR]->u.color.g,
                        .b = fl->values[NS_CSS_COLOR]->u.color.b,
                        .a = fl->values[NS_CSS_COLOR]->u.color.a,
                    };
                    g_array_append_val(box->attrs, a);
                }
                if (fl->values[NS_CSS_BACKGROUND_COLOR] &&
                    fl->values[NS_CSS_BACKGROUND_COLOR]->kind == NS_CSS_V_COLOR) {
                    ns_inline_attr a = {
                        .kind = NS_INLINE_BG_COLOR,
                        .start = fl_start, .len = fl_len,
                        .r = fl->values[NS_CSS_BACKGROUND_COLOR]->u.color.r,
                        .g = fl->values[NS_CSS_BACKGROUND_COLOR]->u.color.g,
                        .b = fl->values[NS_CSS_BACKGROUND_COLOR]->u.color.b,
                        .a = fl->values[NS_CSS_BACKGROUND_COLOR]->u.color.a,
                    };
                    g_array_append_val(box->attrs, a);
                }
                const ns_css_value *fw = fl->values[NS_CSS_FONT_WEIGHT];
                int font_weight = ns_css_font_weight_number(fw, -1);
                if (font_weight > 0) {
                    ns_inline_attr a = {
                        .kind = NS_INLINE_FONT_WEIGHT,
                        .start = fl_start, .len = fl_len,
                        .font_weight = font_weight,
                    };
                    g_array_append_val(box->attrs, a);
                }
                if (keyword_is(fl->values[NS_CSS_FONT_STYLE], "italic") ||
                    keyword_is(fl->values[NS_CSS_FONT_STYLE], "oblique")) {
                    ns_inline_attr a = {
                        .kind = NS_INLINE_ITALIC,
                        .start = fl_start, .len = fl_len,
                    };
                    g_array_append_val(box->attrs, a);
                }
                if (fl->values[NS_CSS_FONT_FAMILY] &&
                    fl->values[NS_CSS_FONT_FAMILY]->kind == NS_CSS_V_KEYWORD) {
                    ns_inline_attr a = {
                        .kind = NS_INLINE_FONT_FAMILY,
                        .start = fl_start, .len = fl_len,
                        .family = fl->values[NS_CSS_FONT_FAMILY]->u.keyword,
                    };
                    g_array_append_val(box->attrs, a);
                }
                int fk = font_kerning_int_from_style(fl);
                const char *flig = font_ligatures_from_style(fl);
                const char *ffea = font_feature_settings_from_style(fl);
                if (fk >= 0 || flig || ffea) {
                    ns_inline_attr a = {
                        .kind = NS_INLINE_FONT_FEATURES,
                        .start = fl_start, .len = fl_len,
                        .font_kerning = fk,
                        .font_ligatures = flig,
                        .font_features = ffea,
                    };
                    g_array_append_val(box->attrs, a);
                }
                const char *fvar = font_variation_settings_from_style(fl);
                if (fvar) {
                    ns_inline_attr a = {
                        .kind = NS_INLINE_FONT_VARIATIONS,
                        .start = fl_start, .len = fl_len,
                        .font_variations = fvar,
                    };
                    g_array_append_val(box->attrs, a);
                }
            }
        }
    }

    for (guint i = 0; i < raw_atomics->len; i++) {
        ns_atomic_raw *rr = &g_array_index(raw_atomics, ns_atomic_raw, i);
        gsize start = rr->start;
        if (start > buf->len) start = buf->len;
        gsize ns = map[start];
        if (ns > collapsed->len) ns = collapsed->len;
        if (!box->inline_atomics)
            box->inline_atomics = g_array_new(FALSE, FALSE, sizeof(ns_inline_atomic));
        ns_inline_atomic ia = { .byte_off = ns, .box = rr->box };
        if (rr->box && !rr->box->parent) rr->box->parent = box;
        g_array_append_val(box->inline_atomics, ia);
    }

    g_free(map);
    g_array_free(raw_links, TRUE);
    g_array_free(raw_attrs, TRUE);
    g_array_free(raw_atomics, TRUE);
    g_string_free(buf, TRUE);

    box->text = g_string_free(collapsed, FALSE);
    return box;
}

static ns_box *
build_inline_run(const ns_node *first, const ns_node *last_excl, GHashTable *styles)
{
    return build_inline_run_impl(first, last_excl, styles, TRUE);
}

static ns_box *
build_inline_run_no_abs_placeholders(const ns_node *first,
                                     const ns_node *last_excl,
                                     GHashTable *styles)
{
    return build_inline_run_impl(first, last_excl, styles, FALSE);
}

static ns_box *
build_form_control_block(const ns_node *n, const ns_style *s, GHashTable *styles)
{
    ns_box *block = box_new(NS_BOX_BLOCK);
    block->dom = n;
    block->style = s;
    collect_box_bg_image(block, s);
    const ns_node *prev_fc = g_form_control_inline;
    g_form_control_inline = n;
    ns_box *run = build_inline_run(n, n->next_sibling, styles);
    g_form_control_inline = prev_fc;
    if (run && run->text && run->text[0]) {
        box_append_child(block, run);
    } else if (run) {
        ns_box_free(run);
    }
    return block;
}

typedef struct {
    char    *url;
    double   density;
    double   width;
} srcset_candidate;

static gboolean
srcset_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}

static gboolean
srcset_valid_integer(const char *s, gsize n)
{
    if (n == 0) return FALSE;
    for (gsize i = 0; i < n; i++)
        if (!g_ascii_isdigit(s[i])) return FALSE;
    return TRUE;
}

static gboolean
srcset_valid_float(const char *s, gsize n)
{
    gsize i = 0, digits = 0;
    if (i < n && s[i] == '-') i++;
    while (i < n && g_ascii_isdigit(s[i])) { i++; digits++; }
    if (i < n && s[i] == '.') {
        i++;
        gsize frac = 0;
        while (i < n && g_ascii_isdigit(s[i])) { i++; frac++; }
        if (frac == 0) return FALSE;
        digits += frac;
    }
    if (digits == 0) return FALSE;
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '-' || s[i] == '+')) i++;
        gsize exp = 0;
        while (i < n && g_ascii_isdigit(s[i])) { i++; exp++; }
        if (exp == 0) return FALSE;
    }
    return i == n;
}

static double
srcset_number(const char *s, gsize n)
{
    char *copy = g_strndup(s, n);
    double v = g_ascii_strtod(copy, NULL);
    g_free(copy);
    return v;
}

static gboolean
srcset_parse_descriptors(GPtrArray *descriptors, double *width, double *density)
{
    gboolean has_w = FALSE, has_x = FALSE, has_h = FALSE;
    for (guint i = 0; i < descriptors->len; i++) {
        const char *d = g_ptr_array_index(descriptors, i);
        gsize n = strlen(d);
        char last = n ? d[n - 1] : '\0';
        if (last == 'w') {
            if (has_w || has_x || !srcset_valid_integer(d, n - 1)) return FALSE;
            double v = srcset_number(d, n - 1);
            if (v <= 0) return FALSE;
            has_w = TRUE;
            *width = v;
        } else if (last == 'x') {
            if (has_w || has_x || has_h || !srcset_valid_float(d, n - 1))
                return FALSE;
            double v = srcset_number(d, n - 1);
            if (v < 0) return FALSE;
            has_x = TRUE;
            *density = v;
        } else if (last == 'h') {
            if (has_h || has_x || !srcset_valid_integer(d, n - 1)) return FALSE;
            if (srcset_number(d, n - 1) <= 0) return FALSE;
            has_h = TRUE;
        } else {
            return FALSE;
        }
    }
    return !has_h || has_w;
}

typedef enum {
    SRCSET_IN_DESCRIPTOR,
    SRCSET_IN_PARENS,
    SRCSET_AFTER_DESCRIPTOR,
} srcset_state;

static const char *
srcset_tokenize_descriptors(const char *p, GPtrArray *descriptors)
{
    while (srcset_is_space(*p)) p++;
    GString *cur = g_string_new(NULL);
    srcset_state state = SRCSET_IN_DESCRIPTOR;
    for (;; p++) {
        char c = *p;
        if (state == SRCSET_IN_DESCRIPTOR) {
            if (srcset_is_space(c)) {
                if (cur->len) {
                    g_ptr_array_add(descriptors, g_strdup(cur->str));
                    g_string_truncate(cur, 0);
                    state = SRCSET_AFTER_DESCRIPTOR;
                }
            } else if (c == ',') {
                p++;
                break;
            } else if (c == '\0') {
                break;
            } else {
                g_string_append_c(cur, c);
                if (c == '(') state = SRCSET_IN_PARENS;
            }
        } else if (state == SRCSET_IN_PARENS) {
            if (c == '\0') break;
            g_string_append_c(cur, c);
            if (c == ')') state = SRCSET_IN_DESCRIPTOR;
        } else {
            if (c == '\0') break;
            if (!srcset_is_space(c)) {
                state = SRCSET_IN_DESCRIPTOR;
                p--;
            }
        }
    }
    if (cur->len) g_ptr_array_add(descriptors, g_strdup(cur->str));
    g_string_free(cur, TRUE);
    return p;
}

static GArray *
srcset_parse(const char *input)
{
    GArray *out = g_array_new(FALSE, FALSE, sizeof(srcset_candidate));
    const char *p = input ? input : "";
    for (;;) {
        while (srcset_is_space(*p) || *p == ',') p++;
        if (!*p) break;
        const char *url_s = p;
        while (*p && !srcset_is_space(*p)) p++;
        gsize url_len = (gsize)(p - url_s);
        GPtrArray *descriptors = g_ptr_array_new_with_free_func(g_free);
        if (url_s[url_len - 1] == ',') {
            while (url_len > 0 && url_s[url_len - 1] == ',') url_len--;
        } else {
            p = srcset_tokenize_descriptors(p, descriptors);
        }
        double width = -1, density = -1;
        if (url_len > 0 &&
            srcset_parse_descriptors(descriptors, &width, &density)) {
            srcset_candidate c = { g_strndup(url_s, url_len), density, width };
            g_array_append_val(out, c);
        }
        g_ptr_array_free(descriptors, TRUE);
    }
    return out;
}

static void
srcset_candidates_free(GArray *candidates)
{
    for (guint i = 0; i < candidates->len; i++)
        g_free(g_array_index(candidates, srcset_candidate, i).url);
    g_array_free(candidates, TRUE);
}

static char *
srcset_select(const char *srcset, const char *sizes, const char *src,
              double *density)
{
    const double dpr = 1.0;
    GArray *cands = srcset_parse(srcset);
    gboolean any_width = FALSE, any_unit_density = FALSE;
    double source_size = -1;
    for (guint i = 0; i < cands->len; i++) {
        srcset_candidate *c = &g_array_index(cands, srcset_candidate, i);
        if (c->width > 0) {
            any_width = TRUE;
            if (source_size < 0) {
                source_size = ns_css_sizes_resolve(sizes);
                if (!isfinite(source_size))
                    source_size = ns_css_sizes_resolve(NULL);
            }
            c->density = source_size > 0 ? c->width / source_size : 1.0;
        } else if (c->density < 0) {
            c->density = 1.0;
        }
        if (c->width <= 0 && c->density == 1.0) any_unit_density = TRUE;
    }
    if (src && *src && !any_width && !any_unit_density) {
        srcset_candidate c = { g_strdup(src), 1.0, -1 };
        g_array_append_val(cands, c);
    }
    const srcset_candidate *best = NULL, *largest = NULL;
    for (guint i = 0; i < cands->len; i++) {
        const srcset_candidate *c = &g_array_index(cands, srcset_candidate, i);
        gboolean duplicate = FALSE;
        for (guint j = 0; j < i && !duplicate; j++)
            duplicate = g_array_index(cands, srcset_candidate, j).density ==
                        c->density;
        if (duplicate) continue;
        if (c->density >= dpr && (!best || c->density < best->density))
            best = c;
        if (!largest || c->density > largest->density) largest = c;
    }
    if (!best) best = largest;
    char *url = best ? g_strdup(best->url) : NULL;
    if (best && density) *density = best->density;
    srcset_candidates_free(cands);
    return url;
}

static gboolean
srcset_has_width_descriptor(const char *srcset)
{
    GArray *cands = srcset_parse(srcset);
    gboolean any = FALSE;
    for (guint i = 0; i < cands->len && !any; i++)
        any = g_array_index(cands, srcset_candidate, i).width > 0;
    srcset_candidates_free(cands);
    return any;
}

static gboolean
ns_pixbuf_likely_supports(const char *mime)
{
    if (!mime || !*mime) return TRUE;
    return ns_image_supports_mime(mime);
}

static char *
pick_picture_source_url(const ns_node *picture, const ns_node *img,
                        double *density)
{
    if (!picture) return NULL;
    char *data_fallback = NULL;
    double data_density = 1.0;
    for (const ns_node *c = picture->first_child; c && c != img;
         c = c->next_sibling) {
        if (c->kind != NS_NODE_ELEMENT || !c->name) continue;
        if (strcmp(c->name, "source") != 0) continue;
        const char *type = ns_element_get_attr(c, "type");
        if (type && !ns_pixbuf_likely_supports(type)) continue;
        const char *media = ns_element_get_attr(c, "media");
        if (media && *media && !ns_css_media_query_matches(media)) continue;
        const char *sizes = ns_element_get_attr(c, "sizes");
        const char *sets[2] = { ns_element_get_attr(c, "data-srcset"),
                                ns_element_get_attr(c, "srcset") };
        for (int i = 0; i < 2; i++) {
            double d = 1.0;
            char *u = srcset_select(sets[i], sizes, NULL, &d);
            if (u && !g_str_has_prefix(u, "data:")) {
                g_free(data_fallback);
                *density = d;
                return u;
            }
            if (u && !data_fallback) {
                data_fallback = u;
                data_density = d;
            } else {
                g_free(u);
            }
        }
        const char *s = ns_element_get_attr(c, "src");
        if (s && *s) {
            if (!g_str_has_prefix(s, "data:")) {
                g_free(data_fallback);
                *density = 1.0;
                return g_strdup(s);
            }
            if (!data_fallback) {
                data_fallback = g_strdup(s);
                data_density = 1.0;
            }
        }
    }
    *density = data_density;
    return data_fallback;
}

static char *
pick_img_url(const ns_node *n, double *density)
{
    if (!n) return NULL;
    const char *src    = ns_element_get_attr(n, "src");
    const char *srcset = ns_element_get_attr(n, "srcset");
    const char *dsrc   = ns_element_get_attr(n, "data-src");
    if (!dsrc || !*dsrc) dsrc = ns_element_get_attr(n, "data-original");
    if (!dsrc || !*dsrc) dsrc = ns_element_get_attr(n, "data-lazy-src");
    const char *dsset  = ns_element_get_attr(n, "data-srcset");
    if (!dsset || !*dsset) dsset = ns_element_get_attr(n, "data-lazy-srcset");
    const char *sizes  = ns_element_get_attr(n, "sizes");

    *density = 1.0;
    char *u = srcset_select(dsset, sizes, NULL, density);
    if (u && (srcset_has_width_descriptor(dsset) || !dsrc || !*dsrc))
        return u;
    g_free(u);
    *density = 1.0;
    if (dsrc && *dsrc) return g_strdup(dsrc);

    gboolean placeholder = src && g_str_has_prefix(src, "data:");
    u = srcset_select(srcset, sizes, placeholder ? NULL : src, density);
    if (u) return u;
    *density = 1.0;
    if (src && *src) return g_strdup(src);
    return NULL;
}

static char *
choose_img_url(const ns_node *n, const ns_node **img_out, double *density)
{
    const ns_node *img = n;
    char *url = NULL;
    *density = 1.0;
    if (ns_node_is_element_named(n, "img") &&
        ns_node_is_element_named(n->parent, "picture"))
        n = n->parent;
    if (n->name && strcmp(n->name, "picture") == 0) {
        for (const ns_node *c = n->first_child; c && img == n;
             c = c->next_sibling)
            if (ns_node_is_element_named(c, "img"))
                img = c;
        double source_density = 1.0;
        char *source_url = pick_picture_source_url(n, img != n ? img : NULL,
                                                   &source_density);
        if (source_url && !g_str_has_prefix(source_url, "data:")) {
            url = source_url;
            *density = source_density;
        } else {
            if (img != n) url = pick_img_url(img, density);
            if (!url && source_url) {
                url = source_url;
                *density = source_density;
            } else {
                g_free(source_url);
            }
        }
    } else {
        url = pick_img_url(n, density);
    }
    if (img_out) *img_out = img;
    return url;
}

static double
image_dimension_attr(const ns_node *n, const char *name)
{
    const char *s = ns_element_get_attr(n, name);
    if (!s || !*s) return 0;
    char *end = NULL;
    double v = g_ascii_strtod(s, &end);
    if (end == s || !(v > 0)) return 0;
    return v;
}

static ns_image *
decode_data_image_for_layout(const char *url, const char *key)
{
    if (!url || !key || !g_image_cache_for_layout ||
        !g_str_has_prefix(url, "data:image/"))
        return NULL;
    ns_image *cached = ns_image_cache_peek(g_image_cache_for_layout, key);
    if (cached) return cached;
    GByteArray *bytes = g_byte_array_new();
    char *ctype = NULL;
    gboolean too_large = FALSE;
    gboolean ok = ns_data_url_decode(url, bytes, NS_LAYOUT_DATA_IMAGE_BUDGET,
                                     &ctype, &too_large);
    g_free(ctype);
    if (!ok || too_large || bytes->len == 0) {
        g_byte_array_free(bytes, TRUE);
        return NULL;
    }
    ns_image *img = ns_image_cache_insert_encoded(g_image_cache_for_layout, key,
                                                  bytes->data, bytes->len);
    g_byte_array_free(bytes, TRUE);
    return img;
}

char *
ns_img_chosen_url(const ns_node *n)
{
    if (!n) return NULL;
    double density = 1.0;
    return choose_img_url(n, NULL, &density);
}

double
ns_img_chosen_density(const ns_node *n)
{
    if (!n) return 1.0;
    double density = 1.0;
    g_free(choose_img_url(n, NULL, &density));
    return density > 0 ? density : 1.0;
}

static ns_box *
build_image_box(const ns_node *n)
{
    const ns_node *img = n;
    double density = 1.0;
    char *url = choose_img_url(n, &img, &density);
    ns_box *box = box_new(NS_BOX_IMAGE);
    box->dom = img;
    ns_box_media *m = ns_box_media_ensure(box);
    m->image_src = url;
    m->image_density = density > 0 ? density : 1.0;
    box->content_width = image_dimension_attr(img, "width");
    box->content_height = image_dimension_attr(img, "height");
    double file_w = image_dimension_attr(img, "data-file-width");
    double file_h = image_dimension_attr(img, "data-file-height");
    if (box->content_width <= 0 && box->content_height > 0 &&
        file_w > 0 && file_h > 0)
        box->content_width = box->content_height * file_w / file_h;
    if (box->content_height <= 0 && box->content_width > 0 &&
        file_w > 0 && file_h > 0)
        box->content_height = box->content_width * file_h / file_w;
    m->declared_image_size =
        box->content_width > 0 && box->content_height > 0;
    if (g_image_cache_for_layout) {
        char *abs = g_base_url_for_layout
            ? ns_url_resolve(g_base_url_for_layout, url)
            : NULL;
        const char *key = abs ? abs : url;
        m->image = decode_data_image_for_layout(url, key);
        if (!m->image)
            m->image = ns_image_cache_peek(g_image_cache_for_layout, key);
        g_free(abs);
    }
    if (!m->declared_image_size && url && !m->image &&
        box->content_width <= 0 && box->content_height <= 0) {
        box->content_width = 200;
        box->content_height = 150;
        m->placeholder_image_size = TRUE;
    }
    return box;
}

static const char *
first_media_source_url(const ns_node *n)
{
    const char *src = ns_element_get_attr(n, NS_MEDIA_SRC_ATTR);
    if (src && *src) return src;
    src = ns_element_get_attr(n, "src");
    if (src && *src) return src;
    src = ns_element_get_attr(n, "data-mp4");
    if (src && *src) return src;
    src = ns_element_get_attr(n, "data-webm");
    if (src && *src) return src;
    for (const ns_node *c = n->first_child; c; c = c->next_sibling) {
        if (c->kind != NS_NODE_ELEMENT || !c->name) continue;
        if (strcmp(c->name, "source") != 0) continue;
        const char *csrc = ns_element_get_attr(c, "src");
        if (csrc && *csrc) return csrc;
    }
    return NULL;
}

static ns_box *
build_audio_box(const ns_node *n)
{
    ns_box *box = box_new(NS_BOX_VIDEO);
    box->dom = n;
    ns_box_media *m = ns_box_media_ensure(box);
    const char *src = first_media_source_url(n);
    if (src) m->video_audio_src = g_strdup(src);
    if (ns_element_get_attr(n, "controls")) {
        const char *ws = ns_element_get_attr(n, "width");
        const char *hs = ns_element_get_attr(n, "height");
        box->content_width = ws ? g_ascii_strtod(ws, NULL) : 250;
        box->content_height = hs ? g_ascii_strtod(hs, NULL) : 32;
    }
    return box;
}

static ns_box *
build_video_box(const ns_node *n)
{
    const char *src = first_media_source_url(n);
    ns_box *box = box_new(NS_BOX_VIDEO);
    box->dom = n;
    ns_box_media *m = ns_box_media_ensure(box);
    if (src) m->video_src = g_strdup(src);
    const char *poster = ns_element_get_attr(n, "poster");
    const char *data_poster = ns_element_get_attr(n, "data-poster");
    const char *fallback_poster = ns_element_get_attr(n, NS_MEDIA_POSTER_ATTR);
    if ((!poster || !*poster || g_str_has_prefix(poster, "data:image/")) &&
        data_poster && *data_poster)
        poster = data_poster;
    if ((!poster || !*poster) && fallback_poster && *fallback_poster)
        poster = fallback_poster;
    if (poster && *poster) m->video_poster = g_strdup(poster);
    const char *ws = ns_element_get_attr(n, "width");
    const char *hs = ns_element_get_attr(n, "height");
    gboolean metadata = node_has_media_metadata(n);
    box->content_width  = ws ? g_ascii_strtod(ws, NULL) : (metadata ? 640 : 300);
    box->content_height = hs ? g_ascii_strtod(hs, NULL) : (metadata ? 360 : 150);
    const char *audio = ns_element_get_attr(n, "data-audio-src");
    if (audio && *audio) m->video_audio_src = g_strdup(audio);
    return box;
}

static ns_box *
build_pseudo_inline_for(const ns_style *ps, const ns_node *host)
{
    if (!ps) return NULL;
    const ns_css_value *cv = ps->values[NS_CSS_CONTENT];
    if (!cv || cv->kind != NS_CSS_V_KEYWORD || !cv->u.keyword) return NULL;
    gboolean inline_atomic = ns_display_is_atomic_inline(ns_css_display_of(ps));
    char *resolved = resolve_pseudo_content(cv->u.keyword, host);
    if (!resolved) {
        if (!inline_atomic) return NULL;
        resolved = g_strdup("");
    }
    char *quote = NULL;
    const char *txt = resolved;
    if (strcmp(txt, "open-quote") == 0)
        txt = quote = quotes_string_for(ps, 0, FALSE);
    else if (strcmp(txt, "close-quote") == 0)
        txt = quote = quotes_string_for(ps, 0, TRUE);
    else if (strcmp(txt, "no-open-quote") == 0 ||
             strcmp(txt, "no-close-quote") == 0) { g_free(resolved); return NULL; }

    if (inline_atomic) {
        ns_box *inner = box_new(NS_BOX_BLOCK);
        inner->style = ps;
        collect_box_bg_image(inner, ps);
        if (txt && *txt) {
            ns_box *txtrun = box_new_inline();
            txtrun->text = g_strdup(txt);
            txtrun->style = ps;
            box_append_child(inner, txtrun);
        }
        g_free(quote);
        g_free(resolved);
        ns_box *run = box_new_inline();
        run->style = ps;
        run->text = g_strdup("\xef\xbf\xbc");
        run->inline_atomics = g_array_new(FALSE, FALSE, sizeof(ns_inline_atomic));
        ns_inline_atomic ia = { .byte_off = 0, .box = inner };
        inner->parent = run;
        g_array_append_val(run->inline_atomics, ia);
        return run;
    }

    ns_box *box = box_new_inline();
    box->text = g_strdup(txt);
    g_free(quote);
    g_free(resolved);
    box->style = ps;

    gsize tlen = strlen(box->text);
    if (ps->values[NS_CSS_COLOR] && ps->values[NS_CSS_COLOR]->kind == NS_CSS_V_COLOR) {
        ns_inline_attr a = {
            .kind = NS_INLINE_COLOR,
            .start = 0, .len = tlen,
            .r = ps->values[NS_CSS_COLOR]->u.color.r,
            .g = ps->values[NS_CSS_COLOR]->u.color.g,
            .b = ps->values[NS_CSS_COLOR]->u.color.b,
            .a = ps->values[NS_CSS_COLOR]->u.color.a,
        };
        g_array_append_val(box->attrs, a);
    }
    if (ps->values[NS_CSS_BACKGROUND_COLOR] &&
        ps->values[NS_CSS_BACKGROUND_COLOR]->kind == NS_CSS_V_COLOR) {
        ns_inline_attr a = {
            .kind = NS_INLINE_BG_COLOR,
            .start = 0, .len = tlen,
            .r = ps->values[NS_CSS_BACKGROUND_COLOR]->u.color.r,
            .g = ps->values[NS_CSS_BACKGROUND_COLOR]->u.color.g,
            .b = ps->values[NS_CSS_BACKGROUND_COLOR]->u.color.b,
            .a = ps->values[NS_CSS_BACKGROUND_COLOR]->u.color.a,
        };
        g_array_append_val(box->attrs, a);
    }
    if (ps->values[NS_CSS_FONT_SIZE] &&
        ps->values[NS_CSS_FONT_SIZE]->kind == NS_CSS_V_LENGTH &&
        ps->values[NS_CSS_FONT_SIZE]->u.length.unit == NS_CSS_UNIT_PX) {
        ns_inline_attr a = {
            .kind = NS_INLINE_FONT_SIZE,
            .start = 0, .len = tlen,
            .font_size_px = ps->values[NS_CSS_FONT_SIZE]->u.length.v,
        };
        g_array_append_val(box->attrs, a);
    }
    const ns_css_value *fw = ps->values[NS_CSS_FONT_WEIGHT];
    int font_weight = ns_css_font_weight_number(fw, -1);
    if (font_weight > 0) {
        ns_inline_attr a = {
            .kind = NS_INLINE_FONT_WEIGHT,
            .start = 0, .len = tlen,
            .font_weight = font_weight,
        };
        g_array_append_val(box->attrs, a);
    }
    if (ps->values[NS_CSS_FONT_STRETCH]) {
        ns_inline_attr a = {
            .kind = NS_INLINE_FONT_STRETCH,
            .start = 0, .len = tlen,
            .font_stretch =
                ns_css_font_stretch_rank(ps->values[NS_CSS_FONT_STRETCH]),
        };
        g_array_append_val(box->attrs, a);
    }
    int fk = font_kerning_int_from_style(ps);
    const char *flig = font_ligatures_from_style(ps);
    const char *ffea = font_feature_settings_from_style(ps);
    if (fk >= 0 || flig || ffea) {
        ns_inline_attr a = {
            .kind = NS_INLINE_FONT_FEATURES,
            .start = 0, .len = tlen,
            .font_kerning = fk,
            .font_ligatures = flig,
            .font_features = ffea,
        };
        g_array_append_val(box->attrs, a);
    }
    const char *fvar = font_variation_settings_from_style(ps);
    if (fvar) {
        ns_inline_attr a = {
            .kind = NS_INLINE_FONT_VARIATIONS,
            .start = 0, .len = tlen,
            .font_variations = fvar,
        };
        g_array_append_val(box->attrs, a);
    }
    if (keyword_is(ps->values[NS_CSS_FONT_STYLE], "italic") ||
        keyword_is(ps->values[NS_CSS_FONT_STYLE], "oblique")) {
        ns_inline_attr a = { .kind = NS_INLINE_ITALIC, .start = 0, .len = tlen };
        g_array_append_val(box->attrs, a);
    }
    return box;
}

static int g_build_block_depth;

static const ns_node *
layout_shadow_root(const ns_node *host)
{
    if (!host || host->kind != NS_NODE_ELEMENT) return NULL;
    for (const ns_node *c = host->first_child; c; c = c->next_sibling)
        if (c->kind == NS_NODE_ELEMENT && ns_element_get_attr(c, NS_SHADOW_ATTR))
            return c;
    return NULL;
}

static const ns_node *
layout_slot_host(const ns_node *slot)
{
    for (const ns_node *p = slot ? slot->parent : NULL; p; p = p->parent)
        if (p->kind == NS_NODE_ELEMENT && ns_element_get_attr(p, NS_SHADOW_ATTR))
            return p->parent;
    return NULL;
}

static ns_box *build_block_impl(const ns_node *n, GHashTable *styles);

static ns_box *
build_block(const ns_node *n, GHashTable *styles)
{
    if (!n || g_build_block_depth >= NS_LAYOUT_MAX_DEPTH) return NULL;
    g_build_block_depth++;
    ns_box *out = build_block_impl(n, styles);
    g_build_block_depth--;
    return out;
}

static int g_contents_depth;

static void
append_display_contents_children(ns_box *block, const ns_node *n,
                                 GHashTable *styles,
                                 gboolean blockify_children,
                                 ns_box **pending_before)
{
    if (g_contents_depth >= NS_LAYOUT_MAX_DEPTH) return;
    g_contents_depth++;
    const ns_style *s = g_hash_table_lookup(styles, n);
    ns_box *contents_before = (s && s->before &&
                               !style_is_absolute_or_fixed(s->before))
        ? build_pseudo_inline_for(s->before, n) : NULL;
    if (pending_before && *pending_before) {
        contents_before = inline_merge_prefix(*pending_before, contents_before);
        *pending_before = NULL;
    }
    if (contents_before)
        box_append_child(block, contents_before);

    const ns_node *c = n->first_child;
    while (c) {
        if (c->kind == NS_NODE_ELEMENT && c->name &&
            tag_is_non_rendering(c->name)) {
            c = c->next_sibling;
            continue;
        }
        if (blockify_children) {
            if (c->kind == NS_NODE_TEXT) {
                if (text_is_ws_only(c->text)) { c = c->next_sibling; continue; }
                ns_box *item = box_new(NS_BOX_BLOCK);
                item->style = NULL;
                ns_box *run = build_inline_run(c, c->next_sibling, styles);
                if (run && run->text && run->text[0]) {
                    box_append_child(item, run);
                    box_append_child(block, item);
                } else {
                    if (run) ns_box_free(run);
                    ns_box_free(item);
                }
                c = c->next_sibling;
                continue;
            }
            if (c->kind != NS_NODE_ELEMENT) { c = c->next_sibling; continue; }
            const ns_style *cs = g_hash_table_lookup(styles, c);
            if (cs && style_is_none(cs)) { c = c->next_sibling; continue; }
            if (style_is_contents(cs)) {
                append_display_contents_children(block, c, styles, TRUE, NULL);
                c = c->next_sibling;
                continue;
            }
            if (cs && style_is_absolute_or_fixed(cs)) {
                ns_box *child = build_block(c, styles);
                if (child) box_append_child(block, child);
                c = c->next_sibling;
                continue;
            }
            if (style_is_block(cs) ||
                contains_block_media(c, styles) ||
                node_has_media_metadata(c) ||
                (c->name && (strcmp(c->name, "img") == 0 ||
                             strcmp(c->name, "picture") == 0 ||
                             strcmp(c->name, "svg") == 0 ||
                             strcmp(c->name, "audio") == 0 ||
                             strcmp(c->name, "video") == 0 ||
                             strcmp(c->name, "table") == 0))) {
                ns_box *child = build_block(c, styles);
                if (child) box_append_child(block, child);
                c = c->next_sibling;
                continue;
            }
            ns_box *item = build_blockified_inline_item(c, styles, pending_before);
            if (item) box_append_child(block, item);
            c = c->next_sibling;
            continue;
        }

        if (c->kind == NS_NODE_ELEMENT) {
            const ns_style *cs = g_hash_table_lookup(styles, c);
            if (cs && style_is_none(cs)) { c = c->next_sibling; continue; }
            if (style_is_contents(cs)) {
                append_display_contents_children(block, c, styles, FALSE, NULL);
                c = c->next_sibling;
                continue;
            }
        }
        if (is_inline_dom(c, styles)) {
            const ns_node *start = c;
            c = c->next_sibling;
            while (c) {
                if (c->kind == NS_NODE_ELEMENT && c->name &&
                    tag_is_non_rendering(c->name)) {
                    c = c->next_sibling;
                    continue;
                }
                if (c->kind == NS_NODE_ELEMENT) {
                    const ns_style *cs = g_hash_table_lookup(styles, c);
                    if (style_is_contents(cs)) break;
                }
                if (!continues_inline_run(c, styles)) break;
                c = c->next_sibling;
            }
            ns_box *run = build_inline_run(start, c, styles);
            if (run && run->text && run->text[0])
                box_append_child(block, run);
            else if (run)
                ns_box_free(run);
        } else {
            ns_box *child = build_block(c, styles);
            if (child) box_append_child(block, child);
            if (c) c = c->next_sibling;
        }
    }

    if (s && s->after && !style_is_absolute_or_fixed(s->after)) {
        ns_box *contents_after = build_pseudo_inline_for(s->after, n);
        if (contents_after) box_append_child(block, contents_after);
    }
    g_contents_depth--;
}

static ns_box *
build_pseudo_block_for(const ns_style *ps, const ns_node *host)
{
    if (!ps) return NULL;
    if (style_is_absolute_or_fixed(ps)) return NULL;
    const ns_css_value *cv = ps->values[NS_CSS_CONTENT];
    if (!cv || cv->kind != NS_CSS_V_KEYWORD || !cv->u.keyword) return NULL;
    if (*cv->u.keyword) {
        char *resolved = resolve_pseudo_content(cv->u.keyword, host);
        gboolean empty = resolved && !*resolved;
        g_free(resolved);
        if (!empty) return NULL;
    }
    ns_display d = ns_css_display_of(ps);
    if (!ps->values[NS_CSS_DISPLAY] || ns_display_is_none(d) ||
        d.outer == NS_DISPLAY_OUTER_INLINE)
        return NULL;
    ns_box *pb = box_new(NS_BOX_BLOCK);
    pb->style = ps;
    collect_box_bg_image(pb, ps);
    return pb;
}

static void
register_abs_pseudo(const ns_node *host, const ns_style *ps)
{
    if (!ps || !g_abs_pending) return;
    if (!style_is_absolute_or_fixed(ps)) return;
    const ns_css_value *cv = ps->values[NS_CSS_CONTENT];
    if (!cv || cv->kind != NS_CSS_V_KEYWORD || !cv->u.keyword) return;
    ns_abs_entry e;
    e.dom = host;
    e.pseudo = ps;
    const ns_css_value *pv = ps->values[NS_CSS_POSITION];
    e.fixed = pv && pv->kind == NS_CSS_V_KEYWORD && pv->u.keyword &&
              strcmp(pv->u.keyword, "fixed") == 0;
    g_array_append_val(g_abs_pending, e);
}

static ns_box *
build_blockified_inline_item(const ns_node *n, GHashTable *styles,
                             ns_box **pending_before)
{
    const ns_style *s = g_hash_table_lookup(styles, n);
    ns_box *item = box_new(NS_BOX_BLOCK);
    item->dom = n;
    item->style = s;
    collect_box_bg_image(item, s);

    if (s) {
        register_abs_pseudo(n, s->before);
        register_abs_pseudo(n, s->after);
    }

    ns_box *before_block = (s && s->before)
        ? build_pseudo_block_for(s->before, n) : NULL;
    if (before_block) box_append_child(item, before_block);

    ns_box *run = build_inline_run_no_abs_placeholders(n, n->next_sibling,
                                                       styles);
    if (pending_before && *pending_before) {
        run = inline_merge_prefix(*pending_before, run);
        *pending_before = NULL;
    }
    if (run && run->text && run->text[0]) {
        box_append_child(item, run);
    } else if (run) {
        ns_box_free(run);
    }

    ns_box *after_block = (s && s->after)
        ? build_pseudo_block_for(s->after, n) : NULL;
    if (after_block) box_append_child(item, after_block);

    if (!item->first_child && !style_has_atomic_inline_box(s)) {
        ns_box_free(item);
        return NULL;
    }
    return item;
}

static const ns_node *g_blockified_legend;
static int float_side_of(const ns_style *s);

static gboolean
style_can_be_rendered_legend(const ns_style *s)
{
    return s && !style_is_none(s) && float_side_of(s) < 0 &&
           !style_is_absolute_or_fixed(s);
}

static const ns_node *
fieldset_rendered_legend_node(const ns_node *n, GHashTable *styles)
{
    if (!ns_node_is_element_named(n, "fieldset")) return NULL;
    for (const ns_node *c = n->first_child; c; c = c->next_sibling) {
        if (!ns_node_is_element_named(c, "legend")) continue;
        if (style_can_be_rendered_legend(g_hash_table_lookup(styles, c)))
            return c;
    }
    return NULL;
}

static ns_box *
build_rendered_legend(const ns_node *n, GHashTable *styles)
{
    const ns_node *saved = g_blockified_legend;
    g_blockified_legend = n;
    ns_box *legend = build_block(n, styles);
    g_blockified_legend = saved;
    if (!legend) {
        legend = box_new(NS_BOX_BLOCK);
        legend->dom = n;
        legend->style = g_hash_table_lookup(styles, n);
    }
    legend->is_rendered_legend = TRUE;
    return legend;
}

static ns_box *
build_block_impl(const ns_node *n, GHashTable *styles)
{
    if (!n) return NULL;
    if (n->kind == NS_NODE_DOCUMENT) {
        const char *saved_base = g_base_url_for_layout;
        if (n->parent && n->parent->kind == NS_NODE_ELEMENT &&
            n->parent->name &&
            (strcmp(n->parent->name, "iframe") == 0 ||
             strcmp(n->parent->name, "frame") == 0 ||
             strcmp(n->parent->name, "object") == 0)) {
            const char *fu = ns_element_get_attr(n->parent,
                                                 "data-nd-frame-url");
            if (fu && *fu) g_base_url_for_layout = fu;
        }
        ns_box *root = box_new(NS_BOX_BLOCK);
        for (const ns_node *c = n->first_child; c; c = c->next_sibling) {
            const ns_style *cs = c->kind == NS_NODE_ELEMENT
                ? g_hash_table_lookup(styles, c) : NULL;
            if (style_is_contents(cs)) {
                append_display_contents_children(root, c, styles, FALSE, NULL);
                continue;
            }
            ns_box *child = build_block(c, styles);
            if (child) box_append_child(root, child);
        }
        g_base_url_for_layout = saved_base;
        return root;
    }
    if (n->kind != NS_NODE_ELEMENT) return NULL;
    if (node_is_frame_fallback(n)) return NULL;
    if (ns_element_get_attr(n, NS_SHADOW_ATTR)) return NULL;

    const ns_style *s = g_hash_table_lookup(styles, n);
    if (s && style_is_none(s)) return NULL;
    if (s && style_is_absolute_or_fixed(s)) {
        if (!g_abs_force_build) {
            if (g_abs_pending &&
                (!g_abs_seen || g_hash_table_add(g_abs_seen, (gpointer)n))) {
                ns_abs_entry e;
                e.dom = n;
                e.pseudo = NULL;
                const ns_css_value *pv = s->values[NS_CSS_POSITION];
                e.fixed = pv && pv->kind == NS_CSS_V_KEYWORD && pv->u.keyword &&
                          strcmp(pv->u.keyword, "fixed") == 0;
                g_array_append_val(g_abs_pending, e);
            }
            return NULL;
        }
        g_abs_force_build = FALSE;
    }

    if (n->name && strcmp(n->name, "br") == 0) {
        if (!clear_kind_of(s)) return NULL;
        ns_box *clearance = box_new(NS_BOX_BLOCK);
        clearance->dom = n;
        clearance->style = s;
        return clearance;
    }

    if (n->name && (strcmp(n->name, "img") == 0 ||
                    strcmp(n->name, "picture") == 0)) {
        ns_box *ib = build_image_box(n);
        if (ib) ib->style = s;
        return ib;
    }

    if (n->name && strcmp(n->name, "math") == 0) {
        ns_box *mb = box_new(NS_BOX_MATH);
        mb->dom = n;
        mb->style = s;
        return mb;
    }

    if (n->name && strcmp(n->name, "input") == 0) {
        if (s && (style_is_absolute_or_fixed(s) || style_is_block(s)))
            return build_form_control_block(n, s, styles);
        ns_box *ir = build_inline_run(n, n->next_sibling, styles);
        if (ir) return ir;
    }

    if (n->name && strcmp(n->name, "textarea") == 0) {
        if (s && (style_is_absolute_or_fixed(s) || style_is_block(s)))
            return build_form_control_block(n, s, styles);
        ns_box *ir = build_inline_run(n, n->next_sibling, styles);
        if (ir) return ir;
    }

    if (n->name && strcmp(n->name, "select") == 0) {
        if (s && (style_is_absolute_or_fixed(s) || style_is_block(s)))
            return build_form_control_block(n, s, styles);
        ns_box *ir = build_inline_run(n, n->next_sibling, styles);
        if (ir) return ir;
    }

    if (n->name && strcmp(n->name, "svg") == 0) {
        ns_box *box = box_new(NS_BOX_SVG);
        box->dom = n;
        box->style = s;
        box->svg_styles = styles;
        ns_box_media *m = ns_box_media_ensure(box);

        ns_svg_size size;
        ns_svg_intrinsic_size(n, &size);

        double css_w = -1, css_h = -1;
        if (s) {
            const ns_css_value *wv = s->values[NS_CSS_WIDTH];
            const ns_css_value *hv = s->values[NS_CSS_HEIGHT];
            if (wv && wv->kind == NS_CSS_V_LENGTH &&
                wv->u.length.unit == NS_CSS_UNIT_PX)
                css_w = wv->u.length.v;
            if (hv && hv->kind == NS_CSS_V_LENGTH &&
                hv->u.length.unit == NS_CSS_UNIT_PX)
                css_h = hv->u.length.v;
        }

        double w = css_w > 0 ? css_w : (size.has_width  ? size.width  : 0);
        double h = css_h > 0 ? css_h : (size.has_height ? size.height : 0);
        if (w > 0 && h <= 0 && size.has_ratio) h = w / size.ratio;
        else if (h > 0 && w <= 0 && size.has_ratio) w = h * size.ratio;

        gboolean root_document = !n->parent || n->parent->kind == NS_NODE_DOCUMENT;
        if (root_document) {
            double vw = ns_css_viewport_w();
            double vh = ns_css_viewport_h();
            if (w <= 0 && vw > 0) w = vw;
            if (h <= 0 && vh > 0) h = vh;
        }

        if (w <= 0 && h <= 0 && size.has_ratio && !root_document) {
            m->intrinsic_ratio_only = TRUE;
            w = 0;
            h = 0;
        } else if (w <= 0 && h <= 0) { w = 300; h = 150; }
        else if (w <= 0) w = 300;
        else if (h <= 0) h = 150;

        box->content_width  = w;
        box->content_height = h;
        m->declared_image_size = w > 0 && h > 0;
        return box;
    }

    if (n->name && strcmp(n->name, "canvas") == 0) {
        ns_box *cb = box_new(NS_BOX_IMAGE);
        cb->dom = n;
        cb->style = s;
        ns_box_media *cm = ns_box_media_ensure(cb);
        cb->content_width  = image_dimension_attr(n, "width");
        cb->content_height = image_dimension_attr(n, "height");
        if (cb->content_width  <= 0) cb->content_width  = 300;
        if (cb->content_height <= 0) cb->content_height = 150;
        cm->declared_image_size = TRUE;
        return cb;
    }

    if (n->name && strcmp(n->name, "audio") == 0) {
        ns_box *ab = build_audio_box(n);
        if (ab) ab->style = s;
        return ab;
    }

    if (n->name && strcmp(n->name, "video") == 0) {
        ns_box *vb = build_video_box(n);
        if (vb) {
            vb->style = s;
            collect_box_bg_image(vb, s);
        }
        return vb;
    }

    if (node_has_media_metadata(n)) {
        gboolean has_src = ns_element_get_attr(n, NS_MEDIA_SRC_ATTR) != NULL;
        gboolean empty_skeleton = TRUE;
        for (const ns_node *mc = n->first_child; mc; mc = mc->next_sibling)
            if (mc->kind == NS_NODE_ELEMENT) { empty_skeleton = FALSE; break; }
        if (has_src || empty_skeleton) {
            ns_box *vb = build_video_box(n);
            if (vb) {
                vb->style = s;
                collect_box_bg_image(vb, s);
            }
            return vb;
        }
    }

    if (is_table_box(n, styles))
        return build_table(n, styles);

    if (n->name && strcmp(n->name, "slot") == 0) {
        const ns_node *host = layout_slot_host(n);
        const ns_node *sr = host ? layout_shadow_root(host) : NULL;
        const char *slot_name = ns_element_get_attr(n, "name");
        ns_box *sbox = box_new(NS_BOX_BLOCK);
        sbox->dom = n;
        sbox->style = s;
        gboolean any = FALSE;
        if (host && sr) {
            for (const ns_node *lc = host->first_child; lc; lc = lc->next_sibling) {
                if (lc == sr) continue;
                const char *cs = (lc->kind == NS_NODE_ELEMENT)
                    ? ns_element_get_attr(lc, "slot") : NULL;
                if (g_strcmp0(cs ? cs : "", slot_name ? slot_name : "") != 0)
                    continue;
                ns_box *cb;
                if (is_inline_dom(lc, styles)) {
                    cb = build_inline_run(lc, lc->next_sibling, styles);
                    if (cb && !(cb->text && cb->text[0])) {
                        ns_box_free(cb);
                        cb = NULL;
                    }
                } else {
                    cb = build_block(lc, styles);
                }
                if (cb) { box_append_child(sbox, cb); any = TRUE; }
            }
        }
        if (any) return sbox;
        ns_box_free(sbox);
    }

    if (!style_is_block(s) && !contains_block_media(n, styles) &&
        !style_is_absolute_or_fixed(s) && n != g_blockified_legend) return NULL;

    ns_box *block = box_new(NS_BOX_BLOCK);
    block->dom = n;
    block->style = s;
    const ns_node *rendered_legend = fieldset_rendered_legend_node(n, styles);

    collect_box_bg_image(block, s);

    gboolean details_collapsed = FALSE;
    if (n->name && strcmp(n->name, "details") == 0 &&
        !ns_element_get_attr(n, "open"))
        details_collapsed = TRUE;

    if (s) {
        register_abs_pseudo(n, s->before);
        register_abs_pseudo(n, s->after);
    }

    ns_box *before_block = (s && s->before)
        ? build_pseudo_block_for(s->before, n) : NULL;
    if (before_block) box_append_child(block, before_block);

    ns_box *pending_before = (s && s->before && !before_block &&
                              !style_is_absolute_or_fixed(s->before))
        ? build_pseudo_inline_for(s->before, n) : NULL;

    gboolean blockify_children = style_is_flex_container(s) ||
                                 style_is_grid_container(s);

    const ns_node *saved_skip = g_inline_skip_node;
    if (rendered_legend) {
        ns_box *legend = build_rendered_legend(rendered_legend, styles);
        if (legend) box_append_child(block, legend);
        g_inline_skip_node = rendered_legend;
    }

    const ns_node *shadow_host_root = layout_shadow_root(n);
    const ns_node *c = shadow_host_root ? shadow_host_root->first_child
                                        : n->first_child;
    while (c) {
        if (c->kind == NS_NODE_ELEMENT && c->name &&
            tag_is_non_rendering(c->name)) {
            c = c->next_sibling;
            continue;
        }
        if (details_collapsed) {
            if (c->kind != NS_NODE_ELEMENT || !c->name ||
                strcmp(c->name, "summary") != 0) {
                c = c->next_sibling;
                continue;
            }
        }
        if (c == rendered_legend) {
            c = c->next_sibling;
            continue;
        }
        if (blockify_children) {
            if (c->kind == NS_NODE_TEXT) {
                if (text_is_ws_only(c->text)) { c = c->next_sibling; continue; }
                ns_box *item = box_new(NS_BOX_BLOCK);
                item->style = NULL;
                ns_box *run = build_inline_run(c, c->next_sibling, styles);
                if (pending_before) {
                    run = inline_merge_prefix(pending_before, run);
                    pending_before = NULL;
                }
                if (run && run->text && run->text[0]) {
                    box_append_child(item, run);
                    box_append_child(block, item);
                } else {
                    if (run) ns_box_free(run);
                    ns_box_free(item);
                }
                c = c->next_sibling;
                continue;
            }
            if (c->kind != NS_NODE_ELEMENT) { c = c->next_sibling; continue; }
            const ns_style *cs = g_hash_table_lookup(styles, c);
            if (cs && style_is_none(cs)) { c = c->next_sibling; continue; }
            if (style_is_contents(cs)) {
                append_display_contents_children(block, c, styles, TRUE,
                                                 &pending_before);
                c = c->next_sibling;
                continue;
            }
            if (cs && style_is_absolute_or_fixed(cs)) {
                ns_box *child = build_block(c, styles);
                if (child) box_append_child(block, child);
                c = c->next_sibling;
                continue;
            }
            if (style_is_block(cs) ||
                contains_block_media(c, styles) ||
                node_has_media_metadata(c) ||
                (c->name && (strcmp(c->name, "img") == 0 ||
                             strcmp(c->name, "picture") == 0 ||
                             strcmp(c->name, "svg") == 0 ||
                             strcmp(c->name, "audio") == 0 ||
                             strcmp(c->name, "video") == 0 ||
                             strcmp(c->name, "table") == 0))) {
                ns_box *child = build_block(c, styles);
                if (child) box_append_child(block, child);
                c = c->next_sibling;
                continue;
            }
            ns_box *item = build_blockified_inline_item(c, styles,
                                                        &pending_before);
            if (item) box_append_child(block, item);
            c = c->next_sibling;
            continue;
        }
        if (c->kind == NS_NODE_ELEMENT) {
            const ns_style *cs = g_hash_table_lookup(styles, c);
            if (style_is_contents(cs)) {
                append_display_contents_children(block, c, styles, FALSE,
                                                 &pending_before);
                c = c->next_sibling;
                continue;
            }
        }
        if (node_is_table_internal(c, styles)) {
            const ns_node *start = c;
            while (c && (node_is_table_internal(c, styles) ||
                         (c->kind == NS_NODE_TEXT && text_is_ws_only(c->text))))
                c = c->next_sibling;
            if (pending_before) {
                box_append_child(block, pending_before);
                pending_before = NULL;
            }
            ns_box *anon = build_anonymous_table(start, c, styles);
            if (anon) box_append_child(block, anon);
            continue;
        }
        if (is_inline_dom(c, styles)) {
            const ns_node *start = c;
            c = c->next_sibling;
            while (c) {
                if (c->kind == NS_NODE_ELEMENT && c->name &&
                    tag_is_non_rendering(c->name)) {
                    c = c->next_sibling;
                    continue;
                }
                if (c == rendered_legend) {
                    c = c->next_sibling;
                    continue;
                }
                if (details_collapsed &&
                    (c->kind != NS_NODE_ELEMENT || !c->name ||
                     strcmp(c->name, "summary") != 0)) break;
                if (c->kind == NS_NODE_ELEMENT) {
                    const ns_style *cs = g_hash_table_lookup(styles, c);
                    if (style_is_contents(cs)) break;
                }
                if (!continues_inline_run(c, styles)) break;
                c = c->next_sibling;
            }
            ns_box *run = build_inline_run(start, c, styles);
            if (pending_before) {
                run = inline_merge_prefix(pending_before, run);
                pending_before = NULL;
            }

            if (run->text && run->text[0] != '\0')
                box_append_child(block, run);
            else
                ns_box_free(run);
        } else {
            if (pending_before) {
                box_append_child(block, pending_before);
                pending_before = NULL;
            }
            ns_box *child = build_block(c, styles);
            if (child) box_append_child(block, child);
            if (c) c = c->next_sibling;
        }
    }
    g_inline_skip_node = saved_skip;

    if (pending_before) {
        box_append_child(block, pending_before);
        pending_before = NULL;
    }

    ns_box *after_block = (s && s->after)
        ? build_pseudo_block_for(s->after, n) : NULL;
    if (after_block) box_append_child(block, after_block);

    if (s && s->after && !after_block &&
        !style_is_absolute_or_fixed(s->after)) {
        ns_box *gen = build_pseudo_inline_for(s->after, n);
        if (gen) {
            ns_box *last = block->first_child;
            while (last && last->next_sibling) last = last->next_sibling;
            if (last && last->kind == NS_BOX_INLINE) {
                gsize ll = last->text ? strlen(last->text) : 0;
                gsize gl = gen->text  ? strlen(gen->text)  : 0;
                if (ll > G_MAXSIZE - gl - 1) { ns_box_free(gen); return block; }
                char *combined = g_malloc(ll + gl + 1);
                if (ll) memcpy(combined, last->text, ll);
                if (gl) memcpy(combined + ll, gen->text, gl);
                combined[ll + gl] = '\0';
                g_free(last->text);
                last->text = combined;
                if (gen->attrs) {
                    for (guint i = 0; i < gen->attrs->len; i++) {
                        ns_inline_attr a = g_array_index(gen->attrs, ns_inline_attr, i);
                        a.start += ll;
                        if (!last->attrs)
                            last->attrs = g_array_new(FALSE, FALSE, sizeof(ns_inline_attr));
                        g_array_append_val(last->attrs, a);
                    }
                }
                if (gen->inline_atomics) {
                    if (!last->inline_atomics)
                        last->inline_atomics =
                            g_array_new(FALSE, FALSE, sizeof(ns_inline_atomic));
                    for (guint i = 0; i < gen->inline_atomics->len; i++) {
                        ns_inline_atomic ia = g_array_index(gen->inline_atomics,
                                                            ns_inline_atomic, i);
                        ia.byte_off += ll;
                        if (ia.box) ia.box->parent = last;
                        g_array_append_val(last->inline_atomics, ia);
                    }
                    g_array_free(gen->inline_atomics, TRUE);
                    gen->inline_atomics = NULL;
                }
                ns_box_free(gen);
            } else {
                box_append_child(block, gen);
            }
        }
    }
    return block;
}

static NsPangoLayout *
make_pango_layout(const ns_style *parent_style)
{
    static NsPangoContext *cached_ctx;
    if (!cached_ctx) {
        NsPangoFontMap *fm = ns_pango_cairo_font_map_get_default();
        cached_ctx = ns_pango_font_map_create_context(fm);
    }
    NsPangoLayout *layout = ns_pango_layout_new(cached_ctx);
    ns_paint_apply_inline_font(layout, parent_style);
    return layout;
}

static void
apply_inline_spacing(NsPangoAttrList *list, const ns_style *style, const char *text)
{
    if (!list || !style || !text) return;
    double ls_px = 0, ws_px = 0;
    const ns_css_value *lv = style->values[NS_CSS_LETTER_SPACING];
    if (lv && lv->kind == NS_CSS_V_LENGTH && lv->u.length.unit == NS_CSS_UNIT_PX)
        ls_px = lv->u.length.v;
    const ns_css_value *wv = style->values[NS_CSS_WORD_SPACING];
    if (wv && wv->kind == NS_CSS_V_LENGTH && wv->u.length.unit == NS_CSS_UNIT_PX)
        ws_px = wv->u.length.v;
    if (ls_px != 0) {
        NsPangoAttribute *ls = ns_pango_attr_letter_spacing_new(
            (int)(ls_px * NS_PANGO_SCALE));
        ls->start_index = 0;
        ls->end_index = G_MAXUINT;
        ns_pango_attr_list_insert(list, ls);
    }
    if (ws_px != 0) {
        int per_space = (int)((ls_px + ws_px) * NS_PANGO_SCALE);
        for (const char *p = text; *p; p++) {
            if (*p == ' ') {
                gsize idx = (gsize)(p - text);
                NsPangoAttribute *a = ns_pango_attr_letter_spacing_new(per_space);
                a->start_index = (guint)idx;
                a->end_index = (guint)(idx + 1);
                ns_pango_attr_list_insert(list, a);
            }
        }
    }
}

static NsPangoWeight
layout_pango_weight_from_css(int weight)
{
    if (weight <= 100) return NS_PANGO_WEIGHT_THIN;
    if (weight <= 200) return NS_PANGO_WEIGHT_ULTRALIGHT;
    if (weight <= 300) return NS_PANGO_WEIGHT_LIGHT;
    if (weight <= 400) return NS_PANGO_WEIGHT_NORMAL;
    if (weight <= 500) return NS_PANGO_WEIGHT_MEDIUM;
    if (weight <= 600) return NS_PANGO_WEIGHT_SEMIBOLD;
    if (weight <= 700) return NS_PANGO_WEIGHT_BOLD;
    if (weight <= 800) return NS_PANGO_WEIGHT_ULTRABOLD;
    if (weight <= 900) return NS_PANGO_WEIGHT_HEAVY;
    return (NsPangoWeight)weight;
}

static NsPangoStretch
layout_pango_stretch_from_css(int rank)
{
    static const NsPangoStretch map[] = {
        NS_PANGO_STRETCH_ULTRA_CONDENSED,
        NS_PANGO_STRETCH_EXTRA_CONDENSED,
        NS_PANGO_STRETCH_CONDENSED,
        NS_PANGO_STRETCH_SEMI_CONDENSED,
        NS_PANGO_STRETCH_NORMAL,
        NS_PANGO_STRETCH_SEMI_EXPANDED,
        NS_PANGO_STRETCH_EXPANDED,
        NS_PANGO_STRETCH_EXTRA_EXPANDED,
        NS_PANGO_STRETCH_ULTRA_EXPANDED,
    };
    if (rank < 0) rank = 0;
    if (rank > 8) rank = 8;
    return map[rank];
}

static void
layout_attr_insert_range(NsPangoAttrList *attrs, NsPangoAttribute *a,
                         gsize start, gsize len)
{
    if (!a || len == 0) return;
    a->start_index = (guint)start;
    a->end_index = (guint)(start + len);
    ns_pango_attr_list_insert(attrs, a);
}

static void
apply_inline_layout_attrs(NsPangoAttrList *attrs, const ns_box *box)
{
    if (!attrs || !box || !box->attrs) return;
    for (gint ii = (gint)box->attrs->len - 1; ii >= 0; ii--) {
        const ns_inline_attr *r =
            &g_array_index(box->attrs, ns_inline_attr, (guint)ii);
        NsPangoAttribute *a = NULL;
        switch (r->kind) {
        case NS_INLINE_BOLD:
            a = ns_pango_attr_weight_new(NS_PANGO_WEIGHT_BOLD);
            break;
        case NS_INLINE_FONT_WEIGHT:
            a = ns_pango_attr_weight_new(layout_pango_weight_from_css(r->font_weight));
            break;
        case NS_INLINE_FONT_STRETCH:
            a = ns_pango_attr_stretch_new(
                layout_pango_stretch_from_css(r->font_stretch));
            break;
        case NS_INLINE_FONT_FEATURES:
            a = ns_paint_font_features_attr_from_values(r->font_kerning,
                                                        r->font_ligatures,
                                                        r->font_features);
            break;
        case NS_INLINE_FONT_VARIATIONS:
            a = ns_paint_font_variations_attr_from_values(r->font_variations);
            break;
        case NS_INLINE_ITALIC:
            a = ns_pango_attr_style_new(NS_PANGO_STYLE_ITALIC);
            break;
        case NS_INLINE_MONOSPACE:
            a = ns_pango_attr_family_new("monospace");
            break;
        case NS_INLINE_INPUT_FIELD:
        case NS_INLINE_INPUT_FIELD_FOCUSED:
        case NS_INLINE_BUTTON:
            if (!(r->dom && r->dom->name &&
                  strcmp(r->dom->name, "textarea") == 0))
                layout_attr_insert_range(attrs,
                    ns_pango_attr_allow_breaks_new(FALSE), r->start, r->len);
            break;
        case NS_INLINE_FONT_SIZE:
            a = ns_pango_attr_size_new_absolute(
                ns_paint_pango_font_size(r->font_size_px));
            break;
        case NS_INLINE_FONT_FAMILY:
            if (r->family) {
                char *ns_pango_family = ns_css_font_family_for_pango(r->family);
                a = ns_pango_attr_family_new(ns_pango_family);
                g_free(ns_pango_family);
            }
            break;
        case NS_INLINE_SUPERSCRIPT:
            layout_attr_insert_range(attrs, ns_pango_attr_rise_new(4000),
                                     r->start, r->len);
            a = ns_pango_attr_scale_new(0.75);
            break;
        case NS_INLINE_SUBSCRIPT:
            layout_attr_insert_range(attrs, ns_pango_attr_rise_new(-3000),
                                     r->start, r->len);
            a = ns_pango_attr_scale_new(0.75);
            break;
        case NS_INLINE_SMALL_CAPS:
            a = ns_pango_attr_variant_new(NS_PANGO_VARIANT_SMALL_CAPS);
            break;
        case NS_INLINE_SPACER: {
            NsPangoRectangle rect = {
                0, 0, (int)(r->box_w * NS_PANGO_SCALE), 0
            };
            a = ns_pango_attr_shape_new(&rect, &rect);
            break;
        }
        default:
            break;
        }
        layout_attr_insert_range(attrs, a, r->start, r->len);
    }
}

static gboolean
field_attr_is_text_input(const ns_inline_attr *r)
{
    if (r->kind != NS_INLINE_INPUT_FIELD &&
        r->kind != NS_INLINE_INPUT_FIELD_FOCUSED)
        return FALSE;
    const ns_node *n = r->dom;
    if (!n || !n->name || strcmp(n->name, "input") != 0) return FALSE;
    const char *type = ns_element_get_attr(n, "type");
    return !type || !*type ||
        g_ascii_strcasecmp(type, "text") == 0 ||
        g_ascii_strcasecmp(type, "search") == 0 ||
        g_ascii_strcasecmp(type, "email") == 0 ||
        g_ascii_strcasecmp(type, "url") == 0 ||
        g_ascii_strcasecmp(type, "tel") == 0 ||
        g_ascii_strcasecmp(type, "number") == 0 ||
        g_ascii_strcasecmp(type, "password") == 0;
}

void
ns_inline_apply_atomic_shapes(NsPangoAttrList *list, const ns_box *box)
{
    if (!box) return;
    if (box->inline_atomics) {
        double max_asc = 0;
        for (guint i = 0; i < box->inline_atomics->len; i++) {
            const ns_inline_atomic *a =
                &g_array_index(box->inline_atomics, ns_inline_atomic, i);
            const ns_box *ab = a->box;
            if (!ab) continue;
            double h = ab->margin.top + ab->border.top + ab->padding.top +
                       ab->content_height +
                       ab->padding.bottom + ab->border.bottom + ab->margin.bottom;
            if (h < 0) h = 0;
            double fs = length_or(ab->style ? ab->style->values[NS_CSS_FONT_SIZE]
                                            : NULL, 16);
            double xh = fs * 0.5;
            const char *va = ab->style
                ? ns_style_keyword(ab->style, NS_CSS_VERTICAL_ALIGN) : NULL;
            double a_asc = h;
            double ab_baseline;
            if (!box_clips_children(ab) && box_first_baseline(ab, &ab_baseline))
                a_asc = ab->margin.top + ab_baseline;
            if (va) {
                if (strcmp(va, "middle") == 0)      a_asc = h / 2 + xh / 2;
                else if (strcmp(va, "super") == 0)  a_asc = h + fs * 0.3;
                else if (strcmp(va, "sub") == 0)    a_asc = h - fs * 0.2;
                else if (strcmp(va, "top") == 0 ||
                         strcmp(va, "text-top") == 0 ||
                         strcmp(va, "bottom") == 0 ||
                         strcmp(va, "text-bottom") == 0)
                    a_asc = fs * 0.8;
            }
            if (a_asc > max_asc) max_asc = a_asc;
        }
        for (guint i = 0; i < box->inline_atomics->len; i++) {
            const ns_inline_atomic *a =
                &g_array_index(box->inline_atomics, ns_inline_atomic, i);
            const ns_box *ab = a->box;
            if (!ab) continue;
            double w = ab->margin.left + ab->border.left + ab->padding.left +
                       ab->content_width +
                       ab->padding.right + ab->border.right + ab->margin.right;
            double h = ab->margin.top + ab->border.top + ab->padding.top +
                       ab->content_height +
                       ab->padding.bottom + ab->border.bottom + ab->margin.bottom;
            if (w < 0) w = 0;
            if (h < 0) h = 0;
            double fs = length_or(ab->style ? ab->style->values[NS_CSS_FONT_SIZE]
                                            : NULL, 16);
            double asc = fs * 0.8, desc = fs * 0.2, xh = fs * 0.5;
            double top = -h;
            double ab_baseline;
            if (!box_clips_children(ab) && box_first_baseline(ab, &ab_baseline))
                top = -(ab->margin.top + ab_baseline);
            if (ab->kind == NS_BOX_MATH) {
                double mw = 0, ma = 0, md = 0;
                ns_math_measure(ab->dom, fs, &mw, &ma, &md);
                top = -(ab->margin.top + ab->border.top + ab->padding.top + ma);
            }
            const char *va = ab->style
                ? ns_style_keyword(ab->style, NS_CSS_VERTICAL_ALIGN) : NULL;
            if (va) {
                double line_asc = max_asc > asc ? max_asc : asc;
                if (strcmp(va, "middle") == 0)           top = -(h / 2 + xh / 2);
                else if (strcmp(va, "text-top") == 0)    top = -asc;
                else if (strcmp(va, "top") == 0)         top = -line_asc;
                else if (strcmp(va, "text-bottom") == 0) top = desc - h;
                else if (strcmp(va, "bottom") == 0)      top = desc - h;
                else if (strcmp(va, "super") == 0)       top -= fs * 0.3;
                else if (strcmp(va, "sub") == 0)         top += fs * 0.2;
            }
            NsPangoRectangle r = { 0, (int)(top * NS_PANGO_SCALE),
                                 (int)(w * NS_PANGO_SCALE), (int)(h * NS_PANGO_SCALE) };
            NsPangoAttribute *attr = ns_pango_attr_shape_new(&r, &r);
            attr->start_index = (guint)a->byte_off;
            attr->end_index   = (guint)(a->byte_off + 3);
            ns_pango_attr_list_insert(list, attr);
        }
    }

}

void
ns_inline_layout_set_attrs(NsPangoLayout *layout, NsPangoAttrList *list,
                           const ns_box *box)
{
    ns_pango_layout_set_attributes(layout, list);
    if (!box || !box->attrs) return;
    gboolean stretched = FALSE;
    for (guint i = 0; i < box->attrs->len; i++) {
        const ns_inline_attr *r =
            &g_array_index(box->attrs, ns_inline_attr, i);
        if (!field_attr_is_text_input(r)) continue;
        if (r->len < 4) continue;
        double css_w = inline_attr_control_width(r, box);
        if (css_w <= 0) continue;
        NsPangoRectangle p0, p1;
        ns_pango_layout_index_to_pos(layout, (int)r->start, &p0);
        ns_pango_layout_index_to_pos(layout, (int)(r->start + r->len - 2), &p1);
        if (p1.y != p0.y) continue;
        double prefix = (double)(p1.x - p0.x) / NS_PANGO_SCALE;
        if (prefix < 0) continue;
        double fs = length_or(r->style ? r->style->values[NS_CSS_FONT_SIZE]
                                       : NULL, 16);
        double w = css_w - prefix;
        if (w < fs * 0.4) continue;
        NsPangoRectangle rect = { 0, (int)(-fs * 0.8 * NS_PANGO_SCALE),
                                (int)(w * NS_PANGO_SCALE), (int)(fs * NS_PANGO_SCALE) };
        NsPangoAttribute *attr = ns_pango_attr_shape_new(&rect, &rect);
        attr->start_index = (guint)(r->start + r->len - 2);
        attr->end_index   = (guint)(r->start + r->len);
        ns_pango_attr_list_insert(list, attr);
        stretched = TRUE;
    }
    if (stretched) ns_pango_layout_context_changed(layout);
}

static double inline_atomic_measure_basis(const ns_box *ab);

static void
inline_layout_atomics_prepare(ns_box *box, const ns_style *parent_style)
{
    if (!box->inline_atomics) return;
    for (guint ai = 0; ai < box->inline_atomics->len; ai++) {
        ns_box *ab =
            g_array_index(box->inline_atomics, ns_inline_atomic, ai).box;
        if (inline_atomic_needs_layout(ab))
            layout_box(ab, inline_atomic_measure_basis(ab), parent_style);
    }
}

static double
inline_line_height(const ns_style *parent_style)
{
    double font_size = length_or(parent_style ? parent_style->values[NS_CSS_FONT_SIZE] : NULL, 16);
    double used = ns_paint_css_line_height_px(parent_style);
    if (used > 0) return used;
    return font_size * 1.2;
}

static double
inline_control_is_textarea(const ns_inline_attr *r)
{
    return r->dom && r->dom->name && strcmp(r->dom->name, "textarea") == 0;
}

static double
inline_control_line_height(const ns_box *box, double line_height)
{
    if (!box || !box->attrs) return line_height;
    double out = line_height;
    for (guint i = 0; i < box->attrs->len; i++) {
        const ns_inline_attr *r =
            &g_array_index(box->attrs, ns_inline_attr, i);
        if (r->kind != NS_INLINE_INPUT_FIELD &&
            r->kind != NS_INLINE_INPUT_FIELD_FOCUSED &&
            r->kind != NS_INLINE_BUTTON)
            continue;
        if (inline_control_is_textarea(r)) continue;
        if (!r->native_chrome) continue;
        double cfs = 0;
        for (guint j = 0; j < box->attrs->len; j++) {
            const ns_inline_attr *f =
                &g_array_index(box->attrs, ns_inline_attr, j);
            if (f->kind != NS_INLINE_FONT_SIZE) continue;
            if (f->start <= r->start &&
                f->start + f->len >= r->start + r->len) {
                cfs = f->font_size_px;
                break;
            }
        }
        double font_box = cfs > 0 ? cfs * 1.3 + 12.0 : 0;
        double h = r->box_h > 0
                   ? r->box_h + (r->native_chrome ? 8.0 : 0.0)
                   : line_height + 18.0;
        if (font_box > h) h = font_box;
        if (h > out) out = h;
    }
    return out;
}

static double
inline_textarea_total_height(const ns_box *box)
{
    if (!box || !box->attrs) return 0;
    double out = 0;
    for (guint i = 0; i < box->attrs->len; i++) {
        const ns_inline_attr *r =
            &g_array_index(box->attrs, ns_inline_attr, i);
        if ((r->kind == NS_INLINE_INPUT_FIELD ||
             r->kind == NS_INLINE_INPUT_FIELD_FOCUSED) &&
            inline_control_is_textarea(r) && r->box_h > 0) {
            double h = r->box_h + (r->native_chrome ? 8.0 : 0.0);
            if (h > out) out = h;
        }
    }
    return out;
}

static gboolean
inline_attr_cacheable_kind(ns_inline_attr_kind k)
{
    switch (k) {
    case NS_INLINE_INPUT_FIELD:
    case NS_INLINE_INPUT_FIELD_FOCUSED:
    case NS_INLINE_BUTTON:
    case NS_INLINE_CHECKBOX:
    case NS_INLINE_CHECKBOX_CHECKED:
    case NS_INLINE_RADIO:
    case NS_INLINE_RADIO_CHECKED:
    case NS_INLINE_PROGRESS:
    case NS_INLINE_METER:
    case NS_INLINE_CARET:
    case NS_INLINE_SELECTION:
        return FALSE;
    default:
        return TRUE;
    }
}

static gboolean
inline_attr_affects_measure(ns_inline_attr_kind k)
{
    switch (k) {
    case NS_INLINE_BOLD:
    case NS_INLINE_ITALIC:
    case NS_INLINE_MONOSPACE:
    case NS_INLINE_FONT_SIZE:
    case NS_INLINE_FONT_WEIGHT:
    case NS_INLINE_FONT_STRETCH:
    case NS_INLINE_FONT_FEATURES:
    case NS_INLINE_FONT_VARIATIONS:
    case NS_INLINE_FONT_FAMILY:
    case NS_INLINE_SUPERSCRIPT:
    case NS_INLINE_SUBSCRIPT:
    case NS_INLINE_SMALL_CAPS:
        return TRUE;
    default:
        return FALSE;
    }
}

static gboolean
inline_box_measure_cacheable(const ns_box *box)
{
    if (!box || (box->inline_atomics && box->inline_atomics->len > 0))
        return FALSE;
    if (!box->attrs) return TRUE;
    for (guint i = 0; i < box->attrs->len; i++) {
        const ns_inline_attr *a =
            &g_array_index(box->attrs, ns_inline_attr, i);
        if (!inline_attr_cacheable_kind(a->kind)) return FALSE;
    }
    return TRUE;
}

static double measure_natural_width(ns_box *box, const ns_style *parent_style);
static double measure_max_content_width(ns_box *box, const ns_style *parent_style);
static double width_contribution_keyword_limits(ns_box *box, double w,
                                                const ns_style *parent_style,
                                                gboolean max_content);
static gboolean flex_box_is_border_box(const ns_box *c);
static void legacy_align_block_child(ns_box *c, double avail_x, double avail_w,
                                     const ns_style *inherited);

static double
inline_atomic_outer_height(const ns_box *b)
{
    if (!b) return 0;
    return b->content_height + b->padding.top + b->padding.bottom +
           b->border.top + b->border.bottom + b->margin.top + b->margin.bottom;
}

static gboolean
inline_box_has_measure_attrs(const ns_box *box)
{
    if (!box || !box->attrs) return FALSE;
    for (guint i = 0; i < box->attrs->len; i++) {
        const ns_inline_attr *a =
            &g_array_index(box->attrs, ns_inline_attr, i);
        if (inline_attr_affects_measure(a->kind)) return TRUE;
    }
    return FALSE;
}

static gboolean
inline_style_has_measure_adjustments(const ns_style *style)
{
    const ns_css_value *lv = style ? style->values[NS_CSS_LETTER_SPACING] : NULL;
    if (lv && lv->kind == NS_CSS_V_LENGTH && lv->u.length.unit == NS_CSS_UNIT_PX &&
        fabs(lv->u.length.v) > 0.001)
        return TRUE;
    const ns_css_value *wv = style ? style->values[NS_CSS_WORD_SPACING] : NULL;
    if (wv && wv->kind == NS_CSS_V_LENGTH && wv->u.length.unit == NS_CSS_UNIT_PX &&
        fabs(wv->u.length.v) > 0.001)
        return TRUE;
    const char *fk = style ? ns_style_keyword(style, NS_CSS_FONT_KERNING) : NULL;
    if (fk && strcmp(fk, "auto") != 0 && strcmp(fk, "normal") != 0) return TRUE;
    const char *fl = style ? ns_style_keyword(style, NS_CSS_FONT_VARIANT_LIGATURES) : NULL;
    if (fl && strcmp(fl, "normal") != 0) return TRUE;
    const char *ff = style ? ns_style_keyword(style, NS_CSS_FONT_FEATURE_SETTINGS) : NULL;
    if (ff && strcmp(ff, "normal") != 0) return TRUE;
    const char *fv = style ? ns_style_keyword(style, NS_CSS_FONT_VARIATION_SETTINGS) : NULL;
    return fv && strcmp(fv, "normal") != 0;
}

static gboolean
inline_text_simple_ascii(const char *text)
{
    if (!text) return FALSE;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p >= 0x80) return FALSE;
    }
    return TRUE;
}

static double
measure_inline_ascii_min_width(ns_box *box, const ns_style *parent_style)
{
    if (!box || !box->text || !*box->text) return 0;
    if (box->inline_atomics && box->inline_atomics->len > 0) return -1;
    if (inline_box_has_measure_attrs(box)) return -1;
    if (inline_style_has_measure_adjustments(parent_style)) return -1;
    if (!inline_text_simple_ascii(box->text)) return -1;
    if (keyword_is(parent_style ? parent_style->values[NS_CSS_WHITE_SPACE] : NULL, "nowrap") ||
        keyword_is(parent_style ? parent_style->values[NS_CSS_WHITE_SPACE] : NULL, "pre"))
        return measure_natural_width(box, parent_style);

    const char *best = NULL;
    gsize best_len = 0;
    const char *run = NULL;
    gsize run_len = 0;
    for (const char *p = box->text;; p++) {
        gboolean br = *p == '\0' || *p == ' ' || *p == '\t' ||
                      *p == '\r' || *p == '\n' || *p == '\f';
        if (br) {
            if (run_len > best_len) {
                best = run;
                best_len = run_len;
            }
            run = NULL;
            run_len = 0;
            if (*p == '\0') break;
        } else {
            if (!run) run = p;
            run_len++;
        }
    }
    if (best_len == 0) return 0;
    if (best_len == strlen(box->text)) return measure_natural_width(box, parent_style);

    NsPangoLayout *layout = make_pango_layout(parent_style);
    ns_pango_layout_set_width(layout, -1);
    ns_pango_layout_set_text(layout, best, (int)best_len);
    NsPangoRectangle logical;
    ns_pango_layout_get_extents(layout, NULL, &logical);
    g_object_unref(layout);
    return ceil((double)logical.width / NS_PANGO_SCALE);
}

static void shift_box_tree(ns_box *b, double dx, double dy);

static void
inline_apply_text_align(NsPangoLayout *layout, const ns_style *s)
{
    const ns_css_value *ta = s ? s->values[NS_CSS_TEXT_ALIGN] : NULL;
    gboolean rtl = ns_pango_context_get_base_dir(
        ns_pango_layout_get_context(layout)) == NS_PANGO_DIRECTION_RTL;
    if (keyword_is(ta, "center"))
        ns_pango_layout_set_alignment(layout, NS_PANGO_ALIGN_CENTER);
    else if (keyword_is(ta, "right") ||
             (keyword_is(ta, "end") && !rtl) ||
             (keyword_is(ta, "start") && rtl) ||
             (!ta && rtl))
        ns_pango_layout_set_alignment(layout, NS_PANGO_ALIGN_RIGHT);
    else if (keyword_is(ta, "justify"))
        ns_pango_layout_set_justify(layout, TRUE);
    else
        ns_pango_layout_set_alignment(layout, NS_PANGO_ALIGN_LEFT);
}

static void
ns_vertical_measure(ns_box *box, const ns_style *ps,
                    double *thickness, double *length)
{
    int orient = ns_css_text_orientation(ps);
    NsPangoLayout *layout = make_pango_layout(ps);
    int pw = 0, ph = 0;
    if (orient == 1) {
        char *stacked = ns_vertical_stack_text(box->text);
        ns_pango_layout_set_width(layout, -1);
        ns_pango_layout_set_alignment(layout, NS_PANGO_ALIGN_CENTER);
        ns_pango_layout_set_text(layout, stacked, -1);
        g_free(stacked);
        ns_pango_layout_get_pixel_size(layout, &pw, &ph);
        *thickness = pw;
        *length = ph;
    } else {
        ns_pango_layout_set_width(layout, -1);
        ns_paint_apply_css_line_spacing(layout, ps);
        NsPangoAttrList *i18n = ns_pango_attr_list_new();
        ns_paint_apply_i18n(layout, i18n, box);
        ns_paint_apply_font_features(i18n, ps, 0, G_MAXUINT);
        apply_inline_spacing(i18n, ps, box->text);
        ns_inline_layout_set_attrs(layout, i18n, box);
        ns_pango_attr_list_unref(i18n);
        ns_pango_layout_set_text(layout, box->text, -1);
        ns_pango_layout_get_pixel_size(layout, &pw, &ph);
        *thickness = ph;
        *length = pw;
    }
    g_object_unref(layout);
}

static gboolean
box_first_baseline(const ns_box *b, double *out)
{
    if (!b) return FALSE;
    if (b->kind == NS_BOX_INLINE) {
        if (b->first_baseline <= 0) return FALSE;
        *out = b->margin.top + b->border.top + b->padding.top +
               b->first_baseline;
        return TRUE;
    }
    for (const ns_box *c = b->first_child; c; c = c->next_sibling) {
        if (style_is_absolute_or_fixed(c->style) || c->is_rendered_legend)
            continue;
        double child_baseline;
        if (box_first_baseline(c, &child_baseline)) {
            *out = (c->y - b->y) + child_baseline;
            return TRUE;
        }
    }
    return FALSE;
}

static const char *
flex_item_align(const ns_box *c, const char *container_align)
{
    if (c && c->style) {
        const char *as = ns_style_keyword(c->style, NS_CSS_ALIGN_SELF);
        if (as && strcmp(as, "auto") != 0) return as;
    }
    return container_align;
}

static gboolean
flex_align_is_baseline(const char *align)
{
    return align && (strcmp(align, "baseline") == 0 ||
                     strcmp(align, "first baseline") == 0);
}

static double
flex_item_baseline(const ns_box *c, double fallback)
{
    if (c->parent && ns_css_writing_mode(c->parent->style) &&
        ns_css_writing_mode(c->style)) {
        double border_h = c->content_height + c->padding.top +
                          c->padding.bottom + c->border.top + c->border.bottom;
        return c->margin.top + border_h / 2.0;
    }
    double baseline;
    return box_first_baseline(c, &baseline) ? baseline : fallback;
}

static void
inline_layout(ns_box *box, double content_width, const ns_style *parent_style)
{
    g_assert(box->kind == NS_BOX_INLINE);
    if (!box->text || !*box->text) {
        box->content_width  = 0;
        box->content_height = 0;
        box->first_baseline = 0;
        return;
    }

    box->vertical_wm = 0;
    box->text_orient = 0;
    if (ns_css_writing_mode(parent_style) &&
        !(box->inline_atomics && box->inline_atomics->len > 0)) {
        box->vertical_wm = ns_css_writing_mode(parent_style);
        box->text_orient = ns_css_text_orientation(parent_style);
        double thickness = 0, length = 0;
        ns_vertical_measure(box, parent_style, &thickness, &length);
        box->content_width  = thickness;
        box->content_height = length;
        box->first_baseline = 0;
        box->inline_layout_cache_valid = FALSE;
        return;
    }

    if (box->inline_atomics) {
        for (guint i = 0; i < box->inline_atomics->len; i++) {
            ns_box *ab = g_array_index(box->inline_atomics, ns_inline_atomic, i).box;
            if (ab && !(g_abs_ph_set && g_hash_table_contains(g_abs_ph_set, ab)))
                layout_box(ab, content_width, parent_style);
        }
    }

    gboolean cacheable = inline_box_measure_cacheable(box);
    if (cacheable && box->inline_layout_cache_valid &&
        box->inline_layout_cache_style == parent_style &&
        fabs(box->inline_layout_cache_width - content_width) < 0.001) {
        box->content_width = content_width;
        box->content_height = box->inline_layout_cache_height;
        return;
    }

    box->content_width = content_width;
    NsPangoLayout *layout = make_pango_layout(parent_style);
    gboolean ws_nowrap = keyword_is(
        parent_style ? parent_style->values[NS_CSS_WHITE_SPACE] : NULL, "nowrap") ||
        keyword_is(parent_style ? parent_style->values[NS_CSS_WHITE_SPACE] : NULL, "pre");
    gboolean ellip = keyword_is(
        parent_style ? parent_style->values[NS_CSS_TEXT_OVERFLOW] : NULL, "ellipsis");
    if (ws_nowrap && !ellip)
        ns_pango_layout_set_width(layout, -1);
    else
        ns_pango_layout_set_width(layout, (int)(content_width * NS_PANGO_SCALE));
    ns_pango_layout_set_wrap(layout, ns_paint_wrap_mode_for(parent_style));
    if (box->inline_atomics && box->inline_atomics->len > 0)
        inline_apply_text_align(layout, parent_style);
    if (!(box->inline_atomics && box->inline_atomics->len > 0))
        ns_paint_apply_css_line_spacing(layout, parent_style);
    {
        double ti = ns_text_indent_px(parent_style, content_width);
        if (ti > 0) ns_pango_layout_set_indent(layout, (int)(ti * NS_PANGO_SCALE));
    }
    if (ellip)
        ns_pango_layout_set_ellipsize(layout, NS_PANGO_ELLIPSIZE_END);
    {
        const ns_css_value *lc = parent_style ? parent_style->values[NS_CSS_LINE_CLAMP] : NULL;
        if (lc && lc->kind == NS_CSS_V_LENGTH && lc->u.length.v >= 1) {
            ns_pango_layout_set_height(layout, -(int)lc->u.length.v);
            ns_pango_layout_set_ellipsize(layout, NS_PANGO_ELLIPSIZE_END);
        }
    }
    inline_layout_atomics_prepare(box, parent_style);
    NsPangoAttrList *i18n = ns_pango_attr_list_new();
    ns_paint_apply_i18n(layout, i18n, box);
    ns_paint_apply_font_features(i18n, parent_style, 0, G_MAXUINT);
    ns_inline_apply_atomic_shapes(i18n, box);
    apply_inline_spacing(i18n, parent_style, box->text);
    apply_inline_layout_attrs(i18n, box);
    ns_inline_layout_set_attrs(layout, i18n, box);
    ns_pango_attr_list_unref(i18n);

    int line_count;
    ns_pango_layout_set_text(layout, box->text, -1);
    int measured_h = 0;
    ns_pango_layout_get_pixel_size(layout, NULL, &measured_h);
    line_count = ns_pango_layout_get_line_count(layout);
    if (g_abs_static && g_abs_ph_set && box->inline_atomics) {
        double indent = ns_text_indent_px(parent_style, content_width);
        for (guint i = 0; i < box->inline_atomics->len; i++) {
            const ns_inline_atomic *a =
                &g_array_index(box->inline_atomics, ns_inline_atomic, i);
            if (!a->box) continue;
            const ns_node *dom = g_hash_table_lookup(g_abs_ph_set, a->box);
            if (!dom) continue;
            NsPangoRectangle pos;
            ns_pango_layout_index_to_pos(layout, (int)a->byte_off, &pos);
            int line_index = 0;
            ns_pango_layout_index_to_line_x(layout, (int)a->byte_off, FALSE,
                                            &line_index, NULL);
            ns_abs_static *st = g_new0(ns_abs_static, 1);
            st->run   = box;
            st->rel_x = indent + (double)pos.x / NS_PANGO_SCALE;
            st->rel_y = pango_layout_line_top(layout, line_index);
            g_hash_table_insert(g_abs_static, (gpointer)dom, st);
        }
    }
    if (line_count < 1) line_count = 1;
    double lh_default = inline_line_height(parent_style);
    if (box->parent && ns_input_is_one_line_text(box->parent->dom))
        lh_default = MAX(lh_default,
                         ns_paint_normal_line_height_px(parent_style));
    double lh_control = inline_control_line_height(box, lh_default);
    double *line_heights = g_new(double, line_count);
    for (int i = 0; i < line_count; i++) line_heights[i] = lh_control;
    if (box->inline_atomics) {
        for (guint i = 0; i < box->inline_atomics->len; i++) {
            const ns_inline_atomic *a =
                &g_array_index(box->inline_atomics, ns_inline_atomic, i);
            const ns_box *atomic = a->box;
            if (!atomic) continue;
            int line = 0;
            ns_pango_layout_index_to_line_x(layout, (int)a->byte_off,
                                         FALSE, &line, NULL);
            if (line < 0 || line >= line_count) continue;
            double outer = atomic->content_height
                + atomic->padding.top + atomic->padding.bottom
                + atomic->border.top + atomic->border.bottom
                + atomic->margin.top + atomic->margin.bottom;
            if (outer > line_heights[line]) line_heights[line] = outer;
        }
    }
    double expected = 0;
    for (int i = 0; i < line_count; i++) expected += line_heights[i];
    box->content_width  = content_width;
    box->content_height = expected;
    double ta_h = inline_textarea_total_height(box);
    if (ta_h > box->content_height) box->content_height = ta_h;
    box->first_baseline =
        (double)ns_pango_layout_get_baseline(layout) / NS_PANGO_SCALE;
    if (line_heights[0] > measured_h)
        box->first_baseline += (line_heights[0] - measured_h) / 2.0;
    if (cacheable) {
        box->inline_layout_cache_style = parent_style;
        box->inline_layout_cache_width = content_width;
        box->inline_layout_cache_height = box->content_height;
        box->inline_layout_cache_valid = TRUE;
    }

    if (box->inline_atomics && box->inline_atomics->len > 0) {
        ns_pango_layout_set_text(layout, box->text, -1);
        double *line_tops = g_new0(double, line_count);
        double *line_pango_h = g_new0(double, line_count);
        NsPangoLayoutIter *iter = ns_pango_layout_get_iter(layout);
        for (int j = 0; j < line_count; j++) {
            NsPangoRectangle logical;
            ns_pango_layout_iter_get_line_extents(iter, NULL, &logical);
            line_tops[j] = (double)logical.y / NS_PANGO_SCALE;
            line_pango_h[j] = (double)logical.height / NS_PANGO_SCALE;
            if (!ns_pango_layout_iter_next_line(iter)) break;
        }
        ns_pango_layout_iter_free(iter);
        double text_x0 = box->x;
        double ti = ns_text_indent_px(parent_style, content_width);
        if (ti < 0) text_x0 += ti;
        for (guint i = 0; i < box->inline_atomics->len; i++) {
            const ns_inline_atomic *a =
                &g_array_index(box->inline_atomics, ns_inline_atomic, i);
            if (!a->box) continue;
            NsPangoRectangle pos;
            ns_pango_layout_index_to_pos(layout, (int)a->byte_off, &pos);
            int line = 0;
            ns_pango_layout_index_to_line_x(layout, (int)a->byte_off,
                                         FALSE, &line, NULL);
            double line_y = 0;
            for (int j = 0; j < line && j < line_count; j++)
                line_y += line_heights[j];
            double within = 0;
            if (line >= 0 && line < line_count)
                within = (double)pos.y / NS_PANGO_SCALE - line_tops[line]
                       + (line_heights[line] - line_pango_h[line]) / 2.0;
            double slack = (line >= 0 && line < line_count)
                ? line_heights[line] - inline_atomic_outer_height(a->box) : 0;
            if (slack < 0) slack = 0;
            if (within < 0) within = 0;
            if (within > slack) within = slack;
            double nx = text_x0 + (double)pos.x / NS_PANGO_SCALE + a->box->margin.left;
            double ny = box->y + line_y + within + a->box->margin.top;
            shift_box_tree(a->box, nx - a->box->x, ny - a->box->y);
        }
        g_free(line_tops);
        g_free(line_pango_h);
    }

    g_free(line_heights);
    g_object_unref(layout);
}

static gboolean style_blocks_hit_testing(const ns_style *s);
static gboolean node_is_form_hit_target(const ns_node *n);

static gboolean
inline_attr_is_form_hit(ns_inline_attr_kind k)
{
    return k == NS_INLINE_INPUT_FIELD ||
           k == NS_INLINE_INPUT_FIELD_FOCUSED ||
           k == NS_INLINE_BUTTON ||
           k == NS_INLINE_CHECKBOX ||
           k == NS_INLINE_CHECKBOX_CHECKED ||
           k == NS_INLINE_RADIO ||
           k == NS_INLINE_RADIO_CHECKED;
}

static gboolean
inline_attr_is_button_hit(ns_inline_attr_kind k)
{
    return k == NS_INLINE_BUTTON ||
           k == NS_INLINE_CHECKBOX ||
           k == NS_INLINE_CHECKBOX_CHECKED ||
           k == NS_INLINE_RADIO ||
           k == NS_INLINE_RADIO_CHECKED;
}

static const ns_node *
inline_box_form_hit(const ns_box *box, double local_x, double local_y,
                    const ns_style *parent_style)
{
    if (!box) return NULL;
    if ((!box->attrs || box->attrs->len == 0) &&
        (!box->inline_atomics || box->inline_atomics->len == 0))
        return NULL;
    if (!box->text || !*box->text) return NULL;
    NsPangoLayout *layout = make_pango_layout(parent_style);
    ns_pango_layout_set_width(layout, (int)(box->content_width * NS_PANGO_SCALE));
    ns_pango_layout_set_wrap(layout, ns_paint_wrap_mode_for(parent_style));
    if (!(box->inline_atomics && box->inline_atomics->len > 0))
        ns_paint_apply_css_line_spacing(layout, parent_style);
    {
        double ti = ns_text_indent_px(parent_style, box->content_width);
        if (ti > 0) ns_pango_layout_set_indent(layout, (int)(ti * NS_PANGO_SCALE));
    }
    if (keyword_is(parent_style ? parent_style->values[NS_CSS_TEXT_OVERFLOW] : NULL,
                   "ellipsis"))
        ns_pango_layout_set_ellipsize(layout, NS_PANGO_ELLIPSIZE_END);
    ns_pango_layout_set_text(layout, box->text, -1);
    NsPangoAttrList *i18n = ns_pango_attr_list_new();
    ns_paint_apply_i18n(layout, i18n, box);
    ns_paint_apply_font_features(i18n, parent_style, 0, G_MAXUINT);
    ns_inline_apply_atomic_shapes(i18n, box);
    apply_inline_spacing(i18n, parent_style, box->text);
    apply_inline_layout_attrs(i18n, box);
    ns_inline_layout_set_attrs(layout, i18n, box);
    ns_pango_attr_list_unref(i18n);
    const ns_css_value *ta_v =
        parent_style ? parent_style->values[NS_CSS_TEXT_ALIGN] : NULL;
    gboolean rtl = ns_pango_context_get_base_dir(
        ns_pango_layout_get_context(layout)) == NS_PANGO_DIRECTION_RTL;
    if (keyword_is(ta_v, "center"))
        ns_pango_layout_set_alignment(layout, NS_PANGO_ALIGN_CENTER);
    else if (keyword_is(ta_v, "right") ||
             (keyword_is(ta_v, "end") && !rtl) ||
             (keyword_is(ta_v, "start") && rtl) ||
             (!ta_v && rtl))
        ns_pango_layout_set_alignment(layout, NS_PANGO_ALIGN_RIGHT);
    else if (keyword_is(ta_v, "justify"))
        ns_pango_layout_set_justify(layout, TRUE);
    else
        ns_pango_layout_set_alignment(layout, NS_PANGO_ALIGN_LEFT);

    int index = 0, trailing = 0;
    gboolean inside = ns_pango_layout_xy_to_index(
        layout,
        (int)(local_x * NS_PANGO_SCALE),
        (int)(local_y * NS_PANGO_SCALE),
        &index, &trailing);
    const ns_node *button_hit = NULL;
    const ns_node *field_hit = NULL;
    const ns_node *atomic_hit = NULL;
    if (inside && index >= 0) {
        gsize idx = (gsize)index;
        if (box->inline_atomics) {
            for (guint i = 0; i < box->inline_atomics->len; i++) {
                const ns_inline_atomic *a =
                    &g_array_index(box->inline_atomics, ns_inline_atomic, i);
                if (!a->box || idx < a->byte_off || idx >= a->byte_off + 3)
                    continue;
                if (node_is_form_hit_target(a->box->dom))
                    atomic_hit = a->box->dom;
            }
        }
        if (box->attrs) {
            for (guint i = 0; i < box->attrs->len; i++) {
                const ns_inline_attr *r =
                    &g_array_index(box->attrs, ns_inline_attr, i);
                if (!inline_attr_is_form_hit(r->kind)) continue;
                if (!r->dom) continue;
                const ns_style *rs = r->style ? r->style : parent_style;
                if (style_blocks_hit_testing(rs)) continue;
                if (idx < r->start || idx >= r->start + r->len) continue;
                if (inline_attr_is_button_hit(r->kind)) button_hit = r->dom;
                else if (!field_hit)             field_hit = r->dom;
            }
        }
    }
    if (box->attrs) {
        for (guint i = 0; i < box->attrs->len; i++) {
            const ns_inline_attr *r =
                &g_array_index(box->attrs, ns_inline_attr, i);
            if (!inline_attr_is_form_hit(r->kind)) continue;
            if (!r->dom) continue;
            const ns_style *rs = r->style ? r->style : parent_style;
            if (style_blocks_hit_testing(rs)) continue;
            NsPangoRectangle r0, r1;
            ns_pango_layout_index_to_pos(layout, (int)r->start, &r0);
            ns_pango_layout_index_to_pos(layout, (int)(r->start + r->len - 1), &r1);
            double bleed_x = r->box_w > 0 || r->box_h > 0 ? 0 : 10;
            double bleed_y = r->box_w > 0 || r->box_h > 0 ? 0 : 5;
            double x0 = (double)r0.x / NS_PANGO_SCALE - bleed_x;
            double y0 = (double)r0.y / NS_PANGO_SCALE - bleed_y;
            double x1 = (double)(r1.x + r1.width) / NS_PANGO_SCALE + bleed_x;
            double y1 = (double)(r0.y + r0.height) / NS_PANGO_SCALE + bleed_y;
            double css_w = inline_attr_control_width(r, box);
            if (css_w > 0) {
                x0 = (double)r0.x / NS_PANGO_SCALE;
                x1 = x0 + css_w;
            }
            if (r->box_h > 0) {
                double cy = (y0 + y1) / 2.0;
                y0 = cy - r->box_h / 2.0;
                y1 = cy + r->box_h / 2.0;
            }
            if (local_x < x0 || local_x > x1 || local_y < y0 || local_y > y1)
                continue;
            if (inline_attr_is_button_hit(r->kind)) button_hit = r->dom;
            else if (!field_hit)             field_hit = r->dom;
        }
    }
    g_object_unref(layout);
    if (atomic_hit) return atomic_hit;
    if (button_hit) return button_hit;
    return field_hit;
}

static gboolean
inline_attr_can_fragment(ns_inline_attr_kind k)
{
    switch (k) {
    case NS_INLINE_INPUT_FIELD:
    case NS_INLINE_INPUT_FIELD_FOCUSED:
    case NS_INLINE_BUTTON:
    case NS_INLINE_CHECKBOX:
    case NS_INLINE_CHECKBOX_CHECKED:
    case NS_INLINE_RADIO:
    case NS_INLINE_RADIO_CHECKED:
    case NS_INLINE_PROGRESS:
    case NS_INLINE_METER:
    case NS_INLINE_CARET:
    case NS_INLINE_SELECTION:
        return FALSE;
    default:
        return TRUE;
    }
}

static gboolean
inline_box_can_fragment_multicol(const ns_box *box, const ns_style *style,
                                 double basis)
{
    if (!box || box->kind != NS_BOX_INLINE || !box->text || !*box->text)
        return FALSE;
    if (box->inline_atomics && box->inline_atomics->len > 0)
        return FALSE;
    if (keyword_is(style ? style->values[NS_CSS_WHITE_SPACE] : NULL, "nowrap") ||
        keyword_is(style ? style->values[NS_CSS_WHITE_SPACE] : NULL, "pre"))
        return FALSE;
    if (keyword_is(style ? style->values[NS_CSS_TEXT_OVERFLOW] : NULL, "ellipsis"))
        return FALSE;
    if (style && style->values[NS_CSS_LINE_CLAMP])
        return FALSE;
    if (fabs(ns_text_indent_px(style, basis)) > 0.01)
        return FALSE;
    if (box->attrs) {
        for (guint i = 0; i < box->attrs->len; i++) {
            const ns_inline_attr *a =
                &g_array_index(box->attrs, ns_inline_attr, i);
            if (!inline_attr_can_fragment(a->kind)) return FALSE;
        }
    }
    return TRUE;
}

static gsize
range_end_clamped(gsize start, gsize len, gsize cap)
{
    gsize end = len > G_MAXSIZE - start ? G_MAXSIZE : start + len;
    return end > cap ? cap : end;
}

static ns_box *
inline_box_clone_range(const ns_box *src, gsize start, gsize end)
{
    gsize text_len = src && src->text ? strlen(src->text) : 0;
    if (!src || start >= text_len || end <= start) return NULL;
    if (end > text_len) end = text_len;

    ns_box *out = box_new_inline();
    out->dom = src->dom;
    out->style = src->style;
    out->text = g_strndup(src->text + start, end - start);

    if (src->attrs) {
        for (guint i = 0; i < src->attrs->len; i++) {
            const ns_inline_attr *a =
                &g_array_index(src->attrs, ns_inline_attr, i);
            gsize a_end = range_end_clamped(a->start, a->len, text_len);
            gsize is = MAX(a->start, start);
            gsize ie = MIN(a_end, end);
            if (ie <= is) continue;
            ns_inline_attr copy = *a;
            copy.start = is - start;
            copy.len = ie - is;
            g_array_append_val(out->attrs, copy);
            if (copy.bg_image_src) {
                ns_box_media *m = ns_box_media_ensure(out);
                if (!m->bg_image_src) {
                    m->bg_image_src = g_strdup(copy.bg_image_src);
                    m->bg_image = copy.bg_image;
                }
            }
        }
    }

    if (src->links) {
        for (guint i = 0; i < src->links->len; i++) {
            const ns_link_range *r =
                &g_array_index(src->links, ns_link_range, i);
            gsize r_end = range_end_clamped(r->start, r->len, text_len);
            gsize is = MAX(r->start, start);
            gsize ie = MIN(r_end, end);
            if (ie <= is) continue;
            ns_link_range copy = {
                .start = is - start,
                .len = ie - is,
                .href = r->href ? g_strdup(r->href) : NULL,
                .target = r->target ? g_strdup(r->target) : NULL,
                .dom = r->dom,
            };
            g_array_append_val(inline_links_ensure(out), copy);
        }
    }

    return out;
}

static NsPangoLayout *
inline_box_layout_for_multicol(const ns_box *box, double content_width,
                               const ns_style *parent_style)
{
    NsPangoLayout *layout = make_pango_layout(parent_style);
    ns_pango_layout_set_width(layout, (int)(content_width * NS_PANGO_SCALE));
    ns_pango_layout_set_wrap(layout, ns_paint_wrap_mode_for(parent_style));
    if (!(box->inline_atomics && box->inline_atomics->len > 0))
        ns_paint_apply_css_line_spacing(layout, parent_style);
    ns_pango_layout_set_text(layout, box->text, -1);

    NsPangoAttrList *i18n = ns_pango_attr_list_new();
    ns_paint_apply_i18n(layout, i18n, box);
    ns_paint_apply_font_features(i18n, parent_style, 0, G_MAXUINT);
    apply_inline_spacing(i18n, parent_style, box->text);
    apply_inline_layout_attrs(i18n, box);
    ns_inline_layout_set_attrs(layout, i18n, box);
    ns_pango_attr_list_unref(i18n);
    return layout;
}

static gboolean
layout_multicol_single_inline(ns_box *box, double inner_x, double inner_y,
                              double col_w, double col_gap, int n_cols,
                              const ns_style *child_inherited,
                              double *cursor_y)
{
    ns_box *src = box ? box->first_child : NULL;
    if (!src || src->next_sibling || n_cols < 2)
        return FALSE;
    if (!inline_box_can_fragment_multicol(src, child_inherited, col_w))
        return FALSE;

    NsPangoLayout *layout = inline_box_layout_for_multicol(src, col_w,
                                                         child_inherited);
    int line_count = ns_pango_layout_get_line_count(layout);
    if (line_count <= 1) {
        g_object_unref(layout);
        return FALSE;
    }

    GPtrArray *fragments = g_ptr_array_new();
    int per_col = (line_count + n_cols - 1) / n_cols;
    gsize text_len = strlen(src->text);
    for (int col = 0; col < n_cols; col++) {
        int first_line = col * per_col;
        int after_line = first_line + per_col;
        if (first_line >= line_count) break;
        if (after_line > line_count) after_line = line_count;
        NsPangoLayoutLine *first =
            ns_pango_layout_get_line_readonly(layout, first_line);
        NsPangoLayoutLine *after =
            after_line < line_count
            ? ns_pango_layout_get_line_readonly(layout, after_line) : NULL;
        if (!first) continue;
        gsize start = (gsize)first->start_index;
        gsize end = after ? (gsize)after->start_index : text_len;
        ns_box *frag = inline_box_clone_range(src, start, end);
        if (!frag) continue;
        frag->x = inner_x + col * (col_w + col_gap);
        frag->y = inner_y;
        layout_box(frag, col_w, child_inherited);
        g_ptr_array_add(fragments, frag);
    }
    g_object_unref(layout);

    if (fragments->len < 2) {
        for (guint i = 0; i < fragments->len; i++)
            ns_box_free(g_ptr_array_index(fragments, i));
        g_ptr_array_free(fragments, TRUE);
        return FALSE;
    }

    box->first_child = NULL;
    box->last_child = NULL;
    for (guint i = 0; i < fragments->len; i++) {
        ns_box *frag = g_ptr_array_index(fragments, i);
        frag->parent = NULL;
        frag->next_sibling = NULL;
        box_append_child(box, frag);
    }
    g_ptr_array_free(fragments, TRUE);
    src->parent = NULL;
    src->next_sibling = NULL;
    ns_box_free(src);

    double max_h = 0;
    for (ns_box *c = box->first_child; c; c = c->next_sibling) {
        if (c->content_height > max_h) max_h = c->content_height;
    }
    box->content_width = col_w * n_cols + col_gap * (n_cols - 1);
    *cursor_y = inner_y + max_h;
    return TRUE;
}

static const char *
overflow_axis_keyword(const ns_style *s, ns_css_prop axis)
{
    return ns_style_overflow_keyword(s, axis);
}

static gboolean
overflow_kw_clips(const char *ov)
{
    return ov && (g_ascii_strcasecmp(ov, "hidden") == 0 ||
                  g_ascii_strcasecmp(ov, "clip")   == 0 ||
                  g_ascii_strcasecmp(ov, "auto")   == 0 ||
                  g_ascii_strcasecmp(ov, "scroll") == 0);
}

static gboolean
overflow_kw_scrolls(const char *ov)
{
    return ov && (g_ascii_strcasecmp(ov, "auto")   == 0 ||
                  g_ascii_strcasecmp(ov, "scroll") == 0);
}

static gboolean
box_clips_children(const ns_box *b)
{
    if (!b || !b->style) return FALSE;
    if (b->kind != NS_BOX_BLOCK && b->kind != NS_BOX_TABLE_CAPTION &&
        b->kind != NS_BOX_TABLE_CELL) return FALSE;
    return overflow_kw_clips(overflow_axis_keyword(b->style, NS_CSS_OVERFLOW_X)) ||
           overflow_kw_clips(overflow_axis_keyword(b->style, NS_CSS_OVERFLOW_Y));
}

static gboolean
box_padding_contains(const ns_box *b, double x, double y)
{
    double x0 = b->x + b->margin.left + b->border.left;
    double y0 = b->y + b->margin.top  + b->border.top;
    double x1 = x0 + b->content_width + b->padding.left + b->padding.right;
    double y1 = y0 + b->content_height + b->padding.top + b->padding.bottom;
    return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

gboolean
ns_box_clips_out_point(const ns_box *b, double x, double y)
{
    return box_clips_children(b) && !box_padding_contains(b, x, y);
}

static gboolean
box_border_contains(const ns_box *b, double x, double y)
{
    double x0 = b->x + b->margin.left;
    double y0 = b->y + b->margin.top;
    double x1 = x0 + b->border.left + b->padding.left + b->content_width +
                b->padding.right + b->border.right;
    double y1 = y0 + b->border.top + b->padding.top + b->content_height +
                b->padding.bottom + b->border.bottom;
    return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

static gboolean
node_is_form_hit_target(const ns_node *n)
{
    if (!n || n->kind != NS_NODE_ELEMENT || !n->name) return FALSE;
    return strcmp(n->name, "button") == 0 ||
           strcmp(n->name, "input") == 0 ||
           strcmp(n->name, "select") == 0 ||
           strcmp(n->name, "textarea") == 0;
}

static gboolean
style_visibility_hidden(const ns_style *s)
{
    const char *vis = ns_style_keyword(s, NS_CSS_VISIBILITY);
    return vis && (strcmp(vis, "hidden") == 0 || strcmp(vis, "collapse") == 0);
}

static gboolean
style_blocks_hit_testing(const ns_style *s)
{
    return s && (ns_css_keyword_is(s->values[NS_CSS_POINTER_EVENTS], "none") ||
                 style_visibility_hidden(s));
}

static gboolean
box_blocks_hit_testing(const ns_box *b)
{
    return b && style_blocks_hit_testing(b->style);
}

static int
hit_box_stack_key(const ns_box *b)
{
    if (!b || !b->style) return 0;
    const ns_css_value *p = b->style->values[NS_CSS_POSITION];
    if (!p || p->kind != NS_CSS_V_KEYWORD || !p->u.keyword) return 0;
    const char *kw = p->u.keyword;
    if (strcmp(kw, "relative") && strcmp(kw, "absolute") &&
        strcmp(kw, "fixed") && strcmp(kw, "sticky")) return 0;
    const ns_css_value *v = b->style->values[NS_CSS_Z_INDEX];
    if (!v || v->kind != NS_CSS_V_LENGTH) return 0;
    return (int)v->u.length.v;
}

typedef struct {
    const ns_box *box;
    int          key;
    guint        order;
} hit_stack_entry;

static int
hit_stack_cmp(const void *a, const void *b)
{
    const hit_stack_entry *pa = a, *pb = b;
    if (pa->key != pb->key) return pa->key < pb->key ? -1 : 1;
    if (pa->order != pb->order) return pa->order < pb->order ? -1 : 1;
    return 0;
}

static const ns_box **
hit_children_stacked(const ns_box *parent, guint *out_n)
{
    guint n = 0;
    gboolean need = FALSE;
    for (const ns_box *c = parent->first_child; c; c = c->next_sibling) {
        if (hit_box_stack_key(c) != 0) need = TRUE;
        n++;
    }
    if (!need || n == 0) { *out_n = 0; return NULL; }
    hit_stack_entry *e = g_new(hit_stack_entry, n);
    guint i = 0;
    for (const ns_box *c = parent->first_child; c; c = c->next_sibling) {
        e[i].box = c;
        e[i].key = hit_box_stack_key(c);
        e[i].order = i;
        i++;
    }
    qsort(e, n, sizeof(*e), hit_stack_cmp);
    const ns_box **arr = g_new(const ns_box *, n);
    for (i = 0; i < n; i++) arr[i] = e[i].box;
    g_free(e);
    *out_n = n;
    return arr;
}

static gboolean box_hit_untransform_point(const ns_box *b, double *x,
                                          double *y);

static void
inline_atomic_hit_point(const ns_box *owner, const ns_inline_atomic *atomic,
                        double x, double y, double *child_x, double *child_y)
{
    double dx = owner->x + atomic->owner_offset_x - atomic->box->x;
    double dy = owner->y + atomic->owner_offset_y - atomic->box->y;
    *child_x = x - dx;
    *child_y = y - dy;
}

static const ns_node *
ns_form_hit_walk(const ns_box *box, double x, double y,
                 const ns_style *inherited)
{
    if (!box) return NULL;
    hit_enter_box(box, &x, &y);
    if (!box_hit_untransform_point(box, &x, &y)) return NULL;
    const ns_style *child_inherited = box->style ? box->style : inherited;
    const ns_node *self_hit = NULL;
    if (node_is_form_hit_target(box->dom) &&
        box_border_contains(box, x, y) &&
        !box_blocks_hit_testing(box))
        self_hit = box->dom;
    if (box->kind == NS_BOX_INLINE) {
        const ns_node *m = inline_box_form_hit(
            box, x - box->x, y - box->y, child_inherited);
        if (m) return m;
    }
    if (box_clips_children(box) && !box_padding_contains(box, x, y))
        return NULL;
    if (ns_paint_3d_registered(box)) return self_hit;
    double cx = x + box->scroll_x;
    double cy = y + box->scroll_y;
    const ns_node *best = NULL;
    guint sn = 0;
    const ns_box **stacked = hit_children_stacked(box, &sn);
    if (stacked) {
        for (guint i = 0; i < sn; i++) {
            const ns_node *m = ns_form_hit_walk(stacked[i], cx, cy, child_inherited);
            if (m) best = m;
        }
        g_free(stacked);
    } else {
        for (const ns_box *c = box->first_child; c; c = c->next_sibling) {
            const ns_node *m = ns_form_hit_walk(c, cx, cy, child_inherited);
            if (m) best = m;
        }
    }
    if (box->inline_atomics)
        for (guint i = 0; i < box->inline_atomics->len; i++) {
            const ns_inline_atomic *atomic =
                &g_array_index(box->inline_atomics, ns_inline_atomic, i);
            const ns_box *ab = atomic->box;
            if (!ab) continue;
            double ax, ay;
            inline_atomic_hit_point(box, atomic, cx, cy, &ax, &ay);
            const ns_node *m = ns_form_hit_walk(ab, ax, ay, child_inherited);
            if (m) best = m;
        }
    return best ? best : self_hit;
}

static double
measure_min_width(ns_box *box, const ns_style *parent_style);
static double
measure_min_content_width(ns_box *box, const ns_style *parent_style);
static void
table_border_spacing(const ns_style *s, double *hsp, double *vsp);

static int
float_side_of(const ns_style *s)
{
    if (!s) return -1;
    const ns_css_value *v = s->values[NS_CSS_FLOAT];
    if (!v || v->kind != NS_CSS_V_KEYWORD || !v->u.keyword) return -1;
    if (strcmp(v->u.keyword, "left") == 0) return 0;
    if (strcmp(v->u.keyword, "right") == 0) return 1;
    return -1;
}

static int
multicol_distributable_children(const ns_box *box)
{
    int n = 0;
    for (const ns_box *c = box->first_child; c; c = c->next_sibling) {
        if (c->kind != NS_BOX_BLOCK && c->kind != NS_BOX_TABLE) continue;
        if (style_is_absolute_or_fixed(c->style)) continue;
        if (float_side_of(c->style) >= 0) continue;
        if (++n >= 2) break;
    }
    return n;
}

static ns_box *
multicol_column_host(ns_box *box)
{
    ns_box *only = NULL;
    for (ns_box *c = box->first_child; c; c = c->next_sibling) {
        if (c->kind != NS_BOX_BLOCK) continue;
        if (style_is_absolute_or_fixed(c->style)) continue;
        if (float_side_of(c->style) >= 0) continue;
        if (only) return NULL;
        only = c;
    }
    if (!only || multicol_distributable_children(only) < 2) return NULL;
    return only;
}

static int
clear_kind_of(const ns_style *s)
{
    if (!s) return 0;
    const ns_css_value *v = s->values[NS_CSS_CLEAR];
    if (!v || v->kind != NS_CSS_V_KEYWORD || !v->u.keyword) return 0;
    if (strcmp(v->u.keyword, "left") == 0) return 1;
    if (strcmp(v->u.keyword, "right") == 0) return 2;
    if (strcmp(v->u.keyword, "both") == 0) return 3;
    return 0;
}

typedef struct float_ref {
    ns_box *box;
    int side;
    double top, bottom;
    double outer_w;
} float_ref;

static gboolean
overflow_establishes_bfc(const ns_style *s)
{
    if (!s) return FALSE;
    const ns_css_value *values[] = {
        s->values[NS_CSS_OVERFLOW],
        s->values[NS_CSS_OVERFLOW_X],
        s->values[NS_CSS_OVERFLOW_Y],
    };
    for (guint i = 0; i < G_N_ELEMENTS(values); i++) {
        const ns_css_value *v = values[i];
        if (!v || v->kind != NS_CSS_V_KEYWORD || !v->u.keyword) continue;
        if (strcmp(v->u.keyword, "hidden") == 0 ||
            strcmp(v->u.keyword, "auto") == 0 ||
            strcmp(v->u.keyword, "scroll") == 0)
            return TRUE;
    }
    return FALSE;
}

static gboolean
box_establishes_bfc(const ns_box *box)
{
    if (!box || !box->parent) return TRUE;
    if (box->kind == NS_BOX_TABLE_CELL || box->kind == NS_BOX_TABLE ||
        box->kind == NS_BOX_TABLE_CAPTION)
        return TRUE;
    if (float_side_of(box->style) >= 0 ||
        style_is_absolute_or_fixed(box->style) ||
        style_is_multicol(box->style) ||
        style_is_flex_container(box->style) ||
        style_is_grid_container(box->style))
        return TRUE;
    if (box->is_rendered_legend ||
        ns_node_is_element_named(box->dom, "fieldset"))
        return TRUE;
    const ns_css_value *d = box->style
        ? box->style->values[NS_CSS_DISPLAY] : NULL;
    if (keyword_is(d, "flow-root") || keyword_is(d, "inline-block") ||
        keyword_is(d, "inline-table"))
        return TRUE;
    return style_is_block_level(box->style) &&
           overflow_establishes_bfc(box->style);
}

static void
collect_escaping_floats(ns_box *box, GArray *floats, int depth)
{
    if (!box || !floats || depth >= NS_LAYOUT_MAX_DEPTH) return;
    int side = float_side_of(box->style);
    if (side >= 0) {
        float_ref ref = {
            .box = box,
            .side = side,
            .top = box->y - box->margin.top,
            .bottom = box->y + box->content_height
                + box->padding.top + box->padding.bottom
                + box->border.top + box->border.bottom
                + box->margin.bottom,
            .outer_w = box->content_width
                + box->padding.left + box->padding.right
                + box->border.left + box->border.right
                + box->margin.left + box->margin.right,
        };
        g_array_append_val(floats, ref);
        return;
    }
    if (box_establishes_bfc(box)) return;
    for (ns_box *child = box->first_child; child;
         child = child->next_sibling)
        collect_escaping_floats(child, floats, depth + 1);
}

static void
floats_offsets_at(const GArray *floats, double y,
                  double *left_out, double *right_out)
{
    double l = 0, r = 0;
    if (floats) {
        for (guint i = 0; i < floats->len; i++) {
            const float_ref *f = &g_array_index(floats, float_ref, i);
            if (y < f->top || y >= f->bottom) continue;
            if (f->side == 0) l += f->outer_w;
            else              r += f->outer_w;
        }
    }
    *left_out = l;
    *right_out = r;
}

static void
floats_offsets_over(const GArray *floats, double top, double bottom,
                    double *left_out, double *right_out)
{
    double l = 0, r = 0;
    floats_offsets_at(floats, top, &l, &r);
    if (floats && bottom > top) {
        for (guint i = 0; i < floats->len; i++) {
            const float_ref *f = &g_array_index(floats, float_ref, i);
            if (f->top <= top || f->top >= bottom) continue;
            double bl = 0, br = 0;
            floats_offsets_at(floats, f->top, &bl, &br);
            if (bl > l) l = bl;
            if (br > r) r = br;
        }
    }
    *left_out = l;
    *right_out = r;
}

static double
floats_clear_y(const GArray *floats, double y, int clear)
{
    if (!floats || clear == 0) return y;
    double out = y;
    for (guint i = 0; i < floats->len; i++) {
        const float_ref *f = &g_array_index(floats, float_ref, i);
        if (clear == 1 && f->side != 0) continue;
        if (clear == 2 && f->side != 1) continue;
        if (f->bottom > out) out = f->bottom;
    }
    return out;
}

static double
floats_max_bottom(const GArray *floats)
{
    double y = 0;
    if (!floats) return y;
    for (guint i = 0; i < floats->len; i++) {
        const float_ref *f = &g_array_index(floats, float_ref, i);
        if (f->bottom > y) y = f->bottom;
    }
    return y;
}

static gboolean
floats_advance_to_readable_width(const GArray *floats, double cw,
                                 double *y, double *left_out,
                                 double *right_out)
{
    if (!floats || floats->len == 0 || !y) return FALSE;
    double min_w = cw * 0.40;
    if (min_w > 260) min_w = 260;
    if (min_w < 120) min_w = 120;
    gboolean moved = FALSE;
    for (;;) {
        double left = 0, right = 0;
        floats_offsets_at(floats, *y, &left, &right);
        double avail = cw - left - right;
        if (avail >= min_w || cw <= min_w) {
            if (left_out) *left_out = left;
            if (right_out) *right_out = right;
            return moved;
        }
        double next_y = *y;
        gboolean advanced = FALSE;
        for (guint i = 0; i < floats->len; i++) {
            const float_ref *f = &g_array_index(floats, float_ref, i);
            if (f->bottom > *y && (!advanced || f->bottom < next_y)) {
                next_y = f->bottom;
                advanced = TRUE;
            }
        }
        if (!advanced || next_y <= *y) {
            if (left_out) *left_out = left;
            if (right_out) *right_out = right;
            return moved;
        }
        *y = next_y;
        moved = TRUE;
    }
}

static gboolean
replaced_size_keyword(const ns_css_value *v)
{
    return v && v->kind == NS_CSS_V_KEYWORD && v->u.keyword &&
           (strcmp(v->u.keyword, "fit-content") == 0 ||
            strcmp(v->u.keyword, "min-content") == 0 ||
            strcmp(v->u.keyword, "max-content") == 0);
}

static void
layout_image(ns_box *box, double parent_content_width)
{
    edges_from_style(box->style, parent_content_width,
                     &box->margin, &box->padding, &box->border);
    const ns_css_value *wv  = box->style ? box->style->values[NS_CSS_WIDTH]      : NULL;
    const ns_css_value *hv  = box->style ? box->style->values[NS_CSS_HEIGHT]     : NULL;
    const ns_css_value *mxw = box->style ? box->style->values[NS_CSS_MAX_WIDTH]  : NULL;
    const ns_css_value *mxh = box->style ? box->style->values[NS_CSS_MAX_HEIGHT] : NULL;
    const ns_css_value *mnw = box->style ? box->style->values[NS_CSS_MIN_WIDTH]  : NULL;
    const ns_css_value *mnh = box->style ? box->style->values[NS_CSS_MIN_HEIGHT] : NULL;

    gboolean declared_size = box->media && box->media->declared_image_size;
    gboolean placeholder_size = box->media && box->media->placeholder_image_size;
    const char *parent_flex_dir = box->parent
        ? flex_direction_of(box->parent->style) : "row";
    gboolean flex_row_item = box->parent &&
        style_is_flex_container(box->parent->style) &&
        (strcmp(parent_flex_dir, "row") == 0 ||
         strcmp(parent_flex_dir, "row-reverse") == 0);
    double pct_width_base = parent_content_width;
    if (flex_row_item && box->parent->content_width > 0)
        pct_width_base = box->parent->content_width;
    double w = -1, h = -1;
    if (wv && (wv->kind == NS_CSS_V_LENGTH || wv->kind == NS_CSS_V_CALC))
        w = length_resolve(wv, pct_width_base, -1);
    else if (height_keyword_stretches(wv)) {
        w = parent_content_width
          - box->margin.left - box->margin.right
          - box->padding.left - box->padding.right
          - box->border.left - box->border.right;
        if (w < 0) w = 0;
    }
    if (hv && (hv->kind == NS_CSS_V_LENGTH || hv->kind == NS_CSS_V_CALC)) {
        if (value_is_percent(hv)) {
            double cb_h = containing_block_definite_height(box);
            if (cb_h >= 0) {
                h = (hv->kind == NS_CSS_V_CALC)
                    ? hv->u.calc.pct / 100.0 * cb_h + hv->u.calc.px
                    : hv->u.length.v * cb_h / 100.0;
            }
        } else {
            h = resolve_used_height(box, hv, parent_content_width, -1);
        }
    } else if (height_keyword_stretches(hv)) {
        double cb_h = containing_block_definite_height(box);
        if (cb_h >= 0) {
            h = cb_h - box->margin.top - box->margin.bottom
              - box->padding.top - box->padding.bottom
              - box->border.top - box->border.bottom;
            if (h < 0) h = 0;
        }
    }

    const ns_image *img = box->media ? (const ns_image *)box->media->image : NULL;
    double density = box->media && box->media->image_density > 0
        ? box->media->image_density : 1.0;
    double nat_w = (img && img->loaded && img->natural_width > 0)
                   ? (double)img->natural_width / density : -1;
    double nat_h = (img && img->loaded && img->natural_height > 0)
                   ? (double)img->natural_height / density : -1;
    if (declared_size) {
        if (nat_w < 0 && box->content_width  > 0) nat_w = box->content_width;
        if (nat_h < 0 && box->content_height > 0) nat_h = box->content_height;
    }
    double intrinsic_ratio = nat_w > 0 && nat_h > 0 ? nat_w / nat_h : -1;
    if (box->kind == NS_BOX_SVG && box->dom) {
        ns_svg_size svg_size;
        ns_svg_intrinsic_size(box->dom, &svg_size);
        if (svg_size.has_ratio && svg_size.ratio > 0)
            intrinsic_ratio = svg_size.ratio;
    }
    gboolean ratio_with_auto = FALSE;
    double specified_ratio = box->style
        ? aspect_ratio_number(box->style->values[NS_CSS_ASPECT_RATIO], &ratio_with_auto) : -1;
    gboolean ratio_overrides = specified_ratio > 0 &&
        (!ratio_with_auto || intrinsic_ratio <= 0);
    if (ratio_overrides) intrinsic_ratio = specified_ratio;
    if (box->media)
        box->media->size_independent_of_image =
            (w >= 0 && h >= 0) || declared_size || placeholder_size;

    gboolean metadata_video =
        box->kind == NS_BOX_VIDEO && node_has_media_metadata(box->dom);
    if (nat_w < 0 && box->content_width  > 0) nat_w = box->content_width;
    if (nat_h < 0 && box->content_height > 0) nat_h = box->content_height;

    gboolean ratio_only = box->media && box->media->intrinsic_ratio_only;
    gboolean w_specified = w >= 0;
    gboolean h_specified = h >= 0;
    if (w < 0 && h < 0) {
        if (ratio_only && intrinsic_ratio > 0) {
            double cb_h = containing_block_definite_height(box);
            if (parent_content_width > 0 && cb_h >= 0) {
                w = parent_content_width; h = cb_h;
            } else if (cb_h >= 0) {
                h = cb_h; w = h * intrinsic_ratio;
            } else if (parent_content_width > 0) {
                w = parent_content_width; h = w / intrinsic_ratio;
            } else { w = 0; h = 0; }
        } else if (nat_w > 0 && nat_h > 0) {
            w = nat_w;
            h = ratio_overrides && !ratio_with_auto ? w / intrinsic_ratio : nat_h;
        } else { w = 0; h = 0; }
    } else if (w < 0) {
        w = intrinsic_ratio > 0 ? h * intrinsic_ratio : h;
    } else if (h < 0) {
        h = intrinsic_ratio > 0 ? w / intrinsic_ratio : w;
    }
    if (metadata_video && h <= 0 && w > 0 && nat_w > 0 && nat_h > 0)
        h = w * (nat_h / nat_w);
    if (metadata_video && w <= 0 && h > 0 && nat_w > 0 && nat_h > 0)
        w = h * (nat_w / nat_h);

    double max_w = length_resolve(mxw, pct_width_base, -1);
    double max_h = resolve_used_height(box, mxh, parent_content_width, -1);
    double min_w = length_resolve(mnw, pct_width_base, -1);
    double min_h = resolve_used_height(box, mnh, parent_content_width, -1);
    double keyword_w = intrinsic_ratio > 0 && h_specified ? h * intrinsic_ratio : nat_w;
    if (max_w < 0 && replaced_size_keyword(mxw) && keyword_w >= 0) max_w = keyword_w;
    if (min_w < 0 && replaced_size_keyword(mnw) && keyword_w >= 0) min_w = keyword_w;

    if (max_w >= 0 && w > max_w) {
        if (h > 0 && w > 0 && !h_specified) h *= max_w / w;
        w = max_w;
    }
    if (max_h >= 0 && h > max_h) {
        if (w > 0 && h > 0 && !w_specified) w *= max_h / h;
        h = max_h;
    }
    if (min_w >= 0 && w < min_w) {
        if (h > 0 && w > 0 && !h_specified && intrinsic_ratio > 0) h = min_w / intrinsic_ratio;
        w = min_w;
    }
    if (min_h >= 0 && h < min_h) {
        if (w > 0 && h > 0 && !w_specified && intrinsic_ratio > 0) w = min_h * intrinsic_ratio;
        h = min_h;
    }

    box->content_width = w;
    box->content_height = h;
}

static double
pango_layout_line_top(NsPangoLayout *layout, int line_index)
{
    NsPangoLayoutIter *iter = ns_pango_layout_get_iter(layout);
    double top = 0;
    for (int j = 0; ; j++) {
        NsPangoRectangle logical;
        ns_pango_layout_iter_get_line_extents(iter, NULL, &logical);
        top = (double)logical.y / NS_PANGO_SCALE;
        if (j >= line_index || !ns_pango_layout_iter_next_line(iter)) break;
    }
    ns_pango_layout_iter_free(iter);
    return top;
}

static gboolean
inline_atomic_needs_layout(const ns_box *ab)
{
    if (!ab) return FALSE;
    return !(g_abs_ph_set && g_hash_table_contains(g_abs_ph_set, ab));
}

static double
inline_atomic_measure_basis(const ns_box *box)
{
    const ns_css_value *wv = box && box->style
        ? box->style->values[NS_CSS_WIDTH] : NULL;
    gboolean replaced = box && (box->kind == NS_BOX_IMAGE ||
                                box->kind == NS_BOX_VIDEO ||
                                box->kind == NS_BOX_SVG);
    if (value_is_percent(wv) && !replaced) {
        double content = measure_natural_width((ns_box *)box, box->style);
        if (content >= 0) return content;
    }
    double basis = ns_css_container_w();
    if (!(basis > 0)) {
        for (const ns_box *p = box ? box->parent : NULL; p; p = p->parent) {
            if (p->content_width > 0) {
                basis = p->content_width;
                break;
            }
        }
    }
    if (!(basis > 0)) basis = ns_css_viewport_w();
    if (!(basis > 0)) basis = 1000;
    if (basis < 32) basis = 32;
    if (basis > 1600) basis = 1600;
    return basis;
}

static double
grid_natural_width(ns_box *box, const ns_style *child_style)
{
    const ns_css_value *fv = box->style->values[NS_CSS_GRID_AUTO_FLOW];
    if (fv && fv->kind == NS_CSS_V_KEYWORD && fv->u.keyword &&
        strstr(fv->u.keyword, "column")) return -1;
    const ns_css_value *cv = box->style->values[NS_CSS_GRID_TEMPLATE_COLUMNS];
    if (!cv || cv->kind != NS_CSS_V_TRACKS || cv->u.tracks.n <= 0 ||
        cv->u.tracks.subgrid ||
        cv->u.tracks.auto_repeat != NS_CSS_AUTO_REPEAT_NONE) return -1;
    const ns_css_tracks *tk = &cv->u.tracks;
    int n = tk->n < NS_CSS_TRACKS_MAX ? tk->n : NS_CSS_TRACKS_MAX;
    double col[NS_CSS_TRACKS_MAX];
    for (int i = 0; i < n; i++) col[i] = 0;
    int slot = 0;
    for (ns_box *c = box->first_child; c; c = c->next_sibling) {
        if (style_is_absolute_or_fixed(c->style)) continue;
        double w = measure_natural_width(c, child_style);
        if (c->style) {
            ns_edges m = {0}, pd = {0}, bd = {0};
            edges_from_style(c->style, 0, &m, &pd, &bd);
            w += m.left + m.right + pd.left + pd.right + bd.left + bd.right;
        }
        int t = slot % n;
        if (w > col[t]) col[t] = w;
        slot++;
    }
    if (slot == 0) return -1;
    double sum = 0;
    for (int i = 0; i < n; i++) {
        double track = col[i];
        if (tk->tracks[i].kind == NS_CSS_TRACK_PX)
            track = tk->tracks[i].v;
        else if (tk->tracks[i].has_min &&
                 tk->tracks[i].min_kind == NS_CSS_TRACK_PX &&
                 tk->tracks[i].min_v > track)
            track = tk->tracks[i].min_v;
        sum += track;
    }
    if (n > 1) {
        const ns_css_value *gv = box->style->values[NS_CSS_COLUMN_GAP];
        if (!gv || (gv->kind != NS_CSS_V_LENGTH && gv->kind != NS_CSS_V_CALC))
            gv = box->style->values[NS_CSS_GAP];
        if (gv && (gv->kind == NS_CSS_V_LENGTH || gv->kind == NS_CSS_V_CALC)) {
            double gap = length_resolve(gv, 0, 0);
            if (gap > 0) sum += gap * (n - 1);
        }
    }
    return sum;
}

static guint
table_column_count(const ns_box *box)
{
    guint max_cols = 0;
    for (ns_box *row = box->first_child; row; row = row->next_sibling) {
        if (row->kind != NS_BOX_TABLE_ROW) continue;
        guint c = 0;
        for (ns_box *cell = row->first_child; cell; cell = cell->next_sibling) {
            c += cell->colspan > 0 ? (guint)cell->colspan : 1;
            if (c > NS_TABLE_MAX_COLS) { c = NS_TABLE_MAX_COLS; break; }
        }
        if (c > max_cols) max_cols = c;
    }
    return max_cols;
}

static void
table_widen_columns(double *cols, guint max_cols, guint col, int span,
                    double outer)
{
    if (span < 1) span = 1;
    double per = outer / (double)span;
    for (int i = 0; i < span && col + (guint)i < max_cols; i++)
        if (per > cols[col + (guint)i]) cols[col + (guint)i] = per;
}

static double
table_cell_definite_width(const ns_style *s, ns_css_prop prop, double h_extra)
{
    const ns_css_value *v = s ? s->values[prop] : NULL;
    if (!v || !(v->kind == NS_CSS_V_LENGTH || v->kind == NS_CSS_V_CALC) ||
        value_is_percent(v))
        return -1;
    double w = length_resolve(v, 0, -1);
    return w >= 0 ? w + h_extra : -1;
}

static double
table_cell_clamp(const ns_box *cell, double h_extra, double w)
{
    const ns_style *s = cell ? cell->style : NULL;
    double max_w = table_cell_definite_width(s, NS_CSS_MAX_WIDTH, h_extra);
    double min_w = table_cell_definite_width(s, NS_CSS_MIN_WIDTH, h_extra);
    if (max_w >= 0 && w > max_w) w = max_w;
    if (min_w >= 0 && w < min_w) w = min_w;
    return w;
}

static double
table_intrinsic_width(ns_box *box, const ns_style *inherited, gboolean min)
{
    double captions = 0;
    for (ns_box *c = box->first_child; c; c = c->next_sibling) {
        if (c->kind != NS_BOX_TABLE_CAPTION) continue;
        const ns_style *cs = c->style ? c->style : inherited;
        double w = min ? measure_min_width(c, cs) : measure_natural_width(c, cs);
        ns_edges m = {0}, pd = {0}, bd = {0};
        edges_from_style(c->style, 0, &m, &pd, &bd);
        w += m.left + m.right + pd.left + pd.right + bd.left + bd.right;
        if (w > captions) captions = w;
    }
    guint max_cols = table_column_count(box);
    if (max_cols == 0) return captions;
    double *cols = g_new0(double, max_cols);
    for (ns_box *row = box->first_child; row; row = row->next_sibling) {
        if (row->kind != NS_BOX_TABLE_ROW) continue;
        guint col = 0;
        for (ns_box *cell = row->first_child; cell && col < max_cols;
             cell = cell->next_sibling) {
            int span = cell->colspan > 0 ? cell->colspan : 1;
            const ns_style *cs = cell->style ? cell->style : inherited;
            ns_edges m = {0}, pd = {0}, bd = {0};
            edges_from_style(cell->style, 0, &m, &pd, &bd);
            double extra = m.left + m.right + pd.left + pd.right +
                           bd.left + bd.right;
            double content_min = measure_min_content_width(cell, cs);
            double w = min ? content_min : measure_natural_width(cell, cs);
            const ns_css_value *wv = cell->style
                ? cell->style->values[NS_CSS_WIDTH] : NULL;
            if (wv && (wv->kind == NS_CSS_V_LENGTH || wv->kind == NS_CSS_V_CALC) &&
                !value_is_percent(wv)) {
                double e = length_resolve(wv, 0, -1);
                if (e >= 0) w = e > content_min ? e : content_min;
            }
            table_widen_columns(cols, max_cols, col, span,
                                table_cell_clamp(cell, extra, w + extra));
            col += (guint)span;
        }
    }
    if (box->table_col_hints) {
        guint col = 0;
        for (guint i = 0; i < box->table_col_hints->len && col < max_cols; i++) {
            ns_table_col_hint *hint =
                &g_array_index(box->table_col_hints, ns_table_col_hint, i);
            int hspan = hint->span > 0 ? hint->span : 1;
            const ns_css_value *wv = hint->style
                ? hint->style->values[NS_CSS_WIDTH] : NULL;
            if (wv && wv->kind == NS_CSS_V_LENGTH &&
                wv->u.length.unit != NS_CSS_UNIT_PERCENT) {
                double w = length_resolve(wv, 0, -1);
                if (w >= 0) table_widen_columns(cols, max_cols, col, hspan, w);
            }
            col += (guint)hspan;
        }
    }
    double hsp = 0, vsp = 0;
    table_border_spacing(box->style, &hsp, &vsp);
    double sum = (double)(max_cols + 1) * hsp;
    for (guint i = 0; i < max_cols; i++) sum += cols[i];
    g_free(cols);
    return sum > captions ? sum : captions;
}

static double
measure_natural_width(ns_box *box, const ns_style *parent_style)
{
    if (!box) return 0;
    if (box->kind == NS_BOX_INLINE) {
        if (box->text && *box->text && ns_css_writing_mode(parent_style) &&
            !(box->inline_atomics && box->inline_atomics->len > 0)) {
            double thickness = 0, length = 0;
            ns_vertical_measure(box, parent_style, &thickness, &length);
            return thickness;
        }
        if (!box->text || !*box->text) {
            if (!box->inline_atomics || box->inline_atomics->len == 0)
                return 0;
            double sum = 0;
            for (guint ai = 0; ai < box->inline_atomics->len; ai++) {
                ns_box *ab = g_array_index(box->inline_atomics,
                                           ns_inline_atomic, ai).box;
                if (!ab) continue;
                if (inline_atomic_needs_layout(ab))
                    layout_box(ab, inline_atomic_measure_basis(ab),
                               parent_style);
                sum += ab->content_width +
                       ab->margin.left + ab->margin.right +
                       ab->padding.left + ab->padding.right +
                       ab->border.left + ab->border.right;
            }
            return sum;
        }
        gboolean cacheable = inline_box_measure_cacheable(box);
        if (cacheable && box->inline_natural_cache_valid &&
            box->inline_natural_cache_style == parent_style)
            return box->inline_natural_cache_width;
        NsPangoLayout *layout = make_pango_layout(parent_style);
        ns_pango_layout_set_width(layout, -1);
        if (box->inline_atomics) {
            for (guint ai = 0; ai < box->inline_atomics->len; ai++) {
                ns_box *ab = g_array_index(box->inline_atomics, ns_inline_atomic, ai).box;
                if (inline_atomic_needs_layout(ab))
                    layout_box(ab, inline_atomic_measure_basis(ab), parent_style);
            }
        }
        ns_pango_layout_set_text(layout, box->text, -1);
        NsPangoAttrList *i18n = ns_pango_attr_list_new();
        ns_paint_apply_i18n(layout, i18n, box);
        ns_paint_apply_font_features(i18n, parent_style, 0, G_MAXUINT);
        ns_inline_apply_atomic_shapes(i18n, box);
        apply_inline_spacing(i18n, parent_style, box->text);
        apply_inline_layout_attrs(i18n, box);
        ns_inline_layout_set_attrs(layout, i18n, box);
        ns_pango_attr_list_unref(i18n);
        NsPangoRectangle logical;
        ns_pango_layout_get_extents(layout, NULL, &logical);
        double slack = 0;
        const ns_css_value *lsv = parent_style
            ? parent_style->values[NS_CSS_LETTER_SPACING] : NULL;
        if (lsv && lsv->kind == NS_CSS_V_LENGTH &&
            lsv->u.length.unit == NS_CSS_UNIT_PX && lsv->u.length.v > 0)
            slack = lsv->u.length.v;
        double pw = ceil((double)logical.width / NS_PANGO_SCALE + slack);
        if (box->inline_atomics) {
            for (guint ai = 0; ai < box->inline_atomics->len; ai++) {
                const ns_inline_atomic *a = &g_array_index(
                    box->inline_atomics, ns_inline_atomic, ai);
                if (!a->box) continue;
                NsPangoRectangle pos;
                ns_pango_layout_index_to_pos(layout, (int)a->byte_off, &pos);
                double end = (double)(pos.x + pos.width) / NS_PANGO_SCALE;
                if (pw < end) pw = ceil(end);
            }
        }
        g_object_unref(layout);
        if (cacheable) {
            box->inline_natural_cache_style = parent_style;
            box->inline_natural_cache_width = pw;
            box->inline_natural_cache_valid = TRUE;
        }
        return pw;
    }
    if (box->kind == NS_BOX_IMAGE || box->kind == NS_BOX_VIDEO ||
        box->kind == NS_BOX_SVG) {
        const ns_css_value *wv = box->style
            ? box->style->values[NS_CSS_WIDTH] : NULL;
        if (wv && (wv->kind == NS_CSS_V_LENGTH ||
                   wv->kind == NS_CSS_V_CALC)) {
            double styled = length_resolve(wv, inline_atomic_measure_basis(box), -1);
            if (styled >= 0) return styled;
        }
        return box->content_width > 0 ? box->content_width : 200;
    }
    if (box->kind == NS_BOX_TEXT) {
        return box->content_width > 0 ? box->content_width : 0;
    }
    {
        const ns_css_value *wv = box->style ? box->style->values[NS_CSS_WIDTH] : NULL;
        if (wv && wv->kind == NS_CSS_V_LENGTH &&
            (wv->u.length.unit == NS_CSS_UNIT_PX ||
             wv->u.length.unit == NS_CSS_UNIT_NUMBER) &&
            wv->u.length.v >= 0) {
            double w = wv->u.length.v;
            if (flex_box_is_border_box(box)) {
                ns_edges m = {0}, pd = {0}, bd = {0};
                edges_from_style(box->style, 0, &m, &pd, &bd);
                w -= pd.left + pd.right + bd.left + bd.right;
                if (w < 0) w = 0;
            }
            return width_contribution_keyword_limits(box, w, parent_style,
                                                     TRUE);
        }
        if (keyword_is(wv, "min-content"))
            return width_contribution_keyword_limits(
                box, measure_min_content_width(box, parent_style),
                parent_style, TRUE);
    }
    return width_contribution_keyword_limits(
        box, measure_max_content_width(box, parent_style), parent_style, TRUE);
}

static double
width_contribution_keyword_limits(ns_box *box, double w,
                                  const ns_style *parent_style,
                                  gboolean max_content)
{
    if (!box->style || (box->kind != NS_BOX_BLOCK && box->kind != NS_BOX_TABLE))
        return w;
    const ns_css_value *mxw = box->style->values[NS_CSS_MAX_WIDTH];
    const ns_css_value *mnw = box->style->values[NS_CSS_MIN_WIDTH];
    if (size_keyword_is_intrinsic(mxw)) {
        gboolean use_max = keyword_is(mxw, "max-content") ||
                           (max_content && keyword_is(mxw, "fit-content"));
        double m = use_max ? measure_max_content_width(box, parent_style)
                           : measure_min_content_width(box, parent_style);
        if (m >= 0 && w > m) w = m;
    }
    if (size_keyword_is_intrinsic(mnw)) {
        gboolean use_max = keyword_is(mnw, "max-content") ||
                           (max_content && keyword_is(mnw, "fit-content"));
        double m = use_max ? measure_max_content_width(box, parent_style)
                           : measure_min_content_width(box, parent_style);
        if (m >= 0 && w < m) w = m;
    }
    return w;
}

static double
measure_max_content_width(ns_box *box, const ns_style *parent_style)
{
    if (box->kind == NS_BOX_INLINE || box->kind == NS_BOX_IMAGE ||
        box->kind == NS_BOX_VIDEO || box->kind == NS_BOX_SVG ||
        box->kind == NS_BOX_TEXT)
        return measure_natural_width(box, parent_style);
    if (style_contains_inline_size(box->style)) return 0;
    const ns_style *child_style = box->style ? box->style : parent_style;
    if (box->kind == NS_BOX_TABLE)
        return table_intrinsic_width(box, child_style, FALSE);
    if (box->style && style_is_grid_container(box->style)) {
        double gw = grid_natural_width(box, child_style);
        if (gw > 0) return gw;
    }
    gboolean flex_row = style_is_flex_container(box->style) &&
        strncmp(flex_direction_of(box->style), "row", 3) == 0;
    double max_child = 0;
    double float_row = 0;
    double row_sum = 0;
    int flex_items = 0;
    for (ns_box *c = box->first_child; c; c = c->next_sibling) {
        double w = measure_natural_width(c, child_style);
        int fside = float_side_of(c->style);
        double outer = w;
        if (c->style) {
            ns_edges m = {0}, pd = {0}, bd = {0};
            edges_from_style(c->style, 0, &m, &pd, &bd);
            outer += m.left + m.right + pd.left + pd.right + bd.left + bd.right;
        }
        if (flex_row) {
            row_sum += outer;
            flex_items++;
        } else if (box->kind == NS_BOX_BLOCK ||
                   box->kind == NS_BOX_TABLE_CELL ||
                   box->kind == NS_BOX_TABLE_CAPTION) {
            if (fside >= 0) {
                float_row += outer;
                if (float_row > max_child) max_child = float_row;
            } else {
                if (outer > max_child) max_child = outer;
                if (w > max_child) max_child = w;
                float_row = 0;
            }
        } else {
            max_child += w;
        }
    }
    if (flex_row && flex_items > 1) {
        const ns_css_value *g = box->style->values[NS_CSS_COLUMN_GAP];
        if (!g || g->kind != NS_CSS_V_LENGTH) g = box->style->values[NS_CSS_GAP];
        double gap = (g && g->kind == NS_CSS_V_LENGTH) ? g->u.length.v : 0;
        row_sum += gap * (flex_items - 1);
    }
    return flex_row ? row_sum : max_child;
}

static double
measure_min_width(ns_box *box, const ns_style *parent_style)
{
    if (!box) return 0;
    if (box->kind == NS_BOX_INLINE) {
        if (!box->text || !*box->text) return 0;
        if (ns_css_writing_mode(parent_style) &&
            !(box->inline_atomics && box->inline_atomics->len > 0)) {
            double thickness = 0, length = 0;
            ns_vertical_measure(box, parent_style, &thickness, &length);
            return thickness;
        }
        gboolean cacheable = inline_box_measure_cacheable(box);
        if (cacheable && box->inline_min_cache_valid &&
            box->inline_min_cache_style == parent_style)
            return box->inline_min_cache_width;
        double fast = measure_inline_ascii_min_width(box, parent_style);
        if (fast >= 0) {
            if (cacheable) {
                box->inline_min_cache_style = parent_style;
                box->inline_min_cache_width = fast;
                box->inline_min_cache_valid = TRUE;
            }
            return fast;
        }
        NsPangoLayout *layout = make_pango_layout(parent_style);
        ns_pango_layout_set_width(layout, 1);
        ns_pango_layout_set_wrap(layout, NS_PANGO_WRAP_WORD);
        if (box->inline_atomics) {
            for (guint ai = 0; ai < box->inline_atomics->len; ai++) {
                ns_box *ab = g_array_index(box->inline_atomics, ns_inline_atomic, ai).box;
                if (inline_atomic_needs_layout(ab))
                    layout_box(ab, inline_atomic_measure_basis(ab), parent_style);
            }
        }
        ns_pango_layout_set_text(layout, box->text, -1);
        NsPangoAttrList *i18n = ns_pango_attr_list_new();
        ns_paint_apply_i18n(layout, i18n, box);
        ns_paint_apply_font_features(i18n, parent_style, 0, G_MAXUINT);
        ns_inline_apply_atomic_shapes(i18n, box);
        apply_inline_spacing(i18n, parent_style, box->text);
        apply_inline_layout_attrs(i18n, box);
        ns_inline_layout_set_attrs(layout, i18n, box);
        ns_pango_attr_list_unref(i18n);
        int pw, ph;
        ns_pango_layout_get_pixel_size(layout, &pw, &ph);
        g_object_unref(layout);
        if (cacheable) {
            box->inline_min_cache_style = parent_style;
            box->inline_min_cache_width = pw;
            box->inline_min_cache_valid = TRUE;
        }
        return pw;
    }
    if (box->kind == NS_BOX_IMAGE || box->kind == NS_BOX_VIDEO ||
        box->kind == NS_BOX_SVG) {
        const ns_css_value *max_width = box->style
            ? box->style->values[NS_CSS_MAX_WIDTH] : NULL;
        if (max_width && max_width->kind == NS_CSS_V_LENGTH &&
            max_width->u.length.unit == NS_CSS_UNIT_PERCENT)
            return 0;
        return box->content_width > 0 ? box->content_width : 200;
    }
    if (box->kind == NS_BOX_TEXT)
        return box->content_width > 0 ? box->content_width : 0;
    if (style_contains_inline_size(box->style)) return 0;
    if (box->style) {
        const ns_css_value *wv = box->style->values[NS_CSS_WIDTH];
        if (wv && (wv->kind == NS_CSS_V_LENGTH || wv->kind == NS_CSS_V_CALC)) {
            double w = length_resolve(wv, 0, -1);
            if (w >= 0) {
                if (ns_css_keyword_is(box->style->values[NS_CSS_BOX_SIZING],
                                      "border-box")) {
                    ns_edges m = {0}, pd = {0}, bd = {0};
                    edges_from_style(box->style, 0, &m, &pd, &bd);
                    w -= pd.left + pd.right + bd.left + bd.right;
                    if (w < 0) w = 0;
                }
                const ns_css_value *mxw = box->style->values[NS_CSS_MAX_WIDTH];
                if (mxw && (mxw->kind == NS_CSS_V_LENGTH ||
                            mxw->kind == NS_CSS_V_CALC)) {
                    double m = length_resolve(mxw, 0, -1);
                    if (m >= 0 && w > m) w = m;
                }
                const ns_css_value *mnw = box->style->values[NS_CSS_MIN_WIDTH];
                if (mnw && (mnw->kind == NS_CSS_V_LENGTH ||
                            mnw->kind == NS_CSS_V_CALC)) {
                    double m = length_resolve(mnw, 0, -1);
                    if (m >= 0 && w < m) w = m;
                }
                return width_contribution_keyword_limits(box, w, parent_style,
                                                         FALSE);
            }
        }
        if (keyword_is(wv, "max-content"))
            return width_contribution_keyword_limits(
                box, measure_max_content_width(box, parent_style),
                parent_style, FALSE);
    }
    return width_contribution_keyword_limits(
        box, measure_min_content_width(box, parent_style), parent_style,
        FALSE);
}

static double
measure_min_content_width(ns_box *box, const ns_style *parent_style)
{
    if (box->kind == NS_BOX_INLINE || box->kind == NS_BOX_IMAGE ||
        box->kind == NS_BOX_VIDEO || box->kind == NS_BOX_SVG ||
        box->kind == NS_BOX_TEXT)
        return measure_min_width(box, parent_style);
    if (style_contains_inline_size(box->style)) return 0;
    if (box->kind == NS_BOX_TABLE)
        return table_intrinsic_width(
            box, box->style ? box->style : parent_style, TRUE);
    if (box->style && style_is_grid_container(box->style)) {
        const ns_css_value *cv =
            box->style->values[NS_CSS_GRID_TEMPLATE_COLUMNS];
        if (cv && cv->kind == NS_CSS_V_TRACKS && cv->u.tracks.n > 0 &&
            !cv->u.tracks.subgrid) {
            const ns_css_tracks *tk = &cv->u.tracks;
            double sum = 0;
            gboolean all_definite = TRUE;
            for (int i = 0; i < tk->n; i++) {
                if (tk->tracks[i].kind == NS_CSS_TRACK_PX) {
                    sum += tk->tracks[i].v;
                } else if (tk->tracks[i].has_min &&
                           tk->tracks[i].min_kind == NS_CSS_TRACK_PX) {
                    sum += tk->tracks[i].min_v;
                } else {
                    all_definite = FALSE;
                    break;
                }
            }
            if (all_definite && sum > 0) {
                const ns_css_value *gv =
                    box->style->values[NS_CSS_COLUMN_GAP];
                if (!gv || !(gv->kind == NS_CSS_V_LENGTH ||
                             gv->kind == NS_CSS_V_CALC))
                    gv = box->style->values[NS_CSS_GAP];
                if (gv && (gv->kind == NS_CSS_V_LENGTH ||
                           gv->kind == NS_CSS_V_CALC) && tk->n > 1) {
                    double gap = length_resolve(gv, 0, 0);
                    if (gap > 0) sum += gap * (tk->n - 1);
                }
                return sum;
            }
        }
    }
    const ns_style *child_style = box->style ? box->style : parent_style;
    double max_child = 0;
    for (ns_box *c = box->first_child; c; c = c->next_sibling) {
        double w = measure_min_width(c, child_style);
        double outer = w;
        if (c->style) {
            ns_edges m = {0}, pd = {0}, bd = {0};
            edges_from_style(c->style, 0, &m, &pd, &bd);
            outer += m.left + m.right + pd.left + pd.right + bd.left + bd.right;
        }
        if (outer > max_child) max_child = outer;
    }
    return max_child;
}

static gboolean
table_width_from_style(const ns_style *s, double basis, double *out)
{
    const ns_css_value *wv = s ? s->values[NS_CSS_WIDTH] : NULL;
    if (!wv || !(wv->kind == NS_CSS_V_LENGTH || wv->kind == NS_CSS_V_CALC))
        return FALSE;
    double w = length_resolve(wv, basis, -1);
    if (w < 0) return FALSE;
    *out = w;
    return TRUE;
}

static void
apply_fixed_table_width(double *col_widths, gboolean *col_fixed,
                        guint max_cols, guint col, int span, double w)
{
    if (span < 1) span = 1;
    if (col >= max_cols) return;
    double per = w / (double)span;
    if (per < 0) per = 0;
    for (int i = 0; i < span && col + (guint)i < max_cols; i++) {
        guint idx = col + (guint)i;
        if (per > col_widths[idx]) col_widths[idx] = per;
        col_fixed[idx] = TRUE;
    }
}

static ns_box *
table_first_row(ns_box *box)
{
    for (ns_box *row = box ? box->first_child : NULL; row; row = row->next_sibling)
        if (row->kind == NS_BOX_TABLE_ROW) return row;
    return NULL;
}

static double
layout_fixed_table_columns(ns_box *box, double cw, guint max_cols,
                           double *col_widths, gboolean *col_fixed)
{
    if (box->table_col_hints) {
        guint col = 0;
        for (guint i = 0; i < box->table_col_hints->len && col < max_cols; i++) {
            ns_table_col_hint *hint =
                &g_array_index(box->table_col_hints, ns_table_col_hint, i);
            double w = 0;
            if (table_width_from_style(hint->style, cw, &w))
                apply_fixed_table_width(col_widths, col_fixed, max_cols,
                                        col, hint->span, w);
            col += hint->span > 0 ? (guint)hint->span : 1;
        }
    }

    ns_box *first_row = table_first_row(box);
    if (first_row) {
        guint col = 0;
        for (ns_box *cell = first_row->first_child; cell; cell = cell->next_sibling) {
            int span = cell->colspan > 0 ? cell->colspan : 1;
            double w = 0;
            if (table_width_from_style(cell->style, cw, &w)) {
                ns_edges m = {0}, pd = {0}, bd = {0};
                edges_from_style(cell->style, cw, &m, &pd, &bd);
                w += m.left + m.right + pd.left + pd.right + bd.left + bd.right;
                apply_fixed_table_width(col_widths, col_fixed, max_cols,
                                        col, span, w);
            }
            col += (guint)span;
        }
    }

    double used = 0;
    guint unset = 0;
    for (guint i = 0; i < max_cols; i++) {
        used += col_widths[i];
        if (!col_fixed[i]) unset++;
    }

    double remaining = cw - used;
    if (remaining < 0) remaining = 0;
    if (unset > 0) {
        double per = remaining / (double)unset;
        for (guint i = 0; i < max_cols; i++)
            if (!col_fixed[i]) col_widths[i] = per;
    } else if (remaining > 0 && max_cols > 0) {
        double per = remaining / (double)max_cols;
        for (guint i = 0; i < max_cols; i++)
            col_widths[i] += per;
    }

    double sum = 0;
    for (guint i = 0; i < max_cols; i++) sum += col_widths[i];
    return sum;
}

static gboolean
table_caption_bottom(const ns_box *caption)
{
    const char *side = caption && caption->style
        ? ns_style_keyword(caption->style, NS_CSS_CAPTION_SIDE) : NULL;
    return side && (strcmp(side, "bottom") == 0 ||
                    strcmp(side, "block-end") == 0);
}

static double
table_child_outer_height(const ns_box *box)
{
    return box->content_height
         + box->margin.top + box->margin.bottom
         + box->padding.top + box->padding.bottom
         + box->border.top + box->border.bottom;
}

static void
layout_table_captions(ns_box *box, gboolean bottom, double inner_x,
                      double cw, const ns_style *child_inherited,
                      double *cursor_y)
{
    for (ns_box *caption = box->first_child; caption; caption = caption->next_sibling) {
        if (caption->kind != NS_BOX_TABLE_CAPTION) continue;
        if (table_caption_bottom(caption) != bottom) continue;
        caption->x = inner_x;
        caption->y = *cursor_y;
        layout_box(caption, cw, child_inherited);
        *cursor_y += table_child_outer_height(caption);
    }
}

static void
table_border_spacing(const ns_style *s, double *hsp, double *vsp)
{
    *hsp = 0;
    *vsp = 0;
    if (!s) return;
    const ns_css_value *bc = s->values[NS_CSS_BORDER_COLLAPSE];
    if (bc && bc->kind == NS_CSS_V_KEYWORD && bc->u.keyword &&
        g_ascii_strcasecmp(bc->u.keyword, "collapse") == 0)
        return;
    const ns_css_value *bs = s->values[NS_CSS_BORDER_SPACING];
    if (bs && bs->kind == NS_CSS_V_SIZE) {
        *hsp = bs->u.size.w;
        *vsp = bs->u.size.h;
    }
}

static gboolean
table_is_collapse(const ns_style *s)
{
    const ns_css_value *bc = s ? s->values[NS_CSS_BORDER_COLLAPSE] : NULL;
    return bc && bc->kind == NS_CSS_V_KEYWORD && bc->u.keyword &&
           g_ascii_strcasecmp(bc->u.keyword, "collapse") == 0;
}

typedef struct ns_cell_pos {
    ns_box *cell;
    guint   r0, c0, cov, rsp;
} ns_cell_pos;

static void
table_collapse_borders(ns_box *box, guint max_cols)
{
    if (max_cols == 0) return;
    guint R = 0;
    for (ns_box *row = box->first_child; row; row = row->next_sibling)
        if (row->kind == NS_BOX_TABLE_ROW) R++;
    if (R == 0) return;
    if ((gsize)R > G_MAXSIZE / sizeof(ns_box *) / max_cols) return;

    ns_box **grid = g_new0(ns_box *, (gsize)R * max_cols);
    GArray *cells = g_array_new(FALSE, FALSE, sizeof(ns_cell_pos));
    guint r = 0;
    for (ns_box *row = box->first_child; row; row = row->next_sibling) {
        if (row->kind != NS_BOX_TABLE_ROW) continue;
        guint col = 0;
        for (ns_box *cell = row->first_child; cell; cell = cell->next_sibling) {
            while (col < max_cols && grid[r * max_cols + col]) col++;
            if (col >= max_cols) break;
            guint cov = cell->colspan > 0 ? (guint)cell->colspan : 1;
            guint rsp = cell->rowspan > 0 ? (guint)cell->rowspan : 1;
            for (guint dr = 0; dr < rsp && r + dr < R; dr++)
                for (guint dc = 0; dc < cov && col + dc < max_cols; dc++)
                    grid[(r + dr) * max_cols + (col + dc)] = cell;
            ns_cell_pos cp = { cell, r, col, cov, rsp };
            g_array_append_val(cells, cp);
            col += cov;
        }
        r++;
    }

    for (guint i = 0; i < cells->len; i++) {
        ns_cell_pos *cp = &g_array_index(cells, ns_cell_pos, i);
        gboolean right_nb = FALSE, below_nb = FALSE;
        guint cr = cp->c0 + cp->cov;
        if (cr < max_cols)
            for (guint rr = cp->r0; rr < cp->r0 + cp->rsp && rr < R; rr++)
                if (grid[rr * max_cols + cr]) { right_nb = TRUE; break; }
        guint br = cp->r0 + cp->rsp;
        if (br < R)
            for (guint cc = cp->c0; cc < cp->c0 + cp->cov && cc < max_cols; cc++)
                if (grid[br * max_cols + cc]) { below_nb = TRUE; break; }
        if (right_nb) cp->cell->border.right = 0;
        if (below_nb) cp->cell->border.bottom = 0;
    }

    g_array_free(cells, TRUE);
    g_free(grid);
}

static void
layout_table(ns_box *box, double parent_content_width, const ns_style *inherited_style)
{
    edges_from_style(box->style, parent_content_width,
                     &box->margin, &box->padding, &box->border);
    double horiz_total = box->margin.left + box->margin.right +
                         box->padding.left + box->padding.right +
                         box->border.left + box->border.right;
    const ns_css_value *wv = box->style ? box->style->values[NS_CSS_WIDTH] : NULL;
    gboolean explicit_width = wv &&
        (wv->kind == NS_CSS_V_LENGTH || wv->kind == NS_CSS_V_CALC);
    double sizing_extras = flex_box_is_border_box(box)
        ? box->padding.left + box->padding.right +
          box->border.left + box->border.right
        : 0;
    double cw;
    if (explicit_width) {
        cw = length_resolve(wv, parent_content_width, 0) - sizing_extras;
    } else {
        cw = parent_content_width - horiz_total;
    }
    const ns_css_value *mxw = box->style ? box->style->values[NS_CSS_MAX_WIDTH] : NULL;
    if (mxw && (mxw->kind == NS_CSS_V_LENGTH || mxw->kind == NS_CSS_V_CALC)) {
        double m = length_resolve(mxw, parent_content_width, -1);
        if (m >= 0 && cw > m - sizing_extras) cw = m - sizing_extras;
    }
    const ns_css_value *mnw = box->style ? box->style->values[NS_CSS_MIN_WIDTH] : NULL;
    if (mnw && (mnw->kind == NS_CSS_V_LENGTH || mnw->kind == NS_CSS_V_CALC)) {
        double m = length_resolve(mnw, parent_content_width, -1);
        if (m >= 0 && cw < m - sizing_extras) cw = m - sizing_extras;
    }
    if (cw < 0) cw = 0;
    box->content_width = cw;

    guint max_cols = table_column_count(box);
    if (max_cols == 0) {
        double inner_x = box->x + box->margin.left + box->border.left + box->padding.left;
        double inner_y = box->y + box->margin.top  + box->border.top  + box->padding.top;
        double cursor_y = inner_y;
        const ns_style *child_inherited = box->style ? box->style : inherited_style;
        layout_table_captions(box, FALSE, inner_x, cw, child_inherited, &cursor_y);
        layout_table_captions(box, TRUE, inner_x, cw, child_inherited, &cursor_y);
        box->content_height = cursor_y - inner_y;
        return;
    }

    double hsp = 0, vsp = 0;
    table_border_spacing(box->style, &hsp, &vsp);
    double total_hsp = (double)(max_cols + 1) * hsp;
    double col_avail = cw - total_hsp;
    if (col_avail < 0) col_avail = 0;

    const ns_style *measure_inherited = box->style ? box->style : inherited_style;
    double *col_widths = g_new0(double, max_cols);
    double *col_min = g_new0(double, max_cols);
    gboolean *col_fixed = g_new0(gboolean, max_cols);
    double *col_explicit = g_new(double, max_cols);
    for (guint i = 0; i < max_cols; i++) col_explicit[i] = -1;
    gboolean has_explicit_cols = FALSE;
    gboolean fixed_layout = explicit_width &&
        keyword_is(box->style ? box->style->values[NS_CSS_TABLE_LAYOUT] : NULL, "fixed");
    if (fixed_layout) {
        double fixed_sum = layout_fixed_table_columns(box, col_avail, max_cols,
                                                      col_widths, col_fixed);
        if (fixed_sum > col_avail) {
            col_avail = fixed_sum;
            cw = col_avail + total_hsp;
            box->content_width = cw;
        }
    } else {
        for (ns_box *row = box->first_child; row; row = row->next_sibling) {
            if (row->kind != NS_BOX_TABLE_ROW) continue;
            guint col = 0;
            for (ns_box *cell = row->first_child; cell; cell = cell->next_sibling) {
                int span = cell->colspan > 0 ? cell->colspan : 1;
                const ns_style *cs = cell->style ? cell->style : measure_inherited;
                double natural = measure_natural_width(cell, cs);
                edges_from_style(cell->style, cw > 0 ? cw : 1000.0,
                                 &cell->margin, &cell->padding, &cell->border);
                double h_extra = cell->padding.left + cell->padding.right
                    + cell->border.left + cell->border.right
                    + cell->margin.left + cell->margin.right;
                double cell_outer =
                    table_cell_clamp(cell, h_extra, natural + h_extra);
                gboolean cell_fixed = FALSE;
                double cell_explicit = -1;
                if (cell->style && cell->style->values[NS_CSS_WIDTH]) {
                    const ns_css_value *cwv = cell->style->values[NS_CSS_WIDTH];
                    if (cwv->kind == NS_CSS_V_LENGTH || cwv->kind == NS_CSS_V_CALC) {
                        cell_fixed = TRUE;
                        double w = length_resolve(cwv, col_avail > 0 ? col_avail : 0, -1);
                        if (w >= 0)
                            cell_explicit =
                                table_cell_clamp(cell, h_extra, w + h_extra);
                    }
                }
                double per_col = cell_outer / (double)span;
                for (int i = 0; i < span && col + (guint)i < max_cols; i++) {
                    if (per_col > col_widths[col + i])
                        col_widths[col + i] = per_col;
                    if (cell_fixed && span == 1) {
                        col_fixed[col + i] = TRUE;
                        has_explicit_cols = TRUE;
                    }
                    if (cell_explicit >= 0 && span == 1 &&
                        cell_explicit > col_explicit[col + i]) {
                        col_explicit[col + i] = cell_explicit;
                        has_explicit_cols = TRUE;
                    }
                }
                col += (guint)span;
            }
        }
        if (box->table_col_hints) {
            guint col = 0;
            for (guint i = 0; i < box->table_col_hints->len && col < max_cols; i++) {
                ns_table_col_hint *hint =
                    &g_array_index(box->table_col_hints, ns_table_col_hint, i);
                int hspan = hint->span > 0 ? hint->span : 1;
                double w = 0;
                if (table_width_from_style(hint->style, col_avail, &w)) {
                    has_explicit_cols = TRUE;
                    double per = w / (double)hspan;
                    for (int k = 0; k < hspan && col + (guint)k < max_cols; k++)
                        if (per > col_explicit[col + k]) col_explicit[col + k] = per;
                }
                col += (guint)hspan;
            }
        }
        double natural_sum_pre = 0;
        for (guint i = 0; i < max_cols; i++) natural_sum_pre += col_widths[i];
        if (has_explicit_cols || (natural_sum_pre > col_avail && col_avail > 0)) {
            for (ns_box *row = box->first_child; row; row = row->next_sibling) {
                if (row->kind != NS_BOX_TABLE_ROW) continue;
                guint col = 0;
                for (ns_box *cell = row->first_child; cell; cell = cell->next_sibling) {
                    int span = cell->colspan > 0 ? cell->colspan : 1;
                    const ns_style *cs = cell->style ? cell->style : measure_inherited;
                    ns_edges m = {0}, pd = {0}, bd = {0};
                    edges_from_style(cell->style, cw > 0 ? cw : 1000.0,
                                     &m, &pd, &bd);
                    double h_extra = pd.left + pd.right + bd.left + bd.right +
                                     m.left + m.right;
                    double per_col_min =
                        table_cell_clamp(cell, h_extra,
                                         measure_min_content_width(cell, cs)
                                         + h_extra) / (double)span;
                    for (int i = 0; i < span && col + (guint)i < max_cols; i++)
                        if (per_col_min > col_min[col + i])
                            col_min[col + i] = per_col_min;
                    col += (guint)span;
                }
            }
        }
        for (guint i = 0; i < max_cols; i++) {
            if (col_explicit[i] >= 0) {
                double e = col_explicit[i];
                if (e < col_min[i]) e = col_min[i];
                col_widths[i] = e;
                col_fixed[i] = TRUE;
            }
        }
        double natural_sum = 0;
        for (guint i = 0; i < max_cols; i++) natural_sum += col_widths[i];
        if (natural_sum > col_avail && col_avail > 0) {
            double min_sum = 0;
            for (guint i = 0; i < max_cols; i++) min_sum += col_min[i];
            if (min_sum >= col_avail) {
                if (min_sum > 0) {
                    for (guint i = 0; i < max_cols; i++)
                        col_widths[i] = col_min[i];
                    col_avail = min_sum;
                    cw = col_avail + total_hsp;
                    box->content_width = cw;
                } else {
                    double evenly = col_avail / (double)max_cols;
                    for (guint i = 0; i < max_cols; i++) col_widths[i] = evenly;
                }
            } else {
                double slack_sum = natural_sum - min_sum;
                double avail_extra = col_avail - min_sum;
                for (guint i = 0; i < max_cols; i++) {
                    double slack = col_widths[i] - col_min[i];
                    col_widths[i] = col_min[i] +
                        (slack_sum > 0 ? slack * (avail_extra / slack_sum) : 0);
                }
            }
        } else if (natural_sum == 0) {
            double evenly = col_avail / (double)max_cols;
            for (guint i = 0; i < max_cols; i++) col_widths[i] = evenly;
        } else if (explicit_width) {
            double extra = col_avail - natural_sum;
            if (extra > 0 && max_cols > 0) {
                double elastic_natural = 0;
                guint elastic_count = 0;
                for (guint i = 0; i < max_cols; i++) {
                    if (!col_fixed[i]) {
                        elastic_natural += col_widths[i];
                        elastic_count++;
                    }
                }
                if (elastic_natural > 0) {
                    for (guint i = 0; i < max_cols; i++) {
                        if (!col_fixed[i])
                            col_widths[i] += extra * col_widths[i] / elastic_natural;
                    }
                } else if (elastic_count > 0) {
                    double per = extra / (double)elastic_count;
                    for (guint i = 0; i < max_cols; i++) {
                        if (!col_fixed[i]) col_widths[i] += per;
                    }
                } else if (natural_sum > 0) {
                    for (guint i = 0; i < max_cols; i++)
                        col_widths[i] += extra * col_widths[i] / natural_sum;
                } else {
                    double per = extra / (double)max_cols;
                    for (guint i = 0; i < max_cols; i++) col_widths[i] += per;
                }
            }
        } else {
            cw = natural_sum + total_hsp;
            box->content_width = cw;
        }
    }
    g_free(col_explicit);
    g_free(col_fixed);
    g_free(col_min);
    double *col_x = g_new0(double, max_cols);
    {
        double cx = hsp;
        for (guint i = 0; i < max_cols; i++) {
            col_x[i] = cx;
            cx += col_widths[i] + hsp;
        }
    }

    {
        gboolean ml_auto = length_is_auto(box->style ? box->style->values[NS_CSS_MARGIN_LEFT]  : NULL);
        gboolean mr_auto = length_is_auto(box->style ? box->style->values[NS_CSS_MARGIN_RIGHT] : NULL);
        if (ml_auto || mr_auto) {
            double outer = cw + box->padding.left + box->padding.right +
                           box->border.left + box->border.right;
            double available = parent_content_width - outer;
            if (available < 0) available = 0;
            if (ml_auto && mr_auto) {
                box->margin.left  = available / 2.0;
                box->margin.right = available / 2.0;
            } else if (ml_auto) {
                box->margin.left  = available - box->margin.right;
                if (box->margin.left < 0) box->margin.left = 0;
            } else if (mr_auto) {
                box->margin.right = available - box->margin.left;
                if (box->margin.right < 0) box->margin.right = 0;
            }
        }
    }

    double inner_x = box->x + box->margin.left + box->border.left + box->padding.left;
    double inner_y = box->y + box->margin.top  + box->border.top  + box->padding.top;
    double cursor_y = inner_y;
    const ns_style *child_inherited = box->style ? box->style : inherited_style;

    layout_table_captions(box, FALSE, inner_x, cw, child_inherited, &cursor_y);
    cursor_y += vsp;

    int *rs_remain = g_new0(int, max_cols);
    ns_box **rs_cell = g_new0(ns_box *, max_cols);
    for (ns_box *row = box->first_child; row; row = row->next_sibling) {
        if (row->kind != NS_BOX_TABLE_ROW) continue;
        row->x = inner_x;
        row->y = cursor_y;
        row->content_width = cw;
        double row_height = 0;
        guint col = 0;
        for (ns_box *cell = row->first_child; cell; cell = cell->next_sibling) {
            while (col < max_cols && rs_remain[col] > 0) col++;
            int span = cell->colspan > 0 ? cell->colspan : 1;
            int rspan = cell->rowspan > 0 ? cell->rowspan : 1;
            double cell_outer_w = 0;
            int covered = 0;
            for (int i = 0; i < span && col + (guint)i < max_cols; i++) {
                cell_outer_w += col_widths[col + i];
                covered++;
            }
            if (covered > 1) cell_outer_w += hsp * (double)(covered - 1);
            if (rspan > 1) {
                for (int i = 0; i < span && col + (guint)i < max_cols; i++) {
                    rs_remain[col + i] = rspan;
                    rs_cell[col + i] = cell;
                }
            }
            cell->x = inner_x + (col < max_cols ? col_x[col] : 0);
            cell->y = cursor_y;
            const ns_style *cs = cell->style ? cell->style : child_inherited;
            edges_from_style(cell->style, cell_outer_w,
                             &cell->margin, &cell->padding, &cell->border);
            double cell_inner_w = cell_outer_w
                - cell->padding.left - cell->padding.right
                - cell->border.left - cell->border.right
                - cell->margin.left - cell->margin.right;
            if (cell_inner_w < 0) cell_inner_w = 0;
            cell->content_width = cell_inner_w;
            double ix = cell->x + cell->margin.left + cell->border.left + cell->padding.left;
            double iy = cell->y + cell->margin.top  + cell->border.top  + cell->padding.top;
            double cell_h;
            if (!cell->style) {
                layout_block(cell, cell_outer_w, cs);
                cell_inner_w = cell->content_width;
                cell_h = cell->content_height;
            } else {
                double sub_y = iy;
                for (ns_box *child = cell->first_child; child; child = child->next_sibling) {
                    child->x = ix;
                    child->y = sub_y;
                    layout_box(child, cell_inner_w, cs);
                    legacy_align_block_child(child, ix, cell_inner_w, cs);
                    double dh = child->content_height;
                    if (child->kind == NS_BOX_BLOCK || child->kind == NS_BOX_TABLE)
                        dh += child->margin.top + child->margin.bottom +
                              child->padding.top + child->padding.bottom +
                              child->border.top + child->border.bottom;
                    sub_y += dh;
                }
                cell_h = sub_y - iy;
            }
            const ns_css_value *cell_hv = cell->style
                ? cell->style->values[NS_CSS_HEIGHT] : NULL;
            const ns_css_value *cell_mnh = cell->style
                ? cell->style->values[NS_CSS_MIN_HEIGHT] : NULL;
            if (cell_hv &&
                (cell_hv->kind == NS_CSS_V_LENGTH || cell_hv->kind == NS_CSS_V_CALC)) {
                double eh = resolve_used_height(cell, cell_hv, cell_inner_w, -1);
                if (eh > cell_h) cell_h = eh;
            }
            if (cell_mnh &&
                (cell_mnh->kind == NS_CSS_V_LENGTH || cell_mnh->kind == NS_CSS_V_CALC)) {
                double mh = resolve_used_height(cell, cell_mnh, cell_inner_w, -1);
                if (mh > cell_h) cell_h = mh;
            }
            cell->content_height = cell_h;
            double cell_outer_h = cell_h
                + cell->margin.top + cell->margin.bottom
                + cell->padding.top + cell->padding.bottom
                + cell->border.top + cell->border.bottom;
            if (rspan <= 1 && cell_outer_h > row_height) row_height = cell_outer_h;
            col += (guint)span;
        }
        const ns_css_value *rhv = row->style ? row->style->values[NS_CSS_HEIGHT] : NULL;
        if (rhv && (rhv->kind == NS_CSS_V_LENGTH || rhv->kind == NS_CSS_V_CALC)) {
            double rh = length_resolve(rhv, 0, -1);
            if (rh > row_height) row_height = rh;
        }
        GHashTable *ending_rowspans = NULL;
        for (guint i = 0; i < max_cols; i++) {
            if (rs_remain[i] != 1 || !rs_cell[i]) continue;
            if (!ending_rowspans)
                ending_rowspans = g_hash_table_new(g_direct_hash,
                                                   g_direct_equal);
            ns_box *rc = rs_cell[i];
            if (g_hash_table_contains(ending_rowspans, rc)) continue;
            g_hash_table_add(ending_rowspans, rc);
            double span_bottom = cursor_y + row_height;
            double avail = span_bottom - rc->y
                         - rc->margin.top - rc->margin.bottom
                         - rc->padding.top - rc->padding.bottom
                         - rc->border.top - rc->border.bottom;
            if (rc->content_height > avail)
                row_height += rc->content_height - avail;
        }
        if (ending_rowspans) g_hash_table_destroy(ending_rowspans);
        for (ns_box *cell = row->first_child; cell; cell = cell->next_sibling) {
            if ((cell->rowspan > 0 ? cell->rowspan : 1) > 1) continue;
            double avail = row_height
                         - cell->margin.top - cell->margin.bottom
                         - cell->padding.top - cell->padding.bottom
                         - cell->border.top - cell->border.bottom;
            double natural = 0;
            for (ns_box *ch = cell->first_child; ch; ch = ch->next_sibling) {
                double dh = ch->content_height;
                if (ch->kind == NS_BOX_BLOCK || ch->kind == NS_BOX_TABLE)
                    dh += ch->margin.top + ch->margin.bottom +
                          ch->padding.top + ch->padding.bottom +
                          ch->border.top + ch->border.bottom;
                natural += dh;
            }
            double extra = avail - natural;
            if (extra > 0) {
                double factor = 0;
                const ns_css_value *va = cell->style
                    ? cell->style->values[NS_CSS_VERTICAL_ALIGN] : NULL;
                if (va && va->kind == NS_CSS_V_KEYWORD && va->u.keyword) {
                    if (g_ascii_strcasecmp(va->u.keyword, "middle") == 0)
                        factor = 0.5;
                    else if (g_ascii_strcasecmp(va->u.keyword, "bottom") == 0)
                        factor = 1.0;
                }
                if (factor > 0)
                    for (ns_box *ch = cell->first_child; ch; ch = ch->next_sibling)
                        shift_box_tree(ch, 0, extra * factor);
            }
            if (avail > cell->content_height) cell->content_height = avail;
        }
        row->content_height = row_height;
        cursor_y += row_height + vsp;
        for (guint i = 0; i < max_cols; i++) {
            if (rs_remain[i] > 0 && --rs_remain[i] == 0 && rs_cell[i]) {
                ns_box *rc = rs_cell[i];
                double h = cursor_y - vsp - rc->y
                    - rc->margin.top - rc->margin.bottom
                    - rc->padding.top - rc->padding.bottom
                    - rc->border.top - rc->border.bottom;
                if (h > rc->content_height) rc->content_height = h;
                rs_cell[i] = NULL;
            }
        }
    }
    layout_table_captions(box, TRUE, inner_x, cw, child_inherited, &cursor_y);
    if (table_is_collapse(box->style))
        table_collapse_borders(box, max_cols);
    g_free(rs_remain);
    g_free(rs_cell);
    g_free(col_widths);
    g_free(col_x);
    box->content_height = cursor_y - inner_y;

    const ns_css_value *thv = box->style ? box->style->values[NS_CSS_HEIGHT] : NULL;
    if (thv && (thv->kind == NS_CSS_V_LENGTH || thv->kind == NS_CSS_V_CALC)) {
        double target = resolve_used_height(box, thv, cw, -1);
        if (box->style && box->style->values[NS_CSS_BOX_SIZING] &&
            ns_css_keyword_is(box->style->values[NS_CSS_BOX_SIZING], "border-box"))
            target -= box->padding.top + box->padding.bottom +
                      box->border.top + box->border.bottom;
        int nrows = 0;
        for (ns_box *row = box->first_child; row; row = row->next_sibling)
            if (row->kind == NS_BOX_TABLE_ROW) nrows++;
        if (nrows > 0 && target > box->content_height + 0.5) {
            double per = (target - box->content_height) / nrows;
            double shift = 0;
            for (ns_box *row = box->first_child; row; row = row->next_sibling) {
                if (row->kind != NS_BOX_TABLE_ROW) continue;
                if (shift > 0) translate_subtree(row, 0, shift);
                row->content_height += per;
                for (ns_box *cell = row->first_child; cell;
                     cell = cell->next_sibling) {
                    if (cell->kind != NS_BOX_TABLE_CELL) continue;
                    double factor = 0;
                    const ns_css_value *va = cell->style
                        ? cell->style->values[NS_CSS_VERTICAL_ALIGN] : NULL;
                    if (va && va->kind == NS_CSS_V_KEYWORD && va->u.keyword) {
                        if (g_ascii_strcasecmp(va->u.keyword, "middle") == 0)
                            factor = 0.5;
                        else if (g_ascii_strcasecmp(va->u.keyword, "bottom") == 0)
                            factor = 1.0;
                    }
                    if (factor > 0)
                        for (ns_box *ch = cell->first_child; ch;
                             ch = ch->next_sibling)
                            shift_box_tree(ch, 0, per * factor);
                    cell->content_height += per;
                }
                shift += per;
            }
            box->content_height = target;
        }
    }
}

static __thread gboolean g_cq_seen_container;

static gboolean
box_is_query_container(const ns_box *b)
{
    if (!b || !b->style) return FALSE;
    const ns_css_value *ct = b->style->values[NS_CSS_CONTAINER_TYPE];
    return ct && ct->kind == NS_CSS_V_KEYWORD && ct->u.keyword &&
           g_ascii_strcasecmp(ct->u.keyword, "normal") != 0;
}

static gboolean
box_is_size_query_container(const ns_box *b)
{
    if (!b || !b->style) return FALSE;
    const ns_css_value *ct = b->style->values[NS_CSS_CONTAINER_TYPE];
    return ct && ct->kind == NS_CSS_V_KEYWORD && ct->u.keyword &&
           g_ascii_strcasecmp(ct->u.keyword, "size") == 0;
}

static void
cq_set_dims_from_ancestors(const ns_box *box)
{
    double inline_size = 0;
    double block_size = 0;
    for (const ns_box *a = box->parent; a; a = a->parent) {
        if (inline_size <= 0 && box_is_query_container(a))
            inline_size = a->content_width;
        if (block_size <= 0 && box_is_size_query_container(a))
            block_size = a->content_height;
        if (inline_size > 0 && block_size > 0) break;
    }
    ns_css_set_container_dims(inline_size, block_size);
}

static void
layout_box(ns_box *box, double parent_content_width, const ns_style *inherited_style)
{
    if (box_is_query_container(box)) g_cq_seen_container = TRUE;
    if (g_cq_seen_container) cq_set_dims_from_ancestors(box);
    if (box->kind == NS_BOX_BLOCK) {
        layout_block(box, parent_content_width, inherited_style);
    } else if (box->kind == NS_BOX_INLINE) {
        inline_layout(box, parent_content_width, inherited_style);
    } else if (box->kind == NS_BOX_IMAGE) {
        layout_image(box, parent_content_width);
    } else if (box->kind == NS_BOX_VIDEO) {
        layout_image(box, parent_content_width);
    } else if (box->kind == NS_BOX_SVG) {
        layout_image(box, parent_content_width);
    } else if (box->kind == NS_BOX_TABLE) {
        layout_table(box, parent_content_width, inherited_style);
    } else if (box->kind == NS_BOX_TABLE_CAPTION) {
        layout_block(box, parent_content_width, inherited_style);
    } else if (box->kind == NS_BOX_MATH) {
        double fpx = length_or(box->style
                               ? box->style->values[NS_CSS_FONT_SIZE] : NULL, 16);
        double w = 0, asc = 0, desc = 0;
        ns_math_measure(box->dom, fpx, &w, &asc, &desc);
        box->content_width = w;
        box->content_height = asc + desc;
    } else {
        box->content_width = parent_content_width;
        box->content_height = 0;
    }
}

static gboolean
flex_box_is_border_box(const ns_box *c)
{
    const ns_css_value *bsv = c && c->style
        ? c->style->values[NS_CSS_BOX_SIZING] : NULL;
    return bsv && bsv->kind == NS_CSS_V_KEYWORD && bsv->u.keyword &&
           strcmp(bsv->u.keyword, "border-box") == 0;
}

static double
flex_main_axis_extras(const ns_box *c)
{
    if (!c) return 0;
    return c->padding.left + c->padding.right + c->border.left + c->border.right;
}

static double
flex_border_box_to_content(const ns_box *c, double v)
{
    if (flex_box_is_border_box(c)) {
        v -= flex_main_axis_extras(c);
        if (v < 0) v = 0;
    }
    return v;
}

static double
flex_item_keyword_width(ns_box *c, const ns_css_value *v, double cw,
                        const ns_style *inherited)
{
    if (!v || v->kind != NS_CSS_V_KEYWORD || !v->u.keyword) return -1;
    double inner = cw - c->margin.left - c->margin.right
                 - c->padding.left - c->padding.right
                 - c->border.left - c->border.right;
    return intrinsic_keyword_width(c, v->u.keyword,
                                   inherited ? inherited : c->style, inner);
}

static gboolean
flex_main_basis_explicit(ns_box *c, double cw, const ns_style *inherited,
                         double *out)
{
    const ns_style *s = c->style;
    if (!s) return FALSE;
    const ns_css_value *b = s->values[NS_CSS_FLEX_BASIS];
    if (b && (b->kind == NS_CSS_V_LENGTH || b->kind == NS_CSS_V_CALC)) {
        *out = flex_border_box_to_content(c, length_resolve(b, cw, 0));
        return TRUE;
    }
    double keyword_basis = flex_item_keyword_width(c, b, cw, inherited);
    if (keyword_basis >= 0) {
        *out = keyword_basis;
        return TRUE;
    }
    const ns_css_value *w = s->values[NS_CSS_WIDTH];
    if (w && (w->kind == NS_CSS_V_LENGTH || w->kind == NS_CSS_V_CALC)) {
        *out = flex_border_box_to_content(c, length_resolve(w, cw, 0));
        return TRUE;
    }
    double keyword_width = flex_item_keyword_width(c, w, cw, inherited);
    if (keyword_width >= 0) {
        *out = keyword_width;
        return TRUE;
    }
    return FALSE;
}

static double
estimate_natural_width(const ns_box *b, double cap)
{
    double font_size = 16;
    if (b->style && b->style->values[NS_CSS_FONT_SIZE]) {
        const ns_css_value *fs = b->style->values[NS_CSS_FONT_SIZE];
        if (fs->kind == NS_CSS_V_LENGTH && fs->u.length.unit == NS_CSS_UNIT_PX)
            font_size = fs->u.length.v;
    } else {
        for (const ns_box *p = b->parent; p; p = p->parent) {
            if (p->style && p->style->values[NS_CSS_FONT_SIZE]) {
                const ns_css_value *fs = p->style->values[NS_CSS_FONT_SIZE];
                if (fs->kind == NS_CSS_V_LENGTH &&
                    fs->u.length.unit == NS_CSS_UNIT_PX) {
                    font_size = fs->u.length.v;
                    break;
                }
            }
        }
    }
    const ns_css_value *swv = b->style ? b->style->values[NS_CSS_WIDTH] : NULL;
    if (swv && swv->kind == NS_CSS_V_LENGTH && swv->u.length.v > 0) {
        double sw = -1;
        switch (swv->u.length.unit) {
        case NS_CSS_UNIT_PX:
        case NS_CSS_UNIT_NUMBER:
            sw = swv->u.length.v;
            break;
        case NS_CSS_UNIT_EM:
            sw = swv->u.length.v * font_size;
            break;
        case NS_CSS_UNIT_REM:
            sw = swv->u.length.v * 16.0;
            break;
        case NS_CSS_UNIT_VW:
            sw = swv->u.length.v * ns_css_viewport_w() / 100.0;
            break;
        default:
            break;
        }
        if (sw > 0) {
            const ns_css_value *bsv = b->style->values[NS_CSS_BOX_SIZING];
            gboolean border_box = bsv && bsv->kind == NS_CSS_V_KEYWORD &&
                                  bsv->u.keyword &&
                                  strcmp(bsv->u.keyword, "border-box") == 0;
            if (!border_box)
                sw += b->padding.left + b->padding.right +
                      b->border.left  + b->border.right;
            return sw > cap ? cap : sw;
        }
    }
    if (style_contains_inline_size(b->style)) return 0;
    double w = 0;
    if (b->kind == NS_BOX_INLINE && b->text) {
        double chars = 0;
        for (const char *p = b->text; *p; p = g_utf8_next_char(p))
            if (g_utf8_get_char(p) != 0xFFFC) chars += 1;
        w = chars * font_size * 0.65 + font_size * 0.5;
        if (b->inline_atomics)
            for (guint i = 0; i < b->inline_atomics->len; i++) {
                const ns_box *ab =
                    g_array_index(b->inline_atomics, ns_inline_atomic, i).box;
                if (!ab) continue;
                double aw = ab->content_width > 0
                    ? ab->content_width +
                      ab->padding.left + ab->padding.right +
                      ab->border.left  + ab->border.right
                    : estimate_natural_width(ab, cap);
                w += aw + ab->margin.left + ab->margin.right;
            }
    } else if (b->kind == NS_BOX_IMAGE || b->kind == NS_BOX_VIDEO ||
               b->kind == NS_BOX_SVG) {
        w = b->content_width > 0 ? b->content_width : 0;
    } else {
        int flow_children = 0;
        gboolean column_flex = b->style &&
            style_is_flex_container(b->style) &&
            (strcmp(flex_direction_of(b->style),
                    "column") == 0 ||
             strcmp(flex_direction_of(b->style),
                    "column-reverse") == 0);
        for (const ns_box *c = b->first_child; c; c = c->next_sibling) {
            if (c->style && c->style != b->style &&
                style_is_absolute_or_fixed(c->style)) continue;
            double cw_child = estimate_natural_width(c, cap);
            if (c->style &&
                (c->style->values[NS_CSS_MARGIN_LEFT] ||
                 c->style->values[NS_CSS_MARGIN_RIGHT])) {
                ns_edges m = {0}, pd = {0}, bd = {0};
                edges_from_style(c->style, 0, &m, &pd, &bd);
                cw_child += m.left + m.right;
            }
            if (column_flex) {
                if (cw_child > w) w = cw_child;
            } else {
                w += cw_child;
            }
            flow_children++;
        }
        if (flow_children > 1 && style_is_flex_container(b->style) &&
            strcmp(flex_direction_of(b->style),
                   "column") != 0 &&
            strcmp(flex_direction_of(b->style),
                   "column-reverse") != 0) {
            const ns_css_value *cg = b->style->values[NS_CSS_COLUMN_GAP];
            const ns_css_value *gg = b->style->values[NS_CSS_GAP];
            const ns_css_value *gv =
                (cg && cg->kind == NS_CSS_V_LENGTH) ? cg :
                (gg && gg->kind == NS_CSS_V_LENGTH) ? gg : NULL;
            if (gv && gv->u.length.unit != NS_CSS_UNIT_PERCENT)
                w += (flow_children - 1) * gv->u.length.v;
        }
    }
    if (b->style) {
        if (b->style->values[NS_CSS_PADDING_LEFT] ||
            b->style->values[NS_CSS_PADDING_RIGHT] ||
            b->style->values[NS_CSS_BORDER_LEFT_WIDTH] ||
            b->style->values[NS_CSS_BORDER_RIGHT_WIDTH]) {
            ns_edges bm = {0}, bpd = {0}, bbd = {0};
            edges_from_style(b->style, cap, &bm, &bpd, &bbd);
            w += bpd.left + bpd.right + bbd.left + bbd.right;
        }
    } else {
        w += b->padding.left + b->padding.right +
             b->border.left  + b->border.right;
    }
    if (b->style) {
        const ns_css_value *bsv = b->style->values[NS_CSS_BOX_SIZING];
        gboolean border_box = bsv && bsv->kind == NS_CSS_V_KEYWORD &&
                              bsv->u.keyword &&
                              strcmp(bsv->u.keyword, "border-box") == 0;
        double box_extras = border_box ? 0 :
            b->padding.left + b->padding.right + b->border.left + b->border.right;
        const ns_css_value *mxw = b->style->values[NS_CSS_MAX_WIDTH];
        if (mxw && (mxw->kind == NS_CSS_V_LENGTH || mxw->kind == NS_CSS_V_CALC)) {
            double mx = length_resolve(mxw, cap, -1);
            if (mx >= 0 && w > mx + box_extras) w = mx + box_extras;
        }
        const ns_css_value *mnw = b->style->values[NS_CSS_MIN_WIDTH];
        if (mnw && (mnw->kind == NS_CSS_V_LENGTH || mnw->kind == NS_CSS_V_CALC)) {
            double mn = length_resolve(mnw, cap, -1);
            if (mn > 0 && w < mn + box_extras) w = mn + box_extras;
        }
    }
    if (w > cap) w = cap;
    return w;
}

static double
flex_content_basis_from_natural(ns_box *b, const ns_style *inherited)
{
    double w = measure_natural_width(b, inherited ? inherited : b->style);
    return w > 0 ? w : 0;
}

static double
flex_item_max_main(ns_box *c, double cw, const ns_style *inherited)
{
    const ns_css_value *mxw = c->style ? c->style->values[NS_CSS_MAX_WIDTH] : NULL;
    if (mxw && mxw->kind == NS_CSS_V_KEYWORD)
        return flex_item_keyword_width(c, mxw, cw, inherited);
    if (!mxw || !(mxw->kind == NS_CSS_V_LENGTH || mxw->kind == NS_CSS_V_CALC))
        return -1;
    double mx = length_resolve(mxw, cw, -1);
    return mx < 0 ? -1 : flex_border_box_to_content(c, mx);
}

static gboolean
flex_item_is_replaced_like(const ns_box *c)
{
    if (c->kind == NS_BOX_IMAGE || c->kind == NS_BOX_VIDEO ||
        c->kind == NS_BOX_SVG)
        return TRUE;
    const ns_node *n = c->dom;
    if (!n || n->kind != NS_NODE_ELEMENT || !n->name) return FALSE;
    if (strcmp(n->name, "input") == 0) {
        const char *type = ns_element_get_attr(n, "type");
        return !type || (g_ascii_strcasecmp(type, "button") != 0 &&
                         g_ascii_strcasecmp(type, "submit") != 0 &&
                         g_ascii_strcasecmp(type, "reset") != 0);
    }
    return strcmp(n->name, "select") == 0 || strcmp(n->name, "textarea") == 0 ||
           strcmp(n->name, "meter") == 0 || strcmp(n->name, "progress") == 0;
}

static double
flex_item_min_main(ns_box *c, double cw, const ns_style *inherited)
{
    const ns_css_value *mnw = c->style ? c->style->values[NS_CSS_MIN_WIDTH] : NULL;
    if (mnw && (mnw->kind == NS_CSS_V_LENGTH || mnw->kind == NS_CSS_V_CALC)) {
        double mn = flex_border_box_to_content(c, length_resolve(mnw, cw, -1));
        return mn > 0 ? mn : 0;
    }
    if (mnw && mnw->kind == NS_CSS_V_KEYWORD && !keyword_is(mnw, "auto")) {
        double mn = flex_item_keyword_width(c, mnw, cw, inherited);
        return mn > 0 ? mn : 0;
    }
    if (box_clips_children(c)) return 0;
    double mn = measure_min_content_width(c, inherited ? inherited : c->style);
    if (mn < 0) mn = 0;
    const ns_css_value *wv = c->style ? c->style->values[NS_CSS_WIDTH] : NULL;
    if (wv && (wv->kind == NS_CSS_V_LENGTH || wv->kind == NS_CSS_V_CALC)) {
        double specified = value_is_percent(wv) && flex_item_is_replaced_like(c)
            ? flex_border_box_to_content(c, length_resolve(wv, 0, -1))
            : flex_border_box_to_content(c, length_resolve(wv, cw, -1));
        if (specified >= 0 && specified < mn) mn = specified;
    } else {
        double specified = flex_item_keyword_width(c, wv, cw, inherited);
        if (specified >= 0 && specified < mn) mn = specified;
    }
    double mx = flex_item_max_main(c, cw, inherited);
    if (mx >= 0 && mn > mx) mn = mx;
    return mn;
}

typedef struct {
    double basis;
    double min;
    double max;
    double grow;
    double shrink;
    double target;
    double violation;
    gboolean frozen;
} ns_flex_len;

static double
flex_clamp_main(double v, double mn, double mx)
{
    if (mx >= 0 && v > mx) v = mx;
    if (v < mn) v = mn;
    return v < 0 ? 0 : v;
}

static void
flex_resolve_lengths(ns_flex_len *it, guint n, double available)
{
    double sum_hyp = 0;
    for (guint i = 0; i < n; i++)
        sum_hyp += flex_clamp_main(it[i].basis, it[i].min, it[i].max);
    gboolean growing = sum_hyp < available;
    double initial_free = available;
    for (guint i = 0; i < n; i++) {
        double hyp = flex_clamp_main(it[i].basis, it[i].min, it[i].max);
        double factor = growing ? it[i].grow : it[i].shrink;
        it[i].frozen = factor <= 0 ||
                       (growing && it[i].basis > hyp) ||
                       (!growing && it[i].basis < hyp);
        it[i].target = hyp;
        initial_free -= it[i].frozen ? hyp : it[i].basis;
    }
    for (guint iter = 0; iter <= n; iter++) {
        double sum_factor = 0, sum_scaled = 0, remaining = available;
        gboolean any = FALSE;
        for (guint i = 0; i < n; i++) {
            if (it[i].frozen) { remaining -= it[i].target; continue; }
            any = TRUE;
            remaining -= it[i].basis;
            sum_factor += growing ? it[i].grow : it[i].shrink;
            sum_scaled += it[i].shrink * it[i].basis;
        }
        if (!any) break;
        if (sum_factor < 1) {
            double product = initial_free * sum_factor;
            if (fabs(product) < fabs(remaining)) remaining = product;
        }
        double total_violation = 0;
        for (guint i = 0; i < n; i++) {
            if (it[i].frozen) continue;
            double t = it[i].basis;
            if (growing && remaining > 0 && sum_factor > 0)
                t += remaining * (it[i].grow / sum_factor);
            else if (!growing && remaining < 0 && sum_scaled > 0)
                t -= fabs(remaining) * (it[i].shrink * it[i].basis / sum_scaled);
            double clamped = flex_clamp_main(t, it[i].min, it[i].max);
            it[i].violation = clamped - t;
            it[i].target = clamped;
            total_violation += clamped - t;
        }
        for (guint i = 0; i < n; i++) {
            if (it[i].frozen) continue;
            if (total_violation > 0.0001 ? it[i].violation > 0 :
                total_violation < -0.0001 ? it[i].violation < 0 : TRUE)
                it[i].frozen = TRUE;
        }
    }
}

static gboolean
flex_container_scrolls(const ns_box *box)
{
    if (!box->style) return FALSE;
    return overflow_kw_scrolls(overflow_axis_keyword(box->style, NS_CSS_OVERFLOW_X)) ||
           overflow_kw_scrolls(overflow_axis_keyword(box->style, NS_CSS_OVERFLOW_Y));
}

static void
flex_justify_offsets(const ns_box *box, const char *justify, double free_main,
                     guint count, gboolean reverse,
                     double *leading, double *between)
{
    *leading = 0;
    *between = 0;
    if (count == 0) return;
    if (free_main < 0) {
        if (flex_container_scrolls(box)) {
            *leading = reverse ? free_main : 0;
            return;
        }
        if (strcmp(justify, "space-between") == 0 ||
            strcmp(justify, "space-around") == 0 ||
            strcmp(justify, "space-evenly") == 0)
            return;
    }
    if (strcmp(justify, "flex-end") == 0 || strcmp(justify, "end") == 0 ||
        strcmp(justify, "right") == 0)
        *leading = free_main;
    else if (strcmp(justify, "center") == 0)
        *leading = free_main / 2.0;
    else if (strcmp(justify, "space-between") == 0)
        *between = count > 1 ? free_main / (count - 1) : 0;
    else if (strcmp(justify, "space-around") == 0) {
        *between = free_main / count;
        *leading = *between / 2.0;
    } else if (strcmp(justify, "space-evenly") == 0) {
        *between = free_main / (count + 1);
        *leading = *between;
    }
}

static double
flex_grow_of(const ns_box *c)
{
    if (!c->style) return 0;
    return number_or(c->style->values[NS_CSS_FLEX_GROW], 0);
}

static double
flex_shrink_of(const ns_box *c)
{
    if (!c->style) return 1;
    double s = number_or(c->style->values[NS_CSS_FLEX_SHRINK], 1);
    return s < 0 ? 0 : s;
}

static void
shift_box_tree(ns_box *b, double dx, double dy)
{
    if (!b) return;
    b->x += dx;
    b->y += dy;
    if (b->grid_col_tracks)
        for (guint i = 0; i < b->grid_col_tracks->len; i++) {
            ns_grid_track_edges *e =
                &g_array_index(b->grid_col_tracks, ns_grid_track_edges, i);
            e->start += dx;
            e->end += dx;
        }
    if (b->grid_row_tracks)
        for (guint i = 0; i < b->grid_row_tracks->len; i++) {
            ns_grid_track_edges *e =
                &g_array_index(b->grid_row_tracks, ns_grid_track_edges, i);
            e->start += dy;
            e->end += dy;
        }
    for (ns_box *c = b->first_child; c; c = c->next_sibling)
        shift_box_tree(c, dx, dy);
    if (b->inline_atomics)
        for (guint i = 0; i < b->inline_atomics->len; i++) {
            ns_box *ab =
                g_array_index(b->inline_atomics, ns_inline_atomic, i).box;
            if (ab) shift_box_tree(ab, dx, dy);
        }
}

static double
gap_px(const ns_css_value *specific, const ns_css_value *shorthand, double basis)
{
    const ns_css_value *v = specific ? specific : shorthand;
    if (!v || (v->kind != NS_CSS_V_LENGTH && v->kind != NS_CSS_V_CALC))
        return 0;
    return length_resolve_nonnegative(v, basis, 0);
}

static double
flex_gap_of(const ns_style *s, double basis)
{
    if (!s) return 0;
    return gap_px(s->values[NS_CSS_COLUMN_GAP], s->values[NS_CSS_GAP], basis);
}

static double
flex_gap_row_of(const ns_style *s, double basis)
{
    if (!s) return 0;
    return gap_px(s->values[NS_CSS_ROW_GAP], s->values[NS_CSS_GAP], basis);
}

static gboolean
flex_wraps(const ns_style *s)
{
    if (!s) return FALSE;
    const ns_css_value *w = s->values[NS_CSS_FLEX_WRAP];
    if (!w || w->kind != NS_CSS_V_KEYWORD || !w->u.keyword) return FALSE;
    return strcmp(w->u.keyword, "wrap") == 0 ||
           strcmp(w->u.keyword, "wrap-reverse") == 0;
}

static double
flex_main_height_outer(const ns_box *c, const ns_css_value *v,
                       double cross_size, double container_main_size)
{
    double out = value_is_percent(v)
        ? resolve_height_with_basis(v, cross_size, container_main_size, 0)
        : length_resolve(v, cross_size, 0);
    if (!flex_box_is_border_box(c))
        out += c->padding.top + c->padding.bottom +
               c->border.top + c->border.bottom;
    return out;
}

static double
flex_basis_main_height(const ns_box *c, double cross_size,
                       double container_main_size, gboolean *out_explicit)
{
    *out_explicit = FALSE;
    const ns_style *s = c->style;
    if (!s) return 0;
    const ns_css_value *b = s->values[NS_CSS_FLEX_BASIS];
    if (b && (b->kind == NS_CSS_V_LENGTH || b->kind == NS_CSS_V_CALC)) {
        if (value_is_percent(b) && container_main_size < 0) return 0;
        *out_explicit = TRUE;
        return flex_main_height_outer(c, b, cross_size,
                                      container_main_size);
    }
    const ns_css_value *h = s->values[NS_CSS_HEIGHT];
    if (h && (h->kind == NS_CSS_V_LENGTH || h->kind == NS_CSS_V_CALC)) {
        if (value_is_percent(h) && container_main_size < 0) return 0;
        *out_explicit = TRUE;
        return flex_main_height_outer(c, h, cross_size,
                                      container_main_size);
    }
    return 0;
}

static void
flex_relayout_after_cross_resize(ns_box *c, double layout_width,
                                 double main_size, double pre_h,
                                 const ns_style *child_inherited)
{
    if (!c->first_child) return;
    double target_h = c->content_height;
    if (fabs(target_h - pre_h) < 0.01) return;
    c->definite_height = target_h;
    c->flex_main_size = main_size;
    c->has_flex_main = TRUE;
    double sx = c->x, sy = c->y;
    layout_box(c, layout_width, child_inherited);
    if (c->x != sx || c->y != sy)
        shift_box_tree(c, sx - c->x, sy - c->y);
    c->content_height = target_h;
}

static gboolean
flex_align_stretches(const char *align)
{
    return strcmp(align, "stretch") == 0 || strcmp(align, "normal") == 0;
}

static gboolean
flex_item_cross_size_auto(const ns_box *c)
{
    const ns_css_value *h = c->style ? c->style->values[NS_CSS_HEIGHT] : NULL;
    if (!h || h->kind == NS_CSS_V_KEYWORD) return !size_keyword_is_intrinsic(h);
    return value_is_percent(h) && containing_block_definite_height(c) < 0;
}

static double
flex_item_stretched_height(const ns_box *c, double line_cross_size,
                           double width_basis)
{
    double vex = c->padding.top + c->padding.bottom +
                 c->border.top + c->border.bottom;
    double h = line_cross_size - c->margin.top - c->margin.bottom - vex;
    if (h < 0) h = 0;
    if (!c->style) return h;
    double sizing_extras = flex_box_is_border_box(c) ? vex : 0;
    double mx = resolve_used_height(c, c->style->values[NS_CSS_MAX_HEIGHT],
                                    width_basis, -1);
    if (mx >= 0 && h > mx - sizing_extras)
        h = mx > sizing_extras ? mx - sizing_extras : 0;
    double mn = resolve_used_height(c, c->style->values[NS_CSS_MIN_HEIGHT],
                                    width_basis, -1);
    if (mn >= 0 && h < mn - sizing_extras) h = mn - sizing_extras;
    return h;
}

static void
flex_item_fit_line_keyword_limits(ns_box *c, double line_cross_size,
                                  double width_basis)
{
    if (!c->style) return;
    const ns_css_value *mxh = c->style->values[NS_CSS_MAX_HEIGHT];
    const ns_css_value *mnh = c->style->values[NS_CSS_MIN_HEIGHT];
    gboolean max_stretches = height_keyword_stretches(mxh);
    gboolean min_stretches = height_keyword_stretches(mnh);
    if (!max_stretches && !min_stretches) return;
    double vex = c->padding.top + c->padding.bottom +
                 c->border.top + c->border.bottom;
    double line_inner = line_cross_size - c->margin.top - c->margin.bottom - vex;
    if (line_inner < 0) line_inner = 0;
    double min_h = line_inner;
    if (!min_stretches) {
        min_h = resolve_used_height(c, mnh, width_basis, -1);
        if (min_h >= 0 && flex_box_is_border_box(c)) min_h -= vex;
    }
    if (max_stretches && c->content_height > line_inner)
        c->content_height = line_inner;
    if (min_h >= 0 && c->content_height < min_h)
        c->content_height = min_h;
}

static gboolean
flex_preset_cross_size(ns_box *c, double line_cross_size, double width_basis)
{
    if (!c->first_child) return FALSE;
    double stretched = flex_item_stretched_height(c, line_cross_size,
                                                  width_basis);
    if (stretched <= 0) return FALSE;
    c->definite_height = stretched;
    return TRUE;
}

static void
layout_flex_row(ns_box *box, double cw,
                double inner_x, double inner_y,
                const ns_style *child_inherited,
                gboolean reverse,
                double parent_content_width,
                double *cursor_y_out)
{
    const ns_css_value *hv_box = box->style ? box->style->values[NS_CSS_HEIGHT] : NULL;
    const ns_css_value *mnh_box = box->style ? box->style->values[NS_CSS_MIN_HEIGHT] : NULL;
    const ns_css_value *mxh_box = box->style ? box->style->values[NS_CSS_MAX_HEIGHT] : NULL;
    double explicit_cross = 0;
    gboolean definite_cross = FALSE;
    if (hv_box && (hv_box->kind == NS_CSS_V_LENGTH || hv_box->kind == NS_CSS_V_CALC)) {
        explicit_cross = resolve_used_height(box, hv_box, parent_content_width, 0);
        definite_cross = explicit_cross > 0;
    }
    double min_cross = resolve_used_height(box, mnh_box, parent_content_width, -1);
    double max_cross_limit = resolve_used_height(box, mxh_box,
                                                 parent_content_width, -1);
    if (box->style && box->style->values[NS_CSS_BOX_SIZING] &&
        box->style->values[NS_CSS_BOX_SIZING]->kind == NS_CSS_V_KEYWORD &&
        strcmp(box->style->values[NS_CSS_BOX_SIZING]->u.keyword, "border-box") == 0) {
        double vex = box->border.top + box->border.bottom +
                     box->padding.top + box->padding.bottom;
        if (explicit_cross > 0) {
            explicit_cross -= vex;
            if (explicit_cross < 0) explicit_cross = 0;
        }
        if (min_cross > 0) {
            min_cross -= vex;
            if (min_cross < 0) min_cross = 0;
        }
        if (max_cross_limit > 0) {
            max_cross_limit -= vex;
            if (max_cross_limit < 0) max_cross_limit = 0;
        }
    }
    if (min_cross > explicit_cross) explicit_cross = min_cross;
    if (explicit_cross <= 0 && box->style &&
        style_is_absolute_or_fixed(box->style) &&
        box->content_height > 0 &&
        (hv_box || (box->style->values[NS_CSS_TOP] &&
                    box->style->values[NS_CSS_BOTTOM])))
        explicit_cross = box->content_height;
    if (explicit_cross <= 0 && box->definite_height > 0)
        explicit_cross = box->definite_height;
    if (!definite_cross && explicit_cross > 0 && box->definite_height > 0)
        definite_cross = TRUE;

    GPtrArray *items = g_ptr_array_new();
    for (ns_box *c = box->first_child; c; c = c->next_sibling)
        if (!style_is_absolute_or_fixed(c->style))
            g_ptr_array_add(items, c);

    double gap = flex_gap_of(box->style, cw);
    double total_extras = 0;
    ns_flex_len *lens = g_new0(ns_flex_len, items->len + 1);
    for (guint i = 0; i < items->len; i++) {
        ns_box *c = items->pdata[i];
        edges_from_style(c->style, cw,
                         &c->margin, &c->padding, &c->border);
        total_extras += c->margin.left + c->margin.right +
                        c->padding.left + c->padding.right +
                        c->border.left + c->border.right;
        double b = 0;
        if (!flex_main_basis_explicit(c, cw, child_inherited, &b))
            b = flex_content_basis_from_natural(c, child_inherited);
        lens[i].basis = b;
        lens[i].min = flex_item_min_main(c, cw, child_inherited);
        lens[i].max = flex_item_max_main(c, cw, child_inherited);
        lens[i].grow = flex_grow_of(c);
        lens[i].shrink = flex_shrink_of(c);
    }
    if (items->len > 1) total_extras += gap * (items->len - 1);
    flex_resolve_lengths(lens, items->len, cw - total_extras);

    GArray *assigned_main = g_array_new(FALSE, FALSE, sizeof(double));
    GArray *measured_h    = g_array_new(FALSE, FALSE, sizeof(double));
    double max_cross = 0;
    double used_main = total_extras;
    for (guint i = 0; i < items->len; i++) {
        g_array_append_val(assigned_main, lens[i].target);
        used_main += lens[i].target;
    }
    g_free(lens);
    double free_main = cw - used_main;
    int auto_margins = 0;
    for (guint i = 0; i < items->len; i++) {
        ns_box *c = items->pdata[i];
        if (!c->style) continue;
        if (keyword_is(c->style->values[NS_CSS_MARGIN_LEFT], "auto"))  auto_margins++;
        if (keyword_is(c->style->values[NS_CSS_MARGIN_RIGHT], "auto")) auto_margins++;
    }
    if (auto_margins > 0 && free_main > 0) {
        double share = free_main / auto_margins;
        for (guint i = 0; i < items->len; i++) {
            ns_box *c = items->pdata[i];
            if (!c->style) continue;
            if (keyword_is(c->style->values[NS_CSS_MARGIN_LEFT], "auto"))
                c->margin.left += share;
            if (keyword_is(c->style->values[NS_CSS_MARGIN_RIGHT], "auto"))
                c->margin.right += share;
        }
        free_main = 0;
    }
    const char *justify = keyword_or(box->style, NS_CSS_JUSTIFY_CONTENT, "flex-start");
    double leading = 0;
    double between = 0;
    if (auto_margins == 0 || free_main < 0)
        flex_justify_offsets(box, justify, free_main, items->len, reverse,
                             &leading, &between);

    for (guint i = 0; i < items->len; i++) {
        ns_box *c = items->pdata[i];
        double a = g_array_index(assigned_main, double, i);
        c->x = inner_x;
        c->y = inner_y;
        c->flex_main_size = a;
        c->has_flex_main = TRUE;
        layout_box(c, a + c->margin.left + c->margin.right
                       + c->border.left + c->border.right
                       + c->padding.left + c->padding.right, child_inherited);
        double item_h = c->content_height +
                        c->padding.top + c->padding.bottom +
                        c->border.top + c->border.bottom +
                        c->margin.top + c->margin.bottom;
        g_array_append_val(measured_h, item_h);
        if (item_h > max_cross) max_cross = item_h;
    }

    gboolean rtl = strcmp(keyword_or(box->style, NS_CSS_DIRECTION, "ltr"),
                          "rtl") == 0;
    gboolean main_reversed = reverse != rtl;
    double cursor_x = main_reversed ? inner_x + cw - leading : inner_x + leading;
    const char *align = keyword_or(box->style, NS_CSS_ALIGN_ITEMS, "stretch");
    double cross_size = definite_cross || max_cross < explicit_cross
                      ? explicit_cross : max_cross;
    if (min_cross > cross_size) cross_size = min_cross;
    if (max_cross_limit >= 0 && cross_size > max_cross_limit) {
        cross_size = max_cross_limit;
        if (cross_size < min_cross) cross_size = min_cross;
    }

    double cross_baseline = 0;
    double cross_below_baseline = 0;
    for (guint k = 0; k < items->len; k++) {
        ns_box *c = items->pdata[k];
        if (!flex_align_is_baseline(flex_item_align(c, align))) continue;
        double item_h_full = g_array_index(measured_h, double, k);
        double b = flex_item_baseline(c, item_h_full);
        if (b > cross_baseline) cross_baseline = b;
        if (item_h_full - b > cross_below_baseline)
            cross_below_baseline = item_h_full - b;
    }
    if (!definite_cross && cross_baseline + cross_below_baseline > cross_size)
        cross_size = cross_baseline + cross_below_baseline;

    for (guint k = 0; k < items->len; k++) {
        guint i = k;
        ns_box *c = items->pdata[i];
        const char *eff_align = flex_item_align(c, align);
        double item_h_full = g_array_index(measured_h, double, i);
        gboolean mt_auto = c->style &&
            keyword_is(c->style->values[NS_CSS_MARGIN_TOP], "auto");
        gboolean mb_auto = c->style &&
            keyword_is(c->style->values[NS_CSS_MARGIN_BOTTOM], "auto");
        double cy = inner_y;
        if (mt_auto || mb_auto) {
            double free_cross = cross_size - item_h_full;
            if (free_cross < 0) free_cross = 0;
            if (mt_auto && mb_auto) cy = inner_y + free_cross / 2.0;
            else if (mt_auto)       cy = inner_y + free_cross;
        } else if (strcmp(eff_align, "center") == 0) {
            cy = inner_y + (cross_size - item_h_full) / 2.0;
        } else if (strcmp(eff_align, "flex-end") == 0 || strcmp(eff_align, "end") == 0) {
            cy = inner_y + cross_size - item_h_full;
        } else if (flex_align_is_baseline(eff_align)) {
            cy = inner_y + cross_baseline - flex_item_baseline(c, item_h_full);
        }
        double a = g_array_index(assigned_main, double, i);
        double outer_main = a + c->margin.left + c->margin.right +
                            c->padding.left + c->padding.right +
                            c->border.left + c->border.right;
        if (main_reversed) cursor_x -= outer_main;
        c->x = cursor_x;
        c->y = cy;
        c->flex_main_size = a;
        c->has_flex_main = TRUE;
        double item_layout_width = a + c->margin.left + c->margin.right
                                     + c->border.left + c->border.right
                                     + c->padding.left + c->padding.right;
        gboolean stretches = !mt_auto && !mb_auto &&
                             flex_align_stretches(eff_align) &&
                             flex_item_cross_size_auto(c);
        gboolean cross_preset = stretches &&
                                flex_preset_cross_size(c, cross_size, cw);
        layout_box(c, item_layout_width, child_inherited);
        if (!stretches)
            flex_item_fit_line_keyword_limits(c, cross_size, cw);
        if (stretches) {
            double pre_h = c->content_height;
            c->content_height = flex_item_stretched_height(c, cross_size, cw);
            if (c->definite_height <= 0)
                c->definite_height = c->content_height;
            if (!cross_preset)
                flex_relayout_after_cross_resize(c, item_layout_width, a,
                                                 pre_h, child_inherited);
        }
        if (main_reversed) cursor_x -= gap + between;
        else               cursor_x += outer_main + gap + between;
    }
    g_array_free(measured_h, TRUE);

    *cursor_y_out = inner_y + cross_size;
    g_array_free(assigned_main, TRUE);
    g_ptr_array_free(items, TRUE);
}

static void
flex_align_content_offsets(const ns_box *box, double free_cross, guint n,
                           double *lead, double *between, double *per_line)
{
    const char *acont = keyword_or(box->style, NS_CSS_ALIGN_CONTENT, "stretch");
    gboolean wrap_reverse =
        keyword_is(box->style ? box->style->values[NS_CSS_FLEX_WRAP] : NULL,
                   "wrap-reverse");
    if (strcmp(acont, "start") == 0 || strcmp(acont, "left") == 0 ||
        strcmp(acont, "self-start") == 0)
        acont = wrap_reverse ? "flex-end" : "flex-start";
    else if (strcmp(acont, "end") == 0 || strcmp(acont, "right") == 0 ||
             strcmp(acont, "self-end") == 0)
        acont = wrap_reverse ? "flex-start" : "flex-end";
    *lead = 0;
    *between = 0;
    *per_line = 0;
    if (n == 0) return;
    if (free_cross < 0) {
        if (flex_container_scrolls(box)) {
            *lead = wrap_reverse ? free_cross : 0;
            return;
        }
        if (strcmp(acont, "space-between") == 0 ||
            strcmp(acont, "space-around") == 0 ||
            strcmp(acont, "space-evenly") == 0 ||
            strcmp(acont, "stretch") == 0 || strcmp(acont, "normal") == 0)
            return;
    }
    if (strcmp(acont, "stretch") == 0 || strcmp(acont, "normal") == 0)
        *per_line = free_cross / n;
    else if (strcmp(acont, "center") == 0)
        *lead = free_cross / 2.0;
    else if (strcmp(acont, "flex-end") == 0 || strcmp(acont, "end") == 0)
        *lead = free_cross;
    else if (strcmp(acont, "space-between") == 0)
        *between = n > 1 ? free_cross / (n - 1) : 0;
    else if (strcmp(acont, "space-around") == 0) {
        *between = free_cross / n;
        *lead = *between / 2.0;
    } else if (strcmp(acont, "space-evenly") == 0) {
        *between = free_cross / (n + 1);
        *lead = *between;
    }
}

static void
layout_flex_row_wrap(ns_box *box, double cw,
                     double inner_x, double inner_y,
                     const ns_style *child_inherited,
                     gboolean reverse,
                     double *cursor_y_out)
{
    GPtrArray *items = g_ptr_array_new();
    for (ns_box *c = box->first_child; c; c = c->next_sibling)
        if (!style_is_absolute_or_fixed(c->style))
            g_ptr_array_add(items, c);
    double gap = flex_gap_of(box->style, cw);
    double row_gap = flex_gap_row_of(box->style,
                                     box->definite_height > 0
                                         ? box->definite_height : 0);
    const char *align = keyword_or(box->style, NS_CSS_ALIGN_ITEMS, "stretch");
    const char *justify = keyword_or(box->style, NS_CSS_JUSTIFY_CONTENT, "flex-start");
    gboolean rtl = strcmp(keyword_or(box->style, NS_CSS_DIRECTION, "ltr"),
                          "rtl") == 0;
    gboolean main_reversed = reverse != rtl;
    typedef struct { double top, height; guint start, count; } flex_line;
    GArray *lines = g_array_new(FALSE, FALSE, sizeof(flex_line));

    GArray *extras_arr = g_array_new(FALSE, TRUE, sizeof(double));
    GArray *main_arr   = g_array_new(FALSE, TRUE, sizeof(double));
    g_array_set_size(extras_arr, items->len);
    g_array_set_size(main_arr, items->len);
    ns_flex_len *lens = g_new0(ns_flex_len, items->len + 1);
    for (guint n = 0; n < items->len; n++) {
        ns_box *c = items->pdata[n];
        edges_from_style(c->style, cw, &c->margin, &c->padding, &c->border);
        g_array_index(extras_arr, double, n) =
            c->margin.left + c->margin.right +
            c->padding.left + c->padding.right +
            c->border.left + c->border.right;
        double b = 0;
        if (!flex_main_basis_explicit(c, cw, child_inherited, &b))
            b = flex_content_basis_from_natural(c, child_inherited);
        lens[n].basis = b;
        lens[n].min = flex_item_min_main(c, cw, child_inherited);
        lens[n].max = flex_item_max_main(c, cw, child_inherited);
        lens[n].grow = flex_grow_of(c);
        lens[n].shrink = flex_shrink_of(c);
    }

    double line_y = inner_y;
    guint i = 0;
    while (i < items->len) {
        guint line_start = i;
        double used = 0;
        double line_max_h = 0;
        guint line_count = 0;
        for (; i < items->len; i++) {
            double item_outer = flex_clamp_main(lens[i].basis, lens[i].min,
                                                lens[i].max) +
                                g_array_index(extras_arr, double, i);
            double try_used = used + (line_count > 0 ? gap : 0) + item_outer;
            if (try_used > cw + 0.5 && line_count > 0) break;
            used = try_used;
            line_count++;
        }

        double line_extras = line_count > 1 ? gap * (line_count - 1) : 0;
        for (guint k = 0; k < line_count; k++)
            line_extras += g_array_index(extras_arr, double, line_start + k);
        flex_resolve_lengths(lens + line_start, line_count, cw - line_extras);
        double remaining = cw - line_extras;
        for (guint k = 0; k < line_count; k++)
            remaining -= lens[line_start + k].target;

        double leading = 0;
        double between = 0;
        flex_justify_offsets(box, justify, remaining, line_count, reverse,
                             &leading, &between);

        for (guint k = 0; k < line_count; k++) {
            guint gi = line_start + k;
            ns_box *c = items->pdata[gi];
            double a = lens[gi].target;
            g_array_index(main_arr, double, gi) = a;
            c->x = inner_x;
            c->y = line_y;
            layout_box(c, a + g_array_index(extras_arr, double, gi),
                       child_inherited);
            double item_h = c->content_height +
                            c->padding.top + c->padding.bottom +
                            c->border.top + c->border.bottom +
                            c->margin.top + c->margin.bottom;
            if (item_h > line_max_h) line_max_h = item_h;
        }

        double line_baseline = 0;
        double line_below_baseline = 0;
        for (guint k = 0; k < line_count; k++) {
            ns_box *c = items->pdata[line_start + k];
            if (!flex_align_is_baseline(flex_item_align(c, align))) continue;
            double item_h_full = c->content_height +
                                 c->padding.top + c->padding.bottom +
                                 c->border.top + c->border.bottom +
                                 c->margin.top + c->margin.bottom;
            double b = flex_item_baseline(c, item_h_full);
            if (b > line_baseline) line_baseline = b;
            if (item_h_full - b > line_below_baseline)
                line_below_baseline = item_h_full - b;
        }
        if (line_baseline + line_below_baseline > line_max_h)
            line_max_h = line_baseline + line_below_baseline;

        double cursor_x = inner_x + leading;
        for (guint k = 0; k < line_count; k++) {
            guint idx = line_start + k;
            ns_box *c = items->pdata[idx];
            const char *eff_align = flex_item_align(c, align);
            double item_h_full = c->content_height +
                                 c->padding.top + c->padding.bottom +
                                 c->border.top + c->border.bottom +
                                 c->margin.top + c->margin.bottom;
            double cy = line_y;
            if (strcmp(eff_align, "center") == 0)
                cy = line_y + (line_max_h - item_h_full) / 2.0;
            else if (strcmp(eff_align, "flex-end") == 0 || strcmp(eff_align, "end") == 0)
                cy = line_y + line_max_h - item_h_full;
            else if (flex_align_is_baseline(eff_align))
                cy = line_y + line_baseline -
                     flex_item_baseline(c, item_h_full);
            c->x = cursor_x;
            c->y = cy;
            c->flex_main_size = g_array_index(main_arr, double, idx);
            c->has_flex_main = TRUE;
            double item_layout_width = g_array_index(main_arr, double, idx) +
                                       g_array_index(extras_arr, double, idx);
            gboolean stretches = flex_item_cross_size_auto(c) &&
                                 flex_align_stretches(eff_align);
            gboolean cross_preset = stretches &&
                                    flex_preset_cross_size(c, line_max_h, cw);
            layout_box(c, item_layout_width, child_inherited);
            double outer = c->content_width
                + c->padding.left + c->padding.right
                + c->border.left + c->border.right;
            if (stretches) {
                double pre_h = c->content_height;
                double stretched = flex_item_stretched_height(c, line_max_h, cw);
                if (stretched > c->content_height) c->content_height = stretched;
                if (!cross_preset)
                    flex_relayout_after_cross_resize(
                        c, item_layout_width,
                        g_array_index(main_arr, double, idx),
                        pre_h, child_inherited);
            }
            cursor_x += outer + c->margin.left + c->margin.right + gap + between;
        }
        if (main_reversed) {
            for (guint k = 0; k < line_count; k++) {
                ns_box *c = items->pdata[line_start + k];
                double w = c->content_width
                         + c->padding.left + c->padding.right
                         + c->border.left + c->border.right
                         + c->margin.left + c->margin.right;
                double nx = inner_x + cw - (c->x - inner_x) - w;
                if (nx != c->x) shift_box_tree(c, nx - c->x, 0);
            }
        }
        flex_line fl = { .top = line_y, .height = line_max_h,
                         .start = line_start, .count = line_count };
        g_array_append_val(lines, fl);
        line_y += line_max_h + row_gap;
    }

    double measured = (line_y - (items->len > 0 ? row_gap : 0)) - inner_y;
    double free_cross = 0;
    double container_cross = -1;
    gboolean cross_definite = FALSE;
    {
        const ns_css_value *hv = box->style
            ? box->style->values[NS_CSS_HEIGHT] : NULL;
        double eh = -1;
        if (hv && (hv->kind == NS_CSS_V_LENGTH || hv->kind == NS_CSS_V_CALC) &&
            lines->len > 0) {
            eh = length_resolve(hv, cw, -1);
            if (keyword_is(box->style->values[NS_CSS_BOX_SIZING], "border-box"))
                eh -= box->padding.top + box->padding.bottom +
                      box->border.top + box->border.bottom;
        } else if (box->definite_height > 0 && lines->len > 0) {
            eh = box->definite_height;
        }
        if (eh >= 0) {
            cross_definite = TRUE;
            container_cross = eh;
            free_cross = eh - measured;
        }
    }
    if (cross_definite && fabs(free_cross) > 0.01) {
        guint n = lines->len;
        double lead = 0, between_lines = 0, per_line = 0;
        flex_align_content_offsets(box, free_cross, n,
                                   &lead, &between_lines, &per_line);
        for (guint li = 0; li < n; li++) {
            flex_line *fl = &g_array_index(lines, flex_line, li);
            double dy = lead + (between_lines + per_line) * li;
            double line_h = fl->height + per_line;
            fl->top += dy;
            fl->height = line_h;
            for (guint k = 0; k < fl->count; k++) {
                ns_box *c = items->pdata[fl->start + k];
                if (dy != 0) shift_box_tree(c, 0, dy);
                if (per_line > 0.5) {
                    const char *eff_align = align;
                    const char *as = c->style
                        ? ns_style_keyword(c->style, NS_CSS_ALIGN_SELF) : NULL;
                    if (as && strcmp(as, "auto") != 0) eff_align = as;
                    if (flex_align_stretches(eff_align) &&
                        flex_item_cross_size_auto(c)) {
                        guint idx = fl->start + k;
                        double pre_h = c->content_height;
                        double stretched =
                            flex_item_stretched_height(c, line_h, cw);
                        if (stretched > c->content_height)
                            c->content_height = stretched;
                        flex_relayout_after_cross_resize(
                            c, g_array_index(main_arr, double, idx)
                               + g_array_index(extras_arr, double, idx),
                            g_array_index(main_arr, double, idx),
                            pre_h, child_inherited);
                    } else if (strcmp(eff_align, "center") == 0) {
                        shift_box_tree(c, 0, per_line / 2.0);
                    } else if (strcmp(eff_align, "flex-end") == 0 ||
                               strcmp(eff_align, "end") == 0) {
                        shift_box_tree(c, 0, per_line);
                    }
                }
            }
        }
        line_y += free_cross > 0 ? free_cross : 0;
    }

    *cursor_y_out = line_y - (items->len > 0 ? row_gap : 0);

    if (keyword_is(box->style->values[NS_CSS_FLEX_WRAP], "wrap-reverse")) {
        double cross_total = container_cross >= 0 ? container_cross
                                                  : *cursor_y_out - inner_y;
        for (guint li = 0; li < lines->len; li++) {
            const flex_line *fl = &g_array_index(lines, flex_line, li);
            double mirrored_top = inner_y + cross_total -
                                  (fl->top - inner_y) - fl->height;
            for (guint k = 0; k < fl->count; k++) {
                ns_box *c = items->pdata[fl->start + k];
                double outer_h = c->content_height +
                    c->margin.top + c->margin.bottom +
                    c->padding.top + c->padding.bottom +
                    c->border.top + c->border.bottom;
                double within = c->y - fl->top;
                double target = mirrored_top + fl->height - within - outer_h;
                if (target != c->y) shift_box_tree(c, 0, target - c->y);
            }
        }
    }

    g_ptr_array_free(items, TRUE);
    g_array_free(lines, TRUE);
    g_free(lens);
    g_array_free(extras_arr, TRUE);
    g_array_free(main_arr, TRUE);
}

static double
flex_item_stretch_main_height(const ns_box *c, double container_main_size)
{
    if (container_main_size < 0) return -1;
    double h = container_main_size - c->margin.top - c->margin.bottom;
    return h > 0 ? h : 0;
}

static double
flex_item_min_main_height(ns_box *c, double cw, double pct_basis)
{
    double vextra = c->padding.top + c->padding.bottom +
                    c->border.top + c->border.bottom;
    const ns_css_value *mnh = c->style ? c->style->values[NS_CSS_MIN_HEIGHT] : NULL;
    if (mnh && (mnh->kind == NS_CSS_V_LENGTH || mnh->kind == NS_CSS_V_CALC)) {
        if (value_is_percent(mnh) && pct_basis < 0) return 0;
        double mn = flex_main_height_outer(c, mnh, cw, pct_basis);
        return mn > 0 ? mn : 0;
    }
    double content = (c->measured_content_height >= 0
                      ? c->measured_content_height : c->content_height) + vextra;
    if (mnh && mnh->kind == NS_CSS_V_KEYWORD && !keyword_is(mnh, "auto")) {
        if (size_keyword_is_intrinsic(mnh))
            return content > 0 ? content : 0;
        double stretch = flex_item_stretch_main_height(c, pct_basis);
        return stretch > 0 ? stretch : 0;
    }
    if (box_clips_children(c)) return 0;
    const ns_css_value *hv = c->style ? c->style->values[NS_CSS_HEIGHT] : NULL;
    if (hv && (hv->kind == NS_CSS_V_LENGTH || hv->kind == NS_CSS_V_CALC) &&
        !(value_is_percent(hv) && pct_basis < 0)) {
        double specified = value_is_percent(hv) && flex_item_is_replaced_like(c)
            ? flex_main_height_outer(c, hv, cw, 0)
            : flex_main_height_outer(c, hv, cw, pct_basis);
        if (specified >= 0 && specified < content) content = specified;
    }
    return content > 0 ? content : 0;
}

static double
flex_item_max_main_height(ns_box *c, double cw, double pct_basis)
{
    const ns_css_value *mxh = c->style ? c->style->values[NS_CSS_MAX_HEIGHT] : NULL;
    if (size_keyword_is_intrinsic(mxh)) {
        double content = c->measured_content_height >= 0
            ? c->measured_content_height : c->content_height;
        return content + c->padding.top + c->padding.bottom +
               c->border.top + c->border.bottom;
    }
    if (height_keyword_stretches(mxh))
        return flex_item_stretch_main_height(c, pct_basis);
    if (!mxh || !(mxh->kind == NS_CSS_V_LENGTH || mxh->kind == NS_CSS_V_CALC))
        return -1;
    if (value_is_percent(mxh) && pct_basis < 0) return -1;
    return flex_main_height_outer(c, mxh, cw, pct_basis);
}

static const char *
flex_column_item_align(const ns_box *c, const char *align)
{
    const char *as = c->style ? ns_style_keyword(c->style, NS_CSS_ALIGN_SELF) : NULL;
    if (as && strcmp(as, "auto") != 0) return as;
    return align;
}

static gboolean
flex_column_item_shrinks_to_fit(const ns_box *c, const char *align)
{
    const char *eff = flex_column_item_align(c, align);
    return strcmp(eff, "stretch") != 0 && strcmp(eff, "normal") != 0;
}

static void
flex_stretch_replaced_width(ns_box *c, double line_w)
{
    if (c->kind != NS_BOX_IMAGE && c->kind != NS_BOX_VIDEO &&
        c->kind != NS_BOX_SVG)
        return;
    const ns_style *s = c->style;
    if (!s || length_is_auto(s->values[NS_CSS_MARGIN_LEFT]) ||
        length_is_auto(s->values[NS_CSS_MARGIN_RIGHT]))
        return;
    double hextra = c->padding.left + c->padding.right +
                    c->border.left + c->border.right;
    double w = line_w - c->margin.left - c->margin.right - hextra;
    gboolean border_box =
        keyword_is(s->values[NS_CSS_BOX_SIZING], "border-box");
    double max_w = length_resolve(s->values[NS_CSS_MAX_WIDTH], line_w, -1);
    double min_w = length_resolve(s->values[NS_CSS_MIN_WIDTH], line_w, -1);
    if (border_box) {
        if (max_w >= 0) max_w = MAX(max_w - hextra, 0);
        if (min_w >= 0) min_w = MAX(min_w - hextra, 0);
    }
    if (max_w >= 0 && w > max_w) w = max_w;
    if (min_w >= 0 && w < min_w) w = min_w;
    if (w < 0) w = 0;
    const ns_css_value *hv = s->values[NS_CSS_HEIGHT];
    gboolean height_auto =
        !(hv && (hv->kind == NS_CSS_V_LENGTH || hv->kind == NS_CSS_V_CALC));
    if (height_auto && c->content_width > 0 && c->content_height > 0)
        c->content_height = w * c->content_height / c->content_width;
    c->content_width = w;
}

static void
flex_column_layout_item(ns_box *c, double cw, double line_w, gboolean fit,
                        const ns_style *child_inherited)
{
    const ns_css_value *wv = c->style ? c->style->values[NS_CSS_WIDTH] : NULL;
    gboolean width_explicit = wv &&
        (wv->kind == NS_CSS_V_LENGTH || wv->kind == NS_CSS_V_CALC);
    double parent_w = line_w;
    if (fit && !width_explicit) {
        double nat = measure_natural_width(c, child_inherited);
        double avail = line_w - c->margin.left - c->margin.right -
                       c->padding.left - c->padding.right -
                       c->border.left - c->border.right;
        if (nat > avail) nat = avail;
        if (nat < 0) nat = 0;
        parent_w = nat + c->margin.left + c->margin.right +
                   c->padding.left + c->padding.right +
                   c->border.left + c->border.right;
    }
    (void)cw;
    c->definite_height = 0;
    c->measured_content_height = -1;
    layout_box(c, parent_w, child_inherited);
    if (!fit && !width_explicit)
        flex_stretch_replaced_width(c, line_w);
}

static double
flex_item_outer_width(const ns_box *c)
{
    return c->content_width
        + c->padding.left + c->padding.right
        + c->border.left + c->border.right
        + c->margin.left + c->margin.right;
}

static void
layout_flex_column(ns_box *box, double cw,
                   double inner_x, double inner_y,
                   const ns_style *child_inherited,
                   gboolean reverse,
                   double parent_content_height,
                   double *cursor_y_out)
{
    GPtrArray *items = g_ptr_array_new();
    for (ns_box *c = box->first_child; c; c = c->next_sibling)
        if (!style_is_absolute_or_fixed(c->style))
            g_ptr_array_add(items, c);

    const char *align = keyword_or(box->style, NS_CSS_ALIGN_ITEMS, "stretch");
    const char *justify = keyword_or(box->style, NS_CSS_JUSTIFY_CONTENT, "flex-start");

    const ns_css_value *hv = box->style ? box->style->values[NS_CSS_HEIGHT] : NULL;
    const ns_css_value *mnh = box->style ? box->style->values[NS_CSS_MIN_HEIGHT] : NULL;
    const ns_css_value *mxh = box->style ? box->style->values[NS_CSS_MAX_HEIGHT] : NULL;
    double explicit_h = -1;
    if (hv && (hv->kind == NS_CSS_V_LENGTH || hv->kind == NS_CSS_V_CALC))
        explicit_h = resolve_used_height(box, hv, cw, -1);
    if (explicit_h < 0 && box->style &&
        style_is_absolute_or_fixed(box->style) &&
        box->content_height > 0 &&
        (hv || (box->style->values[NS_CSS_TOP] && box->style->values[NS_CSS_BOTTOM])))
        explicit_h = box->content_height;
    if (explicit_h < 0 && box->style) {
        double ratio = aspect_ratio_number(box->style->values[NS_CSS_ASPECT_RATIO], NULL);
        if (ratio > 0 && cw > 0)
            explicit_h = cw / ratio;
    }
    if (explicit_h < 0 && box->definite_height > 0)
        explicit_h = box->definite_height;
    if (explicit_h > 0) box->definite_height = explicit_h;
    double row_gap = flex_gap_row_of(box->style,
                                     explicit_h > 0 ? explicit_h : 0);
    double col_gap = flex_gap_of(box->style, cw);
    double min_h = resolve_used_height(box, mnh, parent_content_height, -1);
    double max_h = resolve_used_height(box, mxh, parent_content_height, -1);
    if (keyword_is(box->style ? box->style->values[NS_CSS_BOX_SIZING] : NULL,
                   "border-box")) {
        double vex = box->border.top + box->border.bottom +
                     box->padding.top + box->padding.bottom;
        if (explicit_h > 0) explicit_h = MAX(explicit_h - vex, 0);
        if (min_h > 0) min_h = MAX(min_h - vex, 0);
        if (max_h >= 0) max_h = MAX(max_h - vex, 0);
    }
    double percentage_basis_h = explicit_h;
    if (min_h > explicit_h) explicit_h = min_h;
    if (max_h >= 0 && explicit_h > max_h) explicit_h = max_h;

    double line_limit = explicit_h > 0 ? explicit_h : max_h;
    gboolean multi_line = flex_wraps(box->style);
    gboolean wraps = multi_line && line_limit > 0;
    gboolean wrap_reverse =
        keyword_is(box->style ? box->style->values[NS_CSS_FLEX_WRAP] : NULL,
                   "wrap-reverse");
    gboolean rtl = strcmp(keyword_or(box->style, NS_CSS_DIRECTION, "ltr"),
                          "rtl") == 0;
    gboolean cross_start_right = wrap_reverse != rtl;

    ns_flex_len *lens = g_new0(ns_flex_len, items->len + 1);
    GArray *contribs = g_array_new(FALSE, FALSE, sizeof(double));
    for (guint i = 0; i < items->len; i++) {
        ns_box *c = items->pdata[i];
        edges_from_style(c->style, cw, &c->margin, &c->padding, &c->border);
        c->x = inner_x;
        c->y = inner_y;
        flex_column_layout_item(c, cw, cw,
                                multi_line ||
                                flex_column_item_shrinks_to_fit(c, align),
                                child_inherited);
        gboolean exp = FALSE;
        double b = flex_basis_main_height(c, cw, percentage_basis_h, &exp);
        if (!exp)
            b = c->content_height +
                c->padding.top + c->padding.bottom +
                c->border.top + c->border.bottom;
        lens[i].basis = b;
        lens[i].min = flex_item_min_main_height(c, cw, percentage_basis_h);
        lens[i].max = flex_item_max_main_height(c, cw, percentage_basis_h);
        lens[i].grow = flex_grow_of(c);
        lens[i].shrink = flex_shrink_of(c);
        double contrib = c->content_height +
                         c->padding.top + c->padding.bottom +
                         c->border.top + c->border.bottom;
        if (contrib < b && exp) contrib = b;
        g_array_append_val(contribs, contrib);
    }

    typedef struct { guint start, count; double cross, x; } col_line;
    GArray *lines = g_array_new(FALSE, FALSE, sizeof(col_line));
    guint i = 0;
    while (i < items->len) {
        col_line ln = { .start = i, .count = 0, .cross = 0, .x = inner_x };
        double used = 0;
        for (; i < items->len; i++) {
            ns_box *c = items->pdata[i];
            double outer = flex_clamp_main(lens[i].basis, lens[i].min, lens[i].max)
                         + c->margin.top + c->margin.bottom;
            double try_used = used + (ln.count > 0 ? row_gap : 0) + outer;
            if (wraps && try_used > line_limit + 0.5 && ln.count > 0) break;
            used = try_used;
            ln.count++;
            double w = flex_item_outer_width(c);
            if (w > ln.cross) ln.cross = w;
        }
        g_array_append_val(lines, ln);
        if (ln.count == 0) break;
    }

    double lines_cross = 0;
    for (guint li = 0; li < lines->len; li++)
        lines_cross += g_array_index(lines, col_line, li).cross;
    if (lines->len > 1) lines_cross += col_gap * (lines->len - 1);
    if (lines->len == 0) {
        col_line empty = { .start = 0, .count = 0, .cross = cw, .x = inner_x };
        g_array_append_val(lines, empty);
    }
    if (!multi_line) {
        g_array_index(lines, col_line, 0).cross = cw;
    } else {
        double lead, between_lines, per_line;
        flex_align_content_offsets(box, cw - lines_cross, lines->len,
                                   &lead, &between_lines, &per_line);
        double x = inner_x + lead;
        for (guint li = 0; li < lines->len; li++) {
            col_line *ln = &g_array_index(lines, col_line, li);
            ln->cross += per_line;
            ln->x = x;
            x += ln->cross + col_gap + between_lines;
        }
        if (cross_start_right)
            for (guint li = 0; li < lines->len; li++) {
                col_line *ln = &g_array_index(lines, col_line, li);
                ln->x = inner_x + cw - (ln->x - inner_x) - ln->cross;
            }
    }

    double main_extent = 0;
    for (guint li = 0; li < lines->len; li++) {
        col_line *ln = &g_array_index(lines, col_line, li);
        double gaps = ln->count > 1 ? row_gap * (ln->count - 1) : 0;
        double margins = 0;
        double sum_hyp = 0;
        int auto_margins = 0;
        for (guint k = 0; k < ln->count; k++) {
            ns_box *c = items->pdata[ln->start + k];
            margins += c->margin.top + c->margin.bottom;
            sum_hyp += flex_clamp_main(lens[ln->start + k].basis,
                                       lens[ln->start + k].min,
                                       lens[ln->start + k].max);
            if (c->style) {
                if (keyword_is(c->style->values[NS_CSS_MARGIN_TOP], "auto")) auto_margins++;
                if (keyword_is(c->style->values[NS_CSS_MARGIN_BOTTOM], "auto")) auto_margins++;
            }
        }
        double avail = explicit_h > 0 ? explicit_h : sum_hyp + margins + gaps;
        if (explicit_h <= 0) {
            double fraction = 0;
            for (guint k = 0; k < ln->count; k++) {
                const ns_flex_len *l = &lens[ln->start + k];
                double contrib = g_array_index(contribs, double, ln->start + k);
                double diff = contrib - l->basis;
                double f = diff > 0 ? diff / MAX(l->grow, 1.0)
                         : (l->shrink * l->basis > 0 ? diff / (l->shrink * l->basis) : 0);
                if (f > fraction) fraction = f;
            }
            double sum = margins + gaps;
            for (guint k = 0; k < ln->count; k++) {
                const ns_flex_len *l = &lens[ln->start + k];
                double size = l->basis + fraction * l->grow;
                sum += flex_clamp_main(size, l->min, l->max);
            }
            if (sum > avail) avail = sum;
        }
        if (max_h >= 0 && avail > max_h) avail = max_h;
        if (avail < min_h) avail = min_h;
        flex_resolve_lengths(lens + ln->start, ln->count, avail - margins - gaps);
        double free_main = avail - margins - gaps;
        for (guint k = 0; k < ln->count; k++)
            free_main -= lens[ln->start + k].target;
        double leading = 0, between = 0;
        if (auto_margins > 0 && free_main > 0) {
            double share = free_main / auto_margins;
            for (guint k = 0; k < ln->count; k++) {
                ns_box *c = items->pdata[ln->start + k];
                if (!c->style) continue;
                if (keyword_is(c->style->values[NS_CSS_MARGIN_TOP], "auto"))
                    c->margin.top += share;
                if (keyword_is(c->style->values[NS_CSS_MARGIN_BOTTOM], "auto"))
                    c->margin.bottom += share;
            }
            free_main = 0;
        } else {
            flex_justify_offsets(box, justify, free_main, ln->count, reverse,
                                 &leading, &between);
        }
        if (avail > main_extent) main_extent = avail;

        double cursor_y = reverse ? inner_y + avail - leading : inner_y + leading;
        for (guint k = 0; k < ln->count; k++) {
            guint idx = ln->start + k;
            ns_box *c = items->pdata[idx];
            double main_size = lens[idx].target;
            double vextra = c->padding.top + c->padding.bottom +
                            c->border.top + c->border.bottom;
            const ns_css_value *mlv = c->style ? c->style->values[NS_CSS_MARGIN_LEFT] : NULL;
            const ns_css_value *mrv = c->style ? c->style->values[NS_CSS_MARGIN_RIGHT] : NULL;
            const ns_css_value *wv = c->style ? c->style->values[NS_CSS_WIDTH] : NULL;
            gboolean ml_auto = length_is_auto(mlv);
            gboolean mr_auto = length_is_auto(mrv);
            gboolean width_explicit = wv &&
                (wv->kind == NS_CSS_V_LENGTH || wv->kind == NS_CSS_V_CALC);
            const char *eff_align = flex_column_item_align(c, align);
            gboolean stretches = !ml_auto && !mr_auto && !width_explicit &&
                (strcmp(eff_align, "stretch") == 0 ||
                 strcmp(eff_align, "normal") == 0);
            gboolean at_right = FALSE;
            gboolean centered = strcmp(eff_align, "center") == 0;
            if (strcmp(eff_align, "flex-end") == 0)
                at_right = !cross_start_right;
            else if (strcmp(eff_align, "end") == 0 ||
                     strcmp(eff_align, "self-end") == 0)
                at_right = !rtl;
            else if (strcmp(eff_align, "start") == 0 ||
                     strcmp(eff_align, "self-start") == 0)
                at_right = rtl;
            else if (strcmp(eff_align, "right") == 0)
                at_right = TRUE;
            else if (strcmp(eff_align, "left") == 0)
                at_right = FALSE;
            else if (!centered)
                at_right = cross_start_right;
            if ((stretches || ml_auto || mr_auto) &&
                fabs(flex_item_outer_width(c) - ln->cross) > 0.01) {
                c->x = inner_x;
                c->y = inner_y;
                flex_column_layout_item(c, cw, ln->cross, FALSE, child_inherited);
            }
            double item_outer_w = flex_item_outer_width(c);
            double cx = ln->x;
            if (ml_auto || mr_auto)
                cx = ln->x;
            else if (centered)
                cx = ln->x + (ln->cross - item_outer_w) / 2.0;
            else if (at_right)
                cx = ln->x + ln->cross - item_outer_w;
            double outer_main = main_size + c->margin.top + c->margin.bottom;
            if (reverse) cursor_y -= outer_main;
            double dx = cx - c->x;
            double dy = cursor_y - c->y;
            if (dx != 0 || dy != 0) shift_box_tree(c, dx, dy);
            c->x = cx;
            c->y = cursor_y;

            double target_h = main_size - vextra;
            if (target_h < 0) target_h = 0;
            if (fabs(target_h - c->content_height) > 0.01) {
                double natural_h = c->content_height;
                gboolean shrank = target_h < natural_h;
                c->content_height = target_h;
                const char *covy = c->style
                    ? overflow_axis_keyword(c->style, NS_CSS_OVERFLOW_Y) : NULL;
                if (shrank && overflow_kw_scrolls(covy)) {
                    c->scrolls = TRUE;
                    c->scroll_max_y = natural_h - target_h;
                    if (c->scroll_max_y < 0) c->scroll_max_y = 0;
                }
                if (c->first_child && c->definite_height != target_h) {
                    c->definite_height = target_h;
                    double sx = c->x, sy = c->y;
                    layout_box(c, stretches ? ln->cross : item_outer_w,
                               child_inherited);
                    if (c->x != sx || c->y != sy)
                        shift_box_tree(c, sx - c->x, sy - c->y);
                    c->content_height = target_h;
                }
            }
            if (reverse) cursor_y -= row_gap + between;
            else         cursor_y += outer_main + row_gap + between;
        }
    }

    *cursor_y_out = inner_y + main_extent;
    g_free(lens);
    g_array_free(contribs, TRUE);
    g_array_free(lines, TRUE);
    g_ptr_array_free(items, TRUE);
}

static double
track_min_px(const ns_css_track *t, double avail)
{
    if (!t->has_min) return 0;
    switch (t->min_kind) {
    case NS_CSS_TRACK_PX:      return t->min_v + t->min_pct * avail / 100.0;
    case NS_CSS_TRACK_PERCENT: return t->min_v * avail / 100.0;
    default: return 0;
    }
}

static gboolean
justify_kw_stretches_auto(const char *jc)
{
    return strcmp(jc, "normal") == 0 || strcmp(jc, "stretch") == 0;
}

static gboolean
track_is_intrinsic(ns_css_track_kind k)
{
    return k == NS_CSS_TRACK_AUTO || k == NS_CSS_TRACK_MIN_CONTENT ||
           k == NS_CSS_TRACK_MAX_CONTENT;
}

static void
resolve_track_sizes_full(const ns_css_tracks *tr, double available_main,
                         const double *content_min, const double *content_max,
                         double *out_sizes, gboolean stretch_auto)
{
    double total_fixed = 0;
    double total_fr    = 0;
    double total_shrink = 0;
    double fixed_px[NS_CSS_TRACKS_MAX] = {0};
    double shrink_px[NS_CSS_TRACKS_MAX] = {0};
    int    n_auto      = 0;
    for (int i = 0; i < tr->n; i++) {
        const ns_css_track *t = &tr->tracks[i];
        double fixed = 0;
        switch (t->kind) {
        case NS_CSS_TRACK_PX:
        case NS_CSS_TRACK_PERCENT:
            fixed = t->kind == NS_CSS_TRACK_PX
                ? t->v + t->pct * available_main / 100.0
                : t->v * available_main / 100.0;
            if (t->has_min && track_is_intrinsic(t->min_kind) &&
                content_min && content_min[i] > fixed)
                fixed = content_min[i];
            fixed_px[i] = fixed;
            total_fixed += fixed;
            break;
        case NS_CSS_TRACK_FR:      total_fr += t->v > 0 ? t->v : 0; break;
        case NS_CSS_TRACK_AUTO:    n_auto++; break;
        case NS_CSS_TRACK_MIN_CONTENT:
        case NS_CSS_TRACK_MAX_CONTENT:
            break;
        }
        if ((t->kind == NS_CSS_TRACK_PX || t->kind == NS_CSS_TRACK_PERCENT) &&
            t->has_min) {
            double mn = track_min_px(t, available_main);
            if (fixed > mn) {
                shrink_px[i] = fixed - mn;
                total_shrink += shrink_px[i];
            }
        }
    }
    double auto_base[NS_CSS_TRACKS_MAX] = {0};
    double auto_lim[NS_CSS_TRACKS_MAX]  = {0};
    if (content_min) {
        double base_sum = 0, auto_sum = 0, content_sum = 0;
        for (int i = 0; i < tr->n; i++) {
            if (!track_is_intrinsic(tr->tracks[i].kind)) continue;
            auto_base[i] = content_min[i] > 0 ? content_min[i] : 0;
            double lim = content_max ? content_max[i] : auto_base[i];
            auto_lim[i] = lim > auto_base[i] ? lim : auto_base[i];
            if (tr->tracks[i].kind == NS_CSS_TRACK_MAX_CONTENT)
                auto_base[i] = auto_lim[i];
            base_sum += auto_base[i];
            if (tr->tracks[i].kind == NS_CSS_TRACK_AUTO)
                auto_sum += auto_base[i];
            else
                content_sum += auto_base[i];
        }
        double free_for_auto = available_main - (total_fixed - total_shrink);
        if (free_for_auto < 0) free_for_auto = 0;
        double room = free_for_auto - content_sum;
        if (room < 0) room = 0;
        if (auto_sum > room && auto_sum > 0) {
            double scale = room / auto_sum;
            for (int i = 0; i < tr->n; i++) {
                if (tr->tracks[i].kind != NS_CSS_TRACK_AUTO) continue;
                auto_base[i] *= scale;
                if (auto_lim[i] < auto_base[i]) auto_lim[i] = auto_base[i];
            }
            base_sum = content_sum + room;
        }
        total_fixed += base_sum;
    }

    double shrink_used = 0;
    if (total_fixed > available_main && total_shrink > 0) {
        shrink_used = total_fixed - available_main;
        if (shrink_used > total_shrink) shrink_used = total_shrink;
        total_fixed -= shrink_used;
    }

    double remaining = available_main - total_fixed;
    if (remaining < 0) remaining = 0;

    double auto_grow[NS_CSS_TRACKS_MAX] = {0};
    if (n_auto > 0 && remaining > 0 && content_min) {
        double room_total = 0;
        for (int i = 0; i < tr->n; i++) {
            if (tr->tracks[i].kind != NS_CSS_TRACK_AUTO) continue;
            double room = auto_lim[i] - auto_base[i];
            if (room > 0) room_total += room;
        }
        if (room_total > 0) {
            double give = remaining < room_total ? remaining : room_total;
            for (int i = 0; i < tr->n; i++) {
                if (tr->tracks[i].kind != NS_CSS_TRACK_AUTO) continue;
                double room = auto_lim[i] - auto_base[i];
                if (room > 0) auto_grow[i] = give * (room / room_total);
            }
            remaining -= give;
        }
    }

    double per_fr   = total_fr > 0 ? remaining / MAX(total_fr, 1.0) : 0;
    double per_auto = 0;
    if (total_fr == 0 && n_auto > 0 && stretch_auto)
        per_auto = remaining / n_auto;

    for (int i = 0; i < tr->n; i++) {
        const ns_css_track *t = &tr->tracks[i];
        switch (t->kind) {
        case NS_CSS_TRACK_PX:
        case NS_CSS_TRACK_PERCENT:
            out_sizes[i] = fixed_px[i];
            if (shrink_used > 0 && shrink_px[i] > 0)
                out_sizes[i] -= shrink_used * (shrink_px[i] / total_shrink);
            break;
        case NS_CSS_TRACK_FR:      out_sizes[i] = per_fr * (t->v > 0 ? t->v : 0); break;
        case NS_CSS_TRACK_AUTO:
            out_sizes[i] = auto_base[i] + auto_grow[i] + per_auto;
            break;
        case NS_CSS_TRACK_MIN_CONTENT:
            out_sizes[i] = auto_base[i];
            break;
        case NS_CSS_TRACK_MAX_CONTENT:
            out_sizes[i] = auto_lim[i] > auto_base[i] ? auto_lim[i] : auto_base[i];
            break;
        }
        double mn = track_min_px(t, available_main);
        if (out_sizes[i] < mn) out_sizes[i] = mn;
        if (out_sizes[i] < 0) out_sizes[i] = 0;
    }
}

static double
grid_track_repeat_px(const ns_css_track *t, double available_main)
{
    double min_px = track_min_px(t, available_main);
    if (t->kind == NS_CSS_TRACK_PX || t->kind == NS_CSS_TRACK_PERCENT) {
        double max_px = t->kind == NS_CSS_TRACK_PX
            ? t->v + t->pct * available_main / 100.0
            : t->v * available_main / 100.0;
        return max_px > min_px ? max_px : min_px;
    }
    return min_px;
}

static ns_css_tracks
expand_auto_repeat_ex(const ns_css_tracks *tr, double available_main, double gap,
                      int *fit_start, int *fit_count)
{
    ns_css_tracks out = *tr;
    if (fit_start) *fit_start = 0;
    if (fit_count) *fit_count = 0;
    if (tr->auto_repeat == NS_CSS_AUTO_REPEAT_NONE) return out;
    if (tr->auto_repeat_count <= 0) return out;

    double base_min = 0;
    for (int i = 0; i < tr->auto_repeat_count; i++) {
        const ns_css_track *t = &tr->tracks[tr->auto_repeat_start + i];
        double m = grid_track_repeat_px(t, available_main);
        if (m <= 0) {
            memset(&out, 0, sizeof(out));
            out.n = 1;
            out.tracks[0].kind = NS_CSS_TRACK_AUTO;
            return out;
        }
        base_min += m;
    }
    if (base_min <= 0) return out;
    double others = 0;
    int n_others = tr->n - tr->auto_repeat_count;
    for (int i = 0; i < tr->n; i++) {
        if (i >= tr->auto_repeat_start &&
            i < tr->auto_repeat_start + tr->auto_repeat_count)
            continue;
        double m = grid_track_repeat_px(&tr->tracks[i], available_main);
        if (m > 0) others += m;
    }
    double pattern_with_gap = base_min + gap * tr->auto_repeat_count;
    double room = available_main - others - (n_others - 1) * gap;
    int repeats = 1;
    if (pattern_with_gap > 0)
        repeats = (int)(room / pattern_with_gap);
    if (repeats < 1) repeats = 1;
    if (repeats > NS_CSS_TRACKS_MAX) repeats = NS_CSS_TRACKS_MAX;

    int prefix = tr->auto_repeat_start;
    int suffix_start = tr->auto_repeat_start + tr->auto_repeat_count;
    int suffix_count = tr->n - suffix_start;
    int total = prefix + repeats * tr->auto_repeat_count + suffix_count;
    if (total > NS_CSS_TRACKS_MAX)
        repeats = (NS_CSS_TRACKS_MAX - prefix - suffix_count) /
                  tr->auto_repeat_count;
    if (repeats < 1) repeats = 1;

    out.n = 0;
    for (int i = 0; i < prefix && out.n < NS_CSS_TRACKS_MAX; i++)
        out.tracks[out.n++] = tr->tracks[i];
    for (int r = 0; r < repeats; r++) {
        for (int i = 0; i < tr->auto_repeat_count &&
                        out.n < NS_CSS_TRACKS_MAX; i++)
            out.tracks[out.n++] = tr->tracks[tr->auto_repeat_start + i];
    }
    for (int i = 0; i < suffix_count && out.n < NS_CSS_TRACKS_MAX; i++)
        out.tracks[out.n++] = tr->tracks[suffix_start + i];
    out.auto_repeat = NS_CSS_AUTO_REPEAT_NONE;
    if (tr->auto_repeat == NS_CSS_AUTO_REPEAT_FIT) {
        if (fit_start) *fit_start = prefix;
        if (fit_count) *fit_count = repeats * tr->auto_repeat_count;
    }
    return out;
}

typedef struct grid_lines {
    const ns_css_tracks *tracks;
    const ns_css_areas  *areas;
    gboolean row_axis;
} grid_lines;

static const grid_lines *g_grid_lines;

static int
grid_named_line(const char *name, gsize len, int after)
{
    const grid_lines *gl = g_grid_lines;
    if (!gl || !name || !len) return 0;
    if (gl->tracks) {
        for (int i = 0; i < gl->tracks->n_line_names; i++) {
            const ns_css_line_name *ln = &gl->tracks->line_names[i];
            if (ln->line > after && strlen(ln->name) == len &&
                strncmp(ln->name, name, len) == 0)
                return ln->line;
        }
    }
    if (gl->areas && len > 4) {
        gsize base = 0;
        gboolean want_end = FALSE;
        if (len > 6 && strncmp(name + len - 6, "-start", 6) == 0)
            base = len - 6;
        else if (len > 4 && strncmp(name + len - 4, "-end", 4) == 0) {
            base = len - 4;
            want_end = TRUE;
        }
        if (base) {
            for (int i = 0; i < gl->areas->n_rects; i++) {
                const ns_css_area_rect *a = &gl->areas->rects[i];
                if (!a->name || strlen(a->name) != base ||
                    strncmp(a->name, name, base) != 0)
                    continue;
                int line = gl->row_axis ? (want_end ? a->r1 + 2 : a->r0 + 1)
                                        : (want_end ? a->c1 + 2 : a->c0 + 1);
                if (line > after) return line;
            }
        }
    }
    if (gl->areas) {
        for (int i = 0; i < gl->areas->n_rects; i++) {
            const ns_css_area_rect *a = &gl->areas->rects[i];
            if (!a->name || strlen(a->name) != len ||
                strncmp(a->name, name, len) != 0)
                continue;
            int start = gl->row_axis ? a->r0 + 1 : a->c0 + 1;
            int end = gl->row_axis ? a->r1 + 2 : a->c1 + 2;
            if (start > after) return start;
            if (end > after) return end;
        }
    }
    return 0;
}

static int
grid_resolve_line_from(const char *s, int n_tracks, int after)
{
    if (!s) return 0;
    while (*s == ' ') s++;
    gsize len = strlen(s);
    while (len > 0 && s[len - 1] == ' ') len--;
    char *end = NULL;
    long n = strtol(s, &end, 10);
    if (end == s) {
        int named = grid_named_line(s, len, after);
        if (named > 0) return named;
        return 0;
    }
    while (end && *end == ' ') end++;
    if (!end || *end != '\0' || n == 0) return 0;
    if (n < 0) n = n_tracks + 2 + n;
    if (n < 1 || n > NS_CSS_TRACKS_MAX + 1) return 0;
    return (int)n;
}

static int
grid_resolve_line_number(const char *s, int n_tracks)
{
    return grid_resolve_line_from(s, n_tracks, 0);
}

static int
grid_pos_span(const ns_css_value *v, int n_tracks,
              int *out_start, int *out_span)
{
    *out_start = 0;
    *out_span  = 1;
    if (!v || v->kind != NS_CSS_V_KEYWORD || !v->u.keyword) return 0;
    const char *s = v->u.keyword;
    if (g_str_has_prefix(s, "span ")) {
        *out_span = ns_parse_int(s + 5, 1, 1, NS_CSS_TRACKS_MAX);
        return 0;
    }
    const char *slash = strchr(s, '/');
    if (slash) {
        char *a = g_strndup(s, slash - s);
        const char *b = slash + 1;
        while (*b == ' ') b++;
        int n = grid_resolve_line_number(a, n_tracks);
        *out_start = n > 0 ? n - 1 : 0;
        g_free(a);
        if (g_str_has_prefix(b, "span ")) {
            *out_span = ns_parse_int(b + 5, 1, 1, NS_CSS_TRACKS_MAX);
        } else {
            int e = grid_resolve_line_from(b, n_tracks, n > 0 ? n : 0);
            if (n > 0 && e > n) *out_span = e - n;
        }
        return n > 0;
    }
    int n = grid_resolve_line_number(s, n_tracks);
    if (n > 0) { *out_start = n - 1; return 1; }
    return 0;
}

static int
grid_line_num(const ns_css_value *v, int n_tracks, int after)
{
    if (!v || v->kind != NS_CSS_V_KEYWORD || !v->u.keyword) return 0;
    if (g_str_has_prefix(v->u.keyword, "span ")) return 0;
    return grid_resolve_line_from(v->u.keyword, n_tracks, after);
}

static int
grid_area_axis_pos(const ns_style *st, gboolean row_axis, int n_tracks,
                   int *out_start, int *out_span)
{
    const ns_css_value *v = st ? st->values[NS_CSS_GRID_AREA] : NULL;
    if (!v || v->kind != NS_CSS_V_KEYWORD || !v->u.keyword) return 0;
    char **parts = g_strsplit(v->u.keyword, "/", -1);
    int n = 0;
    while (parts[n]) n++;
    char *sstr = row_axis ? (n > 0 ? parts[0] : NULL)
                          : (n > 1 ? parts[1] : NULL);
    char *estr = row_axis ? (n > 2 ? parts[2] : NULL)
                          : (n > 3 ? parts[3] : NULL);
    int got = 0;
    if (sstr) {
        char *ss = g_strstrip(sstr);
        int s = grid_resolve_line_number(ss, n_tracks);
        if (g_str_has_prefix(ss, "span "))
            *out_span = ns_parse_int(ss + 5, 1, 1, NS_CSS_TRACKS_MAX);
        if (s > 0) {
            *out_start = s - 1;
            *out_span = 1;
            got = 1;
        }
    }
    if (!estr && got && sstr) {
        char *ss = g_strstrip(sstr);
        char *tail = NULL;
        strtol(ss, &tail, 10);
        if (tail == ss) {
            int e = grid_resolve_line_from(ss, n_tracks, *out_start + 1);
            if (e > *out_start + 1) *out_span = e - (*out_start + 1);
        }
    }
    if (estr) {
        char *es = g_strstrip(estr);
        if (g_str_has_prefix(es, "span ")) {
            *out_span = ns_parse_int(es + 5, 1, 1, NS_CSS_TRACKS_MAX);
        } else if (got) {
            int e = grid_resolve_line_from(es, n_tracks, *out_start + 1);
            if (e > *out_start + 1) *out_span = e - (*out_start + 1);
        }
    }
    g_strfreev(parts);
    return got;
}

static int
grid_resolve_pos(const ns_style *st, ns_css_prop shorthand,
                 ns_css_prop start_prop, ns_css_prop end_prop,
                 int n_tracks,
                 int *out_start, int *out_span)
{
    int got = grid_pos_span(st ? st->values[shorthand] : NULL, n_tracks,
                            out_start, out_span);
    if (got) return 1;
    if (!st) return 0;
    if (grid_area_axis_pos(st, start_prop == NS_CSS_GRID_ROW_START, n_tracks,
                           out_start, out_span))
        return 1;
    int sl = grid_line_num(st->values[start_prop], n_tracks, 0);
    int el = grid_line_num(st->values[end_prop], n_tracks, sl);
    if (sl > 0) {
        *out_start = sl - 1;
        const ns_css_value *ev = st->values[end_prop];
        if (el > sl)
            *out_span = el - sl;
        else if (ev && ev->kind == NS_CSS_V_KEYWORD && ev->u.keyword &&
                 g_str_has_prefix(ev->u.keyword, "span "))
            *out_span = ns_parse_int(ev->u.keyword + 5, 1, 1,
                                     NS_CSS_TRACKS_MAX);
        return 1;
    }
    const ns_css_value *sv = st->values[start_prop];
    if (sv && sv->kind == NS_CSS_V_KEYWORD && sv->u.keyword &&
        g_str_has_prefix(sv->u.keyword, "span ")) {
        *out_span = ns_parse_int(sv->u.keyword + 5, 1, 1, NS_CSS_TRACKS_MAX);
        return 0;
    }
    const ns_css_value *ev = st->values[end_prop];
    if (ev && ev->kind == NS_CSS_V_KEYWORD && ev->u.keyword &&
        g_str_has_prefix(ev->u.keyword, "span "))
        *out_span = ns_parse_int(ev->u.keyword + 5, 1, 1, NS_CSS_TRACKS_MAX);
    return 0;
}

#define NS_GRID_ROWS_MAX 4096

typedef gboolean grid_occupancy_row[NS_CSS_TRACKS_MAX];

static GArray *
grid_occupancy_new(void)
{
    return g_array_new(FALSE, TRUE, sizeof(grid_occupancy_row));
}

static gboolean
grid_occupied(const GArray *occupied, int row, int col)
{
    if (row < 0 || col < 0 || col >= NS_CSS_TRACKS_MAX) return TRUE;
    if ((guint)row >= occupied->len) return FALSE;
    return g_array_index(occupied, grid_occupancy_row, row)[col];
}

static void
grid_occupy(GArray *occupied, int row, int col)
{
    if (row < 0 || row >= NS_GRID_ROWS_MAX ||
        col < 0 || col >= NS_CSS_TRACKS_MAX)
        return;
    if ((guint)row >= occupied->len) g_array_set_size(occupied, row + 1);
    g_array_index(occupied, grid_occupancy_row, row)[col] = TRUE;
}

static gboolean
grid_slot_available(const GArray *occupied,
                    int row, int col, int col_span, int row_span,
                    int n_cols)
{
    if (row < 0 || col < 0 || col_span < 1 || row_span < 1)
        return FALSE;
    if (col + col_span > n_cols || row + row_span > NS_GRID_ROWS_MAX)
        return FALSE;
    for (int r = 0; r < row_span; r++) {
        for (int c = 0; c < col_span; c++) {
            if (grid_occupied(occupied, row + r, col + c))
                return FALSE;
        }
    }
    return TRUE;
}

static void
grid_slot_mark(GArray *occupied,
               int row, int col, int col_span, int row_span,
               int n_cols)
{
    if (row < 0 || col < 0 || col_span < 1 || row_span < 1)
        return;
    if (col + col_span > n_cols) col_span = n_cols - col;
    if (row + row_span > NS_GRID_ROWS_MAX)
        row_span = NS_GRID_ROWS_MAX - row;
    for (int r = 0; r < row_span; r++) {
        for (int c = 0; c < col_span; c++)
            grid_occupy(occupied, row + r, col + c);
    }
}

static gboolean
grid_find_slot(const GArray *occupied,
               int *out_row, int *out_col,
               int col_span, int row_span, int n_cols,
               int start_row, int start_col,
               gboolean fixed_row, gboolean fixed_col)
{
    int r0 = start_row >= 0 ? start_row : 0;
    int c0 = start_col >= 0 ? start_col : 0;
    if (fixed_row && fixed_col) {
        *out_row = r0;
        *out_col = c0;
        return r0 < NS_GRID_ROWS_MAX && c0 < n_cols;
    }
    for (int r = r0; r < NS_GRID_ROWS_MAX; r++) {
        int first_col = fixed_col ? c0 : (r == r0 ? c0 : 0);
        int last_col = fixed_col ? c0 : n_cols - col_span;
        if (last_col < first_col) last_col = first_col;
        for (int c = first_col; c <= last_col; c++) {
            if (grid_slot_available(occupied, r, c, col_span, row_span, n_cols)) {
                *out_row = r;
                *out_col = c;
                return TRUE;
            }
        }
        if (fixed_row) break;
    }
    return FALSE;
}

static void
grid_advance_cursor(const GArray *occupied,
                    int *row, int *col, int n_cols)
{
    if (*col >= n_cols) {
        *col = 0;
        (*row)++;
    }
    while (*row < NS_GRID_ROWS_MAX && grid_occupied(occupied, *row, *col)) {
        (*col)++;
        if (*col >= n_cols) {
            *col = 0;
            (*row)++;
        }
    }
}

static double
grid_track_px(const ns_css_track *t, double basis)
{
    if (!t) return 0;
    if (t->kind == NS_CSS_TRACK_PX)
        return t->v + (basis >= 0 ? t->pct * basis / 100.0 : 0);
    if (t->kind == NS_CSS_TRACK_PERCENT && basis >= 0)
        return t->v * basis / 100.0;
    return 0;
}

static gboolean
grid_track_is_fixed(const ns_css_track *t, double basis)
{
    if (!t) return FALSE;
    if (t->kind == NS_CSS_TRACK_PX) {
        if (t->pct != 0 && basis < 0) return FALSE;
    } else if (t->kind != NS_CSS_TRACK_PERCENT || basis < 0) {
        return FALSE;
    }
    if (!t->has_min) return TRUE;
    return t->min_kind == NS_CSS_TRACK_PX ||
           (t->min_kind == NS_CSS_TRACK_PERCENT && basis >= 0);
}

static void
grid_distribute_extra(double *sizes, const double *caps,
                      const gboolean *affected, const gboolean *beyond,
                      int count, double extra)
{
    double grow[NS_CSS_TRACKS_MAX] = {0};
    gboolean frozen[NS_CSS_TRACKS_MAX] = {0};
    for (int i = 0; i < count; i++) frozen[i] = !affected[i];
    while (extra > 1e-9) {
        int open = 0;
        for (int i = 0; i < count; i++) if (!frozen[i]) open++;
        if (!open) break;
        double share = extra / open;
        gboolean capped = FALSE;
        for (int i = 0; i < count; i++) {
            if (frozen[i]) continue;
            double room = caps[i] - (sizes[i] + grow[i]);
            if (room > share + 1e-9) continue;
            if (room < 0) room = 0;
            grow[i] += room;
            extra -= room;
            frozen[i] = TRUE;
            capped = TRUE;
        }
        if (capped) continue;
        for (int i = 0; i < count; i++) if (!frozen[i]) grow[i] += share;
        extra = 0;
    }
    if (extra > 1e-9) {
        int n = 0;
        for (int i = 0; i < count; i++) if (affected[i] && beyond[i]) n++;
        gboolean only_beyond = n > 0;
        if (!only_beyond)
            for (int i = 0; i < count; i++) if (affected[i]) n++;
        for (int i = 0; n > 0 && i < count; i++)
            if (affected[i] && (!only_beyond || beyond[i]))
                grow[i] += extra / n;
    }
    for (int i = 0; i < count; i++) sizes[i] += grow[i];
}

static gboolean
grid_track_max_is_intrinsic(const ns_css_track *t)
{
    return track_is_intrinsic(t->kind);
}

static gboolean
grid_track_min_is_intrinsic(const ns_css_track *t)
{
    return t->has_min ? track_is_intrinsic(t->min_kind)
                      : track_is_intrinsic(t->kind);
}

static double
grid_track_fixed_px(const ns_css_track *t, double avail)
{
    if (t->kind == NS_CSS_TRACK_PX) return t->v + t->pct * avail / 100.0;
    if (t->kind == NS_CSS_TRACK_PERCENT) return t->v * avail / 100.0;
    return 0;
}

static gboolean
grid_span_accommodate(const ns_css_tracks *cols, int c0, int span,
                      const double *gap_after, double avail,
                      double min_contribution, double max_contribution,
                      double *col_min, double *col_content)
{
    double gaps = 0;
    for (int i = 0; i < span; i++) {
        const ns_css_track *t = &cols->tracks[c0 + i];
        if (t->kind == NS_CSS_TRACK_FR) return FALSE;
        if (i + 1 < span) gaps += gap_after[c0 + i];
    }
    double base[NS_CSS_TRACKS_MAX], limit[NS_CSS_TRACKS_MAX];
    double caps[NS_CSS_TRACKS_MAX];
    gboolean affected[NS_CSS_TRACKS_MAX], beyond[NS_CSS_TRACKS_MAX];
    gboolean any = FALSE;
    double sum = gaps;
    for (int i = 0; i < span; i++) {
        const ns_css_track *t = &cols->tracks[c0 + i];
        gboolean min_intrinsic = grid_track_min_is_intrinsic(t);
        gboolean max_intrinsic = grid_track_max_is_intrinsic(t);
        base[i] = min_intrinsic ? col_min[c0 + i]
                : t->has_min ? track_min_px(t, avail)
                             : grid_track_fixed_px(t, avail);
        caps[i] = max_intrinsic ? INFINITY : grid_track_fixed_px(t, avail);
        affected[i] = min_intrinsic;
        beyond[i] = max_intrinsic;
        any = any || min_intrinsic || max_intrinsic;
        sum += base[i];
    }
    if (!any) return FALSE;
    if (min_contribution > sum) {
        grid_distribute_extra(base, caps, affected, beyond, span,
                              min_contribution - sum);
        for (int i = 0; i < span; i++)
            if (affected[i]) col_min[c0 + i] = base[i];
    }
    sum = gaps;
    gboolean any_max_min = FALSE;
    for (int i = 0; i < span; i++) {
        const ns_css_track *t = &cols->tracks[c0 + i];
        ns_css_track_kind min_kind = t->has_min ? t->min_kind : t->kind;
        affected[i] = min_kind == NS_CSS_TRACK_MAX_CONTENT;
        beyond[i] = affected[i];
        any_max_min = any_max_min || affected[i];
        sum += base[i];
    }
    if (any_max_min && max_contribution > sum) {
        grid_distribute_extra(base, caps, affected, beyond, span,
                              max_contribution - sum);
        for (int i = 0; i < span; i++)
            if (affected[i]) col_min[c0 + i] = base[i];
    }
    for (int pass = 0; pass < 2; pass++) {
        double contribution = pass == 0 ? min_contribution : max_contribution;
        sum = gaps;
        for (int i = 0; i < span; i++) {
            const ns_css_track *t = &cols->tracks[c0 + i];
            limit[i] = grid_track_max_is_intrinsic(t)
                ? MAX(col_content[c0 + i], base[i])
                : MAX(grid_track_fixed_px(t, avail), base[i]);
            caps[i] = INFINITY;
            affected[i] = pass == 0 ? grid_track_max_is_intrinsic(t)
                                    : t->kind == NS_CSS_TRACK_AUTO ||
                                      t->kind == NS_CSS_TRACK_MAX_CONTENT;
            beyond[i] = affected[i];
            sum += limit[i];
        }
        if (contribution <= sum) continue;
        grid_distribute_extra(limit, caps, affected, beyond, span,
                              contribution - sum);
        for (int i = 0; i < span; i++)
            if (affected[i]) col_content[c0 + i] = limit[i];
    }
    return TRUE;
}

static void
grid_expand_flexible_rows(double *row_height, int n_rows,
                          const ns_css_tracks *rows_tracks,
                          const ns_css_tracks *auto_rows_tracks,
                          int explicit_rows, double space)
{
    double *fr_factor = g_new0(double, n_rows + 1);
    gboolean any_fr = FALSE;
    for (int r = 0; r < n_rows; r++) {
        const ns_css_track *tk = NULL;
        if (rows_tracks && r < rows_tracks->n) tk = &rows_tracks->tracks[r];
        else if (auto_rows_tracks && auto_rows_tracks->n > 0)
            tk = &auto_rows_tracks->tracks[(r - explicit_rows) % auto_rows_tracks->n];
        if (tk && tk->kind == NS_CSS_TRACK_FR && tk->v > 0) {
            fr_factor[r] = tk->v;
            any_fr = TRUE;
        }
    }
    if (any_fr) {
        for (int r = 0; r < n_rows; r++)
            if (fr_factor[r] <= 0) space -= row_height[r];
        gboolean *inflexible = g_new0(gboolean, n_rows + 1);
        for (int pass = 0; pass <= n_rows; pass++) {
            double sum_fr = 0, leftover = space;
            for (int r = 0; r < n_rows; r++) {
                if (fr_factor[r] <= 0) continue;
                if (inflexible[r]) leftover -= row_height[r];
                else sum_fr += fr_factor[r];
            }
            if (sum_fr <= 0) break;
            double unit = leftover > 0 ? leftover / MAX(sum_fr, 1.0) : 0;
            gboolean changed = FALSE;
            for (int r = 0; r < n_rows; r++) {
                if (fr_factor[r] <= 0 || inflexible[r]) continue;
                if (row_height[r] > unit * fr_factor[r] + 0.01) {
                    inflexible[r] = TRUE;
                    changed = TRUE;
                }
            }
            if (changed) continue;
            for (int r = 0; r < n_rows; r++)
                if (fr_factor[r] > 0 && !inflexible[r])
                    row_height[r] = unit * fr_factor[r];
            break;
        }
        g_free(inflexible);
    }
    g_free(fr_factor);
}

static double
grid_auto_repeat_height(const ns_box *box, double row_basis, double cw)
{
    if (row_basis > 0) return row_basis;
    if (!box->style) return 0;
    const ns_css_value *mx = box->style->values[NS_CSS_MAX_HEIGHT];
    if (mx && (mx->kind == NS_CSS_V_LENGTH || mx->kind == NS_CSS_V_CALC)) {
        double h = specified_height_to_content(box,
                                               resolve_used_height(box, mx, cw, -1));
        if (h > 0) return h;
    }
    const ns_css_value *mn = box->style->values[NS_CSS_MIN_HEIGHT];
    if (mn && (mn->kind == NS_CSS_V_LENGTH || mn->kind == NS_CSS_V_CALC)) {
        double h = specified_height_to_content(box,
                                               resolve_used_height(box, mn, cw, -1));
        if (h > 0) return h;
    }
    return 0;
}

static void
grid_extend_with_auto_tracks(ns_css_tracks *tracks, int from, int to,
                             const ns_css_value *auto_v)
{
    const ns_css_tracks *pattern =
        auto_v && auto_v->kind == NS_CSS_V_TRACKS && auto_v->u.tracks.n > 0 &&
        !auto_v->u.tracks.subgrid ? &auto_v->u.tracks : NULL;
    if (to > NS_CSS_TRACKS_MAX) to = NS_CSS_TRACKS_MAX;
    for (int i = from; i < to; i++) {
        ns_css_track t = { .kind = NS_CSS_TRACK_AUTO };
        if (pattern) t = pattern->tracks[(i - from) % pattern->n];
        tracks->tracks[i] = t;
    }
    if (to > from) tracks->n = to;
}

static void
layout_grid(ns_box *box, double cw,
            double inner_x, double inner_y,
            const ns_style *child_inherited,
            double *cursor_y_out)
{
    const ns_subgrid_cols *sg = g_pending_subgrid_cols;
    const ns_subgrid_rows *sgr = g_pending_subgrid_rows;
    g_pending_subgrid_cols = NULL;
    g_pending_subgrid_rows = NULL;

    const ns_css_value *areas_v = box->style ? box->style->values[NS_CSS_GRID_TEMPLATE_AREAS] : NULL;
    const ns_css_areas *areas = areas_v && areas_v->kind == NS_CSS_V_AREAS
        ? &areas_v->u.areas : NULL;
    const ns_css_value *cols_v = box->style ? box->style->values[NS_CSS_GRID_TEMPLATE_COLUMNS] : NULL;
    gboolean cols_subgrid = cols_v && cols_v->kind == NS_CSS_V_TRACKS &&
                            cols_v->u.tracks.subgrid && sg && sg->n > 0;
    const ns_css_value *rows_v = box->style ? box->style->values[NS_CSS_GRID_TEMPLATE_ROWS]    : NULL;
    gboolean rows_subgrid = rows_v && rows_v->kind == NS_CSS_V_TRACKS &&
                            rows_v->u.tracks.subgrid && sgr && sgr->n > 0;
    ns_css_tracks default_cols = { .n = 1, .tracks = { { .kind = NS_CSS_TRACK_AUTO } } };
    const ns_css_tracks *cols_src =
        (cols_v && cols_v->kind == NS_CSS_V_TRACKS && !cols_v->u.tracks.subgrid) ?
        &cols_v->u.tracks : &default_cols;

    double col_gap = gap_px(box->style ? box->style->values[NS_CSS_COLUMN_GAP] : NULL,
                            box->style ? box->style->values[NS_CSS_GAP] : NULL, cw);
    const ns_css_value *hv_box = box->style ? box->style->values[NS_CSS_HEIGHT] : NULL;
    double row_basis = (hv_box && (hv_box->kind == NS_CSS_V_LENGTH ||
                                   hv_box->kind == NS_CSS_V_CALC))
        ? clamp_height_minmax_px(box->style,
                                 resolve_used_height(box, hv_box, cw, -1))
        : -1;
    double row_gap = gap_px(
        box->style ? box->style->values[NS_CSS_ROW_GAP] : NULL,
        box->style ? box->style->values[NS_CSS_GAP] : NULL,
        row_basis > 0 ? row_basis : 0);
    if (rows_subgrid) row_gap = sgr->gap;

    int fit_start = 0, fit_count = 0;
    ns_css_tracks cols_buf = expand_auto_repeat_ex(cols_src, cw, col_gap,
                                                   &fit_start, &fit_count);
    if (areas && !cols_subgrid) {
        int templated = cols_src == &default_cols ? 0 : cols_buf.n;
        if (areas->n_cols > templated)
            grid_extend_with_auto_tracks(&cols_buf, templated, areas->n_cols,
                box->style->values[NS_CSS_GRID_AUTO_COLUMNS]);
    }
    const ns_css_tracks *cols = &cols_buf;
    int n_cols = cols->n > 0 ? cols->n : 1;
    int explicit_cols = n_cols;
    ns_css_tracks rows_buf = { 0 };
    const ns_css_tracks *rows_template = NULL;
    if (!rows_subgrid && rows_v && rows_v->kind == NS_CSS_V_TRACKS &&
        !rows_v->u.tracks.subgrid) {
        rows_buf = expand_auto_repeat_ex(&rows_v->u.tracks,
                                         grid_auto_repeat_height(box, row_basis, cw),
                                         row_gap, NULL, NULL);
        rows_template = &rows_buf;
    }
    int row_line_tracks = rows_subgrid ? sgr->n :
        (rows_template ? rows_template->n : 1);
    if (areas && !rows_subgrid && areas->n_rows > row_line_tracks)
        row_line_tracks = areas->n_rows;
    if (row_line_tracks < 1) row_line_tracks = 1;

    double col_sizes[NS_CSS_TRACKS_MAX] = {0};
    double avail = cw - (n_cols > 1 ? col_gap * (n_cols - 1) : 0);
    if (avail < 0) avail = 0;

    GPtrArray *items = g_ptr_array_new();
    GArray *col_starts = g_array_new(FALSE, FALSE, sizeof(int));
    GArray *col_spans  = g_array_new(FALSE, FALSE, sizeof(int));
    GArray *row_starts = g_array_new(FALSE, FALSE, sizeof(int));
    GArray *row_spans  = g_array_new(FALSE, FALSE, sizeof(int));
    GArray *placed_cols = g_array_new(FALSE, FALSE, sizeof(int));
    GArray *placed_rows = g_array_new(FALSE, FALSE, sizeof(int));
    GArray *item_heights = g_array_new(FALSE, FALSE, sizeof(double));
    typedef struct { double top, height; } grid_row;
    GArray *grid_rows = g_array_new(FALSE, FALSE, sizeof(grid_row));
    for (ns_box *c = box->first_child; c; c = c->next_sibling) {
        int s = -1, sp = 1;
        int rs_start = -1, rs = 1;
        if (c->style) {
            grid_lines col_lines = { &cols_buf, areas, FALSE };
            grid_lines row_lines = { rows_template, areas, TRUE };
            g_grid_lines = &col_lines;
            int got = grid_resolve_pos(c->style, NS_CSS_GRID_COLUMN,
                                       NS_CSS_GRID_COLUMN_START,
                                       NS_CSS_GRID_COLUMN_END, n_cols,
                                       &s, &sp);
            if (!got) s = -1;
            g_grid_lines = &row_lines;
            got = grid_resolve_pos(c->style, NS_CSS_GRID_ROW,
                                   NS_CSS_GRID_ROW_START,
                                   NS_CSS_GRID_ROW_END, row_line_tracks,
                                   &rs_start, &rs);
            g_grid_lines = NULL;
            if (!got) rs_start = -1;
            if (rs < 1) rs = 1;
        }
        g_ptr_array_add(items, c);
        g_array_append_val(col_starts, s);
        g_array_append_val(col_spans, sp);
        g_array_append_val(row_starts, rs_start);
        g_array_append_val(row_spans, rs);
    }

    if (!cols_subgrid) {
        int max_end = 0;
        for (guint i = 0; i < items->len; i++) {
            int s = g_array_index(col_starts, int, i);
            int sp = g_array_index(col_spans, int, i);
            if (sp < 1) sp = 1;
            int e = (s >= 0 ? s : 0) + sp;
            if (e > max_end) max_end = e;
        }
        if (max_end > NS_CSS_TRACKS_MAX) max_end = NS_CSS_TRACKS_MAX;
        if (max_end > n_cols) {
            grid_extend_with_auto_tracks(&cols_buf, n_cols, max_end,
                box->style->values[NS_CSS_GRID_AUTO_COLUMNS]);
            n_cols = max_end;
            avail = cw - (n_cols > 1 ? col_gap * (n_cols - 1) : 0);
            if (avail < 0) avail = 0;
        }
    }

    const char *auto_flow = box->style
        ? ns_style_keyword(box->style, NS_CSS_GRID_AUTO_FLOW) : NULL;
    gboolean dense = auto_flow && strstr(auto_flow, "dense") != NULL;
    gboolean col_flow = auto_flow && strstr(auto_flow, "column") != NULL &&
                        !cols_subgrid && !rows_subgrid;
    if (col_flow) {
        int flow_rows = rows_template ? rows_template->n : 0;
        if (flow_rows <= 0) flow_rows = 1;
        if (flow_rows > NS_CSS_TRACKS_MAX) flow_rows = NS_CSS_TRACKS_MAX;
        int tmpl_cols = (cols_v && cols_v->kind == NS_CSS_V_TRACKS &&
                         !cols_v->u.tracks.subgrid)
                        ? cols->n : 0;
        gboolean occ[NS_CSS_TRACKS_MAX][NS_CSS_TRACKS_MAX] = {{FALSE}};
        int cur_col = 0, cur_row = 0;
        int used_cols = tmpl_cols;
        for (guint i = 0; i < items->len; i++) {
            int s = g_array_index(col_starts, int, i);
            int sp = g_array_index(col_spans, int, i);
            int rs_start = g_array_index(row_starts, int, i);
            int rs = g_array_index(row_spans, int, i);
            if (sp < 1) sp = 1;
            if (sp > NS_CSS_TRACKS_MAX) sp = NS_CSS_TRACKS_MAX;
            if (rs < 1) rs = 1;
            if (rs > flow_rows) rs = flow_rows;
            gboolean fixed_col = s >= 0 && s + sp <= NS_CSS_TRACKS_MAX;
            gboolean fixed_row = rs_start >= 0 && rs_start + rs <= flow_rows;
            int pc = -1, pr = -1;
            int c0 = fixed_col ? s : (dense ? 0 : cur_col);
            for (int c = c0; c + sp <= NS_CSS_TRACKS_MAX && pc < 0; c++) {
                if (fixed_col && c != s) break;
                int r0 = fixed_row ? rs_start
                       : (!fixed_col && !dense && c == c0) ? cur_row : 0;
                for (int r = r0; r + rs <= flow_rows; r++) {
                    if (fixed_row && r != rs_start) break;
                    gboolean is_free = TRUE;
                    for (int rr = r; rr < r + rs && is_free; rr++)
                        for (int cc = c; cc < c + sp && is_free; cc++)
                            if (occ[rr][cc]) is_free = FALSE;
                    if (is_free) { pc = c; pr = r; break; }
                }
            }
            if (pc < 0) { pc = NS_CSS_TRACKS_MAX - sp; pr = 0; }
            for (int rr = pr; rr < pr + rs; rr++)
                for (int cc = pc; cc < pc + sp; cc++)
                    occ[rr][cc] = TRUE;
            g_array_append_val(placed_cols, pc);
            g_array_append_val(placed_rows, pr);
            if (pc + sp > used_cols) used_cols = pc + sp;
            if (!fixed_col || !fixed_row) {
                cur_col = pc;
                cur_row = pr + rs;
                if (cur_row >= flow_rows) { cur_col = pc + sp; cur_row = 0; }
            }
        }
        if (used_cols < 1) used_cols = 1;
        if (used_cols > NS_CSS_TRACKS_MAX) used_cols = NS_CSS_TRACKS_MAX;
        const ns_css_value *acv =
            box->style->values[NS_CSS_GRID_AUTO_COLUMNS];
        const ns_css_tracks *auto_cols =
            (acv && acv->kind == NS_CSS_V_TRACKS && acv->u.tracks.n > 0 &&
             !acv->u.tracks.subgrid)
            ? &acv->u.tracks : NULL;
        ns_css_tracks flow_cols = {0};
        flow_cols.n = used_cols;
        for (int c = 0; c < used_cols; c++) {
            if (c < tmpl_cols)
                flow_cols.tracks[c] = cols->tracks[c];
            else if (auto_cols)
                flow_cols.tracks[c] =
                    auto_cols->tracks[(c - tmpl_cols) % auto_cols->n];
            else
                flow_cols.tracks[c].kind = NS_CSS_TRACK_AUTO;
        }
        cols_buf = flow_cols;
        n_cols = used_cols;
        avail = cw - (n_cols > 1 ? col_gap * (n_cols - 1) : 0);
        if (avail < 0) avail = 0;
    }

    const ns_css_value *auto_rows_v = box->style
        ? box->style->values[NS_CSS_GRID_AUTO_ROWS] : NULL;
    const ns_css_tracks *rows_tracks = rows_template;
    const ns_css_tracks *auto_rows_tracks =
        (!rows_subgrid && auto_rows_v && auto_rows_v->kind == NS_CSS_V_TRACKS &&
         !auto_rows_v->u.tracks.subgrid && auto_rows_v->u.tracks.n > 0)
        ? &auto_rows_v->u.tracks : NULL;
    int explicit_rows = rows_subgrid ? sgr->n : (rows_tracks ? rows_tracks->n : 0);
    GArray *occupied = grid_occupancy_new();
    int auto_row = 0;
    int auto_col = 0;
    int n_rows = explicit_rows;
    if (areas && !rows_subgrid && areas->n_rows > n_rows)
        n_rows = MIN(areas->n_rows, NS_GRID_ROWS_MAX);
    if (col_flow) {
        for (guint i = 0; i < items->len; i++) {
            int pr = g_array_index(placed_rows, int, i);
            int rs = g_array_index(row_spans, int, i);
            if (rs < 1) rs = 1;
            if (pr + rs > n_rows) n_rows = pr + rs;
        }
        if (n_rows < 1) n_rows = 1;
    }
    if (!col_flow) {
        g_array_set_size(placed_rows, items->len);
        g_array_set_size(placed_cols, items->len);
    }
    for (guint step = 0; !col_flow && step < 2 * items->len; step++) {
        guint i = step % items->len;
        gboolean definite_pass = step < items->len;
        int s = g_array_index(col_starts, int, i);
        int sp = g_array_index(col_spans, int, i);
        int rs_start = g_array_index(row_starts, int, i);
        int rs = g_array_index(row_spans, int, i);
        if (sp < 1) sp = 1;
        if (sp > n_cols) sp = n_cols;
        if (rs < 1) rs = 1;
        if (rs > NS_GRID_ROWS_MAX) rs = NS_GRID_ROWS_MAX;
        gboolean fixed_col = s >= 0 && s + sp <= n_cols;
        gboolean fixed_row = rs_start >= 0 && rs_start < NS_GRID_ROWS_MAX;
        if (definite_pass != (fixed_col && fixed_row)) continue;
        int start_row = fixed_row ? rs_start : (dense ? 0 : auto_row);
        int start_col = fixed_col ? s : (fixed_row || dense ? 0 : auto_col);
        int placed_row = start_row;
        int placed_col = fixed_col ? s : 0;
        if (!grid_find_slot(occupied, &placed_row, &placed_col,
                            sp, rs, n_cols, start_row, start_col,
                            fixed_row, fixed_col)) {
            if (placed_row < 0) placed_row = 0;
            if (placed_col < 0) placed_col = 0;
            if (placed_col + sp > n_cols) placed_col = n_cols - sp;
            if (placed_row + rs > NS_GRID_ROWS_MAX)
                placed_row = NS_GRID_ROWS_MAX - rs;
            if (placed_row < 0) placed_row = 0;
        }
        grid_slot_mark(occupied, placed_row, placed_col, sp, rs, n_cols);
        g_array_index(placed_rows, int, i) = placed_row;
        g_array_index(placed_cols, int, i) = placed_col;
        if (placed_row + rs > n_rows) n_rows = placed_row + rs;
        if (definite_pass) continue;
        auto_row = placed_row;
        auto_col = placed_col + sp;
        grid_advance_cursor(occupied, &auto_row, &auto_col, n_cols);
    }
    g_array_free(occupied, TRUE);
    if (n_rows > NS_GRID_ROWS_MAX) n_rows = NS_GRID_ROWS_MAX;
    if (rows_subgrid && n_rows > sgr->n) n_rows = sgr->n;

    gboolean col_collapsed[NS_CSS_TRACKS_MAX] = {0};
    double col_gap_after[NS_CSS_TRACKS_MAX + 1] = {0};
    if (fit_count > 0 && !cols_subgrid) {
        gboolean used[NS_CSS_TRACKS_MAX] = {0};
        for (guint k = 0; k < placed_cols->len; k++) {
            int c0 = g_array_index(placed_cols, int, k);
            int sp = k < col_spans->len ? g_array_index(col_spans, int, k) : 1;
            for (int j = 0; j < sp && c0 + j < n_cols; j++)
                if (c0 + j >= 0) used[c0 + j] = TRUE;
        }
        for (int t = fit_start; t < fit_start + fit_count && t < n_cols; t++) {
            if (used[t]) continue;
            col_collapsed[t] = TRUE;
            cols_buf.tracks[t].kind = NS_CSS_TRACK_PX;
            cols_buf.tracks[t].v = 0;
            cols_buf.tracks[t].has_min = FALSE;
        }
    }
    {
        double gaps_total = 0;
        for (int t = 0; t < n_cols; t++) {
            gboolean later = FALSE;
            for (int u = t + 1; u < n_cols; u++)
                if (!col_collapsed[u]) { later = TRUE; break; }
            col_gap_after[t] = (!col_collapsed[t] && later) ? col_gap : 0;
            gaps_total += col_gap_after[t];
        }
        avail = cw - gaps_total;
        if (avail < 0) avail = 0;
    }

    double col_content[NS_CSS_TRACKS_MAX] = {0};
    double col_min[NS_CSS_TRACKS_MAX] = {0};
    gboolean any_auto_content = FALSE;
    for (int t = 0; t < n_cols; t++) {
        if (!track_is_intrinsic(cols->tracks[t].kind) &&
            !(cols->tracks[t].has_min &&
              track_is_intrinsic(cols->tracks[t].min_kind)))
            continue;
        for (guint k = 0; k < items->len; k++) {
            int item_col = k < placed_cols->len
                ? g_array_index(placed_cols, int, k) : -1;
            if (item_col != t ||
                g_array_index(col_spans, int, k) != 1) continue;
            ns_box *c = items->pdata[k];
            double nw = measure_natural_width(c, child_inherited);
            double mw = measure_min_width(c, child_inherited);
            if (c->style) {
                ns_edges m = {0}, pd = {0}, bd = {0};
                edges_from_style(c->style, mw, &m, &pd, &bd);
                double extra = m.left + m.right + pd.left + pd.right +
                               bd.left + bd.right;
                mw += extra;
                nw += extra;
            }
            if (nw > avail) nw = avail;
            if (nw > col_content[t]) col_content[t] = nw;
            if (mw > col_min[t]) col_min[t] = mw;
            any_auto_content = TRUE;
        }
    }
    for (int span = 2; span <= n_cols; span++) {
        for (guint k = 0; k < items->len; k++) {
            int c0 = k < placed_cols->len ? g_array_index(placed_cols, int, k) : -1;
            if (c0 < 0 || g_array_index(col_spans, int, k) != span ||
                c0 + span > n_cols)
                continue;
            ns_box *c = items->pdata[k];
            double nw = measure_natural_width(c, child_inherited);
            double mw = measure_min_width(c, child_inherited);
            if (c->style) {
                ns_edges m = {0}, pd = {0}, bd = {0};
                edges_from_style(c->style, mw, &m, &pd, &bd);
                double extra = m.left + m.right + pd.left + pd.right +
                               bd.left + bd.right;
                mw += extra;
                nw += extra;
            }
            if (nw > avail) nw = avail;
            if (grid_span_accommodate(cols, c0, span, col_gap_after, avail,
                                      mw, nw, col_min, col_content))
                any_auto_content = TRUE;
        }
    }
    resolve_track_sizes_full(cols, avail,
                             any_auto_content ? col_min : NULL,
                             any_auto_content ? col_content : NULL, col_sizes,
                             justify_kw_stretches_auto(
                                 keyword_or(box->style, NS_CSS_JUSTIFY_CONTENT,
                                            "normal")));

    double col_x[NS_CSS_TRACKS_MAX + 1];
    col_x[0] = inner_x;
    for (int i = 0; i < n_cols; i++)
        col_x[i + 1] = col_x[i] + col_sizes[i] + col_gap_after[i];

    if (cols_subgrid) {
        n_cols = sg->n;
        col_gap = sg->gap;
        for (int i = 0; i < n_cols; i++) {
            col_sizes[i] = sg->sizes[i];
            col_x[i] = sg->x[i];
        }
        col_x[n_cols] = sg->x[n_cols];
    } else {
        double used_w = 0;
        for (int t = 0; t < n_cols; t++) used_w += col_sizes[t];
        for (int t = 0; t < n_cols; t++) used_w += col_gap_after[t];
        double free_w = cw - used_w;
        if (free_w > 0.5) {
            const char *jc = keyword_or(box->style, NS_CSS_JUSTIFY_CONTENT,
                                        "start");
            double off = 0, extra_gap = 0;
            if (strcmp(jc, "center") == 0) {
                off = free_w / 2.0;
            } else if (strcmp(jc, "end") == 0 || strcmp(jc, "flex-end") == 0 ||
                       strcmp(jc, "right") == 0) {
                off = free_w;
            } else if (strcmp(jc, "space-between") == 0 && n_cols > 1) {
                extra_gap = free_w / (n_cols - 1);
            } else if (strcmp(jc, "space-around") == 0 && n_cols > 0) {
                extra_gap = free_w / n_cols;
                off = extra_gap / 2.0;
            } else if (strcmp(jc, "space-evenly") == 0 && n_cols > 0) {
                extra_gap = free_w / (n_cols + 1);
                off = extra_gap;
            }
            if (off != 0 || extra_gap != 0) {
                col_x[0] = inner_x + off;
                for (int t = 0; t < n_cols; t++)
                    col_x[t + 1] = col_x[t] + col_sizes[t] + col_gap_after[t] + extra_gap;
            }
        }
    }
    gboolean grid_rtl = !cols_subgrid &&
        strcmp(keyword_or(box->style, NS_CSS_DIRECTION, "ltr"), "rtl") == 0;
    if (grid_rtl) {
        double right = inner_x + cw;
        for (int t = 0; t < n_cols; t++)
            col_x[t] = right - (col_x[t] - inner_x) - col_sizes[t];
    }

    double *base_row_height = g_new0(double, n_rows + 1);
    for (int r = 0; r < n_rows; r++) {
        if (rows_subgrid) {
            base_row_height[r] = sgr->sizes[r];
        } else if (rows_tracks && r < rows_tracks->n) {
            base_row_height[r] = grid_track_px(&rows_tracks->tracks[r], row_basis);
        } else if (auto_rows_tracks) {
            int ar = (r - explicit_rows) % auto_rows_tracks->n;
            if (ar < 0) ar = 0;
            base_row_height[r] = grid_track_px(&auto_rows_tracks->tracks[ar], row_basis);
        }
    }
    double *base_row_y = g_new0(double, n_rows + 1);
    base_row_y[0] = rows_subgrid ? sgr->y[0] : inner_y;
    double base_gap = rows_subgrid ? sgr->gap : row_gap;
    for (int r = 0; r < n_rows; r++)
        base_row_y[r + 1] = base_row_y[r] + base_row_height[r] + base_gap;

    for (guint i = 0; i < items->len; i++) {
        ns_box *c = items->pdata[i];
        int chosen = g_array_index(placed_cols, int, i);
        int placed_row = g_array_index(placed_rows, int, i);
        int sp = g_array_index(col_spans, int, i);
        int rs = g_array_index(row_spans, int, i);
        if (sp < 1) sp = 1;
        if (sp > n_cols) sp = n_cols;
        if (rs < 1) rs = 1;
        if (placed_row < 0) placed_row = 0;
        if (placed_row + rs > n_rows) rs = n_rows - placed_row;
        if (chosen < 0) chosen = 0;
        if (chosen + sp > n_cols) chosen = n_cols - sp;

        double w = 0;
        for (int k = 0; k < sp; k++)
            w += col_sizes[chosen + k] + (k > 0 ? col_gap_after[chosen + k - 1] : 0);
        edges_from_style(c->style, w, &c->margin, &c->padding, &c->border);
        double cw_for_item = w - c->margin.left - c->margin.right;
        if (cw_for_item < 0) cw_for_item = 0;
        c->x = grid_rtl ? col_x[chosen + sp - 1] : col_x[chosen];
        c->y = inner_y;

        const char *jself = c->style
            ? ns_style_keyword(c->style, NS_CSS_JUSTIFY_SELF) : NULL;
        const char *j_eff = (jself && strcmp(jself, "auto") != 0)
            ? jself : keyword_or(box->style, NS_CSS_JUSTIFY_ITEMS, "stretch");
        gboolean j_stretch = !j_eff || strcmp(j_eff, "stretch") == 0 ||
                             strcmp(j_eff, "normal") == 0 ||
                             strcmp(j_eff, "legacy") == 0;
        const ns_css_value *iwv = c->style ? c->style->values[NS_CSS_WIDTH] : NULL;
        gboolean i_has_w = iwv && (iwv->kind == NS_CSS_V_LENGTH ||
                                   iwv->kind == NS_CSS_V_CALC);
        double item_w = cw_for_item;
        if (!j_stretch && !i_has_w) {
            double nat = measure_natural_width(c, child_inherited);
            if (nat >= 0 && nat < item_w) item_w = nat;
            if (item_w < 0) item_w = 0;
        }
        ns_subgrid_cols subctx = {0};
        if (sp >= 1 && style_is_grid_container(c->style) &&
            style_columns_are_subgrid(c->style)) {
            subctx.n = sp;
            subctx.gap = col_gap;
            for (int k = 0; k <= sp; k++)
                subctx.x[k] = col_x[chosen + k];
            for (int k = 0; k < sp; k++)
                subctx.sizes[k] = col_sizes[chosen + k];
            g_pending_subgrid_cols = &subctx;
        }
        ns_subgrid_rows subrowctx = {0};
        if (rs >= 1 && rs <= NS_CSS_TRACKS_MAX &&
            style_is_grid_container(c->style) &&
            style_rows_are_subgrid(c->style)) {
            gboolean usable = TRUE;
            for (int k = 0; k < rs; k++) {
                if (base_row_height[placed_row + k] <= 0) {
                    usable = FALSE;
                    break;
                }
            }
            if (usable) {
                double child_inner_y = c->y + c->margin.top +
                                       c->border.top + c->padding.top;
                double parent_row_y = base_row_y[placed_row];
                subrowctx.n = rs;
                subrowctx.gap = base_gap;
                for (int k = 0; k <= rs; k++)
                    subrowctx.y[k] = child_inner_y +
                        (base_row_y[placed_row + k] - parent_row_y);
                for (int k = 0; k < rs; k++)
                    subrowctx.sizes[k] = base_row_height[placed_row + k];
                g_pending_subgrid_rows = &subrowctx;
            }
        }
        double area_h = 0;
        for (int k = 0; k < rs && placed_row + k < n_rows; k++) {
            if (base_row_height[placed_row + k] <= 0) { area_h = 0; break; }
            area_h += base_row_height[placed_row + k] + (k > 0 ? base_gap : 0);
        }
        c->cb_height_override = area_h;
        layout_box(c, item_w + c->margin.left + c->margin.right, child_inherited);
        g_pending_subgrid_cols = NULL;
        g_pending_subgrid_rows = NULL;
        gboolean auto_h_margin = c->style &&
            (keyword_is(c->style->values[NS_CSS_MARGIN_LEFT], "auto") ||
             keyword_is(c->style->values[NS_CSS_MARGIN_RIGHT], "auto"));
        if ((!j_stretch || i_has_w) && !auto_h_margin) {
            double used_w = c->content_width +
                            c->padding.left + c->padding.right +
                            c->border.left + c->border.right;
            double free_w = cw_for_item - used_w;
            double dx = 0;
            const ns_css_value *jraw = c->style
                ? c->style->values[NS_CSS_JUSTIFY_SELF] : NULL;
            gboolean j_safe = jraw && jraw->kind == NS_CSS_V_KEYWORD &&
                jraw->u.keyword && g_str_has_prefix(jraw->u.keyword, "safe ");
            const char *j_kw = j_stretch || (j_safe && free_w < 0)
                ? "start" : j_eff;
            gboolean item_far = self_start_is_far_side(c->style, TRUE);
            gboolean at_right = FALSE;
            if (strcmp(j_kw, "center") == 0) {
                if (free_w > 0) dx = free_w / 2.0;
            } else if (strcmp(j_kw, "end") == 0 || strcmp(j_kw, "flex-end") == 0) {
                at_right = !grid_rtl;
            } else if (strcmp(j_kw, "start") == 0 || strcmp(j_kw, "flex-start") == 0) {
                at_right = grid_rtl;
            } else if (strcmp(j_kw, "self-end") == 0) {
                at_right = !item_far;
            } else if (strcmp(j_kw, "self-start") == 0) {
                at_right = item_far;
            } else if (strcmp(j_kw, "right") == 0) {
                at_right = TRUE;
            }
            if (at_right && free_w != 0) dx = free_w;
            if (dx != 0) shift_box_tree(c, dx, 0);
        }
        double item_outer = c->content_height +
                            c->padding.top + c->padding.bottom +
                            c->border.top + c->border.bottom +
                            c->margin.top + c->margin.bottom;
        g_array_append_val(item_heights, item_outer);
    }

    double *row_height = g_new0(double, n_rows + 1);
    gboolean *row_fixed = g_new0(gboolean, n_rows + 1);
    for (int r = 0; r < n_rows; r++) {
        double fixed = 0;
        const ns_css_track *tk = NULL;
        if (rows_subgrid) {
            fixed = sgr->sizes[r];
        } else if (rows_tracks && r < rows_tracks->n) {
            tk = &rows_tracks->tracks[r];
        } else if (auto_rows_tracks) {
            int ar = (r - explicit_rows) % auto_rows_tracks->n;
            if (ar < 0) ar = 0;
            tk = &auto_rows_tracks->tracks[ar];
        }
        if (tk) {
            fixed = grid_track_px(tk, row_basis);
            row_fixed[r] = grid_track_is_fixed(tk, row_basis);
        }
        if (fixed > row_height[r]) row_height[r] = fixed;
    }
    for (guint i = 0; i < items->len; i++) {
        int row = g_array_index(placed_rows, int, i);
        int rs = g_array_index(row_spans, int, i);
        if (row < 0 || row >= n_rows) continue;
        if (rs < 1) rs = 1;
        if (row + rs > n_rows) rs = n_rows - row;
        double item_outer = g_array_index(item_heights, double, i);
        double used = row_gap * (rs - 1);
        int growable = 0;
        for (int k = 0; k < rs; k++) {
            used += row_height[row + k];
            if (!row_fixed[row + k]) growable++;
        }
        if (item_outer > used && growable > 0) {
            double add = (item_outer - used) / growable;
            for (int k = 0; k < rs; k++)
                if (!row_fixed[row + k]) row_height[row + k] += add;
        }
    }
    g_free(row_fixed);

    if (!rows_subgrid && row_basis > 0 && n_rows > 0) {
        double over = (n_rows > 1 ? row_gap * (n_rows - 1) : 0) - row_basis;
        double *shrinkable = g_new0(double, n_rows + 1);
        double shrink_total = 0;
        for (int r = 0; r < n_rows; r++) {
            over += row_height[r];
            const ns_css_track *tk = NULL;
            if (rows_tracks && r < rows_tracks->n) tk = &rows_tracks->tracks[r];
            else if (auto_rows_tracks && auto_rows_tracks->n > 0)
                tk = &auto_rows_tracks->tracks[(r - explicit_rows) % auto_rows_tracks->n];
            if (tk && tk->has_min &&
                (tk->kind == NS_CSS_TRACK_PX || tk->kind == NS_CSS_TRACK_PERCENT)) {
                double mn = track_min_px(tk, row_basis);
                if (row_height[r] > mn) {
                    shrinkable[r] = row_height[r] - mn;
                    shrink_total += shrinkable[r];
                }
            }
        }
        if (over > 0 && shrink_total > 0) {
            double take = MIN(over, shrink_total);
            for (int r = 0; r < n_rows; r++)
                if (shrinkable[r] > 0)
                    row_height[r] -= take * shrinkable[r] / shrink_total;
        }
        g_free(shrinkable);
        grid_expand_flexible_rows(row_height, n_rows, rows_tracks,
                                  auto_rows_tracks, explicit_rows,
                                  row_basis - (n_rows > 1 ? row_gap * (n_rows - 1) : 0));
    } else if (!rows_subgrid && n_rows > 0) {
        const ns_css_value *mnv = box->style
            ? box->style->values[NS_CSS_MIN_HEIGHT] : NULL;
        double min_h = mnv && (mnv->kind == NS_CSS_V_LENGTH ||
                               mnv->kind == NS_CSS_V_CALC)
            ? specified_height_to_content(box,
                                          resolve_used_height(box, mnv, cw, -1))
            : -1;
        double gaps = n_rows > 1 ? row_gap * (n_rows - 1) : 0;
        double used = gaps;
        for (int r = 0; r < n_rows; r++) used += row_height[r];
        if (min_h > used)
            grid_expand_flexible_rows(row_height, n_rows, rows_tracks,
                                      auto_rows_tracks, explicit_rows,
                                      min_h - gaps);
    }

    double cursor_y = rows_subgrid ? sgr->y[0] : inner_y;
    for (int r = 0; r < n_rows; r++) {
        grid_row gr = { .top = cursor_y, .height = row_height[r] };
        g_array_append_val(grid_rows, gr);
        cursor_y += row_height[r] + row_gap;
    }
    if (grid_rows->len > 0) cursor_y -= row_gap;

    double measured = cursor_y - inner_y;
    if (!rows_subgrid && row_basis <= 0 && rows_tracks && measured > 0) {
        gboolean changed = FALSE;
        for (int r = 0; r < n_rows && r < rows_tracks->n; r++) {
            const ns_css_track *tk = &rows_tracks->tracks[r];
            if (tk->kind != NS_CSS_TRACK_PERCENT) continue;
            double resolved = tk->v * measured / 100.0;
            if (fabs(resolved - row_height[r]) > 0.01) {
                row_height[r] = resolved;
                changed = TRUE;
            }
        }
        if (changed) {
            double y = inner_y;
            for (guint r = 0; r < grid_rows->len; r++) {
                grid_row *gr = &g_array_index(grid_rows, grid_row, r);
                gr->top = y;
                gr->height = row_height[r];
                y += row_height[r] + row_gap;
            }
        }
    }
    double total_extra = 0;
    {
        const ns_css_value *hv = box->style
            ? box->style->values[NS_CSS_HEIGHT] : NULL;
        if (hv && (hv->kind == NS_CSS_V_LENGTH || hv->kind == NS_CSS_V_CALC) &&
            grid_rows->len > 0) {
            double eh = clamp_height_minmax_px(box->style,
                                               resolve_used_height(box, hv, cw,
                                                                   -1));
            if (box->style &&
                keyword_is(box->style->values[NS_CSS_BOX_SIZING], "border-box"))
                eh -= box->padding.top + box->padding.bottom +
                      box->border.top + box->border.bottom;
            if (eh > measured)
                total_extra = eh - measured;
        }
    }
    const char *acont = keyword_or(box->style, NS_CSS_ALIGN_CONTENT, "stretch");
    gboolean ac_stretch = !acont || strcmp(acont, "stretch") == 0 ||
                          strcmp(acont, "normal") == 0;
    double *row_extra = g_new0(double, grid_rows->len + 1);
    double *row_extra_before = g_new0(double, grid_rows->len + 1);
    if (ac_stretch && grid_rows->len > 0 && total_extra > 0) {
        int stretchable = 0;
        gboolean *row_auto = g_new0(gboolean, grid_rows->len + 1);
        for (guint r = 0; r < grid_rows->len; r++) {
            const ns_css_track *tk = NULL;
            if (rows_subgrid) tk = NULL;
            else if (rows_tracks && (int)r < rows_tracks->n) tk = &rows_tracks->tracks[r];
            else if (auto_rows_tracks && auto_rows_tracks->n > 0)
                tk = &auto_rows_tracks->tracks[((int)r - explicit_rows) % auto_rows_tracks->n];
            row_auto[r] = rows_subgrid ? FALSE
                        : (!tk || tk->kind == NS_CSS_TRACK_AUTO);
            if (row_auto[r]) stretchable++;
        }
        if (stretchable > 0) {
            double share = total_extra / stretchable;
            double before = 0;
            for (guint r = 0; r < grid_rows->len; r++) {
                row_extra_before[r] = before;
                row_extra[r] = row_auto[r] ? share : 0;
                before += row_extra[r];
            }
        }
        g_free(row_auto);
    }
    double group_off = 0, row_between = 0;
    if (!ac_stretch && total_extra > 0) {
        guint n = grid_rows->len;
        if (strcmp(acont, "center") == 0)
            group_off = total_extra / 2.0;
        else if (strcmp(acont, "end") == 0 || strcmp(acont, "flex-end") == 0)
            group_off = total_extra;
        else if (strcmp(acont, "space-between") == 0 && n > 1)
            row_between = total_extra / (n - 1);
        else if (strcmp(acont, "space-around") == 0 && n > 0) {
            row_between = total_extra / n;
            group_off = row_between / 2.0;
        } else if (strcmp(acont, "space-evenly") == 0 && n > 0) {
            row_between = total_extra / (n + 1);
            group_off = row_between;
        }
    }
    for (guint j = 0; j < items->len; j++) {
        ns_box *c = items->pdata[j];
        int r = g_array_index(placed_rows, int, j);
        if (r < 0 || r >= (int)grid_rows->len) continue;
        int span = g_array_index(row_spans, int, j);
        if (span < 1) span = 1;
        double row_h = 0;
        for (int k = 0; k < span && r + k < (int)grid_rows->len; k++) {
            grid_row *gk = &g_array_index(grid_rows, grid_row, r + k);
            row_h += gk->height + row_extra[r + k];
            if (k > 0) row_h += row_gap + row_between;
        }
        const char *aself = c->style
            ? ns_style_keyword(c->style, NS_CSS_ALIGN_SELF) : NULL;
        const char *a_eff = (aself && strcmp(aself, "auto") != 0)
            ? aself : keyword_or(box->style, NS_CSS_ALIGN_ITEMS, "stretch");
        gboolean a_stretch = !a_eff || strcmp(a_eff, "stretch") == 0 ||
                             strcmp(a_eff, "normal") == 0;
        double item_outer = c->content_height +
                            c->padding.top + c->padding.bottom +
                            c->border.top + c->border.bottom +
                            c->margin.top + c->margin.bottom;
        double free_h = row_h - item_outer;
        double dy_align = 0;
        gboolean mt_auto = c->style &&
            keyword_is(c->style->values[NS_CSS_MARGIN_TOP], "auto");
        gboolean mb_auto = c->style &&
            keyword_is(c->style->values[NS_CSS_MARGIN_BOTTOM], "auto");
        if ((mt_auto || mb_auto) && free_h > 0.5) {
            dy_align = mt_auto && mb_auto ? free_h / 2.0
                     : mt_auto           ? free_h
                                         : 0;
        } else if (a_stretch) {
            const ns_css_value *ihv = c->style
                ? c->style->values[NS_CSS_HEIGHT] : NULL;
            gboolean i_has_h = ihv && (ihv->kind == NS_CSS_V_LENGTH ||
                                       ihv->kind == NS_CSS_V_CALC);
            gboolean row_definite = FALSE;
            if (!rows_subgrid && rows_tracks && r < rows_tracks->n) {
                ns_css_track_kind rk = rows_tracks->tracks[r].kind;
                row_definite = rk == NS_CSS_TRACK_PX ||
                               rk == NS_CSS_TRACK_PERCENT ||
                               (rk == NS_CSS_TRACK_FR && row_basis > 0);
            }
            if (!i_has_h && c->kind == NS_BOX_BLOCK &&
                (free_h > 0.5 || (row_definite && span == 1 && free_h < -0.5)))
                c->content_height += free_h;
        } else if (free_h > 0.5 && strcmp(a_eff, "center") == 0) {
            dy_align = free_h / 2.0;
        } else if (free_h > 0.5 && (strcmp(a_eff, "end") == 0 ||
                                    strcmp(a_eff, "flex-end") == 0 ||
                                    strcmp(a_eff, "last baseline") == 0)) {
            dy_align = free_h;
        } else if (free_h > 0.5 && (strcmp(a_eff, "self-end") == 0 ||
                                    strcmp(a_eff, "self-start") == 0)) {
            gboolean far = self_start_is_far_side(c->style, FALSE);
            if (far == (strcmp(a_eff, "self-start") == 0)) dy_align = free_h;
        }
        grid_row *gr = &g_array_index(grid_rows, grid_row, r);
        double row_top = gr->top + row_extra_before[r] +
                         group_off + row_between * r;
        double target_y = row_top + dy_align;
        double dy = target_y - c->y;
        if (dy != 0) shift_box_tree(c, 0, dy);
    }
    if (total_extra > 0) cursor_y += total_extra;

    if (box->grid_col_tracks) g_array_free(box->grid_col_tracks, TRUE);
    if (box->grid_row_tracks) g_array_free(box->grid_row_tracks, TRUE);
    box->grid_col_tracks = g_array_new(FALSE, FALSE, sizeof(ns_grid_track_edges));
    box->grid_row_tracks = g_array_new(FALSE, FALSE, sizeof(ns_grid_track_edges));
    for (int t = 0; t < n_cols; t++) {
        ns_grid_track_edges e = { col_x[t], col_x[t] + col_sizes[t] };
        g_array_append_val(box->grid_col_tracks, e);
    }
    for (guint r = 0; r < grid_rows->len; r++) {
        const grid_row *gr = &g_array_index(grid_rows, grid_row, r);
        double top = gr->top + row_extra_before[r] + group_off + row_between * r;
        ns_grid_track_edges e = { top, top + gr->height + row_extra[r] };
        g_array_append_val(box->grid_row_tracks, e);
    }
    box->grid_explicit_cols = explicit_cols;
    box->grid_explicit_rows = row_line_tracks;

    *cursor_y_out = cursor_y;
    g_free(base_row_height);
    g_free(base_row_y);
    g_free(row_height);
    g_free(row_extra);
    g_free(row_extra_before);
    g_ptr_array_free(items, TRUE);
    g_array_free(col_starts, TRUE);
    g_array_free(col_spans, TRUE);
    g_array_free(row_starts, TRUE);
    g_array_free(row_spans, TRUE);
    g_array_free(placed_cols, TRUE);
    g_array_free(placed_rows, TRUE);
    g_array_free(item_heights, TRUE);
    g_array_free(grid_rows, TRUE);
}

static gboolean
box_is_block_level_replaced(const ns_box *c)
{
    if (!c || (c->kind != NS_BOX_IMAGE && c->kind != NS_BOX_VIDEO &&
               c->kind != NS_BOX_SVG)) return FALSE;
    return ns_display_is_block_level(ns_css_display_of(c->style));
}

static double
collapsed_margin(double a, double b)
{
    double positive = MAX(a > 0 ? a : 0, b > 0 ? b : 0);
    double negative = MIN(a < 0 ? a : 0, b < 0 ? b : 0);
    return positive + negative;
}

static gboolean
empty_block_collapses_through(const ns_box *box, int clear)
{
    return box && box->kind == NS_BOX_BLOCK && clear == 0 &&
           box->content_height <= 0 &&
           box->padding.top <= 0 && box->padding.bottom <= 0 &&
           box->border.top <= 0 && box->border.bottom <= 0;
}

static double
text_input_intrinsic_width(const ns_box *box)
{
    if (!box || !box->dom || !box->dom->name ||
        strcmp(box->dom->name, "input") != 0)
        return 0;
    const char *type = ns_element_get_attr(box->dom, "type");
    if (type && *type &&
        g_ascii_strcasecmp(type, "text") != 0 &&
        g_ascii_strcasecmp(type, "password") != 0 &&
        g_ascii_strcasecmp(type, "search") != 0 &&
        g_ascii_strcasecmp(type, "email") != 0 &&
        g_ascii_strcasecmp(type, "url") != 0 &&
        g_ascii_strcasecmp(type, "tel") != 0 &&
        g_ascii_strcasecmp(type, "number") != 0)
        return 0;
    const char *size_attr = ns_element_get_attr(box->dom, "size");
    int size = size_attr ? ns_parse_int(size_attr, 20, 1, 80) : 20;
    double font_size = length_or(box->style
        ? box->style->values[NS_CSS_FONT_SIZE] : NULL, 16);
    return size * font_size * 0.75;
}

static double
intrinsic_keyword_width(ns_box *box, const char *kw, const ns_style *mi,
                        double avail)
{
    if (!kw) return -1;
    if (avail < 0) avail = 0;
    if (strcmp(kw, "min-content") == 0)
        return measure_min_content_width(box, mi);
    if (strcmp(kw, "max-content") == 0)
        return measure_max_content_width(box, mi);
    if (strcmp(kw, "fit-content") == 0) {
        double mn = measure_min_content_width(box, mi);
        double mx = measure_max_content_width(box, mi);
        double w = mx < avail ? mx : avail;
        return w < mn ? mn : w;
    }
    if (strcmp(kw, "stretch") == 0 ||
        strcmp(kw, "-webkit-fill-available") == 0 ||
        strcmp(kw, "-moz-available") == 0)
        return avail;
    return -1;
}

static gboolean
size_keyword_is_intrinsic(const ns_css_value *v)
{
    return v && v->kind == NS_CSS_V_KEYWORD && v->u.keyword &&
           (strcmp(v->u.keyword, "min-content") == 0 ||
            strcmp(v->u.keyword, "max-content") == 0 ||
            strcmp(v->u.keyword, "fit-content") == 0);
}

static gboolean
height_keyword_stretches(const ns_css_value *v)
{
    return v && v->kind == NS_CSS_V_KEYWORD && v->u.keyword &&
           (strcmp(v->u.keyword, "stretch") == 0 ||
            strcmp(v->u.keyword, "-webkit-fill-available") == 0 ||
            strcmp(v->u.keyword, "-moz-available") == 0);
}

static const char *
legacy_block_align(const ns_box *c, const ns_style *inherited)
{
    const char *ta = inherited ? ns_style_keyword(inherited, NS_CSS_TEXT_ALIGN) : NULL;
    if (!ta) return NULL;
    if (strcmp(ta, "-webkit-center") == 0 || strcmp(ta, "-moz-center") == 0)
        return "center";
    if (strcmp(ta, "-webkit-right") == 0 || strcmp(ta, "-moz-right") == 0)
        return "right";
    if (strcmp(ta, "center") != 0 && strcmp(ta, "right") != 0) return NULL;
    for (const ns_box *p = c->parent; p; p = p->parent) {
        if (p->style) {
            const char *pt = ns_style_keyword(p->style, NS_CSS_TEXT_ALIGN);
            if (!pt || strcmp(pt, ta) != 0) return NULL;
        }
        if (!p->dom || p->dom->kind != NS_NODE_ELEMENT) continue;
        if (ns_node_is_element_named(p->dom, "center")) return ta;
        const char *al = ns_element_get_attr(p->dom, "align");
        if (al && g_ascii_strcasecmp(al, ta) == 0) return ta;
    }
    return NULL;
}

static void
legacy_align_block_child(ns_box *c, double avail_x, double avail_w,
                         const ns_style *inherited)
{
    if (c->kind != NS_BOX_BLOCK && c->kind != NS_BOX_TABLE) return;
    if (style_is_absolute_or_fixed(c->style) || float_side_of(c->style) >= 0)
        return;
    const ns_css_value *ml = c->style ? c->style->values[NS_CSS_MARGIN_LEFT] : NULL;
    const ns_css_value *mr = c->style ? c->style->values[NS_CSS_MARGIN_RIGHT] : NULL;
    if (length_is_auto(ml) || length_is_auto(mr) ||
        c->margin.left != 0 || c->margin.right != 0)
        return;
    double outer = c->content_width + c->padding.left + c->padding.right +
                   c->border.left + c->border.right;
    if (outer >= avail_w - 0.5) return;
    const char *legacy = legacy_block_align(c, inherited);
    if (!legacy) return;
    double target_x;
    if (strcmp(legacy, "center") == 0)
        target_x = avail_x + (avail_w - outer) / 2.0;
    else if (strcmp(legacy, "right") == 0)
        target_x = avail_x + avail_w - outer;
    else
        return;
    shift_box_tree(c, target_x - c->x, 0);
}

static gboolean
block_height_is_auto(const ns_box *box, double width_basis)
{
    if (!box->style) return TRUE;
    if (resolve_used_height(box, box->style->values[NS_CSS_MIN_HEIGHT],
                            width_basis, -1) > 0)
        return FALSE;
    const ns_css_value *hv = box->style->values[NS_CSS_HEIGHT];
    if (height_keyword_stretches(hv))
        return containing_block_definite_height(box) < 0;
    if (hv && (hv->kind == NS_CSS_V_LENGTH || hv->kind == NS_CSS_V_CALC))
        return resolve_used_height(box, hv, width_basis, -1) < 0;
    return TRUE;
}

static ns_box *
fieldset_rendered_legend(const ns_box *box)
{
    for (ns_box *c = box->first_child; c; c = c->next_sibling)
        if (c->is_rendered_legend) return c;
    return NULL;
}

static void
box_detach_child(ns_box *parent, ns_box *child)
{
    ns_box *prev = NULL;
    for (ns_box *c = parent->first_child; c && c != child; c = c->next_sibling)
        prev = c;
    if (prev) prev->next_sibling = child->next_sibling;
    else parent->first_child = child->next_sibling;
    if (parent->last_child == child) parent->last_child = prev;
    child->next_sibling = NULL;
}

static void
box_prepend_child(ns_box *parent, ns_box *child)
{
    child->next_sibling = parent->first_child;
    parent->first_child = child;
    if (!parent->last_child) parent->last_child = child;
}

static double
legend_inline_offset(const ns_box *fieldset, const ns_box *legend,
                     double free_w)
{
    const ns_style *ls = legend->style;
    gboolean ml_auto = ls && length_is_auto(ls->values[NS_CSS_MARGIN_LEFT]);
    gboolean mr_auto = ls && length_is_auto(ls->values[NS_CSS_MARGIN_RIGHT]);
    if (ml_auto && mr_auto) return free_w / 2.0;
    if (ml_auto) return free_w;
    if (mr_auto) return 0;
    const char *js = ls ? ns_style_keyword(ls, NS_CSS_JUSTIFY_SELF) : NULL;
    if (js && g_str_has_prefix(js, "safe ")) js += 5;
    else if (js && g_str_has_prefix(js, "unsafe ")) js += 7;
    gboolean fieldset_rtl = fieldset->style &&
        keyword_is(fieldset->style->values[NS_CSS_DIRECTION], "rtl");
    gboolean legend_rtl = ls && keyword_is(ls->values[NS_CSS_DIRECTION], "rtl");
    gboolean at_end = fieldset_rtl;
    if (!js) return at_end ? free_w : 0;
    if (strcmp(js, "center") == 0) return free_w / 2.0;
    if (strcmp(js, "left") == 0) at_end = FALSE;
    else if (strcmp(js, "right") == 0) at_end = TRUE;
    else if (strcmp(js, "end") == 0 || strcmp(js, "flex-end") == 0)
        at_end = !fieldset_rtl;
    else if (strcmp(js, "self-start") == 0) at_end = legend_rtl;
    else if (strcmp(js, "self-end") == 0) at_end = !legend_rtl;
    return at_end ? free_w : 0;
}

static double
layout_rendered_legend(ns_box *fieldset, ns_box *legend, double cw,
                       double inner_x, const ns_style *inherited)
{
    edges_from_style(legend->style, cw,
                     &legend->margin, &legend->padding, &legend->border);
    double outer_extras = legend->padding.left + legend->padding.right +
                          legend->border.left + legend->border.right +
                          legend->margin.left + legend->margin.right;
    const ns_css_value *wv = legend->style
        ? legend->style->values[NS_CSS_WIDTH] : NULL;
    double layout_w = cw;
    if (!wv || length_is_auto(wv)) {
        double avail = MAX(cw - outer_extras, 0);
        double fit = measure_natural_width(legend, inherited);
        if (fit > avail) fit = avail;
        double floor_w = measure_min_width(legend, inherited);
        if (fit < floor_w) fit = floor_w;
        layout_w = fit + outer_extras;
    }
    double top_edge = fieldset->y + fieldset->margin.top;
    legend->x = inner_x;
    legend->y = top_edge;
    layout_box(legend, layout_w, inherited);
    double outer_w = legend->content_width +
                     legend->padding.left + legend->padding.right +
                     legend->border.left + legend->border.right +
                     legend->margin.left + legend->margin.right;
    double border_box_h = legend->content_height +
                          legend->padding.top + legend->padding.bottom +
                          legend->border.top + legend->border.bottom;
    double border_top = fieldset->border.top;
    double legend_top = legend->margin.top +
                        MAX((border_top - border_box_h) / 2.0, 0);
    double target_x = inner_x + legend_inline_offset(fieldset, legend,
                                                     cw - outer_w);
    double target_y = top_edge + legend_top - legend->margin.top;
    shift_box_tree(legend, target_x - legend->x, target_y - legend->y);
    double painted_border_top =
        MAX(legend_top + border_box_h / 2.0 - border_top / 2.0, 0);
    double legend_bottom = legend_top + border_box_h + legend->margin.bottom;
    double extra = MAX(painted_border_top, legend_bottom - border_top);
    return MAX(extra, 0);
}

static void
fieldset_set_content_block_size(ns_box *box, double width_basis,
                                double sizing_extras, double legend_extra)
{
    const ns_css_value *hv = box->style ? box->style->values[NS_CSS_HEIGHT] : NULL;
    if (!hv || (hv->kind != NS_CSS_V_LENGTH && hv->kind != NS_CSS_V_CALC))
        return;
    double h = resolve_used_height(box, hv, width_basis, -1);
    if (h < 0) return;
    h = MAX(h - sizing_extras - legend_extra, 0);
    for (ns_box *c = box->first_child; c; c = c->next_sibling)
        c->cb_height_override = h;
}

gboolean
ns_box_fieldset_legend_gap(const ns_box *fieldset, double *border_inset,
                           double *gap_x0, double *gap_x1,
                           double *gap_y0, double *gap_y1)
{
    const ns_box *legend = fieldset ? fieldset_rendered_legend(fieldset) : NULL;
    if (!legend) return FALSE;
    double top_edge = fieldset->y + fieldset->margin.top;
    double legend_border_top = legend->y + legend->margin.top;
    double border_box_w = legend->content_width +
                          legend->padding.left + legend->padding.right +
                          legend->border.left + legend->border.right;
    double border_box_h = legend->content_height +
                          legend->padding.top + legend->padding.bottom +
                          legend->border.top + legend->border.bottom;
    double inset = MAX(legend_border_top - top_edge + border_box_h / 2.0 -
                       fieldset->border.top / 2.0, 0);
    *border_inset = inset;
    *gap_x0 = legend->x + legend->margin.left;
    *gap_x1 = *gap_x0 + border_box_w;
    *gap_y0 = MIN(legend->y, top_edge + inset);
    *gap_y1 = MAX(legend_border_top + border_box_h + legend->margin.bottom,
                  top_edge + inset + fieldset->border.top);
    return TRUE;
}

static void
layout_block(ns_box *box, double parent_content_width, const ns_style *inherited_style)
{
    edges_from_style(box->style, parent_content_width,
                     &box->margin, &box->padding, &box->border);

    const ns_css_value *wv  = box->style ? box->style->values[NS_CSS_WIDTH]     : NULL;
    const ns_css_value *mxw = box->style ? box->style->values[NS_CSS_MAX_WIDTH] : NULL;
    const ns_css_value *mnw = box->style ? box->style->values[NS_CSS_MIN_WIDTH] : NULL;
    double horiz_extras = box->padding.left + box->padding.right +
                          box->border.left + box->border.right;
    double horiz_total  = horiz_extras + box->margin.left + box->margin.right;
    double vert_extras = box->padding.top + box->padding.bottom +
                         box->border.top + box->border.bottom;
    double cw;
    gboolean explicit_width = FALSE;
    gboolean intrinsic_width = FALSE;
    gboolean flex_grow_filled = FALSE;
    const char *parent_flex_dir = box->parent
        ? flex_direction_of(box->parent->style) : "row";
    gboolean flex_row_item = box->parent &&
        style_is_flex_container(box->parent->style) &&
        (strcmp(parent_flex_dir, "row") == 0 ||
         strcmp(parent_flex_dir, "row-reverse") == 0);
    gboolean flex_col_item_stretch = box->parent &&
        style_is_flex_container(box->parent->style) &&
        (strcmp(parent_flex_dir, "column") == 0 ||
         strcmp(parent_flex_dir, "column-reverse") == 0);
    if (flex_col_item_stretch) {
        const char *eff = box->style
            ? ns_style_keyword(box->style, NS_CSS_ALIGN_SELF) : NULL;
        if (!eff || strcmp(eff, "auto") == 0)
            eff = keyword_or(box->parent->style, NS_CSS_ALIGN_ITEMS, "stretch");
        if (strcmp(eff, "stretch") != 0 && strcmp(eff, "normal") != 0)
            flex_col_item_stretch = FALSE;
        if (length_is_auto(box->style ? box->style->values[NS_CSS_MARGIN_LEFT] : NULL) ||
            length_is_auto(box->style ? box->style->values[NS_CSS_MARGIN_RIGHT] : NULL))
            flex_col_item_stretch = FALSE;
    }
    double pct_width_base = parent_content_width;
    if (flex_row_item && box->parent->content_width > 0)
        pct_width_base = box->parent->content_width;
    if (box->has_flex_main) {
        box->has_flex_main = FALSE;
        cw = box->flex_main_size;
        if (cw < 0) cw = 0;
        explicit_width = TRUE;
        flex_grow_filled = TRUE;
    } else if (flex_row_item && flex_grow_of(box) > 0) {
        cw = parent_content_width - horiz_total;
        if (cw < 0) cw = 0;
        explicit_width = TRUE;
        flex_grow_filled = TRUE;
    } else if (wv && wv->kind == NS_CSS_V_LENGTH) {
        cw = length_resolve(wv, pct_width_base, 0);
        explicit_width = TRUE;
    } else if (wv && wv->kind == NS_CSS_V_CALC) {
        cw = length_resolve(wv, pct_width_base, 0);
        explicit_width = TRUE;
    } else if (wv && wv->kind == NS_CSS_V_KEYWORD && wv->u.keyword &&
               (strcmp(wv->u.keyword, "stretch") == 0 ||
                strcmp(wv->u.keyword, "-webkit-fill-available") == 0 ||
                strcmp(wv->u.keyword, "-moz-available") == 0)) {
        cw = parent_content_width - horiz_total;
        if (cw < 0) cw = 0;
        explicit_width = TRUE;
        intrinsic_width = TRUE;
    } else if (wv && wv->kind == NS_CSS_V_KEYWORD && wv->u.keyword &&
               (strcmp(wv->u.keyword, "max-content") == 0 ||
                strcmp(wv->u.keyword, "min-content") == 0 ||
                strcmp(wv->u.keyword, "fit-content") == 0)) {
        const ns_style *mi = inherited_style ? inherited_style : box->style;
        double avail = parent_content_width - horiz_total;
        if (avail < 0) avail = 0;
        if (strcmp(wv->u.keyword, "min-content") == 0) {
            cw = measure_min_width(box, mi);
        } else if (strcmp(wv->u.keyword, "max-content") == 0) {
            cw = measure_natural_width(box, mi);
        } else {
            double mn = measure_min_width(box, mi);
            double mx = measure_natural_width(box, mi);
            cw = mx < avail ? mx : avail;
            if (cw < mn) cw = mn;
        }
        if (cw < 0) cw = 0;
        explicit_width = TRUE;
        intrinsic_width = TRUE;
    } else if (!flex_col_item_stretch &&
               display_is_atomic_inline_container(
                   ns_css_display_of(box->style))) {
        double avail = parent_content_width - horiz_total;
        if (avail < 0) avail = 0;
        if (height_keyword_stretches(wv)) {
            cw = avail;
            explicit_width = TRUE;
            intrinsic_width = TRUE;
        } else {
            double natural = measure_natural_width(box,
                                                   inherited_style ? inherited_style : box->style);
            double input_width = text_input_intrinsic_width(box);
            if (natural < input_width) natural = input_width;
            cw = natural < avail ? natural : avail;
        }
        if (cw < 0) cw = 0;
    } else {
        cw = parent_content_width - horiz_total;
        if (cw < 0) cw = 0;
    }
    gboolean border_box = FALSE;
    if (box->style && box->style->values[NS_CSS_BOX_SIZING] &&
        box->style->values[NS_CSS_BOX_SIZING]->kind == NS_CSS_V_KEYWORD &&
        strcmp(box->style->values[NS_CSS_BOX_SIZING]->u.keyword, "border-box") == 0)
        border_box = TRUE;
    if (border_box && explicit_width && !intrinsic_width && !flex_grow_filled) {
        cw -= horiz_extras;
        if (cw < 0) cw = 0;
    }
    double max_cw = length_resolve(mxw, pct_width_base, -1);
    if (max_cw < 0 && mxw && mxw->kind == NS_CSS_V_KEYWORD) {
        max_cw = intrinsic_keyword_width(box, mxw->u.keyword,
                                         inherited_style ? inherited_style : box->style,
                                         parent_content_width - horiz_total);
        if (max_cw >= 0 && border_box) max_cw += horiz_extras;
    }
    if (max_cw >= 0) {
        if (border_box) max_cw -= horiz_extras;
        if (max_cw >= 0 && cw > max_cw) { cw = max_cw; explicit_width = TRUE; }
    }
    double min_cw = length_resolve(mnw, pct_width_base, -1);
    if (min_cw < 0 && mnw && mnw->kind == NS_CSS_V_KEYWORD) {
        min_cw = intrinsic_keyword_width(box, mnw->u.keyword,
                                         inherited_style ? inherited_style : box->style,
                                         parent_content_width - horiz_total);
        if (min_cw >= 0 && border_box) min_cw += horiz_extras;
    }
    if (min_cw >= 0) {
        if (border_box) min_cw -= horiz_extras;
        if (min_cw >= 0 && cw < min_cw) { cw = min_cw; explicit_width = TRUE; }
    }
    box->content_width = cw;

    if (explicit_width && !style_is_absolute_or_fixed(box->style)) {
        gboolean ml_auto = length_is_auto(box->style ? box->style->values[NS_CSS_MARGIN_LEFT]  : NULL);
        gboolean mr_auto = length_is_auto(box->style ? box->style->values[NS_CSS_MARGIN_RIGHT] : NULL);
        double available = parent_content_width - cw - horiz_extras;
        if (available < 0) available = 0;
        if (ml_auto && mr_auto) {
            box->margin.left  = available / 2.0;
            box->margin.right = available / 2.0;
        } else if (ml_auto) {
            box->margin.left  = available - box->margin.right;
            if (box->margin.left < 0) box->margin.left = 0;
        } else if (mr_auto) {
            box->margin.right = available - box->margin.left;
            if (box->margin.right < 0) box->margin.right = 0;
        }
    }

    double inner_x = box->x + box->margin.left + box->border.left + box->padding.left;
    double inner_y = box->y + box->margin.top  + box->border.top  + box->padding.top;
    const ns_style *child_inherited = box->style ? box->style : inherited_style;
    ns_box *legend = fieldset_rendered_legend(box);
    double legend_extra = 0;
    if (legend) {
        legend_extra = layout_rendered_legend(box, legend, cw, inner_x,
                                              child_inherited);
        inner_y += legend_extra;
        box_detach_child(box, legend);
        fieldset_set_content_block_size(box, parent_content_width,
                                        border_box ? vert_extras : 0,
                                        legend_extra);
    }
    double cursor_y = inner_y;
    double prev_margin_bottom = 0;
    gboolean collapse_top_with_parent =
        box->padding.top == 0 && box->border.top == 0 &&
        !box_establishes_bfc(box);
    gboolean collapse_bottom_with_parent =
        box->padding.bottom == 0 && box->border.bottom == 0 &&
        box->parent && !box_is_doc_root(box) && !box_establishes_bfc(box) &&
        block_height_is_auto(box, parent_content_width);

    if (style_is_flex_container(box->style) || style_is_grid_container(box->style))
        reorder_children_by_order(box);

    if (style_is_flex_container(box->style)) {
        const char *dir = flex_direction_of(box->style);
        gboolean is_row = strcmp(dir, "row") == 0 || strcmp(dir, "row-reverse") == 0;
        gboolean is_col = strcmp(dir, "column") == 0 || strcmp(dir, "column-reverse") == 0;
        if (is_row) {
            if (flex_wraps(box->style))
                layout_flex_row_wrap(box, cw, inner_x, inner_y, child_inherited,
                                     strcmp(dir, "row-reverse") == 0, &cursor_y);
            else
                layout_flex_row(box, cw, inner_x, inner_y, child_inherited,
                                strcmp(dir, "row-reverse") == 0,
                                parent_content_width, &cursor_y);
            goto flex_done;
        }
        if (is_col) {
            layout_flex_column(box, cw, inner_x, inner_y, child_inherited,
                               strcmp(dir, "column-reverse") == 0,
                               parent_content_width, &cursor_y);
            goto flex_done;
        }
    }

    if (style_is_grid_container(box->style)) {
        layout_grid(box, cw, inner_x, inner_y, child_inherited, &cursor_y);
        goto flex_done;
    }

    double col_gap = 16;
    int n_cols = box->style ? ns_css_used_column_count(box->style, cw, &col_gap)
                            : 1;
    ns_box *column_host = NULL;
    if (n_cols > 1) {
        int distributable = multicol_distributable_children(box);
        gboolean single_fragmentable_inline =
            box->first_child && !box->first_child->next_sibling &&
            inline_box_can_fragment_multicol(box->first_child,
                                             child_inherited, cw);
        if (distributable < 2 && !single_fragmentable_inline) {
            column_host = multicol_column_host(box);
            if (!column_host) n_cols = 1;
        }
    }
    if (n_cols > 1) {
        double col_w = (cw - col_gap * (n_cols - 1)) / n_cols;
        if (col_w > 1) cw = col_w;
        else n_cols = 1;
    }
    box->columns = n_cols;

    GArray *floats = g_array_new(FALSE, FALSE, sizeof(float_ref));
    double inline_line_top = -1;

    for (ns_box *c = box->first_child; c; c = c->next_sibling) {
        c->x = inner_x;
        int fside = float_side_of(c->style);
        int clr = clear_kind_of(c->style);
        if (fside >= 0 && (c->kind == NS_BOX_BLOCK || c->kind == NS_BOX_TABLE ||
                           c->kind == NS_BOX_IMAGE || c->kind == NS_BOX_VIDEO ||
                           c->kind == NS_BOX_SVG)) {
            edges_from_style(c->style, cw,
                             &c->margin, &c->padding, &c->border);
            double float_max_w = cw;
            double float_floor_w = 0;
            double cw_for_float;
            const ns_css_value *wv2 = c->style ? c->style->values[NS_CSS_WIDTH] : NULL;
            const ns_css_value *mxw2 = c->style ? c->style->values[NS_CSS_MAX_WIDTH] : NULL;
            const ns_css_value *mnw2 = c->style ? c->style->values[NS_CSS_MIN_WIDTH] : NULL;
            double float_sizing_extras = flex_box_is_border_box(c)
                ? c->padding.left + c->padding.right
                  + c->border.left + c->border.right
                : 0;
            if (wv2 && (wv2->kind == NS_CSS_V_LENGTH || wv2->kind == NS_CSS_V_CALC)) {
                cw_for_float = length_resolve(wv2, cw, 0) - float_sizing_extras;
                if (cw_for_float < 0) cw_for_float = 0;
            } else {
                double cap = float_max_w
                    - c->padding.left - c->padding.right
                    - c->border.left - c->border.right
                    - c->margin.left - c->margin.right;
                if (cap < 0) cap = 0;
                cw_for_float = height_keyword_stretches(wv2)
                    ? cap : measure_natural_width(c, child_inherited);
                if (cw_for_float > cap) cw_for_float = cap;
                if (!height_keyword_stretches(wv2)) {
                    float_floor_w = measure_min_width(c, child_inherited);
                    if (cw_for_float < float_floor_w) cw_for_float = float_floor_w;
                }
                if (cw_for_float < 0) cw_for_float = 0;
            }
            if (mxw2 && (mxw2->kind == NS_CSS_V_LENGTH || mxw2->kind == NS_CSS_V_CALC)) {
                double mx = length_resolve(mxw2, cw, 0);
                if (mx > 0 && cw_for_float > mx - float_sizing_extras)
                    cw_for_float = MAX(mx - float_sizing_extras, 0);
            }
            if (mnw2 && (mnw2->kind == NS_CSS_V_LENGTH || mnw2->kind == NS_CSS_V_CALC)) {
                double mn = length_resolve(mnw2, cw, 0);
                if (mn > 0 && cw_for_float < mn - float_sizing_extras)
                    cw_for_float = mn - float_sizing_extras;
            }
            double avail = cw_for_float
                + c->padding.left + c->padding.right
                + c->border.left + c->border.right
                + c->margin.left + c->margin.right;
            double float_y = cursor_y;
            if (!clr && inline_line_top >= 0 && inline_line_top < cursor_y)
                float_y = inline_line_top;
            if (clr) {
                double y_after_clear = floats_clear_y(floats, float_y, clr);
                if (y_after_clear > float_y) float_y = y_after_clear;
            }
            double left_off = 0, right_off = 0;
            floats_offsets_at(floats, float_y, &left_off, &right_off);
            while ((avail > cw - left_off - right_off) && floats->len > 0) {
                double next_y = float_y;
                gboolean advanced = FALSE;
                for (guint i = 0; i < floats->len; i++) {
                    const float_ref *f = &g_array_index(floats, float_ref, i);
                    if (f->bottom > float_y &&
                        (!advanced || f->bottom < next_y)) {
                        next_y = f->bottom;
                        advanced = TRUE;
                    }
                }
                if (!advanced) break;
                float_y = next_y;
                floats_offsets_at(floats, float_y, &left_off, &right_off);
            }
            double cw_capped = cw - left_off - right_off
                - c->margin.left - c->margin.right
                - c->padding.left - c->padding.right
                - c->border.left - c->border.right;
            if (cw_for_float > cw_capped && cw_capped > 0)
                cw_for_float = MAX(cw_capped, float_floor_w);
            double tentative_outer = cw_for_float
                + c->padding.left + c->padding.right
                + c->border.left + c->border.right
                + c->margin.left + c->margin.right;
            if (fside == 0)
                c->x = inner_x + left_off;
            else
                c->x = inner_x + cw - right_off - tentative_outer;
            c->y = float_y + c->margin.top;
            double saved_cw = c->content_width;
            c->content_width = cw_for_float;
            gboolean explicit_float_w = wv2 &&
                (wv2->kind == NS_CSS_V_LENGTH || wv2->kind == NS_CSS_V_CALC);
            layout_box(c, explicit_float_w
                       ? cw
                       : cw_for_float
                         + c->padding.left + c->padding.right
                         + c->border.left + c->border.right
                         + c->margin.left + c->margin.right,
                       child_inherited);
            (void)saved_cw;
            double actual_outer = c->content_width
                + c->padding.left + c->padding.right
                + c->border.left + c->border.right
                + c->margin.left + c->margin.right;
            if (fside == 1) {
                double aligned_x = inner_x + cw - right_off - actual_outer;
                if (aligned_x != c->x) shift_box_tree(c, aligned_x - c->x, 0);
            }
            float_ref fr = {
                .box = c, .side = fside,
                .top = c->y - c->margin.top,
                .bottom = c->y + c->content_height
                    + c->padding.top + c->padding.bottom
                    + c->border.top + c->border.bottom
                    + c->margin.bottom,
                .outer_w = actual_outer,
            };
            g_array_append_val(floats, fr);
            continue;
        }
        if (c->kind == NS_BOX_BLOCK || c->kind == NS_BOX_TABLE ||
            box_is_block_level_replaced(c)) {
            edges_from_style(c->style, cw,
                             &c->margin, &c->padding, &c->border);
            double mt = c->margin.top;
            double gap = collapsed_margin(mt, prev_margin_bottom);
            if (collapse_top_with_parent && cursor_y == inner_y)
                gap = collapsed_margin(box->margin.top, gap) - box->margin.top;
            cursor_y += gap;
            if (clr) {
                double y_after_clear = floats_clear_y(floats, cursor_y, clr);
                if (y_after_clear > cursor_y) cursor_y = y_after_clear;
            }
            double left_off = 0, right_off = 0;
            floats_offsets_at(floats, cursor_y, &left_off, &right_off);
            floats_advance_to_readable_width(floats, cw, &cursor_y,
                                             &left_off, &right_off);
            double cw_avail = cw - left_off - right_off;
            if (cw_avail < 0) cw_avail = 0;
            c->x = inner_x + left_off;
            c->y = cursor_y - mt;
            layout_box(c, cw_avail, child_inherited);
            if (box_establishes_bfc(c) && cw_avail > 0) {
                double span_l = 0, span_r = 0;
                double c_h = c->content_height +
                             c->padding.top + c->padding.bottom +
                             c->border.top + c->border.bottom;
                floats_offsets_over(floats, cursor_y, cursor_y + c_h,
                                    &span_l, &span_r);
                if (span_l > left_off || span_r > right_off) {
                    double retry = cw - MAX(span_l, left_off)
                                      - MAX(span_r, right_off);
                    if (retry > 0 && retry < cw_avail) {
                        left_off = MAX(span_l, left_off);
                        right_off = MAX(span_r, right_off);
                        cw_avail = retry;
                        c->x = inner_x + left_off;
                        c->y = cursor_y - mt;
                        layout_box(c, cw_avail, child_inherited);
                    }
                }
            }
            if (keyword_is(box->style ? box->style->values[NS_CSS_DIRECTION]
                                      : NULL, "rtl") &&
                !style_is_absolute_or_fixed(c->style)) {
                double outer = c->margin.left + c->margin.right +
                               c->content_width + c->padding.left +
                               c->padding.right + c->border.left +
                               c->border.right;
                if (fabs(outer - cw_avail) > 0.01)
                    shift_box_tree(c, cw_avail - outer, 0);
            }
            if (c->kind == NS_BOX_BLOCK)
                legacy_align_block_child(c, inner_x + left_off, cw_avail,
                                         child_inherited);
            collect_escaping_floats(c, floats, 0);
            if (empty_block_collapses_through(c, clr)) {
                cursor_y -= gap;
                prev_margin_bottom = collapsed_margin(
                    collapsed_margin(prev_margin_bottom, mt),
                    c->margin.bottom);
            } else {
                cursor_y += c->content_height +
                            c->padding.top + c->padding.bottom +
                            c->border.top + c->border.bottom;
                prev_margin_bottom = c->margin.bottom;
            }
            inline_line_top = -1;
        } else {
            cursor_y += prev_margin_bottom;
            prev_margin_bottom = 0;
            double left_off = 0, right_off = 0;
            floats_offsets_at(floats, cursor_y, &left_off, &right_off);
            floats_advance_to_readable_width(floats, cw, &cursor_y,
                                             &left_off, &right_off);
            double cw_avail = cw - left_off - right_off;
            if (cw_avail < 0) cw_avail = 0;
            c->x = inner_x + left_off;
            c->y = cursor_y;
            layout_box(c, cw_avail, child_inherited);
            collect_escaping_floats(c, floats, 0);
            double line_height = inline_line_height(child_inherited);
            inline_line_top = c->y + MAX(0, c->content_height - line_height);
            cursor_y += c->content_height;
        }
        if ((c->kind == NS_BOX_IMAGE || c->kind == NS_BOX_VIDEO ||
             c->kind == NS_BOX_SVG || c->kind == NS_BOX_TABLE) &&
            c->content_width < cw) {
            double outer = c->content_width;
            if (c->kind == NS_BOX_TABLE)
                outer += c->padding.left + c->padding.right +
                         c->border.left + c->border.right +
                         c->margin.left + c->margin.right;
            const ns_css_value *ta = child_inherited
                ? child_inherited->values[NS_CSS_TEXT_ALIGN] : NULL;
            gboolean self_center = FALSE, self_right = FALSE;
            if (c->kind == NS_BOX_TABLE) {
                const char *al = c->dom ? ns_element_get_attr(c->dom, "align") : NULL;
                if (al && g_ascii_strcasecmp(al, "center") == 0) self_center = TRUE;
                else if (al && g_ascii_strcasecmp(al, "right") == 0) self_right = TRUE;
                const ns_css_value *ml = c->style ? c->style->values[NS_CSS_MARGIN_LEFT] : NULL;
                const ns_css_value *mr = c->style ? c->style->values[NS_CSS_MARGIN_RIGHT] : NULL;
                if (keyword_is(ml, "auto") && keyword_is(mr, "auto")) self_center = TRUE;
            }
            if ((keyword_is(ta, "center") || self_center) && outer < cw)
                shift_box_tree(c, inner_x + (cw - outer) / 2.0 - c->x, 0);
            else if ((keyword_is(ta, "right") || keyword_is(ta, "end") || self_right) && outer < cw)
                shift_box_tree(c, inner_x + (cw - outer) - c->x, 0);
        }
    }
    if (collapse_bottom_with_parent) {
        box->margin.bottom = collapsed_margin(box->margin.bottom,
                                              prev_margin_bottom);
    } else {
        cursor_y += prev_margin_bottom;
    }

    if (box_establishes_bfc(box) && floats && floats->len > 0) {
        double fb = floats_max_bottom(floats);
        if (fb > cursor_y) cursor_y = fb;
    }
    g_array_free(floats, TRUE);

    if (n_cols > 1 &&
        !layout_multicol_single_inline(box, inner_x, inner_y, cw, col_gap,
                                       n_cols, child_inherited, &cursor_y)) {
        ns_box *host = column_host ? column_host : box;
        double flow_x = column_host && host->first_child
            ? host->first_child->x : inner_x;
        double flow_y = column_host && host->first_child
            ? host->first_child->y : inner_y;
        double total_h = cursor_y - inner_y;
        if (column_host) {
            total_h = 0;
            for (ns_box *c = host->first_child; c; c = c->next_sibling) {
                if (c->kind == NS_BOX_BLOCK || c->kind == NS_BOX_TABLE)
                    total_h += c->content_height +
                               c->padding.top + c->padding.bottom +
                               c->border.top + c->border.bottom;
                else
                    total_h += c->content_height;
            }
        }
        double target_h = total_h / n_cols;
        double cur_y = 0;
        double max_col_h = 0;
        int cur_col = 0;
        for (ns_box *c = host->first_child; c; c = c->next_sibling) {
            double c_full_h;
            if (c->kind == NS_BOX_BLOCK || c->kind == NS_BOX_TABLE) {
                c_full_h = c->content_height +
                           c->padding.top + c->padding.bottom +
                           c->border.top + c->border.bottom;
            } else {
                c_full_h = c->content_height;
            }
            if (cur_col < n_cols - 1 &&
                cur_y > 0 && cur_y + c_full_h > target_h) {
                cur_col++;
                cur_y = 0;
            }
            double target_x = flow_x + cur_col * (cw + col_gap);
            double target_y = flow_y + cur_y;
            double dx = target_x - c->x;
            double dy = target_y - c->y;
            if (dx != 0 || dy != 0) shift_box_tree(c, dx, dy);
            cur_y += c_full_h;
            if (cur_y > max_col_h) max_col_h = cur_y;
        }
        box->content_width = cw * n_cols + col_gap * (n_cols - 1);
        if (column_host) {
            column_host->content_width = box->content_width;
            column_host->content_height = max_col_h;
            cursor_y = flow_y + max_col_h +
                       column_host->padding.bottom +
                       column_host->border.bottom +
                       column_host->margin.bottom;
        } else {
            cursor_y = inner_y + max_col_h;
        }
    }

flex_done: ;
    const ns_css_value *hv  = box->style ? box->style->values[NS_CSS_HEIGHT]     : NULL;
    const ns_css_value *mxh = box->style ? box->style->values[NS_CSS_MAX_HEIGHT] : NULL;
    const ns_css_value *mnh = box->style ? box->style->values[NS_CSS_MIN_HEIGHT] : NULL;
    const char *ovx = box->style
        ? overflow_axis_keyword(box->style, NS_CSS_OVERFLOW_X) : NULL;
    const char *ovy = box->style
        ? overflow_axis_keyword(box->style, NS_CSS_OVERFLOW_Y) : NULL;
    gboolean overflow_scrolls  = overflow_kw_scrolls(ovy);
    gboolean overflow_scrolls_x = overflow_kw_scrolls(ovx);
    if (legend) box_prepend_child(box, legend);
    double measured = cursor_y - inner_y + legend_extra;
    box->measured_content_height = measured;
    double explicit_h = -1;
    if (hv && (hv->kind == NS_CSS_V_LENGTH || hv->kind == NS_CSS_V_CALC))
        explicit_h = resolve_used_height(box, hv, parent_content_width, -1);
    double stretch_h = -1;
    if (height_keyword_stretches(hv) || height_keyword_stretches(mnh) ||
        height_keyword_stretches(mxh)) {
        double cb_h = containing_block_definite_height(box);
        if (cb_h >= 0) {
            stretch_h = cb_h - box->margin.top - box->margin.bottom
                      - box->border.top - box->border.bottom
                      - box->padding.top - box->padding.bottom;
            if (stretch_h < 0) stretch_h = 0;
        }
    }
    if (explicit_h < 0 && stretch_h >= 0 && height_keyword_stretches(hv)) {
        explicit_h = stretch_h;
        if (border_box) explicit_h += vert_extras;
    }
    if (explicit_h >= 0) {
        if (border_box) {
            explicit_h -= vert_extras;
            if (explicit_h < 0) explicit_h = 0;
        }
        box->content_height = explicit_h;
    } else {
        double ratio = box->style
            ? aspect_ratio_number(box->style->values[NS_CSS_ASPECT_RATIO], NULL) : -1;
        if (ratio > 0 && box->content_width > 0) {
            double aspect_h = box->content_width / ratio;
            box->content_height = aspect_h > measured ? aspect_h : measured;
        } else {
            box->content_height = measured;
        }
    }
    double max_h = resolve_used_height(box, mxh, parent_content_width, -1);
    if (border_box && max_h >= 0) {
        max_h -= vert_extras;
        if (max_h < 0) max_h = 0;
    }
    if (max_h < 0 && stretch_h >= 0 && height_keyword_stretches(mxh))
        max_h = stretch_h;
    if (max_h < 0 && size_keyword_is_intrinsic(mxh))
        max_h = measured;
    if (max_h >= 0 && box->content_height > max_h)
        box->content_height = max_h;
    double min_h = resolve_used_height(box, mnh, parent_content_width, -1);
    if (min_h < 0 && stretch_h >= 0 && height_keyword_stretches(mnh))
        min_h = stretch_h + (border_box ? vert_extras : 0);
    if (border_box && min_h >= 0) {
        min_h -= vert_extras;
        if (min_h < 0) min_h = 0;
    }
    if (min_h < 0 && size_keyword_is_intrinsic(mnh))
        min_h = measured;
    if (min_h >= 0 && box->content_height < min_h)
        box->content_height = min_h;
    if (box->dom && box->dom->kind == NS_NODE_ELEMENT && box->dom->name &&
        strcmp(box->dom->name, "input") == 0 &&
        box->content_height > measured + 0.5) {
        double shift = (box->content_height - measured) * 0.5;
        for (ns_box *c = box->first_child; c; c = c->next_sibling)
            c->y += shift;
    }
    if (box->dom && box->dom->kind == NS_NODE_ELEMENT && box->dom->name &&
        strcmp(box->dom->name, "button") == 0 &&
        !keyword_is(box->style ? box->style->values[NS_CSS_APPEARANCE] : NULL,
                    "none") &&
        box->content_height > measured + 0.5) {
        double shift = (box->content_height - measured) * 0.5;
        for (ns_box *c = box->first_child; c; c = c->next_sibling) {
            if (!style_is_absolute_or_fixed(c->style))
                shift_box_tree(c, 0, shift);
        }
    }
    if (overflow_scrolls) {
        box->scrolls = TRUE;
        if (measured > box->content_height)
            box->scroll_max_y = measured - box->content_height;
    }
    if (overflow_scrolls_x) {
        double content_right = inner_x + box->content_width;
        double padding_right = content_right + box->padding.right;
        double max_right = padding_right;
        for (const ns_box *c = box->first_child; c; c = c->next_sibling) {
            if (style_is_absolute_or_fixed(c->style)) continue;
            double right = c->x + c->margin.left + c->content_width +
                           c->padding.left + c->padding.right +
                           c->border.left + c->border.right;
            double flow_right = MIN(right + c->margin.right,
                                    MAX(right, content_right)) +
                                box->padding.right;
            if (right > max_right) max_right = right;
            if (flow_right > max_right) max_right = flow_right;
        }
        if (max_right > padding_right + 1.0) {
            box->scrolls = TRUE;
            box->scroll_max_x = max_right - padding_right;
        }
    }
    if (style_content_visibility_hidden(box->style)) {
        double contained_height = explicit_h >= 0 ? explicit_h : 0;
        if (max_h >= 0 && contained_height > max_h)
            contained_height = max_h;
        if (min_h >= 0 && contained_height < min_h)
            contained_height = min_h;
        box->content_height = contained_height;
        box->scrolls = FALSE;
        box->scroll_max_x = 0;
        box->scroll_max_y = 0;
    }
}

typedef struct {
    double w, h;
} ns_frame_viewport;

static GHashTable *g_frame_viewports;

static void
record_frame_viewports_walk(const ns_box *b)
{
    if (!b) return;
    if (b->dom && b->dom->kind == NS_NODE_ELEMENT && b->dom->name &&
        (strcmp(b->dom->name, "iframe") == 0 ||
         strcmp(b->dom->name, "frame") == 0)) {
        ns_frame_viewport *v = g_new0(ns_frame_viewport, 1);
        v->w = b->content_width > 0 ? b->content_width : 0;
        v->h = b->content_height > 0 ? b->content_height : 0;
        g_hash_table_insert(g_frame_viewports, (gpointer)b->dom, v);
    }
    for (const ns_box *c = b->first_child; c; c = c->next_sibling)
        record_frame_viewports_walk(c);
}

static void
record_frame_viewports(const ns_box *root)
{
    if (!g_frame_viewports)
        g_frame_viewports = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                                  NULL, g_free);
    else
        g_hash_table_remove_all(g_frame_viewports);
    record_frame_viewports_walk(root);
}

gboolean
ns_layout_frame_viewport(const ns_node *frame, double *w, double *h)
{
    if (!frame || !g_frame_viewports) return FALSE;
    const ns_frame_viewport *v = g_hash_table_lookup(g_frame_viewports, frame);
    if (!v) return FALSE;
    if (w) *w = v->w;
    if (h) *h = v->h;
    return TRUE;
}

static double
control_char_cell_px(const ns_style *s)
{
    NsPangoLayout *layout = make_pango_layout(s);
    if (!layout) return 0;
    NsPangoContext *ctx = ns_pango_layout_get_context(layout);
    const NsPangoFontDescription *fd =
        ns_pango_layout_get_font_description(layout);
    if (!fd) fd = ns_pango_context_get_font_description(ctx);
    NsPangoFontMetrics *fm = fd ? ns_pango_context_get_metrics(ctx, fd, NULL)
                                : NULL;
    double cell = 0;
    if (fm) {
        cell = (double)ns_pango_font_metrics_get_approximate_char_width(fm) /
               NS_PANGO_SCALE;
        ns_pango_font_metrics_unref(fm);
    }
    g_object_unref(layout);
    return cell;
}

static gboolean
node_is_windowed_text_input(const ns_node *n)
{
    if (!n || n->kind != NS_NODE_ELEMENT || !n->name) return FALSE;
    if (strcmp(n->name, "input") != 0) return FALSE;
    const char *type = ns_element_get_attr(n, "type");
    return !type || !*type ||
        g_ascii_strcasecmp(type, "text") == 0 ||
        g_ascii_strcasecmp(type, "search") == 0 ||
        g_ascii_strcasecmp(type, "email") == 0 ||
        g_ascii_strcasecmp(type, "url") == 0 ||
        g_ascii_strcasecmp(type, "tel") == 0 ||
        g_ascii_strcasecmp(type, "number") == 0 ||
        g_ascii_strcasecmp(type, "password") == 0;
}

static glong
text_input_display_cps(const ns_node *n)
{
    const char *v = ns_input_used_value(n);
    if (!v || !*v) v = ns_element_get_attr(n, "placeholder");
    return (v && *v) ? g_utf8_strlen(v, -1) : 0;
}

static gboolean
control_width_is_definite(const ns_style *s)
{
    double fs = length_or(s->values[NS_CSS_FONT_SIZE], 16);
    return control_dim_px_clamped(s, NS_CSS_WIDTH, NS_CSS_MIN_WIDTH,
                                  NS_CSS_MAX_WIDTH, fs, 0) > 0;
}

static gboolean
collect_text_input_columns(const ns_box *b, GHashTable *cols)
{
    if (!b) return FALSE;
    gboolean changed = FALSE;
    if (b->style && b->content_width > 0 &&
        node_is_windowed_text_input(b->dom)) {
        int have = text_input_size_attr(b->dom);
        glong shown = text_input_display_cps(b->dom);
        double cell = shown > 0 ? control_char_cell_px(b->style) : 0;
        if (cell > 0) {
            int overhead = 2 + text_input_leading_spaces(b->style);
            int fit = (int)floor(b->content_width / cell) - overhead;
            if (fit < 1) fit = 1;
            gboolean grow = fit > have && shown > have;
            gboolean shrink = fit < have && shown > fit &&
                              control_width_is_definite(b->style);
            if (grow || shrink) {
                g_hash_table_insert(cols, (gpointer)b->dom,
                                    GINT_TO_POINTER(fit));
                changed = TRUE;
            }
        }
    }
    for (const ns_box *c = b->first_child; c; c = c->next_sibling)
        if (collect_text_input_columns(c, cols)) changed = TRUE;
    if (b->inline_atomics)
        for (guint i = 0; i < b->inline_atomics->len; i++)
            if (collect_text_input_columns(
                    g_array_index(b->inline_atomics, ns_inline_atomic, i).box,
                    cols))
                changed = TRUE;
    return changed;
}

ns_box *
ns_layout_build(const ns_node *doc, GHashTable *styles, double viewport_width,
                const ns_node *focused_input, gsize focused_caret_byte,
                gsize focused_sel_anchor_byte,
                struct ns_image_cache *image_cache, const char *base_url)
{
    g_focused_input_for_layout = focused_input;
    g_focused_is_contenteditable_for_layout =
        focused_input && focused_input->kind == NS_NODE_ELEMENT &&
        focused_input->name &&
        strcmp(focused_input->name, "input") != 0 &&
        strcmp(focused_input->name, "textarea") != 0 &&
        ns_ce_attr_enables(ns_element_get_attr(focused_input,
                                               "contenteditable"));
    g_focused_caret_byte_for_layout = focused_caret_byte;
    g_focused_sel_anchor_byte_for_layout = focused_sel_anchor_byte;
    g_image_cache_for_layout = image_cache;
    g_base_url_for_layout = base_url;
    g_svg_defs_computed_for_layout = FALSE;
    ns_image_cache_begin_generation(image_cache);
    g_counters_for_layout = build_counter_snapshots(doc, styles);
    ns_box *root = ns_layout_build_(doc, styles, viewport_width);
    if (!g_input_columns_for_layout) {
        GHashTable *cols = g_hash_table_new(g_direct_hash, g_direct_equal);
        if (collect_text_input_columns(root, cols)) {
            g_input_columns_for_layout = cols;
            ns_box_free(root);
            ns_image_cache_begin_generation(image_cache);
            root = ns_layout_build_(doc, styles, viewport_width);
            g_input_columns_for_layout = NULL;
        }
        g_hash_table_destroy(cols);
    }
    ns_image_cache_collect(image_cache);
    g_focused_input_for_layout = NULL;
    g_focused_is_contenteditable_for_layout = FALSE;
    g_focused_caret_byte_for_layout = 0;
    g_focused_sel_anchor_byte_for_layout = 0;
    g_image_cache_for_layout = NULL;
    g_base_url_for_layout = NULL;
    g_svg_defs_computed_for_layout = FALSE;
    if (g_counters_for_layout) {
        g_hash_table_destroy(g_counters_for_layout);
        g_counters_for_layout = NULL;
    }
    record_frame_viewports(root);
    return root;
}

static gboolean
style_is_relative(const ns_style *s)
{
    if (!s || !s->values[NS_CSS_POSITION]) return FALSE;
    const ns_css_value *v = s->values[NS_CSS_POSITION];
    return v->kind == NS_CSS_V_KEYWORD &&
           strcmp(v->u.keyword, "relative") == 0;
}

static double
length_or_zero(const ns_css_value *v, double basis)
{
    if (!v || v->kind != NS_CSS_V_LENGTH) return 0;
    if (v->u.length.unit == NS_CSS_UNIT_PERCENT)
        return v->u.length.v * basis / 100.0;
    if (v->u.length.unit == NS_CSS_UNIT_EM)
        return v->u.length.v * 16.0;
    return v->u.length.v;
}

static void
translate_subtree(ns_box *box, double dx, double dy)
{
    if (!box || (dx == 0 && dy == 0)) return;
    shift_box_tree(box, dx, dy);
}

static double
relative_pct_cb_height(const ns_box *box)
{
    const ns_box *p = box ? box->parent : NULL;
    while (p && !p->style) p = p->parent;
    if (!p) return -1;
    const ns_css_value *h = p->style->values[NS_CSS_HEIGHT];
    if (h && h->kind == NS_CSS_V_KEYWORD)
        return height_keyword_stretches(h) ? containing_block_definite_height(box) : -1;
    if (!h) return -1;
    if (value_is_percent(h)) {
        double base;
        if (p->dom && p->dom->name && strcmp(p->dom->name, "html") == 0)
            base = ns_css_viewport_h();
        else
            base = relative_pct_cb_height(p);
        if (base < 0) return -1;
        if (h->kind == NS_CSS_V_CALC)
            return h->u.calc.pct / 100.0 * base + h->u.calc.px;
        return h->u.length.v * base / 100.0;
    }
    if (p->content_height > 0) return p->content_height;
    return -1;
}

static void
apply_position_offsets(ns_box *box, double parent_w, double parent_h)
{
    if (!box) return;
    double child_w = box->content_width;
    double child_h = box->content_height;
    if (style_is_relative(box->style)) {
        const ns_css_value *lv = box->style->values[NS_CSS_LEFT];
        const ns_css_value *rv = box->style->values[NS_CSS_RIGHT];
        const ns_css_value *tv = box->style->values[NS_CSS_TOP];
        const ns_css_value *bv = box->style->values[NS_CSS_BOTTOM];
        gboolean l_auto = !lv || length_is_auto(lv);
        gboolean t_auto = !tv || length_is_auto(tv);
        double dx = 0, dy = 0;
        if (!l_auto)
            dx = length_or_zero(lv, parent_w);
        else if (rv && !length_is_auto(rv))
            dx = -length_or_zero(rv, parent_w);
        double cb_h = -2;
        if (!t_auto) {
            if (value_is_percent(tv)) {
                if (cb_h == -2) cb_h = relative_pct_cb_height(box);
                dy = cb_h < 0 ? 0 : length_or_zero(tv, cb_h);
            } else {
                dy = length_or_zero(tv, parent_h);
            }
        } else if (bv && !length_is_auto(bv)) {
            if (value_is_percent(bv)) {
                if (cb_h == -2) cb_h = relative_pct_cb_height(box);
                dy = cb_h < 0 ? 0 : -length_or_zero(bv, cb_h);
            } else {
                dy = -length_or_zero(bv, parent_h);
            }
        }
        translate_subtree(box, dx, dy);
    }
    for (ns_box *c = box->first_child; c; c = c->next_sibling)
        apply_position_offsets(c, child_w, child_h);
}

static void
abs_box_map_build(GHashTable *map, ns_box *root)
{
    if (!root) return;
    if (root->dom && !g_hash_table_contains(map, root->dom))
        g_hash_table_insert(map, (gpointer)root->dom, root);
    for (ns_box *c = root->first_child; c; c = c->next_sibling)
        abs_box_map_build(map, c);
    if (root->inline_atomics) {
        for (guint i = 0; i < root->inline_atomics->len; i++) {
            ns_box *ab = g_array_index(root->inline_atomics,
                                       ns_inline_atomic, i).box;
            abs_box_map_build(map, ab);
        }
    }
}

static gboolean
node_is_ancestor_of(const ns_node *a, const ns_node *n)
{
    if (!a || !n) return FALSE;
    for (const ns_node *p = n->parent; p; p = p->parent)
        if (p == a) return TRUE;
    return FALSE;
}

static GHashTable *g_node_order;

static void
node_order_build(const ns_node *root)
{
    g_node_order = g_hash_table_new(g_direct_hash, g_direct_equal);
    guint rank = 0;
    GQueue stack = G_QUEUE_INIT;
    g_queue_push_head(&stack, (gpointer)root);
    while (!g_queue_is_empty(&stack)) {
        const ns_node *n = g_queue_pop_head(&stack);
        g_hash_table_insert(g_node_order, (gpointer)n,
                            GUINT_TO_POINTER(++rank));
        for (const ns_node *c = n->last_child; c; c = c->prev_sibling)
            g_queue_push_head(&stack, (gpointer)c);
    }
}

static gboolean
node_precedes(const ns_node *a, const ns_node *b)
{
    if (!a || !b || a == b) return FALSE;
    if (g_node_order) {
        guint ra = GPOINTER_TO_UINT(g_hash_table_lookup(g_node_order,
                                                        (gpointer)a));
        guint rb = GPOINTER_TO_UINT(g_hash_table_lookup(g_node_order,
                                                        (gpointer)b));
        if (ra && rb) return ra < rb;
    }
    int da = 0, db = 0;
    for (const ns_node *p = a; p; p = p->parent) da++;
    for (const ns_node *p = b; p; p = p->parent) db++;
    const ns_node *pa = a, *pb = b;
    while (da > db + 1) { pa = pa->parent; da--; }
    while (db > da + 1) { pb = pb->parent; db--; }
    if (da > db) { if (pa->parent == pb) return FALSE; pa = pa->parent; da--; }
    else if (db > da) { if (pb->parent == pa) return TRUE; pb = pb->parent; db--; }
    while (pa->parent != pb->parent) {
        pa = pa->parent;
        pb = pb->parent;
        if (!pa || !pb) return FALSE;
    }
    for (const ns_node *s = pa->next_sibling; s; s = s->next_sibling)
        if (s == pb) return TRUE;
    return FALSE;
}

static double
box_outer_bottom(const ns_box *b)
{
    if (!b) return 0;
    return b->y + b->margin.top + b->border.top + b->padding.top +
           b->content_height + b->padding.bottom + b->border.bottom +
           b->margin.bottom;
}

static void
static_abs_y_walk(const ns_box *b, const ns_node *target, double *out)
{
    if (!b || !target || !out) return;
    if (b->dom && b->dom != target && !node_is_ancestor_of(b->dom, target)) {
        if (node_precedes(target, b->dom)) return;
        if (style_is_absolute_or_fixed(b->style)) return;
        if (node_precedes(b->dom, target) &&
            !style_is_absolute_or_fixed(b->style)) {
            double bottom = box_outer_bottom(b);
            if (bottom > *out) *out = bottom;
        }
    }
    if (b->dom && b->dom != target && node_is_ancestor_of(b->dom, target) &&
        !style_is_absolute_or_fixed(b->style)) {
        double edge = b->y + b->margin.top + b->border.top + b->padding.top;
        if (edge > *out) *out = edge;
    }
    for (const ns_box *c = b->first_child; c; c = c->next_sibling)
        static_abs_y_walk(c, target, out);
}

static double
static_abs_x_from_ancestors(const ns_box *cb, const ns_node *target,
                            GHashTable *box_map, double fallback,
                            gboolean *out_rtl, double *out_right)
{
    double best = fallback;
    guint best_depth = 0;
    *out_rtl = cb && cb->style &&
        ns_css_keyword_is(cb->style->values[NS_CSS_DIRECTION], "rtl");
    *out_right = cb ? cb->x + cb->margin.left + cb->border.left +
                      cb->padding.left + cb->content_width : fallback;
    for (const ns_node *p = target ? target->parent : NULL; p; p = p->parent) {
        if (p->kind != NS_NODE_ELEMENT) continue;
        const ns_box *pb = g_hash_table_lookup(box_map, p);
        if (!pb || style_is_absolute_or_fixed(pb->style)) {
            if (pb && pb == cb) break;
            continue;
        }
        gboolean inside_cb = FALSE;
        for (const ns_box *a = pb; a; a = a->parent)
            if (a == cb) { inside_cb = TRUE; break; }
        if (!inside_cb) break;
        guint depth = 0;
        for (const ns_node *q = target; q && q != p; q = q->parent) depth++;
        if (best_depth == 0 || depth < best_depth) {
            best = pb->x + pb->margin.left + pb->border.left + pb->padding.left;
            *out_right = best + pb->content_width;
            *out_rtl = pb->style &&
                ns_css_keyword_is(pb->style->values[NS_CSS_DIRECTION], "rtl");
            best_depth = depth;
        }
        if (pb == cb) break;
    }
    return best;
}

typedef struct ns_abs_static_calc {
    guint entry_index;
    guint rank;
    double y;
    gboolean resolved;
} ns_abs_static_calc;

static int
abs_static_calc_rank_cmp(gconstpointer a, gconstpointer b)
{
    const ns_abs_static_calc *ca = a, *cb = b;
    if (ca->rank < cb->rank) return -1;
    if (ca->rank > cb->rank) return 1;
    return 0;
}

static void
static_abs_y_batch_walk(const ns_box *b, GArray *calcs, guint *next,
                        double *cur_max)
{
    if (!b || *next >= calcs->len) return;
    if (b->dom) {
        guint rb = GPOINTER_TO_UINT(
            g_hash_table_lookup(g_node_order, (gpointer)b->dom));
        if (rb) {
            while (*next < calcs->len &&
                   g_array_index(calcs, ns_abs_static_calc, *next).rank <= rb) {
                ns_abs_static_calc *c =
                    &g_array_index(calcs, ns_abs_static_calc, *next);
                c->y = *cur_max;
                c->resolved = TRUE;
                (*next)++;
            }
        }
    }
    if (b->dom && !style_is_absolute_or_fixed(b->style)) {
        double content_top = b->y + b->margin.top + b->border.top +
                             b->padding.top;
        if (content_top > *cur_max) *cur_max = content_top;
    }
    for (const ns_box *c = b->first_child; c; c = c->next_sibling)
        static_abs_y_batch_walk(c, calcs, next, cur_max);
    while (b->dom && *next < calcs->len) {
        ns_abs_static_calc *c = &g_array_index(calcs, ns_abs_static_calc, *next);
        const ns_abs_entry *e =
            &g_array_index(g_abs_pending, ns_abs_entry, c->entry_index);
        if (!node_is_ancestor_of(b->dom, e->dom)) break;
        c->y = *cur_max;
        c->resolved = TRUE;
        (*next)++;
    }
    if (b->dom && !style_is_absolute_or_fixed(b->style)) {
        double bottom = box_outer_bottom(b);
        if (bottom > *cur_max) *cur_max = bottom;
    }
}

static const ns_node *find_abs_containing_block_dom(const ns_node *n,
                                                    GHashTable *styles);
static gboolean style_creates_abs_cb(const ns_style *s);
static gboolean style_creates_fixed_cb(const ns_style *s);

static const ns_node *
abs_entry_cb_dom(const ns_abs_entry *e, GHashTable *styles)
{
    if (e->pseudo) {
        const ns_style *hs = g_hash_table_lookup(styles, e->dom);
        if (style_creates_abs_cb(hs)) return e->dom;
    }
    return find_abs_containing_block_dom(e->dom, styles);
}

static const ns_node *
fixed_entry_cb_dom(const ns_abs_entry *e, GHashTable *styles)
{
    if (e->pseudo &&
        style_creates_fixed_cb(g_hash_table_lookup(styles, e->dom)))
        return e->dom;
    for (const ns_node *p = e->dom ? e->dom->parent : NULL; p; p = p->parent) {
        if (p->kind != NS_NODE_ELEMENT) continue;
        if (style_creates_fixed_cb(g_hash_table_lookup(styles, p))) return p;
    }
    return NULL;
}

static const ns_node *
positioned_entry_cb_dom(const ns_abs_entry *e, GHashTable *styles)
{
    return e->fixed ? fixed_entry_cb_dom(e, styles) : abs_entry_cb_dom(e, styles);
}

static double
flex_static_main_offset(const char *justify, double free_space,
                        gboolean row, gboolean main_reverse, gboolean rtl)
{
    gboolean flipped = main_reverse != (rtl && row);
    gboolean phys_start_is_end = row && rtl;
    if (strcmp(justify, "start") == 0)
        return phys_start_is_end ? free_space : 0;
    if (strcmp(justify, "end") == 0)
        return phys_start_is_end ? 0 : free_space;
    if (strcmp(justify, "left") == 0)
        return row ? 0 : (phys_start_is_end ? free_space : 0);
    if (strcmp(justify, "right") == 0)
        return row ? free_space : (phys_start_is_end ? free_space : 0);
    double main = 0;
    if (strcmp(justify, "flex-end") == 0)
        main = free_space;
    else if (strcmp(justify, "center") == 0 ||
             strcmp(justify, "space-around") == 0 ||
             strcmp(justify, "space-evenly") == 0)
        main = free_space / 2;
    return flipped ? free_space - main : main;
}

static double
flex_static_cross_offset(const char *align, double free_space,
                         gboolean row, gboolean wrap_reverse, gboolean rtl,
                         gboolean self_rtl)
{
    gboolean flipped = wrap_reverse != (rtl && !row);
    gboolean phys_start_is_end = !row && rtl;
    gboolean self_start_is_end = !row && self_rtl;
    if (strcmp(align, "start") == 0 || flex_align_is_baseline(align))
        return phys_start_is_end ? free_space : 0;
    if (strcmp(align, "end") == 0 || strcmp(align, "last baseline") == 0)
        return phys_start_is_end ? 0 : free_space;
    if (strcmp(align, "self-start") == 0)
        return self_start_is_end ? free_space : 0;
    if (strcmp(align, "self-end") == 0)
        return self_start_is_end ? 0 : free_space;
    if (strcmp(align, "left") == 0)
        return row ? (phys_start_is_end ? free_space : 0) : 0;
    if (strcmp(align, "right") == 0)
        return row ? (phys_start_is_end ? free_space : 0) : free_space;
    double cross = 0;
    if (strcmp(align, "flex-end") == 0)
        cross = free_space;
    else if (strcmp(align, "center") == 0 || strcmp(align, "self-center") == 0)
        cross = free_space / 2;
    return flipped ? free_space - cross : cross;
}

static const ns_box *
abs_flex_parent_box(const ns_node *dom, GHashTable *box_map)
{
    const ns_node *p = dom ? dom->parent : NULL;
    while (p && p->kind != NS_NODE_ELEMENT) p = p->parent;
    return p ? g_hash_table_lookup(box_map, p) : NULL;
}

static gboolean
flex_static_position(ns_box *abox, const ns_box *fc, double *out_x,
                     double *out_y)
{
    if (!fc || !fc->style || !style_is_flex_container(fc->style)) return FALSE;

    double origin_x = fc->x + fc->margin.left + fc->border.left +
                      fc->padding.left;
    double origin_y = fc->y + fc->margin.top + fc->border.top +
                      fc->padding.top;
    double outer_w = abox->content_width + abox->padding.left +
                     abox->padding.right + abox->border.left +
                     abox->border.right + abox->margin.left +
                     abox->margin.right;
    double outer_h = abox->content_height + abox->padding.top +
                     abox->padding.bottom + abox->border.top +
                     abox->border.bottom + abox->margin.top +
                     abox->margin.bottom;

    const char *dir = flex_direction_of(fc->style);
    gboolean row = strncmp(dir, "row", 3) == 0;
    gboolean main_reverse = strstr(dir, "-reverse") != NULL;
    gboolean wrap_reverse = ns_css_keyword_is(
        fc->style->values[NS_CSS_FLEX_WRAP], "wrap-reverse");
    gboolean rtl = ns_css_keyword_is(fc->style->values[NS_CSS_DIRECTION],
                                     "rtl");

    double main_free = (row ? fc->content_width : fc->content_height) -
                       (row ? outer_w : outer_h);
    double cross_free = (row ? fc->content_height : fc->content_width) -
                        (row ? outer_h : outer_w);

    double main = flex_static_main_offset(
        keyword_or(fc->style, NS_CSS_JUSTIFY_CONTENT, "flex-start"), main_free,
        row, main_reverse, rtl);
    gboolean self_rtl = abox->style &&
        ns_css_keyword_is(abox->style->values[NS_CSS_DIRECTION], "rtl");
    double cross = flex_static_cross_offset(
        flex_item_align(abox, keyword_or(fc->style, NS_CSS_ALIGN_ITEMS,
                                         "stretch")), cross_free,
        row, wrap_reverse, rtl, self_rtl);

    *out_x = origin_x + (row ? main : cross);
    *out_y = origin_y + (row ? cross : main);
    return TRUE;
}

static void
abs_calc_array_free(gpointer a)
{
    g_array_free(a, TRUE);
}

static void
static_abs_y_precompute(ns_box *root, GHashTable *box_map, GHashTable *styles,
                        double *out_y, gboolean *out_resolved)
{
    GHashTable *by_cb = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                              NULL, abs_calc_array_free);
    for (guint i = 0; i < g_abs_pending->len; i++) {
        ns_abs_entry e = g_array_index(g_abs_pending, ns_abs_entry, i);
        ns_abs_static *st = (g_abs_static && !e.pseudo)
            ? g_hash_table_lookup(g_abs_static, e.dom) : NULL;
        if (st && st->run) continue;
        guint rank = GPOINTER_TO_UINT(
            g_hash_table_lookup(g_node_order, (gpointer)e.dom));
        if (!rank) continue;
        const ns_node *cb_dom = positioned_entry_cb_dom(&e, styles);
        ns_box *cb = cb_dom ? g_hash_table_lookup(box_map, cb_dom) : root;
        if (cb_dom && !cb) continue;
        if (!cb) cb = root;
        GArray *calcs = g_hash_table_lookup(by_cb, cb);
        if (!calcs) {
            calcs = g_array_new(FALSE, FALSE, sizeof(ns_abs_static_calc));
            g_hash_table_insert(by_cb, cb, calcs);
        }
        ns_abs_static_calc c = { i, rank, 0, FALSE };
        g_array_append_val(calcs, c);
    }

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, by_cb);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        ns_box *cb = key;
        GArray *calcs = value;
        g_array_sort(calcs, abs_static_calc_rank_cmp);
        double cur_max = cb->y + cb->margin.top + cb->border.top +
                         cb->padding.top;
        guint next = 0;
        static_abs_y_batch_walk(cb, calcs, &next, &cur_max);
        for (guint k = 0; k < calcs->len; k++) {
            const ns_abs_static_calc *c =
                &g_array_index(calcs, ns_abs_static_calc, k);
            if (!c->resolved) continue;
            out_y[c->entry_index] = c->y;
            out_resolved[c->entry_index] = TRUE;
        }
    }
    g_hash_table_destroy(by_cb);
}

static gboolean
style_creates_abs_cb(const ns_style *s)
{
    if (!s) return FALSE;
    const ns_css_value *v = s->values[NS_CSS_POSITION];
    if (v && v->kind == NS_CSS_V_KEYWORD && v->u.keyword) {
        const char *kw = v->u.keyword;
        if (strcmp(kw, "relative") == 0 || strcmp(kw, "absolute") == 0 ||
            strcmp(kw, "fixed") == 0    || strcmp(kw, "sticky") == 0)
            return TRUE;
    }
    return style_creates_fixed_cb(s);
}

static gboolean
style_creates_fixed_cb(const ns_style *s)
{
    if (!s) return FALSE;
    static const ns_css_prop tprops[4] = {
        NS_CSS_TRANSFORM, NS_CSS_TRANSLATE, NS_CSS_ROTATE, NS_CSS_SCALE,
    };
    for (int i = 0; i < 4; i++) {
        const ns_css_value *tv = s->values[tprops[i]];
        if (tv && tv->kind == NS_CSS_V_TRANSFORM && tv->u.transform.n_ops > 0)
            return TRUE;
    }
    const ns_css_value *pv = s->values[NS_CSS_PERSPECTIVE];
    if (pv && pv->kind == NS_CSS_V_LENGTH && pv->u.length.v > 0) return TRUE;
    return FALSE;
}

static const ns_node *
find_abs_containing_block_dom(const ns_node *n, GHashTable *styles)
{
    for (const ns_node *p = n ? n->parent : NULL; p; p = p->parent) {
        if (p->kind != NS_NODE_ELEMENT) continue;
        const ns_style *ps = g_hash_table_lookup(styles, p);
        if (style_creates_abs_cb(ps)) return p;
    }
    return NULL;
}

static int
grid_abs_line(const char *tok, int explicit_tracks)
{
    if (!tok) return 0;
    while (*tok == ' ') tok++;
    if (!*tok || strcmp(tok, "auto") == 0 || g_str_has_prefix(tok, "span "))
        return 0;
    int before_start = -1;
    int past_end = explicit_tracks + 2;
    char *end = NULL;
    long n = strtol(tok, &end, 10);
    if (end != tok) {
        while (*end == ' ') end++;
        if (*end || n == 0) return 0;
        if (n < 0) n = explicit_tracks + 2 + n;
        if (n < 1) return before_start;
        if (n > explicit_tracks + 1) return past_end;
        return (int)n;
    }
    int named = grid_resolve_line_from(tok, explicit_tracks, 0);
    if (named < 1) return past_end;
    if (named > explicit_tracks + 1) return past_end;
    return named;
}

static void
grid_abs_axis_lines(const ns_style *st, gboolean row_axis, int explicit_tracks,
                    int *out_start, int *out_end)
{
    *out_start = 0;
    *out_end = 0;
    if (!st) return;
    char *start_tok = NULL, *end_tok = NULL;
    const ns_css_value *area = st->values[NS_CSS_GRID_AREA];
    if (area && area->kind == NS_CSS_V_KEYWORD && area->u.keyword) {
        char **parts = g_strsplit(area->u.keyword, "/", -1);
        int n = 0;
        while (parts[n]) n++;
        int si = row_axis ? 0 : 1, ei = row_axis ? 2 : 3;
        if (si < n) start_tok = g_strstrip(g_strdup(parts[si]));
        if (ei < n) end_tok = g_strstrip(g_strdup(parts[ei]));
        g_strfreev(parts);
    }
    const ns_css_value *sh =
        st->values[row_axis ? NS_CSS_GRID_ROW : NS_CSS_GRID_COLUMN];
    if (sh && sh->kind == NS_CSS_V_KEYWORD && sh->u.keyword) {
        g_free(start_tok);
        g_free(end_tok);
        start_tok = NULL;
        end_tok = NULL;
        const char *slash = strchr(sh->u.keyword, '/');
        if (slash) {
            start_tok = g_strstrip(g_strndup(sh->u.keyword, slash - sh->u.keyword));
            end_tok = g_strstrip(g_strdup(slash + 1));
        } else {
            start_tok = g_strstrip(g_strdup(sh->u.keyword));
        }
    }
    const ns_css_value *sv =
        st->values[row_axis ? NS_CSS_GRID_ROW_START : NS_CSS_GRID_COLUMN_START];
    const ns_css_value *ev =
        st->values[row_axis ? NS_CSS_GRID_ROW_END : NS_CSS_GRID_COLUMN_END];
    if (sv && sv->kind == NS_CSS_V_KEYWORD && sv->u.keyword) {
        g_free(start_tok);
        start_tok = g_strdup(sv->u.keyword);
    }
    if (ev && ev->kind == NS_CSS_V_KEYWORD && ev->u.keyword) {
        g_free(end_tok);
        end_tok = g_strdup(ev->u.keyword);
    }
    int s0 = grid_abs_line(start_tok, explicit_tracks);
    int e0 = grid_abs_line(end_tok, explicit_tracks);
    if (s0 && e0 && s0 > e0) { int t = s0; s0 = e0; e0 = t; }
    if (s0 && e0 && s0 == e0) e0 = 0;
    if (s0 == -1 || s0 >= explicit_tracks + 2) s0 = 0;
    if (e0 == -1 || e0 >= explicit_tracks + 2) e0 = 0;
    *out_start = s0;
    *out_end = e0;
    g_free(start_tok);
    g_free(end_tok);
}

static gboolean
grid_abs_containing_block(const ns_box *cb, const ns_style *st,
                          double *x, double *y, double *w, double *h)
{
    if (!cb || !cb->grid_col_tracks || !cb->grid_row_tracks || !st)
        return FALSE;
    int cs, ce, rs, re;
    const ns_css_value *cols_v = cb->style
        ? cb->style->values[NS_CSS_GRID_TEMPLATE_COLUMNS] : NULL;
    const ns_css_value *rows_v = cb->style
        ? cb->style->values[NS_CSS_GRID_TEMPLATE_ROWS] : NULL;
    grid_lines col_lines = { cols_v && cols_v->kind == NS_CSS_V_TRACKS
                             ? &cols_v->u.tracks : NULL, NULL, FALSE };
    grid_lines row_lines = { rows_v && rows_v->kind == NS_CSS_V_TRACKS
                             ? &rows_v->u.tracks : NULL, NULL, TRUE };
    g_grid_lines = &col_lines;
    grid_abs_axis_lines(st, FALSE, cb->grid_explicit_cols, &cs, &ce);
    g_grid_lines = &row_lines;
    grid_abs_axis_lines(st, TRUE, cb->grid_explicit_rows, &rs, &re);
    g_grid_lines = NULL;
    double pad_x0 = cb->x + cb->margin.left + cb->border.left;
    double pad_y0 = cb->y + cb->margin.top + cb->border.top;
    double pad_x1 = pad_x0 + cb->content_width + cb->padding.left + cb->padding.right;
    double pad_y1 = pad_y0 + cb->content_height + cb->padding.top + cb->padding.bottom;
    const GArray *ct = cb->grid_col_tracks, *rt = cb->grid_row_tracks;
    gboolean rtl = cb->style &&
        ns_css_keyword_is(cb->style->values[NS_CSS_DIRECTION], "rtl");
    double x0 = pad_x0, x1 = pad_x1, y0 = pad_y0, y1 = pad_y1;
    if (ct->len > 0 && (cs || ce)) {
        int n = (int)ct->len;
        double s_edge = rtl ? pad_x1 : pad_x0;
        double e_edge = rtl ? pad_x0 : pad_x1;
        if (cs) {
            const ns_grid_track_edges *t =
                &g_array_index(ct, ns_grid_track_edges, MIN(cs, n) - 1);
            s_edge = cs <= n ? (rtl ? t->end : t->start)
                             : (rtl ? t->start : t->end);
        }
        if (ce) {
            const ns_grid_track_edges *t =
                &g_array_index(ct, ns_grid_track_edges,
                               ce >= 2 ? MIN(ce - 2, n - 1) : 0);
            e_edge = ce >= 2 ? (rtl ? t->start : t->end)
                             : (rtl ? t->end : t->start);
        }
        x0 = MIN(s_edge, e_edge);
        x1 = MAX(s_edge, e_edge);
    }
    if (rs && rt->len > 0) {
        y0 = rs <= (int)rt->len
            ? g_array_index(rt, ns_grid_track_edges, rs - 1).start
            : g_array_index(rt, ns_grid_track_edges, rt->len - 1).end;
    }
    if (re && rt->len > 0) {
        y1 = re >= 2
            ? g_array_index(rt, ns_grid_track_edges, MIN(re - 2, (int)rt->len - 1)).end
            : g_array_index(rt, ns_grid_track_edges, 0).start;
    }
    *x = x0;
    *y = y0;
    *w = MAX(x1 - x0, 0);
    *h = MAX(y1 - y0, 0);
    return TRUE;
}

static void
position_absolute_box_in(ns_box *abox, double cb_inner_x, double cb_inner_y,
                         double cb_w, double cb_h);

static void
grid_append_line_names(GString *s, const ns_css_tracks *tk, int line)
{
    if (!tk) return;
    gboolean open = FALSE;
    for (int i = 0; i < tk->n_line_names; i++) {
        if (tk->line_names[i].line != line) continue;
        g_string_append(s, open ? " " : (s->len ? " [" : "["));
        g_string_append(s, tk->line_names[i].name);
        open = TRUE;
    }
    if (open) g_string_append_c(s, ']');
}

char *
ns_layout_grid_resolved_tracks(const ns_box *box, gboolean columns)
{
    if (!box) return NULL;
    const GArray *tr = columns ? box->grid_col_tracks : box->grid_row_tracks;
    if (!tr) return NULL;
    const ns_css_value *tv = box->style
        ? box->style->values[columns ? NS_CSS_GRID_TEMPLATE_COLUMNS
                                     : NS_CSS_GRID_TEMPLATE_ROWS]
        : NULL;
    const ns_css_tracks *tk = NULL;
    if (tv && tv->kind == NS_CSS_V_TRACKS && tv->u.tracks.subgrid) {
        const ns_box *p = box->parent;
        while (p && !p->style) p = p->parent;
        if (p && ns_display_is_grid_container(ns_css_display_of(p->style)))
            return NULL;
        tv = NULL;
    }
    if (tv && tv->kind == NS_CSS_V_TRACKS &&
        tv->u.tracks.auto_repeat == NS_CSS_AUTO_REPEAT_NONE)
        tk = &tv->u.tracks;
    if (tr->len == 0) return g_strdup("none");
    if (!tk && !(tv && tv->kind == NS_CSS_V_TRACKS)) {
        gboolean has_items = FALSE;
        for (const ns_box *c = box->first_child; c; c = c->next_sibling)
            if (!style_is_absolute_or_fixed(c->style)) { has_items = TRUE; break; }
        if (!has_items) return g_strdup("none");
    }
    GString *s = g_string_new(NULL);
    for (guint i = 0; i < tr->len; i++) {
        const ns_grid_track_edges *e = &g_array_index(tr, ns_grid_track_edges, i);
        grid_append_line_names(s, tk, (int)i + 1);
        if (s->len) g_string_append_c(s, ' ');
        double size = e->end - e->start;
        if (size < 0) size = 0;
        g_string_append_printf(s, "%gpx", round(size * 100.0) / 100.0);
    }
    grid_append_line_names(s, tk, (int)tr->len + 1);
    return g_string_free(s, FALSE);
}

static double
grid_static_align_offset(const char *align, double free_space, gboolean flip)
{
    if (!align || strcmp(align, "auto") == 0) return 0;
    gboolean at_end = strcmp(align, "end") == 0 || strcmp(align, "flex-end") == 0 ||
                      strcmp(align, "self-end") == 0 || strcmp(align, "last baseline") == 0;
    gboolean at_start = strcmp(align, "start") == 0 || strcmp(align, "flex-start") == 0 ||
                        strcmp(align, "self-start") == 0 || strcmp(align, "baseline") == 0;
    if (strcmp(align, "center") == 0) return free_space / 2;
    if (strcmp(align, "left") == 0) return flip ? free_space : 0;
    if (strcmp(align, "right") == 0) return flip ? 0 : free_space;
    if (at_end) return flip ? 0 : free_space;
    if (at_start) return flip ? free_space : 0;
    return flip ? free_space : 0;
}

static void
grid_static_position(ns_box *abox, const ns_box *cb,
                     double area_x, double area_y, double area_w, double area_h,
                     gboolean static_x, gboolean static_y)
{
    gboolean rtl = cb->style &&
        ns_css_keyword_is(cb->style->values[NS_CSS_DIRECTION], "rtl");
    double outer_w = abox->content_width + abox->padding.left + abox->padding.right +
                     abox->border.left + abox->border.right +
                     abox->margin.left + abox->margin.right;
    double outer_h = abox->content_height + abox->padding.top + abox->padding.bottom +
                     abox->border.top + abox->border.bottom +
                     abox->margin.top + abox->margin.bottom;
    if (static_x) {
        const char *js = abox->style ? ns_style_keyword(abox->style, NS_CSS_JUSTIFY_SELF) : NULL;
        if (!js || strcmp(js, "auto") == 0)
            js = keyword_or(cb->style, NS_CSS_JUSTIFY_ITEMS, "normal");
        abox->x = area_x + grid_static_align_offset(js, area_w - outer_w, rtl);
    }
    if (static_y) {
        const char *as = abox->style ? ns_style_keyword(abox->style, NS_CSS_ALIGN_SELF) : NULL;
        if (!as || strcmp(as, "auto") == 0)
            as = keyword_or(cb->style, NS_CSS_ALIGN_ITEMS, "normal");
        abox->y = area_y + grid_static_align_offset(as, area_h - outer_h, FALSE);
    }
}

static void
position_absolute_box(ns_box *abox, ns_box *cb, gboolean cb_is_icb)
{
    if (!abox || !cb) return;
    double cb_w = cb_is_icb ? ns_css_viewport_w()
                            : cb->content_width + cb->padding.left + cb->padding.right;
    double cb_h = cb_is_icb ? ns_css_viewport_h()
                            : cb->content_height + cb->padding.top + cb->padding.bottom;
    double cb_inner_x = cb->x + cb->margin.left + cb->border.left;
    double cb_inner_y = cb->y + cb->margin.top  + cb->border.top;
    position_absolute_box_in(abox, cb_inner_x, cb_inner_y, cb_w, cb_h);
}

static void
position_absolute_box_in(ns_box *abox, double cb_inner_x, double cb_inner_y,
                         double cb_w, double cb_h)
{
    const ns_style *s = abox->style;

    const ns_css_value *lv = s ? s->values[NS_CSS_LEFT]   : NULL;
    const ns_css_value *rv = s ? s->values[NS_CSS_RIGHT]  : NULL;
    const ns_css_value *tv = s ? s->values[NS_CSS_TOP]    : NULL;
    const ns_css_value *bv = s ? s->values[NS_CSS_BOTTOM] : NULL;

    gboolean l_auto = !lv || length_is_auto(lv);
    gboolean r_auto = !rv || length_is_auto(rv);
    gboolean t_auto = !tv || length_is_auto(tv);
    gboolean b_auto = !bv || length_is_auto(bv);

    double left   = l_auto ? 0 : length_resolve(lv, cb_w, 0);
    double right  = r_auto ? 0 : length_resolve(rv, cb_w, 0);
    double top    = t_auto ? 0 : length_resolve(tv, cb_h, 0);
    double bottom = b_auto ? 0 : length_resolve(bv, cb_h, 0);

    double box_outer_w = abox->content_width
                       + abox->padding.left + abox->padding.right
                       + abox->border.left  + abox->border.right
                       + abox->margin.left  + abox->margin.right;
    double box_outer_h = abox->content_height
                       + abox->padding.top + abox->padding.bottom
                       + abox->border.top  + abox->border.bottom
                       + abox->margin.top  + abox->margin.bottom;

    gboolean ml_auto = length_is_auto(s ? s->values[NS_CSS_MARGIN_LEFT]   : NULL);
    gboolean mr_auto = length_is_auto(s ? s->values[NS_CSS_MARGIN_RIGHT]  : NULL);
    gboolean mt_auto = length_is_auto(s ? s->values[NS_CSS_MARGIN_TOP]    : NULL);
    gboolean mb_auto = length_is_auto(s ? s->values[NS_CSS_MARGIN_BOTTOM] : NULL);

    double final_x, final_y;
    if (!l_auto && !r_auto && (ml_auto || mr_auto)) {
        double remaining = cb_w - left - right - box_outer_w;
        double ml;
        if (ml_auto && mr_auto)
            ml = remaining > 0 ? remaining / 2 : 0;
        else if (ml_auto)
            ml = remaining;
        else
            ml = 0;
        final_x = cb_inner_x + left + ml;
    } else if (!l_auto && !r_auto) {
        double remaining = cb_w - left - right - box_outer_w;
        final_x = cb_inner_x + left +
            grid_static_align_offset(abs_self_alignment(abox, NS_CSS_JUSTIFY_SELF),
                                     remaining, FALSE);
    } else if (!l_auto) {
        final_x = cb_inner_x + left;
    } else if (!r_auto) {
        final_x = cb_inner_x + cb_w - right - box_outer_w;
    } else {
        final_x = abox->x;
    }
    if (!t_auto && !b_auto && (mt_auto || mb_auto)) {
        double remaining = cb_h - top - bottom - box_outer_h;
        double mt;
        if (mt_auto && mb_auto)
            mt = remaining / 2;
        else if (mt_auto)
            mt = remaining;
        else
            mt = 0;
        final_y = cb_inner_y + top + mt;
    } else if (!t_auto && !b_auto) {
        double remaining = cb_h - top - bottom - box_outer_h;
        final_y = cb_inner_y + top +
            grid_static_align_offset(abs_self_alignment(abox, NS_CSS_ALIGN_SELF),
                                     remaining, FALSE);
    } else if (!t_auto) {
        final_y = cb_inner_y + top;
    } else if (!b_auto) {
        final_y = cb_inner_y + cb_h - bottom - box_outer_h;
    } else {
        final_y = abox->y;
    }

    double dx = final_x - abox->x;
    double dy = final_y - abox->y;
    if (dx == 0 && dy == 0) return;
    shift_box_tree(abox, dx, dy);
}

static gboolean
box_has_transform_style(const ns_box *b)
{
    const ns_style *s = b ? b->style : NULL;
    if (!s) return FALSE;
    const ns_css_value *tv = s->values[NS_CSS_TRANSFORM];
    if (tv && tv->kind == NS_CSS_V_TRANSFORM && tv->u.transform.n_ops > 0)
        return TRUE;
    return s->values[NS_CSS_TRANSLATE] || s->values[NS_CSS_ROTATE] ||
           s->values[NS_CSS_SCALE] || s->values[NS_CSS_PERSPECTIVE];
}

static gboolean
box_covers_viewport(const ns_box *b)
{
    double x = b->x + b->margin.left;
    double y = b->y + b->margin.top;
    double w = b->content_width + b->padding.left + b->padding.right +
               b->border.left + b->border.right;
    double h = b->content_height + b->padding.top + b->padding.bottom +
               b->border.top + b->border.bottom;
    return x <= 0.5 && y <= 0.5 &&
           x + w >= ns_css_viewport_w() - 0.5 &&
           y + h >= ns_css_viewport_h() - 0.5;
}

static gboolean
box_can_host_fixed(const ns_box *anc)
{
    for (const ns_box *b = anc; b && b->parent; b = b->parent) {
        if (box_has_transform_style(b)) return FALSE;
        if (box_clips_children(b) && !box_covers_viewport(b)) return FALSE;
    }
    return TRUE;
}

static gboolean
box_hit_untransform_point(const ns_box *b, double *x, double *y)
{
    const ns_style *s = b->style;
    if (!s) return TRUE;
    if (!(s->values[NS_CSS_TRANSFORM] || s->values[NS_CSS_TRANSLATE] ||
          s->values[NS_CSS_ROTATE] || s->values[NS_CSS_SCALE]))
        return TRUE;
    ns_css_transform eff;
    eff.n_ops = 0;
    ns_css_style_effective_transform(s, NULL, &eff);
    if (eff.n_ops == 0) return TRUE;
    double bx = b->x + b->margin.left;
    double by = b->y + b->margin.top;
    double bw = b->content_width + b->padding.left + b->padding.right +
                b->border.left + b->border.right;
    double bh = b->content_height + b->padding.top + b->padding.bottom +
                b->border.top + b->border.bottom;
    double ox = bx + bw / 2.0;
    double oy = by + bh / 2.0;
    const ns_css_value *origin = s->values[NS_CSS_TRANSFORM_ORIGIN];
    if (origin && origin->kind == NS_CSS_V_TRANSFORM &&
        origin->u.transform.n_ops > 0) {
        const ns_css_transform_op *o = &origin->u.transform.ops[0];
        ox = bx + (o->a_is_percent ? o->a / 100.0 * bw : o->a);
        oy = by + (o->b_is_percent ? o->b / 100.0 * bh : o->b);
    }
    ns_mat4 m;
    ns_css_transform_to_mat4(&eff, bw, bh, &m);
    if (!ns_mat4_is_affine2d(&m)) return TRUE;
    cairo_matrix_t cm;
    cairo_matrix_init(&cm, m.m[0], m.m[4], m.m[1], m.m[5], m.m[3], m.m[7]);
    if (cairo_matrix_invert(&cm) != CAIRO_STATUS_SUCCESS) return FALSE;
    double px = *x - ox, py = *y - oy;
    double qx = cm.xx * px + cm.xy * py + cm.x0;
    double qy = cm.yx * px + cm.yy * py + cm.y0;
    *x = qx + ox;
    *y = qy + oy;
    return TRUE;
}

static double
abs_height_limit(const ns_box *abox, const ns_css_value *v, double width_basis,
                 double cb_h, double inset_h, double sizing_extras)
{
    double limit = resolve_height_with_basis(v, width_basis, cb_h, -1);
    if (limit < 0 && size_keyword_is_intrinsic(v) &&
        abox->measured_content_height >= 0)
        limit = abox->measured_content_height + sizing_extras;
    if (limit < 0 && height_keyword_stretches(v) && inset_h >= 0)
        limit = inset_h + sizing_extras;
    return limit;
}

static double
abs_height_within_limits(const ns_box *abox, double h, double width_basis,
                         double cb_h, double inset_h)
{
    const ns_style *s = abox->style;
    if (!s) return h;
    double sizing_extras = flex_box_is_border_box(abox)
        ? abox->padding.top + abox->padding.bottom +
          abox->border.top + abox->border.bottom
        : 0;
    double mx = abs_height_limit(abox, s->values[NS_CSS_MAX_HEIGHT],
                                 width_basis, cb_h, inset_h, sizing_extras);
    if (mx >= 0 && h > mx - sizing_extras)
        h = mx > sizing_extras ? mx - sizing_extras : 0;
    double mn = abs_height_limit(abox, s->values[NS_CSS_MIN_HEIGHT],
                                 width_basis, cb_h, inset_h, sizing_extras);
    if (mn >= 0 && h < mn - sizing_extras) h = mn - sizing_extras;
    return h;
}

static void
process_absolute_boxes(ns_box *root, GHashTable *styles, double viewport_width)
{
    if (!g_abs_pending || g_abs_pending->len == 0) return;
    {
        const ns_node *order_root =
            g_array_index(g_abs_pending, ns_abs_entry, 0).dom;
        while (order_root->parent) order_root = order_root->parent;
        node_order_build(order_root);
    }
    GHashTable *box_map = g_hash_table_new(g_direct_hash, g_direct_equal);
    abs_box_map_build(box_map, root);
    guint batch_len = g_abs_pending->len;
    double *batch_y = g_new0(double, batch_len);
    gboolean *batch_resolved = g_new0(gboolean, batch_len);
    static_abs_y_precompute(root, box_map, styles, batch_y, batch_resolved);
    for (guint i = 0; i < g_abs_pending->len; i++) {
        ns_abs_entry e = g_array_index(g_abs_pending, ns_abs_entry, i);
        const ns_node *cb_dom = positioned_entry_cb_dom(&e, styles);
        ns_box *cb = cb_dom ? g_hash_table_lookup(box_map, cb_dom) : root;
        if (!cb) {
            cb = root;
            if (e.fixed) cb_dom = NULL;
        }

        ns_box *paint_parent = cb;
        if (e.fixed && !cb_dom) {
            const ns_node *anc_dom = abs_entry_cb_dom(&e, styles);
            ns_box *anc = anc_dom ? g_hash_table_lookup(box_map, anc_dom) : NULL;
            if (anc && box_can_host_fixed(anc))
                paint_parent = anc;
        }


        int pp_depth = 0;
        for (const ns_box *p = paint_parent; p; p = p->parent)
            if (++pp_depth >= NS_LAYOUT_MAX_DEPTH) break;
        if (pp_depth >= NS_LAYOUT_MAX_DEPTH) continue;

        ns_box *abox;
        if (e.pseudo) {
            abox = box_new(NS_BOX_BLOCK);
            abox->style = e.pseudo;
            collect_box_bg_image(abox, e.pseudo);
            ns_box *gen = build_pseudo_inline_for(e.pseudo, e.dom);
            if (gen) box_append_child(abox, gen);
        } else {
            g_abs_force_build = TRUE;
            abox = build_block(e.dom, styles);
            g_abs_force_build = FALSE;
        }
        if (!abox) continue;

        box_append_child(paint_parent, abox);
        abs_box_map_build(box_map, abox);
        gboolean cb_is_icb = (cb_dom == NULL);
        double cb_pad_w = cb->content_width + cb->padding.left + cb->padding.right;
        double cb_pad_h = cb->content_height + cb->padding.top + cb->padding.bottom;
        double avail = cb_is_icb ? viewport_width
                                 : (cb_pad_w > 0 ? cb_pad_w : viewport_width);
        double cb_h = cb_is_icb ? ns_css_viewport_h() : cb_pad_h;
        const ns_style *cs = cb->style;
        double grid_x = 0, grid_y = 0, grid_w = 0, grid_h = 0;
        gboolean grid_cb = !cb_is_icb && !e.pseudo &&
            grid_abs_containing_block(cb, abox->style,
                                      &grid_x, &grid_y, &grid_w, &grid_h);
        if (grid_cb) {
            avail = grid_w;
            cb_h = grid_h;
        }
        ns_abs_static *st = (g_abs_static && !e.pseudo)
            ? g_hash_table_lookup(g_abs_static, e.dom) : NULL;
        const ns_box *flex_parent = e.pseudo ? NULL
            : abs_flex_parent_box(e.dom, box_map);
        gboolean static_rtl = FALSE;
        double static_right = 0;
        if (st && st->run) {
            abox->x = st->run->x + st->rel_x;
            abox->y = st->run->y + st->rel_y;
        } else {
            double static_y = cb->y + cb->margin.top + cb->border.top + cb->padding.top;
            if (i < batch_len && batch_resolved[i])
                static_y = batch_y[i];
            else
                static_abs_y_walk(cb, e.dom, &static_y);
            double base_x = cb->x + cb->margin.left + cb->border.left +
                            cb->padding.left;
            abox->x = static_abs_x_from_ancestors(cb, e.dom, box_map, base_x,
                                                   &static_rtl, &static_right);
            abox->y = static_y;
        }
        const ns_css_value *awv = abox->style
            ? abox->style->values[NS_CSS_WIDTH] : NULL;
        gboolean has_explicit_width = awv &&
            (awv->kind == NS_CSS_V_LENGTH || awv->kind == NS_CSS_V_CALC);
        const ns_css_value *alv = abox->style ? abox->style->values[NS_CSS_LEFT]   : NULL;
        const ns_css_value *arv = abox->style ? abox->style->values[NS_CSS_RIGHT]  : NULL;
        const ns_css_value *atv = abox->style ? abox->style->values[NS_CSS_TOP]    : NULL;
        const ns_css_value *abv = abox->style ? abox->style->values[NS_CSS_BOTTOM] : NULL;
        gboolean l_set = alv && !length_is_auto(alv) &&
            (alv->kind == NS_CSS_V_LENGTH || alv->kind == NS_CSS_V_CALC);
        gboolean r_set = arv && !length_is_auto(arv) &&
            (arv->kind == NS_CSS_V_LENGTH || arv->kind == NS_CSS_V_CALC);
        gboolean js_stretch = strcmp(abs_self_alignment(abox, NS_CSS_JUSTIFY_SELF),
                                     "stretch") == 0;
        gboolean as_stretch = strcmp(abs_self_alignment(abox, NS_CSS_ALIGN_SELF),
                                     "stretch") == 0;
        gboolean stretch_w = !has_explicit_width &&
            ((l_set && r_set && js_stretch) || height_keyword_stretches(awv));
        double layout_w = avail;
        double inset_w = avail;
        if (l_set && r_set) {
            inset_w = avail - length_resolve(alv, avail, 0) - length_resolve(arv, avail, 0);
            if (inset_w < 0) inset_w = 0;
        }
        if (stretch_w) {
            double l = l_set ? length_resolve(alv, avail, 0) : 0;
            double r = r_set ? length_resolve(arv, avail, 0) : 0;
            if (!l_set && !r_set && !grid_cb) {
                double origin = cb->x + cb->margin.left + cb->border.left;
                if (abox->x > origin) l = abox->x - origin;
            }
            layout_w = avail - l - r
                     - abox->margin.left - abox->margin.right
                     - abox->border.left - abox->border.right
                     - abox->padding.left - abox->padding.right;
            if (layout_w < 0) layout_w = 0;
        }
        const ns_css_value *ahv = abox->style
            ? abox->style->values[NS_CSS_HEIGHT] : NULL;
        gboolean has_explicit_height = ahv &&
            (ahv->kind == NS_CSS_V_LENGTH || ahv->kind == NS_CSS_V_CALC);
        if (has_explicit_height && value_is_percent(ahv) &&
            cb_h > 0) {
            double pre_h = resolve_height_with_basis(ahv, avail, cb_h, -1);
            if (pre_h > 0) {
                abox->content_height = pre_h;
                abox->definite_height = pre_h;
            }
        }
        layout_box(abox, layout_w, cs);
        if (!stretch_w && !has_explicit_width && abox->kind == NS_BOX_BLOCK) {
            ns_edges fm = {0}, fp = {0}, fb = {0};
            edges_from_style(abox->style, avail, &fm, &fp, &fb);
            double box_extras = fp.left + fp.right + fb.left + fb.right;
            double outer_extras = box_extras + fm.left + fm.right;
            double fit = measure_natural_width(abox, cs);
            if (!(fit > 0)) fit = estimate_natural_width(abox, inset_w) - box_extras;
            double floor_w = measure_min_width(abox, cs);
            if (fit < floor_w) fit = floor_w;
            fit += outer_extras;
            floor_w += outer_extras;
            double fit_w = fit < inset_w ? fit
                         : floor_w > inset_w ? floor_w : inset_w;
            if (fit_w != layout_w) {
                layout_w = fit_w;
                layout_box(abox, layout_w, cs);
            }
        }
        gboolean t_set = atv && !length_is_auto(atv) &&
            (atv->kind == NS_CSS_V_LENGTH || atv->kind == NS_CSS_V_CALC);
        gboolean b_set = abv && !length_is_auto(abv) &&
            (abv->kind == NS_CSS_V_LENGTH || abv->kind == NS_CSS_V_CALC);
        double inset_h = -1;
        if (cb_h > 0) {
            inset_h = cb_h
                - (t_set ? length_resolve(atv, cb_h, 0) : 0)
                - (b_set ? length_resolve(abv, cb_h, 0) : 0)
                - abox->margin.top - abox->margin.bottom
                - abox->border.top - abox->border.bottom
                - abox->padding.top - abox->padding.bottom;
            if (inset_h < 0) inset_h = 0;
        }
        if (has_explicit_height) {
            double explicit_h = resolve_height_with_basis(ahv, avail,
                                                          cb_h,
                                                          -1);
            if (explicit_h >= 0) {
                if (flex_box_is_border_box(abox)) {
                    explicit_h -= abox->padding.top + abox->padding.bottom +
                                  abox->border.top + abox->border.bottom;
                    if (explicit_h < 0) explicit_h = 0;
                }
                abox->content_height = abs_height_within_limits(
                    abox, explicit_h, avail, cb_h, inset_h);
            }
        }
        gboolean intrinsic_height = ahv && ahv->kind == NS_CSS_V_KEYWORD &&
            ahv->u.keyword &&
            (strcmp(ahv->u.keyword, "fit-content") == 0 ||
             strcmp(ahv->u.keyword, "min-content") == 0 ||
             strcmp(ahv->u.keyword, "max-content") == 0);
        double stretched_h = -1;
        if (!has_explicit_height && !intrinsic_height &&
            t_set && b_set && cb_h > 0 && as_stretch) {
            double h = abs_height_within_limits(abox, inset_h, avail, cb_h,
                                                inset_h);
            abox->content_height = h;
            stretched_h = h;
        }
        if (stretched_h >= 0) {
            layout_box(abox, layout_w, cs);
            abox->content_height = stretched_h;
        }
        gboolean static_x = (!alv || length_is_auto(alv)) &&
                            (!arv || length_is_auto(arv));
        gboolean static_y = (!atv || length_is_auto(atv)) &&
                            (!abv || length_is_auto(abv));
        if (static_x && static_rtl && !(st && st->run))
            abox->x = static_right - (abox->margin.left + abox->border.left +
                                      abox->padding.left + abox->content_width +
                                      abox->padding.right + abox->border.right +
                                      abox->margin.right);
        double flex_x = 0, flex_y = 0;
        if (flex_parent && (static_x || static_y) &&
            flex_static_position(abox, flex_parent, &flex_x, &flex_y)) {
            if (static_x) abox->x = flex_x;
            if (static_y) abox->y = flex_y;
        }
        if (grid_cb && flex_parent == cb && (static_x || static_y))
            grid_static_position(abox, cb, grid_x, grid_y, grid_w, grid_h,
                                 static_x, static_y);
        apply_position_offsets(abox, avail, cb_h);
        if (grid_cb)
            position_absolute_box_in(abox, grid_x, grid_y, grid_w, grid_h);
        else
            position_absolute_box(abox, cb, cb_is_icb);
    }
    g_free(batch_y);
    g_free(batch_resolved);
    g_hash_table_destroy(box_map);
    g_array_set_size(g_abs_pending, 0);
    g_clear_pointer(&g_node_order, g_hash_table_destroy);
}

static gboolean
box_paint_unbounded(const ns_box *b)
{
    const ns_style *s = b->style;
    if (!s) return FALSE;
    const ns_css_value *tv = s->values[NS_CSS_TRANSFORM];
    if (tv && tv->kind == NS_CSS_V_TRANSFORM && tv->u.transform.n_ops > 0)
        return TRUE;
    if (s->values[NS_CSS_ANIMATION] || s->values[NS_CSS_TRANSITION])
        return TRUE;
    const ns_css_value *pv = s->values[NS_CSS_POSITION];
    if (pv && pv->kind == NS_CSS_V_KEYWORD && pv->u.keyword &&
        (strcmp(pv->u.keyword, "fixed") == 0 ||
         strcmp(pv->u.keyword, "sticky") == 0))
        return TRUE;
    return FALSE;
}

static void
compute_paint_bounds(ns_box *b)
{
    double top = b->y;
    double bottom = b->y + b->margin.top + b->border.top + b->padding.top +
                    b->content_height +
                    b->padding.bottom + b->border.bottom + b->margin.bottom;
    if (box_paint_unbounded(b)) {
        top = -G_MAXDOUBLE;
        bottom = G_MAXDOUBLE;
    }
    for (ns_box *c = b->first_child; c; c = c->next_sibling) {
        compute_paint_bounds(c);
        if (c->paint_top < top) top = c->paint_top;
        if (c->paint_bottom > bottom) bottom = c->paint_bottom;
    }
    if (b->inline_atomics) {
        for (guint i = 0; i < b->inline_atomics->len; i++) {
            const ns_inline_atomic *atomic =
                &g_array_index(b->inline_atomics, ns_inline_atomic, i);
            ns_box *ab = atomic->box;
            if (!ab) continue;
            compute_paint_bounds(ab);
            double dy = b->y + atomic->owner_offset_y - ab->y;
            if (ab->paint_top + dy < top) top = ab->paint_top + dy;
            if (ab->paint_bottom + dy > bottom) bottom = ab->paint_bottom + dy;
        }
    }
    b->paint_top = top;
    b->paint_bottom = bottom;
}

static ns_box *
ns_layout_build_(const ns_node *doc, GHashTable *styles, double viewport_width)
{
    g_abs_pending = g_array_new(FALSE, FALSE, sizeof(ns_abs_entry));
    g_abs_seen = g_hash_table_new(g_direct_hash, g_direct_equal);
    g_abs_ph_set = g_hash_table_new(g_direct_hash, g_direct_equal);
    g_abs_static = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                         NULL, g_free);
    g_contains_block_media_cache = g_hash_table_new(g_direct_hash, g_direct_equal);
    g_cq_seen_container = FALSE;
    ns_css_set_container_dims(0, 0);
    ns_box *root = build_block(doc, styles);
    if (!root) {
        g_array_free(g_abs_pending, TRUE);
        g_abs_pending = NULL;
        g_clear_pointer(&g_abs_seen, g_hash_table_destroy);
        g_clear_pointer(&g_abs_ph_set, g_hash_table_destroy);
        g_clear_pointer(&g_abs_static, g_hash_table_destroy);
        g_hash_table_destroy(g_contains_block_media_cache);
        g_contains_block_media_cache = NULL;
        return NULL;
    }
    root->x = 0;
    root->y = 0;

    layout_block(root, viewport_width, NULL);
    apply_position_offsets(root, viewport_width, root->content_height);
    process_absolute_boxes(root, styles, viewport_width);
    ns_paint_sync_inline_atomic_offsets(root);
    compute_paint_bounds(root);

    g_array_free(g_abs_pending, TRUE);
    g_abs_pending = NULL;
    g_clear_pointer(&g_abs_seen, g_hash_table_destroy);
    g_clear_pointer(&g_abs_ph_set, g_hash_table_destroy);
    g_clear_pointer(&g_abs_static, g_hash_table_destroy);
    g_hash_table_destroy(g_contains_block_media_cache);
    g_contains_block_media_cache = NULL;
    return root;
}

const char *
ns_box_kind_name(ns_box_kind k)
{
    switch (k) {
    case NS_BOX_BLOCK:      return "block";
    case NS_BOX_INLINE:     return "inline";
    case NS_BOX_TEXT:       return "text";
    case NS_BOX_IMAGE:      return "image";
    case NS_BOX_TABLE:      return "table";
    case NS_BOX_TABLE_CAPTION: return "caption";
    case NS_BOX_TABLE_ROW:  return "row";
    case NS_BOX_TABLE_CELL: return "cell";
    case NS_BOX_VIDEO:      return "video";
    case NS_BOX_MATH:       return "math";
    case NS_BOX_SVG:        return "svg";
    }
    return "?";
}

static void
collect_images_walk(const ns_box *b, GPtrArray *out)
{
    if (!b) return;
    if (b->kind == NS_BOX_IMAGE) g_ptr_array_add(out, (gpointer)b);
    if (b->media && (b->media->bg_image_src || b->media->marker_image_src ||
                     b->media->border_image_src))
        g_ptr_array_add(out, (gpointer)b);
    for (const ns_box *c = b->first_child; c; c = c->next_sibling)
        collect_images_walk(c, out);
    if (b->inline_atomics)
        for (guint i = 0; i < b->inline_atomics->len; i++)
            collect_images_walk(
                g_array_index(b->inline_atomics, ns_inline_atomic, i).box, out);
}

static void
collect_videos_walk(const ns_box *b, GPtrArray *out)
{
    if (!b) return;
    if (b->kind == NS_BOX_VIDEO) g_ptr_array_add(out, (gpointer)b);
    for (const ns_box *c = b->first_child; c; c = c->next_sibling)
        collect_videos_walk(c, out);
    if (b->inline_atomics)
        for (guint i = 0; i < b->inline_atomics->len; i++)
            collect_videos_walk(
                g_array_index(b->inline_atomics, ns_inline_atomic, i).box, out);
}

void
ns_layout_collect_videos(const ns_box *root, GPtrArray *out_boxes)
{
    collect_videos_walk(root, out_boxes);
}

void
ns_layout_collect_images(const ns_box *root, GPtrArray *out_boxes)
{
    collect_images_walk(root, out_boxes);
}

gboolean
ns_box_tree_has_sticky(const ns_box *root)
{
    if (!root) return FALSE;
    if (root->style) {
        const ns_css_value *v = root->style->values[NS_CSS_POSITION];
        if (v && v->kind == NS_CSS_V_KEYWORD && v->u.keyword &&
            strcmp(v->u.keyword, "sticky") == 0)
            return TRUE;
    }
    for (const ns_box *c = root->first_child; c; c = c->next_sibling)
        if (ns_box_tree_has_sticky(c)) return TRUE;
    return FALSE;
}

static guint
count_matches_in_text(const char *text, const char *needle,
                      gboolean case_sensitive)
{
    if (!text || !needle || !*needle) return 0;
    gsize needle_len = strlen(needle);
    gsize text_len = strlen(text);
    guint hits = 0;
    for (gsize i = 0; i + needle_len <= text_len; ) {
        gboolean match = case_sensitive
            ? (strncmp(text + i, needle, needle_len) == 0)
            : (g_ascii_strncasecmp(text + i, needle, needle_len) == 0);
        if (match) {
            hits++;
            i += needle_len;
        } else {
            i++;
        }
    }
    return hits;
}

guint
ns_box_count_matches(const ns_box *root, const char *needle,
                     gboolean case_sensitive)
{
    if (!root || !needle || !*needle) return 0;
    guint sum = 0;
    if (root->kind == NS_BOX_INLINE && root->text)
        sum += count_matches_in_text(root->text, needle, case_sensitive);
    for (const ns_box *c = root->first_child; c; c = c->next_sibling)
        sum += ns_box_count_matches(c, needle, case_sensitive);
    return sum;
}

const ns_box *
ns_box_first_match_below(const ns_box *root, const char *needle,
                         double y_threshold, gboolean case_sensitive)
{
    if (!root || !needle || !*needle) return NULL;
    if (root->kind == NS_BOX_INLINE && root->text && root->y > y_threshold) {
        if (count_matches_in_text(root->text, needle, case_sensitive) > 0)
            return root;
    }
    for (const ns_box *c = root->first_child; c; c = c->next_sibling) {
        const ns_box *m = ns_box_first_match_below(c, needle, y_threshold,
                                                   case_sensitive);
        if (m) return m;
    }
    return NULL;
}

const ns_box *
ns_box_first_match_above(const ns_box *root, const char *needle,
                         double y_threshold, gboolean case_sensitive)
{
    if (!root || !needle || !*needle) return NULL;
    const ns_box *best = NULL;
    if (root->kind == NS_BOX_INLINE && root->text && root->y < y_threshold) {
        if (count_matches_in_text(root->text, needle, case_sensitive) > 0)
            best = root;
    }
    for (const ns_box *c = root->first_child; c; c = c->next_sibling) {
        const ns_box *m = ns_box_first_match_above(c, needle, y_threshold,
                                                   case_sensitive);
        if (m && (!best || m->y > best->y))
            best = m;
    }
    return best;
}

static gboolean
match_ordinal_walk(const ns_box *root, const char *needle,
                   const ns_box *target, gboolean case_sensitive,
                   guint *acc)
{
    if (!root) return FALSE;
    if (root->kind == NS_BOX_INLINE && root->text) {
        guint here = count_matches_in_text(root->text, needle, case_sensitive);
        if (root == target) {
            if (here > 0) { *acc += 1; return TRUE; }
            return FALSE;
        }
        *acc += here;
    }
    for (const ns_box *c = root->first_child; c; c = c->next_sibling)
        if (match_ordinal_walk(c, needle, target, case_sensitive, acc))
            return TRUE;
    return FALSE;
}

guint
ns_box_match_ordinal(const ns_box *root, const char *needle,
                     const ns_box *target, gboolean case_sensitive)
{
    if (!root || !needle || !*needle || !target) return 0;
    guint acc = 0;
    if (match_ordinal_walk(root, needle, target, case_sensitive, &acc))
        return acc;
    return 0;
}

const ns_node *
ns_box_hit_form_dom(const ns_box *root, double x, double y)
{
    return ns_form_hit_walk(root, x, y, NULL);
}

ns_box *
ns_box_hit_scrollable(ns_box *root, double x, double y)
{
    if (!root) return NULL;
    hit_enter_box(root, &x, &y);
    if (!box_hit_untransform_point(root, &x, &y)) return NULL;
    if (root->paint_bottom > root->paint_top &&
        (y < root->paint_top - 1.0 || y > root->paint_bottom + 1.0))
        return NULL;
    gboolean clipped = box_clips_children(root);
    if (clipped && !box_padding_contains(root, x, y))
        return NULL;
    double cx = x + root->scroll_x;
    double cy = y + root->scroll_y;
    for (ns_box *c = root->first_child; c; c = c->next_sibling) {
        ns_box *m = ns_box_hit_scrollable(c, cx, cy);
        if (m) return m;
    }
    if (root->inline_atomics)
        for (guint i = 0; i < root->inline_atomics->len; i++) {
            const ns_inline_atomic *atomic =
                &g_array_index(root->inline_atomics, ns_inline_atomic, i);
            if (!atomic->box) continue;
            double ax, ay;
            inline_atomic_hit_point(root, atomic, cx, cy, &ax, &ay);
            ns_box *m = ns_box_hit_scrollable(atomic->box, ax, ay);
            if (m) return m;
        }
    if (root->scrolls && (root->scroll_max_x > 0 || root->scroll_max_y > 0) &&
        box_padding_contains(root, x, y))
        return root;
    return NULL;
}

typedef struct {
    double   best;
    double   prev;
    gboolean any;
} snap_axis;

typedef struct {
    double left, top, right, bottom;
    double cur_x, cur_y;
    double prev_x, prev_y;
    double max_x, max_y;
} snap_port;

static double
snap_length(const ns_style *s, ns_css_prop p, double basis)
{
    const ns_css_value *v = s ? s->values[p] : NULL;
    if (!v || length_is_auto(v)) return 0;
    double px = length_resolve(v, basis, 0);
    return isfinite(px) ? px : 0;
}

static gboolean
snap_better(const snap_axis *axis, double candidate, double current)
{
    double travel = current - axis->prev;
    gboolean ahead = travel > 0.5 ? candidate > axis->prev + 0.5
                   : travel < -0.5 ? candidate < axis->prev - 0.5
                   : TRUE;
    gboolean best_ahead = travel > 0.5 ? axis->best > axis->prev + 0.5
                        : travel < -0.5 ? axis->best < axis->prev - 0.5
                        : TRUE;
    if (!axis->any) return TRUE;
    if (ahead != best_ahead) return ahead;
    return fabs(candidate - current) < fabs(axis->best - current);
}

static void
snap_consider(snap_axis *axis, double candidate, double current, double max)
{
    if (candidate < 0) candidate = 0;
    if (candidate > max) candidate = max;
    if (snap_better(axis, candidate, current)) {
        axis->best = candidate;
        axis->any = TRUE;
    }
}

static void
snap_collect(ns_box *b, const snap_port *port, snap_axis *ax, snap_axis *ay,
             gboolean want_x, gboolean want_y)
{
    double sp_top = port->top, sp_bottom = port->bottom;
    double sp_left = port->left, sp_right = port->right;
    for (ns_box *c = b->first_child; c; c = c->next_sibling) {
        const char *align = c->style
            ? ns_style_keyword(c->style, NS_CSS_SCROLL_SNAP_ALIGN) : NULL;
        if (align && strcmp(align, "none none") != 0) {
            char block[16] = {0}, inline_kw[16] = {0};
            sscanf(align, "%15s %15s", block, inline_kw);
            double port_w = sp_right - sp_left;
            double port_h = sp_bottom - sp_top;
            double top = c->y + c->margin.top -
                snap_length(c->style, NS_CSS_SCROLL_MARGIN_TOP, port_h);
            double bottom = c->y + c->margin.top + c->content_height +
                c->padding.top + c->padding.bottom +
                c->border.top + c->border.bottom +
                snap_length(c->style, NS_CSS_SCROLL_MARGIN_BOTTOM, port_h);
            double left = c->x + c->margin.left -
                snap_length(c->style, NS_CSS_SCROLL_MARGIN_LEFT, port_w);
            double right = c->x + c->margin.left + c->content_width +
                c->padding.left + c->padding.right +
                c->border.left + c->border.right +
                snap_length(c->style, NS_CSS_SCROLL_MARGIN_RIGHT, port_w);
            if (want_y && strcmp(block, "none") != 0)
                snap_consider(ay,
                    strcmp(block, "end") == 0 ? bottom - sp_bottom
                    : strcmp(block, "center") == 0
                        ? (top + bottom - sp_top - sp_bottom) / 2
                        : top - sp_top,
                    port->cur_y, port->max_y);
            if (want_x && strcmp(inline_kw, "none") != 0)
                snap_consider(ax,
                    strcmp(inline_kw, "end") == 0 ? right - sp_right
                    : strcmp(inline_kw, "center") == 0
                        ? (left + right - sp_left - sp_right) / 2
                        : left - sp_left,
                    port->cur_x, port->max_x);
        }
        if (c->scrolls) continue;
        snap_collect(c, port, ax, ay, want_x, want_y);
    }
}

static gboolean
snap_solve(ns_box *root, const char *type, const snap_port *port,
           double *out_x, double *out_y)
{
    char axis_kw[16] = {0}, strictness[16] = {0};
    sscanf(type, "%15s %15s", axis_kw, strictness);
    gboolean want_x = strcmp(axis_kw, "x") == 0 ||
                      strcmp(axis_kw, "inline") == 0 ||
                      strcmp(axis_kw, "both") == 0;
    gboolean want_y = strcmp(axis_kw, "y") == 0 ||
                      strcmp(axis_kw, "block") == 0 ||
                      strcmp(axis_kw, "both") == 0;
    if (!want_x && !want_y) return FALSE;

    snap_axis ax = { .prev = port->prev_x }, ay = { .prev = port->prev_y };
    snap_collect(root, port, &ax, &ay, want_x, want_y);

    gboolean mandatory = strcmp(strictness, "mandatory") == 0;
    double port_w = port->right - port->left;
    double port_h = port->bottom - port->top;
    gboolean snapped = FALSE;
    if (ay.any && (mandatory || fabs(ay.best - port->cur_y) <= port_h / 2)) {
        *out_y = ay.best;
        snapped = TRUE;
    }
    if (ax.any && (mandatory || fabs(ax.best - port->cur_x) <= port_w / 2)) {
        *out_x = ax.best;
        snapped = TRUE;
    }
    return snapped;
}

void
ns_box_scroll_snap_from(ns_box *scroller, double prev_x, double prev_y)
{
    if (!scroller || !scroller->style) return;
    const char *type = ns_style_keyword(scroller->style,
                                        NS_CSS_SCROLL_SNAP_TYPE);
    if (!type || strcmp(type, "none") == 0) return;

    double port_top = scroller->y + scroller->margin.top + scroller->border.top;
    double port_left = scroller->x + scroller->margin.left +
                       scroller->border.left;
    double port_h = scroller->content_height + scroller->padding.top +
                    scroller->padding.bottom;
    double port_w = scroller->content_width + scroller->padding.left +
                    scroller->padding.right;
    const ns_style *s = scroller->style;
    snap_port port = {
        .top = port_top + snap_length(s, NS_CSS_SCROLL_PADDING_TOP, port_h),
        .bottom = port_top + port_h -
                  snap_length(s, NS_CSS_SCROLL_PADDING_BOTTOM, port_h),
        .left = port_left + snap_length(s, NS_CSS_SCROLL_PADDING_LEFT, port_w),
        .right = port_left + port_w -
                 snap_length(s, NS_CSS_SCROLL_PADDING_RIGHT, port_w),
        .cur_x = scroller->scroll_x, .cur_y = scroller->scroll_y,
        .prev_x = prev_x, .prev_y = prev_y,
        .max_x = scroller->scroll_max_x, .max_y = scroller->scroll_max_y,
    };

    double x = scroller->scroll_x, y = scroller->scroll_y;
    if (snap_solve(scroller, type, &port, &x, &y)) {
        scroller->scroll_x = x;
        scroller->scroll_y = y;
    }
}

gboolean
ns_box_scroll_snap_viewport(ns_box *root, const ns_style *s,
                            double viewport_w, double viewport_h,
                            double max_x, double max_y,
                            double prev_x, double prev_y,
                            double *x, double *y)
{
    if (!root || !s || !x || !y) return FALSE;
    if (!(viewport_w > 0) || !(viewport_h > 0)) return FALSE;
    const char *type = ns_style_keyword(s, NS_CSS_SCROLL_SNAP_TYPE);
    if (!type || strcmp(type, "none") == 0) return FALSE;

    snap_port port = {
        .top = snap_length(s, NS_CSS_SCROLL_PADDING_TOP, viewport_h),
        .bottom = viewport_h -
                  snap_length(s, NS_CSS_SCROLL_PADDING_BOTTOM, viewport_h),
        .left = snap_length(s, NS_CSS_SCROLL_PADDING_LEFT, viewport_w),
        .right = viewport_w -
                 snap_length(s, NS_CSS_SCROLL_PADDING_RIGHT, viewport_w),
        .cur_x = *x, .cur_y = *y,
        .prev_x = prev_x, .prev_y = prev_y,
        .max_x = max_x, .max_y = max_y,
    };
    return snap_solve(root, type, &port, x, y);
}

void
ns_box_scroll_snap(ns_box *scroller)
{
    if (scroller)
        ns_box_scroll_snap_from(scroller, scroller->scroll_x,
                                scroller->scroll_y);
}

ns_box *
ns_box_hit_scrollbar(ns_box *root, double x, double y, double *lx, double *ly)
{
    if (!root) return NULL;
    hit_enter_box(root, &x, &y);
    if (!box_hit_untransform_point(root, &x, &y)) return NULL;
    if (root->paint_bottom > root->paint_top &&
        (y < root->paint_top - 1.0 || y > root->paint_bottom + 1.0))
        return NULL;
    gboolean clipped = box_clips_children(root);
    if (clipped && !box_padding_contains(root, x, y))
        return NULL;
    double cx = x + root->scroll_x;
    double cy = y + root->scroll_y;
    for (ns_box *c = root->first_child; c; c = c->next_sibling) {
        ns_box *m = ns_box_hit_scrollbar(c, cx, cy, lx, ly);
        if (m) return m;
    }
    if (root->inline_atomics)
        for (guint i = 0; i < root->inline_atomics->len; i++) {
            const ns_inline_atomic *atomic =
                &g_array_index(root->inline_atomics, ns_inline_atomic, i);
            if (!atomic->box) continue;
            double ax, ay;
            inline_atomic_hit_point(root, atomic, cx, cy, &ax, &ay);
            ns_box *m = ns_box_hit_scrollbar(atomic->box, ax, ay, lx, ly);
            if (m) return m;
        }
    if (root->scrolls && (root->scroll_max_x > 0 || root->scroll_max_y > 0) &&
        box_padding_contains(root, x, y)) {
        if (lx) *lx = x;
        if (ly) *ly = y;
        return root;
    }
    return NULL;
}

typedef struct {
    const ns_box *box;
    double x, y;
    guint order;
    int   z;
} hit_deferred;

static __thread GArray       *g_hit_deferred;
static __thread int           g_hit_defer_depth;
static __thread const ns_box *g_hit_flush_box;
static __thread double        g_hit_local_x, g_hit_local_y;
static double g_hit_vp_x, g_hit_vp_y;

void
ns_box_set_hit_viewport(double scroll_x, double scroll_y)
{
    g_hit_vp_x = isfinite(scroll_x) ? scroll_x : 0;
    g_hit_vp_y = isfinite(scroll_y) ? scroll_y : 0;
}

gboolean
ns_box_is_fixed(const ns_box *b)
{
    if (!b || !b->style ||
        !keyword_is(b->style->values[NS_CSS_POSITION], "fixed"))
        return FALSE;
    for (const ns_box *p = b->parent; p; p = p->parent)
        if (style_creates_fixed_cb(p->style)) return FALSE;
    return TRUE;
}

static void
sticky_inset(const ns_css_value *v, double basis, gboolean *set, double *out)
{
    *set = FALSE;
    *out = 0;
    if (!v || length_is_auto(v)) return;
    if (v->kind != NS_CSS_V_LENGTH && v->kind != NS_CSS_V_CALC) return;
    *out = length_resolve(v, basis, 0);
    *set = isfinite(*out);
}

void
ns_box_sticky_offset_in(const ns_box *b, double sp_x0, double sp_y0,
                        double sp_x1, double sp_y1,
                        double *out_dx, double *out_dy)
{
    *out_dx = 0;
    *out_dy = 0;
    if (!b || !b->style ||
        !keyword_is(b->style->values[NS_CSS_POSITION], "sticky"))
        return;
    double box_top = b->y;
    double box_h = b->margin.top + b->border.top + b->padding.top +
                   b->content_height +
                   b->padding.bottom + b->border.bottom + b->margin.bottom;
    double box_left = b->x;
    double box_w = b->margin.left + b->border.left + b->padding.left +
                   b->content_width +
                   b->padding.right + b->border.right + b->margin.right;
    double cb_top, cb_bot, cb_left, cb_right;
    const ns_box *p = b->parent;
    if (p) {
        cb_left = p->x + p->margin.left + p->border.left + p->padding.left;
        cb_top  = p->y + p->margin.top  + p->border.top  + p->padding.top;
        cb_right = cb_left + p->content_width;
        cb_bot   = cb_top  + p->content_height;
    } else {
        cb_left = sp_x0; cb_top = 0;
        cb_right = sp_x1; cb_bot = G_MAXDOUBLE / 2;
    }
    double sp_w = sp_x1 - sp_x0, sp_h = sp_y1 - sp_y0;
    gboolean has_top, has_bot, has_left, has_right;
    double tval, bval, lval, rval;
    sticky_inset(b->style->values[NS_CSS_TOP],    sp_h, &has_top,   &tval);
    sticky_inset(b->style->values[NS_CSS_BOTTOM], sp_h, &has_bot,   &bval);
    sticky_inset(b->style->values[NS_CSS_LEFT],   sp_w, &has_left,  &lval);
    sticky_inset(b->style->values[NS_CSS_RIGHT],  sp_w, &has_right, &rval);
    if (has_top) {
        double target = sp_y0 + tval;
        if (box_top < target) {
            double want = target - box_top;
            double cap  = cb_bot - (box_top + box_h);
            if (cap < 0) cap = 0;
            *out_dy = want < cap ? want : cap;
        }
    }
    if (has_bot && *out_dy == 0) {
        double target = sp_y1 - bval;
        double box_bot = box_top + box_h;
        if (box_bot > target) {
            double want = target - box_bot;
            double cap  = cb_top - box_top;
            if (cap > 0) cap = 0;
            *out_dy = want > cap ? want : cap;
        }
    }
    if (has_left) {
        double target = sp_x0 + lval;
        if (box_left < target) {
            double want = target - box_left;
            double cap  = cb_right - (box_left + box_w);
            if (cap < 0) cap = 0;
            *out_dx = want < cap ? want : cap;
        }
    }
    if (has_right && *out_dx == 0) {
        double target = sp_x1 - rval;
        double box_right = box_left + box_w;
        if (box_right > target) {
            double want = target - box_right;
            double cap  = cb_left - box_left;
            if (cap > 0) cap = 0;
            *out_dx = want > cap ? want : cap;
        }
    }
    if (!isfinite(*out_dx)) *out_dx = 0;
    if (!isfinite(*out_dy)) *out_dy = 0;
}

static gboolean
box_scrollport_for(const ns_box *b, double *x0, double *y0,
                   double *x1, double *y1)
{
    for (const ns_box *a = b ? b->parent : NULL; a; a = a->parent) {
        if (!a->scrolls) continue;
        *x0 = a->x + a->margin.left + a->border.left + a->scroll_x;
        *y0 = a->y + a->margin.top + a->border.top + a->scroll_y;
        *x1 = *x0 + a->padding.left + a->content_width + a->padding.right;
        *y1 = *y0 + a->padding.top + a->content_height + a->padding.bottom;
        return TRUE;
    }
    return FALSE;
}

void
ns_box_sticky_offset(const ns_box *b, double vp_x0, double vp_y0,
                     double vp_x1, double vp_y1, double *dx, double *dy)
{
    double x0 = vp_x0, y0 = vp_y0, x1 = vp_x1, y1 = vp_y1;
    box_scrollport_for(b, &x0, &y0, &x1, &y1);
    ns_box_sticky_offset_in(b, x0, y0, x1, y1, dx, dy);
}

void
ns_box_hit_offset(const ns_box *b, double *dx, double *dy)
{
    *dx = 0;
    *dy = 0;
    if (!b || !b->style) return;
    const ns_css_value *pv = b->style->values[NS_CSS_POSITION];
    if (!pv || pv->kind != NS_CSS_V_KEYWORD || !pv->u.keyword) return;
    if (strcmp(pv->u.keyword, "fixed") == 0) {
        if (ns_box_is_fixed(b)) {
            *dx = g_hit_vp_x;
            *dy = g_hit_vp_y;
        }
    } else if (strcmp(pv->u.keyword, "sticky") == 0) {
        ns_box_sticky_offset(b, g_hit_vp_x, g_hit_vp_y,
                             g_hit_vp_x + ns_css_viewport_w(),
                             g_hit_vp_y + ns_css_viewport_h(), dx, dy);
    }
}

static void
hit_enter_box(const ns_box *b, double *x, double *y)
{
    double dx, dy;
    ns_box_hit_offset(b, &dx, &dy);
    *x -= dx;
    *y -= dy;
}

static gboolean
box_defers_hit_layer(const ns_box *b, int *out_z)
{
    if (!b || !b->style) return FALSE;
    const ns_css_value *p = b->style->values[NS_CSS_POSITION];
    if (!p || p->kind != NS_CSS_V_KEYWORD || !p->u.keyword) return FALSE;
    const char *kw = p->u.keyword;
    if (strcmp(kw, "relative") && strcmp(kw, "absolute") &&
        strcmp(kw, "fixed") && strcmp(kw, "sticky")) return FALSE;
    const ns_css_value *v = b->style->values[NS_CSS_Z_INDEX];
    int z = (v && v->kind == NS_CSS_V_LENGTH) ? (int)v->u.length.v : 0;
    if (z < 0) return FALSE;
    if (out_z) *out_z = z;
    return TRUE;
}

static gboolean
box_has_hit_transform(const ns_box *b)
{
    const ns_style *s = b ? b->style : NULL;
    return s && (s->values[NS_CSS_TRANSFORM] || s->values[NS_CSS_TRANSLATE] ||
                 s->values[NS_CSS_ROTATE] || s->values[NS_CSS_SCALE]);
}

static gboolean
box_svg_yields_hit(const ns_box *b)
{
    if (!b || b->kind != NS_BOX_SVG) return FALSE;
    const ns_style *s = b->style;
    if (!s) return TRUE;
    const ns_css_value *pe = s->values[NS_CSS_POINTER_EVENTS];
    if (pe && pe->kind == NS_CSS_V_KEYWORD && pe->u.keyword &&
        strcmp(pe->u.keyword, "all") == 0)
        return FALSE;
    const ns_css_value *bg = s->values[NS_CSS_BACKGROUND_COLOR];
    if (bg && bg->kind == NS_CSS_V_COLOR && bg->u.color.a > 0) return FALSE;
    const ns_css_value *bi = s->values[NS_CSS_BACKGROUND_IMAGE];
    if (bi && (bi->kind == NS_CSS_V_URL || bi->kind == NS_CSS_V_GRADIENT))
        return FALSE;
    return b->border.top <= 0 && b->border.right <= 0 &&
           b->border.bottom <= 0 && b->border.left <= 0;
}

static int
hit_deferred_cmp(const void *a, const void *b)
{
    const hit_deferred *pa = a, *pb = b;
    if (pa->z != pb->z) return pa->z < pb->z ? -1 : 1;
    return pa->order < pb->order ? -1 : pa->order > pb->order ? 1 : 0;
}

static const ns_box *box_hit_test_tree(const ns_box *root, double x, double y);

static const ns_box *
hit_flush_deferred(GArray *list)
{
    if (!list || list->len == 0) return NULL;
    g_array_sort(list, hit_deferred_cmp);
    const ns_box *best = NULL;
    const ns_box *saved_flush = g_hit_flush_box;
    for (guint i = 0; i < list->len; i++) {
        const hit_deferred *d = &g_array_index(list, hit_deferred, i);
        g_hit_flush_box = d->box;
        const ns_box *m = box_hit_test_tree(d->box, d->x, d->y);
        if (m) best = m;
    }
    g_hit_flush_box = saved_flush;
    return best;
}

static const ns_box *
box_hit_test_tree(const ns_box *root, double x, double y)
{
    if (!root) return NULL;
    int defer_z = 0;
    if (g_hit_defer_depth > 0 && root != g_hit_flush_box &&
        box_defers_hit_layer(root, &defer_z)) {
        if (!g_hit_deferred)
            g_hit_deferred = g_array_new(FALSE, FALSE, sizeof(hit_deferred));
        hit_deferred d = { root, x, y, g_hit_deferred->len, defer_z };
        g_array_append_val(g_hit_deferred, d);
        return NULL;
    }
    hit_enter_box(root, &x, &y);
    if (!box_hit_untransform_point(root, &x, &y)) return NULL;
    if (root->paint_bottom > root->paint_top &&
        (y < root->paint_top - 1.0 || y > root->paint_bottom + 1.0))
        return NULL;
    gboolean clipped = box_clips_children(root);
    if (clipped && !box_padding_contains(root, x, y))
        goto self_test;
    if (ns_paint_3d_registered(root)) {
        const ns_box *m3 = ns_paint_3d_pick(root, x, y);
        if (m3) return m3;
        goto self_test;
    }
    const ns_box *best = NULL;
    double cx = x + root->scroll_x;
    double cy = y + root->scroll_y;
    gboolean own_scope = root->parent == NULL || root == g_hit_flush_box ||
                         clipped || box_has_hit_transform(root);
    GArray *saved_deferred = NULL;
    if (own_scope) {
        saved_deferred = g_hit_deferred;
        g_hit_deferred = NULL;
        g_hit_defer_depth++;
    }
    guint sn = 0;
    const ns_box **stacked = hit_children_stacked(root, &sn);
    if (stacked) {
        for (guint i = 0; i < sn; i++) {
            const ns_box *m = box_hit_test_tree(stacked[i], cx, cy);
            if (m) best = m;
        }
        g_free(stacked);
    } else {
        for (const ns_box *c = root->first_child; c; c = c->next_sibling) {
            const ns_box *m = box_hit_test_tree(c, cx, cy);
            if (m) best = m;
        }
    }
    if (root->inline_atomics)
        for (guint i = 0; i < root->inline_atomics->len; i++) {
            const ns_inline_atomic *atomic =
                &g_array_index(root->inline_atomics, ns_inline_atomic, i);
            const ns_box *ab = atomic->box;
            if (!ab) continue;
            double ax, ay;
            inline_atomic_hit_point(root, atomic, cx, cy, &ax, &ay);
            const ns_box *m = box_hit_test_tree(ab, ax, ay);
            if (m) best = m;
        }
    if (own_scope) {
        GArray *mine = g_hit_deferred;
        g_hit_deferred = saved_deferred;
        g_hit_defer_depth--;
        const ns_box *m = hit_flush_deferred(mine);
        if (m) best = m;
        if (mine) g_array_free(mine, TRUE);
    }
    if (best) return best;
self_test: ;
    double x0 = root->x;
    double y0 = root->y;
    gboolean block_edges = root->kind == NS_BOX_BLOCK ||
                           root->kind == NS_BOX_TABLE_CAPTION;
    double x1 = x0 + root->content_width
              + (block_edges ? root->padding.left + root->padding.right +
                               root->border.left + root->border.right +
                               root->margin.left + root->margin.right : 0);
    double y1 = y0 + root->content_height
              + (block_edges ? root->padding.top + root->padding.bottom +
                               root->border.top + root->border.bottom +
                               root->margin.top + root->margin.bottom : 0);
    if (!box_blocks_hit_testing(root) && !box_svg_yields_hit(root) &&
        x >= x0 && x <= x1 && y >= y0 && y <= y1 && root->dom) {
        g_hit_local_x = x;
        g_hit_local_y = y;
        return root;
    }
    return NULL;
}

static const ns_box *
box_hit_test_root(const ns_box *root, double x, double y)
{
    GArray *saved_list = g_hit_deferred;
    int saved_depth = g_hit_defer_depth;
    const ns_box *saved_flush = g_hit_flush_box;
    g_hit_deferred = NULL;
    g_hit_defer_depth = 0;
    g_hit_flush_box = root;
    const ns_box *m = box_hit_test_tree(root, x, y);
    g_hit_deferred = saved_list;
    g_hit_defer_depth = saved_depth;
    g_hit_flush_box = saved_flush;
    return m;
}

static const ns_box *
box_for_dom_node(const ns_box *root, const ns_node *node)
{
    if (!root) return NULL;
    if (root->dom == node) return root;
    for (const ns_box *c = root->first_child; c; c = c->next_sibling) {
        const ns_box *m = box_for_dom_node(c, node);
        if (m) return m;
    }
    return NULL;
}

const ns_box *
ns_box_hit_test(const ns_box *root, double x, double y)
{
    const ns_node *modal = ns_dom_active_modal();
    if (modal) {
        const ns_box *top = box_for_dom_node(root, modal);
        if (top && top != root) {
            const ns_box *m = box_hit_test_root(top, x, y);
            if (m) return m;
        }
    }
    return box_hit_test_root(root, x, y);
}

static const ns_node *
image_map_find(const ns_node *n, const char *name, int depth)
{
    if (!n || depth >= 512) return NULL;
    if (ns_node_is_element_named(n, "map")) {
        const char *id = ns_element_get_attr(n, "id");
        const char *nm = ns_element_get_attr(n, "name");
        if ((id && strcmp(id, name) == 0) || (nm && strcmp(nm, name) == 0))
            return n;
    }
    for (const ns_node *c = n->first_child; c; c = c->next_sibling) {
        if (c->kind != NS_NODE_ELEMENT || ns_element_get_attr(c, NS_SHADOW_ATTR))
            continue;
        const ns_node *m = image_map_find(c, name, depth + 1);
        if (m) return m;
    }
    return NULL;
}

static const ns_node *
image_map_for(const ns_node *img)
{
    const char *usemap = ns_element_get_attr(img, "usemap");
    const char *hash = usemap ? strchr(usemap, '#') : NULL;
    if (!hash || !hash[1]) return NULL;
    const ns_node *scope = img;
    while (scope->parent && !ns_element_get_attr(scope, NS_SHADOW_ATTR))
        scope = scope->parent;
    return image_map_find(scope, hash + 1, 0);
}

static gboolean
image_map_is_delimiter(char c)
{
    return c == ',' || c == ';' || c == ' ' || c == '\t' || c == '\n' ||
           c == '\f' || c == '\r';
}

static double
image_map_number(const char *s, const char *end)
{
    const char *p = s;
    double sign = 1.0;
    if (p < end && (*p == '-' || *p == '+')) {
        if (*p == '-') sign = -1.0;
        p++;
    }
    const char *digits = p;
    while (p < end && g_ascii_isdigit(*p)) p++;
    if (p < end && *p == '.' && p + 1 < end && g_ascii_isdigit(p[1])) {
        p++;
        while (p < end && g_ascii_isdigit(*p)) p++;
    }
    if (p == digits) return 0;
    if (p < end && (*p == 'e' || *p == 'E')) {
        const char *e = p + 1;
        if (e < end && (*e == '-' || *e == '+')) e++;
        if (e < end && g_ascii_isdigit(*e)) {
            p = e;
            while (p < end && g_ascii_isdigit(*p)) p++;
        }
    }
    char *copy = g_strndup(digits, (gsize)(p - digits));
    double v = g_ascii_strtod(copy, NULL);
    g_free(copy);
    return isfinite(v) ? sign * v : 0;
}

static GArray *
image_map_coords(const char *coords)
{
    GArray *out = g_array_new(FALSE, FALSE, sizeof(double));
    const char *p = coords ? coords : "";
    while (*p && image_map_is_delimiter(*p)) p++;
    while (*p) {
        while (*p && !image_map_is_delimiter(*p) && !g_ascii_isdigit(*p) &&
               *p != '.' && *p != '-')
            p++;
        const char *start = p;
        while (*p && !image_map_is_delimiter(*p)) p++;
        double v = image_map_number(start, p);
        g_array_append_val(out, v);
        while (*p && image_map_is_delimiter(*p)) p++;
    }
    return out;
}

static gboolean
image_map_polygon_contains(const double *c, guint n, double x, double y)
{
    gboolean inside = FALSE;
    guint pts = n / 2;
    for (guint i = 0, j = pts - 1; i < pts; j = i++) {
        double xi = c[2 * i], yi = c[2 * i + 1];
        double xj = c[2 * j], yj = c[2 * j + 1];
        double cross = (xj - xi) * (y - yi) - (yj - yi) * (x - xi);
        if (fabs(cross) < 1e-6 && x >= MIN(xi, xj) && x <= MAX(xi, xj) &&
            y >= MIN(yi, yj) && y <= MAX(yi, yj))
            return TRUE;
        if ((yi > y) != (yj > y) &&
            x < (xj - xi) * (y - yi) / (yj - yi) + xi)
            inside = !inside;
    }
    return inside;
}

static gboolean
image_map_area_contains(const ns_node *area, double x, double y,
                        double width, double height)
{
    const char *shape = ns_element_get_attr(area, "shape");
    GArray *coords = image_map_coords(ns_element_get_attr(area, "coords"));
    const double *c = &g_array_index(coords, double, 0);
    guint n = coords->len;
    gboolean hit = FALSE;
    if (shape && (g_ascii_strcasecmp(shape, "circle") == 0 ||
                  g_ascii_strcasecmp(shape, "circ") == 0)) {
        if (n >= 3 && c[2] > 0)
            hit = (x - c[0]) * (x - c[0]) + (y - c[1]) * (y - c[1]) <=
                  c[2] * c[2];
    } else if (shape && g_ascii_strcasecmp(shape, "default") == 0) {
        hit = x >= 0 && y >= 0 && x < width && y < height;
    } else if (shape && (g_ascii_strcasecmp(shape, "poly") == 0 ||
                         g_ascii_strcasecmp(shape, "polygon") == 0)) {
        if (n >= 6) hit = image_map_polygon_contains(c, n & ~1u, x, y);
    } else if (n >= 4) {
        double x1 = MIN(c[0], c[2]), x2 = MAX(c[0], c[2]);
        double y1 = MIN(c[1], c[3]), y2 = MAX(c[1], c[3]);
        hit = x >= x1 && x < x2 && y >= y1 && y < y2;
    }
    g_array_free(coords, TRUE);
    return hit;
}

static const ns_node *
image_map_area_in(const ns_node *n, double x, double y, double width,
                  double height, int depth)
{
    if (!n || depth >= 512) return NULL;
    for (const ns_node *c = n->first_child; c; c = c->next_sibling) {
        if (c->kind != NS_NODE_ELEMENT) continue;
        if (ns_node_is_element_named(c, "area") &&
            image_map_area_contains(c, x, y, width, height))
            return c;
        const ns_node *m = image_map_area_in(c, x, y, width, height, depth + 1);
        if (m) return m;
    }
    return NULL;
}

const ns_box *
ns_box_hit_test_local(const ns_box *root, double x, double y,
                      double *local_x, double *local_y)
{
    const ns_box *hit = ns_box_hit_test(root, x, y);
    *local_x = g_hit_local_x;
    *local_y = g_hit_local_y;
    return hit;
}

const ns_node *
ns_box_image_map_area(const ns_box *b, double local_x, double local_y)
{
    if (!b || b->kind != NS_BOX_IMAGE ||
        !ns_node_is_element_named(b->dom, "img") ||
        !ns_element_get_attr(b->dom, "usemap"))
        return NULL;
    const ns_node *map = image_map_for(b->dom);
    if (!map) return NULL;
    double x = local_x - (b->x + b->margin.left + b->border.left +
                          b->padding.left);
    double y = local_y - (b->y + b->margin.top + b->border.top +
                          b->padding.top);
    return image_map_area_in(map, x, y, b->content_width, b->content_height,
                             0);
}

const ns_node *
ns_box_hit_node(const ns_box *root, double x, double y)
{
    double local_x = 0, local_y = 0;
    const ns_box *hit = ns_box_hit_test_local(root, x, y, &local_x, &local_y);
    const ns_node *target = hit ? hit->dom : NULL;
    const ns_node *inline_target = ns_box_hit_inline_dom(root, x, y);
    if (inline_target) target = inline_target;
    const ns_node *form_target = ns_box_hit_form_dom(root, x, y);
    if (form_target) target = form_target;
    if (hit && target == hit->dom) {
        const ns_node *area = ns_box_image_map_area(hit, local_x, local_y);
        if (area) target = area;
    }
    return target;
}

const ns_box *
ns_box_find_by_id(const ns_box *root, const char *id)
{
    if (!root || !id) return NULL;
    if (root->dom && root->dom->kind == NS_NODE_ELEMENT) {
        const char *eid = ns_element_get_attr(root->dom, "id");
        if (eid && strcmp(eid, id) == 0) return root;
    }
    for (const ns_box *c = root->first_child; c; c = c->next_sibling) {
        const ns_box *m = ns_box_find_by_id(c, id);
        if (m) return m;
    }
    return NULL;
}

const ns_box *
ns_box_find_by_id_or_name(const ns_box *root, const char *frag)
{
    if (!root || !frag) return NULL;
    if (root->dom && root->dom->kind == NS_NODE_ELEMENT) {
        const char *eid = ns_element_get_attr(root->dom, "id");
        if (eid && strcmp(eid, frag) == 0) return root;
        if (root->dom->name &&
            g_ascii_strcasecmp(root->dom->name, "a") == 0) {
            const char *nm = ns_element_get_attr(root->dom, "name");
            if (nm && strcmp(nm, frag) == 0) return root;
        }
    }
    for (const ns_box *c = root->first_child; c; c = c->next_sibling) {
        const ns_box *m = ns_box_find_by_id_or_name(c, frag);
        if (m) return m;
    }
    return NULL;
}

const ns_link_range *
ns_box_hit_link_range(const ns_box *root, double x, double y)
{
    if (!root) return NULL;
    if (!box_hit_untransform_point(root, &x, &y)) return NULL;
    if (root->inline_atomics)
        for (guint i = 0; i < root->inline_atomics->len; i++) {
            const ns_inline_atomic *atomic =
                &g_array_index(root->inline_atomics, ns_inline_atomic, i);
            const ns_box *ab = atomic->box;
            if (!ab) continue;
            double ax, ay;
            inline_atomic_hit_point(root, atomic,
                                    x + root->scroll_x,
                                    y + root->scroll_y, &ax, &ay);
            const ns_link_range *r = ns_box_hit_link_range(ab, ax, ay);
            if (r) return r;
        }
    if (!box_blocks_hit_testing(root) &&
        root->kind == NS_BOX_INLINE && root->links &&
        root->links->len > 0) {
        double box_x0 = root->x;
        double box_y0 = root->y;
        double box_y1 = box_y0 + root->content_height;
        if (x >= box_x0 && x <= box_x0 + root->content_width &&
            y >= box_y0 && y <= box_y1) {
            gsize byte = 0;
            if (ns_paint_inline_xy_to_byte(root, x - box_x0, y - box_y0, &byte)) {
                for (guint i = 0; i < root->links->len; i++) {
                    const ns_link_range *r = &g_array_index(root->links, ns_link_range, i);
                    if (byte >= r->start && byte < r->start + r->len)
                        return r;
                }
            }
            return NULL;
        }
    }
    if (box_clips_children(root) && !box_padding_contains(root, x, y))
        return NULL;
    double cx = x + root->scroll_x;
    double cy = y + root->scroll_y;
    const ns_link_range *best = NULL;
    guint sn = 0;
    const ns_box **stacked = hit_children_stacked(root, &sn);
    if (stacked) {
        for (guint i = 0; i < sn; i++) {
            const ns_link_range *r = ns_box_hit_link_range(stacked[i], cx, cy);
            if (r) best = r;
        }
        g_free(stacked);
    } else {
        for (const ns_box *c = root->first_child; c; c = c->next_sibling) {
            const ns_link_range *r = ns_box_hit_link_range(c, cx, cy);
            if (r) best = r;
        }
    }
    return best;
}

const char *
ns_box_hit_link(const ns_box *root, double x, double y)
{
    const ns_link_range *r = ns_box_hit_link_range(root, x, y);
    return r ? r->href : NULL;
}

const ns_node *
ns_box_hit_inline_dom(const ns_box *root, double x, double y)
{
    if (!root) return NULL;
    hit_enter_box(root, &x, &y);
    if (!box_hit_untransform_point(root, &x, &y)) return NULL;
    if (root->paint_bottom > root->paint_top &&
        (y < root->paint_top - 1.0 || y > root->paint_bottom + 1.0))
        return NULL;
    if (root->inline_atomics)
        for (guint i = 0; i < root->inline_atomics->len; i++) {
            const ns_inline_atomic *atomic =
                &g_array_index(root->inline_atomics, ns_inline_atomic, i);
            const ns_box *ab = atomic->box;
            if (!ab) continue;
            double ax, ay;
            inline_atomic_hit_point(root, atomic,
                                    x + root->scroll_x,
                                    y + root->scroll_y, &ax, &ay);
            const ns_node *m = ns_box_hit_inline_dom(ab, ax, ay);
            if (m) return m;
        }
    if (!box_blocks_hit_testing(root) &&
        root->kind == NS_BOX_INLINE && root->attrs &&
        root->attrs->len > 0 && root->text && *root->text) {
        double box_x0 = root->x;
        double box_y0 = root->y;
        double box_y1 = box_y0 + root->content_height;
        if (x >= box_x0 && x <= box_x0 + root->content_width &&
            y >= box_y0 && y <= box_y1) {
            gsize byte = 0;
            if (ns_paint_inline_xy_to_byte(root, x - box_x0, y - box_y0, &byte)) {
                const ns_node *best = NULL;
                gsize best_len = 0;
                for (guint i = 0; i < root->attrs->len; i++) {
                    const ns_inline_attr *r =
                        &g_array_index(root->attrs, ns_inline_attr, i);
                    if (r->kind != NS_INLINE_ELEMENT || !r->dom) continue;
                    if (byte < r->start || byte >= r->start + r->len) continue;
                    if (!best || r->len < best_len) {
                        best = r->dom;
                        best_len = r->len;
                    }
                }
                if (best) return best;
            }
            return NULL;
        }
    }
    if (box_clips_children(root) && !box_padding_contains(root, x, y))
        return NULL;
    if (ns_paint_3d_registered(root)) return NULL;
    double cx = x + root->scroll_x;
    double cy = y + root->scroll_y;
    const ns_node *best = NULL;
    guint sn = 0;
    const ns_box **stacked = hit_children_stacked(root, &sn);
    if (stacked) {
        for (guint i = 0; i < sn; i++) {
            const ns_node *m = ns_box_hit_inline_dom(stacked[i], cx, cy);
            if (m) best = m;
        }
        g_free(stacked);
    } else {
        for (const ns_box *c = root->first_child; c; c = c->next_sibling) {
            const ns_node *m = ns_box_hit_inline_dom(c, cx, cy);
            if (m) best = m;
        }
    }
    return best;
}

static void
box_inline_union_for_dom(const ns_box *root, const ns_node *target,
                         double dx, double dy,
                         double *x0, double *y0, double *x1, double *y1,
                         gboolean *any)
{
    if (!root) return;
    if (root->kind == NS_BOX_INLINE && root->attrs && root->text) {
        for (guint i = 0; i < root->attrs->len; i++) {
            const ns_inline_attr *r =
                &g_array_index(root->attrs, ns_inline_attr, i);
            if (r->dom != target || r->len == 0) continue;
            double ex, ey, ew, eh;
            if (!ns_paint_inline_range_extents(root, r->start, r->len,
                                               &ex, &ey, &ew, &eh))
                continue;
            double rx0 = root->x + dx + ex;
            double ry0 = root->y + dy + ey;
            double rx1 = rx0 + ew;
            double ry1 = ry0 + eh;
            if (!*any) {
                *x0 = rx0; *y0 = ry0; *x1 = rx1; *y1 = ry1;
                *any = TRUE;
            } else {
                if (rx0 < *x0) *x0 = rx0;
                if (ry0 < *y0) *y0 = ry0;
                if (rx1 > *x1) *x1 = rx1;
                if (ry1 > *y1) *y1 = ry1;
            }
        }
    }
    double cdx = dx - root->scroll_x;
    double cdy = dy - root->scroll_y;
    for (const ns_box *c = root->first_child; c; c = c->next_sibling)
        box_inline_union_for_dom(c, target, cdx, cdy, x0, y0, x1, y1, any);
    if (root->inline_atomics)
        for (guint i = 0; i < root->inline_atomics->len; i++) {
            const ns_inline_atomic *atomic =
                &g_array_index(root->inline_atomics, ns_inline_atomic, i);
            if (!atomic->box) continue;
            double adx = cdx + root->x + atomic->owner_offset_x -
                         atomic->box->x;
            double ady = cdy + root->y + atomic->owner_offset_y -
                         atomic->box->y;
            box_inline_union_for_dom(atomic->box, target, adx, ady,
                                     x0, y0, x1, y1, any);
        }
}

gboolean
ns_box_inline_rect_for_dom(const ns_box *root, const ns_node *target,
                           double *x, double *y, double *w, double *h)
{
    if (!root || !target) return FALSE;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    gboolean any = FALSE;
    box_inline_union_for_dom(root, target, 0, 0, &x0, &y0, &x1, &y1, &any);
    if (!any) return FALSE;
    if (x) *x = x0;
    if (y) *y = y0;
    if (w) *w = x1 - x0;
    if (h) *h = y1 - y0;
    return TRUE;
}
