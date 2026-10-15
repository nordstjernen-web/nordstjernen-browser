/* quickjs-temporal-zoned.c — Temporal.ZonedDateTime. */
#include <math.h>
#include <string.h>
#include "cutils.h"
#include "quickjs-temporal.h"

JSValue tp_new_zoned(JSContext *ctx, tp_i128 ns, TPTimeZone tz, TPCalendar cal, JSValueConst new_target)
{
    TPObject *o;
    JSValue v;

    if (!tp_epoch_ns_valid(ns))
        return JS_ThrowRangeError(ctx, "instant is outside the supported range");
    v = tp_alloc(ctx, new_target, TP_KIND_ZONED, &o);
    if (JS_IsException(v))
        return v;
    o->ns = ns;
    o->tz = tz;
    o->cal = cal;
    return v;
}

static int tp_read_zoned_options(JSContext *ctx, JSValueConst options, int *disambiguation, int *offset,
                                 int *overflow)
{
    JSValue opts;
    int r;
    if (tp_get_options_object(ctx, options, &opts))
        return -1;
    r = tp_get_disambiguation_option(ctx, opts, disambiguation) ||
        tp_get_offset_option(ctx, opts, TP_OFFSET_REJECT, offset) || tp_get_overflow_option(ctx, opts, overflow);
    JS_FreeValue(ctx, opts);
    return r ? -1 : 0;
}

static int tp_zoned_from_fields(JSContext *ctx, JSValueConst item, JSValueConst options, tp_i128 *ns,
                                TPTimeZone *tz, TPCalendar *cal)
{
    TPFields fields;
    TPDateTime dt;
    int disambiguation, offset_option, overflow;
    bool has_offset;

    if (tp_get_calendar_with_iso_default(ctx, item, cal))
        return -1;
    if (tp_prepare_calendar_fields(ctx, *cal, item, TP_FM_DATE,
                                   TP_FM_TIME | TP_FB(TP_F_OFFSET) | TP_FB(TP_F_TIME_ZONE), TP_FB(TP_F_TIME_ZONE),
                                   false, &fields))
        return -1;
    *tz = fields.tz;
    has_offset = (fields.has & TP_FB(TP_F_OFFSET)) != 0;
    if (tp_read_zoned_options(ctx, options, &disambiguation, &offset_option, &overflow))
        return -1;
    if (tp_interpret_datetime_fields(ctx, *cal, &fields, overflow, &dt))
        return -1;
    return tp_interpret_iso_datetime_offset(ctx, dt.date, &dt.time,
                                            has_offset ? TP_OFFSET_BEHAVIOUR_OPTION : TP_OFFSET_BEHAVIOUR_WALL,
                                            has_offset ? fields.offset_ns : 0, *tz, disambiguation, offset_option,
                                            false, ns);
}

static int tp_zoned_from_string(JSContext *ctx, const char *s, size_t len, JSValueConst options, tp_i128 *ns,
                                TPTimeZone *tz, TPCalendar *cal)
{
    static const int goals[] = { TP_GOAL_DATETIME_ZONED, TP_GOAL_END };
    TPParsed p;
    TPISODate d;
    int disambiguation, offset_option, overflow, behaviour;
    bool match_minutes;

    if (tp_parse_iso_datetime(ctx, s, len, goals, &p))
        return -1;
    if (tp_time_zone_from_identifier(ctx, s + p.tz_start, p.tz_len, tz))
        return -1;
    if (p.cal_len) {
        if (tp_canonicalize_calendar(ctx, s + p.cal_start, p.cal_len, cal))
            return -1;
    } else {
        *cal = TP_CAL_ISO8601;
    }
    match_minutes = !(p.has_offset && p.offset_sub_minute);
    if (tp_read_zoned_options(ctx, options, &disambiguation, &offset_option, &overflow))
        return -1;
    d.year = p.year;
    d.month = p.month;
    d.day = p.day;
    if (p.z)
        behaviour = TP_OFFSET_BEHAVIOUR_EXACT;
    else if (!p.has_offset)
        behaviour = TP_OFFSET_BEHAVIOUR_WALL;
    else
        behaviour = TP_OFFSET_BEHAVIOUR_OPTION;
    return tp_interpret_iso_datetime_offset(ctx, d, p.has_time ? &p.time : NULL, behaviour,
                                            behaviour == TP_OFFSET_BEHAVIOUR_OPTION ? p.offset_ns : 0, *tz,
                                            disambiguation, offset_option, match_minutes, ns);
}

