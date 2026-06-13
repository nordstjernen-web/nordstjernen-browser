/* Nordstjernen — native ShadowRealm and AsyncContext over the QuickJS C API. */

#include "js_realm.h"

#include <string.h>
#include <glib.h>

/* ---- ShadowRealm -------------------------------------------------------- */

static JSClassID ns_shadowrealm_class_id;

typedef struct {
    JSContext *child;
} ns_shadowrealm;

static void
ns_shadowrealm_finalizer(JSRuntime *rt, JSValue val)
{
    (void)rt;
    ns_shadowrealm *r = JS_GetOpaque(val, ns_shadowrealm_class_id);
    if (!r) return;
    if (r->child) JS_FreeContext(r->child);
    g_free(r);
}

static JSClassDef ns_shadowrealm_class = {
    "ShadowRealm", .finalizer = ns_shadowrealm_finalizer,
};

static JSValue realm_wrap_value(JSContext *ctx, JSValueConst realm_obj,
                                JSValue v);

static JSValue
realm_wrapped_call(JSContext *ctx, JSValueConst this_val, int argc,
                   JSValueConst *argv, int magic, JSValueConst *data)
{
    (void)this_val; (void)magic;
    JSValueConst target = data[0];
    JSValueConst realm_obj = data[1];
    for (int i = 0; i < argc; i++) {
        if (JS_IsObject(argv[i]) && !JS_IsFunction(ctx, argv[i]))
            return JS_ThrowTypeError(ctx,
                "ShadowRealm wrapped function: only primitives and callables "
                "may cross the realm boundary");
    }
    JSValue r = JS_Call(ctx, target, JS_UNDEFINED, argc, argv);
    if (JS_IsException(r)) {
        JSValue exc = JS_GetException(ctx);
        const char *msg = JS_ToCString(ctx, exc);
        JSValue te = JS_ThrowTypeError(ctx, "%s",
                                       msg ? msg : "ShadowRealm callable threw");
        if (msg) JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, exc);
        return te;
    }
    return realm_wrap_value(ctx, realm_obj, r);
}

static JSValue
realm_wrap_value(JSContext *ctx, JSValueConst realm_obj, JSValue v)
{
    if (!JS_IsObject(v))
        return v;
    if (JS_IsFunction(ctx, v)) {
        JSValueConst data[2] = { v, realm_obj };
        JSValue fn = JS_NewCFunctionData(ctx, realm_wrapped_call, 0, 0, 2, data);
        JS_FreeValue(ctx, v);
        return fn;
    }
    JS_FreeValue(ctx, v);
    return JS_ThrowTypeError(ctx,
        "ShadowRealm: evaluation result must be a primitive or a callable");
}

static JSValue
ns_shadowrealm_ctor(JSContext *ctx, JSValueConst new_target,
                    int argc, JSValueConst *argv)
{
    (void)argc; (void)argv;
    JSContext *child = JS_NewContext(JS_GetRuntime(ctx));
    if (!child) return JS_ThrowOutOfMemory(ctx);
    JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    JSValue obj = JS_IsObject(proto)
        ? JS_NewObjectProtoClass(ctx, proto, ns_shadowrealm_class_id)
        : JS_NewObjectClass(ctx, ns_shadowrealm_class_id);
    JS_FreeValue(ctx, proto);
    if (JS_IsException(obj)) { JS_FreeContext(child); return obj; }
    ns_shadowrealm *r = g_new0(ns_shadowrealm, 1);
    r->child = child;
    JS_SetOpaque(obj, r);
    return obj;
}

static JSValue
ns_shadowrealm_evaluate(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv)
{
    ns_shadowrealm *r = JS_GetOpaque2(ctx, this_val, ns_shadowrealm_class_id);
    if (!r) return JS_EXCEPTION;
    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "ShadowRealm.prototype.evaluate expects a string");
    size_t len = 0;
    const char *src = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!src) return JS_EXCEPTION;
    JSValue res = JS_Eval(r->child, src, len, "<shadowrealm>",
                          JS_EVAL_TYPE_GLOBAL);
    JS_FreeCString(ctx, src);
    if (JS_IsException(res)) {
        JSValue exc = JS_GetException(r->child);
        const char *msg = JS_ToCString(r->child, exc);
        JSValue te = JS_ThrowTypeError(ctx, "%s",
                                       msg ? msg : "ShadowRealm evaluate threw");
        if (msg) JS_FreeCString(r->child, msg);
        JS_FreeValue(r->child, exc);
        return te;
    }
    return realm_wrap_value(ctx, this_val, res);
}

