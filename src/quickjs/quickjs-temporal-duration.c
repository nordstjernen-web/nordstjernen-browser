/* quickjs-temporal-duration.c — Temporal.Duration and the duration difference, rounding and total engine. */
#include <math.h>
#include <string.h>
#include "cutils.h"
#include "quickjs-temporal.h"

static const char *const tp_duration_fields_sorted[TP_UNIT_COUNT] = {
    "days", "hours", "microseconds", "milliseconds", "minutes", "months", "nanoseconds", "seconds", "weeks",
    "years"
};

static const int tp_duration_fields_sorted_index[TP_UNIT_COUNT] = {
    TP_UNIT_DAY, TP_UNIT_HOUR, TP_UNIT_MICROSECOND, TP_UNIT_MILLISECOND, TP_UNIT_MINUTE, TP_UNIT_MONTH,
    TP_UNIT_NANOSECOND, TP_UNIT_SECOND, TP_UNIT_WEEK, TP_UNIT_YEAR
};

bool tp_is_valid_duration(const TPDuration *d)
{
    static const double limit = 9.007199254740992e24;
    int sign = 0, i;
    tp_i128 total = 0;

    for (i = 0; i < TP_UNIT_COUNT; i++) {
        double v = d->f[i];
        if (!isfinite(v))
            return false;
        if (v < 0) {
            if (sign > 0)
                return false;
            sign = -1;
        } else if (v > 0) {
            if (sign < 0)
                return false;
            sign = 1;
        }
    }
    for (i = TP_UNIT_YEAR; i <= TP_UNIT_WEEK; i++) {
        if (fabs(d->f[i]) >= 4294967296.0)
            return false;
    }
    for (i = TP_UNIT_DAY; i < TP_UNIT_COUNT; i++) {
        tp_i128 v;
        if (fabs(d->f[i]) * (double)tp_unit_ns[i] > limit * 1.01)
            return false;
        tp_double_to_i128(d->f[i], &v);
        total += v * tp_unit_ns[i];
    }
    return tp_abs128(total) < TP_TIME_DURATION_MAX + 1;
}

int tp_validate_duration(JSContext *ctx, const TPDuration *d)
{
    if (!tp_is_valid_duration(d)) {
        JS_ThrowRangeError(ctx, "invalid duration");
        return -1;
    }
    return 0;
}

JSValue tp_new_duration(JSContext *ctx, const TPDuration *d, JSValueConst new_target)
{
    TPObject *o;
    JSValue v;
    int i;

    if (tp_validate_duration(ctx, d))
        return JS_EXCEPTION;
    v = tp_alloc(ctx, new_target, TP_KIND_DURATION, &o);
    if (JS_IsException(v))
        return v;
    for (i = 0; i < TP_UNIT_COUNT; i++)
        o->dur.f[i] = d->f[i] + 0.0;
    return v;
}

void tp_negate_duration(TPDuration *d)
{
    int i;
    for (i = 0; i < TP_UNIT_COUNT; i++)
        d->f[i] = d->f[i] == 0 ? 0 : -d->f[i];
}

int tp_duration_sign(const TPDuration *d)
{
    int i;
    for (i = 0; i < TP_UNIT_COUNT; i++) {
        if (d->f[i] < 0)
            return -1;
        if (d->f[i] > 0)
            return 1;
    }
    return 0;
}

int tp_date_duration_sign(const TPDateDuration *d)
{
    const int64_t v[4] = { d->years, d->months, d->weeks, d->days };
    int i;
    for (i = 0; i < 4; i++) {
        if (v[i] < 0)
            return -1;
        if (v[i] > 0)
            return 1;
    }
    return 0;
}

int tp_internal_duration_sign(const TPInternalDuration *d)
{
    int s = tp_date_duration_sign(&d->date);
    return s ? s : tp_sign128(d->time);
}

int tp_default_largest_unit(const TPDuration *d)
{
    int i;
    for (i = 0; i < TP_UNIT_NANOSECOND; i++) {
        if (d->f[i] != 0)
            return i;
    }
    return TP_UNIT_NANOSECOND;
}

tp_i128 tp_time_duration_from_components(const TPDuration *d)
{
    tp_i128 total = 0, v;
    int i;
    for (i = TP_UNIT_HOUR; i < TP_UNIT_COUNT; i++) {
        tp_double_to_i128(d->f[i], &v);
        total += v * tp_unit_ns[i];
    }
    return total;
}

int tp_check_time_duration(JSContext *ctx, tp_i128 t)
{
    if (tp_abs128(t) > TP_TIME_DURATION_MAX) {
        JS_ThrowRangeError(ctx, "time duration is out of range");
        return -1;
    }
    return 0;
}

int tp_time_duration_add_days(JSContext *ctx, tp_i128 t, int64_t days, tp_i128 *out)
{
    tp_i128 r = t + (tp_i128)days * TP_NS_PER_DAY;
    if (tp_check_time_duration(ctx, r))
        return -1;
    *out = r;
    return 0;
}

void tp_to_internal_duration(const TPDuration *d, TPInternalDuration *out)
{
    out->date.years = (int64_t)d->f[TP_UNIT_YEAR];
    out->date.months = (int64_t)d->f[TP_UNIT_MONTH];
    out->date.weeks = (int64_t)d->f[TP_UNIT_WEEK];
    out->date.days = (int64_t)d->f[TP_UNIT_DAY];
    out->time = tp_time_duration_from_components(d);
}

int tp_to_internal_duration_24h(JSContext *ctx, const TPDuration *d, TPInternalDuration *out)
{
    tp_to_internal_duration(d, out);
    if (tp_time_duration_add_days(ctx, out->time, out->date.days, &out->time))
        return -1;
    out->date.days = 0;
    return 0;
}

static bool tp_date_duration_valid(const TPDateDuration *d)
{
    TPDuration x;
    memset(&x, 0, sizeof(x));
    x.f[TP_UNIT_YEAR] = (double)d->years;
    x.f[TP_UNIT_MONTH] = (double)d->months;
    x.f[TP_UNIT_WEEK] = (double)d->weeks;
    x.f[TP_UNIT_DAY] = (double)d->days;
    return tp_is_valid_duration(&x);
}

static int tp_create_date_duration(JSContext *ctx, int64_t years, int64_t months, int64_t weeks, int64_t days,
                                   TPDateDuration *out)
{
    TPDateDuration d = { years, months, weeks, days };
    if (!tp_date_duration_valid(&d)) {
        JS_ThrowRangeError(ctx, "date duration is out of range");
        return -1;
    }
    *out = d;
    return 0;
}

int tp_adjust_date_duration(JSContext *ctx, const TPDateDuration *d, int64_t days, const int64_t *weeks,
                            const int64_t *months, TPDateDuration *out)
{
    return tp_create_date_duration(ctx, d->years, months ? *months : d->months, weeks ? *weeks : d->weeks,
                                   days, out);
}

int tp_to_date_duration_without_time(JSContext *ctx, const TPDuration *d, TPDateDuration *out)
{
    TPInternalDuration id;
    if (tp_to_internal_duration_24h(ctx, d, &id))
        return -1;
    return tp_create_date_duration(ctx, id.date.years, id.date.months, id.date.weeks,
                                   (int64_t)(id.time / TP_NS_PER_DAY), out);
}

