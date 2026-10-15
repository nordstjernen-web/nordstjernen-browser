/* quickjs-temporal-instant.c — Temporal.Instant and Temporal.Now. */
#include <math.h>
#include <string.h>
#include "cutils.h"
#include "quickjs-temporal.h"

JSValue tp_new_instant(JSContext *ctx, tp_i128 ns, JSValueConst new_target)
{
    TPObject *o;
    JSValue v;

    if (!tp_epoch_ns_valid(ns))
        return JS_ThrowRangeError(ctx, "instant is outside the supported range");
    v = tp_alloc(ctx, new_target, TP_KIND_INSTANT, &o);
    if (JS_IsException(v))
        return v;
    o->ns = ns;
    return v;
}

static int tp_to_epoch_ns_bigint(JSContext *ctx, JSValueConst v, tp_i128 *out)
{
    JSValue b = js_temporal_to_bigint(ctx, v);
    int r;

    if (JS_IsException(b))
        return -1;
    r = js_temporal_bigint_to_i128(ctx, out, b);
    JS_FreeValue(ctx, b);
    if (r || !tp_epoch_ns_valid(*out)) {
        JS_ThrowRangeError(ctx, "epoch nanoseconds are outside the supported range");
        return -1;
    }
    return 0;
}

int tp_to_temporal_instant(JSContext *ctx, JSValueConst v, tp_i128 *out)
{
    static const int goals[] = { TP_GOAL_INSTANT, TP_GOAL_END };
    TPObject *o;
    JSValue prim;
    const char *s;
    size_t len;
    TPParsed p;
    TPISODate d;
    TPDateTime balanced;
    int r;

    if ((o = tp_get(v, TP_KIND_INSTANT)) || (o = tp_get(v, TP_KIND_ZONED))) {
        *out = o->ns;
        return 0;
    }
    prim = JS_IsObject(v) ? js_temporal_to_primitive(ctx, v, true) : JS_DupValue(ctx, v);
    if (JS_IsException(prim))
        return -1;
    if (!JS_IsString(prim)) {
        JS_FreeValue(ctx, prim);
        JS_ThrowTypeError(ctx, "Instant must be a string or Temporal object");
        return -1;
    }
    s = JS_ToCStringLen(ctx, &len, prim);
    JS_FreeValue(ctx, prim);
    if (!s)
        return -1;
    r = tp_parse_iso_datetime(ctx, s, len, goals, &p);
    JS_FreeCString(ctx, s);
    if (r)
        return -1;
    d.year = p.year;
    d.month = p.month;
    d.day = p.day;
    balanced = tp_balance_datetime(d, tp_time_to_ns(&p.time) - (p.z ? 0 : p.offset_ns));
    if (tp_check_iso_days_range(ctx, balanced.date))
        return -1;
    *out = tp_utc_epoch_ns(&balanced);
    if (!tp_epoch_ns_valid(*out)) {
        JS_ThrowRangeError(ctx, "instant is outside the supported range");
        return -1;
    }
    return 0;
}

static JSValue tp_instant_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    tp_i128 ns;
    if (tp_to_epoch_ns_bigint(ctx, tp_arg(0), &ns))
        return JS_EXCEPTION;
    return tp_new_instant(ctx, ns, new_target);
}

static JSValue tp_instant_from(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    tp_i128 ns;
    if (tp_to_temporal_instant(ctx, tp_arg(0), &ns))
        return JS_EXCEPTION;
    return tp_new_instant(ctx, ns, JS_UNDEFINED);
}

static JSValue tp_instant_from_epoch_milliseconds(JSContext *ctx, JSValueConst this_val, int argc,
                                                  JSValueConst *argv)
{
    double ms;
    tp_i128 ns;

    if (JS_ToFloat64(ctx, &ms, tp_arg(0)))
        return JS_EXCEPTION;
    if (!isfinite(ms) || trunc(ms) != ms)
        return JS_ThrowRangeError(ctx, "epoch milliseconds must be an integer");
    if (fabs(ms) > 8.64e15)
        return JS_ThrowRangeError(ctx, "instant is outside the supported range");
    ns = (tp_i128)(int64_t)ms * 1000000;
    return tp_new_instant(ctx, ns, JS_UNDEFINED);
}

