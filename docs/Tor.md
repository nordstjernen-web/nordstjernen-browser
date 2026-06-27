# Implementing Nordstjernen as a Tor network client — design research

Grounded in the actual source (June 2026, `1.0.16-dev`). File:line refs point at
the code each decision touches.

## TL;DR

Nordstjernen can *already* route page traffic through Tor today
(`nordstjernen --proxy=socks5h://127.0.0.1:9050`, see `docs/Proxy.md`). The
plumbing exists: a single fetch chokepoint, a runtime proxy override
(`ns_net_set_proxy_override`, `src/net.c:1482`), config/env/flag inputs, and
`socks5h://` (remote DNS, no DNS leak).

Turning that into a real **Tor client / "Tor mode"** is mostly *removing the
ways traffic escapes the proxy* and making it **fail-closed**, plus managing a
Tor process and surfacing it in the UI. The hard, separate question — matching
**Tor Browser anonymity** (anti-fingerprinting) — is largely out of scope for a
clean-room engine that ships canvas, WebGL, WASM and WebCrypto; be honest that
this delivers *network-location privacy + .onion*, not unlinkability.

**Recommended shape:**
- **Transport:** keep the system-tor path as the always-works baseline; for an
  integrated mode, spawn and supervise a bundled **Arti** (`arti proxy`) helper
  as an *optional, out-of-tree* binary — same supervision pattern as the
  `nordstjernen-audio` helper and the per-tab renderer. Keep Tor **outside** the
  renderer sandbox; the renderer reaches it only over SOCKS on a pinned
  localhost port.
- **Leak-proofing:** route *every* egress through the proxy (fix the WebSocket /
  EventSource / audio paths that currently skip it), force `socks5h`, and in Tor
  mode add a **Landlock network jail** so the sandboxed renderer can only
  `connect()` to the Tor SOCKS port — proxy-bypass becomes structurally
  impossible, not just policy.
- **Isolation:** per-first-party-domain circuits via SOCKS-auth stream
  isolation, reusing the partition key the engine already computes for cookies.
- **.onion:** works for free through `socks5h`; add `.onion` to the
  potentially-trustworthy (secure-context) set and keep it off the DNS path.

---

## 1. What already exists — the foundation

| Capability | Where | Notes |
|---|---|---|
| Single fetch chokepoint | `ns_fetch_sync_hop`, `src/net.c:3986` | all HTTP(S) page/subresource fetches funnel here |
| Proxy applied to fetches | `ns_net_apply_curl_proxy`, `src/net.c:1609`, called at `:4007` | sets `CURLOPT_PROXY` / `CURLOPT_NOPROXY` |
| Runtime override (all requests) | `ns_net_set_proxy_override`, `src/net.c:1482` (`g_proxy_override`) | one call points the whole browser at a proxy |
| Config + env + flag inputs | `src/config.c:140-142,244-246`; `--proxy=` at `src/gtk/appmain.c:569` | `http_proxy`/`https_proxy`/`no_proxy`, `NS_*_PROXY`, `--proxy=` |
| `socks5h://` (remote DNS) | libcurl | hostname resolved at the proxy — no DNS leak; `.onion` passes through |
| Partition key per top-site | `src/net.c:~3924` (cookie/cache partitioning) | the first-party origin is already known at fetch time — reuse it for circuit isolation |
| Helper-process supervision | `nordstjernen-audio` (`src/audio/main.c`), per-tab renderer (`src/rproc_http.c`) | the pattern to spawn/pump a Tor helper already exists |

So a Tor client is **not** a from-scratch networking project. It is: (a) a
managed Tor transport, (b) closing egress leaks, (c) isolation + UX.

---

## 2. Threat model — say what it actually buys

Be explicit in the UI and docs (today's `docs/Proxy.md:95-98` already gestures
at this):

**You get:**
- Your IP is hidden from the destination site; your ISP/LAN sees only "a Tor
  connection," not which sites.
- Access to `.onion` services.
- No JIT (W^X process-wide) and a per-tab seccomp+Landlock sandbox — a strong
  base against renderer exploitation, better than most "Tor mode" bolt-ons.

