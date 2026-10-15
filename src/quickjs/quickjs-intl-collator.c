/* quickjs-intl-collator.c: Intl.Collator, String.prototype.localeCompare and locale-sensitive case mapping. */

#include <stdlib.h>
#include <string.h>

#include "quickjs-intl.h"
#include "libunicode.h"

static const struct {
    const char *lang;
    const char *collations;
} locale_collations[] = {
    { "ar", "compat" },
    { "bn", "trad" },
    { "de", "phonebk" },
    { "es", "trad" },
    { "fi", "trad" },
    { "hi", "direct" },
    { "ja", "unihan" },
    { "kn", "trad" },
    { "ko", "searchjl unihan" },
    { "ln", "phonetic" },
    { "si", "dict" },
    { "sv", "reformed" },
    { "vi", "trad" },
    { "zh", "big5han gb2312 pinyin stroke unihan zhuyin" },
};

static const char *const all_collations[] = {
    "big5han", "compat", "dict", "direct", "emoji", "eor", "gb2312", "phonebk", "phonetic", "pinyin",
    "reformed", "searchjl", "stroke", "trad", "unihan", "zhuyin",
};

static int cmp_cstr(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int locale_collation_list(const char *locale, const char **list, int max)
{
    char lang[9];
    int n = 0;
    size_t llen = strcspn(locale, "-");
    if (llen >= sizeof(lang))
        llen = sizeof(lang) - 1;
    memcpy(lang, locale, llen);
    lang[llen] = '\0';
    list[n++] = "emoji";
    list[n++] = "eor";
    for (size_t i = 0; i < countof(locale_collations); i++) {
        if (!strcmp(locale_collations[i].lang, lang)) {
            const char *p = locale_collations[i].collations;
            while (*p && n < max) {
                size_t len = strcspn(p, " ");
                for (size_t j = 0; j < countof(all_collations); j++)
                    if (strlen(all_collations[j]) == len && !memcmp(all_collations[j], p, len))
                        list[n++] = all_collations[j];
                p += len;
                if (*p == ' ')
                    p++;
            }
        }
    }
    qsort(list, n, sizeof(list[0]), cmp_cstr);
    return n;
}

JSValue intl_collations_of_locale(JSContext *ctx, const char *locale)
{
    const char *list[24];
    char buf[INTL_TAG_MAX];
    int n;
    if (intl_lookup_available(INTL_CLASS_COLLATOR, locale, buf, sizeof(buf)))
        n = locale_collation_list(buf, list, countof(list));
    else
        n = locale_collation_list("und", list, countof(list));
    return intl_new_string_array(ctx, list, n);
}

JSValue intl_available_collations(JSContext *ctx)
{
    return intl_new_string_array(ctx, all_collations, countof(all_collations));
}

enum { SENS_BASE, SENS_ACCENT, SENS_CASE, SENS_VARIANT };
enum { CF_UPPER, CF_LOWER, CF_FALSE };

static const char *const usage_names[] = { "sort", "search", NULL };
static const char *const sensitivity_names[] = { "base", "accent", "case", "variant", NULL };
static const char *const case_first_names[] = { "upper", "lower", "false", NULL };

enum {
    TAILOR_NONE,
    TAILOR_SV,
    TAILOR_DA,
    TAILOR_DE_PHONEBOOK,
    TAILOR_ES,
    TAILOR_TR,
    TAILOR_PL,
    TAILOR_CS,
    TAILOR_FI,
    TAILOR_ET,
    TAILOR_HU,
    TAILOR_LT,
    TAILOR_LV,
    TAILOR_RO,
    TAILOR_HR,
};

typedef struct IntlCollator {
    char locale[INTL_TAG_MAX];
    bool search;
    char collation[16];
    bool numeric;
    int case_first;
    int sensitivity;
    bool ignore_punctuation;
    int tailoring;
    JSValue bound_compare;
} IntlCollator;

typedef struct CE {
    uint32_t p;
    uint16_t s;
    uint8_t t;
} CE;

typedef struct CEList {
    CE *items;
    int count;
    int size;
} CEList;

static bool ce_push(CEList *l, uint32_t p, uint16_t s, uint8_t t)
{
    if (l->count == l->size) {
        int size = l->size ? l->size * 2 : 32;
        CE *items = realloc(l->items, sizeof(CE) * size);
        if (!items)
            return false;
        l->items = items;
        l->size = size;
    }
    l->items[l->count].p = p;
    l->items[l->count].s = s;
    l->items[l->count].t = t;
    l->count++;
    return true;
}

#define P_SPACE 0x00010000u
#define P_PUNCT 0x00020000u
#define P_SYMBOL 0x00030000u
#define P_DIGIT 0x00100000u
#define P_LATIN 0x01000000u
#define P_GREEK 0x02000000u
#define P_CYRILLIC 0x03000000u
#define P_OTHER 0x10000000u

#define S_BASE 0x20
#define T_LOWER 0x02
#define T_UPPER 0x08

static int digit_value(uint32_t c)
{
    static const uint32_t zeros[] = {
        0x30, 0x660, 0x6f0, 0x7c0, 0x966, 0x9e6, 0xa66, 0xae6, 0xb66, 0xbe6, 0xc66, 0xce6, 0xd66,
        0xde6, 0xe50, 0xed0, 0xf20, 0x1040, 0x1090, 0x17e0, 0x1810, 0x1946, 0x19d0, 0x1a80, 0x1a90,
        0x1b50, 0x1bb0, 0x1c40, 0x1c50, 0xa620, 0xa8d0, 0xa900, 0xa9d0, 0xa9f0, 0xaa50, 0xabf0, 0xff10,
        0x104a0, 0x11066, 0x110f0, 0x11136, 0x111d0, 0x112f0, 0x11450, 0x114d0, 0x11650, 0x116c0,
        0x11730, 0x118e0, 0x11950, 0x11c50, 0x11d50, 0x11da0, 0x16a60, 0x16b50, 0x1d7ce, 0x1d7d8,
        0x1d7e2, 0x1d7ec, 0x1d7f6, 0x1e140, 0x1e2f0, 0x1e950,
    };
    for (size_t i = 0; i < countof(zeros); i++)
        if (c >= zeros[i] && c < zeros[i] + 10)
            return (int)(c - zeros[i]);
    return -1;
}

static bool is_combining_mark(uint32_t c)
{
    if (lre_combining_class(c) != 0)
        return true;
    return (c >= 0x300 && c <= 0x36f) || (c >= 0x1ab0 && c <= 0x1aff) || (c >= 0x1dc0 && c <= 0x1dff) ||
           (c >= 0x20d0 && c <= 0x20ff) || (c >= 0xfe20 && c <= 0xfe2f) || (c >= 0x900 && c <= 0x903) ||
           (c >= 0x93a && c <= 0x94f && c != 0x93d) || (c >= 0xe31 && c <= 0xe3a && c != 0xe32 && c != 0xe33) ||
           (c >= 0xe47 && c <= 0xe4e);
}

static uint32_t to_lower_cp(uint32_t c)
{
    uint32_t res[LRE_CC_RES_LEN_MAX];
    int n = lre_case_conv(res, c, 1);
    return n == 1 ? res[0] : c;
}

static uint32_t latin_primary(char letter, int extra)
{
    return P_LATIN + (uint32_t)(letter - 'a') * 16 + 8 + extra;
}

static int tailoring_for(const char *locale, const char *collation, bool search)
{
    char lang[9];
    size_t llen = strcspn(locale, "-");
    if (llen >= sizeof(lang))
        llen = sizeof(lang) - 1;
    memcpy(lang, locale, llen);
    lang[llen] = '\0';
    if (!strcmp(lang, "de") && (!strcmp(collation, "phonebk") || search))
        return TAILOR_DE_PHONEBOOK;
    if (!strcmp(lang, "sv"))
        return TAILOR_SV;
    if (!strcmp(lang, "fi"))
        return TAILOR_FI;
    if (!strcmp(lang, "da") || !strcmp(lang, "nb") || !strcmp(lang, "nn") || !strcmp(lang, "no"))
        return TAILOR_DA;
    if (!strcmp(lang, "es"))
        return TAILOR_ES;
    if (!strcmp(lang, "tr") || !strcmp(lang, "az"))
        return TAILOR_TR;
    if (!strcmp(lang, "pl"))
        return TAILOR_PL;
    if (!strcmp(lang, "cs") || !strcmp(lang, "sk"))
        return TAILOR_CS;
    if (!strcmp(lang, "et"))
        return TAILOR_ET;
    if (!strcmp(lang, "hu"))
        return TAILOR_HU;
    if (!strcmp(lang, "lt"))
        return TAILOR_LT;
    if (!strcmp(lang, "lv"))
        return TAILOR_LV;
    if (!strcmp(lang, "ro"))
        return TAILOR_RO;
    if (!strcmp(lang, "hr") || !strcmp(lang, "bs") || !strcmp(lang, "sr"))
        return TAILOR_HR;
    return TAILOR_NONE;
}

typedef struct Tailor {
    char base;
    uint32_t mark;
    char after;
    int rank;
    bool expand_e;
} Tailor;

static const Tailor tailor_sv[] = {
    { 'a', 0x30a, 'z', 1, false }, { 'a', 0x308, 'z', 2, false }, { 'o', 0x308, 'z', 3, false },
    { 'u', 0x308, 'y', 0, false },
};
static const Tailor tailor_fi[] = {
    { 'a', 0x30a, 'z', 1, false }, { 'a', 0x308, 'z', 2, false }, { 'o', 0x308, 'z', 3, false },
};
static const Tailor tailor_da[] = {
    { 'a', 0x30a, 'z', 3, false }, { 'a', 0x308, 'z', 1, false }, { 'o', 0x308, 'z', 2, false },
};
static const Tailor tailor_de[] = {
    { 'a', 0x308, 'a', 0, true }, { 'o', 0x308, 'o', 0, true }, { 'u', 0x308, 'u', 0, true },
};
static const Tailor tailor_es[] = { { 'n', 0x303, 'n', 1, false } };
static const Tailor tailor_tr[] = {
    { 'c', 0x327, 'c', 1, false }, { 'g', 0x306, 'g', 1, false }, { 'o', 0x308, 'o', 1, false },
    { 's', 0x327, 's', 1, false }, { 'u', 0x308, 'u', 1, false },
};
static const Tailor tailor_pl[] = {
    { 'a', 0x328, 'a', 1, false }, { 'c', 0x301, 'c', 1, false }, { 'e', 0x328, 'e', 1, false },
    { 'n', 0x301, 'n', 1, false }, { 'o', 0x301, 'o', 1, false }, { 's', 0x301, 's', 1, false },
    { 'z', 0x301, 'z', 1, false }, { 'z', 0x307, 'z', 2, false },
};
static const Tailor tailor_cs[] = {
    { 'c', 0x30c, 'c', 1, false }, { 'r', 0x30c, 'r', 1, false }, { 's', 0x30c, 's', 1, false },
    { 'z', 0x30c, 'z', 1, false },
};
static const Tailor tailor_et[] = {
    { 's', 0x30c, 's', 1, false }, { 'z', 0x30c, 'z', 1, false }, { 'o', 0x303, 'w', 1, false },
    { 'a', 0x308, 'w', 2, false }, { 'o', 0x308, 'w', 3, false }, { 'u', 0x308, 'w', 4, false },
};
static const Tailor tailor_hu[] = { { 'o', 0x308, 'o', 1, false }, { 'u', 0x308, 'u', 1, false } };
static const Tailor tailor_lt[] = {
    { 'c', 0x30c, 'c', 1, false }, { 's', 0x30c, 's', 1, false }, { 'z', 0x30c, 'z', 1, false },
};
static const Tailor tailor_lv[] = {
    { 'c', 0x30c, 'c', 1, false }, { 'g', 0x327, 'g', 1, false }, { 'k', 0x327, 'k', 1, false },
    { 'l', 0x327, 'l', 1, false }, { 'n', 0x327, 'n', 1, false }, { 's', 0x30c, 's', 1, false },
    { 'z', 0x30c, 'z', 1, false },
};
static const Tailor tailor_ro[] = {
    { 'a', 0x306, 'a', 1, false }, { 'a', 0x302, 'a', 2, false }, { 'i', 0x302, 'i', 1, false },
    { 's', 0x326, 's', 1, false }, { 't', 0x326, 't', 1, false },
};
static const Tailor tailor_hr[] = {
    { 'c', 0x30c, 'c', 1, false }, { 'c', 0x301, 'c', 2, false }, { 's', 0x30c, 's', 1, false },
    { 'z', 0x30c, 'z', 1, false },
};

static const Tailor *tailor_table(int t, int *n)
{
    switch (t) {
    case TAILOR_SV: *n = countof(tailor_sv); return tailor_sv;
    case TAILOR_FI: *n = countof(tailor_fi); return tailor_fi;
    case TAILOR_DA: *n = countof(tailor_da); return tailor_da;
    case TAILOR_DE_PHONEBOOK: *n = countof(tailor_de); return tailor_de;
    case TAILOR_ES: *n = countof(tailor_es); return tailor_es;
    case TAILOR_TR: *n = countof(tailor_tr); return tailor_tr;
    case TAILOR_PL: *n = countof(tailor_pl); return tailor_pl;
    case TAILOR_CS: *n = countof(tailor_cs); return tailor_cs;
    case TAILOR_ET: *n = countof(tailor_et); return tailor_et;
    case TAILOR_HU: *n = countof(tailor_hu); return tailor_hu;
    case TAILOR_LT: *n = countof(tailor_lt); return tailor_lt;
    case TAILOR_LV: *n = countof(tailor_lv); return tailor_lv;
    case TAILOR_RO: *n = countof(tailor_ro); return tailor_ro;
    case TAILOR_HR: *n = countof(tailor_hr); return tailor_hr;
    default: *n = 0; return NULL;
    }
}

static uint8_t tertiary(const IntlCollator *c, bool upper, uint8_t extra)
{
    bool up_first = c->case_first == CF_UPPER;
    if (upper)
        return (up_first ? T_LOWER : T_UPPER) + extra;
    return (up_first ? T_UPPER : T_LOWER) + extra;
}

static uint16_t mark_secondary(uint32_t m)
{
    static const uint32_t order[] = {
        0x301, 0x300, 0x306, 0x302, 0x30c, 0x30a, 0x308, 0x30b, 0x303, 0x307, 0x338, 0x327, 0x328,
        0x304, 0x309, 0x31b, 0x323, 0x326, 0x331,
    };
    for (size_t i = 0; i < countof(order); i++)
        if (order[i] == m)
            return S_BASE + 1 + (uint16_t)i;
    return S_BASE + 0x40 + (uint16_t)(m & 0x3ff);
}

static bool is_variable(uint32_t c)
{
    if (lre_is_space(c))
        return true;
    if (c < 0x80)
        return !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
    return !lre_is_id_continue(c) && digit_value(c) < 0;
}

static uint32_t variable_primary(uint32_t c)
{
    if (lre_is_space(c))
        return P_SPACE + (c & 0xffff);
    if (c < 0x80) {
        static const char punct[] = "_-,;:!?.'\"()[]{}@*/\\&#%`^+<=>|~$";
        const char *p = strchr(punct, (int)c);
        if (p && *p)
            return P_PUNCT + (uint32_t)(p - punct);
        return P_SYMBOL + c;
    }
    return P_SYMBOL + 0x100 + (c & 0xfffff);
}

static bool build_ces(const IntlCollator *coll, const uint32_t *s, int n, CEList *out)
{
    int tn;
    const Tailor *tt = tailor_table(coll->tailoring, &tn);
    for (int i = 0; i < n; i++) {
        uint32_t c = s[i];
        if (c < 0x20 || (c >= 0x7f && c < 0xa0) || c == 0x200b || c == 0x200c || c == 0x200d ||
            c == 0xfeff || c == 0xad)
            continue;
        if (is_combining_mark(c)) {
            if (!ce_push(out, 0, mark_secondary(c), 0))
                return false;
            continue;
        }
        if (coll->numeric && digit_value(c) >= 0) {
            int j = i, start;
            while (j < n && digit_value(s[j]) == 0)
                j++;
            start = j;
            while (j < n && digit_value(s[j]) >= 0)
                j++;
            if (start == j) {
                if (!ce_push(out, P_DIGIT, S_BASE, T_LOWER))
                    return false;
            } else {
                if (!ce_push(out, P_DIGIT + 0x1000 + (uint32_t)(j - start), S_BASE, T_LOWER))
                    return false;
                for (int k = start; k < j; k++)
                    if (!ce_push(out, P_DIGIT + 0x10 + (uint32_t)digit_value(s[k]), S_BASE, T_LOWER))
                        return false;
            }
            i = j - 1;
            continue;
        }
        if (digit_value(c) >= 0) {
            if (!ce_push(out, P_DIGIT + 0x10 + (uint32_t)digit_value(c), S_BASE, c >= 0x80 ? T_LOWER + 1 : T_LOWER))
                return false;
            continue;
        }
        if (is_variable(c)) {
            if (coll->ignore_punctuation)
                continue;
            if (!ce_push(out, variable_primary(c), S_BASE, T_LOWER))
                return false;
            continue;
        }
        {
            uint32_t lower = to_lower_cp(c);
            bool upper = lower != c;
            uint8_t textra = 0;
            if (lower >= 0xff41 && lower <= 0xff5a) {
                lower = lower - 0xff41 + 'a';
                textra = 1;
            }
            if (c >= 0xff21 && c <= 0xff3a) {
                lower = c - 0xff21 + 'a';
                upper = true;
                textra = 1;
            }
            if (lower >= 'a' && lower <= 'z') {
                uint32_t mark = (i + 1 < n && is_combining_mark(s[i + 1])) ? s[i + 1] : 0;
                bool done = false;
                if (coll->tailoring == TAILOR_TR && lower == 'i' && upper && mark == 0x307) {
                    if (!ce_push(out, latin_primary('i', 0), S_BASE, tertiary(coll, true, 0)))
                        return false;
                    i++;
                    continue;
                }
                if (coll->tailoring == TAILOR_TR && lower == 'i' && !upper) {
                    if (!ce_push(out, latin_primary('i', 0), S_BASE, tertiary(coll, false, 0)))
                        return false;
                    continue;
                }
                for (int k = 0; k < tn && mark; k++) {
                    if (tt[k].base == (char)lower && tt[k].mark == mark) {
                        if (tt[k].expand_e) {
                            if (!ce_push(out, latin_primary(tt[k].base, 0), S_BASE, tertiary(coll, upper, textra)) ||
                                !ce_push(out, latin_primary('e', 0), S_BASE + 0x60, tertiary(coll, upper, textra)))
                                return false;
                        } else if (!ce_push(out, latin_primary(tt[k].after, tt[k].rank), S_BASE,
                                            tertiary(coll, upper, textra))) {
                            return false;
                        }
                        i++;
                        done = true;
                        break;
                    }
                }
                if (done)
                    continue;
                if (coll->tailoring == TAILOR_CS && lower == 'c' && i + 1 < n && (s[i + 1] | 0x20) == 'h') {
                    if (!ce_push(out, latin_primary('h', 1), S_BASE, tertiary(coll, upper, textra)))
                        return false;
                    i++;
                    continue;
                }
                if (!ce_push(out, latin_primary((char)lower, 0), S_BASE, tertiary(coll, upper, textra)))
                    return false;
                continue;
            }
            switch (lower) {
            case 0xe6:
                if (coll->tailoring == TAILOR_DA || coll->tailoring == TAILOR_SV || coll->tailoring == TAILOR_FI) {
                    if (!ce_push(out, latin_primary('z', coll->tailoring == TAILOR_DA ? 1 : 2), S_BASE,
                                 tertiary(coll, upper, 1)))
                        return false;
                    continue;
                }
                if (!ce_push(out, latin_primary('a', 0), S_BASE, tertiary(coll, upper, 2)) ||
                    !ce_push(out, latin_primary('e', 0), S_BASE + 0x70, tertiary(coll, upper, 2)))
                    return false;
                continue;
            case 0x153:
                if (!ce_push(out, latin_primary('o', 0), S_BASE, tertiary(coll, upper, 2)) ||
                    !ce_push(out, latin_primary('e', 0), S_BASE + 0x70, tertiary(coll, upper, 2)))
                    return false;
                continue;
            case 0xdf:
                if (!ce_push(out, latin_primary('s', 0), S_BASE, tertiary(coll, upper, 2)) ||
                    !ce_push(out, latin_primary('s', 0), S_BASE, tertiary(coll, upper, 2)))
                    return false;
                continue;
            case 0xf8:
                if (coll->tailoring == TAILOR_DA || coll->tailoring == TAILOR_SV || coll->tailoring == TAILOR_FI) {
                    if (!ce_push(out, latin_primary('z', coll->tailoring == TAILOR_DA ? 2 : 3), S_BASE,
                                 tertiary(coll, upper, 1)))
                        return false;
                    continue;
                }
                if (!ce_push(out, latin_primary('o', 0), S_BASE + 0x50, tertiary(coll, upper, 0)))
                    return false;
                continue;
            case 0x111:
                if (!ce_push(out, latin_primary('d', coll->tailoring == TAILOR_HR ? 1 : 0),
                             coll->tailoring == TAILOR_HR ? S_BASE : S_BASE + 0x50, tertiary(coll, upper, 0)))
                    return false;
                continue;
            case 0x142:
                if (!ce_push(out, latin_primary('l', coll->tailoring == TAILOR_PL ? 1 : 0),
                             coll->tailoring == TAILOR_PL ? S_BASE : S_BASE + 0x50, tertiary(coll, upper, 0)))
                    return false;
                continue;
            case 0x131:
                if (!ce_push(out, coll->tailoring == TAILOR_TR ? latin_primary('i', -1) : latin_primary('i', 0),
                             coll->tailoring == TAILOR_TR ? S_BASE : S_BASE + 0x50, tertiary(coll, upper, 0)))
                    return false;
                continue;
            case 0xf0:
                if (!ce_push(out, latin_primary('d', 1), S_BASE, tertiary(coll, upper, 0)))
                    return false;
                continue;
            case 0xfe:
                if (!ce_push(out, latin_primary('z', 5), S_BASE, tertiary(coll, upper, 0)))
                    return false;
                continue;
            case 0x14b:
                if (!ce_push(out, latin_primary('n', 2), S_BASE, tertiary(coll, upper, 0)))
                    return false;
                continue;
            case 0x127:
                if (!ce_push(out, latin_primary('h', 0), S_BASE + 0x50, tertiary(coll, upper, 0)))
                    return false;
                continue;
            case 0x3c2:
                lower = 0x3c3;
                textra = 1;
                break;
            default:
                break;
            }
            if (lower >= 0x30a1 && lower <= 0x30f6) {
                lower -= 0x60;
                textra = 1;
            }
            {
                uint32_t p;
                if (lower >= 0x370 && lower <= 0x3ff)
                    p = P_GREEK + lower;
                else if (lower >= 0x400 && lower <= 0x52f)
                    p = P_CYRILLIC + lower;
                else
                    p = P_OTHER + lower;
                if (!ce_push(out, p, S_BASE, tertiary(coll, upper, textra)))
                    return false;
            }
        }
    }
    return true;
}

static void *intl_realloc(void *opaque, void *ptr, size_t size)
{
    if (!size) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

static int to_nfd(const uint16_t *s, size_t len, uint32_t **out)
{
    uint32_t *cps = malloc(sizeof(uint32_t) * (len + 1));
    int n = 0, r;
    if (!cps)
        return -1;
    for (size_t i = 0; i < len; i++) {
        uint32_t c = s[i];
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < len && s[i + 1] >= 0xdc00 && s[i + 1] < 0xe000) {
            c = 0x10000 + ((c - 0xd800) << 10) + (s[i + 1] - 0xdc00);
            i++;
        }
        cps[n++] = c;
    }
    r = unicode_normalize(out, cps, n, UNICODE_NFD, NULL, intl_realloc);
    free(cps);
    return r;
}

static int compare_ces(const IntlCollator *c, const CEList *a, const CEList *b)
{
    int i = 0, j = 0;
    for (;;) {
        while (i < a->count && a->items[i].p == 0)
            i++;
        while (j < b->count && b->items[j].p == 0)
            j++;
        if (i >= a->count || j >= b->count) {
            if (i < a->count)
                return 1;
            if (j < b->count)
                return -1;
            break;
        }
        if (a->items[i].p != b->items[j].p)
            return a->items[i].p < b->items[j].p ? -1 : 1;
        i++;
        j++;
    }
    if (c->sensitivity == SENS_ACCENT || c->sensitivity == SENS_VARIANT) {
        i = j = 0;
        for (;;) {
            if (i >= a->count || j >= b->count) {
                if (i < a->count)
                    return 1;
                if (j < b->count)
                    return -1;
                break;
            }
            if (a->items[i].s != b->items[j].s)
                return a->items[i].s < b->items[j].s ? -1 : 1;
            i++;
            j++;
        }
    }
    if (c->sensitivity == SENS_CASE || c->sensitivity == SENS_VARIANT) {
        i = j = 0;
        for (;;) {
            while (i < a->count && a->items[i].p == 0)
                i++;
            while (j < b->count && b->items[j].p == 0)
                j++;
            if (i >= a->count || j >= b->count) {
                if (i < a->count)
                    return 1;
                if (j < b->count)
                    return -1;
                break;
            }
            {
                uint8_t ta = a->items[i].t, tb = b->items[j].t;
                if (c->sensitivity == SENS_CASE) {
                    ta = (ta >= T_UPPER) == (c->case_first != CF_UPPER) ? 1 : 0;
                    tb = (tb >= T_UPPER) == (c->case_first != CF_UPPER) ? 1 : 0;
                }
                if (ta != tb)
                    return ta < tb ? -1 : 1;
            }
            i++;
            j++;
        }
    }
    return 0;
}

static int ascii_fast_compare(const IntlCollator *c, const uint16_t *a, size_t na, const uint16_t *b, size_t nb,
                              bool *handled)
{
    size_t i;
    *handled = false;
    if (c->numeric || c->ignore_punctuation || c->tailoring != TAILOR_NONE)
        return 0;
    for (i = 0; i < na; i++)
        if (a[i] >= 0x80 || a[i] < 0x20)
            return 0;
    for (i = 0; i < nb; i++)
        if (b[i] >= 0x80 || b[i] < 0x20)
            return 0;
    *handled = true;
    {
        size_t n = na < nb ? na : nb;
        int tert = 0;
        for (i = 0; i < n; i++) {
            int ca = a[i], cb = b[i];
            int la = (ca >= 'A' && ca <= 'Z') ? ca + 32 : ca;
            int lb = (cb >= 'A' && cb <= 'Z') ? cb + 32 : cb;
            if (la != lb) {
                *handled = false;
                return 0;
            }
            if (!tert && ca != cb)
                tert = ((ca >= 'A' && ca <= 'Z') == (c->case_first != CF_UPPER)) ? 1 : -1;
        }
        if (na != nb) {
            *handled = false;
            return 0;
        }
        if (c->sensitivity == SENS_CASE || c->sensitivity == SENS_VARIANT)
            return tert;
        return 0;
    }
}

int intl_collator_compare_utf16(const IntlCollator *c, const uint16_t *a, size_t na, const uint16_t *b,
                                size_t nb, int *result)
{
    uint32_t *da = NULL, *db = NULL;
    CEList ca = { 0 }, cb = { 0 };
    int la, lb, r = -1;
    bool handled;
    *result = ascii_fast_compare(c, a, na, b, nb, &handled);
    if (handled)
        return 0;
    la = to_nfd(a, na, &da);
    lb = la < 0 ? -1 : to_nfd(b, nb, &db);
    if (la < 0 || lb < 0)
        goto done;
    if (!build_ces(c, da, la, &ca) || !build_ces(c, db, lb, &cb))
        goto done;
    *result = compare_ces(c, &ca, &cb);
    r = 0;
done:
    free(da);
    free(db);
    free(ca.items);
    free(cb.items);
    return r;
}

static const char *const coll_keys[] = { "co", "kf", "kn" };

static bool coll_key_supported(const char *locale, const char *key, const char *value)
{
    if (!strcmp(key, "co")) {
        const char *list[24];
        int n = locale_collation_list(locale, list, countof(list));
        for (int i = 0; i < n; i++)
            if (!strcmp(list[i], value))
                return true;
        return false;
    }
    if (!strcmp(key, "kf"))
        return !strcmp(value, "upper") || !strcmp(value, "lower") || !strcmp(value, "false");
    return !strcmp(value, "true") || !strcmp(value, "false");
}

static const char *coll_key_default(const char *locale, const char *key)
{
    if (!strcmp(key, "co"))
        return NULL;
    if (!strcmp(key, "kf")) {
        if (!strncmp(locale, "da", 2) && (locale[2] == '\0' || locale[2] == '-'))
            return "upper";
        if (!strncmp(locale, "mt", 2) && (locale[2] == '\0' || locale[2] == '-'))
            return "upper";
        return "false";
    }
    return "false";
}

static const IntlResolverOps coll_resolver = { coll_keys, 3, coll_key_supported, coll_key_default };

static int collator_init(JSContext *ctx, IntlCollator *c, JSValueConst locales, JSValueConst options_in)
{
    IntlLocaleList requested;
    JSValue options = JS_UNDEFINED;
    IntlResolved res;
    bool lookup, has_co, has_kf;
    char co[64], kf[16], kn_buf[8];
    char *opts[3];
    int usage, kn, sens, ip, r = -1;

    memset(c, 0, sizeof(*c));
    c->bound_compare = JS_UNDEFINED;
    if (intl_canonicalize_locale_list(ctx, locales, &requested) < 0)
        return -1;
    options = intl_coerce_options(ctx, options_in);
    if (JS_IsException(options))
        goto done;
    usage = intl_get_enum_option(ctx, options, "usage", usage_names, 0);
    if (usage < -1)
        goto done;
    c->search = usage == 1;
    if (intl_get_locale_matcher(ctx, options, &lookup) < 0)
        goto done;
    if (intl_get_resolution_option(ctx, options, "collation", NULL, co, sizeof(co), &has_co) < 0)
        goto done;
    kn = intl_get_bool_option(ctx, options, "numeric", -1);
    if (kn < -1)
        goto done;
    if (intl_get_string_option(ctx, options, "caseFirst", case_first_names, NULL, kf, sizeof(kf), &has_kf) < 0)
        goto done;
    if (kn >= 0)
        strcpy(kn_buf, kn ? "true" : "false");
    opts[0] = has_co ? co : NULL;
    opts[1] = has_kf ? kf : NULL;
    opts[2] = kn >= 0 ? kn_buf : NULL;
    if (intl_resolve_locale(ctx, INTL_CLASS_COLLATOR, &requested, lookup, &coll_resolver, opts, &res) < 0)
        goto done;
    memcpy(c->locale, res.locale, sizeof(c->locale));
    if (res.values[0][0] && strlen(res.values[0]) < sizeof(c->collation))
        strcpy(c->collation, res.values[0]);
    else
        strcpy(c->collation, "default");
    c->numeric = !strcmp(res.values[2], "true");
    c->case_first = !strcmp(res.values[1], "upper") ? CF_UPPER : !strcmp(res.values[1], "lower") ? CF_LOWER : CF_FALSE;
    sens = intl_get_enum_option(ctx, options, "sensitivity", sensitivity_names, SENS_VARIANT);
    if (sens < -1)
        goto done;
    c->sensitivity = sens;
    {
        bool def_ip = !strncmp(res.data_locale, "th", 2) && (res.data_locale[2] == '\0' || res.data_locale[2] == '-');
        ip = intl_get_bool_option(ctx, options, "ignorePunctuation", def_ip);
        if (ip < -1)
            goto done;
        c->ignore_punctuation = ip;
    }
    c->tailoring = tailoring_for(res.data_locale, c->collation, c->search);
    r = 0;
done:
    intl_locale_list_free(ctx, &requested);
    JS_FreeValue(ctx, options);
    return r;
}

static void collator_finalizer(JSRuntime *rt, JSValueConst val)
{
    IntlCollator *c = JS_GetOpaque(val, js_intl_class_id(INTL_CLASS_COLLATOR));
    if (c) {
        JS_FreeValueRT(rt, c->bound_compare);
        free(c);
    }
}

static void collator_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func)
{
    IntlCollator *c = JS_GetOpaque(val, js_intl_class_id(INTL_CLASS_COLLATOR));
    if (c)
        JS_MarkValue(rt, c->bound_compare, mark_func);
}

