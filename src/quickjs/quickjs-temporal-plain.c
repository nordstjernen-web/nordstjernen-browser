/* quickjs-temporal-plain.c — Temporal.PlainDate, Temporal.PlainDateTime, Temporal.PlainTime and ISO formatting. */
#include <math.h>
#include <string.h>
#include "cutils.h"
#include "quickjs-temporal.h"

void tp_put_iso_year(TPBuf *b, int64_t year)
{
    if (year >= 0 && year <= 9999) {
        tp_buf_pad(b, year, 4);
        return;
    }
    tp_buf_putc(b, year < 0 ? '-' : '+');
    tp_buf_pad(b, year < 0 ? -year : year, 6);
}

void tp_put_iso_date(TPBuf *b, TPISODate d)
{
    tp_put_iso_year(b, d.year);
    tp_buf_putc(b, '-');
    tp_buf_pad(b, d.month, 2);
    tp_buf_putc(b, '-');
    tp_buf_pad(b, d.day, 2);
}

void tp_put_fraction(TPBuf *b, int64_t subsec, int precision)
{
    char digits[16];
    int len = 9;

    if (precision == 0 || (precision == TP_PRECISION_AUTO && subsec == 0))
        return;
    snprintf(digits, sizeof(digits), "%09lld", (long long)subsec);
    if (precision == TP_PRECISION_AUTO) {
        while (len > 0 && digits[len - 1] == '0')
            len--;
    } else {
        len = precision;
    }
    tp_buf_putc(b, '.');
    tp_buf_putn(b, digits, (size_t)len);
}

void tp_put_time(TPBuf *b, int h, int m, int s, int64_t subsec, int precision)
{
    tp_buf_pad(b, h, 2);
    tp_buf_putc(b, ':');
    tp_buf_pad(b, m, 2);
    if (precision == TP_PRECISION_MINUTE)
        return;
    tp_buf_putc(b, ':');
    tp_buf_pad(b, s, 2);
    tp_put_fraction(b, subsec, precision);
}

void tp_put_calendar_annotation(TPBuf *b, TPCalendar cal, int show)
{
    if (show == TP_SHOW_CAL_NEVER || (show == TP_SHOW_CAL_AUTO && cal == TP_CAL_ISO8601))
        return;
    tp_buf_putc(b, '[');
    if (show == TP_SHOW_CAL_CRITICAL)
        tp_buf_putc(b, '!');
    tp_buf_puts(b, "u-ca=");
    tp_buf_puts(b, tp_calendar_id(cal));
    tp_buf_putc(b, ']');
}

static int64_t tp_subsecond(const TPTime *t)
{
    return (int64_t)t->millisecond * 1000000 + (int64_t)t->microsecond * 1000 + t->nanosecond;
}

void tp_put_iso_datetime(TPBuf *b, const TPDateTime *dt, TPCalendar cal, int precision, int show_cal)
{
    tp_put_iso_date(b, dt->date);
    tp_buf_putc(b, 'T');
    tp_put_time(b, dt->time.hour, dt->time.minute, dt->time.second, tp_subsecond(&dt->time), precision);
    tp_put_calendar_annotation(b, cal, show_cal);
}

JSValue tp_new_plain_date(JSContext *ctx, TPISODate d, TPCalendar cal, JSValueConst new_target)
{
    TPObject *o;
    JSValue v;

    if (!tp_iso_date_within_limits(d))
        return JS_ThrowRangeError(ctx, "date is outside the supported range");
    v = tp_alloc(ctx, new_target, TP_KIND_DATE, &o);
    if (JS_IsException(v))
        return v;
    o->dt.date = d;
    o->dt.time = tp_midnight();
    o->cal = cal;
    return v;
}

JSValue tp_new_plain_datetime(JSContext *ctx, const TPDateTime *dt, TPCalendar cal, JSValueConst new_target)
{
    TPObject *o;
    JSValue v;

    if (!tp_iso_datetime_within_limits(dt))
        return JS_ThrowRangeError(ctx, "date-time is outside the supported range");
    v = tp_alloc(ctx, new_target, TP_KIND_DATETIME, &o);
    if (JS_IsException(v))
        return v;
    o->dt = *dt;
    o->cal = cal;
    return v;
}

JSValue tp_new_plain_time(JSContext *ctx, const TPTime *t, JSValueConst new_target)
{
    TPObject *o;
    JSValue v = tp_alloc(ctx, new_target, TP_KIND_TIME, &o);
    if (JS_IsException(v))
        return v;
    o->dt.time = *t;
    return v;
}

static int tp_overflow_from_options(JSContext *ctx, JSValueConst options, int *overflow)
{
    JSValue opts;
    int r;
    if (tp_get_options_object(ctx, options, &opts))
        return -1;
    r = tp_get_overflow_option(ctx, opts, overflow);
    JS_FreeValue(ctx, opts);
    return r;
}

static int tp_calendar_argument(JSContext *ctx, int argc, JSValueConst *argv, int index, TPCalendar *cal)
{
    const char *s;
    size_t len;
    int r;

    if (index >= argc || JS_IsUndefined(tp_arg(index))) {
        *cal = TP_CAL_ISO8601;
        return 0;
    }
    if (tp_require_string(ctx, tp_arg(index)))
        return -1;
    s = JS_ToCStringLen(ctx, &len, tp_arg(index));
    if (!s)
        return -1;
    r = tp_canonicalize_calendar(ctx, s, len, cal);
    JS_FreeCString(ctx, s);
    return r;
}

static int tp_integer_argument(JSContext *ctx, int argc, JSValueConst *argv, int index, int64_t *out)
{
    double d = 0;
    if (index < argc && !JS_IsUndefined(tp_arg(index)) && tp_to_integer_with_truncation(ctx, tp_arg(index), &d))
        return -1;
    *out = tp_clamp_double(d);
    return 0;
}

static int tp_required_integer_argument(JSContext *ctx, int argc, JSValueConst *argv, int index, int64_t *out)
{
    double d;
    if (tp_to_integer_with_truncation(ctx, tp_arg(index), &d))
        return -1;
    *out = tp_clamp_double(d);
    return 0;
}

static int tp_parse_string_argument(JSContext *ctx, JSValueConst v, const int *goals, TPParsed *p,
                                    TPCalendar *cal)
{
    const char *s;
    size_t len;
    int r;

    if (tp_require_string(ctx, v))
        return -1;
    s = JS_ToCStringLen(ctx, &len, v);
    if (!s)
        return -1;
    r = tp_parse_iso_datetime(ctx, s, len, goals, p);
    if (!r) {
        if (p->cal_len)
            r = tp_canonicalize_calendar(ctx, s + p->cal_start, p->cal_len, cal);
        else
            *cal = TP_CAL_ISO8601;
    }
    JS_FreeCString(ctx, s);
    return r;
}

