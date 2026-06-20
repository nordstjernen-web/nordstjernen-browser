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
As of mid-2026 the section-by-section walk-through of the in-scope HTML
standard records 136 spec rows fully implemented, 27 partial, and 4
absent.

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
different point in the design space: most of the performance of a JIT,
with none of its attack surface.

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