static JSValue js_intl_collator_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    JSValue obj, ctor = JS_UNDEFINED;
    IntlCollator *c;
    if (JS_IsUndefined(new_target)) {
        ctor = js_intl_get_intrinsic(ctx, INTL_INTRINSIC_COLLATOR);
        new_target = ctor;
    }
    obj = js_intl_create_from_ctor(ctx, new_target, js_intl_class_id(INTL_CLASS_COLLATOR));
    JS_FreeValue(ctx, ctor);
    if (JS_IsException(obj))
        return obj;
    c = malloc(sizeof(*c));
    if (!c) {
        JS_FreeValue(ctx, obj);
        return JS_ThrowOutOfMemory(ctx);
    }
    if (collator_init(ctx, c, argc > 0 ? argv[0] : JS_UNDEFINED, argc > 1 ? argv[1] : JS_UNDEFINED) < 0) {
        free(c);
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    js_intl_set_opaque(obj, c);
    return obj;
}

static JSValue compare_values(JSContext *ctx, const IntlCollator *c, JSValueConst x, JSValueConst y)
{
    JSValue sx, sy;
    const uint16_t *a, *b;
    size_t na, nb;
    int result, r;
    sx = JS_ToString(ctx, x);
    if (JS_IsException(sx))
        return sx;
    sy = JS_ToString(ctx, y);
    if (JS_IsException(sy)) {
        JS_FreeValue(ctx, sx);
        return sy;
    }
    a = JS_ToCStringLenUTF16(ctx, &na, sx);
    b = a ? JS_ToCStringLenUTF16(ctx, &nb, sy) : NULL;
    JS_FreeValue(ctx, sx);
    JS_FreeValue(ctx, sy);
    if (!a || !b) {
        if (a)
            JS_FreeCStringUTF16(ctx, a);
        return JS_EXCEPTION;
    }
    r = intl_collator_compare_utf16(c, a, na, b, nb, &result);
    JS_FreeCStringUTF16(ctx, a);
    JS_FreeCStringUTF16(ctx, b);
    if (r < 0)
        return JS_ThrowOutOfMemory(ctx);
    return JS_NewInt32(ctx, result);
}

