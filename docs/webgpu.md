# WebGPU

Nordstjernen implements WebGPU (`navigator.gpu`) in `src/webgpu.c`, layered on
the external [wgpu-native](https://github.com/gfx-rs/wgpu-native) library. Like
WebGL it is a required part of the desktop build; unlike WebGL it stays off at
runtime until the browser is started with `--enable-webgpu`.

## Building with WebGPU

The `webgpu` meson feature (`auto` by default) resolves wgpu-native in this
order:

1. a `wgpu_native` pkg-config file;
2. `-Dwgpu_native_root=/path/to/extracted/release` (expects
   `lib/libwgpu_native.{so,a,dylib}`), which the Linux packaging scripts use to
   bundle the shared library (`scripts/fetch-wgpu-native.sh`);
3. on glibc Linux, macOS (x86_64/aarch64) and Windows x86_64 (MinGW), the
   pinned release downloaded by `subprojects/wgpu-native-<platform>.wrap` and
   linked **statically**, so no extra shared library ships beside the binary.

On those platforms a missing wgpu-native is a configure error — WebGPU is part
of the build. On the BSDs, musl and the mobile engine builds, where wgpu-native
publishes no release, the feature is optional and skipped when not found.
`-Dwebgpu=enabled` hard-requires it everywhere; `-Dwebgpu=disabled` drops it
everywhere.

The C API headers are vendored under `third_party/wgpu-native/` and pinned to
release **v29.0.1.1**. To move to a newer release, replace those two headers,
bump the version, URLs and hashes in the `subprojects/wgpu-native-*.wrap`
files and `scripts/fetch-wgpu-native.sh`, and rebuild.

## Runtime gating

Even in a build that contains WebGPU, the API is **denied by default**.
`navigator.gpu.requestAdapter()` resolves to `null` unless WebGPU is
explicitly enabled, by either:

- turning on **WebGPU** on `about:settings` (the `webgpu_enabled` config
  key, off by default), which applies to pages loaded afterwards,
- starting the browser with the **`--enable-webgpu`** command-line flag, or
- setting the environment variable **`NS_WEBGPU_ALLOW=1`** (what the flag
  does internally).

The shell sets the variable before the sandboxed renderer is spawned, so the
renderer — where the page's JS and `src/webgpu.c` actually run — inherits the
permission. On a build without WebGPU, `--enable-webgpu` prints a one-line
notice and is otherwise ignored. Unlike WebGL, which is enabled by default,
this keeps the large native GPU stack dormant unless explicitly requested.

## Implemented surface

The implementation (`src/webgpu.c`) covers device acquisition, buffers, and
a working **render-to-canvas** path:

- `navigator.gpu` — `requestAdapter()`, `getPreferredCanvasFormat()`,
  `wgslLanguageFeatures`.
- `GPUAdapter` — `requestDevice()`, `info` (`vendor`/`architecture`/
  `device`/`description`), `features`, `limits`, `isFallbackAdapter`.
- `GPUDevice` — `queue`, `features`, `limits`, `createBuffer()`,
  `createCommandEncoder()`, `getQueue()`, `destroy()`.
- `GPUQueue` — `writeBuffer()`, `submit()`.
- `GPUBuffer` — `size`, `usage`, `destroy()`.
- `GPUCanvasContext` (`canvas.getContext('webgpu')`) — `configure()`
  (`device`, `format`, `alphaMode`), `unconfigure()`, `getConfiguration()`,
  `getCurrentTexture()`.
- `GPUTexture` — `createView()`, `width`/`height`/`format`, `destroy()`;
  `GPUTextureView`.
- `GPUCommandEncoder` — `beginRenderPass()` (color attachment `view`,
  `loadOp`/`storeOp`, `clearValue`), `finish()`.
- `GPURenderPassEncoder` — `end()`, `setPipeline()`, `setVertexBuffer()`,
  `setIndexBuffer()`, `draw()`, `drawIndexed()`. (`setBindGroup`,
  `setViewport`, `setScissorRect` are accepted as no-ops for now.)
- `GPUCommandBuffer`.
- `GPUShaderModule` — `device.createShaderModule({ code })` compiles WGSL
  via wgpu-native's `naga`; `getCompilationInfo()`.
- `GPURenderPipeline` — `device.createRenderPipeline()` with `layout: 'auto'`,
  a `vertex` stage (`module`, `entryPoint`, and `buffers[]` —
  `arrayStride`/`stepMode`/`attributes[{format, offset, shaderLocation}]`),
  a `fragment` stage (`module`, `entryPoint`, `targets[{format}]`), and
  `primitive.topology`.

- `GPUBindGroupLayout` / `GPUPipelineLayout` / `GPUBindGroup` — buffer
  (uniform/storage), sampler, and texture-view bindings; `setBindGroup`;
  `pipeline.getBindGroupLayout()`.
- `GPUSampler` (`device.createSampler`), `GPUTexture` (`device.createTexture`,
  `createView`, `destroy`), `queue.writeTexture`,
  `queue.copyExternalImageToTexture` (uploads an image/`<canvas>`/
  `ImageBitmap`/video frame into a texture), depth/stencil and MSAA resolve
  attachments in render passes, `depthStencil` + `multisample` + `cullMode`
  pipeline state.
- `GPUBuffer.mapAsync()` (poll-driven), `GPUQuerySet` scaffolding
  (`device.createQuerySet`, `commandEncoder.resolveQuerySet` — occlusion is
  real, timestamps are accepted as no-ops so timing-instrumented apps don't
  crash).
- **Compute**: `device.createComputePipeline`, `commandEncoder.beginComputePass`,
  `setPipeline`/`setBindGroup`/`dispatchWorkgroups`/`end`, and
  `pipeline.getBindGroupLayout` — storage buffers in, results read back via
  `copyBufferToBuffer` + `mapAsync`. three.js GPU-compute examples
  (`webgpu_compute_birds`, `webgpu_compute_particles`) run on it.
- Colour-target **blend state** (`blend.color`/`blend.alpha` factors and
  operations) and `writeMask`, so transparent materials composite correctly;
  `texture.createView` **descriptors** (`format`, `dimension` incl. cube /
  2d-array / 3d, `aspect`, base/count mip + array layers).
- `GPUBuffer.getMappedRange()` / `unmap()` (for `mappedAtCreation`),
  `commandEncoder.copyTextureToTexture` / `copyBufferToBuffer`,
  `device.pushErrorScope`/`popErrorScope`, a non-fatal device
  uncaptured-error handler, and the `GPUBufferUsage`/`GPUTextureUsage`/
  `GPUShaderStage`/`GPUColorWrite`/`GPUMapMode` global flag namespaces.

So the full draw path works: WGSL shader modules → bind groups → render
pipeline (with depth) → render pass → `setPipeline`/`setBindGroup`/
`setVertexBuffer`/`draw` → submit → composited to the canvas.

**three.js's `WebGPURenderer` runs on this backend** (no WebGL2 fallback):
the `webgpu_camera` example renders its scene — wireframe spheres, the
camera-frustum helper, the axis gizmo, and the point-cloud starfield — on
real WebGPU through wgpu-native.

A configured canvas owns an offscreen target texture
(`RENDER_ATTACHMENT | COPY_SRC`); each paint, the renderer copies it back
(`copyTextureToBuffer` → map → cairo `ARGB32`, BGRA/RGBA aware,
opaque/premultiplied alpha aware) exactly like the WebGL compositor. So a
page that clears its canvas via a render pass shows the GPU-produced result.

Promise-returning calls (`requestAdapter`, `requestDevice`, buffer mapping)
are resolved by polling wgpu-native's event loop synchronously, so
`await navigator.gpu.requestAdapter()` works without integrating with the
page event loop.

### Since the first cut

- Every interface (`GPUDevice`, `GPUBuffer`, `GPURenderPassEncoder`,
  `GPURenderBundleEncoder`, ...) is a global interface object whose
  prototype carries the methods; `instanceof` checks work.
- `adapter.limits`/`device.limits` and `features` are the real ones;
  `requestDevice()` honours `requiredFeatures` and `requiredLimits`.
- Render passes: up to eight colour attachments, full depth/stencil
  attachment state, `setViewport`, `setScissorRect`, `setBlendConstant`,
  `setStencilReference`, occlusion queries, `drawIndirect`,
  `drawIndexedIndirect` and `executeBundles`. Render bundles are complete.
- Pipelines: all vertex formats, primitive state, stencil faces, depth bias,
  multisample mask / alpha-to-coverage, override constants, omitted entry
  points, and `create{Render,Compute}PipelineAsync`.
- All 99 texture formats; textures report their real attributes;
  `viewFormats`; `rgba16float` canvases.
- Bind group layouts: every view dimension, multisampled and integer
  textures, storage textures, dynamic offsets.
- Encoders: `copyBufferToTexture`, `copyTextureToBuffer`, `clearBuffer`,
  `dispatchWorkgroupsIndirect`; `queue.onSubmittedWorkDone()`.
- Errors: real error scopes, `GPUValidationError` / `GPUOutOfMemoryError` /
  `GPUInternalError`, `uncapturederror` events on the device (an
  `EventTarget`), `device.destroy()` resolving `device.lost`.

### Guarding wgpu-native

wgpu-native aborts the process instead of reporting an error in several
places, so `src/webgpu.c` keeps such input from reaching it: unknown usage
bits become a validation error, zero copy strides become
`WGPU_COPY_STRIDE_UNDEFINED`, popping an empty error scope rejects in
JavaScript, `GPUQuerySet.destroy()` releases instead of destroying,
`wgpuTextureGetTextureBindingViewDimension` (unimplemented upstream) is
never called, and a command buffer that failed validation at `finish()`
or was already submitted is never passed to `wgpuQueueSubmit`.

### Not yet implemented

Real timestamp queries (accepted, but they record nothing), compilation
messages from `getCompilationInfo()`, external textures
(`importExternalTexture`), and `mapAsync` that resolves asynchronously
rather than by polling. Some three.js shaders use WGSL that naga rejects;
the pipeline then reports a validation error and the affected pass is
skipped.

## Architecture & security notes

- The implementation talks to the standard multi-vendor `webgpu.h` C ABI, so
  it is backend-agnostic; wgpu-native selects a backend (Vulkan / Metal /
  D3D12 / GL) at runtime and falls back to software (llvmpipe / lavapipe)
  when no GPU is present.
- wgpu-native is, by a wide margin, the largest dependency the project can
  pull in (tens of MB, plus a Rust toolchain to build from source) and is
  not auditable by a single maintainer. This is the core reason it stays
  opt-in and out of the default build.
- Real (hardware) GPU backends need device access and a large driver attack
  surface that the sandboxed renderer otherwise denies; running WebGPU
  against a hardware backend inside the sandbox is an open design question
  (a GPU-broker process is the likely answer). The software backend used for
  development does not need device access.

## Quick check

```sh
LD_LIBRARY_PATH=/path/to/release/lib \
  ./builddir/src/gtk/nordstjernen --headless --dump=none --enable-webgpu \
  --eval='navigator.gpu.requestAdapter().then(a=>a.info.device)' about:blank
```