int tp_to_temporal_date(JSContext *ctx, JSValueConst v, JSValueConst options, TPISODate *d, TPCalendar *cal)
{
    static const int goals[] = { TP_GOAL_DATETIME, TP_GOAL_END };
    TPObject *o;
    TPFields fields;
    TPParsed p;
    int overflow;

    if (JS_IsObject(v)) {
        if ((o = tp_get(v, TP_KIND_DATE)) || (o = tp_get(v, TP_KIND_DATETIME))) {
            *d = o->dt.date;
            *cal = o->cal;
            return tp_overflow_from_options(ctx, options, &overflow);
        }
        if ((o = tp_get(v, TP_KIND_ZONED))) {
            TPDateTime dt;
            tp_iso_datetime_for(o->tz, o->ns, &dt);
            *d = dt.date;
            *cal = o->cal;
            return tp_overflow_from_options(ctx, options, &overflow);
        }
        if (tp_get_calendar_with_iso_default(ctx, v, cal) ||
            tp_prepare_calendar_fields(ctx, *cal, v, TP_FM_DATE, 0, 0, false, &fields) ||
            tp_overflow_from_options(ctx, options, &overflow))
            return -1;
        return tp_calendar_date_from_fields(ctx, *cal, &fields, overflow, d);
    }
    if (tp_parse_string_argument(ctx, v, goals, &p, cal) || tp_overflow_from_options(ctx, options, &overflow))
        return -1;
    d->year = p.year;
    d->month = p.month;
    d->day = p.day;
    if (!tp_iso_date_within_limits(*d)) {
        JS_ThrowRangeError(ctx, "date is outside the supported range");
        return -1;
    }
    return 0;
}

int tp_to_temporal_datetime(JSContext *ctx, JSValueConst v, JSValueConst options, TPDateTime *dt, TPCalendar *cal)
{
    static const int goals[] = { TP_GOAL_DATETIME, TP_GOAL_END };
    TPObject *o;
    TPFields fields;
    TPParsed p;
    int overflow;

    if (JS_IsObject(v)) {
        if ((o = tp_get(v, TP_KIND_DATETIME))) {
            *dt = o->dt;
            *cal = o->cal;
            return tp_overflow_from_options(ctx, options, &overflow);
        }
        if ((o = tp_get(v, TP_KIND_ZONED))) {
            tp_iso_datetime_for(o->tz, o->ns, dt);
            *cal = o->cal;
            return tp_overflow_from_options(ctx, options, &overflow);
        }
        if ((o = tp_get(v, TP_KIND_DATE))) {
            dt->date = o->dt.date;
            dt->time = tp_midnight();
            *cal = o->cal;
            if (tp_overflow_from_options(ctx, options, &overflow))
                return -1;
        } else {
            if (tp_get_calendar_with_iso_default(ctx, v, cal) ||
                tp_prepare_calendar_fields(ctx, *cal, v, TP_FM_DATE, TP_FM_TIME, 0, false, &fields) ||
                tp_overflow_from_options(ctx, options, &overflow) ||
                tp_interpret_datetime_fields(ctx, *cal, &fields, overflow, dt))
                return -1;
        }
    } else {
        if (tp_parse_string_argument(ctx, v, goals, &p, cal) || tp_overflow_from_options(ctx, options, &overflow))
            return -1;
        dt->date.year = p.year;
        dt->date.month = p.month;
        dt->date.day = p.day;
        dt->time = p.time;
    }
    if (!tp_iso_datetime_within_limits(dt)) {
        JS_ThrowRangeError(ctx, "date-time is outside the supported range");
        return -1;
    }
    return 0;
}

int tp_to_temporal_time_record(JSContext *ctx, JSValueConst v, bool partial, int64_t out[6], bool present[6])
{
    static const char *const names[6] = { "hour", "microsecond", "millisecond", "minute", "nanosecond", "second" };
    static const int index[6] = { 0, 4, 3, 1, 5, 2 };
    bool any = false;
    int i;

    for (i = 0; i < 6; i++) {
        JSValue value = JS_GetPropertyStr(ctx, v, names[i]);
        double d;
        int r;
        if (JS_IsException(value))
            return -1;
        present[index[i]] = !JS_IsUndefined(value);
        if (present[index[i]]) {
            any = true;
            r = tp_to_integer_with_truncation(ctx, value, &d);
            JS_FreeValue(ctx, value);
            if (r)
                return -1;
            out[index[i]] = tp_clamp_double(d);
        } else if (!partial) {
            out[index[i]] = 0;
        }
    }
    if (!any) {
        JS_ThrowTypeError(ctx, "time-like object must have at least one time property");
        return -1;
    }
    return 0;
}

int tp_to_temporal_time(JSContext *ctx, JSValueConst v, JSValueConst options, TPTime *out)
{
    static const int goals[] = { TP_GOAL_TIME, TP_GOAL_END };
    TPObject *o;
    int overflow;

    if (JS_IsObject(v)) {
        int64_t fields[6];
        bool present[6];
        if ((o = tp_get(v, TP_KIND_TIME)) || (o = tp_get(v, TP_KIND_DATETIME))) {
            *out = o->dt.time;
            return tp_overflow_from_options(ctx, options, &overflow);
        }
        if ((o = tp_get(v, TP_KIND_ZONED))) {
            TPDateTime dt;
            tp_iso_datetime_for(o->tz, o->ns, &dt);
            *out = dt.time;
            return tp_overflow_from_options(ctx, options, &overflow);
        }
        if (tp_to_temporal_time_record(ctx, v, false, fields, present) ||
            tp_overflow_from_options(ctx, options, &overflow))
            return -1;
        return tp_regulate_time(ctx, fields, overflow, out);
    } else {
        TPParsed p;
        const char *s;
        size_t len;
        int r;
        if (tp_require_string(ctx, v))
            return -1;
        s = JS_ToCStringLen(ctx, &len, v);
        if (!s)
            return -1;
        r = tp_parse_iso_datetime(ctx, s, len, goals, &p);
        JS_FreeCString(ctx, s);
        if (r)
            return -1;
        *out = p.time;
        return tp_overflow_from_options(ctx, options, &overflow);
    }
}