static JSValue collator_bound_compare(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
                                      int magic, JSValueConst *func_data)
{
    IntlCollator *c = JS_GetOpaque(func_data[0], js_intl_class_id(INTL_CLASS_COLLATOR));
    if (!c)
        return JS_ThrowTypeError(ctx, "invalid Collator");
    return compare_values(ctx, c, argc > 0 ? argv[0] : JS_UNDEFINED, argc > 1 ? argv[1] : JS_UNDEFINED);
}

static IntlCollator *get_collator(JSContext *ctx, JSValueConst this_val, const char *method)
{
    IntlCollator *c = JS_GetOpaque(this_val, js_intl_class_id(INTL_CLASS_COLLATOR));
    if (!c)
        JS_ThrowTypeError(ctx, "Intl.Collator.prototype.%s called on incompatible receiver", method);
    return c;
}

static JSValue js_intl_collator_get_compare(JSContext *ctx, JSValueConst this_val)
{
    IntlCollator *c = get_collator(ctx, this_val, "compare");
    if (!c)
        return JS_EXCEPTION;
    if (JS_IsUndefined(c->bound_compare)) {
        JSValue f = JS_NewCFunctionData2(ctx, collator_bound_compare, "", 2, 0, 1, &this_val);
        if (JS_IsException(f))
            return f;
        c->bound_compare = f;
    }
    return JS_DupValue(ctx, c->bound_compare);
}

