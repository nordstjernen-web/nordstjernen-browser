/* Nordstjernen — WebAssembly JS API implemented over the vendored WAMR interpreter.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "wasm.h"

#include <glib.h>
#include <string.h>

#include <wasm_export.h>

#include "ns_wamr.h"

#define NS_WASM_STACK_SIZE (1u << 20)
#define NS_WASM_RECLAIM_INTERVAL 64

static JSClassID ns_wasm_module_class_id;
static JSClassID ns_wasm_instance_class_id;
static JSClassID ns_wasm_memory_class_id;
static JSClassID ns_wasm_table_class_id;
static JSClassID ns_wasm_func_class_id;

typedef struct {
    JSContext *ctx;
    JSValue fn;
    guint8 param_kinds[64];
    guint param_count;
    guint8 result_kind;
    gboolean has_result;
} ns_wasm_binding;

typedef struct {
    guint8 *bytes;
    size_t len;
    wasm_module_t module;
    gboolean linked;
    GPtrArray *bindings;
    GPtrArray *symbol_blocks;
    GPtrArray *strings;
} ns_wasm_module;

typedef struct {
    JSContext *ctx;
    wasm_module_inst_t inst;
    wasm_exec_env_t exec_env;
    JSValue module_obj;
    JSValue exports;
    JSValue memory_obj;
    JSValue pending_exc;
    gboolean has_pending;
    guint call_depth;
    guint calls_since_reclaim;
} ns_wasm_instance;

typedef struct {
    JSContext *ctx;
    JSValue instance;
    JSValue buffer;
    guint8 *base;
    size_t size;
} ns_wasm_memory;

typedef struct {
    JSContext *ctx;
    JSValue instance;
    char *name;
    wasm_valkind_t elem_kind;
} ns_wasm_table;

typedef struct {
    JSContext *ctx;
    JSValue instance;
    wasm_function_inst_t func;
} ns_wasm_func;

typedef struct {
    JSContext *ctx;
    JSValue v;
} ns_wasm_ref;

static GPrivate ns_wasm_thread_env_done = G_PRIVATE_INIT(NULL);

static void ns_wasm_on_memory_grown(wasm_module_inst_t inst, void *user_data);

static gpointer
ns_wasm_runtime_init_once(gpointer data)
{
    RuntimeInitArgs args;
    memset(&args, 0, sizeof(args));
    args.mem_alloc_type = Alloc_With_System_Allocator;
    if (!wasm_runtime_full_init(&args))
        return NULL;
    wasm_runtime_set_log_level(g_getenv("ND_WASM_LOG")
                                   ? WASM_LOG_LEVEL_VERBOSE
                                   : WASM_LOG_LEVEL_ERROR);
    wasm_runtime_set_enlarge_mem_success_callback(ns_wasm_on_memory_grown,
                                                  NULL);
    (void)data;
    return GINT_TO_POINTER(1);
}

static gboolean
ns_wasm_runtime_ready(void)
{
    static GOnce once = G_ONCE_INIT;
    g_once(&once, ns_wasm_runtime_init_once, NULL);
    if (!once.retval)
        return FALSE;
    if (!g_private_get(&ns_wasm_thread_env_done)) {
        wasm_runtime_init_thread_env();
        g_private_set(&ns_wasm_thread_env_done, GINT_TO_POINTER(1));
    }
    return TRUE;
}

static JSValue
ns_wasm_throw_named(JSContext *ctx, const char *class_name, const char *msg)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue ns = JS_GetPropertyStr(ctx, global, "WebAssembly");
    JSValue ctor = JS_IsObject(ns) ? JS_GetPropertyStr(ctx, ns, class_name)
                                   : JS_UNDEFINED;
    JS_FreeValue(ctx, ns);
    JS_FreeValue(ctx, global);
    if (JS_IsFunction(ctx, ctor)) {
        JSValue arg = JS_NewString(ctx, msg ? msg : "wasm error");
        JSValue err = JS_CallConstructor(ctx, ctor, 1, &arg);
        JS_FreeValue(ctx, arg);
        JS_FreeValue(ctx, ctor);
        if (!JS_IsException(err))
            return JS_Throw(ctx, err);
        JS_FreeValue(ctx, JS_GetException(ctx));
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, ctor);
    return JS_ThrowTypeError(ctx, "%s: %s", class_name,
                             msg ? msg : "wasm error");
}

static guint8 *
ns_wasm_copy_buffer_source(JSContext *ctx, JSValueConst v, size_t *out_len)
{
    *out_len = 0;
    size_t off = 0, blen = 0, bpe = 0;
    JSValue buf = JS_GetTypedArrayBuffer(ctx, v, &off, &blen, &bpe);
    if (!JS_IsException(buf)) {
        size_t total = 0;
        uint8_t *base = JS_GetArrayBuffer(ctx, &total, buf);
        guint8 *out = NULL;
        if (base && off + blen <= total) {
            out = g_memdup2(base + off, blen ? blen : 1);
            *out_len = blen;
        }
        JS_FreeValue(ctx, buf);
        return out;
    }
    JS_FreeValue(ctx, JS_GetException(ctx));
    size_t total = 0;
    uint8_t *base = JS_GetArrayBuffer(ctx, &total, v);
    if (base) {
        *out_len = total;
        return g_memdup2(base, total ? total : 1);
    }
    return NULL;
}

static ns_wasm_ref *
ns_wasm_ref_box(JSContext *ctx, JSValueConst v)
{
    ns_wasm_ref *r = g_new0(ns_wasm_ref, 1);
    r->ctx = ctx;
    r->v = JS_DupValue(ctx, v);
    return r;
}

static void
ns_wasm_ref_cleanup(void *p)
{
    ns_wasm_ref *r = p;
    JS_FreeValue(r->ctx, r->v);
    g_free(r);
}

static gboolean
ns_wasm_ref_register(wasm_module_inst_t inst, ns_wasm_ref *r, uint32_t *out_idx)
{
    uint32_t idx = 0;
    if (!wasm_externref_obj2ref(inst, r, &idx)) {
        ns_wasm_ref_cleanup(r);
        return FALSE;
    }
    wasm_externref_set_cleanup(inst, r, ns_wasm_ref_cleanup);
    if (out_idx)
        *out_idx = idx;
    return TRUE;
}

static JSValue
ns_wasm_ref_to_js(JSContext *ctx, void *obj)
{
    ns_wasm_ref *r = obj;
    if (!r)
        return JS_NULL;
    return JS_DupValue(ctx, r->v);
}

static ns_wasm_instance *
ns_wasm_instance_opaque(JSValueConst v)
{
    return JS_GetOpaque(v, ns_wasm_instance_class_id);
}

static int
ns_wasm_js_to_val(JSContext *ctx, ns_wasm_instance *wi, JSValueConst v,
                  wasm_valkind_t kind, wasm_val_t *out)
{
    memset(out, 0, sizeof(*out));
    out->kind = kind;
    switch (kind) {
    case WASM_I32: {
        int32_t i = 0;
        if (JS_ToInt32(ctx, &i, v))
            return -1;
        out->of.i32 = i;
        return 0;
    }
    case WASM_I64: {
        int64_t i = 0;
        if (JS_ToBigInt64(ctx, &i, v)) {
            JS_FreeValue(ctx, JS_GetException(ctx));
            if (JS_ToInt64(ctx, &i, v))
                return -1;
        }
        out->of.i64 = i;
        return 0;
    }
    case WASM_F32: {
        double d = 0;
        if (JS_ToFloat64(ctx, &d, v))
            return -1;
        out->of.f32 = (float)d;
        return 0;
    }
    case WASM_F64: {
        double d = 0;
        if (JS_ToFloat64(ctx, &d, v))
            return -1;
        out->of.f64 = d;
        return 0;
    }
    case WASM_EXTERNREF: {
        ns_wasm_ref *r = ns_wasm_ref_box(ctx, v);
        if (!ns_wasm_ref_register(wi->inst, r, NULL)) {
            JS_ThrowInternalError(ctx, "wasm externref registration failed");
            return -1;
        }
        out->of.foreign = (uintptr_t)r;
        return 0;
    }
    default:
        JS_ThrowTypeError(ctx, "unsupported wasm value kind %d", (int)kind);
        return -1;
    }
}

static JSValue
ns_wasm_val_to_js(JSContext *ctx, const wasm_val_t *val)
{
    switch (val->kind) {
    case WASM_I32:
        return JS_NewInt32(ctx, val->of.i32);
    case WASM_I64:
        return JS_NewBigInt64(ctx, val->of.i64);
    case WASM_F32:
        return JS_NewFloat64(ctx, val->of.f32);
    case WASM_F64:
        return JS_NewFloat64(ctx, val->of.f64);
    case WASM_EXTERNREF:
        return ns_wasm_ref_to_js(ctx, (void *)val->of.foreign);
    default:
        return JS_UNDEFINED;
    }
}

static JSValue
ns_wasm_call_function(JSContext *ctx, ns_wasm_instance *wi,
                      wasm_function_inst_t func, int argc, JSValueConst *argv)
{
    if (!wi || !wi->inst || !wi->exec_env)
        return JS_ThrowTypeError(ctx, "wasm instance is gone");

    guint n_params = wasm_func_get_param_count(func, wi->inst);
    guint n_results = wasm_func_get_result_count(func, wi->inst);
    wasm_valkind_t param_kinds[64], result_kinds[16];
    if (n_params > G_N_ELEMENTS(param_kinds) ||
        n_results > G_N_ELEMENTS(result_kinds))
        return JS_ThrowTypeError(ctx, "wasm function arity too large");
    wasm_func_get_param_types(func, wi->inst, param_kinds);
    wasm_func_get_result_types(func, wi->inst, result_kinds);

    wasm_val_t args[64], results[16];
    memset(results, 0, sizeof(results));
    for (guint i = 0; i < n_params; i++) {
        JSValueConst src = (int)i < argc ? argv[i] : JS_UNDEFINED;
        if (ns_wasm_js_to_val(ctx, wi, src, param_kinds[i], &args[i]))
            return JS_EXCEPTION;
    }

    if (wi->call_depth == 0) {
        wasm_runtime_clear_exception(wi->inst);
        if (wi->has_pending) {
            JS_FreeValue(ctx, wi->pending_exc);
            wi->pending_exc = JS_UNDEFINED;
            wi->has_pending = FALSE;
        }
    }

    wi->call_depth++;
    gboolean ok = wasm_runtime_call_wasm_a(wi->exec_env, func, n_results,
                                           results, n_params, args);
    wi->call_depth--;

    if (wi->call_depth == 0 &&
        ++wi->calls_since_reclaim >= NS_WASM_RECLAIM_INTERVAL) {
        wi->calls_since_reclaim = 0;
        ns_wamr_externref_reclaim(wi->inst);
    }

    if (!ok) {
        if (wi->call_depth > 0)
            return JS_EXCEPTION;
        if (wi->has_pending) {
            JSValue exc = wi->pending_exc;
            wi->pending_exc = JS_UNDEFINED;
            wi->has_pending = FALSE;
            wasm_runtime_clear_exception(wi->inst);
            return JS_Throw(ctx, exc);
        }
        const char *msg = wasm_runtime_get_exception(wi->inst);
        JSValue ret = ns_wasm_throw_named(ctx, "RuntimeError",
                                          msg ? msg : "wasm trap");
        wasm_runtime_clear_exception(wi->inst);
        return ret;
    }

    if (n_results == 0)
        return JS_UNDEFINED;
    if (n_results == 1)
        return ns_wasm_val_to_js(ctx, &results[0]);
    JSValue arr = JS_NewArray(ctx);
    for (guint i = 0; i < n_results; i++)
        JS_SetPropertyUint32(ctx, arr, i, ns_wasm_val_to_js(ctx, &results[i]));
    return arr;
}

static void
ns_wasm_func_finalizer(JSRuntime *rt, JSValue val)
{
    ns_wasm_func *f = JS_GetOpaque(val, ns_wasm_func_class_id);
    if (!f)
        return;
    JS_FreeValueRT(rt, f->instance);
    g_free(f);
}

static void
ns_wasm_func_gc_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func)
{
    ns_wasm_func *f = JS_GetOpaque(val, ns_wasm_func_class_id);
    if (f)
        JS_MarkValue(rt, f->instance, mark_func);
}

static JSValue
ns_wasm_func_call(JSContext *ctx, JSValueConst func_obj, JSValueConst this_val,
                  int argc, JSValueConst *argv, int flags)
{
    ns_wasm_func *f = JS_GetOpaque(func_obj, ns_wasm_func_class_id);
    if (!f)
        return JS_ThrowTypeError(ctx, "not a wasm function");
    (void)this_val;
    (void)flags;
    return ns_wasm_call_function(ctx, ns_wasm_instance_opaque(f->instance),
                                 f->func, argc, argv);
}

static const JSClassDef ns_wasm_func_class = {
    "WasmFunction",
    .finalizer = ns_wasm_func_finalizer,
    .gc_mark = ns_wasm_func_gc_mark,
    .call = ns_wasm_func_call,
};

static JSValue
ns_wasm_make_func(JSContext *ctx, JSValueConst instance,
                  wasm_function_inst_t func)
{
    JSValue obj = JS_NewObjectClass(ctx, ns_wasm_func_class_id);
    if (JS_IsException(obj))
        return obj;
    ns_wasm_func *f = g_new0(ns_wasm_func, 1);
    f->ctx = ctx;
    f->instance = JS_DupValue(ctx, instance);
    f->func = func;
    JS_SetOpaque(obj, f);
    return obj;
}

static void
ns_wasm_memory_finalizer(JSRuntime *rt, JSValue val)
{
    ns_wasm_memory *m = JS_GetOpaque(val, ns_wasm_memory_class_id);
    if (!m)
        return;
    JS_FreeValueRT(rt, m->instance);
    JS_FreeValueRT(rt, m->buffer);
    g_free(m);
}

static void
ns_wasm_memory_gc_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func)
{
    ns_wasm_memory *m = JS_GetOpaque(val, ns_wasm_memory_class_id);
    if (!m)
        return;
    JS_MarkValue(rt, m->instance, mark_func);
    JS_MarkValue(rt, m->buffer, mark_func);
}

static const JSClassDef ns_wasm_memory_class = {
    "Memory",
    .finalizer = ns_wasm_memory_finalizer,
    .gc_mark = ns_wasm_memory_gc_mark,
};

static JSValue
ns_wasm_memory_buffer_get(JSContext *ctx, JSValueConst this_val)
{
    ns_wasm_memory *m = JS_GetOpaque2(ctx, this_val, ns_wasm_memory_class_id);
    if (!m)
        return JS_EXCEPTION;
    ns_wasm_instance *wi = ns_wasm_instance_opaque(m->instance);
    if (!wi || !wi->inst)
        return JS_ThrowTypeError(ctx, "wasm instance is gone");
    wasm_memory_inst_t mem = wasm_runtime_get_memory(wi->inst, 0);
    if (!mem)
        return JS_ThrowTypeError(ctx, "wasm memory is gone");
    guint8 *base = wasm_memory_get_base_address(mem);
    size_t size = (size_t)wasm_memory_get_cur_page_count(mem) *
                  wasm_memory_get_bytes_per_page(mem);
    if (!JS_IsUndefined(m->buffer) && m->base == base && m->size == size)
        return JS_DupValue(ctx, m->buffer);
    if (!JS_IsUndefined(m->buffer)) {
        JS_DetachArrayBuffer(ctx, m->buffer);
        JS_FreeValue(ctx, m->buffer);
    }
    m->buffer = JS_NewArrayBuffer(ctx, base, size, NULL, NULL, FALSE);
    m->base = base;
    m->size = size;
    return JS_DupValue(ctx, m->buffer);
}

static JSValue
ns_wasm_memory_grow(JSContext *ctx, JSValueConst this_val, int argc,
                    JSValueConst *argv)
{
    ns_wasm_memory *m = JS_GetOpaque2(ctx, this_val, ns_wasm_memory_class_id);
    if (!m)
        return JS_EXCEPTION;
    ns_wasm_instance *wi = ns_wasm_instance_opaque(m->instance);
    if (!wi || !wi->inst)
        return JS_ThrowTypeError(ctx, "wasm instance is gone");
    uint32_t delta = 0;
    if (argc > 0 && JS_ToUint32(ctx, &delta, argv[0]))
        return JS_EXCEPTION;
    wasm_memory_inst_t mem = wasm_runtime_get_memory(wi->inst, 0);
    if (!mem)
        return JS_ThrowTypeError(ctx, "wasm memory is gone");
    uint32_t old_pages = (uint32_t)wasm_memory_get_cur_page_count(mem);
    if (delta && !wasm_runtime_enlarge_memory(wi->inst, delta))
        return JS_ThrowRangeError(ctx, "wasm memory.grow failed");
    return JS_NewUint32(ctx, old_pages);
}

static const JSCFunctionListEntry ns_wasm_memory_proto_funcs[] = {
    JS_CGETSET_DEF("buffer", ns_wasm_memory_buffer_get, NULL),
    JS_CFUNC_DEF("grow", 1, ns_wasm_memory_grow),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WebAssembly.Memory",
                       JS_PROP_CONFIGURABLE),
};

static void
ns_wasm_on_memory_grown(wasm_module_inst_t inst, void *user_data)
{
    (void)user_data;
    ns_wasm_instance *wi = wasm_runtime_get_custom_data(inst);
    if (!wi || JS_IsUndefined(wi->memory_obj))
        return;
    ns_wasm_memory *m = JS_GetOpaque(wi->memory_obj, ns_wasm_memory_class_id);
    if (!m || JS_IsUndefined(m->buffer))
        return;
    JS_DetachArrayBuffer(m->ctx, m->buffer);
    JS_FreeValue(m->ctx, m->buffer);
    m->buffer = JS_UNDEFINED;
    m->base = NULL;
    m->size = 0;
}

static void
ns_wasm_table_finalizer(JSRuntime *rt, JSValue val)
{
    ns_wasm_table *t = JS_GetOpaque(val, ns_wasm_table_class_id);
    if (!t)
        return;
    JS_FreeValueRT(rt, t->instance);
    g_free(t->name);
    g_free(t);
}

static void
ns_wasm_table_gc_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func)
{
    ns_wasm_table *t = JS_GetOpaque(val, ns_wasm_table_class_id);
    if (t)
        JS_MarkValue(rt, t->instance, mark_func);
}

static const JSClassDef ns_wasm_table_class = {
    "Table",
    .finalizer = ns_wasm_table_finalizer,
    .gc_mark = ns_wasm_table_gc_mark,
};

static ns_wasm_table *
ns_wasm_table_opaque(JSContext *ctx, JSValueConst this_val,
                     ns_wasm_instance **out_wi)
{
    ns_wasm_table *t = JS_GetOpaque2(ctx, this_val, ns_wasm_table_class_id);
    if (!t)
        return NULL;
    ns_wasm_instance *wi = ns_wasm_instance_opaque(t->instance);
    if (!wi || !wi->inst) {
        JS_ThrowTypeError(ctx, "wasm instance is gone");
        return NULL;
    }
    *out_wi = wi;
    return t;
}

static JSValue
ns_wasm_table_length_get(JSContext *ctx, JSValueConst this_val)
{
    ns_wasm_instance *wi = NULL;
    ns_wasm_table *t = ns_wasm_table_opaque(ctx, this_val, &wi);
    if (!t)
        return JS_EXCEPTION;
    uint32_t size = 0;
    if (!ns_wamr_table_size(wi->inst, t->name, &size, NULL))
        return JS_ThrowTypeError(ctx, "wasm table is gone");
    return JS_NewUint32(ctx, size);
}

static JSValue
ns_wasm_table_get(JSContext *ctx, JSValueConst this_val, int argc,
                  JSValueConst *argv)
{
    ns_wasm_instance *wi = NULL;
    ns_wasm_table *t = ns_wasm_table_opaque(ctx, this_val, &wi);
    if (!t)
        return JS_EXCEPTION;
    uint32_t idx = 0;
    if (argc > 0 && JS_ToUint32(ctx, &idx, argv[0]))
        return JS_EXCEPTION;
    if (t->elem_kind == WASM_FUNCREF) {
        wasm_table_inst_t tbl;
        if (!wasm_runtime_get_export_table_inst(wi->inst, t->name, &tbl))
            return JS_ThrowTypeError(ctx, "wasm table is gone");
        if (idx >= tbl.cur_size)
            return JS_ThrowRangeError(ctx, "table index out of bounds");
        wasm_function_inst_t func =
            wasm_table_get_func_inst(wi->inst, &tbl, idx);
        if (!func)
            return JS_NULL;
        return ns_wasm_make_func(ctx, t->instance, func);
    }
    uint32_t ref = NS_WAMR_NULL_REF;
    if (!ns_wamr_table_get_ref(wi->inst, t->name, idx, &ref))
        return JS_ThrowRangeError(ctx, "table index out of bounds");
    if (ref == NS_WAMR_NULL_REF)
        return JS_NULL;
    void *obj = NULL;
    if (!wasm_externref_ref2obj(ref, &obj))
        return JS_NULL;
    return ns_wasm_ref_to_js(ctx, obj);
}

static JSValue
ns_wasm_table_set(JSContext *ctx, JSValueConst this_val, int argc,
                  JSValueConst *argv)
{
    ns_wasm_instance *wi = NULL;
    ns_wasm_table *t = ns_wasm_table_opaque(ctx, this_val, &wi);
    if (!t)
        return JS_EXCEPTION;
    uint32_t idx = 0;
    if (argc > 0 && JS_ToUint32(ctx, &idx, argv[0]))
        return JS_EXCEPTION;
    JSValueConst v = argc > 1 ? argv[1] : JS_UNDEFINED;
    if (t->elem_kind == WASM_FUNCREF)
        return JS_ThrowTypeError(ctx,
                                 "setting funcref table entries from JS "
                                 "is not supported");
    uint32_t ref = NS_WAMR_NULL_REF;
    if (!JS_IsNull(v)) {
        ns_wasm_ref *r = ns_wasm_ref_box(ctx, v);
        if (!ns_wasm_ref_register(wi->inst, r, &ref))
            return JS_ThrowInternalError(ctx,
                                         "wasm externref registration failed");
    }
    if (!ns_wamr_table_set_ref(wi->inst, t->name, idx, ref))
        return JS_ThrowRangeError(ctx, "table index out of bounds");
    return JS_UNDEFINED;
}

static JSValue
ns_wasm_table_grow(JSContext *ctx, JSValueConst this_val, int argc,
                   JSValueConst *argv)
{
    ns_wasm_instance *wi = NULL;
    ns_wasm_table *t = ns_wasm_table_opaque(ctx, this_val, &wi);
    if (!t)
        return JS_EXCEPTION;
    uint32_t delta = 0;
    if (argc > 0 && JS_ToUint32(ctx, &delta, argv[0]))
        return JS_EXCEPTION;
    uint32_t old_size = 0;
    if (!ns_wamr_table_grow(wi->inst, t->name, delta, &old_size))
        return JS_ThrowRangeError(ctx, "wasm table.grow failed");
    if (argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]) &&
        t->elem_kind == WASM_EXTERNREF) {
        for (uint32_t i = 0; i < delta; i++) {
            ns_wasm_ref *r = ns_wasm_ref_box(ctx, argv[1]);
            uint32_t ref = NS_WAMR_NULL_REF;
            if (!ns_wasm_ref_register(wi->inst, r, &ref))
                break;
            ns_wamr_table_set_ref(wi->inst, t->name, old_size + i, ref);
        }
    }
    return JS_NewUint32(ctx, old_size);
}

static const JSCFunctionListEntry ns_wasm_table_proto_funcs[] = {
    JS_CGETSET_DEF("length", ns_wasm_table_length_get, NULL),
    JS_CFUNC_DEF("get", 1, ns_wasm_table_get),
    JS_CFUNC_DEF("set", 2, ns_wasm_table_set),
    JS_CFUNC_DEF("grow", 1, ns_wasm_table_grow),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "WebAssembly.Table",
                       JS_PROP_CONFIGURABLE),
};

static void
ns_wasm_module_finalizer(JSRuntime *rt, JSValue val)
{
    ns_wasm_module *m = JS_GetOpaque(val, ns_wasm_module_class_id);
    if (!m)
        return;
    if (m->module)
        wasm_runtime_unload(m->module);
    if (m->bindings) {
        for (guint i = 0; i < m->bindings->len; i++) {
            ns_wasm_binding *b = g_ptr_array_index(m->bindings, i);
            JS_FreeValueRT(rt, b->fn);
            g_free(b);
        }
        g_ptr_array_free(m->bindings, TRUE);
    }
    if (m->symbol_blocks)
        g_ptr_array_free(m->symbol_blocks, TRUE);
    if (m->strings)
        g_ptr_array_free(m->strings, TRUE);
    g_free(m->bytes);
    g_free(m);
}

static void
ns_wasm_module_gc_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func)
{
    ns_wasm_module *m = JS_GetOpaque(val, ns_wasm_module_class_id);
    if (!m || !m->bindings)
        return;
    for (guint i = 0; i < m->bindings->len; i++) {
        ns_wasm_binding *b = g_ptr_array_index(m->bindings, i);
        JS_MarkValue(rt, b->fn, mark_func);
    }
}

static const JSClassDef ns_wasm_module_class = {
    "Module",
    .finalizer = ns_wasm_module_finalizer,
    .gc_mark = ns_wasm_module_gc_mark,
};

static void
ns_wasm_instance_finalizer(JSRuntime *rt, JSValue val)
{
    ns_wasm_instance *wi = JS_GetOpaque(val, ns_wasm_instance_class_id);
    if (!wi)
        return;
    if (wi->exec_env)
        wasm_runtime_destroy_exec_env(wi->exec_env);
    if (wi->inst) {
        wasm_runtime_set_custom_data(wi->inst, NULL);
        wasm_runtime_deinstantiate(wi->inst);
    }
    JS_FreeValueRT(rt, wi->module_obj);
    JS_FreeValueRT(rt, wi->exports);
    JS_FreeValueRT(rt, wi->memory_obj);
    JS_FreeValueRT(rt, wi->pending_exc);
    g_free(wi);
}

static void
ns_wasm_instance_gc_mark(JSRuntime *rt, JSValueConst val,
                         JS_MarkFunc *mark_func)
{
    ns_wasm_instance *wi = JS_GetOpaque(val, ns_wasm_instance_class_id);
    if (!wi)
        return;
    JS_MarkValue(rt, wi->module_obj, mark_func);
    JS_MarkValue(rt, wi->exports, mark_func);
    JS_MarkValue(rt, wi->memory_obj, mark_func);
    JS_MarkValue(rt, wi->pending_exc, mark_func);
}

static const JSClassDef ns_wasm_instance_class = {
    "Instance",
    .finalizer = ns_wasm_instance_finalizer,
    .gc_mark = ns_wasm_instance_gc_mark,
};

static void
ns_wasm_native_dispatch(wasm_exec_env_t exec_env, uint64_t *args)
{
    ns_wasm_binding *b = wasm_runtime_get_function_attachment(exec_env);
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
    ns_wasm_instance *wi = inst ? wasm_runtime_get_custom_data(inst) : NULL;
    JSContext *ctx = b->ctx;

    JSValue argv[64];
    guint argc = b->param_count;
    for (guint i = 0; i < argc; i++) {
        guint64 *slot = &args[i];
        switch (b->param_kinds[i]) {
        case WASM_I32:
            argv[i] = JS_NewInt32(ctx, *(int32_t *)slot);
            break;
        case WASM_I64:
            argv[i] = JS_NewBigInt64(ctx, *(int64_t *)slot);
            break;
        case WASM_F32:
            argv[i] = JS_NewFloat64(ctx, *(float *)slot);
            break;
        case WASM_F64:
            argv[i] = JS_NewFloat64(ctx, *(double *)slot);
            break;
        case WASM_EXTERNREF:
            argv[i] = ns_wasm_ref_to_js(ctx, *(void **)slot);
            break;
        default:
            argv[i] = JS_UNDEFINED;
            break;
        }
    }

    JSValue ret = JS_Call(ctx, b->fn, JS_UNDEFINED, (int)argc, argv);
    for (guint i = 0; i < argc; i++)
        JS_FreeValue(ctx, argv[i]);

    if (JS_IsException(ret)) {
        if (wi && !wi->has_pending) {
            wi->pending_exc = JS_GetException(ctx);
            wi->has_pending = TRUE;
        } else {
            JS_FreeValue(ctx, JS_GetException(ctx));
        }
        if (inst)
            wasm_runtime_set_exception(inst, "uncaught JavaScript exception");
        return;
    }

    if (b->has_result) {
        switch (b->result_kind) {
        case WASM_I32: {
            int32_t i = 0;
            JS_ToInt32(ctx, &i, ret);
            *(int32_t *)args = i;
            break;
        }
        case WASM_I64: {
            int64_t i = 0;
            if (JS_ToBigInt64(ctx, &i, ret)) {
                JS_FreeValue(ctx, JS_GetException(ctx));
                JS_ToInt64(ctx, &i, ret);
            }
            *(int64_t *)args = i;
            break;
        }
        case WASM_F32:
        case WASM_F64: {
            double d = 0;
            JS_ToFloat64(ctx, &d, ret);
            if (b->result_kind == WASM_F32)
                *(float *)args = (float)d;
            else
                *(double *)args = d;
            break;
        }
        case WASM_EXTERNREF: {
            ns_wasm_ref *r = ns_wasm_ref_box(ctx, ret);
            if (ns_wasm_ref_register(inst, r, NULL))
                *(void **)args = r;
            else
                *(void **)args = (void *)(uintptr_t)-1;
            break;
        }
        default:
            break;
        }
    }
    JS_FreeValue(ctx, ret);
}

static char
ns_wasm_signature_char(wasm_valkind_t kind)
{
    switch (kind) {
    case WASM_I32:
        return 'i';
    case WASM_I64:
        return 'I';
    case WASM_F32:
        return 'f';
    case WASM_F64:
        return 'F';
    case WASM_EXTERNREF:
        return 'r';
    case WASM_FUNCREF:
        return 'i';
    default:
        return 'i';
    }
}

static gboolean
ns_wasm_link_imports(JSContext *ctx, ns_wasm_module *mod,
                     JSValueConst import_object)
{
    int32_t n = wasm_runtime_get_import_count(mod->module);
    GHashTable *groups = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                               (GDestroyNotify)g_array_unref);

    for (int32_t i = 0; i < n; i++) {
        wasm_import_t imp;
        wasm_runtime_get_import_type(mod->module, i, &imp);
        if (imp.kind != WASM_IMPORT_EXPORT_KIND_FUNC) {
            ns_wasm_throw_named(ctx, "LinkError",
                                "only function imports are supported");
            g_hash_table_destroy(groups);
            return FALSE;
        }
        if (!JS_IsObject(import_object)) {
            ns_wasm_throw_named(ctx, "LinkError", "import object required");
            g_hash_table_destroy(groups);
            return FALSE;
        }
        JSValue ns = JS_GetPropertyStr(ctx, import_object, imp.module_name);
        JSValue fn = JS_IsObject(ns) ? JS_GetPropertyStr(ctx, ns, imp.name)
                                     : JS_UNDEFINED;
        JS_FreeValue(ctx, ns);
        if (!JS_IsFunction(ctx, fn)) {
            JS_FreeValue(ctx, fn);
            char *msg = g_strdup_printf("import %s.%s is not a function",
                                        imp.module_name, imp.name);
            ns_wasm_throw_named(ctx, "LinkError", msg);
            g_free(msg);
            g_hash_table_destroy(groups);
            return FALSE;
        }

        ns_wasm_binding *b = g_new0(ns_wasm_binding, 1);
        b->ctx = ctx;
        b->fn = fn;
        b->param_count = wasm_func_type_get_param_count(imp.u.func_type);
        if (b->param_count > G_N_ELEMENTS(b->param_kinds))
            b->param_count = G_N_ELEMENTS(b->param_kinds);
        for (guint p = 0; p < b->param_count; p++)
            b->param_kinds[p] =
                wasm_func_type_get_param_valkind(imp.u.func_type, p);
        guint n_results = wasm_func_type_get_result_count(imp.u.func_type);
        b->has_result = n_results > 0;
        if (b->has_result)
            b->result_kind =
                wasm_func_type_get_result_valkind(imp.u.func_type, 0);
        g_ptr_array_add(mod->bindings, b);

        GString *sig = g_string_new("(");
        for (guint p = 0; p < b->param_count; p++)
            g_string_append_c(sig, ns_wasm_signature_char(b->param_kinds[p]));
        g_string_append_c(sig, ')');
        if (b->has_result)
            g_string_append_c(sig, ns_wasm_signature_char(b->result_kind));
        char *sig_str = g_string_free(sig, FALSE);
        g_ptr_array_add(mod->strings, sig_str);
        char *name_str = g_strdup(imp.name);
        g_ptr_array_add(mod->strings, name_str);

        GArray *group = g_hash_table_lookup(groups, imp.module_name);
        if (!group) {
            group = g_array_new(FALSE, TRUE, sizeof(NativeSymbol));
            g_hash_table_insert(groups, g_strdup(imp.module_name), group);
        }
        NativeSymbol sym = {
            .symbol = name_str,
            .func_ptr = (void *)ns_wasm_native_dispatch,
            .signature = sig_str,
            .attachment = b,
        };
        g_array_append_val(group, sym);
    }

    GHashTableIter it;
    gpointer key, value;
    GPtrArray *reg_names = g_ptr_array_new();
    GPtrArray *reg_blocks = g_ptr_array_new();
    g_hash_table_iter_init(&it, groups);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        GArray *group = value;
        NativeSymbol *syms =
            g_memdup2(group->data, group->len * sizeof(NativeSymbol));
        g_ptr_array_add(mod->symbol_blocks, syms);
        char *reg_name = g_strdup(key);
        g_ptr_array_add(mod->strings, reg_name);
        wasm_runtime_register_natives_raw(reg_name, syms, group->len);
        g_ptr_array_add(reg_names, reg_name);
        g_ptr_array_add(reg_blocks, syms);
    }

    gboolean ok = wasm_runtime_resolve_symbols(mod->module);

    for (guint i = 0; i < reg_names->len; i++)
        wasm_runtime_unregister_natives(g_ptr_array_index(reg_names, i),
                                        g_ptr_array_index(reg_blocks, i));
    g_ptr_array_free(reg_names, TRUE);
    g_ptr_array_free(reg_blocks, TRUE);
    g_hash_table_destroy(groups);

    if (!ok) {
        GString *msg = g_string_new("unresolved wasm imports:");
        for (int32_t i = 0; i < n; i++) {
            wasm_import_t imp;
            wasm_runtime_get_import_type(mod->module, i, &imp);
            if (imp.kind == WASM_IMPORT_EXPORT_KIND_FUNC && !imp.linked)
                g_string_append_printf(msg, " %s.%s", imp.module_name,
                                       imp.name);
        }
        ns_wasm_throw_named(ctx, "LinkError", msg->str);
        g_string_free(msg, TRUE);
        return FALSE;
    }
    mod->linked = TRUE;
    return TRUE;
}

static JSValue
ns_wasm_build_exports(JSContext *ctx, JSValueConst instance_obj,
                      ns_wasm_instance *wi, ns_wasm_module *mod)
{
    JSValue exports = JS_NewObject(ctx);
    int32_t n = wasm_runtime_get_export_count(mod->module);
    for (int32_t i = 0; i < n; i++) {
        wasm_export_t exp;
        wasm_runtime_get_export_type(mod->module, i, &exp);
        switch (exp.kind) {
        case WASM_IMPORT_EXPORT_KIND_FUNC: {
            wasm_function_inst_t func =
                wasm_runtime_lookup_function(wi->inst, exp.name);
            if (func)
                JS_SetPropertyStr(ctx, exports, exp.name,
                                  ns_wasm_make_func(ctx, instance_obj, func));
            break;
        }
        case WASM_IMPORT_EXPORT_KIND_MEMORY: {
            JSValue mem_obj =
                JS_NewObjectClass(ctx, ns_wasm_memory_class_id);
            ns_wasm_memory *m = g_new0(ns_wasm_memory, 1);
            m->ctx = ctx;
            m->instance = JS_DupValue(ctx, instance_obj);
            m->buffer = JS_UNDEFINED;
            JS_SetOpaque(mem_obj, m);
            if (JS_IsUndefined(wi->memory_obj))
                wi->memory_obj = JS_DupValue(ctx, mem_obj);
            JS_SetPropertyStr(ctx, exports, exp.name, mem_obj);
            break;
        }
        case WASM_IMPORT_EXPORT_KIND_TABLE: {
            JSValue tbl_obj = JS_NewObjectClass(ctx, ns_wasm_table_class_id);
            ns_wasm_table *t = g_new0(ns_wasm_table, 1);
            t->ctx = ctx;
            t->instance = JS_DupValue(ctx, instance_obj);
            t->name = g_strdup(exp.name);
            t->elem_kind = wasm_table_type_get_elem_kind(exp.u.table_type);
            JS_SetOpaque(tbl_obj, t);
            JS_SetPropertyStr(ctx, exports, exp.name, tbl_obj);
            break;
        }
        default:
            break;
        }
    }
    return exports;
}

static JSValue
ns_wasm_module_from_bytes(JSContext *ctx, const guint8 *bytes, size_t len)
{
    if (!ns_wasm_runtime_ready())
        return JS_ThrowInternalError(ctx, "wasm runtime init failed");

    ns_wasm_module *mod = g_new0(ns_wasm_module, 1);
    mod->bytes = g_memdup2(bytes, len ? len : 1);
    mod->len = len;
    mod->bindings = g_ptr_array_new();
    mod->symbol_blocks = g_ptr_array_new_with_free_func(g_free);
    mod->strings = g_ptr_array_new_with_free_func(g_free);

    char error_buf[192] = "";
    LoadArgs load_args;
    memset(&load_args, 0, sizeof(load_args));
    load_args.name = (char *)"";
    load_args.no_resolve = true;
    mod->module = wasm_runtime_load_ex(mod->bytes, (uint32_t)len, &load_args,
                                       error_buf, sizeof(error_buf));
    JSValue obj = mod->module
                      ? JS_NewObjectClass(ctx, ns_wasm_module_class_id)
                      : JS_EXCEPTION;
    if (JS_IsException(obj)) {
        if (mod->module)
            wasm_runtime_unload(mod->module);
        g_ptr_array_free(mod->bindings, TRUE);
        g_ptr_array_free(mod->symbol_blocks, TRUE);
        g_ptr_array_free(mod->strings, TRUE);
        g_free(mod->bytes);
        g_free(mod);
        if (!JS_HasException(ctx))
            return ns_wasm_throw_named(ctx, "CompileError", error_buf);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(obj, mod);
    return obj;
}

static JSValue
ns_wasm_instance_create(JSContext *ctx, JSValueConst module_obj,
                        JSValueConst import_object)
{
    ns_wasm_module *mod =
        JS_GetOpaque2(ctx, module_obj, ns_wasm_module_class_id);
    if (!mod || !mod->module)
        return JS_EXCEPTION;

    if (!mod->linked && !ns_wasm_link_imports(ctx, mod, import_object))
        return JS_EXCEPTION;

    char error_buf[192] = "";
    wasm_module_inst_t inst =
        wasm_runtime_instantiate(mod->module, NS_WASM_STACK_SIZE, 0, error_buf,
                                 sizeof(error_buf));
    if (!inst)
        return ns_wasm_throw_named(ctx, "LinkError", error_buf);

    wasm_exec_env_t exec_env =
        wasm_runtime_create_exec_env(inst, NS_WASM_STACK_SIZE);
    if (!exec_env) {
        wasm_runtime_deinstantiate(inst);
        return JS_ThrowInternalError(ctx, "wasm exec env creation failed");
    }

    JSValue obj = JS_NewObjectClass(ctx, ns_wasm_instance_class_id);
    if (JS_IsException(obj)) {
        wasm_runtime_destroy_exec_env(exec_env);
        wasm_runtime_deinstantiate(inst);
        return obj;
    }

    ns_wasm_instance *wi = g_new0(ns_wasm_instance, 1);
    wi->ctx = ctx;
    wi->inst = inst;
    wi->exec_env = exec_env;
    wi->module_obj = JS_DupValue(ctx, module_obj);
    wi->exports = JS_UNDEFINED;
    wi->memory_obj = JS_UNDEFINED;
    wi->pending_exc = JS_UNDEFINED;
    JS_SetOpaque(obj, wi);
    wasm_runtime_set_custom_data(inst, wi);

    wi->exports = ns_wasm_build_exports(ctx, obj, wi, mod);
    JS_DefinePropertyValueStr(ctx, obj, "exports",
                              JS_DupValue(ctx, wi->exports),
                              JS_PROP_ENUMERABLE);
    return obj;
}

static JSValue
ns_wasm_module_ctor(JSContext *ctx, JSValueConst new_target, int argc,
                    JSValueConst *argv)
{
    (void)new_target;
    size_t len = 0;
    guint8 *bytes =
        argc > 0 ? ns_wasm_copy_buffer_source(ctx, argv[0], &len) : NULL;
    if (!bytes)
        return JS_ThrowTypeError(ctx, "WebAssembly.Module requires a "
                                      "BufferSource");
    JSValue obj = ns_wasm_module_from_bytes(ctx, bytes, len);
    g_free(bytes);
    return obj;
}

static JSValue
ns_wasm_instance_ctor(JSContext *ctx, JSValueConst new_target, int argc,
                      JSValueConst *argv)
{
    (void)new_target;
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "WebAssembly.Instance requires a "
                                      "Module");
    return ns_wasm_instance_create(ctx, argv[0],
                                   argc > 1 ? argv[1] : JS_UNDEFINED);
}

static JSValue
ns_wasm_unsupported_ctor(JSContext *ctx, JSValueConst new_target, int argc,
                         JSValueConst *argv)
{
    (void)new_target;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "standalone WebAssembly.Memory/Table/Global "
                                  "construction is not supported");
}

static JSValue
ns_wasm_resolved_promise(JSContext *ctx, JSValue value)
{
    JSValue resolvers[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolvers);
    if (JS_IsException(promise)) {
        JS_FreeValue(ctx, value);
        return promise;
    }
    if (JS_IsException(value)) {
        JSValue exc = JS_GetException(ctx);
        JSValue r = JS_Call(ctx, resolvers[1], JS_UNDEFINED, 1,
                            (JSValueConst *)&exc);
        JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, exc);
    } else {
        JSValue r = JS_Call(ctx, resolvers[0], JS_UNDEFINED, 1,
                            (JSValueConst *)&value);
        JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, value);
    }
    JS_FreeValue(ctx, resolvers[0]);
    JS_FreeValue(ctx, resolvers[1]);
    return promise;
}

static JSValue
ns_wasm_compile(JSContext *ctx, JSValueConst this_val, int argc,
                JSValueConst *argv)
{
    (void)this_val;
    size_t len = 0;
    guint8 *bytes =
        argc > 0 ? ns_wasm_copy_buffer_source(ctx, argv[0], &len) : NULL;
    if (!bytes)
        return ns_wasm_resolved_promise(
            ctx, JS_ThrowTypeError(ctx, "WebAssembly.compile requires a "
                                        "BufferSource"));
    JSValue module = ns_wasm_module_from_bytes(ctx, bytes, len);
    g_free(bytes);
    return ns_wasm_resolved_promise(ctx, module);
}

static JSValue
ns_wasm_instantiate(JSContext *ctx, JSValueConst this_val, int argc,
                    JSValueConst *argv)
{
    (void)this_val;
    JSValueConst source = argc > 0 ? argv[0] : JS_UNDEFINED;
    JSValueConst imports = argc > 1 ? argv[1] : JS_UNDEFINED;

    if (JS_GetOpaque(source, ns_wasm_module_class_id))
        return ns_wasm_resolved_promise(
            ctx, ns_wasm_instance_create(ctx, source, imports));

    size_t len = 0;
    guint8 *bytes = ns_wasm_copy_buffer_source(ctx, source, &len);
    if (!bytes)
        return ns_wasm_resolved_promise(
            ctx, JS_ThrowTypeError(ctx, "WebAssembly.instantiate requires a "
                                        "BufferSource or Module"));
    JSValue module = ns_wasm_module_from_bytes(ctx, bytes, len);
    g_free(bytes);
    if (JS_IsException(module))
        return ns_wasm_resolved_promise(ctx, module);

    JSValue instance = ns_wasm_instance_create(ctx, module, imports);
    if (JS_IsException(instance)) {
        JS_FreeValue(ctx, module);
        return ns_wasm_resolved_promise(ctx, instance);
    }
    JSValue pair = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, pair, "module", module);
    JS_SetPropertyStr(ctx, pair, "instance", instance);
    return ns_wasm_resolved_promise(ctx, pair);
}

static JSValue
ns_wasm_validate(JSContext *ctx, JSValueConst this_val, int argc,
                 JSValueConst *argv)
{
    (void)this_val;
    if (!ns_wasm_runtime_ready())
        return JS_NewBool(ctx, FALSE);
    size_t len = 0;
    guint8 *bytes =
        argc > 0 ? ns_wasm_copy_buffer_source(ctx, argv[0], &len) : NULL;
    if (!bytes)
        return JS_ThrowTypeError(ctx, "WebAssembly.validate requires a "
                                      "BufferSource");
    char error_buf[128];
    LoadArgs load_args;
    memset(&load_args, 0, sizeof(load_args));
    load_args.name = (char *)"";
    load_args.no_resolve = true;
    wasm_module_t module = wasm_runtime_load_ex(bytes, (uint32_t)len,
                                                &load_args, error_buf,
                                                sizeof(error_buf));
    gboolean ok = module != NULL;
    if (module)
        wasm_runtime_unload(module);
    g_free(bytes);
    return JS_NewBool(ctx, ok);
}

static const char ns_wasm_bootstrap_js[] =
    "(() => {"
    "  const W = WebAssembly;"
    "  W.CompileError = class CompileError extends Error {};"
    "  W.CompileError.prototype.name = 'CompileError';"
    "  W.LinkError = class LinkError extends Error {};"
    "  W.LinkError.prototype.name = 'LinkError';"
    "  W.RuntimeError = class RuntimeError extends Error {};"
    "  W.RuntimeError.prototype.name = 'RuntimeError';"
    "  W.instantiateStreaming = async (src, imports) =>"
    "    W.instantiate(await (await src).arrayBuffer(), imports);"
    "  W.compileStreaming = async (src) =>"
    "    W.compile(await (await src).arrayBuffer());"
    "})();";

static void
ns_wasm_register_class(JSRuntime *rt, JSClassID *class_id,
                       const JSClassDef *def)
{
    if (!*class_id)
        JS_NewClassID(rt, class_id);
    if (!JS_IsRegisteredClass(rt, *class_id))
        JS_NewClass(rt, *class_id, def);
}

void
ns_wasm_install(JSContext *ctx, JSValueConst global)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    ns_wasm_register_class(rt, &ns_wasm_module_class_id, &ns_wasm_module_class);
    ns_wasm_register_class(rt, &ns_wasm_instance_class_id,
                           &ns_wasm_instance_class);
    ns_wasm_register_class(rt, &ns_wasm_memory_class_id, &ns_wasm_memory_class);
    ns_wasm_register_class(rt, &ns_wasm_table_class_id, &ns_wasm_table_class);
    ns_wasm_register_class(rt, &ns_wasm_func_class_id, &ns_wasm_func_class);

    JSValue memory_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, memory_proto, ns_wasm_memory_proto_funcs,
                               G_N_ELEMENTS(ns_wasm_memory_proto_funcs));
    JS_SetClassProto(ctx, ns_wasm_memory_class_id, memory_proto);

    JSValue table_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, table_proto, ns_wasm_table_proto_funcs,
                               G_N_ELEMENTS(ns_wasm_table_proto_funcs));
    JS_SetClassProto(ctx, ns_wasm_table_class_id, table_proto);

    JSValue module_proto = JS_NewObject(ctx);
    JS_SetClassProto(ctx, ns_wasm_module_class_id,
                     JS_DupValue(ctx, module_proto));
    JSValue instance_proto = JS_NewObject(ctx);
    JS_SetClassProto(ctx, ns_wasm_instance_class_id,
                     JS_DupValue(ctx, instance_proto));

    JSValue function_ctor = JS_GetPropertyStr(ctx, global, "Function");
    JSValue function_proto = JS_GetPropertyStr(ctx, function_ctor,
                                               "prototype");
    JS_FreeValue(ctx, function_ctor);
    JS_SetClassProto(ctx, ns_wasm_func_class_id, function_proto);

    JSValue ns = JS_NewObject(ctx);
    JSValue module_ctor = JS_NewCFunction2(ctx, ns_wasm_module_ctor, "Module",
                                           1, JS_CFUNC_constructor, 0);
    JSValue instance_ctor = JS_NewCFunction2(ctx, ns_wasm_instance_ctor,
                                             "Instance", 2,
                                             JS_CFUNC_constructor, 0);
    JSValue memory_ctor = JS_NewCFunction2(ctx, ns_wasm_unsupported_ctor,
                                           "Memory", 1, JS_CFUNC_constructor,
                                           0);
    JSValue table_ctor = JS_NewCFunction2(ctx, ns_wasm_unsupported_ctor,
                                          "Table", 1, JS_CFUNC_constructor, 0);
    JSValue global_ctor = JS_NewCFunction2(ctx, ns_wasm_unsupported_ctor,
                                           "Global", 1, JS_CFUNC_constructor,
                                           0);
    JS_SetPropertyStr(ctx, memory_ctor, "prototype",
                      JS_DupValue(ctx, memory_proto));
    JS_SetPropertyStr(ctx, table_ctor, "prototype",
                      JS_DupValue(ctx, table_proto));
    JS_SetPropertyStr(ctx, module_ctor, "prototype", module_proto);
    JS_SetPropertyStr(ctx, instance_ctor, "prototype", instance_proto);
    JS_SetPropertyStr(ctx, ns, "Module", module_ctor);
    JS_SetPropertyStr(ctx, ns, "Instance", instance_ctor);
    JS_SetPropertyStr(ctx, ns, "Memory", memory_ctor);
    JS_SetPropertyStr(ctx, ns, "Table", table_ctor);
    JS_SetPropertyStr(ctx, ns, "Global", global_ctor);
    JSValue compile_fn = JS_NewCFunction(ctx, ns_wasm_compile, "compile", 1);
    JS_SetPropertyStr(ctx, ns, "compile", compile_fn);
    JSValue inst_fn = JS_NewCFunction(ctx, ns_wasm_instantiate, "instantiate",
                                      2);
    JS_SetPropertyStr(ctx, ns, "instantiate", inst_fn);
    JSValue validate_fn = JS_NewCFunction(ctx, ns_wasm_validate, "validate",
                                          1);
    JS_SetPropertyStr(ctx, ns, "validate", validate_fn);
    JS_SetPropertyStr(ctx, global, "WebAssembly", ns);

    JSValue boot = JS_Eval(ctx, ns_wasm_bootstrap_js,
                           sizeof(ns_wasm_bootstrap_js) - 1,
                           "<wasm-bootstrap>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(boot))
        JS_FreeValue(ctx, JS_GetException(ctx));
    JS_FreeValue(ctx, boot);
}