int tp_to_temporal_zoned(JSContext *ctx, JSValueConst v, JSValueConst options, tp_i128 *ns, TPTimeZone *tz,
                         TPCalendar *cal)
{
    TPObject *o;
    const char *s;
    size_t len;
    int r, a, b, c;

    if (JS_IsObject(v)) {
        if ((o = tp_get(v, TP_KIND_ZONED))) {
            if (tp_read_zoned_options(ctx, options, &a, &b, &c))
                return -1;
            *ns = o->ns;
            *tz = o->tz;
            *cal = o->cal;
            return 0;
        }
        return tp_zoned_from_fields(ctx, v, options, ns, tz, cal);
    }
    if (tp_require_string(ctx, v))
        return -1;
    s = JS_ToCStringLen(ctx, &len, v);
    if (!s)
        return -1;
    r = tp_zoned_from_string(ctx, s, len, options, ns, tz, cal);
    JS_FreeCString(ctx, s);
    return r;
}

static JSValue tp_zoned_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    JSValue b;
    tp_i128 ns;
    TPTimeZone tz;
    TPCalendar cal = TP_CAL_ISO8601;
    const char *s;
    size_t len;
    int r;

    b = js_temporal_to_bigint(ctx, tp_arg(0));
    if (JS_IsException(b))
        return JS_EXCEPTION;
    r = js_temporal_bigint_to_i128(ctx, &ns, b);
    JS_FreeValue(ctx, b);
    if (r || !tp_epoch_ns_valid(ns))
        return JS_ThrowRangeError(ctx, "epoch nanoseconds are outside the supported range");
    if (tp_require_string(ctx, tp_arg(1)))
        return JS_EXCEPTION;
    s = JS_ToCStringLen(ctx, &len, tp_arg(1));
    if (!s)
        return JS_EXCEPTION;
    r = tp_time_zone_from_identifier(ctx, s, len, &tz);
    JS_FreeCString(ctx, s);
    if (r)
        return JS_EXCEPTION;
    if (argc > 2 && !JS_IsUndefined(tp_arg(2))) {
        if (tp_require_string(ctx, tp_arg(2)))
            return JS_EXCEPTION;
        s = JS_ToCStringLen(ctx, &len, tp_arg(2));
        if (!s)
            return JS_EXCEPTION;
        r = tp_canonicalize_calendar(ctx, s, len, &cal);
        JS_FreeCString(ctx, s);
        if (r)
            return JS_EXCEPTION;
    }
    return tp_new_zoned(ctx, ns, tz, cal, new_target);
}

static JSValue tp_zoned_from(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    tp_i128 ns;
    TPTimeZone tz;
    TPCalendar cal;
    if (tp_to_temporal_zoned(ctx, tp_arg(0), tp_arg(1), &ns, &tz, &cal))
        return JS_EXCEPTION;
    return tp_new_zoned(ctx, ns, tz, cal, JS_UNDEFINED);
}

static JSValue tp_zoned_compare(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    tp_i128 a, b;
    TPTimeZone tz;
    TPCalendar cal;
    if (tp_to_temporal_zoned(ctx, tp_arg(0), JS_UNDEFINED, &a, &tz, &cal) ||
        tp_to_temporal_zoned(ctx, tp_arg(1), JS_UNDEFINED, &b, &tz, &cal))
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, tp_cmp128(a, b));
}

