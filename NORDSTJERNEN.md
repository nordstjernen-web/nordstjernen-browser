# Nordstjernen — Development Plan

Living plan for a clean-room web browser written from scratch in **C**,
small enough for one person to audit end-to-end. The engine and all
non-toolkit logic live as portable C in `src/`; the GUI is a thin,
interchangeable frontend — **GTK 4** is the reference and **Qt 6** is an
experimental second frontend, both over the same shared C and **libcurl**.
No upstream engine (Gecko / WebKit / Blink) is read, ported, or imported.
See `README.md` for the product vision and `CLAUDE.md` for working rules.

## Principles

- **One competent human's worth of code** — when work balloons, cut
  scope, not corners. A working subset beats an unfinished superset.
- **Vertical slices that ship** — every task ends in something runnable.
- **Few small auditable deps**, vendored in-tree: lexbor (HTML + WHATWG
  URL), QuickJS (JS interpreter), Wuffs (image decode), WAMR
  (WebAssembly interpreter).
- **No automated test suite** — verify by running the browser.
- No JIT, therefore more secure. However, this means the rest of the browser
  engine needs to be super-fast. 
- **No code comments** beyond one header line per file (see `CLAUDE.md`).

## Non-goals (won't change)

WebGPU / WebRTC / WebUSB / WebBluetooth / WebMIDI / WebHID;
service-worker network interception, push, background sync (the
registration + lifecycle subset of Service Workers *is* supported);
DRM / EME; **JIT** (QuickJS
interpreter only — W^X holds process-wide); plugins (NPAPI / PPAPI /
WebExtensions); sync / accounts / telemetry /
"studies"; localization beyond English (for now).

WebGL is the one exception to the no-GPU-APIs stance: a minimalist,
opt-in WebGL 1 / 2 implementation mapped directly onto OpenGL ES, off by
default and gated by a per-site trust prompt (see `docs/webgl.md`).
WebGPU remains a non-goal.

## Current state

A usable HTML5 + modern-CSS + ~ES2020 browser. lexbor parses to a DOM;
the engine does the CSS cascade and selectors, block/inline/flex/grid/
table/multicol/float/positioned layout, and Cairo + Pango paint
(gradients incl. `repeating-*` and gradient masks, filters, transforms,
transitions / `@keyframes`). QuickJS bindings cover DOM, Shadow DOM,
`fetch`/XHR, canvas 2D, storage (`localStorage`/`sessionStorage`,
IndexedDB over SQLite), `WebSocket`/`EventSource`, the
Resize/Intersection/Mutation observers, and `crypto.subtle`
(WebCrypto over OpenSSL, `src/webcrypto.c`); forms support submission
and constraint validation; `overflow` boxes scroll. The full
`WebAssembly` JS API runs on a vendored WAMR interpreter
(`src/wasm.c`), and an opt-in, per-site-gated WebGL 1 / 2 maps onto
OpenGL ES (`src/webgl.c`). Painting skips off-screen boxes (viewport
culling). Runs on Linux, Windows (MSYS2) and macOS, with an Android
port in progress; CI builds the desktop three plus musl on every push.
Both the GTK reference frontend and the experimental Qt 6 frontend
(`src/qt/`, off by default) are tabbed, **process-per-tab** browsers:
each tab drives its own sandboxed renderer process over the engine and
shows full-fidelity output. The shells are thin display/input clients —
there is no in-process renderer in either toolkit anymore. The
`about:start` new-tab page hosts a local AI assistant (`src/ai.c`,
llama.cpp over a pinned Meson subproject): chat, Wikipedia/DuckDuckGo
tools, and digest-pinned model downloads, all on-device with no network
at inference time (see `docs/ai.md`).

Version 1.0.6 is the current release (the tree carries 1.0.7-dev in
the meson project definition — surfaced through `src/version.h` — as
development advances toward the next tag).

## Architecture & frontends

The codebase is layered so the GUI stays thin and the engine stays
toolkit-agnostic:

