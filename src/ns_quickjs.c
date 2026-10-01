/* Nordstjernen — quickjs-ng API entry points built over Bellard's original QuickJS.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "ns_quickjs.h"

#include <glib.h>
#include <stdarg.h>
#include <string.h>

typedef struct ns_quickjs_class_ids {
    JSClassID array;
    JSClassID error;
    JSClassID array_buffer;
    JSClassID data_view;
    JSClassID typed_array[JS_TYPED_ARRAY_FLOAT64 + 1];
} ns_quickjs_class_ids;

typedef struct ns_quickjs_array_buffer_owner {
    JSReallocArrayBufferDataFunc *realloc_func;
    void *opaque;
} ns_quickjs_array_buffer_owner;

static ns_quickjs_class_ids ns_quickjs_classes;

static JSClassID
ns_quickjs_class_of(JSContext *ctx, JSValue val)
{
    JSClassID id = JS_GetClassID(val);
    if (JS_IsException(val))
        JS_FreeValue(ctx, JS_GetException(ctx));
    JS_FreeValue(ctx, val);
    return id;
}

static void
ns_quickjs_learn_class_ids(JSContext *ctx)
{
    static const uint8_t one_byte;
    ns_quickjs_class_ids *ids = &ns_quickjs_classes;
    ids->array = ns_quickjs_class_of(ctx, JS_NewArray(ctx));
    ids->error = ns_quickjs_class_of(ctx, JS_NewError(ctx));

    JSValue buffer = JS_NewArrayBufferCopy(ctx, &one_byte, 1);
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue data_view = JS_GetPropertyStr(ctx, global, "DataView");
    ids->data_view = ns_quickjs_class_of(ctx,
        JS_CallConstructor(ctx, data_view, 1, &buffer));
    JS_FreeValue(ctx, data_view);
    JS_FreeValue(ctx, global);
    ids->array_buffer = ns_quickjs_class_of(ctx, buffer);

    JSValue zero = JS_NewInt32(ctx, 0);
    for (int type = JS_TYPED_ARRAY_UINT8C; type <= JS_TYPED_ARRAY_FLOAT64; type++)
        ids->typed_array[type] = ns_quickjs_class_of(ctx,
            JS_NewTypedArray(ctx, 1, &zero, (JSTypedArrayEnum)type));
}

JSContext *
ns_quickjs_new_context(JSRuntime *rt)
{
    static gsize learned;
    JSContext *ctx = (JS_NewContext)(rt);
    if (ctx && g_once_init_enter(&learned)) {
        ns_quickjs_learn_class_ids(ctx);
        g_once_init_leave(&learned, 1);
    }
    return ctx;
}

static bool
ns_quickjs_has_class(JSValueConst val, JSClassID class_id)
{
    return class_id != JS_INVALID_CLASS_ID && JS_GetClassID(val) == class_id;
}

bool
ns_quickjs_is_array(JSValueConst val)
{
    return ns_quickjs_has_class(val, ns_quickjs_classes.array);
}

bool
ns_quickjs_is_error(JSValueConst val)
{
    return ns_quickjs_has_class(val, ns_quickjs_classes.error);
}

bool
JS_IsArrayBuffer(JSValueConst val)
{
    return ns_quickjs_has_class(val, ns_quickjs_classes.array_buffer);
}

bool
JS_IsDataView(JSValueConst val)
{
    return ns_quickjs_has_class(val, ns_quickjs_classes.data_view);
}

int
JS_GetTypedArrayType(JSValueConst val)
{
    for (int type = JS_TYPED_ARRAY_UINT8C; type <= JS_TYPED_ARRAY_FLOAT64; type++)
        if (ns_quickjs_has_class(val, ns_quickjs_classes.typed_array[type]))
            return type;
    return -1;
}

static void
ns_quickjs_array_buffer_free(JSRuntime *rt, void *opaque, void *ptr)
{
    ns_quickjs_array_buffer_owner *owner = opaque;
    owner->realloc_func(rt, owner->opaque, ptr, 0);
    g_free(owner);
}

JSValue
ns_quickjs_new_array_buffer(JSContext *ctx, uint8_t *buf, size_t len,
                            size_t max_len,
                            JSReallocArrayBufferDataFunc *realloc_func,
                            void *opaque, bool is_shared)
{
    if (max_len != 0)
        return JS_ThrowRangeError(ctx, "resizable external ArrayBuffer is not supported");
    if (!realloc_func)
        return (JS_NewArrayBuffer)(ctx, buf, len, NULL, opaque, is_shared);
    ns_quickjs_array_buffer_owner *owner = g_new(ns_quickjs_array_buffer_owner, 1);
    owner->realloc_func = realloc_func;
    owner->opaque = opaque;
    JSValue buffer = (JS_NewArrayBuffer)(ctx, buf, len,
                                         ns_quickjs_array_buffer_free, owner,
                                         is_shared);
    if (JS_IsException(buffer))
        g_free(owner);
    return buffer;
}

JSValue
ns_quickjs_new_typed_array(JSContext *ctx, int argc, JSValueConst *argv,
                           JSTypedArrayEnum type)
{
    JSValueConst padded[3] = { JS_UNDEFINED, JS_UNDEFINED, JS_UNDEFINED };
    if (argc >= 3)
        return (JS_NewTypedArray)(ctx, argc, argv, type);
    for (int i = 0; i < argc; i++)
        padded[i] = argv[i];
    return (JS_NewTypedArray)(ctx, 3, padded, type);
}

static bool
ns_quickjs_parse_array_index(const char *s, size_t len, uint32_t *pval)
{
    if (len == 0 || len > 10 || (len > 1 && s[0] == '0'))
        return false;
    uint64_t value = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9')
            return false;
        value = value * 10 + (uint64_t)(s[i] - '0');
    }
    if (value > 0xFFFFFFFEu)
        return false;
    *pval = (uint32_t)value;
    return true;
}

bool
JS_AtomIsArrayIndex(JSContext *ctx, uint32_t *pval, JSAtom atom)
{
    *pval = 0;
    JSValue key = JS_AtomToValue(ctx, atom);
    bool is_index = false;
    if (JS_IsString(key)) {
        size_t len = 0;
        const char *s = JS_ToCStringLen(ctx, &len, key);
        if (s) {
            is_index = ns_quickjs_parse_array_index(s, len, pval);
            JS_FreeCString(ctx, s);
        }
    }
    JS_FreeValue(ctx, key);
    return is_index;
}

JSValue
JS_EvalThis2(JSContext *ctx, JSValueConst this_obj, const char *input,
             size_t input_len, JSEvalOptions *options)
{
    const char *filename = options->filename ? options->filename : "<unnamed>";
    size_t lines = options->line_num > 1 ? (size_t)options->line_num - 1 : 0;
    size_t columns = options->col_num > 1 ? (size_t)options->col_num - 1 : 0;
    gboolean hashbang = input_len >= 2 && input[0] == '#' && input[1] == '!';
    if ((lines == 0 && columns == 0) || hashbang ||
        input_len > G_MAXSIZE - lines - columns - 1)
        return JS_EvalThis(ctx, this_obj, input, input_len, filename,
                           options->eval_flags);
    size_t padded_len = lines + columns + input_len;
    char *padded = g_try_malloc(padded_len + 1);
    if (!padded)
        return JS_ThrowOutOfMemory(ctx);
    memset(padded, '\n', lines);
    memset(padded + lines, ' ', columns);
    memcpy(padded + lines + columns, input, input_len);
    padded[padded_len] = '\0';
    JSValue result = JS_EvalThis(ctx, this_obj, padded, padded_len,
                                 filename, options->eval_flags);
    g_free(padded);
    return result;
}

JSValue
JS_ToObject(JSContext *ctx, JSValueConst val)
{
    if (JS_IsObject(val))
        return JS_DupValue(ctx, val);
    if (JS_IsNull(val) || JS_IsUndefined(val))
        return JS_ThrowTypeError(ctx, "Cannot convert undefined or null to object");
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue object_ctor = JS_GetPropertyStr(ctx, global, "Object");
    JS_FreeValue(ctx, global);
    JSValue obj = JS_Call(ctx, object_ctor, JS_UNDEFINED, 1, &val);
    JS_FreeValue(ctx, object_ctor);
    return obj;
}

static size_t
ns_quickjs_put_wtf8(char *out, uint32_t c)
{
    if (c < 0x80) {
        out[0] = (char)c;
        return 1;
    }
    if (c < 0x800) {
        out[0] = (char)(0xC0 | (c >> 6));
        out[1] = (char)(0x80 | (c & 0x3F));
        return 2;
    }
    if (c < 0x10000) {
        out[0] = (char)(0xE0 | (c >> 12));
        out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        out[2] = (char)(0x80 | (c & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (c >> 18));
    out[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((c >> 6) & 0x3F));
    out[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}

JSValue
JS_NewStringUTF16(JSContext *ctx, const uint16_t *buf, size_t len)
{
    if (len > (G_MAXSIZE - 1) / 3)
        return JS_ThrowRangeError(ctx, "invalid string length");
    char *wtf8 = g_try_malloc(len * 3 + 1);
    if (!wtf8)
        return JS_ThrowOutOfMemory(ctx);
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        uint32_t c = buf[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < len &&
            buf[i + 1] >= 0xDC00 && buf[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (uint32_t)(buf[i + 1] - 0xDC00);
            i++;
        }
        n += ns_quickjs_put_wtf8(wtf8 + n, c);
    }
    JSValue str = JS_NewStringLen(ctx, wtf8, n);
    g_free(wtf8);
    return str;
}

JSValue
JS_ThrowDOMException(JSContext *ctx, const char *name, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *message = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue ctor = JS_GetPropertyStr(ctx, global, "DOMException");
    JS_FreeValue(ctx, global);
    JSValue error;
    if (JS_IsConstructor(ctx, ctor)) {
        JSValue args[2] = { JS_NewString(ctx, message), JS_NewString(ctx, name) };
        error = JS_CallConstructor(ctx, ctor, 2, args);
        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, args[1]);
    } else {
        error = JS_NewError(ctx);
        if (!JS_IsException(error)) {
            JS_SetPropertyStr(ctx, error, "name", JS_NewString(ctx, name));
            JS_SetPropertyStr(ctx, error, "message", JS_NewString(ctx, message));
        }
    }
    JS_FreeValue(ctx, ctor);
    g_free(message);
    if (JS_IsException(error))
        return JS_EXCEPTION;
    return JS_Throw(ctx, error);
}

const char *
JS_GetVersion(void)
{
    return NS_QUICKJS_ORIGINAL_VERSION;
}
