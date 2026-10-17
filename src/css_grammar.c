/* Nordstjernen — matches keyword-shaped CSS values against a compact grammar and serializes them.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-or-later
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include "css.h"
#include "css_grammar.h"
#include "css_syntax.h"

#define GRAMMAR_GROUPS_MAX 8

static __thread const char *g_active_grammar;

typedef struct {
    const char *start;
    const char *end;
    gboolean    required;
    int         min_count;
    int         max_count;
} grammar_group;

static gboolean
term_is_css_wide(const char *s, gsize n)
{
    static const char *const wide[] = {
        "initial", "inherit", "unset", "revert", "revert-layer", "default",
    };
    for (gsize i = 0; i < G_N_ELEMENTS(wide); i++)
        if (strlen(wide[i]) == n && g_ascii_strncasecmp(s, wide[i], n) == 0)
            return TRUE;
    return FALSE;
}

static double
range_bound(const char *s, const char *end, double fallback)
{
    while (s < end && *s == ' ') s++;
    if (s >= end) return fallback;
    if (g_ascii_strncasecmp(s, "inf", 3) == 0) return INFINITY;
    if (g_ascii_strncasecmp(s, "-inf", 4) == 0) return -INFINITY;
    return g_ascii_strtod(s, NULL);
}

static gboolean
in_range(const char *type_start, const char *type_end, double v)
{
    const char *open = memchr(type_start, '[', (gsize)(type_end - type_start));
    if (!open) return TRUE;
    const char *comma = memchr(open, ',', (gsize)(type_end - open));
    const char *close = memchr(open, ']', (gsize)(type_end - open));
    if (!comma || !close) return TRUE;
    return v >= range_bound(open + 1, comma, -INFINITY) &&
           v <= range_bound(comma + 1, close, INFINITY);
}

static gboolean
type_is(const char *type_start, const char *name)
{
    gsize n = strlen(name);
    return strncmp(type_start + 1, name, n) == 0 &&
           (type_start[n + 1] == '>' || type_start[n + 1] == ' ');
}

static char *
string_serialize(const char *raw)
{
    GString *out = g_string_new("\"");
    const char *p = raw;
    while (*p) {
        if (*p == '\\' && p[1]) {
            GString *one = g_string_new(NULL);
            ns_css_append_unescaped(one, &p);
            for (const char *q = one->str; *q; q++) {
                if (*q == '"' || *q == '\\') g_string_append_c(out, '\\');
                g_string_append_c(out, *q);
            }
            g_string_free(one, TRUE);
            continue;
        }
        if (*p == '"') g_string_append_c(out, '\\');
        g_string_append_c(out, *p++);
    }
    g_string_append_c(out, '"');
    return g_string_free(out, FALSE);
}

static char *
number_str(double v)
{
    return g_strdup_printf("%g", v);
}

static char *
component_text(const ns_css_component *c, const char *src)
{
    return g_strndup(src + c->start, c->end - c->start);
}

static gboolean
component_number(const ns_css_component *c, const char *src, double *out)
{
    if (c->type == NS_CSS_COMPONENT_NUMBER) {
        *out = c->number;
        return TRUE;
    }
    if (c->type != NS_CSS_COMPONENT_FUNCTION) return FALSE;
    char *text = component_text(c, src);
    gboolean ok = ns_css_number_value(text, out);
    g_free(text);
    return ok;
}

static char *
url_serialize(const ns_css_component *c, const char *src)
{
    char *text = component_text(c, src);
    char *inner = g_strndup(text + 4, strlen(text) - 5);
    g_strstrip(inner);
    gsize n = strlen(inner);
    char *body;
    if (n >= 2 && (inner[0] == '"' || inner[0] == '\'') && inner[n - 1] == inner[0])
        body = g_strndup(inner + 1, n - 2);
    else
        body = g_strdup(inner);
    char *quoted = string_serialize(body);
    char *out = g_strdup_printf("url(%s)", quoted);
    g_free(quoted);
    g_free(body);
    g_free(inner);
    g_free(text);
    return out;
}

static char *
type_match(const char *type, const char *type_end, const ns_css_component *c,
           const char *src)
{
    double v = 0;
    if (type_is(type, "string"))
        return c->type == NS_CSS_COMPONENT_STRING ? string_serialize(c->value) : NULL;
    if (type_is(type, "integer")) {
        if (!component_number(c, src, &v)) return NULL;
        if (c->type == NS_CSS_COMPONENT_NUMBER &&
            strpbrk(src + c->start, ".eE") && strpbrk(src + c->start, ".eE") < src + c->end)
            return NULL;
        v = round(v);
        return in_range(type, type_end, v) ? g_strdup_printf("%.0f", v) : NULL;
    }
    if (type_is(type, "number")) {
        if (!component_number(c, src, &v) || !in_range(type, type_end, v)) return NULL;
        return number_str(v);
    }
    if (type_is(type, "percentage")) {
        if (c->type != NS_CSS_COMPONENT_PERCENTAGE || !in_range(type, type_end, c->number))
            return NULL;
        char *num = number_str(c->number);
        char *out = g_strconcat(num, "%", NULL);
        g_free(num);
        return out;
    }
    if (type_is(type, "angle")) {
        if (c->type == NS_CSS_COMPONENT_NUMBER && c->number == 0) return g_strdup("0deg");
        if (c->type != NS_CSS_COMPONENT_DIMENSION) return NULL;
        static const struct { const char *unit; double deg; } units[] = {
            { "deg", 1 }, { "grad", 0.9 }, { "rad", 180.0 / G_PI }, { "turn", 360 },
        };
        for (gsize i = 0; i < G_N_ELEMENTS(units); i++)
            if (g_ascii_strcasecmp(c->value, units[i].unit) == 0) {
                char *num = number_str(c->number * units[i].deg);
                char *out = g_strconcat(num, "deg", NULL);
                g_free(num);
                return out;
            }
        return NULL;
    }
    if (type_is(type, "dashed-ident"))
        return c->type == NS_CSS_COMPONENT_IDENT && g_str_has_prefix(c->value, "--")
            ? component_text(c, src) : NULL;
    if (type_is(type, "custom-ident")) {
        gboolean ok = c->type == NS_CSS_COMPONENT_IDENT &&
                      !term_is_css_wide(c->value, strlen(c->value));
        if (ok && g_active_grammar) {
            char *lower = g_ascii_strdown(c->value, -1);
            ok = !ns_css_grammar_has_keyword(g_active_grammar, lower);
            g_free(lower);
        }
        return ok ? component_text(c, src) : NULL;
    }
    if (type_is(type, "url")) {
        gboolean is_url = c->type == NS_CSS_COMPONENT_FUNCTION &&
                          g_ascii_strcasecmp(c->value, "url") == 0;
        return is_url ? url_serialize(c, src) : NULL;
    }
    return NULL;
}

static char *
term_match(const char *term, const char *term_end, const ns_css_component *c,
           const char *src)
{
    if (*term == '<') return type_match(term, term_end, c, src);
    gsize n = (gsize)(term_end - term);
    if (n && term[n - 1] == '*') n--;
    if (c->type != NS_CSS_COMPONENT_IDENT || strlen(c->value) != n ||
        g_ascii_strncasecmp(c->value, term, n) != 0)
        return NULL;
    return g_ascii_strdown(c->value, -1);
}

static const char *
term_end_of(const char *p, const char *end)
{
    if (*p == '<') {
        const char *close = memchr(p, '>', (gsize)(end - p));
        return close ? close + 1 : end;
    }
    while (p < end && *p != ' ' && *p != ']') p++;
    return p;
}

static char *
group_match(const char *start, const char *end, const ns_css_component *c,
            const char *src, gboolean *omittable)
{
    const char *p = start;
    while (p < end) {
        while (p < end && *p == ' ') p++;
        if (p >= end) break;
        const char *te = term_end_of(p, end);
        char *hit = term_match(p, te, c, src);
        if (hit) {
            if (omittable) *omittable = te[-1] == '*';
            return hit;
        }
        p = te;
    }
    return NULL;
}

static const char *
group_close(const char *p, const char *end)
{
    for (; p < end; p++) {
        if (*p == '<') {
            p = memchr(p, '>', (gsize)(end - p));
            if (!p) return NULL;
        } else if (*p == ']') {
            return p;
        }
    }
    return NULL;
}

static int
parse_groups(const char *alt, const char *end, grammar_group *groups)
{
    int n = 0;
    const char *p = alt;
    while (p < end) {
        while (p < end && *p == ' ') p++;
        if (p >= end) break;
        gboolean required = *p == '!';
        if (required) p++;
        if (*p != '[' || n == GRAMMAR_GROUPS_MAX) return -1;
        const char *close = group_close(p + 1, end);
        if (!close) return -1;
        grammar_group *g = &groups[n++];
        g->start = p + 1;
        g->end = close;
        g->required = required;
        g->min_count = 0;
        g->max_count = 1;
        p = close + 1;
        if (p < end && *p == '{') {
            g->min_count = atoi(p + 1);
            const char *comma = memchr(p, ',', (gsize)(end - p));
            g->max_count = comma ? atoi(comma + 1) : g->min_count;
            p = memchr(p, '}', (gsize)(end - p));
            if (!p) return -1;
            p++;
        }
    }
    return n;
}

static char *
repeat_match(const grammar_group *g, GPtrArray *tokens, guint from, guint to,
             const char *src)
{
    guint count = to - from;
    if ((int)count < g->min_count || (int)count > g->max_count) return NULL;
    GString *out = g_string_new(NULL);
    for (guint i = from; i < to; i++) {
        char *hit = group_match(g->start, g->end, g_ptr_array_index(tokens, i), src, NULL);
        if (!hit) {
            g_string_free(out, TRUE);
            return NULL;
        }
        if (out->len) g_string_append_c(out, ' ');
        g_string_append(out, hit);
        g_free(hit);
    }
    return g_string_free(out, FALSE);
}

static char *
any_order_match(const grammar_group *groups, int n_groups, GPtrArray *tokens,
                guint from, guint to, const char *src)
{
    char *slot[GRAMMAR_GROUPS_MAX] = { 0 };
    gboolean omittable[GRAMMAR_GROUPS_MAX] = { 0 };
    gboolean ok = to > from;
    for (guint i = from; ok && i < to; i++) {
        const ns_css_component *c = g_ptr_array_index(tokens, i);
        char *hit = NULL;
        int k = 0;
        for (; k < n_groups && !hit; k++)
            if (!slot[k]) hit = group_match(groups[k].start, groups[k].end, c, src, &omittable[k]);
        if (hit) slot[k - 1] = hit;
        else ok = FALSE;
    }
    int filled = 0, kept = 0;
    for (int k = 0; ok && k < n_groups; k++) {
        if (groups[k].required && !slot[k]) ok = FALSE;
        if (slot[k]) filled++;
        if (slot[k] && !omittable[k]) kept++;
    }
    GString *out = g_string_new(NULL);
    for (int k = 0; ok && k < n_groups; k++) {
        if (!slot[k] || (omittable[k] && kept > 0 && filled > 1)) continue;
        if (out->len) g_string_append_c(out, ' ');
        g_string_append(out, slot[k]);
    }
    for (int k = 0; k < n_groups; k++) g_free(slot[k]);
    if (!ok) {
        g_string_free(out, TRUE);
        return NULL;
    }
    return g_string_free(out, FALSE);
}

static char *
alternative_match(const char *alt, const char *end, GPtrArray *tokens,
                  guint from, guint to, const char *src)
{
    while (alt < end && *alt == ' ') alt++;
    while (end > alt && end[-1] == ' ') end--;
    if (alt >= end || to <= from) return NULL;
    if (*alt != '[' && *alt != '!') {
        gboolean repeated = end[-1] == '+';
        const char *term_end = repeated ? end - 1 : end;
        if (!repeated && to - from != 1) return NULL;
        GString *out = g_string_new(NULL);
        for (guint i = from; i < to; i++) {
            char *hit = term_match(alt, term_end, g_ptr_array_index(tokens, i), src);
            if (!hit) {
                g_string_free(out, TRUE);
                return NULL;
            }
            if (out->len) g_string_append_c(out, ' ');
            g_string_append(out, hit);
            g_free(hit);
        }
        return g_string_free(out, FALSE);
    }
    grammar_group groups[GRAMMAR_GROUPS_MAX];
    int n = parse_groups(alt, end, groups);
    if (n <= 0) return NULL;
    if (n == 1 && groups[0].max_count > 1)
        return repeat_match(&groups[0], tokens, from, to, src);
    return any_order_match(groups, n, tokens, from, to, src);
}

static const char *
alternative_end(const char *p, const char *end)
{
    int depth = 0;
    for (; p < end; p++) {
        if (*p == '<') {
            const char *close = memchr(p, '>', (gsize)(end - p));
            if (close) p = close;
        } else if (*p == '[') depth++;
        else if (*p == ']') depth--;
        else if (*p == '|' && depth == 0) return p;
    }
    return end;
}

static char *
grammar_match(const char *grammar, const char *end, GPtrArray *tokens,
              guint from, guint to, const char *src)
{
    const char *p = grammar;
    while (p < end) {
        const char *alt_end = alternative_end(p, end);
        char *hit = alternative_match(p, alt_end, tokens, from, to, src);
        if (hit) return hit;
        p = alt_end < end ? alt_end + 1 : end;
    }
    return NULL;
}

static char *
list_match(const char *grammar, const char *end, GPtrArray *tokens, const char *src)
{
    GString *out = g_string_new(NULL);
    guint from = 0;
    for (guint i = 0; i <= tokens->len; i++) {
        const ns_css_component *c = i < tokens->len ? g_ptr_array_index(tokens, i) : NULL;
        if (c && c->type != NS_CSS_COMPONENT_COMMA) continue;
        char *hit = grammar_match(grammar, end, tokens, from, i, src);
        if (!hit) {
            g_string_free(out, TRUE);
            return NULL;
        }
        if (out->len) g_string_append(out, ", ");
        g_string_append(out, hit);
        g_free(hit);
        from = i + 1;
    }
    return g_string_free(out, FALSE);
}

static GPtrArray *
significant_tokens(GPtrArray *components)
{
    GPtrArray *tokens = g_ptr_array_new();
    for (guint i = 0; i < components->len; i++) {
        ns_css_component *c = g_ptr_array_index(components, i);
        if (c->type != NS_CSS_COMPONENT_WHITESPACE) g_ptr_array_add(tokens, c);
    }
    return tokens;
}

char *
ns_css_grammar_canonical(const char *grammar, const char *text)
{
    if (!grammar || !text) return NULL;
    gboolean valid = TRUE;
    GPtrArray *components = ns_css_component_values_parse(text, -1, &valid);
    GPtrArray *tokens = significant_tokens(components);
    char *out = NULL;
    gsize glen = strlen(grammar);
    gboolean is_list = glen > 2 && strcmp(grammar + glen - 2, " #") == 0;
    const char *outer_grammar = g_active_grammar;
    g_active_grammar = grammar;
    if (valid && tokens->len > 0) {
        out = is_list ? list_match(grammar, grammar + glen - 2, tokens, text)
                      : grammar_match(grammar, grammar + glen, tokens, 0, tokens->len, text);
    }
    g_active_grammar = outer_grammar;
    g_ptr_array_free(tokens, TRUE);
    g_ptr_array_free(components, TRUE);
    return out;
}

gboolean
ns_css_grammar_has_keyword(const char *grammar, const char *keyword)
{
    if (!grammar || !keyword || !*keyword) return FALSE;
    gsize n = strlen(keyword);
    for (const char *p = grammar; (p = strstr(p, keyword)); p += n) {
        gboolean start_ok = p == grammar || strchr(" [|!", p[-1]);
        gboolean end_ok = !p[n] || strchr(" ]*|+{", p[n]);
        if (start_ok && end_ok) return TRUE;
    }
    return FALSE;
}