int tp_to_time_record_or_midnight(JSContext *ctx, JSValueConst v, TPTime *out)
{
    if (JS_IsUndefined(v)) {
        *out = tp_midnight();
        return 0;
    }
    return tp_to_temporal_time(ctx, v, JS_UNDEFINED, out);
}

static JSValue tp_plain_date_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    int64_t y, m, d;
    TPCalendar cal;
    TPISODate date;

    if (tp_required_integer_argument(ctx, argc, argv, 0, &y) || tp_required_integer_argument(ctx, argc, argv, 1, &m) ||
        tp_required_integer_argument(ctx, argc, argv, 2, &d) || tp_calendar_argument(ctx, argc, argv, 3, &cal))
        return JS_EXCEPTION;
    if (!tp_is_valid_iso_date(y, m, d))
        return JS_ThrowRangeError(ctx, "invalid date");
    date.year = y;
    date.month = (int32_t)m;
    date.day = (int32_t)d;
    return tp_new_plain_date(ctx, date, cal, new_target);
}

static JSValue tp_plain_date_from(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPISODate d;
    TPCalendar cal;
    if (tp_to_temporal_date(ctx, tp_arg(0), tp_arg(1), &d, &cal))
        return JS_EXCEPTION;
    return tp_new_plain_date(ctx, d, cal, JS_UNDEFINED);
}

static JSValue tp_plain_date_compare(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPISODate a, b;
    TPCalendar cal;
    if (tp_to_temporal_date(ctx, tp_arg(0), JS_UNDEFINED, &a, &cal) ||
        tp_to_temporal_date(ctx, tp_arg(1), JS_UNDEFINED, &b, &cal))
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, tp_compare_iso_date(a, b));
}

static JSValue tp_plain_date_get_field(JSContext *ctx, JSValueConst this_val, int magic)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATE);
    if (!o)
        return JS_EXCEPTION;
    return tp_date_field_value(ctx, o->cal, o->dt.date, magic);
}

static JSValue tp_plain_date_to_year_month(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATE);
    TPFields fields;
    TPISODate d;
    if (!o)
        return JS_EXCEPTION;
    tp_iso_date_to_fields(o->cal, o->dt.date, TP_FIELDS_DATE, &fields);
    if (tp_calendar_year_month_from_fields(ctx, o->cal, &fields, TP_OVERFLOW_CONSTRAIN, &d))
        return JS_EXCEPTION;
    return tp_new_year_month(ctx, d, o->cal, JS_UNDEFINED);
}

static JSValue tp_plain_date_to_month_day(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATE);
    TPFields fields;
    TPISODate d;
    if (!o)
        return JS_EXCEPTION;
    tp_iso_date_to_fields(o->cal, o->dt.date, TP_FIELDS_DATE, &fields);
    if (tp_calendar_month_day_from_fields(ctx, o->cal, &fields, TP_OVERFLOW_CONSTRAIN, &d))
        return JS_EXCEPTION;
    return tp_new_month_day(ctx, d, o->cal, JS_UNDEFINED);
}

static JSValue tp_plain_date_add(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATE);
    TPDuration d;
    TPDateDuration dd;
    TPISODate result;
    int overflow;

    if (!o)
        return JS_EXCEPTION;
    if (tp_to_temporal_duration(ctx, tp_arg(0), &d))
        return JS_EXCEPTION;
    if (magic)
        tp_negate_duration(&d);
    if (tp_to_date_duration_without_time(ctx, &d, &dd) || tp_overflow_from_options(ctx, tp_arg(1), &overflow) ||
        tp_calendar_date_add(ctx, o->cal, o->dt.date, &dd, overflow, &result))
        return JS_EXCEPTION;
    return tp_new_plain_date(ctx, result, o->cal, JS_UNDEFINED);
}

static JSValue tp_plain_date_with(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATE);
    TPFields fields, partial;
    TPISODate d;
    int overflow;

    if (!o)
        return JS_EXCEPTION;
    if (tp_reject_temporal_like_object(ctx, tp_arg(0)))
        return JS_EXCEPTION;
    tp_iso_date_to_fields(o->cal, o->dt.date, TP_FIELDS_DATE, &fields);
    if (tp_prepare_calendar_fields(ctx, o->cal, tp_arg(0), TP_FM_DATE, 0, 0, true, &partial))
        return JS_EXCEPTION;
    tp_calendar_merge_fields(o->cal, &fields, &partial, &fields);
    if (tp_overflow_from_options(ctx, tp_arg(1), &overflow) ||
        tp_calendar_date_from_fields(ctx, o->cal, &fields, overflow, &d))
        return JS_EXCEPTION;
    return tp_new_plain_date(ctx, d, o->cal, JS_UNDEFINED);
}

static JSValue tp_plain_date_with_calendar(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATE);
    TPCalendar cal;
    if (!o)
        return JS_EXCEPTION;
    if (tp_to_calendar_identifier(ctx, tp_arg(0), &cal))
        return JS_EXCEPTION;
    return tp_new_plain_date(ctx, o->dt.date, cal, JS_UNDEFINED);
}

static JSValue tp_plain_date_difference(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
                                        int magic)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATE);
    TPISODate other;
    TPCalendar other_cal;
    JSValue opts, ret = JS_EXCEPTION;
    TPDiffSettings s;
    TPInternalDuration d;

    if (!o)
        return JS_EXCEPTION;
    if (tp_to_temporal_date(ctx, tp_arg(0), JS_UNDEFINED, &other, &other_cal))
        return JS_EXCEPTION;
    if (!tp_calendar_equals(o->cal, other_cal))
        return JS_ThrowRangeError(ctx, "cannot compute the difference between different calendars");
    if (tp_get_options_object(ctx, tp_arg(1), &opts))
        return JS_EXCEPTION;
    if (tp_get_difference_settings(ctx, magic, opts, TP_GROUP_DATE, 0, TP_UNIT_DAY, TP_UNIT_DAY, &s))
        goto done;
    memset(&d, 0, sizeof(d));
    if (tp_compare_iso_date(o->dt.date, other) != 0) {
        tp_calendar_date_until(o->cal, o->dt.date, other, s.largest_unit, &d.date);
        if (s.smallest_unit != TP_UNIT_DAY || s.rounding_increment != 1) {
            TPDateTime a, b;
            a.date = o->dt.date;
            a.time = tp_midnight();
            b.date = other;
            b.time = tp_midnight();
            if (tp_round_relative_duration(ctx, &d, tp_utc_epoch_ns(&a), tp_utc_epoch_ns(&b), &a, NULL, o->cal,
                                           s.largest_unit, s.rounding_increment, s.smallest_unit, s.rounding_mode,
                                           &d))
                goto done;
        }
    }
    ret = tp_duration_result(ctx, &d, TP_UNIT_DAY, magic);