int tp_duration_from_internal(JSContext *ctx, const TPInternalDuration *d, int largest_unit, TPDuration *out)
{
    tp_i128 ns = tp_abs128(d->time), us = 0, ms = 0, s = 0, mi = 0, h = 0, days = 0;
    int sign = tp_sign128(d->time);

    if (largest_unit <= TP_UNIT_MICROSECOND) {
        us = ns / 1000;
        ns %= 1000;
    }
    if (largest_unit <= TP_UNIT_MILLISECOND) {
        ms = us / 1000;
        us %= 1000;
    }
    if (largest_unit <= TP_UNIT_SECOND) {
        s = ms / 1000;
        ms %= 1000;
    }
    if (largest_unit <= TP_UNIT_MINUTE) {
        mi = s / 60;
        s %= 60;
    }
    if (largest_unit <= TP_UNIT_HOUR) {
        h = mi / 60;
        mi %= 60;
    }
    if (largest_unit <= TP_UNIT_DAY) {
        days = h / 24;
        h %= 24;
    }
    out->f[TP_UNIT_YEAR] = (double)d->date.years;
    out->f[TP_UNIT_MONTH] = (double)d->date.months;
    out->f[TP_UNIT_WEEK] = (double)d->date.weeks;
    out->f[TP_UNIT_DAY] = tp_i128_to_double((tp_i128)d->date.days + days * sign);
    out->f[TP_UNIT_HOUR] = tp_i128_to_double(h * sign);
    out->f[TP_UNIT_MINUTE] = tp_i128_to_double(mi * sign);
    out->f[TP_UNIT_SECOND] = tp_i128_to_double(s * sign);
    out->f[TP_UNIT_MILLISECOND] = tp_i128_to_double(ms * sign);
    out->f[TP_UNIT_MICROSECOND] = tp_i128_to_double(us * sign);
    out->f[TP_UNIT_NANOSECOND] = tp_i128_to_double(ns * sign);
    return tp_validate_duration(ctx, out);
}

JSValue tp_duration_result(JSContext *ctx, const TPInternalDuration *d, int largest_unit, bool negate)
{
    TPDuration r;
    if (tp_duration_from_internal(ctx, d, largest_unit, &r))
        return JS_EXCEPTION;
    if (negate)
        tp_negate_duration(&r);
    return tp_new_duration(ctx, &r, JS_UNDEFINED);
}

int tp_round_time_duration(JSContext *ctx, tp_i128 t, int64_t increment, int unit, int mode, tp_i128 *out)
{
    tp_i128 r = tp_round_to_increment(t, (tp_i128)tp_unit_ns[unit] * increment, mode);
    if (tp_check_time_duration(ctx, r))
        return -1;
    *out = r;
    return 0;
}

double tp_total_time_duration(tp_i128 t, int unit)
{
    return tp_i128_fdiv(t, tp_unit_ns[unit]);
}

int tp_add_instant(JSContext *ctx, tp_i128 ns, tp_i128 t, tp_i128 *out)
{
    tp_i128 r = ns + t;
    if (!tp_epoch_ns_valid(r)) {
        JS_ThrowRangeError(ctx, "instant is outside the supported range");
        return -1;
    }
    *out = r;
    return 0;
}

int tp_add_zoned_datetime(JSContext *ctx, tp_i128 ns, TPTimeZone tz, TPCalendar cal,
                          const TPInternalDuration *d, int overflow, tp_i128 *out)
{
    TPDateTime dt, intermediate;
    tp_i128 intermediate_ns;

    if (tp_date_duration_sign(&d->date) == 0)
        return tp_add_instant(ctx, ns, d->time, out);
    tp_iso_datetime_for(tz, ns, &dt);
    if (tp_calendar_date_add(ctx, cal, dt.date, &d->date, overflow, &intermediate.date))
        return -1;
    intermediate.time = dt.time;
    if (tp_epoch_ns_for(ctx, tz, &intermediate, TP_DISAMBIG_COMPATIBLE, &intermediate_ns))
        return -1;
    return tp_add_instant(ctx, intermediate_ns, d->time, out);
}

int tp_difference_iso_datetime(JSContext *ctx, const TPDateTime *a, const TPDateTime *b, TPCalendar cal,
                               int largest_unit, TPInternalDuration *out)
{
    tp_i128 t = tp_difference_time(&a->time, &b->time);
    int time_sign = tp_sign128(t);
    int date_sign = tp_compare_iso_date(a->date, b->date);
    TPISODate adjusted = b->date;
    int date_largest = tp_larger_unit(TP_UNIT_DAY, largest_unit);

    if (time_sign == date_sign) {
        adjusted = tp_add_days(adjusted, time_sign);
        t -= (tp_i128)time_sign * TP_NS_PER_DAY;
    }
    tp_calendar_date_until(cal, a->date, adjusted, date_largest, &out->date);
    if (largest_unit != date_largest) {
        t += (tp_i128)out->date.days * TP_NS_PER_DAY;
        out->date.days = 0;
    }
    out->time = t;
    return tp_check_time_duration(ctx, t);
}

int tp_difference_zoned_datetime(JSContext *ctx, tp_i128 ns1, tp_i128 ns2, TPTimeZone tz, TPCalendar cal,
                                 int largest_unit, TPInternalDuration *out)
{
    TPDateTime start, end, intermediate;
    tp_i128 t, intermediate_ns;
    int sign, max_correction, correction = 0;
    bool success = false;

    memset(out, 0, sizeof(*out));
    if (ns1 == ns2)
        return 0;
    tp_iso_datetime_for(tz, ns1, &start);
    tp_iso_datetime_for(tz, ns2, &end);
    if (tp_compare_iso_date(start.date, end.date) == 0) {
        out->time = ns2 - ns1;
        return 0;
    }
    sign = ns2 - ns1 < 0 ? 1 : -1;
    max_correction = sign == -1 ? 2 : 1;
    t = tp_difference_time(&start.time, &end.time);
    if (tp_sign128(t) == sign)
        correction++;
    intermediate = start;
    while (correction <= max_correction && !success) {
        intermediate.date = tp_add_days(end.date, (int64_t)correction * sign);
        intermediate.time = start.time;
        if (tp_epoch_ns_for(ctx, tz, &intermediate, TP_DISAMBIG_COMPATIBLE, &intermediate_ns))
            return -1;
        t = ns2 - intermediate_ns;
        if (tp_sign128(t) != sign)
            success = true;
        correction++;
    }
    if (!success) {
        JS_ThrowRangeError(ctx, "time zone returned inconsistent results");
        return -1;
    }
    tp_calendar_date_until(cal, start.date, intermediate.date, tp_larger_unit(largest_unit, TP_UNIT_DAY),
                           &out->date);
    out->time = t;
    return 0;
}

static int tp_epoch_ns_maybe_zoned(JSContext *ctx, const TPTimeZone *tz, const TPDateTime *dt, tp_i128 *out)
{
    if (!tz) {
        *out = tp_utc_epoch_ns(dt);
        return 0;
    }
    return tp_epoch_ns_for(ctx, *tz, dt, TP_DISAMBIG_COMPATIBLE, out);
}

typedef struct TPNudgeWindow {
    int64_t r1, r2;
    tp_i128 start_ns, end_ns;
    TPDateDuration start, end;
} TPNudgeWindow;

typedef struct TPNudgeResult {
    TPInternalDuration duration;
    tp_i128 nudged_ns;
    bool did_expand;
} TPNudgeResult;

static int64_t tp_trunc_to_increment(int64_t v, int64_t increment)
{
    return (v / increment) * increment;
}

