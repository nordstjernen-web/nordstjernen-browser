# Engine technical comparison — lessons for Nordstjernen

A structured read of a mature, full-scale browser engine (Mozilla's
Gecko/Servo tree) against Nordstjernen, to mine **transferable ideas**.
Nordstjernen is a clean-room implementation — nothing here is about
importing code. Each item names the upstream subsystem (with a path, so
the design can be studied first-hand) and maps it to Nordstjernen's
current state and a concrete, in-ethos move.

## How to read this

The single most useful framing: almost everything the reference engine
has built over the last decade — in-process library sandboxing, per-site
process isolation, a Rust style engine, a process zoo — is **retrofitting
memory-safety and isolation onto ~30M lines of C++ it cannot rewrite**.
Nordstjernen is ~134k lines of clean-room C. It can capture most of the
*security* benefit far more cheaply, and should deliberately skip the
scale-driven complexity. So the highest-ROI lessons are about **where the
isolation boundaries go**, not about features.

Each proposal is tagged **ROI** (High / Med / Low) and **Effort**
(S / M / L). "Fit" notes how well it sits with Nordstjernen's stated
values: minimal, clean-room, single-human-auditable, no-JIT, no telemetry.

The 20 are grouped Security → Privacy → Performance → Quality/Process,
ordered by leverage within each group. A ranked summary table is at the
end.

---

## Security

### 1. In-process WASM sandboxing of C decoders — reuse the bundled WASM runtime ★

- **Upstream:** risky byte-parsing C/C++ libraries are compiled to WASM
  with `wasm2c` and run behind a memory-safety boundary so a bug in them
  cannot corrupt the host address space. See `security/rlbox`,
  `config/wasm2c.py`, `config/external/rlbox_wasm2c_sandbox`, and the
  per-library glue: `modules/woff2/RLBoxWOFF2Sandbox.cpp` (font
  decompression), `parser/expat/rlbox_expat.h` (XML),
  `extensions/spellcheck/hunspell/glue/RLBoxHunspell.cpp`,
  `dom/media/ogg/OggRLBox.h`, plus `gfx/ots` and `gfx/graphite2`.
- **Nordstjernen today:** a memory-safety bug in `libwebp`, `uchardet`,
  `pl_mpeg`, `minimp3`, or the optional `libav` is a full
  **renderer-process** compromise. (Wuffs is already memory-safe and needs
  none of this — it is the model the others should follow.)
- **The move:** wrap one decoder's entry points so it runs inside the
  **WAMR interpreter Nordstjernen already vendors** (`src/wasm.c`,
  `src/wamr/`), with copy-in/copy-out at the boundary. A bug then stays in
  WASM linear memory instead of the renderer heap. Start with one library
  (`libwebp` or `uchardet`) as a spike, measure overhead, then extend.
- **ROI: High · Effort: M · Fit: excellent.** This is the one place
  Nordstjernen is *better positioned than the reference engine was* — the
  hard dependency (a WASM runtime) is already shipped. It complements, not
  replaces, the process sandbox.

### 2. Sandbox the audio helper

- **Upstream:** *all* media decoding is isolated and sandboxed — video in
  the RDD ("Remote Data Decoder") process (`dom/media/ipc/RDDProcessImpl.cpp`),
  audio in the Utility process (`ipc/glue/UtilityMediaServiceParent.cpp`,
  `ipc/glue/UtilityProcessSandboxing.cpp`). Decoding attacker-controlled
  codec bytes is treated as hostile work that must be confined.
- **Nordstjernen today:** the `nordstjernen-audio` helper
  (`src/audio/main.c`) decodes untrusted MP3/MP2/Opus/Vorbis **with no
  sandbox**, while the renderer already runs under seccomp + Landlock.
  Video, by contrast, is decoded *inside* the sandboxed renderer — already
  the right shape.
- **The move:** apply the renderer's existing seccomp/Landlock profile to
  the audio helper after it opens its SDL2 device, or move decode into the
  sandboxed renderer and leave only the device write outside.
- **ROI: High · Effort: S–M · Fit: excellent — closes a known gap with
  machinery already in the tree.**

### 3. Site isolation (per-origin processes), not just per-tab

- **Upstream:** "Fission" makes the content process boundary **per-site**:
  a cross-origin `<iframe>` runs in its own process, so a Spectre-class
  read cannot reach another origin's memory (`GeckoProcessType_Content`,
  the `dom/ipc` content-process machinery).
- **Nordstjernen today:** process-per-*tab*; cross-origin iframes share
  the tab's renderer (`docs/tab-isolation.md`).
- **The move:** treat the *origin* as the isolation unit for cross-origin
  frames. Lower urgency than elsewhere — Nordstjernen's no-JIT interpreter
  already removes most reliable Spectre gadgetry — but it is the right
  architectural direction.
