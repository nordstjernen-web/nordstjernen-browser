/* quickjs-temporal-partial.c — Temporal.PlainYearMonth and Temporal.PlainMonthDay. */
#include <string.h>
#include "cutils.h"
#include "quickjs-temporal.h"

JSValue tp_new_year_month(JSContext *ctx, TPISODate d, TPCalendar cal, JSValueConst new_target)
{
    TPObject *o;
    JSValue v;

    if (!tp_iso_year_month_within_limits(d))
        return JS_ThrowRangeError(ctx, "year-month is outside the supported range");
    v = tp_alloc(ctx, new_target, TP_KIND_YEAR_MONTH, &o);
    if (JS_IsException(v))
        return v;
    o->dt.date = d;
    o->dt.time = tp_midnight();
    o->cal = cal;
    return v;
}

JSValue tp_new_month_day(JSContext *ctx, TPISODate d, TPCalendar cal, JSValueConst new_target)
{
    TPObject *o;
    JSValue v;

    if (!tp_iso_date_within_limits(d))
        return JS_ThrowRangeError(ctx, "month-day is outside the supported range");
    v = tp_alloc(ctx, new_target, TP_KIND_MONTH_DAY, &o);
    if (JS_IsException(v))
        return v;
    o->dt.date = d;
    o->dt.time = tp_midnight();
    o->cal = cal;
    return v;
}

static int tp_partial_overflow(JSContext *ctx, JSValueConst options, int *overflow)
{
    JSValue opts;
    int r;
    if (tp_get_options_object(ctx, options, &opts))
        return -1;
    r = tp_get_overflow_option(ctx, opts, overflow);
    JS_FreeValue(ctx, opts);
    return r;
}

static int tp_partial_integer(JSContext *ctx, JSValueConst v, int64_t *out)
{
    double d;
    if (tp_to_integer_with_truncation(ctx, v, &d))
        return -1;
    *out = tp_clamp_double(d);
    return 0;
}

static int tp_partial_calendar(JSContext *ctx, JSValueConst v, TPCalendar *cal)
{
    const char *s;
    size_t len;
    int r;

    if (JS_IsUndefined(v)) {
        *cal = TP_CAL_ISO8601;
        return 0;
    }
    if (tp_require_string(ctx, v))
        return -1;
    s = JS_ToCStringLen(ctx, &len, v);
    if (!s)
        return -1;
    r = tp_canonicalize_calendar(ctx, s, len, cal);
    JS_FreeCString(ctx, s);
    return r;
}

