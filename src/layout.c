/* Nordstjernen — block layout.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "layout.h"

#include <math.h>
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <string.h>

#include "css.h"
#include "image.h"
#include "net.h"
#include "paint.h"

#define length_or ns_css_length_or

static double
length_resolve(const ns_css_value *v, double basis, double fallback)
{
    if (!v) return fallback;
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

static gboolean
height_is_percent(const ns_css_value *v)
{
    if (!v) return FALSE;
    if (v->kind == NS_CSS_V_LENGTH && v->u.length.unit == NS_CSS_UNIT_PERCENT)
        return TRUE;
    if (v->kind == NS_CSS_V_CALC && v->u.calc.pct != 0.0)
        return TRUE;
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

static double
resolve_used_height(const ns_box *box, const ns_css_value *hv,
                    double width_basis, double fallback)
{
    if (!hv) return fallback;
    if (height_is_percent(hv)) {
        double vh;
        if (box_is_doc_root(box)) {
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
containing_block_definite_height(const ns_box *box)
{
    const ns_box *p = box ? box->parent : NULL;
    while (p && !p->style) p = p->parent;
    if (!p) return -1;
    const ns_css_value *h = p->style->values[NS_CSS_HEIGHT];
    if (!h) {
        const ns_css_value *top = p->style->values[NS_CSS_TOP];
        const ns_css_value *bottom = p->style->values[NS_CSS_BOTTOM];
        if (style_is_absolute_or_fixed(p->style) &&
            top && !length_is_auto(top) &&
            bottom && !length_is_auto(bottom) &&
            p->content_height > 0)
            return p->content_height;
        return -1;
    }
    if (height_is_percent(h)) {
        double base = box_is_doc_root(p) ? ns_css_viewport_h()
                                         : containing_block_definite_height(p);
        if (base < 0) return -1;
        if (h->kind == NS_CSS_V_CALC)
            return h->u.calc.pct / 100.0 * base + h->u.calc.px;
        return h->u.length.v * base / 100.0;
    }
    if (p->content_height > 0) return p->content_height;
    return length_resolve(h, 0, -1);
}

static double
resolve_height_with_basis(const ns_css_value *hv, double width_basis,
                          double height_basis, double fallback)
{
    if (!hv) return fallback;
    if (hv->kind == NS_CSS_V_CALC) {
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
style_is_block(const ns_style *s)
{
    const ns_css_value *v = s ? s->values[NS_CSS_DISPLAY] : NULL;
    return keyword_is(v, "block")        || keyword_is(v, "flex") ||
           keyword_is(v, "grid")         || keyword_is(v, "list-item") ||
           keyword_is(v, "flow-root")    || keyword_is(v, "inline-block") ||
           keyword_is(v, "table")        || keyword_is(v, "inline-table") ||
           keyword_is(v, "table-caption") || keyword_is(v, "contents") ||
           keyword_is(v, "-webkit-box");
}

static gboolean
style_is_block_level(const ns_style *s)
{
    const ns_css_value *v = s ? s->values[NS_CSS_DISPLAY] : NULL;
    return keyword_is(v, "block")     || keyword_is(v, "flex") ||
           keyword_is(v, "grid")      || keyword_is(v, "list-item") ||
           keyword_is(v, "flow-root") || keyword_is(v, "table") ||
           keyword_is(v, "table-caption") || keyword_is(v, "-webkit-box");
}

static gboolean
style_display_is_table(const ns_style *s)
{
    const ns_css_value *v = s ? s->values[NS_CSS_DISPLAY] : NULL;
    return keyword_is(v, "table") || keyword_is(v, "inline-table");
}

static gboolean
style_display_is_table_row(const ns_style *s)
{
    const ns_css_value *v = s ? s->values[NS_CSS_DISPLAY] : NULL;
    return keyword_is(v, "table-row");
}

static gboolean
style_display_is_table_cell(const ns_style *s)
{
    const ns_css_value *v = s ? s->values[NS_CSS_DISPLAY] : NULL;
    return keyword_is(v, "table-cell");
}

static gboolean
style_display_is_table_caption(const ns_style *s)
{
    const ns_css_value *v = s ? s->values[NS_CSS_DISPLAY] : NULL;
    return keyword_is(v, "table-caption");
}

static gboolean
style_is_flex_container(const ns_style *s)
{
    const ns_css_value *v = s ? s->values[NS_CSS_DISPLAY] : NULL;
    return keyword_is(v, "flex") || keyword_is(v, "inline-flex");
}

static gboolean
style_is_grid_container(const ns_style *s)
{
    const ns_css_value *v = s ? s->values[NS_CSS_DISPLAY] : NULL;
    return keyword_is(v, "grid") || keyword_is(v, "inline-grid");
}

static const char *
keyword_or(const ns_style *s, ns_css_prop p, const char *fallback)
{
    if (!s || !s->values[p]) return fallback;
    const ns_css_value *v = s->values[p];
    if (v->kind != NS_CSS_V_KEYWORD || !v->u.keyword) return fallback;
    return v->u.keyword;
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
style_is_absolute_or_fixed(const ns_style *s)
{
    const ns_css_value *v = s ? s->values[NS_CSS_POSITION] : NULL;
    return keyword_is(v, "absolute") || keyword_is(v, "fixed");
}

static gboolean
style_is_none(const ns_style *s)
{
    return s && s->values[NS_CSS_DISPLAY] && is_keyword(s->values[NS_CSS_DISPLAY], "none");
}

static gboolean
style_is_contents(const ns_style *s)
{
    return s && keyword_is(s->values[NS_CSS_DISPLAY], "contents");
}

static gboolean
text_is_ws_only(const char *text)
{
    if (!text) return TRUE;
    for (const char *p = text; *p; p++)
        if (!g_ascii_isspace((unsigned char)*p)) return FALSE;
    return TRUE;
}

static gboolean
class_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static gboolean
class_token_hides(const char *tok, gsize len)
{
    return len > 7 && tok[len - 7] == ':' &&
           memcmp(tok + len - 6, "hidden", 6) == 0;
}

static gboolean
node_has_hidden_utility(const ns_node *n)
{
    if (!n || n->kind != NS_NODE_ELEMENT) return FALSE;
    const char *cls = ns_element_get_attr(n, "class");
    if (!cls || !*cls) return FALSE;
    const char *s = cls;
    while (*s) {
        while (*s && class_ws(*s)) s++;
        const char *tok = s;
        while (*s && !class_ws(*s)) s++;
        if (s > tok && class_token_hides(tok, (gsize)(s - tok)))
            return TRUE;
    }
    return FALSE;
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
    padding->top    = length_resolve(s->values[NS_CSS_PADDING_TOP],    basis, 0);
    padding->right  = length_resolve(s->values[NS_CSS_PADDING_RIGHT],  basis, 0);
    padding->bottom = length_resolve(s->values[NS_CSS_PADDING_BOTTOM], basis, 0);
    padding->left   = length_resolve(s->values[NS_CSS_PADDING_LEFT],   basis, 0);
    border->top    = length_or(s->values[NS_CSS_BORDER_TOP_WIDTH],    0);
    border->right  = length_or(s->values[NS_CSS_BORDER_RIGHT_WIDTH],  0);
    border->bottom = length_or(s->values[NS_CSS_BORDER_BOTTOM_WIDTH], 0);
    border->left   = length_or(s->values[NS_CSS_BORDER_LEFT_WIDTH],   0);
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
    if (plen > G_MAXSIZE - slen - 1) return prefix;
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
        if (cur->links) g_array_free(cur->links, TRUE);
        if (cur->attrs) g_array_free(cur->attrs, TRUE);
        if (cur->table_col_hints) g_array_free(cur->table_col_hints, TRUE);
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

static gboolean
is_replaced_block_tag(const char *name)
{
    return name && (strcmp(name, "img") == 0 ||
                    strcmp(name, "picture") == 0 ||
                    strcmp(name, "svg") == 0 ||
                    strcmp(name, "audio") == 0 ||
                    strcmp(name, "video") == 0 ||
                    strcmp(name, "table") == 0);
}

static gboolean
is_inline_level_replaced(const ns_node *n, GHashTable *styles)
{
    if (!n || n->kind != NS_NODE_ELEMENT || !n->name) return FALSE;
    if (!(strcmp(n->name, "img") == 0 || strcmp(n->name, "svg") == 0 ||
          strcmp(n->name, "picture") == 0 || strcmp(n->name, "audio") == 0 ||
          strcmp(n->name, "video") == 0))
        return FALSE;
    const ns_style *s = styles ? g_hash_table_lookup(styles, n) : NULL;
    const ns_css_value *d = s ? s->values[NS_CSS_DISPLAY] : NULL;
    if (keyword_is(d, "block")    || keyword_is(d, "flex") ||
        keyword_is(d, "grid")     || keyword_is(d, "table") ||
        keyword_is(d, "list-item")|| keyword_is(d, "flow-root"))
        return FALSE;
    return TRUE;
}

#define NS_LAYOUT_MAX_DEPTH 512

static gboolean tag_is_non_rendering(const char *name);

static GHashTable *g_contains_block_media_cache;

static gboolean
contains_block_media_depth(const ns_node *n, GHashTable *styles, int depth)
{
    if (!n || depth >= NS_LAYOUT_MAX_DEPTH || n->kind != NS_NODE_ELEMENT)
        return FALSE;
    for (const ns_node *c = n->first_child; c; c = c->next_sibling) {
        if (c->kind != NS_NODE_ELEMENT || !c->name) continue;
        if (tag_is_non_rendering(c->name)) continue;
        if (node_has_hidden_utility(c)) continue;
        if ((is_replaced_block_tag(c->name) &&
             !is_inline_level_replaced(c, styles)) ||
            strcmp(c->name, "iframe") == 0)
            return TRUE;
        if (styles) {
            const ns_style *cs = g_hash_table_lookup(styles, c);
            if (cs) {
                if (style_is_none(cs) || style_is_absolute_or_fixed(cs))
                    continue;
                if (style_is_block_level(cs)) return TRUE;
                const ns_css_value *d = cs->values[NS_CSS_DISPLAY];
                if (keyword_is(d, "inline-block") ||
                    keyword_is(d, "inline-flex") ||
                    keyword_is(d, "inline-grid") ||
                    keyword_is(d, "inline-table"))
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
    gboolean r = contains_block_media_depth(n, styles, 0);
    if (g_contains_block_media_cache)
        g_hash_table_insert(g_contains_block_media_cache,
                            (gpointer)n, GINT_TO_POINTER(r ? 2 : 1));
    return r;
}

static gboolean
is_inline_dom(const ns_node *n, GHashTable *styles)
{
    if (!n) return FALSE;
    if (n->kind == NS_NODE_TEXT) return TRUE;
    if (n->kind != NS_NODE_ELEMENT) return FALSE;
    if (node_has_hidden_utility(n)) return FALSE;
    if (n->name && strcmp(n->name, "slot") == 0) return FALSE;
    if (is_replaced_block_tag(n->name)) {
        if (strcmp(n->name, "table") == 0) return FALSE;
        const ns_style *rs = g_hash_table_lookup(styles, n);
        if (rs && style_is_none(rs)) return FALSE;
        if (rs && style_is_absolute_or_fixed(rs)) return FALSE;
        return is_inline_level_replaced(n, styles);
    }
    const ns_style *s = g_hash_table_lookup(styles, n);
    if (!s) return n->name && strchr(n->name, '-') != NULL;
    if (style_is_none(s)) return FALSE;
    if (style_is_absolute_or_fixed(s)) return FALSE;
    if (!style_is_block(s) && contains_block_media(n, styles)) return FALSE;
    if (keyword_is(s->values[NS_CSS_DISPLAY], "inline-block"))
        return TRUE;
    return !style_is_block(s);
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
    const ns_css_value *d = s ? s->values[NS_CSS_DISPLAY] : NULL;
    if (keyword_is(d, "table-header-group")) return ROW_GROUP_HEADER;
    if (keyword_is(d, "table-footer-group")) return ROW_GROUP_FOOTER;
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
static ns_box *build_pseudo_inline_for(const ns_style *ps, const ns_node *host);
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
        const ns_css_value *d = bs ? bs->values[NS_CSS_DISPLAY] : NULL;
        if (keyword_is(d, "inline-block") || keyword_is(d, "inline-flex") ||
            keyword_is(d, "inline-grid") || style_has_atomic_inline_box(bs))
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
        const ns_css_value *d = s ? s->values[NS_CSS_DISPLAY] : NULL;
        if (!(keyword_is(d, "inline-block") || keyword_is(d, "inline-flex") ||
              keyword_is(d, "inline-grid") || keyword_is(d, "flex") ||
              keyword_is(d, "grid")))
            return FALSE;
        return button_has_replaced_child(n) || style_has_atomic_inline_box(s);
    }
    if (strcmp(nm, "img") == 0 || strcmp(nm, "svg") == 0 ||
        strcmp(nm, "audio") == 0 || strcmp(nm, "video") == 0 ||
        strcmp(nm, "picture") == 0)
        return is_inline_level_replaced(n, styles);
    if (strcmp(nm, "input") == 0 || strcmp(nm, "textarea") == 0 ||
        strcmp(nm, "select") == 0 ||
        strcmp(nm, "progress") == 0 || strcmp(nm, "meter") == 0 ||
        strcmp(nm, "br") == 0 || strcmp(nm, "wbr") == 0)
        return FALSE;
    const ns_style *s = styles ? g_hash_table_lookup(styles, n) : NULL;
    if (!s) return FALSE;
    const ns_css_value *d = s->values[NS_CSS_DISPLAY];
    if (!(keyword_is(d, "inline-block") || keyword_is(d, "inline-flex") ||
          keyword_is(d, "inline-grid")))
        return FALSE;
    return style_has_atomic_inline_box(s) || contains_block_media(n, styles);
}
static const ns_node *g_focused_input_for_layout;
static gboolean       g_focused_is_contenteditable_for_layout;
static gsize          g_focused_caret_byte_for_layout;
static gsize          g_focused_sel_anchor_byte_for_layout;
static struct ns_image_cache *g_image_cache_for_layout;
static const char    *g_base_url_for_layout;
static GHashTable    *g_counters_for_layout;
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
    gboolean       fixed;
} ns_abs_entry;

static GArray  *g_abs_pending;
static gboolean g_abs_force_build;

static void
collect_box_bg_image(ns_box *box, const ns_style *s)
{
    const ns_css_value *bg_or_mask =
        (s && s->values[NS_CSS_BACKGROUND_IMAGE] &&
         s->values[NS_CSS_BACKGROUND_IMAGE]->kind == NS_CSS_V_URL &&
         s->values[NS_CSS_BACKGROUND_IMAGE]->u.url)
            ? s->values[NS_CSS_BACKGROUND_IMAGE]
        : (s && s->values[NS_CSS_MASK_IMAGE] &&
           s->values[NS_CSS_MASK_IMAGE]->kind == NS_CSS_V_URL &&
           s->values[NS_CSS_MASK_IMAGE]->u.url)
            ? s->values[NS_CSS_MASK_IMAGE]
            : NULL;
    if (!bg_or_mask) return;
    ns_box_media *m = ns_box_media_ensure(box);
    m->bg_image_src = g_strdup(bg_or_mask->u.url);
    if (g_image_cache_for_layout) {
        char *abs = g_base_url_for_layout
            ? ns_url_resolve(g_base_url_for_layout, m->bg_image_src)
            : NULL;
        m->bg_image = ns_image_cache_peek(g_image_cache_for_layout,
                                          abs ? abs : m->bg_image_src);
        g_free(abs);
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
            while (c && is_inline_dom(c, styles)) c = c->next_sibling;
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
                if (c->kind == NS_NODE_ELEMENT) {
                    const ns_style *cs = g_hash_table_lookup(styles, c);
                    if (style_is_contents(cs)) break;
                }
                if (!is_inline_dom(c, styles)) break;
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
    if (rows->len == 0) {
        ns_box *cell = build_anonymous_table_cell(n, styles);
        if (cell) {
            ns_box *row = box_new(NS_BOX_TABLE_ROW);
            row->dom = n;
            box_append_child(row, cell);
            box_append_child(table, row);
        }
    } else {
        for (guint i = 0; i < rows->len; i++) {
            const ns_node *tr = g_ptr_array_index(rows, i);
            ns_box *row = box_new(NS_BOX_TABLE_ROW);
            row->dom = tr;
            row->style = g_hash_table_lookup(styles, tr);
            for (const ns_node *c = tr->first_child; c; c = c->next_sibling) {
                if (!is_cell_element(c, styles)) continue;
                ns_box *cell = build_cell(c, styles);
                box_append_child(row, cell);
            }
            box_append_child(table, row);
        }
    }
    g_ptr_array_free(rows, TRUE);
    return table;
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
} collector_ctx;

typedef struct ns_atomic_raw {
    gsize start;
    ns_box *box;
} ns_atomic_raw;

static int
text_input_leading_spaces(const ns_style *s)
{
    if (!s) return 0;
    double fs = length_or(s->values[NS_CSS_FONT_SIZE], 16);
    if (!(fs > 0)) fs = 16;
    double pad = length_resolve(s->values[NS_CSS_PADDING_LEFT], fs * 20.0, 0);
    double space = fs * 0.25;
    if (!(space > 0) || pad <= space) return 0;
    int n = (int)((pad - space) / space + 0.5);
    if (n < 0) n = 0;
    if (n > 12) n = 12;
    return n;
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
        "style", "script", "head", "title", "noscript", "template", NULL,
    };
    return name_in(name, set);
}

static void
emit_attr(GArray *attrs, ns_inline_attr_kind k, gsize start, gsize end)
{
    if (end <= start) return;
    ns_inline_attr a = { .kind = k, .start = start, .len = end - start };
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
    if (mn > 0 && out < mn) out = mn;
    if (mx > 0 && out > mx) out = mx;
    return out;
}

static double
inline_attr_control_width(const ns_inline_attr *r, const ns_box *box)
{
    if (!r || !r->style) return r && r->box_w > 0 ? r->box_w : 0;
    double fs = length_or(r->style->values[NS_CSS_FONT_SIZE], 16);
    double w = control_dim_px_clamped(r->style, NS_CSS_WIDTH,
                                      NS_CSS_MIN_WIDTH, NS_CSS_MAX_WIDTH,
                                      fs, box ? box->content_width : 0);
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
control_prefers_css_chrome(ns_inline_attr_kind k, const ns_node *dom,
                           const ns_style *s)
{
    if (!dom || !s) return FALSE;
    if (keyword_is(s->values[NS_CSS_APPEARANCE], "none"))
        return TRUE;
    if (style_has_visible_control_box(s)) return TRUE;
    if (!ns_element_get_attr(dom, "class")) return FALSE;
    const ns_css_value *d = s->values[NS_CSS_DISPLAY];
    if (keyword_is(d, "block") || keyword_is(d, "flex") ||
        keyword_is(d, "grid") || keyword_is(d, "inline-block") ||
        keyword_is(d, "inline-flex") || keyword_is(d, "inline-grid"))
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
        while (v > 0) { v--; buf[n++] = base + (v % 26); v /= 26; }
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
        if (!*p) break;
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            const char *start = p;
            while (*p && *p != q) {
                if (*p == '\\' && p[1]) { p += 2; continue; }
                p++;
            }
            char *raw_str = g_strndup(start, p - start);
            for (const char *r = raw_str; *r; ) {
                if (*r == '\\' && r[1]) {
                    r++;
                    if (g_ascii_isxdigit(*r)) {
                        char hex[8] = {0}; int hn = 0;
                        while (hn < 6 && g_ascii_isxdigit(*r)) hex[hn++] = *r++;
                        gunichar uc = (gunichar)g_ascii_strtoull(hex, NULL, 16);
                        if (*r == ' ') r++;
                        if (uc) g_string_append_unichar(out, uc);
                    } else {
                        g_string_append_c(out, *r++);
                    }
                } else {
                    g_string_append_c(out, *r++);
                }
            }
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
            const char *start = p;
            while (*p && !g_ascii_isspace(*p) && *p != '"' && *p != '\'') p++;
            g_string_append_len(out, start, p - start);
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
                    gunichar uc = g_unichar_toupper(c);
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
    const char *s = ns_element_get_attr(n, attr);
    if (!s) return FALSE;
    while (*s && g_ascii_isspace(*s)) s++;
    if (!*s) return FALSE;
    char *end = NULL;
    double v = g_ascii_strtod(s, &end);
    if (end == s) return FALSE;
    while (*end && g_ascii_isspace(*end)) end++;
    if (*end || !isfinite(v)) return FALSE;
    *out = v;
    return TRUE;
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

static void
collect_walk(const ns_node *n, collector_ctx *ctx, int depth)
{
    if (!n || depth >= NS_LAYOUT_MAX_DEPTH) return;
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
    if (tag_is_non_rendering(n->name)) return;
    if (node_has_hidden_utility(n)) return;
    const ns_style *s = g_hash_table_lookup(ctx->styles, n);
    if (s && style_is_none(s)) return;
    if (s && style_is_absolute_or_fixed(s)) return;

    if (ctx->atomics && is_atomic_inline(n, ctx->styles)) {
        ns_box *sub = build_block(n, ctx->styles);
        if (sub) {
            ns_atomic_raw rec = { .start = ctx->out->len, .box = sub };
            g_string_append(ctx->out, "\xef\xbf\xbc");
            g_array_append_val(ctx->atomics, rec);
        }
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
            const char *real_value = ns_element_get_attr(n, "value");
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
            const char *size_str = ns_element_get_attr(n, "size");
            int size = size_str ? ns_parse_int(size_str, 20, 4, 80) : 20;
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
            if (focused) {
                if (anchor_pos != caret_pos) {
                    gsize s0 = anchor_pos < caret_pos ? anchor_pos : caret_pos;
                    gsize s1 = anchor_pos < caret_pos ? caret_pos : anchor_pos;
                    emit_attr(ctx->attrs, NS_INLINE_SELECTION, s0, s1);
                }
                emit_attr(ctx->attrs, NS_INLINE_CARET, caret_pos, caret_pos + 1);
            }
        } else if (type && (g_ascii_strcasecmp(type, "submit") == 0 ||
                            g_ascii_strcasecmp(type, "button") == 0 ||
                            g_ascii_strcasecmp(type, "reset") == 0)) {
            const char *v = ns_element_get_attr(n, "value");
            gsize start = ctx->out->len;
            if (v) {
                g_string_append(ctx->out, "\xc2\xa0");
                g_string_append(ctx->out, v);
                g_string_append(ctx->out, "\xc2\xa0");
            } else {
                v = g_ascii_strcasecmp(type, "submit") == 0 ? "Submit"
                  : g_ascii_strcasecmp(type, "reset")  == 0 ? "Reset"
                                                            : "Button";
                g_string_append_printf(ctx->out, "\xc2\xa0%s\xc2\xa0", v);
            }
            emit_form_attr_sized(ctx->attrs, NS_INLINE_BUTTON, start, ctx->out->len, n, ctx->styles);
            g_string_append_c(ctx->out, ' ');
        } else if (type && g_ascii_strcasecmp(type, "checkbox") == 0) {
            const char *checked = ns_element_get_attr(n, "checked");
            gsize start = ctx->out->len;
            g_string_append(ctx->out, checked ? "\xe2\x98\x91" : "\xe2\x98\x90");
            emit_form_attr_sized(ctx->attrs,
                checked ? NS_INLINE_CHECKBOX_CHECKED : NS_INLINE_CHECKBOX,
                start, ctx->out->len, n, ctx->styles);
            g_string_append_c(ctx->out, ' ');
        } else if (type && g_ascii_strcasecmp(type, "radio") == 0) {
            const char *checked = ns_element_get_attr(n, "checked");
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
            if (bs && bs->before && bs->before->values[NS_CSS_CONTENT]) {
                const ns_css_value *cv = bs->before->values[NS_CSS_CONTENT];
                if (cv && cv->kind == NS_CSS_V_KEYWORD && cv->u.keyword) {
                    char *gen = resolve_pseudo_content(cv->u.keyword, n);
                    if (gen) g_string_append(pseudo, gen);
                    g_free(gen);
                }
            }
            if (bs && bs->after && bs->after->values[NS_CSS_CONTENT]) {
                const ns_css_value *cv = bs->after->values[NS_CSS_CONTENT];
                if (cv && cv->kind == NS_CSS_V_KEYWORD && cv->u.keyword) {
                    char *gen = resolve_pseudo_content(cv->u.keyword, n);
                    if (gen) g_string_append(pseudo, gen);
                    g_free(gen);
                }
            }
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
            gsize start = ctx->out->len;
            g_string_append(ctx->out, "\xc2\xa0");
            int shown = 0;
            int cap = size_n > 0 ? size_n : 6;
            for (const ns_node *c = n->first_child; c && shown < cap; c = c->next_sibling) {
                if (c->kind != NS_NODE_ELEMENT || !c->name) continue;
                if (strcmp(c->name, "optgroup") == 0) {
                    const char *gl = ns_element_get_attr(c, "label");
                    if (gl && *gl) {
                        if (shown > 0) g_string_append(ctx->out, "\xe2\x80\xa8");
                        g_string_append_printf(ctx->out, "  %s", gl);
                        shown++;
                    }
                    for (const ns_node *opt = c->first_child;
                         opt && shown < cap; opt = opt->next_sibling) {
                        if (opt->kind != NS_NODE_ELEMENT || !opt->name ||
                            strcmp(opt->name, "option") != 0) continue;
                        if (shown > 0) g_string_append(ctx->out, "\xe2\x80\xa8");
                        gboolean sel = ns_element_get_attr(opt, "selected") != NULL;
                        char *t = ns_option_label_dup(opt);
                        g_string_append(ctx->out, sel ? "\xe2\x96\xb8 " : "    ");
                        g_string_append(ctx->out, t ? t : "");
                        g_free(t);
                        shown++;
                    }
                } else if (strcmp(c->name, "option") == 0) {
                    if (shown > 0) g_string_append(ctx->out, "\xe2\x80\xa8");
                    gboolean sel = ns_element_get_attr(c, "selected") != NULL;
                    char *t = ns_option_label_dup(c);
                    g_string_append(ctx->out, sel ? "\xe2\x96\xb8 " : "  ");
                    g_string_append(ctx->out, t ? t : "");
                    g_free(t);
                    shown++;
                }
            }
            if (shown == 0) g_string_append(ctx->out, "  ");
            g_string_append(ctx->out, "\xc2\xa0");
            emit_form_attr_sized(ctx->attrs, NS_INLINE_INPUT_FIELD,
                                 start, ctx->out->len, n, ctx->styles);
            return;
        }
        const ns_node *chosen = ns_select_chosen_option(n);
        char *label = chosen ? ns_node_collect_text(chosen) : g_strdup("");
        if (!label) label = g_strdup("");
        gsize start = ctx->out->len;
        g_string_append(ctx->out, "\xc2\xa0");
        if (*label) g_string_append(ctx->out, label);
        g_string_append(ctx->out, " \xe2\x96\xbe\xc2\xa0");
        emit_form_attr_sized(ctx->attrs, NS_INLINE_INPUT_FIELD,
                             start, ctx->out->len, n, ctx->styles);
        g_free(label);
        return;
    }
    if (strcmp(n->name, "option") == 0 || strcmp(n->name, "optgroup") == 0)
        return;
    if (strcmp(n->name, "textarea") == 0) {
        gsize start = ctx->out->len;
        gboolean focused = (n == g_focused_input_for_layout);
        g_string_append(ctx->out, "\xc2\xa0");
        gsize val_start = ctx->out->len;
        GString *raw = g_string_new(NULL);
        for (const ns_node *c = n->first_child; c; c = c->next_sibling)
            if (c->kind == NS_NODE_TEXT && c->text && *c->text)
                g_string_append(raw, c->text);
        gsize value_byte_len = raw->len;
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
            char ch = raw->str[i];
            if (ch == '\n')
                g_string_append(ctx->out, "\xe2\x80\xa8");
            else if (ch != '\r')
                g_string_append_c(ctx->out, ch);
        }
        g_string_free(raw, TRUE);
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
    if (ml >= 3.0 || pl >= 3.0) g_string_append_c(ctx->out, ' ');
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
    if (bold && ctx->bold_depth++ == 0) ctx->bold_start = ctx->out->len;
    if (italic && ctx->italic_depth++ == 0) ctx->italic_start = ctx->out->len;
    if (mono && ctx->mono_depth++ == 0) ctx->mono_start = ctx->out->len;
    if (uline && ctx->underline_depth++ == 0) ctx->underline_start = ctx->out->len;
    if (oline && ctx->overline_depth++ == 0) ctx->overline_start = ctx->out->len;
    if (strike && ctx->strike_depth++ == 0) ctx->strike_start = ctx->out->len;

    gsize weight_start = ctx->out->len;

    gboolean is_q = strcmp(n->name, "q") == 0;
    if (is_q) {
        g_string_append(ctx->out,
            (ctx->q_depth % 2 == 0) ? "\xe2\x80\x9c" : "\xe2\x80\x98");
        ctx->q_depth++;
    }

    gboolean bidi_override = FALSE;
    if (s) {
        const char *ub = ns_style_keyword(s, NS_CSS_UNICODE_BIDI);
        if (ub && (strcmp(ub, "bidi-override") == 0 ||
                   strcmp(ub, "isolate-override") == 0))
            bidi_override = TRUE;
    }
    if (bidi_override) {
        const char *bd_dir = ns_element_get_attr(n, "dir");
        if (!bd_dir || !*bd_dir) bd_dir = ns_style_keyword(s, NS_CSS_DIRECTION);
        g_string_append(ctx->out,
            (bd_dir && g_ascii_strcasecmp(bd_dir, "rtl") == 0)
                ? "\xe2\x80\xae"
                : "\xe2\x80\xad");
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

    if (s && s->before && s->before->values[NS_CSS_CONTENT]) {
        const ns_css_value *cv = s->before->values[NS_CSS_CONTENT];
        if (cv && cv->kind == NS_CSS_V_KEYWORD && cv->u.keyword) {
            char *gen = resolve_pseudo_content(cv->u.keyword, n);
            if (gen) g_string_append(ctx->out, gen);
            g_free(gen);
        }
    }

    for (const ns_node *c = n->first_child; c; c = c->next_sibling)
        collect_walk(c, ctx, depth + 1);

    if (s && s->after && s->after->values[NS_CSS_CONTENT]) {
        const ns_css_value *cv = s->after->values[NS_CSS_CONTENT];
        if (cv && cv->kind == NS_CSS_V_KEYWORD && cv->u.keyword) {
            char *gen = resolve_pseudo_content(cv->u.keyword, n);
            if (gen) g_string_append(ctx->out, gen);
            g_free(gen);
        }
    }

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

    if (bold && --ctx->bold_depth == 0)
        emit_attr(ctx->attrs, NS_INLINE_BOLD, ctx->bold_start, ctx->out->len);
    if (italic && --ctx->italic_depth == 0)
        emit_attr(ctx->attrs, NS_INLINE_ITALIC, ctx->italic_start, ctx->out->len);
    if (mono && --ctx->mono_depth == 0)
        emit_attr(ctx->attrs, NS_INLINE_MONOSPACE, ctx->mono_start, ctx->out->len);
    if (uline && --ctx->underline_depth == 0)
        emit_attr(ctx->attrs, NS_INLINE_UNDERLINE, ctx->underline_start, ctx->out->len);
    if (oline && --ctx->overline_depth == 0)
        emit_attr(ctx->attrs, NS_INLINE_OVERLINE, ctx->overline_start, ctx->out->len);
    if (strike && --ctx->strike_depth == 0)
        emit_attr(ctx->attrs, NS_INLINE_STRIKETHROUGH, ctx->strike_start, ctx->out->len);
    if (sup && ctx->out->len > rise_start)
        emit_attr(ctx->attrs, NS_INLINE_SUPERSCRIPT, rise_start, ctx->out->len);
    if (sub && ctx->out->len > rise_start)
        emit_attr(ctx->attrs, NS_INLINE_SUBSCRIPT, rise_start, ctx->out->len);
    if (small_caps && ctx->out->len > sc_start)
        emit_attr(ctx->attrs, NS_INLINE_SMALL_CAPS, sc_start, ctx->out->len);
    if (bidi_override)
        g_string_append(ctx->out, "\xe2\x80\xac");
    if (is_q) {
        ctx->q_depth--;
        g_string_append(ctx->out,
            (ctx->q_depth % 2 == 0) ? "\xe2\x80\x9d" : "\xe2\x80\x99");
    }
    if (mr >= 3.0 || pr >= 3.0) g_string_append_c(ctx->out, ' ');
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
build_inline_run(const ns_node *first, const ns_node *last_excl, GHashTable *styles)
{
    GString *buf = g_string_new(NULL);
    GArray  *raw_links = g_array_new(FALSE, FALSE, sizeof(ns_link_range));
    GArray  *raw_attrs = g_array_new(FALSE, FALSE, sizeof(ns_inline_attr));
    GArray  *raw_atomics = g_array_new(FALSE, FALSE, sizeof(ns_atomic_raw));
    g_array_set_clear_func(raw_links, link_clear);
    collector_ctx ctx = {
        .styles = styles, .out = buf, .links = raw_links, .attrs = raw_attrs,
        .atomics = raw_atomics,
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
        first->parent->name &&
        strcmp(first->parent->name, "summary") == 0 &&
        first->parent->parent &&
        first->parent->parent->kind == NS_NODE_ELEMENT &&
        first->parent->parent->name &&
        strcmp(first->parent->parent->name, "details") == 0 &&
        first == first->parent->first_child) {
        gboolean open = ns_element_get_attr(first->parent->parent, "open") != NULL;
        g_string_append(buf, open ? "\xe2\x96\xbe " : "\xe2\x96\xb8 ");
    }
    if (first && first->parent && first->parent->kind == NS_NODE_ELEMENT &&
        first->parent->name &&
        strcmp(first->parent->name, "li") == 0 &&
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
            if (!strstr(kw, "none")) {
                if (strstr(kw, "underline")) {
                    ns_inline_attr a = {
                        .kind = NS_INLINE_UNDERLINE,
                        .start = 0, .len = collapsed->len
                    };
                    g_array_append_val(box->attrs, a);
                }
                if (strstr(kw, "line-through")) {
                    ns_inline_attr a = {
                        .kind = NS_INLINE_STRIKETHROUGH,
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
build_form_control_block(const ns_node *n, const ns_style *s, GHashTable *styles)
{
    ns_box *block = box_new(NS_BOX_BLOCK);
    block->dom = n;
    block->style = s;
    collect_box_bg_image(block, s);
    ns_box *run = build_inline_run(n, n->next_sibling, styles);
    if (run && run->text && run->text[0]) {
        box_append_child(block, run);
    } else if (run) {
        ns_box_free(run);
    }
    return block;
}

static char *
first_url_from_srcset_sized(const char *srcset, const char *sizes)
{
    if (!srcset) return NULL;
    const double dpr = 1.0;

    char *width_url = NULL, *width_fallback = NULL;
    double width_best = -1.0, width_max = -1.0;
    char *density_url = NULL, *density_fallback = NULL;
    double density_best = G_MAXDOUBLE, density_max = -1.0;
    double target_w = -1.0;

    const char *p = srcset;
    while (*p) {
        while (*p && (g_ascii_isspace(*p) || *p == ',')) p++;
        if (!*p) break;
        const char *url_s = p;
        while (*p && *p != ',' && !g_ascii_isspace(*p)) p++;
        gsize url_len = (gsize)(p - url_s);
        while (url_len > 0 && url_s[url_len - 1] == ',') url_len--;
        if (url_len == 0) continue;
        while (*p == ' ' || *p == '\t') p++;
        const char *desc_s = p;
        while (*p && *p != ',') p++;
        gsize desc_len = (gsize)(p - desc_s);
        while (desc_len > 0 && (g_ascii_isspace(desc_s[desc_len - 1]) ||
                                desc_s[desc_len - 1] == ',')) desc_len--;

        double num = 0;
        char unit = 'x';
        if (desc_len > 0) {
            char *d = g_strndup(desc_s, desc_len);
            char *dend = NULL;
            double v = g_ascii_strtod(d, &dend);
            if (dend && dend != d && (*dend == 'w' || *dend == 'W' ||
                                      *dend == 'x' || *dend == 'X')) {
                num = v;
                unit = (*dend == 'w' || *dend == 'W') ? 'w' : 'x';
            }
            g_free(d);
        }

        if (unit == 'w') {
            if (target_w < 0) target_w = ns_css_sizes_resolve(sizes) * dpr;
            if (num >= target_w && (width_best < 0 || num < width_best)) {
                g_free(width_url);
                width_url = g_strndup(url_s, url_len);
                width_best = num;
            }
            if (num > width_max) {
                g_free(width_fallback);
                width_fallback = g_strndup(url_s, url_len);
                width_max = num;
            }
        } else {
            double d = num > 0 ? num : 1.0;
            if (d >= dpr && d < density_best) {
                g_free(density_url);
                density_url = g_strndup(url_s, url_len);
                density_best = d;
            }
            if (d > density_max) {
                g_free(density_fallback);
                density_fallback = g_strndup(url_s, url_len);
                density_max = d;
            }
        }
        if (*p == ',') p++;
    }

    if (width_max >= 0) {
        g_free(density_url);
        g_free(density_fallback);
        if (width_url) { g_free(width_fallback); return width_url; }
        return width_fallback;
    }
    g_free(width_url);
    g_free(width_fallback);
    if (density_url) { g_free(density_fallback); return density_url; }
    return density_fallback;
}

static gboolean
srcset_has_width_descriptor(const char *srcset)
{
    if (!srcset) return FALSE;
    const char *p = srcset;
    while (*p) {
        while (*p && (g_ascii_isspace(*p) || *p == ',')) p++;
        while (*p && *p != ',' && !g_ascii_isspace(*p)) p++;
        while (*p == ' ' || *p == '\t') p++;
        const char *desc_s = p;
        while (*p && *p != ',') p++;
        const char *desc_e = p;
        while (desc_e > desc_s && g_ascii_isspace(desc_e[-1])) desc_e--;
        if (desc_e > desc_s && (desc_e[-1] == 'w' || desc_e[-1] == 'W'))
            return TRUE;
        if (*p == ',') p++;
    }
    return FALSE;
}

static gboolean
ns_pixbuf_likely_supports(const char *mime)
{
    if (!mime || !*mime) return TRUE;
    return ns_image_pixbuf_supports_mime(mime);
}

static char *
pick_picture_source_url(const ns_node *picture)
{
    if (!picture) return NULL;
    char *data_fallback = NULL;
    for (const ns_node *c = picture->first_child; c; c = c->next_sibling) {
        if (c->kind != NS_NODE_ELEMENT || !c->name) continue;
        if (strcmp(c->name, "source") != 0) continue;
        const char *type = ns_element_get_attr(c, "type");
        if (type && !ns_pixbuf_likely_supports(type)) continue;
        const char *media = ns_element_get_attr(c, "media");
        if (media && *media && !ns_css_media_query_matches(media)) continue;
        const char *sizes = ns_element_get_attr(c, "sizes");
        const char *dsset = ns_element_get_attr(c, "data-srcset");
        char *u = first_url_from_srcset_sized(dsset, sizes);
        if (u && !g_str_has_prefix(u, "data:")) return u;
        if (u && !data_fallback) data_fallback = u;
        else g_free(u);
        const char *ss = ns_element_get_attr(c, "srcset");
        u = first_url_from_srcset_sized(ss, sizes);
        if (u && !g_str_has_prefix(u, "data:")) return u;
        if (u && !data_fallback) data_fallback = u;
        else g_free(u);
        const char *s = ns_element_get_attr(c, "src");
        if (s && *s) {
            if (!g_str_has_prefix(s, "data:")) return g_strdup(s);
            if (!data_fallback) data_fallback = g_strdup(s);
        }
    }
    return data_fallback;
}

static char *
pick_img_url(const ns_node *n)
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

    char *u = first_url_from_srcset_sized(dsset, sizes);
    if (u && (srcset_has_width_descriptor(dsset) || !dsrc || !*dsrc))
        return u;
    g_free(u);
    if (dsrc && *dsrc) return g_strdup(dsrc);

    gboolean placeholder = src && g_str_has_prefix(src, "data:");
    u = first_url_from_srcset_sized(srcset, sizes);
    if (u && (srcset_has_width_descriptor(srcset) || placeholder || !src || !*src))
        return u;
    g_free(u);
    if (src && *src && !placeholder) return g_strdup(src);
    if (src && *src) return g_strdup(src);
    return NULL;
}

static const ns_node *
ns_node_document_root(const ns_node *n)
{
    while (n && n->parent) n = n->parent;
    return n;
}

static void
ns_collect_svg_defs(const ns_node *n, GString *out, GHashTable *seen, int depth)
{
    if (!n || depth >= 512) return;
    if (n->kind == NS_NODE_ELEMENT && n->name) {
        if (strcmp(n->name, "symbol") == 0 ||
            strcmp(n->name, "linearGradient") == 0 ||
            strcmp(n->name, "radialGradient") == 0 ||
            strcmp(n->name, "clipPath") == 0 ||
            strcmp(n->name, "mask") == 0 ||
            strcmp(n->name, "filter") == 0 ||
            strcmp(n->name, "pattern") == 0) {
            const char *id = ns_element_get_attr(n, "id");
            if (id && *id && !g_hash_table_contains(seen, id)) {
                g_hash_table_add(seen, g_strdup(id));
                char *xml = ns_node_outer_html(n);
                if (xml) { g_string_append(out, xml); g_free(xml); }
            }
            return;
        }
    }
    for (const ns_node *c = n->first_child; c; c = c->next_sibling)
        ns_collect_svg_defs(c, out, seen, depth + 1);
}

static char *
svg_inject_namespaces(char *outer)
{
    if (!outer) return outer;
    const char *svg = strstr(outer, "<svg");
    if (!svg) return outer;
    const char *gt = strchr(svg, '>');
    if (!gt) return outer;
    char *tag = g_strndup(svg, (gsize)(gt - svg));
    GString *add = g_string_new(NULL);
    if (!strstr(tag, "xmlns="))
        g_string_append(add, " xmlns=\"http://www.w3.org/2000/svg\"");
    if (!strstr(tag, "xmlns:xlink"))
        g_string_append(add, " xmlns:xlink=\"http://www.w3.org/1999/xlink\"");
    g_free(tag);
    if (add->len == 0) { g_string_free(add, TRUE); return outer; }
    gsize ins_at = (gsize)(svg - outer) + 4;
    GString *r = g_string_new(NULL);
    g_string_append_len(r, outer, ins_at);
    g_string_append_len(r, add->str, add->len);
    g_string_append(r, outer + ins_at);
    g_free(outer);
    g_string_free(add, TRUE);
    return g_string_free(r, FALSE);
}

static char *
ns_svg_outer_with_defs(const ns_node *n)
{
    char *outer = svg_inject_namespaces(ns_node_outer_html(n));
    if (!outer || !*outer) return outer;
    const ns_node *root = ns_node_document_root(n);
    if (root == n) return outer;
    GString *defs = g_string_new(NULL);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    ns_collect_svg_defs(root, defs, seen, 0);
    g_hash_table_destroy(seen);
    if (defs->len == 0) {
        g_string_free(defs, TRUE);
        return outer;
    }
    const char *gt = strchr(outer, '>');
    if (!gt) { g_string_free(defs, TRUE); return outer; }
    gsize prefix_len = (gsize)(gt - outer + 1);
    GString *aug = g_string_new(NULL);
    g_string_append_len(aug, outer, prefix_len);
    g_string_append(aug, "<defs>");
    g_string_append_len(aug, defs->str, defs->len);
    g_string_append(aug, "</defs>");
    g_string_append(aug, outer + prefix_len);
    g_free(outer);
    g_string_free(defs, TRUE);
    return g_string_free(aug, FALSE);
}

static ns_box *
build_image_box(const ns_node *n)
{
    const ns_node *img = n;
    char *url = NULL;
    if (n->name && strcmp(n->name, "picture") == 0) {
        for (const ns_node *c = n->first_child; c; c = c->next_sibling) {
            if (ns_node_is_element_named(c, "img")) {
                img = c;
                break;
            }
        }
        char *source_url = pick_picture_source_url(n);
        if (source_url && !g_str_has_prefix(source_url, "data:")) {
            url = source_url;
        } else {
            if (img != n) url = pick_img_url(img);
            if (!url && source_url) {
                url = source_url;
            } else {
                g_free(source_url);
            }
        }
    } else {
        url = pick_img_url(n);
    }
    ns_box *box = box_new(NS_BOX_IMAGE);
    box->dom = img;
    ns_box_media *m = ns_box_media_ensure(box);
    m->image_src = url;
    const char *ws = ns_element_get_attr(img, "width");
    const char *hs = ns_element_get_attr(img, "height");
    box->content_width  = ws ? g_ascii_strtod(ws, NULL) : 0;
    box->content_height = hs ? g_ascii_strtod(hs, NULL) : 0;
    if (g_image_cache_for_layout) {
        char *abs = g_base_url_for_layout
            ? ns_url_resolve(g_base_url_for_layout, url)
            : NULL;
        m->image = ns_image_cache_peek(g_image_cache_for_layout,
                                       abs ? abs : url);
        g_free(abs);
    }
    return box;
}

static const char *
first_media_source_url(const ns_node *n)
{
    const char *src = ns_element_get_attr(n, "src");
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
    if (poster && *poster) m->video_poster = g_strdup(poster);
    const char *ws = ns_element_get_attr(n, "width");
    const char *hs = ns_element_get_attr(n, "height");
    box->content_width  = ws ? g_ascii_strtod(ws, NULL) : 320;
    box->content_height = hs ? g_ascii_strtod(hs, NULL) : 180;
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
    char *resolved = resolve_pseudo_content(cv->u.keyword, host);
    if (!resolved) return NULL;
    const char *txt = resolved;
    if (strcmp(txt, "open-quote") == 0)        txt = "\xe2\x80\x9c";
    else if (strcmp(txt, "close-quote") == 0)  txt = "\xe2\x80\x9d";
    else if (strcmp(txt, "no-open-quote") == 0 ||
             strcmp(txt, "no-close-quote") == 0) { g_free(resolved); return NULL; }

    ns_box *box = box_new_inline();
    box->text = g_strdup(txt);
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
    ns_box *contents_before = (s && s->before)
        ? build_pseudo_inline_for(s->before, n) : NULL;
    if (pending_before && *pending_before) {
        contents_before = inline_merge_prefix(*pending_before, contents_before);
        *pending_before = NULL;
    }
    if (contents_before)
        box_append_child(block, contents_before);

    const ns_node *c = n->first_child;
    while (c) {
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
            ns_box *item = box_new(NS_BOX_BLOCK);
            item->dom = c;
            item->style = cs;
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
                if (c->kind == NS_NODE_ELEMENT) {
                    const ns_style *cs = g_hash_table_lookup(styles, c);
                    if (style_is_contents(cs)) break;
                }
                if (!is_inline_dom(c, styles)) break;
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

    if (s && s->after) {
        ns_box *contents_after = build_pseudo_inline_for(s->after, n);
        if (contents_after) box_append_child(block, contents_after);
    }
    g_contents_depth--;
}

static ns_box *
build_block_impl(const ns_node *n, GHashTable *styles)
{
    if (!n) return NULL;
    if (n->kind == NS_NODE_DOCUMENT) {
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
        return root;
    }
    if (n->kind != NS_NODE_ELEMENT) return NULL;
    if (node_has_hidden_utility(n)) return NULL;
    if (ns_element_get_attr(n, NS_SHADOW_ATTR)) return NULL;

    const ns_style *s = g_hash_table_lookup(styles, n);
    if (s && style_is_none(s)) return NULL;
    if (s && style_is_absolute_or_fixed(s)) {
        if (!g_abs_force_build) {
            if (g_abs_pending) {
                ns_abs_entry e;
                e.dom = n;
                const ns_css_value *pv = s->values[NS_CSS_POSITION];
                e.fixed = pv && pv->kind == NS_CSS_V_KEYWORD && pv->u.keyword &&
                          strcmp(pv->u.keyword, "fixed") == 0;
                g_array_append_val(g_abs_pending, e);
            }
            return NULL;
        }
        g_abs_force_build = FALSE;
    }

    if (n->name && (strcmp(n->name, "img") == 0 ||
                    strcmp(n->name, "picture") == 0)) {
        ns_box *ib = build_image_box(n);
        if (ib) ib->style = s;
        return ib;
    }

    if (n->name && strcmp(n->name, "input") == 0) {
        if (s && (style_is_absolute_or_fixed(s) || style_is_block_level(s)))
            return build_form_control_block(n, s, styles);
        ns_box *ir = build_inline_run(n, n->next_sibling, styles);
        if (ir) return ir;
    }

    if (n->name && strcmp(n->name, "svg") == 0) {
        ns_box *box = box_new(NS_BOX_IMAGE);
        box->dom = n;
        box->style = s;
        ns_box_media *m = ns_box_media_ensure(box);
        const char *ws = ns_element_get_attr(n, "width");
        const char *hs = ns_element_get_attr(n, "height");
        box->content_width  = ws ? g_ascii_strtod(ws, NULL) : 0;
        box->content_height = hs ? g_ascii_strtod(hs, NULL) : 0;
        if (g_image_cache_for_layout) {
            char *xml = ns_svg_outer_with_defs(n);
            if (xml && *xml) {
                char *key = g_strdup_printf("nd-inline-svg:%p", (void *)n);
                m->image_src = key;
                m->image = ns_image_cache_peek(g_image_cache_for_layout, key);
                if (!m->image) {
                    int iw = 0, ih = 0;
                    ns_texture *tex = ns_image_decode_bytes(
                        (const guchar *)xml, strlen(xml), &iw, &ih);
                    if (tex) {
                        m->image = ns_image_cache_insert_loaded(
                            g_image_cache_for_layout, key, tex, iw, ih);
                        if (box->content_width  <= 0) box->content_width  = iw;
                        if (box->content_height <= 0) box->content_height = ih;
                    }
                } else {
                    const ns_image *img = m->image;
                    if (box->content_width  <= 0) box->content_width  = img->natural_width;
                    if (box->content_height <= 0) box->content_height = img->natural_height;
                }
            }
            g_free(xml);
        }
        return box;
    }

    if (n->name && strcmp(n->name, "audio") == 0) {
        ns_box *ab = build_audio_box(n);
        if (ab) ab->style = s;
        return ab;
    }

    if (n->name && strcmp(n->name, "video") == 0) {
        ns_box *vb = build_video_box(n);
        if (vb) vb->style = s;
        return vb;
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
        !style_is_absolute_or_fixed(s)) return NULL;

    ns_box *block = box_new(NS_BOX_BLOCK);
    block->dom = n;
    block->style = s;

    collect_box_bg_image(block, s);

    gboolean details_collapsed = FALSE;
    if (n->name && strcmp(n->name, "details") == 0 &&
        !ns_element_get_attr(n, "open"))
        details_collapsed = TRUE;

    ns_box *pending_before = (s && s->before)
        ? build_pseudo_inline_for(s->before, n) : NULL;

    gboolean blockify_children = style_is_flex_container(s) ||
                                 style_is_grid_container(s);

    const ns_node *c = n->first_child;
    while (c) {
        if (details_collapsed) {
            if (c->kind != NS_NODE_ELEMENT || !c->name ||
                strcmp(c->name, "summary") != 0) {
                c = c->next_sibling;
                continue;
            }
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
            ns_box *item = box_new(NS_BOX_BLOCK);
            item->dom = c;
            item->style = cs;
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
        if (c->kind == NS_NODE_ELEMENT) {
            const ns_style *cs = g_hash_table_lookup(styles, c);
            if (style_is_contents(cs)) {
                append_display_contents_children(block, c, styles, FALSE,
                                                 &pending_before);
                c = c->next_sibling;
                continue;
            }
        }
        if (is_inline_dom(c, styles)) {
            const ns_node *start = c;
            c = c->next_sibling;
            while (c) {
                if (details_collapsed &&
                    (c->kind != NS_NODE_ELEMENT || !c->name ||
                     strcmp(c->name, "summary") != 0)) break;
                if (c->kind == NS_NODE_ELEMENT) {
                    const ns_style *cs = g_hash_table_lookup(styles, c);
                    if (style_is_contents(cs)) break;
                }
                if (!is_inline_dom(c, styles)) break;
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

    if (pending_before) {
        box_append_child(block, pending_before);
        pending_before = NULL;
    }

    if (s && s->after) {
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
                ns_box_free(gen);
            } else {
                box_append_child(block, gen);
            }
        }
    }
    return block;
}

static PangoLayout *
make_pango_layout(const ns_style *parent_style)
{
    static PangoContext *cached_ctx;
    if (!cached_ctx) {
        PangoFontMap *fm = pango_cairo_font_map_get_default();
        cached_ctx = pango_font_map_create_context(fm);
    }
    PangoLayout *layout = pango_layout_new(cached_ctx);
    ns_paint_apply_inline_font(layout, parent_style);
    return layout;
}

static void
apply_inline_spacing(PangoAttrList *list, const ns_style *style, const char *text)
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
        PangoAttribute *ls = pango_attr_letter_spacing_new(
            (int)(ls_px * PANGO_SCALE));
        ls->start_index = 0;
        ls->end_index = G_MAXUINT;
        pango_attr_list_insert(list, ls);
    }
    if (ws_px != 0) {
        int per_space = (int)((ls_px + ws_px) * PANGO_SCALE);
        for (const char *p = text; *p; p++) {
            if (*p == ' ') {
                gsize idx = (gsize)(p - text);
                PangoAttribute *a = pango_attr_letter_spacing_new(per_space);
                a->start_index = (guint)idx;
                a->end_index = (guint)(idx + 1);
                pango_attr_list_insert(list, a);
            }
        }
    }
}

static PangoWeight
layout_pango_weight_from_css(int weight)
{
    if (weight <= 100) return PANGO_WEIGHT_THIN;
    if (weight <= 200) return PANGO_WEIGHT_ULTRALIGHT;
    if (weight <= 300) return PANGO_WEIGHT_LIGHT;
    if (weight <= 400) return PANGO_WEIGHT_NORMAL;
    if (weight <= 500) return PANGO_WEIGHT_MEDIUM;
    if (weight <= 600) return PANGO_WEIGHT_SEMIBOLD;
    if (weight <= 700) return PANGO_WEIGHT_BOLD;
    if (weight <= 800) return PANGO_WEIGHT_ULTRABOLD;
    if (weight <= 900) return PANGO_WEIGHT_HEAVY;
    return (PangoWeight)weight;
}

static void
layout_attr_insert_range(PangoAttrList *attrs, PangoAttribute *a,
                         gsize start, gsize len)
{
    if (!a || len == 0) return;
    a->start_index = (guint)start;
    a->end_index = (guint)(start + len);
    pango_attr_list_insert(attrs, a);
}

static void
apply_inline_layout_attrs(PangoAttrList *attrs, const ns_box *box)
{
    if (!attrs || !box || !box->attrs) return;
    for (gint ii = (gint)box->attrs->len - 1; ii >= 0; ii--) {
        const ns_inline_attr *r =
            &g_array_index(box->attrs, ns_inline_attr, (guint)ii);
        PangoAttribute *a = NULL;
        switch (r->kind) {
        case NS_INLINE_BOLD:
            a = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
            break;
        case NS_INLINE_FONT_WEIGHT:
            a = pango_attr_weight_new(layout_pango_weight_from_css(r->font_weight));
            break;
        case NS_INLINE_ITALIC:
            a = pango_attr_style_new(PANGO_STYLE_ITALIC);
            break;
        case NS_INLINE_MONOSPACE:
            a = pango_attr_family_new("monospace");
            break;
        case NS_INLINE_INPUT_FIELD:
        case NS_INLINE_INPUT_FIELD_FOCUSED:
        case NS_INLINE_BUTTON:
            if (!(r->dom && r->dom->name &&
                  strcmp(r->dom->name, "textarea") == 0))
                layout_attr_insert_range(attrs,
                    pango_attr_allow_breaks_new(FALSE), r->start, r->len);
            break;
        case NS_INLINE_FONT_SIZE:
            a = pango_attr_size_new_absolute(
                (int)(r->font_size_px * PANGO_SCALE));
            break;
        case NS_INLINE_FONT_FAMILY:
            if (r->family) {
                char *pango_family = ns_css_font_family_for_pango(r->family);
                a = pango_attr_family_new(pango_family);
                g_free(pango_family);
            }
            break;
        case NS_INLINE_SUPERSCRIPT:
            layout_attr_insert_range(attrs, pango_attr_rise_new(4000),
                                     r->start, r->len);
            a = pango_attr_scale_new(0.75);
            break;
        case NS_INLINE_SUBSCRIPT:
            layout_attr_insert_range(attrs, pango_attr_rise_new(-3000),
                                     r->start, r->len);
            a = pango_attr_scale_new(0.75);
            break;
        case NS_INLINE_SMALL_CAPS:
            a = pango_attr_variant_new(PANGO_VARIANT_SMALL_CAPS);
            break;
        default:
            break;
        }
        layout_attr_insert_range(attrs, a, r->start, r->len);
    }
}

void
ns_inline_apply_atomic_shapes(PangoAttrList *list, const ns_box *box)
{
    if (!box || !box->inline_atomics) return;
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
        const char *va = ab->style
            ? ns_style_keyword(ab->style, NS_CSS_VERTICAL_ALIGN) : NULL;
        if (va) {
            if (strcmp(va, "middle") == 0)           top = -(h / 2 + xh / 2);
            else if (strcmp(va, "text-top") == 0)    top = -asc;
            else if (strcmp(va, "top") == 0)         top = -asc;
            else if (strcmp(va, "text-bottom") == 0) top = desc - h;
            else if (strcmp(va, "bottom") == 0)      top = desc - h;
            else if (strcmp(va, "super") == 0)       top = -h - fs * 0.3;
            else if (strcmp(va, "sub") == 0)         top = -h + fs * 0.2;
        }
        PangoRectangle r = { 0, (int)(top * PANGO_SCALE),
                             (int)(w * PANGO_SCALE), (int)(h * PANGO_SCALE) };
        PangoAttribute *attr = pango_attr_shape_new(&r, &r);
        attr->start_index = (guint)a->byte_off;
        attr->end_index   = (guint)(a->byte_off + 3);
        pango_attr_list_insert(list, attr);
    }
}

static double
inline_line_height(const ns_style *parent_style)
{
    double font_size = length_or(parent_style ? parent_style->values[NS_CSS_FONT_SIZE] : NULL, 16);
    const ns_css_value *lh = parent_style ? parent_style->values[NS_CSS_LINE_HEIGHT] : NULL;
    if (lh && lh->kind == NS_CSS_V_LENGTH) {
        if (lh->u.length.unit == NS_CSS_UNIT_PX)
            return lh->u.length.v;
        if (lh->u.length.unit == NS_CSS_UNIT_NUMBER)
            return lh->u.length.v * font_size;
        if (lh->u.length.unit == NS_CSS_UNIT_EM)
            return lh->u.length.v * font_size;
        if (lh->u.length.unit == NS_CSS_UNIT_PERCENT)
            return lh->u.length.v / 100.0 * font_size;
    }
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

typedef struct {
    double height;
    int    lines;
} ns_inline_measure;

static GHashTable *g_inline_measure_cache;
static GHashTable *g_inline_width_cache;
static guint       g_inline_measure_serial;

static void
inline_measure_cache_validate(void)
{
    PangoFontMap *fm = pango_cairo_font_map_get_default();
    guint serial = fm ? pango_font_map_get_serial(fm) : 0;
    if (!g_inline_measure_cache) {
        g_inline_measure_cache =
            g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        g_inline_width_cache =
            g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        g_inline_measure_serial = serial;
        return;
    }
    if (serial != g_inline_measure_serial ||
        g_hash_table_size(g_inline_measure_cache) > 262144 ||
        g_hash_table_size(g_inline_width_cache) > 262144) {
        g_hash_table_remove_all(g_inline_measure_cache);
        g_hash_table_remove_all(g_inline_width_cache);
        g_inline_measure_serial = serial;
    }
}

static const char *
inline_measure_nearest_attr(const ns_node *n, const char *name)
{
    for (; n; n = n->parent) {
        if (n->kind != NS_NODE_ELEMENT) continue;
        const char *v = ns_element_get_attr(n, name);
        if (v) return v;
    }
    return NULL;
}

static char *
inline_measure_key(const ns_box *box, const ns_style *style, PangoLayout *layout)
{
    inline_measure_cache_validate();
    const PangoFontDescription *desc = pango_layout_get_font_description(layout);
    char *fd = desc ? pango_font_description_to_string(desc) : g_strdup("");
    double ls_px = 0, ws_px = 0;
    const ns_css_value *lv = style ? style->values[NS_CSS_LETTER_SPACING] : NULL;
    if (lv && lv->kind == NS_CSS_V_LENGTH && lv->u.length.unit == NS_CSS_UNIT_PX)
        ls_px = lv->u.length.v;
    const ns_css_value *wv = style ? style->values[NS_CSS_WORD_SPACING] : NULL;
    if (wv && wv->kind == NS_CSS_V_LENGTH && wv->u.length.unit == NS_CSS_UNIT_PX)
        ws_px = wv->u.length.v;
    const ns_node *dn = box->dom;
    for (const ns_box *p = box->parent; !dn && p; p = p->parent) dn = p->dom;
    const char *lang = inline_measure_nearest_attr(dn, "lang");
    const char *dir  = inline_measure_nearest_attr(dn, "dir");
    int bd = 0;
    if (dir) {
        if (g_ascii_strcasecmp(dir, "rtl") == 0) bd = 2;
        else if (g_ascii_strcasecmp(dir, "ltr") == 0) bd = 1;
    }
    if (!bd && style && keyword_is(style->values[NS_CSS_DIRECTION], "rtl"))
        bd = 2;
    if (!bd)
        bd = pango_context_get_base_dir(pango_layout_get_context(layout)) ==
             PANGO_DIRECTION_RTL ? 4 : 3;
    char *key = g_strdup_printf("%d|%d|%d|%d|%d|%.3f|%.3f|%d|%s|%s|%s",
                                pango_layout_get_width(layout),
                                (int)pango_layout_get_wrap(layout),
                                (int)pango_layout_get_ellipsize(layout),
                                pango_layout_get_height(layout),
                                pango_layout_get_indent(layout),
                                ls_px, ws_px, bd, fd,
                                lang ? lang : "",
                                box->text);
    g_free(fd);
    return key;
}

static void
inline_layout(ns_box *box, double content_width, const ns_style *parent_style)
{
    g_assert(box->kind == NS_BOX_INLINE);
    if (!box->text || !*box->text) {
        box->content_width  = 0;
        box->content_height = 0;
        return;
    }

    if (box->inline_atomics) {
        for (guint i = 0; i < box->inline_atomics->len; i++) {
            ns_box *ab = g_array_index(box->inline_atomics, ns_inline_atomic, i).box;
            if (ab) layout_box(ab, content_width, parent_style);
        }
    }

    PangoLayout *layout = make_pango_layout(parent_style);
    gboolean ws_nowrap = keyword_is(
        parent_style ? parent_style->values[NS_CSS_WHITE_SPACE] : NULL, "nowrap") ||
        keyword_is(parent_style ? parent_style->values[NS_CSS_WHITE_SPACE] : NULL, "pre");
    gboolean ellip = keyword_is(
        parent_style ? parent_style->values[NS_CSS_TEXT_OVERFLOW] : NULL, "ellipsis");
    if (ws_nowrap && !ellip)
        pango_layout_set_width(layout, -1);
    else
        pango_layout_set_width(layout, (int)(content_width * PANGO_SCALE));
    pango_layout_set_wrap(layout, ns_paint_wrap_mode_for(parent_style));
    {
        double ti = ns_text_indent_px(parent_style, content_width);
        if (ti > 0) pango_layout_set_indent(layout, (int)(ti * PANGO_SCALE));
    }
    if (ellip)
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    {
        const ns_css_value *lc = parent_style ? parent_style->values[NS_CSS_LINE_CLAMP] : NULL;
        if (lc && lc->kind == NS_CSS_V_LENGTH && lc->u.length.v >= 1) {
            pango_layout_set_height(layout, -(int)lc->u.length.v);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        }
    }
    PangoAttrList *i18n = pango_attr_list_new();
    ns_paint_apply_i18n(layout, i18n, box);
    ns_inline_apply_atomic_shapes(i18n, box);
    apply_inline_spacing(i18n, parent_style, box->text);
    apply_inline_layout_attrs(i18n, box);
    pango_layout_set_attributes(layout, i18n);
    pango_attr_list_unref(i18n);

    gboolean cacheable =
        (!box->inline_atomics || box->inline_atomics->len == 0) &&
        (!box->attrs || box->attrs->len == 0);
    char *mkey = cacheable
        ? inline_measure_key(box, parent_style, layout) : NULL;
    const ns_inline_measure *cached =
        mkey ? g_hash_table_lookup(g_inline_measure_cache, mkey) : NULL;
    double measured;
    int line_count;
    if (cached) {
        measured   = cached->height;
        line_count = cached->lines;
        g_free(mkey);
    } else {
        pango_layout_set_text(layout, box->text, -1);
        int pw, ph;
        pango_layout_get_pixel_size(layout, &pw, &ph);
        line_count = pango_layout_get_line_count(layout);
        measured = ph;
        if (mkey) {
            ns_inline_measure *e = g_new(ns_inline_measure, 1);
            e->height = measured;
            e->lines  = line_count;
            g_hash_table_insert(g_inline_measure_cache, mkey, e);
        }
    }
    if (line_count < 1) line_count = 1;
    double lh_default = inline_line_height(parent_style);
    double lh_control = inline_control_line_height(box, lh_default);
    double expected = line_count * lh_control;
    box->content_width  = content_width;
    const ns_css_value *lhv = parent_style
        ? parent_style->values[NS_CSS_LINE_HEIGHT] : NULL;
    gboolean lh_explicit = lhv && lhv->kind == NS_CSS_V_LENGTH;
    gboolean has_atomic = box->inline_atomics && box->inline_atomics->len > 0;
    if (lh_explicit && !has_atomic)
        box->content_height = expected;
    else
        box->content_height = measured > expected ? measured : expected;
    double ta_h = inline_textarea_total_height(box);
    if (ta_h > box->content_height) box->content_height = ta_h;

    g_object_unref(layout);
}

static gboolean style_blocks_hit_testing(const ns_style *s);
static gboolean node_is_form_hit_target(const ns_node *n);
static const ns_node *ns_form_hit_walk(const ns_box *box, double x, double y,
                                       const ns_style *inherited);

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
    PangoLayout *layout = make_pango_layout(parent_style);
    pango_layout_set_width(layout, (int)(box->content_width * PANGO_SCALE));
    pango_layout_set_wrap(layout, ns_paint_wrap_mode_for(parent_style));
    {
        double ti = ns_text_indent_px(parent_style, box->content_width);
        if (ti > 0) pango_layout_set_indent(layout, (int)(ti * PANGO_SCALE));
    }
    if (keyword_is(parent_style ? parent_style->values[NS_CSS_TEXT_OVERFLOW] : NULL,
                   "ellipsis"))
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_text(layout, box->text, -1);
    PangoAttrList *i18n = pango_attr_list_new();
    ns_paint_apply_i18n(layout, i18n, box);
    ns_inline_apply_atomic_shapes(i18n, box);
    apply_inline_spacing(i18n, parent_style, box->text);
    apply_inline_layout_attrs(i18n, box);
    pango_layout_set_attributes(layout, i18n);
    pango_attr_list_unref(i18n);
    const ns_css_value *ta_v =
        parent_style ? parent_style->values[NS_CSS_TEXT_ALIGN] : NULL;
    gboolean rtl = pango_context_get_base_dir(
        pango_layout_get_context(layout)) == PANGO_DIRECTION_RTL;
    if (keyword_is(ta_v, "center"))
        pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    else if (keyword_is(ta_v, "right") ||
             (keyword_is(ta_v, "end") && !rtl) ||
             (keyword_is(ta_v, "start") && rtl) ||
             (!ta_v && rtl))
        pango_layout_set_alignment(layout, PANGO_ALIGN_RIGHT);
    else if (keyword_is(ta_v, "justify"))
        pango_layout_set_justify(layout, TRUE);
    else
        pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);

    int index = 0, trailing = 0;
    gboolean inside = pango_layout_xy_to_index(
        layout,
        (int)(local_x * PANGO_SCALE),
        (int)(local_y * PANGO_SCALE),
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
            PangoRectangle r0, r1;
            pango_layout_index_to_pos(layout, (int)r->start, &r0);
            pango_layout_index_to_pos(layout, (int)(r->start + r->len - 1), &r1);
            double x0 = (double)r0.x / PANGO_SCALE - 10;
            double y0 = (double)r0.y / PANGO_SCALE - 5;
            double x1 = (double)(r1.x + r1.width) / PANGO_SCALE + 10;
            double y1 = (double)(r0.y + r0.height) / PANGO_SCALE + 5;
            double css_w = inline_attr_control_width(r, box);
            if (css_w > 0) {
                x0 = (double)r0.x / PANGO_SCALE;
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
    if (box->inline_atomics) {
        for (guint i = 0; i < box->inline_atomics->len; i++) {
            const ns_inline_atomic *a =
                &g_array_index(box->inline_atomics, ns_inline_atomic, i);
            if (!a->box) continue;
            PangoRectangle pos;
            pango_layout_index_to_pos(layout, (int)a->byte_off, &pos);
            double ax = (double)pos.x / PANGO_SCALE;
            double ay = (double)pos.y / PANGO_SCALE;
            const ns_node *m = ns_form_hit_walk(a->box,
                                                local_x - ax,
                                                local_y - ay,
                                                parent_style);
            if (m) atomic_hit = m;
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

static PangoLayout *
inline_box_layout_for_multicol(const ns_box *box, double content_width,
                               const ns_style *parent_style)
{
    PangoLayout *layout = make_pango_layout(parent_style);
    pango_layout_set_width(layout, (int)(content_width * PANGO_SCALE));
    pango_layout_set_wrap(layout, ns_paint_wrap_mode_for(parent_style));
    pango_layout_set_text(layout, box->text, -1);

    PangoAttrList *i18n = pango_attr_list_new();
    ns_paint_apply_i18n(layout, i18n, box);
    apply_inline_spacing(i18n, parent_style, box->text);
    apply_inline_layout_attrs(i18n, box);
    pango_layout_set_attributes(layout, i18n);
    pango_attr_list_unref(i18n);
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

    PangoLayout *layout = inline_box_layout_for_multicol(src, col_w,
                                                         child_inherited);
    int line_count = pango_layout_get_line_count(layout);
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
        PangoLayoutLine *first =
            pango_layout_get_line_readonly(layout, first_line);
        PangoLayoutLine *after =
            after_line < line_count
            ? pango_layout_get_line_readonly(layout, after_line) : NULL;
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
    const char *v = ns_style_keyword(s, axis);
    if (!v) v = ns_style_keyword(s, NS_CSS_OVERFLOW);
    return v;
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

static const ns_node *
ns_form_hit_walk(const ns_box *box, double x, double y,
                 const ns_style *inherited)
{
    if (!box) return NULL;
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
    return best ? best : self_hit;
}

static double
measure_natural_width(ns_box *box, const ns_style *parent_style);
static double
measure_min_width(ns_box *box, const ns_style *parent_style);

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

    double w = -1, h = -1;
    if (wv && (wv->kind == NS_CSS_V_LENGTH || wv->kind == NS_CSS_V_CALC))
        w = length_resolve(wv, parent_content_width, -1);
    if (hv && (hv->kind == NS_CSS_V_LENGTH || hv->kind == NS_CSS_V_CALC)) {
        if (height_is_percent(hv)) {
            double cb_h = containing_block_definite_height(box);
            if (cb_h >= 0) {
                h = (hv->kind == NS_CSS_V_CALC)
                    ? hv->u.calc.pct / 100.0 * cb_h + hv->u.calc.px
                    : hv->u.length.v * cb_h / 100.0;
            }
        } else {
            h = resolve_used_height(box, hv, parent_content_width, -1);
        }
    }

    const ns_image *img = box->media ? (const ns_image *)box->media->image : NULL;
    double nat_w = (img && img->loaded && img->natural_width > 0)
                   ? (double)img->natural_width  : -1;
    double nat_h = (img && img->loaded && img->natural_height > 0)
                   ? (double)img->natural_height : -1;
    if (box->media)
        box->media->size_independent_of_image = (w >= 0 && h >= 0);

    if (nat_w < 0 && box->content_width  > 0) nat_w = box->content_width;
    if (nat_h < 0 && box->content_height > 0) nat_h = box->content_height;

    if (w < 0 && h < 0) {
        if (nat_w > 0 && nat_h > 0) { w = nat_w; h = nat_h; }
        else { w = 0; h = 0; }
    } else if (w < 0) {
        w = (nat_w > 0 && nat_h > 0) ? h * (nat_w / nat_h) : h;
    } else if (h < 0) {
        h = (nat_w > 0 && nat_h > 0) ? w * (nat_h / nat_w) : w;
    }

    double max_w = length_resolve(mxw, parent_content_width, -1);
    double max_h = resolve_used_height(box, mxh, parent_content_width, -1);
    double min_w = length_resolve(mnw, parent_content_width, -1);
    double min_h = resolve_used_height(box, mnh, parent_content_width, -1);

    if (max_w >= 0 && w > max_w) {
        if (h > 0 && w > 0) h *= max_w / w;
        w = max_w;
    }
    if (max_h >= 0 && h > max_h) {
        if (w > 0 && h > 0) w *= max_h / h;
        h = max_h;
    }
    if (min_w >= 0 && w < min_w) w = min_w;
    if (min_h >= 0 && h < min_h) h = min_h;

    if (w > parent_content_width && parent_content_width > 0) {
        double ratio = (w > 0) ? h / w : 0;
        w = parent_content_width;
        h = w * ratio;
    }

    box->content_width = w;
    box->content_height = h;
}

static double
inline_atomic_measure_basis(const ns_box *box)
{
    double basis = ns_css_container_w();
    if (!(basis > 0)) {
        for (const ns_box *p = box; p; p = p->parent) {
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
measure_natural_width(ns_box *box, const ns_style *parent_style)
{
    if (!box) return 0;
    if (box->kind == NS_BOX_INLINE) {
        if (!box->text || !*box->text) return 0;
        PangoLayout *layout = make_pango_layout(parent_style);
        pango_layout_set_width(layout, -1);
        gboolean cacheable =
            (!box->inline_atomics || box->inline_atomics->len == 0) &&
            (!box->attrs || box->attrs->len == 0);
        char *mkey = cacheable
            ? inline_measure_key(box, parent_style, layout) : NULL;
        const double *hit =
            mkey ? g_hash_table_lookup(g_inline_width_cache, mkey) : NULL;
        if (hit) {
            g_free(mkey);
            g_object_unref(layout);
            return *hit;
        }
        if (box->inline_atomics) {
            for (guint ai = 0; ai < box->inline_atomics->len; ai++) {
                ns_box *ab = g_array_index(box->inline_atomics, ns_inline_atomic, ai).box;
                if (ab && ab->content_width == 0 && ab->content_height == 0)
                    layout_box(ab, inline_atomic_measure_basis(ab), parent_style);
            }
        }
        pango_layout_set_text(layout, box->text, -1);
        PangoAttrList *i18n = pango_attr_list_new();
        ns_paint_apply_i18n(layout, i18n, box);
        ns_inline_apply_atomic_shapes(i18n, box);
        apply_inline_spacing(i18n, parent_style, box->text);
        apply_inline_layout_attrs(i18n, box);
        pango_layout_set_attributes(layout, i18n);
        pango_attr_list_unref(i18n);
        int pw, ph;
        pango_layout_get_pixel_size(layout, &pw, &ph);
        g_object_unref(layout);
        if (mkey) {
            double *e = g_new(double, 1);
            *e = pw;
            g_hash_table_insert(g_inline_width_cache, mkey, e);
        }
        return pw;
    }
    if (box->kind == NS_BOX_IMAGE || box->kind == NS_BOX_VIDEO) {
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
            wv->u.length.v > 0)
            return wv->u.length.v;
    }
    const ns_style *child_style = box->style ? box->style : parent_style;
    const ns_css_value *disp = box->style ? box->style->values[NS_CSS_DISPLAY] : NULL;
    gboolean flex_row = (keyword_is(disp, "flex") || keyword_is(disp, "inline-flex")) &&
        !keyword_is(box->style ? box->style->values[NS_CSS_FLEX_DIRECTION] : NULL, "column") &&
        !keyword_is(box->style ? box->style->values[NS_CSS_FLEX_DIRECTION] : NULL, "column-reverse");
    double max_child = 0;
    double float_row = 0;
    double row_sum = 0;
    int flex_items = 0;
    for (ns_box *c = box->first_child; c; c = c->next_sibling) {
        double w = measure_natural_width(c, child_style);
        int fside = float_side_of(c->style);
        double cw_used = w;
        if (fside >= 0 && cw_used < 60) cw_used = 60;
        double outer = cw_used;
        if (c->style) {
            ns_edges m = {0}, pd = {0}, bd = {0};
            edges_from_style(c->style, 0, &m, &pd, &bd);
            outer += m.left + m.right + pd.left + pd.right + bd.left + bd.right;
        }
        if (flex_row) {
            row_sum += outer;
            flex_items++;
        } else if (box->kind == NS_BOX_BLOCK) {
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
        PangoLayout *layout = make_pango_layout(parent_style);
        pango_layout_set_width(layout, 1);
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD);
        gboolean cacheable =
            (!box->inline_atomics || box->inline_atomics->len == 0) &&
            (!box->attrs || box->attrs->len == 0);
        char *mkey = cacheable
            ? inline_measure_key(box, parent_style, layout) : NULL;
        const double *hit =
            mkey ? g_hash_table_lookup(g_inline_width_cache, mkey) : NULL;
        if (hit) {
            g_free(mkey);
            g_object_unref(layout);
            return *hit;
        }
        if (box->inline_atomics) {
            for (guint ai = 0; ai < box->inline_atomics->len; ai++) {
                ns_box *ab = g_array_index(box->inline_atomics, ns_inline_atomic, ai).box;
                if (ab && ab->content_width == 0 && ab->content_height == 0)
                    layout_box(ab, inline_atomic_measure_basis(ab), parent_style);
            }
        }
        pango_layout_set_text(layout, box->text, -1);
        PangoAttrList *i18n = pango_attr_list_new();
        ns_paint_apply_i18n(layout, i18n, box);
        ns_inline_apply_atomic_shapes(i18n, box);
        apply_inline_spacing(i18n, parent_style, box->text);
        apply_inline_layout_attrs(i18n, box);
        pango_layout_set_attributes(layout, i18n);
        pango_attr_list_unref(i18n);
        int pw, ph;
        pango_layout_get_pixel_size(layout, &pw, &ph);
        g_object_unref(layout);
        if (mkey) {
            double *e = g_new(double, 1);
            *e = pw;
            g_hash_table_insert(g_inline_width_cache, mkey, e);
        }
        return pw;
    }
    if (box->kind == NS_BOX_IMAGE || box->kind == NS_BOX_VIDEO)
        return box->content_width > 0 ? box->content_width : 200;
    if (box->kind == NS_BOX_TEXT)
        return box->content_width > 0 ? box->content_width : 0;
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

static void shift_box_tree(ns_box *b, double dx, double dy);

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
    double cw;
    if (explicit_width) {
        cw = length_resolve(wv, parent_content_width, 0);
    } else {
        cw = parent_content_width - horiz_total;
    }
    const ns_css_value *mxw = box->style ? box->style->values[NS_CSS_MAX_WIDTH] : NULL;
    if (mxw && (mxw->kind == NS_CSS_V_LENGTH || mxw->kind == NS_CSS_V_CALC)) {
        double m = length_resolve(mxw, parent_content_width, -1);
        if (m >= 0 && cw > m) cw = m;
    }
    const ns_css_value *mnw = box->style ? box->style->values[NS_CSS_MIN_WIDTH] : NULL;
    if (mnw && (mnw->kind == NS_CSS_V_LENGTH || mnw->kind == NS_CSS_V_CALC)) {
        double m = length_resolve(mnw, parent_content_width, -1);
        if (m >= 0 && cw < m) cw = m;
    }
    if (cw < 0) cw = 0;
    box->content_width = cw;

    guint max_cols = 0;
    for (ns_box *row = box->first_child; row; row = row->next_sibling) {
        if (row->kind != NS_BOX_TABLE_ROW) continue;
        guint c = 0;
        for (ns_box *cell = row->first_child; cell; cell = cell->next_sibling)
            c += cell->colspan > 0 ? (guint)cell->colspan : 1;
        if (c > max_cols) max_cols = c;
    }
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
                double cell_outer = natural + h_extra;
                double cell_min_outer = measure_min_width(cell, cs) + h_extra;
                gboolean cell_fixed = FALSE;
                double cell_explicit = -1;
                if (cell->style && cell->style->values[NS_CSS_WIDTH]) {
                    const ns_css_value *cwv = cell->style->values[NS_CSS_WIDTH];
                    if (cwv->kind == NS_CSS_V_LENGTH || cwv->kind == NS_CSS_V_CALC) {
                        cell_fixed = TRUE;
                        double w = length_resolve(cwv, col_avail > 0 ? col_avail : 0, -1);
                        if (w >= 0) cell_explicit = w + h_extra;
                    }
                }
                double per_col = cell_outer / (double)span;
                double per_col_min = cell_min_outer / (double)span;
                for (int i = 0; i < span && col + (guint)i < max_cols; i++) {
                    if (per_col > col_widths[col + i])
                        col_widths[col + i] = per_col;
                    if (per_col_min > col_min[col + i])
                        col_min[col + i] = per_col_min;
                    if (cell_fixed && span == 1) col_fixed[col + i] = TRUE;
                    if (cell_explicit >= 0 && span == 1 &&
                        cell_explicit > col_explicit[col + i])
                        col_explicit[col + i] = cell_explicit;
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
                    double per = w / (double)hspan;
                    for (int k = 0; k < hspan && col + (guint)k < max_cols; k++)
                        if (per > col_explicit[col + k]) col_explicit[col + k] = per;
                }
                col += (guint)hspan;
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
                        col_widths[i] = col_avail * col_min[i] / min_sum;
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
            double sub_y = iy;
            for (ns_box *child = cell->first_child; child; child = child->next_sibling) {
                child->x = ix;
                child->y = sub_y;
                layout_box(child, cell_inner_w, cs);
                double dh = child->content_height;
                if (child->kind == NS_BOX_BLOCK || child->kind == NS_BOX_TABLE)
                    dh += child->margin.top + child->margin.bottom +
                          child->padding.top + child->padding.bottom +
                          child->border.top + child->border.bottom;
                sub_y += dh;
            }
            double cell_h = sub_y - iy;
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

static void
cq_set_dims_from_ancestors(const ns_box *box)
{
    for (const ns_box *a = box->parent; a; a = a->parent) {
        if (box_is_query_container(a)) {
            ns_css_set_container_dims(a->content_width, a->content_height);
            return;
        }
    }
    ns_css_set_container_dims(0, 0);
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
    } else if (box->kind == NS_BOX_TABLE) {
        layout_table(box, parent_content_width, inherited_style);
    } else if (box->kind == NS_BOX_TABLE_CAPTION) {
        layout_block(box, parent_content_width, inherited_style);
    } else {
        box->content_width = parent_content_width;
        box->content_height = 0;
    }
}

static gboolean
flex_main_basis_explicit(const ns_box *c, double cw, double *out)
{
    const ns_style *s = c->style;
    if (!s) return FALSE;
    const ns_css_value *b = s->values[NS_CSS_FLEX_BASIS];
    if (b && (b->kind == NS_CSS_V_LENGTH || b->kind == NS_CSS_V_CALC)) {
        *out = length_resolve(b, cw, 0);
        return TRUE;
    }
    const ns_css_value *w = s->values[NS_CSS_WIDTH];
    if (w && (w->kind == NS_CSS_V_LENGTH || w->kind == NS_CSS_V_CALC)) {
        *out = length_resolve(w, cw, 0);
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
    double w = 0;
    if (b->kind == NS_BOX_INLINE && b->text) {
        double chars = (double)g_utf8_strlen(b->text, -1);
        w = chars * font_size * 0.65 + font_size * 0.5;
    } else if (b->kind == NS_BOX_IMAGE || b->kind == NS_BOX_VIDEO) {
        w = b->content_width > 0 ? b->content_width : 0;
    } else {
        for (const ns_box *c = b->first_child; c; c = c->next_sibling) {
            if (c->style && style_is_absolute_or_fixed(c->style)) continue;
            double cw_child = estimate_natural_width(c, cap);
            int fside = float_side_of(c->style);
            if (fside >= 0 && cw_child < 60) cw_child = 60;
            if (c->style) {
                ns_edges m = {0}, pd = {0}, bd = {0};
                edges_from_style(c->style, 0, &m, &pd, &bd);
                cw_child += m.left + m.right;
            }
            w += cw_child;
        }
    }
    w += b->padding.left + b->padding.right +
         b->border.left  + b->border.right;
    if (w > cap) w = cap;
    return w;
}

static double
flex_item_box_extras(const ns_box *b)
{
    if (!b) return 0;
    return b->padding.left + b->padding.right +
           b->border.left  + b->border.right;
}

static double
flex_content_basis_from_natural(const ns_box *b, double cap)
{
    double w = estimate_natural_width(b, cap);
    double extras = flex_item_box_extras(b);
    return w > extras ? w - extras : 0;
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
    for (ns_box *c = b->first_child; c; c = c->next_sibling)
        shift_box_tree(c, dx, dy);
}

static double
gap_px(const ns_css_value *specific, const ns_css_value *shorthand, double basis)
{
    const ns_css_value *v =
        (specific && specific->kind == NS_CSS_V_LENGTH) ? specific :
        (shorthand && shorthand->kind == NS_CSS_V_LENGTH) ? shorthand : NULL;
    if (!v) return 0;
    if (v->u.length.unit == NS_CSS_UNIT_PERCENT)
        return basis > 0 ? v->u.length.v * basis / 100.0 : 0;
    return v->u.length.v;
}

static double
flex_gap_of(const ns_style *s, double basis)
{
    if (!s) return 0;
    return gap_px(s->values[NS_CSS_COLUMN_GAP], s->values[NS_CSS_GAP], basis);
}

static double
flex_gap_row_of(const ns_style *s)
{
    if (!s) return 0;
    double g = number_or(s->values[NS_CSS_ROW_GAP], -1);
    if (g >= 0) return g;
    return number_or(s->values[NS_CSS_GAP], 0);
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
flex_basis_main_height(const ns_box *c, double cw, gboolean *out_explicit)
{
    *out_explicit = FALSE;
    const ns_style *s = c->style;
    if (!s) return 0;
    const ns_css_value *b = s->values[NS_CSS_FLEX_BASIS];
    if (b && (b->kind == NS_CSS_V_LENGTH || b->kind == NS_CSS_V_CALC)) {
        *out_explicit = TRUE;
        return length_resolve(b, cw, 0);
    }
    const ns_css_value *h = s->values[NS_CSS_HEIGHT];
    if (h && (h->kind == NS_CSS_V_LENGTH || h->kind == NS_CSS_V_CALC) &&
        !height_is_percent(h)) {
        *out_explicit = TRUE;
        return length_resolve(h, cw, 0);
    }
    return 0;
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
    double explicit_cross = 0;
    if (hv_box && (hv_box->kind == NS_CSS_V_LENGTH || hv_box->kind == NS_CSS_V_CALC))
        explicit_cross = resolve_used_height(box, hv_box, parent_content_width, 0);
    double min_cross = resolve_used_height(box, mnh_box, parent_content_width, -1);
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
    }
    if (min_cross > explicit_cross) explicit_cross = min_cross;

    GPtrArray *items = g_ptr_array_new();
    for (ns_box *c = box->first_child; c; c = c->next_sibling)
        g_ptr_array_add(items, c);

    double gap = flex_gap_of(box->style, cw);

    double total_extras = 0;
    double total_explicit = 0;
    double total_grow = 0;
    int    implicit_count = 0;
    GArray *basis = g_array_new(FALSE, FALSE, sizeof(double));
    GArray *explicit_flags = g_array_new(FALSE, FALSE, sizeof(gboolean));
    for (guint i = 0; i < items->len; i++) {
        ns_box *c = items->pdata[i];
        edges_from_style(c->style, cw,
                         &c->margin, &c->padding, &c->border);
        double extras = c->margin.left + c->margin.right +
                        c->padding.left + c->padding.right +
                        c->border.left + c->border.right;
        total_extras += extras;
        total_grow   += flex_grow_of(c);
        double b = 0;
        gboolean exp_flag = flex_main_basis_explicit(c, cw, &b);
        if (!exp_flag) b = flex_content_basis_from_natural(c, cw);
        g_array_append_val(basis, b);
        g_array_append_val(explicit_flags, exp_flag);
        if (exp_flag) total_explicit += b;
        else          implicit_count++;
    }

    if (items->len > 1) total_extras += gap * (items->len - 1);

    double total_basis = total_explicit;
    for (guint i = 0; i < items->len; i++) {
        gboolean exp_flag = g_array_index(explicit_flags, gboolean, i);
        if (!exp_flag) total_basis += g_array_index(basis, double, i);
    }
    double available = cw - total_extras;
    if (available < total_basis && total_basis > 0) {
        double negative_free = total_basis - available;
        double total_scaled = 0;
        for (guint i = 0; i < items->len; i++)
            total_scaled += flex_shrink_of(items->pdata[i]) *
                            g_array_index(basis, double, i);
        if (total_scaled > 0) {
            double new_total = 0;
            for (guint i = 0; i < items->len; i++) {
                double bi = g_array_index(basis, double, i);
                double scaled = flex_shrink_of(items->pdata[i]) * bi;
                double shrunk = bi - negative_free * (scaled / total_scaled);
                if (shrunk < 0) shrunk = 0;
                g_array_index(basis, double, i) = shrunk;
                new_total += shrunk;
            }
            total_basis = new_total;
        }
    }
    double remaining_free = cw - total_extras - total_basis;
    if (remaining_free < 0) remaining_free = 0;
    double extra_per_grow = (total_grow > 0) ? (remaining_free / total_grow) : 0;
    (void)implicit_count;

    const char *justify = keyword_or(box->style, NS_CSS_JUSTIFY_CONTENT, "flex-start");
    double leading = 0;
    double between = 0;
    (void)remaining_free;

    GArray *assigned_main = g_array_new(FALSE, FALSE, sizeof(double));
    GArray *measured_h    = g_array_new(FALSE, FALSE, sizeof(double));
    double max_cross = 0;
    double leftover_capped = 0;
    for (guint i = 0; i < items->len; i++) {
        ns_box *c = items->pdata[i];
        double a = g_array_index(basis, double, i)
                 + extra_per_grow * flex_grow_of(c);
        if (a < 0) a = 0;
        const ns_css_value *mxw = c->style ? c->style->values[NS_CSS_MAX_WIDTH] : NULL;
        if (mxw && (mxw->kind == NS_CSS_V_LENGTH || mxw->kind == NS_CSS_V_CALC)) {
            double mx = length_resolve(mxw, cw, -1);
            if (mx > 0 && a > mx) {
                leftover_capped += (a - mx);
                a = mx;
            }
        }
        g_array_append_val(assigned_main, a);
    }
    if (leftover_capped > 0) {
        double total_grow_remaining = 0;
        for (guint i = 0; i < items->len; i++) {
            ns_box *c = items->pdata[i];
            double a = g_array_index(assigned_main, double, i);
            const ns_css_value *mxw = c->style ? c->style->values[NS_CSS_MAX_WIDTH] : NULL;
            double mx = -1;
            if (mxw && (mxw->kind == NS_CSS_V_LENGTH || mxw->kind == NS_CSS_V_CALC))
                mx = length_resolve(mxw, cw, -1);
            gboolean at_cap = (mx > 0 && a >= mx - 0.5);
            if (!at_cap && flex_grow_of(c) > 0)
                total_grow_remaining += flex_grow_of(c);
        }
        if (total_grow_remaining > 0) {
            double extra2 = leftover_capped / total_grow_remaining;
            for (guint i = 0; i < items->len; i++) {
                ns_box *c = items->pdata[i];
                double a = g_array_index(assigned_main, double, i);
                const ns_css_value *mxw = c->style ? c->style->values[NS_CSS_MAX_WIDTH] : NULL;
                double mx = -1;
                if (mxw && (mxw->kind == NS_CSS_V_LENGTH || mxw->kind == NS_CSS_V_CALC))
                    mx = length_resolve(mxw, cw, -1);
                gboolean at_cap = (mx > 0 && a >= mx - 0.5);
                if (!at_cap && flex_grow_of(c) > 0) {
                    double add = extra2 * flex_grow_of(c);
                    a += add;
                    if (mx > 0 && a > mx) a = mx;
                    g_array_index(assigned_main, double, i) = a;
                }
            }
        }
    }
    double min_deficit = 0;
    for (guint i = 0; i < items->len; i++) {
        ns_box *c = items->pdata[i];
        const ns_css_value *mnw = c->style ? c->style->values[NS_CSS_MIN_WIDTH] : NULL;
        if (!mnw || (mnw->kind != NS_CSS_V_LENGTH && mnw->kind != NS_CSS_V_CALC))
            continue;
        double mn = length_resolve(mnw, cw, -1);
        double a = g_array_index(assigned_main, double, i);
        if (mn > 0 && a < mn) {
            min_deficit += mn - a;
            g_array_index(assigned_main, double, i) = mn;
        }
    }
    if (min_deficit > 0) {
        double reducible[NS_CSS_TRACKS_MAX];
        gboolean use_arr = items->len <= NS_CSS_TRACKS_MAX;
        double total_reducible = 0;
        for (guint i = 0; i < items->len; i++) {
            ns_box *c = items->pdata[i];
            const ns_css_value *mnw = c->style ? c->style->values[NS_CSS_MIN_WIDTH] : NULL;
            double mn = 0;
            if (mnw && (mnw->kind == NS_CSS_V_LENGTH || mnw->kind == NS_CSS_V_CALC)) {
                double r = length_resolve(mnw, cw, -1);
                if (r > 0) mn = r;
            }
            double a = g_array_index(assigned_main, double, i);
            double room = a > mn ? a - mn : 0;
            if (use_arr) reducible[i] = room;
            total_reducible += room;
        }
        double take = min_deficit < total_reducible ? min_deficit : total_reducible;
        if (take > 0 && total_reducible > 0 && use_arr) {
            for (guint i = 0; i < items->len; i++) {
                if (reducible[i] <= 0) continue;
                double a = g_array_index(assigned_main, double, i);
                g_array_index(assigned_main, double, i) =
                    a - take * (reducible[i] / total_reducible);
            }
        }
    }
    double used_main = total_extras;
    for (guint i = 0; i < items->len; i++)
        used_main += g_array_index(assigned_main, double, i);
    double free_main = cw - used_main;
    if (free_main < 0) free_main = 0;
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
    if (auto_margins == 0 && total_grow == 0 && free_main > 0) {
        if      (strcmp(justify, "flex-end") == 0 ||
                 strcmp(justify, "end") == 0)            leading = free_main;
        else if (strcmp(justify, "center") == 0)         leading = free_main / 2.0;
        else if (strcmp(justify, "space-between") == 0)  between = items->len > 1
                                                            ? free_main / (items->len - 1) : 0;
        else if (strcmp(justify, "space-around") == 0) {
            between = items->len > 0 ? free_main / items->len : 0;
            leading = between / 2.0;
        } else if (strcmp(justify, "space-evenly") == 0) {
            between = items->len > 0 ? free_main / (items->len + 1) : 0;
            leading = between;
        }
    }

    for (guint i = 0; i < items->len; i++) {
        ns_box *c = items->pdata[i];
        double a = g_array_index(assigned_main, double, i);
        c->x = inner_x;
        c->y = inner_y;
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

    if (total_grow > 0) {
        double reclaimed = 0;
        for (guint i = 0; i < items->len; i++) {
            ns_box *c = items->pdata[i];
            if (flex_grow_of(c) > 0) continue;
            double assigned = g_array_index(assigned_main, double, i);
            if (c->content_width >= 0 && c->content_width < assigned) {
                reclaimed += assigned - c->content_width;
                g_array_index(assigned_main, double, i) = c->content_width;
            }
        }
        for (guint i = 0; reclaimed > 0 && i < items->len; i++) {
            ns_box *c = items->pdata[i];
            double gr = flex_grow_of(c);
            if (gr <= 0) continue;
            double na = g_array_index(assigned_main, double, i)
                      + reclaimed * (gr / total_grow);
            g_array_index(assigned_main, double, i) = na;
            c->x = inner_x;
            c->y = inner_y;
            layout_box(c, na + c->margin.left + c->margin.right
                           + c->border.left + c->border.right
                           + c->padding.left + c->padding.right, child_inherited);
        }
    }

    double cursor_x = inner_x + leading;
    const char *align = keyword_or(box->style, NS_CSS_ALIGN_ITEMS, "stretch");
    double cross_size = max_cross > explicit_cross ? max_cross : explicit_cross;

    for (guint k = 0; k < items->len; k++) {
        guint i = reverse ? (items->len - 1 - k) : k;
        ns_box *c = items->pdata[i];
        const char *eff_align = align;
        if (c->style) {
            const char *as = ns_style_keyword(c->style, NS_CSS_ALIGN_SELF);
            if (as && strcmp(as, "auto") != 0) eff_align = as;
        }
        double item_h_full = g_array_index(measured_h, double, i);
        double item_h = item_h_full - c->margin.top - c->margin.bottom;
        gboolean mt_auto = c->style &&
            keyword_is(c->style->values[NS_CSS_MARGIN_TOP], "auto");
        gboolean mb_auto = c->style &&
            keyword_is(c->style->values[NS_CSS_MARGIN_BOTTOM], "auto");
        double cy = inner_y + c->margin.top;
        if (mt_auto || mb_auto) {
            double free_cross = cross_size - item_h_full;
            if (free_cross < 0) free_cross = 0;
            if (mt_auto && mb_auto) cy = inner_y + free_cross / 2.0 + c->margin.top;
            else if (mt_auto)       cy = inner_y + free_cross + c->margin.top;
        } else if (strcmp(eff_align, "center") == 0) {
            cy = inner_y + (cross_size - item_h_full) / 2.0 + c->margin.top;
        } else if (strcmp(eff_align, "flex-end") == 0 || strcmp(eff_align, "end") == 0) {
            cy = inner_y + cross_size - item_h - c->margin.bottom;
        }
        c->x = cursor_x + c->margin.left;
        c->y = cy;
        double a = g_array_index(assigned_main, double, i);
        layout_box(c, a + c->margin.left + c->margin.right
                       + c->border.left + c->border.right
                       + c->padding.left + c->padding.right,
                   child_inherited);
        if (!mt_auto && !mb_auto && strcmp(eff_align, "stretch") == 0) {
            double stretched = cross_size
                - c->margin.top  - c->margin.bottom
                - c->padding.top - c->padding.bottom
                - c->border.top  - c->border.bottom;
            if (stretched > c->content_height) c->content_height = stretched;
        }
        cursor_x += a + c->margin.left + c->margin.right +
                    c->padding.left + c->padding.right +
                    c->border.left + c->border.right + gap + between;
    }
    g_array_free(measured_h, TRUE);

    *cursor_y_out = inner_y + cross_size;
    g_array_free(basis, TRUE);
    g_array_free(explicit_flags, TRUE);
    g_array_free(assigned_main, TRUE);
    g_ptr_array_free(items, TRUE);
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
        g_ptr_array_add(items, c);
    double gap = flex_gap_of(box->style, cw);
    double row_gap = flex_gap_row_of(box->style);
    const char *align = keyword_or(box->style, NS_CSS_ALIGN_ITEMS, "stretch");
    const char *justify = keyword_or(box->style, NS_CSS_JUSTIFY_CONTENT, "flex-start");
    typedef struct { double top, height; guint start, count; } flex_line;
    GArray *lines = g_array_new(FALSE, FALSE, sizeof(flex_line));

    double line_y = inner_y;
    guint i = 0;
    while (i < items->len) {
        guint line_start = i;
        double used = 0;
        double line_max_h = 0;
        guint line_count = 0;
        for (; i < items->len; i++) {
            ns_box *c = items->pdata[i];
            edges_from_style(c->style, cw,
                             &c->margin, &c->padding, &c->border);
            double extras = c->margin.left + c->margin.right +
                            c->padding.left + c->padding.right +
                            c->border.left + c->border.right;
            double basis = 0;
            gboolean exp = flex_main_basis_explicit(c, cw, &basis);
            if (!exp) basis = flex_content_basis_from_natural(c, cw);
            double item_outer = basis + extras;
            double try_used = used + (line_count > 0 ? gap : 0) + item_outer;
            if (try_used > cw && line_count > 0) break;
            used = try_used;
            line_count++;
            c->x = inner_x;
            c->y = line_y;
            layout_box(c, basis + c->margin.left + c->margin.right +
                       c->padding.left + c->padding.right +
                       c->border.left + c->border.right, child_inherited);
            double item_h = c->content_height +
                            c->padding.top + c->padding.bottom +
                            c->border.top + c->border.bottom +
                            c->margin.top + c->margin.bottom;
            if (item_h > line_max_h) line_max_h = item_h;
        }
        double remaining = cw - used;
        if (remaining < 0) remaining = 0;
        double leading = 0;
        double between = 0;
        if (line_count > 0) {
            if (strcmp(justify, "flex-end") == 0 || strcmp(justify, "end") == 0)
                leading = remaining;
            else if (strcmp(justify, "center") == 0)
                leading = remaining / 2.0;
            else if (strcmp(justify, "space-between") == 0)
                between = line_count > 1 ? remaining / (line_count - 1) : 0;
            else if (strcmp(justify, "space-around") == 0) {
                between = remaining / line_count;
                leading = between / 2.0;
            } else if (strcmp(justify, "space-evenly") == 0) {
                between = remaining / (line_count + 1);
                leading = between;
            }
        }
        double cursor_x = inner_x + leading;
        for (guint k = 0; k < line_count; k++) {
            guint idx = reverse ? (line_start + line_count - 1 - k) : (line_start + k);
            ns_box *c = items->pdata[idx];
            const char *eff_align = align;
            if (c->style) {
                const char *as = ns_style_keyword(c->style, NS_CSS_ALIGN_SELF);
                if (as && strcmp(as, "auto") != 0) eff_align = as;
            }
            double item_h_full = c->content_height +
                                 c->padding.top + c->padding.bottom +
                                 c->border.top + c->border.bottom +
                                 c->margin.top + c->margin.bottom;
            double cy = line_y + c->margin.top;
            if (strcmp(eff_align, "center") == 0)
                cy = line_y + (line_max_h - item_h_full) / 2.0 + c->margin.top;
            else if (strcmp(eff_align, "flex-end") == 0 || strcmp(eff_align, "end") == 0)
                cy = line_y + line_max_h - item_h_full + c->margin.top;
            c->x = cursor_x + c->margin.left;
            c->y = cy;
            double outer = c->content_width
                + c->padding.left + c->padding.right
                + c->border.left + c->border.right;
            layout_box(c, outer + c->margin.left + c->margin.right,
                       child_inherited);
            if (strcmp(eff_align, "stretch") == 0) {
                double stretched = line_max_h
                    - c->margin.top  - c->margin.bottom
                    - c->padding.top - c->padding.bottom
                    - c->border.top  - c->border.bottom;
                if (stretched > c->content_height) c->content_height = stretched;
            }
            cursor_x += outer + c->margin.left + c->margin.right + gap + between;
        }
        flex_line fl = { .top = line_y, .height = line_max_h,
                         .start = line_start, .count = line_count };
        g_array_append_val(lines, fl);
        line_y += line_max_h + row_gap;
    }

    double measured = (line_y - (items->len > 0 ? row_gap : 0)) - inner_y;
    double total_extra = 0;
    {
        const ns_css_value *hv = box->style
            ? box->style->values[NS_CSS_HEIGHT] : NULL;
        if (hv && (hv->kind == NS_CSS_V_LENGTH || hv->kind == NS_CSS_V_CALC) &&
            lines->len > 0) {
            double eh = length_resolve(hv, cw, -1);
            if (keyword_is(box->style->values[NS_CSS_BOX_SIZING], "border-box"))
                eh -= box->padding.top + box->padding.bottom +
                      box->border.top + box->border.bottom;
            if (eh > measured) total_extra = eh - measured;
        }
    }
    if (total_extra > 0.5) {
        const char *acont = keyword_or(box->style, NS_CSS_ALIGN_CONTENT,
                                       "stretch");
        gboolean ac_stretch = !acont || strcmp(acont, "stretch") == 0 ||
                              strcmp(acont, "normal") == 0;
        guint n = lines->len;
        double lead = 0, between_lines = 0, per_line = 0;
        if (ac_stretch) {
            per_line = total_extra / n;
        } else if (strcmp(acont, "center") == 0) {
            lead = total_extra / 2.0;
        } else if (strcmp(acont, "flex-end") == 0 || strcmp(acont, "end") == 0) {
            lead = total_extra;
        } else if (strcmp(acont, "space-between") == 0) {
            between_lines = n > 1 ? total_extra / (n - 1) : 0;
        } else if (strcmp(acont, "space-around") == 0) {
            between_lines = total_extra / n;
            lead = between_lines / 2.0;
        } else if (strcmp(acont, "space-evenly") == 0) {
            between_lines = total_extra / (n + 1);
            lead = between_lines;
        }
        for (guint li = 0; li < n; li++) {
            flex_line *fl = &g_array_index(lines, flex_line, li);
            double dy = lead + (between_lines + per_line) * li;
            double line_h = fl->height + per_line;
            for (guint k = 0; k < fl->count; k++) {
                ns_box *c = items->pdata[fl->start + k];
                if (dy != 0) shift_box_tree(c, 0, dy);
                if (per_line > 0.5) {
                    const char *eff_align = align;
                    const char *as = c->style
                        ? ns_style_keyword(c->style, NS_CSS_ALIGN_SELF) : NULL;
                    if (as && strcmp(as, "auto") != 0) eff_align = as;
                    if (strcmp(eff_align, "stretch") == 0) {
                        double stretched = line_h
                            - c->margin.top - c->margin.bottom
                            - c->padding.top - c->padding.bottom
                            - c->border.top - c->border.bottom;
                        if (stretched > c->content_height)
                            c->content_height = stretched;
                    }
                }
            }
        }
        line_y += total_extra;
    }

    *cursor_y_out = line_y - (items->len > 0 ? row_gap : 0);
    g_ptr_array_free(items, TRUE);
    g_array_free(lines, TRUE);
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
        g_ptr_array_add(items, c);

    double row_gap = flex_gap_row_of(box->style);
    const char *align = keyword_or(box->style, NS_CSS_ALIGN_ITEMS, "stretch");
    gboolean shrink_to_fit = (strcmp(align, "center") == 0 ||
                              strcmp(align, "flex-start") == 0 ||
                              strcmp(align, "start") == 0 ||
                              strcmp(align, "flex-end") == 0 ||
                              strcmp(align, "end") == 0);

    const ns_css_value *hv = box->style ? box->style->values[NS_CSS_HEIGHT] : NULL;
    const ns_css_value *mnh = box->style ? box->style->values[NS_CSS_MIN_HEIGHT] : NULL;
    double explicit_h = -1;
    if (hv && (hv->kind == NS_CSS_V_LENGTH || hv->kind == NS_CSS_V_CALC))
        explicit_h = resolve_used_height(box, hv, cw, -1);
    double min_h = resolve_used_height(box, mnh, parent_content_height, -1);
    if (min_h > explicit_h) explicit_h = min_h;

    GArray *basis = g_array_new(FALSE, FALSE, sizeof(double));
    GArray *explicit_flags = g_array_new(FALSE, FALSE, sizeof(gboolean));
    double total_grow = 0;
    double total_basis = 0;
    double total_margins = 0;
    for (guint i = 0; i < items->len; i++) {
        ns_box *c = items->pdata[i];
        edges_from_style(c->style, cw,
                         &c->margin, &c->padding, &c->border);
        double w_for_layout = cw - c->margin.left - c->margin.right;
        if (w_for_layout < 0) w_for_layout = 0;
        const char *as = c->style
            ? ns_style_keyword(c->style, NS_CSS_ALIGN_SELF) : NULL;
        gboolean child_shrink = shrink_to_fit;
        if (as) {
            if (strcmp(as, "stretch") == 0) child_shrink = FALSE;
            else if (strcmp(as, "center") == 0 ||
                     strcmp(as, "flex-start") == 0 || strcmp(as, "start") == 0 ||
                     strcmp(as, "flex-end") == 0   || strcmp(as, "end") == 0)
                child_shrink = TRUE;
        }
        const ns_css_value *wv = c->style ? c->style->values[NS_CSS_WIDTH] : NULL;
        const ns_css_value *mxw = c->style ? c->style->values[NS_CSS_MAX_WIDTH] : NULL;
        gboolean width_explicit = wv &&
            (wv->kind == NS_CSS_V_LENGTH || wv->kind == NS_CSS_V_CALC);
        if (child_shrink && !width_explicit) {
            double nat = estimate_natural_width(c, w_for_layout);
            if (mxw && (mxw->kind == NS_CSS_V_LENGTH || mxw->kind == NS_CSS_V_CALC)) {
                double mx = length_resolve(mxw, cw, 0);
                if (mx > 0 && nat > mx) nat = mx;
            }
            if (nat > 0 && nat < w_for_layout) w_for_layout = nat;
        }
        c->x = inner_x;
        c->y = inner_y;
        layout_box(c, w_for_layout, child_inherited);
        gboolean exp = FALSE;
        double b = flex_basis_main_height(c, cw, &exp);
        if (!exp) {
            b = c->content_height +
                c->padding.top + c->padding.bottom +
                c->border.top + c->border.bottom;
        }
        g_array_append_val(basis, b);
        g_array_append_val(explicit_flags, exp);
        total_basis += b;
        total_margins += c->margin.top + c->margin.bottom;
        total_grow += flex_grow_of(c);
    }
    double gaps = items->len > 1 ? row_gap * (items->len - 1) : 0;

    double extra_per_grow = 0;
    if (explicit_h > 0 && total_grow > 0) {
        double avail = explicit_h - total_basis - total_margins - gaps;
        if (avail > 0) extra_per_grow = avail / total_grow;
    }
    double remaining_free = 0;
    if (explicit_h > 0)
        remaining_free = explicit_h - total_basis - total_margins - gaps;
    if (remaining_free < 0) remaining_free = 0;

    const char *justify = keyword_or(box->style, NS_CSS_JUSTIFY_CONTENT, "flex-start");
    double leading = 0;
    double between = 0;
    if (total_grow == 0 && remaining_free > 0) {
        if (strcmp(justify, "flex-end") == 0 || strcmp(justify, "end") == 0)
            leading = remaining_free;
        else if (strcmp(justify, "center") == 0)
            leading = remaining_free / 2.0;
        else if (strcmp(justify, "space-between") == 0)
            between = items->len > 1 ? remaining_free / (items->len - 1) : 0;
        else if (strcmp(justify, "space-around") == 0) {
            between = items->len > 0 ? remaining_free / items->len : 0;
            leading = between / 2.0;
        } else if (strcmp(justify, "space-evenly") == 0) {
            between = items->len > 0 ? remaining_free / (items->len + 1) : 0;
            leading = between;
        }
    }

    double cursor_y = inner_y + leading;
    for (guint k = 0; k < items->len; k++) {
        guint i = reverse ? (items->len - 1 - k) : k;
        ns_box *c = items->pdata[i];
        double main_size = g_array_index(basis, double, i)
                         + extra_per_grow * flex_grow_of(c);
        if (main_size < 0) main_size = 0;
        double content_outer = c->content_height +
                               c->padding.top + c->padding.bottom +
                               c->border.top + c->border.bottom;
        if (main_size < content_outer) main_size = content_outer;

        double item_outer_w = c->content_width
            + c->padding.left + c->padding.right
            + c->border.left + c->border.right
            + c->margin.left + c->margin.right;
        const ns_css_value *mlv = c->style ? c->style->values[NS_CSS_MARGIN_LEFT] : NULL;
        const ns_css_value *mrv = c->style ? c->style->values[NS_CSS_MARGIN_RIGHT] : NULL;
        const ns_css_value *wv = c->style ? c->style->values[NS_CSS_WIDTH] : NULL;
        gboolean ml_auto = length_is_auto(mlv);
        gboolean mr_auto = length_is_auto(mrv);
        gboolean width_explicit = wv &&
            (wv->kind == NS_CSS_V_LENGTH || wv->kind == NS_CSS_V_CALC);
        const char *eff_align = align;
        if (c->style) {
            const char *as = ns_style_keyword(c->style, NS_CSS_ALIGN_SELF);
            if (as && strcmp(as, "auto") != 0) eff_align = as;
        }
        double cx = inner_x + c->margin.left;
        if (ml_auto || mr_auto)
            cx = inner_x;
        else if (strcmp(eff_align, "center") == 0)
            cx = inner_x + (cw - item_outer_w) / 2.0 + c->margin.left;
        else if (strcmp(eff_align, "flex-end") == 0 || strcmp(eff_align, "end") == 0)
            cx = inner_x + cw - item_outer_w + c->margin.left;
        if (!ml_auto && !mr_auto && !width_explicit &&
            strcmp(eff_align, "stretch") == 0) {
            double stretched = cw - c->margin.left - c->margin.right;
            if (stretched > 0) {
                c->x = inner_x + c->margin.left;
                c->y = cursor_y + c->margin.top;
                layout_box(c, stretched, child_inherited);
            }
            cx = inner_x + c->margin.left;
        }
        double new_y = cursor_y + c->margin.top;
        double dx = cx - c->x;
        double dy = new_y - c->y;
        if (dx != 0 || dy != 0) shift_box_tree(c, dx, dy);
        c->x = cx;
        c->y = cursor_y + c->margin.top;

        if (extra_per_grow > 0 && flex_grow_of(c) > 0) {
            double target_h = main_size -
                              c->padding.top - c->padding.bottom -
                              c->border.top - c->border.bottom;
            if (target_h > c->content_height) c->content_height = target_h;
        }

        cursor_y += main_size + c->margin.top + c->margin.bottom + row_gap + between;
    }
    if (items->len > 0) cursor_y -= row_gap;

    *cursor_y_out = cursor_y;
    g_array_free(basis, TRUE);
    g_array_free(explicit_flags, TRUE);
    g_ptr_array_free(items, TRUE);
}

static double
track_min_px(const ns_css_track *t, double avail)
{
    if (!t->has_min) return 0;
    switch (t->min_kind) {
    case NS_CSS_TRACK_PX:      return t->min_v;
    case NS_CSS_TRACK_PERCENT: return t->min_v * avail / 100.0;
    default: return 0;
    }
}

static void
resolve_track_sizes(const ns_css_tracks *tr, double available_main,
                    const double *content_px, double *out_sizes)
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
            fixed = t->v;
            fixed_px[i] = fixed;
            total_fixed += fixed;
            break;
        case NS_CSS_TRACK_PERCENT:
            fixed = t->v * available_main / 100.0;
            fixed_px[i] = fixed;
            total_fixed += fixed;
            break;
        case NS_CSS_TRACK_FR:      total_fr += t->v > 0 ? t->v : 0; break;
        case NS_CSS_TRACK_AUTO:    n_auto++; break;
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
    if (content_px) {
        for (int i = 0; i < tr->n; i++) {
            if (tr->tracks[i].kind != NS_CSS_TRACK_AUTO) continue;
            auto_base[i] = content_px[i] > 0 ? content_px[i] : 0;
            total_fixed += auto_base[i];
        }
    }

    double shrink_used = 0;
    if (total_fixed > available_main && total_shrink > 0) {
        shrink_used = total_fixed - available_main;
        if (shrink_used > total_shrink) shrink_used = total_shrink;
        total_fixed -= shrink_used;
    }

    double remaining = available_main - total_fixed;
    if (remaining < 0) remaining = 0;
    double per_fr   = total_fr > 0 ? remaining / total_fr : 0;
    double per_auto = 0;
    if (total_fr == 0 && n_auto > 0) per_auto = remaining / n_auto;

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
            out_sizes[i] = auto_base[i] + per_auto;
            break;
        }
        double mn = track_min_px(t, available_main);
        if (out_sizes[i] < mn) out_sizes[i] = mn;
        if (out_sizes[i] < 0) out_sizes[i] = 0;
    }
}

static ns_css_tracks
expand_auto_repeat(const ns_css_tracks *tr, double available_main, double gap)
{
    ns_css_tracks out = *tr;
    if (tr->auto_repeat == NS_CSS_AUTO_REPEAT_NONE) return out;
    if (tr->auto_repeat_count <= 0) return out;

    double base_min = 0;
    for (int i = 0; i < tr->auto_repeat_count; i++) {
        const ns_css_track *t = &tr->tracks[tr->auto_repeat_start + i];
        double m = track_min_px(t, available_main);
        if (m <= 0 && t->kind == NS_CSS_TRACK_PX) m = t->v;
        if (m <= 0) m = 1;
        base_min += m;
    }
    if (base_min <= 0) return out;
    double pattern_with_gap = base_min + gap * tr->auto_repeat_count;
    int repeats = 1;
    if (pattern_with_gap > 0)
        repeats = (int)((available_main + gap) / pattern_with_gap);
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
    return out;
}

static int
grid_pos_span(const ns_css_value *v, int *out_start, int *out_span)
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
        int n = ns_parse_int(a, 0, 0, NS_CSS_TRACKS_MAX);
        *out_start = n > 0 ? n - 1 : 0;
        g_free(a);
        if (g_str_has_prefix(b, "span ")) {
            *out_span = ns_parse_int(b + 5, 1, 1, NS_CSS_TRACKS_MAX);
        } else {
            int e = ns_parse_int(b, 0, 0, NS_CSS_TRACKS_MAX);
            if (e > *out_start + 1) *out_span = e - 1 - *out_start;
        }
        return 1;
    }
    int n = ns_parse_int(s, 0, 0, NS_CSS_TRACKS_MAX);
    if (n > 0) { *out_start = n - 1; return 1; }
    return 0;
}

static int
grid_line_num(const ns_css_value *v)
{
    if (!v || v->kind != NS_CSS_V_KEYWORD || !v->u.keyword) return 0;
    if (g_str_has_prefix(v->u.keyword, "span ")) return 0;
    int n = ns_parse_int(v->u.keyword, 0, 0, NS_CSS_TRACKS_MAX);
    return n > 0 ? n : 0;
}

static int
grid_resolve_pos(const ns_style *st, ns_css_prop shorthand,
                 ns_css_prop start_prop, ns_css_prop end_prop,
                 int *out_start, int *out_span)
{
    int got = grid_pos_span(st ? st->values[shorthand] : NULL, out_start, out_span);
    if (got) return 1;
    if (!st) return 0;
    int sl = grid_line_num(st->values[start_prop]);
    int el = grid_line_num(st->values[end_prop]);
    if (sl > 0) {
        *out_start = sl - 1;
        if (el > sl) *out_span = el - sl;
        return 1;
    }
    const ns_css_value *ev = st->values[end_prop];
    if (ev && ev->kind == NS_CSS_V_KEYWORD && ev->u.keyword &&
        g_str_has_prefix(ev->u.keyword, "span "))
        *out_span = ns_parse_int(ev->u.keyword + 5, 1, 1, NS_CSS_TRACKS_MAX);
    return 0;
}

static gboolean
grid_slot_available(gboolean occupied[NS_CSS_TRACKS_MAX][NS_CSS_TRACKS_MAX],
                    int row, int col, int col_span, int row_span,
                    int n_cols)
{
    if (row < 0 || col < 0 || col_span < 1 || row_span < 1)
        return FALSE;
    if (col + col_span > n_cols || row + row_span > NS_CSS_TRACKS_MAX)
        return FALSE;
    for (int r = 0; r < row_span; r++) {
        for (int c = 0; c < col_span; c++) {
            if (occupied[row + r][col + c])
                return FALSE;
        }
    }
    return TRUE;
}

static void
grid_slot_mark(gboolean occupied[NS_CSS_TRACKS_MAX][NS_CSS_TRACKS_MAX],
               int row, int col, int col_span, int row_span,
               int n_cols)
{
    if (row < 0 || col < 0 || col_span < 1 || row_span < 1)
        return;
    if (col + col_span > n_cols) col_span = n_cols - col;
    if (row + row_span > NS_CSS_TRACKS_MAX)
        row_span = NS_CSS_TRACKS_MAX - row;
    for (int r = 0; r < row_span; r++) {
        for (int c = 0; c < col_span; c++)
            occupied[row + r][col + c] = TRUE;
    }
}

static gboolean
grid_find_slot(gboolean occupied[NS_CSS_TRACKS_MAX][NS_CSS_TRACKS_MAX],
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
        return r0 < NS_CSS_TRACKS_MAX && c0 < n_cols;
    }
    for (int r = r0; r < NS_CSS_TRACKS_MAX; r++) {
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
grid_advance_cursor(gboolean occupied[NS_CSS_TRACKS_MAX][NS_CSS_TRACKS_MAX],
                    int *row, int *col, int n_cols)
{
    if (*col >= n_cols) {
        *col = 0;
        (*row)++;
    }
    while (*row < NS_CSS_TRACKS_MAX && occupied[*row][*col]) {
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
    if (t->kind == NS_CSS_TRACK_PX) return t->v;
    if (t->kind == NS_CSS_TRACK_PERCENT) return t->v * basis / 100.0;
    return 0;
}

static const ns_css_area_rect *
find_area_rect(const ns_css_areas *areas, const char *name)
{
    if (!areas || !name) return NULL;
    for (int i = 0; i < areas->n_rects; i++)
        if (strcmp(areas->rects[i].name, name) == 0)
            return &areas->rects[i];
    return NULL;
}

static void
layout_grid_areas(ns_box *box, double cw,
                  double inner_x, double inner_y,
                  const ns_css_areas *areas,
                  const ns_style *child_inherited,
                  double *cursor_y_out)
{
    const ns_css_value *cols_v = box->style ? box->style->values[NS_CSS_GRID_TEMPLATE_COLUMNS] : NULL;
    const ns_css_value *rows_v = box->style ? box->style->values[NS_CSS_GRID_TEMPLATE_ROWS]    : NULL;
    int n_cols_from_areas = areas->n_cols;
    ns_css_tracks default_cols = { 0 };
    default_cols.n = n_cols_from_areas;
    for (int i = 0; i < n_cols_from_areas; i++) {
        default_cols.tracks[i].kind = NS_CSS_TRACK_FR;
        default_cols.tracks[i].v = 1;
    }
    const ns_css_tracks *cols_src = (cols_v && cols_v->kind == NS_CSS_V_TRACKS) ?
                                    &cols_v->u.tracks : &default_cols;

    double col_gap = gap_px(box->style ? box->style->values[NS_CSS_COLUMN_GAP] : NULL,
                            box->style ? box->style->values[NS_CSS_GAP] : NULL, cw);
    double row_gap = number_or(box->style ? box->style->values[NS_CSS_ROW_GAP] : NULL, -1);
    if (row_gap < 0) row_gap = number_or(box->style ? box->style->values[NS_CSS_GAP] : NULL, 0);
    ns_css_tracks cols_buf = expand_auto_repeat(cols_src, cw, col_gap);
    int n_cols = cols_buf.n > 0 ? cols_buf.n : 1;
    double avail = cw - (n_cols > 1 ? col_gap * (n_cols - 1) : 0);
    if (avail < 0) avail = 0;

    int n_rows = areas->n_rows;

    GPtrArray *items   = g_ptr_array_new();
    GArray    *r0_arr  = g_array_new(FALSE, FALSE, sizeof(int));
    GArray    *r1_arr  = g_array_new(FALSE, FALSE, sizeof(int));
    GArray    *c0_arr  = g_array_new(FALSE, FALSE, sizeof(int));
    GArray    *c1_arr  = g_array_new(FALSE, FALSE, sizeof(int));
    GArray    *h_arr   = g_array_new(FALSE, FALSE, sizeof(double));

    int auto_r = 0, auto_c = 0;
    for (ns_box *c = box->first_child; c; c = c->next_sibling) {
        int r0 = -1, r1 = -1, c0 = -1, c1 = -1;
        const char *name = NULL;
        if (c->style) {
            const ns_css_value *ga = c->style->values[NS_CSS_GRID_AREA];
            if (ga && ga->kind == NS_CSS_V_KEYWORD) name = ga->u.keyword;
        }
        const ns_css_area_rect *rect = find_area_rect(areas, name);
        if (rect) {
            r0 = rect->r0; r1 = rect->r1;
            c0 = rect->c0; c1 = rect->c1;
        } else if (c->style) {
            int crs = -1, crsp = 1, rrs = -1, rrsp = 1;
            grid_pos_span(c->style->values[NS_CSS_GRID_COLUMN], &crs, &crsp);
            grid_pos_span(c->style->values[NS_CSS_GRID_ROW], &rrs, &rrsp);
            if (crs >= 0) { c0 = crs; c1 = crs + (crsp > 0 ? crsp - 1 : 0); }
            if (rrs >= 0) { r0 = rrs; r1 = rrs + (rrsp > 0 ? rrsp - 1 : 0); }
        }
        if (c0 < 0 || r0 < 0) {
            while (auto_r < NS_CSS_TRACKS_MAX) {
                if (auto_c >= n_cols) { auto_c = 0; auto_r++; continue; }
                gboolean occupied = FALSE;
                for (int k = 0; k < areas->n_rects; k++) {
                    const ns_css_area_rect *ar = &areas->rects[k];
                    if (auto_r >= ar->r0 && auto_r <= ar->r1 &&
                        auto_c >= ar->c0 && auto_c <= ar->c1) {
                        occupied = TRUE; break;
                    }
                }
                if (!occupied) break;
                auto_c++;
            }
            r0 = auto_r; r1 = auto_r;
            c0 = auto_c; c1 = auto_c;
            auto_c++;
        }
        if (c0 < 0) c0 = 0;
        if (c0 >= n_cols) c0 = n_cols - 1;
        if (c1 >= n_cols) c1 = n_cols - 1;
        if (c1 < c0) c1 = c0;
        if (r0 < 0) r0 = 0;
        if (r1 < r0) r1 = r0;
        if (r1 >= NS_CSS_TRACKS_MAX) r1 = NS_CSS_TRACKS_MAX - 1;
        if (r1 + 1 > n_rows) n_rows = r1 + 1;

        g_ptr_array_add(items, c);
        g_array_append_val(r0_arr, r0);
        g_array_append_val(r1_arr, r1);
        g_array_append_val(c0_arr, c0);
        g_array_append_val(c1_arr, c1);
        double zero = 0;
        g_array_append_val(h_arr, zero);
    }

    if (n_rows > NS_CSS_TRACKS_MAX) n_rows = NS_CSS_TRACKS_MAX;

    double col_content[NS_CSS_TRACKS_MAX] = {0};
    gboolean any_auto_content = FALSE;
    for (int t = 0; t < n_cols; t++) {
        if (cols_buf.tracks[t].kind != NS_CSS_TRACK_AUTO) continue;
        for (guint i = 0; i < items->len; i++) {
            int cc0 = g_array_index(c0_arr, int, i);
            int cc1 = g_array_index(c1_arr, int, i);
            if (cc0 != t || cc1 != t) continue;
            ns_box *c = items->pdata[i];
            const ns_css_value *wv = c->style ? c->style->values[NS_CSS_WIDTH] : NULL;
            double nw = (wv && (wv->kind == NS_CSS_V_LENGTH ||
                                wv->kind == NS_CSS_V_CALC))
                      ? length_resolve(wv, avail, 0)
                      : measure_natural_width(c, child_inherited);
            if (c->style) {
                ns_edges m = {0}, pd = {0}, bd = {0};
                edges_from_style(c->style, nw, &m, &pd, &bd);
                nw += m.left + m.right + pd.left + pd.right +
                      bd.left + bd.right;
            }
            if (nw > col_content[t]) col_content[t] = nw;
            any_auto_content = TRUE;
        }
    }

    double col_sizes[NS_CSS_TRACKS_MAX];
    resolve_track_sizes(&cols_buf, avail, any_auto_content ? col_content : NULL,
                        col_sizes);

    double col_x[NS_CSS_TRACKS_MAX + 1];
    col_x[0] = inner_x;
    for (int i = 0; i < n_cols; i++)
        col_x[i + 1] = col_x[i] + col_sizes[i] + col_gap;

    for (guint i = 0; i < items->len; i++) {
        ns_box *c = items->pdata[i];
        int cc0 = g_array_index(c0_arr, int, i);
        int cc1 = g_array_index(c1_arr, int, i);
        double w = 0;
        for (int k = cc0; k <= cc1; k++) w += col_sizes[k];
        w += (cc1 - cc0) * col_gap;
        edges_from_style(c->style, w, &c->margin, &c->padding, &c->border);
        double cw_for_item = w - c->margin.left - c->margin.right;
        if (cw_for_item < 0) cw_for_item = 0;
        c->x = col_x[cc0] + c->margin.left;
        c->y = inner_y + c->margin.top;
        layout_box(c, cw_for_item, child_inherited);
        double item_outer = c->content_height +
                            c->padding.top + c->padding.bottom +
                            c->border.top + c->border.bottom +
                            c->margin.top + c->margin.bottom;
        g_array_index(h_arr, double, i) = item_outer;
    }

    double row_height[NS_CSS_TRACKS_MAX] = {0};
    if (rows_v && rows_v->kind == NS_CSS_V_TRACKS) {
        for (int r = 0; r < rows_v->u.tracks.n && r < n_rows; r++) {
            const ns_css_track *t = &rows_v->u.tracks.tracks[r];
            if (t->kind == NS_CSS_TRACK_PX) row_height[r] = t->v;
            else if (t->kind == NS_CSS_TRACK_PERCENT)
                row_height[r] = t->v * cw / 100.0;
        }
    }
    for (guint i = 0; i < items->len; i++) {
        int r0 = g_array_index(r0_arr, int, i);
        int r1 = g_array_index(r1_arr, int, i);
        int span = r1 - r0 + 1;
        if (span < 1) span = 1;
        double item_h = g_array_index(h_arr, double, i);
        if (span == 1) {
            if (r0 < n_rows && item_h > row_height[r0])
                row_height[r0] = item_h;
        } else {
            double used = row_gap * (span - 1);
            for (int r = r0; r <= r1 && r < n_rows; r++)
                used += row_height[r];
            if (item_h > used) {
                int target = r1 < n_rows ? r1 : n_rows - 1;
                if (target >= 0)
                    row_height[target] += item_h - used;
            }
        }
    }

    double row_y[NS_CSS_TRACKS_MAX + 1];
    row_y[0] = inner_y;
    for (int r = 0; r < n_rows; r++)
        row_y[r + 1] = row_y[r] + row_height[r] + row_gap;

    for (guint i = 0; i < items->len; i++) {
        ns_box *c = items->pdata[i];
        int r0 = g_array_index(r0_arr, int, i);
        int cc0 = g_array_index(c0_arr, int, i);
        int cc1 = g_array_index(c1_arr, int, i);
        double w = 0;
        for (int k = cc0; k <= cc1; k++) w += col_sizes[k];
        w += (cc1 - cc0) * col_gap;
        double cw_for_item = w - c->margin.left - c->margin.right;
        if (cw_for_item < 0) cw_for_item = 0;
        c->x = col_x[cc0] + c->margin.left;
        c->y = row_y[r0] + c->margin.top;
        layout_box(c, cw_for_item, child_inherited);
    }

    double cursor_y = row_y[n_rows];
    if (n_rows > 0) cursor_y -= row_gap;
    *cursor_y_out = cursor_y;

    g_ptr_array_free(items, TRUE);
    g_array_free(r0_arr, TRUE);
    g_array_free(r1_arr, TRUE);
    g_array_free(c0_arr, TRUE);
    g_array_free(c1_arr, TRUE);
    g_array_free(h_arr, TRUE);
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
    if (areas_v && areas_v->kind == NS_CSS_V_AREAS && areas_v->u.areas.n_rects > 0) {
        layout_grid_areas(box, cw, inner_x, inner_y,
                          &areas_v->u.areas, child_inherited, cursor_y_out);
        return;
    }
    const ns_css_value *cols_v = box->style ? box->style->values[NS_CSS_GRID_TEMPLATE_COLUMNS] : NULL;
    gboolean cols_subgrid = cols_v && cols_v->kind == NS_CSS_V_TRACKS &&
                            cols_v->u.tracks.subgrid && sg && sg->n > 0;
    const ns_css_value *rows_v = box->style ? box->style->values[NS_CSS_GRID_TEMPLATE_ROWS]    : NULL;
    gboolean rows_subgrid = rows_v && rows_v->kind == NS_CSS_V_TRACKS &&
                            rows_v->u.tracks.subgrid && sgr && sgr->n > 0;
    ns_css_tracks default_cols = { .n = 1, .tracks = { { .kind = NS_CSS_TRACK_FR, .v = 1 } } };
    const ns_css_tracks *cols_src =
        (cols_v && cols_v->kind == NS_CSS_V_TRACKS && !cols_v->u.tracks.subgrid) ?
        &cols_v->u.tracks : &default_cols;

    double col_gap = gap_px(box->style ? box->style->values[NS_CSS_COLUMN_GAP] : NULL,
                            box->style ? box->style->values[NS_CSS_GAP] : NULL, cw);
    double row_gap = number_or(box->style ? box->style->values[NS_CSS_ROW_GAP] : NULL, -1);
    if (row_gap < 0) row_gap = number_or(box->style ? box->style->values[NS_CSS_GAP] : NULL, 0);
    if (rows_subgrid) row_gap = sgr->gap;

    ns_css_tracks cols_buf = expand_auto_repeat(cols_src, cw, col_gap);
    const ns_css_tracks *cols = &cols_buf;
    int n_cols = cols->n > 0 ? cols->n : 1;

    double col_sizes[NS_CSS_TRACKS_MAX];
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
            int got = grid_resolve_pos(c->style, NS_CSS_GRID_COLUMN,
                                       NS_CSS_GRID_COLUMN_START,
                                       NS_CSS_GRID_COLUMN_END, &s, &sp);
            if (!got) s = -1;
            got = grid_resolve_pos(c->style, NS_CSS_GRID_ROW,
                                   NS_CSS_GRID_ROW_START,
                                   NS_CSS_GRID_ROW_END, &rs_start, &rs);
            if (!got) rs_start = -1;
            if (rs < 1) rs = 1;
        }
        g_ptr_array_add(items, c);
        g_array_append_val(col_starts, s);
        g_array_append_val(col_spans, sp);
        g_array_append_val(row_starts, rs_start);
        g_array_append_val(row_spans, rs);
    }

    double col_content[NS_CSS_TRACKS_MAX] = {0};
    gboolean any_auto_content = FALSE;
    for (int t = 0; t < n_cols; t++) {
        if (cols->tracks[t].kind != NS_CSS_TRACK_AUTO) continue;
        for (guint k = 0; k < items->len; k++) {
            if (g_array_index(col_starts, int, k) != t ||
                g_array_index(col_spans, int, k) != 1) continue;
            ns_box *c = items->pdata[k];
            double nw = estimate_natural_width(c, avail);
            if (nw > col_content[t]) col_content[t] = nw;
            any_auto_content = TRUE;
        }
    }
    resolve_track_sizes(cols, avail, any_auto_content ? col_content : NULL,
                        col_sizes);

    double col_x[NS_CSS_TRACKS_MAX + 1];
    col_x[0] = inner_x;
    for (int i = 0; i < n_cols; i++)
        col_x[i + 1] = col_x[i] + col_sizes[i] + col_gap;

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
        used_w += n_cols > 1 ? col_gap * (n_cols - 1) : 0;
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
                    col_x[t + 1] = col_x[t] + col_sizes[t] + col_gap + extra_gap;
            }
        }
    }

    const ns_css_value *auto_rows_v = box->style
        ? box->style->values[NS_CSS_GRID_AUTO_ROWS] : NULL;
    const ns_css_tracks *rows_tracks =
        (!rows_subgrid && rows_v && rows_v->kind == NS_CSS_V_TRACKS &&
         !rows_v->u.tracks.subgrid)
        ? &rows_v->u.tracks : NULL;
    const ns_css_tracks *auto_rows_tracks =
        (!rows_subgrid && auto_rows_v && auto_rows_v->kind == NS_CSS_V_TRACKS &&
         !auto_rows_v->u.tracks.subgrid && auto_rows_v->u.tracks.n > 0)
        ? &auto_rows_v->u.tracks : NULL;
    int explicit_rows = rows_subgrid ? sgr->n : (rows_tracks ? rows_tracks->n : 0);
    gboolean occupied[NS_CSS_TRACKS_MAX][NS_CSS_TRACKS_MAX] = {{FALSE}};
    int auto_row = 0;
    int auto_col = 0;
    int n_rows = explicit_rows;
    for (guint i = 0; i < items->len; i++) {
        int s = g_array_index(col_starts, int, i);
        int sp = g_array_index(col_spans, int, i);
        int rs_start = g_array_index(row_starts, int, i);
        int rs = g_array_index(row_spans, int, i);
        if (sp < 1) sp = 1;
        if (sp > n_cols) sp = n_cols;
        if (rs < 1) rs = 1;
        if (rs > NS_CSS_TRACKS_MAX) rs = NS_CSS_TRACKS_MAX;
        gboolean fixed_col = s >= 0 && s + sp <= n_cols;
        gboolean fixed_row = rs_start >= 0 && rs_start < NS_CSS_TRACKS_MAX;
        int start_row = fixed_row ? rs_start : auto_row;
        int start_col = fixed_col ? s : (fixed_row ? 0 : auto_col);
        int placed_row = start_row;
        int placed_col = fixed_col ? s : 0;
        if (!grid_find_slot(occupied, &placed_row, &placed_col,
                            sp, rs, n_cols, start_row, start_col,
                            fixed_row, fixed_col)) {
            if (placed_row < 0) placed_row = 0;
            if (placed_col < 0) placed_col = 0;
            if (placed_col + sp > n_cols) placed_col = n_cols - sp;
            if (placed_row + rs > NS_CSS_TRACKS_MAX)
                placed_row = NS_CSS_TRACKS_MAX - rs;
            if (placed_row < 0) placed_row = 0;
        }
        grid_slot_mark(occupied, placed_row, placed_col, sp, rs, n_cols);
        g_array_append_val(placed_rows, placed_row);
        g_array_append_val(placed_cols, placed_col);
        if (placed_row + rs > n_rows) n_rows = placed_row + rs;
        auto_row = placed_row;
        auto_col = placed_col + sp;
        grid_advance_cursor(occupied, &auto_row, &auto_col, n_cols);
    }
    if (n_rows > NS_CSS_TRACKS_MAX) n_rows = NS_CSS_TRACKS_MAX;

    double base_row_height[NS_CSS_TRACKS_MAX] = {0};
    for (int r = 0; r < n_rows; r++) {
        if (rows_subgrid) {
            base_row_height[r] = sgr->sizes[r];
        } else if (rows_tracks && r < rows_tracks->n) {
            base_row_height[r] = grid_track_px(&rows_tracks->tracks[r], cw);
        } else if (auto_rows_tracks) {
            int ar = (r - explicit_rows) % auto_rows_tracks->n;
            if (ar < 0) ar = 0;
            base_row_height[r] = grid_track_px(&auto_rows_tracks->tracks[ar], cw);
        }
    }
    double base_row_y[NS_CSS_TRACKS_MAX + 1];
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
            w += col_sizes[chosen + k] + (k > 0 ? col_gap : 0);
        edges_from_style(c->style, w, &c->margin, &c->padding, &c->border);
        double cw_for_item = w - c->margin.left - c->margin.right;
        if (cw_for_item < 0) cw_for_item = 0;
        c->x = col_x[chosen] + c->margin.left;
        c->y = inner_y + c->margin.top;

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
        ns_subgrid_cols subctx;
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
        ns_subgrid_rows subrowctx;
        if (rs >= 1 && style_is_grid_container(c->style) &&
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
        layout_box(c, item_w, child_inherited);
        g_pending_subgrid_cols = NULL;
        g_pending_subgrid_rows = NULL;
        if (!j_stretch) {
            double used_w = c->content_width +
                            c->padding.left + c->padding.right +
                            c->border.left + c->border.right;
            double free_w = cw_for_item - used_w;
            double dx = 0;
            if (free_w > 0) {
                if (strcmp(j_eff, "center") == 0)
                    dx = free_w / 2.0;
                else if (strcmp(j_eff, "end") == 0 ||
                         strcmp(j_eff, "right") == 0 ||
                         strcmp(j_eff, "flex-end") == 0)
                    dx = free_w;
            }
            if (dx != 0) shift_box_tree(c, dx, 0);
        }
        double item_outer = c->content_height +
                            c->padding.top + c->padding.bottom +
                            c->border.top + c->border.bottom +
                            c->margin.top + c->margin.bottom;
        g_array_append_val(item_heights, item_outer);
    }

    double row_height[NS_CSS_TRACKS_MAX] = {0};
    for (int r = 0; r < n_rows; r++) {
        double fixed = 0;
        if (rows_subgrid) {
            fixed = sgr->sizes[r];
        } else if (rows_tracks && r < rows_tracks->n) {
            fixed = grid_track_px(&rows_tracks->tracks[r], cw);
        } else if (auto_rows_tracks) {
            int ar = (r - explicit_rows) % auto_rows_tracks->n;
            if (ar < 0) ar = 0;
            fixed = grid_track_px(&auto_rows_tracks->tracks[ar], cw);
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
        if (rs == 1) {
            if (item_outer > row_height[row])
                row_height[row] = item_outer;
        } else {
            double used = row_gap * (rs - 1);
            for (int k = 0; k < rs; k++)
                used += row_height[row + k];
            if (item_outer > used) {
                double add = (item_outer - used) / rs;
                for (int k = 0; k < rs; k++)
                    row_height[row + k] += add;
            }
        }
    }

    double cursor_y = rows_subgrid ? sgr->y[0] : inner_y;
    for (int r = 0; r < n_rows; r++) {
        grid_row gr = { .top = cursor_y, .height = row_height[r] };
        g_array_append_val(grid_rows, gr);
        cursor_y += row_height[r] + row_gap;
    }
    if (grid_rows->len > 0) cursor_y -= row_gap;

    double measured = cursor_y - inner_y;
    double total_extra = 0;
    {
        const ns_css_value *hv = box->style
            ? box->style->values[NS_CSS_HEIGHT] : NULL;
        if (hv && (hv->kind == NS_CSS_V_LENGTH || hv->kind == NS_CSS_V_CALC) &&
            grid_rows->len > 0) {
            double eh = length_resolve(hv, cw, -1);
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
    double per_row_extra = (ac_stretch && grid_rows->len > 0)
        ? total_extra / grid_rows->len : 0;
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
            row_h += gk->height + per_row_extra;
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
        if (a_stretch) {
            const ns_css_value *ihv = c->style
                ? c->style->values[NS_CSS_HEIGHT] : NULL;
            gboolean i_has_h = ihv && (ihv->kind == NS_CSS_V_LENGTH ||
                                       ihv->kind == NS_CSS_V_CALC);
            if (!i_has_h && c->kind == NS_BOX_BLOCK && free_h > 0.5)
                c->content_height += free_h;
        } else if (free_h > 0.5 && strcmp(a_eff, "center") == 0) {
            dy_align = free_h / 2.0;
        } else if (free_h > 0.5 && (strcmp(a_eff, "end") == 0 ||
                                    strcmp(a_eff, "flex-end") == 0)) {
            dy_align = free_h;
        }
        grid_row *gr = &g_array_index(grid_rows, grid_row, r);
        double row_top = gr->top + per_row_extra * r +
                         group_off + row_between * r;
        double target_y = row_top + c->margin.top + dy_align;
        double dy = target_y - c->y;
        if (dy != 0) shift_box_tree(c, 0, dy);
    }
    if (total_extra > 0) cursor_y += total_extra;

    *cursor_y_out = cursor_y;
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
    const char *parent_flex_dir = box->parent
        ? keyword_or(box->parent->style, NS_CSS_FLEX_DIRECTION, "row") : "row";
    gboolean flex_row_item = box->parent &&
        style_is_flex_container(box->parent->style) &&
        (strcmp(parent_flex_dir, "row") == 0 ||
         strcmp(parent_flex_dir, "row-reverse") == 0);
    if (flex_row_item && flex_grow_of(box) > 0) {
        cw = parent_content_width - horiz_total;
        if (cw < 0) cw = 0;
        explicit_width = TRUE;
    } else if (wv && wv->kind == NS_CSS_V_LENGTH) {
        cw = length_resolve(wv, parent_content_width, 0);
        explicit_width = TRUE;
    } else if (wv && wv->kind == NS_CSS_V_CALC) {
        cw = length_resolve(wv, parent_content_width, 0);
        explicit_width = TRUE;
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
    } else if (box->style &&
               (keyword_is(box->style->values[NS_CSS_DISPLAY], "inline-block") ||
                keyword_is(box->style->values[NS_CSS_DISPLAY], "inline-flex") ||
                keyword_is(box->style->values[NS_CSS_DISPLAY], "inline-grid"))) {
        double natural = measure_natural_width(box,
                                               inherited_style ? inherited_style : box->style);
        double avail = parent_content_width - horiz_total;
        if (avail < 0) avail = 0;
        cw = natural < avail ? natural : avail;
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
    if (border_box && explicit_width && !intrinsic_width) {
        cw -= horiz_extras;
        if (cw < 0) cw = 0;
    }
    double max_cw = length_resolve(mxw, parent_content_width, -1);
    if (max_cw >= 0) {
        if (border_box) max_cw -= horiz_extras;
        if (max_cw >= 0 && cw > max_cw) { cw = max_cw; explicit_width = TRUE; }
    }
    double min_cw = length_resolve(mnw, parent_content_width, -1);
    if (min_cw >= 0) {
        if (border_box) min_cw -= horiz_extras;
        if (min_cw >= 0 && cw < min_cw) { cw = min_cw; explicit_width = TRUE; }
    }
    box->content_width = cw;

    if (explicit_width) {
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
    double cursor_y = inner_y;
    double prev_margin_bottom = 0;
    gboolean collapse_bottom_with_parent =
        box->padding.bottom == 0 && box->border.bottom == 0;
    const ns_style *child_inherited = box->style ? box->style : inherited_style;

    if (style_is_flex_container(box->style) || style_is_grid_container(box->style))
        reorder_children_by_order(box);

    if (style_is_flex_container(box->style)) {
        const char *dir = keyword_or(box->style, NS_CSS_FLEX_DIRECTION, "row");
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
    if (n_cols > 1) {
        int distributable = 0;
        for (ns_box *c = box->first_child; c; c = c->next_sibling)
            if (c->kind == NS_BOX_BLOCK || c->kind == NS_BOX_TABLE)
                if (++distributable >= 2) break;
        gboolean single_fragmentable_inline =
            box->first_child && !box->first_child->next_sibling &&
            inline_box_can_fragment_multicol(box->first_child,
                                             child_inherited, cw);
        if (distributable < 2 &&
            !single_fragmentable_inline)
            n_cols = 1;
    }
    if (n_cols > 1) {
        double col_w = (cw - col_gap * (n_cols - 1)) / n_cols;
        if (col_w > 1) cw = col_w;
        else n_cols = 1;
    }
    box->columns = n_cols;

    GArray *floats = g_array_new(FALSE, FALSE, sizeof(float_ref));

    for (ns_box *c = box->first_child; c; c = c->next_sibling) {
        c->x = inner_x;
        int fside = float_side_of(c->style);
        int clr = clear_kind_of(c->style);
        if (fside >= 0 && (c->kind == NS_BOX_BLOCK || c->kind == NS_BOX_TABLE ||
                           c->kind == NS_BOX_IMAGE || c->kind == NS_BOX_VIDEO)) {
            edges_from_style(c->style, cw,
                             &c->margin, &c->padding, &c->border);
            double float_max_w = cw;
            double cw_for_float;
            const ns_css_value *wv2 = c->style ? c->style->values[NS_CSS_WIDTH] : NULL;
            const ns_css_value *mxw2 = c->style ? c->style->values[NS_CSS_MAX_WIDTH] : NULL;
            const ns_css_value *mnw2 = c->style ? c->style->values[NS_CSS_MIN_WIDTH] : NULL;
            if (wv2 && (wv2->kind == NS_CSS_V_LENGTH || wv2->kind == NS_CSS_V_CALC)) {
                cw_for_float = length_resolve(wv2, cw, 0);
            } else {
                cw_for_float = measure_natural_width(c, child_inherited);
                double cap = float_max_w
                    - c->padding.left - c->padding.right
                    - c->border.left - c->border.right
                    - c->margin.left - c->margin.right;
                if (cap < 60) cap = 60;
                if (cw_for_float > cap) cw_for_float = cap;
                if (cw_for_float < 60) cw_for_float = 60;
            }
            if (mxw2 && (mxw2->kind == NS_CSS_V_LENGTH || mxw2->kind == NS_CSS_V_CALC)) {
                double mx = length_resolve(mxw2, cw, 0);
                if (mx > 0 && cw_for_float > mx) cw_for_float = mx;
            }
            if (mnw2 && (mnw2->kind == NS_CSS_V_LENGTH || mnw2->kind == NS_CSS_V_CALC)) {
                double mn = length_resolve(mnw2, cw, 0);
                if (mn > 0 && cw_for_float < mn) cw_for_float = mn;
            }
            double avail = cw_for_float
                + c->padding.left + c->padding.right
                + c->border.left + c->border.right
                + c->margin.left + c->margin.right;
            double float_y = cursor_y;
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
            if (cw_for_float > cw_capped && cw_capped > 0) cw_for_float = cw_capped;
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
            layout_box(c, cw_for_float
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
        if (c->kind == NS_BOX_BLOCK || c->kind == NS_BOX_TABLE) {
            edges_from_style(c->style, cw,
                             &c->margin, &c->padding, &c->border);
            double mt = c->margin.top;
            double pos = MAX(mt > 0 ? mt : 0,
                             prev_margin_bottom > 0 ? prev_margin_bottom : 0);
            double neg = MIN(mt < 0 ? mt : 0,
                             prev_margin_bottom < 0 ? prev_margin_bottom : 0);
            double gap = pos + neg;
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
            cursor_y += c->content_height +
                        c->padding.top + c->padding.bottom +
                        c->border.top + c->border.bottom;
            prev_margin_bottom = c->margin.bottom;
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
            cursor_y += c->content_height;
        }
        if ((c->kind == NS_BOX_IMAGE || c->kind == NS_BOX_VIDEO ||
             c->kind == NS_BOX_TABLE) &&
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
        double pos = MAX(box->margin.bottom > 0 ? box->margin.bottom : 0,
                         prev_margin_bottom > 0 ? prev_margin_bottom : 0);
        double neg = MIN(box->margin.bottom < 0 ? box->margin.bottom : 0,
                         prev_margin_bottom < 0 ? prev_margin_bottom : 0);
        box->margin.bottom = pos + neg;
    } else {
        cursor_y += prev_margin_bottom;
    }

    if (floats && floats->len > 0) {
        double fb = floats_max_bottom(floats);
        if (fb > cursor_y) cursor_y = fb;
    }
    g_array_free(floats, TRUE);

    if (n_cols > 1 &&
        !layout_multicol_single_inline(box, inner_x, inner_y, cw, col_gap,
                                       n_cols, child_inherited, &cursor_y)) {
        double total_h = cursor_y - inner_y;
        double target_h = total_h / n_cols;
        double cur_y = 0;
        double max_col_h = 0;
        int cur_col = 0;
        for (ns_box *c = box->first_child; c; c = c->next_sibling) {
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
            double target_x = inner_x + cur_col * (cw + col_gap);
            double target_y = inner_y + cur_y;
            double dx = target_x - c->x;
            double dy = target_y - c->y;
            if (dx != 0 || dy != 0) shift_box_tree(c, dx, dy);
            cur_y += c_full_h;
            if (cur_y > max_col_h) max_col_h = cur_y;
        }
        box->content_width = cw * n_cols + col_gap * (n_cols - 1);
        cursor_y = inner_y + max_col_h;
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
    double measured = cursor_y - inner_y;
    if (hv && (hv->kind == NS_CSS_V_LENGTH || hv->kind == NS_CSS_V_CALC)) {
        double explicit_h = resolve_used_height(box, hv, parent_content_width, -1);
        if (explicit_h < 0) {
            box->content_height = measured;
        } else {
            if (border_box) {
                explicit_h -= vert_extras;
                if (explicit_h < 0) explicit_h = 0;
            }
            box->content_height = explicit_h;
        }
    } else {
        const ns_css_value *ar = box->style
            ? box->style->values[NS_CSS_ASPECT_RATIO] : NULL;
        if (ar && ar->kind == NS_CSS_V_LENGTH &&
            ar->u.length.unit == NS_CSS_UNIT_NUMBER &&
            ar->u.length.v > 0 && box->content_width > 0) {
            double aspect_h = box->content_width / ar->u.length.v;
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
    if (max_h >= 0 && box->content_height > max_h)
        box->content_height = max_h;
    double min_h = resolve_used_height(box, mnh, parent_content_width, -1);
    if (border_box && min_h >= 0) {
        min_h -= vert_extras;
        if (min_h < 0) min_h = 0;
    }
    if (min_h >= 0 && box->content_height < min_h)
        box->content_height = min_h;
    if (box->dom && box->dom->kind == NS_NODE_ELEMENT && box->dom->name &&
        strcmp(box->dom->name, "input") == 0 &&
        box->content_height > measured + 0.5) {
        double shift = (box->content_height - measured) * 0.5;
        for (ns_box *c = box->first_child; c; c = c->next_sibling)
            c->y += shift;
    }
    if (overflow_scrolls) {
        box->scrolls = TRUE;
        if (measured > box->content_height)
            box->scroll_max_y = measured - box->content_height;
    }
    if (overflow_scrolls_x) {
        double max_right = inner_x;
        for (const ns_box *c = box->first_child; c; c = c->next_sibling) {
            double right = c->x + c->margin.left + c->content_width +
                           c->padding.left + c->padding.right +
                           c->border.left + c->border.right;
            if (right > max_right) max_right = right;
        }
        double measured_w = max_right - inner_x;
        if (measured_w > box->content_width + 1.0) {
            box->scrolls = TRUE;
            box->scroll_max_x = measured_w - box->content_width;
        }
    }
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
    g_counters_for_layout = build_counter_snapshots(doc, styles);
    ns_box *root = ns_layout_build_(doc, styles, viewport_width);
    g_focused_input_for_layout = NULL;
    g_focused_is_contenteditable_for_layout = FALSE;
    g_focused_caret_byte_for_layout = 0;
    g_focused_sel_anchor_byte_for_layout = 0;
    g_image_cache_for_layout = NULL;
    g_base_url_for_layout = NULL;
    if (g_counters_for_layout) {
        g_hash_table_destroy(g_counters_for_layout);
        g_counters_for_layout = NULL;
    }
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
    GQueue q = G_QUEUE_INIT;
    g_queue_push_tail(&q, box);
    while (!g_queue_is_empty(&q)) {
        ns_box *b = g_queue_pop_head(&q);
        b->x += dx;
        b->y += dy;
        for (ns_box *c = b->first_child; c; c = c->next_sibling)
            g_queue_push_tail(&q, c);
    }
    g_queue_clear(&q);
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
        if (!t_auto)
            dy = length_or_zero(tv, parent_h);
        else if (bv && !length_is_auto(bv))
            dy = -length_or_zero(bv, parent_h);
        translate_subtree(box, dx, dy);
    }
    for (ns_box *c = box->first_child; c; c = c->next_sibling)
        apply_position_offsets(c, child_w, child_h);
}

static ns_box *
find_box_by_dom(ns_box *root, const ns_node *dom)
{
    if (!root || !dom) return NULL;
    if (root->dom == dom) return root;
    for (ns_box *c = root->first_child; c; c = c->next_sibling) {
        ns_box *m = find_box_by_dom(c, dom);
        if (m) return m;
    }
    if (root->inline_atomics) {
        for (guint i = 0; i < root->inline_atomics->len; i++) {
            ns_box *ab = g_array_index(root->inline_atomics,
                                       ns_inline_atomic, i).box;
            ns_box *m = find_box_by_dom(ab, dom);
            if (m) return m;
        }
    }
    return NULL;
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
    if (b->dom && b->dom != target && !node_is_ancestor_of(b->dom, target) &&
        node_precedes(b->dom, target) && !style_is_absolute_or_fixed(b->style)) {
        double bottom = box_outer_bottom(b);
        if (bottom > *out) *out = bottom;
    }
    for (const ns_box *c = b->first_child; c; c = c->next_sibling)
        static_abs_y_walk(c, target, out);
}

static gboolean
style_creates_abs_cb(const ns_style *s)
{
    if (!s) return FALSE;
    const ns_css_value *v = s->values[NS_CSS_POSITION];
    if (!v || v->kind != NS_CSS_V_KEYWORD || !v->u.keyword) return FALSE;
    const char *kw = v->u.keyword;
    return strcmp(kw, "relative") == 0 || strcmp(kw, "absolute") == 0 ||
           strcmp(kw, "fixed") == 0    || strcmp(kw, "sticky") == 0;
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

static void
position_absolute_box(ns_box *abox, ns_box *cb, gboolean cb_is_icb)
{
    if (!abox || !cb) return;
    const ns_style *s = abox->style;
    double cb_w = cb_is_icb ? ns_css_viewport_w()
                            : cb->content_width + cb->padding.left + cb->padding.right;
    double cb_h = cb_is_icb ? ns_css_viewport_h()
                            : cb->content_height + cb->padding.top + cb->padding.bottom;
    double cb_inner_x = cb->x + cb->margin.left + cb->border.left;
    double cb_inner_y = cb->y + cb->margin.top  + cb->border.top;

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

    double final_x, final_y;
    if (!l_auto) {
        final_x = cb_inner_x + left;
    } else if (!r_auto) {
        final_x = cb_inner_x + cb_w - right - box_outer_w;
    } else {
        final_x = abox->x;
    }
    if (!t_auto) {
        final_y = cb_inner_y + top;
    } else if (!b_auto) {
        final_y = cb_inner_y + cb_h - bottom - box_outer_h;
    } else {
        final_y = abox->y;
    }

    double dx = final_x - abox->x;
    double dy = final_y - abox->y;
    if (dx == 0 && dy == 0) return;
    GQueue q = G_QUEUE_INIT;
    g_queue_push_tail(&q, abox);
    while (!g_queue_is_empty(&q)) {
        ns_box *b = g_queue_pop_head(&q);
        b->x += dx;
        b->y += dy;
        for (ns_box *c = b->first_child; c; c = c->next_sibling)
            g_queue_push_tail(&q, c);
    }
    g_queue_clear(&q);
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
    for (guint i = 0; i < g_abs_pending->len; i++) {
        ns_abs_entry e = g_array_index(g_abs_pending, ns_abs_entry, i);
        const ns_node *cb_dom = e.fixed
            ? NULL
            : find_abs_containing_block_dom(e.dom, styles);
        ns_box *cb = cb_dom ? find_box_by_dom(root, cb_dom) : root;
        if (!cb) cb = root;

        g_abs_force_build = TRUE;
        ns_box *abox = build_block(e.dom, styles);
        g_abs_force_build = FALSE;
        if (!abox) continue;

        box_append_child(cb, abox);
        gboolean cb_is_icb = (cb_dom == NULL);
        double avail = cb->content_width > 0 ? cb->content_width : viewport_width;
        double cb_h = cb_is_icb ? ns_css_viewport_h() : cb->content_height;
        if (cb_is_icb) avail = viewport_width;
        const ns_style *cs = cb->style;
        double static_y = cb->y + cb->margin.top + cb->border.top + cb->padding.top;
        static_abs_y_walk(cb, e.dom, &static_y);
        abox->x = cb->x + cb->margin.left + cb->border.left + cb->padding.left;
        abox->y = static_y;
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
        gboolean stretch_w = !has_explicit_width && l_set && r_set;
        double layout_w = avail;
        if (stretch_w) {
            double l = length_resolve(alv, avail, 0);
            double r = length_resolve(arv, avail, 0);
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
        if (has_explicit_height && height_is_percent(ahv) &&
            cb_h > 0)
            abox->content_height = cb_h;
        layout_box(abox, layout_w, cs);
        if (!stretch_w && !has_explicit_width && abox->kind == NS_BOX_BLOCK) {
            double fit = estimate_natural_width(abox, avail);
            if (fit > 0 && fit < avail) layout_box(abox, fit, cs);
        }
        if (has_explicit_height) {
            double explicit_h = resolve_height_with_basis(ahv, avail,
                                                          cb_h,
                                                          -1);
            if (explicit_h >= 0) {
                gboolean border_box = abox->style &&
                    abox->style->values[NS_CSS_BOX_SIZING] &&
                    abox->style->values[NS_CSS_BOX_SIZING]->kind == NS_CSS_V_KEYWORD &&
                    strcmp(abox->style->values[NS_CSS_BOX_SIZING]->u.keyword,
                           "border-box") == 0;
                if (border_box) {
                    explicit_h -= abox->padding.top + abox->padding.bottom +
                                  abox->border.top + abox->border.bottom;
                    if (explicit_h < 0) explicit_h = 0;
                }
                abox->content_height = explicit_h;
            }
        }
        gboolean t_set = atv && !length_is_auto(atv) &&
            (atv->kind == NS_CSS_V_LENGTH || atv->kind == NS_CSS_V_CALC);
        gboolean b_set = abv && !length_is_auto(abv) &&
            (abv->kind == NS_CSS_V_LENGTH || abv->kind == NS_CSS_V_CALC);
        double stretched_h = -1;
        if (!has_explicit_height && t_set && b_set && cb_h > 0) {
            double t = length_resolve(atv, cb_h, 0);
            double bb = length_resolve(abv, cb_h, 0);
            double h = cb_h - t - bb
                     - abox->margin.top - abox->margin.bottom
                     - abox->border.top - abox->border.bottom
                     - abox->padding.top - abox->padding.bottom;
            if (h > abox->content_height) {
                abox->content_height = h;
                stretched_h = h;
            }
        }
        if (stretched_h >= 0) {
            layout_box(abox, layout_w, cs);
            if (abox->content_height < stretched_h)
                abox->content_height = stretched_h;
        }
        apply_position_offsets(abox, avail, cb_h);
        position_absolute_box(abox, cb, cb_is_icb);
    }
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
            ns_box *ab = g_array_index(b->inline_atomics,
                                       ns_inline_atomic, i).box;
            if (!ab) continue;
            compute_paint_bounds(ab);
            if (ab->paint_top < top) top = ab->paint_top;
            if (ab->paint_bottom > bottom) bottom = ab->paint_bottom;
        }
    }
    b->paint_top = top;
    b->paint_bottom = bottom;
}

static ns_box *
ns_layout_build_(const ns_node *doc, GHashTable *styles, double viewport_width)
{
    g_abs_pending = g_array_new(FALSE, FALSE, sizeof(ns_abs_entry));
    g_contains_block_media_cache = g_hash_table_new(g_direct_hash, g_direct_equal);
    g_cq_seen_container = FALSE;
    ns_css_set_container_dims(0, 0);
    ns_box *root = build_block(doc, styles);
    if (!root) {
        g_array_free(g_abs_pending, TRUE);
        g_abs_pending = NULL;
        g_hash_table_destroy(g_contains_block_media_cache);
        g_contains_block_media_cache = NULL;
        return NULL;
    }
    root->x = 0;
    root->y = 0;

    layout_block(root, viewport_width, NULL);
    apply_position_offsets(root, viewport_width, root->content_height);
    process_absolute_boxes(root, styles, viewport_width);
    compute_paint_bounds(root);

    g_array_free(g_abs_pending, TRUE);
    g_abs_pending = NULL;
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
    }
    return "?";
}

static void
collect_images_walk(const ns_box *b, GPtrArray *out)
{
    if (!b) return;
    if (b->kind == NS_BOX_IMAGE) g_ptr_array_add(out, (gpointer)b);
    if (b->media && b->media->bg_image_src) g_ptr_array_add(out, (gpointer)b);
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

static void
dump_box(GString *out, const ns_box *b, int depth)
{
    for (int i = 0; i < depth; i++) g_string_append(out, "  ");
    const char *tag = (b->dom && b->dom->kind == NS_NODE_ELEMENT) ? b->dom->name : "(anon)";
    g_string_append_printf(out,
        "[%s %s] x=%.0f y=%.0f w=%.0f h=%.0f m=%.0f/%.0f/%.0f/%.0f p=%.0f/%.0f/%.0f/%.0f",
        ns_box_kind_name(b->kind), tag,
        b->x, b->y, b->content_width, b->content_height,
        b->margin.top, b->margin.right, b->margin.bottom, b->margin.left,
        b->padding.top, b->padding.right, b->padding.bottom, b->padding.left);
    if (b->kind == NS_BOX_INLINE && b->text) {
        gsize plen = strlen(b->text);
        gsize show = plen > 60 ? 60 : plen;
        g_string_append(out, " text=\"");
        for (gsize i = 0; i < show; i++) {
            char c = b->text[i];
            g_string_append_c(out, (c == '\n' || c == '\t') ? ' ' : c);
        }
        if (plen > show) g_string_append(out, "…");
        g_string_append_c(out, '"');
    }
    g_string_append_c(out, '\n');
    for (const ns_box *c = b->first_child; c; c = c->next_sibling)
        dump_box(out, c, depth + 1);
}

static void
extent_walk(const ns_box *b, double *out_w, double *out_h)
{
    if (!b) return;
    double right = b->x + b->margin.left + b->border.left + b->padding.left +
                   b->content_width + b->padding.right + b->border.right +
                   b->margin.right;
    double bottom = b->y + b->margin.top + b->border.top + b->padding.top +
                    b->content_height + b->padding.bottom + b->border.bottom +
                    b->margin.bottom;
    if (right  > *out_w) *out_w = right;
    if (bottom > *out_h) *out_h = bottom;
    for (const ns_box *c = b->first_child; c; c = c->next_sibling)
        extent_walk(c, out_w, out_h);
}

void
ns_box_content_extent(const ns_box *root, double *out_w, double *out_h)
{
    double w = 0, h = 0;
    extent_walk(root, &w, &h);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

GString *
ns_box_dump(const ns_box *root)
{
    GString *out = g_string_new(NULL);
    if (root) dump_box(out, root, 0);
    return out;
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
    gboolean clipped = box_clips_children(root);
    if (clipped && !box_padding_contains(root, x, y))
        return NULL;
    double cx = x + root->scroll_x;
    double cy = y + root->scroll_y;
    for (ns_box *c = root->first_child; c; c = c->next_sibling) {
        ns_box *m = ns_box_hit_scrollable(c, cx, cy);
        if (m) return m;
    }
    if (root->scrolls && (root->scroll_max_x > 0 || root->scroll_max_y > 0) &&
        box_padding_contains(root, x, y))
        return root;
    return NULL;
}

const ns_box *
ns_box_hit_test(const ns_box *root, double x, double y)
{
    if (!root) return NULL;
    gboolean clipped = box_clips_children(root);
    if (clipped && !box_padding_contains(root, x, y))
        goto self_test;
    const ns_box *best = NULL;
    double cx = x + root->scroll_x;
    double cy = y + root->scroll_y;
    guint sn = 0;
    const ns_box **stacked = hit_children_stacked(root, &sn);
    if (stacked) {
        for (guint i = 0; i < sn; i++) {
            const ns_box *m = ns_box_hit_test(stacked[i], cx, cy);
            if (m) best = m;
        }
        g_free(stacked);
    } else {
        for (const ns_box *c = root->first_child; c; c = c->next_sibling) {
            const ns_box *m = ns_box_hit_test(c, cx, cy);
            if (m) best = m;
        }
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
    if (!box_blocks_hit_testing(root) &&
        x >= x0 && x <= x1 && y >= y0 && y <= y1 && root->dom)
        return root;
    return NULL;
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
