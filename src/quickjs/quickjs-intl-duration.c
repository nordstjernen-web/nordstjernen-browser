/* quickjs-intl-duration.c: Intl.DurationFormat and Temporal.Duration.prototype.toLocaleString. */

#include <stdlib.h>
#include <string.h>

#include "quickjs-intl.h"
#include "quickjs-temporal.h"

enum { DS_LONG, DS_SHORT, DS_NARROW, DS_NUMERIC, DS_2DIGIT, DS_FRACTIONAL, DS_DIGITAL = 3 };
enum { DU_YEARS, DU_MONTHS, DU_WEEKS, DU_DAYS, DU_HOURS, DU_MINUTES, DU_SECONDS, DU_MILLI, DU_MICRO, DU_NANO, DU_COUNT };

typedef struct DurUnitOptions {
    int style;
    int display;
} DurUnitOptions;

typedef struct IntlDurationFormat {
    char locale[INTL_TAG_MAX];
    char numbering[64];
    int style;
    DurUnitOptions u[DU_COUNT];
    int fractional_digits;
    char hm_sep[16];
    char ms_sep[16];
    bool two_digit_hours;
} IntlDurationFormat;

static const char *const df_style_names[] = { "long", "short", "narrow", "digital", NULL };
static const char *const unit_style_names[] = { "long", "short", "narrow", "numeric", "2-digit", "fractional" };
static const char *const date_unit_styles[] = { "long", "short", "narrow", NULL };
static const char *const time_unit_styles[] = { "long", "short", "narrow", "numeric", "2-digit", NULL };
static const char *const sub_second_styles[] = { "long", "short", "narrow", "numeric", NULL };
static const char *const display_names[] = { "auto", "always", NULL };
static const char *const unit_plural[DU_COUNT] = {
    "years", "months", "weeks", "days", "hours", "minutes", "seconds", "milliseconds", "microseconds", "nanoseconds",
};
static const char *const unit_singular[DU_COUNT] = {
    "year", "month", "week", "day", "hour", "minute", "second", "millisecond", "microsecond", "nanosecond",
};
static const char *const display_options[DU_COUNT] = {
    "yearsDisplay", "monthsDisplay", "weeksDisplay", "daysDisplay", "hoursDisplay", "minutesDisplay",
    "secondsDisplay", "millisecondsDisplay", "microsecondsDisplay", "nanosecondsDisplay",
};

static void df_finalizer(JSRuntime *rt, JSValueConst val)
{
    free(JS_GetOpaque(val, js_intl_class_id(INTL_CLASS_DURATION_FORMAT)));
}

static bool is_numeric_style(int s)
{
    return s == DS_NUMERIC || s == DS_2DIGIT || s == DS_FRACTIONAL;
}

static int unit_options(JSContext *ctx, JSValueConst options, int unit, int base_style, int prev_style,
                        bool two_digit_hours, DurUnitOptions *out)
{
    const char *const *styles = unit < DU_HOURS ? date_unit_styles : unit < DU_MILLI ? time_unit_styles
                                                                                     : sub_second_styles;
    int digital_base = unit < DU_HOURS ? DS_SHORT : DS_NUMERIC;
    bool fractional_unit = unit >= DU_MILLI;
    int style = intl_get_enum_option(ctx, options, unit_plural[unit], styles, -1), display_default = 1, display;
    if (style < -1)
        return -1;
    if (style < 0) {
        if (base_style == DS_DIGITAL) {
            style = digital_base;
            if (unit < DU_HOURS || unit > DU_SECONDS)
                display_default = 0;
        } else if (prev_style >= 0 && is_numeric_style(prev_style)) {
            style = DS_NUMERIC;
            if (unit != DU_MINUTES && unit != DU_SECONDS)
                display_default = 0;
        } else {
            style = base_style;
            display_default = 0;
        }
    }
    if (style == DS_NUMERIC && fractional_unit) {
        style = DS_FRACTIONAL;
        display_default = 0;
    }
    display = intl_get_enum_option(ctx, options, display_options[unit], display_names, display_default);
    if (display < -1)
        return -1;
    if (display == 1 && style == DS_FRACTIONAL) {
        JS_ThrowRangeError(ctx, "%s cannot be displayed always with a fractional style", unit_plural[unit]);
        return -1;
    }
    if (prev_style == DS_FRACTIONAL && style != DS_FRACTIONAL) {
        JS_ThrowRangeError(ctx, "%s must use a fractional style", unit_plural[unit]);
        return -1;
    }
    if ((prev_style == DS_NUMERIC || prev_style == DS_2DIGIT) && !is_numeric_style(style)) {
        JS_ThrowRangeError(ctx, "%s must use a numeric style", unit_plural[unit]);
        return -1;
    }
    if (unit == DU_HOURS && two_digit_hours && style == DS_NUMERIC)
        style = DS_2DIGIT;
    if ((unit == DU_MINUTES || unit == DU_SECONDS) && (prev_style == DS_NUMERIC || prev_style == DS_2DIGIT))
        style = DS_2DIGIT;
    out->style = style;
    out->display = display;
    return 0;
}

