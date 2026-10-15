/* quickjs-temporal-parse.c — RFC 9557 / ISO 8601 date-time, duration, offset and month-code parsing. */
#include <string.h>
#include "cutils.h"
#include "dtoa.h"
#include "quickjs-temporal.h"

typedef struct TPP {
    const char *s;
    size_t len;
    size_t pos;
} TPP;

typedef struct TPPTime {
    int hour, minute, second;
    bool has_minute, has_second;
    int64_t fraction_ns;
} TPPTime;

typedef struct TPPOffset {
    int64_t ns;
    bool has_seconds;
} TPPOffset;

static bool pp_eof(TPP *p)
{
    return p->pos >= p->len;
}

static int pp_peek(TPP *p)
{
    return p->pos < p->len ? (unsigned char)p->s[p->pos] : -1;
}

static int pp_peek_at(TPP *p, size_t k)
{
    return p->pos + k < p->len ? (unsigned char)p->s[p->pos + k] : -1;
}

static bool pp_is_digit(int c)
{
    return c >= '0' && c <= '9';
}

static bool pp_is_alpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool pp_char(TPP *p, int c)
{
    if (pp_peek(p) == c) {
        p->pos++;
        return true;
    }
    return false;
}

static bool pp_char_ci(TPP *p, int c)
{
    int x = pp_peek(p);
    if (x == c || x == c + 32) {
        p->pos++;
        return true;
    }
    return false;
}

static bool pp_digits(TPP *p, int n, int *out)
{
    int v = 0, i;
    for (i = 0; i < n; i++) {
        int c = pp_peek_at(p, (size_t)i);
        if (!pp_is_digit(c))
            return false;
        v = v * 10 + (c - '0');
    }
    p->pos += (size_t)n;
    *out = v;
    return true;
}

static bool pp_two_digit_range(TPP *p, int lo, int hi, int *out)
{
    size_t save = p->pos;
    if (!pp_digits(p, 2, out))
        return false;
    if (*out < lo || *out > hi) {
        p->pos = save;
        return false;
    }
    return true;
}

static bool pp_year(TPP *p, int64_t *year)
{
    size_t save = p->pos;
    int c = pp_peek(p), v;

    if (c == '+' || c == '-') {
        p->pos++;
        if (!pp_digits(p, 6, &v)) {
            p->pos = save;
            return false;
        }
        if (c == '-' && v == 0) {
            p->pos = save;
            return false;
        }
        *year = c == '-' ? -(int64_t)v : v;
        return true;
    }
    if (!pp_digits(p, 4, &v))
        return false;
    *year = v;
    return true;
}

static bool pp_fraction(TPP *p, int64_t *ns)
{
    size_t save = p->pos;
    int digits = 0;
    int64_t v = 0;

    if (!pp_char(p, '.') && !pp_char(p, ','))
        return false;
    while (pp_is_digit(pp_peek(p)) && digits < 10) {
        v = v * 10 + (pp_peek(p) - '0');
        p->pos++;
        digits++;
    }
    if (digits == 0 || digits > 9) {
        p->pos = save;
        return false;
    }
    while (digits < 9) {
        v *= 10;
        digits++;
    }
    *ns = v;
    return true;
}

static bool pp_fraction_scaled(TPP *p, int64_t *value, int *scale)
{
    size_t save = p->pos;
    int digits = 0;
    int64_t v = 0;

    if (!pp_char(p, '.') && !pp_char(p, ','))
        return false;
    while (pp_is_digit(pp_peek(p)) && digits < 10) {
        v = v * 10 + (pp_peek(p) - '0');
        p->pos++;
        digits++;
    }
    if (digits == 0 || digits > 9) {
        p->pos = save;
        return false;
    }
    *value = v;
    *scale = digits;
    return true;
}

static bool pp_date_spec(TPP *p, TPParsed *out)
{
    size_t save = p->pos;
    int64_t year;
    int month, day;
    bool extended;

    if (!pp_year(p, &year))
        return false;
    extended = pp_char(p, '-');
    if (!pp_two_digit_range(p, 1, 12, &month))
        goto fail;
    if (extended && !pp_char(p, '-'))
        goto fail;
    if (!pp_two_digit_range(p, 1, 31, &day))
        goto fail;
    if (!tp_is_valid_iso_date(year, month, day))
        goto fail;
    out->has_year = true;
    out->year = year;
    out->month = month;
    out->day = day;
    return true;
fail:
    p->pos = save;
    return false;
}