static TPObject *tp_zoned_this(JSContext *ctx, JSValueConst this_val, TPDateTime *dt)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_ZONED);
    if (o && dt)
        tp_iso_datetime_for(o->tz, o->ns, dt);
    return o;
}

static JSValue tp_zoned_get_field(JSContext *ctx, JSValueConst this_val, int magic)
{
    TPDateTime dt;
    TPObject *o = tp_zoned_this(ctx, this_val, &dt);
    if (!o)
        return JS_EXCEPTION;
    if (magic >= TP_DF_HOUR)
        return tp_time_field_value(ctx, &dt.time, magic);
    return tp_date_field_value(ctx, o->cal, dt.date, magic);
}

static JSValue tp_zoned_get_time_zone_id(JSContext *ctx, JSValueConst this_val)
{
    TPObject *o = tp_zoned_this(ctx, this_val, NULL);
    if (!o)
        return JS_EXCEPTION;
    return tp_time_zone_to_string(ctx, o->tz);
}

static JSValue tp_zoned_get_epoch_milliseconds(JSContext *ctx, JSValueConst this_val)
{
    TPObject *o = tp_zoned_this(ctx, this_val, NULL);
    if (!o)
        return JS_EXCEPTION;
    return JS_NewFloat64(ctx, (double)(int64_t)tp_floor_div128(o->ns, 1000000));
}

static JSValue tp_zoned_get_epoch_nanoseconds(JSContext *ctx, JSValueConst this_val)
{
    TPObject *o = tp_zoned_this(ctx, this_val, NULL);
    if (!o)
        return JS_EXCEPTION;
    return js_temporal_new_bigint128(ctx, o->ns);
}

static JSValue tp_zoned_get_hours_in_day(JSContext *ctx, JSValueConst this_val)
{
    TPDateTime dt;
    TPObject *o = tp_zoned_this(ctx, this_val, &dt);
    tp_i128 today, tomorrow;
    if (!o)
        return JS_EXCEPTION;
    if (tp_start_of_day(ctx, o->tz, dt.date, &today) ||
        tp_start_of_day(ctx, o->tz, tp_add_days(dt.date, 1), &tomorrow))
        return JS_EXCEPTION;
    return JS_NewFloat64(ctx, tp_total_time_duration(tomorrow - today, TP_UNIT_HOUR));
}

static JSValue tp_zoned_get_offset_nanoseconds(JSContext *ctx, JSValueConst this_val)
{
    TPObject *o = tp_zoned_this(ctx, this_val, NULL);
    if (!o)
        return JS_EXCEPTION;
    return JS_NewFloat64(ctx, (double)tp_offset_ns_for(o->tz, o->ns));
}

static JSValue tp_zoned_get_offset(JSContext *ctx, JSValueConst this_val)
{
    TPObject *o = tp_zoned_this(ctx, this_val, NULL);
    TPBuf b;
    if (!o)
        return JS_EXCEPTION;
    tp_buf_init(&b);
    tp_format_offset_ns(&b, tp_offset_ns_for(o->tz, o->ns));
    return tp_buf_to_string(ctx, &b);
}