static void digital_format(IntlDurationFormat *df, int data)
{
    const char *p = intl_data_get(data, "du"), *h, *m, *s;
    snprintf(df->hm_sep, sizeof(df->hm_sep), ":");
    snprintf(df->ms_sep, sizeof(df->ms_sep), ":");
    if (!p)
        return;
    df->two_digit_hours = p[0] == 'h' && p[1] == 'h';
    h = strrchr(p, 'h');
    m = strchr(p, 'm');
    if (h && m && m > h)
        snprintf(df->hm_sep, sizeof(df->hm_sep), "%.*s", (int)(m - h - 1), h + 1);
    m = strrchr(p, 'm');
    s = strchr(p, 's');
    if (m && s && s > m)
        snprintf(df->ms_sep, sizeof(df->ms_sep), "%.*s", (int)(s - m - 1), m + 1);
}

static JSValue js_intl_df_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    JSValue obj, options = JS_UNDEFINED;
    IntlDurationFormat *df = NULL;
    IntlLocaleList requested;
    IntlResolved res;
    char nu[64];
    char *opts[1];
    bool lookup, has_nu;
    int prev = -1;
    obj = js_intl_create_from_ctor(ctx, new_target, js_intl_class_id(INTL_CLASS_DURATION_FORMAT));
    if (JS_IsException(obj))
        return obj;
    if (intl_canonicalize_locale_list(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &requested) < 0)
        goto fail0;
    df = calloc(1, sizeof(*df));
    if (!df) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    options = intl_get_options_object(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
    if (JS_IsException(options))
        goto fail;
    if (intl_get_locale_matcher(ctx, options, &lookup) < 0)
        goto fail;
    if (intl_get_resolution_option(ctx, options, "numberingSystem", NULL, nu, sizeof(nu), &has_nu) < 0)
        goto fail;
    opts[0] = has_nu ? nu : NULL;
    if (intl_resolve_locale(ctx, INTL_CLASS_DURATION_FORMAT, &requested, lookup, &intl_nu_resolver, opts, &res) < 0)
        goto fail;
    snprintf(df->locale, sizeof(df->locale), "%s", res.locale);
    snprintf(df->numbering, sizeof(df->numbering), "%s", res.values[0]);
    digital_format(df, intl_data_node(res.data_locale));
    df->style = intl_get_enum_option(ctx, options, "style", df_style_names, DS_SHORT);
    if (df->style < -1)
        goto fail;
    for (int u = 0; u < DU_COUNT; u++) {
        if (unit_options(ctx, options, u, df->style, prev, df->two_digit_hours, &df->u[u]) < 0)
            goto fail;
        if (u >= DU_HOURS && u <= DU_MICRO)
            prev = df->u[u].style;
    }
    if (intl_get_number_option(ctx, options, "fractionalDigits", 0, 9, -1, &df->fractional_digits) < 0)
        goto fail;
    js_intl_set_opaque(obj, df);
    intl_locale_list_free(ctx, &requested);
    JS_FreeValue(ctx, options);
    return obj;
fail:
    intl_locale_list_free(ctx, &requested);
fail0:
    free(df);
    JS_FreeValue(ctx, options);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static IntlDurationFormat *get_df(JSContext *ctx, JSValueConst this_val, const char *method)
{
    IntlDurationFormat *df = JS_GetOpaque(this_val, js_intl_class_id(INTL_CLASS_DURATION_FORMAT));
    if (!df)
        JS_ThrowTypeError(ctx, "Intl.DurationFormat.prototype.%s called on incompatible receiver", method);
    return df;
}

typedef struct DurValues {
    tp_i128 v[DU_COUNT];
    int sign;
} DurValues;

static void put_i128(DynBuf *b, tp_i128 x)
{
    char tmp[48];
    int n = 0;
    unsigned __int128 u = x < 0 ? (unsigned __int128)(-x) : (unsigned __int128)x;
    do {
        tmp[n++] = (char)('0' + (int)(u % 10));
        u /= 10;
    } while (u);
    while (n > 0)
        dbuf_putc(b, tmp[--n]);
}

static char *decimal_with_fraction(const IntlDurationFormat *df, const DurValues *d, int unit, bool include_fraction,
                                   bool negative_zero)
{
    DynBuf b;
    tp_i128 total = d->v[unit];
    int digits = 0;
    char *r;
    if (include_fraction) {
        for (int j = unit + 1; j < DU_COUNT && df->u[j].style == DS_FRACTIONAL; j++) {
            total = total * 1000 + d->v[j];
            digits += 3;
        }
    }
    dbuf_init(&b);
    if (total < 0 || (total == 0 && negative_zero))
        dbuf_putc(&b, '-');
    if (digits == 0) {
        put_i128(&b, total);
    } else {
        DynBuf num;
        size_t len;
        dbuf_init(&num);
        put_i128(&num, total);
        len = num.size;
        if ((int)len <= digits) {
            dbuf_putstr(&b, "0.");
            for (int k = 0; k < digits - (int)len; k++)
                dbuf_putc(&b, '0');
            dbuf_put(&b, num.buf, len);
        } else {
            dbuf_put(&b, num.buf, len - (size_t)digits);
            dbuf_putc(&b, '.');
            dbuf_put(&b, num.buf + len - digits, (size_t)digits);
        }
        dbuf_free(&num);
    }
    dbuf_putc(&b, '\0');
    r = b.error ? NULL : strdup((const char *)b.buf);
    dbuf_free(&b);
    return r;
}

static int format_number(JSContext *ctx, JSValueConst nf_options, const IntlDurationFormat *df, const char *value,
                         const char *unit, IntlParts *out)
{
    JSValue locale = JS_NewString(ctx, df->locale), nf;
    IntlParts parts;
    int r;
    if (JS_IsException(locale))
        return -1;
    nf = intl_construct_intrinsic(ctx, INTL_INTRINSIC_NUMBER_FORMAT, locale, nf_options);
    JS_FreeValue(ctx, locale);
    if (JS_IsException(nf))
        return -1;
    intl_parts_init(&parts);
    r = intl_nf_format_decimal(ctx, nf, value, &parts);
    if (r == 0)
        intl_parts_append(out, &parts, unit);
    intl_parts_free(&parts);
    JS_FreeValue(ctx, nf);
    return r;
}

static JSValue nf_options_base(JSContext *ctx, const IntlDurationFormat *df)
{
    JSValue o = JS_NewObjectProto(ctx, JS_NULL);
    if (JS_IsException(o))
        return o;
    if (intl_set_prop_str(ctx, o, "numberingSystem", df->numbering) < 0) {
        JS_FreeValue(ctx, o);
        return JS_EXCEPTION;
    }
    return o;
}

static int set_fraction_options(JSContext *ctx, JSValueConst o, const IntlDurationFormat *df)
{
    int fd = df->fractional_digits;
    if (intl_set_prop_value(ctx, o, "maximumFractionDigits", JS_NewInt32(ctx, fd < 0 ? 9 : fd)) < 0 ||
        intl_set_prop_value(ctx, o, "minimumFractionDigits", JS_NewInt32(ctx, fd < 0 ? 0 : fd)) < 0 ||
        intl_set_prop_str(ctx, o, "roundingMode", "trunc") < 0)
        return -1;
    return 0;
}

static int format_numeric_unit(JSContext *ctx, const IntlDurationFormat *df, const DurValues *d, int unit,
                               bool sign_displayed, bool separator, IntlParts *out)
{
    JSValue o = nf_options_base(ctx, df);
    char *value;
    int r = -1;
    if (JS_IsException(o))
        return -1;
    if (separator)
        intl_parts_add(out, "literal", unit == DU_MINUTES ? df->hm_sep : df->ms_sep,
                       strlen(unit == DU_MINUTES ? df->hm_sep : df->ms_sep), 0);
    if (df->u[unit].style == DS_2DIGIT && intl_set_prop_value(ctx, o, "minimumIntegerDigits", JS_NewInt32(ctx, 2)) < 0)
        goto done;
    if (!sign_displayed && intl_set_prop_str(ctx, o, "signDisplay", "never") < 0)
        goto done;
    if (intl_set_prop_value(ctx, o, "useGrouping", JS_FALSE) < 0)
        goto done;
    if (unit == DU_SECONDS && set_fraction_options(ctx, o, df) < 0)
        goto done;
    value = decimal_with_fraction(df, d, unit, unit == DU_SECONDS,
                                  sign_displayed && d->v[unit] == 0 && d->sign < 0 && unit != DU_SECONDS);
    if (!value) {
        JS_ThrowOutOfMemory(ctx);
        goto done;
    }
    r = format_number(ctx, o, df, value, unit_singular[unit], out);
    free(value);
done:
    JS_FreeValue(ctx, o);
    return r;
}

static bool fraction_nonzero(const DurValues *d)
{
    return d->v[DU_MILLI] != 0 || d->v[DU_MICRO] != 0 || d->v[DU_NANO] != 0;
}

static int format_numeric_units(JSContext *ctx, const IntlDurationFormat *df, const DurValues *d, int first,
                                bool sign_displayed, IntlParts *out)
{
    bool hours_formatted = false, minutes_formatted = false, seconds_formatted;
    bool seconds_nonzero = d->v[DU_SECONDS] != 0;
    if (fraction_nonzero(d)) {
        for (int j = DU_MILLI; j < DU_COUNT && df->u[j].style == DS_FRACTIONAL; j++)
            if (d->v[j] != 0)
                seconds_nonzero = true;
    }
    if (first == DU_HOURS && (d->v[DU_HOURS] != 0 || df->u[DU_HOURS].display == 1))
        hours_formatted = true;
    seconds_formatted = seconds_nonzero || df->u[DU_SECONDS].display == 1;
    if (first == DU_HOURS || first == DU_MINUTES) {
        if (hours_formatted && seconds_formatted)
            minutes_formatted = true;
        else if (d->v[DU_MINUTES] != 0 || df->u[DU_MINUTES].display == 1)
            minutes_formatted = true;
    }
    if (hours_formatted) {
        if (format_numeric_unit(ctx, df, d, DU_HOURS, sign_displayed, false, out) < 0)
            return -1;
        sign_displayed = false;
    }
    if (minutes_formatted) {
        if (format_numeric_unit(ctx, df, d, DU_MINUTES, sign_displayed, hours_formatted, out) < 0)
            return -1;
        sign_displayed = false;
    }
    if (seconds_formatted) {
        if (format_numeric_unit(ctx, df, d, DU_SECONDS, sign_displayed, minutes_formatted, out) < 0)
            return -1;
    }
    return 0;
}

static int list_format_parts(JSContext *ctx, const IntlDurationFormat *df, IntlParts *lists, int n, IntlParts *out)
{
    JSValue o = JS_NewObjectProto(ctx, JS_NULL), locale = JS_UNDEFINED, lf = JS_UNDEFINED;
    char **strs = NULL;
    IntlParts lparts;
    int r = -1, idx = 0;
    intl_parts_init(&lparts);
    if (JS_IsException(o))
        return -1;
    if (intl_set_prop_str(ctx, o, "type", "unit") < 0 ||
        intl_set_prop_str(ctx, o, "style", df_style_names[df->style == DS_DIGITAL ? DS_SHORT : df->style]) < 0)
        goto done;
    locale = JS_NewString(ctx, df->locale);
    if (JS_IsException(locale))
        goto done;
    lf = intl_construct_intrinsic(ctx, INTL_INTRINSIC_LIST_FORMAT, locale, o);
    if (JS_IsException(lf))
        goto done;
    strs = calloc((size_t)n + 1, sizeof(*strs));
    if (!strs) {
        JS_ThrowOutOfMemory(ctx);
        goto done;
    }
    for (int i = 0; i < n; i++) {
        strs[i] = intl_parts_join(&lists[i]);
        if (!strs[i]) {
            JS_ThrowOutOfMemory(ctx);
            goto done;
        }
    }
    if (intl_lf_partition_strings(ctx, lf, (const char *const *)strs, (uint32_t)n, &lparts) < 0)
        goto done;
    for (int i = 0; i < lparts.count; i++) {
        if (!strcmp(lparts.items[i].type, "element"))
            intl_parts_append(out, &lists[idx++], NULL);
        else
            intl_parts_add(out, "literal", lparts.items[i].value, strlen(lparts.items[i].value), 0);
    }
    r = 0;
done:
    if (strs) {
        for (int i = 0; i < n; i++)
            free(strs[i]);
        free(strs);
    }
    intl_parts_free(&lparts);
    JS_FreeValue(ctx, lf);
    JS_FreeValue(ctx, locale);
    JS_FreeValue(ctx, o);
    return r;
}

static int df_partition(JSContext *ctx, const IntlDurationFormat *df, const DurValues *d, IntlParts *out)
{
    IntlParts lists[DU_COUNT];
    int n = 0, r = -1;
    bool sign_displayed = true;
    for (int u = 0; u < DU_COUNT; u++) {
        int style = df->u[u].style;
        if (style == DS_NUMERIC || style == DS_2DIGIT) {
            intl_parts_init(&lists[n]);
            if (format_numeric_units(ctx, df, d, u, sign_displayed, &lists[n]) < 0) {
                n++;
                goto done;
            }
            if (lists[n].count > 0)
                n++;
            else
                intl_parts_free(&lists[n]);
            break;
        } else {
            bool next_fractional = u >= DU_SECONDS && u < DU_NANO && df->u[u + 1].style == DS_FRACTIONAL;
            tp_i128 value_nonzero = d->v[u];
            JSValue o;
            char *value;
            if (next_fractional) {
                for (int j = u + 1; j < DU_COUNT && df->u[j].style == DS_FRACTIONAL; j++)
                    value_nonzero |= d->v[j];
            }
            if (!(df->u[u].display == 1 || value_nonzero != 0)) {
                if (next_fractional)
                    break;
                continue;
            }
            o = nf_options_base(ctx, df);
            if (JS_IsException(o))
                goto done;
            if (next_fractional && set_fraction_options(ctx, o, df) < 0) {
                JS_FreeValue(ctx, o);
                goto done;
            }
            if (!sign_displayed && intl_set_prop_str(ctx, o, "signDisplay", "never") < 0) {
                JS_FreeValue(ctx, o);
                goto done;
            }
            if (intl_set_prop_str(ctx, o, "style", "unit") < 0 ||
                intl_set_prop_str(ctx, o, "unit", unit_singular[u]) < 0 ||
                intl_set_prop_str(ctx, o, "unitDisplay", unit_style_names[style]) < 0) {
                JS_FreeValue(ctx, o);
                goto done;
            }
            value = decimal_with_fraction(df, d, u, next_fractional,
                                          sign_displayed && value_nonzero == 0 && d->sign < 0);
            sign_displayed = false;
            if (!value) {
                JS_FreeValue(ctx, o);
                JS_ThrowOutOfMemory(ctx);
                goto done;
            }
            intl_parts_init(&lists[n]);
            n++;
            if (format_number(ctx, o, df, value, unit_singular[u], &lists[n - 1]) < 0) {
                free(value);
                JS_FreeValue(ctx, o);
                goto done;
            }
            free(value);
            JS_FreeValue(ctx, o);
            if (next_fractional)
                break;
        }
    }
    r = list_format_parts(ctx, df, lists, n, out);
    if (r == 0 && out->error) {
        JS_ThrowOutOfMemory(ctx);
        r = -1;
    }
done:
    for (int i = 0; i < n; i++)
        intl_parts_free(&lists[i]);
    return r;
}

static int to_duration_values(JSContext *ctx, JSValueConst v, DurValues *d)
{
    TPDuration dur;
    if (tp_to_temporal_duration(ctx, v, &dur) < 0)
        return -1;
    for (int u = 0; u < DU_COUNT; u++)
        d->v[u] = (tp_i128)dur.f[u];
    d->sign = tp_duration_sign(&dur);
    return 0;
}

static JSValue df_parts_to_array(JSContext *ctx, const IntlParts *p)
{
    JSValue arr = JS_NewArray(ctx);
    if (JS_IsException(arr))
        return arr;
    for (int i = 0; i < p->count; i++) {
        JSValue part = intl_make_part(ctx, p->items[i].type, p->items[i].value, strlen(p->items[i].value));
        if (JS_IsException(part))
            goto fail;
        if (p->items[i].source && intl_set_prop_str(ctx, part, "unit", p->items[i].source) < 0) {
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

static JSValue df_format(JSContext *ctx, const IntlDurationFormat *df, JSValueConst value, bool to_parts)
{
    DurValues d;
    IntlParts parts;
    JSValue r;
    if (to_duration_values(ctx, value, &d) < 0)
        return JS_EXCEPTION;
    intl_parts_init(&parts);
    if (df_partition(ctx, df, &d, &parts) < 0)
        r = JS_EXCEPTION;
    else if (to_parts)
        r = df_parts_to_array(ctx, &parts);
    else
        r = intl_parts_to_string(ctx, &parts);
    intl_parts_free(&parts);
    return r;
}

static JSValue js_intl_df_format(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int to_parts)
{
    IntlDurationFormat *df = get_df(ctx, this_val, to_parts ? "formatToParts" : "format");
    if (!df)
        return JS_EXCEPTION;
    return df_format(ctx, df, argc > 0 ? argv[0] : JS_UNDEFINED, to_parts);
}

static JSValue js_intl_df_resolvedOptions(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    IntlDurationFormat *df = get_df(ctx, this_val, "resolvedOptions");
    JSValue obj;
    if (!df)
        return JS_EXCEPTION;
    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return obj;
    if (intl_set_prop_str(ctx, obj, "locale", df->locale) < 0 ||
        intl_set_prop_str(ctx, obj, "numberingSystem", df->numbering) < 0 ||
        intl_set_prop_str(ctx, obj, "style", df_style_names[df->style]) < 0)
        goto fail;
    for (int u = 0; u < DU_COUNT; u++) {
        int s = df->u[u].style == DS_FRACTIONAL ? DS_NUMERIC : df->u[u].style;
        if (intl_set_prop_str(ctx, obj, unit_plural[u], unit_style_names[s]) < 0 ||
            intl_set_prop_str(ctx, obj, display_options[u], display_names[df->u[u].display]) < 0)
            goto fail;
    }
    if (df->fractional_digits >= 0 &&
        intl_set_prop_value(ctx, obj, "fractionalDigits", JS_NewInt32(ctx, df->fractional_digits)) < 0)
        goto fail;
    return obj;
fail:
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue js_temporal_duration_toLocaleString(JSContext *ctx, JSValueConst this_val, int argc,
                                                   JSValueConst *argv)
{
    JSValue fmt, r;
    IntlDurationFormat *df;
    if (!JS_GetOpaque(this_val, js_temporal_class_id(TP_KIND_DURATION)))
        return JS_ThrowTypeError(ctx, "not a Temporal.Duration");
    fmt = intl_construct_intrinsic(ctx, INTL_INTRINSIC_DURATION_FORMAT, argc > 0 ? argv[0] : JS_UNDEFINED,
                                   argc > 1 ? argv[1] : JS_UNDEFINED);
    if (JS_IsException(fmt))
        return fmt;
    df = JS_GetOpaque(fmt, js_intl_class_id(INTL_CLASS_DURATION_FORMAT));
    r = df_format(ctx, df, this_val, false);
    JS_FreeValue(ctx, fmt);
    return r;
}

static const JSCFunctionListEntry js_intl_df_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("format", 1, js_intl_df_format, 0),
    JS_CFUNC_MAGIC_DEF("formatToParts", 1, js_intl_df_format, 1),
    JS_CFUNC_DEF("resolvedOptions", 0, js_intl_df_resolvedOptions),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Intl.DurationFormat", JS_PROP_CONFIGURABLE),
};

int js_intl_init_duration_format(JSContext *ctx, JSValueConst intl)
{
    JSValue proto;
    int r = 0;
    if (intl_define_service(ctx, intl, "DurationFormat", INTL_CLASS_DURATION_FORMAT, INTL_INTRINSIC_DURATION_FORMAT,
                            js_intl_df_ctor, 0, JS_CFUNC_constructor, js_intl_df_proto_funcs,
                            countof(js_intl_df_proto_funcs), df_finalizer, NULL) < 0)
        return -1;
    proto = JS_GetClassProto(ctx, js_temporal_class_id(TP_KIND_DURATION));
    if (JS_IsObject(proto))
        r = JS_DefinePropertyValueStr(ctx, proto, "toLocaleString",
                                      JS_NewCFunction(ctx, js_temporal_duration_toLocaleString, "toLocaleString", 0),
                                      JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_FreeValue(ctx, proto);
    return r;
}