static JSValue tp_instant_from_epoch_nanoseconds(JSContext *ctx, JSValueConst this_val, int argc,
                                                 JSValueConst *argv)
{
    tp_i128 ns;
    if (tp_to_epoch_ns_bigint(ctx, tp_arg(0), &ns))
        return JS_EXCEPTION;
    return tp_new_instant(ctx, ns, JS_UNDEFINED);
}

static JSValue tp_instant_compare(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    tp_i128 one, two;
    if (tp_to_temporal_instant(ctx, tp_arg(0), &one) || tp_to_temporal_instant(ctx, tp_arg(1), &two))
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, tp_cmp128(one, two));
}

static JSValue tp_instant_get_epoch_milliseconds(JSContext *ctx, JSValueConst this_val)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_INSTANT);
    if (!o)
        return JS_EXCEPTION;
    return JS_NewFloat64(ctx, (double)(int64_t)tp_floor_div128(o->ns, 1000000));
}

static JSValue tp_instant_get_epoch_nanoseconds(JSContext *ctx, JSValueConst this_val)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_INSTANT);
    if (!o)
        return JS_EXCEPTION;
    return js_temporal_new_bigint128(ctx, o->ns);
}

static JSValue tp_instant_add(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_INSTANT);
    TPDuration d;
    TPInternalDuration id;
    tp_i128 ns;

    if (!o)
        return JS_EXCEPTION;
    if (tp_to_temporal_duration(ctx, tp_arg(0), &d))
        return JS_EXCEPTION;
    if (magic)
        tp_negate_duration(&d);
    if (tp_unit_category(tp_default_largest_unit(&d)) == TP_GROUP_DATE)
        return JS_ThrowRangeError(ctx, "Instant arithmetic does not support years, months, weeks, or days");
    if (tp_to_internal_duration_24h(ctx, &d, &id) || tp_add_instant(ctx, o->ns, id.time, &ns))
        return JS_EXCEPTION;
    return tp_new_instant(ctx, ns, JS_UNDEFINED);
}

static JSValue tp_instant_difference(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
                                     int magic)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_INSTANT);
    tp_i128 other;
    JSValue opts;
    TPDiffSettings s;
    TPInternalDuration d;
    JSValue ret = JS_EXCEPTION;

    if (!o)
        return JS_EXCEPTION;
    if (tp_to_temporal_instant(ctx, tp_arg(0), &other))
        return JS_EXCEPTION;
    if (tp_get_options_object(ctx, tp_arg(1), &opts))
        return JS_EXCEPTION;
    if (!tp_get_difference_settings(ctx, magic, opts, TP_GROUP_TIME, 0, TP_UNIT_NANOSECOND, TP_UNIT_SECOND, &s) &&
        !tp_difference_instant(ctx, o->ns, other, s.rounding_increment, s.smallest_unit, s.rounding_mode, &d))
        ret = tp_duration_result(ctx, &d, s.largest_unit, magic);
    JS_FreeValue(ctx, opts);
    return ret;
}

static JSValue tp_instant_round(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    static const int64_t maxima[TP_UNIT_COUNT] = {
        0, 0, 0, 0, 24, 1440, 86400, 86400000LL, 86400000000LL, 86400000000000LL
    };
    TPObject *o = tp_this(ctx, this_val, TP_KIND_INSTANT);
    JSValue opts, ret = JS_EXCEPTION;
    int64_t increment;
    int mode, smallest;

    if (!o)
        return JS_EXCEPTION;
    if (tp_round_options_from_arg(ctx, tp_arg(0), "smallestUnit", &opts))
        return JS_EXCEPTION;
    if (tp_get_rounding_increment_option(ctx, opts, &increment) ||
        tp_get_rounding_mode_option(ctx, opts, TP_ROUND_HALF_EXPAND, &mode) ||
        tp_get_unit_option(ctx, opts, "smallestUnit", true, &smallest) ||
        tp_validate_unit(ctx, smallest, TP_GROUP_TIME, TP_UNIT_UNSET, TP_UNIT_UNSET) ||
        tp_validate_rounding_increment(ctx, increment, maxima[smallest], true))
        goto done;
    ret = tp_new_instant(ctx,
                         tp_round_to_increment_as_if_positive(o->ns, (tp_i128)tp_unit_ns[smallest] * increment, mode),
                         JS_UNDEFINED);
done:
    JS_FreeValue(ctx, opts);
    return ret;
}