static int tp_compute_nudge_window(JSContext *ctx, int sign, const TPInternalDuration *d, tp_i128 origin_ns,
                                   const TPDateTime *dt, const TPTimeZone *tz, TPCalendar cal,
                                   int64_t increment, int unit, bool additional_shift, TPNudgeWindow *w)
{
    TPDateTime start_dt, end_dt;
    int64_t zero = 0;

    if (unit == TP_UNIT_YEAR) {
        int64_t years = tp_trunc_to_increment(d->date.years, increment);
        w->r1 = additional_shift ? years + increment * sign : years;
        w->r2 = w->r1 + increment * sign;
        if (tp_create_date_duration(ctx, w->r1, 0, 0, 0, &w->start) ||
            tp_create_date_duration(ctx, w->r2, 0, 0, 0, &w->end))
            return -1;
    } else if (unit == TP_UNIT_MONTH) {
        int64_t months = tp_trunc_to_increment(d->date.months, increment);
        w->r1 = additional_shift ? months + increment * sign : months;
        w->r2 = w->r1 + increment * sign;
        if (tp_adjust_date_duration(ctx, &d->date, 0, &zero, &w->r1, &w->start) ||
            tp_adjust_date_duration(ctx, &d->date, 0, &zero, &w->r2, &w->end))
            return -1;
    } else if (unit == TP_UNIT_WEEK) {
        TPDateDuration years_months, until;
        TPISODate weeks_start, weeks_end;
        int64_t weeks;
        if (tp_adjust_date_duration(ctx, &d->date, 0, &zero, NULL, &years_months))
            return -1;
        if (tp_calendar_date_add(ctx, cal, dt->date, &years_months, TP_OVERFLOW_CONSTRAIN, &weeks_start))
            return -1;
        weeks_end = tp_add_days(weeks_start, d->date.days);
        tp_calendar_date_until(cal, weeks_start, weeks_end, TP_UNIT_WEEK, &until);
        weeks = tp_trunc_to_increment(d->date.weeks + until.weeks, increment);
        w->r1 = weeks;
        w->r2 = weeks + increment * sign;
        if (tp_adjust_date_duration(ctx, &d->date, 0, &w->r1, NULL, &w->start) ||
            tp_adjust_date_duration(ctx, &d->date, 0, &w->r2, NULL, &w->end))
            return -1;
    } else {
        int64_t days = tp_trunc_to_increment(d->date.days, increment);
        w->r1 = days;
        w->r2 = days + increment * sign;
        if (tp_adjust_date_duration(ctx, &d->date, w->r1, NULL, NULL, &w->start) ||
            tp_adjust_date_duration(ctx, &d->date, w->r2, NULL, NULL, &w->end))
            return -1;
    }
    if (tp_date_duration_sign(&w->start) == 0) {
        w->start_ns = origin_ns;
    } else {
        if (tp_calendar_date_add(ctx, cal, dt->date, &w->start, TP_OVERFLOW_CONSTRAIN, &start_dt.date))
            return -1;
        start_dt.time = dt->time;
        if (tp_epoch_ns_maybe_zoned(ctx, tz, &start_dt, &w->start_ns))
            return -1;
    }
    if (tp_calendar_date_add(ctx, cal, dt->date, &w->end, TP_OVERFLOW_CONSTRAIN, &end_dt.date))
        return -1;
    end_dt.time = dt->time;
    return tp_epoch_ns_maybe_zoned(ctx, tz, &end_dt, &w->end_ns);
}

static bool tp_window_contains(int sign, const TPNudgeWindow *w, tp_i128 dest)
{
    if (sign == 1)
        return w->start_ns <= dest && dest <= w->end_ns;
    return w->end_ns <= dest && dest <= w->start_ns;
}

static int tp_nudge_to_calendar_unit(JSContext *ctx, int sign, const TPInternalDuration *d, tp_i128 origin_ns,
                                     tp_i128 dest_ns, const TPDateTime *dt, const TPTimeZone *tz,
                                     TPCalendar cal, int64_t increment, int unit, int mode,
                                     TPNudgeResult *result, double *total)
{
    TPNudgeWindow w;
    bool did_expand = false, at_r2;
    tp_i128 num, den;

    if (tp_compute_nudge_window(ctx, sign, d, origin_ns, dt, tz, cal, increment, unit, false, &w))
        return -1;
    if (!tp_window_contains(sign, &w, dest_ns)) {
        if (tp_compute_nudge_window(ctx, sign, d, origin_ns, dt, tz, cal, increment, unit, true, &w))
            return -1;
        did_expand = true;
    }
    num = dest_ns - w.start_ns;
    den = w.end_ns - w.start_ns;
    if (den == 0) {
        JS_ThrowRangeError(ctx, "zero-length calendar unit");
        return -1;
    }
    if (total)
        *total = tp_i128_fdiv((tp_i128)w.r1 * den + num * increment * sign, den);
    if (num == den) {
        at_r2 = true;
    } else if (num == 0) {
        at_r2 = false;
    } else {
        int64_t r1 = w.r1 < 0 ? -w.r1 : w.r1;
        int cmp = tp_cmp128(tp_abs128(num) * 2, tp_abs128(den));
        tp_i128 r = tp_apply_unsigned_rounding(0, 1, cmp, (r1 / increment) % 2 == 0,
                                               tp_unsigned_rounding_mode(mode, sign < 0));
        at_r2 = r == 1;
    }
    memset(&result->duration, 0, sizeof(result->duration));
    if (at_r2) {
        did_expand = true;
        result->duration.date = w.end;
        result->nudged_ns = w.end_ns;
    } else {
        result->duration.date = w.start;
        result->nudged_ns = w.start_ns;
    }
    result->did_expand = did_expand;
    return 0;
}

static int tp_nudge_to_zoned_time(JSContext *ctx, int sign, const TPInternalDuration *d, const TPDateTime *dt,
                                  TPTimeZone tz, TPCalendar cal, int64_t increment, int unit, int mode,
                                  TPNudgeResult *result)
{
    TPDateTime start_dt, end_dt;
    tp_i128 start_ns, end_ns, day_span, rounded, beyond;
    int64_t day_delta;

    if (tp_calendar_date_add(ctx, cal, dt->date, &d->date, TP_OVERFLOW_CONSTRAIN, &start_dt.date))
        return -1;
    start_dt.time = dt->time;
    end_dt.date = tp_add_days(start_dt.date, sign);
    end_dt.time = dt->time;
    if (tp_epoch_ns_for(ctx, tz, &start_dt, TP_DISAMBIG_COMPATIBLE, &start_ns) ||
        tp_epoch_ns_for(ctx, tz, &end_dt, TP_DISAMBIG_COMPATIBLE, &end_ns))
        return -1;
    day_span = end_ns - start_ns;
    if (tp_sign128(day_span) != sign) {
        JS_ThrowRangeError(ctx, "time zone returned inconsistent instants");
        return -1;
    }
    if (tp_round_time_duration(ctx, d->time, increment, unit, mode, &rounded))
        return -1;
    beyond = rounded - day_span;
    if (tp_sign128(beyond) != -sign) {
        result->did_expand = true;
        day_delta = sign;
        if (tp_round_time_duration(ctx, beyond, increment, unit, mode, &rounded))
            return -1;
        result->nudged_ns = rounded + end_ns;
    } else {
        result->did_expand = false;
        day_delta = 0;
        result->nudged_ns = rounded + start_ns;
    }
    if (tp_adjust_date_duration(ctx, &d->date, d->date.days + day_delta, NULL, NULL, &result->duration.date))
        return -1;
    result->duration.time = rounded;
    return 0;
}

static int tp_nudge_to_day_or_time(JSContext *ctx, const TPInternalDuration *d, tp_i128 dest_ns,
                                   int largest_unit, int64_t increment, int smallest_unit, int mode,
                                   TPNudgeResult *result)
{
    tp_i128 t, rounded, whole_days, rounded_whole_days, remainder;
    int64_t days = 0;
    int delta_sign;

    if (tp_time_duration_add_days(ctx, d->time, d->date.days, &t))
        return -1;
    if (tp_round_time_duration(ctx, t, increment, smallest_unit, mode, &rounded))
        return -1;
    whole_days = t / TP_NS_PER_DAY;
    rounded_whole_days = rounded / TP_NS_PER_DAY;
    delta_sign = tp_sign128(rounded_whole_days - whole_days);
    result->did_expand = delta_sign == tp_sign128(t);
    result->nudged_ns = rounded - t + dest_ns;
    remainder = rounded;
    if (tp_unit_category(largest_unit) == TP_GROUP_DATE) {
        days = (int64_t)rounded_whole_days;
        remainder = rounded - rounded_whole_days * TP_NS_PER_DAY;
    }
    if (tp_adjust_date_duration(ctx, &d->date, days, NULL, NULL, &result->duration.date))
        return -1;
    result->duration.time = remainder;
    return 0;
}

