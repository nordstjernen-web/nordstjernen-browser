# Research: a compile broker for web-content AOT

This continues `docs/aot-in-renderer.md`. Tier A (build-time AOT of trusted
JS) needs no compiler at runtime. **Tier B** — accelerating *untrusted web
page* JavaScript — does: the code is only known while the page runs, so
something must compile it live. The renderer cannot (`execve` is absent from
its seccomp allow-list, `src/security.c`), so compilation must happen in a
separate **broker** process and the result must be shipped back and executed
read-only.

This study answers, concretely and with the tree as it stands: can the
existing plumbing carry it, how would the code actually be *loaded* (the hard
part), and what it costs. The honest conclusion is unchanged from the
in-renderer study but now sharper: **Tier B is buildable, but the loading
path is real engineering and it deletes the single best security property the
current design has — that the renderer maps zero web-derived executable
memory. It should not ship by default; at most an experimental flag.**

## 1. The transport already exists

Everything the request/response needs is already in the tree:

- The renderer and shell talk over an `AF_UNIX`/`SOCK_STREAM` `socketpair`,
  the renderer on fd 3 (`src/rproc_http.c:135,145-147`,
  `src/renderer_http.c:155-157`).
- The wire format is HTTP/1.1 + JSON, dispatched by a flat `strcmp(path,…)`
  chain (`src/renderer_serve.c:191-657`). A new `/aot-compile` verb drops in
  before the final 404 with no architectural change.
- **Descriptor passing is already used.** `http_send_fd` / `http_recv_fd`
  (`src/ipc_http.c:70-125`) pass a descriptor with `SCM_RIGHTS`, and the
  framebuffer **memfd** is handed shell→renderer exactly this way at startup
  (`src/rproc_http.c:159-160` → `src/renderer_http.c:159-165`, mapped
  `MAP_SHARED`). Returning a compiled-code memfd is the same move.
- The shell already spawns and supervises helper processes (the audio helper,
  `g_subprocess_new`, `src/gtk/procview.c:514-544`); a broker helper follows
  the same pattern.

So the IPC is a solved problem. The difficulty is entirely in *what* gets
compiled, *who* compiles it, and *how the renderer runs the result*.

## 2. The trust boundary: send bytecode, not C

Two shapes are possible:

- **Renderer emits C, broker runs `cc`.** The renderer can run `aotc`'s
  analysis and C-emission (pure computation, allowed in-sandbox). But then a
  *compromised* renderer hands arbitrary C to a less-sandboxed `cc` — a clean
  arbitrary-native-code primitive. Rejected.
- **Renderer emits bytecode, broker runs `aotc` + `cc`.** A compromised
  renderer can submit only QuickJS bytecode; `aotc`'s sound, constrained
  codegen bounds the C to the numeric vocabulary, and `cc` never sees
  attacker-chosen C. This is the right boundary — at the cost of putting
  `aotc` (another untrusted-input parser) in the broker too.

Either way `cc` runs on input derived from web content. The mitigation is to
**sandbox the broker hard**: seccomp around the `aotc`+`cc` invocation,
Landlock confining it to one scratch directory, `rlimit` on CPU/memory/output
size, and a wall-clock timeout that kills a runaway `cc`. The broker produces
exactly one artifact — a code blob in a memfd — and nothing else.

## 3. Loading the result — the actual hard part