static JSValue
ns_shadowrealm_importValue(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    ns_shadowrealm *r = JS_GetOpaque2(ctx, this_val, ns_shadowrealm_class_id);
    if (!r) return JS_EXCEPTION;
    JSValue resolving[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving);
    JSValue err = JS_NewError(ctx);
    JS_SetPropertyStr(ctx, err, "message",
        JS_NewString(ctx, "ShadowRealm.prototype.importValue is not supported"));
    JSValue ret = JS_Call(ctx, resolving[1], JS_UNDEFINED, 1, (JSValueConst *)&err);
    JS_FreeValue(ctx, ret);
    JS_FreeValue(ctx, err);
    JS_FreeValue(ctx, resolving[0]);
    JS_FreeValue(ctx, resolving[1]);
    return promise;
}

/* ---- AsyncContext ------------------------------------------------------- */

static JSValue
realm_async_registry(JSContext *ctx)
{
    JSValue glob = JS_GetGlobalObject(ctx);
    JSValue ac = JS_GetPropertyStr(ctx, glob, "AsyncContext");
    JS_FreeValue(ctx, glob);
    JSValue reg = JS_GetPropertyStr(ctx, ac, "\xff""vars");
    JS_FreeValue(ctx, ac);
    return reg;
}

static JSValue
ns_acvar_ctor(JSContext *ctx, JSValueConst new_target,
              int argc, JSValueConst *argv)
{
    JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    JSValue obj = JS_IsObject(proto) ? JS_NewObjectProto(ctx, proto)
                                     : JS_NewObject(ctx);
    JS_FreeValue(ctx, proto);
    JSValueConst opts = argc >= 1 ? argv[0] : JS_UNDEFINED;
    JSValue name = JS_UNDEFINED, def = JS_UNDEFINED;
    if (JS_IsObject(opts)) {
        name = JS_GetPropertyStr(ctx, opts, "name");
        def = JS_GetPropertyStr(ctx, opts, "defaultValue");
    }
    JS_DefinePropertyValueStr(ctx, obj, "name",
        JS_IsUndefined(name) ? JS_NewString(ctx, "") : name, JS_PROP_ENUMERABLE);
    JS_DefinePropertyValueStr(ctx, obj, "\xff""def", JS_DupValue(ctx, def), 0);
    JS_DefinePropertyValueStr(ctx, obj, "\xff""cur", def,
                              JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);

    JSValue reg = realm_async_registry(ctx);
    if (JS_IsArray(reg)) {
        JSValue push = JS_GetPropertyStr(ctx, reg, "push");
        JSValueConst a[1] = { obj };
        JSValue r = JS_Call(ctx, push, reg, 1, a);
        JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, push);
    }
    JS_FreeValue(ctx, reg);
    return obj;
}

static JSValue
ns_acvar_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)argc; (void)argv;
    return JS_GetPropertyStr(ctx, this_val, "\xff""cur");
}

static JSValue
ns_acvar_run(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc < 2 || !JS_IsFunction(ctx, argv[1]))
        return JS_ThrowTypeError(ctx, "AsyncContext.Variable.run expects (value, fn)");
    JSValue old = JS_GetPropertyStr(ctx, this_val, "\xff""cur");
    JS_SetPropertyStr(ctx, this_val, "\xff""cur", JS_DupValue(ctx, argv[0]));
    JSValue r = JS_Call(ctx, argv[1], JS_UNDEFINED, argc - 2,
                        (JSValueConst *)(argv + 2));
    JS_SetPropertyStr(ctx, this_val, "\xff""cur", old);
    return r;
}