done:
    JS_FreeValue(ctx, opts);
    return ret;
}

static JSValue tp_plain_date_equals(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATE);
    TPISODate other;
    TPCalendar cal;
    if (!o)
        return JS_EXCEPTION;
    if (tp_to_temporal_date(ctx, tp_arg(0), JS_UNDEFINED, &other, &cal))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, tp_compare_iso_date(o->dt.date, other) == 0 && tp_calendar_equals(o->cal, cal));
}

static JSValue tp_plain_date_to_plain_datetime(JSContext *ctx, JSValueConst this_val, int argc,
                                               JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATE);
    TPDateTime dt;
    if (!o)
        return JS_EXCEPTION;
    if (tp_to_time_record_or_midnight(ctx, tp_arg(0), &dt.time))
        return JS_EXCEPTION;
    dt.date = o->dt.date;
    return tp_new_plain_datetime(ctx, &dt, o->cal, JS_UNDEFINED);
}

static JSValue tp_plain_date_to_zoned(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATE);
    JSValue tz_like, time_like = JS_UNDEFINED;
    TPTimeZone tz;
    TPDateTime dt;
    tp_i128 ns;
    int r;

    if (!o)
        return JS_EXCEPTION;
    if (JS_IsObject(tp_arg(0))) {
        tz_like = JS_GetPropertyStr(ctx, tp_arg(0), "timeZone");
        if (JS_IsException(tz_like))
            return JS_EXCEPTION;
        if (JS_IsUndefined(tz_like)) {
            r = tp_to_time_zone_identifier(ctx, tp_arg(0), &tz);
        } else {
            r = tp_to_time_zone_identifier(ctx, tz_like, &tz);
            if (!r) {
                time_like = JS_GetPropertyStr(ctx, tp_arg(0), "plainTime");
                if (JS_IsException(time_like))
                    r = -1;
            }
        }
        JS_FreeValue(ctx, tz_like);
    } else {
        r = tp_to_time_zone_identifier(ctx, tp_arg(0), &tz);
    }
    if (r)
        goto fail;
    if (JS_IsUndefined(time_like)) {
        if (tp_start_of_day(ctx, tz, o->dt.date, &ns))
            goto fail;
    } else {
        dt.date = o->dt.date;
        if (tp_to_temporal_time(ctx, time_like, JS_UNDEFINED, &dt.time))
            goto fail;
        if (!tp_iso_datetime_within_limits(&dt)) {
            JS_ThrowRangeError(ctx, "date-time is outside the supported range");
            goto fail;
        }
        if (tp_epoch_ns_for(ctx, tz, &dt, TP_DISAMBIG_COMPATIBLE, &ns))
            goto fail;
    }
    JS_FreeValue(ctx, time_like);
    return tp_new_zoned(ctx, ns, tz, o->cal, JS_UNDEFINED);
fail:
    JS_FreeValue(ctx, time_like);
    return JS_EXCEPTION;
}

static JSValue tp_plain_date_to_string(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATE);
    JSValue opts;
    int show = TP_SHOW_CAL_AUTO, r;
    TPBuf b;

    if (!o)
        return JS_EXCEPTION;
    if (tp_get_options_object(ctx, tp_arg(0), &opts))
        return JS_EXCEPTION;
    r = tp_get_show_calendar_option(ctx, opts, &show);
    JS_FreeValue(ctx, opts);
    if (r)
        return JS_EXCEPTION;
    tp_buf_init(&b);
    tp_put_iso_date(&b, o->dt.date);
    tp_put_calendar_annotation(&b, o->cal, show);
    return tp_buf_to_string(ctx, &b);
}

static JSValue tp_plain_date_to_json(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATE);
    TPBuf b;
    if (!o)
        return JS_EXCEPTION;
    tp_buf_init(&b);
    tp_put_iso_date(&b, o->dt.date);
    tp_put_calendar_annotation(&b, o->cal, TP_SHOW_CAL_AUTO);
    return tp_buf_to_string(ctx, &b);
}

#define TP_DATE_FIELD(name, field) JS_CGETSET_MAGIC_DEF(name, tp_plain_date_get_field, NULL, field)

static const JSCFunctionListEntry tp_plain_date_proto_funcs[] = {
    TP_DATE_FIELD("calendarId", TP_DF_CALENDAR_ID),
    TP_DATE_FIELD("era", TP_DF_ERA),
    TP_DATE_FIELD("eraYear", TP_DF_ERA_YEAR),
    TP_DATE_FIELD("year", TP_DF_YEAR),
    TP_DATE_FIELD("month", TP_DF_MONTH),
    TP_DATE_FIELD("monthCode", TP_DF_MONTH_CODE),
    TP_DATE_FIELD("day", TP_DF_DAY),
    TP_DATE_FIELD("dayOfWeek", TP_DF_DAY_OF_WEEK),
    TP_DATE_FIELD("dayOfYear", TP_DF_DAY_OF_YEAR),
    TP_DATE_FIELD("weekOfYear", TP_DF_WEEK_OF_YEAR),
    TP_DATE_FIELD("yearOfWeek", TP_DF_YEAR_OF_WEEK),
    TP_DATE_FIELD("daysInWeek", TP_DF_DAYS_IN_WEEK),
    TP_DATE_FIELD("daysInMonth", TP_DF_DAYS_IN_MONTH),
    TP_DATE_FIELD("daysInYear", TP_DF_DAYS_IN_YEAR),
    TP_DATE_FIELD("monthsInYear", TP_DF_MONTHS_IN_YEAR),
    TP_DATE_FIELD("inLeapYear", TP_DF_IN_LEAP_YEAR),
    JS_CFUNC_DEF("toPlainYearMonth", 0, tp_plain_date_to_year_month),
    JS_CFUNC_DEF("toPlainMonthDay", 0, tp_plain_date_to_month_day),
    JS_CFUNC_MAGIC_DEF("add", 1, tp_plain_date_add, 0),
    JS_CFUNC_MAGIC_DEF("subtract", 1, tp_plain_date_add, 1),
    JS_CFUNC_DEF("with", 1, tp_plain_date_with),
    JS_CFUNC_DEF("withCalendar", 1, tp_plain_date_with_calendar),
    JS_CFUNC_MAGIC_DEF("until", 1, tp_plain_date_difference, 0),
    JS_CFUNC_MAGIC_DEF("since", 1, tp_plain_date_difference, 1),
    JS_CFUNC_DEF("equals", 1, tp_plain_date_equals),
    JS_CFUNC_DEF("toPlainDateTime", 0, tp_plain_date_to_plain_datetime),
    JS_CFUNC_DEF("toZonedDateTime", 1, tp_plain_date_to_zoned),
    JS_CFUNC_DEF("toString", 0, tp_plain_date_to_string),
    JS_CFUNC_DEF("toLocaleString", 0, tp_plain_date_to_json),
    JS_CFUNC_DEF("toJSON", 0, tp_plain_date_to_json),
    JS_CFUNC_DEF("valueOf", 0, tp_value_of_throws),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Temporal.PlainDate", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry tp_plain_date_static_funcs[] = {
    JS_CFUNC_DEF("from", 1, tp_plain_date_from),
    JS_CFUNC_DEF("compare", 2, tp_plain_date_compare),
};