static JSValue js_intl_collator_resolvedOptions(JSContext *ctx, JSValueConst this_val, int argc,
                                                JSValueConst *argv)
{
    IntlCollator *c = get_collator(ctx, this_val, "resolvedOptions");
    JSValue obj;
    if (!c)
        return JS_EXCEPTION;
    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return obj;
    if (intl_set_prop_str(ctx, obj, "locale", c->locale) < 0 ||
        intl_set_prop_str(ctx, obj, "usage", usage_names[c->search]) < 0 ||
        intl_set_prop_str(ctx, obj, "sensitivity", sensitivity_names[c->sensitivity]) < 0 ||
        intl_set_prop_value(ctx, obj, "ignorePunctuation", JS_NewBool(ctx, c->ignore_punctuation)) < 0 ||
        intl_set_prop_str(ctx, obj, "collation", c->collation) < 0 ||
        intl_set_prop_value(ctx, obj, "numeric", JS_NewBool(ctx, c->numeric)) < 0 ||
        intl_set_prop_str(ctx, obj, "caseFirst", case_first_names[c->case_first]) < 0) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    return obj;
}

static const JSCFunctionListEntry js_intl_collator_proto_funcs[] = {
    JS_CGETSET_DEF("compare", js_intl_collator_get_compare, NULL),
    JS_CFUNC_DEF("resolvedOptions", 0, js_intl_collator_resolvedOptions),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Intl.Collator", JS_PROP_CONFIGURABLE),
};

