/* quickjs-intl-segmenter.c: Intl.Segmenter with UAX #29 grapheme, word and sentence boundaries. */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "quickjs-intl.h"
#include "quickjs-intl-segment-data.h"

enum { GRAN_GRAPHEME, GRAN_WORD, GRAN_SENTENCE };

enum {
    GCB_OTHER, GCB_CR, GCB_LF, GCB_CONTROL, GCB_EXTEND, GCB_ZWJ, GCB_RI, GCB_PREPEND, GCB_SPACINGMARK, GCB_L, GCB_V,
    GCB_T, GCB_LV, GCB_LVT,
};
enum {
    WB_OTHER, WB_CR, WB_LF, WB_NEWLINE, WB_EXTEND, WB_ZWJ, WB_RI, WB_FORMAT, WB_KATAKANA, WB_HL, WB_ALETTER, WB_SQ,
    WB_DQ, WB_MIDNUMLET, WB_MIDLETTER, WB_MIDNUM, WB_NUMERIC, WB_EXTENDNUMLET, WB_WSEGSPACE,
};
enum {
    SB_OTHER, SB_CR, SB_LF, SB_EXTEND, SB_SEP, SB_FORMAT, SB_SP, SB_LOWER, SB_UPPER, SB_OLETTER, SB_NUMERIC,
    SB_ATERM, SB_SCONTINUE, SB_STERM, SB_CLOSE,
};
enum { INCB_NONE, INCB_LINKER, INCB_CONSONANT, INCB_EXTEND };

typedef struct IntlSegmenter {
    char locale[INTL_TAG_MAX];
    int granularity;
} IntlSegmenter;

typedef struct IntlSegments {
    JSValue string;
    int granularity;
    uint32_t len;
    uint32_t *bounds;
    uint8_t *word_like;
    uint32_t nbounds;
} IntlSegments;

typedef struct IntlSegmentIterator {
    JSValue segments;
    uint32_t position;
} IntlSegmentIterator;

static const char *const gran_names[] = { "grapheme", "word", "sentence", NULL };

static uint32_t cp_props(uint32_t c)
{
    int lo = 0, hi = (int)countof(intl_segment_ranges) - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if ((intl_segment_ranges[mid] >> 8) <= c)
            lo = mid;
        else
            hi = mid - 1;
    }
    return intl_segment_props[intl_segment_ranges[lo] & 0xff];
}

typedef struct CpText {
    uint32_t *props;
    uint32_t *pos;
    int n;
} CpText;

static int gcb(const CpText *t, int i)
{
    return (int)(t->props[i] & 0xf);
}

static int wb(const CpText *t, int i)
{
    return (int)((t->props[i] >> 4) & 0x1f);
}

static int sb(const CpText *t, int i)
{
    return (int)((t->props[i] >> 9) & 0xf);
}

static int incb(const CpText *t, int i)
{
    return (int)((t->props[i] >> 13) & 3);
}

static bool ext_pict(const CpText *t, int i)
{
    return (t->props[i] >> 15) & 1;
}

static bool south_asian(const CpText *t, int i)
{
    return (t->props[i] >> 16) & 1;
}

static bool alphabetic(const CpText *t, int i)
{
    return (t->props[i] >> 17) & 1;
}

static bool grapheme_break(const CpText *t, int i)
{
    int a = gcb(t, i - 1), b = gcb(t, i);
    if (a == GCB_CR && b == GCB_LF)
        return false;
    if (a == GCB_CONTROL || a == GCB_CR || a == GCB_LF)
        return true;
    if (b == GCB_CONTROL || b == GCB_CR || b == GCB_LF)
        return true;
    if (a == GCB_L && (b == GCB_L || b == GCB_V || b == GCB_LV || b == GCB_LVT))
        return false;
    if ((a == GCB_LV || a == GCB_V) && (b == GCB_V || b == GCB_T))
        return false;
    if ((a == GCB_LVT || a == GCB_T) && b == GCB_T)
        return false;
    if (b == GCB_EXTEND || b == GCB_ZWJ || b == GCB_SPACINGMARK || a == GCB_PREPEND)
        return false;
    if (incb(t, i) == INCB_CONSONANT) {
        bool linker = false;
        int j = i - 1;
        while (j >= 0 && (incb(t, j) == INCB_EXTEND || incb(t, j) == INCB_LINKER)) {
            if (incb(t, j) == INCB_LINKER)
                linker = true;
            j--;
        }
        if (linker && j >= 0 && incb(t, j) == INCB_CONSONANT)
            return false;
    }
    if (a == GCB_ZWJ && ext_pict(t, i)) {
        int j = i - 2;
        while (j >= 0 && gcb(t, j) == GCB_EXTEND)
            j--;
        if (j >= 0 && ext_pict(t, j))
            return false;
    }
    if (a == GCB_RI && b == GCB_RI) {
        int count = 0;
        for (int j = i - 1; j >= 0 && gcb(t, j) == GCB_RI; j--)
            count++;
        if (count % 2 == 1)
            return false;
    }
    return true;
}