static int tp_bubble_relative_duration(JSContext *ctx, int sign, const TPInternalDuration *d, tp_i128 nudged_ns,
                                       const TPDateTime *dt, const TPTimeZone *tz, TPCalendar cal,
                                       int largest_unit, int smallest_unit, TPInternalDuration *out)
{
    int unit;
    *out = *d;
    if (smallest_unit == largest_unit)
        return 0;
    for (unit = smallest_unit - 1; unit >= largest_unit; unit--) {
        TPDateDuration end_duration;
        TPDateTime end_dt;
        tp_i128 end_ns;
        int64_t zero = 0, v;

        if (unit == TP_UNIT_WEEK && largest_unit != TP_UNIT_WEEK)
            continue;
        if (unit == TP_UNIT_YEAR) {
            if (tp_create_date_duration(ctx, out->date.years + sign, 0, 0, 0, &end_duration))
                return -1;
        } else if (unit == TP_UNIT_MONTH) {
            v = out->date.months + sign;
            if (tp_adjust_date_duration(ctx, &out->date, 0, &zero, &v, &end_duration))
                return -1;
        } else {
            v = out->date.weeks + sign;
            if (tp_adjust_date_duration(ctx, &out->date, 0, &v, NULL, &end_duration))
                return -1;
        }
        if (tp_calendar_date_add(ctx, cal, dt->date, &end_duration, TP_OVERFLOW_CONSTRAIN, &end_dt.date))
            return -1;
        end_dt.time = dt->time;
        if (tp_epoch_ns_maybe_zoned(ctx, tz, &end_dt, &end_ns))
            return -1;
        if (tp_sign128(nudged_ns - end_ns) != -sign) {
            out->date = end_duration;
            out->time = 0;
        } else {
            break;
        }
    }
    return 0;
}

int tp_round_relative_duration(JSContext *ctx, const TPInternalDuration *d, tp_i128 origin_ns, tp_i128 dest_ns,
                               const TPDateTime *dt, const TPTimeZone *tz, TPCalendar cal, int largest_unit,
                               int64_t increment, int smallest_unit, int mode, TPInternalDuration *out)
{
    bool irregular = tp_is_calendar_unit(smallest_unit) || (tz && smallest_unit == TP_UNIT_DAY);
    int sign = tp_internal_duration_sign(d) < 0 ? -1 : 1;
    TPNudgeResult nudge;
    int r;

    if (irregular)
        r = tp_nudge_to_calendar_unit(ctx, sign, d, origin_ns, dest_ns, dt, tz, cal, increment, smallest_unit,
                                      mode, &nudge, NULL);
    else if (tz)
        r = tp_nudge_to_zoned_time(ctx, sign, d, dt, *tz, cal, increment, smallest_unit, mode, &nudge);
    else
        r = tp_nudge_to_day_or_time(ctx, d, dest_ns, largest_unit, increment, smallest_unit, mode, &nudge);
    if (r)
        return -1;
    if (nudge.did_expand && smallest_unit != TP_UNIT_WEEK)
        return tp_bubble_relative_duration(ctx, sign, &nudge.duration, nudge.nudged_ns, dt, tz, cal, largest_unit,
                                           tp_larger_unit(smallest_unit, TP_UNIT_DAY), out);
    *out = nudge.duration;
    return 0;
}

static int tp_total_relative_duration(JSContext *ctx, const TPInternalDuration *d, tp_i128 origin_ns,
                                      tp_i128 dest_ns, const TPDateTime *dt, const TPTimeZone *tz, TPCalendar cal,
                                      int unit, double *out)
{
    tp_i128 t;
    if (tp_is_calendar_unit(unit) || (tz && unit == TP_UNIT_DAY)) {
        TPNudgeResult nudge;
        int sign = tp_internal_duration_sign(d) < 0 ? -1 : 1;
        return tp_nudge_to_calendar_unit(ctx, sign, d, origin_ns, dest_ns, dt, tz, cal, 1, unit, TP_ROUND_TRUNC,
                                         &nudge, out);
    }
    if (tp_time_duration_add_days(ctx, d->time, d->date.days, &t))
        return -1;
    *out = tp_total_time_duration(t, unit);
    return 0;
}

int tp_difference_plain_datetime_with_rounding(JSContext *ctx, const TPDateTime *a, const TPDateTime *b,
                                               TPCalendar cal, int largest_unit, int64_t increment,
                                               int smallest_unit, int mode, TPInternalDuration *out)
{
    TPInternalDuration diff;

    if (tp_compare_datetime(a, b) == 0) {
        memset(out, 0, sizeof(*out));
        return 0;
    }
    if (!tp_iso_datetime_within_limits(a) || !tp_iso_datetime_within_limits(b)) {
        JS_ThrowRangeError(ctx, "date-time is outside the supported range");
        return -1;
    }
    if (tp_difference_iso_datetime(ctx, a, b, cal, largest_unit, &diff))
        return -1;
    if (smallest_unit == TP_UNIT_NANOSECOND && increment == 1) {
        *out = diff;
        return 0;
    }
    return tp_round_relative_duration(ctx, &diff, tp_utc_epoch_ns(a), tp_utc_epoch_ns(b), a, NULL, cal,
                                      largest_unit, increment, smallest_unit, mode, out);
}

int tp_difference_plain_datetime_with_total(JSContext *ctx, const TPDateTime *a, const TPDateTime *b,
                                            TPCalendar cal, int unit, double *out)
{
    TPInternalDuration diff;

    if (tp_compare_datetime(a, b) == 0) {
        *out = 0;
        return 0;
    }
    if (!tp_iso_datetime_within_limits(a) || !tp_iso_datetime_within_limits(b)) {
        JS_ThrowRangeError(ctx, "date-time is outside the supported range");
        return -1;
    }
    if (tp_difference_iso_datetime(ctx, a, b, cal, unit, &diff))
        return -1;
    if (unit == TP_UNIT_NANOSECOND) {
        *out = tp_i128_to_double(diff.time);
        return 0;
    }
    return tp_total_relative_duration(ctx, &diff, tp_utc_epoch_ns(a), tp_utc_epoch_ns(b), a, NULL, cal, unit, out);
}

int tp_difference_instant(JSContext *ctx, tp_i128 ns1, tp_i128 ns2, int64_t increment, int smallest_unit,
                          int mode, TPInternalDuration *out)
{
    memset(out, 0, sizeof(*out));
    return tp_round_time_duration(ctx, ns2 - ns1, increment, smallest_unit, mode, &out->time);
}

int tp_difference_zoned_datetime_with_rounding(JSContext *ctx, tp_i128 ns1, tp_i128 ns2, TPTimeZone tz,
                                               TPCalendar cal, int largest_unit, int64_t increment,
                                               int smallest_unit, int mode, TPInternalDuration *out)
{
    TPInternalDuration diff;
    TPDateTime dt;

    if (tp_unit_category(largest_unit) == TP_GROUP_TIME)
        return tp_difference_instant(ctx, ns1, ns2, increment, smallest_unit, mode, out);
    if (tp_difference_zoned_datetime(ctx, ns1, ns2, tz, cal, largest_unit, &diff))
        return -1;
    if (smallest_unit == TP_UNIT_NANOSECOND && increment == 1) {
        *out = diff;
        return 0;
    }
    tp_iso_datetime_for(tz, ns1, &dt);
    return tp_round_relative_duration(ctx, &diff, ns1, ns2, &dt, &tz, cal, largest_unit, increment,
                                      smallest_unit, mode, out);
}