static JSValue tp_zoned_with(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPDateTime dt, result;
    TPObject *o = tp_zoned_this(ctx, this_val, &dt);
    TPFields fields, partial;
    JSValue opts;
    int disambiguation, offset_option, overflow, r;
    tp_i128 ns;

    if (!o)
        return JS_EXCEPTION;
    if (tp_reject_temporal_like_object(ctx, tp_arg(0)))
        return JS_EXCEPTION;
    tp_iso_date_to_fields(o->cal, dt.date, TP_FIELDS_DATE, &fields);
    tp_fields_set_time(&fields, &dt.time);
    fields.has |= TP_FB(TP_F_OFFSET);
    fields.offset_ns = tp_offset_ns_for(o->tz, o->ns);
    if (tp_prepare_calendar_fields(ctx, o->cal, tp_arg(0), TP_FM_DATE, TP_FM_TIME | TP_FB(TP_F_OFFSET), 0, true,
                                   &partial))
        return JS_EXCEPTION;
    tp_calendar_merge_fields(o->cal, &fields, &partial, &fields);
    if (tp_get_options_object(ctx, tp_arg(1), &opts))
        return JS_EXCEPTION;
    r = tp_get_disambiguation_option(ctx, opts, &disambiguation) ||
        tp_get_offset_option(ctx, opts, TP_OFFSET_PREFER, &offset_option) ||
        tp_get_overflow_option(ctx, opts, &overflow);
    JS_FreeValue(ctx, opts);
    if (r || tp_interpret_datetime_fields(ctx, o->cal, &fields, overflow, &result))
        return JS_EXCEPTION;
    if (tp_interpret_iso_datetime_offset(ctx, result.date, &result.time, TP_OFFSET_BEHAVIOUR_OPTION,
                                         fields.offset_ns, o->tz, disambiguation, offset_option, false, &ns))
        return JS_EXCEPTION;
    return tp_new_zoned(ctx, ns, o->tz, o->cal, JS_UNDEFINED);
}

static JSValue tp_zoned_with_plain_time(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPDateTime dt;
    TPObject *o = tp_zoned_this(ctx, this_val, &dt);
    tp_i128 ns;

    if (!o)
        return JS_EXCEPTION;
    if (argc < 1 || JS_IsUndefined(tp_arg(0))) {
        if (tp_start_of_day(ctx, o->tz, dt.date, &ns))
            return JS_EXCEPTION;
    } else {
        if (tp_to_temporal_time(ctx, tp_arg(0), JS_UNDEFINED, &dt.time) ||
            tp_epoch_ns_for(ctx, o->tz, &dt, TP_DISAMBIG_COMPATIBLE, &ns))
            return JS_EXCEPTION;
    }
    return tp_new_zoned(ctx, ns, o->tz, o->cal, JS_UNDEFINED);
}

static JSValue tp_zoned_with_time_zone(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_zoned_this(ctx, this_val, NULL);
    TPTimeZone tz;
    if (!o)
        return JS_EXCEPTION;
    if (tp_to_time_zone_identifier(ctx, tp_arg(0), &tz))
        return JS_EXCEPTION;
    return tp_new_zoned(ctx, o->ns, tz, o->cal, JS_UNDEFINED);
}

static JSValue tp_zoned_with_calendar(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_zoned_this(ctx, this_val, NULL);
    TPCalendar cal;
    if (!o)
        return JS_EXCEPTION;
    if (tp_to_calendar_identifier(ctx, tp_arg(0), &cal))
        return JS_EXCEPTION;
    return tp_new_zoned(ctx, o->ns, o->tz, cal, JS_UNDEFINED);
}

static JSValue tp_zoned_add(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
    TPObject *o = tp_zoned_this(ctx, this_val, NULL);
    TPDuration d;
    TPInternalDuration id;
    JSValue opts;
    int overflow, r;
    tp_i128 ns;

    if (!o)
        return JS_EXCEPTION;
    if (tp_to_temporal_duration(ctx, tp_arg(0), &d))
        return JS_EXCEPTION;
    if (magic)
        tp_negate_duration(&d);
    if (tp_get_options_object(ctx, tp_arg(1), &opts))
        return JS_EXCEPTION;
    r = tp_get_overflow_option(ctx, opts, &overflow);
    JS_FreeValue(ctx, opts);
    if (r)
        return JS_EXCEPTION;
    tp_to_internal_duration(&d, &id);
    if (tp_add_zoned_datetime(ctx, o->ns, o->tz, o->cal, &id, overflow, &ns))
        return JS_EXCEPTION;
    return tp_new_zoned(ctx, ns, o->tz, o->cal, JS_UNDEFINED);
}

