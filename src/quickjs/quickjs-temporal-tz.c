/* quickjs-temporal-tz.c — time zone identifiers, UTC offsets, exact-time conversion and disambiguation. */
#include <string.h>
#include "cutils.h"
#include "quickjs-temporal.h"

static bool tz_ascii_ieq(const char *a, size_t alen, const char *b)
{
    size_t i;
    if (strlen(b) != alen)
        return false;
    for (i = 0; i < alen; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')
            x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z')
            y = (char)(y + 32);
        if (x != y)
            return false;
    }
    return true;
}

static int tz_named_lookup(const char *s, size_t len)
{
    if (tz_ascii_ieq(s, len, "UTC"))
        return TP_TZ_UTC_ID;
    return -1;
}

static const char *tz_named_identifier(int id)
{
    return "UTC";
}

static int tz_named_primary(int id)
{
    return TP_TZ_UTC_ID;
}

static int64_t tz_named_offset_ns(int id, tp_i128 ns)
{
    return 0;
}

static int tz_named_possible(int id, const TPDateTime *dt, tp_i128 out[2])
{
    out[0] = tp_utc_epoch_ns(dt);
    return 1;
}

static bool tz_named_next_transition(int id, tp_i128 ns, tp_i128 *out)
{
    return false;
}

static bool tz_named_previous_transition(int id, tp_i128 ns, tp_i128 *out)
{
    return false;
}

static TPTimeZone tz_offset(int32_t minutes)
{
    TPTimeZone tz;
    tz.id = TP_TZ_OFFSET_ID;
    tz.offset_minutes = minutes;
    return tz;
}

static TPTimeZone tz_named(int id)
{
    TPTimeZone tz;
    tz.id = id;
    tz.offset_minutes = 0;
    return tz;
}

static int tz_from_parse(JSContext *ctx, const char *s, const TPTimeZoneParse *p, TPTimeZone *out)
{
    int id;
    if (p->is_offset) {
        *out = tz_offset(p->offset_minutes);
        return 0;
    }
    id = tz_named_lookup(s + p->name_start, p->name_len);
    if (id < 0) {
        JS_ThrowRangeError(ctx, "unknown time zone: %.*s", (int)(p->name_len > 64 ? 64 : p->name_len),
                           s + p->name_start);
        return -1;
    }
    *out = tz_named(id);
    return 0;
}

int tp_time_zone_from_identifier(JSContext *ctx, const char *s, size_t len, TPTimeZone *out)
{
    TPTimeZoneParse p;
    if (!tp_parse_time_zone_identifier(s, len, &p)) {
        JS_ThrowRangeError(ctx, "invalid time zone: %.*s", (int)(len > 64 ? 64 : len), s);
        return -1;
    }
    return tz_from_parse(ctx, s, &p, out);
}

static int tz_parse_temporal_time_zone_string(JSContext *ctx, const char *s, size_t len, TPTimeZone *out)
{
    static const int goals[] = { TP_GOAL_DATETIME_ZONED, TP_GOAL_DATETIME, TP_GOAL_INSTANT, TP_GOAL_TIME,
                                 TP_GOAL_MONTH_DAY, TP_GOAL_YEAR_MONTH, TP_GOAL_END };
    TPTimeZoneParse tzp;
    TPParsed p;

    if (tp_parse_time_zone_identifier(s, len, &tzp))
        return tz_from_parse(ctx, s, &tzp, out);
    if (tp_parse_iso_datetime(ctx, s, len, goals, &p))
        return -1;
    if (p.tz_len) {
        if (!tp_parse_time_zone_identifier(s + p.tz_start, p.tz_len, &tzp)) {
            JS_ThrowRangeError(ctx, "invalid time zone annotation");
            return -1;
        }
        return tz_from_parse(ctx, s + p.tz_start, &tzp, out);
    }
    if (p.z) {
        *out = tz_named(TP_TZ_UTC_ID);
        return 0;
    }
    if (p.has_offset) {
        if (p.offset_sub_minute) {
            JS_ThrowRangeError(ctx, "time zone offsets must not have sub-minute precision");
            return -1;
        }
        *out = tz_offset((int32_t)(p.offset_ns / 60000000000LL));
        return 0;
    }
    JS_ThrowRangeError(ctx, "string does not contain a time zone: %.*s", (int)(len > 64 ? 64 : len), s);
    return -1;
}

int tp_to_time_zone_identifier(JSContext *ctx, JSValueConst v, TPTimeZone *out)
{
    TPObject *z = tp_get(v, TP_KIND_ZONED);
    const char *s;
    size_t len;
    int r;

    if (z) {
        *out = z->tz;
        return 0;
    }
    if (tp_require_string(ctx, v))
        return -1;
    s = JS_ToCStringLen(ctx, &len, v);
    if (!s)
        return -1;
    r = tz_parse_temporal_time_zone_string(ctx, s, len, out);
    JS_FreeCString(ctx, s);
    return r;
}