static int tp_partial_parse(JSContext *ctx, JSValueConst v, int goal, TPParsed *p, TPCalendar *cal)
{
    const int goals[] = { goal, TP_GOAL_END };
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

int tp_to_temporal_year_month(JSContext *ctx, JSValueConst v, JSValueConst options, TPISODate *d, TPCalendar *cal)
{
    TPObject *o;
    TPFields fields;
    TPParsed p;
    int overflow;

    if (JS_IsObject(v)) {
        if ((o = tp_get(v, TP_KIND_YEAR_MONTH))) {
            *d = o->dt.date;
            *cal = o->cal;
            return tp_partial_overflow(ctx, options, &overflow);
        }
        if (tp_get_calendar_with_iso_default(ctx, v, cal) ||
            tp_prepare_calendar_fields(ctx, *cal, v, TP_FB(TP_F_YEAR) | TP_FB(TP_F_MONTH) | TP_FB(TP_F_MONTH_CODE),
                                       0, 0, false, &fields) ||
            tp_partial_overflow(ctx, options, &overflow))
            return -1;
        return tp_calendar_year_month_from_fields(ctx, *cal, &fields, overflow, d);
    }
    if (tp_partial_parse(ctx, v, TP_GOAL_YEAR_MONTH, &p, cal) || tp_partial_overflow(ctx, options, &overflow))
        return -1;
    d->year = p.year;
    d->month = p.month;
    d->day = p.day;
    if (!tp_iso_year_month_within_limits(*d)) {
        JS_ThrowRangeError(ctx, "year-month is outside the supported range");
        return -1;
    }
    tp_iso_date_to_fields(*cal, *d, TP_FIELDS_YEAR_MONTH, &fields);
    return tp_calendar_year_month_from_fields(ctx, *cal, &fields, TP_OVERFLOW_CONSTRAIN, d);
}

int tp_to_temporal_month_day(JSContext *ctx, JSValueConst v, JSValueConst options, TPISODate *d, TPCalendar *cal)
{
    TPObject *o;
    TPFields fields;
    TPParsed p;
    int overflow;

    if (JS_IsObject(v)) {
        if ((o = tp_get(v, TP_KIND_MONTH_DAY))) {
            *d = o->dt.date;
            *cal = o->cal;
            return tp_partial_overflow(ctx, options, &overflow);
        }
        if (tp_get_calendar_with_iso_default(ctx, v, cal) ||
            tp_prepare_calendar_fields(ctx, *cal, v, TP_FM_DATE, 0, 0, false, &fields) ||
            tp_partial_overflow(ctx, options, &overflow))
            return -1;
        return tp_calendar_month_day_from_fields(ctx, *cal, &fields, overflow, d);
    }
    if (tp_partial_parse(ctx, v, TP_GOAL_MONTH_DAY, &p, cal) || tp_partial_overflow(ctx, options, &overflow))
        return -1;
    if (*cal == TP_CAL_ISO8601) {
        d->year = 1972;
        d->month = p.month;
        d->day = p.day;
        return 0;
    }
    d->year = p.year;
    d->month = p.month;
    d->day = p.day;
    if (!tp_iso_date_within_limits(*d)) {
        JS_ThrowRangeError(ctx, "date is outside the supported range");
        return -1;
    }
    tp_iso_date_to_fields(*cal, *d, TP_FIELDS_MONTH_DAY, &fields);
    return tp_calendar_month_day_from_fields(ctx, *cal, &fields, TP_OVERFLOW_CONSTRAIN, d);
}

static JSValue tp_year_month_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    int64_t y, m, ref = 1;
    TPCalendar cal;
    TPISODate d;

    if (tp_partial_integer(ctx, tp_arg(0), &y) || tp_partial_integer(ctx, tp_arg(1), &m) ||
        tp_partial_calendar(ctx, tp_arg(2), &cal))
        return JS_EXCEPTION;
    if (argc > 3 && !JS_IsUndefined(tp_arg(3)) && tp_partial_integer(ctx, tp_arg(3), &ref))
        return JS_EXCEPTION;
    if (!tp_is_valid_iso_date(y, m, ref))
        return JS_ThrowRangeError(ctx, "invalid date");
    d.year = y;
    d.month = (int32_t)m;
    d.day = (int32_t)ref;
    return tp_new_year_month(ctx, d, cal, new_target);
}

static JSValue tp_year_month_from(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPISODate d;
    TPCalendar cal;
    if (tp_to_temporal_year_month(ctx, tp_arg(0), tp_arg(1), &d, &cal))
        return JS_EXCEPTION;
    return tp_new_year_month(ctx, d, cal, JS_UNDEFINED);
}

static JSValue tp_year_month_compare(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPISODate a, b;
    TPCalendar cal;
    if (tp_to_temporal_year_month(ctx, tp_arg(0), JS_UNDEFINED, &a, &cal) ||
        tp_to_temporal_year_month(ctx, tp_arg(1), JS_UNDEFINED, &b, &cal))
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, tp_compare_iso_date(a, b));
}

static JSValue tp_year_month_get_field(JSContext *ctx, JSValueConst this_val, int magic)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_YEAR_MONTH);
    if (!o)
        return JS_EXCEPTION;
    return tp_date_field_value(ctx, o->cal, o->dt.date, magic);
}

static JSValue tp_year_month_with(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_YEAR_MONTH);
    TPFields fields, partial;
    TPISODate d;
    int overflow;

    if (!o)
        return JS_EXCEPTION;
    if (tp_reject_temporal_like_object(ctx, tp_arg(0)))
        return JS_EXCEPTION;
    tp_iso_date_to_fields(o->cal, o->dt.date, TP_FIELDS_YEAR_MONTH, &fields);
    if (tp_prepare_calendar_fields(ctx, o->cal, tp_arg(0), TP_FB(TP_F_YEAR) | TP_FB(TP_F_MONTH) |
                                   TP_FB(TP_F_MONTH_CODE), 0, 0, true, &partial))
        return JS_EXCEPTION;
    tp_calendar_merge_fields(o->cal, &fields, &partial, &fields);
    if (tp_partial_overflow(ctx, tp_arg(1), &overflow) ||
        tp_calendar_year_month_from_fields(ctx, o->cal, &fields, overflow, &d))
        return JS_EXCEPTION;
    return tp_new_year_month(ctx, d, o->cal, JS_UNDEFINED);
}