static JSValue tp_zoned_difference(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
    TPObject *o = tp_zoned_this(ctx, this_val, NULL);
    tp_i128 other_ns;
    TPTimeZone other_tz;
    TPCalendar other_cal;
    JSValue opts, ret = JS_EXCEPTION;
    TPDiffSettings s;
    TPInternalDuration d;

    if (!o)
        return JS_EXCEPTION;
    if (tp_to_temporal_zoned(ctx, tp_arg(0), JS_UNDEFINED, &other_ns, &other_tz, &other_cal))
        return JS_EXCEPTION;
    if (!tp_calendar_equals(o->cal, other_cal))
        return JS_ThrowRangeError(ctx, "cannot compute the difference between different calendars");
    if (tp_get_options_object(ctx, tp_arg(1), &opts))
        return JS_EXCEPTION;
    if (tp_get_difference_settings(ctx, magic, opts, TP_GROUP_DATETIME, 0, TP_UNIT_NANOSECOND, TP_UNIT_HOUR, &s))
        goto done;
    if (tp_unit_category(s.largest_unit) == TP_GROUP_TIME) {
        if (!tp_difference_instant(ctx, o->ns, other_ns, s.rounding_increment, s.smallest_unit, s.rounding_mode,
                                   &d))
            ret = tp_duration_result(ctx, &d, s.largest_unit, magic);
        goto done;
    }
    if (!tp_time_zone_equals(o->tz, other_tz)) {
        JS_ThrowRangeError(ctx, "cannot compute date differences across different time zones");
        goto done;
    }
    if (o->ns == other_ns) {
        memset(&d, 0, sizeof(d));
        ret = tp_duration_result(ctx, &d, TP_UNIT_HOUR, false);
        goto done;
    }
    if (!tp_difference_zoned_datetime_with_rounding(ctx, o->ns, other_ns, o->tz, o->cal, s.largest_unit,
                                                    s.rounding_increment, s.smallest_unit, s.rounding_mode, &d))
        ret = tp_duration_result(ctx, &d, TP_UNIT_HOUR, magic);
done:
    JS_FreeValue(ctx, opts);
    return ret;
}

static JSValue tp_zoned_round(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPDateTime dt;
    TPObject *o = tp_zoned_this(ctx, this_val, &dt);
    JSValue opts, ret = JS_EXCEPTION;
    int64_t increment;
    int mode, smallest, maximum;
    tp_i128 ns;

    if (!o)
        return JS_EXCEPTION;
    if (tp_round_options_from_arg(ctx, tp_arg(0), "smallestUnit", &opts))
        return JS_EXCEPTION;
    if (tp_get_rounding_increment_option(ctx, opts, &increment) ||
        tp_get_rounding_mode_option(ctx, opts, TP_ROUND_HALF_EXPAND, &mode) ||
        tp_get_unit_option(ctx, opts, "smallestUnit", true, &smallest) ||
        tp_validate_unit(ctx, smallest, TP_GROUP_TIME, TP_UNIT_DAY, TP_UNIT_UNSET))
        goto done;
    maximum = smallest == TP_UNIT_DAY ? 1 : tp_maximum_rounding_increment(smallest);
    if (tp_validate_rounding_increment(ctx, increment, maximum, smallest == TP_UNIT_DAY))
        goto done;
    if (smallest == TP_UNIT_NANOSECOND && increment == 1) {
        ret = tp_new_zoned(ctx, o->ns, o->tz, o->cal, JS_UNDEFINED);
        goto done;
    }
    if (smallest == TP_UNIT_DAY) {
        tp_i128 start, end;
        if (tp_start_of_day(ctx, o->tz, dt.date, &start) ||
            tp_start_of_day(ctx, o->tz, tp_add_days(dt.date, 1), &end))
            goto done;
        ns = tp_round_to_increment(o->ns - start, end - start, mode) + start;
    } else {
        TPDateTime rounded = tp_round_iso_datetime(&dt, increment, smallest, mode);
        if (tp_interpret_iso_datetime_offset(ctx, rounded.date, &rounded.time, TP_OFFSET_BEHAVIOUR_OPTION,
                                             tp_offset_ns_for(o->tz, o->ns), o->tz, TP_DISAMBIG_COMPATIBLE,
                                             TP_OFFSET_PREFER, false, &ns))
            goto done;
    }
    ret = tp_new_zoned(ctx, ns, o->tz, o->cal, JS_UNDEFINED);
done:
    JS_FreeValue(ctx, opts);
    return ret;
}