- **`src/` — common C core.** The engine (lexbor parse, CSS cascade,
  layout, Cairo/Pango paint, QuickJS, WAMR, image decode, networking)
  plus frontend-agnostic `ns_` helpers shared by alternative GUIs
  (`htmlbox`, `fetch`, `url`, `page`, `jsrun`, `media`). New shared logic
  lands here in C, in the house style (`ns_` snake_case, one-line SPDX
  header, no comments).
- **`src/gtk/` — GTK 4 frontend (reference).** A thin process-per-tab
  shell (`appmain` entry point, `procwindow`/`procview` over `rproc_http`)
  that spawns one sandboxed `nordstjernen-renderer` process per tab and
  blits its framebuffer. It carries the browser chrome — navigation,
  tabs, history, zoom, selection, `:hover`, find-in-page, a context menu,
  the DevTools console, save/export, media handoff, an app menu, settings,
  and bookmarks. The former in-process engine renderer has been removed.
- **`src/qt/` — Qt 6 frontend (experimental, off by default).** A thin
  C++ shell that links only Qt + `rproc_http.c` (no engine, no GTK), built with
  `-Dqt=enabled`. It is a tabbed browser driving one sandboxed renderer
  process per tab, so every tab shows full engine output; it matches the
  GTK shell on core browsing chrome (navigation, tabs, zoom, selection,
  hover, find, context menu) with the rest in progress. Qt is a C++-only
  toolkit, so the shell is C++; the logic underneath stays C in `src/`.
- **`java/` — Java / JVM binding and Swing app.** A Java library
  (`org.nordstjernen.Nordstjernen`, JDK 21) embeds the engine on the JVM
  through a thin JNI bridge over the C embedding API
  (`src/libnordstjernen.h`), with a no-JNI `RemotePage`/`RemoteBrowser`
  client that drives a separate `nordstjernen-renderer` process over the
  renderer's HTTP/JSON protocol. `org.nordstjernen.app.Browser` is a
  standalone Swing browser app built on that client with GTK-shell-style
  chrome. See `java/README.md`.
- **`android/` — Android port (in progress).** A Kotlin shell
  (`MainActivity`, `PageView`, `NativeBrowser`) over the same engine via
  JNI; see `docs/Android.md`.

**Process-per-tab renderer boundary — shipped.** Each tab now runs in
its own sandboxed *process* running the engine and producing a rendered
surface (see `docs/tab-isolation.md`), with the GUI process reduced to
hosting a widget per tab, blitting the framebuffer, and forwarding input.
This is the payoff that makes the whole design cohere:

- **Very thin GUIs.** Both GTK and Qt become display-plus-input shells;
  the toolkit — and its language — stops mattering to rendering.
- **Both frontends equally good.** They show the *same* engine output, so
  fidelity is identical and the Qt side needs no separate renderer to
  reach parity.
- **Real isolation.** Per-process seccomp/Landlock plus crash isolation —
  security threads cannot provide. The pieces already exist:
  `libnordstjernen` (`ns_browser_render_rgba`, `ns_browser_link_at`),
  headless RGBA rendering, the per-process sandbox, and the fork-based
  media broker as an IPC pattern.

This work is now real in `src/`: shared-memory framebuffer, control-channel
IPC, POSIX/Windows renderer spawn paths, dirty-rect rendering, async Qt
dispatch, and bounded IPC replies. The renderer process sandboxes itself —
`nordstjernen-renderer` applies the same Landlock + seccomp confinement as the
main process (via `ns_browser_sandbox`) after the `HELLO` handshake and before
opening any page, so untrusted content runs under a loaded syscall filter. And
**both frontends are now process-per-tab, full stop**. `nordstjernen-qt`
and `nordstjernen` (GTK) each run a tabbed shell (`procwindow`/`procview`)
where every tab owns its own sandboxed renderer process, making a tab one
engine process and a renderer crash a per-tab failure. There is no
in-process renderer in either toolkit. The shells skip their own
seccomp/Landlock (they must `fork`/`execv` renderers and create POSIX
shm); the security boundary lives in the renderer processes, which sandbox
themselves before opening any page.

**The plan from here** (the explicit near-term direction):

