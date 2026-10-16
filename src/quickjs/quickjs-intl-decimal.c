/* quickjs-intl-decimal.c: exact decimal values, ECMA-402 rounding and CLDR plural rule evaluation. */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "quickjs-intl.h"
#include "libunicode.h"

void intl_decimal_free(IntlDecimal *d)
{
    free(d->digits);
    d->digits = NULL;
    d->nd = 0;
}

static int set_digits(IntlDecimal *d, const char *s, size_t len, long long exp)
{
    size_t start = 0, end = len;
    while (start < end && s[start] == '0')
        start++;
    while (end > start && s[end - 1] == '0') {
        end--;
        exp++;
    }
    free(d->digits);
    d->digits = NULL;
    d->nd = 0;
    d->exp = 0;
    if (start == end)
        return 0;
    d->digits = malloc(end - start + 1);
    if (!d->digits)
        return -1;
    memcpy(d->digits, s + start, end - start);
    d->digits[end - start] = '\0';
    d->nd = (int)(end - start);
    if (exp > 1000000000LL)
        exp = 1000000000LL;
    if (exp < -1000000000LL)
        exp = -1000000000LL;
    d->exp = (int)exp;
    return 0;
}

int intl_decimal_copy(IntlDecimal *dst, const IntlDecimal *src)
{
    *dst = *src;
    dst->digits = NULL;
    if (src->digits) {
        dst->digits = malloc(src->nd + 1);
        if (!dst->digits)
            return -1;
        memcpy(dst->digits, src->digits, src->nd + 1);
    }
    return 0;
}

int intl_decimal_magnitude(const IntlDecimal *d)
{
    return d->nd - 1 + d->exp;
}

static int base_to_decimal(const char *s, size_t len, int base, IntlDecimal *d)
{
    size_t cap = len * 2 + 2, n = 1;
    unsigned char *acc = calloc(cap, 1);
    char *out;
    int r;
    if (!acc)
        return -1;
    for (size_t i = 0; i < len; i++) {
        int c = s[i], v;
        if (c >= '0' && c <= '9')
            v = c - '0';
        else
            v = (c | 0x20) - 'a' + 10;
        for (size_t k = 0; k < n; k++) {
            int t = acc[k] * base + v;
            acc[k] = t % 10;
            v = t / 10;
        }
        while (v) {
            acc[n++] = v % 10;
            v /= 10;
        }
    }
    out = malloc(n + 1);
    if (!out) {
        free(acc);
        return -1;
    }
    for (size_t k = 0; k < n; k++)
        out[k] = '0' + acc[n - 1 - k];
    out[n] = '\0';
    free(acc);
    r = set_digits(d, out, n, 0);
    free(out);
    return r;
}

static int dec_utf8_decode(const char *s, size_t len, size_t *pos)
{
    unsigned char c = s[*pos];
    int cp, n;
    if (c < 0x80) {
        (*pos)++;
        return c;
    }
    if ((c & 0xe0) == 0xc0) {
        cp = c & 0x1f;
        n = 1;
    } else if ((c & 0xf0) == 0xe0) {
        cp = c & 0x0f;
        n = 2;
    } else {
        cp = c & 0x07;
        n = 3;
    }
    (*pos)++;
    while (n-- > 0 && *pos < len) {
        cp = (cp << 6) | (s[*pos] & 0x3f);
        (*pos)++;
    }
    return cp;
}

static void trim_space(const char *s, size_t len, size_t *pstart, size_t *pend)
{
    size_t start = 0, end = len;
    while (start < end) {
        size_t p = start;
        int c = dec_utf8_decode(s, len, &p);
        if (!lre_is_space(c))
            break;
        start = p;
    }
    while (end > start) {
        size_t q = end - 1;
        size_t p;
        while (q > start && (s[q] & 0xc0) == 0x80)
            q--;
        p = q;
        if (!lre_is_space(dec_utf8_decode(s, len, &p)))
            break;
        end = q;
    }
    *pstart = start;
    *pend = end;
}

