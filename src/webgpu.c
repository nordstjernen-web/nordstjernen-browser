/* Nordstjernen — experimental WebGPU (navigator.gpu) over wgpu-native.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "webgpu.h"

#ifdef ND_HAVE_WEBGPU

#include <string.h>
#include <stdint.h>

#include "webgpu/webgpu.h"
#include "webgpu/wgpu.h"
#include "js.h"

static WGPUInstance g_wg_instance;
static JSClassID g_adapter_class;
static JSClassID g_device_class;
static JSClassID g_queue_class;
static JSClassID g_buffer_class;
static JSClassID g_context_class;
static JSClassID g_texture_class;
static JSClassID g_view_class;
static JSClassID g_encoder_class;
static JSClassID g_pass_class;
static JSClassID g_cmdbuf_class;

static GHashTable *g_webgpu_ctx_by_node;

typedef struct { WGPUAdapter adapter; } ns_wg_adapter;
typedef struct { WGPUDevice device; WGPUQueue queue; } ns_wg_device;
typedef struct { WGPUQueue queue; } ns_wg_queue;
typedef struct { WGPUBuffer buffer; uint64_t size; uint32_t usage; } ns_wg_buffer;
typedef struct { WGPUTexture texture; uint32_t w, h; WGPUTextureFormat format; } ns_wg_texture;
typedef struct { WGPUTextureView view; } ns_wg_view;
typedef struct { WGPUCommandEncoder enc; } ns_wg_encoder;
typedef struct { WGPURenderPassEncoder pass; } ns_wg_pass;
typedef struct { WGPUCommandBuffer cmd; } ns_wg_cmdbuf;

typedef struct {
    const ns_node *canvas;
    WGPUDevice     device;
    WGPUQueue      queue;
    WGPUTexture    target;
    WGPUTextureFormat format;
    int            w, h;
    gboolean       configured;
    gboolean       opaque;
    cairo_surface_t *surf;
} ns_wg_context;

static JSValue wg_device_createCommandEncoder(JSContext *ctx,
                                              JSValueConst this_val,
                                              int argc, JSValueConst *argv);

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
    ns_wg_queue *q = wg_queue_unwrap(this_val);
    if (!q || argc < 1) return JS_UNDEFINED;
    uint32_t len = 0;
    JSValue jlen = JS_GetPropertyStr(ctx, argv[0], "length");
    JS_ToUint32(ctx, &len, jlen);
    JS_FreeValue(ctx, jlen);
    if (len == 0) return JS_UNDEFINED;

    WGPUCommandBuffer *cmds = g_new0(WGPUCommandBuffer, len);
    uint32_t n = 0;
    for (uint32_t i = 0; i < len; i++) {
        JSValue e = JS_GetPropertyUint32(ctx, argv[0], i);
        ns_wg_cmdbuf *cb = JS_GetOpaque(e, g_cmdbuf_class);
        if (cb && cb->cmd) cmds[n++] = cb->cmd;
        JS_FreeValue(ctx, e);
    }
    if (n > 0) wgpuQueueSubmit(q->queue, n, cmds);
    g_free(cmds);
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
    wg_bind(ctx, obj, "createCommandEncoder", wg_device_createCommandEncoder, 1);
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

static int
wg_canvas_dim(const ns_node *canvas, const char *name, int defv)
{
    const char *s = ns_element_get_attr(canvas, name);
    if (!s || !*s) return defv;
    long v = strtol(s, NULL, 10);
    if (v <= 0) return defv;
    if (v > 8192) v = 8192;
    return (int)v;
}

static WGPUTextureFormat
wg_format_from_str(const char *s)
{
    if (s && strcmp(s, "rgba8unorm") == 0) return WGPUTextureFormat_RGBA8Unorm;
    return WGPUTextureFormat_BGRA8Unorm;
}

static void
wg_view_finalizer(JSRuntime *rt, JSValue val)
{
    (void)rt;
    ns_wg_view *v = JS_GetOpaque(val, g_view_class);
    if (!v) return;
    if (v->view) wgpuTextureViewRelease(v->view);
    g_free(v);
}

static JSValue
wg_make_view(JSContext *ctx, WGPUTextureView view)
{
    JSValue obj = JS_NewObjectClass(ctx, g_view_class);
    if (JS_IsException(obj)) return obj;
    ns_wg_view *v = g_new0(ns_wg_view, 1);
    v->view = view;
    JS_SetOpaque(obj, v);
    return obj;
}

static JSValue
wg_texture_createView(JSContext *ctx, JSValueConst this_val,
                      int argc, JSValueConst *argv)
{
    (void)argc; (void)argv;
    ns_wg_texture *t = JS_GetOpaque(this_val, g_texture_class);
    if (!t || !t->texture) return JS_UNDEFINED;
    WGPUTextureView view = wgpuTextureCreateView(t->texture, NULL);
    if (!view) return JS_UNDEFINED;
    return wg_make_view(ctx, view);
}

static JSValue
wg_texture_destroy(JSContext *ctx, JSValueConst this_val,
                   int argc, JSValueConst *argv)
{
    (void)ctx; (void)argc; (void)argv;
    ns_wg_texture *t = JS_GetOpaque(this_val, g_texture_class);
    if (t && t->texture) wgpuTextureDestroy(t->texture);
    return JS_UNDEFINED;
}

static void
wg_texture_finalizer(JSRuntime *rt, JSValue val)
{
    (void)rt;
    ns_wg_texture *t = JS_GetOpaque(val, g_texture_class);
    if (!t) return;
    if (t->texture) wgpuTextureRelease(t->texture);
    g_free(t);
}

static JSValue
wg_make_texture(JSContext *ctx, WGPUTexture texture, uint32_t w, uint32_t h,
                WGPUTextureFormat format)
{
    JSValue obj = JS_NewObjectClass(ctx, g_texture_class);
    if (JS_IsException(obj)) return obj;
    ns_wg_texture *t = g_new0(ns_wg_texture, 1);
    t->texture = texture;
    t->w = w; t->h = h; t->format = format;
    JS_SetOpaque(obj, t);
    wg_bind(ctx, obj, "createView", wg_texture_createView, 1);
    wg_bind(ctx, obj, "destroy", wg_texture_destroy, 0);
    JS_SetPropertyStr(ctx, obj, "width", JS_NewUint32(ctx, w));
    JS_SetPropertyStr(ctx, obj, "height", JS_NewUint32(ctx, h));
    JS_SetPropertyStr(ctx, obj, "depthOrArrayLayers", JS_NewUint32(ctx, 1));
    JS_SetPropertyStr(ctx, obj, "format",
                      JS_NewString(ctx, format == WGPUTextureFormat_RGBA8Unorm
                                   ? "rgba8unorm" : "bgra8unorm"));
    return obj;
}

static void
wg_pass_finalizer(JSRuntime *rt, JSValue val)
{
    (void)rt;
    ns_wg_pass *p = JS_GetOpaque(val, g_pass_class);
    if (!p) return;
    if (p->pass) wgpuRenderPassEncoderRelease(p->pass);
    g_free(p);
}

static JSValue
wg_pass_end(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)ctx; (void)argc; (void)argv;
    ns_wg_pass *p = JS_GetOpaque(this_val, g_pass_class);
    if (p && p->pass) wgpuRenderPassEncoderEnd(p->pass);
    return JS_UNDEFINED;
}

static JSValue
wg_pass_noop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_UNDEFINED;
}

static JSValue
wg_make_pass(JSContext *ctx, WGPURenderPassEncoder pass)
{
    JSValue obj = JS_NewObjectClass(ctx, g_pass_class);
    if (JS_IsException(obj)) return obj;
    ns_wg_pass *p = g_new0(ns_wg_pass, 1);
    p->pass = pass;
    JS_SetOpaque(obj, p);
    wg_bind(ctx, obj, "end", wg_pass_end, 0);
    wg_bind(ctx, obj, "setPipeline", wg_pass_noop, 1);
    wg_bind(ctx, obj, "setBindGroup", wg_pass_noop, 2);
    wg_bind(ctx, obj, "setVertexBuffer", wg_pass_noop, 2);
    wg_bind(ctx, obj, "setIndexBuffer", wg_pass_noop, 2);
    wg_bind(ctx, obj, "setViewport", wg_pass_noop, 6);
    wg_bind(ctx, obj, "setScissorRect", wg_pass_noop, 4);
    wg_bind(ctx, obj, "draw", wg_pass_noop, 4);
    wg_bind(ctx, obj, "drawIndexed", wg_pass_noop, 5);
    return obj;
}

static void
wg_cmdbuf_finalizer(JSRuntime *rt, JSValue val)
{
    (void)rt;
    ns_wg_cmdbuf *c = JS_GetOpaque(val, g_cmdbuf_class);
    if (!c) return;
    if (c->cmd) wgpuCommandBufferRelease(c->cmd);
    g_free(c);
}

static JSValue
wg_make_cmdbuf(JSContext *ctx, WGPUCommandBuffer cmd)
{
    JSValue obj = JS_NewObjectClass(ctx, g_cmdbuf_class);
    if (JS_IsException(obj)) return obj;
    ns_wg_cmdbuf *c = g_new0(ns_wg_cmdbuf, 1);
    c->cmd = cmd;
    JS_SetOpaque(obj, c);
    return obj;
}

static double
wg_color_component(JSContext *ctx, JSValueConst color, const char *key, int idx)
{
    double out = 0;
    if (JS_IsArray(color)) {
        JSValue e = JS_GetPropertyUint32(ctx, color, (uint32_t)idx);
        JS_ToFloat64(ctx, &out, e);
        JS_FreeValue(ctx, e);
    } else if (JS_IsObject(color)) {
        JSValue e = JS_GetPropertyStr(ctx, color, key);
        JS_ToFloat64(ctx, &out, e);
        JS_FreeValue(ctx, e);
    }
    return out;
}

static JSValue
wg_encoder_beginRenderPass(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    ns_wg_encoder *e = JS_GetOpaque(this_val, g_encoder_class);
    if (!e || !e->enc || argc < 1 || !JS_IsObject(argv[0]))
        return JS_UNDEFINED;

    JSValue atts = JS_GetPropertyStr(ctx, argv[0], "colorAttachments");
    WGPURenderPassColorAttachment color;
    memset(&color, 0, sizeof color);
    color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;

    if (JS_IsArray(atts)) {
        JSValue a0 = JS_GetPropertyUint32(ctx, atts, 0);
        if (JS_IsObject(a0)) {
            JSValue jview = JS_GetPropertyStr(ctx, a0, "view");
            ns_wg_view *vw = JS_GetOpaque(jview, g_view_class);
            if (vw) color.view = vw->view;
            JS_FreeValue(ctx, jview);

            JSValue jload = JS_GetPropertyStr(ctx, a0, "loadOp");
            const char *ls = JS_IsString(jload) ? JS_ToCString(ctx, jload) : NULL;
            if (ls && strcmp(ls, "load") == 0) color.loadOp = WGPULoadOp_Load;
            if (ls) JS_FreeCString(ctx, ls);
            JS_FreeValue(ctx, jload);

            JSValue jstore = JS_GetPropertyStr(ctx, a0, "storeOp");
            const char *ss = JS_IsString(jstore) ? JS_ToCString(ctx, jstore) : NULL;
            if (ss && strcmp(ss, "discard") == 0) color.storeOp = WGPUStoreOp_Discard;
            if (ss) JS_FreeCString(ctx, ss);
            JS_FreeValue(ctx, jstore);

            JSValue jclear = JS_GetPropertyStr(ctx, a0, "clearValue");
            color.clearValue.r = wg_color_component(ctx, jclear, "r", 0);
            color.clearValue.g = wg_color_component(ctx, jclear, "g", 1);
            color.clearValue.b = wg_color_component(ctx, jclear, "b", 2);
            color.clearValue.a = JS_IsUndefined(jclear)
                ? 1.0 : wg_color_component(ctx, jclear, "a", 3);
            JS_FreeValue(ctx, jclear);
        }
        JS_FreeValue(ctx, a0);
    }
    JS_FreeValue(ctx, atts);

    if (!color.view) return JS_UNDEFINED;

    WGPURenderPassDescriptor desc;
    memset(&desc, 0, sizeof desc);
    desc.colorAttachmentCount = 1;
    desc.colorAttachments = &color;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(e->enc, &desc);
    if (!pass) return JS_UNDEFINED;
    return wg_make_pass(ctx, pass);
}

static JSValue
wg_encoder_finish(JSContext *ctx, JSValueConst this_val,
                  int argc, JSValueConst *argv)
{
    (void)argc; (void)argv;
    ns_wg_encoder *e = JS_GetOpaque(this_val, g_encoder_class);
    if (!e || !e->enc) return JS_UNDEFINED;
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(e->enc, NULL);
    if (!cmd) return JS_UNDEFINED;
    return wg_make_cmdbuf(ctx, cmd);
}

static void
wg_encoder_finalizer(JSRuntime *rt, JSValue val)
{
    (void)rt;
    ns_wg_encoder *e = JS_GetOpaque(val, g_encoder_class);
    if (!e) return;
    if (e->enc) wgpuCommandEncoderRelease(e->enc);
    g_free(e);
}

static JSValue
wg_make_encoder(JSContext *ctx, WGPUCommandEncoder enc)
{
    JSValue obj = JS_NewObjectClass(ctx, g_encoder_class);
    if (JS_IsException(obj)) return obj;
    ns_wg_encoder *e = g_new0(ns_wg_encoder, 1);
    e->enc = enc;
    JS_SetOpaque(obj, e);
    wg_bind(ctx, obj, "beginRenderPass", wg_encoder_beginRenderPass, 1);
    wg_bind(ctx, obj, "finish", wg_encoder_finish, 0);
    return obj;
}

static JSValue
wg_device_createCommandEncoder(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    (void)argc; (void)argv;
    ns_wg_device *d = JS_GetOpaque(this_val, g_device_class);
    if (!d) return JS_UNDEFINED;
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(d->device, NULL);
    if (!enc) return JS_UNDEFINED;
    return wg_make_encoder(ctx, enc);
}

static void
wg_ctx_release_gpu(ns_wg_context *c)
{
    if (c->target) { wgpuTextureRelease(c->target); c->target = NULL; }
    if (c->surf) { cairo_surface_destroy(c->surf); c->surf = NULL; }
}

static gboolean
wg_ctx_ensure_target(ns_wg_context *c)
{
    int w = wg_canvas_dim(c->canvas, "width", 300);
    int h = wg_canvas_dim(c->canvas, "height", 150);
    if (c->target && w == c->w && h == c->h) return TRUE;
    wg_ctx_release_gpu(c);
    c->w = w; c->h = h;

    WGPUTextureDescriptor td;
    memset(&td, 0, sizeof td);
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc |
               WGPUTextureUsage_TextureBinding;
    td.dimension = WGPUTextureDimension_2D;
    td.size.width = (uint32_t)w;
    td.size.height = (uint32_t)h;
    td.size.depthOrArrayLayers = 1;
    td.format = c->format;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    c->target = wgpuDeviceCreateTexture(c->device, &td);
    return c->target != NULL;
}

static JSValue
wg_ctx_configure(JSContext *ctx, JSValueConst this_val,
                 int argc, JSValueConst *argv)
{
    ns_wg_context *c = JS_GetOpaque(this_val, g_context_class);
    if (!c || argc < 1 || !JS_IsObject(argv[0]))
        return JS_ThrowTypeError(ctx, "configure: descriptor required");

    JSValue jdev = JS_GetPropertyStr(ctx, argv[0], "device");
    ns_wg_device *d = JS_GetOpaque(jdev, g_device_class);
    JS_FreeValue(ctx, jdev);
    if (!d) return JS_ThrowTypeError(ctx, "configure: valid device required");

    JSValue jfmt = JS_GetPropertyStr(ctx, argv[0], "format");
    const char *fmt = JS_IsString(jfmt) ? JS_ToCString(ctx, jfmt) : NULL;
    JSValue jalpha = JS_GetPropertyStr(ctx, argv[0], "alphaMode");
    const char *alpha = JS_IsString(jalpha) ? JS_ToCString(ctx, jalpha) : NULL;

    if (c->device) wgpuDeviceRelease(c->device);
    if (c->queue) wgpuQueueRelease(c->queue);
    wgpuDeviceAddRef(d->device);
    c->device = d->device;
    c->queue = wgpuDeviceGetQueue(d->device);
    c->format = wg_format_from_str(fmt);
    c->opaque = !(alpha && strcmp(alpha, "premultiplied") == 0);
    c->configured = TRUE;
    wg_ctx_release_gpu(c);

    if (fmt) JS_FreeCString(ctx, fmt);
    if (alpha) JS_FreeCString(ctx, alpha);
    JS_FreeValue(ctx, jfmt);
    JS_FreeValue(ctx, jalpha);
    return JS_UNDEFINED;
}

static JSValue
wg_ctx_unconfigure(JSContext *ctx, JSValueConst this_val,
                   int argc, JSValueConst *argv)
{
    (void)ctx; (void)argc; (void)argv;
    ns_wg_context *c = JS_GetOpaque(this_val, g_context_class);
    if (c) { c->configured = FALSE; wg_ctx_release_gpu(c); }
    return JS_UNDEFINED;
}

static JSValue
wg_ctx_getCurrentTexture(JSContext *ctx, JSValueConst this_val,
                         int argc, JSValueConst *argv)
{
    (void)argc; (void)argv;
    ns_wg_context *c = JS_GetOpaque(this_val, g_context_class);
    if (!c || !c->configured || !c->device)
        return JS_ThrowTypeError(ctx, "InvalidStateError: getCurrentTexture: not configured");
    if (!wg_ctx_ensure_target(c))
        return JS_ThrowInternalError(ctx, "getCurrentTexture: no target");
    wgpuTextureAddRef(c->target);
    return wg_make_texture(ctx, c->target, (uint32_t)c->w, (uint32_t)c->h,
                           c->format);
}

static JSValue
wg_ctx_getConfiguration(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv)
{
    (void)argc; (void)argv;
    ns_wg_context *c = JS_GetOpaque(this_val, g_context_class);
    if (!c || !c->configured) return JS_NULL;
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "format",
                      JS_NewString(ctx, c->format == WGPUTextureFormat_RGBA8Unorm
                                   ? "rgba8unorm" : "bgra8unorm"));
    JS_SetPropertyStr(ctx, o, "alphaMode",
                      JS_NewString(ctx, c->opaque ? "opaque" : "premultiplied"));
    return o;
}

static void
wg_context_finalizer(JSRuntime *rt, JSValue val)
{
    (void)rt;
    ns_wg_context *c = JS_GetOpaque(val, g_context_class);
    if (!c) return;
    if (g_webgpu_ctx_by_node)
        g_hash_table_remove(g_webgpu_ctx_by_node, c->canvas);
    wg_ctx_release_gpu(c);
    if (c->device) wgpuDeviceRelease(c->device);
    if (c->queue) wgpuQueueRelease(c->queue);
    g_free(c);
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
        wg_register_class(ctx, &g_context_class, "GPUCanvasContext",
                          wg_context_finalizer);
        wg_register_class(ctx, &g_texture_class, "GPUTexture",
                          wg_texture_finalizer);
        wg_register_class(ctx, &g_view_class, "GPUTextureView",
                          wg_view_finalizer);
        wg_register_class(ctx, &g_encoder_class, "GPUCommandEncoder",
                          wg_encoder_finalizer);
        wg_register_class(ctx, &g_pass_class, "GPURenderPassEncoder",
                          wg_pass_finalizer);
        wg_register_class(ctx, &g_cmdbuf_class, "GPUCommandBuffer",
                          wg_cmdbuf_finalizer);
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
    (void)js; (void)canvas_obj;
    if (!ns_webgpu_allowed()) return JS_NULL;
    if (!g_context_class) return JS_NULL;
    if (!g_webgpu_ctx_by_node)
        g_webgpu_ctx_by_node = g_hash_table_new(g_direct_hash, g_direct_equal);

    JSValue obj = JS_NewObjectClass(ctx, g_context_class);
    if (JS_IsException(obj)) return obj;
    ns_wg_context *c = g_new0(ns_wg_context, 1);
    c->canvas = canvas;
    c->format = WGPUTextureFormat_BGRA8Unorm;
    c->opaque = TRUE;
    JS_SetOpaque(obj, c);
    g_hash_table_insert(g_webgpu_ctx_by_node, (gpointer)canvas, c);

    wg_bind(ctx, obj, "configure", wg_ctx_configure, 1);
    wg_bind(ctx, obj, "unconfigure", wg_ctx_unconfigure, 0);
    wg_bind(ctx, obj, "getCurrentTexture", wg_ctx_getCurrentTexture, 0);
    wg_bind(ctx, obj, "getConfiguration", wg_ctx_getConfiguration, 0);
    JS_SetPropertyStr(ctx, obj, "canvas", JS_DupValue(ctx, canvas_obj));
    return obj;
}

typedef struct { int done; } wg_map_wait;

static void
wg_on_map(WGPUMapAsyncStatus status, WGPUStringView message, void *u1, void *u2)
{
    (void)status; (void)message; (void)u2;
    wg_map_wait *w = u1;
    w->done = 1;
}

cairo_surface_t *
ns_webgpu_canvas_surface(const ns_node *canvas)
{
    if (!g_webgpu_ctx_by_node) return NULL;
    ns_wg_context *c = g_hash_table_lookup(g_webgpu_ctx_by_node, canvas);
    if (!c || !c->configured || !c->target || !c->device || !c->queue)
        return NULL;
    int w = c->w, h = c->h;
    if (w <= 0 || h <= 0) return NULL;

    uint32_t bytes_per_row = ((uint32_t)w * 4u + 255u) & ~255u;
    uint64_t buf_size = (uint64_t)bytes_per_row * (uint64_t)h;

    WGPUBufferDescriptor bd;
    memset(&bd, 0, sizeof bd);
    bd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    bd.size = buf_size;
    WGPUBuffer rb = wgpuDeviceCreateBuffer(c->device, &bd);
    if (!rb) return c->surf;

    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(c->device, NULL);
    WGPUTexelCopyTextureInfo src;
    memset(&src, 0, sizeof src);
    src.texture = c->target;
    src.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dst;
    memset(&dst, 0, sizeof dst);
    dst.buffer = rb;
    dst.layout.bytesPerRow = bytes_per_row;
    dst.layout.rowsPerImage = (uint32_t)h;
    WGPUExtent3D copy_size = { (uint32_t)w, (uint32_t)h, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &copy_size);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, NULL);
    wgpuQueueSubmit(c->queue, 1, &cmd);

    wg_map_wait wait = { 0 };
    WGPUBufferMapCallbackInfo mci;
    memset(&mci, 0, sizeof mci);
    mci.mode = WGPUCallbackMode_AllowProcessEvents;
    mci.callback = wg_on_map;
    mci.userdata1 = &wait;
    wgpuBufferMapAsync(rb, WGPUMapMode_Read, 0, (size_t)buf_size, mci);
    for (int i = 0; i < 4000 && !wait.done; i++) {
        wgpuDevicePoll(c->device, 1, NULL);
        wgpuInstanceProcessEvents(ns_webgpu_instance());
    }

    const uint8_t *map = wait.done
        ? wgpuBufferGetConstMappedRange(rb, 0, (size_t)buf_size) : NULL;
    if (map) {
        if (!c->surf)
            c->surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        if (c->surf &&
            cairo_surface_status(c->surf) == CAIRO_STATUS_SUCCESS) {
            cairo_surface_flush(c->surf);
            int stride = cairo_image_surface_get_stride(c->surf);
            uint8_t *dstp = cairo_image_surface_get_data(c->surf);
            gboolean swap = (c->format == WGPUTextureFormat_RGBA8Unorm);
            for (int y = 0; y < h; y++) {
                const uint8_t *s = map + (size_t)y * bytes_per_row;
                uint8_t *d = dstp + (size_t)y * stride;
                for (int x = 0; x < w; x++) {
                    uint8_t r = s[x * 4 + 0], g = s[x * 4 + 1];
                    uint8_t b = s[x * 4 + 2], a = s[x * 4 + 3];
                    if (swap) {
                        d[x * 4 + 0] = b; d[x * 4 + 1] = g;
                        d[x * 4 + 2] = r;
                    } else {
                        d[x * 4 + 0] = r; d[x * 4 + 1] = g;
                        d[x * 4 + 2] = b;
                    }
                    d[x * 4 + 3] = c->opaque ? 255u : a;
                }
            }
            cairo_surface_mark_dirty(c->surf);
        }
        wgpuBufferUnmap(rb);
    }
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(enc);
    wgpuBufferRelease(rb);
    return c->surf;
}

#endif /* ND_HAVE_WEBGPU */