static JSValue js_string_localeCompare(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    IntlCollator c;
    JSValue s, that, r;
    if (JS_IsUndefined(this_val) || JS_IsNull(this_val))
        return JS_ThrowTypeError(ctx, "String.prototype.localeCompare called on null or undefined");
    s = JS_ToString(ctx, this_val);
    if (JS_IsException(s))
        return s;
    that = JS_ToString(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
    if (JS_IsException(that)) {
        JS_FreeValue(ctx, s);
        return that;
    }
    if (collator_init(ctx, &c, argc > 1 ? argv[1] : JS_UNDEFINED, argc > 2 ? argv[2] : JS_UNDEFINED) < 0) {
        r = JS_EXCEPTION;
    } else {
        r = compare_values(ctx, &c, s, that);
    }
    JS_FreeValue(ctx, s);
    JS_FreeValue(ctx, that);
    return r;
}

static bool is_soft_dotted(uint32_t c)
{
    static const uint32_t ranges[][2] = {
        { 0x69, 0x6a }, { 0x12f, 0x12f }, { 0x249, 0x249 }, { 0x268, 0x268 }, { 0x29d, 0x29d },
        { 0x2b2, 0x2b2 }, { 0x3f3, 0x3f3 }, { 0x456, 0x456 }, { 0x458, 0x458 }, { 0x1d62, 0x1d62 },
        { 0x1d96, 0x1d96 }, { 0x1da4, 0x1da4 }, { 0x1da8, 0x1da8 }, { 0x1e2d, 0x1e2d }, { 0x1ecb, 0x1ecb },
        { 0x2071, 0x2071 }, { 0x2148, 0x2149 }, { 0x2c7c, 0x2c7c }, { 0x1d422, 0x1d423 }, { 0x1d456, 0x1d457 },
        { 0x1d48a, 0x1d48b }, { 0x1d4be, 0x1d4bf }, { 0x1d4f2, 0x1d4f3 }, { 0x1d526, 0x1d527 },
        { 0x1d55a, 0x1d55b }, { 0x1d58e, 0x1d58f }, { 0x1d5c2, 0x1d5c3 }, { 0x1d5f6, 0x1d5f7 },
        { 0x1d62a, 0x1d62b }, { 0x1d65e, 0x1d65f }, { 0x1d692, 0x1d693 }, { 0x1df1a, 0x1df1a },
        { 0x1e04c, 0x1e04d }, { 0x1e068, 0x1e068 },
    };
    for (size_t i = 0; i < countof(ranges); i++)
        if (c >= ranges[i][0] && c <= ranges[i][1])
            return true;
    return false;
}

bool intl_collator_has_language(const char *tag, size_t len)
{
    for (size_t i = 0; i < countof(locale_collations); i++)
        if (strlen(locale_collations[i].lang) == len && !memcmp(locale_collations[i].lang, tag, len))
            return true;
    return false;
}

static bool more_above(const uint32_t *s, int n, int i)
{
    for (int k = i + 1; k < n; k++) {
        int cc = lre_combining_class(s[k]);
        if (cc == 230)
            return true;
        if (cc == 0)
            return false;
    }
    return false;
}

static bool before_dot(const uint32_t *s, int n, int i)
{
    for (int k = i + 1; k < n; k++) {
        int cc;
        if (s[k] == 0x307)
            return true;
        cc = lre_combining_class(s[k]);
        if (cc == 0 || cc == 230)
            return false;
    }
    return false;
}

static bool after_soft_dotted(const uint32_t *s, int i)
{
    for (int k = i - 1; k >= 0; k--) {
        int cc;
        if (is_soft_dotted(s[k]))
            return true;
        cc = lre_combining_class(s[k]);
        if (cc == 0 || cc == 230)
            return false;
    }
    return false;
}

static bool after_i(const uint32_t *s, int i)
{
    for (int k = i - 1; k >= 0; k--) {
        int cc;
        if (s[k] == 'I')
            return true;
        cc = lre_combining_class(s[k]);
        if (cc == 0 || cc == 230)
            return false;
    }
    return false;
}

static bool final_sigma(const uint32_t *s, int n, int i)
{
    int k;
    for (k = i - 1; k >= 0; k--) {
        if (lre_is_case_ignorable(s[k]))
            continue;
        if (!lre_is_cased(s[k]))
            return false;
        break;
    }
    if (k < 0)
        return false;
    for (k = i + 1; k < n; k++) {
        if (lre_is_case_ignorable(s[k]))
            continue;
        return !lre_is_cased(s[k]);
    }
    return true;
}

static void put_cp(uint16_t **out, size_t *len, uint32_t c)
{
    if (c >= 0x10000) {
        c -= 0x10000;
        (*out)[(*len)++] = 0xd800 + (c >> 10);
        (*out)[(*len)++] = 0xdc00 + (c & 0x3ff);
    } else {
        (*out)[(*len)++] = (uint16_t)c;
    }
}

static JSValue special_case(JSContext *ctx, JSValueConst str, const char *lang, bool to_lower)
{
    const uint16_t *src;
    size_t len, olen = 0;
    uint32_t *cps;
    uint16_t *out;
    int n = 0;
    bool tr = !strcmp(lang, "tr") || !strcmp(lang, "az");
    JSValue r;
    src = JS_ToCStringLenUTF16(ctx, &len, str);
    if (!src)
        return JS_EXCEPTION;
    cps = malloc(sizeof(uint32_t) * (len + 1));
    out = malloc(sizeof(uint16_t) * (len * 6 + 1));
    if (!cps || !out) {
        free(cps);
        free(out);
        JS_FreeCStringUTF16(ctx, src);
        return JS_ThrowOutOfMemory(ctx);
    }
    for (size_t i = 0; i < len; i++) {
        uint32_t c = src[i];
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < len && src[i + 1] >= 0xdc00 && src[i + 1] < 0xe000) {
            c = 0x10000 + ((c - 0xd800) << 10) + (src[i + 1] - 0xdc00);
            i++;
        }
        cps[n++] = c;
    }
    JS_FreeCStringUTF16(ctx, src);
    for (int i = 0; i < n; i++) {
        uint32_t c = cps[i];
        uint32_t res[LRE_CC_RES_LEN_MAX];
        int k;
        if (to_lower) {
            if (tr) {
                if (c == 0x130) {
                    put_cp(&out, &olen, 'i');
                    continue;
                }
                if (c == 0x307 && after_i(cps, i))
                    continue;
                if (c == 'I' && !before_dot(cps, n, i)) {
                    put_cp(&out, &olen, 0x131);
                    continue;
                }
            } else {
                if ((c == 'I' || c == 'J' || c == 0x12e) && more_above(cps, n, i)) {
                    put_cp(&out, &olen, c == 'I' ? 'i' : c == 'J' ? 'j' : 0x12f);
                    put_cp(&out, &olen, 0x307);
                    continue;
                }
                if (c == 0xcc || c == 0xcd || c == 0x128) {
                    put_cp(&out, &olen, 'i');
                    put_cp(&out, &olen, 0x307);
                    put_cp(&out, &olen, c == 0xcc ? 0x300 : c == 0xcd ? 0x301 : 0x303);
                    continue;
                }
            }
            if (c == 0x3a3) {
                put_cp(&out, &olen, final_sigma(cps, n, i) ? 0x3c2 : 0x3c3);
                continue;
            }
            k = lre_case_conv(res, c, 1);
        } else {
            if (tr && c == 'i') {
                put_cp(&out, &olen, 0x130);
                continue;
            }
            if (!tr && c == 0x307 && after_soft_dotted(cps, i))
                continue;
            k = lre_case_conv(res, c, 0);
        }
        for (int j = 0; j < k; j++)
            put_cp(&out, &olen, res[j]);
    }
    r = JS_NewStringUTF16(ctx, out, olen);
    free(cps);
    free(out);
    return r;
}