static int parse_string_mv(const char *s, size_t len, IntlDecimal *d)
{
    size_t start, end, i, int_start, int_end, frac_start = 0, frac_end = 0;
    bool neg = false, has_int, has_frac = false;
    long long e = 0;
    char *buf;
    int r;

    memset(d, 0, sizeof(*d));
    d->kind = INTL_DEC_FINITE;
    trim_space(s, len, &start, &end);
    if (start == end)
        return 0;
    if (end - start > 2 && s[start] == '0') {
        int base = 0;
        char c = s[start + 1] | 0x20;
        if (c == 'x')
            base = 16;
        else if (c == 'o')
            base = 8;
        else if (c == 'b')
            base = 2;
        if (base) {
            for (i = start + 2; i < end; i++) {
                int v, ch = s[i];
                if (ch >= '0' && ch <= '9')
                    v = ch - '0';
                else if ((ch | 0x20) >= 'a' && (ch | 0x20) <= 'f')
                    v = (ch | 0x20) - 'a' + 10;
                else
                    v = 99;
                if (v >= base)
                    goto nan;
            }
            return base_to_decimal(s + start + 2, end - start - 2, base, d);
        }
    }
    i = start;
    if (s[i] == '+' || s[i] == '-') {
        neg = s[i] == '-';
        i++;
    }
    if (end - i == 8 && !memcmp(s + i, "Infinity", 8)) {
        d->kind = neg ? INTL_DEC_NEG_INF : INTL_DEC_POS_INF;
        d->negative = neg;
        return 0;
    }
    int_start = i;
    while (i < end && s[i] >= '0' && s[i] <= '9')
        i++;
    int_end = i;
    has_int = int_end > int_start;
    if (i < end && s[i] == '.') {
        i++;
        frac_start = i;
        while (i < end && s[i] >= '0' && s[i] <= '9')
            i++;
        frac_end = i;
        has_frac = frac_end > frac_start;
    }
    if (!has_int && !has_frac)
        goto nan;
    if (i < end && (s[i] == 'e' || s[i] == 'E')) {
        bool eneg = false;
        size_t es;
        i++;
        if (i < end && (s[i] == '+' || s[i] == '-')) {
            eneg = s[i] == '-';
            i++;
        }
        es = i;
        while (i < end && s[i] >= '0' && s[i] <= '9') {
            if (e < 100000000000LL)
                e = e * 10 + (s[i] - '0');
            i++;
        }
        if (i == es)
            goto nan;
        if (eneg)
            e = -e;
    }
    if (i != end)
        goto nan;
    buf = malloc((int_end - int_start) + (frac_end - frac_start) + 1);
    if (!buf)
        return -1;
    memcpy(buf, s + int_start, int_end - int_start);
    memcpy(buf + (int_end - int_start), s + frac_start, frac_end - frac_start);
    r = set_digits(d, buf, (int_end - int_start) + (frac_end - frac_start),
                   e - (long long)(frac_end - frac_start));
    free(buf);
    d->negative = neg;
    return r;
nan:
    d->kind = INTL_DEC_NAN;
    return 0;
}

static int round_mv_result(IntlDecimal *d)
{
    long long mag;
    char buf[900];
    double v;
    int n;
    if (d->kind != INTL_DEC_FINITE || d->nd == 0)
        return 0;
    mag = (long long)d->nd - 1 + d->exp;
    if (mag > 400) {
        intl_decimal_free(d);
        d->kind = d->negative ? INTL_DEC_NEG_INF : INTL_DEC_POS_INF;
        return 0;
    }
    if (mag < -400) {
        intl_decimal_free(d);
        return 0;
    }
    n = d->nd < 800 ? d->nd : 800;
    snprintf(buf, sizeof(buf), "%.*se%d", n, d->digits, d->exp + (d->nd - n));
    v = strtod(buf, NULL);
    if (isinf(v)) {
        intl_decimal_free(d);
        d->kind = d->negative ? INTL_DEC_NEG_INF : INTL_DEC_POS_INF;
    } else if (v == 0) {
        intl_decimal_free(d);
    }
    return 0;
}