int tp_difference_zoned_datetime_with_total(JSContext *ctx, tp_i128 ns1, tp_i128 ns2, TPTimeZone tz,
                                            TPCalendar cal, int unit, double *out)
{
    TPInternalDuration diff;
    TPDateTime dt;

    if (tp_unit_category(unit) == TP_GROUP_TIME) {
        *out = tp_total_time_duration(ns2 - ns1, unit);
        return 0;
    }
    if (tp_difference_zoned_datetime(ctx, ns1, ns2, tz, cal, unit, &diff))
        return -1;
    tp_iso_datetime_for(tz, ns1, &dt);
    return tp_total_relative_duration(ctx, &diff, ns1, ns2, &dt, &tz, cal, unit, out);
}

int tp_get_difference_settings(JSContext *ctx, bool since, JSValueConst options, int group,
                               uint32_t disallowed, int fallback_smallest, int smallest_largest_default,
                               TPDiffSettings *out)
{
    int largest, smallest, mode, maximum;
    int64_t increment;

    if (tp_get_unit_option(ctx, options, "largestUnit", false, &largest) ||
        tp_get_rounding_increment_option(ctx, options, &increment) ||
        tp_get_rounding_mode_option(ctx, options, TP_ROUND_TRUNC, &mode) ||
        tp_get_unit_option(ctx, options, "smallestUnit", false, &smallest))
        return -1;
    if (tp_validate_unit(ctx, largest, group, TP_UNIT_AUTO, TP_UNIT_UNSET))
        return -1;
    if (largest == TP_UNIT_UNSET)
        largest = TP_UNIT_AUTO;
    if (largest != TP_UNIT_AUTO && (disallowed & (1u << largest))) {
        JS_ThrowRangeError(ctx, "largestUnit %s is not allowed", tp_unit_singular[largest]);
        return -1;
    }
    if (tp_validate_unit(ctx, smallest, group, TP_UNIT_UNSET, TP_UNIT_UNSET))
        return -1;
    if (smallest == TP_UNIT_UNSET)
        smallest = fallback_smallest;
    if (disallowed & (1u << smallest)) {
        JS_ThrowRangeError(ctx, "smallestUnit %s is not allowed", tp_unit_singular[smallest]);
        return -1;
    }
    if (largest == TP_UNIT_AUTO)
        largest = tp_larger_unit(smallest_largest_default, smallest);
    if (tp_larger_unit(largest, smallest) != largest) {
        JS_ThrowRangeError(ctx, "largestUnit must not be smaller than smallestUnit");
        return -1;
    }
    maximum = tp_maximum_rounding_increment(smallest);
    if (maximum && tp_validate_rounding_increment(ctx, increment, maximum, false))
        return -1;
    out->largest_unit = largest;
    out->smallest_unit = smallest;
    out->rounding_increment = increment;
    out->rounding_mode = since ? tp_negate_rounding_mode(mode) : mode;
    return 0;
}

static int tp_relative_to_from_object(JSContext *ctx, JSValueConst value, TPRelativeTo *out)
{
    TPObject *o;
    TPFields fields;
    TPDateTime dt;
    tp_i128 ns;

    if ((o = tp_get(value, TP_KIND_ZONED))) {
        out->kind = TP_RELATIVE_ZONED;
        out->ns = o->ns;
        out->tz = o->tz;
        out->cal = o->cal;
        return 0;
    }
    if ((o = tp_get(value, TP_KIND_DATE)) || (o = tp_get(value, TP_KIND_DATETIME))) {
        out->kind = TP_RELATIVE_PLAIN;
        out->date = o->dt.date;
        out->cal = o->cal;
        return 0;
    }
    if (tp_get_calendar_with_iso_default(ctx, value, &out->cal))
        return -1;
    if (tp_prepare_calendar_fields(ctx, out->cal, value, TP_FM_DATE,
                                   TP_FM_TIME | TP_FB(TP_F_OFFSET) | TP_FB(TP_F_TIME_ZONE), 0, false, &fields))
        return -1;
    if (tp_interpret_datetime_fields(ctx, out->cal, &fields, TP_OVERFLOW_CONSTRAIN, &dt))
        return -1;
    if (!(fields.has & TP_FB(TP_F_TIME_ZONE))) {
        if (!tp_iso_date_within_limits(dt.date)) {
            JS_ThrowRangeError(ctx, "date is outside the supported range");
            return -1;
        }
        out->kind = TP_RELATIVE_PLAIN;
        out->date = dt.date;
        return 0;
    }
    if (tp_interpret_iso_datetime_offset(ctx, dt.date, &dt.time,
                                         (fields.has & TP_FB(TP_F_OFFSET)) ? TP_OFFSET_BEHAVIOUR_OPTION
                                                                          : TP_OFFSET_BEHAVIOUR_WALL,
                                         (fields.has & TP_FB(TP_F_OFFSET)) ? fields.offset_ns : 0, fields.tz,
                                         TP_DISAMBIG_COMPATIBLE, TP_OFFSET_REJECT, false, &ns))
        return -1;
    out->kind = TP_RELATIVE_ZONED;
    out->ns = ns;
    out->tz = fields.tz;
    return 0;
}

static int tp_relative_to_from_string(JSContext *ctx, const char *s, size_t len, TPRelativeTo *out)
{
    static const int goals[] = { TP_GOAL_DATETIME_ZONED, TP_GOAL_DATETIME, TP_GOAL_END };
    TPParsed p;
    TPISODate d;
    TPTimeZone tz;
    int behaviour = TP_OFFSET_BEHAVIOUR_OPTION;
    bool match_minutes = false;
    tp_i128 ns;

    if (tp_parse_iso_datetime(ctx, s, len, goals, &p))
        return -1;
    if (p.cal_len) {
        if (tp_canonicalize_calendar(ctx, s + p.cal_start, p.cal_len, &out->cal))
            return -1;
    } else {
        out->cal = TP_CAL_ISO8601;
    }
    d.year = p.year;
    d.month = p.month;
    d.day = p.day;
    if (!p.tz_len) {
        if (!tp_iso_date_within_limits(d)) {
            JS_ThrowRangeError(ctx, "date is outside the supported range");
            return -1;
        }
        out->kind = TP_RELATIVE_PLAIN;
        out->date = d;
        return 0;
    }
    if (tp_time_zone_from_identifier(ctx, s + p.tz_start, p.tz_len, &tz))
        return -1;
    if (p.z)
        behaviour = TP_OFFSET_BEHAVIOUR_EXACT;
    else if (!p.has_offset)
        behaviour = TP_OFFSET_BEHAVIOUR_WALL;
    match_minutes = !(p.has_offset && p.offset_sub_minute);
    if (tp_interpret_iso_datetime_offset(ctx, d, p.has_time ? &p.time : NULL, behaviour,
                                         behaviour == TP_OFFSET_BEHAVIOUR_OPTION ? p.offset_ns : 0, tz,
                                         TP_DISAMBIG_COMPATIBLE, TP_OFFSET_REJECT, match_minutes, &ns))
        return -1;
    out->kind = TP_RELATIVE_ZONED;
    out->ns = ns;
    out->tz = tz;
    return 0;
}

int tp_get_relative_to_option(JSContext *ctx, JSValueConst options, TPRelativeTo *out)
{
    JSValue value = JS_GetPropertyStr(ctx, options, "relativeTo");
    int r;

    memset(out, 0, sizeof(*out));
    if (JS_IsException(value))
        return -1;
    if (JS_IsUndefined(value)) {
        out->kind = TP_RELATIVE_NONE;
        return 0;
    }
    if (JS_IsObject(value)) {
        r = tp_relative_to_from_object(ctx, value, out);
    } else if (!JS_IsString(value)) {
        JS_ThrowTypeError(ctx, "relativeTo must be a string or object");
        r = -1;
    } else {
        size_t len;
        const char *s = JS_ToCStringLen(ctx, &len, value);
        r = -1;
        if (s) {
            r = tp_relative_to_from_string(ctx, s, len, out);
            JS_FreeCString(ctx, s);
        }
    }
    JS_FreeValue(ctx, value);
    return r;
}

