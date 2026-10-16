/* quickjs-intl-data.c: lookup of generated CLDR locale data with locale inheritance. */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "quickjs-intl.h"
#include "quickjs-intl-cldr-data.h"

static int find_key(const char *key)
{
    int lo = 0, hi = countof(intl_data_keys) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(intl_data_key_pool + intl_data_keys[mid], key);
        if (c == 0)
            return mid;
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

static int find_node(const char *name)
{
    int lo = 0, hi = countof(intl_data_nodes) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(intl_data_nodes[mid].name, name);
        if (c == 0)
            return mid;
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

int intl_data_root(void)
{
    return find_node("und");
}

int intl_data_node(const char *tag)
{
    IntlLangId id, max;
    char buf[INTL_TAG_MAX];
    DynBuf b;
    int n;
    if (!intl_parse_lang_id(tag, intl_base_name_len(tag), &id))
        return intl_data_root();
    if (id.nvariants) {
        dbuf_init(&b);
        intl_format_lang_id(&b, &id);
        dbuf_putc(&b, '\0');
        n = b.error ? -1 : find_node((const char *)b.buf);
        dbuf_free(&b);
        if (n >= 0)
            return n;
    }
    max = id;
    max.nvariants = 0;
    if (!intl_add_likely_subtags(&max)) {
        max = id;
        max.nvariants = 0;
    }
    if (max.script[0] && max.region[0]) {
        snprintf(buf, sizeof(buf), "%s-%s-%s", id.lang, max.script, max.region);
        if ((n = find_node(buf)) >= 0)
            return n;
    }
    if (max.region[0]) {
        snprintf(buf, sizeof(buf), "%s-%s", id.lang, max.region);
        if ((n = find_node(buf)) >= 0)
            return n;
    }
    if (max.script[0]) {
        snprintf(buf, sizeof(buf), "%s-%s", id.lang, max.script);
        if ((n = find_node(buf)) >= 0)
            return n;
    }
    if ((n = find_node(id.lang)) >= 0)
        return n;
    return intl_data_root();
}

const char *intl_data_get(int node, const char *key)
{
    int k = find_key(key);
    if (k < 0)
        return NULL;
    while (node >= 0) {
        const IntlDataNode *nd = &intl_data_nodes[node];
        int lo = nd->start, hi = nd->start + nd->count - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            int c = (int)intl_data_entry_keys[mid] - k;
            if (c == 0) {
                const uint8_t *v = intl_data_entry_vals[mid];
                const char *s = intl_data_pool + (v[0] | (v[1] << 8) | ((uint32_t)v[2] << 16));
                return s[0] == '\x7f' ? NULL : s;
            }
            if (c < 0)
                lo = mid + 1;
            else
                hi = mid - 1;
        }
        node = nd->parent;
    }
    return NULL;
}

const char *intl_data_getf(int node, const char *fmt, ...)
{
    char key[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(key, sizeof(key), fmt, ap);
    va_end(ap);
    return intl_data_get(node, key);
}

bool intl_data_has_language(const char *tag, size_t len)
{
    char buf[16];
    if (len >= sizeof(buf) || memchr(tag, '-', len))
        return false;
    memcpy(buf, tag, len);
    buf[len] = '\0';
    return find_node(buf) >= 0;
}

const char *intl_data_node_name(int node)
{
    return intl_data_nodes[node].name;
}