int intl_decimal_from_string(const char *s, size_t len, IntlDecimal *d)
{
    if (parse_string_mv(s, len, d) < 0)
        return -1;
    return round_mv_result(d);
}

int intl_decimal_from_double(double x, IntlDecimal *d)
{
    char buf[64];
    memset(d, 0, sizeof(*d));
    if (isnan(x)) {
        d->kind = INTL_DEC_NAN;
        return 0;
    }
    if (isinf(x)) {
        d->kind = x < 0 ? INTL_DEC_NEG_INF : INTL_DEC_POS_INF;
        d->negative = x < 0;
        return 0;
    }
    if (x == 0) {
        d->kind = INTL_DEC_FINITE;
        d->negative = signbit(x) != 0;
        return 0;
    }
    snprintf(buf, sizeof(buf), "%.17g", x);
    {
        int prec;
        for (prec = 1; prec <= 17; prec++) {
            snprintf(buf, sizeof(buf), "%.*e", prec - 1, x);
            if (strtod(buf, NULL) == x)
                break;
        }
    }
    return intl_decimal_from_string(buf, strlen(buf), d);
}

int intl_to_intl_mv(JSContext *ctx, JSValueConst value, IntlDecimal *d)
{
    JSValue prim = js_intl_to_primitive_number(ctx, value);
    const char *s;
    size_t len;
    int r;
    memset(d, 0, sizeof(*d));
    if (JS_IsException(prim))
        return -1;
    if (JS_IsBigInt(prim) || JS_IsString(prim)) {
        s = JS_ToCStringLen(ctx, &len, prim);
        JS_FreeValue(ctx, prim);
        if (!s)
            return -1;
        if (strlen(s) != len) {
            JS_FreeCString(ctx, s);
            d->kind = INTL_DEC_NAN;
            return 0;
        }
        r = intl_decimal_from_string(s, len, d);
        JS_FreeCString(ctx, s);
        if (r < 0)
            JS_ThrowOutOfMemory(ctx);
        return r;
    } else {
        double x;
        r = JS_ToFloat64(ctx, &x, prim);
        JS_FreeValue(ctx, prim);
        if (r < 0)
            return -1;
        if (intl_decimal_from_double(x, d) < 0) {
            JS_ThrowOutOfMemory(ctx);
            return -1;
        }
        return 0;
    }
}

enum {
    UROUND_INFINITY,
    UROUND_ZERO,
    UROUND_HALF_INFINITY,
    UROUND_HALF_ZERO,
    UROUND_HALF_EVEN,
};

static int unsigned_rounding_mode(int mode, bool negative)
{
    switch (mode) {
    case INTL_ROUND_CEIL: return negative ? UROUND_ZERO : UROUND_INFINITY;
    case INTL_ROUND_FLOOR: return negative ? UROUND_INFINITY : UROUND_ZERO;
    case INTL_ROUND_EXPAND: return UROUND_INFINITY;
    case INTL_ROUND_TRUNC: return UROUND_ZERO;
    case INTL_ROUND_HALF_CEIL: return negative ? UROUND_HALF_ZERO : UROUND_HALF_INFINITY;
    case INTL_ROUND_HALF_FLOOR: return negative ? UROUND_HALF_INFINITY : UROUND_HALF_ZERO;
    case INTL_ROUND_HALF_EXPAND: return UROUND_HALF_INFINITY;
    case INTL_ROUND_HALF_TRUNC: return UROUND_HALF_ZERO;
    default: return UROUND_HALF_EVEN;
    }
}

static int str_mod_small(const char *s, int m)
{
    int r = 0;
    for (; *s; s++)
        r = (r * 10 + (*s - '0')) % m;
    return r;
}

static int str_quotient_parity(const char *s, int m)
{
    int r = 0, last = 0;
    for (; *s; s++) {
        int cur = r * 10 + (*s - '0');
        last = cur / m;
        r = cur % m;
    }
    return last & 1;
}