void tp_format_offset_minutes(TPBuf *b, int64_t minutes)
{
    int64_t a = minutes < 0 ? -minutes : minutes;
    tp_buf_putc(b, minutes < 0 ? '-' : '+');
    tp_buf_pad(b, a / 60, 2);
    tp_buf_putc(b, ':');
    tp_buf_pad(b, a % 60, 2);
}

void tp_time_zone_put(TPBuf *b, TPTimeZone tz)
{
    if (tz.id == TP_TZ_OFFSET_ID)
        tp_format_offset_minutes(b, tz.offset_minutes);
    else
        tp_buf_puts(b, tz_named_identifier(tz.id));
}

JSValue tp_time_zone_to_string(JSContext *ctx, TPTimeZone tz)
{
    TPBuf b;
    tp_buf_init(&b);
    tp_time_zone_put(&b, tz);
    return tp_buf_to_string(ctx, &b);
}

bool tp_time_zone_equals(TPTimeZone a, TPTimeZone b)
{
    if (a.id == TP_TZ_OFFSET_ID || b.id == TP_TZ_OFFSET_ID)
        return a.id == b.id && a.offset_minutes == b.offset_minutes;
    return tz_named_primary(a.id) == tz_named_primary(b.id);
}

bool tp_time_zone_is_offset_or_utc(TPTimeZone tz)
{
    return tz.id == TP_TZ_OFFSET_ID || tz_named_primary(tz.id) == TP_TZ_UTC_ID;
}

int64_t tp_offset_ns_for(TPTimeZone tz, tp_i128 ns)
{
    if (tz.id == TP_TZ_OFFSET_ID)
        return (int64_t)tz.offset_minutes * 60000000000LL;
    return tz_named_offset_ns(tz.id, ns);
}

void tp_iso_datetime_for(TPTimeZone tz, tp_i128 ns, TPDateTime *out)
{
    tp_epoch_ns_to_datetime(ns + tp_offset_ns_for(tz, ns), out);
}

int tp_possible_epoch_ns(JSContext *ctx, TPTimeZone tz, const TPDateTime *dt, tp_i128 out[2], int *count)
{
    int i;
    if (tz.id == TP_TZ_OFFSET_ID) {
        TPDateTime balanced = tp_balance_datetime(dt->date, tp_time_to_ns(&dt->time) -
                                                  (tp_i128)tz.offset_minutes * 60000000000LL);
        if (tp_check_iso_days_range(ctx, balanced.date))
            return -1;
        out[0] = tp_utc_epoch_ns(&balanced);
        *count = 1;
    } else {
        if (tp_check_iso_days_range(ctx, dt->date))
            return -1;
        *count = tz_named_possible(tz.id, dt, out);
    }
    for (i = 0; i < *count; i++) {
        if (!tp_epoch_ns_valid(out[i])) {
            JS_ThrowRangeError(ctx, "date-time is outside the supported range");
            return -1;
        }
    }
    return 0;
}

static int tz_disambiguate(JSContext *ctx, const tp_i128 *possible, int n, TPTimeZone tz,
                           const TPDateTime *dt, int disambiguation, tp_i128 *out)
{
    tp_i128 utc, before, after, candidates[2];
    int64_t shift;
    TPDateTime moved;
    int count;

    if (n == 1) {
        *out = possible[0];
        return 0;
    }
    if (n != 0) {
        if (disambiguation == TP_DISAMBIG_EARLIER || disambiguation == TP_DISAMBIG_COMPATIBLE) {
            *out = possible[0];
            return 0;
        }
        if (disambiguation == TP_DISAMBIG_LATER) {
            *out = possible[n - 1];
            return 0;
        }
        JS_ThrowRangeError(ctx, "multiple instants match this date-time and disambiguation is 'reject'");
        return -1;
    }
    if (disambiguation == TP_DISAMBIG_REJECT) {
        JS_ThrowRangeError(ctx, "no instant matches this date-time and disambiguation is 'reject'");
        return -1;
    }
    utc = tp_utc_epoch_ns(dt);
    before = utc - TP_NS_PER_DAY;
    after = utc + TP_NS_PER_DAY;
    if (!tp_epoch_ns_valid(before) || !tp_epoch_ns_valid(after)) {
        JS_ThrowRangeError(ctx, "date-time is outside the supported range");
        return -1;
    }
    shift = tp_offset_ns_for(tz, after) - tp_offset_ns_for(tz, before);
    if (disambiguation == TP_DISAMBIG_EARLIER) {
        moved = tp_balance_datetime(dt->date, tp_time_to_ns(&dt->time) - shift);
        if (tp_possible_epoch_ns(ctx, tz, &moved, candidates, &count))
            return -1;
        *out = candidates[0];
        return 0;
    }
    moved = tp_balance_datetime(dt->date, tp_time_to_ns(&dt->time) + shift);
    if (tp_possible_epoch_ns(ctx, tz, &moved, candidates, &count))
        return -1;
    *out = candidates[count - 1];
    return 0;
}

int tp_epoch_ns_for(JSContext *ctx, TPTimeZone tz, const TPDateTime *dt, int disambiguation, tp_i128 *out)
{
    tp_i128 possible[2];
    int n;
    if (tp_possible_epoch_ns(ctx, tz, dt, possible, &n))
        return -1;
    return tz_disambiguate(ctx, possible, n, tz, dt, disambiguation, out);
}