This is where the earlier one-line sketch ("renderer mmaps it
`PROT_READ|PROT_EXEC`") was too glib. Three obstacles, each verified against
the tree or this kernel:

**(a) No dynamic linker after sandboxing.** `dlopen` is not used anywhere in
the renderer and libdl is not linked post-sandbox; the only dynamic loading in
the codebase (the spellcheck plugin) happens *before* the sandbox is sealed
(`src/renderer_http.c:174-178`, `ns_browser_init` then `ns_browser_sandbox`).
So a returned `.so` cannot be `dlopen`ed. Code must be mapped and called
manually.

**(b) `memfd` + `PROT_EXEC` is the vehicle, and it works.** A memfd is an
anonymous inode with no filesystem path, so it sidesteps the Landlock problem
that a file would hit (Landlock grants `LANDLOCK_ACCESS_FS_EXECUTE` only on
read-only system paths like `/usr`, never on any writable path —
`src/security.c:303-304` vs the writable dirs at `325-356`). I verified the
core move on this kernel: create a memfd, write machine code, `mmap` it
`PROT_READ|PROT_EXEC` (`MAP_PRIVATE`), and call it — succeeds, and the page is
**never** simultaneously writable and executable, so the no-W^X behaviour is
preserved. Seccomp already allows `mmap`/`memfd_create`/`mprotect`
(`src/security.c:546,554,556`). *Caveat:* Landlock itself could not be
exercised here — the syscall returns `ENOSYS` in this container — so whether a
deployment kernel's Landlock mediates `PROT_EXEC` mmap of a *pathless* memfd
must be confirmed on real hardware. By Landlock's path-based design it should
not, but this is the one feasibility item left unproven.

**(c) Relocations — the blob is not self-contained.** `aotc`'s output calls
external symbols: its own helpers (`js_to_int32` ×12, `js_pow`, `js_clz32`,
`js_round`, `js_min2/max2`, … in a representative registry) *and* libm
(`pow`, `cos`, `fmod`, `sqrt`, `hypot`, …). A flat blob `mmap`ped at a runtime
address cannot resolve those. Two ways out:

  - **Vtable ABI (recommended).** Change `aotc` so every external call goes
    through a function-pointer table passed as a hidden argument —
    `entry(const double *args, const JSMathVT *vt)` with `vt->pow(...)` etc.
    The `js_*` helpers are emitted inside the blob (intra-blob PC-relative
    calls under `-fPIC`), so the only externals are the vtable, and there are
    **zero runtime relocations**. The blob is then `cc -fPIC -nostdlib`,
    `objcopy -O binary` to flat code, mapped RX, and called with the
    renderer's own libm pointers. Position-independent, dlopen-free.
  - **Minimal in-renderer ELF loader.** Map a relocatable `.so`'s segments and
    apply relocations by hand against a fixed, renderer-controlled symbol set
    (libm + the helpers). More flexible (handles data, many reloc types) but
    ~200 lines of security-sensitive loader parsing broker output.

The vtable route is smaller and keeps the attack surface tighter, so it is the
one to pursue if Tier B is ever built. It is also a non-trivial `aotc` change
(today it emits a standalone TU; it would need a blob/vtable emission mode in
addition to the `--registry` mode).

## 4. The cost that actually decides it

**It deletes the renderer's strongest property.** Today the renderer maps
**zero** web-derived executable pages: QuickJS interprets, and WAMR is
compiled interpreter-only (`WASM_ENABLE_INTERP=1`, `WASM_ENABLE_FAST_INTERP=0`,
no JIT/AOT — `src/wamr/meson.build:9-10`), so there is no existing
executable-memory precedent to lean on. Tier B introduces the first
attacker-influenced RX memory in the renderer. Even read-only and even with
`aotc` bounding the instruction vocabulary, that is a qualitative change to the
threat model — the property that distinguishes this browser from every
JIT-based one.

**The TCB grows by a C toolchain.** `aotc` is hardened (ASan-clean over 14 MB
of Speedometer), but `cc` is enormous; a codegen or compiler bug on
web-derived input becomes native code in the renderer. A JIT — the thing AOT
was chosen *over* — at least keeps a small fixed code generator instead of a
general-purpose compiler.

**Denial of service.** A hostile page can mint thousands of distinct hot
functions to trigger thousands of `cc` forks. Needs a hotness threshold, a
hard cap on concurrent/total compiles per origin, a content-hash cache to
dedupe, and compile timeouts — all of which the broker must enforce.

**Cache poisoning / persistence.** Any on-disk blob cache is executable code
derived from web content; it must be memfd-only or, if persisted, hash-named,
integrity-checked, and Landlock-scoped — another moving part.

**The benefit is narrow.** The whole-corpus measurement stands: **0 of 2154**
Speedometer top-level functions are numeric-eligible (`docs/aot-js-compiler.md`).
Everyday pages are DOM/object/string-bound and gain nothing. The win is
real only on compute-heavy pages — games, crypto, codecs, image/audio, and
the framework numeric kernels in `tests/framework.js` — i.e. exactly the
workloads that are rare in normal browsing and that a user could often get
from WebAssembly instead.

## 5. End-to-end flow, if built

1. The interpreter counts calls on `JSFunctionBytecode` (the
   `NS_AOT_DISPATCH` field already added); past a threshold a function is a
   candidate.
2. The renderer serializes that function's bytecode and POSTs `/aot-compile`
   over fd 3.
3. The broker (tightly sandboxed: seccomp + Landlock-to-scratch + rlimits +
   timeout) runs `aotc` (decline ⇒ empty reply, stay interpreted) then `cc`
   `-fPIC -nostdlib`, `objcopy -O binary` into a memfd.
4. The broker returns the memfd via `SCM_RIGHTS` (the framebuffer mechanism).
5. The renderer `mmap`s it `PROT_READ|PROT_EXEC` and registers it with
   `JS_SetFunctionAOT` (already implemented). The function ran interpreted the
   whole time; it now runs native, with the same numeric-args type-guard
   bailout, so behaviour is identical.

Every piece exists or is a bounded addition except the blob/vtable emission in
`aotc` and the broker process itself.

## 6. Recommendation

Do not build Tier B into the default browser. The benefit is confined to
compute-heavy pages that are uncommon and often better served by WebAssembly,
while the cost is the renderer's best security property (no web-derived
executable memory) plus a C toolchain in the TCB and a DoS surface. If it is
ever pursued for a specific compute-heavy use case, the only defensible shape
is: **send bytecode (not C); run `aotc`+`cc` in a hard-sandboxed broker;
return a flat, vtable-ABI, relocation-free blob in a memfd; map it RX (never
W^X); gate the whole thing behind an explicit experimental flag** like
`--enable-webgpu`, off by default, so a stock build carries none of it. The
in-engine dispatch half of this is already done and verified
(`docs/aot-in-renderer.md` §6); the broker half is the part whose security
cost is not, in this author's reading, worth paying for general browsing.