static bool pp_valid_month_day(int month, int day)
{
    if (day == 31 && (month == 2 || month == 4 || month == 6 || month == 9 || month == 11))
        return false;
    if (month == 2 && day == 30)
        return false;
    return true;
}

static bool pp_date_spec_month_day(TPP *p, TPParsed *out)
{
    size_t save = p->pos;
    int month, day;

    if (pp_peek(p) == '-' && pp_peek_at(p, 1) == '-')
        p->pos += 2;
    if (!pp_two_digit_range(p, 1, 12, &month))
        goto fail;
    pp_char(p, '-');
    if (!pp_two_digit_range(p, 1, 31, &day))
        goto fail;
    if (!pp_valid_month_day(month, day))
        goto fail;
    out->has_year = false;
    out->month = month;
    out->day = day;
    return true;
fail:
    p->pos = save;
    return false;
}

static bool pp_date_spec_year_month(TPP *p, TPParsed *out)
{
    size_t save = p->pos;
    int64_t year;
    int month;

    if (!pp_year(p, &year))
        return false;
    pp_char(p, '-');
    if (!pp_two_digit_range(p, 1, 12, &month)) {
        p->pos = save;
        return false;
    }
    out->has_year = true;
    out->year = year;
    out->month = month;
    out->day = 1;
    return true;
}

static bool pp_time_spec(TPP *p, TPPTime *t)
{
    size_t save = p->pos;
    bool extended;

    memset(t, 0, sizeof(*t));
    if (!pp_two_digit_range(p, 0, 23, &t->hour))
        return false;
    extended = pp_peek(p) == ':';
    if (extended) {
        p->pos++;
        if (!pp_two_digit_range(p, 0, 59, &t->minute)) {
            p->pos = save;
            return false;
        }
        t->has_minute = true;
        if (pp_peek(p) != ':')
            return true;
        p->pos++;
        if (!pp_two_digit_range(p, 0, 60, &t->second)) {
            p->pos = save;
            return false;
        }
    } else {
        if (!pp_two_digit_range(p, 0, 59, &t->minute))
            return true;
        t->has_minute = true;
        if (!pp_two_digit_range(p, 0, 60, &t->second))
            return true;
    }
    t->has_second = true;
    pp_fraction(p, &t->fraction_ns);
    return true;
}

static bool pp_utc_offset(TPP *p, bool sub_minute, TPPOffset *out)
{
    size_t save = p->pos;
    int c = pp_peek(p), hour, minute = 0, second = 0;
    int64_t fraction = 0;
    bool extended;

    out->has_seconds = false;
    if (c != '+' && c != '-')
        return false;
    p->pos++;
    if (!pp_two_digit_range(p, 0, 23, &hour)) {
        p->pos = save;
        return false;
    }
    extended = pp_peek(p) == ':';
    if (extended) {
        p->pos++;
        if (!pp_two_digit_range(p, 0, 59, &minute)) {
            p->pos = save;
            return false;
        }
        if (sub_minute && pp_peek(p) == ':') {
            size_t before = p->pos;
            p->pos++;
            if (pp_two_digit_range(p, 0, 59, &second)) {
                out->has_seconds = true;
                pp_fraction(p, &fraction);
            } else {
                p->pos = before;
            }
        }
    } else if (pp_two_digit_range(p, 0, 59, &minute)) {
        if (sub_minute && pp_two_digit_range(p, 0, 59, &second)) {
            out->has_seconds = true;
            pp_fraction(p, &fraction);
        }
    }
    out->ns = (((int64_t)hour * 60 + minute) * 60 + second) * 1000000000LL + fraction;
    if (c == '-')
        out->ns = -out->ns;
    return true;
}

static bool pp_tz_leading(int c)
{
    return pp_is_alpha(c) || c == '.' || c == '_';
}

static bool pp_tz_char(int c)
{
    return pp_tz_leading(c) || pp_is_digit(c) || c == '-' || c == '+';
}