**You do NOT automatically get (Tor-Browser-grade anonymity):**
- **Fingerprinting resistance.** Nordstjernen ships Canvas 2D
  (`src/js_canvas.c`), WebGL (`src/webgl.c`), WASM, WebCrypto, font metrics,
  `Intl`/timezone, `navigator.*`. Each is a fingerprinting surface. Routing the
  bytes through Tor does nothing about a canvas hash or a WebGL renderer string.
- **Anti-correlation across sites** unless you add stream isolation (§6).
- **Disk-forensics resistance** (history, IndexedDB-over-SQLite, cache persist
  to the profile unless you run ephemeral).

A clean-room engine is actually an *advantage* for the anti-fingerprinting
endgame — smaller, controllable API surface — but reaching parity is a large,
ongoing program (uniform UA, canvas/WebGL readback noise or prompts, UTC +
`en-US` locale spoofing, letterboxed window sizes, disabled/normalized sensor
APIs). Treat that as a *separate roadmap* (Tier 3 below), not a checkbox.

---

## 3. Where Tor sits in the architecture

The renderer (sandboxed) is the process that runs libcurl and fetches. So the
SOCKS connection originates **inside** the sandbox; the Tor daemon runs
**outside** it.

```
┌─────────────┐   spawn/supervise    ┌───────────────────────────┐
│  GTK/Qt     │─────────────────────▶│  Tor transport (helper)   │
│  shell      │   (like audio helper) │  arti proxy  OR  system   │
│ (thin,      │                       │  tor → SOCKS 127.0.0.1:p  │
│  no parse)  │                       └─────────────┬─────────────┘
└──────┬──────┘                                     │ SOCKS5h
       │ spawn per tab                              │ (remote DNS)
       ▼                                            ▼
┌─────────────────────────────┐            ┌────────────────┐
│  nordstjernen-renderer      │  connect() │   Tor network  │
│  (engine + libcurl)         │───────────▶│  + .onion      │
│  seccomp + Landlock         │  only to   └────────────────┘
│  + (Tor mode) NET jail:     │  port p
│  connect TCP allowed → p    │
└─────────────────────────────┘
```

Key property: with a Landlock **network** jail (kernel 6.7+, ABI v4) the
renderer can `connect()` *only* to the Tor SOCKS port. A compromised or buggy
renderer cannot open a direct `:443` to deanonymize the user — the kernel
refuses it. That is a structural guarantee most browsers' Tor modes can't make.

Caveat: `LANDLOCK_ACCESS_NET_CONNECT_TCP` is **port-scoped, not address-scoped**
— it restricts to a port set, not to `127.0.0.1`. Pin the Tor helper to loopback
and pick a non-standard port; combine with the existing seccomp filter. On
kernels < 6.7 the net jail silently degrades (the code already version-detects
the Landlock ABI at `src/security.c:227`), so Tor mode still works, just without
the extra kernel backstop — fall back to "proxy + no-direct-DNS" enforcement.

---

## 4. Choosing the Tor transport