static char *str_add_small(const char *s, int add, int sub)
{
    size_t len = strlen(s);
    char *out = malloc(len + 2);
    int carry = add - sub;
    if (!out)
        return NULL;
    out[len + 1] = '\0';
    for (size_t k = len; k > 0; k--) {
        int v = (s[k - 1] - '0') + carry;
        carry = 0;
        while (v < 0) {
            v += 10;
            carry--;
        }
        carry += v / 10;
        out[k] = '0' + v % 10;
    }
    out[0] = '0' + carry;
    {
        size_t p = 0;
        while (out[p] == '0' && out[p + 1])
            p++;
        memmove(out, out + p, len + 2 - p);
    }
    return out;
}

enum { FRAC_EXACT, FRAC_BELOW, FRAC_HALF, FRAC_ABOVE };

static char *scaled_floor(const IntlDecimal *x, int shift, int *frac)
{
    long long s = (long long)x->exp + shift;
    char *out;
    *frac = FRAC_EXACT;
    if (x->nd == 0) {
        out = malloc(2);
        if (out)
            strcpy(out, "0");
        return out;
    }
    if (s >= 0) {
        if (s > 100000)
            s = 100000;
        out = malloc(x->nd + s + 1);
        if (!out)
            return NULL;
        memcpy(out, x->digits, x->nd);
        memset(out + x->nd, '0', s);
        out[x->nd + s] = '\0';
        return out;
    }
    {
        long long k = x->nd + s;
        const char *fd;
        long long flen;
        if (k <= 0) {
            out = malloc(2);
            if (!out)
                return NULL;
            strcpy(out, "0");
            if (k < 0) {
                *frac = FRAC_BELOW;
                return out;
            }
            fd = x->digits;
            flen = x->nd;
        } else {
            out = malloc(k + 1);
            if (!out)
                return NULL;
            memcpy(out, x->digits, k);
            out[k] = '\0';
            fd = x->digits + k;
            flen = x->nd - k;
        }
        if (fd[0] > '5')
            *frac = FRAC_ABOVE;
        else if (fd[0] < '5')
            *frac = FRAC_BELOW;
        else
            *frac = flen > 1 ? FRAC_ABOVE : FRAC_HALF;
        return out;
    }
}

static char *round_scaled(const IntlDecimal *x, int shift, int inc, int umode)
{
    int frac, rem, cmp;
    bool exact, take_upper;
    char *n = scaled_floor(x, shift, &frac), *n1, *res;
    if (!n)
        return NULL;
    rem = str_mod_small(n, inc);
    exact = rem == 0 && frac == FRAC_EXACT;
    if (exact)
        return n;
    n1 = rem ? str_add_small(n, 0, rem) : n;
    if (rem && !n1) {
        free(n);
        return NULL;
    }
    if (frac == FRAC_EXACT) {
        cmp = 2 * rem < inc ? -1 : 2 * rem > inc ? 1 : 0;
    } else if (2 * rem + 2 <= inc) {
        cmp = -1;
    } else if (2 * rem >= inc) {
        cmp = 1;
    } else {
        cmp = frac == FRAC_BELOW ? -1 : frac == FRAC_HALF ? 0 : 1;
    }
    switch (umode) {
    case UROUND_ZERO: take_upper = false; break;
    case UROUND_INFINITY: take_upper = true; break;
    case UROUND_HALF_ZERO: take_upper = cmp > 0; break;
    case UROUND_HALF_INFINITY: take_upper = cmp >= 0; break;
    default:
        take_upper = cmp > 0 || (cmp == 0 && str_quotient_parity(n1, inc));
        break;
    }
    if (take_upper) {
        res = str_add_small(n1, inc, 0);
        if (n1 != n)
            free(n1);
        free(n);
        return res;
    }
    if (n1 != n)
        free(n);
    return n1;
}

static int raw_set_rounded(IntlRawResult *r, const char *n, int exp)
{
    return set_digits(&r->rounded, n, strlen(n), exp);
}

