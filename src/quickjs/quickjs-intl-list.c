/* quickjs-intl-list.c: Intl.ListFormat. */

#include <stdlib.h>
#include <string.h>

#include "quickjs-intl.h"

typedef struct IntlListFormat {
    char locale[INTL_TAG_MAX];
    int data;
    int type;
    int style;
} IntlListFormat;

static const char *const lf_type_names[] = { "conjunction", "disjunction", "unit", NULL };
static const char *const lf_style_names[] = { "long", "short", "narrow", NULL };
static const char lf_type_codes[] = "cou";
static const char lf_style_codes[] = "lsn";

static void lf_finalizer(JSRuntime *rt, JSValueConst val)
{
    free(JS_GetOpaque(val, js_intl_class_id(INTL_CLASS_LIST_FORMAT)));
}

static JSValue js_intl_lf_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    JSValue obj, options = JS_UNDEFINED;
    IntlListFormat *lf = NULL;
    IntlLocaleList requested;
    IntlResolved res;
    bool lookup;
    obj = js_intl_create_from_ctor(ctx, new_target, js_intl_class_id(INTL_CLASS_LIST_FORMAT));
    if (JS_IsException(obj))
        return obj;
    if (intl_canonicalize_locale_list(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &requested) < 0)
        goto fail0;
    lf = calloc(1, sizeof(*lf));
    if (!lf) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    options = intl_get_options_object(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
    if (JS_IsException(options))
        goto fail;
    if (intl_get_locale_matcher(ctx, options, &lookup) < 0)
        goto fail;
    if (intl_resolve_locale(ctx, INTL_CLASS_LIST_FORMAT, &requested, lookup, NULL, NULL, &res) < 0)
        goto fail;
    snprintf(lf->locale, sizeof(lf->locale), "%s", res.locale);
    lf->data = intl_data_node(res.data_locale);
    lf->type = intl_get_enum_option(ctx, options, "type", lf_type_names, 0);
    if (lf->type < -1)
        goto fail;
    lf->style = intl_get_enum_option(ctx, options, "style", lf_style_names, 0);
    if (lf->style < -1)
        goto fail;
    js_intl_set_opaque(obj, lf);
    intl_locale_list_free(ctx, &requested);
    JS_FreeValue(ctx, options);
    return obj;
fail:
    intl_locale_list_free(ctx, &requested);
fail0:
    free(lf);
    JS_FreeValue(ctx, options);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static IntlListFormat *get_lf(JSContext *ctx, JSValueConst this_val, const char *method)
{
    IntlListFormat *lf = JS_GetOpaque(this_val, js_intl_class_id(INTL_CLASS_LIST_FORMAT));
    if (!lf)
        JS_ThrowTypeError(ctx, "Intl.ListFormat.prototype.%s called on incompatible receiver", method);
    return lf;
}

static const char *lf_pattern(const IntlListFormat *lf, char which)
{
    const char *p = intl_data_getf(lf->data, "l.%c%c.%c", lf_type_codes[lf->type], lf_style_codes[lf->style], which);
    if (!p && lf->style > 0)
        p = intl_data_getf(lf->data, "l.%c%c.%c", lf_type_codes[lf->type], 'l', which);
    if (!p && lf->type > 0)
        p = intl_data_getf(lf->data, "l.c%c.%c", lf_style_codes[lf->style], which);
    if (!p)
        p = "{0}, {1}";
    return p;
}

static bool lang_is(const IntlListFormat *lf, const char *lang)
{
    size_t n = strlen(lang);
    return !strncmp(lf->locale, lang, n) && (lf->locale[n] == '\0' || lf->locale[n] == '-');
}

static bool starts_with_nocase(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++) {
        char c = *s;
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if (c != *prefix)
            return false;
    }
    return true;
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool spanish_e_before(const char *next)
{
    const char *p;
    if (starts_with_nocase(next, "hi"))
        p = next + 2;
    else if (starts_with_nocase(next, "i"))
        p = next + 1;
    else
        return false;
    return !(*p && strchr("aeouAEOU", *p));
}

static bool spanish_u_before(const char *next)
{
    const char *p;
    if (starts_with_nocase(next, "o") || starts_with_nocase(next, "ho") || next[0] == '8')
        return true;
    if (next[0] != '1' || next[1] != '1')
        return false;
    p = next + 2;
    while (p[0] == '.' && is_digit(p[1]) && is_digit(p[2]) && is_digit(p[3]))
        p += 4;
    while (is_digit(p[0]) && is_digit(p[1]) && is_digit(p[2]))
        p += 3;
    if (*p == ',') {
        p++;
        while (is_digit(*p))
            p++;
    }
    return *p == '\0' || !(*p == '.' || *p == ',' || is_digit(*p));
}

static bool all_hebrew_letters(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    if (!*p)
        return false;
    while (*p) {
        if (p[0] != 0xd7 || p[1] < 0x90 || p[1] > 0xaa)
            return false;
        p += 2;
    }
    return true;
}

static void add_literal(const IntlListFormat *lf, IntlParts *parts, const char *lit, size_t len, const char *next)
{
    if (len == 0)
        return;
    if (next && lang_is(lf, "es")) {
        if (len == 3 && !memcmp(lit, " y ", 3) && spanish_e_before(next)) {
            intl_parts_add(parts, "literal", " e ", 3, 0);
            return;
        }
        if (len == 3 && !memcmp(lit, " o ", 3) && spanish_u_before(next)) {
            intl_parts_add(parts, "literal", " u ", 3, 0);
            return;
        }
    }
    if (next && lang_is(lf, "he") && len >= 2 && !memcmp(lit + len - 2, "\xd7\x95", 2) && !all_hebrew_letters(next)) {
        DynBuf b;
        dbuf_init(&b);
        dbuf_put(&b, (const uint8_t *)lit, len - 2);
        dbuf_put(&b, (const uint8_t *)"-\xd7\x95", 3);
        intl_parts_add(parts, "literal", (const char *)b.buf, b.size, 0);
        dbuf_free(&b);
        return;
    }
    intl_parts_add(parts, "literal", lit, len, 0);
}

static void apply_pattern(const IntlListFormat *lf, const char *pat, const char *first, const char **suffix,
                          size_t *suffix_len, IntlParts *parts, const char *next)
{
    const char *z0 = strstr(pat, "{0}"), *z1 = strstr(pat, "{1}");
    add_literal(lf, parts, pat, (size_t)(z0 - pat), NULL);
    intl_parts_add(parts, "element", first, strlen(first), 0);
    add_literal(lf, parts, z0 + 3, (size_t)(z1 - z0 - 3), next);
    *suffix = z1 + 3;
    *suffix_len = strlen(z1 + 3);
}

static int lf_partition_cstr(JSContext *ctx, const IntlListFormat *lf, const char *const *strs, uint32_t n,
                             IntlParts *parts)
{
    const char **suffixes;
    size_t *suffix_lens;
    int r = -1;
    if (n == 0)
        return 0;
    suffixes = calloc(n, sizeof(*suffixes));
    suffix_lens = calloc(n, sizeof(*suffix_lens));
    if (!suffixes || !suffix_lens) {
        JS_ThrowOutOfMemory(ctx);
        goto done;
    }
    if (n == 1) {
        intl_parts_add(parts, "element", strs[0], strlen(strs[0]), 0);
    } else if (n == 2) {
        apply_pattern(lf, lf_pattern(lf, '2'), strs[0], &suffixes[0], &suffix_lens[0], parts, strs[1]);
        intl_parts_add(parts, "element", strs[1], strlen(strs[1]), 0);
        add_literal(lf, parts, suffixes[0], suffix_lens[0], NULL);
    } else {
        uint32_t i;
        for (i = 0; i < n - 1; i++) {
            char which = i == 0 ? 's' : i == n - 2 ? 'e' : 'm';
            apply_pattern(lf, lf_pattern(lf, which), strs[i], &suffixes[i], &suffix_lens[i], parts, strs[i + 1]);
        }
        intl_parts_add(parts, "element", strs[n - 1], strlen(strs[n - 1]), 0);
        for (i = n - 1; i-- > 0;)
            add_literal(lf, parts, suffixes[i], suffix_lens[i], NULL);
    }
    if (parts->error) {
        JS_ThrowOutOfMemory(ctx);
        goto done;
    }
    r = 0;
done:
    free(suffixes);
    free(suffix_lens);
    return r;
}

static int lf_partition(JSContext *ctx, const IntlListFormat *lf, JSValue *list, uint32_t n, IntlParts *parts)
{
    const char **strs;
    int r = -1;
    if (n == 0)
        return 0;
    strs = calloc(n, sizeof(*strs));
    if (!strs) {
        JS_ThrowOutOfMemory(ctx);
        return -1;
    }
    for (uint32_t i = 0; i < n; i++) {
        strs[i] = JS_ToCString(ctx, list[i]);
        if (!strs[i])
            goto done;
    }
    r = lf_partition_cstr(ctx, lf, strs, n, parts);
done:
    for (uint32_t i = 0; i < n; i++)
        JS_FreeCString(ctx, strs[i]);
    free(strs);
    return r;
}

int intl_lf_partition_strings(JSContext *ctx, JSValueConst lf_obj, const char *const *strs, uint32_t n,
                              IntlParts *parts)
{
    IntlListFormat *lf = JS_GetOpaque(lf_obj, js_intl_class_id(INTL_CLASS_LIST_FORMAT));
    return lf_partition_cstr(ctx, lf, strs, n, parts);
}

static JSValue js_intl_lf_format(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int to_parts)
{
    IntlListFormat *lf = get_lf(ctx, this_val, to_parts ? "formatToParts" : "format");
    JSValue *list, r;
    uint32_t n;
    IntlParts parts;
    if (!lf)
        return JS_EXCEPTION;
    if (js_intl_string_list_from_iterable(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &list, &n) < 0)
        return JS_EXCEPTION;
    intl_parts_init(&parts);
    if (lf_partition(ctx, lf, list, n, &parts) < 0)
        r = JS_EXCEPTION;
    else if (to_parts)
        r = intl_parts_to_array(ctx, &parts, NULL, NULL, false);
    else
        r = intl_parts_to_string(ctx, &parts);
    intl_parts_free(&parts);
    js_intl_free_value_list(ctx, list, n);
    return r;
}

static JSValue js_intl_lf_resolvedOptions(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    IntlListFormat *lf = get_lf(ctx, this_val, "resolvedOptions");
    JSValue obj;
    if (!lf)
        return JS_EXCEPTION;
    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return obj;
    if (intl_set_prop_str(ctx, obj, "locale", lf->locale) < 0 ||
        intl_set_prop_str(ctx, obj, "type", lf_type_names[lf->type]) < 0 ||
        intl_set_prop_str(ctx, obj, "style", lf_style_names[lf->style]) < 0) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    return obj;
}

static const JSCFunctionListEntry js_intl_lf_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("format", 1, js_intl_lf_format, 0),
    JS_CFUNC_MAGIC_DEF("formatToParts", 1, js_intl_lf_format, 1),
    JS_CFUNC_DEF("resolvedOptions", 0, js_intl_lf_resolvedOptions),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Intl.ListFormat", JS_PROP_CONFIGURABLE),
};

int js_intl_init_list_format(JSContext *ctx, JSValueConst intl)
{
    return intl_define_service(ctx, intl, "ListFormat", INTL_CLASS_LIST_FORMAT, INTL_INTRINSIC_LIST_FORMAT,
                               js_intl_lf_ctor, 0, JS_CFUNC_constructor, js_intl_lf_proto_funcs,
                               countof(js_intl_lf_proto_funcs), lf_finalizer, NULL);
}