static bool pp_iana_name(TPP *p)
{
    size_t save = p->pos;
    for (;;) {
        if (!pp_tz_leading(pp_peek(p))) {
            p->pos = save;
            return false;
        }
        p->pos++;
        while (pp_tz_char(pp_peek(p)))
            p->pos++;
        if (pp_peek(p) != '/')
            return true;
        p->pos++;
    }
}

static bool pp_time_zone_identifier(TPP *p, TPTimeZoneParse *out)
{
    size_t start = p->pos;
    TPPOffset off;

    if (pp_utc_offset(p, false, &off)) {
        out->is_offset = true;
        out->offset_minutes = (int32_t)(off.ns / 60000000000LL);
        out->name_start = start;
        out->name_len = p->pos - start;
        return true;
    }
    if (pp_iana_name(p)) {
        out->is_offset = false;
        out->offset_minutes = 0;
        out->name_start = start;
        out->name_len = p->pos - start;
        return true;
    }
    return false;
}

static bool pp_tz_annotation(TPP *p, TPParsed *out)
{
    size_t save = p->pos;
    TPTimeZoneParse tz;

    if (!pp_char(p, '['))
        return false;
    pp_char(p, '!');
    if (!pp_time_zone_identifier(p, &tz) || !pp_char(p, ']')) {
        p->pos = save;
        return false;
    }
    out->tz_start = tz.name_start;
    out->tz_len = tz.name_len;
    return true;
}

static bool pp_annotation_value_component(TPP *p)
{
    size_t start = p->pos;
    while (pp_is_alpha(pp_peek(p)) || pp_is_digit(pp_peek(p)))
        p->pos++;
    return p->pos > start;
}

static bool pp_annotation_value(TPP *p)
{
    size_t save = p->pos;
    if (!pp_annotation_value_component(p))
        return false;
    while (pp_peek(p) == '-') {
        p->pos++;
        if (!pp_annotation_value_component(p)) {
            p->pos = save;
            return false;
        }
    }
    return true;
}

typedef struct TPPAnnotation {
    bool critical;
    size_t key_start, key_len, value_start, value_len;
} TPPAnnotation;

static bool pp_annotation(TPP *p, TPPAnnotation *a)
{
    size_t save = p->pos;
    int c;

    if (!pp_char(p, '['))
        return false;
    a->critical = pp_char(p, '!');
    a->key_start = p->pos;
    c = pp_peek(p);
    if (!((c >= 'a' && c <= 'z') || c == '_'))
        goto fail;
    p->pos++;
    for (;;) {
        c = pp_peek(p);
        if ((c >= 'a' && c <= 'z') || c == '_' || pp_is_digit(c) || c == '-')
            p->pos++;
        else
            break;
    }
    a->key_len = p->pos - a->key_start;
    if (!pp_char(p, '='))
        goto fail;
    a->value_start = p->pos;
    if (!pp_annotation_value(p))
        goto fail;
    a->value_len = p->pos - a->value_start;
    if (!pp_char(p, ']'))
        goto fail;
    return true;
fail:
    p->pos = save;
    return false;
}

static bool pp_annotations_to_end(TPP *p, TPParsed *out, bool *critical_error)
{
    bool cal_critical = false, have_cal = false;
    TPPAnnotation a;

    *critical_error = false;
    while (pp_annotation(p, &a)) {
        if (a.key_len == 4 && memcmp(p->s + a.key_start, "u-ca", 4) == 0) {
            if (!have_cal) {
                have_cal = true;
                out->cal_start = a.value_start;
                out->cal_len = a.value_len;
                cal_critical = a.critical;
            } else if (a.critical || cal_critical) {
                *critical_error = true;
            }
        } else if (a.critical) {
            *critical_error = true;
        }
    }
    return pp_eof(p);
}

static void pp_reset_parsed(TPParsed *out)
{
    memset(out, 0, sizeof(*out));
}

static void pp_store_time(TPParsed *out, const TPPTime *t)
{
    out->has_time = true;
    out->time.hour = t->hour;
    out->time.minute = t->minute;
    out->time.second = t->second == 60 ? 59 : t->second;
    out->time.millisecond = (int32_t)(t->fraction_ns / 1000000);
    out->time.microsecond = (int32_t)(t->fraction_ns / 1000 % 1000);
    out->time.nanosecond = (int32_t)(t->fraction_ns % 1000);
}

