# WebGL

Nordstjernen ships a **minimalist, opt-in WebGL implementation**. It maps
the WebGL API more or less directly onto GL through a toolkit-independent
**offscreen GL context** (`src/glctx.c`) and **libepoxy** for GL dispatch.
The backend is platform-specific: on Linux it is a surfaceless EGL context
bound to the OpenGL ES API (preferring `EGL_MESA_platform_surfaceless`),
and on Windows it is a desktop-OpenGL context created via WGL on a hidden
popup window. WebGL renders into an FBO and the result is read back and
composited by the page painter, so on the EGL path it needs no window, no
display server, and no GTK — it works in the out-of-process renderer and in
GTK-less builds. There is no ANGLE layer and no GL
command-stream validator between the page and the driver: WebGL calls
become GL calls.

Because that is a deliberately thin bridge to the GPU driver, WebGL is
**off by default** and **gated per site** behind an explicit permission
prompt, and the data-transfer entry points are bounds-checked and
zero-initialised (see [Security](#security)).

## Enabling WebGL

The first time a page calls `canvas.getContext("webgl")` (or `"webgl2"` /
`"experimental-webgl"`), Nordstjernen shows a modal dialog:

> **Enable WebGL?**
> This page wants to use WebGL (hardware-accelerated 3D graphics) on
> `<origin>`. WebGL hands the page near-direct access to your GPU driver.
> Only allow it on sites you trust.
>
> &nbsp;&nbsp;**[ Block ]**&nbsp;&nbsp;**[ Allow and trust this site ]**

- **Block** — `getContext` returns `null`, exactly as if WebGL were
  unavailable. Most sites fall back to a 2D canvas or a static image.
- **Allow and trust this site** — opens a second, confirming dialog:
>
> > **Are you sure?**
> > Give `<origin>` near-direct access to your GPU driver? This stays
> > enabled for this origin for the rest of the session.
> >
> > &nbsp;&nbsp;**[ Cancel ]**&nbsp;&nbsp;**[ Enable WebGL ]**
>
  Enabling WebGL therefore always takes **two deliberate clicks** — "Allow
  and trust this site" then "Enable WebGL". Only when both are confirmed is
  a GL context created and the decision remembered for the rest of the
  session; the first time you confirm, the `webgl_enabled` config flag is
  also flipped to `true` and saved. **Cancel** (or **Block**) leaves WebGL
  off for the origin.

The prompt is driven by the **host**, not the engine. WebGL runs in the
out-of-process renderer; when a page asks for a context on an
undecided origin, the renderer reports the origin to the shell over the IPC
control channel (an `X-WebGL` response header, mirroring `X-Nav`). The shell
shows the dialog in its own toolkit, sends the decision back (the `/webgl`
control command), and reloads the page so `getContext` sees the answer.
`src/webgl.c` itself contains no toolkit code.

Decisions are cached per **origin** for the session, so a site is asked at
most once. In headless runs (`--dump` / `--screenshot`) there is no user to
prompt, so the decision defaults to allow (set `NS_WEBGL_ALLOW=0` to
override). A host that does not handle the request at all (e.g. a minimal
embedder) leaves WebGL **denied** unless `NS_WEBGL_ALLOW=1` is set.

## Config

`webgl_enabled` (boolean, default `false`) lives in the normal flat config
file alongside the other preferences (see `ns_config`). It records that the
user has enabled WebGL at least once. The per-site trust prompt still
governs which origins may actually create a context.

## How it works

`src/webgl.c` is the whole implementation; it is compiled into the engine
only when `NS_ENABLE_WEBGL` is defined (every desktop build — Linux, macOS,
Windows), and runs in the engine wherever it is hosted, including the
out-of-process renderer. The Android engine build gets a stub that returns
`null`. The GTK trust dialog is the only GTK-specific part and is compiled
in only when `NS_HAVE_GTK` is defined (the GTK shell).

1. **Context** — `ns_gl_context_create()` (`src/glctx.c`) creates the
   offscreen GL context. On Linux this is a surfaceless **EGL** context
   bound to the OpenGL ES API, preferring `EGL_MESA_platform_surfaceless`
   so it needs no display server, with a 1×1 pbuffer fallback where
   surfaceless current is unsupported; GLES is requested so GLSL ES shaders
   (`#version 100` / `#version 300 es`) compile natively, no translation.
   On Windows it is a desktop-OpenGL context created via **WGL** on a
   hidden popup window, where GLSL-ES shaders rely on the desktop driver
   accepting them. On macOS it is a **CGL** context on the GL 4.1 core
   profile (Metal-backed): its compiler accepts GLSL-ES `#version 100`, so
   `src/webgl.c` prepends `#version 100` when a shader omits a version
   directive, and `glctx.c` binds a default vertex array because the core
   profile has no usable object 0 — together these give working **WebGL 1**.
   WebGL 2 is not advertised on macOS (`getContext('webgl2')` returns `null`,
   so pages fall back to WebGL 1): Apple's OpenGL caps at 4.1 and lacks the
   ES-3 shader compatibility WebGL 2 requires.
2. **Offscreen render target** — the context renders into an offscreen
   framebuffer object (FBO) sized to the canvas, with a colour texture and,
   when requested, a depth/stencil renderbuffer.
3. **Antialiasing** — when `antialias` is requested (the default) and the
   driver supports multisampling, drawing goes to a 4× multisample FBO that
   is resolved with `glBlitFramebuffer` before readback.
4. **Compositing** — when the page draws, the FBO is read back with
   `glReadPixels`, flipped vertically, alpha-premultiplied, and written into
   a Cairo image surface. The existing canvas paint path
   (`src/paint.c`) blits that surface like any other canvas. Readback only
   happens after the page actually draws (a dirty flag), so a static
   one-shot render stays on screen and `requestAnimationFrame` loops update
   every frame.

WebGL objects (`WebGLBuffer`, `WebGLTexture`, `WebGLProgram`,
`WebGLShader`, `WebGLFramebuffer`, `WebGLRenderbuffer`, vertex arrays,
samplers) are thin JS wrappers around the underlying GL integer name.
`WebGLUniformLocation` wraps the `GLint` location.

## Supported API

This is a pragmatic core, not a conformance-complete implementation.

### WebGL 1

- Context attributes: `alpha`, `depth`, `stencil`, `antialias`,
  `premultipliedAlpha`, `preserveDrawingBuffer` (parsed and honoured),
  plus `getContextAttributes()`.
- State: `enable`/`disable`/`isEnabled`, `clearColor`/`clearDepth`/
  `clearStencil`/`clear`, `viewport`, `scissor`, `colorMask`, `depthFunc`/
  `depthMask`/`depthRange`, `blend*`, `stencil*`, `cullFace`/`frontFace`,
  `lineWidth`, `polygonOffset`, `hint`, `pixelStorei` (including
  `UNPACK_FLIP_Y_WEBGL` / `UNPACK_PREMULTIPLY_ALPHA_WEBGL`), `finish`,
  `flush`, `getParameter`, `getError`.
- Shaders & programs: `createShader`, `shaderSource`, `compileShader`,
  `getShaderParameter`/`getShaderInfoLog`/`getShaderSource`,
  `createProgram`, `attachShader`/`detachShader`, `linkProgram`,
  `useProgram`, `validateProgram`, `getProgramParameter`/
  `getProgramInfoLog`, `bindAttribLocation`, `getAttribLocation`,
  `getUniformLocation`, `getActiveAttrib`/`getActiveUniform`.
- Buffers: `createBuffer`, `bindBuffer`, `bufferData`, `bufferSubData`,
  `deleteBuffer`, `getBufferParameter`, `isBuffer`.
- Vertex attributes: `enableVertexAttribArray`,
  `disableVertexAttribArray`, `vertexAttribPointer`, `vertexAttrib[1-4]f`,
  `vertexAttrib[1-4]fv`, `getVertexAttrib`, `getVertexAttribOffset`.
- Uniforms: `uniform[1-4]f`, `uniform[1-4]i`, `uniform[1-4]fv`,
  `uniform[1-4]iv`, `uniformMatrix[2-4]fv`.
- Drawing: `drawArrays`, `drawElements`.
- Textures: `createTexture`, `bindTexture`, `activeTexture`,
  `texParameteri`/`texParameterf`, `generateMipmap`, `texImage2D`,
  `texSubImage2D` — from a typed array, from `ImageData`, and from an
  `<img>` / `<canvas>` / `ImageBitmap` source.
- Framebuffers / renderbuffers: full `*Framebuffer*` and `*Renderbuffer*`
  set, `checkFramebufferStatus`, `readPixels`. Binding framebuffer `null`
  binds the canvas drawing buffer (our FBO), not GL's default framebuffer.
- Queries: `isProgram`, `isShader`, `isTexture`, `isFramebuffer`,
  `isRenderbuffer`, `getTexParameter`, `getRenderbufferParameter`,
  `getFramebufferAttachmentParameter`.

### WebGL 2 (when `getContext("webgl2")`)

Everything above, plus:

- Vertex array objects (`createVertexArray` …), instanced drawing
  (`drawArraysInstanced`, `drawElementsInstanced`, `vertexAttribDivisor`),
  `drawBuffers`, `drawRangeElements`, `vertexAttribIPointer`,
  `vertexAttribI4{i,ui,iv,uiv}`.
- Unsigned-integer uniforms (`uniform[1-4]ui` / `uniform[1-4]uiv`) and
  non-square matrices (`uniformMatrix{2x3,3x2,2x4,4x2,3x4,4x3}fv`).
- 3D / 2D-array textures: `texImage3D`, `texSubImage3D`, `texStorage2D`,
  `texStorage3D`, `copyTexSubImage3D`; plus `copyTexImage2D` /
  `copyTexSubImage2D`.
- Framebuffers: `renderbufferStorageMultisample`, `blitFramebuffer`,
  `framebufferTextureLayer`, `invalidateFramebuffer`, `readBuffer`.
- Buffers: `copyBufferSubData`, `getBufferSubData` (via `glMapBufferRange`),
  `clearBuffer{fv,iv,uiv,fi}`, `bindBufferBase`, `bindBufferRange`.
- Sampler objects, uniform-block introspection / binding
  (`getUniformBlockIndex`, `uniformBlockBinding`, `getActiveUniforms`,
  `getActiveUniformBlockParameter`, `getActiveUniformBlockName`),
  `getFragDataLocation`, `getInternalformatParameter`.
- Query objects (`createQuery`, `beginQuery`/`endQuery`,
  `getQueryParameter`, …), transform feedback (`createTransformFeedback`,
  `transformFeedbackVaryings`, `begin`/`end`/`pause`/`resume`), and sync
  objects (`fenceSync`, `clientWaitSync`, `waitSync`, `getSyncParameter`).
- The WebGL 2 enum constants.

## Security

The original cut shipped with "no security measures"; this is the
hardened model. WebGL is a thin bridge to the GPU driver, so the
mitigations focus on the parts a hostile page can actually reach.

- **Per-site consent** — WebGL is off until the user explicitly allows it
  for an origin (see above). No prompt, no context.
- **No JS-heap out-of-bounds.** Every pixel-transfer entry point validates
  the caller-supplied `ArrayBufferView` against the exact number of bytes
  GL will touch — computed from width/height/depth, format, type and the
  live `PACK`/`UNPACK` pixel-store state. That byte count is computed with
  overflow-checked arithmetic, so hostile width/height/depth or `pixelStorei`
  (`UNPACK_ROW_LENGTH`, `UNPACK_IMAGE_HEIGHT`, `SKIP_*`) values can't wrap it
  to a small number that slips past the bounds check. `readPixels` into an
  undersized buffer is skipped (no write past the view); `texImage2D` /
  `texImage3D` / `texSubImage*` from an undersized array are skipped (no read
  past it).
- **No uninitialized GPU memory disclosure.** `bufferData(size)` and
  `texImage2D/3D(…, null)` upload zero-filled storage instead of leaving
  the contents undefined, so a page can't read back stale VRAM.
- **Buffer-range checks on raw copies.** `bufferSubData` (JS → buffer) and
  `getBufferSubData` (buffer → JS, via `glMapBufferRange`) validate that
  `srcByteOffset + length` lands inside the bound buffer — offset
  non-negative, the add overflow-checked in 64-bit, the sum within
  `GL_BUFFER_SIZE` — before touching GPU memory. This doesn't trust the
  driver to reject an out-of-range map/copy, so a hostile offset can't be
  used to write past or read stale bytes beyond the buffer.
- **Index-buffer bounds on indexed draws.** `drawElements`,
  `drawElementsInstanced` and `drawRangeElements` validate that the index
  range (`offset + count × index-size`) fits inside the currently bound
  `ELEMENT_ARRAY_BUFFER`, with the multiply/add done in overflow-checked
  64-bit. A draw with no index buffer bound, a bad index type, or a range
  that runs off the end of the buffer is dropped instead of handed to the
  driver, so a hostile `count`/`offset` can't make the GPU read indices
  past the buffer. Negative `first`/`count`/`instanceCount` on any draw
  call are likewise rejected.
- **Memory-safe texture decoding.** Texture uploads from a DOM source
  (`<img>` / `<canvas>` / `ImageBitmap`) decode through the same
  Wuffs-first image path as the rest of the engine
  (`ns_image_decode_bytes`): Google's memory-safe Wuffs decoder handles
  PNG/GIF/BMP/JPEG, libwebp handles WebP and libavif AVIF, with
  GDK-Pixbuf and librsvg only as fallbacks. The
  untrusted image bytes never touch a hand-rolled decoder before becoming
  texels. Raw typed-array uploads stay bounds-checked as above.
- **Shader source size cap.** `shaderSource` is bounded (4 MiB) and the
  source is handed to the driver with an explicit length, so an embedded
  NUL can't truncate the shader and a pathologically large string can't be
  used to exhaust memory in the driver's compiler.
- **Allocation + context caps.** A single buffer/texture upload is capped
  (1 GiB) and the number of simultaneous live contexts is capped (32) to
  bound memory and FD/context exhaustion. Canvas dimensions are clamped.
- **Reduced fingerprinting.** `getParameter(VENDOR)` returns a generic
  `"Nordstjernen"` string and `RENDERER` returns `"Nordstjernen WebGL"`,
  and `VERSION` /
  `SHADING_LANGUAGE_VERSION` return the WebGL-style version, not the raw
  driver string. There is no `WEBGL_debug_renderer_info` extension, and
  `getSupportedExtensions` returns an empty list. The numeric capability
  limits that fingerprinting libraries probe — `MAX_TEXTURE_SIZE`,
  `MAX_VIEWPORT_DIMS`, `MAX_VERTEX_ATTRIBS`, the uniform/varying/texture-unit
  maxima, the WebGL 2 block and component limits, and the aliased
  line/point-size ranges — are clamped to fixed common values
  (`wgl_param_cap` in `src/webgl.c`), so high-end and exotic GPUs report the
  same numbers as the mainstream. `COMPRESSED_TEXTURE_FORMATS` is reported as
  empty. Clamping only ever lowers a reported limit, so it never makes the
  driver promise capacity it does not have.
- **No JS-controlled stack write in `getParameter`.** An unrecognised
  `pname` falls through to `glGetIntegerv` into a fixed stack buffer; that
  buffer is sized with generous headroom (32 ints) so a caller-supplied
  enum that returns a multi-valued result can't write past it.
- **No raw GPU handles in JS.** Sync objects (`GLsync` pointers) are handed
  out as opaque session ids via an indirection table, not pointer values.

What is **not** done: there is no full GL command-stream validator (the
ANGLE model). Index-buffer bounds are now checked, but vertex-attribute
range validation (does the largest index fit every enabled attribute
array?) and shader resource limits are still left to the driver. So is
"robust resource initialization": storage allocated by `texStorage2D/3D`
and `renderbufferStorage[Multisample]` is left in the driver's hands, so a
page that attaches a never-cleared renderbuffer or immutable texture to
its own framebuffer and reads it back gets whatever the driver hands out
(zero on the security-conscious drivers, undefined per spec). The browser
clears its own canvas drawing buffer, but does not yet zero every
page-allocated attachment. These gaps are why WebGL stays gated behind the
per-site trust prompt.

## Limitations

- No WebGL extensions: `getExtension()` returns `null` and
  `getSupportedExtensions()` returns `[]`.
- `texImage2D` from a DOM source decodes through the same surface path the
  2D canvas `drawImage` uses, so cross-origin restrictions and decode
  support match the rest of the engine.
- No `webglcontextlost` / restore events — `isContextLost()` is always
  `false`.
- Antialiasing depends on the driver advertising `GL_MAX_SAMPLES > 1`.

These are deliberate: the goal is a small, readable bridge that runs the
common WebGL content, not a full conformance suite.

## Trying it

```sh
meson compile -C builddir
./builddir/src/gtk/nordstjernen path/to/your-webgl-page.html
```

Allow WebGL when prompted. A minimal smoke test page is a hello-triangle:
create a vertex + fragment shader, link a program, upload three vertices,
and `drawArrays(gl.TRIANGLES, 0, 3)`.
