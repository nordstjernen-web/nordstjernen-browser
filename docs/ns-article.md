# Innovations in web technology in the Nordstjernen web browser

Nordstjernen ("the North Star") is a web browser written from scratch in
C, built in Norway. It is a clean-room implementation — no Gecko, no
WebKit, no Blink, nothing forked from an existing engine. Where the rest
of the industry has converged on three engines maintained by a handful of
the largest companies on earth, Nordstjernen is small enough that a single
human can read and audit it end to end: roughly 127,000 lines of C plus a
thin C++/Qt shell. This article surveys what the browser does today, how
it is built, the technical innovations that set it apart, and where it is
going.

## Current features and architecture

Nordstjernen is a real browser, not a toy. It targets the HTML and CSS
standards directly — behaviour is measured against the WHATWG/W3C spec
text, section by section, rather than against another browser's quirks.
The reference standards are the
[WHATWG HTML Living Standard](https://html.spec.whatwg.org/),
the [WHATWG DOM Standard](https://dom.spec.whatwg.org/),
the [WHATWG URL Standard](https://url.spec.whatwg.org/),
and the [W3C CSS specifications](https://www.w3.org/TR/CSS/) (the current
[CSS Snapshot](https://www.w3.org/TR/css-2023/) plus the individual
modules — [Cascade](https://www.w3.org/TR/css-cascade-5/),
[Flexbox](https://www.w3.org/TR/css-flexbox-1/),
[Grid](https://www.w3.org/TR/css-grid-2/),
[Transforms](https://www.w3.org/TR/css-transforms-2/)).
As of mid-2026 the section-by-section walk-through of the in-scope HTML
standard records 136 spec rows fully implemented, 27 partial, and 4
absent.

### Small by design

The single most important number in this whole document is the size of the
source. Nordstjernen's engine is **about 120,000 lines of clean-room C**
(roughly 127,000 counting the thin C++/Qt shell). For comparison, the
engines it competes with are cited at **tens of millions** of lines:
Chromium is commonly estimated at around 35 million lines of code, and
Firefox/Gecko at well over 20 million. Nordstjernen is therefore on the
order of **200–300× smaller** than either.

That is not a vanity metric. A codebase one person can read end to end in a
few weeks is a codebase that can actually be *audited*, reasoned about, and
kept honest. Tens of millions of lines cannot be — not by any individual,
and arguably not by any single team. Small is the security property, the
maintainability property, and the independence property all at once: it is
the reason a browser like this can exist outside a giant corporation at
all.

**Engine stack.** The pieces are deliberately small and vendored in-tree
so there are no submodules and no surprise downloads:

- **HTML → DOM, CSS, and URLs** — the [lexbor](https://github.com/lexbor/lexbor)
  parser, forked in-tree and modified for tight integration. The same
  library provides the WHATWG URL implementation, so origins, IDN, and
  parsing all share one code path.
- **JavaScript** — the [QuickJS](https://github.com/quickjs-ng/quickjs)
  interpreter (quickjs-ng fork), with DOM, Shadow DOM, observer APIs,
  Canvas 2D, and WebCrypto wired in. Crucially, **no JIT** (more on that
  below).
- **WebAssembly** — the full JS API (`compile`, `instantiate`, `Memory`,
  `Table`, externref) over a WAMR interpreter; it runs wasm-bindgen
  bundles.
- **Images** — memory-safe decoding through Google's
  [Wuffs](https://github.com/google/wuffs) (PNG, GIF, BMP, JPEG), with
  libwebp for WebP and librsvg for SVG.
- **Networking** — HTTP/2 over libcurl, with HSTS, CSP, and partitioned
  cookies.
- **Cryptography** — `crypto.subtle` implemented directly over OpenSSL's
  EVP APIs: hashing, HMAC, AES, RSA, ECDSA/ECDH, HKDF, and PBKDF2.

**Rendering and the UI shell.** CSS cascade, flexbox, grid, transforms,
gradients, and `@keyframes` are laid out and painted through GTK 4's GSK
renderer (a Qt 6 shell is also shipped). A minimalist presentation-MathML
renderer puts equations inline on the text baseline. WebGL 1/2 is mapped
onto OpenGL ES, opt-in per site and gated behind a trust prompt. An
experimental WebGPU surface layers over external wgpu-native and stays off
unless explicitly enabled.

**Media, kept deliberately tiny.** Exactly one video codec is built in —
MPEG-1, decoded in pure portable C by the vendored single-file
[pl_mpeg](https://github.com/phoboslab/pl_mpeg). An `.mpg`/`.mpeg`/`.m1v`
`<video>` plays inline, honouring `autoplay`/`loop`/`muted`/`poster` and
click-to-play. Audio (MP2 via pl_mpeg, MP3 via minimp3) plays through a
small unsandboxed helper over SDL2. Everything else — other codecs,
streaming `blob:` sources — is handed to an external player. There is no
media stack to maintain or to be exploited.

**Process-per-tab isolation.** Each tab's engine runs in its own
sandboxed `nordstjernen-renderer` process. The GTK/Qt application is a
thin shell that blits the renderer's shared-memory framebuffer and
forwards input over an IPC control channel. A page that crashes — or is
compromised — cannot take down the UI or reach its siblings. An optional
`--single-process` mode trades the sandbox for footprint on low-memory
machines and for debugging.

**A local-only AI start page.** The `about:start` new-tab page is a chat
with a small language model running entirely on the user's machine via
llama.cpp — no cloud, no network at inference time. Models download once,
integrity-checked against a pinned SHA-256 digest, with optional Vulkan or
Metal GPU offload. This is the *only* AI in the product: there are no
AI-style web APIs exposed to pages, by design.

Nordstjernen ships no telemetry of any kind, does not phone home, and does
not run "studies" infrastructure.

## Why C

Writing a new browser in C in 2026 looks contrarian, and it is a deliberate
choice. The benefits the language buys here are concrete:

- **Ubiquity and portability.** A C99/C23 toolchain exists for every
  platform that matters — and several that don't. The same source builds
  with GCC and Clang on Windows, macOS, Linux, Android, FreeBSD, and
  NetBSD. That is a large part of how a tiny project reaches so many
  targets.
- **No runtime, no GC, no hidden machinery.** There is no language runtime
  to ship, no garbage collector pausing the renderer, and no framework
  between the code and the system call. What you read is what runs —
  which is exactly what makes a security audit tractable.
- **Predictable performance and memory.** Layout and paint are
  memory-bandwidth-bound work; C's direct control over allocation, data
  layout, and cache behaviour matters, and there are no surprises from a
  JIT warming up or a GC kicking in.
- **A stable ABI and clean embedding.** The C ABI is the lingua franca of
  software. It is why the engine drops cleanly behind a C embedding API, a
  JNI bridge to the JVM, and a C++/Qt shell without glue layers or
  marshalling runtimes.
- **Longevity and small dependencies.** C code written carefully today will
  still compile in twenty years. The toolchain is everywhere, mature, and
  not going anywhere — no churning ecosystem, no package-manager supply
  chain to vet on every build.
- **Auditability.** Combined with the comment-free, self-explaining house
  style and the ~120,000-line ceiling, plain C keeps the whole engine
  within reach of a single reader.

### Modern C: the language kept evolving

C is not the language it was in 1999. The current standard, **C23**
(ISO/IEC 9899:2024), modernised it considerably, and Nordstjernen builds
against contemporary GCC and Clang that implement it. The improvements that
matter for safe, readable systems code include:

- **`nullptr`** and a real `bool` / `true` / `false` as keywords, removing
  a layer of macro hackery and null-pointer ambiguity.
- **`constexpr`** objects and **`typeof`** / `typeof_unqual`, bringing
  compile-time constants and type introspection into standard C.
- **`_BitInt(N)`** for exact-width integers, and **`<stdckdint.h>`** checked
  integer arithmetic (`ckd_add`/`ckd_sub`/`ckd_mul`) — overflow-safe math
  without hand-rolled guards, which is a real security win for a parser.
- **`[[nodiscard]]`, `[[maybe_unused]]`, `[[fallthrough]]`** and friends:
  standard attributes that let the compiler catch whole classes of mistakes
  the way other languages do.
- **`#embed`** for pulling binary assets straight into the build, binary
  integer literals, `auto` type inference, `enum`s with a fixed underlying
  type, and UTF-8 (`u8`) string improvements.

The takeaway: "written in C" no longer means "written in K&R C." A modern C
codebase gets static-analysis-friendly attributes, checked arithmetic, and
stronger typing while keeping everything that makes C portable, fast, and
auditable.

## Development methodology

Nordstjernen is developed in a way that is itself part of the story.

- **Clean-room and self-auditable.** The guiding constraint is that the
  source stays readable and maintainable by a single human. Every
  dependency is small, vendored, and understood. There are no millions of
  lines of inherited engine code that nobody on the project has read.
- **Spec-first, not browser-first.** Conformance is tracked against the
  spec text section by section, and against the open
  web-platform-tests suite, with a running scoreboard that drives the
  highest-return-on-investment fixes first.
- **The running browser is the test oracle.** The project has no automated
  unit-test suite by deliberate choice. Correctness is verified by
  building and running the browser on the affected path. A change is
  "done" only when it compiles cleanly with both GCC and Clang, the
  browser launches, the affected UI path works, and it is committed.
- **Comment-free, self-explaining code.** House style forbids inline
  comments: each file carries one short header line and the code is made
  to explain itself through naming and decomposition instead.
- **Autonomous, long-running AI sessions.** Much of the work happens in
  uninterrupted agent sessions that default to acting rather than asking —
  diagnose, fix, build, smoke-test, commit, push — with a human steering
  direction rather than approving each step.
- **Cross-platform CI as a sanity net.** Linux, macOS, Windows, Qt,
  Android, and Java workflows run on every push, with CodeQL and Semgrep
  scanning, while the local Linux build remains the primary correctness
  gate.

## Innovations

### Ahead-of-time JavaScript compilation — speed without a JIT

The headline innovation is the **AOT JavaScript compiler**. Every
production browser speeds up JavaScript with a just-in-time compiler that
allocates writable memory at runtime, writes machine code into it, and
flips it executable. That single capability is the substrate for an entire
class of real-world browser exploits — JIT spraying, type-confusion → JIT
— and it forces the sandbox to permit `mprotect(PROT_EXEC)`, weakening it
for everything else.

Nordstjernen moves code generation to **build time**. Its `aotc` tool
reuses the real QuickJS front end to compile JavaScript to bytecode,
proves a function lies in a safe numeric subset, and lowers that bytecode
to C, which a trusted offline compiler turns into native code mapped from
a read-only executable image. The analysis is **sound by construction**:
the compiler emits native code only when it can prove the result is
bit-for-bit identical to interpreting the function, and otherwise declines
so the interpreter handles it. It can never change a program's meaning —
at worst a function simply is not accelerated. A 55-case self-check
confirms AOT and interpreter agree across arithmetic, logic, loops,
recursion, float math, and edge cases, and that out-of-subset programs
fall back correctly.

The payoff: **3.8×–35× faster** than the interpreter on numeric
workloads — `fib(28)` retires 9.2M instructions versus the interpreter's
1.06 billion — **while the renderer never needs writable-executable
memory at all**. The seccomp sandbox keeps denying it. This is a genuinely
different point in the design space: JIT-class speed on the workloads it
can prove safe to accelerate, with none of a JIT's attack surface. The
accelerated subset is numeric today and widening; everything outside it
runs interpreted, exactly as before.

### Security: a landlocked renderer

Security is structural rather than bolted on. Each tab's engine runs as an
unprivileged child process behind an IPC and shared-memory boundary, and
on Linux that process is **landlocked** — confined with both a `seccomp`
system-call filter and the kernel's Landlock LSM — so even a fully
compromised renderer can touch almost nothing: no filesystem it was not
explicitly granted, no sound device (audio is brokered out to a separate
helper), and, thanks to the no-JIT design, no way to create executable
memory. The untrusted page never launches external programs; only the
trusted UI shell does. Combined with no telemetry and no auto-update
pinger, the result is a browser whose threat surface is small enough to
reason about.

### Built clean-room by AI agents

Nordstjernen is, as a practical matter, a browser written largely by
**AI coding agents** — Claude and Codex — working in long autonomous
sessions against a human-defined product vision and a strict set of
operating constraints. It is a concrete demonstration that a from-scratch,
spec-conformant, cross-platform browser engine is now within reach of
agentic development, when paired with disciplined methodology: small
vendored dependencies, a self-auditable codebase, a spec-first conformance
target, and the running browser as the verification oracle. The 127,000
lines are not inherited; they were written, here, deliberately.

### A license that prevents harmful free-riding while allowing commercial use

Nordstjernen ships under the **Nordstjernen Source License v1.0**
(NSL-1.0), inspired by the Functional Source License. The intent is to be
genuinely open and fair while preventing the one outcome that kills
small open projects — a larger company taking the code wholesale and
shipping a competing browser:

- You may **use, copy, modify, create derivative works, publish, and
  distribute** the software for any purpose **except a Competing Use** —
  i.e. offering it as a substitute browser product or service.
- Internal use, non-commercial education and research, and professional
  services for licensees are all explicitly permitted.
- **Commercialization is available** by separate written agreement: a
  commercial license can grant rights beyond the public terms, including
  the right to make a competing product or to redistribute under different
  terms.
- Every release **converts to the MIT license ten years after it is
  published** — an irrevocable grant — so the work always flows into the
  fully-free commons on a fixed schedule.

This combination — open and modifiable for almost everyone, protected
against extractive free-riding, monetizable by agreement, and guaranteed
to become MIT over time — is itself one of the project's deliberate
innovations.

### And several quieter ones

The headline items above are not the whole list. A few smaller innovations
matter in aggregate:

- **A two-layer crash architecture.** A renderer crash is contained to its
  tab; a crash or hang of the thin UI shell itself is caught by an on-by-
  default **watchdog supervisor** that respawns it transparently. The two
  layers compose so neither a hostile page nor a shell bug takes the
  browser down.
- **A shared-memory framebuffer protocol.** The renderer paints into a
  shared-memory buffer that the shell blits, with input forwarded over an
  IPC control channel. The same protocol drives in-process
  (`--single-process`), out-of-process, and even a remote
  `nordstjernen-renderer` over HTTP/JSON — one boundary, many deployments.
- **No-JNI JVM embedding.** On the JVM the engine can be driven either
  through a thin JNI bridge or, for crash isolation, as a separate
  renderer process over the HTTP/JSON protocol, so an engine crash cannot
  take down the host JVM. A standalone Swing browser app rides the same
  binding.
- **Translation without gettext.** UI strings are English-source and
  translated at startup through an in-tree catalogue (`src/i18n.c`,
  `data/i18n/*.lang`) with English fallback — no `.po` tooling, no gettext
  dependency, no build-time codegen.
- **Memory-safe-by-default media paths.** Image bytes go through Wuffs
  (transpiled, memory-safe), the lone video codec is pure-C MPEG-1 needing
  no syscalls beyond memory, and audio is brokered to a separate helper —
  so the most exploited surfaces in other browsers are either memory-safe
  or out of the sandbox entirely.

## David vs Goliath: why the web needs more engines

It is worth stating plainly what Nordstjernen is up against. Essentially
the entire web now renders on **three engines**: Blink (Google), WebKit
(Apple), and Gecko (Mozilla, itself largely funded by Google). Blink and
WebKit share a common ancestor, and Mozilla's market share keeps
shrinking, so in practice the web is increasingly **one engine,
controlled by one advertising company**. When a browser engine is also the
de-facto specification, "standards compliance" quietly becomes "whatever
Chrome does." A single vendor effectively decides what the web *is*.

That monoculture is fragile in three ways:

- **Security**, because a single class of bug — a V8 type confusion, say —
  is simultaneously a flaw in Chrome, Edge, Brave, Opera, Electron, and
  every other Chromium shell.
- **Governance**, because web features that are inconvenient to an ad
  business (or convenient to it) ship or don't ship at one company's
  discretion.
- **Viability**, because the engines are now so enormous — tens of millions
  of lines — that no new entrant can realistically write one, which is
  exactly why there have been no new ones for over a decade.

Nordstjernen is the contrary bet: that a from-scratch engine *can* still be
built by a tiny team, precisely **because** it refuses the bloat — ~127,000
lines instead of tens of millions, no JIT, one video codec, no telemetry,
auditable end to end by one person. The web does not need another Chromium
skin; it needs a genuinely independent engine, and an independent engine is
only credible if it is small enough to actually exist outside a
trillion-dollar company. More alternatives are not a luxury here — they are
the only thing that keeps the web a commons rather than a product.

## Is it more secure than Chrome?

Honestly: **in some structural ways yes, in overall battle-hardening not
yet — and the two answers are not in tension.**

Where Nordstjernen has the stronger *design*:

- **No JIT, ever.** The renderer never needs writable-executable memory, so
  the single most exploited capability in modern browsers — runtime code
  generation — is simply absent. The seccomp filter can keep denying
  `mprotect(PROT_EXEC)` for every code path.
- **A far smaller attack surface.** ~127,000 auditable lines versus tens of
  millions; one media codec instead of a full media stack; memory-safe
  image decoding via Wuffs.
- **Defence in depth on Linux.** seccomp *and* Landlock confine each
  renderer; audio is brokered out; the page never launches subprocesses.
- **No telemetry, no auto-update channel** to attack or to leak through.

Where Chrome is, candidly, ahead today:

- **Maturity and scale of review.** V8 and Blink are continuously fuzzed by
  a large dedicated security team, with a multi-million-dollar bug-bounty
  program and the fastest patch-and-ship pipeline in the industry.
- **Site Isolation and the V8 sandbox.** Chrome's process-per-site-origin
  model and V8's heap-isolation sandbox are mature mitigations refined over
  years of real attacks.
- **Exposure as proof.** Chrome is attacked constantly, so its defences are
  tested constantly; Nordstjernen's simply have not faced the same fire.

The fair conclusion: Nordstjernen's *threat model is smaller and easier to
reason about*, and it structurally eliminates the exploit class that
produces most in-the-wild browser zero-days. That is a real and durable
advantage. But "smaller attack surface" is not the same as "more
hardened," and the project does not claim to have out-engineered Google's
security organisation. It claims something narrower and more defensible:
**there is much less to attack, and the worst foothold — a JIT — is not
there at all.**

### The exploit class Nordstjernen designs away: JIT CVEs in 2026

The argument is not abstract. Chrome shipped a string of V8 zero-days in
2026, several of them exploited in the wild before a patch existed, and the
JIT compiler is squarely in the firing line:

- **CVE-2026-3910** — a **type confusion in V8's Maglev JIT compiler**,
  specifically its Phi-untagging pass: once a JIT-compiled function runs,
  the type confusion yields out-of-bounds read/write in the renderer.
  Exploited in the wild before the fix in Chrome 146, and added to CISA's
  Known Exploited Vulnerabilities catalog in March 2026. This is exactly
  the bug shape AOT removes — there is no Maglev, no runtime-JITed
  function, and no writable-executable page to confuse.
- **CVE-2026-11645** — out-of-bounds memory access in V8 (CVSS 8.8), the
  fifth Chrome zero-day of 2026, with an exploit acknowledged in the wild.
- **CVE-2026-6363** — V8 type confusion allowing memory corruption and code
  execution, fixed in Chrome 147.
- **CVE-2026-2441, CVE-2026-3909, CVE-2026-5281** — the remaining 2026
  Chrome zero-days exploited in the wild, several in V8.

Every one of these lives in the JIT/optimising-compiler machinery or the
V8 type system that the JIT exists to exploit. A browser with no JIT does
not get to be *invulnerable* — but it does get to be **immune to this
entire category**, which in 2026 was the category doing the most damage.

## Future work and the path to world dominance

The near-term roadmap follows the spec scoreboard: close the remaining
partial and absent HTML rows, raise the web-platform-tests score on the
highest-return areas, and broaden CSS and forms coverage until typical
sites render indistinguishably from the incumbents. The AOT compiler's
proven-safe subset will widen beyond pure numerics toward strings, arrays,
and object shapes, narrowing the remaining gap to a JIT without ever
reintroducing one.

Reach is the other axis. Nordstjernen already runs on Windows, macOS,
Linux, Android, FreeBSD, NetBSD, and the JVM, with both GTK and Qt shells
and a C and Java embedding API. The strategy for broad adoption is the
opposite of the incumbents': instead of an ever-larger engine that only a
megacorporation can maintain, Nordstjernen offers a browser small enough
to *understand* — auditable, telemetry-free, JIT-free, and structurally
sandboxed — as the rational default for anyone who actually cares what
their browser is doing. Embeddability turns the engine into a building
block for other software; the local-AI start page shows how assistance can
be added without surrendering privacy; and the ten-year MIT conversion
guarantees the work outlives any single steward.

The North Star is fixed: a fast, secure, minimal, self-auditable web
browser that one person can hold in their head — and that, multiplied
across everyone who wants exactly that, is the path to the top.