int tp_init_plain_date(JSContext *ctx, JSValue ns)
{
    return tp_define_class(ctx, ns, TP_KIND_DATE, "PlainDate", tp_plain_date_constructor, 3,
                           tp_plain_date_proto_funcs, countof(tp_plain_date_proto_funcs),
                           tp_plain_date_static_funcs, countof(tp_plain_date_static_funcs));
}

static JSValue tp_plain_datetime_constructor(JSContext *ctx, JSValueConst new_target, int argc,
                                             JSValueConst *argv)
{
    int64_t v[9];
    TPCalendar cal;
    TPDateTime dt;
    int i;

    for (i = 0; i < 9; i++) {
        if ((i < 3 ? tp_required_integer_argument : tp_integer_argument)(ctx, argc, argv, i, &v[i]))
            return JS_EXCEPTION;
    }
    if (tp_calendar_argument(ctx, argc, argv, 9, &cal))
        return JS_EXCEPTION;
    if (!tp_is_valid_iso_date(v[0], v[1], v[2]))
        return JS_ThrowRangeError(ctx, "invalid date");
    if (!tp_is_valid_time(v[3], v[4], v[5], v[6], v[7], v[8]))
        return JS_ThrowRangeError(ctx, "invalid time");
    dt.date.year = v[0];
    dt.date.month = (int32_t)v[1];
    dt.date.day = (int32_t)v[2];
    tp_regulate_time(ctx, v + 3, TP_OVERFLOW_CONSTRAIN, &dt.time);
    return tp_new_plain_datetime(ctx, &dt, cal, new_target);
}

static JSValue tp_plain_datetime_from(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPDateTime dt;
    TPCalendar cal;
    if (tp_to_temporal_datetime(ctx, tp_arg(0), tp_arg(1), &dt, &cal))
        return JS_EXCEPTION;
    return tp_new_plain_datetime(ctx, &dt, cal, JS_UNDEFINED);
}

static JSValue tp_plain_datetime_compare(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPDateTime a, b;
    TPCalendar cal;
    if (tp_to_temporal_datetime(ctx, tp_arg(0), JS_UNDEFINED, &a, &cal) ||
        tp_to_temporal_datetime(ctx, tp_arg(1), JS_UNDEFINED, &b, &cal))
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, tp_compare_datetime(&a, &b));
}

static JSValue tp_plain_datetime_get_field(JSContext *ctx, JSValueConst this_val, int magic)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATETIME);
    if (!o)
        return JS_EXCEPTION;
    if (magic >= TP_DF_HOUR)
        return tp_time_field_value(ctx, &o->dt.time, magic);
    return tp_date_field_value(ctx, o->cal, o->dt.date, magic);
}

static JSValue tp_plain_datetime_with(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATETIME);
    TPFields fields, partial;
    TPDateTime dt;
    int overflow;

    if (!o)
        return JS_EXCEPTION;
    if (tp_reject_temporal_like_object(ctx, tp_arg(0)))
        return JS_EXCEPTION;
    tp_iso_date_to_fields(o->cal, o->dt.date, TP_FIELDS_DATE, &fields);
    tp_fields_set_time(&fields, &o->dt.time);
    if (tp_prepare_calendar_fields(ctx, o->cal, tp_arg(0), TP_FM_DATE, TP_FM_TIME, 0, true, &partial))
        return JS_EXCEPTION;
    tp_calendar_merge_fields(o->cal, &fields, &partial, &fields);
    if (tp_overflow_from_options(ctx, tp_arg(1), &overflow) ||
        tp_interpret_datetime_fields(ctx, o->cal, &fields, overflow, &dt))
        return JS_EXCEPTION;
    return tp_new_plain_datetime(ctx, &dt, o->cal, JS_UNDEFINED);
}

static JSValue tp_plain_datetime_with_plain_time(JSContext *ctx, JSValueConst this_val, int argc,
                                                 JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATETIME);
    TPDateTime dt;
    if (!o)
        return JS_EXCEPTION;
    if (tp_to_time_record_or_midnight(ctx, tp_arg(0), &dt.time))
        return JS_EXCEPTION;
    dt.date = o->dt.date;
    return tp_new_plain_datetime(ctx, &dt, o->cal, JS_UNDEFINED);
}

static JSValue tp_plain_datetime_with_calendar(JSContext *ctx, JSValueConst this_val, int argc,
                                               JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATETIME);
    TPCalendar cal;
    if (!o)
        return JS_EXCEPTION;
    if (tp_to_calendar_identifier(ctx, tp_arg(0), &cal))
        return JS_EXCEPTION;
    return tp_new_plain_datetime(ctx, &o->dt, cal, JS_UNDEFINED);
}

static JSValue tp_plain_datetime_add(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
                                     int magic)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATETIME);
    TPDuration d;
    TPInternalDuration id;
    TPDateDuration dd;
    TPDateTime result;
    int64_t days;
    int overflow;

    if (!o)
        return JS_EXCEPTION;
    if (tp_to_temporal_duration(ctx, tp_arg(0), &d))
        return JS_EXCEPTION;
    if (magic)
        tp_negate_duration(&d);
    if (tp_overflow_from_options(ctx, tp_arg(1), &overflow) || tp_to_internal_duration_24h(ctx, &d, &id))
        return JS_EXCEPTION;
    days = tp_add_time(&o->dt.time, id.time, &result.time);
    if (tp_adjust_date_duration(ctx, &id.date, days, NULL, NULL, &dd) ||
        tp_calendar_date_add(ctx, o->cal, o->dt.date, &dd, overflow, &result.date))
        return JS_EXCEPTION;
    return tp_new_plain_datetime(ctx, &result, o->cal, JS_UNDEFINED);
}