static int raw_fixed(const IntlDecimal *x, int min_f, int max_f, int inc, int umode, IntlRawResult *r)
{
    int f = max_f;
    char *n = round_scaled(x, f, inc, umode);
    size_t k;
    DynBuf b;
    if (!n)
        return -1;
    if (raw_set_rounded(r, n, -f) < 0) {
        free(n);
        return -1;
    }
    dbuf_init(&b);
    k = strlen(n);
    if (f != 0) {
        size_t zeros = k <= (size_t)f ? (size_t)f + 1 - k : 0;
        size_t total = k + zeros, a = total - f;
        for (size_t z = 0; z < zeros; z++)
            dbuf_putc(&b, '0');
        dbuf_put(&b, (const uint8_t *)n, k);
        if (!b.error) {
            char *tmp = malloc(total + 2);
            if (!tmp) {
                free(n);
                dbuf_free(&b);
                return -1;
            }
            memcpy(tmp, b.buf, a);
            tmp[a] = '.';
            memcpy(tmp + a + 1, b.buf + a, f);
            tmp[total + 1] = '\0';
            b.size = 0;
            dbuf_put(&b, (const uint8_t *)tmp, total + 2);
            free(tmp);
        }
        r->int_digits = (int)a;
    } else {
        dbuf_putstr(&b, n);
        dbuf_putc(&b, '\0');
        r->int_digits = (int)k;
    }
    free(n);
    if (b.error) {
        dbuf_free(&b);
        return -1;
    }
    {
        char *m = (char *)b.buf;
        size_t len = strlen(m);
        int cut = max_f - min_f;
        while (cut > 0 && len > 0 && m[len - 1] == '0') {
            m[--len] = '\0';
            cut--;
        }
        if (len > 0 && m[len - 1] == '.')
            m[--len] = '\0';
        r->str = m;
    }
    r->rounding_magnitude = -f;
    return 0;
}

static int raw_precision(const IntlDecimal *x, int min_p, int max_p, int umode, IntlRawResult *r)
{
    int p = max_p, e;
    char *n;
    DynBuf b;
    dbuf_init(&b);
    if (x->nd == 0) {
        for (int i = 0; i < p; i++)
            dbuf_putc(&b, '0');
        e = 0;
        memset(&r->rounded, 0, sizeof(r->rounded));
        n = NULL;
    } else {
        e = intl_decimal_magnitude(x);
        n = round_scaled(x, p - 1 - e, 1, umode);
        if (!n)
            return -1;
        if ((int)strlen(n) > p) {
            n[p] = '\0';
            e++;
        }
        if (raw_set_rounded(r, n, e - p + 1) < 0) {
            free(n);
            return -1;
        }
        dbuf_putstr(&b, n);
    }
    dbuf_putc(&b, '\0');
    free(n);
    if (b.error) {
        dbuf_free(&b);
        return -1;
    }
    {
        char *m = (char *)b.buf;
        DynBuf o;
        dbuf_init(&o);
        if (e >= p - 1) {
            dbuf_putstr(&o, m);
            for (int i = 0; i < e - p + 1; i++)
                dbuf_putc(&o, '0');
            r->int_digits = e + 1;
        } else if (e >= 0) {
            dbuf_put(&o, (const uint8_t *)m, e + 1);
            dbuf_putc(&o, '.');
            dbuf_putstr(&o, m + e + 1);
            r->int_digits = e + 1;
        } else {
            dbuf_putstr(&o, "0.");
            for (int i = 0; i < -(e + 1); i++)
                dbuf_putc(&o, '0');
            dbuf_putstr(&o, m);
            r->int_digits = 1;
        }
        dbuf_putc(&o, '\0');
        dbuf_free(&b);
        if (o.error) {
            dbuf_free(&o);
            return -1;
        }
        m = (char *)o.buf;
        if (strchr(m, '.') && max_p > min_p) {
            size_t len = strlen(m);
            int cut = max_p - min_p;
            while (cut > 0 && m[len - 1] == '0') {
                m[--len] = '\0';
                cut--;
            }
            if (m[len - 1] == '.')
                m[--len] = '\0';
        }
        r->str = m;
    }
    r->rounding_magnitude = e - p + 1;
    return 0;
}

