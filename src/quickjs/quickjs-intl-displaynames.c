/* quickjs-intl-displaynames.c: Intl.DisplayNames. */

#include <stdlib.h>
#include <string.h>

#include "quickjs-intl.h"

enum { DN_LANGUAGE, DN_REGION, DN_SCRIPT, DN_CURRENCY, DN_CALENDAR, DN_DATETIME_FIELD };

typedef struct IntlDisplayNames {
    char locale[INTL_TAG_MAX];
    int data;
    int style;
    int type;
    int fallback;
    int language_display;
} IntlDisplayNames;

static const char *const dn_style_names[] = { "narrow", "short", "long", NULL };
static const char *const dn_type_names[] = {
    "language", "region", "script", "currency", "calendar", "dateTimeField", NULL,
};
static const char *const dn_fallback_names[] = { "code", "none", NULL };
static const char *const dn_language_display_names[] = { "dialect", "standard", NULL };
static const char *const dn_fields[] = {
    "era", "year", "quarter", "month", "weekOfYear", "weekday", "day", "dayPeriod", "hour", "minute", "second",
    "timeZoneName",
};

static void dn_finalizer(JSRuntime *rt, JSValueConst val)
{
    free(JS_GetOpaque(val, js_intl_class_id(INTL_CLASS_DISPLAY_NAMES)));
}

static JSValue js_intl_dn_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    JSValue obj, options = JS_UNDEFINED, opt_in = argc > 1 ? argv[1] : JS_UNDEFINED;
    IntlDisplayNames *dn = NULL;
    IntlLocaleList requested;
    IntlResolved res;
    bool lookup;
    obj = js_intl_create_from_ctor(ctx, new_target, js_intl_class_id(INTL_CLASS_DISPLAY_NAMES));
    if (JS_IsException(obj))
        return obj;
    if (intl_canonicalize_locale_list(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &requested) < 0)
        goto fail0;
    dn = calloc(1, sizeof(*dn));
    if (!dn) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    if (!JS_IsObject(opt_in)) {
        JS_ThrowTypeError(ctx, "Intl.DisplayNames requires an options object");
        goto fail;
    }
    options = JS_DupValue(ctx, opt_in);
    if (intl_get_locale_matcher(ctx, options, &lookup) < 0)
        goto fail;
    if (intl_resolve_locale(ctx, INTL_CLASS_DISPLAY_NAMES, &requested, lookup, NULL, NULL, &res) < 0)
        goto fail;
    dn->style = intl_get_enum_option(ctx, options, "style", dn_style_names, 2);
    if (dn->style < -1)
        goto fail;
    dn->type = intl_get_enum_option(ctx, options, "type", dn_type_names, -1);
    if (dn->type < -1)
        goto fail;
    if (dn->type < 0) {
        JS_ThrowTypeError(ctx, "Intl.DisplayNames requires a type option");
        goto fail;
    }
    dn->fallback = intl_get_enum_option(ctx, options, "fallback", dn_fallback_names, 0);
    if (dn->fallback < -1)
        goto fail;
    snprintf(dn->locale, sizeof(dn->locale), "%s", res.locale);
    dn->data = intl_data_node(res.data_locale);
    dn->language_display = intl_get_enum_option(ctx, options, "languageDisplay", dn_language_display_names, 0);
    if (dn->language_display < -1)
        goto fail;
    js_intl_set_opaque(obj, dn);
    intl_locale_list_free(ctx, &requested);
    JS_FreeValue(ctx, options);
    return obj;
fail:
    intl_locale_list_free(ctx, &requested);
fail0:
    free(dn);
    JS_FreeValue(ctx, options);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static IntlDisplayNames *get_dn(JSContext *ctx, JSValueConst this_val, const char *method)
{
    IntlDisplayNames *dn = JS_GetOpaque(this_val, js_intl_class_id(INTL_CLASS_DISPLAY_NAMES));
    if (!dn)
        JS_ThrowTypeError(ctx, "Intl.DisplayNames.prototype.%s called on incompatible receiver", method);
    return dn;
}