static JSValue tp_plain_datetime_difference(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
                                            int magic)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATETIME);
    TPDateTime other;
    TPCalendar other_cal;
    JSValue opts, ret = JS_EXCEPTION;
    TPDiffSettings s;
    TPInternalDuration d;

    if (!o)
        return JS_EXCEPTION;
    if (tp_to_temporal_datetime(ctx, tp_arg(0), JS_UNDEFINED, &other, &other_cal))
        return JS_EXCEPTION;
    if (!tp_calendar_equals(o->cal, other_cal))
        return JS_ThrowRangeError(ctx, "cannot compute the difference between different calendars");
    if (tp_get_options_object(ctx, tp_arg(1), &opts))
        return JS_EXCEPTION;
    if (tp_get_difference_settings(ctx, magic, opts, TP_GROUP_DATETIME, 0, TP_UNIT_NANOSECOND, TP_UNIT_DAY, &s))
        goto done;
    if (tp_compare_datetime(&o->dt, &other) == 0) {
        memset(&d, 0, sizeof(d));
        ret = tp_duration_result(ctx, &d, TP_UNIT_DAY, false);
        goto done;
    }
    if (!tp_difference_plain_datetime_with_rounding(ctx, &o->dt, &other, o->cal, s.largest_unit,
                                                    s.rounding_increment, s.smallest_unit, s.rounding_mode, &d))
        ret = tp_duration_result(ctx, &d, s.largest_unit, magic);
done:
    JS_FreeValue(ctx, opts);
    return ret;
}

static JSValue tp_plain_datetime_round(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATETIME);
    JSValue opts, ret = JS_EXCEPTION;
    int64_t increment;
    int mode, smallest, maximum;
    TPDateTime result;

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
        ret = tp_new_plain_datetime(ctx, &o->dt, o->cal, JS_UNDEFINED);
        goto done;
    }
    result = tp_round_iso_datetime(&o->dt, increment, smallest, mode);
    ret = tp_new_plain_datetime(ctx, &result, o->cal, JS_UNDEFINED);
done:
    JS_FreeValue(ctx, opts);
    return ret;
}

static JSValue tp_plain_datetime_equals(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATETIME);
    TPDateTime other;
    TPCalendar cal;
    if (!o)
        return JS_EXCEPTION;
    if (tp_to_temporal_datetime(ctx, tp_arg(0), JS_UNDEFINED, &other, &cal))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, tp_compare_datetime(&o->dt, &other) == 0 && tp_calendar_equals(o->cal, cal));
}

static JSValue tp_plain_datetime_to_string(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATETIME);
    JSValue opts, ret = JS_EXCEPTION;
    int show_cal, digits, mode, smallest;
    TPPrecision p;
    TPDateTime result;
    TPBuf b;

    if (!o)
        return JS_EXCEPTION;
    if (tp_get_options_object(ctx, tp_arg(0), &opts))
        return JS_EXCEPTION;
    if (tp_get_show_calendar_option(ctx, opts, &show_cal) ||
        tp_get_fractional_second_digits_option(ctx, opts, &digits) ||
        tp_get_rounding_mode_option(ctx, opts, TP_ROUND_TRUNC, &mode) ||
        tp_get_unit_option(ctx, opts, "smallestUnit", false, &smallest) ||
        tp_validate_unit(ctx, smallest, TP_GROUP_TIME, TP_UNIT_UNSET, TP_UNIT_UNSET))
        goto done;
    if (smallest == TP_UNIT_HOUR) {
        JS_ThrowRangeError(ctx, "smallestUnit must not be hour");
        goto done;
    }
    p = tp_seconds_precision(smallest, digits);
    result = tp_round_iso_datetime(&o->dt, p.increment, p.unit, mode);
    if (!tp_iso_datetime_within_limits(&result)) {
        JS_ThrowRangeError(ctx, "date-time is outside the supported range");
        goto done;
    }
    tp_buf_init(&b);
    tp_put_iso_datetime(&b, &result, o->cal, p.precision, show_cal);
    ret = tp_buf_to_string(ctx, &b);
done:
    JS_FreeValue(ctx, opts);
    return ret;
}

static JSValue tp_plain_datetime_to_json(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATETIME);
    TPBuf b;
    if (!o)
        return JS_EXCEPTION;
    tp_buf_init(&b);
    tp_put_iso_datetime(&b, &o->dt, o->cal, TP_PRECISION_AUTO, TP_SHOW_CAL_AUTO);
    return tp_buf_to_string(ctx, &b);
}

static JSValue tp_plain_datetime_to_zoned(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATETIME);
    TPTimeZone tz;
    JSValue opts;
    int disambiguation, r;
    tp_i128 ns;

    if (!o)
        return JS_EXCEPTION;
    if (tp_to_time_zone_identifier(ctx, tp_arg(0), &tz))
        return JS_EXCEPTION;
    if (tp_get_options_object(ctx, tp_arg(1), &opts))
        return JS_EXCEPTION;
    r = tp_get_disambiguation_option(ctx, opts, &disambiguation);
    JS_FreeValue(ctx, opts);
    if (r || tp_epoch_ns_for(ctx, tz, &o->dt, disambiguation, &ns))
        return JS_EXCEPTION;
    return tp_new_zoned(ctx, ns, tz, o->cal, JS_UNDEFINED);
}

static JSValue tp_plain_datetime_to_plain(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
                                          int magic)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_DATETIME);
    if (!o)
        return JS_EXCEPTION;
    if (magic == TP_KIND_DATE)
        return tp_new_plain_date(ctx, o->dt.date, o->cal, JS_UNDEFINED);
    return tp_new_plain_time(ctx, &o->dt.time, JS_UNDEFINED);
}

#define TP_DATETIME_FIELD(name, field) JS_CGETSET_MAGIC_DEF(name, tp_plain_datetime_get_field, NULL, field)