static int tp_date_duration_days(JSContext *ctx, const TPDateDuration *d, const TPRelativeTo *rel, int64_t *out)
{
    TPDateDuration ymw = *d;
    TPISODate later;

    ymw.days = 0;
    if (tp_date_duration_sign(&ymw) == 0) {
        *out = d->days;
        return 0;
    }
    if (tp_calendar_date_add(ctx, rel->cal, rel->date, &ymw, TP_OVERFLOW_CONSTRAIN, &later))
        return -1;
    *out = d->days + tp_iso_date_epoch_days(later) - tp_iso_date_epoch_days(rel->date);
    return 0;
}

static void tp_put_double_int(TPBuf *b, double v)
{
    char buf[48];
    tp_i128 x;
    tp_double_to_i128(fabs(v), &x);
    tp_i128_to_str(buf, x);
    tp_buf_puts(b, buf);
}

void tp_put_duration(TPBuf *b, const TPDuration *d, int precision)
{
    static const char date_designators[4] = { 'Y', 'M', 'W', 'D' };
    int sign = tp_duration_sign(d), i, largest = tp_default_largest_unit(d);
    tp_i128 seconds = 0, v;
    TPBuf time;
    char buf[48];

    if (sign < 0)
        tp_buf_putc(b, '-');
    tp_buf_putc(b, 'P');
    for (i = 0; i < 4; i++) {
        if (d->f[i] != 0) {
            tp_put_double_int(b, d->f[i]);
            tp_buf_putc(b, date_designators[i]);
        }
    }
    tp_buf_init(&time);
    if (d->f[TP_UNIT_HOUR] != 0) {
        tp_put_double_int(&time, d->f[TP_UNIT_HOUR]);
        tp_buf_putc(&time, 'H');
    }
    if (d->f[TP_UNIT_MINUTE] != 0) {
        tp_put_double_int(&time, d->f[TP_UNIT_MINUTE]);
        tp_buf_putc(&time, 'M');
    }
    for (i = TP_UNIT_SECOND; i < TP_UNIT_COUNT; i++) {
        tp_double_to_i128(d->f[i], &v);
        seconds += v * tp_unit_ns[i];
    }
    if (seconds != 0 || largest >= TP_UNIT_SECOND || precision != TP_PRECISION_AUTO) {
        tp_i128 a = tp_abs128(seconds);
        tp_i128_to_str(buf, a / 1000000000);
        tp_buf_puts(&time, buf);
        tp_put_fraction(&time, (int64_t)(a % 1000000000), precision);
        tp_buf_putc(&time, 'S');
    }
    if (time.len) {
        tp_buf_putc(b, 'T');
        tp_buf_putn(b, time.s, (size_t)time.len);
    }
}

static TPObject *tp_duration_this(JSContext *ctx, JSValueConst this_val)
{
    return tp_this(ctx, this_val, TP_KIND_DURATION);
}

static int tp_partial_duration(JSContext *ctx, JSValueConst v, TPDuration *out, bool present[TP_UNIT_COUNT])
{
    bool any = false;
    int i;

    if (!JS_IsObject(v)) {
        JS_ThrowTypeError(ctx, "duration-like argument must be an object");
        return -1;
    }
    for (i = 0; i < TP_UNIT_COUNT; i++) {
        int field = tp_duration_fields_sorted_index[i];
        JSValue value = JS_GetPropertyStr(ctx, v, tp_duration_fields_sorted[i]);
        int r;
        if (JS_IsException(value))
            return -1;
        present[field] = !JS_IsUndefined(value);
        if (present[field]) {
            any = true;
            r = tp_to_integer_if_integral(ctx, value, &out->f[field]);
            JS_FreeValue(ctx, value);
            if (r)
                return -1;
        }
    }
    if (!any) {
        JS_ThrowTypeError(ctx, "duration-like object must have at least one duration property");
        return -1;
    }
    return 0;
}

int tp_to_temporal_duration(JSContext *ctx, JSValueConst v, TPDuration *out)
{
    TPObject *o = tp_get(v, TP_KIND_DURATION);
    bool present[TP_UNIT_COUNT];
    int r;

    if (o) {
        *out = o->dur;
        return 0;
    }
    memset(out, 0, sizeof(*out));
    if (!JS_IsObject(v)) {
        size_t len;
        const char *s;
        if (tp_require_string(ctx, v))
            return -1;
        s = JS_ToCStringLen(ctx, &len, v);
        if (!s)
            return -1;
        r = tp_parse_duration(ctx, s, len, out);
        JS_FreeCString(ctx, s);
        if (r)
            return -1;
        return tp_validate_duration(ctx, out);
    }
    if (tp_partial_duration(ctx, v, out, present))
        return -1;
    return tp_validate_duration(ctx, out);
}

static JSValue tp_duration_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    TPDuration d;
    int i;

    memset(&d, 0, sizeof(d));
    for (i = 0; i < TP_UNIT_COUNT && i < argc; i++) {
        if (!JS_IsUndefined(tp_arg(i)) && tp_to_integer_if_integral(ctx, tp_arg(i), &d.f[i]))
            return JS_EXCEPTION;
    }
    return tp_new_duration(ctx, &d, new_target);
}

static JSValue tp_duration_get_field(JSContext *ctx, JSValueConst this_val, int magic)
{
    TPObject *o = tp_duration_this(ctx, this_val);
    if (!o)
        return JS_EXCEPTION;
    return JS_NewFloat64(ctx, o->dur.f[magic]);
}

static JSValue tp_duration_get_sign(JSContext *ctx, JSValueConst this_val)
{
    TPObject *o = tp_duration_this(ctx, this_val);
    if (!o)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, tp_duration_sign(&o->dur));
}

static JSValue tp_duration_get_blank(JSContext *ctx, JSValueConst this_val)
{
    TPObject *o = tp_duration_this(ctx, this_val);
    if (!o)
        return JS_EXCEPTION;
    return JS_NewBool(ctx, tp_duration_sign(&o->dur) == 0);
}

static JSValue tp_duration_with(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_duration_this(ctx, this_val);
    TPDuration partial, result;
    bool present[TP_UNIT_COUNT];
    int i;

    if (!o)
        return JS_EXCEPTION;
    if (tp_partial_duration(ctx, tp_arg(0), &partial, present))
        return JS_EXCEPTION;
    for (i = 0; i < TP_UNIT_COUNT; i++)
        result.f[i] = present[i] ? partial.f[i] : o->dur.f[i];
    return tp_new_duration(ctx, &result, JS_UNDEFINED);
}

static JSValue tp_duration_negated(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_duration_this(ctx, this_val);
    TPDuration d;
    if (!o)
        return JS_EXCEPTION;
    d = o->dur;
    tp_negate_duration(&d);
    return tp_new_duration(ctx, &d, JS_UNDEFINED);
}

static JSValue tp_duration_abs(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_duration_this(ctx, this_val);
    TPDuration d;
    int i;
    if (!o)
        return JS_EXCEPTION;
    for (i = 0; i < TP_UNIT_COUNT; i++)
        d.f[i] = fabs(o->dur.f[i]);
    return tp_new_duration(ctx, &d, JS_UNDEFINED);
}

