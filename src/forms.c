/* Nordstjernen — HTML form validation, serialization, and submission helpers.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include <glib.h>
#include <string.h>

#include "forms.h"
#include "dom.h"
#include "net.h"

static void
ns_clear_radio_group(ns_node *root, const ns_node *doc, const ns_node *owner,
                     const char *name, const ns_node *keep)
{
    if (!root) return;
    if (ns_node_is_element_named(root, "input") && root != keep) {
        const char *type = ns_element_get_attr(root, "type");
        const char *grp = ns_element_get_attr(root, "name");
        if (type && grp && g_ascii_strcasecmp(type, "radio") == 0 &&
            strcmp(grp, name) == 0 &&
            ns_form_owner(root, doc) == owner)
            ns_element_remove_attr(root, "checked");
    }
    for (ns_node *c = root->first_child; c; c = c->next_sibling)
        ns_clear_radio_group(c, doc, owner, name, keep);
}

void
ns_clear_radio_group_for(const ns_node *doc, const ns_node *keep)
{
    if (!keep) return;
    const char *name = ns_element_get_attr(keep, "name");
    if (!name) return;
    const ns_node *d = doc ? doc : ns_node_root(keep);
    const ns_node *owner = ns_form_owner(keep, d);
    ns_node *root = (ns_node *)(d ? d : ns_node_root(keep));
    if (!root) root = (ns_node *)keep;
    ns_clear_radio_group(root, d, owner, name, keep);
}

gboolean
ns_form_is_submit_trigger(const ns_node *n)
{
    if (!n || n->kind != NS_NODE_ELEMENT || !n->name) return FALSE;
    if (strcmp(n->name, "button") == 0) {
        const char *type = ns_element_get_attr(n, "type");
        return !type || g_ascii_strcasecmp(type, "submit") == 0;
    }
    if (strcmp(n->name, "input") == 0) {
        const char *type = ns_element_get_attr(n, "type");
        return type && (g_ascii_strcasecmp(type, "submit") == 0 ||
                        g_ascii_strcasecmp(type, "image") == 0);
    }
    return FALSE;
}

gboolean
ns_form_is_reset_trigger(const ns_node *n)
{
    if (!n || n->kind != NS_NODE_ELEMENT || !n->name) return FALSE;
    const char *type = ns_element_get_attr(n, "type");
    if (!type || g_ascii_strcasecmp(type, "reset") != 0) return FALSE;
    return strcmp(n->name, "button") == 0 ||
           strcmp(n->name, "input") == 0;
}

static gboolean
form_control_belongs_to(const ns_node *form, const ns_node *control,
                        const ns_node *doc)
{
    return form && control && ns_form_owner(control, doc) == form;
}

static gboolean
form_option_disabled(const ns_node *option)
{
    if (ns_element_effectively_disabled(option)) return TRUE;
    for (const ns_node *p = option ? option->parent : NULL; p; p = p->parent) {
        if (ns_node_is_element_named(p, "select")) return FALSE;
        if (ns_node_is_element_named(p, "optgroup") &&
            ns_element_get_attr(p, "disabled"))
            return TRUE;
    }
    return FALSE;
}

static GPtrArray *
form_selected_options(const ns_node *select)
{
    GPtrArray *out = g_ptr_array_new();
    if (!select) return out;
    if (!ns_element_get_attr(select, "multiple")) {
        const ns_node *opt = ns_select_chosen_option(select);
        if (opt && !form_option_disabled(opt))
            g_ptr_array_add(out, (gpointer)opt);
        return out;
    }
    for (const ns_node *c = select->first_child; c; c = c->next_sibling) {
        if (ns_node_is_element_named(c, "optgroup")) {
            if (ns_element_effectively_disabled(c) ||
                ns_element_get_attr(c, "disabled"))
                continue;
            for (const ns_node *cc = c->first_child; cc; cc = cc->next_sibling) {
                if (ns_node_is_element_named(cc, "option") &&
                    ns_element_get_attr(cc, "selected") &&
                    !form_option_disabled(cc))
                    g_ptr_array_add(out, (gpointer)cc);
            }
        } else if (ns_node_is_element_named(c, "option") &&
                   ns_element_get_attr(c, "selected") &&
                   !form_option_disabled(c)) {
            g_ptr_array_add(out, (gpointer)c);
        }
    }
    return out;
}

gboolean
ns_form_has_file_upload(const ns_node *form, const ns_node *n, const ns_node *doc)
{
    if (!n) return FALSE;
    if (ns_node_is_element_named(n, "input") &&
        form_control_belongs_to(form, n, doc)) {
        const char *type = ns_element_get_attr(n, "type");
        if (type && g_ascii_strcasecmp(type, "file") == 0 &&
            !ns_element_effectively_disabled(n) &&
            ns_element_get_attr(n, "data-nd-file-path"))
            return TRUE;
    }
    for (const ns_node *c = n->first_child; c; c = c->next_sibling)
        if (ns_form_has_file_upload(form, c, doc)) return TRUE;
    return FALSE;
}

static void
multipart_append_field(GString *body, const char *boundary,
                       const char *name, const char *value)
{
    g_string_append_printf(body, "--%s\r\n", boundary);
    g_string_append(body, "Content-Disposition: form-data; name=\"");
    ns_multipart_quote_field(body, name ? name : "");
    g_string_append(body, "\"\r\n\r\n");
    if (value) g_string_append(body, value);
    g_string_append(body, "\r\n");
}

static gboolean
multipart_append_file(GString *body, const char *boundary,
                      const char *name, const char *path)
{
    if (!path || !*path) {
        g_string_append_printf(body, "--%s\r\n", boundary);
        g_string_append(body, "Content-Disposition: form-data; name=\"");
        ns_multipart_quote_field(body, name ? name : "");
        g_string_append(body,
            "\"; filename=\"\"\r\n"
            "Content-Type: application/octet-stream\r\n\r\n\r\n");
        return TRUE;
    }
    char *contents = NULL;
    gsize len = 0;
    GError *err = NULL;
    if (!g_file_get_contents(path, &contents, &len, &err)) {
        if (err) g_error_free(err);
        return FALSE;
    }
    if (len > (gsize)G_MAXSSIZE) {
        g_free(contents);
        return FALSE;
    }
    const char *base = strrchr(path, '/');
#ifdef G_OS_WIN32
    const char *base_w = strrchr(path, '\\');
    if (!base || (base_w && base_w > base)) base = base_w;
#endif
    const char *fname = base ? base + 1 : path;
    g_autofree char *mime = g_content_type_guess(path, (const guchar *)contents,
                                                  len < 4096 ? len : 4096, NULL);
    g_autofree char *type = mime ? g_content_type_get_mime_type(mime) : NULL;
    g_string_append_printf(body, "--%s\r\n", boundary);
    g_string_append(body, "Content-Disposition: form-data; name=\"");
    ns_multipart_quote_field(body, name ? name : "");
    g_string_append(body, "\"; filename=\"");
    ns_multipart_quote_field(body, fname);
    g_string_append_printf(body, "\"\r\nContent-Type: %s\r\n\r\n",
        type && *type ? type : "application/octet-stream");
    g_string_append_len(body, contents, (gssize)len);
    g_string_append(body, "\r\n");
    g_free(contents);
    return TRUE;
}

void
ns_form_collect_multipart(const ns_node *form, const ns_node *n,
                       const ns_node *doc, GString *body,
                       const char *boundary, const ns_node *submitter)
{
    if (!n) return;
    if (n->kind == NS_NODE_ELEMENT && n->name) {
        gboolean is_input    = strcmp(n->name, "input") == 0;
        gboolean is_textarea = strcmp(n->name, "textarea") == 0;
        gboolean is_select   = strcmp(n->name, "select") == 0;
        gboolean is_button   = strcmp(n->name, "button") == 0;
        if (is_input || is_textarea || is_select || is_button) {
            if (!form_control_belongs_to(form, n, doc)) goto recurse_mp;
            const char *name = ns_element_get_attr(n, "name");
            if (!name || !*name) goto recurse_mp;
            if (ns_element_effectively_disabled(n)) goto recurse_mp;
            if (is_input) {
                const char *type = ns_element_get_attr(n, "type");
                if (type && g_ascii_strcasecmp(type, "file") == 0) {
                    const char *path = ns_element_get_attr(n, "data-nd-file-path");
                    multipart_append_file(body, boundary, name, path);
                    goto recurse_mp;
                }
                if (type && (g_ascii_strcasecmp(type, "checkbox") == 0 ||
                             g_ascii_strcasecmp(type, "radio") == 0)) {
                    if (!ns_element_get_attr(n, "checked")) goto recurse_mp;
                }
                if (type && (g_ascii_strcasecmp(type, "submit") == 0 ||
                             g_ascii_strcasecmp(type, "image") == 0)) {
                    if (n == submitter) {
                        const char *v = ns_element_get_attr(n, "value");
                        multipart_append_field(body, boundary, name,
                                               v ? v : "Submit");
                    }
                    goto recurse_mp;
                }
                if (type && (g_ascii_strcasecmp(type, "button") == 0 ||
                             g_ascii_strcasecmp(type, "reset")  == 0))
                    goto recurse_mp;
                const char *value = ns_element_get_attr(n, "value");
                if (!value && type &&
                    (g_ascii_strcasecmp(type, "checkbox") == 0 ||
                     g_ascii_strcasecmp(type, "radio") == 0))
                    value = "on";
                multipart_append_field(body, boundary, name, value);
            } else if (is_textarea) {
                char *text = ns_node_collect_text(n);
                multipart_append_field(body, boundary, name, text ? text : "");
                g_free(text);
            } else if (is_select) {
                GPtrArray *opts = form_selected_options(n);
                for (guint i = 0; i < opts->len; i++) {
                    char *v = ns_option_value_dup(g_ptr_array_index(opts, i));
                    multipart_append_field(body, boundary, name, v ? v : "");
                    g_free(v);
                }
                g_ptr_array_free(opts, TRUE);
                goto recurse_mp;
            } else if (is_button) {
                const char *type = ns_element_get_attr(n, "type");
                gboolean acts_as_submit = !type ||
                                          g_ascii_strcasecmp(type, "submit") == 0;
                if (acts_as_submit && n == submitter) {
                    const char *v = ns_element_get_attr(n, "value");
                    if (!v) {
                        char *text = ns_node_collect_text(n);
                        multipart_append_field(body, boundary, name,
                                               text ? text : "");
                        g_free(text);
                    } else {
                        multipart_append_field(body, boundary, name, v);
                    }
                }
                goto recurse_mp;
            }
        }
    }
recurse_mp:
    for (const ns_node *c = n->first_child; c; c = c->next_sibling)
        ns_form_collect_multipart(form, c, doc, body, boundary, submitter);
}

void
ns_form_collect_inputs(const ns_node *form, const ns_node *n, const ns_node *doc,
                    GString *query, gboolean *first, const ns_node *submitter)
{
    if (!n) return;
    if (n->kind == NS_NODE_ELEMENT && n->name) {
        gboolean is_input    = strcmp(n->name, "input") == 0;
        gboolean is_textarea = strcmp(n->name, "textarea") == 0;
        gboolean is_select   = strcmp(n->name, "select") == 0;
        gboolean is_button   = strcmp(n->name, "button") == 0;
        if (is_input || is_textarea || is_select || is_button) {
            if (!form_control_belongs_to(form, n, doc)) goto recurse;
            const char *name = ns_element_get_attr(n, "name");
            if (!name || !*name) goto recurse;
            if (ns_element_effectively_disabled(n)) goto recurse;
            if (is_input) {
                const char *type = ns_element_get_attr(n, "type");
                if (type && (g_ascii_strcasecmp(type, "checkbox") == 0 ||
                             g_ascii_strcasecmp(type, "radio") == 0)) {
                    if (!ns_element_get_attr(n, "checked")) goto recurse;
                }
                if (type && (g_ascii_strcasecmp(type, "submit") == 0 ||
                             g_ascii_strcasecmp(type, "image") == 0)) {
                    if (n == submitter) {
                        const char *v = ns_element_get_attr(n, "value");
                        ns_form_urlencoded_append_pair(query, first, name, v ? v : "Submit");
                    }
                    goto recurse;
                }
                if (type && (g_ascii_strcasecmp(type, "button") == 0 ||
                             g_ascii_strcasecmp(type, "reset")  == 0 ||
                             g_ascii_strcasecmp(type, "file")   == 0))
                    goto recurse;
                const char *value = ns_element_get_attr(n, "value");
                if (!value && type &&
                    (g_ascii_strcasecmp(type, "checkbox") == 0 ||
                     g_ascii_strcasecmp(type, "radio") == 0))
                    value = "on";
                ns_form_urlencoded_append_pair(query, first, name, value);
            } else if (is_textarea) {
                char *text = ns_node_collect_text(n);
                ns_form_urlencoded_append_pair(query, first, name, text ? text : "");
                g_free(text);
            } else if (is_select) {
                GPtrArray *opts = form_selected_options(n);
                for (guint i = 0; i < opts->len; i++) {
                    char *v = ns_option_value_dup(g_ptr_array_index(opts, i));
                    ns_form_urlencoded_append_pair(query, first, name, v ? v : "");
                    g_free(v);
                }
                g_ptr_array_free(opts, TRUE);
                goto recurse;
            } else if (is_button) {
                const char *type = ns_element_get_attr(n, "type");
                gboolean acts_as_submit = !type || g_ascii_strcasecmp(type, "submit") == 0;
                if (acts_as_submit && n == submitter) {
                    const char *v = ns_element_get_attr(n, "value");
                    if (!v) {
                        char *text = ns_node_collect_text(n);
                        ns_form_urlencoded_append_pair(query, first, name, text ? text : "");
                        g_free(text);
                    } else {
                        ns_form_urlencoded_append_pair(query, first, name, v);
                    }
                }
                goto recurse;
            }
        }
    }
recurse:
    for (const ns_node *c = n->first_child; c; c = c->next_sibling)
        ns_form_collect_inputs(form, c, doc, query, first, submitter);
}

static gboolean
ns_value_matches_pattern(const char *value, const char *pattern)
{
    if (!pattern || !*pattern) return TRUE;
    char *anchored = g_strdup_printf("^(?:%s)$", pattern);
    GError *err = NULL;
    GRegex *re = g_regex_new(anchored, 0, 0, &err);
    g_free(anchored);
    if (!re) { g_clear_error(&err); return TRUE; }
    gboolean ok = g_regex_match(re, value ? value : "", 0, NULL);
    g_regex_unref(re);
    return ok;
}

static gboolean
ns_value_matches_type(const ns_node *n, const char *value, const char *type)
{
    if (!value || !*value || !type) return TRUE;
    if (g_ascii_strcasecmp(type, "email") == 0)
        return ns_input_email_value_valid(n, value);
    if (g_ascii_strcasecmp(type, "url") == 0)
        return ns_url_is_valid_absolute(value);
    if (ns_input_type_has_number_value(type))
        return ns_input_value_to_number(type, value, NULL);
    return TRUE;
}

static gboolean
ns_value_matches_range(const ns_node *n, const char *value, const char *type)
{
    (void)type;
    gboolean under = FALSE, over = FALSE;
    if (!ns_input_value_range_state(n, value, &under, &over)) return TRUE;
    return !under && !over;
}

static gboolean
ns_value_matches_step(const ns_node *n, const char *value, const char *type)
{
    (void)type;
    return !ns_input_value_step_mismatch(n, value);
}

const ns_node *
ns_form_first_invalid(const ns_node *form, const ns_node *n, const ns_node *doc)
{
    if (!n) return NULL;
    if (n->kind == NS_NODE_ELEMENT && n->name) {
        gboolean is_input    = strcmp(n->name, "input") == 0;
        gboolean is_textarea = strcmp(n->name, "textarea") == 0;
        gboolean is_select   = strcmp(n->name, "select") == 0;
        if (is_input || is_textarea || is_select) {
            if (form_control_belongs_to(form, n, doc) &&
                !ns_element_effectively_disabled(n) &&
                !ns_form_control_readonly_bars_validation(n)) {
                const char *type = is_input ? ns_element_get_attr(n, "type") : NULL;
                gboolean skip = type && (g_ascii_strcasecmp(type, "submit") == 0 ||
                                         g_ascii_strcasecmp(type, "button") == 0 ||
                                         g_ascii_strcasecmp(type, "reset")  == 0 ||
                                         g_ascii_strcasecmp(type, "image")  == 0 ||
                                         g_ascii_strcasecmp(type, "hidden") == 0);
                if (!skip) {
                    const char *custom = ns_element_get_attr(n, NS_CUSTOM_VALIDITY_ATTR);
                    if (custom && *custom) return n;
                    const char *value;
                    char *collected = NULL;
                    if (is_textarea) {
                        collected = ns_node_collect_text(n);
                        value = collected ? collected : "";
                    } else if (is_select) {
                        GPtrArray *opts = form_selected_options(n);
                        if (opts->len > 0)
                            collected = ns_option_value_dup(g_ptr_array_index(opts, 0));
                        g_ptr_array_free(opts, TRUE);
                        value = collected ? collected : "";
                    } else {
                        value = ns_element_get_attr(n, "value");
                        if (!value) value = "";
                    }
                    gboolean required = ns_form_control_supports_required(n) &&
                                        ns_element_get_attr(n, "required") != NULL;
                    if (required && ns_form_control_value_missing(n, value, doc)) {
                        g_free(collected);
                        return n;
                    }
                    if (*value) {
                        const char *pattern = ns_element_get_attr(n, "pattern");
                        if (is_input &&
                            ns_input_type_supports_text_constraints(type) &&
                            !ns_value_matches_pattern(value, pattern)) {
                            g_free(collected);
                            return n;
                        }
                        if (is_input && !ns_value_matches_type(n, value, type)) {
                            g_free(collected);
                            return n;
                        }
                        if (is_input && !ns_value_matches_range(n, value, type)) {
                            g_free(collected);
                            return n;
                        }
                        if (is_input && !ns_value_matches_step(n, value, type)) {
                            g_free(collected);
                            return n;
                        }
                        if (ns_form_control_length_limits_apply(n)) {
                            const char *minlen = ns_element_get_attr(n, "minlength");
                            const char *maxlen = ns_element_get_attr(n, "maxlength");
                            glong vlen = (glong)g_utf8_strlen(value, -1);
                            if (minlen) {
                                glong mn = (glong)ns_parse_int(minlen, 0, 0, 1000000);
                                if (vlen < mn) { g_free(collected); return n; }
                            }
                            if (maxlen) {
                                glong mx = (glong)ns_parse_int(maxlen, 0, 0, 1000000);
                                if (vlen > mx) { g_free(collected); return n; }
                            }
                        }
                    }
                    g_free(collected);
                }
            }
        }
    }
    for (const ns_node *c = n->first_child; c; c = c->next_sibling) {
        const ns_node *m = ns_form_first_invalid(form, c, doc);
        if (m) return m;
    }
    return NULL;
}