static JSValue
ns_acsnap_ctor(JSContext *ctx, JSValueConst new_target,
               int argc, JSValueConst *argv)
{
    (void)argc; (void)argv;
    JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    JSValue obj = JS_IsObject(proto) ? JS_NewObjectProto(ctx, proto)
                                     : JS_NewObject(ctx);
    JS_FreeValue(ctx, proto);
    JSValue reg = realm_async_registry(ctx);
    JSValue vals = JS_NewArray(ctx);
    uint32_t n = 0;
    if (JS_IsArray(reg)) {
        JSValue lv = JS_GetPropertyStr(ctx, reg, "length");
        JS_ToUint32(ctx, &n, lv); JS_FreeValue(ctx, lv);
        for (uint32_t i = 0; i < n; i++) {
            JSValue var = JS_GetPropertyUint32(ctx, reg, i);
            JSValue cur = JS_GetPropertyStr(ctx, var, "\xff""cur");
            JS_SetPropertyUint32(ctx, vals, i, cur);
            JS_FreeValue(ctx, var);
        }
    }
    JS_FreeValue(ctx, reg);
    JS_DefinePropertyValueStr(ctx, obj, "\xff""vals", vals, 0);
    JS_DefinePropertyValueStr(ctx, obj, "\xff""n", JS_NewInt32(ctx, (int32_t)n), 0);
    return obj;
}

static JSValue
ns_acsnap_run(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "AsyncContext.Snapshot.run expects a function");
    JSValue reg = realm_async_registry(ctx);
    JSValue vals = JS_GetPropertyStr(ctx, this_val, "\xff""vals");
    int snap_n = 0;
    { JSValue nv = JS_GetPropertyStr(ctx, this_val, "\xff""n");
      int32_t t; if (JS_ToInt32(ctx, &t, nv) == 0) snap_n = t; JS_FreeValue(ctx, nv); }

    JSValue saved = JS_NewArray(ctx);
    uint32_t total = 0;
    if (JS_IsArray(reg)) {
        JSValue lv = JS_GetPropertyStr(ctx, reg, "length");
        JS_ToUint32(ctx, &total, lv); JS_FreeValue(ctx, lv);
        for (uint32_t i = 0; i < total; i++) {
            JSValue var = JS_GetPropertyUint32(ctx, reg, i);
            JS_SetPropertyUint32(ctx, saved, i,
                                 JS_GetPropertyStr(ctx, var, "\xff""cur"));
            JSValue nv = (i < (uint32_t)snap_n)
                ? JS_GetPropertyUint32(ctx, vals, i)
                : JS_GetPropertyStr(ctx, var, "\xff""def");
            JS_SetPropertyStr(ctx, var, "\xff""cur", nv);
            JS_FreeValue(ctx, var);
        }
    }
    JSValue r = JS_Call(ctx, argv[0], JS_UNDEFINED, argc - 1,
                        (JSValueConst *)(argv + 1));
    if (JS_IsArray(reg)) {
        for (uint32_t i = 0; i < total; i++) {
            JSValue var = JS_GetPropertyUint32(ctx, reg, i);
            JS_SetPropertyStr(ctx, var, "\xff""cur",
                              JS_GetPropertyUint32(ctx, saved, i));
            JS_FreeValue(ctx, var);
        }
    }
    JS_FreeValue(ctx, saved);
    JS_FreeValue(ctx, vals);
    JS_FreeValue(ctx, reg);
    return r;
}

static JSValue
ns_acsnap_wrapped(JSContext *ctx, JSValueConst this_val, int argc,
                  JSValueConst *argv, int magic, JSValueConst *data)
{
    (void)this_val; (void)magic;
    JSValueConst snap = data[0];
    JSValueConst fn = data[1];
    JSValue run = JS_GetPropertyStr(ctx, snap, "run");
    int n = argc + 1;
    JSValue *args = g_new(JSValue, n);
    args[0] = JS_DupValue(ctx, fn);
    for (int i = 0; i < argc; i++) args[i + 1] = JS_DupValue(ctx, argv[i]);
    JSValue r = JS_Call(ctx, run, snap, n, (JSValueConst *)args);
    for (int i = 0; i < n; i++) JS_FreeValue(ctx, args[i]);
    g_free(args);
    JS_FreeValue(ctx, run);
    return r;
}

