# WebGPU (experimental, opt-in)

Nordstjernen has an **experimental, off-by-default** WebGPU
(`navigator.gpu`) implementation. Unlike WebGL — which is in the standard
build and mapped onto the in-tree GLES path — WebGPU is layered on the
external [wgpu-native](https://github.com/gfx-rs/wgpu-native) library and is
**not part of the default build**. It exists for experimentation and is kept
strictly behind a build flag and a runtime gate because wgpu-native is a
large dependency that does not fit the minimalism of the rest of the engine.

With the feature disabled (the default), the engine contains **no** WebGPU
symbol, code, or dependency: `navigator.gpu` is simply `undefined`.

## Building with WebGPU

WebGPU is gated by the `webgpu` meson feature, which is `disabled` by
default. Enabling it requires the wgpu-native library and headers.

1. Get a wgpu-native release for your platform from
   <https://github.com/gfx-rs/wgpu-native/releases> and extract it. A
   release contains `lib/libwgpu_native.{so,a}` and
   `include/webgpu/{webgpu.h,wgpu.h}`. The C API headers are also vendored
   in-tree under `third_party/wgpu-native/` (pinned to the supported
   release — currently **v29.0.0.0**); only the library is fetched
   externally.

2. Configure with the feature enabled, pointing at the extracted release:

   ```sh
   meson setup builddir -Dwebgpu=enabled -Dwgpu_native_root=/path/to/wgpu-native-release
   meson compile -C builddir
   ```

   If your distribution ships a `wgpu_native` pkg-config file, meson finds
   the library automatically and `-Dwgpu_native_root` is unnecessary.

   At runtime the shared library must be on the loader path (e.g.
   `LD_LIBRARY_PATH=/path/to/release/lib`) unless it is installed
   system-wide.

The headers are pinned in-tree so the binding always compiles against a
known API version; to move to a newer wgpu-native, replace the two headers
under `third_party/wgpu-native/include/webgpu/` with the matching release
and rebuild.

## Runtime gating

Even in a WebGPU-enabled build, the API is **denied by default**.
`navigator.gpu.requestAdapter()` resolves to `null` unless the environment
variable `NS_WEBGPU_ALLOW=1` is set. This mirrors the opt-in posture of
WebGL (off until trusted) and keeps the large native GPU stack dormant
unless explicitly requested.

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
- `GPURenderPassEncoder` — `end()`. `setPipeline`/`setBindGroup`/
  `setVertexBuffer`/`draw`/… are accepted as no-ops (see below).
- `GPUCommandBuffer`.

A configured canvas owns an offscreen target texture
(`RENDER_ATTACHMENT | COPY_SRC`); each paint, the renderer copies it back
(`copyTextureToBuffer` → map → cairo `ARGB32`, BGRA/RGBA aware,
opaque/premultiplied alpha aware) exactly like the WebGL compositor. So a
page that clears its canvas via a render pass shows the GPU-produced result.

Promise-returning calls (`requestAdapter`, `requestDevice`, buffer mapping)
are resolved by polling wgpu-native's event loop synchronously, so
`await navigator.gpu.requestAdapter()` works without integrating with the
page event loop.

### Not yet implemented

Geometry rendering is not wired up: shader modules (WGSL), render / compute
**pipelines**, bind groups / bind-group layouts, vertex/index buffers as
draw sources, samplers, and non-color (depth/stencil) attachments are not
bound. The render-pass draw calls (`setPipeline`, `draw`, …) are accepted
but do nothing, so a page that sets up a full pipeline renders only its
clear color rather than its geometry. WGSL is available from wgpu-native's
`naga` compiler and becomes useful once pipelines are bound — that is the
next phase. This document and feature-detection reflect exactly what runs.

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
LD_LIBRARY_PATH=/path/to/release/lib NS_WEBGPU_ALLOW=1 \
  ./builddir/src/gtk/nordstjernen --headless --dump=none \
  --eval='navigator.gpu.requestAdapter().then(a=>a.info.device)' about:blank
```