static JSValue tp_year_month_add(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_YEAR_MONTH);
    TPDuration dur;
    TPInternalDuration id;
    TPFields fields;
    TPISODate date, added;
    int overflow;

    if (!o)
        return JS_EXCEPTION;
    if (tp_to_temporal_duration(ctx, tp_arg(0), &dur))
        return JS_EXCEPTION;
    if (magic)
        tp_negate_duration(&dur);
    tp_to_internal_duration(&dur, &id);
    if (tp_partial_overflow(ctx, tp_arg(1), &overflow))
        return JS_EXCEPTION;
    if (id.date.weeks != 0 || id.date.days != 0 || id.time != 0)
        return JS_ThrowRangeError(ctx, "only years and months can be added to a PlainYearMonth");
    tp_iso_date_to_fields(o->cal, o->dt.date, TP_FIELDS_YEAR_MONTH, &fields);
    fields.has |= TP_FB(TP_F_DAY);
    fields.v[TP_F_DAY] = 1;
    if (tp_calendar_date_from_fields(ctx, o->cal, &fields, TP_OVERFLOW_CONSTRAIN, &date) ||
        tp_calendar_date_add(ctx, o->cal, date, &id.date, overflow, &added))
        return JS_EXCEPTION;
    tp_iso_date_to_fields(o->cal, added, TP_FIELDS_YEAR_MONTH, &fields);
    if (tp_calendar_year_month_from_fields(ctx, o->cal, &fields, overflow, &date))
        return JS_EXCEPTION;
    return tp_new_year_month(ctx, date, o->cal, JS_UNDEFINED);
}

static JSValue tp_year_month_difference(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
                                        int magic)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_YEAR_MONTH);
    TPISODate other, this_date, other_date;
    TPCalendar other_cal;
    JSValue opts, ret = JS_EXCEPTION;
    TPDiffSettings s;
    TPInternalDuration d;
    TPFields fields;

    if (!o)
        return JS_EXCEPTION;
    if (tp_to_temporal_year_month(ctx, tp_arg(0), JS_UNDEFINED, &other, &other_cal))
        return JS_EXCEPTION;
    if (!tp_calendar_equals(o->cal, other_cal))
        return JS_ThrowRangeError(ctx, "cannot compute the difference between different calendars");
    if (tp_get_options_object(ctx, tp_arg(1), &opts))
        return JS_EXCEPTION;
    if (tp_get_difference_settings(ctx, magic, opts, TP_GROUP_DATE, (1u << TP_UNIT_WEEK) | (1u << TP_UNIT_DAY),
                                   TP_UNIT_MONTH, TP_UNIT_YEAR, &s))
        goto done;
    memset(&d, 0, sizeof(d));
    if (tp_compare_iso_date(o->dt.date, other) == 0) {
        ret = tp_duration_result(ctx, &d, TP_UNIT_DAY, false);
        goto done;
    }
    tp_iso_date_to_fields(o->cal, o->dt.date, TP_FIELDS_YEAR_MONTH, &fields);
    fields.has |= TP_FB(TP_F_DAY);
    fields.v[TP_F_DAY] = 1;
    if (tp_calendar_date_from_fields(ctx, o->cal, &fields, TP_OVERFLOW_CONSTRAIN, &this_date))
        goto done;
    tp_iso_date_to_fields(o->cal, other, TP_FIELDS_YEAR_MONTH, &fields);
    fields.has |= TP_FB(TP_F_DAY);
    fields.v[TP_F_DAY] = 1;
    if (tp_calendar_date_from_fields(ctx, o->cal, &fields, TP_OVERFLOW_CONSTRAIN, &other_date))
        goto done;
    tp_calendar_date_until(o->cal, this_date, other_date, s.largest_unit, &d.date);
    d.date.weeks = 0;
    d.date.days = 0;
    if (s.smallest_unit != TP_UNIT_MONTH || s.rounding_increment != 1) {
        TPDateTime a, b;
        a.date = this_date;
        a.time = tp_midnight();
        b.date = other_date;
        b.time = tp_midnight();
        if (tp_round_relative_duration(ctx, &d, tp_utc_epoch_ns(&a), tp_utc_epoch_ns(&b), &a, NULL, o->cal,
                                       s.largest_unit, s.rounding_increment, s.smallest_unit, s.rounding_mode, &d))
            goto done;
    }
    ret = tp_duration_result(ctx, &d, TP_UNIT_DAY, magic);
