/* Nordstjernen — WebIDL brand checks for the native members of interfaces. */

#include "js_brand.h"

#include <string.h>
#include <glib.h>

typedef struct {
    JSValue node_proto;
    JSValue attr_proto;
    int node_brand;
    int attr_brand;
    int element_brand;
} ns_brand_plan;

static gboolean
ns_str_in_list(const char *str, const char *const *list)
{
    for (int i = 0; list[i]; i++)
        if (strcmp(str, list[i]) == 0) return TRUE;
    return FALSE;
}

static JSBrandMode
ns_brand_mode_for(const char *name)
{
    static const char *const lenient[] = {
        "onmouseenter", "onmouseleave", "onreadystatechange", NULL
    };
    static const char *const promised[] = {
        "decode", "exitFullscreen", "exitPictureInPicture", "hasStorageAccess",
        "play", "requestFullscreen", "requestPictureInPicture",
        "requestPointerLock", "requestStorageAccess", "scroll", "scrollBy",
        "scrollIntoView", "scrollTo", "setMediaKeys", "setSinkId", NULL
    };
    if (ns_str_in_list(name, lenient)) return JS_BRAND_IGNORE;
    if (ns_str_in_list(name, promised)) return JS_BRAND_REJECT;
    return JS_BRAND_THROW;
}

static void
ns_js_brand_member(JSContext *ctx, JSValueConst proto, JSAtom atom, int brand)
{
    const char *name = JS_AtomToCString(ctx, atom);
    JSPropertyDescriptor d;
    if (name && strcmp(name, "constructor") != 0 &&
        JS_GetOwnProperty(ctx, &d, proto, atom) > 0) {
        JSBrandMode mode = ns_brand_mode_for(name);
        JS_SetCFunctionBrand(ctx, d.value, brand, mode);
        JS_SetCFunctionBrand(ctx, d.getter, brand, mode);
        JS_SetCFunctionBrand(ctx, d.setter, brand, mode);
        JS_FreeValue(ctx, d.value);
        JS_FreeValue(ctx, d.getter);
        JS_FreeValue(ctx, d.setter);
    }
    JS_FreeCString(ctx, name);
}

static void
ns_js_brand_members(JSContext *ctx, JSValueConst proto, int brand)
{
    JSPropertyEnum *props = NULL;
    uint32_t n = 0;
    if (!brand ||
        JS_GetOwnPropertyNames(ctx, &props, &n, proto,
                               JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK) < 0)
        return;
    for (uint32_t i = 0; i < n; i++)
        ns_js_brand_member(ctx, proto, props[i].atom, brand);
    JS_FreePropertyEnum(ctx, props, n);
}

static gboolean
ns_js_proto_inherits(JSContext *ctx, JSValueConst proto, JSValueConst root)
{
    JSValue cur = JS_DupValue(ctx, proto);
    gboolean found = FALSE;
    for (int depth = 0; depth < 64 && JS_IsObject(cur) && !found; depth++) {
        found = JS_VALUE_GET_PTR(cur) == JS_VALUE_GET_PTR(root);
        JSValue next = JS_GetPrototype(ctx, cur);
        JS_FreeValue(ctx, cur);
        cur = next;
    }
    JS_FreeValue(ctx, cur);
    return found;
}

static int
ns_brand_for_proto(const ns_brand_plan *plan, JSValueConst proto)
{
    if (JS_VALUE_GET_PTR(proto) == JS_VALUE_GET_PTR(plan->node_proto))
        return plan->node_brand;
    if (JS_IsObject(plan->attr_proto) &&
        JS_VALUE_GET_PTR(proto) == JS_VALUE_GET_PTR(plan->attr_proto))
        return plan->attr_brand;
    return plan->element_brand;
}

static void
ns_js_brand_global_interface(JSContext *ctx, JSValueConst global, JSAtom atom,
                             const ns_brand_plan *plan)
{
    JSPropertyDescriptor d;
    if (JS_GetOwnProperty(ctx, &d, global, atom) <= 0) return;
    if (JS_IsFunction(ctx, d.value)) {
        JSValue proto = JS_GetPropertyStr(ctx, d.value, "prototype");
        if (JS_IsObject(proto) &&
            ns_js_proto_inherits(ctx, proto, plan->node_proto))
            ns_js_brand_members(ctx, proto, ns_brand_for_proto(plan, proto));
        JS_FreeValue(ctx, proto);
    }
    JS_FreeValue(ctx, d.value);
    JS_FreeValue(ctx, d.getter);
    JS_FreeValue(ctx, d.setter);
}

static JSValue
ns_interface_proto(JSContext *ctx, JSValueConst global, const char *name)
{
    JSValue ctor = JS_GetPropertyStr(ctx, global, name);
    JSValue proto = JS_GetPropertyStr(ctx, ctor, "prototype");
    JS_FreeValue(ctx, ctor);
    return proto;
}

void
ns_js_brand_node_interfaces(JSContext *ctx, JSClassID element_cid,
                            JSClassID attr_cid)
{
    JSClassID node_classes[2] = { element_cid, attr_cid };
    JSValue global = JS_GetGlobalObject(ctx);
    ns_brand_plan plan = {
        .node_proto = ns_interface_proto(ctx, global, "Node"),
        .attr_proto = ns_interface_proto(ctx, global, "Attr"),
        .node_brand = JS_NewCFunctionBrand(ctx, node_classes, 2),
        .attr_brand = JS_NewCFunctionBrand(ctx, &attr_cid, 1),
        .element_brand = JS_NewCFunctionBrand(ctx, &element_cid, 1),
    };
    JSPropertyEnum *props = NULL;
    uint32_t n = 0;
    if (JS_IsObject(plan.node_proto) &&
        JS_GetOwnPropertyNames(ctx, &props, &n, global, JS_GPN_STRING_MASK) == 0) {
        for (uint32_t i = 0; i < n; i++)
            ns_js_brand_global_interface(ctx, global, props[i].atom, &plan);
        JS_FreePropertyEnum(ctx, props, n);
    }
    JS_FreeValue(ctx, plan.node_proto);
    JS_FreeValue(ctx, plan.attr_proto);
    JS_FreeValue(ctx, global);
}