static JSValue tp_duration_add(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
    TPObject *o = tp_duration_this(ctx, this_val);
    TPDuration other;
    TPInternalDuration d1, d2, result;
    int largest;

    if (!o)
        return JS_EXCEPTION;
    if (tp_to_temporal_duration(ctx, tp_arg(0), &other))
        return JS_EXCEPTION;
    if (magic)
        tp_negate_duration(&other);
    largest = tp_larger_unit(tp_default_largest_unit(&o->dur), tp_default_largest_unit(&other));
    if (tp_is_calendar_unit(largest))
        return JS_ThrowRangeError(ctx, "a starting point is required for years, months, or weeks arithmetic");
    if (tp_to_internal_duration_24h(ctx, &o->dur, &d1) || tp_to_internal_duration_24h(ctx, &other, &d2))
        return JS_EXCEPTION;
    memset(&result, 0, sizeof(result));
    result.time = d1.time + d2.time;
    if (tp_check_time_duration(ctx, result.time))
        return JS_EXCEPTION;
    return tp_duration_result(ctx, &result, largest, false);
}

static int tp_plain_relative_target(JSContext *ctx, const TPDuration *dur, const TPRelativeTo *rel,
                                    TPDateTime *start, TPDateTime *target)
{
    TPInternalDuration d;
    TPDateDuration date_duration;
    int64_t delta;

    if (tp_to_internal_duration_24h(ctx, dur, &d))
        return -1;
    delta = tp_add_time(&(TPTime){ 0, 0, 0, 0, 0, 0 }, d.time, &target->time);
    if (tp_adjust_date_duration(ctx, &d.date, delta, NULL, NULL, &date_duration))
        return -1;
    if (tp_calendar_date_add(ctx, rel->cal, rel->date, &date_duration, TP_OVERFLOW_CONSTRAIN, &target->date))
        return -1;
    start->date = rel->date;
    start->time = tp_midnight();
    return 0;
}

static JSValue tp_duration_round(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_duration_this(ctx, this_val);
    JSValue opts;
    TPRelativeTo rel;
    TPInternalDuration d, result;
    int largest, smallest, mode, existing, default_largest, maximum;
    int64_t increment;
    bool smallest_present = true, largest_present = true;
    JSValue ret = JS_EXCEPTION;

    if (!o)
        return JS_EXCEPTION;
    if (tp_round_options_from_arg(ctx, tp_arg(0), "smallestUnit", &opts))
        return JS_EXCEPTION;
    if (tp_get_unit_option(ctx, opts, "largestUnit", false, &largest) ||
        tp_get_relative_to_option(ctx, opts, &rel) ||
        tp_get_rounding_increment_option(ctx, opts, &increment) ||
        tp_get_rounding_mode_option(ctx, opts, TP_ROUND_HALF_EXPAND, &mode) ||
        tp_get_unit_option(ctx, opts, "smallestUnit", false, &smallest))
        goto done;
    if (tp_validate_unit(ctx, smallest, TP_GROUP_DATETIME, TP_UNIT_UNSET, TP_UNIT_UNSET))
        goto done;
    if (smallest == TP_UNIT_UNSET) {
        smallest_present = false;
        smallest = TP_UNIT_NANOSECOND;
    }
    existing = tp_default_largest_unit(&o->dur);
    default_largest = tp_larger_unit(existing, smallest);
    if (largest == TP_UNIT_UNSET) {
        largest_present = false;
        largest = default_largest;
    } else if (largest == TP_UNIT_AUTO) {
        largest = default_largest;
    }
    if (!smallest_present && !largest_present) {
        JS_ThrowRangeError(ctx, "at least one of smallestUnit or largestUnit is required");
        goto done;
    }
    if (tp_larger_unit(largest, smallest) != largest) {
        JS_ThrowRangeError(ctx, "largestUnit must not be smaller than smallestUnit");
        goto done;
    }
    maximum = tp_maximum_rounding_increment(smallest);
    if (maximum && tp_validate_rounding_increment(ctx, increment, maximum, false))
        goto done;
    if (increment > 1 && largest != smallest && tp_unit_category(smallest) == TP_GROUP_DATE) {
        JS_ThrowRangeError(ctx, "roundingIncrement > 1 requires largestUnit to equal smallestUnit for date units");
        goto done;
    }
    if (rel.kind == TP_RELATIVE_ZONED) {
        tp_i128 target;
        tp_to_internal_duration(&o->dur, &d);
        if (tp_add_zoned_datetime(ctx, rel.ns, rel.tz, rel.cal, &d, TP_OVERFLOW_CONSTRAIN, &target) ||
            tp_difference_zoned_datetime_with_rounding(ctx, rel.ns, target, rel.tz, rel.cal, largest, increment,
                                                       smallest, mode, &result))
            goto done;
        if (tp_unit_category(largest) == TP_GROUP_DATE)
            largest = TP_UNIT_HOUR;
        ret = tp_duration_result(ctx, &result, largest, false);
        goto done;
    }
    if (rel.kind == TP_RELATIVE_PLAIN) {
        TPDateTime start, target;
        if (tp_plain_relative_target(ctx, &o->dur, &rel, &start, &target) ||
            tp_difference_plain_datetime_with_rounding(ctx, &start, &target, rel.cal, largest, increment, smallest,
                                                       mode, &result))
            goto done;
        ret = tp_duration_result(ctx, &result, largest, false);
        goto done;
    }
    if (tp_is_calendar_unit(existing) || tp_is_calendar_unit(largest)) {
        JS_ThrowRangeError(ctx, "a starting point is required for years, months, or weeks rounding");
        goto done;
    }
    if (tp_to_internal_duration_24h(ctx, &o->dur, &d))
        goto done;
    memset(&result, 0, sizeof(result));
    if (smallest == TP_UNIT_DAY) {
        tp_i128 rounded = tp_round_to_increment(d.time, (tp_i128)TP_NS_PER_DAY * increment, mode);
        if (tp_create_date_duration(ctx, 0, 0, 0, (int64_t)(rounded / TP_NS_PER_DAY), &result.date))
            goto done;
    } else if (tp_round_time_duration(ctx, d.time, increment, smallest, mode, &result.time)) {
        goto done;
    }
    ret = tp_duration_result(ctx, &result, largest, false);
done:
    JS_FreeValue(ctx, opts);
    return ret;
}

static JSValue tp_duration_total(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_duration_this(ctx, this_val);
    JSValue opts;
    TPRelativeTo rel;
    TPInternalDuration d;
    int unit;
    double total;
    JSValue ret = JS_EXCEPTION;

    if (!o)
        return JS_EXCEPTION;
    if (tp_round_options_from_arg(ctx, tp_arg(0), "unit", &opts))
        return JS_EXCEPTION;
    if (tp_get_relative_to_option(ctx, opts, &rel) || tp_get_unit_option(ctx, opts, "unit", true, &unit) ||
        tp_validate_unit(ctx, unit, TP_GROUP_DATETIME, TP_UNIT_UNSET, TP_UNIT_UNSET))
        goto done;
    if (rel.kind == TP_RELATIVE_ZONED) {
        tp_i128 target;
        tp_to_internal_duration(&o->dur, &d);
        if (tp_add_zoned_datetime(ctx, rel.ns, rel.tz, rel.cal, &d, TP_OVERFLOW_CONSTRAIN, &target) ||
            tp_difference_zoned_datetime_with_total(ctx, rel.ns, target, rel.tz, rel.cal, unit, &total))
            goto done;
    } else if (rel.kind == TP_RELATIVE_PLAIN) {
        TPDateTime start, target;
        if (tp_plain_relative_target(ctx, &o->dur, &rel, &start, &target) ||
            tp_difference_plain_datetime_with_total(ctx, &start, &target, rel.cal, unit, &total))
            goto done;
    } else {
        if (tp_is_calendar_unit(tp_default_largest_unit(&o->dur)) || tp_is_calendar_unit(unit)) {
            JS_ThrowRangeError(ctx, "a starting point is required for years, months, or weeks totals");
            goto done;
        }
        if (tp_to_internal_duration_24h(ctx, &o->dur, &d))
            goto done;
        total = tp_total_time_duration(d.time, unit);
    }
    ret = JS_NewFloat64(ctx, total);
done:
    JS_FreeValue(ctx, opts);
    return ret;
}