void intl_raw_result_free(IntlRawResult *r)
{
    free(r->str);
    r->str = NULL;
    intl_decimal_free(&r->rounded);
}

int intl_format_numeric_to_string(const IntlDigitOptions *o, const IntlDecimal *x, IntlRawResult *out)
{
    IntlDecimal ax = *x;
    bool negative = x->negative;
    int umode;
    IntlRawResult res, other;
    memset(&res, 0, sizeof(res));
    memset(&other, 0, sizeof(other));
    memset(out, 0, sizeof(*out));
    ax.negative = false;
    umode = unsigned_rounding_mode(o->rounding_mode, negative);
    if (o->rounding_type == INTL_ROUNDING_SIGNIFICANT) {
        if (raw_precision(&ax, o->min_sig, o->max_sig, umode, &res) < 0)
            return -1;
    } else if (o->rounding_type == INTL_ROUNDING_FRACTION) {
        if (raw_fixed(&ax, o->min_frac, o->max_frac, o->rounding_increment, umode, &res) < 0)
            return -1;
    } else {
        bool fixed_more;
        if (raw_precision(&ax, o->min_sig, o->max_sig, umode, &res) < 0)
            return -1;
        if (raw_fixed(&ax, o->min_frac, o->max_frac, o->rounding_increment, umode, &other) < 0) {
            intl_raw_result_free(&res);
            return -1;
        }
        fixed_more = other.rounding_magnitude < res.rounding_magnitude;
        if ((o->rounding_type == INTL_ROUNDING_MORE && fixed_more) ||
            (o->rounding_type == INTL_ROUNDING_LESS && !fixed_more)) {
            intl_raw_result_free(&res);
            res = other;
        } else {
            intl_raw_result_free(&other);
        }
    }
    if (o->trailing_zero_strip && (res.rounded.nd == 0 || res.rounded.exp >= 0)) {
        char *dot = strchr(res.str, '.');
        if (dot)
            *dot = '\0';
    }
    if (res.int_digits < o->min_int) {
        size_t len = strlen(res.str);
        int pad = o->min_int - res.int_digits;
        char *m = malloc(len + pad + 1);
        if (!m) {
            intl_raw_result_free(&res);
            return -1;
        }
        memset(m, '0', pad);
        memcpy(m + pad, res.str, len + 1);
        free(res.str);
        res.str = m;
    }
    res.rounded.kind = INTL_DEC_FINITE;
    res.rounded.negative = negative;
    *out = res;
    return 0;
}

typedef struct PluralOperands {
    const char *i;
    size_t ilen;
    const char *f;
    size_t flen;
    size_t tlen;
    int e;
} PluralOperands;

static unsigned long long digits_value(const char *s, size_t len, int mod)
{
    unsigned long long v = 0;
    if (mod > 0) {
        for (size_t k = 0; k < len; k++)
            v = (v * 10 + (s[k] - '0')) % (unsigned)mod;
        return v;
    }
    while (len > 1 && *s == '0') {
        s++;
        len--;
    }
    if (len > 18)
        return ~0ULL;
    for (size_t k = 0; k < len; k++)
        v = v * 10 + (s[k] - '0');
    return v;
}

static const char *skip_ws(const char *p)
{
    while (*p == ' ')
        p++;
    return p;
}

static bool match_word(const char **pp, const char *w)
{
    const char *p = skip_ws(*pp);
    size_t n = strlen(w);
    if (!strncmp(p, w, n) && (p[n] == ' ' || p[n] == '\0')) {
        *pp = p + n;
        return true;
    }
    return false;
}

static unsigned long long parse_uint(const char **pp)
{
    const char *p = skip_ws(*pp);
    unsigned long long v = 0;
    while (*p >= '0' && *p <= '9')
        v = v * 10 + (*p++ - '0');
    *pp = p;
    return v;
}