static JSValue tp_instant_equals(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_INSTANT);
    tp_i128 other;
    if (!o)
        return JS_EXCEPTION;
    if (tp_to_temporal_instant(ctx, tp_arg(0), &other))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, o->ns == other);
}

static JSValue tp_instant_string(JSContext *ctx, tp_i128 ns, const TPTimeZone *tz, int precision)
{
    TPTimeZone output = { TP_TZ_UTC_ID, 0 };
    TPDateTime dt;
    TPBuf b;

    if (tz)
        output = *tz;
    tp_iso_datetime_for(output, ns, &dt);
    tp_buf_init(&b);
    tp_put_iso_datetime(&b, &dt, TP_CAL_ISO8601, precision, TP_SHOW_CAL_NEVER);
    if (tz)
        tp_format_offset_rounded(&b, tp_offset_ns_for(output, ns));
    else
        tp_buf_putc(&b, 'Z');
    return tp_buf_to_string(ctx, &b);
}

static JSValue tp_instant_to_string(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_INSTANT);
    JSValue opts, tzv = JS_UNDEFINED, ret = JS_EXCEPTION;
    int digits, mode, smallest;
    TPTimeZone tz;
    bool has_tz = false;
    TPPrecision p;
    tp_i128 rounded;

    if (!o)
        return JS_EXCEPTION;
    if (tp_get_options_object(ctx, tp_arg(0), &opts))
        return JS_EXCEPTION;
    if (tp_get_fractional_second_digits_option(ctx, opts, &digits) ||
        tp_get_rounding_mode_option(ctx, opts, TP_ROUND_TRUNC, &mode) ||
        tp_get_unit_option(ctx, opts, "smallestUnit", false, &smallest))
        goto done;
    tzv = JS_GetPropertyStr(ctx, opts, "timeZone");
    if (JS_IsException(tzv) || tp_validate_unit(ctx, smallest, TP_GROUP_TIME, TP_UNIT_UNSET, TP_UNIT_UNSET))
        goto done;
    if (smallest == TP_UNIT_HOUR) {
        JS_ThrowRangeError(ctx, "smallestUnit must not be hour");
        goto done;
    }
    if (!JS_IsUndefined(tzv)) {
        if (tp_to_time_zone_identifier(ctx, tzv, &tz))
            goto done;
        has_tz = true;
    }
    p = tp_seconds_precision(smallest, digits);
    rounded = tp_round_to_increment_as_if_positive(o->ns, (tp_i128)tp_unit_ns[p.unit] * p.increment, mode);
    ret = tp_instant_string(ctx, rounded, has_tz ? &tz : NULL, p.precision);
done:
    JS_FreeValue(ctx, tzv);
    JS_FreeValue(ctx, opts);
    return ret;
}

static JSValue tp_instant_to_json(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_INSTANT);
    if (!o)
        return JS_EXCEPTION;
    return tp_instant_string(ctx, o->ns, NULL, TP_PRECISION_AUTO);
}

static JSValue tp_instant_to_zoned_date_time_iso(JSContext *ctx, JSValueConst this_val, int argc,
                                                 JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_INSTANT);
    TPTimeZone tz;
    if (!o)
        return JS_EXCEPTION;
    if (tp_to_time_zone_identifier(ctx, tp_arg(0), &tz))
        return JS_EXCEPTION;
    return tp_new_zoned(ctx, o->ns, tz, TP_CAL_ISO8601, JS_UNDEFINED);
}

static const JSCFunctionListEntry tp_instant_proto_funcs[] = {
    JS_CGETSET_DEF("epochMilliseconds", tp_instant_get_epoch_milliseconds, NULL),
    JS_CGETSET_DEF("epochNanoseconds", tp_instant_get_epoch_nanoseconds, NULL),
    JS_CFUNC_MAGIC_DEF("add", 1, tp_instant_add, 0),
    JS_CFUNC_MAGIC_DEF("subtract", 1, tp_instant_add, 1),
    JS_CFUNC_MAGIC_DEF("until", 1, tp_instant_difference, 0),
    JS_CFUNC_MAGIC_DEF("since", 1, tp_instant_difference, 1),
    JS_CFUNC_DEF("round", 1, tp_instant_round),
    JS_CFUNC_DEF("equals", 1, tp_instant_equals),
    JS_CFUNC_DEF("toString", 0, tp_instant_to_string),
    JS_CFUNC_DEF("toLocaleString", 0, tp_instant_to_json),
    JS_CFUNC_DEF("toJSON", 0, tp_instant_to_json),
    JS_CFUNC_DEF("valueOf", 0, tp_value_of_throws),
    JS_CFUNC_DEF("toZonedDateTimeISO", 1, tp_instant_to_zoned_date_time_iso),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Temporal.Instant", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry tp_instant_static_funcs[] = {
    JS_CFUNC_DEF("from", 1, tp_instant_from),
    JS_CFUNC_DEF("fromEpochMilliseconds", 1, tp_instant_from_epoch_milliseconds),
    JS_CFUNC_DEF("fromEpochNanoseconds", 1, tp_instant_from_epoch_nanoseconds),
    JS_CFUNC_DEF("compare", 2, tp_instant_compare),
};

