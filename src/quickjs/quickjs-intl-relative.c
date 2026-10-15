/* quickjs-intl-relative.c: Intl.RelativeTimeFormat. */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "quickjs-intl.h"

typedef struct IntlRelativeTimeFormat {
    char locale[INTL_TAG_MAX];
    char numbering[64];
    int data;
    int style;
    int numeric;
    JSValue number_format;
    JSValue plural_rules;
} IntlRelativeTimeFormat;

static const char *const rtf_style_names[] = { "long", "short", "narrow", NULL };
static const char *const rtf_numeric_names[] = { "always", "auto", NULL };
static const char rtf_style_codes[] = "lsn";
static const char *const rtf_units[] = { "second", "minute", "hour", "day", "week", "month", "quarter", "year" };
static const char rtf_unit_codes[] = "snhdwmqy";

static void rtf_finalizer(JSRuntime *rt, JSValueConst val)
{
    IntlRelativeTimeFormat *rtf = JS_GetOpaque(val, js_intl_class_id(INTL_CLASS_RELATIVE_TIME_FORMAT));
    if (rtf) {
        JS_FreeValueRT(rt, rtf->number_format);
        JS_FreeValueRT(rt, rtf->plural_rules);
        free(rtf);
    }
}

static void rtf_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func)
{
    IntlRelativeTimeFormat *rtf = JS_GetOpaque(val, js_intl_class_id(INTL_CLASS_RELATIVE_TIME_FORMAT));
    if (rtf) {
        JS_MarkValue(rt, rtf->number_format, mark_func);
        JS_MarkValue(rt, rtf->plural_rules, mark_func);
    }
}

static int rtf_make_helpers(JSContext *ctx, IntlRelativeTimeFormat *rtf)
{
    JSValue locale = JS_NewString(ctx, rtf->locale), nf_options;
    int r = -1;
    if (JS_IsException(locale))
        return -1;
    nf_options = JS_NewObjectProto(ctx, JS_NULL);
    if (JS_IsException(nf_options) || intl_set_prop_str(ctx, nf_options, "numberingSystem", rtf->numbering) < 0)
        goto done;
    rtf->number_format = intl_construct_intrinsic(ctx, INTL_INTRINSIC_NUMBER_FORMAT, locale, nf_options);
    if (JS_IsException(rtf->number_format))
        goto done;
    rtf->plural_rules = intl_construct_intrinsic(ctx, INTL_INTRINSIC_PLURAL_RULES, locale, JS_UNDEFINED);
    if (JS_IsException(rtf->plural_rules))
        goto done;
    r = 0;
done:
    JS_FreeValue(ctx, locale);
    JS_FreeValue(ctx, nf_options);
    return r;
}