done:
    JS_FreeValue(ctx, opts);
    return ret;
}

static JSValue tp_year_month_equals(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_YEAR_MONTH);
    TPISODate other;
    TPCalendar cal;
    if (!o)
        return JS_EXCEPTION;
    if (tp_to_temporal_year_month(ctx, tp_arg(0), JS_UNDEFINED, &other, &cal))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, tp_compare_iso_date(o->dt.date, other) == 0 && tp_calendar_equals(o->cal, cal));
}

static JSValue tp_year_month_string(JSContext *ctx, const TPObject *o, int show)
{
    TPBuf b;
    tp_buf_init(&b);
    tp_put_iso_year(&b, o->dt.date.year);
    tp_buf_putc(&b, '-');
    tp_buf_pad(&b, o->dt.date.month, 2);
    if (show == TP_SHOW_CAL_ALWAYS || show == TP_SHOW_CAL_CRITICAL || o->cal != TP_CAL_ISO8601) {
        tp_buf_putc(&b, '-');
        tp_buf_pad(&b, o->dt.date.day, 2);
    }
    tp_put_calendar_annotation(&b, o->cal, show);
    return tp_buf_to_string(ctx, &b);
}

static JSValue tp_year_month_to_string(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_YEAR_MONTH);
    JSValue opts;
    int show, r;
    if (!o)
        return JS_EXCEPTION;
    if (tp_get_options_object(ctx, tp_arg(0), &opts))
        return JS_EXCEPTION;
    r = tp_get_show_calendar_option(ctx, opts, &show);
    JS_FreeValue(ctx, opts);
    if (r)
        return JS_EXCEPTION;
    return tp_year_month_string(ctx, o, show);
}

static JSValue tp_year_month_to_json(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_YEAR_MONTH);
    if (!o)
        return JS_EXCEPTION;
    return tp_year_month_string(ctx, o, TP_SHOW_CAL_AUTO);
}

static JSValue tp_year_month_to_plain_date(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_YEAR_MONTH);
    TPFields fields, input;
    TPISODate d;

    if (!o)
        return JS_EXCEPTION;
    if (!JS_IsObject(tp_arg(0)))
        return JS_ThrowTypeError(ctx, "argument must be an object");
    tp_iso_date_to_fields(o->cal, o->dt.date, TP_FIELDS_YEAR_MONTH, &fields);
    if (tp_prepare_calendar_fields(ctx, o->cal, tp_arg(0), TP_FB(TP_F_DAY), 0, 0, false, &input))
        return JS_EXCEPTION;
    tp_calendar_merge_fields(o->cal, &fields, &input, &fields);
    if (tp_calendar_date_from_fields(ctx, o->cal, &fields, TP_OVERFLOW_CONSTRAIN, &d))
        return JS_EXCEPTION;
    return tp_new_plain_date(ctx, d, o->cal, JS_UNDEFINED);
}

#define TP_YM_FIELD(name, field) JS_CGETSET_MAGIC_DEF(name, tp_year_month_get_field, NULL, field)