static JSValue tp_zoned_equals(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_zoned_this(ctx, this_val, NULL);
    tp_i128 ns;
    TPTimeZone tz;
    TPCalendar cal;
    if (!o)
        return JS_EXCEPTION;
    if (tp_to_temporal_zoned(ctx, tp_arg(0), JS_UNDEFINED, &ns, &tz, &cal))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, o->ns == ns && tp_time_zone_equals(o->tz, tz) && tp_calendar_equals(o->cal, cal));
}

static JSValue tp_zoned_string(JSContext *ctx, const TPObject *o, int precision, int show_cal, int show_tz,
                               int show_offset, int64_t increment, int unit, int mode)
{
    tp_i128 ns = tp_round_to_increment_as_if_positive(o->ns, (tp_i128)tp_unit_ns[unit] * increment, mode);
    TPDateTime dt;
    TPBuf b;

    tp_iso_datetime_for(o->tz, ns, &dt);
    tp_buf_init(&b);
    tp_put_iso_datetime(&b, &dt, TP_CAL_ISO8601, precision, TP_SHOW_CAL_NEVER);
    if (show_offset != TP_SHOW_OFFSET_NEVER)
        tp_format_offset_rounded(&b, tp_offset_ns_for(o->tz, ns));
    if (show_tz != TP_SHOW_TZ_NEVER) {
        tp_buf_putc(&b, '[');
        if (show_tz == TP_SHOW_TZ_CRITICAL)
            tp_buf_putc(&b, '!');
        tp_time_zone_put(&b, o->tz);
        tp_buf_putc(&b, ']');
    }
    tp_put_calendar_annotation(&b, o->cal, show_cal);
    return tp_buf_to_string(ctx, &b);
}

static JSValue tp_zoned_to_string(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_zoned_this(ctx, this_val, NULL);
    JSValue opts, ret = JS_EXCEPTION;
    int show_cal, digits, show_offset, mode, smallest, show_tz;
    TPPrecision p;

    if (!o)
        return JS_EXCEPTION;
    if (tp_get_options_object(ctx, tp_arg(0), &opts))
        return JS_EXCEPTION;
    if (tp_get_show_calendar_option(ctx, opts, &show_cal) ||
        tp_get_fractional_second_digits_option(ctx, opts, &digits) ||
        tp_get_show_offset_option(ctx, opts, &show_offset) ||
        tp_get_rounding_mode_option(ctx, opts, TP_ROUND_TRUNC, &mode) ||
        tp_get_unit_option(ctx, opts, "smallestUnit", false, &smallest) ||
        tp_get_show_time_zone_option(ctx, opts, &show_tz) ||
        tp_validate_unit(ctx, smallest, TP_GROUP_TIME, TP_UNIT_UNSET, TP_UNIT_UNSET))
        goto done;
    if (smallest == TP_UNIT_HOUR) {
        JS_ThrowRangeError(ctx, "smallestUnit must not be hour");
        goto done;
    }
    p = tp_seconds_precision(smallest, digits);
    ret = tp_zoned_string(ctx, o, p.precision, show_cal, show_tz, show_offset, p.increment, p.unit, mode);
done:
    JS_FreeValue(ctx, opts);
    return ret;
}

static JSValue tp_zoned_to_json(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_zoned_this(ctx, this_val, NULL);
    if (!o)
        return JS_EXCEPTION;
    return tp_zoned_string(ctx, o, TP_PRECISION_AUTO, TP_SHOW_CAL_AUTO, TP_SHOW_TZ_AUTO, TP_SHOW_OFFSET_AUTO, 1,
                           TP_UNIT_NANOSECOND, TP_ROUND_TRUNC);
}

