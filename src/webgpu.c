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
static JSClassID g_shader_class;
static JSClassID g_pipeline_class;
static JSClassID g_bgl_class;
static JSClassID g_pllayout_class;
static JSClassID g_bindgroup_class;

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
typedef struct { WGPUShaderModule mod; } ns_wg_shader;
typedef struct { WGPURenderPipeline pipe; } ns_wg_pipeline;
typedef struct { WGPUBindGroupLayout layout; } ns_wg_bgl;
typedef struct { WGPUPipelineLayout layout; } ns_wg_pllayout;
typedef struct { WGPUBindGroup group; } ns_wg_bindgroup;

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
static JSValue wg_device_createShaderModule(JSContext *ctx,
                                            JSValueConst this_val,
                                            int argc, JSValueConst *argv);
static JSValue wg_device_createRenderPipeline(JSContext *ctx,
                                              JSValueConst this_val,
                                              int argc, JSValueConst *argv);
static JSValue wg_device_createBindGroupLayout(JSContext *ctx,
                                               JSValueConst this_val,
                                               int argc, JSValueConst *argv);
static JSValue wg_device_createPipelineLayout(JSContext *ctx,
                                              JSValueConst this_val,
                                              int argc, JSValueConst *argv);
static JSValue wg_device_createBindGroup(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv);
static JSValue wg_make_bgl(JSContext *ctx, WGPUBindGroupLayout layout);
static JSValue wg_pipeline_getBindGroupLayout(JSContext *ctx,
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
    {
        JSValue lost_funcs[2];
        JSValue lost = JS_NewPromiseCapability(ctx, lost_funcs);
        JS_FreeValue(ctx, lost_funcs[0]);
        JS_FreeValue(ctx, lost_funcs[1]);
        JS_SetPropertyStr(ctx, obj, "lost", lost);
    }
    JS_SetPropertyStr(ctx, obj, "features", wg_new_feature_set(ctx));
    WGPULimits limits; memset(&limits, 0, sizeof limits);
    JS_SetPropertyStr(ctx, obj, "limits", wg_limits_object(ctx, &limits));
    JS_SetPropertyStr(ctx, obj, "label", JS_NewString(ctx, ""));
    wg_bind(ctx, obj, "createBuffer", wg_device_createBuffer, 1);
    wg_bind(ctx, obj, "createCommandEncoder", wg_device_createCommandEncoder, 1);
    wg_bind(ctx, obj, "createShaderModule", wg_device_createShaderModule, 1);
    wg_bind(ctx, obj, "createRenderPipeline", wg_device_createRenderPipeline, 1);
    wg_bind(ctx, obj, "createBindGroupLayout", wg_device_createBindGroupLayout, 1);
    wg_bind(ctx, obj, "createPipelineLayout", wg_device_createPipelineLayout, 1);
    wg_bind(ctx, obj, "createBindGroup", wg_device_createBindGroup, 1);
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
wg_pass_setPipeline(JSContext *ctx, JSValueConst this_val,
                    int argc, JSValueConst *argv)
{
    (void)ctx;
    ns_wg_pass *p = JS_GetOpaque(this_val, g_pass_class);
    if (!p || !p->pass || argc < 1) return JS_UNDEFINED;
    ns_wg_pipeline *pl = JS_GetOpaque(argv[0], g_pipeline_class);
    if (pl && pl->pipe) wgpuRenderPassEncoderSetPipeline(p->pass, pl->pipe);
    return JS_UNDEFINED;
}

static JSValue
wg_pass_setVertexBuffer(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv)
{
    ns_wg_pass *p = JS_GetOpaque(this_val, g_pass_class);
    if (!p || !p->pass || argc < 2) return JS_UNDEFINED;
    int32_t slot = 0;
    JS_ToInt32(ctx, &slot, argv[0]);
    ns_wg_buffer *b = JS_GetOpaque(argv[1], g_buffer_class);
    if (!b || !b->buffer) return JS_UNDEFINED;
    int64_t offset = 0;
    if (argc >= 3) JS_ToInt64(ctx, &offset, argv[2]);
    uint64_t size = b->size > (uint64_t)offset ? b->size - (uint64_t)offset : 0;
    if (argc >= 4 && !JS_IsUndefined(argv[3])) {
        int64_t s = 0; JS_ToInt64(ctx, &s, argv[3]);
        if (s > 0) size = (uint64_t)s;
    }
    wgpuRenderPassEncoderSetVertexBuffer(p->pass, (uint32_t)slot, b->buffer,
                                         (uint64_t)offset, size);
    return JS_UNDEFINED;
}

static JSValue
wg_pass_setIndexBuffer(JSContext *ctx, JSValueConst this_val,
                       int argc, JSValueConst *argv)
{
    ns_wg_pass *p = JS_GetOpaque(this_val, g_pass_class);
    if (!p || !p->pass || argc < 2) return JS_UNDEFINED;
    ns_wg_buffer *b = JS_GetOpaque(argv[0], g_buffer_class);
    if (!b || !b->buffer) return JS_UNDEFINED;
    WGPUIndexFormat fmt = WGPUIndexFormat_Uint32;
    const char *fs = JS_IsString(argv[1]) ? JS_ToCString(ctx, argv[1]) : NULL;
    if (fs && strcmp(fs, "uint16") == 0) fmt = WGPUIndexFormat_Uint16;
    if (fs) JS_FreeCString(ctx, fs);
    int64_t offset = 0;
    if (argc >= 3) JS_ToInt64(ctx, &offset, argv[2]);
    uint64_t size = b->size > (uint64_t)offset ? b->size - (uint64_t)offset : 0;
    if (argc >= 4 && !JS_IsUndefined(argv[3])) {
        int64_t s = 0; JS_ToInt64(ctx, &s, argv[3]);
        if (s > 0) size = (uint64_t)s;
    }
    wgpuRenderPassEncoderSetIndexBuffer(p->pass, b->buffer, fmt,
                                        (uint64_t)offset, size);
    return JS_UNDEFINED;
}

static JSValue
wg_pass_setBindGroup(JSContext *ctx, JSValueConst this_val,
                     int argc, JSValueConst *argv)
{
    ns_wg_pass *p = JS_GetOpaque(this_val, g_pass_class);
    if (!p || !p->pass || argc < 2) return JS_UNDEFINED;
    uint32_t index = 0;
    JS_ToUint32(ctx, &index, argv[0]);
    ns_wg_bindgroup *bg = JS_GetOpaque(argv[1], g_bindgroup_class);
    wgpuRenderPassEncoderSetBindGroup(p->pass, index,
                                      bg ? bg->group : NULL, 0, NULL);
    return JS_UNDEFINED;
}

static JSValue
wg_pass_draw(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    ns_wg_pass *p = JS_GetOpaque(this_val, g_pass_class);
    if (!p || !p->pass) return JS_UNDEFINED;
    uint32_t vc = 0, ic = 1, fv = 0, fi = 0;
    if (argc >= 1) JS_ToUint32(ctx, &vc, argv[0]);
    if (argc >= 2 && !JS_IsUndefined(argv[1])) JS_ToUint32(ctx, &ic, argv[1]);
    if (argc >= 3) JS_ToUint32(ctx, &fv, argv[2]);
    if (argc >= 4) JS_ToUint32(ctx, &fi, argv[3]);
    wgpuRenderPassEncoderDraw(p->pass, vc, ic, fv, fi);
    return JS_UNDEFINED;
}

static JSValue
wg_pass_drawIndexed(JSContext *ctx, JSValueConst this_val,
                    int argc, JSValueConst *argv)
{
    ns_wg_pass *p = JS_GetOpaque(this_val, g_pass_class);
    if (!p || !p->pass) return JS_UNDEFINED;
    uint32_t icnt = 0, inst = 1, fi = 0, ff = 0;
    int32_t bv = 0;
    if (argc >= 1) JS_ToUint32(ctx, &icnt, argv[0]);
    if (argc >= 2 && !JS_IsUndefined(argv[1])) JS_ToUint32(ctx, &inst, argv[1]);
    if (argc >= 3) JS_ToUint32(ctx, &fi, argv[2]);
    if (argc >= 4) JS_ToInt32(ctx, &bv, argv[3]);
    if (argc >= 5) JS_ToUint32(ctx, &ff, argv[4]);
    wgpuRenderPassEncoderDrawIndexed(p->pass, icnt, inst, fi, bv, ff);
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
    wg_bind(ctx, obj, "setPipeline", wg_pass_setPipeline, 1);
    wg_bind(ctx, obj, "setBindGroup", wg_pass_setBindGroup, 2);
    wg_bind(ctx, obj, "setVertexBuffer", wg_pass_setVertexBuffer, 2);
    wg_bind(ctx, obj, "setIndexBuffer", wg_pass_setIndexBuffer, 2);
    wg_bind(ctx, obj, "setViewport", wg_pass_noop, 6);
    wg_bind(ctx, obj, "setScissorRect", wg_pass_noop, 4);
    wg_bind(ctx, obj, "draw", wg_pass_draw, 4);
    wg_bind(ctx, obj, "drawIndexed", wg_pass_drawIndexed, 5);
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

static WGPUStringView
wg_sv(const char *s)
{
    WGPUStringView v;
    v.data = s;
    v.length = s ? strlen(s) : 0;
    return v;
}

static WGPUVertexFormat
wg_vertex_format(const char *s)
{
    if (!s) return WGPUVertexFormat_Float32x3;
    if (strcmp(s, "float32x2") == 0) return WGPUVertexFormat_Float32x2;
    if (strcmp(s, "float32x4") == 0) return WGPUVertexFormat_Float32x4;
    if (strcmp(s, "float32") == 0)   return WGPUVertexFormat_Float32;
    if (strcmp(s, "uint32") == 0)    return WGPUVertexFormat_Uint32;
    return WGPUVertexFormat_Float32x3;
}

static WGPUPrimitiveTopology
wg_topology(const char *s)
{
    if (!s) return WGPUPrimitiveTopology_TriangleList;
    if (strcmp(s, "triangle-strip") == 0) return WGPUPrimitiveTopology_TriangleStrip;
    if (strcmp(s, "line-list") == 0)  return WGPUPrimitiveTopology_LineList;
    if (strcmp(s, "line-strip") == 0) return WGPUPrimitiveTopology_LineStrip;
    if (strcmp(s, "point-list") == 0) return WGPUPrimitiveTopology_PointList;
    return WGPUPrimitiveTopology_TriangleList;
}

static void
wg_shader_finalizer(JSRuntime *rt, JSValue val)
{
    (void)rt;
    ns_wg_shader *s = JS_GetOpaque(val, g_shader_class);
    if (!s) return;
    if (s->mod) wgpuShaderModuleRelease(s->mod);
    g_free(s);
}

static JSValue
wg_shader_compilationInfo(JSContext *ctx, JSValueConst this_val,
                          int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    JSValue info = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, info, "messages", JS_NewArray(ctx));
    return wg_promise_resolved(ctx, info);
}

static JSValue
wg_device_createShaderModule(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    ns_wg_device *d = JS_GetOpaque(this_val, g_device_class);
    if (!d || argc < 1 || !JS_IsObject(argv[0]))
        return JS_ThrowTypeError(ctx, "createShaderModule: descriptor required");
    JSValue jcode = JS_GetPropertyStr(ctx, argv[0], "code");
    const char *code = JS_IsString(jcode) ? JS_ToCString(ctx, jcode) : NULL;
    JS_FreeValue(ctx, jcode);
    if (!code) return JS_ThrowTypeError(ctx, "createShaderModule: code required");

    WGPUShaderSourceWGSL src;
    memset(&src, 0, sizeof src);
    src.chain.sType = WGPUSType_ShaderSourceWGSL;
    src.code = wg_sv(code);
    WGPUShaderModuleDescriptor desc;
    memset(&desc, 0, sizeof desc);
    desc.nextInChain = (WGPUChainedStruct *)&src;
    WGPUShaderModule mod = wgpuDeviceCreateShaderModule(d->device, &desc);
    JS_FreeCString(ctx, code);
    if (!mod) return JS_ThrowInternalError(ctx, "createShaderModule failed");

    JSValue obj = JS_NewObjectClass(ctx, g_shader_class);
    ns_wg_shader *s = g_new0(ns_wg_shader, 1);
    s->mod = mod;
    JS_SetOpaque(obj, s);
    wg_bind(ctx, obj, "getCompilationInfo", wg_shader_compilationInfo, 0);
    return obj;
}

static void
wg_pipeline_finalizer(JSRuntime *rt, JSValue val)
{
    (void)rt;
    ns_wg_pipeline *p = JS_GetOpaque(val, g_pipeline_class);
    if (!p) return;
    if (p->pipe) wgpuRenderPipelineRelease(p->pipe);
    g_free(p);
}

#define NS_WG_MAX_VBUF 16
#define NS_WG_MAX_ATTR 16
#define NS_WG_MAX_TARGET 8

static JSValue
wg_device_createRenderPipeline(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    ns_wg_device *d = JS_GetOpaque(this_val, g_device_class);
    if (!d || argc < 1 || !JS_IsObject(argv[0]))
        return JS_ThrowTypeError(ctx, "createRenderPipeline: descriptor required");

    WGPURenderPipelineDescriptor desc;
    memset(&desc, 0, sizeof desc);
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;

    JSValue jlayout = JS_GetPropertyStr(ctx, argv[0], "layout");
    ns_wg_pllayout *pll = JS_GetOpaque(jlayout, g_pllayout_class);
    if (pll) desc.layout = pll->layout;
    JS_FreeValue(ctx, jlayout);

    char *vs_entry = NULL, *fs_entry = NULL;
    WGPUVertexBufferLayout vbl[NS_WG_MAX_VBUF];
    WGPUVertexAttribute attrs[NS_WG_MAX_VBUF][NS_WG_MAX_ATTR];
    WGPUColorTargetState targets[NS_WG_MAX_TARGET];
    WGPUFragmentState frag;
    memset(vbl, 0, sizeof vbl);
    memset(attrs, 0, sizeof attrs);
    memset(targets, 0, sizeof targets);
    memset(&frag, 0, sizeof frag);

    JSValue jvertex = JS_GetPropertyStr(ctx, argv[0], "vertex");
    if (JS_IsObject(jvertex)) {
        JSValue jmod = JS_GetPropertyStr(ctx, jvertex, "module");
        ns_wg_shader *vm = JS_GetOpaque(jmod, g_shader_class);
        if (vm) desc.vertex.module = vm->mod;
        JS_FreeValue(ctx, jmod);
        JSValue jentry = JS_GetPropertyStr(ctx, jvertex, "entryPoint");
        if (JS_IsString(jentry)) vs_entry = (char *)JS_ToCString(ctx, jentry);
        JS_FreeValue(ctx, jentry);
        desc.vertex.entryPoint = wg_sv(vs_entry);

        JSValue jbufs = JS_GetPropertyStr(ctx, jvertex, "buffers");
        if (JS_IsArray(jbufs)) {
            uint32_t nb = 0;
            JSValue jbl = JS_GetPropertyStr(ctx, jbufs, "length");
            JS_ToUint32(ctx, &nb, jbl);
            JS_FreeValue(ctx, jbl);
            if (nb > NS_WG_MAX_VBUF) nb = NS_WG_MAX_VBUF;
            for (uint32_t i = 0; i < nb; i++) {
                JSValue jb = JS_GetPropertyUint32(ctx, jbufs, i);
                if (!JS_IsObject(jb)) { JS_FreeValue(ctx, jb); continue; }
                int64_t stride = 0;
                JSValue jstride = JS_GetPropertyStr(ctx, jb, "arrayStride");
                JS_ToInt64(ctx, &stride, jstride);
                JS_FreeValue(ctx, jstride);
                vbl[i].arrayStride = (uint64_t)stride;
                vbl[i].stepMode = WGPUVertexStepMode_Vertex;
                JSValue jsm = JS_GetPropertyStr(ctx, jb, "stepMode");
                const char *sm = JS_IsString(jsm) ? JS_ToCString(ctx, jsm) : NULL;
                if (sm && strcmp(sm, "instance") == 0)
                    vbl[i].stepMode = WGPUVertexStepMode_Instance;
                if (sm) JS_FreeCString(ctx, sm);
                JS_FreeValue(ctx, jsm);

                JSValue jattrs = JS_GetPropertyStr(ctx, jb, "attributes");
                uint32_t na = 0;
                if (JS_IsArray(jattrs)) {
                    JSValue jal = JS_GetPropertyStr(ctx, jattrs, "length");
                    JS_ToUint32(ctx, &na, jal);
                    JS_FreeValue(ctx, jal);
                    if (na > NS_WG_MAX_ATTR) na = NS_WG_MAX_ATTR;
                    for (uint32_t k = 0; k < na; k++) {
                        JSValue ja = JS_GetPropertyUint32(ctx, jattrs, k);
                        JSValue jf = JS_GetPropertyStr(ctx, ja, "format");
                        const char *fs = JS_IsString(jf) ? JS_ToCString(ctx, jf) : NULL;
                        attrs[i][k].format = wg_vertex_format(fs);
                        if (fs) JS_FreeCString(ctx, fs);
                        JS_FreeValue(ctx, jf);
                        int64_t off = 0; uint32_t loc = 0;
                        JSValue jo = JS_GetPropertyStr(ctx, ja, "offset");
                        JS_ToInt64(ctx, &off, jo); JS_FreeValue(ctx, jo);
                        JSValue jl = JS_GetPropertyStr(ctx, ja, "shaderLocation");
                        JS_ToUint32(ctx, &loc, jl); JS_FreeValue(ctx, jl);
                        attrs[i][k].offset = (uint64_t)off;
                        attrs[i][k].shaderLocation = loc;
                        JS_FreeValue(ctx, ja);
                    }
                }
                JS_FreeValue(ctx, jattrs);
                vbl[i].attributeCount = na;
                vbl[i].attributes = attrs[i];
                JS_FreeValue(ctx, jb);
            }
            desc.vertex.bufferCount = nb;
            desc.vertex.buffers = vbl;
        }
        JS_FreeValue(ctx, jbufs);
    }
    JS_FreeValue(ctx, jvertex);

    JSValue jprim = JS_GetPropertyStr(ctx, argv[0], "primitive");
    if (JS_IsObject(jprim)) {
        JSValue jt = JS_GetPropertyStr(ctx, jprim, "topology");
        const char *ts = JS_IsString(jt) ? JS_ToCString(ctx, jt) : NULL;
        desc.primitive.topology = wg_topology(ts);
        if (ts) JS_FreeCString(ctx, ts);
        JS_FreeValue(ctx, jt);
    }
    JS_FreeValue(ctx, jprim);

    JSValue jfrag = JS_GetPropertyStr(ctx, argv[0], "fragment");
    if (JS_IsObject(jfrag)) {
        JSValue jmod = JS_GetPropertyStr(ctx, jfrag, "module");
        ns_wg_shader *fm = JS_GetOpaque(jmod, g_shader_class);
        if (fm) frag.module = fm->mod;
        JS_FreeValue(ctx, jmod);
        JSValue jentry = JS_GetPropertyStr(ctx, jfrag, "entryPoint");
        if (JS_IsString(jentry)) fs_entry = (char *)JS_ToCString(ctx, jentry);
        JS_FreeValue(ctx, jentry);
        frag.entryPoint = wg_sv(fs_entry);

        JSValue jtargets = JS_GetPropertyStr(ctx, jfrag, "targets");
        uint32_t nt = 0;
        if (JS_IsArray(jtargets)) {
            JSValue jtl = JS_GetPropertyStr(ctx, jtargets, "length");
            JS_ToUint32(ctx, &nt, jtl);
            JS_FreeValue(ctx, jtl);
            if (nt > NS_WG_MAX_TARGET) nt = NS_WG_MAX_TARGET;
            for (uint32_t i = 0; i < nt; i++) {
                JSValue jtg = JS_GetPropertyUint32(ctx, jtargets, i);
                JSValue jf = JS_GetPropertyStr(ctx, jtg, "format");
                const char *fs = JS_IsString(jf) ? JS_ToCString(ctx, jf) : NULL;
                targets[i].format = wg_format_from_str(fs);
                if (fs) JS_FreeCString(ctx, fs);
                JS_FreeValue(ctx, jf);
                targets[i].writeMask = WGPUColorWriteMask_All;
                JS_FreeValue(ctx, jtg);
            }
        }
        JS_FreeValue(ctx, jtargets);
        frag.targetCount = nt;
        frag.targets = targets;
        desc.fragment = &frag;
    }
    JS_FreeValue(ctx, jfrag);

    WGPURenderPipeline pipe = wgpuDeviceCreateRenderPipeline(d->device, &desc);
    if (vs_entry) JS_FreeCString(ctx, vs_entry);
    if (fs_entry) JS_FreeCString(ctx, fs_entry);
    if (!pipe) return JS_ThrowInternalError(ctx, "createRenderPipeline failed");

    JSValue obj = JS_NewObjectClass(ctx, g_pipeline_class);
    ns_wg_pipeline *p = g_new0(ns_wg_pipeline, 1);
    p->pipe = pipe;
    JS_SetOpaque(obj, p);
    wg_bind(ctx, obj, "getBindGroupLayout", wg_pipeline_getBindGroupLayout, 1);
    return obj;
}

static JSValue
wg_pipeline_getBindGroupLayout(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    ns_wg_pipeline *p = JS_GetOpaque(this_val, g_pipeline_class);
    if (!p || !p->pipe || argc < 1) return JS_UNDEFINED;
    uint32_t index = 0;
    JS_ToUint32(ctx, &index, argv[0]);
    WGPUBindGroupLayout l = wgpuRenderPipelineGetBindGroupLayout(p->pipe, index);
    if (!l) return JS_UNDEFINED;
    return wg_make_bgl(ctx, l);
}

static void
wg_bgl_finalizer(JSRuntime *rt, JSValue val)
{
    (void)rt;
    ns_wg_bgl *b = JS_GetOpaque(val, g_bgl_class);
    if (!b) return;
    if (b->layout) wgpuBindGroupLayoutRelease(b->layout);
    g_free(b);
}

static JSValue
wg_make_bgl(JSContext *ctx, WGPUBindGroupLayout layout)
{
    JSValue obj = JS_NewObjectClass(ctx, g_bgl_class);
    if (JS_IsException(obj)) return obj;
    ns_wg_bgl *b = g_new0(ns_wg_bgl, 1);
    b->layout = layout;
    JS_SetOpaque(obj, b);
    return obj;
}

static WGPUBufferBindingType
wg_buffer_binding_type(const char *s)
{
    if (s && strcmp(s, "storage") == 0) return WGPUBufferBindingType_Storage;
    if (s && strcmp(s, "read-only-storage") == 0)
        return WGPUBufferBindingType_ReadOnlyStorage;
    return WGPUBufferBindingType_Uniform;
}

#define NS_WG_MAX_BGL_ENTRY 32

static JSValue
wg_device_createBindGroupLayout(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    ns_wg_device *d = JS_GetOpaque(this_val, g_device_class);
    if (!d || argc < 1 || !JS_IsObject(argv[0]))
        return JS_ThrowTypeError(ctx, "createBindGroupLayout: descriptor");

    WGPUBindGroupLayoutEntry entries[NS_WG_MAX_BGL_ENTRY];
    memset(entries, 0, sizeof entries);
    uint32_t n = 0;
    JSValue jentries = JS_GetPropertyStr(ctx, argv[0], "entries");
    if (JS_IsArray(jentries)) {
        JSValue jl = JS_GetPropertyStr(ctx, jentries, "length");
        JS_ToUint32(ctx, &n, jl);
        JS_FreeValue(ctx, jl);
        if (n > NS_WG_MAX_BGL_ENTRY) n = NS_WG_MAX_BGL_ENTRY;
        for (uint32_t i = 0; i < n; i++) {
            JSValue e = JS_GetPropertyUint32(ctx, jentries, i);
            uint32_t binding = 0, vis = 0;
            JSValue jb = JS_GetPropertyStr(ctx, e, "binding");
            JS_ToUint32(ctx, &binding, jb); JS_FreeValue(ctx, jb);
            JSValue jv = JS_GetPropertyStr(ctx, e, "visibility");
            JS_ToUint32(ctx, &vis, jv); JS_FreeValue(ctx, jv);
            entries[i].binding = binding;
            entries[i].visibility = vis;
            JSValue jbuf = JS_GetPropertyStr(ctx, e, "buffer");
            if (JS_IsObject(jbuf)) {
                JSValue jt = JS_GetPropertyStr(ctx, jbuf, "type");
                const char *ts = JS_IsString(jt) ? JS_ToCString(ctx, jt) : NULL;
                entries[i].buffer.type = wg_buffer_binding_type(ts);
                if (ts) JS_FreeCString(ctx, ts);
                JS_FreeValue(ctx, jt);
            }
            JS_FreeValue(ctx, jbuf);
            JS_FreeValue(ctx, e);
        }
    }
    JS_FreeValue(ctx, jentries);

    WGPUBindGroupLayoutDescriptor desc;
    memset(&desc, 0, sizeof desc);
    desc.entryCount = n;
    desc.entries = entries;
    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(d->device, &desc);
    if (!layout)
        return JS_ThrowInternalError(ctx, "createBindGroupLayout failed");
    return wg_make_bgl(ctx, layout);
}

static void
wg_pllayout_finalizer(JSRuntime *rt, JSValue val)
{
    (void)rt;
    ns_wg_pllayout *p = JS_GetOpaque(val, g_pllayout_class);
    if (!p) return;
    if (p->layout) wgpuPipelineLayoutRelease(p->layout);
    g_free(p);
}

static JSValue
wg_device_createPipelineLayout(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    ns_wg_device *d = JS_GetOpaque(this_val, g_device_class);
    if (!d || argc < 1 || !JS_IsObject(argv[0]))
        return JS_ThrowTypeError(ctx, "createPipelineLayout: descriptor");

    WGPUBindGroupLayout layouts[NS_WG_MAX_BGL_ENTRY];
    memset(layouts, 0, sizeof layouts);
    uint32_t n = 0;
    JSValue jbgls = JS_GetPropertyStr(ctx, argv[0], "bindGroupLayouts");
    if (JS_IsArray(jbgls)) {
        JSValue jl = JS_GetPropertyStr(ctx, jbgls, "length");
        JS_ToUint32(ctx, &n, jl);
        JS_FreeValue(ctx, jl);
        if (n > NS_WG_MAX_BGL_ENTRY) n = NS_WG_MAX_BGL_ENTRY;
        for (uint32_t i = 0; i < n; i++) {
            JSValue e = JS_GetPropertyUint32(ctx, jbgls, i);
            ns_wg_bgl *b = JS_GetOpaque(e, g_bgl_class);
            if (b) layouts[i] = b->layout;
            JS_FreeValue(ctx, e);
        }
    }
    JS_FreeValue(ctx, jbgls);

    WGPUPipelineLayoutDescriptor desc;
    memset(&desc, 0, sizeof desc);
    desc.bindGroupLayoutCount = n;
    desc.bindGroupLayouts = layouts;
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(d->device, &desc);
    if (!layout)
        return JS_ThrowInternalError(ctx, "createPipelineLayout failed");

    JSValue obj = JS_NewObjectClass(ctx, g_pllayout_class);
    ns_wg_pllayout *p = g_new0(ns_wg_pllayout, 1);
    p->layout = layout;
    JS_SetOpaque(obj, p);
    return obj;
}

static void
wg_bindgroup_finalizer(JSRuntime *rt, JSValue val)
{
    (void)rt;
    ns_wg_bindgroup *b = JS_GetOpaque(val, g_bindgroup_class);
    if (!b) return;
    if (b->group) wgpuBindGroupRelease(b->group);
    g_free(b);
}

static JSValue
wg_device_createBindGroup(JSContext *ctx, JSValueConst this_val,
                          int argc, JSValueConst *argv)
{
    ns_wg_device *d = JS_GetOpaque(this_val, g_device_class);
    if (!d || argc < 1 || !JS_IsObject(argv[0]))
        return JS_ThrowTypeError(ctx, "createBindGroup: descriptor");

    JSValue jlayout = JS_GetPropertyStr(ctx, argv[0], "layout");
    ns_wg_bgl *bgl = JS_GetOpaque(jlayout, g_bgl_class);
    JS_FreeValue(ctx, jlayout);
    if (!bgl) return JS_ThrowTypeError(ctx, "createBindGroup: layout");

    WGPUBindGroupEntry entries[NS_WG_MAX_BGL_ENTRY];
    memset(entries, 0, sizeof entries);
    uint32_t n = 0;
    JSValue jentries = JS_GetPropertyStr(ctx, argv[0], "entries");
    if (JS_IsArray(jentries)) {
        JSValue jl = JS_GetPropertyStr(ctx, jentries, "length");
        JS_ToUint32(ctx, &n, jl);
        JS_FreeValue(ctx, jl);
        if (n > NS_WG_MAX_BGL_ENTRY) n = NS_WG_MAX_BGL_ENTRY;
        for (uint32_t i = 0; i < n; i++) {
            JSValue e = JS_GetPropertyUint32(ctx, jentries, i);
            uint32_t binding = 0;
            JSValue jb = JS_GetPropertyStr(ctx, e, "binding");
            JS_ToUint32(ctx, &binding, jb); JS_FreeValue(ctx, jb);
            entries[i].binding = binding;
            JSValue jres = JS_GetPropertyStr(ctx, e, "resource");
            ns_wg_view *vw = JS_GetOpaque(jres, g_view_class);
            if (vw) {
                entries[i].textureView = vw->view;
            } else if (JS_IsObject(jres)) {
                JSValue jbuf = JS_GetPropertyStr(ctx, jres, "buffer");
                ns_wg_buffer *buf = JS_GetOpaque(jbuf, g_buffer_class);
                if (buf) {
                    entries[i].buffer = buf->buffer;
                    int64_t off = 0, sz = (int64_t)buf->size;
                    JSValue jo = JS_GetPropertyStr(ctx, jres, "offset");
                    if (!JS_IsUndefined(jo)) JS_ToInt64(ctx, &off, jo);
                    JS_FreeValue(ctx, jo);
                    JSValue js = JS_GetPropertyStr(ctx, jres, "size");
                    if (!JS_IsUndefined(js)) JS_ToInt64(ctx, &sz, js);
                    JS_FreeValue(ctx, js);
                    entries[i].offset = (uint64_t)off;
                    entries[i].size = (uint64_t)sz;
                }
                JS_FreeValue(ctx, jbuf);
            }
            JS_FreeValue(ctx, jres);
            JS_FreeValue(ctx, e);
        }
    }
    JS_FreeValue(ctx, jentries);

    WGPUBindGroupDescriptor desc;
    memset(&desc, 0, sizeof desc);
    desc.layout = bgl->layout;
    desc.entryCount = n;
    desc.entries = entries;
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(d->device, &desc);
    if (!group) return JS_ThrowInternalError(ctx, "createBindGroup failed");

    JSValue obj = JS_NewObjectClass(ctx, g_bindgroup_class);
    ns_wg_bindgroup *b = g_new0(ns_wg_bindgroup, 1);
    b->group = group;
    JS_SetOpaque(obj, b);
    return obj;
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
        wg_register_class(ctx, &g_shader_class, "GPUShaderModule",
                          wg_shader_finalizer);
        wg_register_class(ctx, &g_pipeline_class, "GPURenderPipeline",
                          wg_pipeline_finalizer);
        wg_register_class(ctx, &g_bgl_class, "GPUBindGroupLayout",
                          wg_bgl_finalizer);
        wg_register_class(ctx, &g_pllayout_class, "GPUPipelineLayout",
                          wg_pllayout_finalizer);
        wg_register_class(ctx, &g_bindgroup_class, "GPUBindGroup",
                          wg_bindgroup_finalizer);
    }

    JSValue gpu = JS_NewObject(ctx);
    wg_bind(ctx, gpu, "requestAdapter", wg_gpu_requestAdapter, 1);
    wg_bind(ctx, gpu, "getPreferredCanvasFormat",
            wg_gpu_getPreferredCanvasFormat, 0);
    JS_SetPropertyStr(ctx, gpu, "wgslLanguageFeatures", wg_new_feature_set(ctx));
    JS_SetPropertyStr(ctx, (JSValueConst)navigator, "gpu", gpu);

    JSValue global = JS_GetGlobalObject(ctx);

    JSValue buf_usage = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, buf_usage, "MAP_READ", JS_NewInt32(ctx, 0x0001));
    JS_SetPropertyStr(ctx, buf_usage, "MAP_WRITE", JS_NewInt32(ctx, 0x0002));
    JS_SetPropertyStr(ctx, buf_usage, "COPY_SRC", JS_NewInt32(ctx, 0x0004));
    JS_SetPropertyStr(ctx, buf_usage, "COPY_DST", JS_NewInt32(ctx, 0x0008));
    JS_SetPropertyStr(ctx, buf_usage, "INDEX", JS_NewInt32(ctx, 0x0010));
    JS_SetPropertyStr(ctx, buf_usage, "VERTEX", JS_NewInt32(ctx, 0x0020));
    JS_SetPropertyStr(ctx, buf_usage, "UNIFORM", JS_NewInt32(ctx, 0x0040));
    JS_SetPropertyStr(ctx, buf_usage, "STORAGE", JS_NewInt32(ctx, 0x0080));
    JS_SetPropertyStr(ctx, buf_usage, "INDIRECT", JS_NewInt32(ctx, 0x0100));
    JS_SetPropertyStr(ctx, buf_usage, "QUERY_RESOLVE", JS_NewInt32(ctx, 0x0200));
    JS_SetPropertyStr(ctx, global, "GPUBufferUsage", buf_usage);

    JSValue tex_usage = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, tex_usage, "COPY_SRC", JS_NewInt32(ctx, 0x01));
    JS_SetPropertyStr(ctx, tex_usage, "COPY_DST", JS_NewInt32(ctx, 0x02));
    JS_SetPropertyStr(ctx, tex_usage, "TEXTURE_BINDING", JS_NewInt32(ctx, 0x04));
    JS_SetPropertyStr(ctx, tex_usage, "STORAGE_BINDING", JS_NewInt32(ctx, 0x08));
    JS_SetPropertyStr(ctx, tex_usage, "RENDER_ATTACHMENT", JS_NewInt32(ctx, 0x10));
    JS_SetPropertyStr(ctx, global, "GPUTextureUsage", tex_usage);

    JSValue shader_stage = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, shader_stage, "VERTEX", JS_NewInt32(ctx, 0x1));
    JS_SetPropertyStr(ctx, shader_stage, "FRAGMENT", JS_NewInt32(ctx, 0x2));
    JS_SetPropertyStr(ctx, shader_stage, "COMPUTE", JS_NewInt32(ctx, 0x4));
    JS_SetPropertyStr(ctx, global, "GPUShaderStage", shader_stage);

    JSValue color_write = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, color_write, "RED", JS_NewInt32(ctx, 0x1));
    JS_SetPropertyStr(ctx, color_write, "GREEN", JS_NewInt32(ctx, 0x2));
    JS_SetPropertyStr(ctx, color_write, "BLUE", JS_NewInt32(ctx, 0x4));
    JS_SetPropertyStr(ctx, color_write, "ALPHA", JS_NewInt32(ctx, 0x8));
    JS_SetPropertyStr(ctx, color_write, "ALL", JS_NewInt32(ctx, 0xF));
    JS_SetPropertyStr(ctx, global, "GPUColorWrite", color_write);

    JSValue map_mode = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, map_mode, "READ", JS_NewInt32(ctx, 0x1));
    JS_SetPropertyStr(ctx, map_mode, "WRITE", JS_NewInt32(ctx, 0x2));
    JS_SetPropertyStr(ctx, global, "GPUMapMode", map_mode);

    JS_FreeValue(ctx, global);
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
