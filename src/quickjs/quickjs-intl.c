/* quickjs-intl.c: the ECMA-402 Intl object, locale negotiation and option processing. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "quickjs-intl.h"


JSValue intl_throw_range(JSContext *ctx, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return JS_ThrowRangeError(ctx, "%s", buf);
}

JSValue intl_throw_type(JSContext *ctx, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return JS_ThrowTypeError(ctx, "%s", buf);
}

void intl_locale_list_free(JSContext *ctx, IntlLocaleList *list)
{
    for (int i = 0; i < list->count; i++)
        free(list->items[i]);
    free(list->items);
    list->items = NULL;
    list->count = list->size = 0;
}

static int locale_list_push(JSContext *ctx, IntlLocaleList *list, char *tag)
{
    for (int i = 0; i < list->count; i++) {
        if (!strcmp(list->items[i], tag)) {
            free(tag);
            return 0;
        }
    }
    if (list->count == list->size) {
        int size = list->size ? list->size * 2 : 4;
        char **items = realloc(list->items, sizeof(char *) * size);
        if (!items) {
            free(tag);
            JS_ThrowOutOfMemory(ctx);
            return -1;
        }
        list->items = items;
        list->size = size;
    }
    list->items[list->count++] = tag;
    return 0;
}

static int add_locale_value(JSContext *ctx, IntlLocaleList *list, JSValueConst value)
{
    bool is_locale;
    JSValue tagv;
    const char *s;
    size_t len;
    char *canon;
    if (!JS_IsString(value) && !JS_IsObject(value)) {
        JS_ThrowTypeError(ctx, "locale must be a string or object");
        return -1;
    }
    tagv = intl_locale_get_tag(ctx, value, &is_locale);
    if (!is_locale)
        tagv = JS_ToString(ctx, value);
    if (JS_IsException(tagv))
        return -1;
    s = JS_ToCStringLen(ctx, &len, tagv);
    JS_FreeValue(ctx, tagv);
    if (!s)
        return -1;
    if (strlen(s) != len || !intl_is_well_formed_tag(s)) {
        JS_ThrowRangeError(ctx, "invalid language tag: %s", s);
        JS_FreeCString(ctx, s);
        return -1;
    }
    canon = intl_canonicalize_tag(ctx, s);
    JS_FreeCString(ctx, s);
    if (!canon)
        return -1;
    return locale_list_push(ctx, list, canon);
}

int intl_canonicalize_locale_list(JSContext *ctx, JSValueConst locales, IntlLocaleList *list)
{
    JSValue obj;
    int64_t len, k;
    bool is_locale;
    memset(list, 0, sizeof(*list));
    if (JS_IsUndefined(locales))
        return 0;
    if (JS_IsString(locales))
        return add_locale_value(ctx, list, locales) < 0 ? (intl_locale_list_free(ctx, list), -1) : 0;
    JS_FreeValue(ctx, intl_locale_get_tag(ctx, locales, &is_locale));
    if (is_locale)
        return add_locale_value(ctx, list, locales) < 0 ? (intl_locale_list_free(ctx, list), -1) : 0;
    obj = JS_ToObject(ctx, locales);
    if (JS_IsException(obj))
        return -1;
    if (JS_GetLength(ctx, obj, &len) < 0)
        goto fail;
    for (k = 0; k < len; k++) {
        JSAtom atom;
        if (k <= 0xffffffffLL) {
            atom = JS_NewAtomUInt32(ctx, (uint32_t)k);
        } else {
            char kbuf[24];
            snprintf(kbuf, sizeof(kbuf), "%lld", (long long)k);
            atom = JS_NewAtom(ctx, kbuf);
        }
        int present;
        JSValue v;
        if (atom == JS_ATOM_NULL)
            goto fail;
        present = JS_HasProperty(ctx, obj, atom);
        if (present < 0) {
            JS_FreeAtom(ctx, atom);
            goto fail;
        }
        if (!present) {
            JS_FreeAtom(ctx, atom);
            continue;
        }
        v = JS_GetProperty(ctx, obj, atom);
        JS_FreeAtom(ctx, atom);
        if (JS_IsException(v))
            goto fail;
        if (add_locale_value(ctx, list, v) < 0) {
            JS_FreeValue(ctx, v);
            goto fail;
        }
        JS_FreeValue(ctx, v);
    }
    JS_FreeValue(ctx, obj);
    return 0;
fail:
    JS_FreeValue(ctx, obj);
    intl_locale_list_free(ctx, list);
    return -1;
}

bool intl_service_available(int service, const char *tag, size_t len)
{
    if (intl_locale_available(tag, len))
        return true;
    if (service == INTL_CLASS_PLURAL_RULES)
        return intl_data_has_language(tag, len);
    if (service == INTL_CLASS_COLLATOR)
        return intl_collator_has_language(tag, len);
    return false;
}

const char *intl_lookup_available(int service, const char *tag, char *buf, size_t size)
{
    size_t pos;
    snprintf(buf, size, "%.*s", (int)intl_base_name_len(tag), tag);
    pos = strlen(buf);
    while (pos > 0) {
        if (intl_service_available(service, buf, pos)) {
            buf[pos] = '\0';
            return buf;
        }
        while (pos > 0 && buf[pos - 1] != '-')
            pos--;
        if (pos == 0)
            break;
        pos--;
        if (pos >= 2 && buf[pos - 2] == '-')
            pos -= 2;
    }
    return NULL;
}

char *intl_default_locale(JSContext *ctx)
{
    const char *host = js_intl_runtime_default_locale(JS_GetRuntime(ctx));
    char buf[INTL_TAG_MAX];
    if (host && *host && intl_is_well_formed_tag(host)) {
        char *canon = intl_canonicalize_tag(ctx, host);
        char *base;
        if (!canon)
            return NULL;
        base = intl_strip_unicode_ext(ctx, canon);
        free(canon);
        if (!base)
            return NULL;
        if (intl_lookup_available(-1, base, buf, sizeof(buf))) {
            free(base);
            return intl_strdup(ctx, buf);
        }
        free(base);
    }
    return intl_strdup(ctx, "en-US");
}

static const char *find_keyword(const IntlExtension *ext, const char *key)
{
    for (int i = 0; i < ext->nkeywords; i++)
        if (!strcmp(ext->keywords[i].key, key))
            return ext->keywords[i].value;
    return NULL;
}

const char intl_option_null[] = "";

int intl_resolve_locale(JSContext *ctx, int service, const IntlLocaleList *requested, bool lookup,
                        const IntlResolverOps *ops, char *const *options, IntlResolved *res)
{
    char buf[INTL_TAG_MAX];
    const char *found = NULL;
    const char *ext = NULL;
    size_t ext_len = 0;
    IntlExtension keywords, supported;

    (void)lookup;
    memset(res, 0, sizeof(*res));
    for (int i = 0; i < requested->count && !found; i++) {
        found = intl_lookup_available(service, requested->items[i], buf, sizeof(buf));
        if (found)
            ext = intl_find_unicode_ext(requested->items[i], &ext_len);
    }
    if (!found) {
        char *def = intl_default_locale(ctx);
        if (!def)
            return -1;
        snprintf(buf, sizeof(buf), "%s", def);
        free(def);
        found = buf;
        ext = NULL;
    }
    snprintf(res->data_locale, sizeof(res->data_locale), "%s", found);
    if (ext)
        intl_unicode_ext_components(ext, ext_len, &keywords);
    else
        memset(&keywords, 0, sizeof(keywords));
    memset(&supported, 0, sizeof(supported));
    for (int k = 0; ops && k < ops->nkeys; k++) {
        const char *key = ops->keys[k];
        const char *value = ops->key_default(found, key);
        const char *requested_value = find_keyword(&keywords, key);
        bool keep = false;
        char keep_value[64] = "";
        char *opt;
        if (requested_value) {
            if (requested_value[0]) {
                if (ops->key_supported(found, key, requested_value)) {
                    value = requested_value;
                    keep = true;
                    snprintf(keep_value, sizeof(keep_value), "%s", requested_value);
                }
            } else if (ops->key_supported(found, key, "true")) {
                value = "true";
                keep = true;
            }
        }
        opt = options ? options[k] : NULL;
        if (opt == intl_option_null) {
            value = NULL;
            keep = false;
            res->values[k][0] = '\0';
        } else if (opt) {
            char *canon = intl_canonicalize_uvalue(ctx, key, opt);
            const char *ov;
            if (!canon)
                return -1;
            ov = canon[0] ? canon : "true";
            if ((!value || strcmp(ov, value)) && ops->key_supported(found, key, ov)) {
                snprintf(res->values[k], sizeof(res->values[k]), "%s", ov);
                value = res->values[k];
                keep = false;
            }
            free(canon);
        }
        if (keep && supported.nkeywords < (int)countof(supported.keywords)) {
            IntlKeyword *kw = &supported.keywords[supported.nkeywords++];
            snprintf(kw->key, sizeof(kw->key), "%s", key);
            snprintf(kw->value, sizeof(kw->value), "%s", keep_value);
        }
        if (value != res->values[k])
            snprintf(res->values[k], sizeof(res->values[k]), "%s", value ? value : "");
    }
    if (supported.nkeywords) {
        char *loc = intl_insert_unicode_ext(ctx, found, &supported);
        if (!loc)
            return -1;
        snprintf(res->locale, sizeof(res->locale), "%s", loc);
        free(loc);
    } else {
        snprintf(res->locale, sizeof(res->locale), "%s", found);
    }
    return 0;
}

JSValue intl_get_options_object(JSContext *ctx, JSValueConst options)
{
    if (JS_IsUndefined(options))
        return JS_NewObjectProto(ctx, JS_NULL);
    if (JS_IsObject(options))
        return JS_DupValue(ctx, options);
    return JS_ThrowTypeError(ctx, "options must be an object");
}

JSValue intl_coerce_options(JSContext *ctx, JSValueConst options)
{
    if (JS_IsUndefined(options))
        return JS_NewObjectProto(ctx, JS_NULL);
    return JS_ToObject(ctx, options);
}

int intl_get_string_option(JSContext *ctx, JSValueConst options, const char *name,
                           const char *const *values, const char *fallback,
                           char *buf, size_t size, bool *present)
{
    JSValue v = JS_GetPropertyStr(ctx, options, name);
    const char *s;
    size_t len;
    if (present)
        *present = false;
    if (JS_IsException(v))
        return -1;
    if (JS_IsUndefined(v)) {
        snprintf(buf, size, "%s", fallback ? fallback : "");
        return 0;
    }
    s = JS_ToCStringLen(ctx, &len, v);
    JS_FreeValue(ctx, v);
    if (!s)
        return -1;
    if (values) {
        bool ok = false;
        for (int i = 0; values[i]; i++)
            if (strlen(values[i]) == len && !memcmp(values[i], s, len))
                ok = true;
        if (!ok) {
            JS_ThrowRangeError(ctx, "invalid value \"%s\" for option %s", s, name);
            JS_FreeCString(ctx, s);
            return -1;
        }
    }
    if (len >= size || strlen(s) != len) {
        if (!values) {
            snprintf(buf, size, "%s", "\x01");
            JS_FreeCString(ctx, s);
            if (present)
                *present = true;
            return 1;
        }
    }
    snprintf(buf, size, "%s", s);
    JS_FreeCString(ctx, s);
    if (present)
        *present = true;
    return 1;
}

int intl_get_enum_option(JSContext *ctx, JSValueConst options, const char *name,
                         const char *const *values, int fallback)
{
    char buf[64];
    bool present;
    if (intl_get_string_option(ctx, options, name, values, NULL, buf, sizeof(buf), &present) < 0)
        return -2;
    if (!present)
        return fallback;
    for (int i = 0; values[i]; i++)
        if (!strcmp(values[i], buf))
            return i;
    return fallback;
}

int intl_get_bool_option(JSContext *ctx, JSValueConst options, const char *name, int fallback)
{
    JSValue v = JS_GetPropertyStr(ctx, options, name);
    int r;
    if (JS_IsException(v))
        return -2;
    if (JS_IsUndefined(v))
        return fallback;
    r = JS_ToBool(ctx, v);
    JS_FreeValue(ctx, v);
    return r;
}

int intl_default_number_option(JSContext *ctx, JSValueConst value, int minimum, int maximum,
                               int fallback, int *out)
{
    double d;
    if (JS_IsUndefined(value)) {
        *out = fallback;
        return 0;
    }
    if (JS_ToFloat64(ctx, &d, value) < 0)
        return -1;
    if (d != d || d < minimum || d > maximum) {
        JS_ThrowRangeError(ctx, "value is out of range");
        return -1;
    }
    *out = (int)d;
    if ((double)*out > d)
        (*out)--;
    return 0;
}

int intl_get_number_option(JSContext *ctx, JSValueConst options, const char *name,
                           int minimum, int maximum, int fallback, int *out)
{
    JSValue v = JS_GetPropertyStr(ctx, options, name);
    int r;
    if (JS_IsException(v))
        return -1;
    r = intl_default_number_option(ctx, v, minimum, maximum, fallback, out);
    if (r < 0 && JS_IsUndefined(v))
        r = -1;
    JS_FreeValue(ctx, v);
    return r;
}

int intl_get_locale_matcher(JSContext *ctx, JSValueConst options, bool *lookup)
{
    static const char *const values[] = { "lookup", "best fit", NULL };
    int r = intl_get_enum_option(ctx, options, "localeMatcher", values, 1);
    if (r < -1)
        return -1;
    *lookup = r == 0;
    return 0;
}

int intl_get_resolution_option(JSContext *ctx, JSValueConst options, const char *name,
                               const char *const *values, char *buf, size_t size, bool *present)
{
    if (intl_get_string_option(ctx, options, name, values, NULL, buf, size, present) < 0)
        return -1;
    if (*present && !intl_is_type_sequence(buf)) {
        JS_ThrowRangeError(ctx, "invalid value for option %s", name);
        return -1;
    }
    return 0;
}

JSValue intl_supported_locales(JSContext *ctx, int service, JSValueConst locales, JSValueConst options)
{
    IntlLocaleList list;
    JSValue opts, arr;
    bool lookup;
    uint32_t n = 0;
    char buf[INTL_TAG_MAX];
    if (intl_canonicalize_locale_list(ctx, locales, &list) < 0)
        return JS_EXCEPTION;
    opts = intl_coerce_options(ctx, options);
    if (JS_IsException(opts))
        goto fail;
    if (intl_get_locale_matcher(ctx, opts, &lookup) < 0) {
        JS_FreeValue(ctx, opts);
        goto fail;
    }
    JS_FreeValue(ctx, opts);
    arr = JS_NewArray(ctx);
    if (JS_IsException(arr))
        goto fail;
    for (int i = 0; i < list.count; i++) {
        if (intl_lookup_available(service, list.items[i], buf, sizeof(buf))) {
            if (JS_DefinePropertyValueUint32(ctx, arr, n++, JS_NewString(ctx, list.items[i]),
                                             JS_PROP_C_W_E) < 0) {
                JS_FreeValue(ctx, arr);
                goto fail;
            }
        }
    }
    intl_locale_list_free(ctx, &list);
    return arr;
fail:
    intl_locale_list_free(ctx, &list);
    return JS_EXCEPTION;
}

int intl_set_prop_str(JSContext *ctx, JSValueConst obj, const char *name, const char *value)
{
    if (!value)
        return 0;
    return JS_DefinePropertyValueStr(ctx, obj, name, JS_NewString(ctx, value), JS_PROP_C_W_E);
}

int intl_set_prop_value(JSContext *ctx, JSValueConst obj, const char *name, JSValue value)
{
    return JS_DefinePropertyValueStr(ctx, obj, name, value, JS_PROP_C_W_E);
}

JSValue intl_make_part(JSContext *ctx, const char *type, const char *value, size_t len)
{
    JSValue obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return obj;
    if (JS_DefinePropertyValueStr(ctx, obj, "type", JS_NewString(ctx, type), JS_PROP_C_W_E) < 0 ||
        JS_DefinePropertyValueStr(ctx, obj, "value", JS_NewStringLen(ctx, value, len), JS_PROP_C_W_E) < 0) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    return obj;
}

int intl_push_part(JSContext *ctx, JSValueConst parts, uint32_t *idx, const char *type,
                   const char *value, size_t len, const char *extra_name, const char *extra_value)
{
    JSValue part = intl_make_part(ctx, type, value, len);
    if (JS_IsException(part))
        return -1;
    if (extra_name && extra_value &&
        JS_DefinePropertyValueStr(ctx, part, extra_name, JS_NewString(ctx, extra_value), JS_PROP_C_W_E) < 0) {
        JS_FreeValue(ctx, part);
        return -1;
    }
    return JS_DefinePropertyValueUint32(ctx, parts, (*idx)++, part, JS_PROP_C_W_E);
}

JSValue intl_new_string_array(JSContext *ctx, const char *const *items, int count)
{
    JSValue arr = JS_NewArray(ctx);
    if (JS_IsException(arr))
        return arr;
    for (int i = 0; i < count; i++) {
        if (JS_DefinePropertyValueUint32(ctx, arr, i, JS_NewString(ctx, items[i]), JS_PROP_C_W_E) < 0) {
            JS_FreeValue(ctx, arr);
            return JS_EXCEPTION;
        }
    }
    return arr;
}

static const char *const sanctioned_units[] = {
    "acre", "bit", "byte", "celsius", "centimeter", "day", "degree", "fahrenheit",
    "fluid-ounce", "foot", "gallon", "gigabit", "gigabyte", "gram", "hectare", "hour",
    "inch", "kilobit", "kilobyte", "kilogram", "kilometer", "liter", "megabit", "megabyte",
    "meter", "microsecond", "mile", "mile-scandinavian", "milliliter", "millimeter",
    "millisecond", "minute", "month", "nanosecond", "ounce", "percent", "petabyte", "pound",
    "second", "stone", "terabit", "terabyte", "week", "yard", "year",
};

bool intl_is_sanctioned_unit(const char *s, size_t len)
{
    for (size_t i = 0; i < countof(sanctioned_units); i++)
        if (strlen(sanctioned_units[i]) == len && !memcmp(sanctioned_units[i], s, len))
            return true;
    return false;
}

bool intl_is_well_formed_unit(const char *s)
{
    const char *per;
    if (intl_is_sanctioned_unit(s, strlen(s)))
        return true;
    per = strstr(s, "-per-");
    if (!per || strstr(per + 5, "-per-"))
        return false;
    return intl_is_sanctioned_unit(s, (size_t)(per - s)) &&
           intl_is_sanctioned_unit(per + 5, strlen(per + 5));
}

JSValue intl_available_currencies(JSContext *ctx);
JSValue intl_available_numbering_systems(JSContext *ctx);
JSValue intl_available_calendars(JSContext *ctx);
JSValue intl_available_collations(JSContext *ctx);
JSValue intl_primary_time_zones(JSContext *ctx, const char *region);

static JSValue js_intl_getCanonicalLocales(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv)
{
    IntlLocaleList list;
    JSValue arr;
    if (intl_canonicalize_locale_list(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &list) < 0)
        return JS_EXCEPTION;
    arr = intl_new_string_array(ctx, (const char *const *)list.items, list.count);
    intl_locale_list_free(ctx, &list);
    return arr;
}

static JSValue js_intl_supportedValuesOf(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    size_t len;
    const char *key = JS_ToCStringLen(ctx, &len, argc > 0 ? argv[0] : JS_UNDEFINED);
    JSValue r;
    if (!key)
        return JS_EXCEPTION;
    if (strlen(key) != len) {
        JS_FreeCString(ctx, key);
        return JS_ThrowRangeError(ctx, "invalid key");
    }
    if (!strcmp(key, "calendar"))
        r = intl_available_calendars(ctx);
    else if (!strcmp(key, "collation"))
        r = intl_available_collations(ctx);
    else if (!strcmp(key, "currency"))
        r = intl_available_currencies(ctx);
    else if (!strcmp(key, "numberingSystem"))
        r = intl_available_numbering_systems(ctx);
    else if (!strcmp(key, "timeZone"))
        r = intl_primary_time_zones(ctx, NULL);
    else if (!strcmp(key, "unit"))
        r = intl_new_string_array(ctx, sanctioned_units, countof(sanctioned_units));
    else
        r = JS_ThrowRangeError(ctx, "invalid key: %s", key);
    JS_FreeCString(ctx, key);
    return r;
}

static const JSCFunctionListEntry js_intl_funcs[] = {
    JS_CFUNC_DEF("getCanonicalLocales", 1, js_intl_getCanonicalLocales),
    JS_CFUNC_DEF("supportedValuesOf", 1, js_intl_supportedValuesOf),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Intl", JS_PROP_CONFIGURABLE),
};

int JS_AddIntrinsicIntl(JSContext *ctx)
{
    JSValue global, intl, sym;
    int r = -1;
    intl = JS_NewObject(ctx);
    if (JS_IsException(intl))
        return -1;
    if (JS_SetPropertyFunctionList(ctx, intl, js_intl_funcs, countof(js_intl_funcs)) < 0)
        goto done;
    sym = JS_NewSymbol(ctx, "IntlLegacyConstructedSymbol", false);
    if (JS_IsException(sym))
        goto done;
    js_intl_set_fallback_symbol(ctx, sym);
    if (js_intl_init_locale(ctx, intl) < 0 ||
        js_intl_init_number_format(ctx, intl) < 0 ||
        js_intl_init_plural_rules(ctx, intl) < 0 ||
        js_intl_init_collator(ctx, intl) < 0 ||
        js_intl_init_date_time_format(ctx, intl) < 0 ||
        js_intl_init_list_format(ctx, intl) < 0 ||
        js_intl_init_relative_time_format(ctx, intl) < 0 ||
        js_intl_init_display_names(ctx, intl) < 0 ||
        js_intl_init_duration_format(ctx, intl) < 0)
        goto done;
    global = JS_GetGlobalObject(ctx);
    r = JS_DefinePropertyValueStr(ctx, global, "Intl", JS_DupValue(ctx, intl),
                                  JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_FreeValue(ctx, global);
done:
    JS_FreeValue(ctx, intl);
    return r < 0 ? -1 : 0;
}