static JSValue tp_zoned_start_of_day(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPDateTime dt;
    TPObject *o = tp_zoned_this(ctx, this_val, &dt);
    tp_i128 ns;
    if (!o)
        return JS_EXCEPTION;
    if (tp_start_of_day(ctx, o->tz, dt.date, &ns))
        return JS_EXCEPTION;
    return tp_new_zoned(ctx, ns, o->tz, o->cal, JS_UNDEFINED);
}

static JSValue tp_zoned_get_time_zone_transition(JSContext *ctx, JSValueConst this_val, int argc,
                                                 JSValueConst *argv)
{
    TPObject *o = tp_zoned_this(ctx, this_val, NULL);
    JSValue opts;
    int direction, r;
    tp_i128 ns;
    bool found;

    if (!o)
        return JS_EXCEPTION;
    if (tp_round_options_from_arg(ctx, tp_arg(0), "direction", &opts))
        return JS_EXCEPTION;
    r = tp_get_direction_option(ctx, opts, &direction);
    JS_FreeValue(ctx, opts);
    if (r)
        return JS_EXCEPTION;
    if (o->tz.id == TP_TZ_OFFSET_ID)
        return JS_NULL;
    found = direction == 0 ? tp_next_transition(o->tz, o->ns, &ns) : tp_previous_transition(o->tz, o->ns, &ns);
    if (!found)
        return JS_NULL;
    return tp_new_zoned(ctx, ns, o->tz, o->cal, JS_UNDEFINED);
}

static JSValue tp_zoned_to_instant(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_zoned_this(ctx, this_val, NULL);
    if (!o)
        return JS_EXCEPTION;
    return tp_new_instant(ctx, o->ns, JS_UNDEFINED);
}

static JSValue tp_zoned_to_plain(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
    TPDateTime dt;
    TPObject *o = tp_zoned_this(ctx, this_val, &dt);
    if (!o)
        return JS_EXCEPTION;
    if (magic == TP_KIND_DATE)
        return tp_new_plain_date(ctx, dt.date, o->cal, JS_UNDEFINED);
    if (magic == TP_KIND_TIME)
        return tp_new_plain_time(ctx, &dt.time, JS_UNDEFINED);
    return tp_new_plain_datetime(ctx, &dt, o->cal, JS_UNDEFINED);
}

#define TP_ZONED_FIELD(name, field) JS_CGETSET_MAGIC_DEF(name, tp_zoned_get_field, NULL, field)

