/* quickjs-intl-number.c: Intl.NumberFormat, Intl.PluralRules and Number/BigInt toLocaleString. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "quickjs-intl.h"
#include "libunicode.h"

static void str_concat(char *dst, size_t size, const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    if (la > size - 1)
        la = size - 1;
    if (lb > size - 1 - la)
        lb = size - 1 - la;
    memmove(dst, a, la);
    memmove(dst + la, b, lb);
    dst[la + lb] = '\0';
}

void intl_parts_init(IntlParts *p)
{
    memset(p, 0, sizeof(*p));
}

void intl_parts_free(IntlParts *p)
{
    for (int i = 0; i < p->count; i++)
        free(p->items[i].value);
    free(p->items);
    memset(p, 0, sizeof(*p));
}

int intl_parts_add(IntlParts *p, const char *type, const char *s, size_t len, int level)
{
    IntlPart *it;
    if (p->error)
        return -1;
    if (p->count == p->size) {
        int size = p->size ? p->size * 2 : 8;
        IntlPart *items = realloc(p->items, sizeof(IntlPart) * size);
        if (!items) {
            p->error = true;
            return -1;
        }
        p->items = items;
        p->size = size;
    }
    it = &p->items[p->count];
    it->value = malloc(len + 1);
    if (!it->value) {
        p->error = true;
        return -1;
    }
    memcpy(it->value, s, len);
    it->value[len] = '\0';
    it->type = type;
    it->level = level;
    it->source = NULL;
    p->count++;
    return 0;
}

int intl_parts_append(IntlParts *dst, const IntlParts *src, const char *source)
{
    for (int i = 0; i < src->count; i++) {
        if (intl_parts_add(dst, src->items[i].type, src->items[i].value, strlen(src->items[i].value),
                           src->items[i].level) < 0)
            return -1;
        dst->items[dst->count - 1].source = source ? source : src->items[i].source;
    }
    return 0;
}

char *intl_parts_join(const IntlParts *p)
{
    DynBuf b;
    dbuf_init(&b);
    for (int i = 0; i < p->count; i++)
        dbuf_putstr(&b, p->items[i].value);
    dbuf_putc(&b, '\0');
    if (b.error) {
        dbuf_free(&b);
        return NULL;
    }
    return (char *)b.buf;
}

JSValue intl_parts_to_string(JSContext *ctx, const IntlParts *p)
{
    char *s;
    JSValue r;
    if (p->error)
        return JS_ThrowOutOfMemory(ctx);
    s = intl_parts_join(p);
    if (!s)
        return JS_ThrowOutOfMemory(ctx);
    r = JS_NewString(ctx, s);
    free(s);
    return r;
}

JSValue intl_parts_to_array(JSContext *ctx, const IntlParts *p, const char *extra_name,
                            const char *extra_value, bool use_source)
{
    JSValue arr;
    if (p->error)
        return JS_ThrowOutOfMemory(ctx);
    arr = JS_NewArray(ctx);
    if (JS_IsException(arr))
        return arr;
    for (int i = 0; i < p->count; i++) {
        JSValue part = intl_make_part(ctx, p->items[i].type, p->items[i].value, strlen(p->items[i].value));
        if (JS_IsException(part))
            goto fail;
        if (extra_name && extra_value &&
            JS_DefinePropertyValueStr(ctx, part, extra_name, JS_NewString(ctx, extra_value), JS_PROP_C_W_E) < 0) {
            JS_FreeValue(ctx, part);
            goto fail;
        }
        if (use_source && p->items[i].source &&
            JS_DefinePropertyValueStr(ctx, part, "source", JS_NewString(ctx, p->items[i].source),
                                      JS_PROP_C_W_E) < 0) {
            JS_FreeValue(ctx, part);
            goto fail;
        }
        if (JS_DefinePropertyValueUint32(ctx, arr, i, part, JS_PROP_C_W_E) < 0)
            goto fail;
    }
    return arr;
fail:
    JS_FreeValue(ctx, arr);
    return JS_EXCEPTION;
}

static const char *const rounding_mode_names[] = {
    "ceil", "floor", "expand", "trunc", "halfCeil", "halfFloor", "halfExpand", "halfTrunc", "halfEven", NULL,
};
static const char *const rounding_priority_names[] = { "auto", "morePrecision", "lessPrecision", NULL };
static const char *const trailing_zero_names[] = { "auto", "stripIfInteger", NULL };

static int get_raw_option(JSContext *ctx, JSValueConst options, const char *name, JSValue *out)
{
    *out = JS_GetPropertyStr(ctx, options, name);
    return JS_IsException(*out) ? -1 : 0;
}

int intl_set_digit_options(JSContext *ctx, IntlDigitOptions *o, JSValueConst options,
                           int mnfd_default, int mxfd_default, bool compact)
{
    static const int increments[] = { 1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 2500, 5000 };
    JSValue mnfd = JS_UNDEFINED, mxfd = JS_UNDEFINED, mnsd = JS_UNDEFINED, mxsd = JS_UNDEFINED;
    int mnid, inc, mode, priority, tz, r = -1;
    bool has_sd, has_fd, need_sd = true, need_fd = true, ok = false;

    if (intl_get_number_option(ctx, options, "minimumIntegerDigits", 1, 21, 1, &mnid) < 0)
        return -1;
    if (get_raw_option(ctx, options, "minimumFractionDigits", &mnfd) < 0 ||
        get_raw_option(ctx, options, "maximumFractionDigits", &mxfd) < 0 ||
        get_raw_option(ctx, options, "minimumSignificantDigits", &mnsd) < 0 ||
        get_raw_option(ctx, options, "maximumSignificantDigits", &mxsd) < 0)
        goto done;
    o->min_int = mnid;
    if (intl_get_number_option(ctx, options, "roundingIncrement", 1, 5000, 1, &inc) < 0)
        goto done;
    for (size_t i = 0; i < countof(increments); i++)
        if (increments[i] == inc)
            ok = true;
    if (!ok) {
        JS_ThrowRangeError(ctx, "invalid roundingIncrement");
        goto done;
    }
    mode = intl_get_enum_option(ctx, options, "roundingMode", rounding_mode_names, INTL_ROUND_HALF_EXPAND);
    if (mode < -1)
        goto done;
    priority = intl_get_enum_option(ctx, options, "roundingPriority", rounding_priority_names, 0);
    if (priority < -1)
        goto done;
    tz = intl_get_enum_option(ctx, options, "trailingZeroDisplay", trailing_zero_names, 0);
    if (tz < -1)
        goto done;
    if (inc != 1)
        mxfd_default = mnfd_default;
    o->rounding_increment = inc;
    o->rounding_mode = mode;
    o->trailing_zero_strip = tz == 1;
    has_sd = !JS_IsUndefined(mnsd) || !JS_IsUndefined(mxsd);
    has_fd = !JS_IsUndefined(mnfd) || !JS_IsUndefined(mxfd);
    o->min_sig = o->max_sig = -1;
    o->min_frac = o->max_frac = -1;
    if (priority == 0) {
        need_sd = has_sd;
        if (need_sd || (!has_fd && compact))
            need_fd = false;
    }
    if (need_sd) {
        if (has_sd) {
            if (intl_default_number_option(ctx, mnsd, 1, 21, 1, &o->min_sig) < 0 ||
                intl_default_number_option(ctx, mxsd, o->min_sig, 21, 21, &o->max_sig) < 0)
                goto done;
        } else {
            o->min_sig = 1;
            o->max_sig = 21;
        }
    }
    if (need_fd) {
        if (has_fd) {
            int a, b;
            if (intl_default_number_option(ctx, mnfd, 0, 100, -1, &a) < 0 ||
                intl_default_number_option(ctx, mxfd, 0, 100, -1, &b) < 0)
                goto done;
            if (a == -1)
                a = mnfd_default < b ? mnfd_default : b;
            else if (b == -1)
                b = mxfd_default > a ? mxfd_default : a;
            else if (a > b) {
                JS_ThrowRangeError(ctx, "minimumFractionDigits is greater than maximumFractionDigits");
                goto done;
            }
            o->min_frac = a;
            o->max_frac = b;
        } else {
            o->min_frac = mnfd_default;
            o->max_frac = mxfd_default;
        }
    }
    if (!need_sd && !need_fd) {
        o->min_frac = 0;
        o->max_frac = 0;
        o->min_sig = 1;
        o->max_sig = 2;
        o->rounding_type = INTL_ROUNDING_MORE;
        o->rounding_priority = 1;
    } else if (priority == 1) {
        o->rounding_type = INTL_ROUNDING_MORE;
        o->rounding_priority = 1;
    } else if (priority == 2) {
        o->rounding_type = INTL_ROUNDING_LESS;
        o->rounding_priority = 2;
    } else if (has_sd) {
        o->rounding_type = INTL_ROUNDING_SIGNIFICANT;
        o->rounding_priority = 0;
    } else {
        o->rounding_type = INTL_ROUNDING_FRACTION;
        o->rounding_priority = 0;
    }
    if (inc != 1) {
        if (o->rounding_type != INTL_ROUNDING_FRACTION) {
            JS_ThrowTypeError(ctx, "roundingIncrement requires fraction digit rounding");
            goto done;
        }
        if (o->max_frac != o->min_frac) {
            JS_ThrowRangeError(ctx, "roundingIncrement requires equal fraction digits");
            goto done;
        }
    }
    r = 0;
done:
    JS_FreeValue(ctx, mnfd);
    JS_FreeValue(ctx, mxfd);
    JS_FreeValue(ctx, mnsd);
    JS_FreeValue(ctx, mxsd);
    return r;
}

static int set_int_prop(JSContext *ctx, JSValueConst obj, const char *name, int v)
{
    if (v < 0)
        return 0;
    return JS_DefinePropertyValueStr(ctx, obj, name, JS_NewInt32(ctx, v), JS_PROP_C_W_E);
}

int intl_resolved_digit_options(JSContext *ctx, JSValueConst obj, const IntlDigitOptions *o,
                                bool before_categories)
{
    if (before_categories) {
        if (set_int_prop(ctx, obj, "minimumIntegerDigits", o->min_int) < 0 ||
            set_int_prop(ctx, obj, "minimumFractionDigits", o->min_frac) < 0 ||
            set_int_prop(ctx, obj, "maximumFractionDigits", o->max_frac) < 0 ||
            set_int_prop(ctx, obj, "minimumSignificantDigits", o->min_sig) < 0 ||
            set_int_prop(ctx, obj, "maximumSignificantDigits", o->max_sig) < 0)
            return -1;
        return 0;
    }
    if (set_int_prop(ctx, obj, "roundingIncrement", o->rounding_increment) < 0 ||
        intl_set_prop_str(ctx, obj, "roundingMode", rounding_mode_names[o->rounding_mode]) < 0 ||
        intl_set_prop_str(ctx, obj, "roundingPriority", rounding_priority_names[o->rounding_priority]) < 0 ||
        intl_set_prop_str(ctx, obj, "trailingZeroDisplay", trailing_zero_names[o->trailing_zero_strip]) < 0)
        return -1;
    return 0;
}

enum { STYLE_DECIMAL, STYLE_PERCENT, STYLE_CURRENCY, STYLE_UNIT };
enum { CDISP_CODE, CDISP_SYMBOL, CDISP_NARROW, CDISP_NAME };
enum { NOTATION_STANDARD, NOTATION_SCIENTIFIC, NOTATION_ENGINEERING, NOTATION_COMPACT };
enum { GROUP_MIN2, GROUP_AUTO, GROUP_ALWAYS, GROUP_FALSE };
enum { SIGN_AUTO, SIGN_NEVER, SIGN_ALWAYS, SIGN_EXCEPT_ZERO, SIGN_NEGATIVE };

static const char *const style_names[] = { "decimal", "percent", "currency", "unit", NULL };
static const char *const cdisp_names[] = { "code", "symbol", "narrowSymbol", "name", NULL };
static const char *const csign_names[] = { "standard", "accounting", NULL };
static const char *const udisp_names[] = { "short", "narrow", "long", NULL };
static const char *const notation_names[] = { "standard", "scientific", "engineering", "compact", NULL };
static const char *const compact_names[] = { "short", "long", NULL };
static const char *const grouping_names[] = { "min2", "auto", "always", "true", "false", NULL };
static const char *const sign_names[] = { "auto", "never", "always", "exceptZero", "negative", NULL };

struct IntlNumberFormat {
    char locale[INTL_TAG_MAX];
    int data;
    char numbering[16];
    const char *digits;
    int style;
    char currency[4];
    int currency_display;
    int currency_sign;
    char unit[64];
    int unit_display;
    IntlDigitOptions d;
    int notation;
    int compact_display;
    int use_grouping;
    int sign_display;
    JSValue bound_format;
};

static const char *const nf_keys[] = { "nu" };

static bool nu_supported(const char *locale, const char *key, const char *value)
{
    return intl_numbering_system_digits(value) != NULL;
}

const char *intl_default_numbering_system(const char *locale)
{
    int node = intl_data_node(locale);
    const char *nu = intl_data_get(node, "nu");
    if (!nu || !intl_numbering_system_digits(nu))
        nu = "latn";
    return nu;
}

static const char *nu_default(const char *locale, const char *key)
{
    return intl_default_numbering_system(locale);
}

const IntlResolverOps intl_nu_resolver = { nf_keys, 1, nu_supported, nu_default };

static int nf_init(JSContext *ctx, IntlNumberFormat *nf, JSValueConst locales, JSValueConst options_in)
{
    IntlLocaleList requested;
    JSValue options = JS_UNDEFINED;
    IntlResolved res;
    char nu[64], currency[64], unit[64];
    bool lookup, has_nu, has_currency, has_unit;
    char *opts[1];
    int cdisp, csign, udisp, notation, compact, sign, mnfd_default, mxfd_default, r = -1;

    memset(nf, 0, sizeof(*nf));
    nf->bound_format = JS_UNDEFINED;
    if (intl_canonicalize_locale_list(ctx, locales, &requested) < 0)
        return -1;
    options = intl_coerce_options(ctx, options_in);
    if (JS_IsException(options))
        goto done;
    if (intl_get_locale_matcher(ctx, options, &lookup) < 0)
        goto done;
    if (intl_get_resolution_option(ctx, options, "numberingSystem", NULL, nu, sizeof(nu), &has_nu) < 0)
        goto done;
    opts[0] = has_nu ? nu : NULL;
    if (intl_resolve_locale(ctx, INTL_CLASS_NUMBER_FORMAT, &requested, lookup, &intl_nu_resolver, opts, &res) < 0)
        goto done;
    str_concat(nf->locale, sizeof(nf->locale), res.locale, "");
    str_concat(nf->numbering, sizeof(nf->numbering), res.values[0], "");
    nf->data = intl_data_node(res.data_locale);
    nf->digits = intl_numbering_system_digits(nf->numbering);

    nf->style = intl_get_enum_option(ctx, options, "style", style_names, STYLE_DECIMAL);
    if (nf->style < -1)
        goto done;
    if (intl_get_string_option(ctx, options, "currency", NULL, NULL, currency, sizeof(currency), &has_currency) < 0)
        goto done;
    if (!has_currency) {
        if (nf->style == STYLE_CURRENCY) {
            JS_ThrowTypeError(ctx, "currency option is required with currency style");
            goto done;
        }
    } else if (!intl_is_well_formed_currency(currency)) {
        JS_ThrowRangeError(ctx, "invalid currency code: %s", currency);
        goto done;
    }
    cdisp = intl_get_enum_option(ctx, options, "currencyDisplay", cdisp_names, CDISP_SYMBOL);
    if (cdisp < -1)
        goto done;
    csign = intl_get_enum_option(ctx, options, "currencySign", csign_names, 0);
    if (csign < -1)
        goto done;
    if (intl_get_string_option(ctx, options, "unit", NULL, NULL, unit, sizeof(unit), &has_unit) < 0)
        goto done;
    if (!has_unit) {
        if (nf->style == STYLE_UNIT) {
            JS_ThrowTypeError(ctx, "unit option is required with unit style");
            goto done;
        }
    } else if (!intl_is_well_formed_unit(unit)) {
        JS_ThrowRangeError(ctx, "invalid unit: %s", unit);
        goto done;
    }
    udisp = intl_get_enum_option(ctx, options, "unitDisplay", udisp_names, 0);
    if (udisp < -1)
        goto done;
    if (nf->style == STYLE_CURRENCY) {
        str_concat(nf->currency, sizeof(nf->currency), currency, "");
        intl_ascii_upper(nf->currency);
        nf->currency_display = cdisp;
        nf->currency_sign = csign;
    }
    if (nf->style == STYLE_UNIT) {
        snprintf(nf->unit, sizeof(nf->unit), "%s", unit);
        nf->unit_display = udisp;
    }
    notation = intl_get_enum_option(ctx, options, "notation", notation_names, NOTATION_STANDARD);
    if (notation < -1)
        goto done;
    nf->notation = notation;
    if (nf->style == STYLE_CURRENCY && notation == NOTATION_STANDARD) {
        mnfd_default = mxfd_default = intl_currency_digits(nf->currency);
    } else {
        mnfd_default = 0;
        mxfd_default = nf->style == STYLE_PERCENT ? 0 : 3;
    }
    if (intl_set_digit_options(ctx, &nf->d, options, mnfd_default, mxfd_default,
                               notation == NOTATION_COMPACT) < 0)
        goto done;
    compact = intl_get_enum_option(ctx, options, "compactDisplay", compact_names, 0);
    if (compact < -1)
        goto done;
    nf->compact_display = notation == NOTATION_COMPACT ? compact : -1;
    {
        int def = notation == NOTATION_COMPACT ? GROUP_MIN2 : GROUP_AUTO;
        JSValue v = JS_GetPropertyStr(ctx, options, "useGrouping");
        if (JS_IsException(v))
            goto done;
        if (JS_IsUndefined(v)) {
            nf->use_grouping = def;
        } else if (JS_IsBool(v) && JS_ToBool(ctx, v)) {
            nf->use_grouping = GROUP_ALWAYS;
        } else if (!JS_ToBool(ctx, v)) {
            nf->use_grouping = GROUP_FALSE;
        } else {
            size_t len;
            const char *s = JS_ToCStringLen(ctx, &len, v);
            int k = -1;
            JS_FreeValue(ctx, v);
            v = JS_UNDEFINED;
            if (!s)
                goto done;
            for (int i = 0; grouping_names[i]; i++)
                if (strlen(grouping_names[i]) == len && !memcmp(grouping_names[i], s, len))
                    k = i;
            if (k < 0) {
                JS_ThrowRangeError(ctx, "invalid value for useGrouping: %s", s);
                JS_FreeCString(ctx, s);
                goto done;
            }
            JS_FreeCString(ctx, s);
            nf->use_grouping = k >= 3 ? def : k;
        }
        JS_FreeValue(ctx, v);
    }
    sign = intl_get_enum_option(ctx, options, "signDisplay", sign_names, SIGN_AUTO);
    if (sign < -1)
        goto done;
    nf->sign_display = sign;
    r = 0;
done:
    intl_locale_list_free(ctx, &requested);
    JS_FreeValue(ctx, options);
    return r;
}

static const char *nf_symbol(const IntlNumberFormat *nf, const char *name)
{
    const char *s = intl_data_getf(nf->data, "n.%s.%s", nf->numbering, name);
    if (!s)
        s = intl_data_getf(nf->data, "n.latn.%s", name);
    if (!s) {
        if (!strcmp(name, "decimal"))
            s = ".";
        else if (!strcmp(name, "group"))
            s = ",";
        else if (!strcmp(name, "minusSign"))
            s = "-";
        else if (!strcmp(name, "plusSign"))
            s = "+";
        else if (!strcmp(name, "percentSign"))
            s = "%";
        else if (!strcmp(name, "infinity"))
            s = "\xe2\x88\x9e";
        else if (!strcmp(name, "nan"))
            s = "NaN";
        else if (!strcmp(name, "exponential"))
            s = "E";
        else if (!strcmp(name, "approximatelySign"))
            s = "~";
        else
            s = "";
    }
    return s;
}

static const char *nf_pattern(const IntlNumberFormat *nf, const char *name)
{
    const char *s = intl_data_getf(nf->data, "n.%s.%s", nf->numbering, name);
    if (!s)
        s = intl_data_getf(nf->data, "n.latn.%s", name);
    return s;
}

typedef struct Affixes {
    char pos_prefix[96], pos_suffix[96];
    char neg_prefix[96], neg_suffix[96];
    bool has_neg;
    int group1, group2;
} Affixes;

static bool is_pattern_number_char(int c)
{
    return c == '#' || c == '0' || c == ',' || c == '.' || c == '@' || (c >= '1' && c <= '9');
}

static void split_subpattern(const char *p, size_t len, char *prefix, size_t psize, char *suffix,
                             size_t ssize, int *g1, int *g2)
{
    size_t i = 0, start, end;
    bool quoted = false;
    while (i < len) {
        if (p[i] == '\'')
            quoted = !quoted;
        else if (!quoted && is_pattern_number_char((unsigned char)p[i]))
            break;
        i++;
    }
    start = i;
    end = start;
    while (end < len && is_pattern_number_char((unsigned char)p[end]))
        end++;
    snprintf(prefix, psize, "%.*s", (int)start, p);
    snprintf(suffix, ssize, "%.*s", (int)(len - end), p + end);
    if (g1) {
        const char *num = p + start;
        size_t nlen = end - start;
        size_t dot = nlen, last = (size_t)-1, prev = (size_t)-1;
        for (size_t k = 0; k < nlen; k++) {
            if (num[k] == '.') {
                dot = k;
                break;
            }
            if (num[k] == ',') {
                prev = last;
                last = k;
            }
        }
        *g1 = last == (size_t)-1 ? 0 : (int)(dot - last - 1);
        *g2 = prev == (size_t)-1 ? *g1 : (int)(last - prev - 1);
    }
}

static void parse_affixes(const char *pattern, Affixes *a)
{
    const char *semi = strchr(pattern, ';');
    memset(a, 0, sizeof(*a));
    if (!pattern)
        return;
    split_subpattern(pattern, semi ? (size_t)(semi - pattern) : strlen(pattern),
                     a->pos_prefix, sizeof(a->pos_prefix), a->pos_suffix, sizeof(a->pos_suffix),
                     &a->group1, &a->group2);
    if (semi) {
        a->has_neg = true;
        split_subpattern(semi + 1, strlen(semi + 1), a->neg_prefix, sizeof(a->neg_prefix),
                         a->neg_suffix, sizeof(a->neg_suffix), NULL, NULL);
    }
}

static bool affix_has(const char *s, char ch)
{
    bool quoted = false;
    for (; *s; s++) {
        if (*s == '\'')
            quoted = !quoted;
        else if (!quoted && *s == ch)
            return true;
    }
    return false;
}

static int utf8_prev_cp(const char *s, size_t len)
{
    size_t p;
    int cp;
    if (len == 0)
        return -1;
    p = len - 1;
    while (p > 0 && (s[p] & 0xc0) == 0x80)
        p--;
    cp = (unsigned char)s[p];
    if (cp >= 0xf0)
        cp &= 0x07;
    else if (cp >= 0xe0)
        cp &= 0x0f;
    else if (cp >= 0xc0)
        cp &= 0x1f;
    for (size_t k = p + 1; k < len; k++)
        cp = (cp << 6) | (s[k] & 0x3f);
    return cp;
}

static int utf8_first_cp(const char *s)
{
    int cp = (unsigned char)s[0], n = 0;
    if (cp >= 0xf0) {
        cp &= 0x07;
        n = 3;
    } else if (cp >= 0xe0) {
        cp &= 0x0f;
        n = 2;
    } else if (cp >= 0xc0) {
        cp &= 0x1f;
        n = 1;
    }
    for (int k = 1; k <= n && s[k]; k++)
        cp = (cp << 6) | (s[k] & 0x3f);
    return cp;
}

static bool is_symbol_cp(int c)
{
    if (c < 0x80)
        return c && strchr("$+<=>^`|~", c) != NULL;
    return (c >= 0xa2 && c <= 0xa6) || c == 0xa8 || c == 0xa9 || c == 0xac || (c >= 0xae && c <= 0xb1) ||
           c == 0xb4 || c == 0xb8 || c == 0xd7 || c == 0xf7 || c == 0x58f || c == 0x60b ||
           (c >= 0x9f2 && c <= 0x9f3) || c == 0xaf1 || c == 0xbf9 || c == 0xe3f || c == 0x17db ||
           (c >= 0x2044 && c <= 0x2044) || (c >= 0x20a0 && c <= 0x20cf) || (c >= 0x2100 && c <= 0x214f) ||
           (c >= 0x2190 && c <= 0x2bff) || c == 0xfdfc || (c >= 0xfe62 && c <= 0xfe66) || c == 0xfe69 ||
           c == 0xff04 || (c >= 0xffe0 && c <= 0xffe6);
}

static bool is_space_cp(int c)
{
    return c == ' ' || c == 0xa0 || c == 0x202f || c == 0x2009 || c == 0x200a || c == 0x2002 ||
           c == 0x2003 || c == 0x200e || c == 0x200f || c == 0x061c;
}

static bool is_whitespace_cp(int c)
{
    return c == ' ' || c == 0xa0 || c == 0x202f || c == 0x2009 || c == 0x200a || c == 0x2002 || c == 0x2003;
}

enum {
    LVL_OUTER_PRE,
    LVL_MID_PRE,
    LVL_INNER_PRE,
    LVL_NUMBER,
    LVL_INNER_SUF,
    LVL_MID_SUF,
    LVL_OUTER_SUF,
};

static void add_text_parts(IntlParts *parts, const char *type, const char *s, size_t len, int level)
{
    size_t start = 0, end = len;
    while (start < end) {
        size_t p = start;
        int cp;
        int nb = ((unsigned char)s[p] >= 0xf0) ? 4 : ((unsigned char)s[p] >= 0xe0) ? 3 :
                 ((unsigned char)s[p] >= 0xc0) ? 2 : 1;
        cp = utf8_first_cp(s + p);
        if (!is_whitespace_cp(cp) && cp != 0x200e && cp != 0x200f && cp != 0x061c)
            break;
        start += nb;
    }
    while (end > start) {
        int cp = utf8_prev_cp(s, end);
        size_t q = end - 1;
        if (!is_whitespace_cp(cp) && cp != 0x200e && cp != 0x200f && cp != 0x061c)
            break;
        while (q > start && (s[q] & 0xc0) == 0x80)
            q--;
        end = q;
    }
    if (start > 0)
        intl_parts_add(parts, "literal", s, start, level);
    if (end > start)
        intl_parts_add(parts, type, s + start, end - start, level);
    if (len > end)
        intl_parts_add(parts, "literal", s + end, len - end, level);
}

typedef struct FormatCtx {
    const IntlNumberFormat *nf;
    IntlParts *parts;
    const char *currency_str;
    bool percent_style;
} FormatCtx;

static void emit_affix(FormatCtx *fc, const char *affix, bool prepend_sign, bool plus, int level)
{
    DynBuf lit;
    bool quoted = false;
    const char *p = affix;
    dbuf_init(&lit);
#define FLUSH() do { if (lit.size) { intl_parts_add(fc->parts, "literal", (const char *)lit.buf, lit.size, level); lit.size = 0; } } while (0)
    if (prepend_sign) {
        intl_parts_add(fc->parts, plus ? "plusSign" : "minusSign",
                       nf_symbol(fc->nf, plus ? "plusSign" : "minusSign"),
                       strlen(nf_symbol(fc->nf, plus ? "plusSign" : "minusSign")), level);
    }
    while (*p) {
        unsigned char c = *p;
        if (c == '\'') {
            if (p[1] == '\'') {
                dbuf_putc(&lit, '\'');
                p += 2;
                continue;
            }
            quoted = !quoted;
            p++;
            continue;
        }
        if (!quoted && (c == '-' || c == '+')) {
            const char *sym = nf_symbol(fc->nf, (c == '+' || plus) ? "plusSign" : "minusSign");
            FLUSH();
            intl_parts_add(fc->parts, (c == '+' || plus) ? "plusSign" : "minusSign", sym, strlen(sym), level);
            p++;
            continue;
        }
        if (!quoted && c == '%') {
            const char *sym = nf_symbol(fc->nf, "percentSign");
            FLUSH();
            if (fc->percent_style)
                intl_parts_add(fc->parts, "percentSign", sym, strlen(sym), level);
            p++;
            continue;
        }
        if (!quoted && c == 0xc2 && (unsigned char)p[1] == 0xa4) {
            FLUSH();
            while (*p && (unsigned char)p[0] == 0xc2 && (unsigned char)p[1] == 0xa4)
                p += 2;
            if (fc->currency_str)
                intl_parts_add(fc->parts, "currency", fc->currency_str, strlen(fc->currency_str), level);
            continue;
        }
        if (!quoted && c == 0xe2 && (unsigned char)p[1] == 0x80 && (unsigned char)p[2] == 0xb0) {
            const char *sym = nf_symbol(fc->nf, "perMille");
            FLUSH();
            intl_parts_add(fc->parts, "percentSign", sym, strlen(sym), level);
            p += 3;
            continue;
        }
        dbuf_putc(&lit, c);
        p++;
    }
    FLUSH();
#undef FLUSH
    dbuf_free(&lit);
}

static void put_digits(const IntlNumberFormat *nf, DynBuf *b, const char *s, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (s[i] >= '0' && s[i] <= '9' && nf->digits && strcmp(nf->numbering, "latn")) {
            const char *d = nf->digits;
            int idx = s[i] - '0';
            while (idx-- > 0) {
                d++;
                while ((*d & 0xc0) == 0x80)
                    d++;
            }
            {
                const char *e = d + 1;
                while ((*e & 0xc0) == 0x80)
                    e++;
                dbuf_put(b, (const uint8_t *)d, e - d);
            }
        } else {
            dbuf_putc(b, s[i]);
        }
    }
}

static void emit_digits_part(const IntlNumberFormat *nf, IntlParts *parts, const char *type,
                             const char *s, size_t len, int level)
{
    DynBuf b;
    dbuf_init(&b);
    put_digits(nf, &b, s, len);
    if (b.error)
        parts->error = true;
    else
        intl_parts_add(parts, type, (const char *)b.buf, b.size, level);
    dbuf_free(&b);
}

static void emit_number_core(const IntlNumberFormat *nf, IntlParts *parts, const char *str,
                             const Affixes *aff)
{
    const char *dot = strchr(str, '.');
    size_t ilen = dot ? (size_t)(dot - str) : strlen(str);
    int g1 = aff->group1, g2 = aff->group2, min_group = 1;
    bool group = nf->use_grouping != GROUP_FALSE && g1 > 0;
    if (group) {
        const char *mg = intl_data_get(nf->data, "n.mingroup");
        int locale_min = mg ? atoi(mg) : 1;
        if (nf->use_grouping == GROUP_ALWAYS)
            min_group = 1;
        else if (nf->use_grouping == GROUP_MIN2)
            min_group = locale_min > 2 ? locale_min : 2;
        else
            min_group = locale_min;
        if ((int)ilen < g1 + min_group)
            group = false;
    }
    if (!group) {
        emit_digits_part(nf, parts, "integer", str, ilen, LVL_NUMBER);
    } else {
        int sizes[64], n = 0;
        size_t rem = ilen;
        const char *sep = nf_symbol(nf, "group");
        if (g2 <= 0)
            g2 = g1;
        sizes[n++] = g1;
        rem -= g1;
        while (rem > 0 && n < 63) {
            int s = (int)rem > g2 ? g2 : (int)rem;
            if ((int)rem <= g2)
                s = (int)rem;
            sizes[n++] = s;
            rem -= s;
        }
        {
            size_t pos = 0;
            for (int k = n - 1; k >= 0; k--) {
                size_t sz = sizes[k];
                if (k == n - 1 && rem > 0)
                    sz += rem;
                emit_digits_part(nf, parts, "integer", str + pos, sz, LVL_NUMBER);
                pos += sz;
                if (k > 0)
                    intl_parts_add(parts, "group", sep, strlen(sep), LVL_NUMBER);
            }
        }
    }
    if (dot) {
        const char *dec = nf_symbol(nf, "decimal");
        intl_parts_add(parts, "decimal", dec, strlen(dec), LVL_NUMBER);
        emit_digits_part(nf, parts, "fraction", dot + 1, strlen(dot + 1), LVL_NUMBER);
    }
}

static const char *compact_pattern(const IntlNumberFormat *nf, int magnitude, const char *count)
{
    char key[64];
    const char *p;
    const char *w = nf->compact_display == 1 ? "cl" : "cs";
    if (magnitude > 14)
        magnitude = 14;
    if (magnitude < 3)
        return NULL;
    snprintf(key, sizeof(key), "n.%s.%s.%d.%s", nf->numbering, w, magnitude, count);
    p = intl_data_get(nf->data, key);
    if (!p) {
        snprintf(key, sizeof(key), "n.%s.%s.%d.other", nf->numbering, w, magnitude);
        p = intl_data_get(nf->data, key);
    }
    if (!p) {
        snprintf(key, sizeof(key), "n.latn.%s.%d.%s", w, magnitude, count);
        p = intl_data_get(nf->data, key);
    }
    if (!p) {
        snprintf(key, sizeof(key), "n.latn.%s.%d.other", w, magnitude);
        p = intl_data_get(nf->data, key);
    }
    if (!p && nf->compact_display == 1) {
        snprintf(key, sizeof(key), "n.latn.cs.%d.%s", magnitude, count);
        p = intl_data_get(nf->data, key);
        if (!p) {
            snprintf(key, sizeof(key), "n.latn.cs.%d.other", magnitude);
            p = intl_data_get(nf->data, key);
        }
    }
    return p;
}

static int compact_exponent(const IntlNumberFormat *nf, int magnitude)
{
    const char *p;
    int zeros = 0, m = magnitude > 14 ? 14 : magnitude;
    if (m < 3)
        return 0;
    p = compact_pattern(nf, m, "other");
    if (!p || !strcmp(p, "0"))
        return 0;
    for (const char *q = p; *q; q++)
        if (*q == '0')
            zeros++;
    if (zeros == 0)
        return 0;
    return m - zeros + 1;
}

static int exponent_for_magnitude(const IntlNumberFormat *nf, int magnitude)
{
    switch (nf->notation) {
    case NOTATION_SCIENTIFIC:
        return magnitude;
    case NOTATION_ENGINEERING:
        return (magnitude >= 0 ? magnitude / 3 : -((-magnitude + 2) / 3)) * 3;
    case NOTATION_COMPACT:
        return compact_exponent(nf, magnitude);
    default:
        return 0;
    }
}

static int compute_exponent(const IntlNumberFormat *nf, const IntlDecimal *x, int *exponent)
{
    IntlDecimal y;
    IntlRawResult res;
    int magnitude, e;
    if (x->nd == 0) {
        *exponent = 0;
        return 0;
    }
    magnitude = intl_decimal_magnitude(x);
    e = exponent_for_magnitude(nf, magnitude);
    y = *x;
    y.exp -= e;
    if (intl_format_numeric_to_string(&nf->d, &y, &res) < 0)
        return -1;
    if (res.rounded.nd == 0 || intl_decimal_magnitude(&res.rounded) == magnitude - e) {
        *exponent = e;
    } else {
        *exponent = exponent_for_magnitude(nf, magnitude + 1);
    }
    intl_raw_result_free(&res);
    return 0;
}

static const char *plural_name(int cat)
{
    return intl_plural_categories[cat];
}

static const char *currency_display_string(const IntlNumberFormat *nf, const char *count)
{
    const char *s;
    switch (nf->currency_display) {
    case CDISP_CODE:
        return nf->currency;
    case CDISP_NARROW:
        s = intl_data_getf(nf->data, "c.%s.n", nf->currency);
        if (s)
            return s;
        s = intl_data_getf(nf->data, "c.%s.s", nf->currency);
        return s ? s : nf->currency;
    case CDISP_NAME:
        s = intl_data_getf(nf->data, "c.%s.%s", nf->currency, count);
        if (!s)
            s = intl_data_getf(nf->data, "c.%s.other", nf->currency);
        if (!s)
            s = intl_data_getf(nf->data, "c.%s.d", nf->currency);
        return s ? s : nf->currency;
    default:
        s = intl_data_getf(nf->data, "c.%s.s", nf->currency);
        return s ? s : nf->currency;
    }
}

static const char *unit_pattern(const IntlNumberFormat *nf, const char *unit, size_t len, const char *count,
                                const char *suffix_field)
{
    static const char *const widths[] = { "short", "narrow", "long" };
    const char *w = widths[nf->unit_display];
    char key[128];
    const char *p;
    snprintf(key, sizeof(key), "u.%.*s.%s.%s", (int)len, unit, w, suffix_field ? suffix_field : count);
    p = intl_data_get(nf->data, key);
    if (!p && !suffix_field) {
        snprintf(key, sizeof(key), "u.%.*s.%s.other", (int)len, unit, w);
        p = intl_data_get(nf->data, key);
    }
    if (!p && nf->unit_display != 0) {
        snprintf(key, sizeof(key), "u.%.*s.short.%s", (int)len, unit, suffix_field ? suffix_field : count);
        p = intl_data_get(nf->data, key);
        if (!p && !suffix_field) {
            snprintf(key, sizeof(key), "u.%.*s.short.other", (int)len, unit);
            p = intl_data_get(nf->data, key);
        }
    }
    return p;
}

static char *build_unit_pattern(const IntlNumberFormat *nf, const char *count)
{
    static const char *const widths[] = { "short", "narrow", "long" };
    const char *unit = nf->unit;
    const char *per = strstr(unit, "-per-");
    const char *p = unit_pattern(nf, unit, strlen(unit), count, NULL);
    DynBuf b;
    if (p)
        return strdup(p);
    if (!per) {
        dbuf_init(&b);
        dbuf_printf(&b, "{0} %s", unit);
        dbuf_putc(&b, '\0');
        return (char *)b.buf;
    }
    {
        const char *num = unit_pattern(nf, unit, (size_t)(per - unit), count, NULL);
        const char *den_per = unit_pattern(nf, per + 5, strlen(per + 5), count, "per");
        const char *num_pat = num ? num : "{0}";
        char *res;
        dbuf_init(&b);
        if (den_per) {
            const char *z = strstr(den_per, "{0}");
            const char *nz = strstr(num_pat, "{0}");
            if (z && nz) {
                dbuf_put(&b, (const uint8_t *)den_per, z - den_per);
                dbuf_putstr(&b, num_pat);
                dbuf_putstr(&b, z + 3);
            } else {
                dbuf_putstr(&b, num_pat);
            }
        } else {
            const char *compound = intl_data_getf(nf->data, "u.per.%s", widths[nf->unit_display]);
            const char *den = unit_pattern(nf, per + 5, strlen(per + 5), "one", NULL);
            char den_name[128] = "";
            if (den) {
                const char *z = strstr(den, "{0}");
                if (z) {
                    char tmp[128];
                    snprintf(tmp, sizeof(tmp), "%.*s%s", (int)(z - den), den, z + 3);
                    {
                        char *s = tmp, *e;
                        while (*s == ' ' || ((unsigned char)s[0] == 0xc2 && (unsigned char)s[1] == 0xa0))
                            s += *s == ' ' ? 1 : 2;
                        e = s + strlen(s);
                        while (e > s && (e[-1] == ' ' || ((unsigned char)e[-1] == 0xa0 && e - s >= 2 &&
                                                          (unsigned char)e[-2] == 0xc2)))
                            e -= e[-1] == ' ' ? 1 : 2;
                        snprintf(den_name, sizeof(den_name), "%.*s", (int)(e - s), s);
                    }
                }
            } else {
                snprintf(den_name, sizeof(den_name), "%s", per + 5);
            }
            if (!compound)
                compound = "{0}/{1}";
            for (const char *q = compound; *q; q++) {
                if (q[0] == '{' && q[1] == '0' && q[2] == '}') {
                    dbuf_putstr(&b, num_pat);
                    q += 2;
                } else if (q[0] == '{' && q[1] == '1' && q[2] == '}') {
                    dbuf_putstr(&b, den_name);
                    q += 2;
                } else {
                    dbuf_putc(&b, *q);
                }
            }
        }
        dbuf_putc(&b, '\0');
        if (b.error) {
            dbuf_free(&b);
            return NULL;
        }
        res = (char *)b.buf;
        return res;
    }
}

static bool currency_alpha_adjacent(const char *cur, bool prefix)
{
    int cp;
    size_t len = strlen(cur);
    if (!len)
        return false;
    cp = prefix ? utf8_prev_cp(cur, len) : utf8_first_cp(cur);
    return !is_symbol_cp(cp) && !is_space_cp(cp);
}

static bool affix_ends_with_currency(const char *affix)
{
    size_t len = strlen(affix);
    return len >= 2 && (unsigned char)affix[len - 2] == 0xc2 && (unsigned char)affix[len - 1] == 0xa4;
}

static bool affix_starts_with_currency(const char *affix)
{
    return (unsigned char)affix[0] == 0xc2 && (unsigned char)affix[1] == 0xa4;
}

static int sign_category(const IntlNumberFormat *nf, const IntlDecimal *x)
{
    int cat;
    if (x->kind == INTL_DEC_NEG_INF)
        cat = 3;
    else if (x->kind == INTL_DEC_NAN)
        cat = 0;
    else if (x->kind == INTL_DEC_POS_INF)
        cat = 1;
    else if (x->nd == 0)
        cat = x->negative ? 2 : 0;
    else
        cat = x->negative ? 3 : 1;
    switch (nf->sign_display) {
    case SIGN_NEVER:
        return 0;
    case SIGN_AUTO:
        return cat >= 2 ? -1 : 0;
    case SIGN_ALWAYS:
        return cat >= 2 ? -1 : 1;
    case SIGN_EXCEPT_ZERO:
        return (cat == 0 || cat == 2) ? 0 : cat == 1 ? 1 : -1;
    default:
        return cat == 3 ? -1 : 0;
    }
}

int intl_nf_partition(JSContext *ctx, const IntlNumberFormat *nf, const IntlDecimal *xin, IntlParts *parts)
{
    IntlDecimal x;
    IntlRawResult res;
    int exponent = 0, signum, plural = 5, mag_for_compact = 0;
    const char *formatted = NULL;
    char *num_pattern_alloc = NULL, *outer_pattern = NULL;
    const char *cldr_pattern;
    char count_buf[16];
    Affixes aff;
    FormatCtx fc;
    const char *currency_str = NULL;
    bool finite = xin->kind == INTL_DEC_FINITE;

    memset(&res, 0, sizeof(res));
    x = *xin;
    if (finite) {
        if (x.nd != 0) {
            if (nf->style == STYLE_PERCENT)
                x.exp += 2;
            if (compute_exponent(nf, &x, &exponent) < 0)
                goto oom;
            x.exp -= exponent;
        }
        if (intl_format_numeric_to_string(&nf->d, &x, &res) < 0)
            goto oom;
        formatted = res.str;
        x = res.rounded;
        if (nf->notation == NOTATION_COMPACT)
            mag_for_compact = res.rounded.nd ? intl_decimal_magnitude(&res.rounded) + exponent : 0;
    }
    signum = sign_category(nf, finite ? &res.rounded : xin);
    if (finite) {
        int data = nf->data;
        plural = intl_plural_select(data, false, formatted,
                                    nf->notation == NOTATION_COMPACT ? exponent : 0);
    }
    snprintf(count_buf, sizeof(count_buf), "%s", plural_name(plural));

    if (nf->style == STYLE_PERCENT)
        cldr_pattern = nf_pattern(nf, "percent.pat");
    else if (nf->style == STYLE_CURRENCY && nf->currency_display != CDISP_NAME)
        cldr_pattern = nf_pattern(nf, nf->currency_sign == 1 ? "cur.accounting" : "cur.standard");
    else
        cldr_pattern = nf_pattern(nf, "decimal.pat");
    if (!cldr_pattern)
        cldr_pattern = nf->style == STYLE_PERCENT ? "#,##0%" : "#,##0.###";
    parse_affixes(cldr_pattern, &aff);
    if (nf->style == STYLE_CURRENCY) {
        currency_str = currency_display_string(nf, count_buf);
        if (nf->currency_display != CDISP_NAME) {
            bool pre = affix_ends_with_currency(aff.pos_prefix);
            bool suf = affix_starts_with_currency(aff.pos_suffix);
            if ((pre && currency_alpha_adjacent(currency_str, true)) ||
                (suf && currency_alpha_adjacent(currency_str, false))) {
                const char *alt = nf_pattern(nf, nf->currency_sign == 1 ? "cur.accounting-alphaNextToNumber"
                                                                         : "cur.standard-alphaNextToNumber");
                if (alt) {
                    parse_affixes(alt, &aff);
                } else {
                    Affixes t = aff;
                    if (pre) {
                        str_concat(aff.pos_prefix, sizeof(aff.pos_prefix), t.pos_prefix, "\xc2\xa0");
                        if (aff.has_neg && affix_ends_with_currency(t.neg_prefix))
                            str_concat(aff.neg_prefix, sizeof(aff.neg_prefix), t.neg_prefix, "\xc2\xa0");
                    } else {
                        str_concat(aff.pos_suffix, sizeof(aff.pos_suffix), "\xc2\xa0", t.pos_suffix);
                        if (aff.has_neg && affix_starts_with_currency(t.neg_suffix))
                            str_concat(aff.neg_suffix, sizeof(aff.neg_suffix), "\xc2\xa0", t.neg_suffix);
                    }
                }
            }
        }
    }
    if (nf->style == STYLE_UNIT)
        outer_pattern = build_unit_pattern(nf, count_buf);
    else if (nf->style == STYLE_CURRENCY && nf->currency_display == CDISP_NAME) {
        const char *up;
        char key[32];
        snprintf(key, sizeof(key), "cur.unit.%s", count_buf);
        up = nf_pattern(nf, key);
        if (!up)
            up = nf_pattern(nf, "cur.unit.other");
        if (!up)
            up = "{0} {1}";
        {
            DynBuf b;
            dbuf_init(&b);
            for (const char *q = up; *q; q++) {
                if (q[0] == '{' && q[1] == '1' && q[2] == '}') {
                    dbuf_putstr(&b, currency_str);
                    q += 2;
                } else {
                    dbuf_putc(&b, *q);
                }
            }
            dbuf_putc(&b, '\0');
            outer_pattern = (char *)b.buf;
        }
    }

    fc.nf = nf;
    fc.parts = parts;
    fc.currency_str = currency_str;
    fc.percent_style = nf->style == STYLE_PERCENT;
    {
        const char *outer_pre = NULL, *outer_suf = NULL;
        size_t outer_pre_len = 0;
        const char *prefix, *suffix;
        bool prepend = false, plus = false;
        if (outer_pattern) {
            const char *z = strstr(outer_pattern, "{0}");
            if (z) {
                outer_pre = outer_pattern;
                outer_pre_len = z - outer_pattern;
                outer_suf = z + 3;
            } else {
                outer_suf = outer_pattern;
            }
        }
        if (outer_pre && outer_pre_len)
            add_text_parts(parts, nf->style == STYLE_UNIT ? "unit" : "currency", outer_pre, outer_pre_len,
                           LVL_OUTER_PRE);
        if (signum < 0) {
            if (aff.has_neg) {
                prefix = aff.neg_prefix;
                suffix = aff.neg_suffix;
            } else {
                prefix = aff.pos_prefix;
                suffix = aff.pos_suffix;
                prepend = true;
            }
        } else if (signum > 0) {
            plus = true;
            if (affix_has(aff.pos_prefix, '+') || affix_has(aff.pos_suffix, '+')) {
                prefix = aff.pos_prefix;
                suffix = aff.pos_suffix;
                plus = false;
            } else if (aff.has_neg && (affix_has(aff.neg_prefix, '-') || affix_has(aff.neg_suffix, '-'))) {
                prefix = aff.neg_prefix;
                suffix = aff.neg_suffix;
            } else {
                prefix = aff.pos_prefix;
                suffix = aff.pos_suffix;
                prepend = true;
            }
        } else {
            prefix = aff.pos_prefix;
            suffix = aff.pos_suffix;
        }
        emit_affix(&fc, prefix, prepend, plus, LVL_MID_PRE);
        if (!finite) {
            if (xin->kind == INTL_DEC_NAN) {
                const char *s = nf_symbol(nf, "nan");
                intl_parts_add(parts, "nan", s, strlen(s), LVL_NUMBER);
            } else {
                const char *s = nf_symbol(nf, "infinity");
                intl_parts_add(parts, "infinity", s, strlen(s), LVL_NUMBER);
            }
        } else if (nf->notation == NOTATION_COMPACT && exponent != 0) {
            const char *cp = compact_pattern(nf, mag_for_compact, count_buf);
            const char *zs = cp ? strchr(cp, '0') : NULL;
            if (!zs) {
                emit_number_core(nf, parts, formatted, &aff);
            } else {
                const char *ze = zs;
                char pre[96], suf[96];
                size_t pl = 0, sl = 0;
                bool q = false;
                while (*ze == '0')
                    ze++;
                for (const char *t = cp; t < zs; t++) {
                    if (*t == '\'') {
                        q = !q;
                        continue;
                    }
                    if (pl < sizeof(pre) - 1)
                        pre[pl++] = *t;
                }
                for (const char *t = ze; *t; t++) {
                    if (*t == '\'') {
                        if (t[1] == '\'') {
                            if (sl < sizeof(suf) - 1)
                                suf[sl++] = '\'';
                            t++;
                        }
                        continue;
                    }
                    if (sl < sizeof(suf) - 1)
                        suf[sl++] = *t;
                }
                pre[pl] = suf[sl] = '\0';
                if (pl)
                    add_text_parts(parts, "compact", pre, pl, LVL_INNER_PRE);
                emit_number_core(nf, parts, formatted, &aff);
                if (sl)
                    add_text_parts(parts, "compact", suf, sl, LVL_INNER_SUF);
            }
        } else if (nf->notation == NOTATION_SCIENTIFIC || nf->notation == NOTATION_ENGINEERING) {
            char ebuf[16];
            const char *sep = nf_symbol(nf, "exponential");
            int e = exponent;
            emit_number_core(nf, parts, formatted, &aff);
            intl_parts_add(parts, "exponentSeparator", sep, strlen(sep), LVL_INNER_SUF);
            if (e < 0) {
                const char *m = nf_symbol(nf, "minusSign");
                intl_parts_add(parts, "exponentMinusSign", m, strlen(m), LVL_INNER_SUF);
                e = -e;
            }
            snprintf(ebuf, sizeof(ebuf), "%d", e);
            emit_digits_part(nf, parts, "exponentInteger", ebuf, strlen(ebuf), LVL_INNER_SUF);
        } else {
            emit_number_core(nf, parts, formatted, &aff);
        }
        emit_affix(&fc, suffix, false, plus, LVL_MID_SUF);
        if (outer_suf && *outer_suf)
            add_text_parts(parts, nf->style == STYLE_UNIT ? "unit" : "currency", outer_suf, strlen(outer_suf),
                           LVL_OUTER_SUF);
    }
    free(num_pattern_alloc);
    free(outer_pattern);
    intl_raw_result_free(&res);
    if (parts->error) {
        JS_ThrowOutOfMemory(ctx);
        return -1;
    }
    return 0;
oom:
    free(outer_pattern);
    intl_raw_result_free(&res);
    JS_ThrowOutOfMemory(ctx);
    return -1;
}

static bool parts_level_equal(const IntlParts *a, const IntlParts *b, int lvl_pre, int lvl_suf)
{
    int ia = 0, ib = 0;
    for (;;) {
        while (ia < a->count && a->items[ia].level != lvl_pre && a->items[ia].level != lvl_suf)
            ia++;
        while (ib < b->count && b->items[ib].level != lvl_pre && b->items[ib].level != lvl_suf)
            ib++;
        if (ia >= a->count || ib >= b->count)
            return ia >= a->count && ib >= b->count;
        if (a->items[ia].level != b->items[ib].level || strcmp(a->items[ia].type, b->items[ib].type))
            return false;
        if (strcmp(a->items[ia].type, "unit") && strcmp(a->items[ia].type, "currency") &&
            strcmp(a->items[ia].value, b->items[ib].value))
            return false;
        ia++;
        ib++;
    }
}

static int level_cp_count(const IntlParts *a, int lvl_pre, int lvl_suf, bool *has_currency_or_percent)
{
    int n = 0;
    for (int i = 0; i < a->count; i++) {
        if (a->items[i].level != lvl_pre && a->items[i].level != lvl_suf)
            continue;
        if (!strcmp(a->items[i].type, "currency") || !strcmp(a->items[i].type, "percentSign"))
            *has_currency_or_percent = true;
        for (const char *s = a->items[i].value; *s; s++)
            if ((*s & 0xc0) != 0x80)
                n++;
    }
    return n;
}

static int nf_partition_range(JSContext *ctx, const IntlNumberFormat *nf, const IntlDecimal *x,
                              const IntlDecimal *y, IntlParts *out)
{
    IntlParts px, py;
    char *sx = NULL, *sy = NULL;
    int r = -1;
    intl_parts_init(&px);
    intl_parts_init(&py);
    if (x->kind == INTL_DEC_NAN || y->kind == INTL_DEC_NAN) {
        JS_ThrowRangeError(ctx, "formatRange: NaN is not allowed");
        return -1;
    }
    if (intl_nf_partition(ctx, nf, x, &px) < 0 || intl_nf_partition(ctx, nf, y, &py) < 0)
        goto done;
    sx = intl_parts_join(&px);
    sy = intl_parts_join(&py);
    if (!sx || !sy) {
        JS_ThrowOutOfMemory(ctx);
        goto done;
    }
    if (!strcmp(sx, sy)) {
        const char *approx = nf_symbol(nf, "approximatelySign");
        bool inserted = false;
        for (int i = 0; i < px.count; i++) {
            if (!inserted && px.items[i].level >= LVL_MID_PRE) {
                intl_parts_add(out, "approximatelySign", approx, strlen(approx), LVL_MID_PRE);
                out->items[out->count - 1].source = "shared";
                inserted = true;
            }
            intl_parts_add(out, px.items[i].type, px.items[i].value, strlen(px.items[i].value), px.items[i].level);
            out->items[out->count - 1].source = "shared";
        }
        r = out->error ? -1 : 0;
        goto done;
    }
    {
        bool c_outer, c_mid = false, c_inner = false, cur_or_pct = false;
        const char *range = nf_pattern(nf, "range");
        char sep[64];
        int collapsed_pre, collapsed_suf;
        bool repeat;
        if (!range)
            range = "{0}\xe2\x80\x93{1}";
        {
            const char *a = strstr(range, "{0}"), *b = strstr(range, "{1}");
            if (a && b && b > a)
                snprintf(sep, sizeof(sep), "%.*s", (int)(b - a - 3), a + 3);
            else
                snprintf(sep, sizeof(sep), "\xe2\x80\x93");
        }
        c_outer = parts_level_equal(&px, &py, LVL_OUTER_PRE, LVL_OUTER_SUF);
        if (c_outer) {
            c_mid = parts_level_equal(&px, &py, LVL_MID_PRE, LVL_MID_SUF);
            if (c_mid && level_cp_count(&px, LVL_MID_PRE, LVL_MID_SUF, &cur_or_pct) <= 1)
                c_mid = false;
            if (c_mid)
                c_inner = parts_level_equal(&px, &py, LVL_INNER_PRE, LVL_INNER_SUF);
        }
        collapsed_pre = c_inner ? LVL_INNER_PRE : c_mid ? LVL_MID_PRE : c_outer ? LVL_OUTER_PRE : -1;
        collapsed_suf = c_inner ? LVL_INNER_SUF : c_mid ? LVL_MID_SUF : c_outer ? LVL_OUTER_SUF : 99;
        repeat = false;
        for (int i = 0; i < px.count; i++) {
            int lv = px.items[i].level;
            if (lv != LVL_NUMBER && !(lv <= collapsed_pre) && !(lv >= collapsed_suf))
                repeat = true;
        }
        for (int i = 0; i < py.count; i++) {
            if (py.items[i].level <= collapsed_pre) {
                intl_parts_add(out, py.items[i].type, py.items[i].value, strlen(py.items[i].value),
                               py.items[i].level);
                out->items[out->count - 1].source = "shared";
            }
        }
        for (int i = 0; i < px.count; i++) {
            int lv = px.items[i].level;
            if (lv <= collapsed_pre || lv >= collapsed_suf)
                continue;
            intl_parts_add(out, px.items[i].type, px.items[i].value, strlen(px.items[i].value), lv);
            out->items[out->count - 1].source = "startRange";
        }
        {
            char sbuf[80];
            size_t sl = strlen(sep);
            if (repeat) {
                bool lead = sl > 0 && (sep[0] == ' ' || (unsigned char)sep[0] == 0xc2);
                bool trail = sl > 0 && (sep[sl - 1] == ' ' || (unsigned char)sep[sl - 1] == 0xa0);
                snprintf(sbuf, sizeof(sbuf), "%s%s%s", lead ? "" : " ", sep, trail ? "" : " ");
            } else {
                snprintf(sbuf, sizeof(sbuf), "%s", sep);
            }
            intl_parts_add(out, "literal", sbuf, strlen(sbuf), LVL_NUMBER);
            out->items[out->count - 1].source = "shared";
        }
        for (int i = 0; i < py.count; i++) {
            int lv = py.items[i].level;
            if (lv <= collapsed_pre || lv >= collapsed_suf)
                continue;
            intl_parts_add(out, py.items[i].type, py.items[i].value, strlen(py.items[i].value), lv);
            out->items[out->count - 1].source = "endRange";
        }
        for (int i = 0; i < py.count; i++) {
            if (py.items[i].level >= collapsed_suf) {
                intl_parts_add(out, py.items[i].type, py.items[i].value, strlen(py.items[i].value),
                               py.items[i].level);
                out->items[out->count - 1].source = "shared";
            }
        }
        r = out->error ? -1 : 0;
        if (r < 0)
            JS_ThrowOutOfMemory(ctx);
    }
done:
    free(sx);
    free(sy);
    intl_parts_free(&px);
    intl_parts_free(&py);
    return r;
}

static void nf_finalizer(JSRuntime *rt, JSValueConst val)
{
    IntlNumberFormat *nf = JS_GetOpaque(val, js_intl_class_id(INTL_CLASS_NUMBER_FORMAT));
    if (nf) {
        JS_FreeValueRT(rt, nf->bound_format);
        free(nf);
    }
}

static void nf_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func)
{
    IntlNumberFormat *nf = JS_GetOpaque(val, js_intl_class_id(INTL_CLASS_NUMBER_FORMAT));
    if (nf)
        JS_MarkValue(rt, nf->bound_format, mark_func);
}

int intl_nf_format_double(JSContext *ctx, JSValueConst nf_obj, double x, IntlParts *parts)
{
    IntlNumberFormat *nf = JS_GetOpaque(nf_obj, js_intl_class_id(INTL_CLASS_NUMBER_FORMAT));
    IntlDecimal d;
    int r;
    if (intl_decimal_from_double(x, &d) < 0) {
        JS_ThrowOutOfMemory(ctx);
        return -1;
    }
    r = intl_nf_partition(ctx, nf, &d, parts);
    intl_decimal_free(&d);
    return r;
}

int intl_nf_format_decimal(JSContext *ctx, JSValueConst nf_obj, const char *s, IntlParts *parts)
{
    IntlNumberFormat *nf = JS_GetOpaque(nf_obj, js_intl_class_id(INTL_CLASS_NUMBER_FORMAT));
    IntlDecimal d;
    int r;
    if (intl_decimal_from_string(s, strlen(s), &d) < 0) {
        JS_ThrowOutOfMemory(ctx);
        return -1;
    }
    r = intl_nf_partition(ctx, nf, &d, parts);
    intl_decimal_free(&d);
    return r;
}

JSValue intl_construct_intrinsic(JSContext *ctx, int intrinsic, JSValueConst locale, JSValueConst options)
{
    JSValue ctor = js_intl_get_intrinsic(ctx, intrinsic);
    JSValueConst args[2] = { locale, options };
    JSValue r = JS_CallConstructor(ctx, ctor, 2, args);
    JS_FreeValue(ctx, ctor);
    return r;
}

IntlNumberFormat *intl_nf_get(JSValueConst obj)
{
    return JS_GetOpaque(obj, js_intl_class_id(INTL_CLASS_NUMBER_FORMAT));
}

static JSValue js_intl_nf_ctor(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    bool construct = js_intl_in_constructor_call(ctx);
    JSValue new_target = construct ? this_val : JS_UNDEFINED;
    JSValue obj, ctor = JS_UNDEFINED;
    IntlNumberFormat *nf;
    if (!construct) {
        ctor = js_intl_get_intrinsic(ctx, INTL_INTRINSIC_NUMBER_FORMAT);
        new_target = ctor;
    }
    obj = js_intl_create_from_ctor(ctx, new_target, js_intl_class_id(INTL_CLASS_NUMBER_FORMAT));
    if (JS_IsException(obj)) {
        JS_FreeValue(ctx, ctor);
        return obj;
    }
    nf = malloc(sizeof(*nf));
    if (!nf) {
        JS_FreeValue(ctx, ctor);
        JS_FreeValue(ctx, obj);
        return JS_ThrowOutOfMemory(ctx);
    }
    if (nf_init(ctx, nf, argc > 0 ? argv[0] : JS_UNDEFINED, argc > 1 ? argv[1] : JS_UNDEFINED) < 0) {
        free(nf);
        JS_FreeValue(ctx, ctor);
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    js_intl_set_opaque(obj, nf);
    if (!construct) {
        int inst = js_intl_ordinary_has_instance(ctx, this_val, ctor);
        if (inst < 0) {
            JS_FreeValue(ctx, ctor);
            JS_FreeValue(ctx, obj);
            return JS_EXCEPTION;
        }
        if (inst) {
            JSValue sym = js_intl_get_fallback_symbol(ctx);
            JSAtom atom = JS_ValueToAtom(ctx, sym);
            int ok;
            JS_FreeValue(ctx, sym);
            ok = JS_DefinePropertyValue(ctx, this_val, atom, obj, JS_PROP_THROW);
            JS_FreeAtom(ctx, atom);
            JS_FreeValue(ctx, ctor);
            if (ok < 0)
                return JS_EXCEPTION;
            return JS_DupValue(ctx, this_val);
        }
    }
    JS_FreeValue(ctx, ctor);
    return obj;
}

static IntlNumberFormat *unwrap_nf(JSContext *ctx, JSValueConst this_val, const char *method, bool legacy,
                                   JSValue *holder)
{
    IntlNumberFormat *nf = intl_nf_get(this_val);
    *holder = JS_UNDEFINED;
    if (nf)
        return nf;
    if (legacy && JS_IsObject(this_val)) {
        JSValue ctor = js_intl_get_intrinsic(ctx, INTL_INTRINSIC_NUMBER_FORMAT);
        int inst = js_intl_ordinary_has_instance(ctx, this_val, ctor);
        JS_FreeValue(ctx, ctor);
        if (inst < 0)
            return NULL;
        if (inst) {
            JSValue sym = js_intl_get_fallback_symbol(ctx);
            JSAtom atom = JS_ValueToAtom(ctx, sym);
            JSValue v = JS_GetProperty(ctx, this_val, atom);
            JS_FreeAtom(ctx, atom);
            JS_FreeValue(ctx, sym);
            if (JS_IsException(v))
                return NULL;
            nf = intl_nf_get(v);
            if (nf) {
                *holder = v;
                return nf;
            }
            JS_FreeValue(ctx, v);
        }
    }
    JS_ThrowTypeError(ctx, "Intl.NumberFormat.prototype.%s called on incompatible receiver", method);
    return NULL;
}

static JSValue nf_format_value(JSContext *ctx, const IntlNumberFormat *nf, JSValueConst value, bool to_parts)
{
    IntlDecimal x;
    IntlParts parts;
    JSValue r;
    if (intl_to_intl_mv(ctx, value, &x) < 0)
        return JS_EXCEPTION;
    intl_parts_init(&parts);
    if (intl_nf_partition(ctx, nf, &x, &parts) < 0) {
        intl_decimal_free(&x);
        intl_parts_free(&parts);
        return JS_EXCEPTION;
    }
    intl_decimal_free(&x);
    r = to_parts ? intl_parts_to_array(ctx, &parts, NULL, NULL, false) : intl_parts_to_string(ctx, &parts);
    intl_parts_free(&parts);
    return r;
}

static JSValue nf_bound_format(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
                               int magic, JSValueConst *func_data)
{
    IntlNumberFormat *nf = intl_nf_get(func_data[0]);
    if (!nf)
        return JS_ThrowTypeError(ctx, "invalid NumberFormat");
    return nf_format_value(ctx, nf, argc > 0 ? argv[0] : JS_UNDEFINED, false);
}

static JSValue js_intl_nf_get_format(JSContext *ctx, JSValueConst this_val)
{
    JSValue holder;
    IntlNumberFormat *nf = unwrap_nf(ctx, this_val, "format", true, &holder);
    if (!nf)
        return JS_EXCEPTION;
    if (JS_IsUndefined(nf->bound_format)) {
        JSValueConst target = JS_IsUndefined(holder) ? this_val : holder;
        JSValue f = JS_NewCFunctionData2(ctx, nf_bound_format, "", 1, 0, 1, &target);
        if (JS_IsException(f)) {
            JS_FreeValue(ctx, holder);
            return f;
        }
        nf->bound_format = f;
    }
    JS_FreeValue(ctx, holder);
    return JS_DupValue(ctx, nf->bound_format);
}

static JSValue js_intl_nf_formatToParts(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    IntlNumberFormat *nf = intl_nf_get(this_val);
    if (!nf)
        return JS_ThrowTypeError(ctx, "Intl.NumberFormat.prototype.formatToParts called on incompatible receiver");
    return nf_format_value(ctx, nf, argc > 0 ? argv[0] : JS_UNDEFINED, true);
}

static JSValue js_intl_nf_formatRange(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
                                      int to_parts)
{
    IntlNumberFormat *nf = intl_nf_get(this_val);
    JSValueConst start = argc > 0 ? argv[0] : JS_UNDEFINED, end = argc > 1 ? argv[1] : JS_UNDEFINED;
    IntlDecimal x, y;
    IntlParts parts;
    JSValue r;
    if (!nf)
        return JS_ThrowTypeError(ctx, "Intl.NumberFormat.prototype.formatRange called on incompatible receiver");
    if (JS_IsUndefined(start) || JS_IsUndefined(end))
        return JS_ThrowTypeError(ctx, "formatRange requires start and end");
    if (intl_to_intl_mv(ctx, start, &x) < 0)
        return JS_EXCEPTION;
    if (intl_to_intl_mv(ctx, end, &y) < 0) {
        intl_decimal_free(&x);
        return JS_EXCEPTION;
    }
    intl_parts_init(&parts);
    if (nf_partition_range(ctx, nf, &x, &y, &parts) < 0) {
        r = JS_EXCEPTION;
    } else {
        r = to_parts ? intl_parts_to_array(ctx, &parts, NULL, NULL, true) : intl_parts_to_string(ctx, &parts);
    }
    intl_decimal_free(&x);
    intl_decimal_free(&y);
    intl_parts_free(&parts);
    return r;
}

static JSValue js_intl_nf_resolvedOptions(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    JSValue holder, obj;
    IntlNumberFormat *nf = unwrap_nf(ctx, this_val, "resolvedOptions", true, &holder);
    if (!nf)
        return JS_EXCEPTION;
    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        goto done;
    if (intl_set_prop_str(ctx, obj, "locale", nf->locale) < 0 ||
        intl_set_prop_str(ctx, obj, "numberingSystem", nf->numbering) < 0 ||
        intl_set_prop_str(ctx, obj, "style", style_names[nf->style]) < 0)
        goto fail;
    if (nf->style == STYLE_CURRENCY) {
        if (intl_set_prop_str(ctx, obj, "currency", nf->currency) < 0 ||
            intl_set_prop_str(ctx, obj, "currencyDisplay", cdisp_names[nf->currency_display]) < 0 ||
            intl_set_prop_str(ctx, obj, "currencySign", csign_names[nf->currency_sign]) < 0)
            goto fail;
    }
    if (nf->style == STYLE_UNIT) {
        if (intl_set_prop_str(ctx, obj, "unit", nf->unit) < 0 ||
            intl_set_prop_str(ctx, obj, "unitDisplay", udisp_names[nf->unit_display]) < 0)
            goto fail;
    }
    if (intl_resolved_digit_options(ctx, obj, &nf->d, true) < 0)
        goto fail;
    if (nf->use_grouping == GROUP_FALSE) {
        if (intl_set_prop_value(ctx, obj, "useGrouping", JS_FALSE) < 0)
            goto fail;
    } else if (intl_set_prop_str(ctx, obj, "useGrouping", grouping_names[nf->use_grouping]) < 0) {
        goto fail;
    }
    if (intl_set_prop_str(ctx, obj, "notation", notation_names[nf->notation]) < 0)
        goto fail;
    if (nf->compact_display >= 0 &&
        intl_set_prop_str(ctx, obj, "compactDisplay", compact_names[nf->compact_display]) < 0)
        goto fail;
    if (intl_set_prop_str(ctx, obj, "signDisplay", sign_names[nf->sign_display]) < 0 ||
        intl_resolved_digit_options(ctx, obj, &nf->d, false) < 0)
        goto fail;
done:
    JS_FreeValue(ctx, holder);
    return obj;
fail:
    JS_FreeValue(ctx, holder);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue js_intl_supportedLocalesOf(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
                                          int service)
{
    return intl_supported_locales(ctx, service, argc > 0 ? argv[0] : JS_UNDEFINED,
                                  argc > 1 ? argv[1] : JS_UNDEFINED);
}

static const JSCFunctionListEntry js_intl_nf_proto_funcs[] = {
    JS_CGETSET_DEF("format", js_intl_nf_get_format, NULL),
    JS_CFUNC_DEF("formatToParts", 1, js_intl_nf_formatToParts),
    JS_CFUNC_MAGIC_DEF("formatRange", 2, js_intl_nf_formatRange, 0),
    JS_CFUNC_MAGIC_DEF("formatRangeToParts", 2, js_intl_nf_formatRange, 1),
    JS_CFUNC_DEF("resolvedOptions", 0, js_intl_nf_resolvedOptions),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Intl.NumberFormat", JS_PROP_CONFIGURABLE),
};

int intl_define_service(JSContext *ctx, JSValueConst intl, const char *name, int cls, int intrinsic,
                        JSCFunction *ctor_fn, int length, JSCFunctionEnum cproto,
                        const JSCFunctionListEntry *proto_funcs, int nfuncs,
                        JSClassFinalizer *finalizer, JSClassGCMark *mark)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    JSClassID cid = js_intl_class_id(cls);
    JSValue proto, ctor;
    if (!JS_IsRegisteredClass(rt, cid)) {
        JSClassDef def;
        memset(&def, 0, sizeof(def));
        def.class_name = name;
        def.finalizer = finalizer;
        def.gc_mark = mark;
        if (JS_NewClass(rt, cid, &def) < 0)
            return -1;
    }
    proto = JS_NewObject(ctx);
    if (JS_IsException(proto))
        return -1;
    if (JS_SetPropertyFunctionList(ctx, proto, proto_funcs, nfuncs) < 0) {
        JS_FreeValue(ctx, proto);
        return -1;
    }
    ctor = JS_NewCFunction2(ctx, ctor_fn, name, length, cproto, 0);
    if (JS_IsException(ctor)) {
        JS_FreeValue(ctx, proto);
        return -1;
    }
    if (cproto == JS_CFUNC_generic)
        JS_SetConstructorBit(ctx, ctor, true);
    JS_SetConstructor(ctx, ctor, proto);
    if (JS_DefinePropertyValueStr(ctx, ctor, "supportedLocalesOf",
                                  JS_NewCFunctionMagic(ctx, js_intl_supportedLocalesOf,
                                                   "supportedLocalesOf", 1, JS_CFUNC_generic_magic, cls),
                                  JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE) < 0) {
        JS_FreeValue(ctx, ctor);
        JS_FreeValue(ctx, proto);
        return -1;
    }
    JS_SetClassProto(ctx, cid, proto);
    js_intl_set_intrinsic(ctx, intrinsic, JS_DupValue(ctx, ctor));
    return JS_DefinePropertyValueStr(ctx, intl, name, ctor, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
}

static JSValue js_number_toLocaleString(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    JSValue v = js_intl_this_number_value(ctx, this_val);
    IntlNumberFormat nf;
    JSValue r;
    double d;
    if (JS_IsException(v))
        return v;
    if (nf_init(ctx, &nf, argc > 0 ? argv[0] : JS_UNDEFINED, argc > 1 ? argv[1] : JS_UNDEFINED) < 0) {
        JS_FreeValue(ctx, v);
        return JS_EXCEPTION;
    }
    JS_ToFloat64(ctx, &d, v);
    JS_FreeValue(ctx, v);
    {
        IntlDecimal x;
        IntlParts parts;
        if (intl_decimal_from_double(d, &x) < 0)
            return JS_ThrowOutOfMemory(ctx);
        intl_parts_init(&parts);
        if (intl_nf_partition(ctx, &nf, &x, &parts) < 0)
            r = JS_EXCEPTION;
        else
            r = intl_parts_to_string(ctx, &parts);
        intl_decimal_free(&x);
        intl_parts_free(&parts);
    }
    return r;
}

static JSValue js_bigint_toLocaleString(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    JSValue v = js_intl_this_bigint_value(ctx, this_val);
    IntlNumberFormat nf;
    JSValue r;
    if (JS_IsException(v))
        return v;
    if (nf_init(ctx, &nf, argc > 0 ? argv[0] : JS_UNDEFINED, argc > 1 ? argv[1] : JS_UNDEFINED) < 0) {
        JS_FreeValue(ctx, v);
        return JS_EXCEPTION;
    }
    r = nf_format_value(ctx, &nf, v, false);
    JS_FreeValue(ctx, v);
    return r;
}

static int install_method(JSContext *ctx, const char *ctor_name, const char *name, JSCFunction *fn, int length)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue ctor = JS_GetPropertyStr(ctx, global, ctor_name);
    JSValue proto = JS_GetPropertyStr(ctx, ctor, "prototype");
    JSValue f = JS_NewCFunction2(ctx, fn, name, length, JS_CFUNC_generic, 0);
    int r = JS_DefinePropertyValueStr(ctx, proto, name, f, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_FreeValue(ctx, proto);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, global);
    return r;
}

int js_intl_init_number_format(JSContext *ctx, JSValueConst intl)
{
    if (intl_define_service(ctx, intl, "NumberFormat", INTL_CLASS_NUMBER_FORMAT, INTL_INTRINSIC_NUMBER_FORMAT,
                            js_intl_nf_ctor, 0, JS_CFUNC_generic, js_intl_nf_proto_funcs,
                            countof(js_intl_nf_proto_funcs), nf_finalizer, nf_mark) < 0)
        return -1;
    if (install_method(ctx, "Number", "toLocaleString", js_number_toLocaleString, 0) < 0 ||
        install_method(ctx, "BigInt", "toLocaleString", js_bigint_toLocaleString, 0) < 0)
        return -1;
    return 0;
}

typedef struct IntlPluralRules {
    char locale[INTL_TAG_MAX];
    int data;
    bool ordinal;
    int notation;
    int compact_display;
    IntlDigitOptions d;
} IntlPluralRules;

static void pr_finalizer(JSRuntime *rt, JSValueConst val)
{
    free(JS_GetOpaque(val, js_intl_class_id(INTL_CLASS_PLURAL_RULES)));
}

static const char *const pr_type_names[] = { "cardinal", "ordinal", NULL };

static JSValue js_intl_pr_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    JSValue obj, options = JS_UNDEFINED;
    IntlPluralRules *pr = NULL;
    IntlLocaleList requested;
    IntlResolved res;
    bool lookup;
    int type, notation, compact;
    if (JS_IsUndefined(new_target))
        return JS_ThrowTypeError(ctx, "Intl.PluralRules must be called with new");
    obj = js_intl_create_from_ctor(ctx, new_target, js_intl_class_id(INTL_CLASS_PLURAL_RULES));
    if (JS_IsException(obj))
        return obj;
    if (intl_canonicalize_locale_list(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &requested) < 0)
        goto fail0;
    pr = calloc(1, sizeof(*pr));
    if (!pr) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    options = intl_coerce_options(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
    if (JS_IsException(options))
        goto fail;
    if (intl_get_locale_matcher(ctx, options, &lookup) < 0)
        goto fail;
    if (intl_resolve_locale(ctx, INTL_CLASS_PLURAL_RULES, &requested, lookup, NULL, NULL, &res) < 0)
        goto fail;
    snprintf(pr->locale, sizeof(pr->locale), "%s", res.locale);
    pr->data = intl_data_node(res.data_locale);
    type = intl_get_enum_option(ctx, options, "type", pr_type_names, 0);
    if (type < -1)
        goto fail;
    pr->ordinal = type == 1;
    notation = intl_get_enum_option(ctx, options, "notation", notation_names, NOTATION_STANDARD);
    if (notation < -1)
        goto fail;
    pr->notation = notation;
    compact = intl_get_enum_option(ctx, options, "compactDisplay", compact_names, 0);
    if (compact < -1)
        goto fail;
    pr->compact_display = notation == NOTATION_COMPACT ? compact : -1;
    if (intl_set_digit_options(ctx, &pr->d, options, 0, 3, notation == NOTATION_COMPACT) < 0)
        goto fail;
    js_intl_set_opaque(obj, pr);
    intl_locale_list_free(ctx, &requested);
    JS_FreeValue(ctx, options);
    return obj;
fail:
    intl_locale_list_free(ctx, &requested);
fail0:
    free(pr);
    JS_FreeValue(ctx, options);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static IntlPluralRules *get_pr(JSContext *ctx, JSValueConst this_val, const char *method)
{
    IntlPluralRules *pr = JS_GetOpaque(this_val, js_intl_class_id(INTL_CLASS_PLURAL_RULES));
    if (!pr)
        JS_ThrowTypeError(ctx, "Intl.PluralRules.prototype.%s called on incompatible receiver", method);
    return pr;
}

static int pr_compact_exponent(const IntlPluralRules *pr, const IntlRawResult *res)
{
    IntlNumberFormat tmp;
    if (pr->notation != NOTATION_COMPACT || res->rounded.nd == 0)
        return 0;
    memset(&tmp, 0, sizeof(tmp));
    tmp.data = pr->data;
    strcpy(tmp.numbering, "latn");
    tmp.notation = NOTATION_COMPACT;
    tmp.compact_display = pr->compact_display;
    return compact_exponent(&tmp, intl_decimal_magnitude(&res->rounded));
}

static int pr_resolve(JSContext *ctx, const IntlPluralRules *pr, const IntlDecimal *x, char **formatted)
{
    IntlRawResult res;
    int cat;
    *formatted = NULL;
    if (x->kind != INTL_DEC_FINITE)
        return 5;
    if (intl_format_numeric_to_string(&pr->d, x, &res) < 0) {
        JS_ThrowOutOfMemory(ctx);
        return -1;
    }
    cat = intl_plural_select(pr->data, pr->ordinal, res.str, pr_compact_exponent(pr, &res));
    *formatted = res.str;
    res.str = NULL;
    intl_raw_result_free(&res);
    return cat;
}

int intl_pr_select_double(JSContext *ctx, JSValueConst pr_obj, double x)
{
    IntlPluralRules *pr = JS_GetOpaque(pr_obj, js_intl_class_id(INTL_CLASS_PLURAL_RULES));
    IntlDecimal d;
    char *formatted;
    int cat;
    if (intl_decimal_from_double(x, &d) < 0) {
        JS_ThrowOutOfMemory(ctx);
        return -1;
    }
    cat = pr_resolve(ctx, pr, &d, &formatted);
    intl_decimal_free(&d);
    free(formatted);
    return cat;
}

static JSValue js_intl_pr_select(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    IntlPluralRules *pr = get_pr(ctx, this_val, "select");
    IntlDecimal x;
    char *formatted;
    int cat;
    if (!pr)
        return JS_EXCEPTION;
    if (intl_to_intl_mv(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &x) < 0)
        return JS_EXCEPTION;
    cat = pr_resolve(ctx, pr, &x, &formatted);
    intl_decimal_free(&x);
    free(formatted);
    if (cat < 0)
        return JS_EXCEPTION;
    return JS_NewString(ctx, intl_plural_categories[cat]);
}

static JSValue js_intl_pr_selectRange(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    IntlPluralRules *pr = get_pr(ctx, this_val, "selectRange");
    JSValueConst start = argc > 0 ? argv[0] : JS_UNDEFINED, end = argc > 1 ? argv[1] : JS_UNDEFINED;
    IntlDecimal x, y;
    char *fx = NULL, *fy = NULL;
    int cx, cy, r;
    if (!pr)
        return JS_EXCEPTION;
    if (JS_IsUndefined(start) || JS_IsUndefined(end))
        return JS_ThrowTypeError(ctx, "selectRange requires start and end");
    if (intl_to_intl_mv(ctx, start, &x) < 0)
        return JS_EXCEPTION;
    if (intl_to_intl_mv(ctx, end, &y) < 0) {
        intl_decimal_free(&x);
        return JS_EXCEPTION;
    }
    if (x.kind == INTL_DEC_NAN || y.kind == INTL_DEC_NAN) {
        intl_decimal_free(&x);
        intl_decimal_free(&y);
        return JS_ThrowRangeError(ctx, "selectRange: NaN is not allowed");
    }
    cx = pr_resolve(ctx, pr, &x, &fx);
    cy = cx < 0 ? -1 : pr_resolve(ctx, pr, &y, &fy);
    intl_decimal_free(&x);
    intl_decimal_free(&y);
    if (cx < 0 || cy < 0) {
        free(fx);
        free(fy);
        return JS_EXCEPTION;
    }
    if (fx && fy && !strcmp(fx, fy))
        r = cx;
    else
        r = pr->ordinal ? cy : intl_plural_select_range(pr->data, cx, cy);
    free(fx);
    free(fy);
    return JS_NewString(ctx, intl_plural_categories[r]);
}

static JSValue js_intl_pr_resolvedOptions(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    IntlPluralRules *pr = get_pr(ctx, this_val, "resolvedOptions");
    JSValue obj;
    int cats[6], n;
    const char *names[6];
    if (!pr)
        return JS_EXCEPTION;
    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return obj;
    n = intl_plural_category_list(pr->data, pr->ordinal, cats);
    for (int i = 0; i < n; i++)
        names[i] = intl_plural_categories[cats[i]];
    if (intl_set_prop_str(ctx, obj, "locale", pr->locale) < 0 ||
        intl_set_prop_str(ctx, obj, "type", pr_type_names[pr->ordinal]) < 0 ||
        intl_set_prop_str(ctx, obj, "notation", notation_names[pr->notation]) < 0 ||
        (pr->compact_display >= 0 &&
         intl_set_prop_str(ctx, obj, "compactDisplay", compact_names[pr->compact_display]) < 0) ||
        intl_resolved_digit_options(ctx, obj, &pr->d, true) < 0 ||
        intl_set_prop_value(ctx, obj, "pluralCategories", intl_new_string_array(ctx, names, n)) < 0 ||
        intl_resolved_digit_options(ctx, obj, &pr->d, false) < 0) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    return obj;
}

static const JSCFunctionListEntry js_intl_pr_proto_funcs[] = {
    JS_CFUNC_DEF("select", 1, js_intl_pr_select),
    JS_CFUNC_DEF("selectRange", 2, js_intl_pr_selectRange),
    JS_CFUNC_DEF("resolvedOptions", 0, js_intl_pr_resolvedOptions),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Intl.PluralRules", JS_PROP_CONFIGURABLE),
};

int js_intl_init_plural_rules(JSContext *ctx, JSValueConst intl)
{
    return intl_define_service(ctx, intl, "PluralRules", INTL_CLASS_PLURAL_RULES, INTL_INTRINSIC_PLURAL_RULES,
                               js_intl_pr_ctor, 0, JS_CFUNC_constructor, js_intl_pr_proto_funcs,
                               countof(js_intl_pr_proto_funcs), pr_finalizer, NULL);
}