static bool pp_date_time_offset(TPP *p, bool allow_z, bool require, TPParsed *out)
{
    TPPOffset off;
    if (allow_z && (pp_char(p, 'Z') || pp_char(p, 'z'))) {
        out->z = true;
        return true;
    }
    if (pp_utc_offset(p, true, &off)) {
        out->has_offset = true;
        out->offset_ns = off.ns;
        out->offset_sub_minute = off.has_seconds;
        return true;
    }
    return !require;
}

static bool pp_tail(TPP *p, bool require_tz, TPParsed *out, bool *critical_error)
{
    if (!pp_tz_annotation(p, out) && require_tz)
        return false;
    return pp_annotations_to_end(p, out, critical_error);
}

static bool pp_date_time(TPP *p, bool zoned, bool time_required, bool instant, TPParsed *out, bool *crit)
{
    TPPTime t;
    int c;

    if (!pp_date_spec(p, out))
        return false;
    c = pp_peek(p);
    if (c == 'T' || c == 't' || c == ' ') {
        size_t save = p->pos;
        p->pos++;
        if (pp_time_spec(p, &t)) {
            pp_store_time(out, &t);
            if (!pp_date_time_offset(p, zoned || instant, instant, out))
                return false;
        } else {
            p->pos = save;
        }
    }
    if ((time_required || instant) && !out->has_time)
        return false;
    return pp_tail(p, zoned, out, crit);
}

static bool pp_try_ambiguous(const char *s, size_t len, bool month_day)
{
    TPP q = { s, len, 0 };
    TPParsed tmp;
    pp_reset_parsed(&tmp);
    if (month_day)
        return pp_date_spec_month_day(&q, &tmp) && pp_eof(&q);
    return pp_date_spec_year_month(&q, &tmp) && pp_eof(&q);
}

static bool pp_annotated_time(TPP *p, TPParsed *out, bool *crit)
{
    TPPTime t;
    bool designator = pp_char_ci(p, 'T');
    size_t start = p->pos, end;

    if (!pp_time_spec(p, &t))
        return false;
    pp_store_time(out, &t);
    if (!pp_date_time_offset(p, false, false, out))
        return false;
    end = p->pos;
    if (!designator) {
        if (pp_try_ambiguous(p->s + start, end - start, true) ||
            pp_try_ambiguous(p->s + start, end - start, false))
            return false;
    }
    return pp_tail(p, false, out, crit);
}

static bool pp_goal(const char *s, size_t len, int goal, TPParsed *out, bool *crit, bool *short_form)
{
    TPP p = { s, len, 0 };

    *short_form = false;
    pp_reset_parsed(out);
    switch (goal) {
    case TP_GOAL_DATETIME_ZONED:
        return pp_date_time(&p, true, false, false, out, crit);
    case TP_GOAL_DATETIME:
        return pp_date_time(&p, false, false, false, out, crit);
    case TP_GOAL_INSTANT:
        return pp_date_time(&p, false, true, true, out, crit);
    case TP_GOAL_TIME:
        if (pp_annotated_time(&p, out, crit))
            return true;
        p.pos = 0;
        pp_reset_parsed(out);
        return pp_date_time(&p, false, true, false, out, crit);
    case TP_GOAL_MONTH_DAY:
        if (pp_date_spec_month_day(&p, out) && pp_tail(&p, false, out, crit)) {
            *short_form = true;
            return true;
        }
        p.pos = 0;
        pp_reset_parsed(out);
        return pp_date_time(&p, false, false, false, out, crit);
    case TP_GOAL_YEAR_MONTH:
        if (pp_date_spec_year_month(&p, out) && pp_tail(&p, false, out, crit)) {
            *short_form = true;
            return true;
        }
        p.pos = 0;
        pp_reset_parsed(out);
        return pp_date_time(&p, false, false, false, out, crit);
    default:
        return false;
    }
}