static const JSCFunctionListEntry tp_zoned_proto_funcs[] = {
    TP_ZONED_FIELD("calendarId", TP_DF_CALENDAR_ID),
    JS_CGETSET_DEF("timeZoneId", tp_zoned_get_time_zone_id, NULL),
    TP_ZONED_FIELD("era", TP_DF_ERA),
    TP_ZONED_FIELD("eraYear", TP_DF_ERA_YEAR),
    TP_ZONED_FIELD("year", TP_DF_YEAR),
    TP_ZONED_FIELD("month", TP_DF_MONTH),
    TP_ZONED_FIELD("monthCode", TP_DF_MONTH_CODE),
    TP_ZONED_FIELD("day", TP_DF_DAY),
    TP_ZONED_FIELD("hour", TP_DF_HOUR),
    TP_ZONED_FIELD("minute", TP_DF_MINUTE),
    TP_ZONED_FIELD("second", TP_DF_SECOND),
    TP_ZONED_FIELD("millisecond", TP_DF_MILLISECOND),
    TP_ZONED_FIELD("microsecond", TP_DF_MICROSECOND),
    TP_ZONED_FIELD("nanosecond", TP_DF_NANOSECOND),
    JS_CGETSET_DEF("epochMilliseconds", tp_zoned_get_epoch_milliseconds, NULL),
    JS_CGETSET_DEF("epochNanoseconds", tp_zoned_get_epoch_nanoseconds, NULL),
    TP_ZONED_FIELD("dayOfWeek", TP_DF_DAY_OF_WEEK),
    TP_ZONED_FIELD("dayOfYear", TP_DF_DAY_OF_YEAR),
    TP_ZONED_FIELD("weekOfYear", TP_DF_WEEK_OF_YEAR),
    TP_ZONED_FIELD("yearOfWeek", TP_DF_YEAR_OF_WEEK),
    JS_CGETSET_DEF("hoursInDay", tp_zoned_get_hours_in_day, NULL),
    TP_ZONED_FIELD("daysInWeek", TP_DF_DAYS_IN_WEEK),
    TP_ZONED_FIELD("daysInMonth", TP_DF_DAYS_IN_MONTH),
    TP_ZONED_FIELD("daysInYear", TP_DF_DAYS_IN_YEAR),
    TP_ZONED_FIELD("monthsInYear", TP_DF_MONTHS_IN_YEAR),
    TP_ZONED_FIELD("inLeapYear", TP_DF_IN_LEAP_YEAR),
    JS_CGETSET_DEF("offsetNanoseconds", tp_zoned_get_offset_nanoseconds, NULL),
    JS_CGETSET_DEF("offset", tp_zoned_get_offset, NULL),
    JS_CFUNC_DEF("with", 1, tp_zoned_with),
    JS_CFUNC_DEF("withPlainTime", 0, tp_zoned_with_plain_time),
    JS_CFUNC_DEF("withTimeZone", 1, tp_zoned_with_time_zone),
    JS_CFUNC_DEF("withCalendar", 1, tp_zoned_with_calendar),
    JS_CFUNC_MAGIC_DEF("add", 1, tp_zoned_add, 0),
    JS_CFUNC_MAGIC_DEF("subtract", 1, tp_zoned_add, 1),
    JS_CFUNC_MAGIC_DEF("until", 1, tp_zoned_difference, 0),
    JS_CFUNC_MAGIC_DEF("since", 1, tp_zoned_difference, 1),
    JS_CFUNC_DEF("round", 1, tp_zoned_round),
    JS_CFUNC_DEF("equals", 1, tp_zoned_equals),
    JS_CFUNC_DEF("toString", 0, tp_zoned_to_string),
    JS_CFUNC_DEF("toLocaleString", 0, tp_zoned_to_json),
    JS_CFUNC_DEF("toJSON", 0, tp_zoned_to_json),
    JS_CFUNC_DEF("valueOf", 0, tp_value_of_throws),
    JS_CFUNC_DEF("startOfDay", 0, tp_zoned_start_of_day),
    JS_CFUNC_DEF("getTimeZoneTransition", 1, tp_zoned_get_time_zone_transition),
    JS_CFUNC_DEF("toInstant", 0, tp_zoned_to_instant),
    JS_CFUNC_MAGIC_DEF("toPlainDate", 0, tp_zoned_to_plain, TP_KIND_DATE),
    JS_CFUNC_MAGIC_DEF("toPlainTime", 0, tp_zoned_to_plain, TP_KIND_TIME),
    JS_CFUNC_MAGIC_DEF("toPlainDateTime", 0, tp_zoned_to_plain, TP_KIND_DATETIME),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Temporal.ZonedDateTime", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry tp_zoned_static_funcs[] = {
    JS_CFUNC_DEF("from", 1, tp_zoned_from),
    JS_CFUNC_DEF("compare", 2, tp_zoned_compare),
};

int tp_init_zoned(JSContext *ctx, JSValue ns)
{
    return tp_define_class(ctx, ns, TP_KIND_ZONED, "ZonedDateTime", tp_zoned_constructor, 2, tp_zoned_proto_funcs,
                           countof(tp_zoned_proto_funcs), tp_zoned_static_funcs, countof(tp_zoned_static_funcs));
}