static JSValue js_string_toLocaleCase(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
                                      int to_lower)
{
    JSValue s, r;
    IntlLocaleList list;
    char lang[9] = "";
    if (JS_IsUndefined(this_val) || JS_IsNull(this_val))
        return JS_ThrowTypeError(ctx, "String.prototype.toLocale%sCase called on null or undefined",
                                 to_lower ? "Lower" : "Upper");
    s = JS_ToString(ctx, this_val);
    if (JS_IsException(s))
        return s;
    if (intl_canonicalize_locale_list(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &list) < 0) {
        JS_FreeValue(ctx, s);
        return JS_EXCEPTION;
    }
    {
        const char *loc = list.count > 0 ? list.items[0] : NULL;
        char *def = NULL;
        size_t l;
        if (!loc) {
            def = intl_default_locale(ctx);
            if (!def) {
                intl_locale_list_free(ctx, &list);
                JS_FreeValue(ctx, s);
                return JS_EXCEPTION;
            }
            loc = def;
        }
        l = strcspn(loc, "-");
        if (l < sizeof(lang)) {
            memcpy(lang, loc, l);
            lang[l] = '\0';
        }
        free(def);
    }
    intl_locale_list_free(ctx, &list);
    if (!strcmp(lang, "tr") || !strcmp(lang, "az") || !strcmp(lang, "lt"))
        r = special_case(ctx, s, lang, to_lower);
    else
        r = js_intl_string_case(ctx, s, to_lower);
    JS_FreeValue(ctx, s);
    return r;
}