1. **The IPC renderer is the renderer.** The proc views load, render,
   scroll (wheel + keyboard + scrollbars), reflow on resize, hit-test
   links, open links in new process-tabs, and recover from renderer
   crashes; JavaScript stays live (the renderer pumps the bundled QuickJS
   each frame via `ns_browser_tick`, budget `NS_TICK_MS`, default 16 ms),
   and there is a continuous frame loop while a page animates. The GTK
   shell carries the full chrome — toolbar, address bar, status bar,
   tabs/history, zoom, text selection, `:hover` + pointer events,
   find-in-page, a right-click context menu, the DevTools console,
   save/export, media handoff, an app menu, settings, and bookmarks — and
   the Qt shell matches the core browsing chrome. The IPC protocol
   (`rproc_http`) has grown to carry all of this: render/viewport, click/key/
   hover/select, find, export, media, console/eval.
2. **Browser-process broker services** (networking, cookies, cache,
   storage) so the renderer can be credential-less rather than fetching
   and persisting on its own — the true security payoff.

**Sandbox / watchdog in proc mode** (see `docs/tab-isolation.md`): the
renderers carry the real Landlock + seccomp confinement; the thin shell, which
parses no untrusted bytes but must spawn renderers and create POSIX shm, runs
under a widened Landlock (writable `/dev/shm`, executable renderer dir) with
seccomp skipped, and with watchdog supervision disabled (per-tab renderer
crash-recovery replaces it). A shell-specific seccomp profile or a renderer
zygote, and re-enabled shell supervision, are the follow-ups.

## Priorities

**Now**
- **18 · CSS cascade performance** — the biggest "feels slow" lever.
  Incremental/partial re-cascade of dirtied subtrees plus a conservative
  computed-style sharing cache. A full r/news-scale cascade is ~0.5 s
  today and the post-hydration re-cascade dominates SPAs. Land as small,
  measured commits — profile before/after; this area regresses easily
  (prior per-element class-hashset attempts both regressed).

**Next**
- **19 · Incremental paint** — viewport culling shipped; the real win is
  dirty-region partial repaint (re-rasterize only changed rects), which
  also unblocks animated smooth scrolling. A retained full-document
  surface cache was tried and reverted (re-rastered the whole page on
  every change); smooth wheel scrolling was reverted for the same reason
  (full repaint × ~15 ease-frames saturated CPU). Both need dirty rects.
- **14 · YouTube watch-page playback** — baseline shipped: clicking a
  streaming `<video>` (MSE/`blob:`, no file URL) hands the *page* URL to
  the external player, which resolves it with yt-dlp
  (`ns_media_launch_external`, `stream=TRUE`). Next: detect more sites and
  surface a clearer in-page affordance; optionally run the `base.js`
  signature-cipher transform in QuickJS to get a direct URL without
  yt-dlp.
- **8 · Sign the Windows build** — biggest distribution-side ROI. Wire
  signtool + timestamp now; flip on once a cert is procured.
- **11 · Flathub packaging** — canonical Linux install path. The
  `.desktop` file now installs from meson (as
  `com.nordstjernen.Browser.desktop`); the remaining blocker is a
  missing AppStream `metainfo.xml`.

**Later**
- 5 · Reader mode · 6 · APNG playback · 9 · macOS notarized DMG.

## Future focus areas

Candidate directions once the performance work above lands — none
committed, listed to keep the long view in one place:

- **Layout & paint throughput** — the dirty-region partial repaint and
  incremental re-cascade in *Now/Next* are the headline wins; both
  unblock smooth animated scrolling, which has been reverted twice for
  want of them. Treat them as the gating performance work.