static bool eval_relation(const char **pp, const PluralOperands *ops)
{
    const char *p = skip_ws(*pp);
    char op = *p++;
    int mod = 0;
    bool is_n = false, n_has_frac = false, eq, found = false;
    unsigned long long val;
    p = skip_ws(p);
    if (*p == '%') {
        p++;
        mod = (int)parse_uint(&p);
    }
    switch (op) {
    case 'n':
        is_n = true;
        n_has_frac = ops->tlen > 0;
        val = digits_value(ops->i, ops->ilen, mod);
        break;
    case 'i':
        val = digits_value(ops->i, ops->ilen, mod);
        break;
    case 'v':
        val = mod ? ops->flen % mod : ops->flen;
        break;
    case 'w':
        val = mod ? ops->tlen % mod : ops->tlen;
        break;
    case 'f':
        val = ops->flen ? digits_value(ops->f, ops->flen, mod) : 0;
        break;
    case 't':
        val = ops->tlen ? digits_value(ops->f, ops->tlen, mod) : 0;
        break;
    case 'c':
    case 'e':
        val = mod ? (unsigned)ops->e % mod : (unsigned)ops->e;
        break;
    default:
        val = 0;
        break;
    }
    p = skip_ws(p);
    if (p[0] == '!' && p[1] == '=') {
        eq = false;
        p += 2;
    } else {
        eq = true;
        if (*p == '=')
            p++;
    }
    for (;;) {
        unsigned long long lo = parse_uint(&p), hi = lo;
        if (p[0] == '.' && p[1] == '.') {
            p += 2;
            hi = parse_uint(&p);
        }
        if (!(is_n && n_has_frac) && val >= lo && val <= hi)
            found = true;
        p = skip_ws(p);
        if (*p != ',')
            break;
        p++;
    }
    *pp = p;
    return eq ? found : !found;
}

static bool eval_condition(const char *rule, const PluralOperands *ops)
{
    const char *p = rule;
    bool or_result = false;
    if (!strcmp(rule, "-") || !*rule)
        return false;
    for (;;) {
        bool and_result = true;
        for (;;) {
            if (!eval_relation(&p, ops))
                and_result = false;
            if (!match_word(&p, "and"))
                break;
        }
        if (and_result)
            or_result = true;
        if (!match_word(&p, "or"))
            break;
    }
    return or_result;
}

const char *const intl_plural_categories[6] = { "zero", "one", "two", "few", "many", "other" };

int intl_plural_select(int data, bool ordinal, const char *formatted, int exponent)
{
    PluralOperands ops;
    const char *dot = strchr(formatted, '.');
    memset(&ops, 0, sizeof(ops));
    if (*formatted == '-')
        formatted++;
    ops.i = formatted;
    ops.ilen = dot ? (size_t)(dot - formatted) : strlen(formatted);
    if (dot) {
        ops.f = dot + 1;
        ops.flen = strlen(dot + 1);
        ops.tlen = ops.flen;
        while (ops.tlen > 0 && ops.f[ops.tlen - 1] == '0')
            ops.tlen--;
    }
    ops.e = exponent;
    for (int c = 0; c < 5; c++) {
        const char *rule = intl_data_getf(data, "p.%c.%s", ordinal ? 'o' : 'c', intl_plural_categories[c]);
        if (rule && eval_condition(rule, &ops))
            return c;
    }
    return 5;
}

int intl_plural_category_list(int data, bool ordinal, int *cats)
{
    int n = 0;
    for (int c = 0; c < 5; c++)
        if (intl_data_getf(data, "p.%c.%s", ordinal ? 'o' : 'c', intl_plural_categories[c]))
            cats[n++] = c;
    cats[n++] = 5;
    return n;
}

int intl_plural_select_range(int data, int start, int end)
{
    const char *r = intl_data_getf(data, "p.r.%s.%s", intl_plural_categories[start], intl_plural_categories[end]);
    if (r) {
        for (int c = 0; c < 6; c++)
            if (!strcmp(intl_plural_categories[c], r))
                return c;
    }
    return end;
}
