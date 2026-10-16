/* quickjs-temporal-calendars.c — the non-ISO calendars of ECMA-402 Temporal: years, eras, month codes and arithmetic. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cutils.h"
#include "quickjs-temporal.h"
#include "quickjs-temporal-caldata.h"

#define TP_CAL_YEAR_LIMIT ((int64_t)300000)
#define TP_RD_UNIX ((int64_t)719163)
#define TP_DAY_MS ((int64_t)86400000)
#define TP_SYNODIC_DAYS 29.530588853
#define TP_NEW_MOON_2000 ((int64_t)10962)

typedef struct TPCalYear {
    int64_t year;
    int64_t start;
    int months;
    int leap;
    uint8_t len[13];
} TPCalYear;

typedef struct TPEraInfo {
    TPCalendar cal;
    const char *era;
    const char *alias;
    int kind;
    int64_t offset;
} TPEraInfo;

enum { TP_ERA_EPOCH, TP_ERA_NEGATIVE, TP_ERA_OFFSET };

static const TPEraInfo tp_eras[] = {
    { TP_CAL_BUDDHIST, "be", NULL, TP_ERA_EPOCH, 0 },
    { TP_CAL_COPTIC, "am", NULL, TP_ERA_EPOCH, 0 },
    { TP_CAL_ETHIOAA, "aa", NULL, TP_ERA_EPOCH, 0 },
    { TP_CAL_ETHIOPIC, "am", NULL, TP_ERA_EPOCH, 0 },
    { TP_CAL_ETHIOPIC, "aa", NULL, TP_ERA_OFFSET, -5499 },
    { TP_CAL_GREGORY, "ce", "ad", TP_ERA_EPOCH, 0 },
    { TP_CAL_GREGORY, "bce", "bc", TP_ERA_NEGATIVE, 0 },
    { TP_CAL_HEBREW, "am", NULL, TP_ERA_EPOCH, 0 },
    { TP_CAL_INDIAN, "shaka", NULL, TP_ERA_EPOCH, 0 },
    { TP_CAL_ISLAMIC_CIVIL, "ah", NULL, TP_ERA_EPOCH, 0 },
    { TP_CAL_ISLAMIC_CIVIL, "bh", NULL, TP_ERA_NEGATIVE, 0 },
    { TP_CAL_ISLAMIC_TBLA, "ah", NULL, TP_ERA_EPOCH, 0 },
    { TP_CAL_ISLAMIC_TBLA, "bh", NULL, TP_ERA_NEGATIVE, 0 },
    { TP_CAL_ISLAMIC_UMALQURA, "ah", NULL, TP_ERA_EPOCH, 0 },
    { TP_CAL_ISLAMIC_UMALQURA, "bh", NULL, TP_ERA_NEGATIVE, 0 },
    { TP_CAL_JAPANESE, "reiwa", NULL, TP_ERA_OFFSET, 2019 },
    { TP_CAL_JAPANESE, "heisei", NULL, TP_ERA_OFFSET, 1989 },
    { TP_CAL_JAPANESE, "showa", NULL, TP_ERA_OFFSET, 1926 },
    { TP_CAL_JAPANESE, "taisho", NULL, TP_ERA_OFFSET, 1912 },
    { TP_CAL_JAPANESE, "meiji", NULL, TP_ERA_OFFSET, 1868 },
    { TP_CAL_JAPANESE, "ce", "ad", TP_ERA_EPOCH, 0 },
    { TP_CAL_JAPANESE, "bce", "bc", TP_ERA_NEGATIVE, 0 },
    { TP_CAL_PERSIAN, "ap", NULL, TP_ERA_EPOCH, 0 },
    { TP_CAL_ROC, "roc", NULL, TP_ERA_EPOCH, 0 },
    { TP_CAL_ROC, "broc", NULL, TP_ERA_NEGATIVE, 0 },
};

static const int32_t tp_persian_non_leap[] = {
    1502, 1601, 1634, 1667, 1700, 1733, 1766, 1799, 1832, 1865, 1898, 1931, 1964, 1997, 2030, 2059,
    2063, 2096, 2129, 2158, 2162, 2191, 2195, 2224, 2228, 2257, 2261, 2290, 2294, 2323, 2327, 2356,
    2360, 2389, 2393, 2422, 2426, 2455, 2459, 2488, 2492, 2521, 2525, 2554, 2558, 2587, 2591, 2620,
    2624, 2653, 2657, 2686, 2690, 2719, 2723, 2748, 2752, 2756, 2781, 2785, 2789, 2818, 2822, 2847,
    2851, 2855, 2880, 2884, 2888, 2913, 2917, 2921, 2946, 2950, 2954, 2979, 2983, 2987,
};

static int64_t cal_rd(int64_t year, int month, int day)
{
    return tp_epoch_days(year, month - 1, day) + TP_RD_UNIX;
}

static bool cal_is_gregorian_family(TPCalendar cal)
{
    return cal == TP_CAL_GREGORY || cal == TP_CAL_BUDDHIST || cal == TP_CAL_ROC || cal == TP_CAL_JAPANESE;
}

static bool cal_is_coptic_family(TPCalendar cal)
{
    return cal == TP_CAL_COPTIC || cal == TP_CAL_ETHIOPIC || cal == TP_CAL_ETHIOAA;
}

static bool cal_is_islamic(TPCalendar cal)
{
    return cal == TP_CAL_ISLAMIC_CIVIL || cal == TP_CAL_ISLAMIC_TBLA || cal == TP_CAL_ISLAMIC_UMALQURA;
}

static bool cal_is_east_asian(TPCalendar cal)
{
    return cal == TP_CAL_CHINESE || cal == TP_CAL_DANGI;
}

static int64_t cal_gregorian_offset(TPCalendar cal)
{
    if (cal == TP_CAL_BUDDHIST)
        return 543;
    if (cal == TP_CAL_ROC)
        return -1911;
    return 0;
}

static void cal_fill_lengths(TPCalYear *y, const uint8_t *len, int months)
{
    y->months = months;
    memcpy(y->len, len, (size_t)months);
}

static void cal_gregorian_year(TPCalendar cal, int64_t year, TPCalYear *y)
{
    int64_t iso = year - cal_gregorian_offset(cal);
    int m;
    y->start = tp_epoch_days(iso, 0, 1);
    y->months = 12;
    y->leap = 0;
    for (m = 1; m <= 12; m++)
        y->len[m - 1] = (uint8_t)tp_days_in_month(iso, m);
}

static void cal_coptic_year(TPCalendar cal, int64_t year, TPCalYear *y)
{
    static const uint8_t base[13] = { 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 5 };
    int64_t epoch = cal == TP_CAL_COPTIC ? 103605 : 2796;
    if (cal == TP_CAL_ETHIOAA)
        year -= 5500;
    y->start = epoch + 365 * (year - 1) + tp_floor_div64(year, 4) - TP_RD_UNIX;
    cal_fill_lengths(y, base, 13);
    y->leap = 0;
    if (tp_floor_mod64(year, 4) == 3)
        y->len[12] = 6;
}

static void cal_indian_year(int64_t year, TPCalYear *y)
{
    static const uint8_t base[12] = { 30, 31, 31, 31, 31, 31, 30, 30, 30, 30, 30, 30 };
    int64_t g = year + 78;
    bool leap = tp_is_leap_year(g);
    y->start = tp_epoch_days(g, 2, leap ? 21 : 22);
    cal_fill_lengths(y, base, 12);
    y->leap = 0;
    if (leap)
        y->len[0] = 31;
}

static bool cal_persian_correction(int64_t year)
{
    int lo = 0, hi = (int)countof(tp_persian_non_leap) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (tp_persian_non_leap[mid] == year)
            return true;
        if (tp_persian_non_leap[mid] < year)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return false;
}

static bool cal_persian_leap(int64_t year)
{
    if (year >= 1502 && cal_persian_correction(year))
        return false;
    if (year > 1502 && cal_persian_correction(year - 1))
        return true;
    return tp_floor_mod64(25 * year + 11, 33) < 8;
}

static int64_t cal_persian_new_year(int64_t year)
{
    int64_t ny = 226896 - 1 + 365 * (year - 1) + tp_floor_div64(8 * year + 21, 33);
    if (year > 1502 && cal_persian_correction(year - 1))
        ny--;
    return ny - TP_RD_UNIX;
}

static void cal_persian_year(int64_t year, TPCalYear *y)
{
    static const uint8_t base[12] = { 31, 31, 31, 31, 31, 31, 30, 30, 30, 30, 30, 29 };
    y->start = cal_persian_new_year(year);
    cal_fill_lengths(y, base, 12);
    y->leap = 0;
    if (cal_persian_leap(year))
        y->len[11] = 30;
}

static int64_t cal_hebrew_elapsed(int64_t year)
{
    int64_t months = tp_floor_div64(235 * year - 234, 19);
    int64_t parts = 12084 + 13753 * months;
    int64_t days = 29 * months + tp_floor_div64(parts, 25920);
    return tp_floor_mod64(3 * (days + 1), 7) < 3 ? days + 1 : days;
}

static int64_t cal_hebrew_new_year(int64_t year)
{
    int64_t ny0 = cal_hebrew_elapsed(year - 1), ny1 = cal_hebrew_elapsed(year), ny2 = cal_hebrew_elapsed(year + 1);
    int correction = ny2 - ny1 == 356 ? 2 : ny1 - ny0 == 382 ? 1 : 0;
    return -1373427 + ny1 + correction - TP_RD_UNIX;
}

static void cal_hebrew_year(int64_t year, TPCalYear *y)
{
    static const uint8_t common[12] = { 30, 29, 30, 29, 30, 29, 30, 29, 30, 29, 30, 29 };
    static const uint8_t leap[13] = { 30, 29, 30, 29, 30, 30, 29, 30, 29, 30, 29, 30, 29 };
    int64_t days;
    bool is_leap = tp_floor_mod64(7 * year + 1, 19) < 7;

    y->start = cal_hebrew_new_year(year);
    days = cal_hebrew_new_year(year + 1) - y->start;
    if (is_leap) {
        cal_fill_lengths(y, leap, 13);
        y->leap = 6;
    } else {
        cal_fill_lengths(y, common, 12);
        y->leap = 0;
    }
    if (days % 10 == 5)
        y->len[1] = 30;
    if (days % 10 == 3)
        y->len[2] = 29;
}

static int64_t cal_tabular_start(int64_t year, int64_t epoch_rd)
{
    return epoch_rd + (year - 1) * 354 + tp_floor_div64(3 + 11 * year, 30) - TP_RD_UNIX;
}

static void cal_tabular_year(int64_t year, int64_t epoch_rd, TPCalYear *y)
{
    int m;
    y->start = cal_tabular_start(year, epoch_rd);
    y->months = 12;
    y->leap = 0;
    for (m = 0; m < 12; m++)
        y->len[m] = m % 2 == 0 ? 30 : 29;
    if (tp_floor_mod64(14 + 11 * year, 30) < 11)
        y->len[11] = 30;
}

static void cal_umalqura_year(int64_t year, TPCalYear *y)
{
    uint32_t w;
    int m;
    if (year < TP_UMMALQURA_FIRST_YEAR || year >= TP_UMMALQURA_FIRST_YEAR + (int64_t)countof(tp_ummalqura_years)) {
        cal_tabular_year(year, 227015, y);
        return;
    }
    w = tp_ummalqura_years[year - TP_UMMALQURA_FIRST_YEAR];
    y->start = cal_tabular_start(year, 227015) + (int64_t)((w >> 12) & 15) - 8;
    y->months = 12;
    y->leap = 0;
    for (m = 0; m < 12; m++)
        y->len[m] = (w >> m) & 1 ? 30 : 29;
}

typedef struct TPMoment {
    int64_t rd;
    int64_t ms;
} TPMoment;

static TPMoment cal_moment_add(TPMoment m, int64_t ms)
{
    int64_t t = m.ms + ms;
    m.rd += tp_floor_div64(t, TP_DAY_MS);
    m.ms = tp_floor_mod64(t, TP_DAY_MS);
    return m;
}

static TPMoment cal_periodic_on_or_before(int64_t rd, TPMoment base, int64_t period)
{
    int64_t diff = (rd - base.rd) * TP_DAY_MS - base.ms;
    int64_t n = tp_floor_div64(diff + TP_DAY_MS - 1, period);
    TPMoment r;
    r.rd = base.rd;
    r.ms = base.ms;
    return cal_moment_add(r, n * period);
}

static void cal_east_asian_simple(int64_t year, int64_t offset_ms, TPCalYear *y)
{
    const int64_t year_ms = 31556952000LL, term_ms = 2629746000LL, moon_ms = 2551442876LL;
    TPMoment solstice = { cal_rd(1999, 12, 22), 27840000 }, moon0 = { cal_rd(2000, 1, 6), 65640000 };
    TPMoment term, moon, next;
    int solar_term = -2, leap = 0;
    bool had_leap = false;

    solstice = cal_moment_add(solstice, offset_ms);
    moon0 = cal_moment_add(moon0, offset_ms);
    term = cal_periodic_on_or_before(cal_rd(year, 1, 1) - 1, solstice, year_ms);
    moon = cal_periodic_on_or_before(term.rd, moon0, moon_ms);
    next = cal_moment_add(moon, moon_ms);
    while (solar_term < 0 || (next.rd <= term.rd && !had_leap)) {
        if (next.rd <= term.rd && !had_leap) {
            had_leap = true;
        } else {
            solar_term++;
            term = cal_moment_add(term, term_ms);
        }
        moon = next;
        next = cal_moment_add(next, moon_ms);
    }
    y->start = moon.rd - TP_RD_UNIX;
    memset(y->len, 29, sizeof(y->len));
    while (solar_term < 12 || (next.rd <= term.rd && !had_leap)) {
        int idx = solar_term + (leap ? 1 : 0);
        if (idx < 13)
            y->len[idx] = (uint8_t)(next.rd - moon.rd);
        if (next.rd <= term.rd && !had_leap) {
            had_leap = true;
            leap = solar_term + 1;
        } else {
            solar_term++;
            term = cal_moment_add(term, term_ms);
        }
        moon = next;
        next = cal_moment_add(next, moon_ms);
    }
    y->leap = leap;
    y->months = leap ? 13 : 12;
}

static void cal_east_asian_table(uint32_t w, int64_t year, TPCalYear *y)
{
    int m;
    y->leap = (int)((w >> 13) & 15);
    y->months = y->leap ? 13 : 12;
    y->start = tp_epoch_days(year, 0, 1) + (int64_t)((w >> 17) & 63);
    for (m = 0; m < 13; m++)
        y->len[m] = (w >> m) & 1 ? 30 : 29;
}

static void cal_east_asian_year(TPCalendar cal, int64_t year, TPCalYear *y)
{
    const uint32_t *table = cal == TP_CAL_CHINESE ? tp_china_years : tp_korea_years;
    int64_t first = cal == TP_CAL_CHINESE ? TP_CHINA_FIRST_YEAR : TP_KOREA_FIRST_YEAR;
    int64_t count = cal == TP_CAL_CHINESE ? (int64_t)countof(tp_china_years) : (int64_t)countof(tp_korea_years);

    if (year >= first && year < first + count) {
        cal_east_asian_table(table[year - first], year, y);
        return;
    }
    if (year >= first + count)
        cal_east_asian_simple(year, cal == TP_CAL_CHINESE ? 28800000 : 32400000, y);
    else
        cal_east_asian_simple(year, 27940000, y);
}

static bool cal_year(TPCalendar cal, int64_t year, TPCalYear *y)
{
    if (year > TP_CAL_YEAR_LIMIT || year < -TP_CAL_YEAR_LIMIT)
        return false;
    y->year = year;
    if (cal_is_gregorian_family(cal))
        cal_gregorian_year(cal, year, y);
    else if (cal_is_coptic_family(cal))
        cal_coptic_year(cal, year, y);
    else if (cal == TP_CAL_INDIAN)
        cal_indian_year(year, y);
    else if (cal == TP_CAL_PERSIAN)
        cal_persian_year(year, y);
    else if (cal == TP_CAL_HEBREW)
        cal_hebrew_year(year, y);
    else if (cal == TP_CAL_ISLAMIC_CIVIL)
        cal_tabular_year(year, 227015, y);
    else if (cal == TP_CAL_ISLAMIC_TBLA)
        cal_tabular_year(year, 227014, y);
    else if (cal == TP_CAL_ISLAMIC_UMALQURA)
        cal_umalqura_year(year, y);
    else
        cal_east_asian_year(cal, year, y);
    return true;
}

static int cal_year_days(const TPCalYear *y)
{
    int d = 0, m;
    for (m = 0; m < y->months; m++)
        d += y->len[m];
    return d;
}

static int64_t cal_estimate_year(TPCalendar cal, int64_t days)
{
    int64_t rd = days + TP_RD_UNIX;
    if (cal_is_gregorian_family(cal) || cal_is_east_asian(cal))
        return tp_epoch_days_to_date(days).year + cal_gregorian_offset(cal);
    if (cal == TP_CAL_COPTIC)
        return tp_floor_div64(4 * (rd - 103605) + 1463, 1461);
    if (cal == TP_CAL_ETHIOPIC)
        return tp_floor_div64(4 * (rd - 2796) + 1463, 1461);
    if (cal == TP_CAL_ETHIOAA)
        return tp_floor_div64(4 * (rd - 2796) + 1463, 1461) + 5500;
    if (cal == TP_CAL_INDIAN)
        return tp_epoch_days_to_date(days).year - 78;
    if (cal == TP_CAL_PERSIAN)
        return 1 + tp_floor_div64(33 * (rd - 226896 + 1) + 3, 12053);
    if (cal == TP_CAL_HEBREW)
        return tp_floor_div64((rd + 1373427) * 98496, 35975351) + 1;
    return tp_floor_div64(30 * (rd - 5 - 227015) + 10646, 10631);
}

static void cal_year_containing(TPCalendar cal, int64_t days, TPCalYear *y)
{
    int64_t year = cal_estimate_year(cal, days);
    TPCalYear next;

    cal_year(cal, year, y);
    while (y->start > days)
        cal_year(cal, y->year - 1, y);
    for (;;) {
        cal_year(cal, y->year + 1, &next);
        if (next.start > days)
            break;
        *y = next;
    }
}

static int64_t cal_month_start(const TPCalYear *y, int ordinal)
{
    int64_t d = y->start;
    int m;
    for (m = 1; m < ordinal; m++)
        d += y->len[m - 1];
    return d;
}

static void cal_ordinal_to_code(const TPCalYear *y, int ordinal, int *num, bool *leap)
{
    *leap = y->leap && ordinal == y->leap;
    *num = y->leap && ordinal >= y->leap ? ordinal - 1 : ordinal;
}

bool tp_ncal_supports_era(TPCalendar cal)
{
    return cal != TP_CAL_ISO8601 && !cal_is_east_asian(cal);
}

static bool cal_has_mid_year_eras(TPCalendar cal)
{
    return cal == TP_CAL_JAPANESE;
}

static bool cal_valid_month_code(TPCalendar cal, int num, bool leap)
{
    if (!leap && num >= 1 && num <= 12)
        return true;
    if (!leap && num == 13)
        return cal_is_coptic_family(cal);
    if (leap && num >= 1 && num <= 12 && cal_is_east_asian(cal))
        return true;
    return leap && num == 5 && cal == TP_CAL_HEBREW;
}

static bool cal_year_contains_code(const TPCalYear *y, int num, bool leap)
{
    if (!leap)
        return num <= y->months;
    return y->leap && y->leap - 1 == num;
}

static int cal_code_to_ordinal(const TPCalYear *y, int num, bool leap)
{
    if (leap)
        return y->leap;
    return y->leap && num >= y->leap ? num + 1 : num;
}

static int cal_constrain_code(JSContext *ctx, TPCalendar cal, const TPCalYear *y, int *num, bool *leap,
                              int overflow)
{
    if (cal_year_contains_code(y, *num, *leap))
        return 0;
    if (overflow == TP_OVERFLOW_REJECT) {
        JS_ThrowRangeError(ctx, "month code does not exist in this year");
        return -1;
    }
    if (cal == TP_CAL_HEBREW)
        *num = 6;
    *leap = false;
    return 0;
}

static bool cal_era_matches(const TPEraInfo *e, const char *era)
{
    return strcmp(e->era, era) == 0 || (e->alias && strcmp(e->alias, era) == 0);
}

static const TPEraInfo *cal_find_era(TPCalendar cal, const char *era)
{
    size_t i;
    for (i = 0; i < countof(tp_eras); i++) {
        if (tp_eras[i].cal == cal && cal_era_matches(&tp_eras[i], era))
            return &tp_eras[i];
    }
    return NULL;
}

static int64_t cal_era_arithmetic_year(const TPEraInfo *e, int64_t era_year)
{
    if (e->kind == TP_ERA_EPOCH)
        return era_year;
    if (e->kind == TP_ERA_NEGATIVE)
        return 1 - era_year;
    return e->offset + era_year - 1;
}

static void cal_set_era(TPCalendarDate *out, const char *era, int64_t era_year)
{
    out->has_era = true;
    snprintf(out->era, sizeof(out->era), "%s", era);
    out->era_year = era_year;
}

static void cal_japanese_era(TPISODate d, int64_t year, TPCalendarDate *out)
{
    static const struct {
        int64_t year;
        int month, day;
        const char *era;
        int64_t offset;
    } eras[] = {
        { 2019, 5, 1, "reiwa", 2019 }, { 1989, 1, 8, "heisei", 1989 }, { 1926, 12, 25, "showa", 1926 },
        { 1912, 7, 30, "taisho", 1912 }, { 1873, 1, 1, "meiji", 1868 },
    };
    size_t i;
    for (i = 0; i < countof(eras); i++) {
        TPISODate start = { eras[i].year, eras[i].month, eras[i].day };
        if (tp_compare_iso_date(d, start) >= 0) {
            cal_set_era(out, eras[i].era, year - eras[i].offset + 1);
            return;
        }
    }
    if (year >= 1)
        cal_set_era(out, "ce", year);
    else
        cal_set_era(out, "bce", 1 - year);
}

static void cal_date_era(TPCalendar cal, TPISODate d, int64_t year, TPCalendarDate *out)
{
    out->has_era = false;
    switch (cal) {
    case TP_CAL_GREGORY:
        cal_set_era(out, year >= 1 ? "ce" : "bce", year >= 1 ? year : 1 - year);
        break;
    case TP_CAL_JAPANESE:
        cal_japanese_era(d, year, out);
        break;
    case TP_CAL_ROC:
        cal_set_era(out, year >= 1 ? "roc" : "broc", year >= 1 ? year : 1 - year);
        break;
    case TP_CAL_BUDDHIST:
        cal_set_era(out, "be", year);
        break;
    case TP_CAL_COPTIC:
    case TP_CAL_HEBREW:
        cal_set_era(out, "am", year);
        break;
    case TP_CAL_ETHIOAA:
        cal_set_era(out, "aa", year);
        break;
    case TP_CAL_ETHIOPIC:
        cal_set_era(out, year >= 1 ? "am" : "aa", year >= 1 ? year : year + 5500);
        break;
    case TP_CAL_INDIAN:
        cal_set_era(out, "shaka", year);
        break;
    case TP_CAL_PERSIAN:
        cal_set_era(out, "ap", year);
        break;
    case TP_CAL_ISLAMIC_CIVIL:
    case TP_CAL_ISLAMIC_TBLA:
    case TP_CAL_ISLAMIC_UMALQURA:
        cal_set_era(out, year >= 1 ? "ah" : "bh", year >= 1 ? year : 1 - year);
        break;
    default:
        break;
    }
}

static bool cal_in_leap_year(TPCalendar cal, const TPCalYear *y)
{
    int days = cal_year_days(y);
    if (cal == TP_CAL_HEBREW || cal_is_east_asian(cal))
        return y->months == 13;
    if (cal_is_islamic(cal))
        return days == 355;
    return days == 366;
}

void tp_ncal_iso_to_date(TPCalendar cal, TPISODate d, TPCalendarDate *out)
{
    int64_t days = tp_iso_date_epoch_days(d), rest;
    TPCalYear y;
    int m = 1, num;
    bool leap;

    memset(out, 0, sizeof(*out));
    cal_year_containing(cal, days, &y);
    rest = days - y.start;
    while (m < y.months && rest >= y.len[m - 1]) {
        rest -= y.len[m - 1];
        m++;
    }
    cal_ordinal_to_code(&y, m, &num, &leap);
    out->year = y.year;
    out->month = m;
    out->month_code = num;
    out->month_code_leap = leap;
    out->day = (int32_t)rest + 1;
    out->day_of_week = tp_iso_day_of_week(d);
    out->day_of_year = (int32_t)(days - y.start) + 1;
    out->has_week_of_year = false;
    out->days_in_week = 7;
    out->days_in_month = y.len[m - 1];
    out->days_in_year = cal_year_days(&y);
    out->months_in_year = y.months;
    out->in_leap_year = cal_in_leap_year(cal, &y);
    cal_date_era(cal, d, y.year, out);
}

static int cal_year_or_throw(JSContext *ctx, TPCalendar cal, int64_t year, TPCalYear *y)
{
    if (!cal_year(cal, year, y)) {
        JS_ThrowRangeError(ctx, "date is outside the supported range");
        return -1;
    }
    return 0;
}

static int cal_integers_to_iso(JSContext *ctx, const TPCalYear *y, int month, int64_t day, TPISODate *out)
{
    if (month < 1 || month > y->months || day < 1 || day > y->len[month - 1]) {
        JS_ThrowRangeError(ctx, "date does not exist in this calendar");
        return -1;
    }
    *out = tp_epoch_days_to_date(cal_month_start(y, month) + day - 1);
    return 0;
}

uint32_t tp_ncal_field_keys_to_ignore(TPCalendar cal, uint32_t keys)
{
    uint32_t ignored = keys;
    if (keys & TP_FB(TP_F_MONTH))
        ignored |= TP_FB(TP_F_MONTH_CODE);
    if (keys & TP_FB(TP_F_MONTH_CODE))
        ignored |= TP_FB(TP_F_MONTH);
    if ((keys & (TP_FB(TP_F_ERA) | TP_FB(TP_F_ERA_YEAR) | TP_FB(TP_F_YEAR))) && tp_ncal_supports_era(cal))
        ignored |= TP_FB(TP_F_ERA) | TP_FB(TP_F_ERA_YEAR) | TP_FB(TP_F_YEAR);
    if ((keys & (TP_FB(TP_F_DAY) | TP_FB(TP_F_MONTH) | TP_FB(TP_F_MONTH_CODE))) && cal_has_mid_year_eras(cal))
        ignored |= TP_FB(TP_F_ERA) | TP_FB(TP_F_ERA_YEAR);
    return ignored;
}

int tp_ncal_resolve_fields(JSContext *ctx, TPCalendar cal, TPFields *f, int type)
{
    bool needs_year = type == TP_FIELDS_DATE || type == TP_FIELDS_YEAR_MONTH;
    bool needs_day = type == TP_FIELDS_DATE || type == TP_FIELDS_MONTH_DAY;
    bool has_era = (f->has & TP_FB(TP_F_ERA)) != 0, has_era_year = (f->has & TP_FB(TP_F_ERA_YEAR)) != 0;
    bool eras = tp_ncal_supports_era(cal);

    if (!(f->has & TP_FB(TP_F_MONTH_CODE)) || (f->has & TP_FB(TP_F_MONTH)))
        needs_year = true;
    if (needs_year && !(f->has & TP_FB(TP_F_YEAR)) && (!eras || !has_era || !has_era_year)) {
        JS_ThrowTypeError(ctx, "year (or era and eraYear) is required");
        return -1;
    }
    if (eras && has_era != has_era_year) {
        JS_ThrowTypeError(ctx, "era and eraYear must be given together");
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
    if (eras && has_era_year) {
        const TPEraInfo *e = cal_find_era(cal, f->era);
        int64_t year;
        if (!e) {
            JS_ThrowRangeError(ctx, "invalid era for this calendar: %s", f->era);
            return -1;
        }
        year = cal_era_arithmetic_year(e, f->v[TP_F_ERA_YEAR]);
        if ((f->has & TP_FB(TP_F_YEAR)) && f->v[TP_F_YEAR] != year) {
            JS_ThrowRangeError(ctx, "year does not match era and eraYear");
            return -1;
        }
        f->has |= TP_FB(TP_F_YEAR);
        f->v[TP_F_YEAR] = year;
        f->has &= ~(TP_FB(TP_F_ERA) | TP_FB(TP_F_ERA_YEAR));
    }
    if (f->has & TP_FB(TP_F_MONTH_CODE)) {
        int num = (int)f->v[TP_F_MONTH_CODE];
        bool leap = f->month_code_leap;
        if (!cal_valid_month_code(cal, num, leap)) {
            JS_ThrowRangeError(ctx, "invalid monthCode for this calendar");
            return -1;
        }
        if (f->has & TP_FB(TP_F_YEAR)) {
            TPCalYear y;
            int month;
            if (cal_year_or_throw(ctx, cal, f->v[TP_F_YEAR], &y))
                return -1;
            if (!cal_year_contains_code(&y, num, leap))
                cal_constrain_code(ctx, cal, &y, &num, &leap, TP_OVERFLOW_CONSTRAIN);
            month = cal_code_to_ordinal(&y, num, leap);
            if ((f->has & TP_FB(TP_F_MONTH)) && f->v[TP_F_MONTH] != month) {
                JS_ThrowRangeError(ctx, "month and monthCode do not agree");
                return -1;
            }
            f->has |= TP_FB(TP_F_MONTH);
            f->v[TP_F_MONTH] = month;
        }
    }
    return 0;
}

static int cal_check_iso(JSContext *ctx, TPISODate d)
{
    if (!tp_iso_date_within_limits(d)) {
        JS_ThrowRangeError(ctx, "date is outside the supported range");
        return -1;
    }
    return 0;
}

int tp_ncal_date_to_iso(JSContext *ctx, TPCalendar cal, TPFields *f, int overflow, TPISODate *out)
{
    TPCalYear y;
    int64_t month, day;

    if (cal_year_or_throw(ctx, cal, f->v[TP_F_YEAR], &y))
        return -1;
    if (f->has & TP_FB(TP_F_MONTH_CODE)) {
        int num = (int)f->v[TP_F_MONTH_CODE];
        bool leap = f->month_code_leap;
        if (cal_constrain_code(ctx, cal, &y, &num, &leap, overflow))
            return -1;
    }
    month = f->v[TP_F_MONTH];
    if (month > y.months) {
        if (overflow == TP_OVERFLOW_REJECT) {
            JS_ThrowRangeError(ctx, "month is out of range for this year");
            return -1;
        }
        month = y.months;
    }
    day = f->v[TP_F_DAY];
    if (day > y.len[month - 1]) {
        if (overflow == TP_OVERFLOW_REJECT) {
            JS_ThrowRangeError(ctx, "day is out of range for this month");
            return -1;
        }
        day = y.len[month - 1];
    }
    return cal_integers_to_iso(ctx, &y, (int)month, day, out);
}

static int cal_max_days_in_month(TPCalendar cal, int num, bool leap)
{
    static const uint8_t hebrew[13] = { 30, 30, 30, 29, 30, 29, 30, 29, 30, 29, 30, 29, 30 };
    if (cal_is_gregorian_family(cal))
        return tp_days_in_month(1972, num);
    if (cal_is_coptic_family(cal))
        return num == 13 ? 6 : 30;
    if (cal == TP_CAL_INDIAN)
        return num <= 6 ? 31 : 30;
    if (cal == TP_CAL_PERSIAN)
        return num <= 6 ? 31 : 30;
    if (cal == TP_CAL_HEBREW)
        return leap ? 30 : hebrew[num - 1];
    if (cal == TP_CAL_ISLAMIC_UMALQURA || cal_is_east_asian(cal))
        return 30;
    return num % 2 == 1 || num == 12 ? 30 : 29;
}

static int cal_east_asian_reference_year(TPCalendar cal, int num, bool leap, int64_t day)
{
    static const int16_t common[12][2] = {
        { 1972, 1970 }, { 1972, 1972 }, { 1972, 1966 }, { 1972, 1970 }, { 1972, 1972 }, { 1972, 1971 },
        { 1972, 1972 }, { 1972, 1971 }, { 1972, 1972 }, { 1972, 1972 }, { 1972, 1970 }, { 1972, 1972 },
    };
    static const int16_t leap_years[12][2] = {
        { 0, 0 }, { 1947, 0 }, { 1966, 1955 }, { 1963, 1944 }, { 1971, 1952 }, { 1960, 1941 },
        { 1968, 1938 }, { 1957, 0 }, { 2014, 0 }, { 1984, 0 }, { 2034, 0 }, { 0, 0 },
    };
    int col = day == 30 ? 1 : 0;
    if (!leap) {
        if (num == 3 && col == 1 && cal == TP_CAL_DANGI)
            return 1968;
        return common[num - 1][col];
    }
    if (num == 11 && col == 0 && day <= 10)
        return 2033;
    return leap_years[num - 1][col];
}

static bool cal_find_in_iso_year(TPCalendar cal, int64_t iso_year, int num, bool leap, int64_t day,
                                 TPISODate *out)
{
    int64_t first = tp_epoch_days(iso_year, 0, 1), last = tp_epoch_days(iso_year, 11, 31);
    TPCalYear y;
    bool found = false;

    cal_year_containing(cal, last, &y);
    for (;;) {
        if (cal_year_contains_code(&y, num, leap)) {
            int ordinal = cal_code_to_ordinal(&y, num, leap);
            if (day <= y.len[ordinal - 1]) {
                int64_t d = cal_month_start(&y, ordinal) + day - 1;
                if (d >= first && d <= last && (!found || d > tp_iso_date_epoch_days(*out))) {
                    *out = tp_epoch_days_to_date(d);
                    found = true;
                }
            }
        }
        if (y.start <= first)
            break;
        cal_year(cal, y.year - 1, &y);
    }
    return found;
}

static bool cal_reference_year(TPCalendar cal, int num, bool leap, int64_t day, int64_t *out)
{
    int64_t iso_year;
    TPISODate d;
    for (iso_year = 1972; iso_year >= 1900; iso_year--) {
        if (cal_find_in_iso_year(cal, iso_year, num, leap, day, &d)) {
            *out = iso_year;
            return true;
        }
    }
    for (iso_year = 1973; iso_year <= 2035; iso_year++) {
        if (cal_find_in_iso_year(cal, iso_year, num, leap, day, &d)) {
            *out = iso_year;
            return true;
        }
    }
    return false;
}

static bool cal_year_within_limits(TPCalendar cal, const TPCalYear *y)
{
    int64_t end = y->start + cal_year_days(y) - 1;
    return end >= -100000001 && y->start <= 100000000;
}

int tp_ncal_month_day_to_iso(JSContext *ctx, TPCalendar cal, TPFields *f, int overflow, TPISODate *out)
{
    int num, max_days;
    bool leap;
    int64_t day = f->v[TP_F_DAY], reference;

    if (f->has & TP_FB(TP_F_YEAR)) {
        TPCalYear y;
        int64_t month = f->v[TP_F_MONTH];
        if (cal_year_or_throw(ctx, cal, f->v[TP_F_YEAR], &y))
            return -1;
        if (!cal_year_within_limits(cal, &y)) {
            JS_ThrowRangeError(ctx, "year is outside the supported range");
            return -1;
        }
        if (month > y.months) {
            if (overflow == TP_OVERFLOW_REJECT) {
                JS_ThrowRangeError(ctx, "month is out of range for this year");
                return -1;
            }
            month = y.months;
        }
        if (f->has & TP_FB(TP_F_MONTH_CODE)) {
            num = (int)f->v[TP_F_MONTH_CODE];
            leap = f->month_code_leap;
            if (cal_constrain_code(ctx, cal, &y, &num, &leap, overflow))
                return -1;
        } else {
            cal_ordinal_to_code(&y, (int)month, &num, &leap);
        }
        max_days = y.len[month - 1];
    } else {
        num = (int)f->v[TP_F_MONTH_CODE];
        leap = f->month_code_leap;
        max_days = cal_is_east_asian(cal) ? 30 : cal_max_days_in_month(cal, num, leap);
    }
    if (day > max_days) {
        if (overflow == TP_OVERFLOW_REJECT) {
            JS_ThrowRangeError(ctx, "day is out of range for this month");
            return -1;
        }
        day = max_days;
    }
    if (cal_is_east_asian(cal)) {
        reference = cal_east_asian_reference_year(cal, num, leap, day);
        if (!reference) {
            if (overflow == TP_OVERFLOW_REJECT) {
                JS_ThrowRangeError(ctx, "this month-day does not occur in the reference range");
                return -1;
            }
            leap = false;
            reference = cal_east_asian_reference_year(cal, num, leap, day);
        }
    } else if (!cal_reference_year(cal, num, leap, day, &reference)) {
        JS_ThrowRangeError(ctx, "this month-day has no reference year");
        return -1;
    }
    if (!cal_find_in_iso_year(cal, reference, num, leap, day, out)) {
        JS_ThrowRangeError(ctx, "this month-day has no reference date");
        return -1;
    }
    return 0;
}

typedef struct TPCalYmd {
    int64_t year;
    int month;
    int64_t day;
} TPCalYmd;

static bool cal_balance(TPCalendar cal, int64_t year, int month, int64_t day, TPCalYmd *out)
{
    TPCalYear y;
    int64_t d;

    if (!cal_year(cal, year, &y) || day < -200000000 || day > 200000000)
        return false;
    d = cal_month_start(&y, month) + day - 1;
    if (d < -110000000 || d > 110000000)
        return false;
    cal_year_containing(cal, d, &y);
    if (y.year > TP_CAL_YEAR_LIMIT || y.year < -TP_CAL_YEAR_LIMIT)
        return false;
    out->year = y.year;
    out->month = 1;
    d -= y.start;
    while (out->month < y.months && d >= y.len[out->month - 1]) {
        d -= y.len[out->month - 1];
        out->month++;
    }
    out->day = d + 1;
    return true;
}

static int64_t cal_hebrew_months_before(int64_t year)
{
    return tp_floor_div64(235 * year - 234, 19);
}

static int64_t cal_lunar_month_index(int64_t days)
{
    return (int64_t)floor((double)(days - TP_NEW_MOON_2000) / TP_SYNODIC_DAYS + 0.5);
}

static bool cal_add_months(TPCalendar cal, int64_t year, int64_t ordinal, int64_t months, TPCalYmd *out)
{
    TPCalYear y;
    int64_t m;

    if (months > 13 * TP_CAL_YEAR_LIMIT * 2 || months < -13 * TP_CAL_YEAR_LIMIT * 2)
        return false;
    out->day = 1;
    if (cal == TP_CAL_HEBREW) {
        int64_t index = cal_hebrew_months_before(year) + ordinal - 1 + months;
        int64_t target = tp_floor_div64(19 * index, 235) + 1;
        while (cal_hebrew_months_before(target) > index)
            target--;
        while (cal_hebrew_months_before(target + 1) <= index)
            target++;
        if (target > TP_CAL_YEAR_LIMIT || target < -TP_CAL_YEAR_LIMIT)
            return false;
        out->year = target;
        out->month = (int)(index - cal_hebrew_months_before(target)) + 1;
        return true;
    }
    if (!cal_is_east_asian(cal)) {
        int64_t per = cal_is_coptic_family(cal) ? 13 : 12, total = ordinal - 1 + months;
        out->year = year + tp_floor_div64(total, per);
        out->month = (int)tp_floor_mod64(total, per) + 1;
        return out->year <= TP_CAL_YEAR_LIMIT && out->year >= -TP_CAL_YEAR_LIMIT;
    }
    if (!cal_year(cal, year, &y))
        return false;
    m = cal_lunar_month_index(y.start) + ordinal - 1 + months;
    if (m > 13 * TP_CAL_YEAR_LIMIT || m < -13 * TP_CAL_YEAR_LIMIT)
        return false;
    cal_year_containing(cal, TP_NEW_MOON_2000 + (int64_t)((double)m * TP_SYNODIC_DAYS) + 15, &y);
    while (cal_lunar_month_index(y.start) > m) {
        if (!cal_year(cal, y.year - 1, &y))
            return false;
    }
    for (;;) {
        TPCalYear next;
        if (!cal_year(cal, y.year + 1, &next))
            return false;
        if (cal_lunar_month_index(next.start) > m)
            break;
        y = next;
    }
    out->year = y.year;
    out->month = (int)(m - cal_lunar_month_index(y.start)) + 1;
    return out->year <= TP_CAL_YEAR_LIMIT && out->year >= -TP_CAL_YEAR_LIMIT;
}
static int cal_compare_surpasses_code(int sign, int64_t year, int num, bool leap, int64_t day,
                                      const TPCalendarDate *t)
{
    int a = num * 2 + (leap ? 1 : 0), b = t->month_code * 2 + (t->month_code_leap ? 1 : 0);
    if (year != t->year)
        return sign * (year - t->year) > 0;
    if (a != b)
        return sign * (a - b) > 0;
    if (day != t->day)
        return sign * (day - t->day) > 0;
    return 0;
}

static int cal_compare_surpasses_month(int sign, int64_t year, int month, int64_t day, const TPCalendarDate *t)
{
    if (year != t->year)
        return sign * (year - t->year) > 0;
    if (month != t->month)
        return sign * (month - t->month) > 0;
    if (day != t->day)
        return sign * (day - t->day) > 0;
    return 0;
}

static bool cal_surpasses(TPCalendar cal, int sign, const TPCalendarDate *from, const TPCalendarDate *to,
                          int64_t years, int64_t months)
{
    int64_t y0 = from->year + years;
    int num = from->month_code;
    bool leap = from->month_code_leap;
    TPCalYear y;
    TPCalYmd added;

    if (cal_compare_surpasses_code(sign, y0, num, leap, from->day, to))
        return true;
    if (!cal_year(cal, y0, &y))
        return true;
    if (!cal_year_contains_code(&y, num, leap))
        cal_constrain_code(NULL, cal, &y, &num, &leap, TP_OVERFLOW_CONSTRAIN);
    if (!cal_add_months(cal, y0, cal_code_to_ordinal(&y, num, leap), months, &added))
        return true;
    return cal_compare_surpasses_month(sign, added.year, added.month, from->day, to);
}

static int64_t cal_search(TPCalendar cal, int sign, const TPCalendarDate *from, const TPCalendarDate *to,
                          int64_t years, bool searching_years)
{
    int64_t lo = 0, hi = 1;
    while (!(searching_years ? cal_surpasses(cal, sign, from, to, hi * sign, 0)
                             : cal_surpasses(cal, sign, from, to, years, hi * sign))) {
        lo = hi;
        hi *= 2;
    }
    while (hi - lo > 1) {
        int64_t mid = lo + (hi - lo) / 2;
        if (searching_years ? cal_surpasses(cal, sign, from, to, mid * sign, 0)
                            : cal_surpasses(cal, sign, from, to, years, mid * sign))
            hi = mid;
        else
            lo = mid;
    }
    return lo * sign;
}

void tp_ncal_date_until(TPCalendar cal, TPISODate one, TPISODate two, int largest_unit, TPDateDuration *out)
{
    int sign = -tp_compare_iso_date(one, two);
    TPCalendarDate from, to;
    TPCalYear y;
    TPCalYmd added;
    int64_t years = 0, months = 0, days, regulated;
    int num;
    bool leap;

    memset(out, 0, sizeof(*out));
    if (sign == 0)
        return;
    tp_ncal_iso_to_date(cal, one, &from);
    tp_ncal_iso_to_date(cal, two, &to);
    if (largest_unit == TP_UNIT_YEAR)
        years = cal_search(cal, sign, &from, &to, 0, true);
    if (largest_unit == TP_UNIT_YEAR || largest_unit == TP_UNIT_MONTH)
        months = cal_search(cal, sign, &from, &to, years, false);
    num = from.month_code;
    leap = from.month_code_leap;
    cal_year(cal, from.year + years, &y);
    if (!cal_year_contains_code(&y, num, leap))
        cal_constrain_code(NULL, cal, &y, &num, &leap, TP_OVERFLOW_CONSTRAIN);
    cal_add_months(cal, y.year, cal_code_to_ordinal(&y, num, leap), months, &added);
    cal_year(cal, added.year, &y);
    regulated = from.day <= y.len[added.month - 1] ? from.day : y.len[added.month - 1];
    days = tp_iso_date_epoch_days(two) - (cal_month_start(&y, added.month) + regulated - 1);
    out->years = years;
    out->months = months;
    if (largest_unit == TP_UNIT_WEEK) {
        out->weeks = days / 7;
        days %= 7;
    }
    out->days = days;
}

int tp_ncal_date_add(JSContext *ctx, TPCalendar cal, TPISODate d, const TPDateDuration *dur, int overflow,
                     TPISODate *out)
{
    TPCalendarDate parts;
    TPCalYear y;
    TPCalYmd added, balanced;
    int num;
    bool leap;
    int64_t regulated;

    tp_ncal_iso_to_date(cal, d, &parts);
    if (cal_year_or_throw(ctx, cal, parts.year + dur->years, &y))
        return -1;
    num = parts.month_code;
    leap = parts.month_code_leap;
    if (cal_constrain_code(ctx, cal, &y, &num, &leap, overflow))
        return -1;
    if (!cal_add_months(cal, y.year, cal_code_to_ordinal(&y, num, leap), dur->months, &added) ||
        !cal_year(cal, added.year, &y)) {
        JS_ThrowRangeError(ctx, "date is outside the supported range");
        return -1;
    }
    regulated = parts.day;
    if (regulated > y.len[added.month - 1]) {
        if (overflow == TP_OVERFLOW_REJECT) {
            JS_ThrowRangeError(ctx, "day is out of range for this month");
            return -1;
        }
        regulated = y.len[added.month - 1];
    }
    if (!cal_balance(cal, added.year, added.month, regulated + 7 * dur->weeks + dur->days, &balanced) ||
        !cal_year(cal, balanced.year, &y)) {
        JS_ThrowRangeError(ctx, "date is outside the supported range");
        return -1;
    }
    if (cal_integers_to_iso(ctx, &y, balanced.month, balanced.day, out))
        return -1;
    return cal_check_iso(ctx, *out);
}