static bool is_alnum(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool is_type_nonterminal(const char *s)
{
    size_t run = 0;
    if (!*s)
        return false;
    for (;; s++) {
        if (*s == '-' || *s == '\0') {
            if (run < 3 || run > 8)
                return false;
            if (*s == '\0')
                return true;
            run = 0;
        } else if (is_alnum(*s)) {
            run++;
        } else {
            return false;
        }
    }
}

static char *canonical_code(JSContext *ctx, const IntlDisplayNames *dn, const char *code, size_t len)
{
    char *r;
    IntlLangId id;
    if (strlen(code) != len)
        goto invalid;
    switch (dn->type) {
    case DN_LANGUAGE:
        if (!intl_parse_lang_id(code, len, &id) || !intl_is_well_formed_tag(code) || !strcmp(code, "root"))
            goto invalid;
        r = intl_canonicalize_tag(ctx, code);
        if (!r)
            return NULL;
        return r;
    case DN_REGION:
        if (!intl_is_region_subtag(code, len))
            goto invalid;
        r = intl_strdup(ctx, code);
        if (r)
            intl_ascii_upper(r);
        return r;
    case DN_SCRIPT:
        if (!intl_is_script_subtag(code, len))
            goto invalid;
        r = intl_strdup(ctx, code);
        if (r) {
            intl_ascii_lower(r);
            r[0] = (char)(r[0] - 32);
        }
        return r;
    case DN_CALENDAR:
        if (!is_type_nonterminal(code))
            goto invalid;
        r = intl_strdup(ctx, code);
        if (r)
            intl_ascii_lower(r);
        return r;
    case DN_DATETIME_FIELD:
        for (size_t i = 0; i < countof(dn_fields); i++)
            if (!strcmp(code, dn_fields[i]))
                return intl_strdup(ctx, code);
        goto invalid;
    default:
        if (!intl_is_well_formed_currency(code))
            goto invalid;
        r = intl_strdup(ctx, code);
        if (r)
            intl_ascii_upper(r);
        return r;
    }
invalid:
    JS_ThrowRangeError(ctx, "invalid code for Intl.DisplayNames: %s", code);
    return NULL;
}

static const char *dn_name(const IntlDisplayNames *dn, char kind, const char *code)
{
    const char *s = NULL;
    if (dn->style < 2)
        s = intl_data_getf(dn->data, "dn.%c.%s", kind - 32, code);
    if (!s)
        s = intl_data_getf(dn->data, "dn.%c.%s", kind, code);
    return s;
}

static void put_pattern(DynBuf *b, const char *pattern, const char *a, const char *c)
{
    for (const char *q = pattern; *q; q++) {
        if (q[0] == '{' && (q[1] == '0' || q[1] == '1') && q[2] == '}') {
            dbuf_putstr(b, q[1] == '0' ? a : c);
            q += 2;
        } else {
            dbuf_putc(b, *q);
        }
    }
}

static char *language_name(const IntlDisplayNames *dn, const char *tag)
{
    IntlLangId id;
    char key[INTL_TAG_MAX];
    const char *base = NULL, *sep, *pat;
    bool used_script = false, used_region = false;
    DynBuf quals, out;
    char *r = NULL;
    size_t blen = intl_base_name_len(tag);
    if (!intl_parse_lang_id(tag, blen, &id))
        return NULL;
    if (dn->language_display == 0) {
        if (id.script[0] && id.region[0]) {
            snprintf(key, sizeof(key), "%s-%s-%s", id.lang, id.script, id.region);
            base = dn_name(dn, 'l', key);
            used_script = used_region = base != NULL;
        }
        if (!base && id.script[0]) {
            snprintf(key, sizeof(key), "%s-%s", id.lang, id.script);
            base = dn_name(dn, 'l', key);
            used_script = base != NULL;
        }
        if (!base && id.region[0]) {
            snprintf(key, sizeof(key), "%s-%s", id.lang, id.region);
            base = dn_name(dn, 'l', key);
            used_region = base != NULL;
        }
    }
    if (!base)
        base = dn_name(dn, 'l', id.lang);
    if (!base)
        return NULL;
    sep = intl_data_get(dn->data, "dn.ls");
    pat = intl_data_get(dn->data, "dn.lp");
    if (!sep)
        sep = "{0}, {1}";
    if (!pat)
        pat = "{0} ({1})";
    dbuf_init(&quals);
    dbuf_init(&out);
    {
        const char *items[20];
        int n = 0;
        if (id.script[0] && !used_script) {
            const char *s = dn_name(dn, 's', id.script);
            items[n++] = s ? s : id.script;
        }
        if (id.region[0] && !used_region) {
            const char *s = dn_name(dn, 'r', id.region);
            items[n++] = s ? s : id.region;
        }
        for (int i = 0; i < id.nvariants && n < (int)countof(items); i++) {
            char vkey[16];
            const char *s;
            snprintf(vkey, sizeof(vkey), "%s", id.variants[i]);
            intl_ascii_upper(vkey);
            s = dn_name(dn, 'v', vkey);
            items[n++] = s ? s : id.variants[i];
        }
        for (int i = 0; i < n; i++) {
            if (i == 0) {
                dbuf_putstr(&quals, items[0]);
            } else {
                DynBuf tmp;
                dbuf_init(&tmp);
                dbuf_putc(&quals, '\0');
                put_pattern(&tmp, sep, (const char *)quals.buf, items[i]);
                dbuf_free(&quals);
                quals = tmp;
            }
        }
        if (n == 0) {
            dbuf_putstr(&out, base);
        } else {
            dbuf_putc(&quals, '\0');
            put_pattern(&out, pat, base, (const char *)quals.buf);
        }
    }
    dbuf_putc(&out, '\0');
    if (!out.error && !quals.error)
        r = strdup((const char *)out.buf);
    dbuf_free(&quals);
    dbuf_free(&out);
    return r;
}

static JSValue js_intl_dn_of(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    IntlDisplayNames *dn = get_dn(ctx, this_val, "of");
    const char *s;
    size_t len;
    char *code, *owned = NULL;
    const char *name = NULL;
    JSValue r;
    if (!dn)
        return JS_EXCEPTION;
    s = JS_ToCStringLen(ctx, &len, argc > 0 ? argv[0] : JS_UNDEFINED);
    if (!s)
        return JS_EXCEPTION;
    code = canonical_code(ctx, dn, s, len);
    JS_FreeCString(ctx, s);
    if (!code)
        return JS_EXCEPTION;
    switch (dn->type) {
    case DN_LANGUAGE:
        owned = language_name(dn, code);
        name = owned;
        break;
    case DN_REGION:
        name = dn_name(dn, 'r', code);
        break;
    case DN_SCRIPT:
        name = dn_name(dn, 's', code);
        break;
    case DN_CALENDAR:
        name = intl_data_getf(dn->data, "dn.c.%s", code);
        break;
    case DN_DATETIME_FIELD:
        name = intl_data_getf(dn->data, "dn.f.%s.%c", code, "nsl"[dn->style]);
        if (!name)
            name = intl_data_getf(dn->data, "dn.f.%s.l", code);
        break;
    default:
        name = intl_data_getf(dn->data, "c.%s.%c", code, dn->style == 2 ? 'd' : dn->style == 1 ? 's' : 'n');
        if (!name && dn->style != 2)
            name = intl_data_getf(dn->data, "c.%s.s", code);
        if (!name)
            name = intl_data_getf(dn->data, "c.%s.d", code);
        break;
    }
    if (name)
        r = JS_NewString(ctx, name);
    else if (dn->fallback == 0)
        r = JS_NewString(ctx, code);
    else
        r = JS_UNDEFINED;
    free(owned);
    free(code);
    return r;
}

static JSValue js_intl_dn_resolvedOptions(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    IntlDisplayNames *dn = get_dn(ctx, this_val, "resolvedOptions");
    JSValue obj;
    if (!dn)
        return JS_EXCEPTION;
    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return obj;
    if (intl_set_prop_str(ctx, obj, "locale", dn->locale) < 0 ||
        intl_set_prop_str(ctx, obj, "style", dn_style_names[dn->style]) < 0 ||
        intl_set_prop_str(ctx, obj, "type", dn_type_names[dn->type]) < 0 ||
        intl_set_prop_str(ctx, obj, "fallback", dn_fallback_names[dn->fallback]) < 0 ||
        (dn->type == DN_LANGUAGE &&
         intl_set_prop_str(ctx, obj, "languageDisplay", dn_language_display_names[dn->language_display]) < 0)) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    return obj;
}

static const JSCFunctionListEntry js_intl_dn_proto_funcs[] = {
    JS_CFUNC_DEF("of", 1, js_intl_dn_of),
    JS_CFUNC_DEF("resolvedOptions", 0, js_intl_dn_resolvedOptions),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Intl.DisplayNames", JS_PROP_CONFIGURABLE),
};

int js_intl_init_display_names(JSContext *ctx, JSValueConst intl)
{
    return intl_define_service(ctx, intl, "DisplayNames", INTL_CLASS_DISPLAY_NAMES, INTL_INTRINSIC_DISPLAY_NAMES,
                               js_intl_dn_ctor, 2, JS_CFUNC_constructor, js_intl_dn_proto_funcs,
                               countof(js_intl_dn_proto_funcs), dn_finalizer, NULL);
}