static const JSCFunctionListEntry tp_plain_datetime_proto_funcs[] = {
    TP_DATETIME_FIELD("calendarId", TP_DF_CALENDAR_ID),
    TP_DATETIME_FIELD("era", TP_DF_ERA),
    TP_DATETIME_FIELD("eraYear", TP_DF_ERA_YEAR),
    TP_DATETIME_FIELD("year", TP_DF_YEAR),
    TP_DATETIME_FIELD("month", TP_DF_MONTH),
    TP_DATETIME_FIELD("monthCode", TP_DF_MONTH_CODE),
    TP_DATETIME_FIELD("day", TP_DF_DAY),
    TP_DATETIME_FIELD("hour", TP_DF_HOUR),
    TP_DATETIME_FIELD("minute", TP_DF_MINUTE),
    TP_DATETIME_FIELD("second", TP_DF_SECOND),
    TP_DATETIME_FIELD("millisecond", TP_DF_MILLISECOND),
    TP_DATETIME_FIELD("microsecond", TP_DF_MICROSECOND),
    TP_DATETIME_FIELD("nanosecond", TP_DF_NANOSECOND),
    TP_DATETIME_FIELD("dayOfWeek", TP_DF_DAY_OF_WEEK),
    TP_DATETIME_FIELD("dayOfYear", TP_DF_DAY_OF_YEAR),
    TP_DATETIME_FIELD("weekOfYear", TP_DF_WEEK_OF_YEAR),
    TP_DATETIME_FIELD("yearOfWeek", TP_DF_YEAR_OF_WEEK),
    TP_DATETIME_FIELD("daysInWeek", TP_DF_DAYS_IN_WEEK),
    TP_DATETIME_FIELD("daysInMonth", TP_DF_DAYS_IN_MONTH),
    TP_DATETIME_FIELD("daysInYear", TP_DF_DAYS_IN_YEAR),
    TP_DATETIME_FIELD("monthsInYear", TP_DF_MONTHS_IN_YEAR),
    TP_DATETIME_FIELD("inLeapYear", TP_DF_IN_LEAP_YEAR),
    JS_CFUNC_DEF("with", 1, tp_plain_datetime_with),
    JS_CFUNC_DEF("withPlainTime", 0, tp_plain_datetime_with_plain_time),
    JS_CFUNC_DEF("withCalendar", 1, tp_plain_datetime_with_calendar),
    JS_CFUNC_MAGIC_DEF("add", 1, tp_plain_datetime_add, 0),
    JS_CFUNC_MAGIC_DEF("subtract", 1, tp_plain_datetime_add, 1),
    JS_CFUNC_MAGIC_DEF("until", 1, tp_plain_datetime_difference, 0),
    JS_CFUNC_MAGIC_DEF("since", 1, tp_plain_datetime_difference, 1),
    JS_CFUNC_DEF("round", 1, tp_plain_datetime_round),
    JS_CFUNC_DEF("equals", 1, tp_plain_datetime_equals),
    JS_CFUNC_DEF("toString", 0, tp_plain_datetime_to_string),
    JS_CFUNC_DEF("toLocaleString", 0, tp_plain_datetime_to_json),
    JS_CFUNC_DEF("toJSON", 0, tp_plain_datetime_to_json),
    JS_CFUNC_DEF("valueOf", 0, tp_value_of_throws),
    JS_CFUNC_DEF("toZonedDateTime", 1, tp_plain_datetime_to_zoned),
    JS_CFUNC_MAGIC_DEF("toPlainDate", 0, tp_plain_datetime_to_plain, TP_KIND_DATE),
    JS_CFUNC_MAGIC_DEF("toPlainTime", 0, tp_plain_datetime_to_plain, TP_KIND_TIME),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Temporal.PlainDateTime", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry tp_plain_datetime_static_funcs[] = {
    JS_CFUNC_DEF("from", 1, tp_plain_datetime_from),
    JS_CFUNC_DEF("compare", 2, tp_plain_datetime_compare),
};

int tp_init_plain_datetime(JSContext *ctx, JSValue ns)
{
    return tp_define_class(ctx, ns, TP_KIND_DATETIME, "PlainDateTime", tp_plain_datetime_constructor, 3,
                           tp_plain_datetime_proto_funcs, countof(tp_plain_datetime_proto_funcs),
                           tp_plain_datetime_static_funcs, countof(tp_plain_datetime_static_funcs));
}

static JSValue tp_plain_time_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    int64_t v[6];
    TPTime t;
    int i;

    for (i = 0; i < 6; i++) {
        if (tp_integer_argument(ctx, argc, argv, i, &v[i]))
            return JS_EXCEPTION;
    }
    if (!tp_is_valid_time(v[0], v[1], v[2], v[3], v[4], v[5]))
        return JS_ThrowRangeError(ctx, "invalid time");
    tp_regulate_time(ctx, v, TP_OVERFLOW_CONSTRAIN, &t);
    return tp_new_plain_time(ctx, &t, new_target);
}

static JSValue tp_plain_time_from(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPTime t;
    if (tp_to_temporal_time(ctx, tp_arg(0), tp_arg(1), &t))
        return JS_EXCEPTION;
    return tp_new_plain_time(ctx, &t, JS_UNDEFINED);
}

static JSValue tp_plain_time_compare(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPTime a, b;
    if (tp_to_temporal_time(ctx, tp_arg(0), JS_UNDEFINED, &a) || tp_to_temporal_time(ctx, tp_arg(1), JS_UNDEFINED, &b))
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, tp_compare_time(&a, &b));
}

static JSValue tp_plain_time_get_field(JSContext *ctx, JSValueConst this_val, int magic)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_TIME);
    if (!o)
        return JS_EXCEPTION;
    return tp_time_field_value(ctx, &o->dt.time, magic);
}

static JSValue tp_plain_time_add(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_TIME);
    TPDuration d;
    TPInternalDuration id;
    TPTime t;

    if (!o)
        return JS_EXCEPTION;
    if (tp_to_temporal_duration(ctx, tp_arg(0), &d))
        return JS_EXCEPTION;
    if (magic)
        tp_negate_duration(&d);
    tp_to_internal_duration(&d, &id);
    tp_add_time(&o->dt.time, id.time, &t);
    return tp_new_plain_time(ctx, &t, JS_UNDEFINED);
}

static JSValue tp_plain_time_with(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_TIME);
    int64_t fields[6];
    bool present[6];
    const TPTime *t;
    TPTime result;
    int overflow;

    if (!o)
        return JS_EXCEPTION;
    t = &o->dt.time;
    if (tp_reject_temporal_like_object(ctx, tp_arg(0)) ||
        tp_to_temporal_time_record(ctx, tp_arg(0), true, fields, present))
        return JS_EXCEPTION;
    if (!present[0])
        fields[0] = t->hour;
    if (!present[1])
        fields[1] = t->minute;
    if (!present[2])
        fields[2] = t->second;
    if (!present[3])
        fields[3] = t->millisecond;
    if (!present[4])
        fields[4] = t->microsecond;
    if (!present[5])
        fields[5] = t->nanosecond;
    if (tp_overflow_from_options(ctx, tp_arg(1), &overflow) || tp_regulate_time(ctx, fields, overflow, &result))
        return JS_EXCEPTION;
    return tp_new_plain_time(ctx, &result, JS_UNDEFINED);
}