| Option | Fit | Verdict |
|---|---|---|
| **System `tor`** (user-run) | Works **today** via `--proxy=socks5h://127.0.0.1:9050`. Zero bundling. | Keep as the baseline / power-user path. Poor first-run UX (user must install + run tor). |
| **Bundled Arti `arti proxy` (subprocess)** | Single static Rust binary; runs as a SOCKS proxy; modern, maintained; client + onion-service support matured by 2026 (HTTP-CONNECT added in 2.2.0). Spawned/supervised like the audio helper. | **Recommended** for integrated Tor mode. Optional, out-of-tree binary (Rust toolchain only needed to build the helper, never the C core) — same "large optional dep, not vendored" stance as wgpu-native. |
| **Embed `arti-client` (library)** | Tightest control (in-proc circuits, programmatic NEWNYM). | Pulls Rust into the link; and you'd be embedding a network client inside (or beside) the sandbox. Architecturally heavier than the subprocess. Defer. |
| **Bundled C-tor** (Tor Browser's classic path) | Proven, but C-tor + libevent + openssl is the *old* stack; the Tor Project is steering to Arti. | Only if a target platform lacks Arti support. Not the default choice in 2026. |

**Why subprocess over embedding:** Nordstjernen is already a multi-process
supervisor (shell ↔ renderers ↔ audio helper) and deliberately keeps untrusted
/ heavy components out of the core. A Tor helper as its own process keeps the
Tor state dir, network access, and (large) dependency entirely outside the
sandboxed engine, and lets the renderer stay locked behind the SOCKS port.

Wire it as a `meson` feature `tor` = `auto`/`enabled`/`disabled`, mirroring
`webgpu`: present when the helper binary is found, silently absent otherwise, so
a stock build stays C-only and Tor-free.

---

## 5. Leak inventory & fail-closed egress — the actual work

A Tor client is only as private as its leakiest socket. The egress sweep found
that **not every connection honors the NS proxy config**:

| Egress | Proxy applied? | Evidence | Action |
|---|---|---|---|
| Page/subresource fetch | ✅ yes | `src/net.c:4007` | the chokepoint — already correct |
| WebSocket | ✅ yes | `src/ws.c:438,941` now also apply `ns_net_apply_curl_proxy` | fixed in 1.0.17-dev |
| EventSource/SSE | ✅ yes | `src/eventsource.c:325` now applies `ns_net_apply_curl_proxy` | fixed in 1.0.17-dev |
| Audio/video stream fetch | ✅ yes | helper is spawned with the proxy + CA in its environment (`src/gtk/procview.c`); the unsandboxed helper's libcurl honors it | fixed in 1.0.17-dev |
| AI model download (HuggingFace) | ✅ yes | `src/ai.c:376` | proxied, but multi-GB over Tor is impractical — **disable model download in Tor mode** |
| AI web search (DuckDuckGo) | ✅ yes | `src/ai.c:583` (`html.duckduckgo.com`) | proxied; consider gating background AI calls in Tor mode |
| AI image search (Wikipedia) | ✅ yes | `src/ai.c:583` (`<lang>.wikipedia.org`) | proxied; reveals UI language — gate in Tor mode |

As of 1.0.17-dev the WebSocket, SSE, and audio-helper paths apply the configured
proxy (the helper via its spawn environment), and `--proxy=` is propagated to
every renderer through `NS_HTTP_PROXY`/`NS_HTTPS_PROXY` (which `config.c` loads in
each process). So `docs/Proxy.md`'s "tunneled through the same proxy" now holds
for `--proxy=` and config-file proxies, not only for inherited lowercase env.

**Make egress fail-closed when Tor mode is on:**
1. **One enforcement point.** Have `ns_net_apply_curl_proxy` (and the ws/es/audio
   paths) consult a global `tor_mode` flag. In Tor mode: force the proxy to the
   Tor SOCKS endpoint, force `socks5h` (remote DNS), and **refuse the request if
   no proxy is set** rather than fetching directly. No silent fallback.
2. **Kill direct DNS.** `socks5h` already resolves at the proxy; additionally
   ensure nothing calls `getaddrinfo`/prefetch directly (the prefetch path at
   `src/js.c:37129` reuses the main fetch — good; audit for any other resolver
   use).
3. **Landlock net jail** (kernel ≥ 6.7): extend `landlock_create_ruleset` at
   `src/security.c:282` to also handle `LANDLOCK_ACCESS_NET_CONNECT_TCP` and add
   a single rule allowing the Tor SOCKS port. Now the renderer *cannot* connect
   anywhere else. (Today `handled_access_fs` is the only handled class — network
   is unrestricted, which is why direct connects are even possible.)
4. **Propagate the setting to renderers.** *Done in 1.0.17-dev.* `--proxy=` still
   sets `g_proxy_override` in the *shell* (`appmain.c`) and now also exports
   `NS_HTTP_PROXY`/`NS_HTTPS_PROXY`, which `config.c` loads in every per-tab
   renderer — so the renderer that actually does the fetching is pointed at the
   proxy too, not just the shell. (Verified: with `--proxy` set, WebSocket/SSE
   egress from a renderer process routes through the proxy.)
5. **WebRTC:** none exists (it's a documented non-goal), so the classic Tor
   IP-leak vector is absent by construction. Worth stating as a plus.

---

## 6. Stream isolation & "New Identity"

**Per-site circuits.** Tor Browser sets the SOCKS *username* per first-party
domain; tor's `IsolateSOCKSAuth` keeps streams with different SOCKS auth on
different circuits (formalized by the `<torS0X>` SOCKS-username magic). In
libcurl this is just `CURLOPT_PROXYUSERNAME` / `CURLOPT_PROXYPASSWORD` per
handle. The engine already computes a top-site partition key for cookies/cache
(`src/net.c:~3924`) — **reuse it** as the SOCKS username so circuit isolation
lines up exactly with the storage partition. One key, three uses (cookies,
cache, circuit). Add a per-session random nonce so circuits don't persist across
restarts.

**New Identity / New circuit for this site.** Two mechanisms:
- *Cheap:* rotate the isolation nonce → subsequent streams get fresh circuits.
- *Full:* combine nonce rotation with clearing cookies/storage/cache and the
  libcurl connection pool, and respawn the tab's renderer. Nordstjernen's
  **per-tab renderer process maps naturally onto "New Identity"** — kill the
  renderer, drop its profile slice, restart with a new isolation token.

(With a control channel — C-tor `ControlPort` `NEWNYM`, or Arti's RPC — you can
also force-rotate all circuits, but the SOCKS-auth approach needs no control
port and is implementation-agnostic.)

---

## 7. `.onion` handling

- **Resolution:** with `socks5h`, the `.onion` hostname is handed to the Tor
  transport, which resolves it natively. **Never** send `.onion` to the system
  resolver (RFC 7686 — a legacy DNS lookup *is itself* the deanonymizing leak).
  libcurl is already `.onion`-aware; the Landlock net jail + `socks5h` make a
  stray lookup impossible anyway.
- **Secure context:** an `http://…onion` site is transport-encrypted and
  key-authenticated by the onion protocol. Add `.onion` to Nordstjernen's
  *potentially-trustworthy* set so secure-context-gated APIs (SubtleCrypto,
  service workers, etc. in `src/security.c` / the origin checks in `src/js.c`)
  work on onion sites without HTTPS — this is what Tor Browser does, and the W3C
  secure-contexts spec permits the UA to do it.
- **Origin/cookie scoping:** `.onion` is not in libpsl's public-suffix list, so
  registrable-domain logic (`psl_registrable_domain`, used for cookie scoping at
  `src/net.c`) must special-case `.onion` (treat the full
  `<v3-addr>.onion` as its own registrable domain) — otherwise cookie/partition
  scoping misbehaves on onion sites.
- **Scheme allow-list:** the existing http/https-only allow-listing already
  covers `http(s)://…onion`; just make sure `.onion` isn't rejected as an
  "invalid TLD" anywhere in URL validation.

---

## 8. UX & configuration

- **Entry points:** a `--tor` launch flag and a Settings toggle ("Connect
  through Tor"). Optionally a dedicated **Tor window** (per-window mode, like
  Brave) so a user can mix a normal and a Tor tab — though per-window Tor is a
  known footgun (state bleed); a whole-app Tor mode is safer to get right first.
- **Status surface:** bootstrap progress ("Connecting to Tor… 45%"), a
  connected indicator, the **exit relay / circuit for this site**, and an onion
  indicator in the address bar. Source the bootstrap % from the helper
  (Arti/tor log or control channel).
- **Actions:** "New identity" and "New circuit for this site" (§6).
- **Honest banner:** first-run note that this is Tor *transport*, not Tor Browser
  anonymity (link to the threat-model doc).
- **Reuse config precedence** already in `docs/Proxy.md` (flag > env > config >
  curl autodetect); Tor mode just forces the proxy to the managed endpoint and
  flips fail-closed on.

---

## 9. Build & packaging

- `meson` feature `tor` = `auto` (build the integration when the Arti helper is
  present), `enabled` (hard-require), `disabled` (never) — mirrors `webgpu`.
- The Arti/tor helper is an **optional, out-of-tree** binary, **not vendored**
  (Rust toolchain stays out of the C core's build). Ship it in the nightly
  bundles for platforms that opt in; a stock `meson setup` on a clean machine
  still produces a Tor-free, C-only binary.
- Platform notes: Landlock net jail is Linux-only and kernel-gated (degrades
  gracefully); macOS/Windows get proxy + fail-closed enforcement without the
  kernel backstop. The audio helper already abstracts per-OS process spawning to
  copy from.

---

## 10. Tiered implementation plan

**Tier 0 — already shipped.** `--proxy=socks5h://127.0.0.1:9050` with system
tor. Document the limits (done in `docs/Proxy.md`).

**Tier 1 — leak-safe Tor mode (MVP, the bulk of the value):**
1. Global `tor_mode` flag, plumbed to every renderer.
2. Fix the proxy-skipping egress: WebSocket (`src/ws.c:423,911`), EventSource
   (`src/eventsource.c:304`), audio helper (`src/audio/main.c:547`).
3. Fail-closed: in Tor mode force the managed SOCKS endpoint + `socks5h`, refuse
   on no-proxy, gate AI background fetches and disable model download.
4. Landlock `CONNECT_TCP` net jail to the SOCKS port (`src/security.c:282`),
   kernel-gated.
5. Manage the transport: spawn/supervise system tor or the Arti helper; surface
   bootstrap/connected status.
6. Fix the `docs/Proxy.md` WebSocket claim.

**Tier 2 — anti-correlation:**
7. Per-first-party stream isolation via SOCKS auth (reuse the partition key).
8. "New identity" / "New circuit" (renderer respawn + nonce rotation + state
   clear).
9. `.onion`: secure-context treatment + libpsl special-casing.
10. Ephemeral profile option (no history/IndexedDB/cache persistence).

**Tier 3 — anonymity (separate, long-horizon roadmap):**
11. Uniform user-agent + `navigator` normalization.
12. Canvas/WebGL fingerprint defenses (readback noise or per-site prompt; note
    WebGL is already off-by-default/trust-gated — extend that posture).
13. UTC + `en-US` locale/timezone spoofing in `Intl`/`Date`.
14. Letterboxed window dimensions; resist precise timing
    (`performance.now()` coarsening).
15. Per-feature parity audit against the Tor Browser design doc.

Ship Tier 1 and call it **"Tor mode (transport)"**; gate the *word "anonymous"*
behind Tier 3.

---

## 11. Prior art & pitfalls

- **Tor Browser** = Firefox + tor + a large anti-fingerprinting patchset
  (RFP/letterboxing/locale spoofing). The transport is the easy 10%; the
  fingerprinting hardening is the 90%. Don't imply parity without it.
- **Brave "Private window with Tor"** bundles tor, is per-window, and has shipped
  several proxy-/DNS-bypass leak CVEs — exactly the egress-leak class in §5.
  Per-window modes are leak-prone; fail-closed + the kernel net jail is the
  antidote.
- **`socks5` vs `socks5h`:** plain `socks5` resolves DNS locally → leak. Force
  the `h`.
- **State persistence** across "new identity" is the usual unlinkability bug —
  cookies, IndexedDB-over-SQLite, HSTS supercookies (`src/net.c` HSTS cache!),
  connection pool, and DNS cache all need clearing. The shared `CURLSH` handle
  and HSTS store are easy to forget.
- **Background requests** the user didn't initiate (the AI start page's
  HuggingFace/Wikipedia/DuckDuckGo calls) are the subtle ones — they're proxied
  today, but in Tor mode they should be gated, not just tunneled.

---

## Sources

- Arti (Rust Tor), production status & embedding —
  https://blog.torproject.org/arti_100_released/ ,
  https://blog.torproject.org/arti_1_7_0_released/ ,
  https://tpo.pages.torproject.net/core/doc/rust/arti_client/ ,
  https://crates.io/crates/arti
- Tor SOCKS extensions / stream isolation —
  https://spec.torproject.org/socks-extensions.html ,
  https://spec.torproject.org/path-spec/stream-isolation.html ,
  https://www.whonix.org/wiki/Stream_Isolation
- Landlock network access (ABI v4, Linux 6.7) —
  https://docs.kernel.org/userspace-api/landlock.html ,
  https://www.phoronix.com/news/Landlock-Networking-Linux-6.7
- `.onion` special-use + secure context —
  https://www.rfc-editor.org/rfc/rfc7686.html ,
  https://www.w3.org/TR/secure-contexts/ ,
  https://daniel.haxx.se/blog/2024/05/17/curl-tor-dot-onion-and-socks/
- In-tree: `docs/Proxy.md`, `src/net.c`, `src/ws.c`, `src/eventsource.c`,
  `src/audio/main.c`, `src/ai.c`, `src/security.c`, `src/config.c`,
  `src/gtk/appmain.c`.