static bool wb_ignorable(int w)
{
    return w == WB_EXTEND || w == WB_FORMAT || w == WB_ZWJ;
}

static int wb_prev(const CpText *t, int i)
{
    int j = i - 1;
    while (j > 0 && wb_ignorable(wb(t, j)))
        j--;
    return j;
}

static int wb_next(const CpText *t, int i)
{
    int j = i + 1;
    while (j < t->n && wb_ignorable(wb(t, j)))
        j++;
    return j;
}

static bool ahletter(int w)
{
    return w == WB_ALETTER || w == WB_HL;
}

static bool mid_num_let_q(int w)
{
    return w == WB_MIDNUMLET || w == WB_SQ;
}

static bool word_break(const CpText *t, int i)
{
    int a = wb(t, i - 1), b = wb(t, i), p, A, AA = -1, C = -1;
    if (a == WB_CR && b == WB_LF)
        return false;
    if (a == WB_NEWLINE || a == WB_CR || a == WB_LF)
        return true;
    if (b == WB_NEWLINE || b == WB_CR || b == WB_LF)
        return true;
    if (a == WB_ZWJ && ext_pict(t, i))
        return false;
    if (a == WB_WSEGSPACE && b == WB_WSEGSPACE)
        return false;
    if (wb_ignorable(b))
        return false;
    p = wb_prev(t, i);
    A = wb(t, p);
    if (p > 0) {
        int pp = wb_prev(t, p);
        if (pp >= 0 && pp < p)
            AA = wb(t, pp);
    }
    {
        int q = wb_next(t, i);
        if (q < t->n)
            C = wb(t, q);
    }
    if (ahletter(A) && ahletter(b))
        return false;
    if (ahletter(A) && (b == WB_MIDLETTER || mid_num_let_q(b)) && ahletter(C))
        return false;
    if (ahletter(AA) && (A == WB_MIDLETTER || mid_num_let_q(A)) && ahletter(b))
        return false;
    if (A == WB_HL && b == WB_SQ)
        return false;
    if (A == WB_HL && b == WB_DQ && C == WB_HL)
        return false;
    if (AA == WB_HL && A == WB_DQ && b == WB_HL)
        return false;
    if (A == WB_NUMERIC && b == WB_NUMERIC)
        return false;
    if (ahletter(A) && b == WB_NUMERIC)
        return false;
    if (A == WB_NUMERIC && ahletter(b))
        return false;
    if (AA == WB_NUMERIC && (A == WB_MIDNUM || mid_num_let_q(A)) && b == WB_NUMERIC)
        return false;
    if (A == WB_NUMERIC && (b == WB_MIDNUM || mid_num_let_q(b)) && C == WB_NUMERIC)
        return false;
    if (A == WB_KATAKANA && b == WB_KATAKANA)
        return false;
    if ((ahletter(A) || A == WB_NUMERIC || A == WB_KATAKANA || A == WB_EXTENDNUMLET) && b == WB_EXTENDNUMLET)
        return false;
    if (A == WB_EXTENDNUMLET && (ahletter(b) || b == WB_NUMERIC || b == WB_KATAKANA))
        return false;
    if (A == WB_RI && b == WB_RI) {
        int count = 0;
        for (int j = p; j >= 0 && (wb(t, j) == WB_RI || wb_ignorable(wb(t, j))); j--)
            if (wb(t, j) == WB_RI)
                count++;
        if (count % 2 == 1)
            return false;
    }
    if (south_asian(t, p) && south_asian(t, i))
        return false;
    return true;
}

static bool sb_ignorable(int s)
{
    return s == SB_EXTEND || s == SB_FORMAT;
}