static int install_string_method(JSContext *ctx, const char *name, JSCFunction *fn, JSCFunctionMagic *mfn,
                                 int length, int magic)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue ctor = JS_GetPropertyStr(ctx, global, "String");
    JSValue proto = JS_GetPropertyStr(ctx, ctor, "prototype");
    JSValue f = fn ? JS_NewCFunction2(ctx, fn, name, length, JS_CFUNC_generic, 0)
                   : JS_NewCFunctionMagic(ctx, mfn, name, length, JS_CFUNC_generic_magic, magic);
    int r = JS_DefinePropertyValueStr(ctx, proto, name, f, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_FreeValue(ctx, proto);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, global);
    return r;
}

int js_intl_init_collator(JSContext *ctx, JSValueConst intl)
{
    if (intl_define_service(ctx, intl, "Collator", INTL_CLASS_COLLATOR, INTL_INTRINSIC_COLLATOR,
                            js_intl_collator_ctor, 0, JS_CFUNC_constructor_or_func, js_intl_collator_proto_funcs,
                            countof(js_intl_collator_proto_funcs), collator_finalizer, collator_mark) < 0)
        return -1;
    if (install_string_method(ctx, "localeCompare", js_string_localeCompare, NULL, 1, 0) < 0 ||
        install_string_method(ctx, "toLocaleLowerCase", NULL, js_string_toLocaleCase, 0, 1) < 0 ||
        install_string_method(ctx, "toLocaleUpperCase", NULL, js_string_toLocaleCase, 0, 0) < 0)
        return -1;
    return 0;
}
