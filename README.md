Nordstjernen web browser
========================

Nordstjernen is a web browser written from scratch in C,
focused on support for modern HTML, CSS and JavaScript standards.

![Nordstjernen showing a Wikipedia article in the light theme](docs/screenshot.png)

Supported platforms:
* [Windows](https://apps.microsoft.com/detail/9nw8t7w5z4pl)
* MacOS
* Linux
* FreeBSD and NetBSD
* [Android](https://play.google.com/store/apps/details?id=org.nordstjernen.WebBrowser)
* Java

**Current release:** **1.0.32** (October 2026) — see [Changelog.md](Changelog.md).

**Standards.** Behaviour is measured against the spec text, section by
section, not against another browser. The walk-through of the in-scope
WHATWG HTML standard (§1–§16) in
[docs/HTML-compatibility.md](docs/HTML-compatibility.md) records **140 spec
rows fully implemented, 31 partial, 0 absent** (July 2026), besides a handful
that are non-goals by design: `embed`/`object` plugins, `frame`/`frameset`,
`applet`/`marquee`, telemetry, and AI-style web APIs.

Conformance is also measured with the shared test suites, run locally
against a static [web-platform-tests](https://github.com/web-platform-tests/wpt)
checkout (October 2026). These are the subtests that pass:

| Suite | Passing |
|-------|---------|
| WPT `css/` (6,991 test files) | 110,540 of 151,048 subtests (73.2 %) |
| WPT `html/`, `dom/`, `url/` and related (4,899 test files) | 151,974 of 166,164 subtests (91.5 %) |
| [test262](https://github.com/tc39/test262) on the QuickJS engine | 82,243 of 82,248 tests run (5 failures) |

The HTML run leaves out the `canvas`, `browsers` and cross-origin
directories. Tests that need the WPT Python server, such as
form-submission echo handlers, cannot pass against a static checkout
and are counted as failures. The test262 count covers the tests run
after the engine's feature exclusions.

**Security.** Each tab's engine runs in its own renderer process behind an
IPC + shared-memory boundary. The renderer is sandboxed with seccomp + Landlock
on Linux and a Seatbelt profile on macOS; on Windows it runs with process
mitigation policies (forced ASLR, strict handle checks, restricted image
loads) and the browser drops administrator rights at start-up. No JIT, in
either the JavaScript or the WebAssembly engine.

**Minimalism.** The core engine is about 245,000 lines of project C and
headers, excluding vendored libraries and generated assets — small enough for
one person to read and audit end-to-end.

See [Northstar](https://github.com/nordstjernen-web/northstar-browser)
for the GPL-licensed sibling project.

<img src="docs/nordstjernen-now.gif" alt="Nordstjernen Now!" width="140">

## Download

| Platform | Download |
|----------|----------|
| Windows | [Windows store](https://apps.microsoft.com/detail/9nw8t7w5z4pl) |
| Android | [Google Play](https://play.google.com/store/apps/details?id=org.nordstjernen.WebBrowser) |
| macOS | notarised `.dmg` on [GitHub Releases](https://github.com/nordstjernen-web/nordstjernen-browser/releases) |
| Debian / Ubuntu | `.deb` packages (Debian 13, Ubuntu 24.04 and 26.04) on [GitHub Releases](https://github.com/nordstjernen-web/nordstjernen-browser/releases) |
| Java/JVM | [`org.nordstjernen:nordstjernen-java`](https://github.com/nordstjernen-web/nordstjernen-browser/packages) on GitHub Packages (Maven) |
| Source | [release tags](https://github.com/nordstjernen-web/nordstjernen-browser/tags) |

Windows 10 or later is required (the GTK 4 frontend links
DirectComposition). Other platforms build from source — see "Build" below
and the per-platform install notes in [docs/](docs/README.md); packaging
scripts for AppImage, RPM, Alpine, the BSDs and MSIX are in `scripts/`.

## Browser features

- **HTML/CSS** — HTML is parsed by the in-tree lexbor engine; the CSS engine
  covers the modern cascade and CSSOM, Media Queries Level 4, container
  queries and units, flex, grid, transforms, gradients, animations and
  vertical writing modes. It also covers CSS Color 4/5 (`oklch()`,
  `color-mix()`, relative colors, `light-dark()`, `contrast-color()`) and
  CSS anchor positioning with `@position-try` fallbacks. CSS Shapes
  (`shape-outside`, `clip-path` basic shapes, `path()` and `shape()`)
  are parsed, and floats do not yet wrap around a shape. Web Animations
  composition (`add`/`accumulate`) works for transforms, filters and
  shapes, and transform lists interpolate through matrix decomposition.
- **Text layout** — desktop builds shape text with **ns-pango**, a Pango fork
  pinned as a meson subproject that caches finished glyph strings and font
  metrics across layouts, so measuring and painting a run reach HarfBuzz once
  instead of three times. Android and iOS link the system Pango;
  `-Dns-pango=disabled` builds that path on the desktop too.
- **JavaScript** on the QuickJS interpreter — DOM, Shadow DOM, observer APIs,
  Canvas 2D (`Path2D`, `ImageBitmap`, `DOMMatrix`), WebCrypto
  (`crypto.subtle` over OpenSSL), custom elements including customized
  built-ins (`is=` / `{extends}`), and the `Navigation` API for single-page
  routing, plus Web Workers, IndexedDB (over SQLite), WebSocket,
  `EventSource` and Web Animations (`Element.animate`). Compiled script
  bytecode is cached on disk between visits. The QuickJS engine is selectable at build time: the in-tree
  quickjs-ng fork by default, or Fabrice Bellard's original QuickJS with
  `-Dquickjs=quickjs` — see [docs/quickjs.md](docs/quickjs.md).
- **Networking** over HTTP/2 with libcurl — HSTS, CSP, subresource-integrity
  checks, partitioned cookies, speculative subresource loading, request
  coalescing and a `Vary`-aware HTTP cache. An in-tree **libnghttp2** transport backend is selectable at build
  time (`-Dhttp_backend=nghttp2`), with **HTTP/3 over QUIC** via ngtcp2 +
  nghttp3 + gnutls when present. Both backends fetch byte-identically, so the
  independent transports cross-check each other. See
  [docs/http-backends.md](docs/http-backends.md).
- **Images and graphics** — Wuffs decodes PNG/APNG, GIF, BMP, JPEG and lossless
  WebP; libwebp handles lossy and animated WebP; ICO and SVG are rendered
  in-engine, with optional AVIF, JPEG XL and inline PDF (poppler) support.
  Web fonts load as TTF/OTF, WOFF and WOFF2 (in-tree decoder over brotli).
- **Media** — `<video>` plays **inline** for MPEG-1 (decoded in-tree by
  [pl_mpeg](https://github.com/phoboslab/pl_mpeg)), **WebM** (VP9/VP8 +
  Opus/Vorbis) through FFmpeg's libav, which Linux and Windows builds require,
  and **MP4 with H.264/AAC**, decoded by the operating system — Media
  Foundation on Windows, VideoToolbox on macOS/iOS, MediaCodec on Android and
  GStreamer on Linux/BSD — never by a bundled decoder. MSE/`blob:` streaming
  and HLS/DASH manifests decode in the renderer too, and a `<track default>`
  WebVTT file is drawn over the video. Audio (MP3 via minimp3, MP2, Opus,
  Vorbis, AAC) plays through a separate `nordstjernen-audio` helper over SDL2.
  Other codecs render a poster and play overlay. See
  [docs/media.md](docs/media.md).
- **Web Audio and capture** — `AudioContext`/`OfflineAudioContext` graphs are
  rendered in-engine; `getUserMedia` reaches the microphone (SDL capture) and,
  on Linux, a V4L2 webcam, behind a per-site permission prompt.
- **WebGL / WebGPU / WebAssembly** — WebGL 1/2 mapped onto OpenGL ES, on by
  default ([docs/webgl.md](docs/webgl.md)); `navigator.gpu` over
  wgpu-native (fetched and linked statically on Linux, macOS and Windows),
  on by default and switchable in Settings ([docs/webgpu.md](docs/webgpu.md)); the full
  WebAssembly JS API over a vendored WAMR interpreter
  ([docs/webassembly.md](docs/webassembly.md)).
- **MathML** — a minimalist presentation-MathML renderer (`src/mathml.c`)
  laid out over Pango/Cairo and embedded inline on the text baseline.
- **Spell checking** — optional, via Enchant: misspelled words in editable
  text get a red wavy underline, honouring the `spellcheck` attribute.
- **Safe browsing** — a top-level navigation's host is checked against a local
  SHA-256 blocklist before it is fetched, entirely on-device; a match shows a
  full-page warning. Overridable via
  `~/.config/nordstjernen/safebrowsing.list`.
- **Process-per-tab** — each tab's engine runs in its own sandboxed
  `nordstjernen-renderer` process; the GTK app is a thin shell that composites
  the shared-memory tiles the renderer paints and forwards input over an
  HTTP/JSON control channel, so a page can't take down the UI
  ([docs/tab-isolation.md](docs/tab-isolation.md)). `--single-process` runs
  every tab's engine in the shell process instead
  ([docs/single-process-mode.md](docs/single-process-mode.md)). A watchdog
  supervisor restarts the browser after a crash or hang
  ([docs/watchdog.md](docs/watchdog.md)).
- **Privacy** — no telemetry or update pings, standards-compliant client
  hints, local-only safe browsing, partitioned cookies and a `--private`
  session mode.
- **UI** — tabs, bookmarks, history, downloads, find-in-page, page zoom,
  session restore, light/dark/system themes, `view-source:`, printing and
  save-to-PDF, a JavaScript console, settings, and a C embedding API. A
  headless mode (`--headless` with `--screenshot=`, `--dump=`, `--eval=`)
  drives the engine from scripts ([docs/Embedding.md](docs/Embedding.md)). The interface is
  translated into 40 languages and follows the operating-system language
  ([docs/i18n.md](docs/i18n.md)).
- **Extensions** — initial support for simple, page-facing WebExtensions
  ([docs/extensions.md](docs/extensions.md)).
- **Java/JVM** — `org.nordstjernen.Nordstjernen` drives fetch / parse / layout
  / script / render from Java over a JNI bridge, or `RemoteBrowser` /
  `RemotePage` drive a separate renderer process so an engine crash can't take
  down the JVM. The fat jar is both the embedding library and a
  standalone Swing browser (`java -jar nordstjernen-java.jar <url>`). See
  [java/README.md](java/README.md).

## Build

```sh
sudo apt install build-essential git pkg-config meson ninja-build \
    libgtk-4-dev libepoxy-dev libcurl4-openssl-dev libssl-dev libuchardet-dev \
    libpsl-dev libsqlite3-dev libseccomp-dev libwebp-dev libsdl2-dev \
    libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev
meson setup builddir && meson compile -C builddir
./builddir/src/gtk/nordstjernen
```

Windows, Fedora, openSUSE and macOS instructions are in
[docs/](docs/README.md); keyboard, mouse and touch controls are in
[docs/Controls.md](docs/Controls.md).

## Dependencies

Nordstjernen is an independent engine — no upstream browser code.

**Vendored in-tree**, built from the main tree with no submodules:
[lexbor](https://github.com/lexbor/lexbor) (HTML5 → DOM parser, CSS, and the
WHATWG URL module), [QuickJS](https://github.com/quickjs-ng/quickjs)
(quickjs-ng fork, no JIT), [WAMR](https://github.com/bytecodealliance/wasm-micro-runtime)
(WebAssembly interpreter), [Wuffs](https://github.com/google/wuffs)
(memory-safe image decoding), [pl_mpeg](https://github.com/phoboslab/pl_mpeg)
(MPEG-1 video + MP2 audio) and [minimp3](https://github.com/lieff/minimp3)
(MP3). The first `meson setup` downloads two pinned subprojects:
[ns-pango](https://github.com/nordstjernen-web/ns-pango), the text-shaping
fork, and the [wgpu-native](https://github.com/gfx-rs/wgpu-native) release
for WebGPU, linked statically. `-Dns-pango=disabled -Dwebgpu=disabled` link
the system Pango, drop WebGPU and need no network.

**Required system libraries:**

| Library | Min version | Role |
|---------|-------------|------|
| GTK 4 | **≥ 4.22.1 on Windows** (MSYS2 stock), ≥ 4.14 elsewhere (≥ 4.22 preferred) | UI toolkit, GSK renderer |
| GLib / GModule, Pango, cairo, HarfBuzz | (ship with GTK; ns-pango needs GLib ≥ 2.80, cairo ≥ 1.18, HarfBuzz ≥ 8.3, fontconfig ≥ 2.15) | core types, dynamic module loading, text shaping, 2D drawing |
| libepoxy | — | OpenGL/ES function dispatch for WebGL |
| libcurl | ≥ 8.5 (≥ 8.11 for WebSocket) | HTTP/2 networking, HSTS, cookies, native WebSocket |
| OpenSSL (libcrypto, libssl) | — | WebCrypto (`crypto.subtle`), TLS for the nghttp2 backend |
| uchardet | — | charset detection |
| libpsl | — | public-suffix list for cookie scoping |
| SQLite | — | IndexedDB persistent storage |
| libwebp | — | lossy and animated WebP decoding |
| FFmpeg libav\* | ≥ 6.0 (Linux, Windows; optional on macOS) | inline WebM (VP9/VP8 video, Opus/Vorbis audio) |
| libseccomp | — (Linux only) | renderer syscall sandbox |

**Optional**, auto-detected or build-time-selected:
SDL2 (audio output and microphone capture, `-Daudio`),
[FFmpeg](https://github.com/FFmpeg/FFmpeg) libav\* on macOS (inline WebM),
GStreamer ≥ 1.20 (H.264/AAC on Linux and the BSDs, `-Dgstreamer`),
vorbisfile / opusfile (Ogg audio without FFmpeg), brotli and zstd (HTTP
content encodings, WOFF2),
poppler-glib (inline PDF), libavif (AVIF images), libjxl (JPEG XL images), Enchant (spell checking), fontconfig / pangoft2
(extra font backends), [libnghttp2](https://github.com/nghttp2/nghttp2) (the
in-tree HTTP/2 backend), [ngtcp2](https://github.com/ngtcp2/ngtcp2) +
[nghttp3](https://github.com/ngtcp2/nghttp3) + GnuTLS (HTTP/3 over QUIC inside
it), wgpu-native on platforms it publishes no release for (the BSDs, musl,
Android, iOS) and Fabrice Bellard's original
[QuickJS](https://github.com/bellard/quickjs) (`-Dquickjs=quickjs`, fetched
through a meson wrap).

## License

Nordstjernen is dual-licensed: use it under **either** the Nordstjernen
Source License v1.0 **or** the GNU General Public License version 3 or later,
at your option (`LicenseRef-NSL-1.0 OR GPL-3.0-or-later`).

- **GPL-3.0-or-later** — free software: use, modify and redistribute it for any
  purpose, provided derivative works are also released under the GPL. See
  [COPYING](COPYING).
- **NSL-1.0** — use, modify and redistribute freely, except as a competing
  browser; each release becomes MIT after ten years. It is inspired by the
  [Functional Source License](https://fsl.software/).

See [License.md](License.md) for the full terms. Commercial licenses by
agreement. Bundled third-party components keep their own licenses
([THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md)).

# Development team
Nordstjernen is developed by a team located in Norway, Poland and Spain.  

Project home: <https://nordstjernen.org>  

[Join the Discord](https://discord.gg/4W959nW5vF)  

## Builds
[![linux](https://github.com/nordstjernen-web/nordstjernen-browser/actions/workflows/linux.yml/badge.svg?branch=main)](https://github.com/nordstjernen-web/nordstjernen-browser/actions/workflows/linux.yml)
[![macos](https://github.com/nordstjernen-web/nordstjernen-browser/actions/workflows/macos.yml/badge.svg?branch=main)](https://github.com/nordstjernen-web/nordstjernen-browser/actions/workflows/macos.yml)
[![windows](https://github.com/nordstjernen-web/nordstjernen-browser/actions/workflows/windows.yml/badge.svg?branch=main)](https://github.com/nordstjernen-web/nordstjernen-browser/actions/workflows/windows.yml)
[![android](https://github.com/nordstjernen-web/nordstjernen-browser/actions/workflows/android.yml/badge.svg?branch=main)](https://github.com/nordstjernen-web/nordstjernen-browser/actions/workflows/android.yml)
[![java](https://github.com/nordstjernen-web/nordstjernen-browser/actions/workflows/java.yml/badge.svg?branch=main)](https://github.com/nordstjernen-web/nordstjernen-browser/actions/workflows/java.yml)
[![freebsd](https://github.com/nordstjernen-web/nordstjernen-browser/actions/workflows/freebsd.yml/badge.svg?branch=main)](https://github.com/nordstjernen-web/nordstjernen-browser/actions/workflows/freebsd.yml)
[![netbsd](https://github.com/nordstjernen-web/nordstjernen-browser/actions/workflows/netbsd.yml/badge.svg?branch=main)](https://github.com/nordstjernen-web/nordstjernen-browser/actions/workflows/netbsd.yml)
[![musl](https://github.com/nordstjernen-web/nordstjernen-browser/actions/workflows/musl.yml/badge.svg?branch=main)](https://github.com/nordstjernen-web/nordstjernen-browser/actions/workflows/musl.yml)

<img src="docs/best-viewed-in-nordstjernen.gif" alt="Best viewed in Nordstjernen" width="140">

----

Copyright 2026 Andreas Røsdal ·  