static int sb_prev(const CpText *t, int i)
{
    int j = i - 1;
    while (j >= 0 && sb_ignorable(sb(t, j)))
        j--;
    return j;
}

static bool para_sep(int s)
{
    return s == SB_SEP || s == SB_CR || s == SB_LF;
}

static bool sb8_lower_follows(const CpText *t, int i)
{
    for (int j = i; j < t->n; j++) {
        int s = sb(t, j);
        if (s == SB_LOWER)
            return true;
        if (s == SB_OLETTER || s == SB_UPPER || para_sep(s) || s == SB_ATERM || s == SB_STERM)
            return false;
    }
    return false;
}

static bool sentence_break(const CpText *t, int i)
{
    int a = sb(t, i - 1), b = sb(t, i), p, A, j, term;
    bool sp_seen = false;
    if (a == SB_CR && b == SB_LF)
        return false;
    if (para_sep(a))
        return true;
    if (sb_ignorable(b))
        return false;
    p = sb_prev(t, i);
    if (p < 0)
        return false;
    A = sb(t, p);
    if (A == SB_ATERM && b == SB_NUMERIC)
        return false;
    if (A == SB_ATERM && b == SB_UPPER) {
        int pp = sb_prev(t, p);
        if (pp >= 0 && (sb(t, pp) == SB_UPPER || sb(t, pp) == SB_LOWER))
            return false;
    }
    j = p;
    while (j >= 0 && sb(t, j) == SB_SP) {
        sp_seen = true;
        j = sb_prev(t, j);
    }
    while (j >= 0 && sb(t, j) == SB_CLOSE)
        j = sb_prev(t, j);
    term = j >= 0 ? sb(t, j) : -1;
    if (term != SB_ATERM && term != SB_STERM)
        return false;
    if (term == SB_ATERM && sb8_lower_follows(t, i))
        return false;
    if (b == SB_SCONTINUE || b == SB_ATERM || b == SB_STERM)
        return false;
    if (!sp_seen && (b == SB_CLOSE || b == SB_SP || para_sep(b)))
        return false;
    if (b == SB_SP || para_sep(b))
        return false;
    return true;
}

static bool decode_text(const uint16_t *s, uint32_t len, CpText *t)
{
    int n = 0;
    t->props = malloc(sizeof(uint32_t) * (len + 1));
    t->pos = malloc(sizeof(uint32_t) * (len + 1));
    if (!t->props || !t->pos)
        return false;
    for (uint32_t i = 0; i < len;) {
        uint32_t c = s[i];
        t->pos[n] = i;
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < len && s[i + 1] >= 0xdc00 && s[i + 1] < 0xe000) {
            c = 0x10000 + ((c - 0xd800) << 10) + (s[i + 1] - 0xdc00);
            i += 2;
        } else {
            i++;
        }
        t->props[n++] = cp_props(c);
    }
    t->pos[n] = len;
    t->n = n;
    return true;
}

static bool segment_word_like(const CpText *t, int start, int end)
{
    for (int i = start; i < end; i++) {
        int w = wb(t, i);
        if (alphabetic(t, i) || w == WB_NUMERIC || w == WB_ALETTER || w == WB_HL || w == WB_KATAKANA)
            return true;
    }
    return false;
}

static int compute_bounds(IntlSegments *seg, const uint16_t *s)
{
    CpText t = { NULL, NULL, 0 };
    int r = -1, prev = 0;
    if (!decode_text(s, seg->len, &t))
        goto done;
    seg->bounds = malloc(sizeof(uint32_t) * ((size_t)t.n + 1));
    seg->word_like = malloc((size_t)t.n + 1);
    if (!seg->bounds || !seg->word_like)
        goto done;
    seg->nbounds = 0;
    seg->bounds[seg->nbounds++] = 0;
    for (int i = 1; i <= t.n; i++) {
        bool brk = i == t.n;
        if (!brk) {
            if (seg->granularity == GRAN_GRAPHEME)
                brk = grapheme_break(&t, i);
            else if (seg->granularity == GRAN_WORD)
                brk = word_break(&t, i);
            else
                brk = sentence_break(&t, i);
        }
        if (brk) {
            seg->word_like[seg->nbounds - 1] = seg->granularity == GRAN_WORD && segment_word_like(&t, prev, i);
            seg->bounds[seg->nbounds++] = t.pos[i];
            prev = i;
        }
    }
    r = 0;
done:
    free(t.props);
    free(t.pos);
    return r;
}

