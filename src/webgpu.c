/* Nordstjernen — experimental WebGPU (navigator.gpu) over wgpu-native.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "webgpu.h"

#ifdef ND_HAVE_WEBGPU

#include <string.h>
#include <stdint.h>

#include "webgpu/webgpu.h"
#include "js.h"

static WGPUInstance g_wg_instance;
static JSClassID g_adapter_class;
static JSClassID g_device_class;
static JSClassID g_queue_class;
static JSClassID g_buffer_class;

typedef struct { WGPUAdapter adapter; } ns_wg_adapter;
typedef struct { WGPUDevice device; WGPUQueue queue; } ns_wg_device;
typedef struct { WGPUQueue queue; } ns_wg_queue;
typedef struct { WGPUBuffer buffer; uint64_t size; uint32_t usage; } ns_wg_buffer;

static gboolean
ns_webgpu_allowed(void)
{
    const char *env = g_getenv("NS_WEBGPU_ALLOW");
    return env && env[0] == '1';
}

static WGPUInstance
ns_webgpu_instance(void)
{
    if (!g_wg_instance)
        g_wg_instance = wgpuCreateInstance(NULL);
    return g_wg_instance;
}

static char *
wg_sv_dup(WGPUStringView sv)
{
    if (sv.data && sv.length > 0)
        return g_strndup(sv.data, sv.length);
    return g_strdup("");
}

static JSValue
wg_promise_resolved(JSContext *ctx, JSValue value)
{
    JSValue funcs[2];
    JSValue p = JS_NewPromiseCapability(ctx, funcs);
    JSValue r = JS_Call(ctx, funcs[0], JS_UNDEFINED, 1, (JSValueConst *)&value);
    JS_FreeValue(ctx, r);
    JS_FreeValue(ctx, funcs[0]);
    JS_FreeValue(ctx, funcs[1]);
    JS_FreeValue(ctx, value);
    return p;
}

static JSValue
wg_promise_rejected(JSContext *ctx, const char *message)
{
    JSValue funcs[2];
    JSValue p = JS_NewPromiseCapability(ctx, funcs);
    JSValue err = JS_NewError(ctx);
    JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, message));
    JSValue r = JS_Call(ctx, funcs[1], JS_UNDEFINED, 1, (JSValueConst *)&err);
    JS_FreeValue(ctx, r);
    JS_FreeValue(ctx, funcs[0]);
    JS_FreeValue(ctx, funcs[1]);
    JS_FreeValue(ctx, err);
    return p;
}

static void
wg_bind(JSContext *ctx, JSValueConst obj, const char *name,
        JSCFunction *fn, int argc)
{
    JS_SetPropertyStr(ctx, obj, name,
                      JS_NewCFunction(ctx, fn, name, argc));
}

static JSValue
wg_new_feature_set(JSContext *ctx)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue ctor = JS_GetPropertyStr(ctx, global, "Set");
    JSValue set = JS_CallConstructor(ctx, ctor, 0, NULL);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, global);
    if (JS_IsException(set)) return JS_NewObject(ctx);
    return set;
}

static JSValue
wg_limits_object(JSContext *ctx, const WGPULimits *l)
{
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "maxTextureDimension1D",
                      JS_NewUint32(ctx, l->maxTextureDimension1D));
    JS_SetPropertyStr(ctx, o, "maxTextureDimension2D",
                      JS_NewUint32(ctx, l->maxTextureDimension2D));
    JS_SetPropertyStr(ctx, o, "maxTextureDimension3D",
                      JS_NewUint32(ctx, l->maxTextureDimension3D));
    JS_SetPropertyStr(ctx, o, "maxTextureArrayLayers",
                      JS_NewUint32(ctx, l->maxTextureArrayLayers));
    JS_SetPropertyStr(ctx, o, "maxBindGroups",
                      JS_NewUint32(ctx, l->maxBindGroups));
    JS_SetPropertyStr(ctx, o, "maxBindingsPerBindGroup",
                      JS_NewUint32(ctx, l->maxBindingsPerBindGroup));
    JS_SetPropertyStr(ctx, o, "maxUniformBufferBindingSize",
                      JS_NewFloat64(ctx, (double)l->maxUniformBufferBindingSize));
    JS_SetPropertyStr(ctx, o, "maxStorageBufferBindingSize",
                      JS_NewFloat64(ctx, (double)l->maxStorageBufferBindingSize));
    JS_SetPropertyStr(ctx, o, "maxBufferSize",
                      JS_NewFloat64(ctx, (double)l->maxBufferSize));
    JS_SetPropertyStr(ctx, o, "maxComputeWorkgroupSizeX",
                      JS_NewUint32(ctx, l->maxComputeWorkgroupSizeX));
    JS_SetPropertyStr(ctx, o, "maxComputeWorkgroupSizeY",
                      JS_NewUint32(ctx, l->maxComputeWorkgroupSizeY));
    JS_SetPropertyStr(ctx, o, "maxComputeWorkgroupSizeZ",
                      JS_NewUint32(ctx, l->maxComputeWorkgroupSizeZ));
    JS_SetPropertyStr(ctx, o, "maxComputeInvocationsPerWorkgroup",
                      JS_NewUint32(ctx, l->maxComputeInvocationsPerWorkgroup));
    return o;
}

static ns_wg_queue *
wg_queue_unwrap(JSValueConst v)
{
    return JS_GetOpaque(v, g_queue_class);
}

static JSValue
wg_queue_writeBuffer(JSContext *ctx, JSValueConst this_val,
                     int argc, JSValueConst *argv)
{
    ns_wg_queue *q = wg_queue_unwrap(this_val);
    if (!q || argc < 3) return JS_UNDEFINED;
    ns_wg_buffer *buf = JS_GetOpaque(argv[0], g_buffer_class);
    if (!buf) return JS_UNDEFINED;
    int64_t buffer_offset = 0;
    JS_ToInt64(ctx, &buffer_offset, argv[1]);

    size_t byte_len = 0;
    size_t view_off = 0, view_len = 0, bpe = 0;
    uint8_t *bytes = NULL;
    JSValue abuf = JS_GetTypedArrayBuffer(ctx, argv[2], &view_off, &view_len, &bpe);
    if (!JS_IsException(abuf)) {
        size_t total = 0;
        uint8_t *base = JS_GetArrayBuffer(ctx, &total, abuf);
        if (base) { bytes = base + view_off; byte_len = view_len; }
        JS_FreeValue(ctx, abuf);
    } else {
        JS_FreeValue(ctx, abuf);
        bytes = JS_GetArrayBuffer(ctx, &byte_len, argv[2]);
    }
    if (!bytes) return JS_UNDEFINED;

    int64_t data_offset = 0, size = (int64_t)byte_len;
    if (argc >= 4) JS_ToInt64(ctx, &data_offset, argv[3]);
    if (argc >= 5) JS_ToInt64(ctx, &size, argv[4]);
    if (data_offset < 0 || (size_t)data_offset > byte_len) return JS_UNDEFINED;
    if (size < 0 || (size_t)(data_offset + size) > byte_len)
        size = (int64_t)(byte_len - data_offset);

    wgpuQueueWriteBuffer(q->queue, buf->buffer, (uint64_t)buffer_offset,
                         bytes + data_offset, (size_t)size);
    return JS_UNDEFINED;
}

static JSValue
wg_queue_submit(JSContext *ctx, JSValueConst this_val,
                int argc, JSValueConst *argv)
{
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_UNDEFINED;
}

static void
wg_queue_finalizer(JSRuntime *rt, JSValue val)
{
    (void)rt;
    ns_wg_queue *q = JS_GetOpaque(val, g_queue_class);
    if (!q) return;
    if (q->queue) wgpuQueueRelease(q->queue);
    g_free(q);
}

static JSValue
wg_make_queue(JSContext *ctx, WGPUQueue queue)
{
    JSValue obj = JS_NewObjectClass(ctx, g_queue_class);
    if (JS_IsException(obj)) return obj;
    ns_wg_queue *q = g_new0(ns_wg_queue, 1);
    q->queue = queue;
    JS_SetOpaque(obj, q);
    wg_bind(ctx, obj, "writeBuffer", wg_queue_writeBuffer, 5);
    wg_bind(ctx, obj, "submit", wg_queue_submit, 1);
    JS_SetPropertyStr(ctx, obj, "label", JS_NewString(ctx, ""));
    return obj;
}

static void
wg_buffer_finalizer(JSRuntime *rt, JSValue val)
{
    (void)rt;
    ns_wg_buffer *b = JS_GetOpaque(val, g_buffer_class);
    if (!b) return;
    if (b->buffer) wgpuBufferRelease(b->buffer);
    g_free(b);
}

static JSValue
wg_buffer_destroy(JSContext *ctx, JSValueConst this_val,
                  int argc, JSValueConst *argv)
{
    (void)ctx; (void)argc; (void)argv;
    ns_wg_buffer *b = JS_GetOpaque(this_val, g_buffer_class);
    if (b && b->buffer) { wgpuBufferDestroy(b->buffer); }
    return JS_UNDEFINED;
}

static JSValue
wg_device_createBuffer(JSContext *ctx, JSValueConst this_val,
                       int argc, JSValueConst *argv)
{
    ns_wg_device *d = JS_GetOpaque(this_val, g_device_class);
    if (!d || argc < 1 || !JS_IsObject(argv[0]))
        return wg_promise_rejected(ctx, "createBuffer: descriptor required");

    JSValue jsize = JS_GetPropertyStr(ctx, argv[0], "size");
    JSValue jusage = JS_GetPropertyStr(ctx, argv[0], "usage");
    JSValue jmap = JS_GetPropertyStr(ctx, argv[0], "mappedAtCreation");
    int64_t size = 0; uint32_t usage = 0;
    JS_ToInt64(ctx, &size, jsize);
    JS_ToUint32(ctx, &usage, jusage);
    int mapped = JS_ToBool(ctx, jmap);
    JS_FreeValue(ctx, jsize);
    JS_FreeValue(ctx, jusage);
    JS_FreeValue(ctx, jmap);

    WGPUBufferDescriptor desc;
    memset(&desc, 0, sizeof desc);
    desc.size = (uint64_t)(size < 0 ? 0 : size);
    desc.usage = (WGPUBufferUsage)usage;
    desc.mappedAtCreation = mapped ? 1 : 0;
    WGPUBuffer wbuf = wgpuDeviceCreateBuffer(d->device, &desc);
    if (!wbuf)
        return JS_ThrowInternalError(ctx, "createBuffer: wgpu returned null");

    JSValue obj = JS_NewObjectClass(ctx, g_buffer_class);
    ns_wg_buffer *b = g_new0(ns_wg_buffer, 1);
    b->buffer = wbuf;
    b->size = desc.size;
    b->usage = usage;
    JS_SetOpaque(obj, b);
    wg_bind(ctx, obj, "destroy", wg_buffer_destroy, 0);
    JS_SetPropertyStr(ctx, obj, "size", JS_NewFloat64(ctx, (double)desc.size));
    JS_SetPropertyStr(ctx, obj, "usage", JS_NewUint32(ctx, usage));
    JS_SetPropertyStr(ctx, obj, "label", JS_NewString(ctx, ""));
    return obj;
}

static JSValue
wg_device_getQueue(JSContext *ctx, JSValueConst this_val,
                   int argc, JSValueConst *argv)
{
    (void)argc; (void)argv;
    ns_wg_device *d = JS_GetOpaque(this_val, g_device_class);
    if (!d) return JS_UNDEFINED;
    wgpuQueueAddRef(d->queue);
    return wg_make_queue(ctx, d->queue);
}

static JSValue
wg_device_destroy(JSContext *ctx, JSValueConst this_val,
                  int argc, JSValueConst *argv)
{
    (void)ctx; (void)argc; (void)argv;
    (void)this_val;
    return JS_UNDEFINED;
}

static void
wg_device_finalizer(JSRuntime *rt, JSValue val)
{
    (void)rt;
    ns_wg_device *d = JS_GetOpaque(val, g_device_class);
    if (!d) return;
    if (d->queue) wgpuQueueRelease(d->queue);
    if (d->device) wgpuDeviceRelease(d->device);
    g_free(d);
}

static JSValue
wg_make_device(JSContext *ctx, WGPUDevice device)
{
    JSValue obj = JS_NewObjectClass(ctx, g_device_class);
    if (JS_IsException(obj)) return obj;
    ns_wg_device *d = g_new0(ns_wg_device, 1);
    d->device = device;
    d->queue = wgpuDeviceGetQueue(device);
    JS_SetOpaque(obj, d);

    wgpuQueueAddRef(d->queue);
    JS_SetPropertyStr(ctx, obj, "queue", wg_make_queue(ctx, d->queue));
    JS_SetPropertyStr(ctx, obj, "features", wg_new_feature_set(ctx));
    WGPULimits limits; memset(&limits, 0, sizeof limits);
    JS_SetPropertyStr(ctx, obj, "limits", wg_limits_object(ctx, &limits));
    JS_SetPropertyStr(ctx, obj, "label", JS_NewString(ctx, ""));
    wg_bind(ctx, obj, "createBuffer", wg_device_createBuffer, 1);
    wg_bind(ctx, obj, "getQueue", wg_device_getQueue, 0);
    wg_bind(ctx, obj, "destroy", wg_device_destroy, 0);
    return obj;
}

typedef struct { WGPUDevice device; int done; } wg_device_wait;

static void
wg_on_device(WGPURequestDeviceStatus status, WGPUDevice device,
             WGPUStringView message, void *u1, void *u2)
{
    (void)message; (void)u2;
    wg_device_wait *w = u1;
    w->device = (status == WGPURequestDeviceStatus_Success) ? device : NULL;
    w->done = 1;
}

static JSValue
wg_adapter_requestDevice(JSContext *ctx, JSValueConst this_val,
                         int argc, JSValueConst *argv)
{
    (void)argc; (void)argv;
    ns_wg_adapter *a = JS_GetOpaque(this_val, g_adapter_class);
    if (!a) return wg_promise_rejected(ctx, "requestDevice: invalid adapter");

    wg_device_wait wait; memset(&wait, 0, sizeof wait);
    WGPURequestDeviceCallbackInfo ci; memset(&ci, 0, sizeof ci);
    ci.mode = WGPUCallbackMode_AllowProcessEvents;
    ci.callback = wg_on_device;
    ci.userdata1 = &wait;
    wgpuAdapterRequestDevice(a->adapter, NULL, ci);
    for (int i = 0; i < 2000 && !wait.done; i++)
        wgpuInstanceProcessEvents(ns_webgpu_instance());
    if (!wait.device)
        return wg_promise_rejected(ctx, "requestDevice: no device");
    return wg_promise_resolved(ctx, wg_make_device(ctx, wait.device));
}

static JSValue
wg_adapter_info(JSContext *ctx, WGPUAdapter adapter)
{
    WGPUAdapterInfo info; memset(&info, 0, sizeof info);
    JSValue o = JS_NewObject(ctx);
    if (wgpuAdapterGetInfo(adapter, &info) == WGPUStatus_Success) {
        char *vendor = wg_sv_dup(info.vendor);
        char *arch = wg_sv_dup(info.architecture);
        char *dev = wg_sv_dup(info.device);
        char *descr = wg_sv_dup(info.description);
        JS_SetPropertyStr(ctx, o, "vendor", JS_NewString(ctx, vendor));
        JS_SetPropertyStr(ctx, o, "architecture", JS_NewString(ctx, arch));
        JS_SetPropertyStr(ctx, o, "device", JS_NewString(ctx, dev));
        JS_SetPropertyStr(ctx, o, "description", JS_NewString(ctx, descr));
        g_free(vendor); g_free(arch); g_free(dev); g_free(descr);
        wgpuAdapterInfoFreeMembers(info);
    }
    return o;
}

static void
wg_adapter_finalizer(JSRuntime *rt, JSValue val)
{
    (void)rt;
    ns_wg_adapter *a = JS_GetOpaque(val, g_adapter_class);
    if (!a) return;
    if (a->adapter) wgpuAdapterRelease(a->adapter);
    g_free(a);
}

static JSValue
wg_make_adapter(JSContext *ctx, WGPUAdapter adapter)
{
    JSValue obj = JS_NewObjectClass(ctx, g_adapter_class);
    if (JS_IsException(obj)) return obj;
    ns_wg_adapter *a = g_new0(ns_wg_adapter, 1);
    a->adapter = adapter;
    JS_SetOpaque(obj, a);

    JS_SetPropertyStr(ctx, obj, "info", wg_adapter_info(ctx, adapter));
    JS_SetPropertyStr(ctx, obj, "features", wg_new_feature_set(ctx));
    WGPULimits limits; memset(&limits, 0, sizeof limits);
    wgpuAdapterGetLimits(adapter, &limits);
    JS_SetPropertyStr(ctx, obj, "limits", wg_limits_object(ctx, &limits));
    JS_SetPropertyStr(ctx, obj, "isFallbackAdapter", JS_FALSE);
    wg_bind(ctx, obj, "requestDevice", wg_adapter_requestDevice, 1);
    return obj;
}

typedef struct { WGPUAdapter adapter; int done; } wg_adapter_wait;

static void
wg_on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter,
              WGPUStringView message, void *u1, void *u2)
{
    (void)message; (void)u2;
    wg_adapter_wait *w = u1;
    w->adapter = (status == WGPURequestAdapterStatus_Success) ? adapter : NULL;
    w->done = 1;
}

static JSValue
wg_gpu_requestAdapter(JSContext *ctx, JSValueConst this_val,
                      int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    if (!ns_webgpu_allowed())
        return wg_promise_resolved(ctx, JS_NULL);
    WGPUInstance inst = ns_webgpu_instance();
    if (!inst)
        return wg_promise_resolved(ctx, JS_NULL);

    wg_adapter_wait wait; memset(&wait, 0, sizeof wait);
    WGPURequestAdapterCallbackInfo ci; memset(&ci, 0, sizeof ci);
    ci.mode = WGPUCallbackMode_AllowProcessEvents;
    ci.callback = wg_on_adapter;
    ci.userdata1 = &wait;
    wgpuInstanceRequestAdapter(inst, NULL, ci);
    for (int i = 0; i < 2000 && !wait.done; i++)
        wgpuInstanceProcessEvents(inst);
    if (!wait.adapter)
        return wg_promise_resolved(ctx, JS_NULL);
    return wg_promise_resolved(ctx, wg_make_adapter(ctx, wait.adapter));
}

static JSValue
wg_gpu_getPreferredCanvasFormat(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return JS_NewString(ctx, "bgra8unorm");
}

static void
wg_register_class(JSContext *ctx, JSClassID *id, const char *name,
                  JSClassFinalizer *finalizer)
{
    JSClassDef def;
    memset(&def, 0, sizeof def);
    def.class_name = name;
    def.finalizer = finalizer;
    JS_NewClassID(JS_GetRuntime(ctx), id);
    JS_NewClass(JS_GetRuntime(ctx), *id, &def);
}

void
ns_webgpu_install(JSContext *ctx, ns_js *js, JSValueConst navigator)
{
    (void)js;
    if (!g_adapter_class) {
        wg_register_class(ctx, &g_adapter_class, "GPUAdapter",
                          wg_adapter_finalizer);
        wg_register_class(ctx, &g_device_class, "GPUDevice",
                          wg_device_finalizer);
        wg_register_class(ctx, &g_queue_class, "GPUQueue",
                          wg_queue_finalizer);
        wg_register_class(ctx, &g_buffer_class, "GPUBuffer",
                          wg_buffer_finalizer);
    }

    JSValue gpu = JS_NewObject(ctx);
    wg_bind(ctx, gpu, "requestAdapter", wg_gpu_requestAdapter, 1);
    wg_bind(ctx, gpu, "getPreferredCanvasFormat",
            wg_gpu_getPreferredCanvasFormat, 0);
    JS_SetPropertyStr(ctx, gpu, "wgslLanguageFeatures", wg_new_feature_set(ctx));
    JS_SetPropertyStr(ctx, (JSValueConst)navigator, "gpu", gpu);
}

JSValue
ns_webgpu_get_context(JSContext *ctx, ns_js *js, JSValueConst canvas_obj,
                      const ns_node *canvas)
{
    (void)ctx; (void)js; (void)canvas_obj; (void)canvas;
    return JS_NULL;
}

cairo_surface_t *
ns_webgpu_canvas_surface(const ns_node *canvas)
{
    (void)canvas;
    return NULL;
}

#endif /* ND_HAVE_WEBGPU */