int tp_start_of_day(JSContext *ctx, TPTimeZone tz, TPISODate d, tp_i128 *out)
{
    TPDateTime dt;
    tp_i128 possible[2], before;
    int n;

    dt.date = d;
    dt.time = tp_midnight();
    if (tp_possible_epoch_ns(ctx, tz, &dt, possible, &n))
        return -1;
    if (n > 0) {
        *out = possible[0];
        return 0;
    }
    before = tp_utc_epoch_ns(&dt) - TP_NS_PER_DAY;
    if (!tp_epoch_ns_valid(before) || !tp_next_transition(tz, before, out)) {
        JS_ThrowRangeError(ctx, "cannot determine the start of day");
        return -1;
    }
    return 0;
}

int tp_interpret_iso_datetime_offset(JSContext *ctx, TPISODate d, const TPTime *time, int behaviour,
                                     int64_t offset_ns, TPTimeZone tz, int disambiguation,
                                     int offset_option, bool match_minutes, tp_i128 *out)
{
    TPDateTime dt;
    tp_i128 utc, possible[2];
    int n, i;

    if (!time)
        return tp_start_of_day(ctx, tz, d, out);
    dt.date = d;
    dt.time = *time;
    if (behaviour == TP_OFFSET_BEHAVIOUR_WALL ||
        (behaviour == TP_OFFSET_BEHAVIOUR_OPTION && offset_option == TP_OFFSET_IGNORE))
        return tp_epoch_ns_for(ctx, tz, &dt, disambiguation, out);
    if (behaviour == TP_OFFSET_BEHAVIOUR_EXACT ||
        (behaviour == TP_OFFSET_BEHAVIOUR_OPTION && offset_option == TP_OFFSET_USE)) {
        TPDateTime balanced = tp_balance_datetime(d, tp_time_to_ns(time) - offset_ns);
        if (tp_check_iso_days_range(ctx, balanced.date))
            return -1;
        *out = tp_utc_epoch_ns(&balanced);
        if (!tp_epoch_ns_valid(*out)) {
            JS_ThrowRangeError(ctx, "date-time is outside the supported range");
            return -1;
        }
        return 0;
    }
    if (tp_check_iso_days_range(ctx, d))
        return -1;
    utc = tp_utc_epoch_ns(&dt);
    if (tp_possible_epoch_ns(ctx, tz, &dt, possible, &n))
        return -1;
    for (i = 0; i < n; i++) {
        tp_i128 candidate_offset = utc - possible[i];
        if (candidate_offset == offset_ns) {
            *out = possible[i];
            return 0;
        }
        if (match_minutes &&
            tp_round_to_increment(candidate_offset, 60000000000LL, TP_ROUND_HALF_EXPAND) == offset_ns) {
            *out = possible[i];
            return 0;
        }
    }
    if (offset_option == TP_OFFSET_REJECT) {
        JS_ThrowRangeError(ctx, "offset is invalid for this date-time in the time zone");
        return -1;
    }
    return tz_disambiguate(ctx, possible, n, tz, &dt, disambiguation, out);
}

bool tp_next_transition(TPTimeZone tz, tp_i128 ns, tp_i128 *out)
{
    if (tz.id == TP_TZ_OFFSET_ID)
        return false;
    return tz_named_next_transition(tz.id, ns, out);
}

bool tp_previous_transition(TPTimeZone tz, tp_i128 ns, tp_i128 *out)
{
    if (tz.id == TP_TZ_OFFSET_ID)
        return false;
    return tz_named_previous_transition(tz.id, ns, out);
}

void tp_format_offset_ns(TPBuf *b, int64_t offset_ns)
{
    int64_t a = offset_ns < 0 ? -offset_ns : offset_ns;
    int64_t hour = a / 3600000000000LL, minute = a / 60000000000LL % 60, second = a / 1000000000LL % 60;
    int64_t sub = a % 1000000000LL;

    tp_buf_putc(b, offset_ns < 0 ? '-' : '+');
    tp_put_time(b, (int)hour, (int)minute, (int)second, sub,
                second == 0 && sub == 0 ? TP_PRECISION_MINUTE : TP_PRECISION_AUTO);
}

void tp_format_offset_rounded(TPBuf *b, int64_t offset_ns)
{
    int64_t rounded = (int64_t)tp_round_to_increment(offset_ns, 60000000000LL, TP_ROUND_HALF_EXPAND);
    tp_format_offset_minutes(b, rounded / 60000000000LL);
}

TPTimeZone tp_system_time_zone(void)
{
    return tz_named(TP_TZ_UTC_ID);
}

tp_i128 tp_system_epoch_ns(void)
{
    tp_i128 ns = (tp_i128)js__gettimeofday_us() * 1000;
    if (ns > TP_NS_MAX_INSTANT)
        ns = TP_NS_MAX_INSTANT;
    if (ns < -TP_NS_MAX_INSTANT)
        ns = -TP_NS_MAX_INSTANT;
    return ns;
}