static JSValue tp_plain_time_difference(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
                                        int magic)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_TIME);
    TPTime other;
    JSValue opts, ret = JS_EXCEPTION;
    TPDiffSettings s;
    TPInternalDuration d;

    if (!o)
        return JS_EXCEPTION;
    if (tp_to_temporal_time(ctx, tp_arg(0), JS_UNDEFINED, &other))
        return JS_EXCEPTION;
    if (tp_get_options_object(ctx, tp_arg(1), &opts))
        return JS_EXCEPTION;
    memset(&d, 0, sizeof(d));
    if (!tp_get_difference_settings(ctx, magic, opts, TP_GROUP_TIME, 0, TP_UNIT_NANOSECOND, TP_UNIT_HOUR, &s) &&
        !tp_round_time_duration(ctx, tp_difference_time(&o->dt.time, &other), s.rounding_increment,
                                s.smallest_unit, s.rounding_mode, &d.time))
        ret = tp_duration_result(ctx, &d, s.largest_unit, magic);
    JS_FreeValue(ctx, opts);
    return ret;
}

static JSValue tp_plain_time_round(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_TIME);
    JSValue opts, ret = JS_EXCEPTION;
    int64_t increment;
    int mode, smallest;
    TPTime t;

    if (!o)
        return JS_EXCEPTION;
    if (tp_round_options_from_arg(ctx, tp_arg(0), "smallestUnit", &opts))
        return JS_EXCEPTION;
    if (tp_get_rounding_increment_option(ctx, opts, &increment) ||
        tp_get_rounding_mode_option(ctx, opts, TP_ROUND_HALF_EXPAND, &mode) ||
        tp_get_unit_option(ctx, opts, "smallestUnit", true, &smallest) ||
        tp_validate_unit(ctx, smallest, TP_GROUP_TIME, TP_UNIT_UNSET, TP_UNIT_UNSET) ||
        tp_validate_rounding_increment(ctx, increment, tp_maximum_rounding_increment(smallest), false))
        goto done;
    tp_round_time(&o->dt.time, increment, smallest, mode, &t);
    ret = tp_new_plain_time(ctx, &t, JS_UNDEFINED);
done:
    JS_FreeValue(ctx, opts);
    return ret;
}

static JSValue tp_plain_time_equals(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_TIME);
    TPTime other;
    if (!o)
        return JS_EXCEPTION;
    if (tp_to_temporal_time(ctx, tp_arg(0), JS_UNDEFINED, &other))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, tp_compare_time(&o->dt.time, &other) == 0);
}

static JSValue tp_time_string(JSContext *ctx, const TPTime *t, int precision)
{
    TPBuf b;
    tp_buf_init(&b);
    tp_put_time(&b, t->hour, t->minute, t->second, tp_subsecond(t), precision);
    return tp_buf_to_string(ctx, &b);
}

static JSValue tp_plain_time_to_string(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_TIME);
    JSValue opts, ret = JS_EXCEPTION;
    int digits, mode, smallest;
    TPPrecision p;
    TPTime t;

    if (!o)
        return JS_EXCEPTION;
    if (tp_get_options_object(ctx, tp_arg(0), &opts))
        return JS_EXCEPTION;
    if (tp_get_fractional_second_digits_option(ctx, opts, &digits) ||
        tp_get_rounding_mode_option(ctx, opts, TP_ROUND_TRUNC, &mode) ||
        tp_get_unit_option(ctx, opts, "smallestUnit", false, &smallest) ||
        tp_validate_unit(ctx, smallest, TP_GROUP_TIME, TP_UNIT_UNSET, TP_UNIT_UNSET))
        goto done;
    if (smallest == TP_UNIT_HOUR) {
        JS_ThrowRangeError(ctx, "smallestUnit must not be hour");
        goto done;
    }
    p = tp_seconds_precision(smallest, digits);
    tp_round_time(&o->dt.time, p.increment, p.unit, mode, &t);
    ret = tp_time_string(ctx, &t, p.precision);
done:
    JS_FreeValue(ctx, opts);
    return ret;
}

static JSValue tp_plain_time_to_json(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_TIME);
    if (!o)
        return JS_EXCEPTION;
    return tp_time_string(ctx, &o->dt.time, TP_PRECISION_AUTO);
}

#define TP_TIME_FIELD(name, field) JS_CGETSET_MAGIC_DEF(name, tp_plain_time_get_field, NULL, field)

static const JSCFunctionListEntry tp_plain_time_proto_funcs[] = {
    TP_TIME_FIELD("hour", TP_DF_HOUR),
    TP_TIME_FIELD("minute", TP_DF_MINUTE),
    TP_TIME_FIELD("second", TP_DF_SECOND),
    TP_TIME_FIELD("millisecond", TP_DF_MILLISECOND),
    TP_TIME_FIELD("microsecond", TP_DF_MICROSECOND),
    TP_TIME_FIELD("nanosecond", TP_DF_NANOSECOND),
    JS_CFUNC_MAGIC_DEF("add", 1, tp_plain_time_add, 0),
    JS_CFUNC_MAGIC_DEF("subtract", 1, tp_plain_time_add, 1),
    JS_CFUNC_DEF("with", 1, tp_plain_time_with),
    JS_CFUNC_MAGIC_DEF("until", 1, tp_plain_time_difference, 0),
    JS_CFUNC_MAGIC_DEF("since", 1, tp_plain_time_difference, 1),
    JS_CFUNC_DEF("round", 1, tp_plain_time_round),
    JS_CFUNC_DEF("equals", 1, tp_plain_time_equals),
    JS_CFUNC_DEF("toString", 0, tp_plain_time_to_string),
    JS_CFUNC_DEF("toLocaleString", 0, tp_plain_time_to_json),
    JS_CFUNC_DEF("toJSON", 0, tp_plain_time_to_json),
    JS_CFUNC_DEF("valueOf", 0, tp_value_of_throws),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Temporal.PlainTime", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry tp_plain_time_static_funcs[] = {
    JS_CFUNC_DEF("from", 1, tp_plain_time_from),
    JS_CFUNC_DEF("compare", 2, tp_plain_time_compare),
};

int tp_init_plain_time(JSContext *ctx, JSValue ns)
{
    return tp_define_class(ctx, ns, TP_KIND_TIME, "PlainTime", tp_plain_time_constructor, 0,
                           tp_plain_time_proto_funcs, countof(tp_plain_time_proto_funcs),
                           tp_plain_time_static_funcs, countof(tp_plain_time_static_funcs));
}
