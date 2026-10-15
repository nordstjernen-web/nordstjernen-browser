/* quickjs-intl-calendar.c: conversion of fixed day numbers to the dates of the calendars Intl.DateTimeFormat supports. */

#include <string.h>

#include "quickjs-intl.h"
#include "quickjs-temporal.h"

#define RD_EPOCH_1970 719163

static int64_t floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q--;
    return q;
}

static int64_t floor_mod(int64_t a, int64_t b)
{
    return a - floor_div(a, b) * b;
}

static bool greg_leap(int64_t y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

int64_t intl_fixed_from_gregorian(int64_t y, int m, int d)
{
    int64_t y1 = y - 1;
    return 365 * y1 + floor_div(y1, 4) - floor_div(y1, 100) + floor_div(y1, 400) +
           floor_div(367 * m - 362, 12) + (m <= 2 ? 0 : greg_leap(y) ? -1 : -2) + d;
}

void intl_gregorian_from_fixed(int64_t date, int64_t *py, int *pm, int *pd)
{
    int64_t d0 = date - 1, n400 = floor_div(d0, 146097), d1 = floor_mod(d0, 146097);
    int64_t n100 = floor_div(d1, 36524), d2 = floor_mod(d1, 36524);
    int64_t n4 = floor_div(d2, 1461), d3 = floor_mod(d2, 1461);
    int64_t n1 = floor_div(d3, 365);
    int64_t year = 400 * n400 + 100 * n100 + 4 * n4 + n1;
    int64_t prior, correction;
    int month;
    if (!(n100 == 4 || n1 == 4))
        year++;
    prior = date - intl_fixed_from_gregorian(year, 1, 1);
    correction = date < intl_fixed_from_gregorian(year, 3, 1) ? 0 : greg_leap(year) ? 1 : 2;
    month = (int)floor_div(12 * (prior + correction) + 373, 367);
    *py = year;
    *pm = month;
    *pd = (int)(date - intl_fixed_from_gregorian(year, month, 1) + 1);
}
static void temporal_fields(TPCalendar cal, int64_t y, int m, int d, IntlCalendarDate *out)
{
    TPCalendarDate cd;
    TPISODate iso = { y, m, d };
    tp_calendar_iso_to_date(cal, iso, &cd);
    out->year = cd.year;
    out->month = cd.month_code;
    out->month_key = cd.month_code;
    out->leap_month = cd.month_code_leap;
    out->ordinal_month = cd.month;
    out->day = cd.day;
}

static const int64_t japanese_era_starts[][3] = {
    { 1868, 10, 23 }, { 1912, 7, 30 }, { 1926, 12, 25 }, { 1989, 1, 8 }, { 2019, 5, 1 },
};
int intl_calendar_compute(int cal, int64_t date, IntlCalendarDate *out)
{
    int64_t y;
    int m, d;
    memset(out, 0, sizeof(*out));
    intl_gregorian_from_fixed(date, &y, &m, &d);
    out->weekday = (int)floor_mod(date, 7);
    out->iso_year = y;
    switch (cal) {
    case INTL_CAL_GREGORY:
    case INTL_CAL_ISO8601:
        out->year = y;
        out->era = y > 0 ? 1 : 0;
        out->era_year = y > 0 ? y : 1 - y;
        out->month = m;
        out->month_key = m;
        out->day = d;
        return 0;
    case INTL_CAL_BUDDHIST:
        out->year = y + 543;
        out->era = 0;
        out->era_year = out->year;
        out->month = m;
        out->month_key = m;
        out->day = d;
        return 0;
    case INTL_CAL_ROC:
        out->year = y - 1911;
        out->era = y > 1911 ? 1 : 0;
        out->era_year = y > 1911 ? y - 1911 : 1912 - y;
        out->month = m;
        out->month_key = m;
        out->day = d;
        return 0;
    case INTL_CAL_JAPANESE:
        out->month = m;
        out->month_key = m;
        out->day = d;
        out->year = y;
        out->era = -1;
        for (int i = 4; i >= 0; i--) {
            if (date >= intl_fixed_from_gregorian(japanese_era_starts[i][0], (int)japanese_era_starts[i][1],
                                                  (int)japanese_era_starts[i][2])) {
                out->era = 232 + i;
                out->era_year = y - japanese_era_starts[i][0] + 1;
                break;
            }
        }
        if (out->era < 0) {
            out->era_gregorian = true;
            out->era = y > 0 ? 1 : 0;
            out->era_year = y > 0 ? y : 1 - y;
        }
        return 0;
    case INTL_CAL_COPTIC:
        temporal_fields(TP_CAL_COPTIC, y, m, d, out);
        out->era = 1;
        out->era_year = out->year;
        return 0;
    case INTL_CAL_ETHIOPIC:
        temporal_fields(TP_CAL_ETHIOPIC, y, m, d, out);
        out->era = out->year > 0 ? 1 : 0;
        out->era_year = out->year > 0 ? out->year : out->year + 5500;
        return 0;
    case INTL_CAL_ETHIOAA:
        temporal_fields(TP_CAL_ETHIOAA, y, m, d, out);
        out->era = 0;
        out->era_year = out->year;
        return 0;
    case INTL_CAL_INDIAN:
    case INTL_CAL_PERSIAN:
        temporal_fields(cal == INTL_CAL_INDIAN ? TP_CAL_INDIAN : TP_CAL_PERSIAN, y, m, d, out);
        out->era = 0;
        out->era_year = out->year;
        return 0;
    case INTL_CAL_ISLAMIC_CIVIL:
    case INTL_CAL_ISLAMIC_UMALQURA:
    case INTL_CAL_ISLAMIC_TBLA:
        temporal_fields(cal == INTL_CAL_ISLAMIC_CIVIL ? TP_CAL_ISLAMIC_CIVIL :
                        cal == INTL_CAL_ISLAMIC_TBLA ? TP_CAL_ISLAMIC_TBLA : TP_CAL_ISLAMIC_UMALQURA, y, m, d, out);
        out->era = out->year > 0 ? 0 : 1;
        out->era_year = out->year > 0 ? out->year : 1 - out->year;
        return 0;
    case INTL_CAL_HEBREW: {
        TPCalendarDate cd;
        TPISODate iso = { y, m, d };
        tp_calendar_iso_to_date(TP_CAL_HEBREW, iso, &cd);
        out->year = cd.year;
        out->day = cd.day;
        out->era = 0;
        out->era_year = cd.year;
        out->ordinal_month = cd.month;
        if (cd.month_code_leap)
            out->month_key = 6;
        else if (cd.month_code <= 5)
            out->month_key = cd.month_code;
        else
            out->month_key = cd.month_code + 1;
        out->leap_key = cd.month_code == 6 && cd.in_leap_year;
        out->month = out->month_key;
        return 0;
    }
    case INTL_CAL_CHINESE:
    case INTL_CAL_DANGI:
        temporal_fields(cal == INTL_CAL_DANGI ? TP_CAL_DANGI : TP_CAL_CHINESE, y, m, d, out);
        out->related_year = out->year;
        out->cyclic_year = (int)floor_mod(out->year - 4, 60) + 1;
        out->era = 0;
        out->era_year = out->cyclic_year;
        return 0;
    default:
        return -1;
    }
}
