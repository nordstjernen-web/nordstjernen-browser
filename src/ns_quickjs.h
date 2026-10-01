/* Nordstjernen — the quickjs-ng API the engine is written against, on either QuickJS.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_QUICKJS_H
#define NS_QUICKJS_H

#include <quickjs.h>

#ifdef NS_QUICKJS_ORIGINAL

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef JS_BOOL ns_js_bool;

#define JS_EVAL_OPTIONS_VERSION 1

typedef struct JSEvalOptions {
    int version;
    int eval_flags;
    const char *filename;
    int line_num;
    int col_num;
} JSEvalOptions;

typedef void *JSReallocArrayBufferDataFunc(JSRuntime *rt, void *opaque,
                                           void *ptr, size_t size);

JSContext *ns_quickjs_new_context(JSRuntime *rt);
bool ns_quickjs_is_array(JSValueConst val);
bool ns_quickjs_is_error(JSValueConst val);
JSValue ns_quickjs_new_array_buffer(JSContext *ctx, uint8_t *buf, size_t len,
                                    size_t max_len,
                                    JSReallocArrayBufferDataFunc *realloc_func,
                                    void *opaque, bool is_shared);

bool JS_IsArrayBuffer(JSValueConst val);
bool JS_IsDataView(JSValueConst val);
int JS_GetTypedArrayType(JSValueConst val);
bool JS_AtomIsArrayIndex(JSContext *ctx, uint32_t *pval, JSAtom atom);
JSValue JS_EvalThis2(JSContext *ctx, JSValueConst this_obj, const char *input,
                     size_t input_len, JSEvalOptions *options);
JSValue JS_ToObject(JSContext *ctx, JSValueConst val);
JSValue JS_NewStringUTF16(JSContext *ctx, const uint16_t *buf, size_t len);
JSValue __js_printf_like(3, 4) JS_ThrowDOMException(JSContext *ctx,
                                                    const char *name,
                                                    const char *fmt, ...);
const char *JS_GetVersion(void);

static inline bool
ns_quickjs_is_big_int(JSValueConst val)
{
    int tag = JS_VALUE_GET_TAG(val);
    return tag == JS_TAG_BIG_INT || tag == JS_TAG_SHORT_BIG_INT;
}

static inline bool
JS_IsStrictEqual(JSContext *ctx, JSValueConst op1, JSValueConst op2)
{
    return JS_StrictEq(ctx, op1, op2);
}

static inline JSContext *
JS_GetCallerRealm(JSContext *ctx)
{
    return ctx;
}

static inline JSContext *
JS_GetFunctionRealm(JSContext *ctx, JSValueConst func_obj)
{
    (void)func_obj;
    return ctx;
}

static inline int
JS_RepointArrayBuffer(JSContext *ctx, JSValueConst obj, uint8_t *data,
                      size_t byte_length)
{
    (void)ctx;
    (void)obj;
    (void)data;
    (void)byte_length;
    return -1;
}

#define JS_NewContext(rt) ns_quickjs_new_context(rt)
#define JS_IsArray(val)   ns_quickjs_is_array(val)
#define JS_IsError(val)   ns_quickjs_is_error(val)
#define JS_IsBigInt(val)  ns_quickjs_is_big_int(val)
#define JS_NewArrayBuffer(ctx, buf, len, max_len, realloc_func, opaque, shared) \
    ns_quickjs_new_array_buffer(ctx, buf, len, max_len, realloc_func, opaque, shared)

#else

typedef bool ns_js_bool;

#endif

#endif /* NS_QUICKJS_H */