static const JSCFunctionListEntry tp_year_month_proto_funcs[] = {
    TP_YM_FIELD("calendarId", TP_DF_CALENDAR_ID),
    TP_YM_FIELD("era", TP_DF_ERA),
    TP_YM_FIELD("eraYear", TP_DF_ERA_YEAR),
    TP_YM_FIELD("year", TP_DF_YEAR),
    TP_YM_FIELD("month", TP_DF_MONTH),
    TP_YM_FIELD("monthCode", TP_DF_MONTH_CODE),
    TP_YM_FIELD("daysInMonth", TP_DF_DAYS_IN_MONTH),
    TP_YM_FIELD("daysInYear", TP_DF_DAYS_IN_YEAR),
    TP_YM_FIELD("monthsInYear", TP_DF_MONTHS_IN_YEAR),
    TP_YM_FIELD("inLeapYear", TP_DF_IN_LEAP_YEAR),
    JS_CFUNC_DEF("with", 1, tp_year_month_with),
    JS_CFUNC_MAGIC_DEF("add", 1, tp_year_month_add, 0),
    JS_CFUNC_MAGIC_DEF("subtract", 1, tp_year_month_add, 1),
    JS_CFUNC_MAGIC_DEF("until", 1, tp_year_month_difference, 0),
    JS_CFUNC_MAGIC_DEF("since", 1, tp_year_month_difference, 1),
    JS_CFUNC_DEF("equals", 1, tp_year_month_equals),
    JS_CFUNC_DEF("toString", 0, tp_year_month_to_string),
    JS_CFUNC_DEF("toLocaleString", 0, tp_year_month_to_json),
    JS_CFUNC_DEF("toJSON", 0, tp_year_month_to_json),
    JS_CFUNC_DEF("valueOf", 0, tp_value_of_throws),
    JS_CFUNC_DEF("toPlainDate", 1, tp_year_month_to_plain_date),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Temporal.PlainYearMonth", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry tp_year_month_static_funcs[] = {
    JS_CFUNC_DEF("from", 1, tp_year_month_from),
    JS_CFUNC_DEF("compare", 2, tp_year_month_compare),
};

int tp_init_year_month(JSContext *ctx, JSValue ns)
{
    return tp_define_class(ctx, ns, TP_KIND_YEAR_MONTH, "PlainYearMonth", tp_year_month_constructor, 2,
                           tp_year_month_proto_funcs, countof(tp_year_month_proto_funcs),
                           tp_year_month_static_funcs, countof(tp_year_month_static_funcs));
}

static JSValue tp_month_day_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    int64_t m, d, ref = 1972;
    TPCalendar cal;
    TPISODate date;

    if (tp_partial_integer(ctx, tp_arg(0), &m) || tp_partial_integer(ctx, tp_arg(1), &d) ||
        tp_partial_calendar(ctx, tp_arg(2), &cal))
        return JS_EXCEPTION;
    if (argc > 3 && !JS_IsUndefined(tp_arg(3)) && tp_partial_integer(ctx, tp_arg(3), &ref))
        return JS_EXCEPTION;
    if (!tp_is_valid_iso_date(ref, m, d))
        return JS_ThrowRangeError(ctx, "invalid date");
    date.year = ref;
    date.month = (int32_t)m;
    date.day = (int32_t)d;
    return tp_new_month_day(ctx, date, cal, new_target);
}

static JSValue tp_month_day_from(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPISODate d;
    TPCalendar cal;
    if (tp_to_temporal_month_day(ctx, tp_arg(0), tp_arg(1), &d, &cal))
        return JS_EXCEPTION;
    return tp_new_month_day(ctx, d, cal, JS_UNDEFINED);
}

static JSValue tp_month_day_get_field(JSContext *ctx, JSValueConst this_val, int magic)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_MONTH_DAY);
    if (!o)
        return JS_EXCEPTION;
    return tp_date_field_value(ctx, o->cal, o->dt.date, magic);
}

static JSValue tp_month_day_with(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_MONTH_DAY);
    TPFields fields, partial;
    TPISODate d;
    int overflow;

    if (!o)
        return JS_EXCEPTION;
    if (tp_reject_temporal_like_object(ctx, tp_arg(0)))
        return JS_EXCEPTION;
    tp_iso_date_to_fields(o->cal, o->dt.date, TP_FIELDS_MONTH_DAY, &fields);
    if (tp_prepare_calendar_fields(ctx, o->cal, tp_arg(0), TP_FM_DATE, 0, 0, true, &partial))
        return JS_EXCEPTION;
    tp_calendar_merge_fields(o->cal, &fields, &partial, &fields);
    if (tp_partial_overflow(ctx, tp_arg(1), &overflow) ||
        tp_calendar_month_day_from_fields(ctx, o->cal, &fields, overflow, &d))
        return JS_EXCEPTION;
    return tp_new_month_day(ctx, d, o->cal, JS_UNDEFINED);
}