static void segmenter_finalizer(JSRuntime *rt, JSValueConst val)
{
    free(JS_GetOpaque(val, js_intl_class_id(INTL_CLASS_SEGMENTER)));
}

static void segments_finalizer(JSRuntime *rt, JSValueConst val)
{
    IntlSegments *seg = JS_GetOpaque(val, js_intl_class_id(INTL_CLASS_SEGMENTS));
    if (seg) {
        JS_FreeValueRT(rt, seg->string);
        free(seg->bounds);
        free(seg->word_like);
        free(seg);
    }
}

static void segments_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func)
{
    IntlSegments *seg = JS_GetOpaque(val, js_intl_class_id(INTL_CLASS_SEGMENTS));
    if (seg)
        JS_MarkValue(rt, seg->string, mark_func);
}

static void iterator_finalizer(JSRuntime *rt, JSValueConst val)
{
    IntlSegmentIterator *it = JS_GetOpaque(val, js_intl_class_id(INTL_CLASS_SEGMENT_ITERATOR));
    if (it) {
        JS_FreeValueRT(rt, it->segments);
        free(it);
    }
}

static void iterator_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func)
{
    IntlSegmentIterator *it = JS_GetOpaque(val, js_intl_class_id(INTL_CLASS_SEGMENT_ITERATOR));
    if (it)
        JS_MarkValue(rt, it->segments, mark_func);
}