- **ROI: Med · Effort: L · Fit: good (matches the existing IPC model).**

### 4. Sanitize fonts before the rasterizer sees them

- **Upstream:** every downloadable font is passed through the OpenType
  Sanitizer (`gfx/ots/src`) — a strict validator that rejects or repairs
  malformed tables — *before* it reaches FreeType/the shaper, because font
  files are a classic memory-corruption vector.
- **Nordstjernen today:** `@font-face` web fonts are handed toward
  Pango/FreeType without an independent sanitization pass.
- **The move:** add a validation/sanitization gate for downloaded fonts
  (a small OTS-style table validator, or — better — run the font path
  through the #1 WASM sandbox).
- **ROI: Med · Effort: M · Fit: good.**

### 5. Opaque Response Blocking (ORB)

- **Upstream:** cross-origin responses that no `<img>`/`<script>`/CSS
  could legitimately consume (e.g. an HTML or JSON body fetched as a
  "script") are blocked from ever entering the content process, shrinking
  the Spectre attack surface — `netwerk/protocol/http/OpaqueResponseUtils.cpp`.
- **Nordstjernen today:** subresources are fetched and delivered to the
  renderer without a CORB/ORB-style cross-origin content sniff gate.
- **The move:** before delivering a cross-origin, non-CORS subresource to
  a context that can only legitimately use a narrow set of types, sniff
  and block mislabelled HTML/JSON/XML.
- **ROI: Med · Effort: M · Fit: good — pure network-layer policy.**

### 6. Compact certificate-revocation checking (CRLite-style)

- **Upstream:** revocation is delivered as a compact, regularly-updated
  filter rather than per-connection OCSP, giving fast offline-capable
  revocation without the privacy leak of OCSP
  (`security/manager/ssl`, the CRLite filter machinery).
- **Nordstjernen today:** revocation relies on whatever the TLS backend
  (OpenSSL via libcurl) does by default; there is no first-class,
  privacy-preserving revocation story.
- **The move:** ship/refresh a compact revocation set and consult it
  during certificate validation. Heavier to operate (needs a data feed) —
  list as a directional security improvement.
- **ROI: Med · Effort: L · Fit: medium (needs an update channel, but no
  telemetry).**

### 7. Reduce and jitter exposed timer precision (Spectre hardening)

- **Upstream:** all script-visible clocks are clamped (and optionally
  jittered) so high-resolution timers cannot be used to build
  cache/side-channel oracles — `privacy.reduceTimerPrecision`,
  `privacy.reduceTimerPrecision.unconditional`.
- **Nordstjernen today:** **partly there** — the monotonic clock is
  already coarsened (~5µs). Worth auditing every exposed source
  (`performance.now()`, `Date.now()`, animation timestamps,
  `SharedArrayBuffer`-based timers if any) to a single clamped policy.
- **The move:** route every time source through one clamp-and-(optionally)
  jitter helper; gate `SharedArrayBuffer` on cross-origin isolation.
- **ROI: Med · Effort: S · Fit: excellent (extends work already done).**

## Privacy

### 8. HTTPS-First: proactively upgrade every `http://` navigation

- **Upstream:** navigations are tried over HTTPS first with a timed
  fallback to HTTP only if the upgrade fails — `dom/security/nsHTTPSOnlyUtils.h`,
  prefs `dom.security.https_first` / `https_only_mode`.
- **Nordstjernen today:** upgrades only hosts already in the HSTS cache
  (`ns_net_hsts_should_upgrade` in `src/net.c`); first visits and non-HSTS
  sites stay on cleartext.
- **The move:** for top-level `http://` navigations, attempt `https://`
  first and fall back on connection/cert failure. Config-gated like the
  other privacy defaults (`do_not_track`, first-party cookies).
- **ROI: High · Effort: S–M · Fit: excellent — a self-contained extension
  of the existing upgrade path.**

### 9. Partition the whole storage stack by top-level site

- **Upstream:** cache, network state, blob URLs, service workers,
  `localStorage`, and IndexedDB are all keyed by first party
  ("dynamic first-party isolation") — see the `privacy.partition.*` prefs.
- **Nordstjernen today:** cookies are already partitioned; HTTP cache,
  `localStorage`, IndexedDB, and blob URLs are not keyed by top-level site,
  leaving supercookie vectors.
- **The move:** thread a `(top-level-site, resource-origin)` partition key
  through the cache (`src/cache.c`), storage, and IndexedDB (`src/idb.c`)
  layers — the same key cookies already use.
- **ROI: High · Effort: M · Fit: excellent — extends an existing pattern.**

### 10. Bounce-tracking protection

- **Upstream:** sites that only ever appear as redirect hops and never get
  user interaction have their storage periodically purged —
  `toolkit/components/antitracking/bouncetrackingprotection/`.
- **Nordstjernen today:** nothing equivalent; a redirect-only tracker can
  set and re-read storage across sites.
- **The move:** record which origins are visited only as redirect
  intermediaries, and clear their storage if no user interaction follows
  within a window.
- **ROI: Med–High · Effort: M · Fit: excellent (privacy-first ethos).**

### 11. Strip known tracking query parameters

- **Upstream:** a curated set of pure-tracking query parameters
  (`utm_*`, `fbclid`, `gclid`, …) is removed from navigations —
  `toolkit/components/antitracking/URLQueryStringStripper.cpp`,
  prefs `privacy.query_stripping.*`.
- **Nordstjernen today:** no query-parameter stripping; click-ids and
  campaign tags ride along into requests, the address bar, and history.
- **The move:** on top-level GET navigations (never POST, never
  subresources), drop a curated, unambiguous list of tracking parameters
  before the request and before recording the document URL; preserve every
  other token byte-for-byte. A natural companion to #10.
- **ROI: High · Effort: S · Fit: excellent.**

### 12. DNS-over-HTTPS (encrypted, centralizable resolver)

- **Upstream:** DNS can be resolved over HTTPS to a trusted resolver,
  hiding lookups from the local network — `netwerk/dns/TRRService.cpp`,
  `netwerk/dns/TRR.h`.
- **Nordstjernen today:** name resolution uses the system resolver
  (cleartext DNS on most networks).
- **The move:** libcurl already supports DoH (`CURLOPT_DOH_URL`); expose
  an opt-in resolver URL in config and wire it into the easy-handle setup
  in `src/net.c`.
- **ROI: Med · Effort: S · Fit: excellent — small, opt-in, no new dep.**

## Performance

### 13. Speculative preload scanning during HTML parse

- **Upstream:** a speculative scanner kicks off fetches for
  `<script src>`, `<img>`, `<link rel=stylesheet/preload>` *while the main
  parser is still running* — `parser/html/nsHtml5SpeculativeLoad.cpp`,
  `nsHtml5Speculation.cpp`.
- **Nordstjernen today:** the document is parsed and *then* subresources
  are fetched, serializing render-blocking work behind network latency.
- **The move:** a lightweight pre-scan over the token stream that feeds
  discovered URLs into the existing async fetch path
  (`ns_net_fetch_async`) ahead of layout.
- **ROI: High (real-page load time) · Effort: M · Fit: good.**

### 14. Back/forward cache (bfcache)

- **Upstream:** recently-visited pages are frozen and kept alive so
  back/forward is instant instead of a reload — the bfcache machinery in
  `dom/base/Document.cpp` and the `*bfcache*` test corpus.
- **Nordstjernen today:** every history navigation re-fetches and
  re-renders (each `/open` is a fresh load through `browser_open_common`).
- **The move:** cache the last *N* frozen page states keyed by history
  entry; restore on back/forward when the page is cacheable (no
  `unload`/`Cache-Control: no-store` constraints).
- **ROI: High (UX) · Effort: M · Fit: good.**

### 15. Style-sharing cache + ancestor Bloom filter

- **Upstream:** the style engine shares computed style between similar
  sibling elements and prunes descendant-selector matching with a per-element
  ancestor Bloom filter (`servo/components/style`) — large wins even before
  any parallelism.
- **Nordstjernen today:** `src/css.c` matches selectors per element from
  scratch.
- **The move:** add a style-sharing cache (reuse a sibling's computed
  style when class/attr/state match) and an ancestor Bloom filter to skip
  obviously-non-matching descendant selectors. Stays single-threaded and
  minimal.
- **ROI: Med (large-DOM perf) · Effort: M · Fit: good.**

### 16. Image surface cache with downscale-on-decode

- **Upstream:** decoded image surfaces are held in a bounded, shared cache
  and large images are decoded directly to the displayed size
  (`image/SurfaceCache.cpp`, `image/SurfaceCacheUtils.h`), capping image
  memory.
- **Nordstjernen today:** image decode/caching (`src/image.c`,
  `ns_image_cache`) does not appear to downscale-on-decode or enforce a
  global decoded-surface budget.
- **The move:** decode oversized images straight to display resolution and
  bound total decoded-surface memory with eviction.
- **ROI: Med (memory on image-heavy pages) · Effort: M · Fit: good.**

### 17. Pre-spawned process (fork-server) for instant new tabs

- **Upstream:** on Linux a fork-server keeps a warm, pre-initialized
  template process so spawning a new content process is cheap
  (`ipc/glue/ForkServer.cpp`).
- **Nordstjernen today:** process-per-tab pays full renderer startup on
  every new tab.
- **The move:** keep one pre-initialized renderer warm (post-sandbox,
  pre-navigation) and hand it the first URL, hiding cold-start latency.
- **ROI: Med (new-tab latency) · Effort: M · Fit: good (matches the
  process model).**

## Quality, UX & process

### 18. Reader mode (article extraction)

- **Upstream:** a standalone readability algorithm extracts the main
  article and renders a clean, distraction-free view —
  `toolkit/components/reader/readability/Readability.js`.
- **Nordstjernen today:** no reader mode.
- **The move:** a content-extraction pass (scoring blocks by text/link
  density) feeding a minimal reader stylesheet. Self-contained; pairs well
  with the engine's strong text-layout path.
- **ROI: Med (UX) · Effort: M · Fit: good.**

### 19. Expose an accessibility tree

- **Upstream:** a full accessibility subsystem maps the DOM to platform
  a11y APIs for screen readers and assistive tech (`accessible/`, ~10MB of
  source).
- **Nordstjernen today:** no AT-SPI/UIA accessibility tree appears to be
  exposed, so the browser is largely unusable with a screen reader.
- **The move:** even a minimal mapping of roles/names/states to the
  platform a11y bus (AT-SPI on Linux, UIA on Windows, NSAccessibility on
  macOS) is a large inclusivity win.
- **ROI: Med (inclusion; can be a release blocker for some users) ·
  Effort: L · Fit: good.**

### 20. A pinned-revision + audit ledger for vendored libraries

- **Upstream:** every third-party dependency version is recorded with who
  reviewed it (`supply-chain/audits.toml`, thousands of lines of
  cargo-vet records).
- **Nordstjernen today:** lexbor, QuickJS, WAMR, Wuffs, pl_mpeg, minimp3
  are vendored without a formal upstream-revision + audit ledger.
- **The move:** a small manifest per vendored library — upstream commit,
  import date, last-audited, CVE-watch link. The lightweight version of
  the same discipline, and a direct expression of the "one human can audit
  the whole thing" value.
- **ROI: Low–Med · Effort: S · Fit: excellent (on-brand).**

---

## Ranked summary

Leverage is the headline judgement (security/privacy weighted for a
browser whose pitch is security and privacy); effort is rough.

| # | Proposal | Area | ROI | Effort |
|---|----------|------|-----|--------|
| 1 | WASM-sandbox a C decoder (reuse WAMR) | Security | High | M |
| 2 | Sandbox the audio helper | Security | High | S–M |
| 8 | HTTPS-First upgrade | Privacy/Sec | High | S–M |
| 11 | Strip tracking query parameters | Privacy | High | S |
| 9 | Partition the whole storage stack | Privacy | High | M |
| 13 | Speculative preload scanner | Perf | High | M |
| 14 | Back/forward cache | Perf | High | M |
| 10 | Bounce-tracking protection | Privacy | Med–High | M |
| 7 | Reduce/jitter timer precision | Security | Med | S |
| 12 | DNS-over-HTTPS | Privacy | Med | S |
| 5 | Opaque Response Blocking | Security | Med | M |
| 4 | Font sanitization before raster | Security | Med | M |
| 15 | Style-sharing cache + Bloom filter | Perf | Med | M |
| 16 | Image surface cache + downscale | Perf | Med | M |
| 17 | Pre-spawned renderer | Perf | Med | M |
| 18 | Reader mode | UX | Med | M |
| 3 | Site isolation (per-origin) | Security | Med | L |
| 19 | Accessibility tree | Quality | Med | L |
| 6 | Compact cert revocation | Security | Med | L |
| 20 | Vendored-library audit ledger | Process | Low–Med | S |

Suggested first picks: **#2** and **#8** are the lowest-risk, highest-certainty
wins; **#11** is tiny; **#1** is the highest-leverage and the most
architecturally interesting (and uniquely cheap here because the WASM
runtime is already in-tree).

## Anti-lessons — what *not* to copy

The reference engine's scale forces choices that would actively harm a
minimalist clean-room browser:

- **A JIT.** Nordstjernen's no-JIT interpreter is a deliberate, defensible
  security win (no W^X churn, far fewer exploit primitives). Keep it.
- **A polyglot C++/Rust mega-tree.** Antithetical to
  single-human-auditability.
- **GPU-retained compositor complexity** (a WebRender-scale display-list
  pipeline). Overkill for the target.
- **Telemetry / experimentation / remote-config infrastructure** and a
  gettext/Fluent localization stack — both already ruled out by the
  project's values, and rightly so.

The throughline: adopt the reference engine's **security architecture**
(isolation boundaries drawn tightly around every place untrusted bytes are
parsed), and decline its scale-driven complexity.
