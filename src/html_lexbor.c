/* Nordstjernen — lexbor-backed HTML parser.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "html.h"

#include <string.h>

#include <lexbor/dom/dom.h>
#include <lexbor/html/html.h>
#include <lexbor/html/interfaces/template_element.h>

static void
lxb_doc_destroy_void(void *p)
{
    if (p) lxb_html_document_destroy((lxb_html_document_t *)p);
}

static void
lxb_borrow_attributes(lxb_dom_element_t *el, ns_node *out)
{
    lxb_dom_attr_t *attr = lxb_dom_element_first_attribute(el);
    while (attr) {
        size_t klen = 0, vlen = 0;
        const lxb_char_t *k = lxb_dom_attr_qualified_name(attr, &klen);
        const lxb_char_t *v = lxb_dom_attr_value(attr, &vlen);
        if (k && klen > 0 && !ns_attr_name_is_internal((const char *)k)) {
            (void)klen;
            (void)vlen;
            ns_element_append_attr_borrow(out,
                (const char *)k,
                v ? (const char *)v : "");
        }
        attr = lxb_dom_element_next_attribute(attr);
    }
}

static ns_node *
lxb_node_convert(lxb_dom_node_t *src)
{
    switch (src->type) {
    case LXB_DOM_NODE_TYPE_DOCUMENT:
    case LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT:
        return ns_node_new_document();
    case LXB_DOM_NODE_TYPE_ELEMENT: {
        lxb_dom_element_t *el = lxb_dom_interface_element(src);
        size_t nlen = 0;
        const lxb_char_t *name = lxb_dom_element_qualified_name(el, &nlen);
        (void)nlen;
        ns_node *out = ns_node_new_element(NULL);
        ns_node_set_name_borrow(out, name ? (const char *)name : "unknown");
        lxb_borrow_attributes(el, out);
        return out;
    }
    case LXB_DOM_NODE_TYPE_TEXT:
    case LXB_DOM_NODE_TYPE_CDATA_SECTION: {
        lxb_dom_character_data_t *cd = lxb_dom_interface_character_data(src);
        ns_node *out = ns_node_new_text(NULL);
        ns_node_set_text_borrow(out, cd->data.data ? (const char *)cd->data.data : "");
        return out;
    }
    case LXB_DOM_NODE_TYPE_COMMENT: {
        lxb_dom_character_data_t *cd = lxb_dom_interface_character_data(src);
        ns_node *out = ns_node_new_comment(NULL);
        ns_node_set_text_borrow(out, cd->data.data ? (const char *)cd->data.data : "");
        return out;
    }
    case LXB_DOM_NODE_TYPE_DOCUMENT_TYPE: {
        lxb_dom_document_type_t *dt = lxb_dom_interface_document_type(src);
        size_t nlen = 0, plen = 0, slen = 0;
        const lxb_char_t *name = lxb_dom_document_type_name(dt, &nlen);
        const lxb_char_t *pub = lxb_dom_document_type_public_id(dt, &plen);
        const lxb_char_t *sys = lxb_dom_document_type_system_id(dt, &slen);
        ns_node *out = ns_node_new_element(NULL);
        ns_node_set_name_borrow(out,
            name && nlen ? (const char *)name : "html");
        ns_element_set_attr(out, "publicId",
            pub && plen ? (const char *)pub : "");
        ns_element_set_attr(out, "systemId",
            sys && slen ? (const char *)sys : "");
        out->kind = NS_NODE_DOCTYPE;
        return out;
    }
    case LXB_DOM_NODE_TYPE_PROCESSING_INSTRUCTION:
    case LXB_DOM_NODE_TYPE_ATTRIBUTE:
    case LXB_DOM_NODE_TYPE_ENTITY_REFERENCE:
    case LXB_DOM_NODE_TYPE_ENTITY:
    case LXB_DOM_NODE_TYPE_NOTATION:
    default:
        return NULL;
    }
}

static lxb_dom_node_t *
lxb_template_content_first_child(lxb_dom_node_t *src)
{
    if (src->type != LXB_DOM_NODE_TYPE_ELEMENT) return NULL;
    if (src->ns != LXB_NS_HTML) return NULL;
    if (src->local_name != LXB_TAG_TEMPLATE) return NULL;
    lxb_html_template_element_t *tpl = lxb_html_interface_template(src);
    if (!tpl || !tpl->content) return NULL;
    return tpl->content->node.first_child;
}

typedef struct lxb_walk_frame {
    lxb_dom_node_t *src_child;
    ns_node        *ns_parent;
} lxb_walk_frame;

static void
lxb_walk_push(GArray *stack, lxb_dom_node_t *child, ns_node *parent)
{
    if (!child || !parent) return;
    lxb_walk_frame fr = { .src_child = child, .ns_parent = parent };
    g_array_append_val(stack, fr);
}

static void
lxb_walk_into(lxb_dom_node_t *src_root, ns_node *ns_root)
{
    GArray *stack = g_array_new(FALSE, FALSE, sizeof(lxb_walk_frame));
    lxb_walk_push(stack, src_root->first_child, ns_root);
    lxb_walk_push(stack, lxb_template_content_first_child(src_root), ns_root);
    while (stack->len > 0) {
        lxb_walk_frame fr = g_array_index(stack, lxb_walk_frame, stack->len - 1);
        g_array_set_size(stack, stack->len - 1);
        lxb_dom_node_t *src = fr.src_child;
        ns_node *parent = fr.ns_parent;
        while (src) {
            lxb_dom_node_t *next = src->next;
            ns_node *converted = lxb_node_convert(src);
            if (converted) {
                ns_node_append_child(parent, converted);
                lxb_dom_node_t *kids = src->first_child;
                lxb_dom_node_t *tpl_kids = lxb_template_content_first_child(src);
                if (next) lxb_walk_push(stack, next, parent);
                if (tpl_kids) lxb_walk_push(stack, tpl_kids, converted);
                if (kids) {
                    src = kids;
                    parent = converted;
                    continue;
                }
            } else if (next) {
                src = next;
                continue;
            }
            src = NULL;
        }
    }
    g_array_free(stack, TRUE);
}

static ns_node *
lxb_to_nd_root(lxb_dom_node_t *root)
{
    if (!root) return NULL;
    ns_node *out = lxb_node_convert(root);
    if (!out) return NULL;
    lxb_walk_into(root, out);
    return out;
}

static void
ns_dsd_convert(ns_node *n, int depth)
{
    if (!n || depth >= 512) return;
    for (ns_node *c = n->first_child; c; c = c->next_sibling) {
        if (c->kind == NS_NODE_ELEMENT && c->name &&
            g_ascii_strcasecmp(c->name, "template") == 0) {
            const char *mode = ns_element_get_attr(c, "shadowrootmode");
            if (!mode) mode = ns_element_get_attr(c, "shadowroot");
            if (mode && (g_ascii_strcasecmp(mode, "open") == 0 ||
                         g_ascii_strcasecmp(mode, "closed") == 0)) {
                ns_node_set_name_borrow(c, "div");
                ns_element_set_attr(c, NS_SHADOW_ATTR,
                    g_ascii_strcasecmp(mode, "closed") == 0 ? "closed" : "open");
            }
        }
        ns_dsd_convert(c, depth + 1);
    }
}

static void
text_descendants_append(const ns_node *n, GString *out, int depth)
{
    if (!n || !out || depth >= 64) return;
    if (n->kind == NS_NODE_TEXT && n->text)
        g_string_append(out, n->text);
    for (const ns_node *c = n->first_child; c; c = c->next_sibling)
        text_descendants_append(c, out, depth + 1);
}

static char *
script_text(const ns_node *n)
{
    GString *out = g_string_new(NULL);
    text_descendants_append(n, out, 0);
    return g_string_free(out, FALSE);
}

static gboolean
range_has(const char *start, const char *end, const char *needle)
{
    gsize nlen = strlen(needle);
    if (!start || !end || end <= start || nlen == 0) return FALSE;
    for (const char *p = start; p + nlen <= end; p++)
        if (memcmp(p, needle, nlen) == 0)
            return TRUE;
    return FALSE;
}

static char *
json_string_unescape(const char *p)
{
    if (!p) return NULL;
    GString *out = g_string_new(NULL);
    for (; *p && *p != '"'; p++) {
        if (*p != '\\') {
            g_string_append_c(out, *p);
            continue;
        }
        p++;
        if (!*p) break;
        if (*p == '/' || *p == '"' || *p == '\\') {
            g_string_append_c(out, *p);
        } else if (*p == 'n') {
            g_string_append_c(out, '\n');
        } else if (*p == 't') {
            g_string_append_c(out, '\t');
        } else if (*p == 'u' &&
                   g_ascii_isxdigit(p[1]) && g_ascii_isxdigit(p[2]) &&
                   g_ascii_isxdigit(p[3]) && g_ascii_isxdigit(p[4])) {
            char hex[5] = { p[1], p[2], p[3], p[4], 0 };
            gunichar ch = (gunichar)g_ascii_strtoull(hex, NULL, 16);
            if (ch) {
                char buf[8] = {0};
                gint len = g_unichar_to_utf8(ch, buf);
                g_string_append_len(out, buf, len);
            }
            p += 4;
        } else {
            g_string_append_c(out, *p);
        }
    }
    return g_string_free(out, FALSE);
}

static const char *
json_string_value_for_key(const char *text, const char *key, const char **out_key)
{
    const char *p = text;
    gsize klen = key ? strlen(key) : 0;
    if (!text || !key || klen == 0) return NULL;
    while ((p = strstr(p, key))) {
        const char *q = p + klen;
        while (g_ascii_isspace(*q)) q++;
        if (*q == ':') {
            q++;
            while (g_ascii_isspace(*q)) q++;
            if (*q == '"') {
                if (out_key) *out_key = p;
                return q + 1;
            }
        }
        p += klen;
    }
    return NULL;
}

static char *
json_first_url_for_key(const char *text, const char *key)
{
    const char *value = json_string_value_for_key(text, key, NULL);
    return value ? json_string_unescape(value) : NULL;
}

static int
media_url_score(const char *url, const char *obj_start, const char *obj_end)
{
    if (!url || !g_str_has_prefix(url, "http")) return -1;
    int score = 0;
    if (strstr(url, ".m3u8")) score += 40;
    if (strstr(url, "get_media")) score += 35;
    if (strstr(url, ".mp4")) score += 20;
    if (range_has(obj_start, obj_end, "\"defaultQuality\":true")) score += 100;
    if (range_has(obj_start, obj_end, "\"format\":\"hls\"")) score += 15;
    return score;
}

static char *
script_media_url(const char *text)
{
    static const char key[] = "\"videoUrl\"";
    char *best = NULL;
    int best_score = -1;
    const char *p = text;
    const char *match = NULL;
    while (p && (p = json_string_value_for_key(p, key, &match))) {
        const char *value = p;
        char *url = json_string_unescape(value);
        const char *obj_start = match;
        while (obj_start > text && *obj_start != '{') obj_start--;
        const char *obj_end = match;
        while (*obj_end && *obj_end != '}') obj_end++;
        int score = media_url_score(url, obj_start, obj_end);
        if (score > best_score) {
            g_free(best);
            best = url;
            best_score = score;
        } else {
            g_free(url);
        }
        p = value;
    }
    return best;
}

static gboolean
attr_contains_word(const ns_node *n, const char *attr, const char *word)
{
    const char *v = ns_element_get_attr(n, attr);
    return v && word && g_strstr_len(v, -1, word) != NULL;
}

static ns_node *
script_media_target(ns_node *script)
{
    for (ns_node *p = script ? script->parent : NULL; p; p = p->parent) {
        if (p->kind != NS_NODE_ELEMENT || !p->name) continue;
        if (strcmp(p->name, "head") == 0 || strcmp(p->name, "body") == 0 ||
            strcmp(p->name, "html") == 0)
            break;
        if (attr_contains_word(p, "id", "player") ||
            attr_contains_word(p, "class", "player") ||
            attr_contains_word(p, "class", "video"))
            return p;
    }
    return NULL;
}

static void
ns_media_metadata_convert(ns_node *n, int depth)
{
    if (!n || depth >= 512) return;
    if (n->kind == NS_NODE_ELEMENT && n->name &&
        strcmp(n->name, "script") == 0) {
        g_autofree char *text = script_text(n);
        if (text && strstr(text, "mediaDefinitions") && strstr(text, "videoUrl")) {
            g_autofree char *media_url = script_media_url(text);
            if (media_url && *media_url) {
                ns_node *target = script_media_target(n);
                if (target && !ns_element_get_attr(target, NS_MEDIA_SRC_ATTR)) {
                    g_autofree char *poster =
                        json_first_url_for_key(text, "\"image_url\"");
                    ns_element_set_attr(target, NS_MEDIA_SRC_ATTR, media_url);
                    if (poster && *poster)
                        ns_element_set_attr(target, NS_MEDIA_POSTER_ATTR, poster);
                }
            }
        }
    }
    for (ns_node *c = n->first_child; c; c = c->next_sibling)
        ns_media_metadata_convert(c, depth + 1);
}

ns_node *
ns_html_parse(const char *input, gssize len)
{
    if (!input) return NULL;
    size_t n = (len < 0) ? strlen(input) : (size_t)len;
    lxb_html_document_t *doc = lxb_html_document_create();
    if (!doc) return NULL;
    lxb_html_document_dom_opt_set(doc, LXB_DOM_DOCUMENT_OPT_WO_EVENTS);
    lxb_html_document_scripting_set(doc, true);
    lxb_status_t status = lxb_html_document_parse(doc,
                                                  (const lxb_char_t *)input, n);
    if (status != LXB_STATUS_OK) {
        lxb_html_document_destroy(doc);
        return NULL;
    }
    ns_node *root = lxb_to_nd_root(lxb_dom_interface_node(doc));
    if (!root) {
        lxb_html_document_destroy(doc);
        return NULL;
    }
    if (doc->dom_document.compat_mode == LXB_DOM_DOCUMENT_CMODE_QUIRKS)
        root->flags |= NS_NODE_QUIRKS;
    else if (doc->dom_document.compat_mode == LXB_DOM_DOCUMENT_CMODE_LIMITED_QUIRKS)
        root->flags |= NS_NODE_LIMITED_QUIRKS;
    ns_dsd_convert(root, 0);
    ns_media_metadata_convert(root, 0);
    ns_node_attach_backing(root, doc, lxb_doc_destroy_void);
    return root;
}

static lxb_tag_id_t
lxb_tag_id_from_name(lxb_html_document_t *doc, const char *name)
{
    if (!name || !*name) return LXB_TAG_BODY;
    lexbor_hash_t *hash = doc->dom_document.tags;
    const lxb_tag_data_t *data = lxb_tag_data_by_name(hash,
        (const lxb_char_t *)name, strlen(name));
    if (!data) return LXB_TAG_BODY;
    return data->tag_id;
}

ns_node *
ns_html_parse_fragment_in(const char *context_tag,
                          const char *input, gssize len)
{
    if (!input) return NULL;
    size_t n = (len < 0) ? strlen(input) : (size_t)len;
    lxb_html_parser_t *parser = lxb_html_parser_create();
    if (!parser || lxb_html_parser_init(parser) != LXB_STATUS_OK) {
        if (parser) lxb_html_parser_destroy(parser);
        return NULL;
    }
    lxb_html_parser_dom_opt_set(parser, LXB_DOM_DOCUMENT_OPT_WO_EVENTS);
    lxb_html_parser_scripting_set(parser, true);
    lxb_html_document_t *doc = lxb_html_document_create();
    if (!doc) {
        lxb_html_parser_destroy(parser);
        return NULL;
    }
    lxb_html_document_scripting_set(doc, true);
    lxb_tag_id_t tag_id = lxb_tag_id_from_name(doc, context_tag);
    lxb_dom_node_t *frag = lxb_html_parse_fragment_by_tag_id(
        parser, doc, tag_id, LXB_NS_HTML,
        (const lxb_char_t *)input, n);
    lxb_html_parser_destroy(parser);
    if (!frag) {
        lxb_html_document_destroy(doc);
        return NULL;
    }
    ns_node *out = ns_node_new_document();
    lxb_walk_into(frag, out);
    ns_media_metadata_convert(out, 0);
    ns_node_attach_backing(out, doc, lxb_doc_destroy_void);
    return out;
}