static JSValue tp_month_day_equals(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_MONTH_DAY);
    TPISODate other;
    TPCalendar cal;
    if (!o)
        return JS_EXCEPTION;
    if (tp_to_temporal_month_day(ctx, tp_arg(0), JS_UNDEFINED, &other, &cal))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, tp_compare_iso_date(o->dt.date, other) == 0 && tp_calendar_equals(o->cal, cal));
}

static JSValue tp_month_day_string(JSContext *ctx, const TPObject *o, int show)
{
    TPBuf b;
    tp_buf_init(&b);
    if (show == TP_SHOW_CAL_ALWAYS || show == TP_SHOW_CAL_CRITICAL || o->cal != TP_CAL_ISO8601) {
        tp_put_iso_year(&b, o->dt.date.year);
        tp_buf_putc(&b, '-');
    }
    tp_buf_pad(&b, o->dt.date.month, 2);
    tp_buf_putc(&b, '-');
    tp_buf_pad(&b, o->dt.date.day, 2);
    tp_put_calendar_annotation(&b, o->cal, show);
    return tp_buf_to_string(ctx, &b);
}

static JSValue tp_month_day_to_string(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_MONTH_DAY);
    JSValue opts;
    int show, r;
    if (!o)
        return JS_EXCEPTION;
    if (tp_get_options_object(ctx, tp_arg(0), &opts))
        return JS_EXCEPTION;
    r = tp_get_show_calendar_option(ctx, opts, &show);
    JS_FreeValue(ctx, opts);
    if (r)
        return JS_EXCEPTION;
    return tp_month_day_string(ctx, o, show);
}

static JSValue tp_month_day_to_json(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_MONTH_DAY);
    if (!o)
        return JS_EXCEPTION;
    return tp_month_day_string(ctx, o, TP_SHOW_CAL_AUTO);
}

static JSValue tp_month_day_to_plain_date(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_this(ctx, this_val, TP_KIND_MONTH_DAY);
    TPFields fields, input;
    TPISODate d;

    if (!o)
        return JS_EXCEPTION;
    if (!JS_IsObject(tp_arg(0)))
        return JS_ThrowTypeError(ctx, "argument must be an object");
    tp_iso_date_to_fields(o->cal, o->dt.date, TP_FIELDS_MONTH_DAY, &fields);
    if (tp_prepare_calendar_fields(ctx, o->cal, tp_arg(0), TP_FB(TP_F_YEAR), 0, 0, false, &input))
        return JS_EXCEPTION;
    tp_calendar_merge_fields(o->cal, &fields, &input, &fields);
    if (tp_calendar_date_from_fields(ctx, o->cal, &fields, TP_OVERFLOW_CONSTRAIN, &d))
        return JS_EXCEPTION;
    return tp_new_plain_date(ctx, d, o->cal, JS_UNDEFINED);
}

#define TP_MD_FIELD(name, field) JS_CGETSET_MAGIC_DEF(name, tp_month_day_get_field, NULL, field)

static const JSCFunctionListEntry tp_month_day_proto_funcs[] = {
    TP_MD_FIELD("calendarId", TP_DF_CALENDAR_ID),
    TP_MD_FIELD("monthCode", TP_DF_MONTH_CODE),
    TP_MD_FIELD("day", TP_DF_DAY),
    JS_CFUNC_DEF("with", 1, tp_month_day_with),
    JS_CFUNC_DEF("equals", 1, tp_month_day_equals),
    JS_CFUNC_DEF("toString", 0, tp_month_day_to_string),
    JS_CFUNC_DEF("toLocaleString", 0, tp_month_day_to_json),
    JS_CFUNC_DEF("toJSON", 0, tp_month_day_to_json),
    JS_CFUNC_DEF("valueOf", 0, tp_value_of_throws),
    JS_CFUNC_DEF("toPlainDate", 1, tp_month_day_to_plain_date),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Temporal.PlainMonthDay", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry tp_month_day_static_funcs[] = {
    JS_CFUNC_DEF("from", 1, tp_month_day_from),
};

int tp_init_month_day(JSContext *ctx, JSValue ns)
{
    return tp_define_class(ctx, ns, TP_KIND_MONTH_DAY, "PlainMonthDay", tp_month_day_constructor, 2,
                           tp_month_day_proto_funcs, countof(tp_month_day_proto_funcs),
                           tp_month_day_static_funcs, countof(tp_month_day_static_funcs));
}
