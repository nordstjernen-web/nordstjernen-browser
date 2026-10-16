/* quickjs-temporal-calendar.c — ISO 8601 date arithmetic, the calendar interface and calendar field records. */
#include <math.h>
#include <string.h>
#include "cutils.h"
#include "quickjs-temporal.h"

static const char *const tp_field_names[TP_F_COUNT] = {
    "era", "eraYear", "year", "month", "monthCode", "day", "hour", "minute", "second", "millisecond",
    "microsecond", "nanosecond", "offset", "timeZone"
};

static const int tp_sorted_fields[TP_F_COUNT] = {
    TP_F_DAY, TP_F_ERA, TP_F_ERA_YEAR, TP_F_HOUR, TP_F_MICROSECOND, TP_F_MILLISECOND, TP_F_MINUTE,
    TP_F_MONTH, TP_F_MONTH_CODE, TP_F_NANOSECOND, TP_F_OFFSET, TP_F_SECOND, TP_F_TIME_ZONE, TP_F_YEAR
};

static const char *const tp_calendar_ids[TP_CAL_COUNT] = {
    "iso8601", "buddhist", "chinese", "coptic", "dangi", "ethioaa", "ethiopic", "gregory", "hebrew", "indian",
    "islamic-civil", "islamic-tbla", "islamic-umalqura", "japanese", "persian", "roc"
};

static const struct {
    const char *alias;
    TPCalendar cal;
} tp_calendar_aliases[] = {
    { "ethiopic-amete-alem", TP_CAL_ETHIOAA },
    { "islamicc", TP_CAL_ISLAMIC_CIVIL },
};

bool tp_is_leap_year(int64_t year)
{
    return (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0);
}

int tp_days_in_month(int64_t year, int month)
{
    static const int dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2)
        return tp_is_leap_year(year) ? 29 : 28;
    return dim[month - 1];
}

int tp_days_in_year(int64_t year)
{
    return tp_is_leap_year(year) ? 366 : 365;
}

static int64_t tp_days_from_year(int64_t y)
{
    return 365 * (y - 1970) + tp_floor_div64(y - 1969, 4) - tp_floor_div64(y - 1901, 100) +
           tp_floor_div64(y - 1601, 400);
}