int tp_init_instant(JSContext *ctx, JSValue ns)
{
    return tp_define_class(ctx, ns, TP_KIND_INSTANT, "Instant", tp_instant_constructor, 1, tp_instant_proto_funcs,
                           countof(tp_instant_proto_funcs), tp_instant_static_funcs,
                           countof(tp_instant_static_funcs));
}

static int tp_now_time_zone(JSContext *ctx, int argc, JSValueConst *argv, TPTimeZone *tz)
{
    if (argc < 1 || JS_IsUndefined(tp_arg(0))) {
        *tz = tp_system_time_zone();
        return 0;
    }
    return tp_to_time_zone_identifier(ctx, tp_arg(0), tz);
}

static int tp_now_datetime(JSContext *ctx, int argc, JSValueConst *argv, TPDateTime *dt)
{
    TPTimeZone tz;
    if (tp_now_time_zone(ctx, argc, argv, &tz))
        return -1;
    tp_iso_datetime_for(tz, tp_system_epoch_ns(), dt);
    return 0;
}

static JSValue tp_now_instant(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    return tp_new_instant(ctx, tp_system_epoch_ns(), JS_UNDEFINED);
}

static JSValue tp_now_time_zone_id(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    return tp_time_zone_to_string(ctx, tp_system_time_zone());
}

static JSValue tp_now_zoned_date_time_iso(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPTimeZone tz;
    if (tp_now_time_zone(ctx, argc, argv, &tz))
        return JS_EXCEPTION;
    return tp_new_zoned(ctx, tp_system_epoch_ns(), tz, TP_CAL_ISO8601, JS_UNDEFINED);
}

static JSValue tp_now_plain_date_time_iso(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPDateTime dt;
    if (tp_now_datetime(ctx, argc, argv, &dt))
        return JS_EXCEPTION;
    return tp_new_plain_datetime(ctx, &dt, TP_CAL_ISO8601, JS_UNDEFINED);
}

static JSValue tp_now_plain_date_iso(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPDateTime dt;
    if (tp_now_datetime(ctx, argc, argv, &dt))
        return JS_EXCEPTION;
    return tp_new_plain_date(ctx, dt.date, TP_CAL_ISO8601, JS_UNDEFINED);
}

static JSValue tp_now_plain_time_iso(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPDateTime dt;
    if (tp_now_datetime(ctx, argc, argv, &dt))
        return JS_EXCEPTION;
    return tp_new_plain_time(ctx, &dt.time, JS_UNDEFINED);
}

static const JSCFunctionListEntry tp_now_funcs[] = {
    JS_CFUNC_DEF("instant", 0, tp_now_instant),
    JS_CFUNC_DEF("timeZoneId", 0, tp_now_time_zone_id),
    JS_CFUNC_DEF("zonedDateTimeISO", 0, tp_now_zoned_date_time_iso),
    JS_CFUNC_DEF("plainDateTimeISO", 0, tp_now_plain_date_time_iso),
    JS_CFUNC_DEF("plainDateISO", 0, tp_now_plain_date_iso),
    JS_CFUNC_DEF("plainTimeISO", 0, tp_now_plain_time_iso),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Temporal.Now", JS_PROP_CONFIGURABLE),
};

int tp_init_now(JSContext *ctx, JSValue ns)
{
    JSValue now = JS_NewObject(ctx);
    if (JS_IsException(now))
        return -1;
    if (JS_SetPropertyFunctionList(ctx, now, tp_now_funcs, countof(tp_now_funcs))) {
        JS_FreeValue(ctx, now);
        return -1;
    }
    return JS_DefinePropertyValueStr(ctx, ns, "Now", now, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE) < 0 ? -1 : 0;
}
