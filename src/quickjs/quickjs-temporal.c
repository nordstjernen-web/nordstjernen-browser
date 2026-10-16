/* quickjs-temporal.c — Temporal namespace registration, object plumbing, option parsing and rounding. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cutils.h"
#include "dtoa.h"
#include "quickjs-temporal.h"

JSClassID tp_class_id[TP_KIND_COUNT];

const char *const tp_unit_singular[TP_UNIT_COUNT] = {
    "year", "month", "week", "day", "hour", "minute", "second", "millisecond", "microsecond", "nanosecond"
};

static const char *const tp_unit_plural[TP_UNIT_COUNT] = {
    "years", "months", "weeks", "days", "hours", "minutes", "seconds", "milliseconds", "microseconds",
    "nanoseconds"
};

const int64_t tp_unit_ns[TP_UNIT_COUNT] = {
    0, 0, 0, TP_NS_PER_DAY, 3600000000000LL, 60000000000LL, 1000000000LL, 1000000LL, 1000LL, 1LL
};

static const char *const tp_class_names[TP_KIND_COUNT] = {
    "Instant", "ZonedDateTime", "PlainDate", "PlainDateTime", "PlainTime", "PlainYearMonth",
    "PlainMonthDay", "Duration"
};

int64_t tp_floor_div64(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q--;
    return q;
}

int64_t tp_floor_mod64(int64_t a, int64_t b)
{
    return a - tp_floor_div64(a, b) * b;
}

tp_i128 tp_floor_div128(tp_i128 a, tp_i128 b)
{
    tp_i128 q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q--;
    return q;
}

tp_i128 tp_floor_mod128(tp_i128 a, tp_i128 b)
{
    return a - tp_floor_div128(a, b) * b;
}

tp_i128 tp_abs128(tp_i128 a)
{
    return a < 0 ? -a : a;
}

int tp_sign128(tp_i128 a)
{
    return (a > 0) - (a < 0);
}

int tp_cmp128(tp_i128 a, tp_i128 b)
{
    return (a > b) - (a < b);
}

int tp_i128_to_str(char *buf, tp_i128 v)
{
    char tmp[48];
    int n = 0, len = 0;
    unsigned __int128 u = v < 0 ? -(unsigned __int128)v : (unsigned __int128)v;

    do {
        tmp[n++] = (char)('0' + (int)(u % 10));
        u /= 10;
    } while (u != 0);
    if (v < 0)
        buf[len++] = '-';
    while (n > 0)
        buf[len++] = tmp[--n];
    buf[len] = '\0';
    return len;
}

double tp_i128_to_double(tp_i128 v)
{
    char buf[48];
    JSATODTempMem mem;
    if (v > -((tp_i128)1 << 53) && v < ((tp_i128)1 << 53))
        return (double)(int64_t)v;
    tp_i128_to_str(buf, v);
    return js_atod(buf, NULL, 10, 0, &mem);
}

double tp_i128_fdiv(tp_i128 num, tp_i128 den)
{
    char buf[128];
    int len = 0, digits = 0;
    unsigned __int128 n, d, q, r;
    bool negative = (num < 0) != (den < 0);
    JSATODTempMem mem;

    n = num < 0 ? -(unsigned __int128)num : (unsigned __int128)num;
    d = den < 0 ? -(unsigned __int128)den : (unsigned __int128)den;
    q = n / d;
    r = n % d;
    if (negative && (q != 0 || r != 0))
        buf[len++] = '-';
    len += tp_i128_to_str(buf + len, (tp_i128)q);
    if (r != 0) {
        buf[len++] = '.';
        while (r != 0 && digits < 60) {
            r *= 10;
            buf[len++] = (char)('0' + (int)(r / d));
            r %= d;
            digits++;
        }
        if (r != 0)
            buf[len++] = '1';
    }
    buf[len] = '\0';
    return js_atod(buf, NULL, 10, 0, &mem);
}

bool tp_double_to_i128(double d, tp_i128 *out)
{
    if (!isfinite(d) || fabs(d) >= 1e37)
        return false;
    *out = (tp_i128)d;
    return true;
}

int64_t tp_clamp_double(double d)
{
    if (d > (double)TP_FIELD_CLAMP)
        return TP_FIELD_CLAMP;
    if (d < -(double)TP_FIELD_CLAMP)
        return -TP_FIELD_CLAMP;
    return (int64_t)d;
}

void tp_buf_init(TPBuf *b)
{
    b->len = 0;
    b->s[0] = '\0';
}

void tp_buf_putn(TPBuf *b, const char *s, size_t n)
{
    if (n > sizeof(b->s) - 1 - (size_t)b->len)
        n = sizeof(b->s) - 1 - (size_t)b->len;
    memcpy(b->s + b->len, s, n);
    b->len += (int)n;
    b->s[b->len] = '\0';
}

void tp_buf_putc(TPBuf *b, char c)
{
    tp_buf_putn(b, &c, 1);
}

void tp_buf_puts(TPBuf *b, const char *s)
{
    tp_buf_putn(b, s, strlen(s));
}

void tp_buf_pad(TPBuf *b, int64_t v, int width)
{
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), "%0*lld", width, (long long)v);
    tp_buf_putn(b, tmp, (size_t)n);
}

JSValue tp_buf_to_string(JSContext *ctx, TPBuf *b)
{
    return JS_NewStringLen(ctx, b->s, (size_t)b->len);
}

TPObject *tp_get(JSValueConst v, int kind)
{
    return JS_GetOpaque(v, tp_class_id[kind]);
}

TPObject *tp_this(JSContext *ctx, JSValueConst this_val, int kind)
{
    TPObject *o = tp_get(this_val, kind);
    if (!o)
        JS_ThrowTypeError(ctx, "invalid receiver: expected Temporal.%s", tp_class_names[kind]);
    return o;
}

JSValue tp_alloc(JSContext *ctx, JSValueConst new_target, int kind, TPObject **pobj)
{
    JSValue v = js_temporal_create_from_ctor(ctx, new_target, tp_class_id[kind]);
    TPObject *o;

    if (JS_IsException(v))
        return v;
    o = js_mallocz(ctx, sizeof(TPObject));
    if (!o) {
        JS_FreeValue(ctx, v);
        return JS_EXCEPTION;
    }
    o->kind = kind;
    js_temporal_set_opaque(v, o);
    *pobj = o;
    return v;
}

static void tp_finalizer(JSRuntime *rt, JSValueConst val)
{
    JSClassID id;
    void *o = JS_GetAnyOpaque(val, &id);
    if (o)
        js_free_rt(rt, o);
}

int tp_to_integer_with_truncation(JSContext *ctx, JSValueConst v, double *out)
{
    double d;
    if (JS_ToFloat64(ctx, &d, v))
        return -1;
    if (!isfinite(d)) {
        JS_ThrowRangeError(ctx, "value must be a finite number");
        return -1;
    }
    *out = trunc(d) + 0.0;
    return 0;
}

int tp_to_positive_integer_with_truncation(JSContext *ctx, JSValueConst v, double *out)
{
    if (tp_to_integer_with_truncation(ctx, v, out))
        return -1;
    if (*out <= 0) {
        JS_ThrowRangeError(ctx, "value must be a positive integer");
        return -1;
    }
    return 0;
}

int tp_to_integer_if_integral(JSContext *ctx, JSValueConst v, double *out)
{
    double d;
    if (JS_ToFloat64(ctx, &d, v))
        return -1;
    if (!isfinite(d)) {
        JS_ThrowRangeError(ctx, "value must be a finite number");
        return -1;
    }
    if (trunc(d) != d) {
        JS_ThrowRangeError(ctx, "value must be an integer");
        return -1;
    }
    *out = d + 0.0;
    return 0;
}

int tp_require_string(JSContext *ctx, JSValueConst v)
{
    if (!JS_IsString(v)) {
        JS_ThrowTypeError(ctx, "expected a string");
        return -1;
    }
    return 0;
}

const char *tp_to_cstring_ascii(JSContext *ctx, JSValueConst v, size_t *plen)
{
    return JS_ToCStringLen(ctx, plen, v);
}

int tp_get_options_object(JSContext *ctx, JSValueConst options, JSValue *out)
{
    if (JS_IsUndefined(options)) {
        *out = JS_NewObjectProto(ctx, JS_NULL);
        return JS_IsException(*out) ? -1 : 0;
    }
    if (JS_IsObject(options)) {
        *out = JS_DupValue(ctx, options);
        return 0;
    }
    JS_ThrowTypeError(ctx, "options must be an object or undefined");
    return -1;
}

int tp_get_string_option(JSContext *ctx, JSValueConst options, const char *prop,
                         const char *const *values, int count, int fallback, int *out)
{
    JSValue v = JS_GetPropertyStr(ctx, options, prop);
    const char *s;
    size_t len;
    int i;

    if (JS_IsException(v))
        return -1;
    if (JS_IsUndefined(v)) {
        if (fallback == -1) {
            JS_ThrowRangeError(ctx, "%s option is required", prop);
            return -1;
        }
        *out = fallback;
        return 0;
    }
    s = JS_ToCStringLen(ctx, &len, v);
    JS_FreeValue(ctx, v);
    if (!s)
        return -1;
    for (i = 0; i < count; i++) {
        if (strlen(values[i]) == len && memcmp(values[i], s, len) == 0) {
            JS_FreeCString(ctx, s);
            *out = i;
            return 0;
        }
    }
    JS_ThrowRangeError(ctx, "invalid value for %s option: %s", prop, s);
    JS_FreeCString(ctx, s);
    return -1;
}

int tp_get_overflow_option(JSContext *ctx, JSValueConst options, int *out)
{
    static const char *const values[] = { "constrain", "reject" };
    return tp_get_string_option(ctx, options, "overflow", values, 2, TP_OVERFLOW_CONSTRAIN, out);
}

int tp_get_disambiguation_option(JSContext *ctx, JSValueConst options, int *out)
{
    static const char *const values[] = { "compatible", "earlier", "later", "reject" };
    return tp_get_string_option(ctx, options, "disambiguation", values, 4, TP_DISAMBIG_COMPATIBLE, out);
}

int tp_get_offset_option(JSContext *ctx, JSValueConst options, int fallback, int *out)
{
    static const char *const values[] = { "prefer", "use", "ignore", "reject" };
    return tp_get_string_option(ctx, options, "offset", values, 4, fallback, out);
}

int tp_get_show_calendar_option(JSContext *ctx, JSValueConst options, int *out)
{
    static const char *const values[] = { "auto", "always", "never", "critical" };
    return tp_get_string_option(ctx, options, "calendarName", values, 4, TP_SHOW_CAL_AUTO, out);
}

int tp_get_show_time_zone_option(JSContext *ctx, JSValueConst options, int *out)
{
    static const char *const values[] = { "auto", "never", "critical" };
    return tp_get_string_option(ctx, options, "timeZoneName", values, 3, TP_SHOW_TZ_AUTO, out);
}

int tp_get_show_offset_option(JSContext *ctx, JSValueConst options, int *out)
{
    static const char *const values[] = { "auto", "never" };
    return tp_get_string_option(ctx, options, "offset", values, 2, TP_SHOW_OFFSET_AUTO, out);
}

int tp_get_direction_option(JSContext *ctx, JSValueConst options, int *out)
{
    static const char *const values[] = { "next", "previous" };
    return tp_get_string_option(ctx, options, "direction", values, 2, -1, out);
}

int tp_get_rounding_mode_option(JSContext *ctx, JSValueConst options, int fallback, int *out)
{
    static const char *const values[] = {
        "ceil", "floor", "expand", "trunc", "halfCeil", "halfFloor", "halfExpand", "halfTrunc", "halfEven"
    };
    return tp_get_string_option(ctx, options, "roundingMode", values, 9, fallback, out);
}

int tp_get_rounding_increment_option(JSContext *ctx, JSValueConst options, int64_t *out)
{
    JSValue v = JS_GetPropertyStr(ctx, options, "roundingIncrement");
    double d;
    int r;

    if (JS_IsException(v))
        return -1;
    if (JS_IsUndefined(v)) {
        *out = 1;
        return 0;
    }
    r = tp_to_integer_with_truncation(ctx, v, &d);
    JS_FreeValue(ctx, v);
    if (r)
        return -1;
    if (d < 1 || d > 1e9) {
        JS_ThrowRangeError(ctx, "roundingIncrement must be between 1 and 1e9");
        return -1;
    }
    *out = (int64_t)d;
    return 0;
}

int tp_get_fractional_second_digits_option(JSContext *ctx, JSValueConst options, int *out)
{
    JSValue v = JS_GetPropertyStr(ctx, options, "fractionalSecondDigits");
    double d;

    if (JS_IsException(v))
        return -1;
    if (JS_IsUndefined(v)) {
        *out = TP_PRECISION_AUTO;
        return 0;
    }
    if (!JS_IsNumber(v)) {
        size_t len;
        const char *s = JS_ToCStringLen(ctx, &len, v);
        bool ok;
        JS_FreeValue(ctx, v);
        if (!s)
            return -1;
        ok = len == 4 && memcmp(s, "auto", 4) == 0;
        JS_FreeCString(ctx, s);
        if (!ok) {
            JS_ThrowRangeError(ctx, "fractionalSecondDigits must be 'auto' or 0 through 9");
            return -1;
        }
        *out = TP_PRECISION_AUTO;
        return 0;
    }
    JS_ToFloat64(ctx, &d, v);
    if (!isfinite(d) || floor(d) < 0 || floor(d) > 9) {
        JS_ThrowRangeError(ctx, "fractionalSecondDigits must be 'auto' or 0 through 9");
        return -1;
    }
    *out = (int)floor(d);
    return 0;
}

int tp_get_unit_option(JSContext *ctx, JSValueConst options, const char *key, bool required, int *out)
{
    const char *values[2 * TP_UNIT_COUNT + 1];
    int i, r, idx;

    for (i = 0; i < TP_UNIT_COUNT; i++) {
        values[2 * i] = tp_unit_singular[i];
        values[2 * i + 1] = tp_unit_plural[i];
    }
    values[2 * TP_UNIT_COUNT] = "auto";
    r = tp_get_string_option(ctx, options, key, values, 2 * TP_UNIT_COUNT + 1, required ? -1 : -2, &idx);
    if (r)
        return r;
    if (idx == -2)
        *out = TP_UNIT_UNSET;
    else if (idx == 2 * TP_UNIT_COUNT)
        *out = TP_UNIT_AUTO;
    else
        *out = idx / 2;
    return 0;
}

int tp_validate_unit(JSContext *ctx, int unit, int group, int extra1, int extra2)
{
    if (unit == TP_UNIT_UNSET || unit == extra1 || unit == extra2)
        return 0;
    if (unit != TP_UNIT_AUTO) {
        int category = tp_unit_category(unit);
        if (group == TP_GROUP_DATETIME || group == category)
            return 0;
    }
    JS_ThrowRangeError(ctx, "invalid unit %s", unit == TP_UNIT_AUTO ? "auto" : tp_unit_singular[unit]);
    return -1;
}

int tp_validate_rounding_increment(JSContext *ctx, int64_t increment, int64_t dividend, bool inclusive)
{
    int64_t maximum = inclusive ? dividend : dividend - 1;
    if (increment > maximum || dividend % increment != 0) {
        JS_ThrowRangeError(ctx, "invalid roundingIncrement %lld", (long long)increment);
        return -1;
    }
    return 0;
}

int tp_maximum_rounding_increment(int unit)
{
    switch (unit) {
    case TP_UNIT_HOUR:
        return 24;
    case TP_UNIT_MINUTE:
    case TP_UNIT_SECOND:
        return 60;
    case TP_UNIT_MILLISECOND:
    case TP_UNIT_MICROSECOND:
    case TP_UNIT_NANOSECOND:
        return 1000;
    default:
        return 0;
    }
}

TPPrecision tp_seconds_precision(int smallest_unit, int digits)
{
    TPPrecision p;
    p.increment = 1;
    switch (smallest_unit) {
    case TP_UNIT_MINUTE:
        p.precision = TP_PRECISION_MINUTE;
        p.unit = TP_UNIT_MINUTE;
        return p;
    case TP_UNIT_SECOND:
        p.precision = 0;
        p.unit = TP_UNIT_SECOND;
        return p;
    case TP_UNIT_MILLISECOND:
        p.precision = 3;
        p.unit = TP_UNIT_MILLISECOND;
        return p;
    case TP_UNIT_MICROSECOND:
        p.precision = 6;
        p.unit = TP_UNIT_MICROSECOND;
        return p;
    case TP_UNIT_NANOSECOND:
        p.precision = 9;
        p.unit = TP_UNIT_NANOSECOND;
        return p;
    default:
        break;
    }
    p.precision = digits;
    if (digits == TP_PRECISION_AUTO) {
        p.unit = TP_UNIT_NANOSECOND;
    } else if (digits == 0) {
        p.unit = TP_UNIT_SECOND;
    } else if (digits <= 3) {
        p.unit = TP_UNIT_MILLISECOND;
        p.increment = digits == 1 ? 100 : digits == 2 ? 10 : 1;
    } else if (digits <= 6) {
        p.unit = TP_UNIT_MICROSECOND;
        p.increment = digits == 4 ? 100 : digits == 5 ? 10 : 1;
    } else {
        p.unit = TP_UNIT_NANOSECOND;
        p.increment = digits == 7 ? 100 : digits == 8 ? 10 : 1;
    }
    return p;
}

int tp_negate_rounding_mode(int mode)
{
    switch (mode) {
    case TP_ROUND_CEIL:
        return TP_ROUND_FLOOR;
    case TP_ROUND_FLOOR:
        return TP_ROUND_CEIL;
    case TP_ROUND_HALF_CEIL:
        return TP_ROUND_HALF_FLOOR;
    case TP_ROUND_HALF_FLOOR:
        return TP_ROUND_HALF_CEIL;
    default:
        return mode;
    }
}

int tp_larger_unit(int a, int b)
{
    return a < b ? a : b;
}

bool tp_is_calendar_unit(int unit)
{
    return unit == TP_UNIT_YEAR || unit == TP_UNIT_MONTH || unit == TP_UNIT_WEEK;
}

int tp_unit_category(int unit)
{
    return unit <= TP_UNIT_DAY ? TP_GROUP_DATE : TP_GROUP_TIME;
}

int tp_round_options_from_arg(JSContext *ctx, JSValueConst arg, const char *key, JSValue *out)
{
    if (JS_IsUndefined(arg)) {
        JS_ThrowTypeError(ctx, "options parameter is required");
        return -1;
    }
    if (JS_IsString(arg)) {
        JSValue o = JS_NewObjectProto(ctx, JS_NULL);
        if (JS_IsException(o))
            return -1;
        if (JS_SetPropertyStr(ctx, o, key, JS_DupValue(ctx, arg)) < 0) {
            JS_FreeValue(ctx, o);
            return -1;
        }
        *out = o;
        return 0;
    }
    return tp_get_options_object(ctx, arg, out);
}

enum { TP_UROUND_ZERO, TP_UROUND_INFINITY, TP_UROUND_HALF_ZERO, TP_UROUND_HALF_INFINITY, TP_UROUND_HALF_EVEN };

int tp_unsigned_rounding_mode(int mode, bool negative)
{
    switch (mode) {
    case TP_ROUND_CEIL:
        return negative ? TP_UROUND_ZERO : TP_UROUND_INFINITY;
    case TP_ROUND_FLOOR:
        return negative ? TP_UROUND_INFINITY : TP_UROUND_ZERO;
    case TP_ROUND_EXPAND:
        return TP_UROUND_INFINITY;
    case TP_ROUND_TRUNC:
        return TP_UROUND_ZERO;
    case TP_ROUND_HALF_CEIL:
        return negative ? TP_UROUND_HALF_ZERO : TP_UROUND_HALF_INFINITY;
    case TP_ROUND_HALF_FLOOR:
        return negative ? TP_UROUND_HALF_INFINITY : TP_UROUND_HALF_ZERO;
    case TP_ROUND_HALF_EXPAND:
        return TP_UROUND_HALF_INFINITY;
    case TP_ROUND_HALF_TRUNC:
        return TP_UROUND_HALF_ZERO;
    default:
        return TP_UROUND_HALF_EVEN;
    }
}

tp_i128 tp_apply_unsigned_rounding(tp_i128 r1, tp_i128 r2, int cmp, bool even, int umode)
{
    if (umode == TP_UROUND_ZERO)
        return r1;
    if (umode == TP_UROUND_INFINITY)
        return r2;
    if (cmp < 0)
        return r1;
    if (cmp > 0)
        return r2;
    if (umode == TP_UROUND_HALF_ZERO)
        return r1;
    if (umode == TP_UROUND_HALF_INFINITY)
        return r2;
    return even ? r1 : r2;
}

tp_i128 tp_round_to_increment(tp_i128 x, tp_i128 increment, int mode)
{
    tp_i128 q = x / increment, r = x % increment, r1, rounded;
    bool negative = x < 0;
    int cmp;

    if (r == 0)
        return x;
    r1 = tp_abs128(q);
    cmp = tp_cmp128(tp_abs128(r) * 2, increment);
    rounded = tp_apply_unsigned_rounding(r1, r1 + 1, cmp, r1 % 2 == 0,
                                         tp_unsigned_rounding_mode(mode, negative));
    return (negative ? -rounded : rounded) * increment;
}

tp_i128 tp_round_to_increment_as_if_positive(tp_i128 x, tp_i128 increment, int mode)
{
    tp_i128 q = tp_floor_div128(x, increment), r = x - q * increment, rounded;
    int cmp;

    if (r == 0)
        return x;
    cmp = tp_cmp128(r * 2, increment);
    rounded = tp_apply_unsigned_rounding(q, q + 1, cmp, q % 2 == 0, tp_unsigned_rounding_mode(mode, false));
    return rounded * increment;
}

JSValue tp_value_of_throws(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    return JS_ThrowTypeError(ctx, "Temporal objects cannot be converted to primitives; use compare() or toString()");
}

int tp_define_class(JSContext *ctx, JSValue ns, int kind, const char *name, JSCFunction *ctor, int length,
                    const JSCFunctionListEntry *proto_funcs, int proto_count,
                    const JSCFunctionListEntry *static_funcs, int static_count)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    JSValue proto, func;

    tp_class_id[kind] = js_temporal_class_id(kind);
    if (!JS_IsRegisteredClass(rt, tp_class_id[kind])) {
        JSClassDef def = { tp_class_names[kind], .finalizer = tp_finalizer };
        if (JS_NewClass(rt, tp_class_id[kind], &def))
            return -1;
    }
    proto = JS_NewObject(ctx);
    if (JS_IsException(proto))
        return -1;
    if (JS_SetPropertyFunctionList(ctx, proto, proto_funcs, proto_count)) {
        JS_FreeValue(ctx, proto);
        return -1;
    }
    func = JS_NewCFunction2(ctx, ctor, name, length, JS_CFUNC_constructor, 0);
    if (JS_IsException(func)) {
        JS_FreeValue(ctx, proto);
        return -1;
    }
    if (static_count && JS_SetPropertyFunctionList(ctx, func, static_funcs, static_count)) {
        JS_FreeValue(ctx, proto);
        JS_FreeValue(ctx, func);
        return -1;
    }
    JS_SetConstructor(ctx, func, proto);
    JS_SetClassProto(ctx, tp_class_id[kind], proto);
    return JS_DefinePropertyValueStr(ctx, ns, name, func, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE) < 0 ? -1 : 0;
}

static JSValue tp_date_to_temporal_instant(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    double t;
    if (js_temporal_this_time_value(ctx, &t, this_val))
        return JS_EXCEPTION;
    if (!isfinite(t))
        return JS_ThrowRangeError(ctx, "invalid Date");
    return tp_new_instant(ctx, (tp_i128)(int64_t)t * 1000000, JS_UNDEFINED);
}

static const JSCFunctionListEntry tp_date_proto_funcs[] = {
    JS_CFUNC_DEF("toTemporalInstant", 0, tp_date_to_temporal_instant),
};

static const JSCFunctionListEntry tp_namespace_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Temporal", JS_PROP_CONFIGURABLE),
};

int JS_AddIntrinsicTemporal(JSContext *ctx)
{
    JSValue ns, global, date_proto;
    int r;

    ns = JS_NewObject(ctx);
    if (JS_IsException(ns))
        return -1;
    if (JS_SetPropertyFunctionList(ctx, ns, tp_namespace_funcs, countof(tp_namespace_funcs)) ||
        tp_init_instant(ctx, ns) || tp_init_plain_date(ctx, ns) || tp_init_plain_datetime(ctx, ns) ||
        tp_init_duration(ctx, ns) || tp_init_plain_time(ctx, ns) || tp_init_year_month(ctx, ns) ||
        tp_init_month_day(ctx, ns) || tp_init_zoned(ctx, ns) || tp_init_now(ctx, ns)) {
        JS_FreeValue(ctx, ns);
        return -1;
    }
    global = JS_GetGlobalObject(ctx);
    r = JS_DefinePropertyValueStr(ctx, global, "Temporal", ns, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_FreeValue(ctx, global);
    if (r < 0)
        return -1;
    date_proto = js_temporal_date_prototype(ctx);
    r = JS_SetPropertyFunctionList(ctx, date_proto, tp_date_proto_funcs, countof(tp_date_proto_funcs));
    JS_FreeValue(ctx, date_proto);
    return r;
}