int64_t tp_epoch_days(int64_t year, int64_t month0, int64_t day)
{
    static const int cumulative[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    int64_t y = year + tp_floor_div64(month0, 12);
    int m = (int)tp_floor_mod64(month0, 12);
    int64_t days = tp_days_from_year(y) + cumulative[m];
    if (m >= 2 && tp_is_leap_year(y))
        days++;
    return days + day - 1;
}

int64_t tp_iso_date_epoch_days(TPISODate d)
{
    return tp_epoch_days(d.year, d.month - 1, d.day);
}

TPISODate tp_epoch_days_to_date(int64_t days)
{
    TPISODate r;
    int64_t z = days + 719468;
    int64_t era = tp_floor_div64(z, 146097);
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int m = (int)(mp < 10 ? mp + 3 : mp - 9);

    r.year = yoe + era * 400 + (m <= 2);
    r.month = m;
    r.day = (int)(doy - (153 * mp + 2) / 5 + 1);
    return r;
}

TPISODate tp_balance_year_month(int64_t year, int64_t month)
{
    TPISODate r;
    r.year = year + tp_floor_div64(month - 1, 12);
    r.month = (int)tp_floor_mod64(month - 1, 12) + 1;
    r.day = 1;
    return r;
}

TPISODate tp_add_days(TPISODate d, int64_t days)
{
    return tp_epoch_days_to_date(tp_iso_date_epoch_days(d) + days);
}

bool tp_is_valid_iso_date(int64_t year, int64_t month, int64_t day)
{
    if (month < 1 || month > 12)
        return false;
    return day >= 1 && day <= tp_days_in_month(year, (int)month);
}

bool tp_is_valid_time(int64_t h, int64_t mi, int64_t s, int64_t ms, int64_t us, int64_t ns)
{
    return h >= 0 && h <= 23 && mi >= 0 && mi <= 59 && s >= 0 && s <= 59 && ms >= 0 && ms <= 999 &&
           us >= 0 && us <= 999 && ns >= 0 && ns <= 999;
}

static int64_t tp_clamp64(int64_t v, int64_t lo, int64_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

int tp_regulate_iso_date(JSContext *ctx, int64_t year, int64_t month, int64_t day, int overflow, TPISODate *out)
{
    if (overflow == TP_OVERFLOW_REJECT) {
        if (!tp_is_valid_iso_date(year, month, day)) {
            JS_ThrowRangeError(ctx, "date is out of range");
            return -1;
        }
    } else {
        month = tp_clamp64(month, 1, 12);
        day = tp_clamp64(day, 1, tp_days_in_month(year, (int)month));
    }
    out->year = year;
    out->month = (int32_t)month;
    out->day = (int32_t)day;
    return 0;
}

int tp_regulate_time(JSContext *ctx, const int64_t v[6], int overflow, TPTime *out)
{
    static const int64_t maxima[6] = { 23, 59, 59, 999, 999, 999 };
    int64_t r[6];
    int i;

    if (overflow == TP_OVERFLOW_REJECT) {
        if (!tp_is_valid_time(v[0], v[1], v[2], v[3], v[4], v[5])) {
            JS_ThrowRangeError(ctx, "time is out of range");
            return -1;
        }
    }
    for (i = 0; i < 6; i++)
        r[i] = tp_clamp64(v[i], 0, maxima[i]);
    out->hour = (int32_t)r[0];
    out->minute = (int32_t)r[1];
    out->second = (int32_t)r[2];
    out->millisecond = (int32_t)r[3];
    out->microsecond = (int32_t)r[4];
    out->nanosecond = (int32_t)r[5];
    return 0;
}

int tp_compare_iso_date(TPISODate a, TPISODate b)
{
    if (a.year != b.year)
        return a.year < b.year ? -1 : 1;
    if (a.month != b.month)
        return a.month < b.month ? -1 : 1;
    if (a.day != b.day)
        return a.day < b.day ? -1 : 1;
    return 0;
}

int tp_compare_time(const TPTime *a, const TPTime *b)
{
    return tp_cmp128(tp_time_to_ns(a), tp_time_to_ns(b));
}

int tp_compare_datetime(const TPDateTime *a, const TPDateTime *b)
{
    int r = tp_compare_iso_date(a->date, b->date);
    return r ? r : tp_compare_time(&a->time, &b->time);
}

bool tp_iso_date_within_limits(TPISODate d)
{
    int64_t days = tp_iso_date_epoch_days(d);
    return days >= -100000001 && days <= 100000000;
}

bool tp_iso_datetime_within_limits(const TPDateTime *dt)
{
    tp_i128 ns;
    int64_t days = tp_iso_date_epoch_days(dt->date);
    if (days < -100000002 || days > 100000002)
        return false;
    ns = tp_utc_epoch_ns(dt);
    return ns > -TP_NS_MAX_INSTANT - TP_NS_PER_DAY && ns < TP_NS_MAX_INSTANT + TP_NS_PER_DAY;
}

bool tp_iso_year_month_within_limits(TPISODate d)
{
    if (d.year < -271821 || d.year > 275760)
        return false;
    if (d.year == -271821 && d.month < 4)
        return false;
    if (d.year == 275760 && d.month > 9)
        return false;
    return true;
}

bool tp_epoch_ns_valid(tp_i128 ns)
{
    return ns >= -TP_NS_MAX_INSTANT && ns <= TP_NS_MAX_INSTANT;
}

int tp_check_iso_days_range(JSContext *ctx, TPISODate d)
{
    int64_t days = tp_iso_date_epoch_days(d);
    if (days > 100000000 || days < -100000000) {
        JS_ThrowRangeError(ctx, "date is outside the supported range");
        return -1;
    }
    return 0;
}

tp_i128 tp_time_to_ns(const TPTime *t)
{
    return ((((tp_i128)t->hour * 60 + t->minute) * 60 + t->second) * 1000000000) +
           (tp_i128)t->millisecond * 1000000 + (tp_i128)t->microsecond * 1000 + t->nanosecond;
}

tp_i128 tp_utc_epoch_ns(const TPDateTime *dt)
{
    return (tp_i128)tp_iso_date_epoch_days(dt->date) * TP_NS_PER_DAY + tp_time_to_ns(&dt->time);
}

int64_t tp_balance_time_ns(tp_i128 ns, TPTime *out)
{
    tp_i128 days = tp_floor_div128(ns, TP_NS_PER_DAY);
    int64_t rem = (int64_t)(ns - days * TP_NS_PER_DAY);

    out->nanosecond = (int32_t)(rem % 1000);
    rem /= 1000;
    out->microsecond = (int32_t)(rem % 1000);
    rem /= 1000;
    out->millisecond = (int32_t)(rem % 1000);
    rem /= 1000;
    out->second = (int32_t)(rem % 60);
    rem /= 60;
    out->minute = (int32_t)(rem % 60);
    out->hour = (int32_t)(rem / 60);
    return (int64_t)days;
}

void tp_epoch_ns_to_datetime(tp_i128 ns, TPDateTime *out)
{
    int64_t days = tp_balance_time_ns(ns, &out->time);
    out->date = tp_epoch_days_to_date(days);
}

TPDateTime tp_balance_datetime(TPISODate d, tp_i128 time_ns)
{
    TPDateTime r;
    int64_t days = tp_balance_time_ns(time_ns, &r.time);
    r.date = tp_add_days(d, days);
    return r;
}

int64_t tp_add_time(const TPTime *t, tp_i128 duration_ns, TPTime *out)
{
    return tp_balance_time_ns(tp_time_to_ns(t) + duration_ns, out);
}

tp_i128 tp_difference_time(const TPTime *a, const TPTime *b)
{
    return tp_time_to_ns(b) - tp_time_to_ns(a);
}

int64_t tp_round_time(const TPTime *t, int64_t increment, int unit, int mode, TPTime *out)
{
    tp_i128 total = tp_time_to_ns(t), prefix = 0, quantity, rounded;

    switch (unit) {
    case TP_UNIT_MINUTE:
        prefix = (tp_i128)t->hour * 3600000000000LL;
        break;
    case TP_UNIT_SECOND:
        prefix = (tp_i128)t->hour * 3600000000000LL + (tp_i128)t->minute * 60000000000LL;
        break;
    case TP_UNIT_MILLISECOND:
        prefix = total - ((tp_i128)t->millisecond * 1000000 + t->microsecond * 1000 + t->nanosecond);
        break;
    case TP_UNIT_MICROSECOND:
        prefix = total - ((tp_i128)t->microsecond * 1000 + t->nanosecond);
        break;
    case TP_UNIT_NANOSECOND:
        prefix = total - t->nanosecond;
        break;
    default:
        break;
    }
    quantity = total - prefix;
    rounded = tp_round_to_increment(quantity, (tp_i128)tp_unit_ns[unit] * increment, mode);
    return tp_balance_time_ns(prefix + rounded, out);
}

TPDateTime tp_round_iso_datetime(const TPDateTime *dt, int64_t increment, int unit, int mode)
{
    TPDateTime r;
    int64_t days = tp_round_time(&dt->time, increment, unit, mode, &r.time);
    r.date = tp_add_days(dt->date, days);
    return r;
}

int tp_iso_day_of_week(TPISODate d)
{
    int dow = (int)tp_floor_mod64(tp_iso_date_epoch_days(d) + 4, 7);
    return dow == 0 ? 7 : dow;
}

int tp_iso_day_of_year(TPISODate d)
{
    return (int)(tp_iso_date_epoch_days(d) - tp_epoch_days(d.year, 0, 1)) + 1;
}

void tp_iso_week_of_year(TPISODate d, int *week, int64_t *year)
{
    int doy = tp_iso_day_of_year(d), dow = tp_iso_day_of_week(d);
    int w = (int)tp_floor_div64(doy + 7 - dow + 3, 7);
    TPISODate jan1;

    if (w < 1) {
        jan1.year = d.year;
        jan1.month = 1;
        jan1.day = 1;
        int jan1_dow = tp_iso_day_of_week(jan1);
        *year = d.year - 1;
        if (jan1_dow == 5 || (jan1_dow == 6 && tp_is_leap_year(d.year - 1)))
            *week = 53;
        else
            *week = 52;
        return;
    }
    if (w == 53 && tp_days_in_year(d.year) - doy < 4 - dow) {
        *week = 1;
        *year = d.year + 1;
        return;
    }
    *week = w;
    *year = d.year;
}

TPTime tp_midnight(void)
{
    TPTime t = { 0, 0, 0, 0, 0, 0 };
    return t;
}

static bool tp_ascii_lower_equals(const char *s, size_t len, const char *id)
{
    size_t i;
    if (strlen(id) != len)
        return false;
    for (i = 0; i < len; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if (c != id[i])
            return false;
    }
    return true;
}

int tp_canonicalize_calendar(JSContext *ctx, const char *s, size_t len, TPCalendar *out)
{
    int i;
    for (i = 0; i < (int)countof(tp_calendar_ids); i++) {
        if (tp_ascii_lower_equals(s, len, tp_calendar_ids[i])) {
            *out = i;
            return 0;
        }
    }
    for (i = 0; i < (int)countof(tp_calendar_aliases); i++) {
        if (tp_ascii_lower_equals(s, len, tp_calendar_aliases[i].alias)) {
            *out = tp_calendar_aliases[i].cal;
            return 0;
        }
    }
    JS_ThrowRangeError(ctx, "unsupported calendar: %.*s", (int)(len > 64 ? 64 : len), s);
    return -1;
}

const char *tp_calendar_id(TPCalendar cal)
{
    return tp_calendar_ids[cal];
}

bool tp_calendar_equals(TPCalendar a, TPCalendar b)
{
    return a == b;
}

static TPObject *tp_calendar_holder(JSValueConst v)
{
    static const int kinds[] = { TP_KIND_DATE, TP_KIND_DATETIME, TP_KIND_MONTH_DAY, TP_KIND_YEAR_MONTH,
                                 TP_KIND_ZONED };
    int i;
    for (i = 0; i < (int)countof(kinds); i++) {
        TPObject *o = tp_get(v, kinds[i]);
        if (o)
            return o;
    }
    return NULL;
}

static int tp_parse_calendar_string(JSContext *ctx, const char *s, size_t len, TPCalendar *out)
{
    static const int goals[] = { TP_GOAL_DATETIME_ZONED, TP_GOAL_DATETIME, TP_GOAL_INSTANT, TP_GOAL_TIME,
                                 TP_GOAL_MONTH_DAY, TP_GOAL_YEAR_MONTH, TP_GOAL_END };
    TPParsed p;

    if (tp_parse_iso_datetime(NULL, s, len, goals, &p) == 0) {
        if (p.cal_len == 0)
            return tp_canonicalize_calendar(ctx, "iso8601", 7, out);
        return tp_canonicalize_calendar(ctx, s + p.cal_start, p.cal_len, out);
    }
    if (!tp_parse_annotation_value(s, len)) {
        JS_ThrowRangeError(ctx, "invalid calendar: %.*s", (int)(len > 64 ? 64 : len), s);
        return -1;
    }
    return tp_canonicalize_calendar(ctx, s, len, out);
}

int tp_to_calendar_identifier(JSContext *ctx, JSValueConst v, TPCalendar *out)
{
    TPObject *o = tp_calendar_holder(v);
    const char *s;
    size_t len;
    int r;

    if (o) {
        *out = o->cal;
        return 0;
    }
    if (tp_require_string(ctx, v))
        return -1;
    s = JS_ToCStringLen(ctx, &len, v);
    if (!s)
        return -1;
    r = tp_parse_calendar_string(ctx, s, len, out);
    JS_FreeCString(ctx, s);
    return r;
}

int tp_get_calendar_with_iso_default(JSContext *ctx, JSValueConst item, TPCalendar *out)
{
    TPObject *o = tp_calendar_holder(item);
    JSValue v;
    int r;

    if (o) {
        *out = o->cal;
        return 0;
    }
    v = JS_GetPropertyStr(ctx, item, "calendar");
    if (JS_IsException(v))
        return -1;
    if (JS_IsUndefined(v)) {
        *out = TP_CAL_ISO8601;
        return 0;
    }
    r = tp_to_calendar_identifier(ctx, v, out);
    JS_FreeValue(ctx, v);
    return r;
}

void tp_calendar_iso_to_date(TPCalendar cal, TPISODate d, TPCalendarDate *out)
{
    int week;
    int64_t week_year;

    if (cal != TP_CAL_ISO8601) {
        tp_ncal_iso_to_date(cal, d, out);
        return;
    }
    memset(out, 0, sizeof(*out));
    out->has_era = false;
    out->year = d.year;
    out->month = d.month;
    out->month_code = d.month;
    out->month_code_leap = false;
    out->day = d.day;
    out->day_of_week = tp_iso_day_of_week(d);
    out->day_of_year = tp_iso_day_of_year(d);
    tp_iso_week_of_year(d, &week, &week_year);
    out->has_week_of_year = true;
    out->week_of_year = week;
    out->year_of_week = week_year;
    out->days_in_week = 7;
    out->days_in_month = tp_days_in_month(d.year, d.month);
    out->days_in_year = tp_days_in_year(d.year);
    out->months_in_year = 12;
    out->in_leap_year = tp_is_leap_year(d.year);
}

int tp_calendar_date_add(JSContext *ctx, TPCalendar cal, TPISODate d, const TPDateDuration *dur,
                         int overflow, TPISODate *out)
{
    TPISODate intermediate = tp_balance_year_month(d.year + dur->years, (int64_t)d.month + dur->months);
    TPISODate regulated;

    if (cal != TP_CAL_ISO8601)
        return tp_ncal_date_add(ctx, cal, d, dur, overflow, out);
    if (tp_regulate_iso_date(ctx, intermediate.year, intermediate.month, d.day, overflow, &regulated))
        return -1;
    *out = tp_add_days(regulated, dur->days + 7 * dur->weeks);
    if (!tp_iso_date_within_limits(*out)) {
        JS_ThrowRangeError(ctx, "date is outside the supported range");
        return -1;
    }
    return 0;
}

static bool tp_compare_surpasses(int sign, int64_t year, int64_t month, int64_t day, TPISODate target)
{
    if (year != target.year)
        return sign * (year - target.year) > 0;
    if (month != target.month)
        return sign * (month - target.month) > 0;
    if (day != target.day)
        return sign * (day - target.day) > 0;
    return false;
}

static bool tp_iso_date_surpasses(int sign, TPISODate base, TPISODate target, int64_t years, int64_t months)
{
    int64_t y0 = base.year + years;
    TPISODate added;
    if (tp_compare_surpasses(sign, y0, base.month, base.day, target))
        return true;
    if (months == 0)
        return false;
    added = tp_balance_year_month(y0, base.month + months);
    return tp_compare_surpasses(sign, added.year, added.month, base.day, target);
}

void tp_calendar_date_until(TPCalendar cal, TPISODate one, TPISODate two, int largest_unit,
                            TPDateDuration *out)
{
    int sign = -tp_compare_iso_date(one, two);
    int64_t years = 0, months = 0, days, candidate;
    TPISODate intermediate;

    if (cal != TP_CAL_ISO8601) {
        tp_ncal_date_until(cal, one, two, largest_unit, out);
        return;
    }
    memset(out, 0, sizeof(*out));
    if (sign == 0)
        return;
    if (largest_unit == TP_UNIT_YEAR) {
        candidate = two.year - one.year;
        if (candidate != 0)
            candidate -= sign;
        while (!tp_iso_date_surpasses(sign, one, two, candidate, 0)) {
            years = candidate;
            candidate += sign;
        }
    }
    if (largest_unit == TP_UNIT_YEAR || largest_unit == TP_UNIT_MONTH) {
        if (largest_unit == TP_UNIT_MONTH) {
            candidate = (two.year - one.year) * 12 + (two.month - one.month);
            if (candidate != 0)
                candidate -= sign;
        } else {
            candidate = sign;
        }
        while (!tp_iso_date_surpasses(sign, one, two, years, candidate)) {
            months = candidate;
            candidate += sign;
        }
    }
    intermediate = tp_balance_year_month(one.year + years, one.month + months);
    intermediate.day = one.day;
    if (intermediate.day > tp_days_in_month(intermediate.year, intermediate.month))
        intermediate.day = tp_days_in_month(intermediate.year, intermediate.month);
    days = tp_iso_date_epoch_days(two) - tp_iso_date_epoch_days(intermediate);
    out->years = years;
    out->months = months;
    if (largest_unit == TP_UNIT_WEEK) {
        out->weeks = days / 7;
        days %= 7;
    }
    out->days = days;
}

int tp_reject_temporal_like_object(JSContext *ctx, JSValueConst v)
{
    static const int kinds[] = { TP_KIND_DATE, TP_KIND_DATETIME, TP_KIND_MONTH_DAY, TP_KIND_TIME,
                                 TP_KIND_YEAR_MONTH, TP_KIND_ZONED };
    JSValue p;
    int i;
    bool defined;

    if (!JS_IsObject(v)) {
        JS_ThrowTypeError(ctx, "argument must be an object");
        return -1;
    }
    for (i = 0; i < (int)countof(kinds); i++) {
        if (tp_get(v, kinds[i])) {
            JS_ThrowTypeError(ctx, "argument must be a partial object, not a Temporal object");
            return -1;
        }
    }
    p = JS_GetPropertyStr(ctx, v, "calendar");
    if (JS_IsException(p))
        return -1;
    defined = !JS_IsUndefined(p);
    JS_FreeValue(ctx, p);
    if (defined) {
        JS_ThrowTypeError(ctx, "argument must not have a calendar property");
        return -1;
    }
    p = JS_GetPropertyStr(ctx, v, "timeZone");
    if (JS_IsException(p))
        return -1;
    defined = !JS_IsUndefined(p);
    JS_FreeValue(ctx, p);
    if (defined) {
        JS_ThrowTypeError(ctx, "argument must not have a timeZone property");
        return -1;
    }
    return 0;
}

static uint32_t tp_calendar_extra_fields(TPCalendar cal, uint32_t fields)
{
    if ((fields & TP_FB(TP_F_YEAR)) && tp_ncal_supports_era(cal))
        return TP_FB(TP_F_ERA) | TP_FB(TP_F_ERA_YEAR);
    return 0;
}

static int tp_convert_field(JSContext *ctx, int field, JSValueConst value, TPFields *out)
{
    double d;
    int num;
    bool leap;

    switch (field) {
    case TP_F_ERA: {
        size_t len;
        const char *s;
        JSValue str = JS_ToString(ctx, value);
        if (JS_IsException(str))
            return -1;
        s = JS_ToCStringLen(ctx, &len, str);
        JS_FreeValue(ctx, str);
        if (!s)
            return -1;
        if (len >= sizeof(out->era))
            len = sizeof(out->era) - 1;
        memcpy(out->era, s, len);
        out->era[len] = '\0';
        JS_FreeCString(ctx, s);
        return 0;
    }
    case TP_F_MONTH:
    case TP_F_DAY:
        if (tp_to_positive_integer_with_truncation(ctx, value, &d))
            return -1;
        out->v[field] = tp_clamp_double(d);
        return 0;
    case TP_F_MONTH_CODE:
        if (tp_parse_month_code(ctx, value, &num, &leap))
            return -1;
        out->v[field] = num;
        out->month_code_leap = leap;
        return 0;
    case TP_F_OFFSET:
        return tp_parse_offset_string_value(ctx, value, &out->offset_ns);
    case TP_F_TIME_ZONE:
        return tp_to_time_zone_identifier(ctx, value, &out->tz);
    default:
        if (tp_to_integer_with_truncation(ctx, value, &d))
            return -1;
        out->v[field] = tp_clamp_double(d);
        return 0;
    }
}

int tp_prepare_calendar_fields(JSContext *ctx, TPCalendar cal, JSValueConst obj, uint32_t cal_fields,
                               uint32_t noncal_fields, uint32_t required, bool partial, TPFields *out)
{
    uint32_t names = cal_fields | noncal_fields | tp_calendar_extra_fields(cal, cal_fields);
    bool any = false;
    int i;

    memset(out, 0, sizeof(*out));
    for (i = 0; i < TP_F_COUNT; i++) {
        int field = tp_sorted_fields[i];
        JSValue value;
        int r;

        if (!(names & TP_FB(field)))
            continue;
        value = JS_GetPropertyStr(ctx, obj, tp_field_names[field]);
        if (JS_IsException(value))
            return -1;
        if (!JS_IsUndefined(value)) {
            any = true;
            r = tp_convert_field(ctx, field, value, out);
            JS_FreeValue(ctx, value);
            if (r)
                return -1;
            out->has |= TP_FB(field);
        } else if (!partial) {
            if (required & TP_FB(field)) {
                JS_ThrowTypeError(ctx, "required property '%s' is missing or undefined", tp_field_names[field]);
                return -1;
            }
            if (TP_FM_TIME & TP_FB(field)) {
                out->v[field] = 0;
                out->has |= TP_FB(field);
            }
        }
    }
    if (partial && !any) {
        JS_ThrowTypeError(ctx, "object must have at least one recognized property");
        return -1;
    }
    return 0;
}

static uint32_t tp_calendar_field_keys_to_ignore(TPCalendar cal, uint32_t keys)
{
    uint32_t ignored = keys;
    if (cal != TP_CAL_ISO8601)
        return tp_ncal_field_keys_to_ignore(cal, keys);
    if (keys & TP_FB(TP_F_MONTH))
        ignored |= TP_FB(TP_F_MONTH_CODE);
    if (keys & TP_FB(TP_F_MONTH_CODE))
        ignored |= TP_FB(TP_F_MONTH);
    return ignored;
}

static void tp_copy_field(TPFields *dst, const TPFields *src, int field)
{
    dst->has |= TP_FB(field);
    dst->v[field] = src->v[field];
    if (field == TP_F_MONTH_CODE)
        dst->month_code_leap = src->month_code_leap;
    else if (field == TP_F_ERA)
        memcpy(dst->era, src->era, sizeof(dst->era));
    else if (field == TP_F_OFFSET)
        dst->offset_ns = src->offset_ns;
    else if (field == TP_F_TIME_ZONE)
        dst->tz = src->tz;
}

void tp_calendar_merge_fields(TPCalendar cal, const TPFields *fields, const TPFields *additional, TPFields *out)
{
    uint32_t overridden = tp_calendar_field_keys_to_ignore(cal, additional->has);
    TPFields merged;
    int i;

    memset(&merged, 0, sizeof(merged));
    for (i = 0; i < TP_F_COUNT; i++) {
        if ((fields->has & TP_FB(i)) && !(overridden & TP_FB(i)))
            tp_copy_field(&merged, fields, i);
        if (additional->has & TP_FB(i))
            tp_copy_field(&merged, additional, i);
    }
    *out = merged;
}

void tp_iso_date_to_fields(TPCalendar cal, TPISODate d, int type, TPFields *out)
{
    TPCalendarDate cd;
    tp_calendar_iso_to_date(cal, d, &cd);
    memset(out, 0, sizeof(*out));
    out->has = TP_FB(TP_F_MONTH_CODE);
    out->v[TP_F_MONTH_CODE] = cd.month_code;
    out->month_code_leap = cd.month_code_leap;
    if (type == TP_FIELDS_MONTH_DAY || type == TP_FIELDS_DATE) {
        out->has |= TP_FB(TP_F_DAY);
        out->v[TP_F_DAY] = cd.day;
    }
    if (type == TP_FIELDS_YEAR_MONTH || type == TP_FIELDS_DATE) {
        out->has |= TP_FB(TP_F_YEAR);
        out->v[TP_F_YEAR] = cd.year;
    }
}

void tp_fields_set_time(TPFields *f, const TPTime *t)
{
    f->has |= TP_FM_TIME;
    f->v[TP_F_HOUR] = t->hour;
    f->v[TP_F_MINUTE] = t->minute;
    f->v[TP_F_SECOND] = t->second;
    f->v[TP_F_MILLISECOND] = t->millisecond;
    f->v[TP_F_MICROSECOND] = t->microsecond;
    f->v[TP_F_NANOSECOND] = t->nanosecond;
}

static int tp_calendar_resolve_fields(JSContext *ctx, TPCalendar cal, TPFields *f, int type)
{
    bool needs_year = type == TP_FIELDS_DATE || type == TP_FIELDS_YEAR_MONTH;
    bool needs_day = type == TP_FIELDS_DATE || type == TP_FIELDS_MONTH_DAY;

    if (cal != TP_CAL_ISO8601)
        return tp_ncal_resolve_fields(ctx, cal, f, type);
    if (needs_year && !(f->has & TP_FB(TP_F_YEAR))) {
        JS_ThrowTypeError(ctx, "year is required");
        return -1;
    }
    if (needs_day && !(f->has & TP_FB(TP_F_DAY))) {
        JS_ThrowTypeError(ctx, "day is required");
        return -1;
    }
    if (!(f->has & (TP_FB(TP_F_MONTH) | TP_FB(TP_F_MONTH_CODE)))) {
        JS_ThrowTypeError(ctx, "month or monthCode is required");
        return -1;
    }
    if (f->has & TP_FB(TP_F_MONTH_CODE)) {
        int64_t month = f->v[TP_F_MONTH_CODE];
        if (f->month_code_leap || month > 12) {
            JS_ThrowRangeError(ctx, "invalid monthCode for the ISO 8601 calendar");
            return -1;
        }
        if ((f->has & TP_FB(TP_F_MONTH)) && f->v[TP_F_MONTH] != month) {
            JS_ThrowRangeError(ctx, "month and monthCode do not agree");
            return -1;
        }
        f->has |= TP_FB(TP_F_MONTH);
        f->v[TP_F_MONTH] = month;
    }
    return 0;
}

int tp_calendar_date_from_fields(JSContext *ctx, TPCalendar cal, TPFields *fields, int overflow, TPISODate *out)
{
    if (tp_calendar_resolve_fields(ctx, cal, fields, TP_FIELDS_DATE))
        return -1;
    if (cal != TP_CAL_ISO8601) {
        if (tp_ncal_date_to_iso(ctx, cal, fields, overflow, out))
            return -1;
    } else if (tp_regulate_iso_date(ctx, fields->v[TP_F_YEAR], fields->v[TP_F_MONTH], fields->v[TP_F_DAY], overflow, out))
        return -1;
    if (!tp_iso_date_within_limits(*out)) {
        JS_ThrowRangeError(ctx, "date is outside the supported range");
        return -1;
    }
    return 0;
}

int tp_calendar_year_month_from_fields(JSContext *ctx, TPCalendar cal, TPFields *fields, int overflow,
                                       TPISODate *out)
{
    fields->has |= TP_FB(TP_F_DAY);
    fields->v[TP_F_DAY] = 1;
    if (tp_calendar_resolve_fields(ctx, cal, fields, TP_FIELDS_YEAR_MONTH))
        return -1;
    if (cal != TP_CAL_ISO8601) {
        if (tp_ncal_date_to_iso(ctx, cal, fields, overflow, out))
            return -1;
    } else if (tp_regulate_iso_date(ctx, fields->v[TP_F_YEAR], fields->v[TP_F_MONTH], fields->v[TP_F_DAY], overflow, out))
        return -1;
    if (!tp_iso_year_month_within_limits(*out)) {
        JS_ThrowRangeError(ctx, "year-month is outside the supported range");
        return -1;
    }
    return 0;
}

int tp_calendar_month_day_from_fields(JSContext *ctx, TPCalendar cal, TPFields *fields, int overflow,
                                      TPISODate *out)
{
    int64_t year;
    TPISODate r;

    if (tp_calendar_resolve_fields(ctx, cal, fields, TP_FIELDS_MONTH_DAY))
        return -1;
    if (cal != TP_CAL_ISO8601)
        return tp_ncal_month_day_to_iso(ctx, cal, fields, overflow, out);
    year = (fields->has & TP_FB(TP_F_YEAR)) ? fields->v[TP_F_YEAR] : 1972;
    if (tp_regulate_iso_date(ctx, year, fields->v[TP_F_MONTH], fields->v[TP_F_DAY], overflow, &r))
        return -1;
    out->year = 1972;
    out->month = r.month;
    out->day = r.day;
    return 0;
}

int tp_interpret_datetime_fields(JSContext *ctx, TPCalendar cal, TPFields *fields, int overflow, TPDateTime *out)
{
    int64_t t[6];
    int i;

    if (tp_calendar_date_from_fields(ctx, cal, fields, overflow, &out->date))
        return -1;
    for (i = 0; i < 6; i++)
        t[i] = fields->v[TP_F_HOUR + i];
    return tp_regulate_time(ctx, t, overflow, &out->time);
}

JSValue tp_calendar_month_code_string(JSContext *ctx, int month, bool leap)
{
    char buf[8];
    buf[0] = 'M';
    buf[1] = (char)('0' + month / 10);
    buf[2] = (char)('0' + month % 10);
    if (leap) {
        buf[3] = 'L';
        return JS_NewStringLen(ctx, buf, 4);
    }
    return JS_NewStringLen(ctx, buf, 3);
}

JSValue tp_date_field_value(JSContext *ctx, TPCalendar cal, TPISODate d, int field)
{
    TPCalendarDate cd;

    if (field == TP_DF_CALENDAR_ID)
        return JS_NewString(ctx, tp_calendar_id(cal));
    tp_calendar_iso_to_date(cal, d, &cd);
    switch (field) {
    case TP_DF_ERA:
        return cd.has_era ? JS_NewString(ctx, cd.era) : JS_UNDEFINED;
    case TP_DF_ERA_YEAR:
        return cd.has_era ? JS_NewInt64(ctx, cd.era_year) : JS_UNDEFINED;
    case TP_DF_YEAR:
        return JS_NewInt64(ctx, cd.year);
    case TP_DF_MONTH:
        return JS_NewInt32(ctx, cd.month);
    case TP_DF_MONTH_CODE:
        return tp_calendar_month_code_string(ctx, cd.month_code, cd.month_code_leap);
    case TP_DF_DAY:
        return JS_NewInt32(ctx, cd.day);
    case TP_DF_DAY_OF_WEEK:
        return JS_NewInt32(ctx, cd.day_of_week);
    case TP_DF_DAY_OF_YEAR:
        return JS_NewInt32(ctx, cd.day_of_year);
    case TP_DF_WEEK_OF_YEAR:
        return cd.has_week_of_year ? JS_NewInt32(ctx, cd.week_of_year) : JS_UNDEFINED;
    case TP_DF_YEAR_OF_WEEK:
        return cd.has_week_of_year ? JS_NewInt64(ctx, cd.year_of_week) : JS_UNDEFINED;
    case TP_DF_DAYS_IN_WEEK:
        return JS_NewInt32(ctx, cd.days_in_week);
    case TP_DF_DAYS_IN_MONTH:
        return JS_NewInt32(ctx, cd.days_in_month);
    case TP_DF_DAYS_IN_YEAR:
        return JS_NewInt32(ctx, cd.days_in_year);
    case TP_DF_MONTHS_IN_YEAR:
        return JS_NewInt32(ctx, cd.months_in_year);
    default:
        return JS_NewBool(ctx, cd.in_leap_year);
    }
}

JSValue tp_time_field_value(JSContext *ctx, const TPTime *t, int field)
{
    switch (field) {
    case TP_DF_HOUR:
        return JS_NewInt32(ctx, t->hour);
    case TP_DF_MINUTE:
        return JS_NewInt32(ctx, t->minute);
    case TP_DF_SECOND:
        return JS_NewInt32(ctx, t->second);
    case TP_DF_MILLISECOND:
        return JS_NewInt32(ctx, t->millisecond);
    case TP_DF_MICROSECOND:
        return JS_NewInt32(ctx, t->microsecond);
    default:
        return JS_NewInt32(ctx, t->nanosecond);
    }
}