static JSValue tp_duration_to_string(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_duration_this(ctx, this_val);
    JSValue opts;
    int digits, mode, smallest;
    TPPrecision p;
    TPBuf b;
    TPDuration rounded;
    TPInternalDuration d;
    JSValue ret = JS_EXCEPTION;

    if (!o)
        return JS_EXCEPTION;
    if (tp_get_options_object(ctx, tp_arg(0), &opts))
        return JS_EXCEPTION;
    if (tp_get_fractional_second_digits_option(ctx, opts, &digits) ||
        tp_get_rounding_mode_option(ctx, opts, TP_ROUND_TRUNC, &mode) ||
        tp_get_unit_option(ctx, opts, "smallestUnit", false, &smallest) ||
        tp_validate_unit(ctx, smallest, TP_GROUP_TIME, TP_UNIT_UNSET, TP_UNIT_UNSET))
        goto done;
    if (smallest == TP_UNIT_HOUR || smallest == TP_UNIT_MINUTE) {
        JS_ThrowRangeError(ctx, "smallestUnit must not be hour or minute");
        goto done;
    }
    p = tp_seconds_precision(smallest, digits);
    tp_buf_init(&b);
    if (p.unit == TP_UNIT_NANOSECOND && p.increment == 1) {
        tp_put_duration(&b, &o->dur, p.precision);
        ret = tp_buf_to_string(ctx, &b);
        goto done;
    }
    tp_to_internal_duration(&o->dur, &d);
    if (tp_round_time_duration(ctx, d.time, p.increment, p.unit, mode, &d.time) ||
        tp_duration_from_internal(ctx, &d, tp_larger_unit(tp_default_largest_unit(&o->dur), TP_UNIT_SECOND),
                                  &rounded))
        goto done;
    tp_put_duration(&b, &rounded, p.precision);
    ret = tp_buf_to_string(ctx, &b);
done:
    JS_FreeValue(ctx, opts);
    return ret;
}

static JSValue tp_duration_to_json(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPObject *o = tp_duration_this(ctx, this_val);
    TPBuf b;
    if (!o)
        return JS_EXCEPTION;
    tp_buf_init(&b);
    tp_put_duration(&b, &o->dur, TP_PRECISION_AUTO);
    return tp_buf_to_string(ctx, &b);
}

static JSValue tp_duration_from(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPDuration d;
    if (tp_to_temporal_duration(ctx, tp_arg(0), &d))
        return JS_EXCEPTION;
    return tp_new_duration(ctx, &d, JS_UNDEFINED);
}

static JSValue tp_duration_compare(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    TPDuration one, two;
    TPRelativeTo rel;
    TPInternalDuration d1, d2;
    JSValue opts;
    int lu1, lu2, r = -2;
    int64_t days1, days2;
    tp_i128 t1, t2;

    if (tp_to_temporal_duration(ctx, tp_arg(0), &one) || tp_to_temporal_duration(ctx, tp_arg(1), &two))
        return JS_EXCEPTION;
    if (tp_get_options_object(ctx, tp_arg(2), &opts))
        return JS_EXCEPTION;
    if (tp_get_relative_to_option(ctx, opts, &rel))
        goto done;
    if (memcmp(&one, &two, sizeof(one)) == 0) {
        r = 0;
        goto done;
    }
    lu1 = tp_default_largest_unit(&one);
    lu2 = tp_default_largest_unit(&two);
    tp_to_internal_duration(&one, &d1);
    tp_to_internal_duration(&two, &d2);
    if (rel.kind == TP_RELATIVE_ZONED &&
        (tp_unit_category(lu1) == TP_GROUP_DATE || tp_unit_category(lu2) == TP_GROUP_DATE)) {
        tp_i128 after1, after2;
        if (tp_add_zoned_datetime(ctx, rel.ns, rel.tz, rel.cal, &d1, TP_OVERFLOW_CONSTRAIN, &after1) ||
            tp_add_zoned_datetime(ctx, rel.ns, rel.tz, rel.cal, &d2, TP_OVERFLOW_CONSTRAIN, &after2))
            goto done;
        r = tp_cmp128(after1, after2);
        goto done;
    }
    if (tp_is_calendar_unit(lu1) || tp_is_calendar_unit(lu2)) {
        if (rel.kind != TP_RELATIVE_PLAIN) {
            JS_ThrowRangeError(ctx, "a starting point is required for comparing years, months, or weeks");
            goto done;
        }
        if (tp_date_duration_days(ctx, &d1.date, &rel, &days1) || tp_date_duration_days(ctx, &d2.date, &rel, &days2))
            goto done;
    } else {
        days1 = d1.date.days;
        days2 = d2.date.days;
    }
    if (tp_time_duration_add_days(ctx, d1.time, days1, &t1) || tp_time_duration_add_days(ctx, d2.time, days2, &t2))
        goto done;
    r = tp_cmp128(t1, t2);
done:
    JS_FreeValue(ctx, opts);
    if (r == -2)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, r);
}

static const JSCFunctionListEntry tp_duration_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("years", tp_duration_get_field, NULL, TP_UNIT_YEAR),
    JS_CGETSET_MAGIC_DEF("months", tp_duration_get_field, NULL, TP_UNIT_MONTH),
    JS_CGETSET_MAGIC_DEF("weeks", tp_duration_get_field, NULL, TP_UNIT_WEEK),
    JS_CGETSET_MAGIC_DEF("days", tp_duration_get_field, NULL, TP_UNIT_DAY),
    JS_CGETSET_MAGIC_DEF("hours", tp_duration_get_field, NULL, TP_UNIT_HOUR),
    JS_CGETSET_MAGIC_DEF("minutes", tp_duration_get_field, NULL, TP_UNIT_MINUTE),
    JS_CGETSET_MAGIC_DEF("seconds", tp_duration_get_field, NULL, TP_UNIT_SECOND),
    JS_CGETSET_MAGIC_DEF("milliseconds", tp_duration_get_field, NULL, TP_UNIT_MILLISECOND),
    JS_CGETSET_MAGIC_DEF("microseconds", tp_duration_get_field, NULL, TP_UNIT_MICROSECOND),
    JS_CGETSET_MAGIC_DEF("nanoseconds", tp_duration_get_field, NULL, TP_UNIT_NANOSECOND),
    JS_CGETSET_DEF("sign", tp_duration_get_sign, NULL),
    JS_CGETSET_DEF("blank", tp_duration_get_blank, NULL),
    JS_CFUNC_DEF("with", 1, tp_duration_with),
    JS_CFUNC_DEF("negated", 0, tp_duration_negated),
    JS_CFUNC_DEF("abs", 0, tp_duration_abs),
    JS_CFUNC_MAGIC_DEF("add", 1, tp_duration_add, 0),
    JS_CFUNC_MAGIC_DEF("subtract", 1, tp_duration_add, 1),
    JS_CFUNC_DEF("round", 1, tp_duration_round),
    JS_CFUNC_DEF("total", 1, tp_duration_total),
    JS_CFUNC_DEF("toString", 0, tp_duration_to_string),
    JS_CFUNC_DEF("toJSON", 0, tp_duration_to_json),
    JS_CFUNC_DEF("toLocaleString", 0, tp_duration_to_json),
    JS_CFUNC_DEF("valueOf", 0, tp_value_of_throws),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Temporal.Duration", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry tp_duration_static_funcs[] = {
    JS_CFUNC_DEF("from", 1, tp_duration_from),
    JS_CFUNC_DEF("compare", 2, tp_duration_compare),
};

int tp_init_duration(JSContext *ctx, JSValue ns)
{
    return tp_define_class(ctx, ns, TP_KIND_DURATION, "Duration", tp_duration_constructor, 0,
                           tp_duration_proto_funcs, countof(tp_duration_proto_funcs), tp_duration_static_funcs,
                           countof(tp_duration_static_funcs));
}