static JSValue js_intl_segmenter_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    JSValue obj, options = JS_UNDEFINED;
    IntlSegmenter *sg = NULL;
    IntlLocaleList requested;
    IntlResolved res;
    bool lookup;
    obj = js_intl_create_from_ctor(ctx, new_target, js_intl_class_id(INTL_CLASS_SEGMENTER));
    if (JS_IsException(obj))
        return obj;
    if (intl_canonicalize_locale_list(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &requested) < 0)
        goto fail0;
    sg = calloc(1, sizeof(*sg));
    if (!sg) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    options = intl_get_options_object(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
    if (JS_IsException(options))
        goto fail;
    if (intl_get_locale_matcher(ctx, options, &lookup) < 0)
        goto fail;
    if (intl_resolve_locale(ctx, INTL_CLASS_SEGMENTER, &requested, lookup, NULL, NULL, &res) < 0)
        goto fail;
    snprintf(sg->locale, sizeof(sg->locale), "%s", res.locale);
    sg->granularity = intl_get_enum_option(ctx, options, "granularity", gran_names, GRAN_GRAPHEME);
    if (sg->granularity < -1)
        goto fail;
    js_intl_set_opaque(obj, sg);
    intl_locale_list_free(ctx, &requested);
    JS_FreeValue(ctx, options);
    return obj;
fail:
    intl_locale_list_free(ctx, &requested);
fail0:
    free(sg);
    JS_FreeValue(ctx, options);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue js_intl_segmenter_segment(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    IntlSegmenter *sg = JS_GetOpaque(this_val, js_intl_class_id(INTL_CLASS_SEGMENTER));
    JSValue str, proto, obj;
    IntlSegments *seg;
    uint16_t *units;
    if (!sg)
        return JS_ThrowTypeError(ctx, "Intl.Segmenter.prototype.segment called on incompatible receiver");
    str = JS_ToString(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
    if (JS_IsException(str))
        return str;
    seg = calloc(1, sizeof(*seg));
    if (!seg) {
        JS_FreeValue(ctx, str);
        return JS_ThrowOutOfMemory(ctx);
    }
    seg->string = str;
    seg->granularity = sg->granularity;
    if (js_intl_string_utf16(ctx, str, &units, &seg->len) < 0) {
        JS_FreeValue(ctx, str);
        free(seg);
        return JS_EXCEPTION;
    }
    if (compute_bounds(seg, units) < 0) {
        js_free(ctx, units);
        JS_FreeValue(ctx, str);
        free(seg->bounds);
        free(seg->word_like);
        free(seg);
        return JS_ThrowOutOfMemory(ctx);
    }
    js_free(ctx, units);
    proto = js_intl_get_intrinsic(ctx, INTL_INTRINSIC_SEGMENTS_PROTO);
    obj = JS_NewObjectProtoClass(ctx, proto, js_intl_class_id(INTL_CLASS_SEGMENTS));
    JS_FreeValue(ctx, proto);
    if (JS_IsException(obj)) {
        JS_FreeValue(ctx, str);
        free(seg->bounds);
        free(seg->word_like);
        free(seg);
        return obj;
    }
    js_intl_set_opaque(obj, seg);
    return obj;
}

static JSValue js_intl_segmenter_resolvedOptions(JSContext *ctx, JSValueConst this_val, int argc,
                                                 JSValueConst *argv)
{
    IntlSegmenter *sg = JS_GetOpaque(this_val, js_intl_class_id(INTL_CLASS_SEGMENTER));
    JSValue obj;
    if (!sg)
        return JS_ThrowTypeError(ctx, "Intl.Segmenter.prototype.resolvedOptions called on incompatible receiver");
    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return obj;
    if (intl_set_prop_str(ctx, obj, "locale", sg->locale) < 0 ||
        intl_set_prop_str(ctx, obj, "granularity", gran_names[sg->granularity]) < 0) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    return obj;
}

static uint32_t find_segment(const IntlSegments *seg, uint32_t index)
{
    uint32_t lo = 0, hi = seg->nbounds - 1;
    while (hi - lo > 1) {
        uint32_t mid = (lo + hi) / 2;
        if (seg->bounds[mid] <= index)
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

static JSValue segment_data(JSContext *ctx, const IntlSegments *seg, uint32_t k)
{
    JSValue obj = JS_NewObject(ctx), sub;
    uint32_t start = seg->bounds[k], end = seg->bounds[k + 1];
    if (JS_IsException(obj))
        return obj;
    sub = js_intl_sub_string(ctx, seg->string, start, end);
    if (JS_IsException(sub) || intl_set_prop_value(ctx, obj, "segment", sub) < 0 ||
        intl_set_prop_value(ctx, obj, "index", JS_NewUint32(ctx, start)) < 0 ||
        intl_set_prop_value(ctx, obj, "input", JS_DupValue(ctx, seg->string)) < 0 ||
        (seg->granularity == GRAN_WORD &&
         intl_set_prop_value(ctx, obj, "isWordLike", JS_NewBool(ctx, seg->word_like[k])) < 0)) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    return obj;
}

static JSValue js_intl_segments_containing(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    IntlSegments *seg = JS_GetOpaque(this_val, js_intl_class_id(INTL_CLASS_SEGMENTS));
    double n;
    if (!seg)
        return JS_ThrowTypeError(ctx, "%%Segments.prototype%%.containing called on incompatible receiver");
    if (JS_ToFloat64(ctx, &n, argc > 0 ? argv[0] : JS_UNDEFINED) < 0)
        return JS_EXCEPTION;
    n = isnan(n) ? 0 : trunc(n);
    if (n < 0 || n >= seg->len)
        return JS_UNDEFINED;
    return segment_data(ctx, seg, find_segment(seg, (uint32_t)n));
}

static JSValue js_intl_segments_iterator(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    IntlSegments *seg = JS_GetOpaque(this_val, js_intl_class_id(INTL_CLASS_SEGMENTS));
    IntlSegmentIterator *it;
    JSValue proto, obj;
    if (!seg)
        return JS_ThrowTypeError(ctx, "%%Segments.prototype%%[Symbol.iterator] called on incompatible receiver");
    it = calloc(1, sizeof(*it));
    if (!it)
        return JS_ThrowOutOfMemory(ctx);
    proto = js_intl_get_intrinsic(ctx, INTL_INTRINSIC_SEGMENT_ITERATOR_PROTO);
    obj = JS_NewObjectProtoClass(ctx, proto, js_intl_class_id(INTL_CLASS_SEGMENT_ITERATOR));
    JS_FreeValue(ctx, proto);
    if (JS_IsException(obj)) {
        free(it);
        return obj;
    }
    it->segments = JS_DupValue(ctx, this_val);
    js_intl_set_opaque(obj, it);
    return obj;
}

static JSValue js_intl_segment_iterator_next(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    IntlSegmentIterator *it = JS_GetOpaque(this_val, js_intl_class_id(INTL_CLASS_SEGMENT_ITERATOR));
    IntlSegments *seg;
    JSValue result, value;
    uint32_t k;
    if (!it)
        return JS_ThrowTypeError(ctx, "%%SegmentIterator.prototype%%.next called on incompatible receiver");
    seg = JS_GetOpaque(it->segments, js_intl_class_id(INTL_CLASS_SEGMENTS));
    result = JS_NewObject(ctx);
    if (JS_IsException(result))
        return result;
    if (it->position >= seg->len) {
        if (intl_set_prop_value(ctx, result, "value", JS_UNDEFINED) < 0 ||
            intl_set_prop_value(ctx, result, "done", JS_TRUE) < 0)
            goto fail;
        return result;
    }
    k = find_segment(seg, it->position);
    value = segment_data(ctx, seg, k);
    if (JS_IsException(value))
        goto fail;
    it->position = seg->bounds[k + 1];
    if (intl_set_prop_value(ctx, result, "value", value) < 0 ||
        intl_set_prop_value(ctx, result, "done", JS_FALSE) < 0)
        goto fail;
    return result;
fail:
    JS_FreeValue(ctx, result);
    return JS_EXCEPTION;
}

static const JSCFunctionListEntry js_intl_segmenter_proto_funcs[] = {
    JS_CFUNC_DEF("segment", 1, js_intl_segmenter_segment),
    JS_CFUNC_DEF("resolvedOptions", 0, js_intl_segmenter_resolvedOptions),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Intl.Segmenter", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_intl_segments_proto_funcs[] = {
    JS_CFUNC_DEF("containing", 1, js_intl_segments_containing),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_intl_segments_iterator),
};

static const JSCFunctionListEntry js_intl_segment_iterator_proto_funcs[] = {
    JS_CFUNC_DEF("next", 0, js_intl_segment_iterator_next),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Segmenter String Iterator", JS_PROP_CONFIGURABLE),
};

static int define_internal_class(JSContext *ctx, int cls, const char *name, JSClassFinalizer *finalizer,
                                 JSClassGCMark *mark, JSValueConst parent, const JSCFunctionListEntry *funcs,
                                 int nfuncs, int intrinsic)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    JSClassID cid = js_intl_class_id(cls);
    JSValue proto;
    if (!JS_IsRegisteredClass(rt, cid)) {
        JSClassDef def;
        memset(&def, 0, sizeof(def));
        def.class_name = name;
        def.finalizer = finalizer;
        def.gc_mark = mark;
        if (JS_NewClass(rt, cid, &def) < 0)
            return -1;
    }
    proto = JS_IsUndefined(parent) ? JS_NewObject(ctx) : JS_NewObjectProto(ctx, parent);
    if (JS_IsException(proto))
        return -1;
    if (JS_SetPropertyFunctionList(ctx, proto, funcs, nfuncs) < 0) {
        JS_FreeValue(ctx, proto);
        return -1;
    }
    js_intl_set_intrinsic(ctx, intrinsic, proto);
    return 0;
}

int js_intl_init_segmenter(JSContext *ctx, JSValueConst intl)
{
    JSValue iter_proto;
    int r;
    if (intl_define_service(ctx, intl, "Segmenter", INTL_CLASS_SEGMENTER, INTL_INTRINSIC_SEGMENTER,
                            js_intl_segmenter_ctor, 0, JS_CFUNC_constructor, js_intl_segmenter_proto_funcs,
                            countof(js_intl_segmenter_proto_funcs), segmenter_finalizer, NULL) < 0)
        return -1;
    if (define_internal_class(ctx, INTL_CLASS_SEGMENTS, "Segments", segments_finalizer, segments_mark, JS_UNDEFINED,
                              js_intl_segments_proto_funcs, countof(js_intl_segments_proto_funcs),
                              INTL_INTRINSIC_SEGMENTS_PROTO) < 0)
        return -1;
    iter_proto = js_intl_iterator_proto(ctx);
    r = define_internal_class(ctx, INTL_CLASS_SEGMENT_ITERATOR, "Segmenter String Iterator", iterator_finalizer,
                              iterator_mark, iter_proto, js_intl_segment_iterator_proto_funcs,
                              countof(js_intl_segment_iterator_proto_funcs), INTL_INTRINSIC_SEGMENT_ITERATOR_PROTO);
    JS_FreeValue(ctx, iter_proto);
    return r;
}