- **Frame-loop idling / animation throttling** *(partially landed)* —
  quiet-page idling shipped: the renderer reports `X-Anim` from
  `ns_browser_animating`, the shell stops requesting frames once a page
  is quiet (6a519a6, 6d598fc), and a tick that ran no work reuses the
  previous frame (`X-Unchanged`, staged-surface reuse in the shell) —
  safe because it skips only when *nothing* ran, unlike the reverted
  coarse `frame_dirty` flag that skipped after no-op rAFs and caused
  judder; don't re-attempt that version. The reflow-loop dampener now
  *defers* suppressed relayouts instead of discarding them (51659ba) —
  pending JS work must always count as animating or the page freezes.
  What remains is the rAF-pinned SPA case (the full `duckduckgo.com`
  spinner still burns CPU because its rAF genuinely runs every frame):
  that needs per-region paint invalidation (the dirty-region work above)
  and, for transform/opacity spinners, compositor-thread animation — a
  larger architectural effort. The re-entrant-relayout leak was a
  distinct bug, fixed in 80ca0d5.
- **Worker maturity** — dedicated workers are partial today (§10). Round
  out `postMessage` structured clone, transferables, and module workers
  so wasm-bindgen + worker bundles run unmodified.
- **`iframe` rendering** — `sandbox` is parsed and enforced, but nested
  document rendering is still limited. Real child-document layout/paint
  would close one of the larger remaining HTML gaps.
- **Image animation** — APNG/animated-WebP playback (GIF animation
  already works through the Wuffs path).
- **Accessibility** — no AT-SPI / accessibility tree is exposed yet;
  a minimal accessible-name + role surface would be a high-value,
  self-contained slice.
- **WebGL extensions** — `getExtension()` returns `null` today; a small
  allow-list of widely-used, safe extensions (e.g. instancing,
  `OES_*` float textures) would unblock more content without
  re-architecting `src/webgl.c`.
- **Packaging reach** — Flathub (blocked only on AppStream
  `metainfo.xml`), a signed Windows build, and a notarized macOS DMG
  are the distribution-side levers.

**Done:** process-per-tab renderers behind the IPC +
shared-memory-framebuffer boundary (both GTK and Qt are thin display
clients now; in-process renderers removed — see *Architecture &
frontends* and `docs/tab-isolation.md`),
10 embeddable `libnordstjernen` (built and header-installed
from meson, see `docs/Embedding.md`; Java JNI binding and Swing app in
`java/`, see `java/README.md`),
15 Debian/Ubuntu `.deb` packaging (built nightly, see `docs/Nightly.md`).

**Ongoing — security hardening passes.** Recurring source audits of the
attacker-reachable surface (network parsing, cookie scoping, layout
allocation, the renderer/IPC boundary). Landed so far: `document.cookie`
`Domain` attributes that name a public suffix are now rejected via libpsl
(matching the network jar and the WHATWG cookie rules); the auto/fixed
table layout caps accumulated column counts (`NS_TABLE_MAX_COLS`) so a
colspan-packed table can't overflow the `guint` column counter and
under-size the collapse-borders grid; `http_read_body`/`http_skip_body`
reject negative lengths before the copy loop; HTTP requests set
`CURLOPT_UNRESTRICTED_AUTH = 0` so credentials are never replayed to a
different host on a redirect; the renderer/IPC favicon reply now clamps
the `X-W`/`X-H`/`X-Stride` dimensions before multiplying them, so a
crafted reply can't integer-overflow the size check and have the shell
read past a bounded buffer (the `/render` path already clamped against
the session's `max_w`/`max_h`); and the CSS engine caps the recursive
parsers reachable from untrusted stylesheets — `var()` fallbacks,
`@supports`, `color-mix()`, and `@media … or …` all carry depth limits,
`ns_css_value_free` frees the background-layer chain iteratively, and
`filter: blur()` clamps its radius — so a deeply nested or absurdly
large value can't exhaust the stack or overflow the blur window. The
broader fundamentals stay strong:
http/https-only scheme allow-listing with no HTTPS→HTTP redirect
downgrade, CRLF-filtered request headers, percent-encoded multipart
field names, and per-renderer Landlock + seccomp confinement applied
before any page is opened.

**Mostly done / partial:** 12 downloads under Landlock (path 2),
13 fragment navigation, 16 silent-stub honesty, 17 GTK renderer
selection.

## How we work

meson + ninja, with `ccache` for fast rebuilds. Build and smoke-launch
locally before pushing — the local machine is the build and run oracle.
Commit small; push logical units to `origin/main` as they land.
