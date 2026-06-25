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

The first 20 are grouped Security → Privacy → Performance → Quality/Process,
ordered by leverage within each group; a second pass adds ten more (21–30),
leaning privacy-and-platform, and a third (31–40) digs into engine internals
— memory-safety mitigation and main-thread responsiveness; two final addenda
(#41, #42) close cross-platform and download security gaps. A single
priority ranking of all 42 (by ROI, then effort/risk) is at the end.

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

## Additional proposals (second pass)

A further ten, numbered sequentially and tagged by area, found on a deeper
read of the same tree. They lean privacy-and-platform rather than the
process-architecture themes above.

### 21. Fingerprinting resistance ★ *(Privacy)*

- **Upstream:** a coordinated mode normalizes or spoofs the values that
  uniquely identify a browser — canvas/WebGL readback, enumerable fonts,
  screen/`devicePixelRatio`, timezone, `navigator` details, audio
  fingerprint — `toolkit/components/resistfingerprinting/`, pref
  `privacy.resistFingerprinting`.
- **Nordstjernen today:** no fingerprinting defense; canvas/WebGL readback,
  the font list, screen metrics, and timezone each leak an identifying
  signal — and being a rare engine makes the user-agent itself a strong
  discriminator.
- **The move:** a single opt-in mode that clamps `screen`/DPR to common
  values, restricts font enumeration to a standard set, adds per-session
  noise to canvas/`getImageData` readback, and pins timezone/locale where
  configured. Pairs naturally with the timer clamp (#7).
- **ROI: High · Effort: L · Fit: excellent — squarely the project's pitch.**

### 22. Tracking-protection blocklists *(Privacy / Security)*

- **Upstream:** known trackers, cryptominers, and fingerprinters are
  blocked at the network layer from curated lists — `netwerk/url-classifier`,
  `toolkit/components/url-classifier`.
- **Nordstjernen today:** enforces CSP/SRI/HSTS but does no
  blocklist-based blocking of third-party trackers or malware hosts.
- **The move:** ship a compact, refreshable host/URL blocklist and consult
  it in the fetch path (`ns_net_fetch_async`) before connecting, with a
  per-site toggle. Complements the URL- and storage-level anti-tracking
  items (#10, #11).
- **ROI: High · Effort: M · Fit: excellent (needs a list channel, no
  telemetry).**

### 23. Cookie-banner auto-handling *(Privacy / UX)*

- **Upstream:** consent banners are auto-dismissed (preferring "reject")
  via per-site rules and common-framework heuristics —
  `toolkit/components/cookiebanners`.
- **Nordstjernen today:** none; every site shows its banner.
- **The move:** a small rule set plus generic heuristics that click
  reject/close on load. Self-contained; pure content scripting over the
  existing DOM/JS path.
- **ROI: Med · Effort: M · Fit: good.**

### 24. Encrypted Client Hello (ECH) *(Privacy / Security)*

- **Upstream:** the TLS ClientHello — including the SNI server name — is
  encrypted so on-path observers can't see which host is being visited
  (ECH plumbing in `netwerk/protocol/http/`).
- **Nordstjernen today:** SNI is sent in cleartext on every HTTPS
  connection, revealing the destination host to the network.
- **The move:** OpenSSL/libcurl expose ECH; fetch the ECH config (via the
  HTTPS/SVCB DNS record, ideally over the DoH resolver from #12) and enable
  it on the easy handle in `src/net.c`.
- **ROI: Med · Effort: S–M · Fit: excellent — opt-in, no new dependency.**

### 25. COOP/COEP and real `crossOriginIsolated` *(Security)*

- **Upstream:** `Cross-Origin-Opener-Policy` / `Cross-Origin-Embedder-Policy`
  headers are parsed to put a document in a cross-origin-isolated agent
  cluster, which gates powerful features like `SharedArrayBuffer`
  (`dom/base/Document.cpp`, `netwerk/protocol/http/HttpBaseChannel.h`).
- **Nordstjernen today:** `crossOriginIsolated` is hard-coded `false`
  (`src/js.c:32437`) and the headers are not processed, so the isolation
  state can never be granted and any SAB exposure is ungated.
- **The move:** parse COOP/COEP, compute the isolation state, and gate
  `SharedArrayBuffer`/high-res timers on it — the natural companion to the
  timer-precision work in #7.
- **ROI: Med · Effort: M · Fit: good.**

### 26. MIME-sniffing safety (`nosniff` + type enforcement) *(Security)*

- **Upstream:** content-type handling honors `X-Content-Type-Options:
  nosniff` and constrains sniffing so a mislabelled response can't be
  executed as script/style (`netwerk/base/nsIContentSniffer.idl`).
- **Nordstjernen today:** `X-Content-Type-Options: nosniff` is not honored
  and there is no guardrail stopping a `text/plain` body from being used as
  a script/stylesheet.
- **The move:** honor `nosniff`, and refuse to execute/apply a subresource
  whose declared type doesn't match the context. A small, high-value
  network-layer rule that complements ORB (#5).
- **ROI: Med · Effort: S · Fit: excellent.**

### 27. Lazy image loading (`loading=lazy`) *(Performance)*

- **Upstream:** images (and iframes) with `loading=lazy`, and off-screen
  images generally, are deferred until near the viewport
  (`dom/html/HTMLImageElement.cpp`).
- **Nordstjernen today:** the `loading` attribute is reflected as an IDL
  property (`src/js.c:3265`) but off-screen images are still fetched and
  decoded eagerly.
- **The move:** defer the fetch/decode of images whose computed position is
  far below the viewport until scroll brings them close, using the layout
  geometry already available.
- **ROI: Med · Effort: S–M · Fit: good.**

### 28. On-device page translation *(UX / feature)*

- **Upstream:** full-page translation runs locally with bundled models —
  no cloud round-trip — `toolkit/components/translations`.
- **Nordstjernen today:** no translation; but the project *already* ships
  on-device inference (llama.cpp for `about:start`) and an in-tree i18n
  catalogue, so the "local, no-cloud" muscle exists.
- **The move:** an opt-in, on-device translate action that reuses the
  existing local-model infrastructure — a strong thematic fit with the
  no-telemetry, offline-capable ethos.
- **ROI: Med · Effort: L · Fit: excellent (reuses on-device AI).**

### 29. Password manager / credential storage *(Security / UX)*

- **Upstream:** credentials are stored encrypted with autofill and a
  breach-aware UI — `toolkit/components/passwordmgr`.
- **Nordstjernen today:** no credential storage; users depend entirely on
  an external manager.
- **The move:** an optional, locally-encrypted credential store (the
  WebCrypto/OpenSSL primitives in `src/webcrypto.c` already exist) with
  save/fill on forms. Keep it strictly local — no sync, no cloud.
- **ROI: Med · Effort: L · Fit: good (local-only).**

### 30. Session restore / crash recovery *(UX)*

- **Upstream:** open tabs and their navigation state are persisted and
  restored after a restart or crash — `toolkit/components/sessionstore`.
- **Nordstjernen today:** process-per-tab already keeps one tab's crash
  from taking down the UI, but there is no whole-session restore after a
  full restart or shell crash.
- **The move:** periodically serialize open tabs + history + scroll
  position and offer to restore them on next launch.
- **ROI: Med · Effort: M · Fit: good.**

## Third pass — engine internals (31–40)

Ten more from a source-level read, weighted toward the engine internals
where the leverage is highest: memory-safety mitigation, main-thread
responsiveness, and a couple of remaining platform gaps. Each was checked
against the current code.

### 31. A hardened heap allocator ★ *(Security)*

- **Upstream:** the in-tree allocator poisons freed memory, quarantines
  and delays reuse, and a probabilistic guard-page checker (PHC) catches
  use-after-free/overflows in the wild — `memory/build/mozjemalloc.cpp`
  and the PHC replace-malloc layer.
- **Nordstjernen today:** links the **system `malloc`** with no poisoning,
  guard pages, or delayed reuse — so a use-after-free in 134k lines of C is
  directly exploitable rather than a likely crash.
- **The move:** adopt a hardened allocator (a poisoning/guard-page malloc,
  or a thin wrapper that poisons on free and delays reuse for hot
  allocation classes). One of the highest mitigation-per-effort wins
  available to a C codebase, and complementary to the #1 sandboxing.
- **ROI: High · Effort: M · Fit: excellent — pure mitigation, no API
  surface.**

### 32. Off-main-thread image decoding ★ *(Performance)*

- **Upstream:** image decoding runs on a dedicated thread pool so a big or
  slow image never stalls script/layout — `image/DecodePool.cpp`.
- **Nordstjernen today:** image decode (`src/image.c`, Wuffs/libwebp) runs
  **synchronously on the calling thread**, so a large JPEG/PNG blocks the
  render path.
- **The move:** move decode to a worker thread (the project already has a
  threading model) and upload the surface when ready, showing layout
  reflow on completion.
- **ROI: High · Effort: M · Fit: good.**

### 33. Retained paint tree / partial repaint ★ *(Performance)*

- **Upstream:** the display list is *retained* and only the invalidated
  subtrees are rebuilt between frames, instead of regenerating the whole
  scene on every change — `layout/painting/RetainedDisplayListBuilder.h`.
- **Nordstjernen today:** no damage-rect/partial-invalidation path is
  visible in `src/paint.c`/`src/render.c`; the paint tree is rebuilt wholesale.
- **The move:** track a dirty region and rebuild only the affected paint
  subtrees, reusing cached render nodes elsewhere. Large win for
  scroll/animation/hover on complex pages.
- **ROI: High · Effort: M–L · Fit: good.**

### 34. Unload background tabs under memory pressure *(Memory)*

- **Upstream:** under memory pressure the least-recently-used background
  tabs are discarded (and seamlessly reloaded on return), bounding total
  footprint — the `TabUnloader` mechanism.
- **Nordstjernen today:** every open tab keeps a live renderer process; no
  memory-pressure-driven discard.
- **The move:** on a memory-pressure signal, snapshot and tear down the
  LRU background tab's renderer, restoring it (via the #14 bfcache state)
  when reselected.
- **ROI: Med · Effort: M · Fit: good (pairs with bfcache).**

### 35. SafeBrowsing-style phishing & malware protection *(Safety)*

- **Upstream:** navigations are checked against regularly-updated phishing
  and malware lists, queried privately via hash prefixes —
  `toolkit/components/url-classifier`.
- **Nordstjernen today:** no phishing/malware interstitial; a user can be
  navigated straight onto a known-malicious page.
- **The move:** consult a compact, privately-queried (prefix-hash) blocklist
  before committing a top-level navigation and show an interstitial. Distinct
  from the *tracking* blocklist (#22): this protects the user, not their
  privacy.
- **ROI: Med–High · Effort: M · Fit: good (needs a list channel, no
  telemetry).**

### 36. Resource hints: speculative preconnect & prefetch *(Performance)*

- **Upstream:** `dns-prefetch`/`preconnect` warm DNS+TLS before a resource
  is requested and `prefetch`/speculation-rules pull likely next-navigation
  resources early — `dom/html/HTMLDNSPrefetch.cpp`,
  `dom/base/SpeculationRules.cpp`, `nsIOService::SpeculativeConnect`.
- **Nordstjernen today:** `<link rel=preconnect/prefetch/dns-prefetch>` are
  recognized as valid `rel` tokens but **not acted on**; only external
  *scripts* are pre-fetched (`ns_js_prefetch_external_scripts`). No
  connection warming, no next-navigation prefetch.
- **The move:** honor `preconnect` (open the TLS connection early via
  libcurl) and `prefetch` (fetch into the existing disk cache), hiding
  latency on the next request/navigation.
- **ROI: Med · Effort: S–M · Fit: good.**

### 37. Private browsing mode ★ *(Privacy)*

- **Upstream:** a private window keeps cookies/cache/history/storage in an
  ephemeral, isolated container discarded on close (keyed by
  `privateBrowsingId` in the origin attributes — `netwerk/base/LoadInfo.cpp`).
- **Nordstjernen today:** no private/incognito mode; all state is persistent.
- **The move:** an ephemeral profile — in-memory cookie jar/cache/storage,
  no history writes, cleared on window close. The cookie/storage
  partitioning machinery already present is most of the plumbing.
- **ROI: High · Effort: M · Fit: excellent (core to a privacy browser).**

### 38. HTML Sanitizer API (`Element.setHTML`) *(Security)*

- **Upstream:** a built-in sanitizer drops scripts/event-handlers/unsafe
  URLs from untrusted HTML at the DOM-injection sink —
  `dom/security/sanitizer/`.
- **Nordstjernen today:** no Sanitizer API; pages that want to inject
  untrusted HTML safely have no built-in safe sink.
- **The move:** implement `Element.setHTML()`/`Sanitizer` over the existing
  lexbor parse + an allow-list filter — a standards-track XSS defense that
  complements CSP and ORB (#5).
- **ROI: Med · Effort: M · Fit: good.**

### 39. Off-main-thread HTML parsing/tokenization *(Performance)*

- **Upstream:** the HTML tokenizer runs on a separate stream-parser thread
  so parsing overlaps network and never blocks the UI —
  `parser/html/nsHtml5StreamParser.cpp`.
- **Nordstjernen today:** `ns_html_parse` runs **synchronously** on the
  caller; a large document parses in one main-thread burst.
- **The move:** run tokenization on a worker as bytes arrive, handing the
  built tree back for layout — overlapping parse with download and keeping
  the shell responsive on big pages.
- **ROI: Med · Effort: L · Fit: good (a threading model already exists).**

### 40. Incremental / low-pause garbage collection *(Performance)*

- **Upstream:** the JS engine collects incrementally and generationally to
  keep GC pauses off the frame budget — `js/src/gc/`.
- **Nordstjernen today:** QuickJS reclaims via reference counting plus a
  stop-the-world mark-sweep for cycles; on a large heap that pause is fully
  visible because there is no JIT headroom to absorb it.
- **The move:** budget the cycle collector incrementally (time-sliced
  marking) so collection interleaves with rendering instead of stalling it.
  Higher risk (engine internals) — listed for completeness as the remaining
  responsiveness lever.
- **ROI: Med · Effort: L · Fit: medium (touches the JS runtime).**

## Addenda — remaining security gaps (41–42)

### 41. Sandbox the renderer on macOS and Windows, not only Linux ★ *(Security)*

- **Upstream:** every content/decoder process is confined by a
  platform-native sandbox on all three desktops — Linux seccomp-bpf + user
  namespaces (`security/sandbox/linux`), macOS Seatbelt profiles
  (`security/sandbox/mac/Sandbox.h`, `sandbox_init`), and a Windows
  AppContainer / restricted-token / job-object broker
  (`security/sandbox/win/src/sandboxbroker/sandboxBroker.cpp`).
- **Nordstjernen today:** the renderer is sandboxed **only on Linux**
  (Landlock + seccomp in `src/security.c`, all under `#ifdef __linux__`); on
  macOS and Windows `ns_security_sandbox_init` does nothing beyond refusing
  to run as root, so a compromised renderer on those platforms has the
  user's full filesystem and network access. Process-per-tab still stops a
  page from taking down the UI, but not from stealing data.
- **The move:** add the two missing backends behind the existing
  `ns_security_sandbox_init` seam — a macOS Seatbelt profile via
  `sandbox_init_with_parameters` denying filesystem/network beyond the
  renderer's needs, and on Windows an AppContainer + restricted token + job
  object + `SetProcessMitigationPolicy`, with a thin broker for the few
  privileged operations. It is the same boundary already designed for Linux,
  extended to the other two supported desktops — arguably the single biggest
  security gap relative to the project's own pitch, since two of three
  desktop platforms currently ship an unconfined renderer.
- **ROI: High · Effort: M (macOS) / M–L (Windows) · Fit: excellent — closes
  a whole-platform hole and reuses the existing IPC/sandbox seam.**

### 42. Mark downloaded files with the OS "downloaded-from-internet" tag ★ *(Security)*

- **Upstream:** every completed download is tagged with the platform
  mark-of-the-web so the OS's own gatekeeper screens it before it runs, and
  executables get an extra reputation check — the quarantine attribute is
  applied in `toolkit/components/downloads/DownloadIntegration.sys.mjs`, with
  `toolkit/components/reputationservice/ApplicationReputation.cpp` vetting
  executables.
- **Nordstjernen today:** the shell saves downloads (the
  `NS_PROC_EVT_DOWNLOAD` path in `src/gtk/procview.c`) with **no OS
  quarantine** — no `com.apple.quarantine` xattr on macOS, no
  `Zone.Identifier` alternate data stream on Windows — so Gatekeeper and
  SmartScreen never get the chance to warn before a downloaded
  installer/script/binary is executed.
- **The move:** on save, set the platform mark-of-the-web
  (`com.apple.quarantine` xattr / a `Zone.Identifier:$DATA` stream carrying
  the source URL), and optionally warn when the saved file is a directly
  executable type. This needs **no network and no telemetry** — it simply
  lets the OS's existing gatekeeper do its job, so it sidesteps the
  reputation-service machinery (which would otherwise overlap SafeBrowsing,
  #35).
- **ROI: High (per effort) · Effort: S · Fit: excellent — pure local
  security, delegates the warning to the OS.**

---

## Priority ranking

The proposals above keep stable IDs (`#`, numbered in the order they were
found). This table sorts the same 42 into a single execution priority
(`P`). The rule: **leverage (ROI) first**; within a band, cheaper effort
and lower risk rank higher, with security/privacy nudged up for a browser
whose pitch is security and privacy. Two items (#41, #1) are pulled above
pure effort order on strategic weight — see the notes below.

The bands: **P1–P15** are the High-ROI set, **P16–P17** Med–High,
**P18–P23** cheap Med wins, **P24–P34** Med/medium-effort, **P35–P41**
Med/large-effort, **P42** the lone Low–Med housekeeping item.

| P | # | Proposal | Area | ROI | Effort |
|---|---|----------|------|-----|--------|
| 1 | 11 | Strip tracking query parameters | Privacy | High | S |
| 2 | 42 | Mark-of-the-web on downloads | Security | High | S |
| 3 | 2 | Sandbox the audio helper | Security | High | S–M |
| 4 | 8 | HTTPS-First upgrade | Privacy/Sec | High | S–M |
| 5 | 31 | Hardened heap allocator | Security | High | M |
| 6 | 1 | WASM-sandbox a C decoder (reuse WAMR) | Security | High | M |
| 7 | 41 | Cross-platform renderer sandbox (macOS/Windows) | Security | High | M–L |
| 8 | 37 | Private browsing mode | Privacy | High | M |
| 9 | 9 | Partition the whole storage stack | Privacy | High | M |
| 10 | 32 | Off-main-thread image decoding | Perf | High | M |
| 11 | 13 | Speculative preload scanner | Perf | High | M |
| 12 | 14 | Back/forward cache | Perf | High | M |
| 13 | 22 | Tracking-protection blocklists | Privacy | High | M |
| 14 | 33 | Retained paint tree / partial repaint | Perf | High | M–L |
| 15 | 21 | Fingerprinting resistance (RFP) | Privacy | High | L |
| 16 | 35 | SafeBrowsing phishing/malware | Safety | Med–High | M |
| 17 | 10 | Bounce-tracking protection | Privacy | Med–High | M |
| 18 | 26 | MIME-sniffing safety (nosniff) | Security | Med | S |
| 19 | 7 | Reduce/jitter timer precision | Security | Med | S |
| 20 | 12 | DNS-over-HTTPS | Privacy | Med | S |
| 21 | 24 | Encrypted Client Hello (ECH) | Privacy/Sec | Med | S–M |
| 22 | 36 | Resource hints (preconnect/prefetch) | Perf | Med | S–M |
| 23 | 27 | Lazy image loading | Perf | Med | S–M |
| 24 | 38 | HTML Sanitizer API | Security | Med | M |
| 25 | 5 | Opaque Response Blocking | Security | Med | M |
| 26 | 25 | COOP/COEP + crossOriginIsolated | Security | Med | M |
| 27 | 4 | Font sanitization before raster | Security | Med | M |
| 28 | 34 | Background-tab unloading | Memory | Med | M |
| 29 | 15 | Style-sharing cache + Bloom filter | Perf | Med | M |
| 30 | 16 | Image surface cache + downscale | Perf | Med | M |
| 31 | 17 | Pre-spawned renderer | Perf | Med | M |
| 32 | 30 | Session restore / crash recovery | UX | Med | M |
| 33 | 23 | Cookie-banner auto-handling | Privacy | Med | M |
| 34 | 18 | Reader mode | UX | Med | M |
| 35 | 3 | Site isolation (per-origin) | Security | Med | L |
| 36 | 6 | Compact cert revocation | Security | Med | L |
| 37 | 19 | Accessibility tree | Quality | Med | L |
| 38 | 39 | Off-main-thread HTML parsing | Perf | Med | L |
| 39 | 29 | Password manager (local-only) | Security/UX | Med | L |
| 40 | 28 | On-device page translation | UX | Med | L |
| 41 | 40 | Incremental / low-pause GC | Perf | Med | L |
| 42 | 20 | Vendored-library audit ledger | Process | Low–Med | S |

Notes on the ranking:

- **P1–P4 are the do-this-week set** — all small, low-risk, and either pure
  security or pure privacy: strip tracking params (#11), mark-of-the-web on
  downloads (#42), sandbox the audio helper (#2), HTTPS-First (#8).
- **#41 (P7) and #1 (P6) are pulled up on strategic weight.** They carry
  more effort than their neighbours but are the security work most worth
  scheduling: #41 because two of three desktops ship an *unconfined
  renderer* today (the biggest gap versus the project's own pitch), and #1
  because in-process WASM decoder sandboxing is the highest-leverage
  architectural mitigation — and uniquely cheap here, since the runtime is
  already in-tree.
- **#21 (P15) and #37 (P8) are the biggest single privacy statements**;
  #37 ranks higher only because the cookie/storage-partitioning plumbing it
  needs largely exists.
- **#20 (P42)** is cheap (S) but ranks last purely on leverage; it is
  reasonable to fold in early as housekeeping rather than treat it as
  "last to do."

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

## Codebase & tech-stack comparison

The size and language gap between the two trees is the single most
important context for every proposal above, so it is worth stating plainly.
Counts below are raw line counts (`find | wc -l`) over the two checkouts
this comparison was drawn from; treat them as orders of magnitude, not
precise SLOC.

| | Nordstjernen | Mozilla (Gecko / Servo) |
|---|---|---|
| Hand-written / authored engine | **~136k** lines, **all C** (+ a ~3k C++ Qt shell, ~7k JS polyfills) | **~12M C/C++ + ~10.4M JS + ~0.5M Rust** (excl. tests & third-party) |
| Whole checkout | ~1.0M C/C++ (incl. vendored libs) | ~17.4M C/C++ + ~15.2M JS + ~5.9M Rust (**≈ 38M**, with tests + vendored crates) |
| JavaScript engine | QuickJS (forked), **~97k**, interpreter, **no JIT** | SpiderMonkey, **~1.28M**, JIT |
| HTML / CSS / URL | lexbor (forked), ~606k | Gecko layout (C++) + Stylo (Rust) |
| Wasm | WAMR subset, ~73k, interpreter | SpiderMonkey, JIT |
| Languages in the engine | **C** (one language end to end) | **C++, Rust, JS** |

The gap is roughly **three orders of magnitude** in authored code. That one
number explains most of the divergence in this document: the claim that a
single person can read and audit the whole engine end to end is credible at
136k lines and simply is not at ~38M — so Nordstjernen can take Mozilla's
*mitigations* but not its *scale*. Note also that Nordstjernen's biggest
in-tree component is not its own code but the vendored **lexbor** parser
(~606k); its entire JS engine (QuickJS, ~97k) is **~13× smaller** than
SpiderMonkey alone.

**Language choice.** Nordstjernen is one language end to end — C for the
engine, with only a thin C++ shell (Qt/GTK glue) and a little JS
(polyfills). That buys maximal portability, one mental model, small
binaries, and direct control, and pays for it in memory safety — which it
buys back three ways: staying small enough to audit, leaning on memory-safe
vendored decoders (Wuffs) and an **interpreter with no JIT** (removing the
single largest class of browser exploit primitives), and confining the
renderer in a process sandbox. Mozilla is polyglot by scale and history:
C++ for the legacy bulk it cannot rewrite, **Rust** for the new
safety-critical pieces (Stylo's CSS engine, WebRender's compositor, parts
of media/network/security), and **JS** for the entire front end and much of
the platform.

**Tech stack.** Nordstjernen composes a small set of focused libraries —
GTK 4 / GSK (UI + software rendering), libcurl (network), OpenSSL (TLS +
WebCrypto), QuickJS (script), lexbor (parse), Wuffs (images), WAMR (wasm),
Pango/Cairo (text/2D), SDL2 (audio) — with **no custom GPU compositor and
no JIT**. Mozilla builds those layers itself at scale: SpiderMonkey (JIT
script + wasm), Stylo (parallel CSS), WebRender (GPU-retained display
lists), Necko (network), NSS (crypto), and an XPCOM platform layer beneath
it all.

**Two answers to the same problem.** Both engines are mostly
memory-unsafe C/C++ at the core and both know it. Mozilla's answer is
incremental rewrite-in-Rust plus in-process sandboxing of the C it keeps
(RLBox, #1). Nordstjernen's answer is to stay small enough to audit, drop
the JIT, and lean on the OS sandbox and compiler hardening it already ships.
Read that way, this whole document is an exercise in importing the first
answer's cheap, bounded mitigations into the second answer's much smaller
frame.