static JSValue js_intl_rtf_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    JSValue obj, options = JS_UNDEFINED;
    IntlRelativeTimeFormat *rtf = NULL;
    IntlLocaleList requested;
    IntlResolved res;
    char nu[64];
    char *opts[1];
    bool lookup, has_nu;
    obj = js_intl_create_from_ctor(ctx, new_target, js_intl_class_id(INTL_CLASS_RELATIVE_TIME_FORMAT));
    if (JS_IsException(obj))
        return obj;
    if (intl_canonicalize_locale_list(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &requested) < 0)
        goto fail0;
    rtf = calloc(1, sizeof(*rtf));
    if (!rtf) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    rtf->number_format = JS_UNDEFINED;
    rtf->plural_rules = JS_UNDEFINED;
    options = intl_coerce_options(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
    if (JS_IsException(options))
        goto fail;
    if (intl_get_locale_matcher(ctx, options, &lookup) < 0)
        goto fail;
    if (intl_get_resolution_option(ctx, options, "numberingSystem", NULL, nu, sizeof(nu), &has_nu) < 0)
        goto fail;
    opts[0] = has_nu ? nu : NULL;
    if (intl_resolve_locale(ctx, INTL_CLASS_RELATIVE_TIME_FORMAT, &requested, lookup, &intl_nu_resolver, opts,
                            &res) < 0)
        goto fail;
    snprintf(rtf->locale, sizeof(rtf->locale), "%s", res.locale);
    snprintf(rtf->numbering, sizeof(rtf->numbering), "%s", res.values[0]);
    rtf->data = intl_data_node(res.data_locale);
    rtf->style = intl_get_enum_option(ctx, options, "style", rtf_style_names, 0);
    if (rtf->style < -1)
        goto fail;
    rtf->numeric = intl_get_enum_option(ctx, options, "numeric", rtf_numeric_names, 0);
    if (rtf->numeric < -1)
        goto fail;
    if (rtf_make_helpers(ctx, rtf) < 0)
        goto fail;
    js_intl_set_opaque(obj, rtf);
    intl_locale_list_free(ctx, &requested);
    JS_FreeValue(ctx, options);
    return obj;
fail:
    intl_locale_list_free(ctx, &requested);
fail0:
    if (rtf) {
        JS_FreeValue(ctx, rtf->number_format);
        JS_FreeValue(ctx, rtf->plural_rules);
    }
    free(rtf);
    JS_FreeValue(ctx, options);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static IntlRelativeTimeFormat *get_rtf(JSContext *ctx, JSValueConst this_val, const char *method)
{
    IntlRelativeTimeFormat *rtf = JS_GetOpaque(this_val, js_intl_class_id(INTL_CLASS_RELATIVE_TIME_FORMAT));
    if (!rtf)
        JS_ThrowTypeError(ctx, "Intl.RelativeTimeFormat.prototype.%s called on incompatible receiver", method);
    return rtf;
}

static int singular_unit(const char *s)
{
    size_t len = strlen(s);
    for (int i = 0; i < (int)countof(rtf_units); i++) {
        size_t ul = strlen(rtf_units[i]);
        if ((len == ul && !memcmp(s, rtf_units[i], ul)) ||
            (len == ul + 1 && !memcmp(s, rtf_units[i], ul) && s[ul] == 's'))
            return i;
    }
    return -1;
}

static const char *rtf_get(const IntlRelativeTimeFormat *rtf, int unit, const char *what)
{
    const char *p = intl_data_getf(rtf->data, "r.%c%c.%s", rtf_unit_codes[unit], rtf_style_codes[rtf->style], what);
    if (!p && rtf->style > 0)
        p = intl_data_getf(rtf->data, "r.%c%c.%s", rtf_unit_codes[unit], 'l', what);
    return p;
}

static int rtf_partition(JSContext *ctx, const IntlRelativeTimeFormat *rtf, double value, int unit, IntlParts *out)
{
    const char *pattern, *z;
    char key[32];
    IntlParts num;
    int cat;
    if (!isfinite(value)) {
        JS_ThrowRangeError(ctx, "invalid value for relative time");
        return -1;
    }
    if (rtf->numeric == 1 && value == floor(value) && fabs(value) <= 2) {
        const char *p;
        snprintf(key, sizeof(key), "%d", (int)value);
        p = rtf_get(rtf, unit, key);
        if (p) {
            intl_parts_add(out, "literal", p, strlen(p), 0);
            return 0;
        }
    }
    cat = intl_pr_select_double(ctx, rtf->plural_rules, fabs(value));
    if (cat < 0)
        return -1;
    snprintf(key, sizeof(key), "%c.%s", signbit(value) ? 'p' : 'f', intl_plural_categories[cat]);
    pattern = rtf_get(rtf, unit, key);
    if (!pattern) {
        snprintf(key, sizeof(key), "%c.other", signbit(value) ? 'p' : 'f');
        pattern = rtf_get(rtf, unit, key);
    }
    if (!pattern)
        pattern = signbit(value) ? "-{0}" : "+{0}";
    intl_parts_init(&num);
    if (intl_nf_format_double(ctx, rtf->number_format, fabs(value), &num) < 0) {
        intl_parts_free(&num);
        return -1;
    }
    z = strstr(pattern, "{0}");
    if (!z) {
        intl_parts_add(out, "literal", pattern, strlen(pattern), 0);
    } else {
        if (z > pattern)
            intl_parts_add(out, "literal", pattern, (size_t)(z - pattern), 0);
        intl_parts_append(out, &num, rtf_units[unit]);
        if (z[3])
            intl_parts_add(out, "literal", z + 3, strlen(z + 3), 0);
    }
    intl_parts_free(&num);
    if (out->error) {
        JS_ThrowOutOfMemory(ctx);
        return -1;
    }
    return 0;
}

static JSValue rtf_parts_to_array(JSContext *ctx, const IntlParts *p)
{
    JSValue arr = JS_NewArray(ctx);
    if (JS_IsException(arr))
        return arr;
    for (int i = 0; i < p->count; i++) {
        JSValue part = intl_make_part(ctx, p->items[i].type, p->items[i].value, strlen(p->items[i].value));
        if (JS_IsException(part))
            goto fail;
        if (p->items[i].source && strcmp(p->items[i].type, "literal") &&
            intl_set_prop_str(ctx, part, "unit", p->items[i].source) < 0) {
            JS_FreeValue(ctx, part);
            goto fail;
        }
        if (JS_DefinePropertyValueUint32(ctx, arr, i, part, JS_PROP_C_W_E) < 0)
            goto fail;
    }
    return arr;
fail:
    JS_FreeValue(ctx, arr);
    return JS_EXCEPTION;
}

static JSValue js_intl_rtf_format(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int to_parts)
{
    IntlRelativeTimeFormat *rtf = get_rtf(ctx, this_val, to_parts ? "formatToParts" : "format");
    double value;
    const char *unit_str;
    int unit;
    IntlParts parts;
    JSValue r;
    if (!rtf)
        return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &value, argc > 0 ? argv[0] : JS_UNDEFINED) < 0)
        return JS_EXCEPTION;
    unit_str = JS_ToCString(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
    if (!unit_str)
        return JS_EXCEPTION;
    unit = singular_unit(unit_str);
    if (unit < 0) {
        JS_ThrowRangeError(ctx, "invalid unit: %s", unit_str);
        JS_FreeCString(ctx, unit_str);
        return JS_EXCEPTION;
    }
    JS_FreeCString(ctx, unit_str);
    intl_parts_init(&parts);
    if (rtf_partition(ctx, rtf, value, unit, &parts) < 0)
        r = JS_EXCEPTION;
    else if (to_parts)
        r = rtf_parts_to_array(ctx, &parts);
    else
        r = intl_parts_to_string(ctx, &parts);
    intl_parts_free(&parts);
    return r;
}