static JSValue
ns_acsnap_static_wrap(JSContext *ctx, JSValueConst this_val,
                      int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "AsyncContext.Snapshot.wrap expects a function");
    JSValue glob = JS_GetGlobalObject(ctx);
    JSValue ac = JS_GetPropertyStr(ctx, glob, "AsyncContext");
    JSValue snapctor = JS_GetPropertyStr(ctx, ac, "Snapshot");
    JSValue snap = JS_CallConstructor(ctx, snapctor, 0, NULL);
    JS_FreeValue(ctx, snapctor); JS_FreeValue(ctx, ac); JS_FreeValue(ctx, glob);
    JSValueConst data[2] = { snap, argv[0] };
    JSValue wrapped = JS_NewCFunctionData(ctx, ns_acsnap_wrapped, 0, 0, 2, data);
    JS_FreeValue(ctx, snap);
    return wrapped;
}

/* ---- install ------------------------------------------------------------ */

static void
realm_bind(JSContext *ctx, JSValueConst obj, const char *name,
           JSCFunction *fn, int argc)
{
    JS_SetPropertyStr(ctx, obj, name, JS_NewCFunction(ctx, fn, name, argc));
}

static JSValue
realm_make_ctor(JSContext *ctx, JSValueConst target, const char *name,
                JSCFunction *ctor, int argc)
{
    JSValue func = JS_NewCFunction2(ctx, ctor, name, argc,
                                    JS_CFUNC_constructor, 0);
    JSValue proto = JS_NewObject(ctx);
    JS_SetConstructor(ctx, func, proto);
    JS_SetPropertyStr(ctx, target, name, JS_DupValue(ctx, func));
    JS_FreeValue(ctx, proto);
    return func;
}

void
ns_js_realm_install(JSContext *ctx, JSValueConst global)
{
    JSRuntime *rt = JS_GetRuntime(ctx);

    JSAtom sr_atom = JS_NewAtom(ctx, "ShadowRealm");
    int has_sr = JS_HasProperty(ctx, global, sr_atom);
    JS_FreeAtom(ctx, sr_atom);
    if (has_sr <= 0) {
        if (!ns_shadowrealm_class_id) {
            JS_NewClassID(rt, &ns_shadowrealm_class_id);
            JS_NewClass(rt, ns_shadowrealm_class_id, &ns_shadowrealm_class);
        }
        JSValue ctor = JS_NewCFunction2(ctx, ns_shadowrealm_ctor, "ShadowRealm",
                                        0, JS_CFUNC_constructor, 0);
        JSValue proto = JS_NewObject(ctx);
        realm_bind(ctx, proto, "evaluate", ns_shadowrealm_evaluate, 1);
        realm_bind(ctx, proto, "importValue", ns_shadowrealm_importValue, 2);
        JS_SetConstructor(ctx, ctor, proto);
        JS_FreeValue(ctx, proto);
        JS_SetPropertyStr(ctx, global, "ShadowRealm", ctor);
    }

    JSAtom ac_atom = JS_NewAtom(ctx, "AsyncContext");
    int has_ac = JS_HasProperty(ctx, global, ac_atom);
    JS_FreeAtom(ctx, ac_atom);
    if (has_ac <= 0) {
        JSValue ac = JS_NewObject(ctx);
        JS_DefinePropertyValueStr(ctx, ac, "\xff""vars", JS_NewArray(ctx), 0);

        JSValue var = realm_make_ctor(ctx, ac, "Variable", ns_acvar_ctor, 1);
        JSValue var_proto = JS_GetPropertyStr(ctx, var, "prototype");
        realm_bind(ctx, var_proto, "get", ns_acvar_get, 0);
        realm_bind(ctx, var_proto, "run", ns_acvar_run, 2);
        JS_FreeValue(ctx, var_proto);
        JS_FreeValue(ctx, var);

        JSValue snap = realm_make_ctor(ctx, ac, "Snapshot", ns_acsnap_ctor, 0);
        JSValue snap_proto = JS_GetPropertyStr(ctx, snap, "prototype");
        realm_bind(ctx, snap_proto, "run", ns_acsnap_run, 1);
        JS_FreeValue(ctx, snap_proto);
        realm_bind(ctx, snap, "wrap", ns_acsnap_static_wrap, 1);
        JS_FreeValue(ctx, snap);

        JS_SetPropertyStr(ctx, global, "AsyncContext", ac);
    }
}