static bool pp_is_iso_calendar_annotation(const char *s, const TPParsed *out)
{
    static const char iso[] = "iso8601";
    size_t i;
    if (out->cal_len == 0)
        return true;
    if (out->cal_len != 7)
        return false;
    for (i = 0; i < 7; i++) {
        char c = s[out->cal_start + i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if (c != iso[i])
            return false;
    }
    return true;
}

int tp_parse_iso_datetime(JSContext *ctx, const char *s, size_t len, const int *goals, TPParsed *out)
{
    int i;
    for (i = 0; goals[i] != TP_GOAL_END; i++) {
        bool crit = false, short_form;
        if (!pp_goal(s, len, goals[i], out, &crit, &short_form))
            continue;
        if (crit)
            goto fail;
        if (short_form && !pp_is_iso_calendar_annotation(s, out))
            goto fail;
        if (!out->has_time)
            out->time = tp_midnight();
        return 0;
    }
fail:
    if (ctx)
        JS_ThrowRangeError(ctx, "invalid ISO 8601 string: %.*s", (int)(len > 80 ? 80 : len), s);
    return -1;
}

static double pp_digits_value(const char *s, size_t n)
{
    char buf[512];
    JSATODTempMem mem;
    if (n >= sizeof(buf)) {
        size_t i;
        for (i = 0; i < n && s[i] == '0'; i++)
            ;
        if (n - i >= sizeof(buf))
            return 1e300;
        s += i;
        n -= i;
    }
    memcpy(buf, s, n);
    buf[n] = '\0';
    return js_atod(buf, NULL, 10, 0, &mem);
}

static bool pp_duration_number(TPP *p, size_t *start, size_t *n)
{
    *start = p->pos;
    while (pp_is_digit(pp_peek(p)))
        p->pos++;
    *n = p->pos - *start;
    return *n > 0;
}

static double pp_neg(double v, bool negative)
{
    return negative && v != 0 ? -v : v;
}

int tp_parse_duration(JSContext *ctx, const char *s, size_t len, TPDuration *out)
{
    static const char date_designators[4] = { 'Y', 'M', 'W', 'D' };
    static const char time_designators[3] = { 'H', 'M', 'S' };
    TPP p = { s, len, 0 };
    bool negative = false, any = false, any_time = false, fraction_seen = false;
    int next_date = 0, next_time = 0, i;
    int64_t fraction_ns = 0;
    int fraction_unit = -1;

    memset(out, 0, sizeof(*out));
    if (pp_char(&p, '-'))
        negative = true;
    else
        pp_char(&p, '+');
    if (!pp_char_ci(&p, 'P'))
        goto fail;
    while (!pp_eof(&p) && pp_is_digit(pp_peek(&p))) {
        size_t start, n;
        int c;
        pp_duration_number(&p, &start, &n);
        c = pp_peek(&p);
        if (c >= 'a' && c <= 'z')
            c -= 32;
        for (i = next_date; i < 4; i++)
            if (date_designators[i] == c)
                break;
        if (i == 4)
            goto fail;
        p.pos++;
        out->f[i] = pp_digits_value(s + start, n);
        next_date = i + 1;
        any = true;
    }
    if (pp_char_ci(&p, 'T')) {
        while (!pp_eof(&p)) {
            size_t start, n;
            int c, scale = 0;
            int64_t fvalue = 0;
            bool has_fraction;
            if (fraction_seen || !pp_duration_number(&p, &start, &n))
                goto fail;
            has_fraction = pp_fraction_scaled(&p, &fvalue, &scale);
            c = pp_peek(&p);
            if (c >= 'a' && c <= 'z')
                c -= 32;
            for (i = next_time; i < 3; i++)
                if (time_designators[i] == c)
                    break;
            if (i == 3)
                goto fail;
            p.pos++;
            out->f[TP_UNIT_HOUR + i] = pp_digits_value(s + start, n);
            next_time = i + 1;
            any_time = true;
            if (has_fraction) {
                int64_t pow10 = 1;
                int k;
                for (k = 0; k < scale; k++)
                    pow10 *= 10;
                fraction_ns = fvalue * (tp_unit_ns[TP_UNIT_HOUR + i] / pow10);
                fraction_unit = TP_UNIT_HOUR + i;
                fraction_seen = true;
            }
        }
        if (!any_time)
            goto fail;
        any = true;
    }
    if (!pp_eof(&p) || !any)
        goto fail;
    if (fraction_unit >= 0) {
        int64_t rem = fraction_ns;
        if (fraction_unit == TP_UNIT_HOUR) {
            out->f[TP_UNIT_MINUTE] = (double)(rem / 60000000000LL);
            rem %= 60000000000LL;
        }
        if (fraction_unit <= TP_UNIT_MINUTE) {
            out->f[TP_UNIT_SECOND] = (double)(rem / 1000000000LL);
            rem %= 1000000000LL;
        }
        out->f[TP_UNIT_MILLISECOND] = (double)(rem / 1000000);
        out->f[TP_UNIT_MICROSECOND] = (double)(rem / 1000 % 1000);
        out->f[TP_UNIT_NANOSECOND] = (double)(rem % 1000);
    }
    for (i = 0; i < TP_UNIT_COUNT; i++)
        out->f[i] = pp_neg(out->f[i], negative);
    return 0;
fail:
    JS_ThrowRangeError(ctx, "invalid duration string: %.*s", (int)(len > 80 ? 80 : len), s);
    return -1;
}

bool tp_parse_utc_offset(const char *s, size_t len, bool sub_minute, int64_t *ns, bool *has_seconds)
{
    TPP p = { s, len, 0 };
    TPPOffset off;
    if (!pp_utc_offset(&p, sub_minute, &off) || !pp_eof(&p))
        return false;
    *ns = off.ns;
    if (has_seconds)
        *has_seconds = off.has_seconds;
    return true;
}

bool tp_parse_time_zone_identifier(const char *s, size_t len, TPTimeZoneParse *out)
{
    TPP p = { s, len, 0 };
    return pp_time_zone_identifier(&p, out) && pp_eof(&p);
}

bool tp_parse_annotation_value(const char *s, size_t len)
{
    TPP p = { s, len, 0 };
    return pp_annotation_value(&p) && pp_eof(&p);
}

int tp_parse_month_code(JSContext *ctx, JSValueConst v, int *num, bool *leap)
{
    JSValue prim = js_temporal_to_primitive(ctx, v, true);
    const char *s;
    size_t len;
    bool ok = false;

    if (JS_IsException(prim))
        return -1;
    if (!JS_IsString(prim)) {
        JS_FreeValue(ctx, prim);
        JS_ThrowTypeError(ctx, "monthCode must be a string");
        return -1;
    }
    s = JS_ToCStringLen(ctx, &len, prim);
    JS_FreeValue(ctx, prim);
    if (!s)
        return -1;
    if ((len == 3 || len == 4) && s[0] == 'M' && pp_is_digit(s[1]) && pp_is_digit(s[2])) {
        int n = (s[1] - '0') * 10 + (s[2] - '0');
        bool l = len == 4;
        if (!l || s[3] == 'L') {
            if (n == 0 ? l : true) {
                ok = true;
                *num = n;
                *leap = l;
            }
        }
    }
    if (!ok)
        JS_ThrowRangeError(ctx, "invalid monthCode: %.*s", (int)(len > 32 ? 32 : len), s);
    JS_FreeCString(ctx, s);
    return ok ? 0 : -1;
}

int tp_parse_offset_string_value(JSContext *ctx, JSValueConst v, int64_t *ns)
{
    JSValue prim = js_temporal_to_primitive(ctx, v, true);
    const char *s;
    size_t len;
    bool ok;

    if (JS_IsException(prim))
        return -1;
    if (!JS_IsString(prim)) {
        JS_FreeValue(ctx, prim);
        JS_ThrowTypeError(ctx, "offset must be a string");
        return -1;
    }
    s = JS_ToCStringLen(ctx, &len, prim);
    JS_FreeValue(ctx, prim);
    if (!s)
        return -1;
    ok = tp_parse_utc_offset(s, len, true, ns, NULL);
    if (!ok)
        JS_ThrowRangeError(ctx, "invalid offset: %.*s", (int)(len > 32 ? 32 : len), s);
    JS_FreeCString(ctx, s);
    return ok ? 0 : -1;
}