static JSValue js_intl_rtf_resolvedOptions(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    IntlRelativeTimeFormat *rtf = get_rtf(ctx, this_val, "resolvedOptions");
    JSValue obj;
    if (!rtf)
        return JS_EXCEPTION;
    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return obj;
    if (intl_set_prop_str(ctx, obj, "locale", rtf->locale) < 0 ||
        intl_set_prop_str(ctx, obj, "style", rtf_style_names[rtf->style]) < 0 ||
        intl_set_prop_str(ctx, obj, "numeric", rtf_numeric_names[rtf->numeric]) < 0 ||
        intl_set_prop_str(ctx, obj, "numberingSystem", rtf->numbering) < 0) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    return obj;
}

static const JSCFunctionListEntry js_intl_rtf_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("format", 2, js_intl_rtf_format, 0),
    JS_CFUNC_MAGIC_DEF("formatToParts", 2, js_intl_rtf_format, 1),
    JS_CFUNC_DEF("resolvedOptions", 0, js_intl_rtf_resolvedOptions),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Intl.RelativeTimeFormat", JS_PROP_CONFIGURABLE),
};

int js_intl_init_relative_time_format(JSContext *ctx, JSValueConst intl)
{
    return intl_define_service(ctx, intl, "RelativeTimeFormat", INTL_CLASS_RELATIVE_TIME_FORMAT,
                               INTL_INTRINSIC_RELATIVE_TIME_FORMAT, js_intl_rtf_ctor, 0, JS_CFUNC_constructor,
                               js_intl_rtf_proto_funcs, countof(js_intl_rtf_proto_funcs), rtf_finalizer, rtf_mark);
}
